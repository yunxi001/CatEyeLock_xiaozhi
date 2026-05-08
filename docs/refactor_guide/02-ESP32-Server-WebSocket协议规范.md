# ESP32 ↔ 服务器 WebSocket 通信协议规范 v6.0

> **链路：** B（ESP32 ↔ 服务器）
> **版本：** v6.0（简化版，单级ACK，从 v3.0 演进）
> **日期：** 2026-05-05

---

## 1. 传输层

| 项目 | 说明 |
|------|------|
| 主协议 | WebSocket |
| 备选协议 | MQTT + UDP（保留原始实现，本文档不涉及） |
| 连接地址 | 由OTA服务器下发 |
| 认证方式 | HTTP Header: `Authorization: Bearer <token>` |
| 端口 | 8000（WebSocket），8003（HTTP API） |

**注意：** MQTT+UDP备选通道保留原始 xiaozhi-esp32 的实现不变，本文档描述的JSON消息同时适用于WebSocket和MQTT文本帧。

## 2. 数据帧类型

| 帧类型 | 格式 | 用途 |
|--------|------|------|
| Text Frame | JSON | 信令控制、状态上报、用户管理、AI对话 |
| Binary Frame | BinaryProtocol2 | 音频流（OPUS）、视频流（JPEG）、人脸识别图像 |

### 2.1 BinaryProtocol2（保留原始实现）

16字节固定头部，用于音视频流和监控视频帧：

```text
+----------------+----------------+--------------------------------+
|   version(2B)  |    type(2B)    |         reserved(4B)           |
+----------------+----------------+--------------------------------+
|           timestamp(4B)         |        payload_size(4B)         |
+----------------+----------------+--------------------------------+
|                    payload data...                               |
+------------------------------------------------------------------+
```

| type | reserved | 数据类型 | 负载 |
|------|----------|---------|------|
| 0 | 0 | 音频流 | OPUS编码，16kHz，60ms帧 |
| 0 | 非0 = (width<<16)\|height | 监控视频流 | JPEG编码 |
| 2 | 非0 = (width<<16)\|height | 人脸识别图像 | JPEG编码 |

**此部分完全保留原始 xiaozhi-esp32 实现，不做任何修改。**

## 3. 保留的原始消息类型（不做修改）

以下消息类型来自原始 xiaozhi-esp32 项目，**协议格式和实现代码均保持不变**：

### 3.1 ESP32 → Server（原始消息）

| type | 用途 |
|------|------|
| `hello` | 连接后握手，告知音频参数和设备能力 |
| `listen` | 开始聆听状态通知 |
| `abort` | 中止TTS播放通知 |
| `wake_word_detected` | 检测到唤醒词 |

### 3.2 Server → ESP32（原始消息）

| type | 用途 |
|------|------|
| `hello` | 握手响应，协商音频参数 |
| `tts` | 文本转语音（状态：start/stop/sentence_start） |
| `stt` | 语音转文本结果 |
| `llm` | 大语言模型对话（含emotion字段） |
| `mcp` | MCP JSON-RPC 2.0 工具调用 |
| `system` | 系统命令（reboot、start_monitor、stop_monitor） |
| `alert` | 显示通知/警告 |
| `custom` | 自定义消息（可选，需编译开关） |

**这些消息的JSON格式、字段、处理逻辑完全不变。参见原始文档 `docs/websocket.md`。**

## 4. 新增门锁消息（简化版）

### 4.1 设计要点

- **无 seq_id 字段**：所有命令不带序列号
- **单级ACK**：ESP32执行完成后回复ack，不发送esp32_ack
- **无防重放**：不检查重复消息
- **错误码简化**：code: 0=成功, 1=失败 + msg描述

### 4.2 Server → ESP32（门锁命令，4种）

#### 4.2.1 lock_control — 锁控制

```json
{
  "type": "lock_control",
  "command": "unlock"
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| type | string | 是 | 固定为 `"lock_control"` |
| command | string | 是 | `"unlock"`（开锁）或 `"lock"`（关锁） |

**ESP32处理：**
1. 收到后向STM32发送CMD_LOCK
2. 等待STM32的RPT_UNLOCK（3秒超时）
3. 收到RPT_UNLOCK后：
   - 上报 log_report
   - 回复 ack {code:0}
4. 超时则回复 ack {code:1, msg:"STM32无响应"}

#### 4.2.2 dev_control — 设备控制

```json
// 蜂鸣器
{"type": "dev_control", "target": "beep", "count": 3, "mode": "alarm"}

