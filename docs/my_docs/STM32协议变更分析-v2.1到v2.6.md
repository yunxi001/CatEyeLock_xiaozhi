# STM32 协议变更分析 (v2.1 → v2.6)

> 生成日期：2026-01-13  
> 对比文件：`智能猫眼门锁系统-STM32端.md` (v2.1) vs `智能猫眼门锁系统-STM32端 - 副本.md` (v2.6)

---

## 1. 版本演进概览

| 版本 | 日期 | 主要变更 |
|------|------|----------|
| v2.2 | 2026-01-06 | NFC 录入新增状态码 0x06/0x07 |
| v2.3 | 2026-01-06 | 指纹录入新增状态码 0x06/0x07，ID 范围改为 1-40 |
| v2.4 | 2026-01-12 | 协议空值统一改为 0xFF |
| v2.5 | 2026-01-12 | 新增 EVT_LOCK_STATUS 事件，新增自动上锁场景 |
| v2.6 | 2026-01-12 | 拆分 RPT_UNLOCK/RPT_DOOR_OPENED，新增开门来源字段 |

---

## 2. 核心变更详情

### 2.1 空值统一改为 0xFF (v2.4)

**变更原因**：便于区分有效数据（0x00 可能是有效值）和未使用字段

**影响范围**：

| 消息类型 | 字段 | 旧值 | 新值 |
|----------|------|------|------|
| ACK_OK | D1, D2 | 0x00 | 0xFF |
| ACK_ERR | D2 | 0x00 | 0xFF |
| CMD_LOCK | D2 | 0x00 | 0xFF |
| CMD_OLED | D1, D2 | 0x00 | 0xFF |
| CMD_BEEP | D2 | 0x00 | 0xFF |
| CMD_LIGHT | D1, D2 | 0x00 | 0xFF |
| Q_SENSORS | D0, D1, D2 | 0x00 | 0xFF |
| Q_STATUS | D0, D1, D2 | 0x00 | 0xFF |
| RPT_EVENT | D2 | 0x00 | 0xFF |
| RPT_STATE | D2 | 0x00 | 0xFF |
| SYS_PING/PONG | D0, D1, D2 | 0x00 | 0xFF |
| 指纹/NFC 管理 | 未使用字段 | 0x00 | 0xFF |
| 密码查询 | D0, D1, D2 | 0x00 | 0xFF |

---

### 2.2 新增 EVT_LOCK_STATUS 事件 (v2.5)

**新增事件 ID**：`0x06`

**状态码定义**：

| D1 值 | 宏定义 | 说明 |
|-------|--------|------|
| 0x00 | `LOCK_STATUS_DOOR_CLOSED` | 门关闭（MPU6050 检测） |
| 0x01 | `LOCK_STATUS_LOCK_SUCCESS` | 上锁成功（PA15 确认） |
| 0x02 | `LOCK_STATUS_BOLT_ALARM` | 锁舌未到位报警（30秒超时） |

**使用场景**：自动上锁流程

```
门关闭 → EVT_LOCK_STATUS(0x00) → 15秒倒计时 → 上锁 → EVT_LOCK_STATUS(0x01/0x02)
```

---

### 2.3 拆分开锁/开门上报 (v2.6 核心变更)

**原设计 (v2.1)**：
- `RPT_UNLOCK (0xA1)` 同时承担开锁日志和开门日志功能

**新设计 (v2.6)**：

| 消息 | TYPE | 触发时机 | 用途 |
|------|------|----------|------|
| `RPT_UNLOCK` | 0xA1 | 开锁命令执行成功 | 记录开锁操作 |
| `RPT_DOOR_OPENED` | 0xA2 | 用户实际开门 | 记录开门行为 |

**RPT_UNLOCK (0xA1) 字段变更**：

| 字段 | v2.1 | v2.6 |
|------|------|------|
| D0 | 开锁方式 | 开锁方式（不变） |
| D1 | ID/0x00 | 用户ID（1-40 或 0xFF） |
| D2 | 结果 | 结果（不变） |

**新增 RPT_DOOR_OPENED (0xA2)**：

| 字段 | 说明 |
|------|------|
| D0 | 开锁方式（与 RPT_UNLOCK 相同） |
| D1 | 开门来源 |
| D2 | 0xFF（保留） |

**开门来源 (D1)**：

