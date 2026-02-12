# ESP32 人脸识别通信流程分析

## 文档信息

| 项目 | 内容 |
|-----|------|
| **文档版本** | 1.0 |
| **创建日期** | 2026-01-17 |
| **协议版本** | v5.2 |
| **适用项目** | xiaozhi-esp32 智能猫眼门禁系统 |

---

## 一、整体架构

### 1.1 三层通信架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         通信层级                                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  第一层：STM32 ↔ ESP32 (UART 串口通信)                          │
│  ├─ 协议：7字节固定长度帧                                        │
│  ├─ 波特率：9600                                                 │
│  └─ 用途：事件触发、锁控命令、状态查询                           │
│                                                                  │
│  第二层：ESP32 ↔ 服务器 (WebSocket 通信)                        │
│  ├─ 协议：JSON (信令) + BinaryProtocol2 (数据流)                │
│  ├─ 用途：人脸识别、监控模式、远程控制                           │
│  └─ 可靠性：两级确认机制 (esp32_ack + ack)                      │
│                                                                  │
│  第三层：服务器 ↔ App (HTTP/WebSocket)                          │
│  └─ 用途：用户界面、远程控制、数据存储                           │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 核心模块关系

```
Application (应用主控)
    │
    ├─── LockControlService (锁控服务)
    │       └─── UART Driver (串口驱动)
    │               └─── STM32C8T6 (锁控MCU)
    │
    ├─── Esp32Camera (摄像头)
    │       └─── OV2640 (摄像头模块)
    │
    └─── Protocol (通信协议)
            ├─── WebsocketProtocol (WebSocket实现)
            └─── MqttProtocol (MQTT实现)
```

---

## 二、人脸识别完整流程

### 2.1 流程概览


```
┌─────────┐         ┌─────────┐         ┌─────────┐
│  STM32  │         │  ESP32  │         │ Server  │
└────┬────┘         └────┬────┘         └────┬────┘
     │                   │                   │
     │ ① 门铃/PIR触发    │                   │
     │──────────────────>│                   │
     │                   │                   │
     │                   │ ② 拍照+编码       │
     │                   │                   │
     │                   │ ③ 发送JPEG图像    │
     │                   │──────────────────>│
     │                   │                   │
     │                   │                   │ ④ AI识别
     │                   │                   │
     │                   │ ⑤ 返回识别结果    │
     │                   │<──────────────────│
     │                   │                   │
     │ ⑥ 开锁命令        │                   │
     │<──────────────────│                   │
     │                   │                   │
     │ ⑦ ACK_OK          │                   │
     │──────────────────>│                   │
     │                   │                   │
     │ ⑧ 开锁日志上报    │                   │
     │──────────────────>│                   │
     │                   │                   │
     │                   │ ⑨ log_report      │
     │                   │──────────────────>│
```

### 2.2 详细步骤说明

#### 步骤 ① STM32 触发事件 (UART)

**触发条件：**
- 门铃按下 (DOORBELL_PRESSED)
- PIR 人体检测 (HUMAN_DETECTED)

**UART 消息格式：**
```
[0xAA][0x01][0x01/0x02][0xFF][0xFF][0xFF][CHK]
  │     │      │          └──────┬──────┘   │
  帧头  类别   事件类型      数据域      校验和
       (RPT)  (门铃/PIR)
```

**代码路径：**
```cpp
// 接收处理
LockControlService::RxLoop()
  └─> LockProtocol::ParseMessage()
      └─> Application::HandleLockEvent()
          └─> Application::HandleLockReportMessage()
              └─> Application::TriggerFaceRecognition()
```

#### 步骤 ② ESP32 拍照与编码

**关键检查：**
```cpp
// Application::TriggerFaceRecognition()
1. 检查设备状态 (必须为 Idle 或 Listening)
2. 检查摄像头可用性
3. 检查内存 (PSRAM > 100KB)
4. 检查是否已在识别中 (防止重复触发)
```

**拍照流程：**
```cpp
Esp32Camera::Capture()
  ├─ 取3帧保留最后一帧 (提高质量)
  ├─ 显示预览 (可选)
  ├─ 支持旋转 (根据配置)
  └─ 耗时约 150ms
```

