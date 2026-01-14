# ESP32 消息 ID 追溯机制改进需求

> 版本：v3.0  
> 创建日期：2026-01-13  
> 更新日期：2026-01-13  
> 状态：待评审

---

## 1. 问题背景

### 1.1 当前通信架构

```
┌─────────┐         ┌────────────┐         ┌─────────┐         ┌─────────┐
│   App   │◄───────►│   Server   │◄───────►│  ESP32  │◄───────►│  STM32  │
└─────────┘         └────────────┘         └─────────┘         └─────────┘
   WebSocket           WebSocket              UART (7字节)
   
   命令发起方           中转/路由              协议转换           硬件执行
   seq_id 生成         防重放检查             seq_id 处理        实际操作
```

### 1.2 核心问题

**当前 ESP32 只有一级 ACK 响应，无法区分"命令已收到"和"命令已执行完成"。**

问题表现：
1. ESP32 收到命令后立即返回 ACK，但此时 STM32 可能还未执行
2. STM32 执行完成后返回结果，ESP32 上报时无法携带原始 `seq_id`
3. App 无法准确追踪命令的执行状态

---

## 2. 设计目标：两级确认机制

### 2.1 机制定义

| 级别 | 确认消息 | 触发时机 | 含义 | 携带 seq_id |
|------|----------|----------|------|-------------|
| 第一级 | `esp32_ack` | ESP32 收到服务器命令 | 命令已收到，开始处理 | ✅ 携带 |
| 第二级 | `ack` | STM32 执行完成 | 命令已执行完成 | ✅ 携带 |

### 2.2 两级确认消息格式

```json
// esp32_ack：命令已收到，开始处理
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
```

### 2.3 完整确认链路

```
App                    Server                  ESP32                   STM32
 │                       │                       │                       │
 │ 命令 (seq_id=xxx)     │                       │                       │
 │──────────────────────►│                       │                       │
 │                       │                       │                       │
 │ server_ack            │                       │                       │
 │ (seq_id=xxx, code=0)  │                       │                       │
 │◄──────────────────────│                       │                       │
 │                       │                       │                       │
 │                       │ 命令 (seq_id=xxx)     │                       │
 │                       │──────────────────────►│                       │
 │                       │                       │                       │
 │                       │ esp32_ack             │ ← 第一级：命令已收到   │
 │                       │ seq_id=xxx            │                       │
 │                       │ code=0, msg=received  │                       │
 │                       │◄──────────────────────│                       │
 │                       │                       │                       │
 │                       │                       │ UART 命令             │
 │                       │                       │──────────────────────►│
 │                       │                       │                       │
 │                       │                       │                       │ 执行
 │                       │                       │                       │
 │                       │                       │ UART ACK/结果         │
 │                       │                       │◄──────────────────────│
 │                       │                       │                       │
 │                       │ ack (seq_id=xxx)      │ ← 第二级：执行完成     │
 │                       │ code=0/错误码         │                       │
 │                       │◄──────────────────────│                       │
 │                       │                       │                       │
 │ ack 转发              │                       │                       │
 │ (seq_id=xxx)          │                       │                       │
 │◄──────────────────────│                       │                       │
```

---

## 3. 命令分类与 ACK 策略

### 3.1 STM32 ACK 策略分类（参考 UART 协议 v2.7）

| 策略类型 | 说明 | 适用场景 |
|----------|------|----------|
| **执行后 ACK** | STM32 执行完成后回复 ACK | 即时执行的短命令 |
| **先 ACK 后数据** | STM32 先回复 ACK，再发送数据帧 | 查询类命令 |
| **先 ACK 后上报** | STM32 先回复 ACK，过程通过 Report 帧上报 | 长流程交互命令 |

### 3.2 ESP32 两级确认对应关系

| 命令类型 | STM32 策略 | esp32_ack 时机 | ack 时机 |
|---------|-----------|----------------|----------|
| 即时命令 | 执行后 ACK | 收到命令时 | 收到 STM32 ACK 时 |
| 查询命令 | 先 ACK 后数据 | 收到命令时 | 收到数据帧时 |
| 长流程命令 | 先 ACK 后上报 | 收到命令时 | 收到最终结果时 |

### 3.3 详细命令分类

