#include "video_stream_service.h"
#include "camera.h"
#include "esp32_camera.h"
#include <esp_log.h>
#include <esp_heap_caps.h>

#define TAG "VideoStreamService"

VideoStreamService::VideoStreamService()
    : camera_(nullptr)
    , capture_task_handle_(nullptr)
    , running_(false)
    , target_fps_(10) {
}

VideoStreamService::~VideoStreamService() {
    Stop();
}

bool VideoStreamService::Start(Camera* camera, int fps) {
    if (running_) {
        ESP_LOGW(TAG, "Service already running");
        return false;
    }

    if (camera == nullptr) {
        ESP_LOGE(TAG, "Camera is null");
        return false;
    }

    camera_ = camera;
    target_fps_ = fps;
    running_ = true;

    // 创建捕获任务
    BaseType_t ret = xTaskCreate(
        CaptureTask,
        "video_capture",
        4096,
        this,
        5,
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

void VideoStreamService::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    // 等待任务结束
    if (capture_task_handle_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(200));  // 给任务时间退出
        capture_task_handle_ = nullptr;
    }

    // 清空队列
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!frame_queue_.empty()) {
        frame_queue_.pop();
    }

    ESP_LOGI(TAG, "Video stream service stopped");
}

std::unique_ptr<JpegFrame> VideoStreamService::GetNextFrame() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (frame_queue_.empty()) {
        return nullptr;
    }

    auto frame = std::move(frame_queue_.front());
    frame_queue_.pop();
    return frame;
}

size_t VideoStreamService::GetQueueSize() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return frame_queue_.size();
}

void VideoStreamService::CaptureTask(void* param) {
    auto* service = static_cast<VideoStreamService*>(param);
    service->CaptureLoop();
    vTaskDelete(nullptr);
}

void VideoStreamService::CaptureLoop() {
    const TickType_t frame_delay = pdMS_TO_TICKS(1000 / target_fps_);
    
    ESP_LOGI(TAG, "Capture loop started, frame_delay=%d ms", 1000 / target_fps_);

    while (running_) {
        TickType_t start_tick = xTaskGetTickCount();

        // 检查摄像头是否可用
        auto* esp32_camera = dynamic_cast<Esp32Camera*>(camera_);
        if (esp32_camera == nullptr || !esp32_camera->IsAvailable()) {
            ESP_LOGW(TAG, "Camera not available");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // 使用高效捕获函数（只取一帧，不显示预览）
        if (!esp32_camera->CaptureForStream()) {
            ESP_LOGW(TAG, "Failed to capture frame");
            vTaskDelay(frame_delay);
            continue;
        }

        // 编码为 JPEG
        uint8_t* jpeg_data = nullptr;
        size_t jpeg_size = 0;
        
        if (!esp32_camera->CaptureJpeg(&jpeg_data, &jpeg_size, kDefaultQuality)) {
            ESP_LOGW(TAG, "Failed to encode JPEG");
            vTaskDelay(frame_delay);
            continue;
        }

        // 创建帧对象
        auto frame = std::make_unique<JpegFrame>();
        frame->data.assign(jpeg_data, jpeg_data + jpeg_size);
        frame->width = esp32_camera->GetFrameWidth();
        frame->height = esp32_camera->GetFrameHeight();
        frame->timestamp = xTaskGetTickCount();
        
        // 释放临时分配的内存
        heap_caps_free(jpeg_data);

        // 添加到队列
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            
            // 如果队列满了，丢弃最旧的帧
            if (frame_queue_.size() >= kMaxQueueSize) {
                ESP_LOGD(TAG, "Queue full, dropping oldest frame");
                frame_queue_.pop();
            }
            
            frame_queue_.push(std::move(frame));
        }

        // 控制帧率
        TickType_t elapsed = xTaskGetTickCount() - start_tick;
        if (elapsed < frame_delay) {
            vTaskDelay(frame_delay - elapsed);
        }
    }

    ESP_LOGI(TAG, "Capture loop ended");
}
