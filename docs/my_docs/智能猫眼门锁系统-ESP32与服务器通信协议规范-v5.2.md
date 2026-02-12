# 智能猫眼门锁系统 - ESP32与服务器通信协议规范 v5.2

## 1. 协议概述

### 1.1 传输层

| 项目 | 说明 |
|------|------|
| 传输协议 | WebSocket (主要) / MQTT (备选) |
| 连接地址 | 由 OTA 服务器下发 |
| 认证方式 | HTTP Header: `Authorization: Bearer <token>` |

### 1.2 数据帧类型

| 帧类型 | 格式 | 用途 |
|--------|------|------|
| Text Frame | JSON | 信令控制、状态上报、用户管理 |
| Binary Frame | BinaryProtocol2 | 音频流、视频流、人脸识别图像 |

### 1.3 可靠性设计

- Server 下发的关键指令携带 `seq_id`（兼容旧版 `msg_id`）
- ESP32 收到指令后实现**两级确认机制**：
  - 第一级：`esp32_ack` - 命令已收到，开始处理
  - 第二级：`ack` - 命令执行完成
- 重复的 `seq_id` 将被 ESP32 忽略（防重放）

---

## 2. 二进制流媒体协议 (BinaryProtocol2)

### 2.1 帧结构

```text
+----------------+----------------+--------------------------------+
|   version(2)   |    type(2)     |         reserved(4)            |
+----------------+----------------+--------------------------------+
|           timestamp(4)          |        payload_size(4)         |
+----------------+----------------+--------------------------------+
|                    payload data...                               |
+------------------------------------------------------------------+
```

| 字段 | 大小 | 说明 |
|------|------|------|
| version | 2字节 | 协议版本，固定为 `2` |
| type | 2字节 | 消息类型（见下表） |
| reserved | 4字节 | 扩展字段（视频时存储分辨率） |
| timestamp | 4字节 | 时间戳（毫秒） |
| payload_size | 4字节 | 负载数据大小 |
| payload | 可变 | 负载数据 |

**字节序：** 所有多字节字段使用大端序（Big-Endian / 网络字节序）

### 2.2 消息类型定义

| type | reserved | 数据类型 | 负载格式 | 方向 |
|------|----------|----------|----------|------|
| 0 | 0 | 音频流 | OPUS 编码 | 双向 |
| 0 | 非0 | 监控视频流 | JPEG 编码 | Device → Server |
| 2 | 非0 | 人脸识别图像 | JPEG 编码 | Device → Server |

### 2.3 视频 reserved 字段编码

```
reserved = (width << 16) | height
```

| 分辨率 | reserved 值 |
|--------|-------------|
| 640×480 | `0x028001E0` |

### 2.4 音频参数

| 参数 | 值 |
|------|-----|
| 编码格式 | OPUS |
| 采样率 | 16000 Hz |
| 声道数 | 1 (单声道) |
| 帧时长 | 60 ms |

### 2.5 视频参数

| 参数 | 值 |
|------|-----|
| 编码格式 | MJPEG |
| 分辨率 | 640×480 |
| 帧率 | 10 fps |
| JPEG 质量 | 60-80 |
| 单帧大小 | ~15KB |

---

## 3. JSON 信令协议 (Text Frame)

### 3.1 通用规范

#### 3.1.1 两级确认机制【v5.1 新增】

ESP32 收到带 `seq_id` 的指令后，实现两级确认：

**第一级确认：esp32_ack (Device → Server)**

ESP32 收到命令后**立即**发送，表示"命令已收到，开始处理"。

```json
{
    "type": "esp32_ack",
    "seq_id": "1702234567890_0",
    "code": 0,
    "msg": "received"
}
```

**第二级确认：ack (Device → Server)**

STM32 执行完成后发送，表示"命令执行完成"，携带执行结果。