#### 即时命令（执行后 ACK）

| 服务器消息 | 命令 | STM32 TYPE | ack 时机 |
|-----------|------|-----------|----------|
| `lock_control` | unlock/lock | CMD_LOCK (0x10) | 收到 STM32 ACK |
| `lock_control` | temp_code | TEMP_PWD (0x32+0x33) | 收到第二包 ACK |
| `dev_control` | beep | CMD_BEEP (0x12) | 收到 STM32 ACK |
| `dev_control` | oled | CMD_OLED (0x11) | 收到 STM32 ACK |
| `dev_control` | light | CMD_LIGHT (0x14) | 收到 STM32 ACK |
| `user_mgmt` | finger.del | FP_CMD (0x10) | 收到 STM32 ACK |
| `user_mgmt` | finger.clear | FP_CMD (0x10) | 收到 STM32 ACK |
| `user_mgmt` | nfc.del | NFC_CMD (0x20) | 收到 STM32 ACK |
| `user_mgmt` | nfc.clear | NFC_CMD (0x20) | 收到 STM32 ACK |
| `user_mgmt` | password.set | PWD_SET (0x30) | 收到 STM32 ACK |

#### 查询命令（先 ACK 后数据）

| 服务器消息 | 命令 | STM32 TYPE | 数据帧 | ack 时机 |
|-----------|------|-----------|--------|----------|
| `query` | sensors | Q_SENSORS (0x80) | RPT_ENV (0xB0) | 收到 RPT_ENV |
| `query` | status | Q_STATUS (0x81) | RPT_STATE (0xB1) | 收到 RPT_STATE |
| `user_mgmt` | finger.query | FP_CMD (0x10) | FP_RESP (0x11) | 收到 FP_RESP(0x05) |
| `user_mgmt` | nfc.query | NFC_CMD (0x20) | NFC_RESP (0x21) | 收到 NFC_RESP(0x05) |
| `user_mgmt` | password.query | PWD_QUERY (0x31) | RPT_PWD (0xC0) | 收到 RPT_PWD |

#### 长流程命令（先 ACK 后上报）

| 服务器消息 | 命令 | STM32 TYPE | 最终结果状态码 | ack 时机 |
|-----------|------|-----------|---------------|----------|
| `user_mgmt` | finger.add | FP_CMD (0x10) | 0x03/0x04/0x06/0x07 | 收到最终状态 |
| `user_mgmt` | nfc.add | NFC_CMD (0x20) | 0x03/0x04/0x06/0x07 | 收到最终状态 |

**最终状态码说明**：
- `0x03`: 录入成功
- `0x04`: 操作失败
- `0x06`: 已存在（返回已有 ID）
- `0x07`: ID 被占用（自动分配新 ID）

---

## 4. 数据结构设计

### 4.1 待处理命令类型

```cpp
/**
 * @brief 待处理命令类型
 */
enum class PendingCommandType {
    IMMEDIATE,   ///< 即时命令：等待 STM32 ACK
    QUERY,       ///< 查询命令：等待 STM32 ACK + 数据帧
    LONG_FLOW,   ///< 长流程命令：等待 STM32 ACK + 最终结果
};
```

### 4.2 待处理命令结构体

```cpp
/**
 * @brief 待处理命令信息
 */
struct PendingCommand {
    std::string seq_id;           ///< 原始消息 ID（统一使用 seq_id）
    PendingCommandType type;      ///< 命令类型
    std::string category;         ///< 类别：finger/nfc/password/lock/dev/query
    std::string command;          ///< 命令：add/del/clear/query/unlock/lock/beep/...
    uint8_t uart_type;            ///< UART 命令 TYPE（用于匹配 ACK）
    uint8_t uart_subtype;         ///< UART 子命令（用于区分同 TYPE 的不同操作）
    int64_t timestamp_ms;         ///< 发送时间戳（毫秒）
    bool esp32_ack_sent;          ///< 是否已发送 esp32_ack
    bool stm32_ack_received;      ///< 是否已收到 STM32 ACK
    int stm32_error_code;         ///< STM32 ACK 错误码（0=成功）
};
```

### 4.3 Application 类新增成员

