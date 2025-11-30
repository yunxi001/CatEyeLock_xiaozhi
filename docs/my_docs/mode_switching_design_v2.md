# 模式切换设计 V2（简化版）

## 1. 设计原则

### 1.1 核心原则

1. **最小化修改**：不修改现有正常模式代码，只新增监控模式相关代码
2. **最大化复用**：监控模式尽量复用现有的音频服务、协议层、摄像头模块
3. **简单可靠**：不追求极致性能，优先保证稳定性和可维护性

### 1.2 硬件资源（ESP32-S3-N16R8）

| 资源 | 容量 | 用途规划 |
|------|------|----------|
| SRAM | 512KB (IRAM 192KB + DRAM 328KB) | 任务栈、小型缓冲、系统 |
| PSRAM | 8MB | 视频帧缓冲、JPEG缓冲、AI模型 |
| Flash | 16MB | 固件、资源文件 |

---

## 2. 模式定义

### 2.1 两种模式

```cpp
enum DeviceMode {
    kModeNormal,      // 普通模式：AI语音对话（现有功能，不修改）
    kModeMonitor,     // 监控模式：实时视频对讲（新增功能）
};
```

### 2.2 模式对比

| 特性 | 普通模式 | 监控模式 |
|------|----------|----------|
| 唤醒词检测 | ✅ 启用 | ❌ 禁用 |
| AI语音对话 | ✅ 启用 | ❌ 禁用 |
| 音频采集 | ✅ 启用 | ✅ 启用（复用） |
| 音频播放 | ✅ 启用 | ✅ 启用（复用） |
| AEC回声消除 | ✅ 可选 | ✅ 启用（复用） |
| 视频采集 | ❌ 按需拍照 | ✅ 连续采集 |
| 视频传输 | ❌ 无 | ✅ MJPEG流 |

---

## 3. 复用策略

