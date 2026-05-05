#include "local_preview_service.h"

#include "application.h"
#include "boards/common/board.h"
#include "boards/common/esp32_camera.h"
#include "display/lcd_display.h"
#include "preview_frame.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace xiaozhi {

static const char *TAG = "LocalPreview";

// ============================================================================
// 单例实现
// ============================================================================

LocalPreviewService &LocalPreviewService::GetInstance() {
  static LocalPreviewService instance;
  return instance;
}

LocalPreviewService::LocalPreviewService()
    : active_(false), capture_task_(nullptr), display_task_(nullptr),
      frame_queue_(nullptr) {
  ESP_LOGI(TAG, "本地预览服务已创建");
}

LocalPreviewService::~LocalPreviewService() {
  Stop();
  ESP_LOGI(TAG, "本地预览服务已销毁");
}

// ============================================================================
// 公共接口实现
// ============================================================================

bool LocalPreviewService::Start() {
  if (active_) {
    ESP_LOGW(TAG, "本地预览已在运行中");
    return true;
  }

  ESP_LOGI(TAG, "正在启动本地预览...");

  // 1. 检查摄像头可用性
  auto *camera = Board::GetInstance().GetCamera();
  auto *esp32_camera = dynamic_cast<Esp32Camera *>(camera);
  if (!esp32_camera || !esp32_camera->IsAvailable()) {
    ESP_LOGE(TAG, "摄像头不可用");
    return false;
  }

  // 2. 检查与监控模式的互斥
  Application &app = Application::GetInstance();
  if (app.IsMonitorMode()) {
    ESP_LOGE(TAG, "监控模式运行中，无法启动本地预览");
    return false;
  }

  // 3. 创建 FreeRTOS 队列（深度 1）
  frame_queue_ = xQueueCreate(kQueueDepth, sizeof(PreviewFrame *));
  if (frame_queue_ == nullptr) {
    ESP_LOGE(TAG, "无法创建帧队列");
    return false;
  }

  // 4. 创建 Capture Task
  BaseType_t ret =
      xTaskCreate(CaptureTaskEntry, "preview_capture", kTaskStackSize, this,
                  kTaskPriority, &capture_task_);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "无法创建捕获任务");
    vQueueDelete(frame_queue_);
    frame_queue_ = nullptr;
    return false;
  }

  // 5. 创建 Display Task
  ret = xTaskCreate(DisplayTaskEntry, "preview_display", kTaskStackSize, this,
                    kTaskPriority, &display_task_);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "无法创建显示任务");
    // 清理已创建的资源
    active_ = false;
    vTaskDelete(capture_task_);
    capture_task_ = nullptr;
    vQueueDelete(frame_queue_);
    frame_queue_ = nullptr;
    return false;
  }

  // 6. 切换 LCD 到预览模式
  auto *display = Board::GetInstance().GetDisplay();
  auto *lcd_display = dynamic_cast<LcdDisplay *>(display);
  if (lcd_display && !lcd_display->EnterPreviewMode()) {
    ESP_LOGW(TAG, "无法进入预览模式，但继续运行");
  }

  active_ = true;
  ESP_LOGI(TAG, "本地预览已启动");
  return true;
}

void LocalPreviewService::Stop() {
  if (!active_) {
    return;
  }

  ESP_LOGI(TAG, "正在停止本地预览...");

  // 1. 设置停止标志
  active_ = false;

  // 2. 等待任务退出
  if (capture_task_ != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(200));
    capture_task_ = nullptr;
  }

  if (display_task_ != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(200));
    display_task_ = nullptr;
  }

  // 3. 清空队列并释放所有帧内存
  if (frame_queue_ != nullptr) {
    PreviewFrame *frame = nullptr;
    while (xQueueReceive(frame_queue_, &frame, 0) == pdTRUE) {
      delete frame; // 析构函数会释放 PSRAM
    }
    vQueueDelete(frame_queue_);
    frame_queue_ = nullptr;
  }

  // 4. 恢复 LCD 到正常模式
  auto *display = Board::GetInstance().GetDisplay();
  auto *lcd_display = dynamic_cast<LcdDisplay *>(display);
  if (lcd_display) {
    lcd_display->ExitPreviewMode();
  }

  ESP_LOGI(TAG, "本地预览已停止");
}

bool LocalPreviewService::IsActive() const { return active_; }

// ============================================================================
// 任务入口函数（静态）
// ============================================================================

void LocalPreviewService::CaptureTaskEntry(void *param) {
  auto *service = static_cast<LocalPreviewService *>(param);
  ESP_LOGI(TAG, "捕获任务已启动");
  service->CaptureLoop();
  ESP_LOGI(TAG, "捕获任务已退出");
  vTaskDelete(nullptr);
}

void LocalPreviewService::DisplayTaskEntry(void *param) {
  auto *service = static_cast<LocalPreviewService *>(param);
  ESP_LOGI(TAG, "显示任务已启动");
  service->DisplayLoop();
  ESP_LOGI(TAG, "显示任务已退出");
  vTaskDelete(nullptr);
}

// ============================================================================
// 任务循环逻辑
// ============================================================================