```cpp
class Application {
private:
    /**
     * @brief 待处理命令映射表
     * 
     * Key: UART 命令 TYPE (uint8_t)
     * Value: 待处理命令信息
     * 
     * 设计说明：
     * - 使用 uart_type 作为 key，便于匹配 STM32 ACK
     * - 同一 TYPE 同时只能有一个待处理命令
     */
    std::map<uint8_t, PendingCommand> pending_commands_;
    
    /** 超时时间配置 */
    static constexpr int64_t IMMEDIATE_TIMEOUT_MS = 3000;   ///< 即时命令 3 秒
    static constexpr int64_t QUERY_TIMEOUT_MS = 5000;       ///< 查询命令 5 秒
    static constexpr int64_t LONG_FLOW_TIMEOUT_MS = 60000;  ///< 长流程 60 秒
};
```

---

## 5. 处理流程

### 5.1 命令下发流程

```cpp
void Application::HandleServerCommand(const cJSON* root, const std::string& seq_id) {
    // 1. 立即发送 esp32_ack（携带 seq_id）
    protocol_->SendEsp32Ack(seq_id, 0, "received");
    
    // 2. 解析命令，确定类型
    PendingCommandType type = DetermineCommandType(category, command);
    uint8_t uart_type = GetUartType(category, command);
    
    // 3. 保存待处理命令
    pending_commands_[uart_type] = {
        .seq_id = seq_id,
        .type = type,
        .category = category,
        .command = command,
        .uart_type = uart_type,
        .timestamp_ms = esp_timer_get_time() / 1000,
        .esp32_ack_sent = true,
        .stm32_ack_received = false,
        .stm32_error_code = 0
    };
    
    // 4. 发送 UART 命令到 STM32
    lock_control_->SendCommand(...);
}
```

### 5.2 收到 STM32 ACK 时的处理

```cpp
void Application::HandleLockSystemMessage(const xiaozhi::LockMessage& msg) {
    if (msg.IsAckOk() || msg.IsAckErr()) {
        uint8_t orig_type = msg.data[0];
        
        auto it = pending_commands_.find(orig_type);
        if (it == pending_commands_.end()) {
            return;  // 无对应的待处理命令
        }
        
        auto& cmd = it->second;
        cmd.stm32_ack_received = true;
        cmd.stm32_error_code = msg.IsAckErr() ? msg.data[1] : 0;
        
        // 即时命令：收到 STM32 ACK 后发送 ack
        if (cmd.type == PendingCommandType::IMMEDIATE) {
            int code = MapStm32ErrorCode(cmd.stm32_error_code);
            std::string error_msg = GetErrorMessage(cmd.stm32_error_code);
            
            // 发送 ack（携带 seq_id）
            protocol_->SendAck(cmd.seq_id, code, error_msg);
            pending_commands_.erase(it);
        }
        // QUERY 和 LONG_FLOW 类型继续等待数据帧
    }
}
```

### 5.3 收到 STM32 数据帧时的处理

```cpp
void Application::HandleLockUserMessage(const xiaozhi::LockMessage& msg) {
    // 指纹响应 (TYPE = 0x11)
    if (msg.type == 0x11) {
        uint8_t status = msg.data[0];
        
        auto it = pending_commands_.find(0x10);  // FP_CMD
        if (it == pending_commands_.end()) {
            return;
        }
        
        auto& cmd = it->second;
        
        // 判断是否为最终结果
        bool is_final = (status == 0x03 || status == 0x04 || 
                        status == 0x05 || status == 0x06 || status == 0x07);
        
        if (is_final) {
            // 构建结果
            bool success = (status == 0x03 || status == 0x05 || 
                           status == 0x06 || status == 0x07);
            int code = success ? 0 : MapFpErrorCode(msg.data[1]);
            
            // 发送 ack（携带 seq_id 和结果数据）
            protocol_->SendAck(cmd.seq_id, code, GetStatusMessage(status));
            
            // 如果需要，额外发送详细结果
            if (cmd.type == PendingCommandType::LONG_FLOW) {
                protocol_->SendUserMgmtResult(
                    cmd.category, cmd.command, success,
                    msg.data[1], GetStatusMessage(status), cmd.seq_id
                );
            }
            
            pending_commands_.erase(it);
        }
        // 中间状态（0x01 请按手指, 0x02 请抬起）不发送 ack，继续等待
    }
}
```

