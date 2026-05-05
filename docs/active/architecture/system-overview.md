# 系统架构概览

**最后更新**: 2026-05-05

---

## 整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         ESP32-S3                                 │
│                                                                  │
│  ┌──────────────┐      ┌──────────────┐      ┌──────────────┐  │
│  │ Application  │◄────►│ LockControl  │◄────►│ UART Driver  │  │
│  │              │      │   Service    │      │              │  │
│  └──────┬───────┘      └──────────────┘      └──────┬───────┘  │
│         │                                            │          │
│         │ 触发人脸识别                               │ 串口     │
│         ▼                                            ▼          │
│  ┌──────────────┐      ┌──────────────┐      ┌──────────────┐  │
│  │ Esp32Camera  │      │   Protocol   │      │   STM32C8T6  │  │
│  │              │      │  (WebSocket) │      │   (锁控MCU)  │  │
│  └──────┬───────┘      └──────┬───────┘      └──────────────┘  │
│         │                     │                                 │
│         │ JPEG                │ 视频帧/音频                     │
│         └────────────────────►│                                 │
└───────────────────────────────┼─────────────────────────────────┘
                                │ WebSocket
                                ▼
                        ┌──────────────┐
                        │    Server    │
                        │ (人脸识别)   │
                        └──────────────┘
```

---

## 核心模块

### 1. Application

**职责**:

- 应用主控制器
- 状态机管理
- 模式切换（Idle/Listening/Speaking/Monitoring）
- 事件分发

**关键方法**:

```cpp
class Application {
public:
    static Application& GetInstance();

    void Initialize();
    void Loop();

    // 状态管理
    DeviceState GetDeviceState() const;
    void SetDeviceState(DeviceState state);

    // 监控模式
    bool StartMonitorMode();
    void StopMonitorMode();
    bool IsMonitorMode() const;

    // 本地预览
    bool StartLocalPreview();
    void StopLocalPreview();

    // 人脸识别
    void TriggerFaceRecognition();
    void HandleFaceRecognitionResult(cJSON* root);
};
```

### 2. Board

**职责**:

- 硬件抽象层
- 提供统一的硬件访问接口
- 隔离硬件差异

**关键方法**:

```cpp
class Board {
public:
    static Board* GetInstance();

    virtual AudioCodec* GetAudioCodec() = 0;
    virtual Display* GetDisplay() = 0;
    virtual Led* GetLed() = 0;
    virtual Network* GetNetwork() = 0;
    virtual Camera* GetCamera() = 0;
    virtual LockControlService* GetLockControl() = 0;
};
```

### 3. Protocol

**职责**:

- 通信协议抽象
- WebSocket/MQTT 实现
- 消息编解码

**关键方法**:

```cpp
class Protocol {
public:
    // 音频
    virtual void SendAudio(const uint8_t* data, size_t size) = 0;

    // 视频
    virtual void SendVideo(const uint8_t* data, size_t size,
                          uint16_t width, uint16_t height) = 0;

    // 人脸识别
    virtual void SendFaceRecognition(const uint8_t* data, size_t size) = 0;

    // v5.0 协议扩展
    virtual void SendAck(const std::string& msg_id, int code,
                        const std::string& msg) = 0;
    virtual void SendStatusReport(...) = 0;
    virtual void SendEventReport(...) = 0;
    virtual void SendLogReport(...) = 0;
};
```

### 4. LockControlService

**职责**:

- ESP32 ↔ STM32 UART 通信
- 锁控命令发送
- 事件接收和处理

**关键方法**:

```cpp
class LockControlService {
public:
    bool Start(uart_port_t port, int tx_pin, int rx_pin);
    void Stop();

    // 控制命令
    bool SendUnlock();
    bool SendLock();
    bool SendBeep(uint8_t count, BeepFreq freq);
    bool SendOledIcon(OledIcon icon);
    bool SendLight(LightMode mode);

    // 用户管理
    bool FingerprintEnroll(uint8_t user_id);
    bool FingerprintDelete(uint8_t user_id);
    bool NfcEnroll(uint8_t user_id);
    bool SetPassword(uint32_t password);

    // 查询
    bool QuerySensors();
    bool QueryStatus();

    // 事件回调
    void SetEventCallback(EventCallback callback);
};
```

### 5. MonitorService

**职责**:

- 监控模式管理
- 视频流捕获和发送
- 音频双向传输

**关键方法**:

```cpp
class MonitorService {
public:
    bool Start();
    void Stop();
    bool IsRunning() const;
};
```

### 6. LocalPreviewService

**职责**:

- 本地预览模式管理
- 摄像头画面显示到 LCD

**关键方法**:

```cpp
class LocalPreviewService {
public:
    bool Start();
    void Stop();
    bool IsRunning() const;
};
```

---

## 数据流

### 1. 人脸识别流程

```
STM32 (门铃/PIR)
  │
  │ UART: RPT_EVENT
  ▼
