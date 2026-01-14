# ESP32 锁控协议升级需求

> 版本：v1.0  
> 创建日期：2026-01-14  
> 状态：待实施

---

## 1. 背景

STM32 端通信协议从 v2.1 升级到 v2.7，ESP32 端代码需要同步更新以支持新协议。

---

## 2. 协议变更汇总

| 版本 | 变更内容 |
|------|----------|
| v2.4 | 空值字段从 0x00 改为 0xFF |
| v2.5 | 新增 EVT_LOCK_STATUS 事件 |
| v2.6 | 拆分 RPT_UNLOCK 和 RPT_DOOR_OPENED |
| v2.7 | ACK 响应规则优化，CMD_LOCK 幂等设计 |

---

## 3. 详细修改清单

### 3.1 lock_protocol.h 修改

#### 3.1.1 新增上报类型

```cpp
enum class RptType : uint8_t {
  RPT_EVENT       = 0xA0,  ///< 事件上报
  RPT_UNLOCK      = 0xA1,  ///< 开锁日志（开锁命令执行时上报）
  RPT_DOOR_OPENED = 0xA2,  ///< 开门日志（用户实际开门时上报）【新增】
  RPT_ENV         = 0xB0,  ///< 环境数据上报
  RPT_STATE       = 0xB1,  ///< 状态上报
  RPT_PWD         = 0xC0,  ///< 密码查询响应
};
```

#### 3.1.2 新增事件 ID

```cpp
enum class EventId : uint8_t {
  EVT_DOORBELL    = 0x01,  ///< 门铃按下
  EVT_PIR         = 0x02,  ///< PIR 人体检测
  EVT_TAMPER      = 0x03,  ///< 防拆报警
  EVT_DOOR_OPEN   = 0x04,  ///< 门未关超时
  EVT_LOW_BATTERY = 0x05,  ///< 低电量警告
  EVT_LOCK_STATUS = 0x06,  ///< 关门/上锁状态【新增】
};
```

#### 3.1.3 新增锁状态码枚举

```cpp
/**
 * @brief 锁状态码（EVT_LOCK_STATUS 的 D1 字段）
 */
enum class LockStatusCode : uint8_t {
  DOOR_CLOSED   = 0x00,  ///< 门关闭
  LOCK_SUCCESS  = 0x01,  ///< 上锁成功
  BOLT_ALARM    = 0x02,  ///< 锁舌未到位报警
};
```

#### 3.1.4 新增开门来源枚举

```cpp
/**
 * @brief 开门来源（RPT_DOOR_OPENED 的 D1 字段）
 */
enum class DoorSource : uint8_t {
  OUTSIDE = 0x00,  ///< 室外开门（PIR 检测到人体）
  INSIDE  = 0x01,  ///< 室内开门（PIR 未检测到人体）
  UNKNOWN = 0xFF,  ///< 未知/不适用
};
```

#### 3.1.5 新增指纹/NFC 响应状态码

```cpp
enum class FpRespStatus : uint8_t {
  FP_PRESS_FINGER   = 0x01,  ///< 请按压手指
  FP_LIFT_FINGER    = 0x02,  ///< 请抬起手指
  FP_SUCCESS        = 0x03,  ///< 录入成功
  FP_FAILED         = 0x04,  ///< 录入失败
  FP_COUNT_RESP     = 0x05,  ///< 数量查询响应
  FP_ALREADY_EXISTS = 0x06,  ///< 指纹已存在【新增】
  FP_ID_OCCUPIED    = 0x07,  ///< ID 被占用，自动分配新 ID【新增】
};
```

#### 3.1.6 新增协议空值常量

```cpp
/** 协议空值（未使用字段填充） */
constexpr uint8_t LOCK_PROTOCOL_EMPTY = 0xFF;
```

### 3.2 lock_protocol.cc 修改

#### 3.2.1 BuildAckOk 修改

```cpp
// 旧版
std::vector<uint8_t> LockProtocol::BuildAckOk(uint8_t orig_type) {
  std::array<uint8_t, 3> data = {orig_type, 0x00, 0x00};  // ❌ 旧版
  ...
}

// 新版
std::vector<uint8_t> LockProtocol::BuildAckOk(uint8_t orig_type) {
  std::array<uint8_t, 3> data = {orig_type, LOCK_PROTOCOL_EMPTY, LOCK_PROTOCOL_EMPTY};  // ✅ 新版
  ...
}
```

#### 3.2.2 BuildAckErr 修改

```cpp
// 旧版
std::vector<uint8_t> LockProtocol::BuildAckErr(uint8_t orig_type, AckError error) {
  std::array<uint8_t, 3> data = {orig_type, static_cast<uint8_t>(error), 0x00};  // ❌ 旧版
  ...
}

// 新版
std::vector<uint8_t> LockProtocol::BuildAckErr(uint8_t orig_type, AckError error) {
  std::array<uint8_t, 3> data = {orig_type, static_cast<uint8_t>(error), LOCK_PROTOCOL_EMPTY};  // ✅ 新版
  ...
}
```

### 3.3 lock_control.cc 修改

#### 3.3.1 发送命令时的空值字段

所有发送命令时，未使用的字段从 `0x00` 改为 `0xFF`：

