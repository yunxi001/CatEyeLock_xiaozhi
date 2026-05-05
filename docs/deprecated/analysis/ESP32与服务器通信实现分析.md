# ESP32 与服务器通信实现分析

> 版本：v1.0  
> 更新日期：2026-01-16  
> 适用于：智能猫眼门锁系统

---

## 一、通信架构概览

### 1.1 消息流向

```
┌─────────┐                    ┌─────────┐                    ┌─────────┐
│ Server  │◄──── WebSocket ────►│  ESP32  │◄────── UART ──────►│  STM32  │
└─────────┘                    └─────────┘                    └─────────┘
     │                              │                              │
     │  JSON / Binary               │  JSON / Binary               │  7字节帧
     │                              │                              │
     ▼                              ▼                              ▼
  命令下发                      协议转换                       硬件执行
  结果接收                      状态上报                       事件上报
```

### 1.2 消息分类

| 方向 | 格式 | 确认机制 | 说明 |
|------|------|----------|------|
| Server → ESP32 | JSON | ✅ 两级确认 | 命令下发，ESP32 回复 esp32_ack + ack |
| ESP32 → Server | JSON | ❌ 无需确认 | 状态/事件上报，服务器不回复 |
| ESP32 ↔ Server | Binary | ❌ 无需确认 | 音频/视频/人脸识别图像 |

---

## 二、ESP32 接收服务器消息

### 2.1 消息接收入口

消息接收入口位于 `WebsocketProtocol::OpenAudioChannel()` 中设置的回调：

```cpp
websocket_->OnData([this](const char *data, size_t len, bool binary) {
    if (binary) {
        // 处理二进制数据（音频）
        on_incoming_audio_(...)
    } else {
        // 处理 JSON 数据
        auto root = cJSON_Parse(data);
        auto type = cJSON_GetObjectItem(root, "type");
        
        // msg_id 防重放检查
        if (IsDuplicateMsgId(msg_id_str)) {
            return;  // 忽略重复消息
        }
        AddMsgIdToCache(msg_id_str);
        
        // 分发到 Application 处理
        on_incoming_json_(root);
    }
});
```

### 2.2 消息类型分发

`on_incoming_json_` 回调在 `Application::Start()` 中设置：

```cpp
protocol_->OnIncomingJson([this](const cJSON *root) {
    auto type = cJSON_GetObjectItem(root, "type");
    
    if (strcmp(type->valuestring, "tts") == 0) { ... }
    else if (strcmp(type->valuestring, "stt") == 0) { ... }
    else if (strcmp(type->valuestring, "llm") == 0) { ... }
    else if (strcmp(type->valuestring, "system") == 0) { ... }
    else if (HandleSmartLockJsonMessage(root, type->valuestring)) { ... }
    else { ESP_LOGW(TAG, "Unknown message type"); }
});
```

### 2.3 智能门锁消息处理

所有智能门锁消息由 `HandleSmartLockJsonMessage()` 统一处理：

| 消息类型 | 处理逻辑 | 两级确认 | 状态 |
|----------|----------|----------|------|
| `face_result` | 人脸识别结果处理 | ❌ 不需要 | 已实现 |
| `lock_control` | 锁控命令（开锁/关锁/临时密码） | ✅ | 已实现 |
| `dev_control` | 硬件控制（蜂鸣器/OLED/补光灯） | ✅ | 已实现 |
| `user_mgmt` | 用户管理（指纹/NFC/密码） | ✅ | 已实现 |
| `query` | 查询命令（传感器/状态） | ✅ | 已实现 |
| `heartbeat_ack` | 心跳响应 | ❌ 无需确认 | 已实现 |

### 2.4 两级确认机制