### 3.1 完全复用的模块（不修改）

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           完全复用的模块                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  AudioCodec          - I2S音频输入输出，两种模式完全相同                     │
│  AudioService        - 音频服务，监控模式复用其编解码和队列机制              │
│  OpusEncoder/Decoder - OPUS编解码，两种模式完全相同                          │
│  AudioProcessor      - AEC/VAD/NS，监控模式需要AEC                          │
│  Protocol            - WebSocket/MQTT协议层，扩展支持视频消息                │
│  Esp32Camera         - 摄像头驱动，扩展连续捕获接口                          │
│  Display             - 显示模块，两种模式完全相同                            │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 需要扩展的模块

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           需要扩展的模块                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Esp32Camera         - 新增: StartStreaming() / StopStreaming()             │
│  Protocol            - 新增: SendVideo() 方法                               │
│  Application         - 新增: 监控模式状态和切换逻辑                          │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.3 需要新增的模块

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           需要新增的模块                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  VideoStreamService  - 视频流服务，管理连续视频采集和编码                    │
│  MonitorService      - 监控服务，协调音视频流的发送                          │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```


---

## 4. 监控模式架构

### 4.1 数据流设计

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         监控模式数据流                                       │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────┐                                                            │
│  │   摄像头     │                                                            │
│  │  (DVP)      │                                                            │
│  └──────┬──────┘                                                            │
│         │ 原始帧                                                             │
│         ▼                                                                   │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │              VideoStreamService (新增)                           │       │
│  │  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐         │       │
│  │  │ 视频捕获任务 │ -> │ 帧缓冲队列  │ -> │ JPEG编码任务 │         │       │
│  │  │(复用Camera) │    │  (PSRAM)    │    │(复用现有)   │         │       │
│  │  └─────────────┘    └─────────────┘    └──────┬──────┘         │       │
│  └───────────────────────────────────────────────┼─────────────────┘       │
│                                                  │ JPEG数据                 │
│                                                  ▼                          │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │              MonitorService (新增)                               │       │
│  │                                                                  │       │
│  │  JPEG数据 ──────────────────────────────────────┐               │       │
│  │                                                  │               │       │
│  │  ┌─────────────────────────────────────────┐    │               │       │
│  │  │         AudioService (复用)              │    │               │       │
│  │  │  音频采集 -> AEC处理 -> OPUS编码         │    │               │       │
│  │  └──────────────────────┬──────────────────┘    │               │       │
│  │                         │ OPUS数据              │               │       │
│  │                         ▼                       ▼               │       │
│  │                  ┌─────────────────────────────────┐            │       │
│  │                  │      发送调度器                  │            │       │
│  │                  │  (音视频交替发送，时间戳同步)    │            │       │
│  │                  └──────────────┬──────────────────┘            │       │
│  └─────────────────────────────────┼───────────────────────────────┘       │
│                                    │                                        │
│                                    ▼                                        │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │              Protocol (复用+扩展)                                │       │
│  │  SendAudio() - 复用现有                                         │       │
│  │  SendVideo() - 新增                                             │       │
│  └──────────────────────────────────┬──────────────────────────────┘       │
│                                     │                                       │
│                                     ▼                                       │
│                              ┌──────────┐                                   │
│                              │  服务器   │                                   │
│                              └──────────┘                                   │
│                                                                             │
│  服务器 -> Protocol -> AudioService -> 扬声器 (下行音频，完全复用)          │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 4.2 内存规划（8MB PSRAM）

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         PSRAM 内存分配 (8MB)                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  普通模式:                                                                  │
│  ├── AI唤醒词模型      ~2.0 MB                                              │
│  ├── 显示缓冲          ~0.2 MB                                              │
│  ├── 音频缓冲          ~0.1 MB                                              │
│  └── 其他              ~0.5 MB                                              │
│  总计: ~2.8 MB，剩余 ~5.2 MB                                                │
│                                                                             │
│  监控模式:                                                                  │
│  ├── 视频帧缓冲 (3帧)  640*480*2*3 = 1.8 MB (RGB565/YUV422)                │
│  ├── JPEG输出缓冲 (2个) 100KB*2 = 0.2 MB                                    │
│  ├── 显示缓冲          ~0.2 MB                                              │
│  ├── 音频缓冲          ~0.1 MB                                              │
│  ├── AI唤醒词模型      ~2.0 MB (可选择释放以获得更多空间)                   │
│  └── 其他              ~0.5 MB                                              │
│  总计: ~4.8 MB (保留模型) 或 ~2.8 MB (释放模型)                             │
│                                                                             │
│  建议: 监控模式下保留唤醒词模型，简化切换逻辑                               │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 5. 模式切换设计

### 5.1 简化的切换策略

**核心思想**：不释放唤醒词模型，只是禁用/启用相关功能

```cpp
// 切换到监控模式
bool Application::SwitchToMonitorMode() {
    // 1. 禁用普通模式功能（不释放资源）
    audio_service_.EnableWakeWordDetection(false);  // 禁用唤醒词
    
    // 2. 确保AEC已启用（监控模式需要）
    if (aec_mode_ == kAecOff) {
        audio_service_.EnableDeviceAec(true);
    }
    
    // 3. 启动视频流服务
    if (!video_stream_service_) {
        video_stream_service_ = std::make_unique<VideoStreamService>();
    }
    video_stream_service_->Start();
    
    // 4. 启动监控服务
    if (!monitor_service_) {
        monitor_service_ = std::make_unique<MonitorService>();
    }
    monitor_service_->Start();
    
    // 5. 更新状态
    device_mode_ = kModeMonitor;
    SetDeviceState(kDeviceStateMonitorStreaming);
    
    return true;
}

