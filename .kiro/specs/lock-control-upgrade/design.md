# Design Document: 锁控模块升级

## Overview

本设计文档描述了 ESP32 锁控模块的综合升级方案，包括：
1. UART 协议升级（v2.1 → v2.7）
2. 事件处理逻辑修改
3. 消息 ID 追溯机制（两级确认）

升级后，ESP32 将能够：
- 与 STM32 v2.7 协议完全兼容
- 正确处理新增的上报类型和事件
- 实现 seq_id 全链路追溯
- 提供统一的错误码映射

## Architecture

### 系统架构

```
┌─────────┐         ┌────────────┐         ┌─────────┐         ┌─────────┐
│   App   │◄───────►│   Server   │◄───────►│  ESP32  │◄───────►│  STM32  │
└─────────┘         └────────────┘         └─────────┘         └─────────┘
   WebSocket           WebSocket              UART (7字节)
   
   命令发起方           中转/路由              协议转换           硬件执行
   seq_id 生成         防重放检查             两级确认           实际操作
```

### 两级确认机制

```
Server                  ESP32                   STM32
 │                       │                       │
 │ 命令 (seq_id=xxx)     │                       │
 │──────────────────────►│                       │
 │                       │                       │
 │ esp32_ack             │ ← 第一级：命令已收到   │
 │ seq_id=xxx            │                       │
 │◄──────────────────────│                       │
 │                       │                       │
 │                       │ UART 命令             │
 │                       │──────────────────────►│
 │                       │                       │
 │                       │ UART ACK/结果         │
 │                       │◄──────────────────────│
 │                       │                       │
 │ ack (seq_id=xxx)      │ ← 第二级：执行完成     │
 │ code=0/错误码         │                       │
 │◄──────────────────────│                       │
```

## Components and Interfaces

### 1. LockProtocol 组件修改

#### 1.1 新增常量

```cpp
// lock_protocol.h
/** 协议空值（未使用字段填充） */
constexpr uint8_t LOCK_PROTOCOL_EMPTY = 0xFF;
```

#### 1.2 新增枚举

```cpp
// RptType 枚举新增
enum class RptType : uint8_t {
  RPT_EVENT       = 0xA0,
  RPT_UNLOCK      = 0xA1,
  RPT_DOOR_OPENED = 0xA2,  // 【新增】开门日志
  RPT_ENV         = 0xB0,
  RPT_STATE       = 0xB1,
  RPT_PWD         = 0xC0,
};

// EventId 枚举新增
enum class EventId : uint8_t {
  EVT_DOORBELL    = 0x01,
  EVT_PIR         = 0x02,
  EVT_TAMPER      = 0x03,
  EVT_DOOR_OPEN   = 0x04,
  EVT_LOW_BATTERY = 0x05,
  EVT_LOCK_STATUS = 0x06,  // 【新增】关门/上锁状态
};

// 【新增】锁状态码枚举
enum class LockStatusCode : uint8_t {
  DOOR_CLOSED   = 0x00,  // 门关闭
  LOCK_SUCCESS  = 0x01,  // 上锁成功
  BOLT_ALARM    = 0x02,  // 锁舌未到位报警
};

// 【新增】开门来源枚举
enum class DoorSource : uint8_t {
  OUTSIDE = 0x00,  // 室外开门
  INSIDE  = 0x01,  // 室内开门
  UNKNOWN = 0xFF,  // 未知
};

// FpRespStatus 枚举新增
enum class FpRespStatus : uint8_t {
  FP_PRESS_FINGER   = 0x01,
  FP_LIFT_FINGER    = 0x02,
  FP_SUCCESS        = 0x03,
  FP_FAILED         = 0x04,
  FP_COUNT_RESP     = 0x05,
  FP_ALREADY_EXISTS = 0x06,  // 【新增】已存在
  FP_ID_OCCUPIED    = 0x07,  // 【新增】ID 被占用
};
```

#### 1.3 BuildAckOk/BuildAckErr 修改