```json
{
    "type": "ack",
    "seq_id": "1702234567890_0",
    "code": 0,
    "msg": "OK"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| type | string | `"esp32_ack"` 或 `"ack"` |
| seq_id | string | **回填 Server 下发的原 ID**（保证消息可追溯） |
| code | int | 状态码（见错误码定义） |
| msg | string | 状态描述 |

> **说明：** 
> - `seq_id` 必须与 Server 下发的命令中的 `seq_id` 完全一致，用于消息追溯
> - `seq_id` 为新版字段名，兼容旧版 `msg_id`
> - `esp32_ack` 的 code 固定为 0，msg 固定为 "received"
> - `ack` 的 code 反映实际执行结果
> - `seq_id` 格式由 App 生成，推荐格式：`时间戳_序号`（如 `1702234567890_0`）

#### 3.1.2 统一错误码定义【v5.1 更新】

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

**STM32 错误码映射：**

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

### 3.2 状态与事件上报 (Device → Server)

#### 3.2.1 传感器状态上报 (status_report)

**触发条件：** 状态变化时上报。

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

| 字段 | 类型 | 说明 |
|------|------|------|
| bat | int | 电量百分比 (0-100) |
| lux | int | 光照值 (Lux) |
| lock | int | 锁状态：0=关闭, 1=打开 |
| light | int | 补光灯：0=灭, 1=亮 |

> **说明：** 对应 STM32 的 `RPT_ENV` + `RPT_STATE` 消息。

#### 3.2.2 关键事件上报 (event_report)

**触发条件：** STM32 检测到事件时（对应 STM32 的 `RPT_EVENT`）。

```json
{
    "type": "event_report",
    "ts": 1702234567890,
    "event": "pir_trigger",
    "param": 1
}
```

| event | 说明 | param 含义 | STM32 事件ID |
|-------|------|------------|--------------|
| `bell` | 门铃按下 | 无 | 0x01 |
| `pir_trigger` | PIR 人体检测 | 持续时间(秒) | 0x02 |
| `tamper` | 撬锁报警 | 报警级别 (1-3) | 0x03 |
| `door_open` | 门未关超时 | 超时时间(分钟) | 0x04 |
| `low_battery` | 低电量警告 | 当前电量(%) | 0x05 |
| `door_closed` | 门关闭【v5.1 新增】 | 无 | 0x06 (status=0) |
| `lock_success` | 自动上锁成功【v5.1 新增】 | 无 | 0x06 (status=1) |
| `bolt_alarm` | 锁舌未到位报警【v5.1 新增】 | 无 | 0x06 (status=2) |

> **v5.1 新增说明：** `door_closed`、`lock_success`、`bolt_alarm` 对应 STM32 的 `EVT_LOCK_STATUS (0x06)` 事件，通过 param 区分状态。

#### 3.2.3 开锁日志上报 (log_report)【v5.1 更新】

**触发条件：** 用户尝试开锁（对应 STM32 的 `RPT_UNLOCK`）。

> **v5.1 变更：** 
> - `result` 字段改为 `status` 字符串类型，支持三种状态
> - 新增 `lock_time` 字段，用于认证锁定场景

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

| 字段 | 类型 | 说明 |
|------|------|------|
| method | string | 开锁方式 |
| status | string | 状态：`success`/`fail`/`locked`【v5.1 变更】 |
| uid | int | 用户 ID（成功时有效，失败时可能为 0xFF 表示无法识别） |
| fail_count | int | 失败次数（1-5，仅 `fail` 状态有效） |
| lock_time | int | 剩余锁定时间（分钟，仅 `locked` 状态有效）【v5.1 新增】 |

**status 取值说明：**

| status | 说明 | uid 含义 | fail_count | lock_time |
|--------|------|----------|------------|-----------|
| `success` | 开锁成功 | 用户 ID | 0 | 0 |
| `fail` | 认证失败 | 用户 ID（0xFF=无法识别） | 1-5 | 0 |
| `locked` | 设备锁定 | 无意义 | 0 | 剩余锁定时间（分钟） |

**method 取值：**

| method | 说明 | uid 说明 |
|--------|------|----------|
| `finger` | 指纹开锁 | 指纹 ID |
| `nfc` | NFC 开锁 | NFC 卡 ID |
| `face` | 人脸开锁 | 人脸用户 ID |
| `pwd` | 密码开锁 | 固定为 0 |
| `temp_pwd` | 临时密码开锁 | 固定为 0 |
| `key` | 机械钥匙 | 固定为 0 |
| `remote` | 远程开锁(App) | App 用户 ID |

> **STM32 映射说明：**
> - `status=success`：D2=0x00
> - `status=fail`：D2=0x01-0x05（失败次数）
> - `status=locked`：D2=0x06，D1=剩余锁定时间（分钟）

#### 3.2.4 开门日志上报 (door_opened_report)【v5.1 新增】

**触发条件：** 用户实际开门时（对应 STM32 的 `RPT_DOOR_OPENED`）。

> **说明：** 与 `log_report` 区分：
> - `log_report`：记录开锁操作（命令执行成功）
> - `door_opened_report`：记录开门行为（用户实际开门）

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
| method | string | 开锁方式（与 `log_report` 相同） |
| source | string | 开门来源：`outside`/`inside`/`unknown` |

**source 取值说明：**

| source | 说明 | STM32 D1 值 |
|--------|------|-------------|
| `outside` | 室外开门（PIR 检测到人体） | 0x00 |
| `inside` | 室内开门（PIR 未检测到人体） | 0x01 |
| `unknown` | 未知/不适用 | 0xFF |

> **STM32 映射：** 对应 `RPT_DOOR_OPENED (0xA2)`，D0=开锁方式，D1=开门来源

#### 3.2.5 密码上报 (password_report)【v5.1 新增】

**触发条件：** 密码查询结果返回时（对应 STM32 的 `RPT_PWD`）。

```json
{
    "type": "password_report",
    "ts": 1702234567890,
    "data": {
        "password": "123456"
    }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| password | string | 6 位密码（零填充字符串） |

> **STM32 映射：** 对应 `RPT_PWD (0xC0)`，密码由 D0/D1/D2 三字节 Hex 编码

---

### 3.3 监控模式 (Monitor Mode)

> **说明：** 监控模式使用原有 `system` 命令实现，此处仅记录协议格式。

#### 3.3.1 启动监控 (Server → Device)

```json
{
    "type": "system",
    "command": "start_monitor"
}
```

#### 3.3.2 停止监控 (Server → Device)

```json
{
    "type": "system",
    "command": "stop_monitor"
}
```

#### 3.3.3 监控数据流

- **视频帧：** BinaryProtocol2, type=0, reserved=分辨率
- **音频帧：** BinaryProtocol2, type=0, reserved=0
- **方向：** 视频单向（Device → Server），音频双向

---

### 3.4 人脸识别 (Face Recognition)

#### 3.4.1 识别流程

```
STM32 (门铃/PIR)
  │
  │ UART: RPT_EVENT
  ▼
ESP32
  │
  │ BinaryProtocol2 (type=2) 发送 JPEG
  ▼
Server
  │
  │ AI 人脸识别
  │ 返回 JSON 结果
  ▼
ESP32
  │
  │ UART: CMD_LOCK (开锁) 或 CMD_BEEP (警报)
  ▼
STM32
```

#### 3.4.2 识别结果 (Server → Device)

> **重要说明**：face_result 是服务器主动推送的识别结果，不是用户发起的命令。
> - **不需要** seq_id 字段
> - **不需要** esp32_ack 和 ack 两级确认
> - 开锁结果通过 log_report 上报

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

| 字段 | 类型 | 说明 |
|------|------|------|
| result | string | 识别结果 |
| user_id | int | 用户 ID（known 时有效） |
| access.granted | bool | 是否授权开锁 |
| access.reason | string | 授权/拒绝原因 |

**result 取值：**

| result | 说明 |
|--------|------|
| `known` | 识别成功（已注册用户） |
| `unknown` | 陌生人 |
| `no_face` | 未检测到人脸 |
| `error` | 识别错误 |

**access.reason 取值：**

| reason | 说明 |
|--------|------|
| `authorized_user` | 授权用户 |
| `time_restricted` | 时间段限制 |
| `blacklisted` | 黑名单用户 |
| `guest_expired` | 访客权限过期 |
| `unauthorized_user` | 非授权用户 |

---

### 3.5 锁控命令 (Lock Control)

#### 3.5.1 远程开锁/关锁 (Server → Device)

```json
{
    "type": "lock_control",
    "seq_id": "1702234567890_1",
    "command": "unlock",
    "duration": 5
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| command | string | `unlock`(开锁), `lock`(关锁) |
| duration | int | 开锁保持时间(秒)，0=默认3分钟 |

> **STM32 映射：** 对应 `CMD_LOCK` (0x10)，D0=0x01(开)/0x02(关)，D1=duration

#### 3.5.2 临时密码 (Server → Device)

```json
{
    "type": "lock_control",
    "seq_id": "1702234567890_2",
    "command": "temp_code",
    "code": "123456",
    "expires": 3600
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| code | string | 6位临时密码 |
| expires | int | 有效期(秒) |

> **STM32 映射：** 对应 `TEMP_PWD` (0x32+0x33)，分两包发送

---

### 3.6 硬件外设控制 (Dev Control)

#### 3.6.1 蜂鸣器控制 (Server → Device)

```json
{
    "type": "dev_control",
    "seq_id": "1702234567890_3",
    "target": "beep",
    "count": 3,
    "mode": "alarm"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| target | string | 固定为 `"beep"` |
| count | int | 响铃次数 |
| mode | string | 模式：`short`(短滴), `long`(长鸣), `alarm`(报警) |

> **STM32 映射：** 对应 `CMD_BEEP` (0x12)，D0=count，D1=mode(0x01/0x02/0x03)

#### 3.6.2 OLED 图标显示 (Server → Device)

```json
{
    "type": "dev_control",
    "seq_id": "1702234567890_4",
    "target": "oled",
    "icon": 3
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| target | string | 固定为 `"oled"` |
| icon | int | 图标 ID |

> **STM32 映射：** 对应 `CMD_OLED` (0x11)，D0=icon

**icon 与 STM32 对应关系：**

| icon | 说明 | STM32 D0 值 |
|------|------|-------------|
| 0 | 清屏/待机 | 0x00 |
| 1 | WiFi 已连接 | 0x01 |
| 2 | 云端已连接 | 0x02 |
| 3 | 识别中 | 0x03 |
| 4 | 识别成功 | 0x04 |
| 5 | 识别失败 | 0x05 |

#### 3.6.3 补光灯控制 (Server → Device)

```json
{
    "type": "dev_control",
    "seq_id": "1702234567890_5",
    "target": "light",
    "action": "on"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| target | string | 固定为 `"light"` |
| action | string | `on`(开), `off`(关), `auto`(恢复自动控制) |

> **STM32 映射：** 对应 `CMD_LIGHT` (0x14)，D0=action(0x01=开, 0x02=关, 0x00=自动)

---

### 3.7 查询命令 (Query)【v5.2 新增】

> **v5.2 新增：** 支持服务器远程查询传感器数据和设备状态。

#### 3.7.1 查询传感器数据 (Server → Device)

```json
{
    "type": "query",
    "seq_id": "1702234567890_0",
    "command": "sensors"
}
```

**响应流程：**
1. ESP32 收到后立即发送 `esp32_ack`
2. ESP32 向 STM32 发送 `Q_SENSORS` 查询命令
3. STM32 返回 `RPT_ENV` 数据帧
4. ESP32 发送 `status_report` 上报数据
5. ESP32 发送 `ack` 确认完成

#### 3.7.2 查询设备状态 (Server → Device)

```json
{
    "type": "query",
    "seq_id": "1702234567890_1",
    "command": "status"
}
```

**响应流程：**
1. ESP32 收到后立即发送 `esp32_ack`
2. ESP32 向 STM32 发送 `Q_STATUS` 查询命令
3. STM32 返回 `RPT_STATE` 数据帧
4. ESP32 发送 `status_report` 上报数据
5. ESP32 发送 `ack` 确认完成

#### 3.7.3 command 与 UART 映射

| command | 说明 | STM32 TYPE | 数据帧 |
|---------|------|------------|--------|
| `sensors` | 查询传感器（电量、光照） | Q_SENSORS (0x80) | RPT_ENV (0xB0) |
| `status` | 查询状态（锁、补光灯） | Q_STATUS (0x81) | RPT_STATE (0xB1) |

---

### 3.8 用户管理 (User Management)

> ESP32 收到指令后，自行控制 STM32 完成录入/删除流程，并通过 TTS 语音提示用户。Server 只需等待最终结果。

#### 3.8.1 发起管理流程 (Server → Device)

```json
{
    "type": "user_mgmt",
    "seq_id": "1702234567890_6",
    "category": "finger",
    "command": "add",
    "user_id": 0,
    "payload": ""
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| category | string | `finger`(指纹), `nfc`(卡片), `password`(密码) |
| command | string | `add`(增), `del`(删), `clear`(清), `query`(查), `set`(设置密码) |
| user_id | int | 0=自动分配，删除时必填指定 ID |
| payload | string | 仅 `category=password` 且 `command=set` 时有效 |

**STM32 映射：**

| category | command | STM32 TYPE | STM32 D0 |
|----------|---------|------------|----------|
| finger | add | 0x10 | 0x01 |
| finger | del | 0x10 | 0x02 |
| finger | clear | 0x10 | 0x03 |
| finger | query | 0x10 | 0x04 |
| nfc | add | 0x20 | 0x01 |
| nfc | del | 0x20 | 0x02 |
| nfc | clear | 0x20 | 0x03 |
| nfc | query | 0x20 | 0x04 |
| password | set | 0x30 | - |
| password | query | 0x31 | - |

#### 3.8.2 管理结果上报 (Device → Server)

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

| 字段 | 类型 | 说明 |
|------|------|------|
| result | bool | 操作结果 |
| val | int | 成功时：分配的 ID / 查询的总数；失败时：错误码 |
| msg | string | 结果描述 |

**特殊成功场景【v5.1 新增】：**

| 场景 | result | val | msg |
|------|--------|-----|-----|
| 指纹/NFC 已存在 | true | 已有 ID | "Already exists" |
| ID 被占用自动分配 | true | 新分配 ID | "ID occupied, auto assigned" |

**错误码 (val) 与 STM32 对应关系：**

| val | 说明 | STM32 错误码 |
|-----|------|--------------|
| 0x01 | 设备忙碌 | ERR_BUSY |
| 0x02 | 不支持 | ERR_UNSUPPORT |
| 0x03 | 参数错误 | ERR_PARAM |
| 0x04 | 指纹库已满 | ERR_FP_FULL |
| 0x05 | NFC 库已满 | ERR_NFC_FULL |
| 0x06 | 硬件故障 | ERR_HARDWARE |
| 0xFF | 超时 | ERR_TIMEOUT |

---

### 3.9 心跳机制 (Heartbeat) - 预留功能

> **状态：** 已实现，暂不启用。后续可通过配置开启。

#### 3.9.1 心跳请求 (Device → Server)

**触发条件：** 每 30 秒发送一次（启用时）。

```json
{
    "type": "heartbeat",
    "ts": 1702234567890,
    "uptime": 3600
}
```

#### 3.9.2 心跳响应 (Server → Device)

```json
{
    "type": "heartbeat_ack",
    "ts": 1702234567891,
    "server_time": 1702234567891
}
```

---

## 4. 数据流示意图

### 4.1 人脸识别流程【v5.2 更新】

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
     │                   │ (无 seq_id)       │
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
     │                   │                   │
     │ UART: RPT_DOOR_OPENED                 │
     │ (开门日志)        │                   │
     │──────────────────>│                   │
     │                   │                   │
     │                   │ JSON: door_opened_report
     │                   │──────────────────>│
     │                   │                   │
```

> **说明**：face_result 不需要两级确认，因为它是服务器主动推送的识别结果，不是用户命令。开锁成功与否通过 log_report 上报。

### 4.2 监控模式流程

```
┌─────────┐         ┌─────────┐         ┌─────────┐
│  STM32  │         │  ESP32  │         │ Server  │
└────┬────┘         └────┬────┘         └────┬────┘
     │                   │                   │
     │                   │ JSON: start_monitor
     │                   │<──────────────────│
     │                   │                   │
     │                   │ Binary: type=0    │
     │                   │ (视频帧 JPEG)      │
     │                   │──────────────────>│
     │                   │                   │
     │                   │ Binary: type=0    │
     │                   │ (音频帧 OPUS)      │
     │                   │<────────────────->│
     │                   │                   │
     │                   │ JSON: stop_monitor│
     │                   │<──────────────────│
     │                   │                   │
```

---

## 5. 性能指标

| 指标 | 数值 |
|------|------|
| 视频分辨率 | 640×480 |
| 视频帧率 | 10 fps |
| JPEG 质量 | 60-80 |
| 单帧大小 | ~15KB |
| 视频带宽 | ~1.2 Mbps |
| 音频带宽 | ~0.5 Mbps（双向） |
| 总带宽 | ~1.7 Mbps |
| 协议开销 | 16字节/帧（<0.1%） |
| 状态上报 | 状态变化时上报 |

---

## 6. 错误处理

### 6.1 协议层错误

| 错误类型 | 处理方式 |
|----------|----------|
| 协议版本不匹配 | 拒绝连接，返回错误 |
| JSON 解析失败 | 丢弃消息，记录日志 |
| 负载大小不匹配 | 丢弃消息 |
| JPEG 解码失败 | 丢弃帧 |
| seq_id 重复 | 忽略消息（防重放） |

### 6.2 业务层错误

| 错误类型 | 处理方式 |
|----------|----------|
| 设备忙碌 | 返回 ack code=2 |
| 硬件不可用 | 返回 ack code=6 |
| 操作超时 | 返回 ack code=5 |
| 权限不足 | 返回 ack code=8 |

---

## 7. 安全设计

### 7.1 防重放攻击

- 所有关键指令携带唯一 `seq_id`
- ESP32 维护最近 100 条 seq_id 缓存
- 重复 seq_id 直接忽略

### 7.2 异常检测

- 连续 5 次开锁失败触发锁定
- 锁定时间递增（1/3/5/10/30 分钟）
- 撬锁检测立即上报
- 低电量提前预警

---

## 8. 实现状态

> **说明：** 以下为 ESP32 端代码实现状态，供开发参考。

### 8.1 已实现功能

| 功能 | 代码位置 | 状态 |
|------|----------|------|
| esp32_ack 响应【v5.1 新增】 | `WebsocketProtocol::SendEsp32Ack()` | ✅ |
| ack 响应 | `WebsocketProtocol::SendAck()` | ✅ |
| 状态上报 | `WebsocketProtocol::SendStatusReport()` | ✅ |
| 事件上报 | `WebsocketProtocol::SendEventReport()` | ✅ |
| 开锁日志 | `WebsocketProtocol::SendLogReport()` | ✅ |
| 开门日志【v5.1 新增】 | `WebsocketProtocol::SendDoorOpenedReport()` | ✅ |
| 密码上报【v5.1 新增】 | `WebsocketProtocol::SendPasswordReport()` | ✅ |
| 监控模式 | `Application::StartMonitorMode()` | ✅ |
| 人脸识别 | `WebsocketProtocol::SendFaceRecognition()` | ✅ |
| 锁控命令 | `Application::HandleSmartLockJsonMessage()` | ✅ |
| 硬件控制 | `Application::HandleSmartLockJsonMessage()` | ✅ |
| 用户管理 | `Application::HandleSmartLockJsonMessage()` | ✅ |
| 查询命令【v5.2 新增】 | `Application::HandleSmartLockJsonMessage()` | ✅ |
| face_result 处理【v5.2 更新】 | `Application::HandleSmartLockJsonMessage()` | ✅ |
| 心跳机制 | `WebsocketProtocol::SendHeartbeat()` | ✅ (预留) |
| seq_id 防重放 | `WebsocketProtocol::IsDuplicateMsgId()` | ✅ |
| 待处理命令队列【v5.1 新增】 | `Application::pending_commands_` | ✅ |
| 超时清理【v5.1 新增】 | `Application::CleanupPendingCommands()` | ✅ |
| 错误码映射【v5.1 新增】 | `Application::MapStm32ErrorCode()` | ✅ |

### 8.2 设计说明

| 功能 | 说明 |
|------|------|
| 两级确认 | esp32_ack（收到）+ ack（完成），支持命令追溯 |
| face_result | 服务器主动推送，不需要 seq_id 和两级确认【v5.2 更新】 |
| 状态上报 | 仅在状态变化时上报（由 STM32 触发） |
| 心跳机制 | 已实现发送方法，间隔30秒，暂不启用（预留功能） |
| 待处理命令 | 按 UART TYPE 索引，支持超时清理 |
| 查询命令【v5.2 新增】 | 支持 sensors/status 远程查询，ack 在收到 STM32 数据后发送 |

### 8.3 seq_id 说明

- **格式**：字符串类型，由 App 生成，如 `"1702234567890_0"`
- **兼容性**：同时支持旧版 `msg_id` 字段
- **防重放**：ESP32 维护最近 100 条 seq_id 缓存（FIFO 淘汰）
- **检查位置**：在 `WebsocketProtocol::OnData()` 中进行，重复消息直接丢弃

---

## 9. 消息类型汇总

### 9.1 Device → Server

| 消息类型 | 说明 | 携带 seq_id |
|----------|------|-------------|
| `esp32_ack` | 命令已收到确认【v5.1 新增】 | ✅ |
| `ack` | 命令执行完成确认 | ✅ |
| `status_report` | 状态上报 | ❌ |
| `event_report` | 事件上报 | ❌ |
| `log_report` | 开锁日志上报 | ❌ |
| `door_opened_report` | 开门日志上报【v5.1 新增】 | ❌ |
| `password_report` | 密码上报【v5.1 新增】 | ❌ |
| `user_mgmt_result` | 用户管理结果 | ❌ |
| `heartbeat` | 心跳（预留） | ❌ |

### 9.2 Server → Device

| 消息类型 | 说明 | 携带 seq_id |
|----------|------|-------------|
| `face_result` | 人脸识别结果 | ❌ |
| `lock_control` | 锁控命令 | ✅ |
| `dev_control` | 硬件控制 | ✅ |
| `query` | 查询命令【v5.2 新增】 | ✅ |
| `user_mgmt` | 用户管理 | ✅ |
| `system` | 系统命令（监控模式） | ❌ |
| `heartbeat_ack` | 心跳响应（预留） | ❌ |

---

## 10. 版本历史

| 版本 | 日期 | 变更说明 |
|------|------|----------|
| v5.2 | 2026-01-17 | 新增 query 消息处理（sensors/status）；明确 face_result 不需要 seq_id 和两级确认 |
| v5.1 | 2026-01-16 | 两级确认机制（esp32_ack + ack）；统一错误码；log_report 支持 status/lock_time；新增 door_opened_report、password_report；event_report 新增 lock_status 事件 |
| v5.0 | 2024-12-12 | 整合协议，与服务器协议对齐，统一 method 取值 |
| v4.0 | 2024-12-09 | 新增 msg_id/ACK 机制、用户管理闭环 |
| v3.0 | 2024-12-08 | 新增监控模式、人脸识别 |
| v2.0 | 2024-12-07 | 新增锁控命令、事件上报 |
| v1.0 | 2024-12-06 | 初始版本，基础音频通信 |

---

**文档维护者：** 毕业设计项目组  
**最后更新：** 2026-01-17