### 5.4 超时清理机制

```cpp
void Application::CleanupPendingCommands() {
    int64_t now_ms = esp_timer_get_time() / 1000;
    
    for (auto it = pending_commands_.begin(); it != pending_commands_.end(); ) {
        auto& cmd = it->second;
        int64_t timeout_ms = GetTimeoutForType(cmd.type);
        
        if (now_ms - cmd.timestamp_ms > timeout_ms) {
            ESP_LOGW(TAG, "命令超时: type=0x%02X, seq_id=%s", 
                     cmd.uart_type, cmd.seq_id.c_str());
            
            // 发送 ack（超时错误，code=5）
            protocol_->SendAck(cmd.seq_id, 5, "Timeout");
            
            it = pending_commands_.erase(it);
        } else {
            ++it;
        }
    }
}
```

int64_t Application::GetTimeoutForType(PendingCommandType type) {
    switch (type) {
        case PendingCommandType::IMMEDIATE:  return IMMEDIATE_TIMEOUT_MS;
        case PendingCommandType::QUERY:      return QUERY_TIMEOUT_MS;
        case PendingCommandType::LONG_FLOW:  return LONG_FLOW_TIMEOUT_MS;
        default:                             return IMMEDIATE_TIMEOUT_MS;
    }
}
```

---

## 6. 完整流程示例

### 6.1 即时命令：开锁

```
App                    Server                  ESP32                   STM32
 │                       │                       │                       │
 │ lock_control          │                       │                       │
 │ seq_id=xxx            │                       │                       │
 │ command=unlock        │                       │                       │
 │──────────────────────►│                       │                       │
 │                       │                       │                       │
 │ server_ack            │                       │                       │
 │ seq_id=xxx            │                       │                       │
 │◄──────────────────────│                       │                       │
 │                       │                       │                       │
 │                       │ lock_control          │                       │
 │                       │ seq_id=xxx            │                       │
 │                       │──────────────────────►│                       │
 │                       │                       │                       │
 │                       │ esp32_ack             │ ← esp32_ack           │
 │                       │ seq_id=xxx            │                       │
 │                       │ code=0, msg=received  │                       │
 │                       │◄──────────────────────│                       │
 │                       │                       │                       │
 │                       │                       │ 保存 pending          │
 │                       │                       │ uart_type=0x10        │
 │                       │                       │                       │
 │                       │                       │ CMD_LOCK              │
 │                       │                       │──────────────────────►│
 │                       │                       │                       │
 │                       │                       │                       │ 执行开锁
 │                       │                       │                       │
 │                       │                       │ ACK_OK (TYPE=0x10)    │
 │                       │                       │◄──────────────────────│
 │                       │                       │                       │
 │                       │                       │ 匹配 pending          │
 │                       │                       │ 清除 pending          │
 │                       │                       │                       │
 │                       │ ack (seq_id=xxx)      │ ← ack                 │
 │                       │ code=0, msg=OK        │                       │
 │                       │◄──────────────────────│                       │
 │                       │                       │                       │
 │ ack 转发              │                       │                       │
 │ seq_id=xxx, code=0    │                       │                       │
 │◄──────────────────────│                       │                       │
