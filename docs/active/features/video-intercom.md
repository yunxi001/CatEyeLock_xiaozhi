# 实时视频对讲功能（监控模式）

**状态**: ✅ 已完成  
**最后更新**: 2025-12-10  
**代码位置**: `main/monitor/`, `main/video/`, `main/application.cc`

---

## 功能概述

实时视频对讲（监控模式）允许用户通过 APP 远程查看门口摄像头画面，并进行双向音频对讲。

### 核心特性

- ✅ 实时视频流传输（JPEG 编码，640x480@10fps）
- ✅ 双向音频对讲（OPUS 编码）
- ✅ 远程启动/停止控制
- ✅ 与人脸识别互斥保护

---

## 系统架构

```
Application
    ├── MonitorService
    │   └── VideoStreamService
    │       └── Esp32Camera
    └── Protocol (WebSocket/MQTT)
```

---

## 工作流程

```
┌─────────┐         ┌─────────┐         ┌─────────┐
│   APP   │         │  ESP32  │         │ Server  │
└────┬────┘         └────┬────┘         └────┬────┘
     │                   │                   │
     │ JSON: start_monitor                   │
     │──────────────────────────────────────>│
     │                   │<──────────────────│
     │                   │                   │
     │                   │ 启动监控服务       │
     │                   │                   │
     │                   │ Binary: type=0    │
     │                   │ (视频帧 JPEG)      │
     │                   │──────────────────>│
     │<──────────────────────────────────────│
     │                   │                   │
     │                   │ Binary: type=0    │
     │                   │ (音频帧 OPUS)      │
     │<──────────────────────────────────────│
     │──────────────────────────────────────>│
     │                   │                   │
     │ JSON: stop_monitor                    │
     │──────────────────────────────────────>│
     │                   │<──────────────────│
     │                   │                   │
     │                   │ 停止监控服务       │
```

---

## 实现细节

### 1. 启动监控模式

**代码位置**: `main/application.cc`

```cpp
bool Application::StartMonitorMode() {
    // 1. 状态检查
    if (monitor_mode_) {
        ESP_LOGW(TAG, "监控模式已在运行");
        return false;
    }

    // 2. 检查摄像头
    auto camera = board_->GetCamera();
    if (!camera || !camera->IsAvailable()) {
        ESP_LOGE(TAG, "摄像头不可用");
        return false;
    }

    // 3. 创建监控服务
    monitor_service_ = new MonitorService(camera, protocol_);

    // 4. 启动服务
    if (!monitor_service_->Start()) {
        ESP_LOGE(TAG, "监控服务启动失败");
        delete monitor_service_;
        monitor_service_ = nullptr;
        return false;
    }

    // 5. 更新状态
    monitor_mode_ = true;
    SetDeviceState(kDeviceStateMonitoring);

    ESP_LOGI(TAG, "监控模式已启动");
    return true;
}
```

### 2. 视频流捕获

**代码位置**: `main/video/video_stream_service.cc`