// OLED
{"type": "dev_control", "target": "oled", "icon": 3}

// 补光灯
{"type": "dev_control", "target": "light", "action": "on"}
```

**蜂鸣器字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| target | string | 固定为 `"beep"` |
| count | int | 响铃次数（1-255） |
| mode | string | `"short"`（短滴）、`"long"`（长鸣）、`"alarm"`（报警） |

**OLED字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| target | string | 固定为 `"oled"` |
| icon | int | 图标ID：0=清屏, 1=WiFi已连接, 2=云端已连接, 3=识别中, 4=识别成功, 5=识别失败 |

**补光灯字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| target | string | 固定为 `"light"` |
| action | string | `"on"`（强制开）、`"off"`（强制关）、`"auto"`（恢复自动） |

**ESP32处理：**
1. 转换为对应的UART命令发送给STM32
2. 立即回复 ack {code:0}（不等待STM32响应）

#### 4.2.3 user_mgmt — 用户管理

```json
// 录入指纹
{"type": "user_mgmt", "category": "finger", "command": "add", "user_id": 0}

// 删除指定指纹
{"type": "user_mgmt", "category": "finger", "command": "del", "user_id": 5}

// 清空全部指纹
{"type": "user_mgmt", "category": "finger", "command": "clear"}

// 查询指纹数量
{"type": "user_mgmt", "category": "finger", "command": "query"}

// NFC操作
{"type": "user_mgmt", "category": "nfc", "command": "add", "user_id": 0}
{"type": "user_mgmt", "category": "nfc", "command": "del", "user_id": 3}

// 设置密码
{"type": "user_mgmt", "category": "password", "command": "set", "payload": "123456"}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| type | string | 是 | 固定为 `"user_mgmt"` |
| category | string | 是 | `"finger"`、`"nfc"`、`"password"` |
| command | string | 是 | `"add"`、`"del"`、`"clear"`、`"query"`、`"set"` |
| user_id | int | 否 | 0=自动分配ID；`del`命令时必填具体ID |
| payload | string | 否 | 仅 `category=password` 且 `command=set` 时有效（6位密码） |

**ESP32处理：**
1. 转换为对应的UART命令发送给STM32
2. 对于录入命令（add），等待完整的录入反馈序列（超时30秒），然后回复 ack 并发送 user_mgmt_result
3. 对于删除/清空命令，立即回复 ack {code:0}
4. 对于查询命令，等待STM32数量响应，回复 ack 并发送 user_mgmt_result
5. 对于密码设置，立即回复 ack {code:0}

#### 4.2.4 face_result — 人脸识别结果

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
| type | string | 固定为 `"face_result"` |
| result | string | `"known"`、`"unknown"`、`"no_face"`、`"error"` |
| user_id | int | 用户ID（known时有效） |
| access.granted | bool | 是否授权开锁 |
| access.reason | string | 授权/拒绝原因 |

**ESP32处理：**
1. 如果 `access.granted=true`，向STM32发送CMD_LOCK开锁
2. 等待RPT_UNLOCK，上报log_report
3. 此消息 **不需要回复ack**（Server主动推送的识别结果，不是App命令）
4. **face_result 可以中断当前正在等待的命令**：因为它是Server主动推送，不受App串行发送约束。如果ESP32正在等待某个命令的响应（如lock_control等待RPT_UNLOCK），face_result到达时优先处理——发送CMD_LOCK开锁，然后继续等待原来的响应。

### 4.4 并发控制

由于App串行发送命令（见文档00 §2.1第4条），ESP32同一时间最多处理一个来自App的命令。但 `face_result` 来自Server，可能随时到达。

#### 4.4.1 忙状态定义

ESP32维护一个简单的忙标志 `busy_`：

