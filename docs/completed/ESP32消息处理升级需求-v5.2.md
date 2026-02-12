# ESP32 消息处理升级需求 v5.2

> 版本：v5.2  
> 日期：2026-01-16  
> 状态：**已完成**

---

## 一、升级概述

本次升级需要完成两项功能：

1. **新增 `query` 消息类型处理** - 支持服务器远程查询传感器数据和设备状态
2. **升级 `face_result` 消息处理** - 添加两级确认机制和 seq_id 支持

---

## 二、修改清单

### 2.1 新增 `query` 消息处理

**文件**：`main/application.cc`

**位置**：`HandleSmartLockJsonMessage()` 函数中，在 `user_mgmt` 处理块之后、`heartbeat_ack` 之前

**需要添加的代码逻辑**：

```cpp
// -------------------------------------------------------------------------
// 查询命令：sensors（传感器数据）、status（设备状态）
// -------------------------------------------------------------------------
if (strcmp(type, "query") == 0) {
    // 1. 提取 seq_id（优先）或 msg_id（兼容）
    // 2. 提取 command 字段
    // 3. 立即发送 esp32_ack（第一级确认）
    // 4. 调度到主循环执行：
    //    - 检查 lock_control_ 是否可用
    //    - 确定 uart_type（Q_SENSORS 或 Q_STATUS）
    //    - 保存 PendingCommand（type = QUERY）
    //    - 调用 lock_control_->QuerySensors() 或 QueryStatus()
    // 5. 等待 STM32 返回数据帧（RPT_ENV 或 RPT_STATE）
    // 6. 收到数据后发送 ack（第二级确认）
}
```

**消息格式**：

```json
// Server → ESP32
{
    "type": "query",
    "seq_id": "1702234567890_0",
    "command": "sensors"  // 或 "status"
}

// ESP32 → Server（第一级确认）
{
    "type": "esp32_ack",
    "seq_id": "1702234567890_0",
    "code": 0,
    "msg": "received"
}

// ESP32 → Server（第二级确认，收到 STM32 数据后）
{
    "type": "ack",
    "seq_id": "1702234567890_0",
    "code": 0,
    "msg": "OK"
}
```

**command 与 UART 映射**：

| command | UART TYPE | 数据帧 | 说明 |
|---------|-----------|--------|------|
| `sensors` | Q_SENSORS (0x80) | RPT_ENV (0xB0) | 电量、光照 |
| `status` | Q_STATUS (0x81) | RPT_STATE (0xB1) | 锁状态、补光灯 |

---

### 2.2 升级 `face_result` 消息处理

**文件**：`main/application.cc`

**位置**：`HandleSmartLockJsonMessage()` 函数中，`face_result` / `face_recognition` 处理块

**当前实现问题**：

1. 只使用 `msg_id`，未支持 `seq_id`
2. 只发送 `ack`，未实现两级确认（缺少 `esp32_ack`）
3. 未保存 PendingCommand，无法追踪命令状态

**需要修改的代码逻辑**：

```cpp
// 当前代码（需要修改）
if (strcmp(type, "face_recognition") == 0 || strcmp(type, "face_result") == 0) {
    auto msg_id = cJSON_GetObjectItem(root, "msg_id");
    std::string msg_id_str = cJSON_IsString(msg_id) ? msg_id->valuestring : "";

    Schedule([this, root_copy = cJSON_Duplicate(root, 1), msg_id_str]() {
        // 发送 ACK 响应
        if (!msg_id_str.empty()) {
            protocol_->SendAck(msg_id_str, 0, "OK");
        }
        HandleFaceRecognitionResult(root_copy);
        cJSON_Delete(root_copy);
    });
    return true;
}

// 修改后代码
if (strcmp(type, "face_recognition") == 0 || strcmp(type, "face_result") == 0) {
    // 1. 优先使用 seq_id，兼容旧版 msg_id
    auto seq_id = cJSON_GetObjectItem(root, "seq_id");
    auto msg_id = cJSON_GetObjectItem(root, "msg_id");
    std::string seq_id_str = cJSON_IsString(seq_id)   ? seq_id->valuestring
                             : cJSON_IsString(msg_id) ? msg_id->valuestring
                                                      : "";

    // 2. 立即发送 esp32_ack（第一级确认）
    if (!seq_id_str.empty()) {
        auto ws_protocol = dynamic_cast<WebsocketProtocol *>(protocol_.get());
        if (ws_protocol) {
            ws_protocol->SendEsp32Ack(seq_id_str, 0, "received");
        }
    }

    // 3. 调度到主循环执行
    Schedule([this, root_copy = cJSON_Duplicate(root, 1), seq_id_str]() {
        // 4. 保存 PendingCommand（如果需要等待 STM32 开锁响应）
        // 5. 处理人脸识别结果
        HandleFaceRecognitionResult(root_copy);
        
        // 6. 发送 ack（第二级确认）
        //    注意：如果 HandleFaceRecognitionResult 触发了开锁命令，
        //    ack 应该在开锁完成后发送（由 lock_control 的 ACK 触发）
        //    如果未触发开锁（如识别失败），则立即发送 ack
        if (!seq_id_str.empty()) {
            // 根据处理结果决定 ack 时机
            protocol_->SendAck(seq_id_str, 0, "OK");
        }
        
        cJSON_Delete(root_copy);
    });
    return true;
}
```