**JPEG 编码：**
```cpp
Esp32Camera::CaptureJpeg()
  ├─ 质量参数：80
  ├─ 分辨率：640×480
  ├─ 内存分配：使用 PSRAM
  └─ 单帧大小：约 15-30KB
```

#### 步骤 ③ 发送图像到服务器 (WebSocket Binary)

**协议格式：BinaryProtocol2**
```c
struct BinaryProtocol2 {
    uint16_t version;      // 固定为 2
    uint16_t type;         // 固定为 2 (人脸识别)
    uint32_t reserved;     // (width << 16) | height
    uint32_t timestamp;    // 时间戳 (毫秒)
    uint32_t payload_size; // JPEG 数据大小
    uint8_t payload[];     // JPEG 数据
};
```

**示例数据：**
```
version:      0x0002
type:         0x0002
reserved:     0x028001E0  (640×480)
timestamp:    0x65A1B2C3
payload_size: 0x00003A98  (15000 字节)
payload:      [JPEG 数据...]
```

**代码路径：**
```cpp
WebsocketProtocol::SendFaceRecognition()
  └─> WebSocket::SendBinary()
```

#### 步骤 ④ 服务器 AI 识别

**服务器处理：**
1. 接收 JPEG 图像
2. 人脸检测
3. 特征提取
4. 数据库比对
5. 生成识别结果
6. 生成 TTS 语音反馈

#### 步骤 ⑤ 服务器返回识别结果 (WebSocket JSON)

**消息格式：**
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

> **注意：** face_result 不需要 seq_id，因为它是服务器主动推送的识别结果，不是用户发起的命令。开锁结果通过 log_report 上报。

**result 取值：**
| result | 说明 | ESP32 处理 |
|--------|------|-----------|
| `known` | 识别成功 | 检查 access.granted 决定是否开锁 |
| `unknown` | 陌生人 | 不开锁，播放拒绝语音 |
| `no_face` | 未检测到人脸 | 不开锁，播放提示语音 |
| `error` | 识别错误 | 不开锁，记录错误 |

**代码路径：**
```cpp
WebsocketProtocol::OnData()
  └─> Application::HandleFaceRecognitionResult()
```


#### 步骤 ⑥ ESP32 处理识别结果

**处理逻辑：**
```cpp
Application::HandleFaceRecognitionResult()
  ├─ 解析 result 和 access.granted
  ├─ 如果 result="known" && granted=true
  │   └─ 发送开锁命令到 STM32
  └─ 播放 TTS 语音反馈
```

> **注意：** 虽然代码中实现了 esp32_ack 和 ack（v5.2 新增），但从协议设计角度，face_result 不需要两级确认，因为它是服务器主动推送的结果，不是命令。

**代码路径：**
```cpp
Application::HandleFaceRecognitionResult()
```

#### 步骤 ⑦ ESP32 发送开锁命令到 STM32 (UART)

**条件判断：**
```cpp
if (result == "known" && access.granted == true) {
    lock_control_->SendUnlock(30);  // 开锁30秒
}
```

**UART 消息格式：**
```
[0xAA][0x02][0x10][0x01][0x1E][0xFF][CHK]
  │     │     │     │     │     │     │
  帧头  类别  类型  开锁  30秒  空   校验和
       (CMD) (LOCK)(UNLOCK)
```

**代码路径：**
```cpp
LockControlService::SendUnlock()
  └─> LockControlService::SendLock(UNLOCK, 30)
      └─> LockControlService::SendMessage()
          └─> uart_write_bytes()
```

#### 步骤 ⑧ STM32 返回 ACK (UART)

**UART 消息格式：**
```
[0xAA][0x00][0x01][0x10][0xFF][0xFF][CHK]
  │     │     │     │     └──┬──┘   │
  帧头  类别  类型  原TYPE  空     校验和
       (SYS) (ACK_OK)
```

**代码路径：**
```cpp
LockControlService::RxLoop()
  └─> LockProtocol::ParseMessage()
      └─> Application::HandleLockSystemMessage()
```

#### 步骤 ⑨ STM32 上报开锁日志 (UART)