// 切换回普通模式
bool Application::SwitchToNormalMode() {
    // 1. 停止监控服务
    if (monitor_service_) {
        monitor_service_->Stop();
    }
    
    // 2. 停止视频流服务
    if (video_stream_service_) {
        video_stream_service_->Stop();
    }
    
    // 3. 恢复AEC设置
    if (aec_mode_ == kAecOff) {
        audio_service_.EnableDeviceAec(false);
    }
    
    // 4. 重新启用唤醒词检测
    audio_service_.EnableWakeWordDetection(true);
    
    // 5. 更新状态
    device_mode_ = kModeNormal;
    SetDeviceState(kDeviceStateIdle);
    
    return true;
}
```

### 5.2 状态扩展

```cpp
// 在 device_state.h 中新增状态
enum DeviceState {
    // ... 现有状态保持不变 ...
    
    // 新增监控模式状态
    kDeviceStateMonitorConnecting,  // 监控模式-连接中
    kDeviceStateMonitorStreaming,   // 监控模式-流媒体传输中
};
```

### 5.3 切换时序

```
普通模式 -> 监控模式 (预计 <200ms)
┌────────────────────────────────────────────────────────────────┐
│ 1. 禁用唤醒词检测        ~10ms  (只是设置标志位)               │
│ 2. 启用AEC              ~10ms  (如果之前未启用)                │
│ 3. 初始化视频流服务      ~100ms (分配PSRAM缓冲)                │
│ 4. 启动视频捕获          ~50ms  (摄像头已初始化)               │
│ 5. 启动监控服务          ~10ms                                 │
└────────────────────────────────────────────────────────────────┘

监控模式 -> 普通模式 (预计 <100ms)
┌────────────────────────────────────────────────────────────────┐
│ 1. 停止监控服务          ~10ms                                 │
│ 2. 停止视频流服务        ~50ms  (等待当前帧完成)               │
│ 3. 恢复AEC设置           ~10ms                                 │
│ 4. 启用唤醒词检测        ~10ms  (模型已在内存中)               │
└────────────────────────────────────────────────────────────────┘
```


---

## 6. VideoStreamService 设计

### 6.1 类设计

```cpp
/**
 * @class VideoStreamService
 * @brief 视频流服务，负责连续视频采集和JPEG编码
 * 
 * 设计原则：
 * - 复用现有 Esp32Camera 的捕获能力
 * - 复用现有 image_to_jpeg 编码函数
 * - 使用PSRAM存储帧缓冲
 */
class VideoStreamService {
public:
    VideoStreamService();
    ~VideoStreamService();
    
    // 生命周期
    bool Start();
    void Stop();
    bool IsRunning() const { return running_; }
    
    // 配置
    void SetFrameRate(int fps);
    void SetQuality(int quality);
    
    // 获取编码后的JPEG帧
    // 返回nullptr表示没有可用帧
    std::unique_ptr<JpegFrame> PopFrame();
    
    // 统计信息
    int GetActualFps() const { return actual_fps_; }
    size_t GetAverageBitrate() const { return avg_bitrate_; }
    
private:
    void CaptureTask();      // 视频捕获任务
    void EncodeTask();       // JPEG编码任务
    
    // 配置
    int target_fps_ = 15;
    int jpeg_quality_ = 80;
    
    // 状态
    std::atomic<bool> running_{false};
    std::atomic<int> actual_fps_{0};
    std::atomic<size_t> avg_bitrate_{0};
    
    // 任务句柄
    TaskHandle_t capture_task_handle_ = nullptr;
    TaskHandle_t encode_task_handle_ = nullptr;
    
    // 帧缓冲队列（PSRAM）
    static constexpr int FRAME_BUFFER_COUNT = 3;
    struct FrameBuffer {
        uint8_t* data = nullptr;
        size_t size = 0;
        uint32_t timestamp = 0;
        bool ready = false;
    };
    std::array<FrameBuffer, FRAME_BUFFER_COUNT> frame_buffers_;
    int capture_index_ = 0;
    int encode_index_ = 0;
    
