# 人脸识别门禁功能

**状态**: ✅ 已完成  
**最后更新**: 2025-12-12  
**代码位置**: `main/lock_control/`, `main/application.cc`

---

## 功能概述

人脸识别门禁是智能猫眼门锁系统的核心功能之一，实现了基于人脸识别的自动开锁流程。

### 核心特性

- ✅ 门铃/PIR 触发自动拍照
- ✅ 服务器端人脸识别
- ✅ 识别结果自动开锁
- ✅ TTS 语音反馈
- ✅ 开锁日志上报

---

## 工作流程

```
┌─────────┐         ┌─────────┐         ┌─────────┐
│  STM32  │         │  ESP32  │         │ Server  │
└────┬────┘         └────┬────┘         └────┬────┘
     │                   │                   │
     │ UART: RPT_EVENT   │                   │
     │ (门铃/PIR)        │                   │
     │──────────────────>│                   │
     │                   │                   │
     │                   │ Binary: type=2    │
     │                   │ (JPEG 人脸图像)    │
     │                   │──────────────────>│
     │                   │                   │
     │                   │                   │ AI 识别
     │                   │                   │
     │                   │ JSON: face_result │
     │                   │<──────────────────│
     │                   │                   │
     │ UART: CMD_LOCK    │                   │
     │ (开锁)            │                   │
     │<──────────────────│                   │
     │                   │                   │
     │ UART: ACK_OK      │                   │
     │──────────────────>│                   │
     │                   │                   │
     │ UART: RPT_UNLOCK  │                   │
     │ (开锁日志)        │                   │
     │──────────────────>│                   │
     │                   │                   │
     │                   │ JSON: log_report  │
     │                   │──────────────────>│
```

---

## 触发条件

人脸识别可由以下事件触发：

| 事件     | STM32 事件ID          | 说明                     |
| -------- | --------------------- | ------------------------ |
| 门铃按下 | `EVT_DOORBELL (0x01)` | 用户按下门铃按钮         |
| PIR 检测 | `EVT_PIR (0x02)`      | 人体红外传感器检测到人体 |

---

## 实现细节

### 1. 事件接收

**代码位置**: `main/lock_control/lock_control.cc`

```cpp
void LockControlService::RxLoop() {
    // 接收 UART 消息
    LockMessage msg;
    if (LockProtocol::ParseMessage(buffer, msg)) {
        // 触发事件回调
        if (event_callback_) {
            event_callback_(msg);
        }
    }
}
```

### 2. 触发人脸识别

**代码位置**: `main/application.cc`

```cpp
void Application::TriggerFaceRecognition() {
    // 1. 状态检查
    if (face_recognition_in_progress_) {
        ESP_LOGW(TAG, "人脸识别正在进行中，忽略新的触发");
        return;
    }

    // 2. 检查设备状态
    if (device_state_ != kDeviceStateIdle &&
        device_state_ != kDeviceStateListening) {
        ESP_LOGW(TAG, "设备状态不允许人脸识别");
        return;
    }

    // 3. 检查摄像头
    auto camera = board_->GetCamera();
    if (!camera || !camera->IsAvailable()) {
        ESP_LOGE(TAG, "摄像头不可用");
        return;
    }

    // 4. 检查内存
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < 100 * 1024) {
        ESP_LOGE(TAG, "内存不足");
        return;
    }

    // 5. 拍照
    face_recognition_in_progress_ = true;
    if (!camera->Capture()) {
        ESP_LOGE(TAG, "拍照失败");
        face_recognition_in_progress_ = false;
        return;
    }

    // 6. JPEG 编码
    uint8_t* jpeg_data = nullptr;
    size_t jpeg_size = 0;
    if (!camera->CaptureJpeg(&jpeg_data, &jpeg_size, 80)) {
        ESP_LOGE(TAG, "JPEG 编码失败");
        face_recognition_in_progress_ = false;
        return;
    }

    // 7. 发送到服务器
    protocol_->SendFaceRecognition(jpeg_data, jpeg_size);

    // 8. 释放内存
    heap_caps_free(jpeg_data);
    face_recognition_in_progress_ = false;
}
```

### 3. 处理识别结果

**代码位置**: `main/application.cc`

```cpp
void Application::HandleFaceRecognitionResult(cJSON* root) {
    // 解析结果
    auto result = cJSON_GetObjectItem(root, "result");
    auto access = cJSON_GetObjectItem(root, "access");
    auto granted = cJSON_GetObjectItem(access, "granted");
    auto user_id = cJSON_GetObjectItem(root, "user_id");

    // 判断是否开锁
    if (cJSON_IsString(result) &&
        strcmp(result->valuestring, "known") == 0 &&
        cJSON_IsBool(granted) &&
        cJSON_IsTrue(granted)) {

        // 保存用户ID（用于后续开锁日志）
        if (cJSON_IsNumber(user_id)) {
            last_face_user_id_ = user_id->valueint;
        }

        // 发送开锁命令
        auto lock_control = board_->GetLockControl();
        if (lock_control) {
            lock_control->SendUnlock();
        }
    } else {
        // 识别失败，清除用户ID
        last_face_user_id_ = 0;
    }
}
```

### 4. 开锁日志上报

**代码位置**: `main/application.cc`