**UART 消息格式：**
```
[0xAA][0x01][0xA1][0x07][0x05][0x00][CHK]
  │     │     │     │     │     │     │
  帧头  类别  类型  方式  UID  成功  校验和
       (RPT) (UNLOCK)(FACE)
```

**代码路径：**
```cpp
LockControlService::RxLoop()
  └─> Application::HandleLockReportMessage()
      └─> WebsocketProtocol::SendLogReport()
```

#### 步骤 ⑩ ESP32 上报开锁日志到服务器 (WebSocket JSON)

**消息格式：**
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

## 三、UART 通信协议详解

### 3.1 协议帧结构

```
+------+------+------+------+------+------+------+
| 帧头 | 类别 | 类型 | D0   | D1   | D2   | 校验 |
+------+------+------+------+------+------+------+
| 0xAA | CAT  | TYPE | DATA[0-2]          | CHK  |
+------+------+------+------+------+------+------+
  1字节  1字节  1字节  3字节              1字节

校验和 = (CAT + TYPE + D0 + D1 + D2) & 0xFF
```

### 3.2 消息类别 (CAT)

| CAT | 名称 | 方向 | 说明 |
|-----|------|------|------|
| 0x00 | SYS | 双向 | 系统握手 (ACK、心跳) |
| 0x01 | RPT | STM32→ESP32 | 状态上报 (事件、传感器) |
| 0x02 | CMD | ESP32→STM32 | 控制命令 (开锁、蜂鸣器) |
| 0x03 | USER | 双向 | 用户管理 (指纹、NFC、密码) |

### 3.3 人脸识别相关消息

#### 3.3.1 事件上报 (STM32 → ESP32)

**门铃按下：**
```
[0xAA][0x01][0xA0][0x01][0xFF][0xFF][CHK]
              └─┬─┘ └─┬─┘
            RPT_EVENT  DOORBELL
```

**PIR 人体检测：**
```
[0xAA][0x01][0xA0][0x02][持续时间][0xFF][CHK]
              └─┬─┘ └─┬─┘
            RPT_EVENT  PIR
```

#### 3.3.2 开锁命令 (ESP32 → STM32)

```
[0xAA][0x02][0x10][0x01][保持时间][0xFF][CHK]
       └─┬─┘ └─┬─┘ └─┬─┘
        CMD  CMD_LOCK UNLOCK
```

**保持时间说明：**
- 单位：秒
- 0 = 使用默认值 (3分钟)
- 最大值：255 秒

#### 3.3.3 开锁日志上报 (STM32 → ESP32)

```
[0xAA][0x01][0xA1][方式][UID][结果][CHK]
       └─┬─┘ └─┬─┘
        RPT  RPT_UNLOCK
```

**方式 (D0) 枚举：**
| 值 | 说明 |
|----|------|
| 0x01 | 指纹 |
| 0x02 | NFC |
| 0x03 | 密码 |
| 0x04 | 远程 |
| 0x05 | 钥匙 |
| 0x06 | 临时密码 |
| 0x07 | 人脸 |

**结果 (D2) 枚举：**
| 值 | 说明 |
|----|------|
| 0x00 | 成功 |
| 0x01-0x05 | 失败 1-5 次 |
| 0x06 | 已锁定 (D1=剩余锁定时间) |

---

## 四、WebSocket 通信协议详解

### 4.1 BinaryProtocol2 格式

**用于：** 音频流、视频流、人脸识别图像

```c
struct BinaryProtocol2 {
    uint16_t version;      // 协议版本 (网络字节序)
    uint16_t type;         // 消息类型
    uint32_t reserved;     // 扩展字段
    uint32_t timestamp;    // 时间戳 (毫秒)
    uint32_t payload_size; // 载荷大小
    uint8_t payload[];     // 载荷数据
} __attribute__((packed));
```

**消息类型区分：**
| type | reserved | 数据类型 | 载荷格式 |
|------|----------|----------|----------|
| 0 | 0 | 音频流 | OPUS 编码 |
| 0 | 非0 | 监控视频流 | JPEG 编码 |
| 2 | 非0 | 人脸识别图像 | JPEG 编码 |

**reserved 字段编码 (视频)：**
```c
reserved = (width << 16) | height
// 示例：640×480 = 0x028001E0
```

