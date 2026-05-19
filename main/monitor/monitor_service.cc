/**
 * @file monitor_service.cc
 * @brief 监控服务类实现
 * 
 * 实现了监控模式的核心功能，包括视频流管理、网络传输和状态控制。
 * 监控模式用于实时视频对讲，允许远程查看摄像头画面并进行双向音频通信。
 */

#include "monitor_service.h"
#include "protocol.h"
#include "camera.h"
#include "audio_service.h"
#include <esp_log.h>
#include <cJSON.h>

/// 日志标签
#define TAG "MonitorService"

/**
 * @brief 构造函数
 * 
 * 初始化所有成员变量为默认值。
 * 服务启动前不会创建任何任务或分配资源。
 */
MonitorService::MonitorService()
    : protocol_(nullptr),
      camera_(nullptr),
      audio_service_(nullptr),
      transmit_task_handle_(nullptr),
      running_(false) {
}

/**
 * @brief 析构函数
 * 
 * 确保服务正确停止，释放所有资源。
 */
MonitorService::~MonitorService() {
    Stop();
}

/**
 * @brief 启动监控模式
 * 
 * 执行以下初始化步骤：
 * 1. 验证参数有效性
 * 2. 保存服务引用
 * 3. 启用协议的监控模式（强制使用 BinaryProtocol2 格式）
 * 4. 创建并启动视频流服务
 * 5. 创建视频传输任务
 * 
 * @param protocol 协议实例（用于网络通信）
 * @param camera 摄像头实例
 * @param audio_service 音频服务实例
 * @return bool 启动成功返回 true
 */
bool MonitorService::Start(Protocol* protocol, Camera* camera, AudioService* audio_service) {
    // 检查是否已在运行
    if (running_) {
        ESP_LOGW(TAG, "Monitor service already running");
        return false;
    }

    // 验证参数有效性
    if (protocol == nullptr || camera == nullptr || audio_service == nullptr) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    // 保存服务引用
    protocol_ = protocol;
    camera_ = camera;
    audio_service_ = audio_service;

    // 启用协议的监控模式
    // 监控模式下强制使用 BinaryProtocol2 格式，支持视频帧传输
    protocol_->SetMonitorMode(true);

    // 创建并启动视频流服务
    video_stream_ = std::make_unique<VideoStreamService>();
    if (!video_stream_->Start(camera_, kDefaultFps)) {
        ESP_LOGE(TAG, "Failed to start video stream service");
        protocol_->SetMonitorMode(false);  // 恢复协议模式
        return false;
    }

    running_ = true;

    // 创建视频传输任务
    // 任务名：monitor_transmit，栈大小：4096 字节，优先级：5
    BaseType_t ret = xTaskCreate(
        VideoTransmitTask,      // 任务函数
        "monitor_transmit",     // 任务名称
        4096,                   // 栈大小（字节）
        this,                   // 任务参数
        5,                      // 优先级
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
    
    // 触发状态变化回调
    if (state_change_callback_) {
        state_change_callback_(true);
    }
    
    return true;
}

/**
 * @brief 停止监控模式
 * 
 * 执行以下清理步骤：
 * 1. 设置停止标志
 * 2. 禁用协议的监控模式
 * 3. 停止视频流服务
 * 4. 等待传输任务退出
 * 5. 触发状态变化回调
 */
void MonitorService::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    // 禁用协议的监控模式，恢复正常通信格式
    if (protocol_) {
        protocol_->SetMonitorMode(false);
    }

    // 停止视频流服务
    if (video_stream_) {
        video_stream_->Stop();
        video_stream_.reset();  // 释放资源
    }

    // 等待传输任务退出
    if (transmit_task_handle_ != nullptr) {
        // 给任务 200ms 时间完成当前操作并退出
        vTaskDelay(pdMS_TO_TICKS(200));
        transmit_task_handle_ = nullptr;
    }

    ESP_LOGI(TAG, "Monitor service stopped");
    
    // 触发状态变化回调
    if (state_change_callback_) {
        state_change_callback_(false);
    }
}

/**
 * @brief 设置状态变化回调
 * 
 * 当监控服务启动或停止时，会调用此回调函数通知外部。
 * 
 * @param callback 回调函数，参数为 true 表示已启动，false 表示已停止
 */