```cpp
// lock_protocol.cc
std::vector<uint8_t> LockProtocol::BuildAckOk(uint8_t orig_type) {
  std::array<uint8_t, 3> data = {orig_type, LOCK_PROTOCOL_EMPTY, LOCK_PROTOCOL_EMPTY};
  return BuildMessage(static_cast<uint8_t>(MsgCategory::SYS),
                      static_cast<uint8_t>(SysType::ACK_OK), data);
}

std::vector<uint8_t> LockProtocol::BuildAckErr(uint8_t orig_type, AckError error) {
  std::array<uint8_t, 3> data = {orig_type, static_cast<uint8_t>(error), LOCK_PROTOCOL_EMPTY};
  return BuildMessage(static_cast<uint8_t>(MsgCategory::SYS),
                      static_cast<uint8_t>(SysType::ACK_ERR), data);
}
```

### 2. LockControlService 组件修改

所有发送命令的方法需要将未使用的数据字段从 0x00 改为 LOCK_PROTOCOL_EMPTY (0xFF)。

受影响的方法：
- `SendLock()` - D2 改为 0xFF
- `SendOledIcon()` - D1/D2 改为 0xFF
- `SendBeep()` - D2 改为 0xFF
- `SendLight()` - D1/D2 改为 0xFF
- `QuerySensors()` - D0/D1/D2 改为 0xFF
- `QueryStatus()` - D0/D1/D2 改为 0xFF
- `FingerprintEnroll()` - D2 改为 0xFF
- `FingerprintDelete()` - D2 改为 0xFF
- `FingerprintClear()` - D0/D1/D2 改为 0xFF
- `FingerprintQueryCount()` - D0/D1/D2 改为 0xFF
- `NfcXxx()` - 同上
- `QueryPassword()` - D0/D1/D2 改为 0xFF
- `SendPing()` - D0/D1/D2 改为 0xFF

### 3. WebsocketProtocol 组件修改

#### 3.1 新增方法声明

```cpp
// websocket_protocol.h
class WebsocketProtocol : public Protocol {
public:
    // 【新增】发送 esp32_ack 响应（命令已收到）
    void SendEsp32Ack(const std::string& seq_id, int code = 0, 
                      const std::string& msg = "received");
    
    // 【修改】SendAck 方法签名（增加 seq_id 参数）
    void SendAck(const std::string& seq_id, int code = 0, 
                 const std::string& msg = "OK") override;
    
    // 【新增】发送密码上报
    void SendPasswordReport(uint32_t password);
    
    // 【新增】发送开门日志上报
    void SendDoorOpenedReport(const std::string& method, uint8_t source);
    
    // 【新增】发送锁状态事件上报
    void SendLockStatusEvent(uint8_t status);
};
```

#### 3.2 消息格式

```json
// esp32_ack：命令已收到
{
    "type": "esp32_ack",
    "seq_id": "1702234567890_0",
    "code": 0,
    "msg": "received"
}

// ack：执行完成
{
    "type": "ack",
    "seq_id": "1702234567890_0",
    "code": 0,
    "msg": "OK"
}

// password_report：密码上报
{
    "type": "password_report",
    "ts": 1702234567890,
    "data": {
        "password": "123456"
    }
}
```

### 4. Application 组件修改

#### 4.1 新增数据结构

```cpp
// application.h

/**
 * @brief 待处理命令类型
 */
enum class PendingCommandType {
    IMMEDIATE,   // 即时命令：等待 STM32 ACK
    QUERY,       // 查询命令：等待 STM32 ACK + 数据帧
    LONG_FLOW,   // 长流程命令：等待 STM32 ACK + 最终结果
};

/**
 * @brief 待处理命令信息
 */
struct PendingCommand {
    std::string seq_id;           // 原始消息 ID
    PendingCommandType type;      // 命令类型
    std::string category;         // 类别：finger/nfc/password/lock/dev/query
    std::string command;          // 命令：add/del/clear/query/unlock/lock/beep/...
    uint8_t uart_type;            // UART 命令 TYPE
    uint8_t uart_subtype;         // UART 子命令
    int64_t timestamp_ms;         // 发送时间戳
    bool esp32_ack_sent;          // 是否已发送 esp32_ack
    bool stm32_ack_received;      // 是否已收到 STM32 ACK
    int stm32_error_code;         // STM32 ACK 错误码
};
```

#### 4.2 新增成员变量