| D1 值 | 宏定义 | 说明 | 判断逻辑 |
|-------|--------|------|----------|
| 0x00 | `DOOR_SOURCE_OUTSIDE` | 室外开门 | 开门时间 - PIR 时间 ≤ 5秒 |
| 0x01 | `DOOR_SOURCE_INSIDE` | 室内开门 | PIR 未在 5 秒内触发 |
| 0xFF | - | 未知/不适用 | - |

---

### 2.4 指纹/NFC 录入增强 (v2.2/v2.3)

**新增状态码**：

| 状态码 | 说明 | D1 | D2 |
|--------|------|-----|-----|
| 0x06 | 指纹/UID 已存在 | 已有槽位 ID | 0xFF |
| 0x07 | 指定 ID 被占用 | 新分配 ID | 原指定 ID |

**特殊场景处理**：

| 场景 | STM32 行为 | 反馈 |
|------|------------|------|
| 指纹已存在 | 不重复录入 | 0x06 + 已有 ID |
| ID 被占用 | 自动分配新 ID | 0x07 + 新 ID + 原 ID |

**ID 范围明确**：1-40（对外），内部索引 0-39

---

## 3. 新增场景流程

### 3.1 场景 8：自动上锁流程

```
1. 用户开门后关门，MPU6050 检测到门关闭
   STM → ESP: [AA][01][A0][06][00][FF][CS]  (EVT_LOCK_STATUS: 门关闭)

2. STM32 启动 15 秒自动上锁倒计时

3. 倒计时结束，STM32 执行上锁（电机反转）

4. PA12 中断触发，电机停止（锁舌弹出到位）

5. STM32 等待 PA15 锁舌确认（30 秒超时）

6a. PA15 触发，上锁成功
    STM → ESP: [AA][01][A0][06][01][FF][CS]  (EVT_LOCK_STATUS: 上锁成功)

6b. 30 秒超时，锁舌未到位报警
    STM → ESP: [AA][01][A0][06][02][FF][CS]  (EVT_LOCK_STATUS: 锁舌报警)
```

### 3.2 场景 9：室内/室外开门检测

```
1. PIR 传感器检测到人体（室外有人）
   STM32 记录 PIR 触发时间戳

2. 用户通过指纹开锁，STM32 上报开锁日志
   STM → ESP: [AA][01][A1][01][05][00][CS]  (RPT_UNLOCK: 指纹, 用户ID=5, 成功)

3. 用户开门，STM32 判断开门来源
   开门时间 - PIR 时间 <= 5 秒 → 室外开门

4. STM32 上报开门日志（D1=0x00 表示室外）
   STM → ESP: [AA][01][A2][01][00][FF][CS]  (RPT_DOOR_OPENED: 指纹, 室外开门)

5. 若 PIR 未在 5 秒内触发，则判定为室内开门
   STM → ESP: [AA][01][A2][01][01][FF][CS]  (RPT_DOOR_OPENED: 指纹, 室内开门)
```

### 3.3 场景 10：本地指纹开锁完整流程

```
1. 用户按压指纹，STM32 验证成功

2. STM32 执行开锁（电机正转）

3. PA12 中断触发，电机停止（开锁到位）

4. STM32 上报开锁日志
   STM → ESP: [AA][01][A1][01][03][00][CS]  (RPT_UNLOCK: 指纹, 用户ID=3, 成功)

5. 用户开门，MPU6050 检测到门打开

6. STM32 上报开门日志
   STM → ESP: [AA][01][A2][01][00][FF][CS]  (RPT_DOOR_OPENED: 指纹, 室外开门)
```

---

## 4. 对 ESP32 代码的影响

### 4.1 需要修改的文件

| 文件 | 修改内容 |
|------|----------|
| `lock_protocol.h` | 新增枚举值、结构体字段 |
| `lock_protocol.cc` | 更新解析逻辑 |
| `lock_control.cc` | 处理新事件类型 |
| `application.cc` | 转发新事件到服务器 |

### 4.2 新增枚举值

```cpp
// 事件 ID
enum EventId {
    // ... 现有值 ...
    EVT_LOCK_STATUS = 0x06,  // 新增
};

// 锁状态码
enum LockStatusCode {
    LOCK_STATUS_DOOR_CLOSED = 0x00,
    LOCK_STATUS_LOCK_SUCCESS = 0x01,
    LOCK_STATUS_BOLT_ALARM = 0x02,
};

// 上报类型
enum ReportType {
    RPT_EVENT = 0xA0,
    RPT_UNLOCK = 0xA1,
    RPT_DOOR_OPENED = 0xA2,  // 新增
    // ...
};

// 开门来源
enum DoorSource {
    DOOR_SOURCE_OUTSIDE = 0x00,
    DOOR_SOURCE_INSIDE = 0x01,
    DOOR_SOURCE_UNKNOWN = 0xFF,
};

// 指纹/NFC 反馈状态
enum UserMgmtStatus {
    // ... 现有值 ...
    STATUS_ALREADY_EXISTS = 0x06,  // 新增
    STATUS_ID_OCCUPIED = 0x07,     // 新增
};
```

