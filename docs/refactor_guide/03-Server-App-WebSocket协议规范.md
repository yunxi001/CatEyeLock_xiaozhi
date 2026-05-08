# 服务器 ↔ App WebSocket 通信协议规范 v6.0

> **链路：** C（服务器 ↔ App）
> **版本：** v6.0（简化版，无server_ack，从 v2.5 演进）
> **日期：** 2026-05-05

---

## 1. 传输层

| 项目 | 说明 |
|------|------|
| 传输协议 | WebSocket |
| 连接端点 | `ws://{server}:8000/ws/app` |
| 数据格式 | JSON文本帧 + PCM二进制帧（监控对讲） |

## 2. 连接架构

```
┌─────────┐         ┌─────────┐         ┌─────────┐
│   App   │◄───────►│ Server  │◄───────►│  ESP32  │
└─────────┘         └─────────┘         └─────────┘
   /ws/app              │                  /ws
                        │
               ConnectionManager
               (设备关联管理)
```

Server的角色是**透明代理**：
- App发送的命令 → Server记录日志 → 原封不动转发给ESP32
- ESP32的上报 → Server存储数据库 → 原封不动转发给App
- Server自己生成的推送 → visit_notification、visitor_intent_notification、device_status

## 3. 通信模式

| 模式 | 说明 |
|------|------|
| 请求-响应 | App发送命令，收到ESP32的ack即完成 |
| 服务器推送 | Server主动推送ESP32上报的状态、事件、通知 |
| 实时流 | 监控模式下的音视频流（二进制帧） |

**App命令发送约束：App必须串行发送命令**，即在前一个命令收到ack之前，不发送下一个命令。这是因为ack消息不带命令标识（无seq_id），App通过"下一个ack就是当前命令的响应"来匹配。违反此约束会导致ack错配。

实现方式：
- 所有控制按钮在点击后立即disabled，收到ack或超时后恢复
- 或使用命令队列，上一个命令的ack回来后自动发送下一个

**不再使用的模式：**
- ~~命令代理 + server_ack~~ → 直接等ESP32的ack，Server不单独确认
- ~~WebSocket查询~~ → 改为HTTP API（见§9说明）

## 4. 连接认证

### 4.1 Hello 请求 (App → Server)