```cpp
// 示例：发送开锁命令
// 旧版
std::array<uint8_t, 3> data = {static_cast<uint8_t>(mode), hold_seconds, 0x00};

// 新版
std::array<uint8_t, 3> data = {static_cast<uint8_t>(mode), hold_seconds, LOCK_PROTOCOL_EMPTY};
```

**需要修改的函数**：
- `SendLock()` - D2 改为 0xFF
- `SendOledIcon()` - D1/D2 改为 0xFF
- `SendBeep()` - D2 改为 0xFF
- `SendLight()` - D1/D2 改为 0xFF
- `QuerySensors()` - D0/D1/D2 改为 0xFF
- `QueryStatus()` - D0/D1/D2 改为 0xFF
- `FingerprintEnroll()` - D2 改为 0xFF
- `FingerprintDelete()` - D2 改为 0xFF
- `FingerprintClear()` - D0/D1/D2 改为 0xFF
- `FingerprintCount()` - D0/D1/D2 改为 0xFF
- `NfcXxx()` - 同上
- `QueryPassword()` - D0/D1/D2 改为 0xFF
- `SendPing()` - D0/D1/D2 改为 0xFF

### 3.4 application.cc 修改

#### 3.4.1 新增 RPT_DOOR_OPENED 处理

```cpp
void Application::HandleLockReportMessage(const xiaozhi::LockMessage& msg) {
  switch (static_cast<xiaozhi::RptType>(msg.type)) {
    case xiaozhi::RptType::RPT_UNLOCK:
      HandleUnlockReport(msg);
      break;
    case xiaozhi::RptType::RPT_DOOR_OPENED:  // 【新增】
      HandleDoorOpenedReport(msg);
      break;
    // ...
  }
}

// 新增处理函数
void Application::HandleDoorOpenedReport(const xiaozhi::LockMessage& msg) {
  uint8_t method = msg.data[0];      // 开锁方式
  uint8_t source = msg.data[1];      // 开门来源：0x00=室外, 0x01=室内
  
  // 上报服务器
  // ...
}
```

#### 3.4.2 新增 EVT_LOCK_STATUS 处理

```cpp
void Application::HandleLockEvent(const xiaozhi::LockMessage& msg) {
  xiaozhi::EventId event_id = static_cast<xiaozhi::EventId>(msg.data[0]);
  
  switch (event_id) {
    // ...
    case xiaozhi::EventId::EVT_LOCK_STATUS: {  // 【新增】
      uint8_t status = msg.data[1];
      switch (status) {
        case 0x00:  // 门关闭
          ESP_LOGI(TAG, "门已关闭");
          break;
        case 0x01:  // 上锁成功
          ESP_LOGI(TAG, "自动上锁成功");
          break;
        case 0x02:  // 锁舌报警
          ESP_LOGW(TAG, "锁舌未到位报警");
          // 上报服务器告警
          break;
      }
      break;
    }
    // ...
  }
}
```

#### 3.4.3 处理指纹/NFC 新状态码

```cpp
void Application::HandleFingerprintResponse(const xiaozhi::LockMessage& msg) {
  uint8_t status = msg.data[0];
  uint8_t param = msg.data[1];
  
  switch (status) {
    // ...
    case 0x06:  // 指纹已存在【新增】
      ESP_LOGI(TAG, "指纹已存在，ID=%d", param);
      // 返回已有 ID 给服务器
      break;
    case 0x07:  // ID 被占用【新增】
      ESP_LOGI(TAG, "指定 ID 被占用，自动分配新 ID=%d，原 ID=%d", param, msg.data[2]);
      // 返回新分配的 ID 给服务器
      break;
  }
}
```

---

## 4. 修改文件清单

| 文件 | 修改类型 | 说明 |
|------|----------|------|
| `main/lock_control/lock_protocol.h` | 修改 | 新增枚举、常量 |
| `main/lock_control/lock_protocol.cc` | 修改 | 空值字段改为 0xFF |
| `main/lock_control/lock_control.cc` | 修改 | 发送命令空值改为 0xFF |
| `main/application.cc` | 修改 | 新增消息处理逻辑 |

---

## 5. 测试要点

### 5.1 空值字段验证

- [ ] 发送 CMD_LOCK 命令，D2 应为 0xFF
- [ ] 发送 Q_SENSORS 命令，D0/D1/D2 应为 0xFF
- [ ] 收到 ACK_OK，D1/D2 应为 0xFF

### 5.2 新消息类型验证

- [ ] 收到 RPT_DOOR_OPENED (0xA2)，正确解析开门来源
- [ ] 收到 EVT_LOCK_STATUS (0x06)，正确处理三种状态码
- [ ] 收到指纹响应 0x06/0x07，正确处理特殊场景

### 5.3 ACK 响应验证

- [ ] CMD_LOCK 命令等待执行后 ACK
- [ ] Q_SENSORS 命令等待 ACK + RPT_ENV
- [ ] 长流程命令（指纹录入）等待最终结果

---

## 6. 版本历史

| 版本 | 日期 | 变更说明 |
|------|------|----------|
| v1.0 | 2026-01-14 | 初始版本 |

---

**文档维护者**：毕业设计项目组  
**最后更新**：2026-01-14
