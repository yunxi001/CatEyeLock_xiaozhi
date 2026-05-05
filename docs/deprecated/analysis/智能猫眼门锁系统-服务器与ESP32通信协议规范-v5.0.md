# 智能猫眼门锁系统 - ESP32 通信协议规范 v5.0

## 1. 协议概述

### 1.1 传输层

| 项目 | 说明 |
|------|------|
| 传输协议 | WebSocket (主要) / MQTT (备选) |
| 连接端点 | `ws://{server}:8000/xiaozhi/v1/` |
| 认证方式 | HTTP Header: `Authorization: Bearer <token>` |

### 1.2 数据帧类型

| 帧类型 | 格式 | 用途 |
|--------|------|------|
| Text Frame | JSON | 信令控制、状态上报、用户管理 |
| Binary Frame | BinaryProtocol2 | 音频流、视频流、人脸识别图像 |

### 1.3 可靠性设计

- Server 下发的关键指令携带 `msg_id`
- ESP32 收到带 `msg_id` 的指令后必须回复 `ack`
- 重复的 `msg_id` 将被 ESP32 忽略（防重放）

### 1.4 系统架构

```
┌─────────┐         ┌─────────┐         ┌─────────┐
│  STM32  │◄──UART─►│  ESP32  │◄──WS───►│ Server  │
└─────────┘         └─────────┘         └─────────┘
  门锁硬件            通信模块             云端服务
```

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
| type | 2字节 | 消息类型 |
| reserved | 4字节 | 扩展字段（视频时存储分辨率） |
| timestamp | 4字节 | 时间戳（毫秒） |
| payload_size | 4字节 | 负载数据大小 |
| payload | 可变 | 负载数据 |

**字节序：** 所有多字节字段使用大端序（Big-Endian）

### 2.2 消息类型定义

| type | reserved | 数据类型 | 负载格式 | 方向 |
|------|----------|----------|----------|------|
| 0 | 0 | 音频流 | OPUS 编码 | 双向 |
| 0 | 非0 | 监控视频流 | JPEG 编码 | ESP32 → Server |
| 2 | 非0 | 人脸识别图像 | JPEG 编码 | ESP32 → Server |

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

### 3.1 通用 ACK 响应 (ESP32 → Server)

ESP32 收到任何带 `msg_id` 的指令后，必须立即回复：