```cpp
void VideoStreamService::CaptureTask(void* arg) {
    auto* service = static_cast<VideoStreamService*>(arg);

    while (service->running_) {
        // 1. 捕获帧（监控模式专用）
        if (!service->camera_->CaptureForStream()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 2. JPEG 编码
        uint8_t* jpeg_data = nullptr;
        size_t jpeg_size = 0;
        if (!service->camera_->CaptureJpeg(&jpeg_data, &jpeg_size, 60)) {
            continue;
        }

        // 3. 创建帧对象
        JpegFrame* frame = new JpegFrame();
        frame->data = jpeg_data;
        frame->size = jpeg_size;
        frame->width = service->camera_->GetFrameWidth();
        frame->height = service->camera_->GetFrameHeight();
        frame->timestamp = esp_timer_get_time() / 1000;

        // 4. 加入队列
        if (!service->frame_queue_.Push(frame)) {
            // 队列满，丢弃最旧的帧
            JpegFrame* old_frame = nullptr;
            service->frame_queue_.Pop(&old_frame);
            if (old_frame) {
                heap_caps_free(old_frame->data);
                delete old_frame;
            }
            service->frame_queue_.Push(frame);
        }

        // 5. 控制帧率（10 fps）
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

### 3. 视频流发送

**代码位置**: `main/monitor/monitor_service.cc`

```cpp
void MonitorService::SendTask(void* arg) {
    auto* service = static_cast<MonitorService*>(arg);

    while (service->running_) {
        // 1. 从队列获取帧
        JpegFrame* frame = nullptr;
        if (!service->video_service_->GetFrame(&frame)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 2. 发送视频帧
        service->protocol_->SendVideo(
            frame->data,
            frame->size,
            frame->width,
            frame->height,
            frame->timestamp
        );

        // 3. 释放帧
        heap_caps_free(frame->data);
        delete frame;
    }
}
```

### 4. 停止监控模式

**代码位置**: `main/application.cc`

```cpp
void Application::StopMonitorMode() {
    if (!monitor_mode_) {
        return;
    }

    // 1. 停止服务
    if (monitor_service_) {
        monitor_service_->Stop();
        delete monitor_service_;
        monitor_service_ = nullptr;
    }

    // 2. 更新状态
    monitor_mode_ = false;
    SetDeviceState(kDeviceStateIdle);

    ESP_LOGI(TAG, "监控模式已停止");
}
```

---

## 协议格式

### 1. 启动监控命令（服务器 → ESP32）

```json
{
  "type": "system",
  "command": "start_monitor"
}
```

### 2. 停止监控命令（服务器 → ESP32）

```json
{
  "type": "system",
  "command": "stop_monitor"
}
```

### 3. 视频帧传输（ESP32 → 服务器）

**格式**: BinaryProtocol2

```c
struct BinaryProtocol2 {
    uint16_t version;      // 协议版本 = 2
    uint16_t type;         // 消息类型 = 0
    uint32_t reserved;     // 分辨率 (width << 16) | height
    uint32_t timestamp;    // 时间戳（毫秒）
    uint32_t payload_size; // JPEG 大小
    uint8_t payload[];     // JPEG 数据
};
```

**区分规则**:

| type | reserved | 数据类型     |
| ---- | -------- | ------------ |
| 0    | 0        | 音频流       |
| 0    | 非0      | 监控视频流   |
| 2    | 非0      | 人脸识别图像 |

**reserved 字段编码**:

```c
reserved = (width << 16) | height
// 例如 640x480: reserved = 0x028001E0
```

### 4. 音频帧传输（双向）

**格式**: BinaryProtocol2

```c
struct BinaryProtocol2 {
    uint16_t version;      // 协议版本 = 2
    uint16_t type;         // 消息类型 = 0
    uint32_t reserved;     // 0（音频标识）
    uint32_t timestamp;    // 时间戳（毫秒）
    uint32_t payload_size; // OPUS 大小
    uint8_t payload[];     // OPUS 数据
};
```

---

## 摄像头捕获模式

### Capture() vs CaptureForStream()

| 特性 | Capture()          | CaptureForStream() |
| ---- | ------------------ | ------------------ |
| 用途 | 人脸识别、用户拍照 | 监控视频流         |
| 帧数 | 取3帧保留最后一帧  | 只取1帧            |
| 预览 | 显示预览           | 无预览             |
| 旋转 | 支持旋转           | 无旋转             |
| 耗时 | ~150ms             | ~40ms              |
| 质量 | 高质量             | 适中质量           |

**使用原则**:

- 人脸识别使用 `Capture()` - 需要高质量图像和预览
- 监控视频流使用 `CaptureForStream()` - 需要高帧率

---

## 性能指标

| 指标       | 目标值     | 实际值    |
| ---------- | ---------- | --------- |
| 视频分辨率 | 640×480    | 640×480   |
| 视频帧率   | 10 fps     | 10 fps    |
| JPEG 质量  | 60-80      | 60        |
| 单帧大小   | ~15KB      | ~12-18KB  |
| 视频带宽   | ~1.2 Mbps  | ~1.0 Mbps |
| 音频带宽   | ~0.5 Mbps  | ~0.4 Mbps |
| 端到端延迟 | <500ms     | ~300ms    |
| 内存使用   | <5MB PSRAM | ~3MB      |

---

## 互斥保护

### 与人脸识别互斥

```cpp
void Application::HandleLockEvent(const LockMessage& msg) {
    // 监控模式中忽略锁控事件
    if (IsMonitorMode()) {
        ESP_LOGW(TAG, "监控模式中，忽略锁控事件");
        return;
    }

    // 处理锁控事件...
}
```

### 与本地预览互斥

```cpp
bool Application::StartMonitorMode() {
    // 检查本地预览是否运行
    if (local_preview_running_) {
        ESP_LOGE(TAG, "本地预览运行中，无法启动监控模式");
        return false;
    }

    // 启动监控模式...
}
```

---

## 错误处理

| 错误场景     | 处理方式           |
| ------------ | ------------------ |
| 摄像头不可用 | 返回错误，拒绝启动 |
| 内存不足     | 丢弃最旧的帧       |
| 帧队列满     | 丢弃最旧的帧       |
| 网络发送失败 | 记录警告，继续运行 |
| 捕获失败     | 跳过该帧，继续捕获 |
| 编码失败     | 跳过该帧，继续捕获 |

---

## 内存管理

### 帧队列管理

```cpp
class FrameQueue {
private:
    static const size_t kMaxFrames = 5;  // 最多缓存5帧
    std::queue<JpegFrame*> queue_;

public:
    bool Push(JpegFrame* frame) {
        if (queue_.size() >= kMaxFrames) {
            return false;  // 队列满
        }
        queue_.push(frame);
        return true;
    }

    bool Pop(JpegFrame** frame) {
        if (queue_.empty()) {
            return false;
        }
        *frame = queue_.front();
        queue_.pop();
        return true;
    }
};
```

### JPEG 数据分配

```cpp
// 使用 PSRAM 分配 JPEG 数据
uint8_t* jpeg_data = (uint8_t*)heap_caps_malloc(
    jpeg_size,
    MALLOC_CAP_SPIRAM
);

// 使用完毕后释放
heap_caps_free(jpeg_data);
```

---

## 配置选项

### 视频参数

**位置**: `main/video/video_stream_service.h`

```c
#define VIDEO_FRAME_RATE        10      // 帧率 (fps)
#define VIDEO_JPEG_QUALITY      60      // JPEG 质量
#define VIDEO_FRAME_QUEUE_SIZE  5       // 帧队列大小
```

### 音频参数

**位置**: `main/audio/audio_service.h`

```c
#define AUDIO_SAMPLE_RATE       16000   // 采样率 (Hz)
#define AUDIO_CHANNELS          1       // 声道数
#define AUDIO_FRAME_DURATION    60      // 帧时长 (ms)
```

---

## 调试技巧

### 1. 查看帧率

```cpp
static uint32_t frame_count = 0;
static uint32_t last_time = 0;

frame_count++;
uint32_t now = esp_timer_get_time() / 1000000;
if (now - last_time >= 1) {
    ESP_LOGI(TAG, "帧率: %d fps", frame_count);
    frame_count = 0;
    last_time = now;
}
```

### 2. 查看内存使用

```cpp
ESP_LOGI(TAG, "PSRAM 剩余: %d KB",
    heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
ESP_LOGI(TAG, "队列大小: %d", frame_queue_.Size());
```

### 3. 查看带宽

```cpp
static size_t total_bytes = 0;
static uint32_t last_time = 0;

total_bytes += jpeg_size;
uint32_t now = esp_timer_get_time() / 1000000;
if (now - last_time >= 1) {
    ESP_LOGI(TAG, "带宽: %.2f Mbps",
        (total_bytes * 8.0) / 1000000.0);
    total_bytes = 0;
    last_time = now;
}
```

---

## 优化建议

### 1. 帧率优化

- 降低 JPEG 质量（60 → 50）可提高帧率
- 降低分辨率（640x480 → 320x240）可提高帧率
- 增加帧队列大小可平滑帧率波动

### 2. 延迟优化

- 减少帧队列大小可降低延迟
- 使用更快的网络连接
- 优化 JPEG 编码参数

### 3. 内存优化

- 及时释放已发送的帧
- 限制帧队列大小
- 使用 PSRAM 存储 JPEG 数据

---

## 相关文档

- [ESP32与服务器通信协议](../protocols/esp32-server-v5.2.md)
- [本地预览功能](local-preview.md)
- [人脸识别功能](face-recognition.md)
- [变更日志](../reference/CHANGELOG.md)

---

**最后更新**: 2025-12-10