```

### 6.2 长流程命令：指纹录入

```
App                    Server                  ESP32                   STM32
 │                       │                       │                       │
 │ user_mgmt             │                       │                       │
 │ seq_id=xxx            │                       │                       │
 │ category=finger       │                       │                       │
 │ command=add           │                       │                       │
 │──────────────────────►│                       │                       │
 │                       │                       │                       │
 │ server_ack            │                       │                       │
 │◄──────────────────────│                       │                       │
 │                       │                       │                       │
 │                       │ user_mgmt             │                       │
 │                       │ seq_id=xxx            │                       │
 │                       │──────────────────────►│                       │
 │                       │                       │                       │
 │                       │ esp32_ack             │ ← esp32_ack           │
 │                       │ seq_id=xxx            │                       │
 │                       │ code=0, msg=received  │                       │
 │                       │◄──────────────────────│                       │
 │                       │                       │                       │
 │                       │                       │ 保存 pending          │
 │                       │                       │ type=LONG_FLOW        │
 │                       │                       │                       │
 │                       │                       │ FP_CMD (录入)         │
 │                       │                       │──────────────────────►│
 │                       │                       │                       │
 │                       │                       │ ACK_OK                │
 │                       │                       │◄──────────────────────│
 │                       │                       │                       │
 │                       │                       │ FP_RESP (请按手指)    │
 │                       │                       │◄──────────────────────│
 │                       │                       │ (不发送 ack，继续等待) │
 │                       │                       │                       │
 │                       │                       │ ... 多次交互 ...       │
 │                       │                       │                       │
 │                       │                       │ FP_RESP (成功, ID=5)  │
 │                       │                       │◄──────────────────────│
 │                       │                       │                       │
 │                       │                       │ 匹配 pending          │
 │                       │                       │ 清除 pending          │
 │                       │                       │                       │
 │                       │ ack (seq_id=xxx)      │ ← ack                 │
 │                       │ code=0, msg=OK        │                       │
 │                       │◄──────────────────────│                       │
 │                       │                       │                       │
 │                       │ user_mgmt_result      │ ← 详细结果（可选）     │
 │                       │ seq_id=xxx            │                       │
 │                       │ result=true, val=5    │                       │
 │                       │◄──────────────────────│                       │
 │                       │                       │                       │
 │ ack + result 转发     │                       │                       │
 │◄──────────────────────│                       │                       │
```

### 6.3 查询命令：传感器数据

```
App                    Server                  ESP32                   STM32
 │                       │                       │                       │
 │ query                 │                       │                       │
 │ seq_id=xxx            │                       │                       │
 │ target=sensors        │                       │                       │
 │──────────────────────►│                       │                       │
 │                       │                       │                       │
 │                       │ query                 │                       │
 │                       │ seq_id=xxx            │                       │
 │                       │──────────────────────►│                       │
 │                       │                       │                       │
 │                       │ esp32_ack             │ ← esp32_ack           │
 │                       │ seq_id=xxx            │                       │
 │                       │◄──────────────────────│                       │
 │                       │                       │                       │
 │                       │                       │ Q_SENSORS             │
 │                       │                       │──────────────────────►│
 │                       │                       │                       │
 │                       │                       │ ACK_OK                │
 │                       │                       │◄──────────────────────│
 │                       │                       │                       │
 │                       │                       │ RPT_ENV (数据)        │
 │                       │                       │◄──────────────────────│
 │                       │                       │                       │
 │                       │ ack (seq_id=xxx)      │ ← ack                 │
 │                       │ code=0                │                       │
 │                       │◄──────────────────────│                       │
 │                       │                       │                       │
 │                       │ query_result          │ ← 查询结果            │
 │                       │ seq_id=xxx            │                       │
 │                       │ battery=85, lux=300   │                       │
 │                       │◄──────────────────────│                       │
```

---

## 7. 接口变更

### 7.1 新增 SendEsp32Ack 方法

```cpp
// protocol.h

/**
 * @brief 发送 esp32_ack 响应（命令已收到）
 * 
 * @param seq_id 消息 ID
 * @param code   响应码（0=成功）
 * @param msg    响应消息
 */
virtual void SendEsp32Ack(const std::string& seq_id, int code = 0, 
                          const std::string& msg = "received");

/**
 * @brief 发送 ack 响应（执行完成）
 * 
 * @param seq_id 消息 ID
 * @param code   响应码（0=成功）
 * @param msg    响应消息
 */
virtual void SendAck(const std::string& seq_id, int code = 0, 
                     const std::string& msg = "OK");
```

### 7.2 消息格式

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
```

### 7.3 统一错误码定义

| code | 含义 | STM32 映射 | 说明 |
|------|------|------------|------|
| 0 | 成功 | ACK_OK | 命令执行成功 |
| 1 | 设备离线 | - | ESP32 未连接（Server 层） |
| 2 | 设备忙 | 0x01 ERR_BUSY | STM32 正在执行其他操作 |
| 3 | 参数错误 | 0x03 ERR_PARAM | 命令参数无效 |
| 4 | 不支持 | 0x02 ERR_UNSUPPORT | 不支持的命令 |
| 5 | 超时 | 0xFF ERR_TIMEOUT | 等待响应超时 |
| 6 | 硬件故障 | 0x06 ERR_HARDWARE | STM32 硬件异常 |
| 7 | 资源已满 | 0x04/0x05 ERR_FP/NFC_FULL | 指纹/NFC 存储已满 |
| 8 | 未认证 | - | 用户未登录（Server 层） |
| 9 | 重复消息 | - | seq_id 重复 |
| 10 | 内部错误 | - | 未知内部异常 |