| 状态 | 触发条件 | 清除条件 |
|------|---------|---------|
| 忙 | 收到 `lock_control` 且正在等待RPT_UNLOCK | 收到RPT_UNLOCK或3秒超时 |
| 忙 | 收到 `user_mgmt`(add) 且正在等待录入序列 | 录入完成或30秒超时 |
| 忙 | 收到 `user_mgmt`(query) 且正在等待数量响应 | 收到响应或2秒超时 |
| 闲 | 其他所有时间 | — |

`dev_control`（beep/oled/light）不设置忙标志——它们发完UART即回复ack，不等待STM32响应。

#### 4.4.2 命令处理规则

```
收到命令时:
  if 命令 == face_result:
    → 无论busy_状态，立即处理（优先级最高）
    → 如果busy_且当前在等待RPT_UNLOCK，新开锁会触发新的RPT_UNLOCK
       → 将等待的RPT_UNLOCK计数器+1（STM32会上报两次RPT_UNLOCK）
  elif busy_:
    → 回复 ack{code:1, msg:"设备忙，请稍后重试"}
    → 不发送UART命令
  else:
    → 设置busy_=true
    → 处理命令
    → 清除busy_（在收到对应STM32响应或超时时）
```

#### 4.4.3 并发场景矩阵

| 当前操作 | 新到命令 | 处理方式 |
|---------|---------|---------|
| 空闲 | 任意App命令 | 正常处理 |
| 等待RPT_UNLOCK | `dev_control`(beep/oled/light) | **允许**（发完即回ack，不冲突） |
| 等待RPT_UNLOCK | `lock_control`(unlock) | **拒绝**（返回ack{code:1, msg:"设备忙"}） |
| 指纹录入中(30s) | `dev_control` | **允许**（发完即回ack） |
| 指纹录入中(30s) | `lock_control` | **拒绝**（返回ack{code:1, msg:"设备忙"}） |
| 等待RPT_UNLOCK | `face_result` | **允许**（中断，优先处理开锁） |
| 指纹录入中(30s) | `face_result` | **允许**（中断录入，先处理开锁） |

**注意：** `dev_control` 的命令（beep/oled/light）在任何时候都允许，因为它们不需要等待STM32响应，且与STM32的UART通信是原子的（单帧发送）。这也意味着OLED/蜂鸣器可能打断当前的"录入中"显示状态——这是可接受的设计取舍。

### 4.3 ESP32 → Server（门锁上报，6种）

#### 4.3.1 ack — 统一确认

