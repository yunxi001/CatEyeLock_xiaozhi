# ESP32 固件更新指南 - 霍尔传感器移除适配

## 文档信息

- **创建日期**: 2026-04-30
- **版本**: v1.0
- **目标**: ESP32 端适配 STM32 霍尔传感器移除

---

## 一、变更概述

### 1.1 STM32 端变更

- ❌ 移除 PA12 霍尔传感器（电机到位检测）
- ❌ 移除 PA15 霍尔传感器（锁舌闭合检测）
- ✅ 简化状态机：8个状态 → 2个状态
- ✅ 改用固定时间控制（2秒）

### 1.2 ESP32 需要适配的内容

- 🔴 **必须修改**: 删除锁舌报警事件处理
- 🔴 **必须修改**: 简化状态机同步逻辑
- 🟡 **建议修改**: 简化 UI 显示
- 🟡 **建议修改**: 删除锁舌报警推送通知

### 1.3 不受影响的功能

- ✅ 开锁日志记录（`RPT_UNLOCK`）
- ✅ 开门日志记录（`RPT_DOOR_OPENED`）
- ✅ 环境数据上报（`RPT_ENV`）
- ✅ 状态查询（`Q_STATUS`）
- ✅ 用户管理（指纹/NFC/密码）
- ✅ 远程控制（`CMD_LOCK`）
- ✅ 其他事件（门铃、PIR、撬锁、低电量）

---

## 二、协议变更

### 2.1 删除的协议内容

#### 事件类型: `EVT_LOCK_STATUS` (0x06)

**原有状态码:**

```python
LOCK_STATUS_DOOR_CLOSED   = 0x00  # 门关闭 ✅ 保留
LOCK_STATUS_LOCK_SUCCESS  = 0x01  # 上锁成功 ✅ 保留
LOCK_STATUS_BOLT_ALARM    = 0x02  # 锁舌未到位报警 ❌ 删除
```

**修改后:**

```python
LOCK_STATUS_DOOR_CLOSED   = 0x00  # 门关闭
LOCK_STATUS_LOCK_SUCCESS  = 0x01  # 上锁成功
# LOCK_STATUS_BOLT_ALARM (0x02) 不再使用
```

### 2.2 保留的协议（不变）

| 协议类型          | 代码 | 说明       | 状态    |
| ----------------- | ---- | ---------- | ------- |
| `RPT_UNLOCK`      | 0xA1 | 开锁日志   | ✅ 不变 |
| `RPT_DOOR_OPENED` | 0xA2 | 开门日志   | ✅ 不变 |
| `RPT_ENV`         | 0xB0 | 环境数据   | ✅ 不变 |
| `RPT_STATE`       | 0xB1 | 状态上报   | ✅ 不变 |
| `EVT_DOORBELL`    | 0x01 | 门铃事件   | ✅ 不变 |
| `EVT_PIR`         | 0x02 | PIR 检测   | ✅ 不变 |
| `EVT_TAMPER`      | 0x03 | 撬锁报警   | ✅ 不变 |
| `EVT_DOOR_OPEN`   | 0x04 | 门未关超时 | ✅ 不变 |
| `EVT_LOW_BATTERY` | 0x05 | 低电量     | ✅ 不变 |

---

## 四、状态同步逻辑修改

### 4.1 开锁流程

**修改前（复杂流程）:**

```python
# STM32 状态转换
LOCKED_CLOSED → UNLOCKING → UNLOCKED_CLOSED → UNLOCKED_OPEN

# ESP32 同步
1. 收到开锁命令 → 状态: UNLOCKING
2. 收到 RPT_UNLOCK → 状态: UNLOCKED_CLOSED
3. 检测门打开 → 状态: UNLOCKED_OPEN
```

**修改后（简化流程）:**

```python
# STM32 状态转换
LOCKED → UNLOCKED

# ESP32 同步
1. 收到开锁命令 → 状态: UNLOCKED
2. 收到 RPT_UNLOCK → 确认状态: UNLOCKED
```

**代码示例:**

---

### 4.2 上锁流程

**修改前（复杂流程）:**

```python
# STM32 状态转换
UNLOCKED_OPEN → AUTO_LOCK_PENDING → LOCKING → BOLT_CONFIRMING → LOCKED_CLOSED
                                                      ↓
                                                 BOLT_ALARM (超时)

# ESP32 同步
1. 门关闭 → 状态: AUTO_LOCK_PENDING
2. 15秒后 → 状态: LOCKING
3. 收到 EVT_LOCK_STATUS(LOCK_SUCCESS) → 状态: LOCKED_CLOSED
4. 或收到 EVT_LOCK_STATUS(BOLT_ALARM) → 状态: BOLT_ALARM
```

