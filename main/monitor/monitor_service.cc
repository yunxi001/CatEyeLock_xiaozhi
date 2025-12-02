#include "monitor_service.h"
#include "protocol.h"
#include "camera.h"
#include "audio_service.h"
#include <esp_log.h>
#include <cJSON.h>

#define TAG "MonitorService"

MonitorService::MonitorService()
    : protocol_(nullptr)
    , camera_(nullptr)
    , audio_service_(nullptr)
    , transmit_task_handle_(nullptr)
    , running_(false) {
}

MonitorService::~MonitorService() {
    Stop();
}

bool MonitorService::Start(Protocol* protocol, Camera* camera, AudioService* audio_service) {
    if (running_) {
        ESP_LOGW(TAG, "Monitor service already running");
        return false;
    }

    if (protocol == nullptr || camera == nullptr || audio_service == nullptr) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    protocol_ = protocol;
    camera_ = camera;
    audio_service_ = audio_service;

    // 创建视频流服务
    video_stream_ = std::make_unique<VideoStreamService>();
    if (!video_stream_->Start(camera_, kDefaultFps)) {
        ESP_LOGE(TAG, "Failed to start video stream service");
        return false;
    }

    running_ = true;

    // 创建视频传输任务
    BaseType_t ret = xTaskCreate(
        VideoTransmitTask,
        "monitor_transmit",
        4096,
        this,
        5,
        &transmit_task_handle_
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create transmit task");
        video_stream_->Stop();
        video_stream_.reset();
        running_ = false;
        return false;
    }

    ESP_LOGI(TAG, "Monitor service started");
    
    if (state_change_callback_) {
        state_change_callback_(true);
    }
    
    return true;
}

void MonitorService::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    // 停止视频流
    if (video_stream_) {
        video_stream_->Stop();
        video_stream_.reset();
    }

    // 等待传输任务结束
    if (transmit_task_handle_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(200));
        transmit_task_handle_ = nullptr;
    }

    ESP_LOGI(TAG, "Monitor service stopped");
    
    if (state_change_callback_) {
        state_change_callback_(false);
    }
}

void MonitorService::SetStateChangeCallback(std::function<void(bool)> callback) {
    state_change_callback_ = callback;
}

void MonitorService::VideoTransmitTask(void* param) {
    auto* service = static_cast<MonitorService*>(param);
    service->VideoTransmitLoop();
    vTaskDelete(nullptr);
}

void MonitorService::VideoTransmitLoop() {
    ESP_LOGI(TAG, "Video transmit loop started");

    while (running_) {
        // 获取下一帧
        auto frame = video_stream_->GetNextFrame();
        if (!frame) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 发送帧到服务器
        if (!SendVideoFrame(*frame)) {
            ESP_LOGW(TAG, "Failed to send video frame");
        }
    }

    ESP_LOGI(TAG, "Video transmit loop ended");
}

bool MonitorService::SendVideoFrame(const JpegFrame& frame) {
    if (!protocol_) {
        return false;
    }

    // 使用 Protocol 的 SendVideo 方法发送视频帧
    // 使用 BinaryProtocol2 格式，通过 reserved 字段传递宽高信息
    bool success = protocol_->SendVideo(
        frame.data.data(),
        frame.data.size(),
        frame.timestamp,
        frame.width,
        frame.height
    );

    if (success) {
        ESP_LOGD(TAG, "Sent video frame: %dx%d, size=%zu bytes, timestamp=%u", 
                 frame.width, frame.height, frame.data.size(), frame.timestamp);
    } else {
        ESP_LOGW(TAG, "Failed to send video frame");
    }

    return success;
}