LockControlService
  │
  │ 事件回调
  ▼
Application::HandleLockEvent()
  │
  │ 触发人脸识别
  ▼
Application::TriggerFaceRecognition()
  │
  │ 拍照 + JPEG 编码
  ▼
Protocol::SendFaceRecognition()
  │
  │ WebSocket Binary
  ▼
Server (AI 识别)
  │
  │ JSON: face_result
  ▼
Application::HandleFaceRecognitionResult()
  │
  │ 判断是否开锁
  ▼
LockControlService::SendUnlock()
  │
  │ UART: CMD_LOCK
  ▼
STM32 (执行开锁)
```

### 2. 监控模式流程

```
Server
  │
  │ JSON: start_monitor
  ▼
Application::StartMonitorMode()
  │
  │ 创建 MonitorService
  ▼
MonitorService::Start()
  │
  │ 创建 VideoStreamService
  ▼
VideoStreamService::CaptureTask()
  │
  │ 循环捕获帧
  ▼
Camera::CaptureForStream()
  │
  │ JPEG 编码
  ▼
FrameQueue::Push()
  │
  │ 帧队列
  ▼
MonitorService::SendTask()
  │
  │ 从队列获取帧
  ▼
Protocol::SendVideo()
  │
  │ WebSocket Binary
  ▼
Server
```

---

## 状态机

### 设备状态

```
kDeviceStateUnknown
  │
  ▼
kDeviceStateStarting
  │
  ├─> kDeviceStateWifiConfiguring
  │
  ├─> kDeviceStateActivating
  │     │
  │     ├─> kDeviceStateUpgrading
  │     │
  │     └─> kDeviceStateIdle
  │
  └─> kDeviceStateIdle
        │
        ├─> kDeviceStateConnecting
        │     │
        │     └─> kDeviceStateListening
        │           │
        │           └─> kDeviceStateSpeaking
        │
        └─> kDeviceStateMonitoring
```

---

## 线程模型

### 主要任务

| 任务               | 优先级 | 核心 | 说明      |
| ------------------ | ------ | ---- | --------- |
| main_task          | 1      | 0    | 主循环    |
| audio_input_task   | 5      | 1    | 音频输入  |
| audio_output_task  | 5      | 1    | 音频输出  |
| lock_rx_task       | 5      | 1    | UART 接收 |
| video_capture_task | 5      | 1    | 视频捕获  |
| video_send_task    | 5      | 1    | 视频发送  |
| preview_task       | 5      | 1    | 本地预览  |

---

## 内存管理

### 内存分配策略

- **PSRAM**: 用于大块数据（JPEG、音频缓冲区）
- **DRAM**: 用于小块数据和频繁访问的数据
- **IRAM**: 用于中断处理函数

### 内存使用

| 模块       | PSRAM | DRAM | 说明                  |
| ---------- | ----- | ---- | --------------------- |
| JPEG 数据  | ✅    | ❌   | 使用 heap_caps_malloc |
| 音频缓冲区 | ✅    | ❌   | 使用 heap_caps_malloc |
| 帧队列     | ✅    | ❌   | 使用 PSRAM            |
| 协议栈     | ❌    | ✅   | 使用 DRAM             |

---

## 互斥关系

### 功能互斥

| 功能     | 监控模式 | 本地预览 | 人脸识别 |
| -------- | -------- | -------- | -------- |
| 监控模式 | -        | ❌       | ❌       |
| 本地预览 | ❌       | -        | ❌       |
| 人脸识别 | ❌       | ❌       | -        |

### 互斥实现

```cpp
// 检查互斥条件
if (IsMonitorMode()) {
    ESP_LOGW(TAG, "监控模式运行中，无法启动其他功能");
    return false;
}

if (local_preview_running_) {
    ESP_LOGW(TAG, "本地预览运行中，无法启动其他功能");
    return false;
}

if (face_recognition_in_progress_) {
    ESP_LOGW(TAG, "人脸识别运行中，无法启动其他功能");
    return false;
}
```

---

## 相关文档

- [硬件抽象层](hardware-abstraction.md)
- [通信协议](../protocols/esp32-server-v5.2.md)
- [功能模块](../features/)

---

**最后更新**: 2026-05-05