**修改后（简化流程）:**

```python
# STM32 状态转换
UNLOCKED → LOCKED

# ESP32 同步
1. 门关闭 → 启动 15 秒倒计时
2. 15秒后 → 状态: LOCKED
3. 收到 EVT_LOCK_STATUS(LOCK_SUCCESS) → 确认状态: LOCKED
```

---

---

## 六、数据库修改

### 6.1 事件表修改

**添加废弃标记:**

```sql
-- 添加字段
ALTER TABLE lock_events
  ADD COLUMN is_deprecated BOOLEAN DEFAULT FALSE;

-- 标记锁舌报警事件为已废弃
UPDATE lock_events
  SET is_deprecated = TRUE
  WHERE event_type = 'bolt_alarm';

-- 查询时过滤废弃事件
SELECT * FROM lock_events
  WHERE is_deprecated = FALSE;
```

### 6.2 状态表修改

**简化状态枚举:**

```sql
-- 旧状态表
CREATE TABLE lock_states (
  state_id INT PRIMARY KEY,
  state_name VARCHAR(50),
  -- 0: LOCKED_CLOSED
  -- 1: UNLOCKING
  -- 2: UNLOCKED_CLOSED
  -- 3: UNLOCKED_OPEN
  -- 4: AUTO_LOCK_PENDING
  -- 5: LOCKING
  -- 6: BOLT_CONFIRMING  ← 废弃
  -- 7: BOLT_ALARM       ← 废弃
);

-- 新状态表
CREATE TABLE lock_states_v2 (
  state_id INT PRIMARY KEY,
  state_name VARCHAR(50),
  -- 0: LOCKED
  -- 1: UNLOCKED
);

-- 数据迁移
INSERT INTO lock_states_v2 (state_id, state_name)
SELECT
  CASE
    WHEN state_id IN (0, 5, 6, 7) THEN 0  -- LOCKED
    WHEN state_id IN (1, 2, 3, 4) THEN 1  -- UNLOCKED
  END as state_id,
  CASE
    WHEN state_id IN (0, 5, 6, 7) THEN 'LOCKED'
    WHEN state_id IN (1, 2, 3, 4) THEN 'UNLOCKED'
  END as state_name
FROM lock_states
GROUP BY state_id;
```

---

## 七、测试计划

### 7.1 协议测试

| 测试项   | 测试方法 | 预期结果                         |
| -------- | -------- | -------------------------------- |
| 开锁日志 | 刷卡开锁 | 收到 `RPT_UNLOCK`                |
| 上锁成功 | 自动上锁 | 收到 `EVT_LOCK_STATUS(0x01)`     |
| 门关闭   | 关门     | 收到 `EVT_LOCK_STATUS(0x00)`     |
| 锁舌报警 | -        | 不再收到 `EVT_LOCK_STATUS(0x02)` |

### 7.2 状态同步测试

| 测试项   | 测试方法 | 预期结果                   |
| -------- | -------- | -------------------------- |
| 开锁同步 | 远程开锁 | ESP32 状态更新为 UNLOCKED  |
| 上锁同步 | 自动上锁 | ESP32 状态更新为 LOCKED    |
| 状态查询 | 查询状态 | 返回正确的 LOCKED/UNLOCKED |

### 7.3 兼容性测试

| 测试项              | 测试方法     | 预期结果   |
| ------------------- | ------------ | ---------- |
| 新 ESP32 + 新 STM32 | 完整流程测试 | 正常工作   |
| 新 ESP32 + 旧 STM32 | 完整流程测试 | 兼容工作   |
| 旧 ESP32 + 新 STM32 | 完整流程测试 | 超时后正常 |

---

## 八、注意事项

### 8.1 关键注意点

1. **状态更新时机**
   - ⚠️ 新版本不再等待硬件确认
   - ⚠️ 基于固定时间（2秒）更新状态
   - ⚠️ 可能出现状态与实际不符的情况

2. **超时处理**
   - ✅ 添加超时保护机制
   - ✅ 超时后自动更新状态
   - ✅ 记录警告日志但不阻塞流程

3. **用户提示**
   - ✅ UI 显示"请手动确认门已锁好"
   - ✅ 推送通知添加免责说明
   - ✅ 帮助文档更新操作指南

### 8.2 错误处理

**场景 1: 开锁命令发送失败**

```python
def send_unlock_command():
    try:
        send_command(CMD_LOCK, action=0x01)
        update_lock_status(DoorLockState.UNLOCKED)
    except CommunicationError:
        print("错误：开锁命令发送失败")
        # 不更新状态
        show_error_message("通信失败，请重试")
```

