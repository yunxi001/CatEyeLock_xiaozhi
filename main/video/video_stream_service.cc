/**
 * @file video_stream_service.cc
 * @brief 视频流服务类实现
 * 
 * 实现了视频流的捕获、编码和队列管理功能。
 * 该服务作为监控模式的视频数据源，为 MonitorService 提供 JPEG 帧。
 */

#include "video_stream_service.h"
#include "camera.h"
#include "esp32_camera.h"
#include <esp_log.h>
#include <esp_heap_caps.h>

/// 日志标签
#define TAG "VideoStreamService"

/**
 * @brief 构造函数
 * 
 * 初始化所有成员变量为默认值。
 * 服务启动前不会创建任何任务或分配资源。
 */
VideoStreamService::VideoStreamService()
    : camera_(nullptr),
      capture_task_handle_(nullptr),
      running_(false),
      target_fps_(10) {
}

/**
 * @brief 析构函数
 * 
 * 确保服务正确停止，释放所有资源。
 */
VideoStreamService::~VideoStreamService() {
    Stop();
}

/**
 * @brief 启动视频流服务
 * 
 * 执行以下初始化步骤：
 * 1. 验证参数有效性
 * 2. 保存摄像头引用和帧率设置
 * 3. 创建捕获任务
 * 
 * @param camera 摄像头实例指针
 * @param fps 目标帧率
 * @return bool 启动成功返回 true
 */
bool VideoStreamService::Start(Camera* camera, int fps) {
    // 检查是否已在运行
    if (running_) {
        ESP_LOGW(TAG, "Service already running");
        return false;
    }

    // 验证摄像头参数
    if (camera == nullptr) {
        ESP_LOGE(TAG, "Camera is null");
        return false;
    }

    // 保存配置
    camera_ = camera;
    target_fps_ = fps;
    running_ = true;

    // 创建捕获任务
    // 任务名：video_capture，栈大小：4096 字节，优先级：5
    BaseType_t ret = xTaskCreate(
        CaptureTask,        // 任务函数
        "video_capture",    // 任务名称
        4096,               // 栈大小（字节）
        this,               // 任务参数
        5,                  // 优先级
        &capture_task_handle_
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create capture task");
        running_ = false;
        return false;
    }

    ESP_LOGI(TAG, "Video stream service started, fps=%d", fps);
    return true;
}

/**
 * @brief 停止视频流服务
 * 
 * 执行以下清理步骤：
 * 1. 设置停止标志
 * 2. 等待捕获任务退出
 * 3. 清空帧队列
 */
void VideoStreamService::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    // 等待捕获任务退出
    if (capture_task_handle_ != nullptr) {
        // 给任务 200ms 时间完成当前帧处理并退出
        vTaskDelay(pdMS_TO_TICKS(200));
        capture_task_handle_ = nullptr;
    }

    // 清空帧队列，释放所有帧内存
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!frame_queue_.empty()) {
            frame_queue_.pop();  // unique_ptr 自动释放内存
        }
    }

    ESP_LOGI(TAG, "Video stream service stopped");
}

/**
 * @brief 获取下一帧
 * 
 * 从队列头部取出一帧，所有权转移给调用者。
 * 此方法是线程安全的，可从任意任务调用。
 * 
 * @return std::unique_ptr<JpegFrame> 帧数据，队列为空返回 nullptr
 */
std::unique_ptr<JpegFrame> VideoStreamService::GetNextFrame() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (frame_queue_.empty()) {
        return nullptr;
    }

    // 移动队首帧的所有权
    auto frame = std::move(frame_queue_.front());
    frame_queue_.pop();
    return frame;
}

/**
 * @brief 获取队列大小
 * 
 * 返回当前队列中等待处理的帧数量。
 * 
 * @return size_t 队列中的帧数
 */
size_t VideoStreamService::GetQueueSize() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return frame_queue_.size();
}

/**
 * @brief 捕获任务入口函数
 * 
 * FreeRTOS 任务入口点，调用实例的 CaptureLoop() 方法。
 * 任务结束时自动删除自身。
 * 
 * @param param 任务参数（VideoStreamService 实例指针）
 */