**关键决策点**：

`face_result` 的 ack 发送时机有两种选择：

| 方案 | ack 发送时机 | 优点 | 缺点 |
|------|--------------|------|------|
| A | 处理完成后立即发送 | 简单，响应快 | 不反映开锁是否成功 |
| B | 等待开锁完成后发送 | 完整反映执行结果 | 需要关联 face_result 和 lock_control |

**建议采用方案 A**：`face_result` 的 ack 表示"人脸识别结果已处理"，开锁结果通过 `log_report` 上报。

---

## 三、协议规范更新

### 3.1 新增 query 消息定义

**文件**：`docs/my_docs/智能猫眼门锁系统-ESP32与服务器通信协议规范-v5.1.md`

**位置**：在 3.6 硬件外设控制 之后，新增 3.7 查询命令

```markdown
### 3.7 查询命令 (Query)

#### 3.7.1 查询传感器数据 (Server → Device)

\`\`\`json
{
    "type": "query",
    "seq_id": "1702234567890_0",
    "command": "sensors"
}
\`\`\`

**响应**：ESP32 收到 STM32 的 RPT_ENV 后，自动发送 status_report。

#### 3.7.2 查询设备状态 (Server → Device)

\`\`\`json
{
    "type": "query",
    "seq_id": "1702234567890_1",
    "command": "status"
}
\`\`\`

**响应**：ESP32 收到 STM32 的 RPT_STATE 后，自动发送 status_report。

| command | 说明 | STM32 TYPE | 数据帧 |
|---------|------|------------|--------|
| sensors | 查询传感器（电量、光照） | Q_SENSORS (0x80) | RPT_ENV (0xB0) |
| status | 查询状态（锁、补光灯） | Q_STATUS (0x81) | RPT_STATE (0xB1) |
```

### 3.2 更新 face_result 消息定义

**位置**：3.4 人脸识别

**修改内容**：

1. 添加 `seq_id` 字段说明（兼容 `msg_id`）
2. 说明两级确认机制

### 3.3 更新消息类型汇总表

**位置**：9.2 Server → Device

添加 `query` 消息类型：

| 消息类型 | 说明 | 携带 seq_id |
|----------|------|-------------|
| `query` | 查询命令【v5.2 新增】 | ✅ |

---

## 四、文档更新

### 4.1 更新 ESP32接收服务器消息处理逻辑分析.md

**修改内容**：

1. 在"二、消息类型处理详情"中添加 `query` 消息处理说明
2. 更新 `face_result` 处理说明，标注两级确认机制

### 4.2 更新 消息ID机制与工作流程.md

**修改内容**：

1. 在"六、消息类型与命令分类"中添加 `query` 命令分类
2. 更新实现状态表

---

## 五、实现检查清单

### 5.1 query 消息处理

- [x] 在 `HandleSmartLockJsonMessage()` 中添加 `query` 类型判断
- [x] 提取 seq_id（优先）或 msg_id（兼容）
- [x] 发送 esp32_ack（第一级确认）
- [x] 保存 PendingCommand（type = QUERY）
- [x] 调用 `lock_control_->QuerySensors()` 或 `QueryStatus()`
- [x] 在 `HandleLockReportMessage()` 中处理 RPT_ENV/RPT_STATE 时发送 ack（已有实现）

### 5.2 face_result 消息处理

- [x] 修改 seq_id 提取逻辑（优先 seq_id，兼容 msg_id）
- [x] 添加 esp32_ack 发送（第一级确认）
- [x] 确认 ack 发送时机（采用方案 A：处理完成后立即发送）

### 5.3 协议规范更新

- [x] 更新 v5.1 → v5.2
- [x] 添加 query 消息定义（3.7 查询命令）
- [x] 更新 face_result 消息定义（已在代码中实现两级确认）
- [x] 更新消息类型汇总表（9.2 Server → Device）
- [x] 更新版本历史（10. 版本历史）
- [x] 更新实现状态表（8.1 已实现功能）

---

## 六、版本历史

| 版本 | 日期 | 变更说明 |
|------|------|----------|
| v5.2 | 2026-01-16 | 新增 query 消息处理；face_result 添加两级确认机制 |

---

**文档维护者**：毕业设计项目组  
**最后更新**：2026-01-16