### 4.2 JSON 信令格式

#### 4.2.1 人脸识别结果 (Server → Device)

```json
{
    "type": "face_result",
    "seq_id": "1702234567890_0",
    "result": "known",
    "user_id": 5,
    "access": {
        "granted": true,
        "reason": "authorized_user"
    }
}
```

#### 4.2.2 两级确认机制

**第一级：esp32_ack (Device → Server)**
```json
{
    "type": "esp32_ack",
    "seq_id": "1702234567890_0",
    "code": 0,
    "msg": "received"
}
```

**第二级：ack (Device → Server)**
```json
{
    "type": "ack",
    "seq_id": "1702234567890_0",
    "code": 0,
    "msg": "OK"
}
```

**seq_id 说明：**
- 格式：字符串，由 App 生成
- 推荐格式：`时间戳_序号` (如 `1702234567890_0`)
- 用途：消息追溯、防重放攻击
- 缓存：ESP32 维护最近 100 条 seq_id


#### 4.2.3 开锁日志上报 (Device → Server)

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

**status 取值：**
| status | 说明 | uid | fail_count | lock_time |
|--------|------|-----|------------|-----------|
| `success` | 开锁成功 | 用户ID | 0 | 0 |
| `fail` | 认证失败 | 用户ID/0xFF | 1-5 | 0 |
| `locked` | 设备锁定 | 无意义 | 0 | 剩余分钟数 |

---

## 五、关键代码路径

### 5.1 Application 类 (应用主控)

**文件：** `main/application.cc` / `main/application.h`

**核心方法：**
```cpp
// 处理锁控事件
void HandleLockEvent(const LockMessage& msg);

// 触发人脸识别
void TriggerFaceRecognition();

// 处理识别结果
void HandleFaceRecognitionResult(cJSON* root);

// 处理上报消息
void HandleLockReportMessage(const LockMessage& msg);

// 处理系统消息 (ACK)
void HandleLockSystemMessage(const LockMessage& msg);
```

**状态检查：**
```cpp
// TriggerFaceRecognition() 中的检查
1. device_state_ == kDeviceStateIdle || 
   device_state_ == kDeviceStateListening
2. camera->IsAvailable()
3. heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 100*1024
4. !face_recognition_in_progress_
```

### 5.2 LockControlService 类 (锁控服务)

**文件：** `main/lock_control/lock_control.cc` / `lock_control.h`

**核心方法：**
```cpp
// 启动服务
bool Start(uart_port_t port, int tx_pin, int rx_pin);

// 发送开锁命令
bool SendUnlock(uint8_t hold_seconds = 0);

// 设置事件回调
void SetEventCallback(EventCallback callback);

// 接收循环 (私有)
void RxLoop();
```

**UART 配置：**
```cpp
波特率: 9600
数据位: 8
校验位: 无
停止位: 1
端口: UART_NUM_1
TX引脚: GPIO_NUM_3
RX引脚: GPIO_NUM_14
```

### 5.3 LockProtocol 类 (协议编解码)

**文件：** `main/lock_control/lock_protocol.cc` / `lock_protocol.h`

**核心方法：**
```cpp
// 构建消息
static std::vector<uint8_t> BuildMessage(
    uint8_t cat, uint8_t type, 
    const std::array<uint8_t, 3>& data
);

// 解析消息
static LockMessage ParseMessage(
    const uint8_t* data, size_t len
);

// 计算校验和
static uint8_t CalculateChecksum(
    uint8_t cat, uint8_t type, 
    const uint8_t* data
);
```

### 5.4 WebsocketProtocol 类 (WebSocket 协议)

**文件：** `main/protocols/websocket_protocol.cc` / `websocket_protocol.h`

**核心方法：**
```cpp
// 发送人脸识别图像
bool SendFaceRecognition(
    const uint8_t* jpeg_data, size_t jpeg_size,
    uint16_t width, uint16_t height
);

// 发送第一级确认
void SendEsp32Ack(
    const std::string& seq_id, 
    int code = 0, 
    const std::string& msg = "received"
);

// 发送第二级确认
void SendAck(
    const std::string& seq_id, 
    int code = 0, 
    const std::string& msg = "OK"
);

// 发送开锁日志
void SendLogReport(
    const std::string& method, 
    const std::string& status,
    int uid, int fail_count = 0, 
    int lock_time = 0
);
```