---

## 5. 对服务器协议的影响

### 5.1 需要新增的事件类型

| event | 说明 | param 含义 |
|-------|------|------------|
| `door_closed` | 门关闭 | 无 |
| `lock_success` | 上锁成功 | 无 |
| `bolt_alarm` | 锁舌未到位 | 无 |

### 5.2 需要新增的上报消息

**开门日志上报 (door_opened_report)**：

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

| 字段 | 类型 | 说明 |
|------|------|------|
| method | string | 开锁方式（与 log_report 相同） |
| source | string | 开门来源：`outside`/`inside`/`unknown` |

### 5.3 用户管理结果扩展

新增错误场景：

| result | val | msg |
|--------|-----|-----|
| false | 0x06 | "Already exists" |
| true | 新ID | "ID occupied, auto assigned" |

---

## 6. 数据库表变更建议

### 6.1 新增 door_events 表

```sql
CREATE TABLE door_events (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    device_id VARCHAR(64) NOT NULL,
    event_type ENUM('door_closed', 'lock_success', 'bolt_alarm') NOT NULL,
    created_at DATETIME NOT NULL,
    INDEX idx_device_time (device_id, created_at)
);
```

### 6.2 新增 door_opened_logs 表

```sql
CREATE TABLE door_opened_logs (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    device_id VARCHAR(64) NOT NULL,
    method VARCHAR(16) NOT NULL,
    source ENUM('outside', 'inside', 'unknown') NOT NULL,
    created_at DATETIME NOT NULL,
    INDEX idx_device_time (device_id, created_at)
);
```

### 6.3 或扩展现有 unlock_logs 表

```sql
ALTER TABLE unlock_logs 
ADD COLUMN source ENUM('outside', 'inside', 'unknown') DEFAULT 'unknown' AFTER fail_count;
```

---

## 7. 总结

### 7.1 变更优先级

| 优先级 | 变更项 | 原因 |
|--------|--------|------|
| 高 | 空值改为 0xFF | 影响所有消息解析 |
| 高 | 拆分 RPT_UNLOCK/RPT_DOOR_OPENED | 核心业务逻辑变更 |
| 中 | 新增 EVT_LOCK_STATUS | 新增功能 |
| 低 | 指纹/NFC 状态码扩展 | 边缘场景处理 |

### 7.2 兼容性考虑

- 空值变更需要 ESP32 和 STM32 同步升级
- 新增消息类型可向后兼容（旧版本忽略未知类型）
- 建议在协议中增加版本协商机制



---

## 8. 服务器协议修改建议

基于 STM32 协议 v2.6 的变更，`智能猫眼门锁系统-服务器与ESP32通信协议规范-v5.0.md` 需要进行以下修改：

### 8.1 事件上报 (event_report) 扩展

**当前定义 (v5.0)**：

| event | 说明 | param 含义 | STM32 事件ID |
|-------|------|------------|--------------|
| `bell` | 门铃按下 | 无 | 0x01 |
| `pir_trigger` | PIR 人体检测 | 持续时间(秒) | 0x02 |
| `tamper` | 撬锁报警 | 报警级别 (1-3) | 0x03 |
| `door_open` | 门未关超时 | 超时时间(分钟) | 0x04 |
| `low_battery` | 低电量警告 | 当前电量(%) | 0x05 |

**需要新增**：

| event | 说明 | param 含义 | STM32 事件ID |
|-------|------|------------|--------------|
| `door_closed` | 门关闭 | 无 | 0x06 (D1=0x00) |
| `lock_success` | 自动上锁成功 | 无 | 0x06 (D1=0x01) |
| `bolt_alarm` | 锁舌未到位报警 | 无 | 0x06 (D1=0x02) |

**修改位置**：第 4.2 节 "关键事件上报"

---

### 8.2 新增开门日志上报 (door_opened_report)

**需要在第 4 节新增 4.4 小节**：