```json
{"type": "ack", "code": 0, "msg": "ok"}
{"type": "ack", "code": 1, "msg": "STM32无响应"}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| type | string | 固定为 `"ack"` |
| code | int | `0` = 成功，`1` = 失败 |
| msg | string | 结果描述 |

**发送时机：** ESP32收到 lock_control、dev_control、user_mgmt 命令并处理完成后发送。

**注意：** face_result 不触发ack。

#### 4.3.2 status_report — 状态上报

```json
{
  "type": "status_report",
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
| bat | int | 电量百分比（0-100） |
| lux | int | 光照值（Lux） |
| lock | int | 锁状态：0=关闭，1=打开 |
| light | int | 补光灯：0=灭，1=亮 |

**发送时机：** STM32上报RPT_ENV或RPT_STATE时，ESP32转换后发送。状态变化时发送，不轮询。

#### 4.3.3 event_report — 事件上报

```json
{"type": "event_report", "event": "bell"}
{"type": "event_report", "event": "pir_trigger", "param": 3}
{"type": "event_report", "event": "tamper", "param": 1}
{"type": "event_report", "event": "door_open", "param": 5}
{"type": "event_report", "event": "low_battery", "param": 15}
{"type": "event_report", "event": "door_closed"}
{"type": "event_report", "event": "lock_success"}
```

| event | 说明 | param | 对应STM32事件 |
|-------|------|-------|-------------|
| `bell` | 门铃按下 | 无 | 0x01 |
| `pir_trigger` | PIR人体检测 | 持续时间（秒） | 0x02 |
| `tamper` | 撬锁报警 | 报警级别（1-3） | 0x03 |
| `door_open` | 门未关超时 | 超时时间（分钟） | 0x04 |
| `low_battery` | 低电量 | 当前电量（%） | 0x05 |
| `door_closed` | 门已关闭 | 无 | 0x06 status=0 |
| `lock_success` | 上锁成功 | 无 | 0x06 status=1 |

**发送时机：** STM32上报RPT_EVENT时，ESP32转换后发送。

#### 4.3.4 log_report — 开锁日志

```json
{
  "type": "log_report",
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
| method | string | 开锁方式：`finger`、`nfc`、`face`、`pwd`、`temp_pwd`、`key`、`remote` |
| status | string | `"success"`（成功）、`"fail"`（失败）、`"locked"`（已锁定） |
| uid | int | 用户ID（成功时），失败时可能为0 |
| fail_count | int | 连续失败次数（1-5），仅fail状态有效 |
| lock_time | int | 剩余锁定时间（分钟），仅locked状态有效 |

**发送时机：** STM32上报RPT_UNLOCK时，ESP32转换后发送。

#### 4.3.5 door_opened_report — 开门日志

```json
{
  "type": "door_opened_report",
  "data": {
    "method": "finger",
    "source": "outside"
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| method | string | 开锁方式（同log_report） |
| source | string | `"outside"`（室外）、`"inside"`（室内）、`"unknown"`（未知） |

**发送时机：** STM32上报RPT_DOOR_OPENED时，ESP32转换后发送。

#### 4.3.6 user_mgmt_result — 用户管理结果

```json
{
  "type": "user_mgmt_result",
  "category": "finger",
  "command": "add",
  "result": true,
  "val": 6,
  "msg": "录入成功"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| type | string | 固定为 `"user_mgmt_result"` |
| category | string | `"finger"`、`"nfc"`、`"password"` |
| command | string | `"add"`、`"del"`、`"clear"`、`"query"`、`"set"` |
| result | bool | true=成功，false=失败 |
| val | int | 成功时=分配的ID或查询的总数；失败时=错误原因码 |
| msg | string | 结果描述 |

**发送时机：** 用户管理操作完成后发送（与ack一并发送）。

## 5. ESP32端消息处理流程

### 5.1 消息分发逻辑（伪代码）

```
OnIncomingJson(root):
  type = root["type"]
  
  // 原始消息（保持不变）
  if type == "tts":       HandleTts(root)
  if type == "stt":       HandleStt(root)
  if type == "llm":       HandleLlm(root)
  if type == "mcp":       HandleMcp(root)
  if type == "system":    HandleSystem(root)     // reboot, start/stop_monitor
  if type == "alert":     HandleAlert(root)
  if type == "custom":    HandleCustom(root)
  if type == "local_preview": HandleLocalPreview(root)
  
  // 门锁消息（简化版）
  if type == "lock_control":   HandleLockControl(root)
  if type == "dev_control":    HandleDevControl(root)
  if type == "user_mgmt":      HandleUserMgmt(root)
  if type == "face_result":    HandleFaceResult(root)
```

### 5.2 HandleLockControl 逻辑

```
1. 解析 command 字段
2. 构建 UART CMD_LOCK 帧
3. 发送给 STM32
4. 等待 RPT_UNLOCK（3秒超时）
5. 收到 RPT_UNLOCK → 发送 log_report → 发送 ack{code:0}
6. 超时 → 发送 ack{code:1, msg:"STM32无响应"}
```

### 5.3 HandleDevControl 逻辑

```
1. 解析 target 字段
2. 转换为对应 UART 命令：
   - beep  → CMD_BEEP
   - oled  → CMD_OLED
   - light → CMD_LIGHT
3. 发送给 STM32
4. 立即发送 ack{code:0}
```

### 5.4 HandleUserMgmt 逻辑

```
1. 解析 category + command
2. 转换为对应 UART 命令
3. 如果是 add（录入）：
   - 等待完整的 FP_RESP/NFC_RESP 序列（30秒超时）
   - 发送 user_mgmt_result
   - 发送 ack
4. 如果是 del/clear：
   - 发送 UART 命令后立即发送 ack{code:0}
5. 如果是 query：
   - 等待 FP_RESP(0x05) 或 NFC_RESP(0x05)（2秒超时）
   - 发送 user_mgmt_result
   - 发送 ack
6. 如果是 set（密码）：
   - 发送 UART 命令后立即发送 ack{code:0}
```

### 5.5 HandleFaceResult 逻辑

```
1. 解析 result + access.granted
2. 如果 access.granted == true：
   - 发送 CMD_LOCK(开锁) 给 STM32
   - 等待 RPT_UNLOCK
   - 发送 log_report
3. 如果 access.granted == false：
   - 发送 CMD_OLED(识别失败) + CMD_BEEP(报警)
4. 不发送 ack（face_result不是用户命令）
```

## 6. 消息类型汇总

### 6.1 ESP32 → Server

| type | 来源 | 携带数据 |
|------|------|---------|
| `hello` | 原始 | 音频参数、设备能力 |
| `listen` | 原始 | 聆听状态 |
| `abort` | 原始 | 中止原因 |
| `wake_word_detected` | 原始 | 唤醒词 |
| `ack` | **新增** | 命令执行结果（code + msg） |
| `status_report` | **新增** | 电量、光照、锁、灯 |
| `event_report` | **新增** | 事件类型 + 参数 |
| `log_report` | **新增** | 开锁方法、结果、用户ID |
| `door_opened_report` | **新增** | 开锁方法、开门来源 |
| `user_mgmt_result` | **新增** | 用户管理操作结果 |
| BinaryProtocol2 音频 | 原始 | OPUS编码音频 |
| BinaryProtocol2 视频 | 原始 | JPEG监控视频 |
| BinaryProtocol2 人脸 | 原始 | JPEG人脸图像 |

### 6.2 Server → ESP32

| type | 来源 | 说明 |
|------|------|------|
| `hello` | 原始 | 握手响应 |
| `tts` | 原始 | TTS语音合成 |
| `stt` | 原始 | 语音识别结果 |
| `llm` | 原始 | LLM对话 |
| `mcp` | 原始 | MCP工具调用 |
| `system` | 原始 | reboot/start_monitor/stop_monitor |
| `alert` | 原始 | 通知/警告 |
| `custom` | 原始 | 自定义消息 |
| `lock_control` | **新增** | 开锁/关锁 |
| `dev_control` | **新增** | 蜂鸣器/OLED/补光灯 |
| `user_mgmt` | **新增** | 指纹/NFC/密码管理 |
| `face_result` | **新增** | 人脸识别结果 |
| BinaryProtocol2 音频 | 原始 | OPUS编码TTS音频 |

## 7. 监控模式（保留原始实现）

监控模式的启动/停止使用原始 `system` 消息：

```json
// 启动监控
{"type": "system", "command": "start_monitor"}

// 停止监控
{"type": "system", "command": "stop_monitor"}
```

监控模式下的音视频流使用BinaryProtocol2，保留原始实现。

## 8. 人脸识别完整流程

```
1. STM32检测到门铃/PIR
   STM32 → ESP32: RPT_EVENT (门铃 或 PIR)

2. ESP32拍照并上传
   ESP32 → Server: BinaryProtocol2 type=2 (JPEG人脸图像)

3. Server AI人脸识别
   Server → ESP32: face_result {result:"known", user_id:5, access:{granted:true}}

4. ESP32发送开锁命令
   ESP32 → STM32: CMD_LOCK (开锁)

5. STM32上报开锁结果
   STM32 → ESP32: RPT_UNLOCK (远程开锁, 成功)

6. ESP32上报日志
   ESP32 → Server: log_report {method:"face", status:"success", uid:5}

7. 用户开门
   STM32 → ESP32: RPT_DOOR_OPENED

8. ESP32上报开门日志
   ESP32 → Server: door_opened_report {method:"face", source:"outside"}
```

**注意：** 此流程中 face_result 不触发 ack（它不是用户命令，是Server主动推送）。

## 9. 版本历史

| 版本 | 日期 | 变更说明 |
|------|------|---------|
| ... | ... | v1.0–v3.0 历史版本 |
| v6.0 | 2026-05-05 | 简化版：去掉了esp32_ack、seq_id、防重放、heartbeat、pending_commands；错误码简化为0/1+msg；原始消息类型保持不变 |