```cpp
// application.h
class Application {
private:
    // 待处理命令映射表
    std::map<uint8_t, PendingCommand> pending_commands_;
    
    // 超时时间配置
    static constexpr int64_t IMMEDIATE_TIMEOUT_MS = 3000;
    static constexpr int64_t QUERY_TIMEOUT_MS = 5000;
    static constexpr int64_t LONG_FLOW_TIMEOUT_MS = 60000;
};
```

#### 4.3 新增方法

```cpp
// application.h
class Application {
private:
    // 确定命令类型
    PendingCommandType DetermineCommandType(const std::string& category, 
                                            const std::string& command);
    
    // 获取 UART TYPE
    uint8_t GetUartType(const std::string& category, const std::string& command);
    
    // 清理超时命令
    void CleanupPendingCommands();
    
    // 获取命令超时时间
    int64_t GetTimeoutForType(PendingCommandType type);
    
    // STM32 错误码映射
    int MapStm32ErrorCode(uint8_t stm32_err);
    
    // 处理 RPT_DOOR_OPENED 消息
    void HandleDoorOpenedReport(const xiaozhi::LockMessage& msg);
    
    // 处理 EVT_LOCK_STATUS 事件
    void HandleLockStatusEvent(uint8_t status);
};
```

## Data Models

### 命令分类表

| 命令类型 | STM32 策略 | esp32_ack 时机 | ack 时机 |
|---------|-----------|----------------|----------|
| 即时命令 | 执行后 ACK | 收到命令时 | 收到 STM32 ACK 时 |
| 查询命令 | 先 ACK 后数据 | 收到命令时 | 收到数据帧时 |
| 长流程命令 | 先 ACK 后上报 | 收到命令时 | 收到最终结果时 |

### 即时命令列表

| 服务器消息 | 命令 | STM32 TYPE |
|-----------|------|-----------|
| `lock_control` | unlock/lock | CMD_LOCK (0x10) |
| `dev_control` | beep | CMD_BEEP (0x12) |
| `dev_control` | oled | CMD_OLED (0x11) |
| `dev_control` | light | CMD_LIGHT (0x14) |
| `user_mgmt` | finger.del | FP_CMD (0x10) |
| `user_mgmt` | finger.clear | FP_CMD (0x10) |
| `user_mgmt` | nfc.del | NFC_CMD (0x20) |
| `user_mgmt` | nfc.clear | NFC_CMD (0x20) |
| `user_mgmt` | password.set | PWD_SET (0x30) |

### 查询命令列表

| 服务器消息 | 命令 | STM32 TYPE | 数据帧 |
|-----------|------|-----------|--------|
| `query` | sensors | Q_SENSORS (0x80) | RPT_ENV (0xB0) |
| `query` | status | Q_STATUS (0x81) | RPT_STATE (0xB1) |
| `user_mgmt` | finger.query | FP_CMD (0x10) | FP_RESP (0x11) |
| `user_mgmt` | nfc.query | NFC_CMD (0x20) | NFC_RESP (0x21) |
| `user_mgmt` | password.query | PWD_QUERY (0x31) | RPT_PWD (0xC0) |

### 长流程命令列表

| 服务器消息 | 命令 | STM32 TYPE | 最终结果状态码 |
|-----------|------|-----------|---------------|
| `user_mgmt` | finger.add | FP_CMD (0x10) | 0x03/0x04/0x06/0x07 |
| `user_mgmt` | nfc.add | NFC_CMD (0x20) | 0x03/0x04/0x06/0x07 |

### 统一错误码表

| code | 含义 | STM32 映射 |
|------|------|------------|
| 0 | 成功 | ACK_OK |
| 2 | 设备忙 | 0x01 ERR_BUSY |
| 3 | 参数错误 | 0x03 ERR_PARAM |
| 4 | 不支持 | 0x02 ERR_UNSUPPORT |
| 5 | 超时 | 0xFF ERR_TIMEOUT |
| 6 | 硬件故障 | 0x06 ERR_HARDWARE |
| 7 | 资源已满 | 0x04/0x05 ERR_FP/NFC_FULL |
| 10 | 内部错误 | 未知错误 |

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system-essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: ACK_OK 空值字段填充