```markdown
### 4.4 开门日志上报 (door_opened_report)

**触发条件：** 用户实际开门时（门从关闭变为打开）

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

| 字段 | 类型 | 说明 |
|------|------|------|
| method | string | 开锁方式（与 log_report 相同） |
| source | string | 开门来源 |

**source 取值：**

| source | 说明 | 判断逻辑 |
|--------|------|----------|
| `outside` | 室外开门 | PIR 在开门前 5 秒内检测到人体 |
| `inside` | 室内开门 | PIR 未在 5 秒内触发 |
| `unknown` | 未知 | 无法判断 |

> **Server 处理**：存储到数据库，并转发给所有关联的 App
```

---

### 8.3 开锁日志上报 (log_report) 说明更新

**当前描述**：
> **触发条件：** 用户尝试开锁时

**建议修改为**：
> **触发条件：** 开锁命令执行成功时（不包含实际开门动作）

**新增说明**：
> **注意**：`log_report` 仅记录开锁操作，实际开门行为由 `door_opened_report` 单独上报。

---

### 8.4 用户管理结果扩展

**当前错误码表**：

| val | 说明 |
|-----|------|
| 0x01 | 设备忙碌 |
| 0x02 | 不支持 |
| 0x03 | 参数错误 |
| 0x04 | 指纹库已满 |
| 0x05 | NFC 库已满 |
| 0x06 | 硬件故障 |
| 0xFF | 超时 |

**需要新增成功场景说明**：

```markdown
**特殊成功场景：**

| 场景 | result | val | msg |
|------|--------|-----|-----|
| 正常录入 | true | 新分配 ID | "Success" |
| 指纹/卡片已存在 | true | 已有槽位 ID | "Already exists at slot X" |
| 指定 ID 被占用 | true | 新分配 ID | "ID occupied, assigned to slot X" |
```

---

### 8.5 消息类型汇总更新

**第 11.1 节 ESP32 上报消息表需要新增**：

| type | 说明 | Server 处理 |
|------|------|-------------|
| `door_opened_report` | 开门日志 | 存储 + 转发给 App |

---

### 8.6 数据存储规范更新

**第 13.2 节需要新增表设计**：

#### door_opened_logs (开门日志)

| 字段 | 类型 | 说明 |
|------|------|------|
| id | BIGINT | 自增主键 |
| device_id | VARCHAR(64) | 设备 ID |
| method | VARCHAR(16) | 开锁方式 |
| source | ENUM('outside','inside','unknown') | 开门来源 |
| created_at | DATETIME | 记录时间 |

**或扩展现有 unlock_logs 表**：

```sql
ALTER TABLE unlock_logs 
ADD COLUMN source ENUM('outside', 'inside', 'unknown') DEFAULT NULL AFTER fail_count,
ADD COLUMN is_door_opened TINYINT DEFAULT 0 COMMENT '是否实际开门';
```

---

### 8.7 实现状态更新

**第 17.1 节 ESP32 端实现表需要新增**：

| 功能 | 代码位置 | 状态 |
|------|----------|------|
| 开门日志上报 | `WebsocketProtocol::SendDoorOpenedReport()` | ⏳ 待实现 |
| 锁状态事件处理 | `Application::HandleLockStatusEvent()` | ⏳ 待实现 |

**第 17.2 节 Server 端实现表需要新增**：

| 功能 | 代码位置 | 状态 |
|------|----------|------|
| 开门日志处理 | `core/handle/textHandler/doorOpenedReportHandler.py` | ⏳ 待实现 |

---

### 8.8 版本历史更新

**需要新增版本记录**：

| 版本 | 日期 | 变更说明 |
|------|------|----------|
| v5.1 | 2026-01-13 | 新增 door_opened_report 开门日志；扩展 event_report 支持锁状态事件；用户管理结果新增特殊场景 |

---

## 9. 修改清单汇总

| 章节 | 修改类型 | 具体内容 |
|------|----------|----------|
| 4.2 | 扩展 | 新增 door_closed/lock_success/bolt_alarm 事件 |
| 4.4 | 新增 | 新增 door_opened_report 消息定义 |
| 4.3 | 更新 | 更新 log_report 触发条件说明 |
| 7.2 | 扩展 | 用户管理结果新增特殊成功场景 |
| 11.1 | 扩展 | 消息类型汇总新增 door_opened_report |
| 13.2 | 新增 | 数据库表设计新增 door_opened_logs |
| 17.1 | 更新 | ESP32 实现状态新增待实现功能 |
| 17.2 | 更新 | Server 实现状态新增待实现功能 |
| 18 | 更新 | 版本历史新增 v5.1 |