```
Server                  ESP32                   STM32
 │                       │                       │
 │ 命令 (seq_id=xxx)     │                       │
 │──────────────────────>│                       │
 │                       │                       │
 │                       │ ① 解析 seq_id         │
 │                       │ ② 发送 esp32_ack      │
 │ esp32_ack             │                       │
 │<──────────────────────│                       │
 │                       │                       │
 │                       │ ③ 保存 PendingCommand │
 │                       │ ④ 发送 UART 命令      │
 │                       │──────────────────────>│
 │                       │                       │
 │                       │ ⑤ 收到 UART 响应      │
 │                       │<──────────────────────│
 │                       │                       │
 │                       │ ⑥ 查找 PendingCommand │
 │                       │ ⑦ 发送 ack            │
 │ ack (code=0/错误码)   │                       │
 │<──────────────────────│                       │
```

### 2.5 PendingCommand 数据结构

```cpp
struct PendingCommand {
    std::string seq_id;           // 原始消息 ID（用于回填 ack）
    PendingCommandType type;      // 命令类型：IMMEDIATE/QUERY/LONG_FLOW
    std::string category;         // 类别：lock/dev/finger/nfc/password
    std::string command;          // 命令：unlock/lock/add/del/query...
    uint8_t uart_type;            // UART 命令 TYPE（用于匹配响应）
    uint8_t uart_subtype;         // UART 子命令
    int64_t timestamp_ms;         // 发送时间戳（用于超时检测）
    bool esp32_ack_sent;          // 是否已发送 esp32_ack
    bool stm32_ack_received;      // 是否已收到 STM32 ACK
    int stm32_error_code;         // STM32 ACK 错误码
};
```

### 2.6 命令类型分类

| 类型 | 说明 | ack 发送时机 | 超时时间 | 示例 |
|------|------|--------------|----------|------|
| IMMEDIATE | 即时命令 | 收到 STM32 ACK 后 | 3秒 | unlock, lock, beep, oled |
| QUERY | 查询命令 | 收到数据帧后 | 5秒 | sensors, status, finger.query |
| LONG_FLOW | 长流程命令 | 收到最终结果后 | 60秒 | finger.add, nfc.add |

---

## 三、ESP32 发送服务器消息

### 3.1 消息类型汇总

| 消息类型 | 触发来源 | 说明 | 携带 seq_id |
|----------|----------|------|-------------|
| `esp32_ack` | 服务器命令 | 第一级确认：命令已收到 | ✅ |
| `ack` | 服务器命令 | 第二级确认：命令执行完成 | ✅ |
| `status_report` | STM32 上报 | 状态上报（电量、光照、锁、灯） | ❌ |
| `event_report` | STM32 上报 | 事件上报（门铃、PIR、撬锁等） | ❌ |
| `log_report` | STM32 上报 | 开锁日志 | ❌ |
| `door_opened_report` | STM32 上报 | 开门日志 | ❌ |
| `password_report` | STM32 上报 | 密码查询结果 | ❌ |
| `user_mgmt_result` | STM32 上报 | 用户管理结果 | ❌ |
| `heartbeat` | 定时器（预留） | 心跳 | ❌ |

> **注意**：ESP32 主动上报的消息，服务器不需要回复 ack。

### 3.2 确认响应消息

#### esp32_ack（第一级确认）

**触发条件**：收到服务器带 `seq_id` 的命令后立即发送

```json
{
    "type": "esp32_ack",
    "seq_id": "1702234567890_0",
    "code": 0,
    "msg": "received"
}
```

#### ack（第二级确认）

**触发条件**：命令执行完成后发送（STM32 响应后）

```json
{
    "type": "ack",
    "seq_id": "1702234567890_0",
    "code": 0,
    "msg": "OK"
}
```

### 3.3 状态与事件上报

#### status_report（状态上报）

**触发条件**：STM32 上报 `RPT_ENV`（环境数据变化时）或 `RPT_STATE`

```json
{
    "type": "status_report",
    "ts": 1702234567890,
    "data": {
        "bat": 85,
        "lux": 300,
        "lock": 0,
        "light": 1
    }
}
```

#### event_report（事件上报）

**触发条件**：STM32 上报 `RPT_EVENT`

```json
{
    "type": "event_report",
    "ts": 1702234567890,
    "event": "bell",
    "param": 0
}
```

**事件类型映射**：