void VideoStreamService::CaptureTask(void* param) {
    auto* service = static_cast<VideoStreamService*>(param);
    service->CaptureLoop();
    vTaskDelete(nullptr);  // 任务结束时删除自身
}

/**
 * @brief 捕获循环
 * 
 * 持续执行视频帧捕获和编码，直到服务停止。
 * 
 * 工作流程：
 * 1. 检查摄像头可用性
 * 2. 调用 CaptureForStream() 捕获原始图像
 * 3. 调用 CaptureJpeg() 编码为 JPEG
 * 4. 创建 JpegFrame 对象并添加到队列
 * 5. 控制帧率，等待下一帧时间
 * 
 * 帧率控制：
 * - 计算每帧应耗时 = 1000ms / fps
 * - 减去实际处理时间后延迟剩余时间
 * 
 * 队列管理：
 * - 队列满时丢弃最旧的帧
 * - 保证队列中始终是最新的帧
 */
void VideoStreamService::CaptureLoop() {
    // 计算每帧间隔时间（毫秒转换为 tick）
    const TickType_t frame_delay = pdMS_TO_TICKS(1000 / target_fps_);
    
    ESP_LOGI(TAG, "Capture loop started, frame_delay=%d ms", 1000 / target_fps_);

    while (running_) {
        // 记录帧开始时间，用于帧率控制
        TickType_t start_tick = xTaskGetTickCount();

        // 检查摄像头是否可用
        // 需要转换为 Esp32Camera 以访问特定方法
        auto* esp32_camera = dynamic_cast<Esp32Camera*>(camera_);
        if (esp32_camera == nullptr || !esp32_camera->IsAvailable()) {
            ESP_LOGW(TAG, "Camera not available");
            vTaskDelay(pdMS_TO_TICKS(1000));  // 等待 1 秒后重试
            continue;
        }

        // 捕获原始图像
        // CaptureForStream() 专为视频流优化，不显示预览
        if (!esp32_camera->CaptureForStream()) {
            ESP_LOGW(TAG, "Failed to capture frame");
            vTaskDelay(frame_delay);
            continue;
        }

        // 编码为 JPEG 格式
        uint8_t* jpeg_data = nullptr;
        size_t jpeg_size = 0;
        
        if (!esp32_camera->CaptureJpeg(&jpeg_data, &jpeg_size, kDefaultQuality)) {
            ESP_LOGW(TAG, "Failed to encode JPEG");
            vTaskDelay(frame_delay);
            continue;
        }

        // 创建帧对象并填充数据
        auto frame = std::make_unique<JpegFrame>();
        frame->data.assign(jpeg_data, jpeg_data + jpeg_size);  // 复制 JPEG 数据
        frame->width = esp32_camera->GetFrameWidth();
        frame->height = esp32_camera->GetFrameHeight();
        frame->timestamp = xTaskGetTickCount();  // 使用系统 tick 作为时间戳
        
        // 释放临时分配的 JPEG 数据内存
        // CaptureJpeg() 使用 heap_caps_malloc 分配，需要用 heap_caps_free 释放
        heap_caps_free(jpeg_data);

        // 将帧添加到队列
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            
            // 如果队列已满，丢弃最旧的帧以保持实时性
            if (frame_queue_.size() >= kMaxQueueSize) {
                ESP_LOGD(TAG, "Queue full, dropping oldest frame");
                frame_queue_.pop();
            }
            
            // 添加新帧到队列尾部
            frame_queue_.push(std::move(frame));
        }

        // 帧率控制：计算剩余等待时间
        TickType_t elapsed = xTaskGetTickCount() - start_tick;
        if (elapsed < frame_delay) {
            // 等待剩余时间以维持目标帧率
            vTaskDelay(frame_delay - elapsed);
        }
        // 如果处理时间超过帧间隔，不等待，立即处理下一帧
    }

    ESP_LOGI(TAG, "Capture loop ended");
}