void MonitorService::SetStateChangeCallback(std::function<void(bool)> callback) {
    state_change_callback_ = callback;
}

/**
 * @brief 视频传输任务入口函数
 * 
 * FreeRTOS 任务入口点，调用实例的 VideoTransmitLoop() 方法。
 * 任务结束时自动删除自身。
 * 
 * @param param 任务参数（MonitorService 实例指针）
 */
void MonitorService::VideoTransmitTask(void* param) {
    auto* service = static_cast<MonitorService*>(param);
    service->VideoTransmitLoop();
    vTaskDelete(nullptr);  // 任务结束时删除自身
}

/**
 * @brief 视频传输循环
 * 
 * 持续从视频流服务获取帧并发送到服务器，直到服务停止。
 * 
 * 工作流程：
 * 1. 从 VideoStreamService 获取下一帧
 * 2. 如果没有可用帧，短暂等待后重试
 * 3. 调用 SendVideoFrame() 发送帧到服务器
 * 4. 循环直到 running_ 为 false
 */
void MonitorService::VideoTransmitLoop() {
    ESP_LOGI(TAG, "Video transmit loop started");

    // === 帧率统计变量 ===
    int frame_count = 0;
    TickType_t stats_start = xTaskGetTickCount();

    while (running_) {
        // 从视频流服务获取下一帧（非阻塞）
        auto frame = video_stream_->GetNextFrame();
        if (!frame) {
            // 没有可用帧，短暂等待后重试
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 发送帧到服务器
        if (!SendVideoFrame(*frame)) {
            ESP_LOGW(TAG, "Failed to send video frame");
            // 发送失败不中断循环，继续尝试发送下一帧
        }

        // === 每秒输出一次实际帧率统计 ===
        frame_count++;
        TickType_t now = xTaskGetTickCount();
        TickType_t elapsed_ms = (now - stats_start) * portTICK_PERIOD_MS;
        if (elapsed_ms >= 2000) {  // 每 2 秒统计一次
            float actual_fps = (float)frame_count * 1000.0f / (float)elapsed_ms;
            size_t queue_size = video_stream_->GetQueueSize();
            ESP_LOGI(TAG, "[帧率统计] 实际发送: %.1f fps, 队列积压: %zu 帧, 统计周期: %dms",
                     actual_fps, queue_size, (int)elapsed_ms);
            frame_count = 0;
            stats_start = now;
        }
    }

    ESP_LOGI(TAG, "Video transmit loop ended");
}

/**
 * @brief 发送视频帧到服务器
 * 
 * 使用 Protocol 的 SendVideo 方法将 JPEG 帧发送到服务器。
 * 采用 BinaryProtocol2 格式，通过 reserved 字段传递图像宽高信息。
 * 
 * 帧格式说明：
 * - 数据：JPEG 压缩的图像数据
 * - 时间戳：帧捕获时间（毫秒）
 * - 宽度/高度：图像尺寸，用于服务器端解码
 * 
 * @param frame JPEG 帧数据
 * @return bool 发送成功返回 true
 */
bool MonitorService::SendVideoFrame(const JpegFrame& frame) {
    // 检查协议实例是否有效
    if (!protocol_) {
        return false;
    }

    // === 性能计时：网络发送 ===
    TickType_t t_send_start = xTaskGetTickCount();

    // 使用 Protocol 的 SendVideo 方法发送视频帧
    // BinaryProtocol2 格式通过 reserved 字段传递宽高信息
    bool success = protocol_->SendVideo(
        frame.data.data(),   // JPEG 数据指针
        frame.data.size(),   // JPEG 数据大小
        frame.timestamp,     // 时间戳
        frame.width,         // 图像宽度
        frame.height         // 图像高度
    );

    TickType_t t_send_done = xTaskGetTickCount();

    if (success) {
        // 发送成功，输出性能日志
        ESP_LOGI(TAG, "[性能] 发送视频帧: %dx%d, size=%zu bytes, 发送耗时=%dms",
                 frame.width, frame.height, frame.data.size(),
                 (int)((t_send_done - t_send_start) * portTICK_PERIOD_MS));
    } else {
        ESP_LOGW(TAG, "发送视频帧失败, size=%zu bytes, 耗时=%dms",
                 frame.data.size(),
                 (int)((t_send_done - t_send_start) * portTICK_PERIOD_MS));
    }

    return success;
}