void LocalPreviewService::CaptureLoop() {
  int consecutive_failures = 0;
  TickType_t last_capture_time = xTaskGetTickCount();

  while (active_) {
    // 计算距离上次捕获的时间
    TickType_t now = xTaskGetTickCount();
    TickType_t elapsed = now - last_capture_time;
    TickType_t frame_interval_ticks = pdMS_TO_TICKS(kFrameIntervalMs);

    // 如果还没到下一帧的时间，等待
    if (elapsed < frame_interval_ticks) {
      vTaskDelay(frame_interval_ticks - elapsed);
    }

    last_capture_time = xTaskGetTickCount();

    // 获取摄像头
    auto *camera = Board::GetInstance().GetCamera();
    auto *esp32_camera = dynamic_cast<Esp32Camera *>(camera);

    // 捕获帧
    if (!esp32_camera || !esp32_camera->CaptureForPreview()) {
      consecutive_failures++;
      ESP_LOGW(TAG, "捕获失败 (%d/%d)", consecutive_failures,
               kMaxConsecutiveFailures);

      // 连续失败过多，停止服务
      if (consecutive_failures >= kMaxConsecutiveFailures) {
        ESP_LOGE(TAG, "连续捕获失败次数过多，停止预览");
        Application::GetInstance().Schedule([this]() {
          Stop();
          Application::GetInstance().Alert("错误", "摄像头异常", "error",
                                           "error");
        });
        break;
      }

      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // 重置失败计数
    consecutive_failures = 0;

    // 获取帧数据
    const uint8_t *rgb565_data = esp32_camera->GetRgb565Data();
    size_t data_size = esp32_camera->GetRgb565DataSize();
    uint16_t width = esp32_camera->GetFrameWidth();
    uint16_t height = esp32_camera->GetFrameHeight();

    if (rgb565_data == nullptr || data_size == 0) {
      ESP_LOGW(TAG, "捕获的帧数据无效");
      continue;
    }

    // 分配帧对象
    PreviewFrame *frame = new (std::nothrow) PreviewFrame(width, height);
    if (frame == nullptr || !frame->IsValid()) {
      ESP_LOGE(TAG, "无法分配帧对象，停止预览");
      if (frame != nullptr) {
        delete frame;
      }
      Application::GetInstance().Schedule([this]() {
        Stop();
        Application::GetInstance().Alert("错误", "内存不足", "error", "error");
      });
      break;
    }

    // 复制数据
    memcpy(frame->data, rgb565_data, data_size);
    frame->timestamp = xTaskGetTickCount();

    ESP_LOGD(TAG, "已捕获帧: %dx%d, %zu 字节", width, height, data_size);

    // 发送到队列（非阻塞）
    if (xQueueSend(frame_queue_, &frame, 0) != pdTRUE) {
      // 队列满，丢弃旧帧
      PreviewFrame *old_frame = nullptr;
      if (xQueueReceive(frame_queue_, &old_frame, 0) == pdTRUE) {
        ESP_LOGD(TAG, "队列已满，丢弃旧帧");
        delete old_frame;
      }
      // 重新发送
      if (xQueueSend(frame_queue_, &frame, 0) != pdTRUE) {
        ESP_LOGW(TAG, "无法发送帧到队列");
        delete frame;
      }
    }
  }

  ESP_LOGI(TAG, "捕获循环已结束");
}

void LocalPreviewService::DisplayLoop() {
  while (active_) {
    // 从队列获取最新帧（超时 100ms）
    PreviewFrame *frame = nullptr;
    if (xQueueReceive(frame_queue_, &frame, pdMS_TO_TICKS(kQueueTimeoutMs)) !=
        pdTRUE) {
      // 队列超时，继续等待
      ESP_LOGD(TAG, "队列超时，等待新帧");
      continue;
    }

    if (frame == nullptr || !frame->IsValid()) {
      ESP_LOGW(TAG, "收到无效帧");
      if (frame != nullptr) {
        delete frame;
      }
      continue;
    }

    // 更新显示
    auto *display = Board::GetInstance().GetDisplay();
    auto *lcd_display = dynamic_cast<LcdDisplay *>(display);

    if (lcd_display) {
      TickType_t start_time = xTaskGetTickCount();

      bool success = lcd_display->UpdatePreviewCanvas(frame->data, frame->width,
                                                      frame->height);

      TickType_t end_time = xTaskGetTickCount();
      uint32_t render_time_ms = (end_time - start_time) * portTICK_PERIOD_MS;
      uint32_t total_time_ms =
          (end_time - frame->timestamp) * portTICK_PERIOD_MS;

      if (success) {
        ESP_LOGD(TAG, "已显示帧: 渲染=%ums, 总延迟=%ums", render_time_ms,
                 total_time_ms);
      } else {
        ESP_LOGW(TAG, "显示帧失败");
      }
    } else {
      ESP_LOGW(TAG, "无法获取 LCD 显示器");
    }

    // 释放帧内存
    delete frame;
  }

  ESP_LOGI(TAG, "显示循环已结束");
}

} // namespace xiaozhi