### 5.5 Esp32Camera 类 (摄像头)

**文件：** `main/boards/common/esp32_camera.cc` / `esp32_camera.h`

**核心方法：**
```cpp
// 正常模式拍照 (人脸识别用)
bool Capture();

// 监控模式捕获 (视频流用)
bool CaptureForStream();

// JPEG 编码
bool CaptureJpeg(
    uint8_t** jpeg_data, 
    size_t* jpeg_size, 
    int quality = 80
);
```

**两种模式对比：**
| 模式 | 函数 | 帧数 | 预览 | 旋转 | 耗时 | 用途 |
|------|------|------|------|------|------|------|
| 正常 | `Capture()` | 3帧取最后 | ✅ | ✅ | ~150ms | 人脸识别 |
| 监控 | `CaptureForStream()` | 1帧 | ❌ | ❌ | ~40ms | 视频流 |

---

## 六、错误处理机制

### 6.1 UART 通信错误

| 错误场景 | 处理方式 | 代码位置 |
|----------|----------|----------|
| 发送失败 | 重试3次，间隔10ms | `SendMessage()` |
| 接收超时 | 1秒超时后丢弃不完整消息 | `RxLoop()` |
| 校验和错误 | 丢弃消息，记录日志 | `ParseMessage()` |
| 缓冲区溢出 | 重置状态，重新同步 | `RxLoop()` |

### 6.2 人脸识别错误

| 错误场景 | 处理方式 | 代码位置 |
|----------|----------|----------|
| 设备状态不对 | 忽略触发事件 | `TriggerFaceRecognition()` |
| 摄像头不可用 | 记录错误，中止流程 | `TriggerFaceRecognition()` |
| 内存不足 (<100KB) | 拒绝请求，记录日志 | `TriggerFaceRecognition()` |
| JPEG 编码失败 | 释放内存，中止流程 | `TriggerFaceRecognition()` |
| 网络发送失败 | 释放内存，中止流程 | `SendFaceRecognition()` |
| 监控模式冲突 | 忽略触发事件 | `HandleLockEvent()` |

### 6.3 WebSocket 通信错误

| 错误场景 | 处理方式 | 代码位置 |
|----------|----------|----------|
| seq_id 重复 | 忽略消息 (防重放) | `IsDuplicateMsgId()` |
| JSON 解析失败 | 丢弃消息，记录日志 | `OnData()` |
| 协议版本不匹配 | 拒绝连接 | `ParseServerHello()` |
| 连接断开 | 自动重连 | `Start()` |

### 6.4 STM32 错误码映射

| STM32 错误码 | 宏定义 | 统一 code | 说明 |
|--------------|--------|-----------|------|
| 0x01 | ERR_BUSY | 2 | 设备忙 |
| 0x02 | ERR_UNSUPPORT | 4 | 不支持 |
| 0x03 | ERR_PARAM | 3 | 参数错误 |
| 0x04 | ERR_FP_FULL | 7 | 指纹库已满 |
| 0x05 | ERR_NFC_FULL | 7 | NFC 库已满 |
| 0x06 | ERR_HARDWARE | 6 | 硬件故障 |
| 0xFF | ERR_TIMEOUT | 5 | 超时 |

**代码实现：**
```cpp
int Application::MapStm32ErrorCode(uint8_t stm32_err) {
    switch (stm32_err) {
        case 0x01: return 2;  // ERR_BUSY
        case 0x02: return 4;  // ERR_UNSUPPORT
        case 0x03: return 3;  // ERR_PARAM
        case 0x04:
        case 0x05: return 7;  // ERR_FP_FULL / ERR_NFC_FULL
        case 0x06: return 6;  // ERR_HARDWARE
        case 0xFF: return 5;  // ERR_TIMEOUT
        default:   return 10; // 内部错误
    }
}
```

---

## 七、性能指标

### 7.1 人脸识别响应时间

