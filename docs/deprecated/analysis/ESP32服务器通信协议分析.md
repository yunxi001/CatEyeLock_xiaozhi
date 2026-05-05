# ESP32 与服务器通信协议分析

本文档详细分析 ESP32 与服务器之间的 WebSocket 通信协议，包括消息格式、交互流程和处理逻辑。

## 一、协议概述

### 1.1 通信方式

| 项目 | 说明 |
|------|------|
| 传输协议 | WebSocket (主要) / MQTT (备选) |
| 数据格式 | JSON 文本 + 二进制音视频流 |
| 音频编码 | OPUS，16kHz 采样率，单声道，60ms 帧时长 |
| 视频编码 | JPEG，640x480@10fps |
| 安全 | TLS 加密，Bearer Token 认证 |

### 1.2 二进制协议版本

**BinaryProtocol2（16 字节头 + 载荷）**：
```c
struct BinaryProtocol2 {
    uint16_t version;       // 协议版本（网络字节序）
    uint16_t type;          // 消息类型：0=音频/视频, 1=JSON, 2=人脸识别
    uint32_t reserved;      // 视频时编码宽高（高16位:宽, 低16位:高）
    uint32_t timestamp;     // 时间戳（ms）
    uint32_t payload_size;  // 载荷大小（字节）
    uint8_t payload[];      // 载荷数据
} __attribute__((packed));
```

**type 字段含义**：
| type 值 | 含义 | reserved 字段 |
|---------|------|---------------|
| 0 | 音频（OPUS）或视频（JPEG） | 音频=0，视频=(width<<16)\|height |
| 1 | JSON 消息 | 未使用 |
| 2 | 人脸识别图像 | (width<<16)\|height |

---

## 二、连接建立流程

### 2.1 WebSocket 连接

**请求头**：
| Header | 说明 |
|--------|------|
| Authorization | `Bearer <token>` 认证令牌 |
| Protocol-Version | 协议版本号（1/2/3） |
| Device-Id | 设备 MAC 地址 |
| Client-Id | 软件生成的 UUID |

### 2.2 握手流程

```
ESP32                                    服务器
  |                                        |
  |-------- WebSocket Connect ------------>|
  |                                        |
  |-------- Client Hello (JSON) ---------->|
  |                                        |
  |<------- Server Hello (JSON) -----------|
  |                                        |
  |======== 音频通道已建立 ================|
```