*For any* orig_type value (0x00-0xFF), when building an ACK_OK response, the resulting message SHALL have D1 and D2 equal to LOCK_PROTOCOL_EMPTY (0xFF).

**Validates: Requirements 1.2**

### Property 2: ACK_ERR 空值字段填充

*For any* orig_type value and error code, when building an ACK_ERR response, the resulting message SHALL have D2 equal to LOCK_PROTOCOL_EMPTY (0xFF).

**Validates: Requirements 1.3**

### Property 3: 密码上报消息格式

*For any* valid password (0-999999), when SendPasswordReport is called, the resulting JSON message SHALL contain type "password_report" and a 6-digit zero-padded password string.

**Validates: Requirements 8.3**

### Property 4: esp32_ack 消息格式

*For any* seq_id string, when SendEsp32Ack is called, the resulting JSON message SHALL contain type "esp32_ack", the original seq_id, code 0, and msg "received".

**Validates: Requirements 9.3**

### Property 5: ack 消息格式

*For any* seq_id string and code value, when SendAck is called, the resulting JSON message SHALL contain type "ack", the original seq_id, the code value, and a msg string.

**Validates: Requirements 10.5**

### Property 6: 待处理命令保存

*For any* server command with a seq_id, when the command is sent to STM32, the pending_commands_ map SHALL contain an entry with uart_type as key and the original seq_id preserved.

**Validates: Requirements 11.3**

### Property 7: 待处理命令查找

*For any* STM32 response with a uart_type, when looking up the pending command, the retrieved seq_id SHALL match the original command's seq_id.

**Validates: Requirements 11.4**

### Property 8: 超时命令清理

*For any* pending command that exceeds its timeout duration, when CleanupPendingCommands is called, the command SHALL be removed from pending_commands_ and an ack with code 5 SHALL be sent.

**Validates: Requirements 12.3**

### Property 9: STM32 错误码映射完整性

*For any* STM32 error code, the MapStm32ErrorCode function SHALL return a valid unified error code (0, 2, 3, 4, 5, 6, 7, or 10).

**Validates: Requirements 13.2, 13.3, 13.4, 13.5, 13.6, 13.7, 13.8**

## Error Handling

### UART 通信错误

| 错误场景 | 处理方式 |
|---------|---------|
| STM32 无响应 | 重试 3 次，每次间隔 1 秒，超时后发送 ack(code=5) |
| 校验和错误 | 丢弃消息，等待下一帧 |
| 帧格式错误 | 丢弃消息，记录日志 |

### WebSocket 通信错误

| 错误场景 | 处理方式 |
|---------|---------|
| 连接断开 | 不发送 ack，等待重连 |
| 发送失败 | 记录日志，不重试 |

### 命令执行错误

| 错误场景 | 处理方式 |
|---------|---------|
| STM32 返回 ACK_ERR | 映射错误码，发送 ack(code=映射值) |
| 命令超时 | 发送 ack(code=5)，清理 pending |

## Testing Strategy

### 单元测试

1. **协议层测试**
   - BuildAckOk/BuildAckErr 空值字段验证
   - 新增枚举值验证
   - 密码编解码测试

2. **消息格式测试**
   - esp32_ack JSON 格式验证
   - ack JSON 格式验证
   - password_report JSON 格式验证

3. **错误码映射测试**
   - 所有 STM32 错误码映射验证
   - 未知错误码处理验证

### 属性测试

使用 C++ 属性测试框架（如 RapidCheck）验证：

1. **Property 1-2**: ACK 消息空值字段
2. **Property 3-5**: JSON 消息格式
3. **Property 6-8**: 待处理命令队列操作
4. **Property 9**: 错误码映射完整性

### 集成测试

1. **两级确认流程测试**
   - 即时命令完整流程
   - 查询命令完整流程
   - 长流程命令完整流程

2. **事件处理测试**
   - EVT_TAMPER 仅上报验证
   - EVT_DOOR_OPEN 语音播报验证
   - EVT_LOW_BATTERY 仅上报验证
   - EVT_LOCK_STATUS 上报验证

3. **超时处理测试**
   - 各类型命令超时验证
   - 超时后 ack 发送验证