| event | STM32 事件ID | param 含义 | 附加动作 |
|-------|--------------|------------|----------|
| `bell` | EVT_DOORBELL | 无 | 触发人脸识别 |
| `pir_trigger` | EVT_PIR | 持续时间(秒) | 触发人脸识别 |
| `tamper` | EVT_TAMPER | 报警级别 | 记录日志 |
| `door_open` | EVT_DOOR_OPEN | 超时时间(分钟) | 记录日志 |
| `low_battery` | EVT_LOW_BATTERY | 当前电量(%) | 记录日志 |
| `door_closed` | EVT_LOCK_STATUS (status=0) | 无 | - |
| `lock_success` | EVT_LOCK_STATUS (status=1) | 无 | - |
| `bolt_alarm` | EVT_LOCK_STATUS (status=2) | 无 | - |

#### log_report（开锁日志）

**触发条件**：STM32 上报 `RPT_UNLOCK`

```json
{
    "type": "log_report",
    "ts": 1702234567890,
    "data": {
        "method": "finger",
        "status": "success",
        "uid": 5,
        "fail_count": 0,
        "lock_time": 0
    }
}
```

#### door_opened_report（开门日志）

**触发条件**：STM32 上报 `RPT_DOOR_OPENED`

```json
{
    "type": "door_opened_report",
    "ts": 1702234567890,
    "data": {
        "method": "finger",
        "source": "outside"
    }
}
```

#### password_report（密码上报）

**触发条件**：STM32 上报 `RPT_PWD`

```json
{
    "type": "password_report",
    "ts": 1702234567890,
    "data": {
        "password": "123456"
    }
}
```

#### user_mgmt_result（用户管理结果）

**触发条件**：用户管理命令执行完成

```json
{
    "type": "user_mgmt_result",
    "category": "finger",
    "command": "add",
    "result": true,
    "val": 6,
    "msg": "Success"
}
```

---

## 四、二进制消息（Binary Frame）

### 4.1 BinaryProtocol2 格式

```text
+----------------+----------------+--------------------------------+
|   version(2)   |    type(2)     |         reserved(4)            |
+----------------+----------------+--------------------------------+
|           timestamp(4)          |        payload_size(4)         |
+----------------+----------------+--------------------------------+
|                    payload data...                               |
+------------------------------------------------------------------+
```

### 4.2 消息类型

| type | reserved | 数据类型 | 方向 | 说明 |
|------|----------|----------|------|------|
| 0 | 0 | 音频流 | 双向 | OPUS 编码 |
| 0 | 非0 | 监控视频流 | ESP32 → Server | JPEG 编码，监控模式 |
| 2 | 非0 | 人脸识别图像 | ESP32 → Server | JPEG 编码，正常模式 |

### 4.3 人脸识别图像发送流程

```
STM32: RPT_EVENT (门铃/PIR)
    │
    ▼
ESP32: HandleLockReportMessage()
    │
    ├─ SendEventReport() → 服务器（JSON）
    │
    └─ TriggerFaceRecognition()
           │
           ├─ 摄像头拍照
           │
           └─ SendFaceRecognition() → 服务器（Binary, type=2）
```

---

## 五、STM32 上报消息处理

### 5.1 处理入口

`HandleLockReportMessage()` 函数处理所有 STM32 主动上报的消息（`CAT_RPT` 类别）。

### 5.2 上报消息类型

| 消息类型 | TYPE | 处理内容 | 服务器上报 |
|----------|------|----------|------------|
| RPT_EVENT | 0xA0 | 事件上报 | `SendEventReport()` |
| RPT_UNLOCK | 0xA1 | 开锁日志 | `SendLogReport()` |
| RPT_DOOR_OPENED | 0xA2 | 开门日志 | `SendDoorOpenedReport()` |
| RPT_ENV | 0xB0 | 环境数据 | `SendStatusReport()`（变化时） |
| RPT_STATE | 0xB1 | 状态上报 | `SendStatusReport()` |
| RPT_PWD | 0xC0 | 密码查询结果 | `SendPasswordReport()` |

### 5.3 查询命令的 ack 处理