    // JPEG输出队列
    std::mutex jpeg_mutex_;
    std::deque<std::unique_ptr<JpegFrame>> jpeg_queue_;
    static constexpr int MAX_JPEG_QUEUE_SIZE = 2;
    
    // 同步
    SemaphoreHandle_t frame_ready_sem_ = nullptr;
};
```

### 6.2 JPEG帧结构

```cpp
struct JpegFrame {
    std::vector<uint8_t> data;  // JPEG数据
    uint32_t timestamp;          // 时间戳（毫秒）
    uint16_t width;
    uint16_t height;
    
    JpegFrame() = default;
    JpegFrame(JpegFrame&&) = default;
    JpegFrame& operator=(JpegFrame&&) = default;
};
```

### 6.3 视频捕获任务

```cpp
void VideoStreamService::CaptureTask() {
    auto camera = Board::GetInstance().GetCamera();
    auto esp32_camera = dynamic_cast<Esp32Camera*>(camera);
    
    TickType_t last_capture_time = xTaskGetTickCount();
    const TickType_t frame_interval = pdMS_TO_TICKS(1000 / target_fps_);
    
    while (running_) {
        // 帧率控制
        TickType_t now = xTaskGetTickCount();
        TickType_t elapsed = now - last_capture_time;
        if (elapsed < frame_interval) {
            vTaskDelay(frame_interval - elapsed);
        }
        last_capture_time = xTaskGetTickCount();
        
        // 获取下一个可用的帧缓冲
        FrameBuffer& buffer = frame_buffers_[capture_index_];
        
        // 等待缓冲可用（编码任务已处理完）
        if (buffer.ready) {
            // 缓冲还未被消费，跳过这一帧（丢帧）
            continue;
        }
        
        // 捕获一帧（复用现有的Capture逻辑，但不显示预览）
        if (esp32_camera->CaptureToBuffer(buffer.data, buffer.size)) {
            buffer.timestamp = esp_timer_get_time() / 1000;  // 毫秒
            buffer.ready = true;
            
            // 通知编码任务
            xSemaphoreGive(frame_ready_sem_);
            
            // 移动到下一个缓冲
            capture_index_ = (capture_index_ + 1) % FRAME_BUFFER_COUNT;
        }
    }
}
```

### 6.4 JPEG编码任务

```cpp
void VideoStreamService::EncodeTask() {
    while (running_) {
        // 等待帧就绪
        if (xSemaphoreTake(frame_ready_sem_, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        
        FrameBuffer& buffer = frame_buffers_[encode_index_];
        if (!buffer.ready) {
            continue;
        }
        
        // 创建JPEG帧
        auto jpeg_frame = std::make_unique<JpegFrame>();
        jpeg_frame->timestamp = buffer.timestamp;
        jpeg_frame->width = 640;  // 从配置获取
        jpeg_frame->height = 480;
        
        // 复用现有的JPEG编码函数
        // image_to_jpeg 已经在 esp32_camera.cc 中使用
        bool success = image_to_jpeg_cb(
            buffer.data, buffer.size,
            jpeg_frame->width, jpeg_frame->height,
            V4L2_PIX_FMT_YUYV,  // 或实际格式
            jpeg_quality_,
            [](void* arg, size_t index, const void* data, size_t len) -> size_t {
                auto frame = static_cast<JpegFrame*>(arg);
                if (data && len > 0) {
                    size_t old_size = frame->data.size();
                    frame->data.resize(old_size + len);
                    memcpy(frame->data.data() + old_size, data, len);
                }
                return len;
            },
            jpeg_frame.get()
        );
        
        // 标记缓冲为可用
        buffer.ready = false;
        encode_index_ = (encode_index_ + 1) % FRAME_BUFFER_COUNT;
        
        if (success && !jpeg_frame->data.empty()) {
            // 加入输出队列
            std::lock_guard<std::mutex> lock(jpeg_mutex_);
            if (jpeg_queue_.size() < MAX_JPEG_QUEUE_SIZE) {
                jpeg_queue_.push_back(std::move(jpeg_frame));
            }
            // 队列满则丢弃最旧的帧
            else {
                jpeg_queue_.pop_front();
                jpeg_queue_.push_back(std::move(jpeg_frame));
            }
        }
    }
}
```

---

## 7. MonitorService 设计

### 7.1 类设计

```cpp
/**
 * @class MonitorService
 * @brief 监控服务，协调音视频流的发送
 * 
 * 设计原则：
 * - 复用现有 AudioService 的音频流
 * - 复用现有 Protocol 的网络发送
 * - 新增视频流发送逻辑
 */
class MonitorService {
public:
    MonitorService();
    ~MonitorService();
    
    bool Start();
    void Stop();
    bool IsRunning() const { return running_; }
    
    // 统计
    struct Stats {
        int video_fps;
        int audio_fps;
        size_t video_bitrate;
        size_t audio_bitrate;
        uint32_t latency_ms;
    };
    Stats GetStats() const;
    
private:
    void StreamTask();  // 流媒体发送任务
    
    std::atomic<bool> running_{false};
    TaskHandle_t stream_task_handle_ = nullptr;
    
    // 时间戳基准
    int64_t start_time_ = 0;
    
    // 统计
    Stats stats_;
};
```

### 7.2 流媒体发送任务

```cpp
void MonitorService::StreamTask() {
    auto& app = Application::GetInstance();
    auto& audio_service = app.GetAudioService();
    auto& video_service = VideoStreamService::GetInstance();
    auto protocol = /* 获取协议实例 */;
    
    start_time_ = esp_timer_get_time();
    
    while (running_) {
        bool sent_something = false;
        
        // 1. 发送音频（优先级更高，保证实时性）
        while (auto audio_packet = audio_service.PopPacketFromSendQueue()) {
            protocol->SendAudio(std::move(audio_packet));
            sent_something = true;
            stats_.audio_bitrate += audio_packet->payload.size();
        }
        
        // 2. 发送视频
        if (auto jpeg_frame = video_service.PopFrame()) {
            protocol->SendVideo(std::move(jpeg_frame));
            sent_something = true;
            stats_.video_bitrate += jpeg_frame->data.size();
            stats_.video_fps++;
        }
        
        // 3. 如果没有数据，短暂休眠
        if (!sent_something) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}
```

---

## 8. 协议扩展

### 8.1 Protocol 类扩展

```cpp
// 在 protocol.h 中新增
class Protocol {
public:
    // ... 现有接口保持不变 ...
    
    // 新增：发送视频帧
    virtual bool SendVideo(std::unique_ptr<JpegFrame> frame);
    
    // 新增：监控模式消息
    virtual void SendMonitorStatus(const MonitorService::Stats& stats);
};
```

### 8.2 WebSocket协议实现

```cpp
bool WebsocketProtocol::SendVideo(std::unique_ptr<JpegFrame> frame) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }
    
    // 使用扩展的二进制协议
    // type = 1 表示视频帧
    if (version_ >= 2) {
        std::vector<uint8_t> packet;
        packet.resize(sizeof(BinaryProtocol2) + frame->data.size());
        
        auto* header = reinterpret_cast<BinaryProtocol2*>(packet.data());
        header->version = htons(version_);
        header->type = htons(1);  // 1 = JPEG视频
        header->reserved = 0;
        header->timestamp = htonl(frame->timestamp);
        header->payload_size = htonl(frame->data.size());
        
        memcpy(header->payload, frame->data.data(), frame->data.size());
        
        return websocket_->Send(packet.data(), packet.size(), true);
    }
    
    // 版本1不支持视频
    return false;
}
```

### 8.3 消息格式

```cpp
// 服务器 -> 设备：启动监控模式
{
    "type": "monitor",
    "command": "start",
    "config": {
        "video_fps": 15,
        "video_quality": 80,
        "audio_enabled": true
    }
}

// 服务器 -> 设备：停止监控模式
{
    "type": "monitor",
    "command": "stop"
}

// 设备 -> 服务器：监控状态
{
    "type": "monitor",
    "state": "streaming",
    "stats": {
        "video_fps": 15,
        "audio_fps": 16,
        "video_bitrate": 3500000,
        "audio_bitrate": 20000
    }
}
```


---

## 9. Application 层集成

### 9.1 新增成员和方法

```cpp
// 在 application.h 中新增
class Application {
public:
    // ... 现有接口保持不变 ...
    
    // 新增：模式管理
    DeviceMode GetDeviceMode() const { return device_mode_; }
    bool SwitchToMonitorMode();
    bool SwitchToNormalMode();
    
private:
    // 新增成员
    DeviceMode device_mode_ = kModeNormal;
    std::unique_ptr<VideoStreamService> video_stream_service_;
    std::unique_ptr<MonitorService> monitor_service_;
    
    // 新增方法
    void HandleMonitorCommand(const cJSON* root);
};
```

### 9.2 消息处理扩展

```cpp
// 在 OnIncomingJson 回调中新增
protocol_->OnIncomingJson([this, display](const cJSON* root) {
    auto type = cJSON_GetObjectItem(root, "type");
    if (strcmp(type->valuestring, "monitor") == 0) {
        HandleMonitorCommand(root);
    }
    // ... 现有消息处理保持不变 ...
});

void Application::HandleMonitorCommand(const cJSON* root) {
    auto command = cJSON_GetObjectItem(root, "command");
    if (!cJSON_IsString(command)) {
        return;
    }
    
    if (strcmp(command->valuestring, "start") == 0) {
        Schedule([this]() {
            SwitchToMonitorMode();
        });
    } else if (strcmp(command->valuestring, "stop") == 0) {
        Schedule([this]() {
            SwitchToNormalMode();
        });
    }
}
```

### 9.3 状态切换扩展

```cpp
void Application::SetDeviceState(DeviceState state) {
    // ... 现有逻辑保持不变 ...
    
    switch (state) {
        // ... 现有状态处理保持不变 ...
        
        // 新增监控模式状态处理
        case kDeviceStateMonitorConnecting:
            display->SetStatus("连接监控...");
            display->SetEmotion("camera");
            break;
            
        case kDeviceStateMonitorStreaming:
            display->SetStatus("监控中");
            display->SetEmotion("camera");
            break;
    }
}
```

---

## 10. Esp32Camera 扩展

### 10.1 新增接口

```cpp
// 在 esp32_camera.h 中新增
class Esp32Camera : public Camera {
public:
    // ... 现有接口保持不变 ...
    
    // 新增：直接捕获到缓冲区（不显示预览）
    bool CaptureToBuffer(uint8_t* buffer, size_t& size);
    
    // 新增：获取帧信息
    uint16_t GetFrameWidth() const { return frame_.width; }
    uint16_t GetFrameHeight() const { return frame_.height; }
    v4l2_pix_fmt_t GetFrameFormat() const { return frame_.format; }
};
```

### 10.2 实现

```cpp
bool Esp32Camera::CaptureToBuffer(uint8_t* buffer, size_t& size) {
    if (!streaming_on_ || video_fd_ < 0) {
        return false;
    }
    
    // 出队缓冲区
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(video_fd_, VIDIOC_DQBUF, &buf) != 0) {
        return false;
    }
    
    // 复制数据到提供的缓冲区
    size_t copy_size = std::min(size, (size_t)buf.bytesused);
    memcpy(buffer, mmap_buffers_[buf.index].start, copy_size);
    size = copy_size;
    
    // 入队缓冲区
    if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
        ESP_LOGE(TAG, "VIDIOC_QBUF failed");
    }
    
    return true;
}
```

---

## 11. 任务优先级和资源分配

### 11.1 任务优先级（监控模式）

```cpp
// 复用现有任务优先级
#define PRIORITY_AUDIO_INPUT        8   // 音频输入（复用）
#define PRIORITY_AUDIO_OUTPUT       4   // 音频输出（复用）
#define PRIORITY_OPUS_CODEC         2   // OPUS编解码（复用）
#define PRIORITY_MAIN_LOOP          3   // 主事件循环（复用）