```cpp
void Application::HandleLockReportMessage(const LockMessage& msg) {
    if (msg.type == static_cast<uint8_t>(RptType::RPT_UNLOCK)) {
        // 解析开锁方式
        uint8_t method = msg.data[0];
        uint8_t uid = msg.data[1];

        // 人脸开锁特殊处理
        if (method == static_cast<uint8_t>(UnlockMethod::UNLOCK_FACE)) {
            // 使用缓存的用户ID
            uid = last_face_user_id_;
        }

        // 上报服务器
        protocol_->SendLogReport(
            GetUnlockMethodString(method),
            uid,
            true,  // 成功
            0      // 失败次数
        );
    }
}
```

---

## 协议格式

### 1. 人脸识别图像发送

**格式**: BinaryProtocol2

```c
struct BinaryProtocol2 {
    uint16_t version;      // 协议版本 = 2
    uint16_t type;         // 消息类型 = 2 (人脸识别)
    uint32_t reserved;     // 分辨率 (width << 16) | height
    uint32_t timestamp;    // 时间戳（毫秒）
    uint32_t payload_size; // JPEG 大小
    uint8_t payload[];     // JPEG 数据
};
```

### 2. 识别结果（服务器 → ESP32）

```json
{
  "type": "face_result",
  "result": "known",
  "user_id": 5,
  "access": {
    "granted": true,
    "reason": "authorized_user"
  }
}
```

**字段说明**:

| 字段           | 类型   | 说明                                |
| -------------- | ------ | ----------------------------------- |
| result         | string | `known`/`unknown`/`no_face`/`error` |
| user_id        | int    | 用户ID（known 时有效）              |
| access.granted | bool   | 是否授权开锁                        |
| access.reason  | string | 授权/拒绝原因                       |

### 3. 开锁日志上报（ESP32 → 服务器）

```json
{
  "type": "log_report",
  "ts": 1702234567890,
  "data": {
    "method": "face",
    "status": "success",
    "uid": 5,
    "fail_count": 0,
    "lock_time": 0
  }
}
```

---

## 性能指标

| 指标           | 目标值   | 实际值     |
| -------------- | -------- | ---------- |
| 触发到拍照     | <100ms   | ~50ms      |
| JPEG 编码      | <500ms   | ~300ms     |
| 网络传输       | <100ms   | ~80ms      |
| 服务器识别     | <1000ms  | ~800ms     |
| 开锁命令发送   | <100ms   | ~50ms      |
| **总响应时间** | **<2秒** | **~1.3秒** |

---

## 错误处理

| 错误场景           | 处理方式                 |
| ------------------ | ------------------------ |
| 摄像头不可用       | 记录错误，中止流程       |
| 内存不足 (<100KB)  | 拒绝新的识别请求         |
| 拍照失败           | 记录错误，清除进行中标志 |
| JPEG 编码失败      | 释放内存，中止流程       |
| 网络发送失败       | 释放内存，中止流程       |
| 识别结果为 unknown | 不开锁，播放拒绝语音     |
| 监控模式运行中     | 忽略触发事件             |
| 识别正在进行中     | 忽略新的触发事件         |

---

## 状态保护

### 防止重复触发

```cpp
// 使用标志位防止重复触发
bool face_recognition_in_progress_ = false;

void TriggerFaceRecognition() {
    if (face_recognition_in_progress_) {
        return;  // 忽略
    }
    face_recognition_in_progress_ = true;

    // ... 执行识别流程 ...

    face_recognition_in_progress_ = false;
}
```

### 与监控模式互斥

```cpp
void HandleLockEvent(const LockMessage& msg) {
    // 监控模式中忽略锁控事件
    if (IsMonitorMode()) {
        ESP_LOGW(TAG, "监控模式中，忽略锁控事件");
        return;
    }

    // 处理事件...
}
```

---

## 配置选项

### 摄像头配置

**位置**: `main/boards/bread-compact-wifi-s3cam/config.h`

```c
#define CAMERA_FRAME_SIZE FRAMESIZE_VGA  // 640x480
#define CAMERA_JPEG_QUALITY 80           // JPEG 质量
```

### UART 配置

**位置**: `main/boards/bread-compact-wifi-s3cam/config.h`

```c
#define LOCK_UART_PORT      UART_NUM_1
#define LOCK_UART_TX_PIN    GPIO_NUM_3
#define LOCK_UART_RX_PIN    GPIO_NUM_14
#define LOCK_UART_BAUD_RATE 9600
```

---

## 调试技巧

### 1. 查看 UART 通信

```bash
# 监控日志，过滤锁控相关
idf.py monitor | grep "LockControl"
```

### 2. 查看人脸识别流程

```bash
# 监控日志，过滤人脸识别
idf.py monitor | grep "Face"
```

### 3. 查看内存使用

```cpp
ESP_LOGI(TAG, "PSRAM 剩余: %d KB",
    heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
```

---

## 相关文档

- [ESP32与STM32锁控协议](../protocols/esp32-stm32-v2.0.md)
- [ESP32与服务器通信协议](../protocols/esp32-server-v5.2.md)
- [锁控系统](lock-control.md)
- [变更日志](../reference/CHANGELOG.md)

---

**最后更新**: 2025-12-12