RPT_ENV 和 RPT_STATE 处理中已实现查询命令的 ack 发送：

```cpp
case RPT_ENV: {
    // ... 保存数据、上报服务器 ...
    
    // 两级确认机制：查询命令收到数据帧后发送 ack
    uint8_t query_type = static_cast<uint8_t>(xiaozhi::CmdType::Q_SENSORS);
    auto it = pending_commands_.find(query_type);
    if (it != pending_commands_.end()) {
        protocol_->SendAck(cmd.seq_id, 0, "OK");
        pending_commands_.erase(it);
    }
    break;
}
```

---

## 六、错误码映射

### 6.1 统一错误码定义

| code | 含义 | 说明 |
|------|------|------|
| 0 | 成功 | 操作成功完成 |
| 1 | 设备离线 | ESP32 未连接服务器 |
| 2 | 设备忙 | 正在执行其他操作 |
| 3 | 参数错误 | 命令格式或参数无效 |
| 4 | 不支持 | 不支持的命令或操作 |
| 5 | 超时 | 等待响应超时 |
| 6 | 硬件故障 | 硬件异常或不可用 |
| 7 | 资源已满 | 指纹/NFC 存储已满 |
| 8 | 未认证 | 用户未登录或权限不足 |
| 9 | 重复消息 | seq_id 重复（防重放） |
| 10 | 内部错误 | 未知内部异常 |

### 6.2 STM32 错误码映射

| STM32 错误码 | 宏定义 | 统一 code |
|--------------|--------|-----------|
| 0x01 | ERR_BUSY | 2 |
| 0x02 | ERR_UNSUPPORT | 4 |
| 0x03 | ERR_PARAM | 3 |
| 0x04 | ERR_FP_FULL | 7 |
| 0x05 | ERR_NFC_FULL | 7 |
| 0x06 | ERR_HARDWARE | 6 |
| 0xFF | ERR_TIMEOUT | 5 |

---

## 七、代码位置汇总

| 功能 | 文件 | 函数 |
|------|------|------|
| WebSocket 数据接收 | websocket_protocol.cc | `OpenAudioChannel()` |
| JSON 消息分发 | application.cc | `Start()` 中的 `OnIncomingJson` 回调 |
| 智能门锁消息处理 | application.cc | `HandleSmartLockJsonMessage()` |
| STM32 上报处理 | application.cc | `HandleLockReportMessage()` |
| STM32 ACK 处理 | application.cc | `HandleLockSystemMessage()` |
| 用户管理反馈 | application.cc | `HandleLockUserMessage()` |
| 命令类型判断 | application.cc | `DetermineCommandType()` |
| 错误码映射 | application.cc | `MapStm32ErrorCode()` |
| 超时清理 | application.cc | `CleanupPendingCommands()` |
| esp32_ack 发送 | websocket_protocol.cc | `SendEsp32Ack()` |
| ack 发送 | websocket_protocol.cc | `SendAck()` |
| 状态上报 | websocket_protocol.cc | `SendStatusReport()` |
| 事件上报 | websocket_protocol.cc | `SendEventReport()` |
| 开锁日志 | websocket_protocol.cc | `SendLogReport()` |
| 开门日志 | websocket_protocol.cc | `SendDoorOpenedReport()` |
| 密码上报 | websocket_protocol.cc | `SendPasswordReport()` |
| 用户管理结果 | websocket_protocol.cc | `SendUserMgmtResult()` |
| 人脸识别图像 | websocket_protocol.cc | `SendFaceRecognition()` |
| 人脸识别触发 | application.cc | `TriggerFaceRecognition()` |

---

## 八、实现状态总结

| 功能 | 当前状态 | 说明 |
|------|----------|------|
| `face_result` 处理 | ✅ 已实现 | 不需要 seq_id 和两级确认 |
| `query` 消息接收 | ✅ 已实现 | 支持 sensors/status 查询，完整两级确认 |
| 心跳定时发送 | ⏸️ 预留 | 已实现发送方法，未启用定时器 |

---

**文档维护者**：毕业设计项目组  
**最后更新**：2026-01-17