**Client Hello 消息**：
```json
{
  "type": "hello",
  "version": 2,
  "features": {
    "aec": true,
    "mcp": true
  },
  "transport": "websocket",
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

**Server Hello 消息**：
```json
{
  "type": "hello",
  "transport": "websocket",
  "session_id": "xxx-xxx-xxx",
  "audio_params": {
    "format": "opus",
    "sample_rate": 24000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

---

## 三、ESP32 发送到服务器的消息

### 3.1 语音交互消息

#### 3.1.1 开始监听 (listen)

**触发时机**：用户唤醒或按键触发语音交互

```json
{
  "session_id": "xxx",
  "type": "listen",
  "state": "start",
  "mode": "auto"
}
```

**mode 取值**：
| 值 | 含义 |
|----|------|
| auto | 自动停止（VAD 检测静音后停止） |
| manual | 手动停止（用户主动结束） |
| realtime | 实时模式（需要 AEC 支持） |

#### 3.1.2 停止监听 (listen)

```json
{
  "session_id": "xxx",
  "type": "listen",
  "state": "stop"
}
```

#### 3.1.3 唤醒词检测 (listen)

**触发时机**：检测到唤醒词

```json
{
  "session_id": "xxx",
  "type": "listen",
  "state": "detect",
  "text": "你好小智"
}
```

#### 3.1.4 中止说话 (abort)

**触发时机**：用户打断 TTS 播放

```json
{
  "session_id": "xxx",
  "type": "abort",
  "reason": "wake_word_detected"
}
```

---

### 3.2 智能门锁扩展消息 (v5.0 协议)

#### 3.2.1 状态上报 (status_report)

**触发时机**：收到 STM32 环境数据或状态变化时

```json
{
  "type": "status_report",
  "ts": 1234567890,
  "data": {
    "bat": 85,
    "lux": 500,
    "lock": 0,
    "light": 1
  }
}
```

| 字段 | 说明 |
|------|------|
| bat | 电量百分比 |
| lux | 光照强度（Lux） |
| lock | 锁状态（0=关, 1=开） |
| light | 灯状态（0=关, 1=开） |

#### 3.2.2 事件上报 (event_report)

**触发时机**：收到 STM32 事件上报时

```json
{
  "type": "event_report",
  "ts": 1234567890,
  "event": "bell",
  "param": 0
}
```

**事件类型**：
| event | 说明 | param |
|-------|------|-------|
| bell | 门铃按下 | 0 |
| pir_trigger | PIR 人体检测 | 持续秒数 |
| tamper | 撬锁报警 | 报警级别 |
| door_open | 门未关超时 | 超时分钟数 |
| low_battery | 低电量警告 | 电量百分比 |
| door_closed | 门已关闭 | 0 |
| lock_success | 自动上锁成功 | 0 |
| bolt_alarm | 锁舌未到位报警 | 0 |

#### 3.2.3 开锁日志上报 (log_report)

**触发时机**：收到 STM32 开锁日志时

```json
{
  "type": "log_report",
  "ts": 1234567890,
  "data": {
    "method": "finger",
    "uid": 1,
    "result": true,
    "fail_count": 0
  }
}
```

**开锁方式**：
| method | 说明 |
|--------|------|
| finger | 指纹开锁 |
| nfc | NFC 开锁 |
| pwd | 密码开锁 |
| remote | 远程开锁 |
| key | 钥匙开锁 |
| temp_pwd | 临时密码开锁 |
| face | 人脸开锁 |

#### 3.2.4 用户管理结果上报 (user_mgmt_result)

**触发时机**：指纹/NFC 录入完成时

```json
{
  "type": "user_mgmt_result",
  "category": "finger",
  "command": "add",
  "result": true,
  "val": 5,
  "msg": "Success"
}
```

| 字段 | 说明 |
|------|------|
| category | 类别：finger/nfc/password |
| command | 命令：add/del/clear/query |
| result | 是否成功 |
| val | 返回值（ID 或数量） |
| msg | 消息说明 |

#### 3.2.5 密码上报 (password_report)

**触发时机**：查询密码完成时

```json
{
  "type": "password_report",
  "ts": 1234567890,
  "data": {
    "password": "123456"
  }
}
```

#### 3.2.6 心跳 (heartbeat)

```json
{
  "type": "heartbeat",
  "ts": 1234567890,
  "uptime": 3600
}
```

---

### 3.3 确认消息 (两级确认机制)

#### 3.3.1 第一级确认 (esp32_ack)

**触发时机**：ESP32 收到服务器命令后立即发送

```json
{
  "type": "esp32_ack",
  "seq_id": "cmd-001",
  "code": 0,
  "msg": "received"
}
```

#### 3.3.2 第二级确认 (ack)

**触发时机**：命令执行完成后发送

```json
{
  "type": "ack",
  "seq_id": "cmd-001",
  "code": 0,
  "msg": "OK"
}
```

**错误码定义**：
| code | 说明 |
|------|------|
| 0 | 成功 |
| 1 | 设备忙 |
| 2 | 不支持的命令 |
| 3 | 参数错误 |
| 4 | 指纹库已满 |
| 5 | NFC 库已满 |
| 6 | 硬件错误 |
| 10 | 内部错误 |
| 255 | 操作超时 |

---

### 3.4 二进制数据发送

#### 3.4.1 音频数据

**格式**：BinaryProtocol2，type=0，reserved=0

**发送时机**：监听状态下持续发送麦克风采集的 OPUS 编码音频

#### 3.4.2 视频数据（监控模式）

**格式**：BinaryProtocol2，type=0，reserved=(width<<16)|height

**发送时机**：监控模式下持续发送 JPEG 编码视频帧

#### 3.4.3 人脸识别图像

**格式**：BinaryProtocol2，type=2，reserved=(width<<16)|height

**发送时机**：门铃/PIR 触发人脸识别时发送

---

## 四、服务器发送到 ESP32 的消息

### 4.1 语音交互消息

#### 4.1.1 语音识别结果 (stt)

```json
{
  "session_id": "xxx",
  "type": "stt",
  "text": "今天天气怎么样"
}
```

**ESP32 处理**：在显示屏上显示用户说的话

#### 4.1.2 TTS 控制 (tts)

**开始播放**：
```json
{
  "session_id": "xxx",
  "type": "tts",
  "state": "start"
}
```

**ESP32 处理**：
- 设置设备状态为 `kDeviceStateSpeaking`
- 准备接收音频数据

**句子开始**：
```json
{
  "session_id": "xxx",
  "type": "tts",
  "state": "sentence_start",
  "text": "今天天气晴朗，温度25度"
}
```

**ESP32 处理**：在显示屏上显示 AI 回复的文本

**停止播放**：
```json
{
  "session_id": "xxx",
  "type": "tts",
  "state": "stop"
}
```

**ESP32 处理**：
- 根据监听模式决定下一状态
- 手动模式：回到空闲状态
- 自动模式：继续监听

#### 4.1.3 表情控制 (llm)

```json
{
  "session_id": "xxx",
  "type": "llm",
  "emotion": "happy"
}
```

**ESP32 处理**：更新显示屏上的表情动画

---

### 4.2 系统控制消息

#### 4.2.1 系统命令 (system)

```json
{
  "session_id": "xxx",
  "type": "system",
  "command": "reboot"
}
```

**支持的命令**：
| command | ESP32 处理 |
|---------|------------|
| reboot | 重启设备 |
| start_monitor | 启动监控模式 |
| stop_monitor | 停止监控模式 |

#### 4.2.2 警告消息 (alert)

```json
{
  "session_id": "xxx",
  "type": "alert",
  "status": "警告",
  "message": "检测到异常",
  "emotion": "warning"
}
```

**ESP32 处理**：
- 显示状态和消息
- 更新表情
- 播放警告音效

---

### 4.3 智能门锁控制消息 (v5.0 协议)

#### 4.3.1 人脸识别结果 (face_result)

```json
{
  "type": "face_result",
  "msg_id": "face-001",
  "result": "known",
  "access": {
    "granted": true
  }
}
```

**result 取值**：
| 值 | 说明 |
|----|------|
| known | 已知用户 |
| unknown | 未知用户 |
| no_face | 未检测到人脸 |

**ESP32 处理**：
1. 发送 ACK 响应
2. 如果 `result == "known"` 且 `access.granted == true`：
   - 调用 `lock_control_->SendUnlock()` 发送开锁命令给 STM32

#### 4.3.2 锁控命令 (lock_control)

```json
{
  "type": "lock_control",
  "seq_id": "lock-001",
  "command": "unlock",
  "duration": 30
}
```

**支持的命令**：
| command | 参数 | ESP32 处理 |
|---------|------|------------|
| unlock | duration（秒） | 发送开锁命令给 STM32 |
| lock | 无 | 发送关锁命令给 STM32 |
| temp_code | code, expires | 设置临时密码 |

**ESP32 处理流程**：
1. 发送 `esp32_ack`（第一级确认）
2. 转发命令给 STM32
3. 等待 STM32 ACK
4. 发送 `ack`（第二级确认）

#### 4.3.3 设备控制命令 (dev_control)

```json
{
  "type": "dev_control",
  "seq_id": "dev-001",
  "target": "beep",
  "count": 3,
  "mode": "short"
}
```

**支持的 target**：
| target | 参数 | ESP32 处理 |
|--------|------|------------|
| beep | count, mode(short/long/alarm) | 控制蜂鸣器 |
| oled | icon | 控制 OLED 显示图标 |
| light | action(on/off/auto) | 控制补光灯 |

#### 4.3.4 用户管理命令 (user_mgmt)

```json
{
  "type": "user_mgmt",
  "seq_id": "user-001",
  "category": "finger",
  "command": "add",
  "user_id": 0
}
```

**支持的操作**：
| category | command | 参数 | ESP32 处理 |
|----------|---------|------|------------|
| finger | add | user_id | 开始录入指纹 |
| finger | del | user_id | 删除指定指纹 |
| finger | clear | 无 | 清空所有指纹 |
| finger | query | 无 | 查询指纹数量 |
| nfc | add | 无 | 开始录入 NFC |
| nfc | del | user_id | 删除指定 NFC |
| nfc | clear | 无 | 清空所有 NFC |
| nfc | query | 无 | 查询 NFC 数量 |
| password | set | payload | 设置全局密码 |
| password | query | 无 | 查询当前密码 |

#### 4.3.5 心跳响应 (heartbeat_ack)

```json
{
  "type": "heartbeat_ack"
}
```

**ESP32 处理**：记录日志，无其他操作

---

### 4.4 MCP 协议消息

```json
{
  "session_id": "xxx",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "self.audio_speaker.set_volume",
      "arguments": { "volume": 50 }
    },
    "id": 1
  }
}
```

**ESP32 处理**：转发给 `McpServer` 处理，执行对应的工具函数

---

## 五、消息防重放机制

### 5.1 msg_id 缓存

ESP32 维护一个 FIFO 队列（最大 100 条）用于存储已处理的 `msg_id`：

```cpp
std::deque<std::string> msg_id_queue_;  // FIFO 队列
std::set<std::string> msg_id_set_;      // 快速查找集合
```

### 5.2 处理流程

1. 收到带 `msg_id` 的消息
2. 检查 `msg_id` 是否在缓存中
3. 如果重复，忽略消息并记录警告日志
4. 如果不重复，添加到缓存并处理消息
5. 缓存满时，移除最旧的 `msg_id`

---

## 六、两级确认机制详解

### 6.1 机制说明

为确保命令执行的可靠性，ESP32 实现了两级确认机制：

```
服务器                    ESP32                     STM32
  |                         |                         |
  |--- lock_control ------->|                         |
  |                         |--- esp32_ack ---------->|
  |                         |                         |
  |                         |--- UART CMD ----------->|
  |                         |                         |
  |                         |<-- UART ACK ------------|
  |                         |                         |
  |<-- ack -----------------|                         |
```

### 6.2 命令类型分类

| 类型 | 命令示例 | 第二级确认时机 |
|------|----------|----------------|
| IMMEDIATE | 开锁、关锁、蜂鸣器、OLED、补光灯 | 收到 STM32 ACK |
| QUERY | 查询传感器、状态、密码 | 收到数据帧 |
| LONG_FLOW | 指纹录入、NFC 录入 | 收到最终结果 |

### 6.3 待处理命令管理

```cpp
struct PendingCommand {
    std::string seq_id;           // 服务器消息序列号
    PendingCommandType type;      // 命令类型
    std::string category;         // 命令类别
    std::string command;          // 命令名称
    uint8_t uart_type;            // UART 命令 TYPE
    uint8_t uart_subtype;         // UART 子命令
    int64_t timestamp_ms;         // 发送时间戳
    bool esp32_ack_sent;          // 是否已发送第一级确认
    bool stm32_ack_received;      // 是否已收到 STM32 ACK
    uint8_t stm32_error_code;     // STM32 错误码
};

std::map<uint8_t, PendingCommand> pending_commands_;  // 按 UART TYPE 索引
```

### 6.4 超时清理

每秒执行一次 `CleanupPendingCommands()`，清理超时的待处理命令。

---

## 七、监控模式通信

### 7.1 启动监控模式

**服务器命令**：
```json
{
  "type": "system",
  "command": "start_monitor"
}
```

**ESP32 处理**：
1. 调用 `StartMonitorMode()`
2. 设置 `protocol_->SetMonitorMode(true)`
3. 启动视频流服务
4. 设置设备状态为 `kDeviceStateMonitorStreaming`

### 7.2 监控模式数据流

```
ESP32                                    服务器
  |                                        |
  |-------- 视频帧 (JPEG) ---------------->|
  |-------- 音频帧 (OPUS) ---------------->|
  |                                        |
  |<------- 音频帧 (OPUS) -----------------|
  |                                        |
```

**视频帧格式**：BinaryProtocol2，type=0，reserved=(640<<16)|480

**音频帧格式**：BinaryProtocol2，type=0，reserved=0

### 7.3 停止监控模式

**服务器命令**：
```json
{
  "type": "system",
  "command": "stop_monitor"
}
```

**ESP32 处理**：
1. 调用 `StopMonitorMode()`
2. 设置 `protocol_->SetMonitorMode(false)`
3. 停止视频流服务
4. 恢复设备状态

---

## 八、错误处理

### 8.1 连接错误

| 错误场景 | ESP32 处理 |
|----------|------------|
| WebSocket 连接失败 | 触发 `on_network_error_` 回调，显示错误信息 |
| Server Hello 超时（10秒） | 设置错误状态，关闭连接 |
| 连接断开 | 触发 `on_audio_channel_closed_` 回调，回到空闲状态 |

### 8.2 通道超时

- 超时时间：120 秒无数据接收
- 处理：`IsTimeout()` 返回 true，音频通道被认为已关闭

### 8.3 JSON 解析错误

- 缺少 `type` 字段：记录错误日志，忽略消息
- 未知消息类型：记录警告日志，忽略消息

---

## 九、状态流转图

```
                    ┌─────────────────┐
                    │  kDeviceStateIdle │
                    └────────┬────────┘
                             │ 唤醒/按键
                             ▼
                    ┌─────────────────┐
                    │ kDeviceStateConnecting │
                    └────────┬────────┘
                             │ 连接成功
                             ▼
                    ┌─────────────────┐
              ┌─────│ kDeviceStateListening │◄────┐
              │     └────────┬────────┘          │
              │              │ TTS start         │ TTS stop
              │              ▼                   │
              │     ┌─────────────────┐          │
              │     │ kDeviceStateSpeaking │─────┘
              │     └─────────────────┘
              │
              │ start_monitor
              ▼
    ┌─────────────────────┐
    │ kDeviceStateMonitorConnecting │
    └────────┬────────────┘
             │ 连接成功
             ▼
    ┌─────────────────────┐
    │ kDeviceStateMonitorStreaming │
    └─────────────────────┘
```

---

## 十、总结

ESP32 与服务器的通信协议涵盖以下核心功能：

1. **语音交互**：唤醒词检测、语音识别、TTS 播放、表情控制
2. **智能门锁**：人脸识别、锁控命令、设备控制、用户管理
3. **监控模式**：实时视频对讲、双向音频
4. **可靠性保障**：两级确认机制、消息防重放、超时处理

协议设计遵循以下原则：
- JSON 文本用于控制消息，二进制用于音视频流
- 两级确认机制确保命令执行可靠性
- msg_id 防重放机制防止消息重复处理
- 监控模式与正常模式互斥，避免资源冲突