### 7.4 错误码映射函数

```cpp
// ESP32 端：STM32 错误码 → 统一错误码
int MapStm32ErrorCode(uint8_t stm32_err) {
    switch (stm32_err) {
        case 0x01: return 2;   // ERR_BUSY → 设备忙
        case 0x02: return 4;   // ERR_UNSUPPORT → 不支持
        case 0x03: return 3;   // ERR_PARAM → 参数错误
        case 0x04: return 7;   // ERR_FP_FULL → 资源已满
        case 0x05: return 7;   // ERR_NFC_FULL → 资源已满
        case 0x06: return 6;   // ERR_HARDWARE → 硬件故障
        case 0xFF: return 5;   // ERR_TIMEOUT → 超时
        default:   return 10;  // 未知 → 内部错误
    }
}
```

---

## 8. 修改清单

### 8.1 文件修改列表

| 文件 | 修改类型 | 说明 |
|------|---------|------|
| `main/application.h` | 修改 | 新增 `PendingCommand` 结构体、`PendingCommandType` 枚举、`pending_commands_` 成员 |
| `main/application.cc` | 修改 | 实现 esp32_ack + ack 两级确认逻辑、超时清理 |
| `main/protocols/protocol.h` | 修改 | 新增 `SendEsp32Ack` 方法，更新 `SendAck` 方法 |
| `main/protocols/websocket_protocol.cc` | 修改 | 实现 `SendEsp32Ack` 和 `SendAck` |

### 8.2 新增功能

| 功能 | 说明 |
|------|------|
| esp32_ack + ack 两级确认 | esp32_ack 确认收到，ack 确认执行完成 |
| 待处理命令队列 | 保存命令的 seq_id 和状态 |
| 超时清理机制 | 根据命令类型设置不同超时时间 |
| STM32 ACK 匹配 | 通过 uart_type 匹配待处理命令 |
| 统一错误码 | 所有层共用一套错误码（0-10） |

---

## 9. 待讨论问题

### 9.1 并发操作处理

当前设计假设同一 UART TYPE 同时只有一个待处理命令。

**场景**：App 快速发送两个开锁命令
```
命令1: seq_id=aaa, command=unlock
命令2: seq_id=bbb, command=unlock
```

**处理方案**：
1. 第二个命令覆盖第一个（当前设计）
2. 第二个命令返回 code=2（设备忙）错误
3. 使用队列按顺序处理

**建议**：采用方案 2，在保存 pending 前检查是否已存在。

### 9.2 user_mgmt_result 是否保留

ack 已经携带了执行结果，`user_mgmt_result` 是否还需要？

**建议**：保留 `user_mgmt_result`，用于携带详细数据（如新分配的 ID）。ack 仅表示成功/失败。

---

## 10. 实施计划

| 阶段 | 任务 | 优先级 |
|------|------|--------|
| P0 | 新增 `PendingCommand` 数据结构 | 高 |
| P0 | 新增 `SendEsp32Ack` 方法 | 高 |
| P0 | 实现 esp32_ack（收到命令时） | 高 |
| P0 | 实现 ack（STM32 响应时） | 高 |
| P0 | 实现统一错误码映射 | 高 |
| P1 | 实现超时清理机制 | 中 |
| P1 | 并发操作检测和拒绝 | 中 |
| P2 | 更新协议文档 | 低 |

---

## 11. 版本历史

| 版本 | 日期 | 变更说明 |
|------|------|----------|
| v3.0 | 2026-01-13 | 统一使用 seq_id；区分 esp32_ack（收到）和 ack（完成）；统一错误码（0-10） |
| v2.0 | 2026-01-13 | 重构为两级 ACK 机制 |
| v1.0 | 2026-01-13 | 初始版本 |

---

**文档维护者**：毕业设计项目组  
**最后更新**：2026-01-13