// 新增任务优先级
#define PRIORITY_VIDEO_CAPTURE      6   // 视频捕获
#define PRIORITY_JPEG_ENCODE        5   // JPEG编码
#define PRIORITY_MONITOR_STREAM     4   // 监控流发送
```

### 11.2 任务栈分配

```cpp
// 新增任务栈大小
#define VIDEO_CAPTURE_STACK_SIZE    4096   // 视频捕获任务
#define JPEG_ENCODE_STACK_SIZE      8192   // JPEG编码任务（需要较大栈）
#define MONITOR_STREAM_STACK_SIZE   4096   // 监控流发送任务
```

### 11.3 PSRAM缓冲分配

```cpp
// VideoStreamService 初始化时分配
bool VideoStreamService::AllocateBuffers() {
    const size_t frame_size = 640 * 480 * 2;  // RGB565/YUV422
    
    for (int i = 0; i < FRAME_BUFFER_COUNT; i++) {
        frame_buffers_[i].data = (uint8_t*)heap_caps_malloc(
            frame_size, 
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        if (!frame_buffers_[i].data) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer %d", i);
            return false;
        }
        frame_buffers_[i].size = frame_size;
    }
    
    return true;
}
```

---

## 12. 文件结构

### 12.1 新增文件

```
main/
├── video/
│   ├── video_stream_service.h      # 视频流服务头文件
│   ├── video_stream_service.cc     # 视频流服务实现
│   └── jpeg_frame.h                # JPEG帧结构定义
├── monitor/
│   ├── monitor_service.h           # 监控服务头文件
│   └── monitor_service.cc          # 监控服务实现
```

### 12.2 需要修改的文件

```
main/
├── application.h                   # 新增模式管理接口
├── application.cc                  # 新增监控模式处理逻辑
├── device_state.h                  # 新增监控模式状态
├── protocols/
│   ├── protocol.h                  # 新增 SendVideo 接口
│   └── websocket_protocol.cc       # 实现 SendVideo
├── boards/common/
│   ├── esp32_camera.h              # 新增 CaptureToBuffer 接口
│   └── esp32_camera.cc             # 实现 CaptureToBuffer
└── CMakeLists.txt                  # 添加新文件到编译
```

---

## 13. 总结

### 13.1 设计特点

1. **最小修改原则**：现有正常模式代码几乎不需要修改
2. **最大复用原则**：音频服务、协议层、摄像头驱动全部复用
3. **简单切换**：不释放唤醒词模型，切换时间 <200ms
4. **AEC支持**：监控模式复用现有的AudioProcessor实现AEC

### 13.2 工作量估算

| 模块 | 工作量 | 说明 |
|------|--------|------|
| VideoStreamService | 2-3天 | 新增，但复用现有捕获和编码逻辑 |
| MonitorService | 1-2天 | 新增，逻辑简单 |
| Protocol扩展 | 1天 | 新增SendVideo方法 |
| Application集成 | 1天 | 新增模式切换逻辑 |
| Esp32Camera扩展 | 0.5天 | 新增CaptureToBuffer方法 |
| 测试调试 | 2-3天 | 集成测试 |
| **总计** | **7-10天** | |

### 13.3 预期性能

| 指标 | 目标值 | 说明 |
|------|--------|------|
| 视频帧率 | 15-20 fps | VGA分辨率 |
| 视频延迟 | <500ms | 端到端 |
| 音频延迟 | <300ms | 复用现有性能 |
| 模式切换 | <200ms | 不释放模型 |
| 内存使用 | <5MB PSRAM | 保留唤醒词模型 |