| 阶段 | 目标时间 | 实际测量 |
|------|---------|----------|
| 触发到拍照 | <100ms | 待测试 |
| JPEG 编码 | <500ms | 待测试 |
| 网络传输 | <100ms | 取决于网络 |
| 服务器识别 | <1000ms | 取决于服务器 |
| 开锁命令发送 | <100ms | 待测试 |
| **总计** | **<1800ms** | **待测试** |

### 7.2 UART 通信性能

| 指标 | 数值 |
|------|------|
| 波特率 | 9600 bps |
| 单帧大小 | 7 字节 |
| 传输时间 | ~7.3 ms |
| ACK 响应时间 | <100 ms |
| 超时时间 | 1000 ms |

### 7.3 图像传输性能

| 指标 | 数值 |
|------|------|
| 分辨率 | 640×480 |
| JPEG 质量 | 80 |
| 单帧大小 | 15-30 KB |
| 协议开销 | 16 字节 |
| 传输时间 | 取决于网络带宽 |

### 7.4 内存使用

| 项目 | 大小 |
|------|------|
| JPEG 缓冲区 | ~30 KB (PSRAM) |
| UART 接收缓冲区 | 256 字节 |
| UART 发送缓冲区 | 256 字节 |
| WebSocket 缓冲区 | 取决于实现 |
| 最低可用 PSRAM | 100 KB |

---

## 八、调试与测试

### 8.1 日志输出

**关键日志标签：**
```cpp
TAG = "LockControl"   // 锁控服务
TAG = "Application"   // 应用主控
TAG = "WebSocket"     // WebSocket 协议
TAG = "Camera"        // 摄像头
```

**日志级别设置：**
```bash
# menuconfig 配置
Component config → Log output → Default log verbosity → Debug
```

### 8.2 UART 调试

**使用逻辑分析仪：**
1. 连接 TX (GPIO_NUM_3) 和 RX (GPIO_NUM_14)
2. 设置波特率 9600
3. 验证帧格式和校验和
4. 测量 ACK 响应时间

**模拟 STM32 发送：**
```python
import serial

ser = serial.Serial('/dev/ttyUSB0', 9600)

# 发送门铃事件
msg = bytes([0xAA, 0x01, 0xA0, 0x01, 0xFF, 0xFF])
checksum = (0x01 + 0xA0 + 0x01 + 0xFF + 0xFF) & 0xFF
msg += bytes([checksum])
ser.write(msg)
```

### 8.3 WebSocket 调试

**使用 Wireshark：**
1. 捕获 WebSocket 流量
2. 过滤器：`websocket`
3. 查看 Binary Frame 和 Text Frame
4. 验证 BinaryProtocol2 格式

**使用 wscat 工具：**
```bash
# 连接 WebSocket
wscat -c ws://server:port

# 发送测试消息
{"type":"face_result","seq_id":"test_0","result":"known","user_id":5,"access":{"granted":true}}
```

### 8.4 测试用例

#### 测试用例 1：正常识别流程

**步骤：**
1. 模拟 STM32 发送门铃事件
2. 验证 ESP32 拍照
3. 验证 JPEG 发送到服务器
4. 模拟服务器返回识别成功
5. 验证 ESP32 发送开锁命令
6. 验证日志上报

**预期结果：** 全流程正常，开锁成功

#### 测试用例 2：陌生人拒绝

**步骤：**
1. 模拟 STM32 发送门铃事件
2. 模拟服务器返回 `result="unknown"`
3. 验证 ESP32 不发送开锁命令
4. 验证播放拒绝语音

**预期结果：** 不开锁，语音提示

#### 测试用例 3：内存不足

**步骤：**
1. 人为降低可用 PSRAM
2. 模拟 STM32 发送门铃事件
3. 验证 ESP32 拒绝请求

**预期结果：** 记录错误日志，不拍照

#### 测试用例 4：UART 超时

**步骤：**
1. ESP32 发送开锁命令
2. STM32 不回复 ACK
3. 等待 3 秒

**预期结果：** ESP32 发送 `ack(code=5)` 超时错误

#### 测试用例 5：seq_id 重复

**步骤：**
1. 服务器发送 `face_result` (seq_id="test_0")
2. ESP32 处理并回复
3. 服务器再次发送相同 seq_id

**预期结果：** ESP32 忽略第二次消息

---

## 九、常见问题

### 9.1 face_result 需要两级确认吗？