```json
{
    "type": "ack",
    "msg_id": "cmd_88293",
    "code": 0,
    "msg": "OK"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| type | string | 固定为 `"ack"` |
| msg_id | string | 回填 Server 下发的 ID |
| code | int | 状态码 |
| msg | string | 状态描述 |

### 3.2 错误码定义

| code | 含义 | 说明 |
|------|------|------|
| 0 | 成功 | 指令已接收/执行中 |
| 1 | 设备忙碌 | 当前有其他操作进行中 |
| 2 | 参数错误 | 指令参数不合法 |
| 3 | 硬件故障 | 相关硬件不可用 |
| 4 | 超时 | 操作超时 |
| 5 | 未授权 | 权限不足 |
| 6 | 资源不足 | 内存/存储空间不足 |
| 7 | 不支持 | 功能不支持 |

> **注意**：此错误码与 App 协议中 `server_ack` 的错误码不同，`server_ack` 用于 Server 确认收到 App 消息。

---

## 4. 状态与事件上报 (ESP32 → Server)

### 4.1 传感器状态上报 (status_report)

**触发条件：** 状态变化时

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

> **Server 处理**：存储到数据库，并转发给所有关联的 App

### 4.2 关键事件上报 (event_report)

**触发条件：** STM32 检测到事件时

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

> **Server 处理**：存储到数据库，并转发给所有关联的 App

### 4.3 开锁日志上报 (log_report)

**触发条件：** 用户尝试开锁时

```json
{
    "type": "log_report",
    "ts": 1702234567890,
    "data": {
        "method": "finger",
        "uid": 5,
        "result": true,
        "fail_count": 0
    }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| method | string | 开锁方式 |
| uid | int | 用户 ID（0=无法识别用户） |
| result | bool | 开锁结果 |
| fail_count | int | 连续失败次数（成功时为 0） |

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

> **Server 处理**：存储到数据库，并转发给所有关联的 App

---

## 5. 锁控命令 (Server → ESP32)

### 5.1 远程开锁/关锁

```json
{
    "type": "lock_control",
    "msg_id": "cmd_1001",
    "command": "unlock",
    "duration": 5
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| msg_id | string | 消息 ID（ESP32 需回复 ack） |
| command | string | `unlock`(开锁), `lock`(关锁) |
| duration | int | 开锁保持时间(秒)，0=默认3分钟 |

### 5.2 临时密码

```json
{
    "type": "lock_control",
    "msg_id": "cmd_1002",
    "command": "temp_code",
    "code": "123456",
    "expires": 3600
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| code | string | 6位临时密码 |
| expires | int | 有效期(秒) |

---

## 6. 硬件外设控制 (Server → ESP32)

### 6.1 蜂鸣器控制

```json
{
    "type": "dev_control",
    "msg_id": "cmd_2001",
    "target": "beep",
    "count": 3,
    "mode": "alarm"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| target | string | 固定为 `"beep"` |
| count | int | 响铃次数 |
| mode | string | `short`(短滴), `long`(长鸣), `alarm`(报警) |

### 6.2 OLED 图标显示

```json
{
    "type": "dev_control",
    "msg_id": "cmd_2002",
    "target": "oled",
    "icon": 3
}
```

| icon | 说明 |
|------|------|
| 0 | 清屏/待机 |
| 1 | WiFi 已连接 |
| 2 | 云端已连接 |
| 3 | 识别中 |
| 4 | 识别成功 |
| 5 | 识别失败 |

### 6.3 补光灯控制

```json
{
    "type": "dev_control",
    "msg_id": "cmd_2003",
    "target": "light",
    "action": "on"
}
```

| action | 说明 |
|--------|------|
| `on` | 强制开灯 |
| `off` | 强制关灯 |
| `auto` | 恢复自动控制 |

---

## 7. 用户管理 (Server → ESP32)

### 7.1 发起管理流程

```json
{
    "type": "user_mgmt",
    "msg_id": "cmd_4001",
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

### 7.2 管理结果上报 (ESP32 → Server)

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

**错误码 (val)：**

| val | 说明 |
|-----|------|
| 0x01 | 设备忙碌 |
| 0x02 | 不支持 |
| 0x03 | 参数错误 |
| 0x04 | 指纹库已满 |
| 0x05 | NFC 库已满 |
| 0x06 | 硬件故障 |
| 0xFF | 超时 |

> **Server 处理**：转发给所有关联的 App

---

## 8. 监控模式 (Monitor Mode)

### 8.1 启动监控 (Server → ESP32)

```json
{
    "type": "system",
    "command": "start_monitor"
}
```

### 8.2 停止监控 (Server → ESP32)

```json
{
    "type": "system",
    "command": "stop_monitor"
}
```

### 8.3 监控数据流

| 数据类型 | 协议 | 方向 |
|----------|------|------|
| 视频帧 | BinaryProtocol2, type=0, reserved=分辨率 | ESP32 → Server |
| 音频帧 | BinaryProtocol2, type=0, reserved=0 | 双向 |

> **Server 处理**：将视频帧（JPEG）和音频帧（解码为 PCM）转发给所有关联的 App

---

## 9. 人脸识别 (Face Recognition)

### 9.1 识别流程

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
  │ 推送 visit_notification 给 App
  ▼
ESP32
  │
  │ UART: CMD_LOCK (开锁) 或 CMD_BEEP (警报)
  ▼
STM32
```

### 9.2 识别结果 (Server → ESP32)

```json
{
    "type": "face_result",
    "msg_id": "face_001",
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

> **Server 处理**：同时向 App 推送 `visit_notification`（含人脸图片）

---

## 10. 心跳机制 (预留功能)

> **状态：** 已实现，暂不启用。后续可通过配置开启。

### 10.1 心跳请求 (ESP32 → Server)

```json
{
    "type": "heartbeat",
    "ts": 1702234567890,
    "uptime": 3600
}
```

### 10.2 心跳响应 (Server → ESP32)

```json
{
    "type": "heartbeat_ack",
    "ts": 1702234567891,
    "server_time": 1702234567891
}
```

> **注意**：App 协议不使用心跳机制，App 通过 `device_status` 消息获知设备上下线状态。

---

## 11. 消息类型汇总

### 11.1 ESP32 上报消息

| type | 说明 | Server 处理 |
|------|------|-------------|
| `ack` | 命令确认 | 转发给 App |
| `status_report` | 状态上报 | 存储 + 转发给 App |
| `event_report` | 事件上报 | 存储 + 转发给 App |
| `log_report` | 开锁日志 | 存储 + 转发给 App |
| `user_mgmt_result` | 用户管理结果 | 转发给 App |
| `heartbeat` | 心跳（预留） | 回复 heartbeat_ack |

### 11.2 Server 下发消息

| type | 说明 | 需要 ACK |
|------|------|----------|
| `lock_control` | 锁控命令 | ✅ |
| `dev_control` | 设备控制 | ✅ |
| `user_mgmt` | 用户管理 | ✅ |
| `system` | 系统命令 | ❌ |
| `face_result` | 人脸识别结果 | ✅ |
| `heartbeat_ack` | 心跳响应 | ❌ |

---

## 12. 与 App 协议对比

### 12.1 协议差异

| 差异点 | ESP32 协议 | App 协议 |
|--------|------------|----------|
| 连接端点 | `/xiaozhi/v1/` | `/ws/app` |
| 认证方式 | HTTP Header Token | hello 消息 |
| 身份标识 | device_id (MAC) | app_id (用户ID) |
| 二进制流 | BinaryProtocol2 | 透传 JPEG/PCM |
| ACK 机制 | msg_id + ack | seq_id + server_ack |

### 12.2 消息流向

```
┌─────────┐                ┌─────────┐                ┌─────────┐
│   App   │                │ Server  │                │  ESP32  │
└────┬────┘                └────┬────┘                └────┬────┘
     │                          │                          │
     │ lock_control ───────────►│                          │
     │ (seq_id)                 │                          │
     │◄─── server_ack ─────────│                          │
     │                          │ lock_control ───────────►│
     │                          │ (msg_id)                 │
     │                          │◄─────────── ack ────────│
     │◄─────────── ack ────────│                          │
     │                          │                          │
     │                          │◄── status_report ───────│
     │◄── status_report ───────│ (存储 + 转发)            │
     │                          │                          │
```

### 12.3 共享消息格式

以下消息格式在两个协议中完全一致：

| 消息类型 | 字段格式 |
|----------|----------|
| `status_report` | ts, data.{bat, lux, lock, light} |
| `event_report` | ts, event, param |
| `log_report` | ts, data.{method, uid, result, fail_count} |
| `lock_control` | command, duration, code, expires |
| `dev_control` | target, count, mode, action, icon |
| `user_mgmt` | category, command, user_id, payload |
| `user_mgmt_result` | category, command, result, val, msg |
| `ack` | msg_id, code, msg |

---

## 13. 数据存储规范

### 13.1 存储架构

| 数据类型 | 存储位置 | 保留策略 |
|----------|----------|----------|
| 设备状态 | MySQL | 7 天 |
| 设备事件 | MySQL | 30 天 |
| 开锁日志 | MySQL | 90 天 |
| 用户信息 | MySQL | 永久 |
| 人脸图片 | 文件系统 | 30 天 |
| 监控录像 | 文件系统 | 30 天 |

### 13.2 数据库表设计

#### device_status (设备状态)

| 字段 | 类型 | 说明 |
|------|------|------|
| id | BIGINT | 自增主键 |
| device_id | VARCHAR(64) | 设备 ID |
| battery | INT | 电量百分比 |
| lux | INT | 光照值 |
| lock_state | TINYINT | 锁状态 |
| light_state | TINYINT | 补光灯状态 |
| created_at | DATETIME | 记录时间 |

#### device_events (设备事件)

| 字段 | 类型 | 说明 |
|------|------|------|
| id | BIGINT | 自增主键 |
| device_id | VARCHAR(64) | 设备 ID |
| event_type | VARCHAR(32) | 事件类型 |
| param | INT | 事件参数 |
| created_at | DATETIME | 事件时间 |

#### unlock_logs (开锁日志)

| 字段 | 类型 | 说明 |
|------|------|------|
| id | BIGINT | 自增主键 |
| device_id | VARCHAR(64) | 设备 ID |
| method | VARCHAR(16) | 开锁方式 |
| user_id | INT | 用户 ID |
| result | TINYINT | 结果 (0=失败, 1=成功) |
| fail_count | INT | 连续失败次数 |
| created_at | DATETIME | 记录时间 |

#### media_files (媒体文件)

| 字段 | 类型 | 说明 |
|------|------|------|
| id | BIGINT | 自增主键 |
| device_id | VARCHAR(64) | 设备 ID |
| file_type | VARCHAR(16) | 类型 (face/recording) |
| file_path | VARCHAR(256) | 文件相对路径 |
| file_size | INT | 文件大小 (字节) |
| duration | INT | 时长 (秒，仅录像) |
| user_id | INT | 关联用户 ID (仅人脸) |
| created_at | DATETIME | 创建时间 |

### 13.3 媒体文件目录结构

```
data/media/
├── faces/                    # 人脸识别图片
│   └── {device_id}/
│       └── {YYYY-MM-DD}/
│           └── face_{timestamp}_{user_id}.jpg
└── recordings/               # 监控录像
    └── {device_id}/
        └── {YYYY-MM-DD}/
            └── rec_{timestamp}.mp4
```

---

## 14. 性能指标

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
| 状态上报间隔 | 60 秒 |

---

## 15. 错误处理

### 15.1 协议层错误

| 错误类型 | 处理方式 |
|----------|----------|
| 协议版本不匹配 | 拒绝连接 |
| JSON 解析失败 | 丢弃消息，记录日志 |
| 负载大小不匹配 | 丢弃消息 |
| JPEG 解码失败 | 丢弃帧 |
| msg_id 重复 | 忽略消息（防重放） |

### 15.2 业务层错误

| 错误类型 | ACK code |
|----------|----------|
| 设备忙碌 | 1 |
| 参数错误 | 2 |
| 硬件不可用 | 3 |
| 操作超时 | 4 |
| 权限不足 | 5 |

---

## 16. 安全设计

### 16.1 防重放攻击

- 所有关键指令携带唯一 `msg_id`
- ESP32 维护最近 100 条 msg_id 缓存
- 重复 msg_id 直接忽略

### 16.2 异常检测

- 连续 5 次开锁失败触发警报
- 撬锁检测立即上报并触发警报
- 低电量提前预警

---

## 17. 实现状态

### 17.1 ESP32 端实现

| 功能 | 代码位置 | 状态 |
|------|----------|------|
| ACK 响应 | `WebsocketProtocol::SendAck()` | ✅ |
| 状态上报 | `WebsocketProtocol::SendStatusReport()` | ✅ |
| 事件上报 | `WebsocketProtocol::SendEventReport()` | ✅ |
| 开锁日志 | `WebsocketProtocol::SendLogReport()` | ✅ |
| 监控模式 | `Application::StartMonitorMode()` | ✅ |
| 人脸识别 | `WebsocketProtocol::SendFaceRecognition()` | ✅ |
| 锁控命令 | `Application::HandleSmartLockJsonMessage()` | ✅ |
| 硬件控制 | `Application::HandleSmartLockJsonMessage()` | ✅ |
| 用户管理 | `Application::HandleSmartLockJsonMessage()` | ✅ |
| 心跳机制 | `WebsocketProtocol::SendHeartbeat()` | ✅ (预留) |
| msg_id 防重放 | `WebsocketProtocol::IsDuplicateMsgId()` | ✅ |

### 17.2 Server 端实现

| 功能 | 代码位置 | 状态 |
|------|----------|------|
| ESP32 连接处理 | `core/connection.py` | ✅ |
| 状态上报处理 | `core/handle/textHandler/statusReportHandler.py` | ✅ |
| 事件上报处理 | `core/handle/textHandler/eventReportHandler.py` | ✅ |
| 开锁日志处理 | `core/handle/textHandler/logReportHandler.py` | ✅ |
| ACK 处理 | `core/handle/textHandler/ackHandler.py` | ✅ |
| 用户管理结果 | `core/handle/textHandler/userMgmtResultHandler.py` | ✅ |
| 人脸识别 | `core/handle/textHandler/faceRecognitionHandler.py` | ✅ |
| 监控模式 | `core/handle/textHandler/systemMessageHandler.py` | ✅ |
| 数据库存储 | `core/providers/doorlock/database.py` | ✅ |
| 转发给 App | 各 Handler 的 `_forward_to_apps()` | ✅ |

---

## 18. 版本历史

| 版本 | 日期 | 变更说明 |
|------|------|----------|
| v5.0 | 2024-12-11 | 整合协议，与 App 协议对齐；新增与 App 协议对比章节；完善实现状态 |
| v4.0 | 2024-12-09 | 新增 msg_id/ACK 机制、用户管理闭环 |
| v3.0 | 2024-12-08 | 新增监控模式、人脸识别 |
| v2.0 | 2024-12-07 | 新增锁控命令、事件上报 |
| v1.0 | 2024-12-06 | 初始版本，基础音频通信 |

---

**文档维护者：** 毕业设计项目组  
**最后更新：** 2024-12-11

---

## 附录：相关文档

- [App 通信协议规范 v2.2](智能猫眼门锁系统-App通信协议规范-v2.2.md) - App 与 Server 之间的通信协议