**场景 2: 状态不一致**

```python
def handle_state_mismatch():
    """处理状态不一致"""
    # 定期查询 STM32 状态
    stm32_state = query_stm32_state()
    esp32_state = get_local_state()

    if stm32_state != esp32_state:
        print(f"警告：状态不一致 STM32={stm32_state} ESP32={esp32_state}")
        # 以 STM32 状态为准
        update_lock_status(stm32_state)
```

### 8.3 日志记录

**建议添加详细日志:**

```python
def log_state_change(old_state, new_state, reason):
    """记录状态变化"""
    log_entry = {
        "timestamp": now(),
        "old_state": old_state,
        "new_state": new_state,
        "reason": reason,
        "stm32_version": get_stm32_version(),
        "esp32_version": get_esp32_version()
    }

    # 记录到本地和云端
    log_to_local(log_entry)
    log_to_cloud(log_entry)
```

---

## 九、部署建议

### 9.1 分阶段部署

| 阶段   | 范围       | 时间 | 回退方案 |
| ------ | ---------- | ---- | -------- |
| 阶段 1 | 测试环境   | 1 周 | 立即回退 |
| 阶段 2 | 10 台设备  | 1 周 | 远程回退 |
| 阶段 3 | 100 台设备 | 2 周 | OTA 回退 |
| 阶段 4 | 全部设备   | 4 周 | 分批回退 |

### 9.2 监控指标

**需要监控的指标:**

| 指标         | 阈值  | 告警         |
| ------------ | ----- | ------------ |
| 开锁成功率   | > 95% | 低于阈值告警 |
| 上锁成功率   | > 95% | 低于阈值告警 |
| 状态同步延迟 | < 3秒 | 超过阈值告警 |
| 通信失败率   | < 5%  | 超过阈值告警 |
| 用户投诉率   | < 1%  | 超过阈值告警 |

### 9.3 回退触发条件

**满足以下任一条件立即回退:**

1. 开锁成功率 < 90%
2. 上锁成功率 < 90%
3. 用户投诉率 > 5%
4. 出现安全事故
5. 通信失败率 > 10%

---

## 十、FAQ

### Q1: 为什么不再等待锁舌确认？

**A:** STM32 移除了 PA15 霍尔传感器，无法检测锁舌是否闭合。改用固定时间（2秒）控制，假设电机正常运行。

### Q2: 如何确保上锁成功？

**A:** 无法通过硬件确认。建议：

1. UI 提示用户手动确认
2. 增加电流检测（硬件方案）
3. 定期维护检查电机

### Q3: 旧 ESP32 固件是否兼容？

**A:** 部分兼容。旧固件会等待锁舌确认超时（30秒），然后自动认为上锁成功。建议同步更新。

### Q4: 如何处理状态不一致？

**A:**

1. 定期查询 STM32 状态（每 5 分钟）
2. 以 STM32 状态为准
3. 记录不一致日志用于分析

### Q5: 是否需要修改 APP？

**A:** 建议修改：

1. 简化状态显示（LOCKED/UNLOCKED）
2. 删除"确认中"等中间状态
3. 添加"请手动确认"提示

---

## 十一、总结

### 11.1 ESP32 修改工作量

| 修改项           | 工作量         | 优先级 |
| ---------------- | -------------- | ------ |
| 删除锁舌报警处理 | 🟢 低（1小时） | 🔴 高  |
| 简化状态机       | 🟡 中（4小时） | 🔴 高  |
| 修改 UI 显示     | 🟡 中（2小时） | 🟡 中  |
| 删除推送通知     | 🟢 低（1小时） | 🟡 中  |
| 数据库迁移       | 🟡 中（2小时） | 🟢 低  |
| 测试验证         | 🟡 中（4小时） | 🔴 高  |

**总计:** 约 14 小时（2 天）

### 11.2 关键风险

| 风险         | 影响 | 缓解措施            |
| ------------ | ---- | ------------------- |
| 状态不同步   | 中   | 定期查询 + 日志监控 |
| 兼容性问题   | 中   | 版本检测 + 超时保护 |
| 用户体验下降 | 高   | 添加提示 + 用户教育 |

### 11.3 建议

1. **同步更新** - ESP32 和 STM32 固件同步更新
2. **充分测试** - 测试环境验证 1 周以上
3. **监控告警** - 部署后密切监控关键指标
4. **用户沟通** - 提前通知用户功能变化
5. **保留回退** - 准备快速回退方案

---

**文档结束**