**协议设计：不需要**

**原因：**
- face_result 是服务器主动推送的识别结果，不是用户发起的命令
- 开锁结果通过 log_report 上报即可
- 不需要 seq_id 和两级确认机制

**代码实现：已实现（v5.2）**

虽然代码中实现了 esp32_ack 和 ack，但这是为了统一处理流程，实际上：
- 协议规范中 face_result 不携带 seq_id
- 服务器不需要等待 ESP32 的确认
- 开锁成功与否通过 log_report 上报

### 9.2 query 命令需要两级确认吗？

**需要（v5.2 新增）**

**原因：**
1. query 是用户发起的命令，需要确认收到
2. 需要等待 STM32 返回数据后才能发送最终 ack
3. 支持超时处理（5秒）

**流程：**
```
Server 发送 query(seq_id="xxx")
  ↓
ESP32 立即回复 esp32_ack(seq_id="xxx")
  ↓
ESP32 向 STM32 发送 Q_SENSORS/Q_STATUS
  ↓
STM32 返回 RPT_ENV/RPT_STATE
  ↓
ESP32 发送 status_report 上报数据
  ↓
ESP32 发送 ack(seq_id="xxx") 确认完成
```

### 9.3 为什么需要两级确认？

**原因：**
1. **第一级 (esp32_ack)：** 确保 ESP32 收到命令，避免网络丢包
2. **第二级 (ack)：** 确保 STM32 执行完成，反映真实结果

**场景示例：**
- 服务器发送开锁命令
- ESP32 收到后立即回复 `esp32_ack`（告知服务器"我收到了"）
- ESP32 向 STM32 发送 UART 命令
- STM32 执行并回复 ACK
- ESP32 收到 STM32 ACK 后回复 `ack`（告知服务器"执行完成"）

### 9.4 为什么人脸识别用 type=2，监控视频用 type=0？

**原因：**
- **type=2：** 单次图像，服务器需要进行 AI 识别，优先级高
- **type=0 + reserved≠0：** 连续视频流，服务器只需显示，优先级低

**区分意义：**
- 服务器可以根据 type 分配不同的处理队列
- 人脸识别需要更高的处理优先级

### 9.5 为什么 Capture() 和 CaptureForStream() 不同？

**原因：**
- **Capture()：** 用于人脸识别，需要高质量图像
  - 取3帧保留最后一帧（提高质量）
  - 显示预览（用户体验）
  - 支持旋转（适配不同安装角度）
  - 耗时约 150ms

- **CaptureForStream()：** 用于视频流，需要高帧率
  - 只取1帧（减少延迟）
  - 无预览（节省资源）
  - 无旋转（减少计算）
  - 耗时约 40ms

### 9.6 为什么需要检查 PSRAM > 100KB？

**原因：**
- JPEG 编码需要分配临时缓冲区（约 30KB）
- 如果内存不足，会导致编码失败或系统崩溃
- 100KB 是安全阈值，留有余量

### 9.7 监控模式和人脸识别能同时进行吗？

**不能。**

**原因：**
1. 摄像头资源冲突（同一时间只能一个任务使用）
2. 内存资源冲突（PSRAM 有限）
3. 网络带宽冲突（视频流已占用大量带宽）

**处理方式：**
- 监控模式下，忽略锁控事件
- 人脸识别时，不允许启动监控模式

---

## 十、参考文档

| 文档 | 路径 | 说明 |
|------|------|------|
| 需求文档 | `.kiro/specs/face-recognition/requirements.md` | EARS 格式完整需求 |
| 设计文档 | `.kiro/specs/face-recognition/design.md` | 技术架构和接口设计 |
| 任务清单 | `.kiro/specs/face-recognition/tasks.md` | 实施进度跟踪 |
| 交接文档 | `docs/completed/face_recognition_handover.md` | 开发交接文档 |
| 服务器协议 | `docs/my_docs/智能猫眼门锁系统-ESP32与服务器通信协议规范-v5.2.md` | 完整协议规范 |
| 快速参考 | `docs/my_docs/quick_reference.md` | 命令和 API 速查 |

---

**文档维护者：** 毕业设计项目组  
**最后更新：** 2026-01-17