```json
{
  "type": "hello",
  "device_id": "AA:BB:CC:DD:EE:FF",
  "app_id": "user_12345"
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| type | string | 是 | 固定为 `"hello"` |
| device_id | string | 是 | 要关联的ESP32设备MAC地址（格式：`AA:BB:CC:DD:EE:FF`） |
| app_id | string | 是 | App用户唯一标识，用于日志审计 |

### 4.2 Hello 响应 (Server → App)

**成功：**
```json
{
  "type": "hello",
  "status": "ok",
  "device_info": {
    "online": true,
    "mode": "normal"
  }
}
```

**失败：**
```json
{
  "type": "hello",
  "status": "error",
  "message": "设备不在线"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| status | string | `"ok"` 或 `"error"` |
| device_info.online | bool | ESP32是否在线 |
| device_info.mode | string | `"normal"` 或 `"monitor"` |
| message | string | 失败时的错误描述 |

## 5. App → Server（命令代理）

App发送的命令**格式与ESP32协议完全一致**。Server验证后原封不动转发。

### 5.1 锁控制

```json
{"type": "lock_control", "command": "unlock"}
{"type": "lock_control", "command": "lock"}
```

### 5.2 设备控制

```json
{"type": "dev_control", "target": "beep", "count": 3, "mode": "alarm"}
{"type": "dev_control", "target": "oled", "icon": 3}
{"type": "dev_control", "target": "light", "action": "on"}
```

### 5.3 用户管理

```json
{"type": "user_mgmt", "category": "finger", "command": "add", "user_id": 0, "user_name": "张三的右手食指"}
{"type": "user_mgmt", "category": "finger", "command": "del", "user_id": 5}
{"type": "user_mgmt", "category": "nfc", "command": "add", "user_id": 0, "user_name": "门禁卡"}
{"type": "user_mgmt", "category": "password", "command": "set", "payload": "123456"}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| user_name | string | 可选，仅App端使用。给指纹/NFC添加备注名称，Server存入数据库，不转发给ESP32 |

### 5.4 系统命令

```json
// 启动监控
{"type": "system", "command": "start_monitor"}

// 停止监控
{"type": "system", "command": "stop_monitor"}
```

### 5.5 Server处理流程

```
App发送命令
    │
    ▼
Server验证:
  1. App是否已完成hello认证？ → 否 → 返回错误
  2. 目标ESP32是否在线？     → 否 → 返回错误
  3. 检查通过 → 记录操作日志（app_id、命令内容、时间戳）
    │
    ▼
Server转发给ESP32（JSON不变，不添加任何字段）
    │
    ▼
Server等待ESP32的ack...
    │
    ▼
收到ack → 转发给App
```

## 6. Server → App（推送消息）

### 6.1 ESP32上报透传（6种）

以下消息由ESP32发出，Server**原封不动**转发给所有关联该设备的App：

| type | 说明 | 格式参见 |
|------|------|---------|
| `ack` | 命令执行结果 | [02-ESP32-Server协议](02-ESP32-Server-WebSocket协议规范.md#431-ack--统一确认) |
| `status_report` | 设备状态 | [02-ESP32-Server协议](02-ESP32-Server-WebSocket协议规范.md#432-status_report--状态上报) |
| `event_report` | 事件通知 | [02-ESP32-Server协议](02-ESP32-Server-WebSocket协议规范.md#433-event_report--事件上报) |
| `log_report` | 开锁日志 | [02-ESP32-Server协议](02-ESP32-Server-WebSocket协议规范.md#434-log_report--开锁日志) |
| `door_opened_report` | 开门日志 | [02-ESP32-Server协议](02-ESP32-Server-WebSocket协议规范.md#435-door_opened_report--开门日志) |
| `user_mgmt_result` | 用户管理结果 | [02-ESP32-Server协议](02-ESP32-Server-WebSocket协议规范.md#436-user_mgmt_result--用户管理结果) |

### 6.2 设备上下线通知（Server生成）

```json
// 上线
{"type": "device_status", "status": "online", "device_id": "AA:BB:CC:DD:EE:FF", "ts": 1702234567890}

// 下线
{"type": "device_status", "status": "offline", "device_id": "AA:BB:CC:DD:EE:FF", "ts": 1702234567890, "reason": "connection_lost"}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| status | string | `"online"` 或 `"offline"` |
| device_id | string | 设备MAC地址 |
| ts | int | 时间戳（毫秒） |
| reason | string | 下线原因（仅offline时）：`"connection_lost"`、`"connection_closed"`、`"timeout"`、`"error"` |

### 6.3 到访通知（Server生成）

人脸识别完成后，Server生成到访通知：

```json
{
  "type": "visit_notification",
  "ts": 1702234567890,
  "data": {
    "visit_id": 123,
    "person_id": 5,
    "person_name": "张三",
    "relation": "family",
    "result": "known",
    "access_granted": true,
    "image_path": "faces/AA:BB:CC:DD:EE:FF/2024-12-11/face_1702234567890_5.jpg"
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| visit_id | int | 到访记录ID |
| person_id | int | 人员ID（陌生人为null） |
| person_name | string | 人员姓名（陌生人为"陌生人"） |
| relation | string | 关系类型 |
| result | string | `"known"`、`"unknown"`、`"no_face"` |
| access_granted | bool | 是否授权开锁 |
| image_path | string | 抓拍图片的服务器存储路径 |

**注意：** 不再包含Base64图片数据（`image`字段），App如需显示图片，通过HTTP API下载。

### 6.4 访客意图通知（Server生成，保留）

```json
{
  "type": "visitor_intent_notification",
  "ts": 1702234567890,
  "visit_id": 12345,
  "session_id": "session_123",
  "person_info": {
    "person_id": 10,
    "name": "张三",
    "relation_type": "family"
  },
  "intent_summary": {
    "intent_type": "delivery",
    "summary": "快递员送快递，已放门口",
    "important_notes": ["【留言】快递放门口了，麻烦签收一下"],
    "ai_analysis": "访客是快递员，来送快递，表示快递已放在门口，请求签收。"
  },
  "dialogue_history": [
    {"role": "assistant", "content": "您好，请问有什么可以帮您？"},
    {"role": "user", "content": "我是来送快递的"}
  ],
  "package_check": {
    "threat_level": "low",
    "action": "normal",
    "description": "快递安全，未被触碰"
  }
}
```

**此消息完全保留旧版格式不变。** 这是重要的AI功能。

## 7. 消息类型汇总

### 7.1 App → Server

| type | 说明 |
|------|------|
| `hello` | 连接认证 |
| `lock_control` | 锁控制（开锁/关锁） |
| `dev_control` | 设备控制（蜂鸣器/OLED/补光灯） |
| `user_mgmt` | 用户管理（指纹/NFC/密码） |
| `system` | 系统命令（启动/停止监控） |

### 7.2 Server → App

| type | 来源 | 说明 |
|------|------|------|
| `hello` | Server | 认证响应 |
| `ack` | ESP32透传 | 命令执行结果 |
| `status_report` | ESP32透传 | 设备状态 |
| `event_report` | ESP32透传 | 事件通知 |
| `log_report` | ESP32透传 | 开锁日志 |
| `door_opened_report` | ESP32透传 | 开门日志 |
| `user_mgmt_result` | ESP32透传 | 用户管理结果 |
| `device_status` | Server | 设备上下线通知 |
| `visit_notification` | Server | 到访通知 |
| `visitor_intent_notification` | Server | 访客意图通知 |

## 8. 与旧版协议的主要差异

| 功能 | 旧版(v5.2/v3.0/v2.5) | 新版(v6.0) |
|------|-----------|-----------|
| server_ack | Server收到命令后立即回复 | **去掉** |
| seq_id | App生成，全链路追踪 | **去掉** |
| 错误码 | 0-10统一错误码 + code/error/message三种格式 | **统一 {code, msg}** |
| query（WebSocket查询） | 9种查询（status/history/events/unlock_logs/media/password/doorlock_users/visitor_intents/package_alerts） | **去掉**（改为HTTP API） |
| media_download | WebSocket分片下载 | **去掉**（改为HTTP下载） |
| face_management | 独立消息类型（register/get_persons/delete_person/update_permission/get_visits） | **去掉**（改为HTTP API） |
| 命令代理格式 | App消息 → Server添加msg_id → 转发ESP32 | **原封不动转发** |
| esp32_ack | ESP32收到命令即发，不转发给App | **去掉** |
| visit_notification中的image | Base64内嵌图片 | **去掉**（改为image_path） |

## 9. HTTP API 接口

### 9.1 为什么查询改为HTTP API而不是WebSocket

WebSocket query在无seq_id的简化协议下有两个问题：

1. **响应匹配困难**：App发送query后，响应是异步的。没有seq_id无法区分"这是哪个query的响应"，和ack匹配问题是同源的。
2. **大体积响应阻塞实时推送**：历史日志、媒体文件列表等响应体积大，会阻塞同一条WebSocket上的实时事件（如门铃推送）。

HTTP API天然是请求-响应模式，不存在匹配问题，且不占用WebSocket通道。

### 9.2 实现优先级

HTTP API的实现在本重构包中分为两档：

**必须实现（替代WebSocket query的核心查询）：**

| 端点 | 方法 | 用途 |
|------|------|------|
| `/api/doorlock/status` | GET | 查询设备当前状态（App打开首页时需要） |
| `/api/doorlock/faces` | GET/POST | 人脸列表查询与注册 |

**后续扩展（本次重构可暂不实现）：**

| 端点 | 方法 | 用途 |
|------|------|------|
| `/api/doorlock/events` | GET | 查询事件历史 |
| `/api/doorlock/unlock_logs` | GET | 查询开锁日志 |
| `/api/doorlock/media` | GET | 查询媒体文件列表 |
| `/api/doorlock/media/<id>` | GET | 下载媒体文件 |
| `/api/doorlock/faces/<id>` | DELETE | 删除人脸 |
| `/api/doorlock/password` | GET | 查询设备密码 |

### 9.3 保留的HTTP API

以下原有HTTP API保持不变：

| 端点 | 方法 | 用途 |
|------|------|------|
| `/api/doorlock/config` | GET/POST | 门锁配置 |
| `/api/doorlock/guard` | POST/DELETE | 看护模式 |
| `/api/doorlock/welcome` | GET/POST | 欢迎词 |
| `/api/doorlock/history/intent` | GET | 访客意图历史 |
| `/api/doorlock/history/package_alarm` | GET | 快递警报历史 |




## 10. 典型交互流程

### 10.1 App远程开锁

```
App                    Server                   ESP32
 │                        │                        │
 │── lock_control ───────►│                        │
 │   (command: "unlock") │                        │
 │                        │ 记录日志               │
 │                        │── lock_control ───────►│
 │                        │   (完全相同的JSON)     │
 │                        │                        │── CMD_LOCK ──►STM32
 │                        │                        │◀─ RPT_UNLOCK ─STM32
 │                        │◀── log_report ────────│
 │                        │ 存储数据库             │
 │◀── log_report ────────│ (透传)                 │
 │                        │◀── ack ───────────────│
 │◀── ack ───────────────│ (透传)                 │
```

### 10.2 App收到门铃推送

```
STM32                ESP32                   Server                   App
 │                     │                        │                        │
 │── RPT_EVENT ──────►│                        │                        │
 │   (门铃)           │                        │                        │
 │                     │── event_report ───────►│                        │
 │                     │   (event: "bell")     │                        │
 │                     │                        │ 存储数据库             │
 │                     │                        │── event_report ──────►│
 │                     │                        │   (透传)              │
 │                     │                        │                        │
 │                     │── BinaryProtocol2 ────►│                        │
 │                     │   (type=2, JPEG人脸)   │ AI人脸识别             │
 │                     │                        │── visit_notification ─►│
 │                     │                        │   (Server生成)        │
 │                     │◀── face_result ───────│                        │
 │   (如果授权开锁)    │                        │                        │
 │── CMD_LOCK ───────►│                        │                        │
```

## 11. 版本历史

| 版本 | 日期 | 变更说明 |
|------|------|---------|
| ... | ... | v1.0–v2.5 历史版本 |
| v6.0 | 2026-05-05 | 简化版：去掉server_ack、seq_id、WebSocket query、media_download、face_management消息类型；Server做透明代理不修改消息格式；visit_notification去掉Base64图片改用image_path |
