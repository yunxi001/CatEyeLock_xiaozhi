# ESP32 接收 STM32 消息处理分析

> 生成日期：2026-01-13  
> 基于代码版本：lock_protocol.h v2.0, lock_control.cc v2.0, application.cc

---

## 1. 概述

### 1.1 消息流向

```
STM32 (UART TX)
    │
    │ 7字节协议帧
    ▼
ESP32 LockControlService::RxLoop()
    │
    │ 解析 LockMessage
    ▼
ESP32 Application::HandleLockEvent()
    │
    │ 根据 CAT 分发
    ├─► HandleLockReportMessage()  (CAT=0x01)
    ├─► HandleLockSystemMessage()  (CAT=0x00)
    └─► HandleLockUserMessage()    (CAT=0x03)
    │
    │ 转发到服务器
    ▼
Server (WebSocket)
```

### 1.2 消息类别

| CAT | 名称 | 方向 | 说明 |
|-----|------|------|------|
| 0x00 | SYS | 双向 | 系统握手（ACK、心跳） |
| 0x01 | RPT | STM→ESP | 状态/事件上报 |
| 0x02 | CMD | ESP→STM | 控制命令（本文档不涉及） |
| 0x03 | USER | 双向 | 用户管理反馈 |

---

## 2. 上报消息处理 (CAT = 0x01)

### 2.1 RPT_EVENT (0xA0) - 事件上报

**帧格式**：`[AA][01][A0][事件ID][参数][0x00][CS]`

| 事件ID | 宏定义 | ESP32 处理 | 服务器上报 |
|--------|--------|------------|------------|
| 0x01 | EVT_DOORBELL | 触发人脸识别 `TriggerFaceRecognition()` | `event_report: bell` |
| 0x02 | EVT_PIR | 触发人脸识别 `TriggerFaceRecognition()` | `event_report: pir_trigger` |
| 0x03 | EVT_TAMPER | 蜂鸣器报警 + 显示警告 `HandleTamperAlert()` | `event_report: tamper` |
| 0x04 | EVT_DOOR_OPEN | 显示提示 `HandleDoorNotClosed()` | `event_report: door_open` |
| 0x05 | EVT_LOW_BATTERY | 显示低电量警告 | `event_report: low_battery` |


**详细处理流程**：

#### 2.1.1 门铃事件 (EVT_DOORBELL = 0x01)

```
STM32: [AA][01][A0][01][00][00][CS]
                    │   │
                    │   └─ 参数（无意义）
                    └───── 事件ID: 门铃

ESP32 处理：
1. 日志: "门铃按下 - 触发人脸识别"
2. 调用 TriggerFaceRecognition()
   - 检查设备状态（仅 Idle/Listening 允许）
   - 检查内存（需 100KB+ PSRAM）
   - 拍照 + JPEG 编码
   - 发送 BinaryProtocol2 (type=2) 到服务器
3. 上报服务器: {"type":"event_report","event":"bell","param":0}
```

#### 2.1.2 PIR 人体检测 (EVT_PIR = 0x02)

```
STM32: [AA][01][A0][02][03][00][CS]
                    │   │
                    │   └─ 持续时间: 3秒
                    └───── 事件ID: PIR

ESP32 处理：
1. 日志: "PIR 检测到人体 (持续 3 秒) - 触发人脸识别"
2. 调用 TriggerFaceRecognition()（同门铃）
3. 上报服务器: {"type":"event_report","event":"pir_trigger","param":3}
```

#### 2.1.3 撬锁报警 (EVT_TAMPER = 0x03)

```
STM32: [AA][01][A0][03][02][00][CS]
                    │   │
                    │   └─ 报警级别: 2
                    └───── 事件ID: 撬锁

ESP32 处理：
1. 日志: "撬锁报警 (级别 2)"
2. 调用 HandleTamperAlert(2)
   - 发送蜂鸣器命令: SendBeep(6, BEEP_ALARM)
   - 显示警报: Alert("警报", "检测到暴力破坏", ...)
3. 上报服务器: {"type":"event_report","event":"tamper","param":2}
```

#### 2.1.4 门未关超时 (EVT_DOOR_OPEN = 0x04)

```
STM32: [AA][01][A0][04][05][00][CS]
                    │   │
                    │   └─ 超时时间: 5分钟
                    └───── 事件ID: 门未关

ESP32 处理：
1. 日志: "门未关超时 (5 分钟)"
2. 调用 HandleDoorNotClosed()
   - 显示提示: Alert("提示", "门未关严实", ...)
3. 上报服务器: {"type":"event_report","event":"door_open","param":5}
```

#### 2.1.5 低电量警告 (EVT_LOW_BATTERY = 0x05)

```
STM32: [AA][01][A0][05][15][00][CS]
                    │   │
                    │   └─ 当前电量: 21%
                    └───── 事件ID: 低电量

ESP32 处理：
1. 日志: "低电量警告: 21%"
2. 显示警告: Alert("", "电量低", "", "")
3. 上报服务器: {"type":"event_report","event":"low_battery","param":21}
```

---

### 2.2 RPT_UNLOCK (0xA1) - 开锁日志

**帧格式**：`[AA][01][A1][开锁方式][用户ID][结果][CS]`

| 字段 | 位置 | 说明 |
|------|------|------|
| 开锁方式 | D0 | 见下表 |
| 用户ID | D1 | 指纹/NFC ID (1-40)，密码/远程为 0 |
| 结果 | D2 | 0=成功，其他=失败次数 |

**开锁方式映射**：

| D0 值 | 枚举 | 服务器 method |
|-------|------|---------------|
| 0x01 | UNLOCK_FINGERPRINT | `finger` |
| 0x02 | UNLOCK_NFC | `nfc` |
| 0x03 | UNLOCK_PASSWORD | `pwd` |
| 0x04 | UNLOCK_REMOTE | `remote` |
| 0x05 | UNLOCK_KEY | `key` |
| 0x06 | UNLOCK_TEMP_PWD | `temp_pwd` |
| 0x07 | UNLOCK_FACE | `face` |


**处理流程**：

```
STM32: [AA][01][A1][01][05][00][CS]
                    │   │   │
                    │   │   └─ 结果: 成功
                    │   └───── 用户ID: 5
                    └───────── 方式: 指纹

ESP32 处理：
1. 日志: "开锁日志: 方式=1, ID=5, 结果=0"
2. 转换方式: GetUnlockMethodString(0x01) → "finger"
3. 上报服务器:
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

---

### 2.3 RPT_ENV (0xB0) - 环境数据

**帧格式**：`[AA][01][B0][电量%][光照H][光照L][CS]`

| 字段 | 位置 | 说明 |
|------|------|------|
| 电量 | D0 | 百分比 (0-100) |
| 光照高字节 | D1 | Lux 高 8 位 |
| 光照低字节 | D2 | Lux 低 8 位 |

**处理流程**：

```
STM32: [AA][01][B0][55][01][2C][CS]
                    │   │   │
                    │   │   └─ 光照低字节: 0x2C
                    │   └───── 光照高字节: 0x01
                    └───────── 电量: 85%

ESP32 处理：
1. 解析: battery=85, lux=(0x01<<8)|0x2C=300
2. 日志: "环境数据: 电量=85%, 光照=300 Lux"
3. 检查变化: 与上次数据比较
4. 保存: last_battery_=85, last_lux_=300
5. 若有变化，上报服务器:
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

---

### 2.4 RPT_STATE (0xB1) - 状态上报

**帧格式**：`[AA][01][B1][锁状态][灯状态][0x00][CS]`

| 字段 | 位置 | 值 | 说明 |
|------|------|-----|------|
| 锁状态 | D0 | 0x00 | 锁已关 |
| | | 0x01 | 锁已开 |
| 灯状态 | D1 | 0x00 | 灯灭 |
| | | 0x01 | 灯亮 |

**处理流程**：

```
STM32: [AA][01][B1][00][01][00][CS]
                    │   │
                    │   └─ 灯状态: 亮
                    └───── 锁状态: 关

ESP32 处理：
1. 解析: lock_open=false, light_on=true
2. 日志: "状态: 锁=关, 灯=亮"
3. 保存: last_lock_state_=0, last_light_state_=1
4. 上报服务器:
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

---

### 2.5 RPT_PWD (0xC0) - 密码查询结果

**帧格式**：`[AA][01][C0][Pwd_H][Pwd_M][Pwd_L][CS]`

**处理流程**：

```
STM32: [AA][01][C0][01][E2][40][CS]
                    │   │   │
                    │   │   └─ 低字节: 0x40
                    │   └───── 中字节: 0xE2
                    └───────── 高字节: 0x01

ESP32 处理：
1. 解码: pwd = (0x01<<16)|(0xE2<<8)|0x40 = 123456
2. 日志: "当前密码: 123456"
3. 不上报服务器（安全考虑）
```

---

## 3. 系统消息处理 (CAT = 0x00)

### 3.1 ACK_OK (0x01) - 成功应答

**帧格式**：`[AA][00][01][原TYPE][0x00][0x00][CS]`

```
STM32: [AA][00][01][10][00][00][CS]
                    │
                    └─ 原指令TYPE: CMD_LOCK

ESP32 处理：
1. 日志(DEBUG): "收到 ACK_OK，原指令 TYPE=0x10"
2. 无其他操作
```

### 3.2 ACK_ERR (0x00) - 错误应答

**帧格式**：`[AA][00][00][原TYPE][错误码][0x00][CS]`

**错误码定义**：

| 错误码 | 宏定义 | 说明 |
|--------|--------|------|
| 0x01 | ERR_BUSY | 设备忙 |
| 0x02 | ERR_UNSUPPORT | 不支持的命令 |
| 0x03 | ERR_PARAM | 参数错误 |
| 0x04 | ERR_FP_FULL | 指纹库已满 |
| 0x05 | ERR_NFC_FULL | NFC 库已满 |
| 0x06 | ERR_HARDWARE | 硬件错误 |
| 0xFF | ERR_TIMEOUT | 操作超时 |

```
STM32: [AA][00][00][10][01][00][CS]
                    │   │
                    │   └─ 错误码: 设备忙
                    └───── 原指令TYPE: CMD_LOCK

ESP32 处理：
1. 日志(WARN): "收到 ACK_ERR，原指令 TYPE=0x10，错误码=0x01"
2. 无其他操作（可扩展重试逻辑）
```

### 3.3 SYS_PONG (0xF1) - 心跳响应

**帧格式**：`[AA][00][F1][0x00][0x00][0x00][CS]`

```
ESP32 处理：
1. 日志(DEBUG): "收到心跳响应"
2. 无其他操作
```


---

## 4. 用户管理反馈处理 (CAT = 0x03)

### 4.1 FP_RESP (0x11) - 指纹管理反馈

**帧格式**：`[AA][03][11][状态][参数1][参数2][CS]`

**状态码定义**：

| 状态码 | 宏定义 | 说明 | D1 含义 | D2 含义 |
|--------|--------|------|---------|---------|
| 0x01 | FP_PRESS_FINGER | 请按手指 | 当前次数 | 0x00 |
| 0x02 | FP_LIFT_FINGER | 请抬起手指 | 0x00 | 0x00 |
| 0x03 | FP_SUCCESS | 录入成功 | 新分配ID | 0x00 |
| 0x04 | FP_FAILED | 操作失败 | 错误码 | 0x00 |
| 0x05 | FP_COUNT_RESP | 数量查询 | 总数 | 0x00 |

**处理流程**：

#### 4.1.1 录入中间状态（不上报服务器）

```
STM32: [AA][03][11][01][02][00][CS]  (请按手指，第2次)
ESP32: 日志 "指纹录入：请按手指 (第 2 次)"
       不上报服务器

STM32: [AA][03][11][02][00][00][CS]  (请抬起手指)
ESP32: 日志 "指纹录入：请抬起手指"
       不上报服务器
```

#### 4.1.2 录入成功

```
STM32: [AA][03][11][03][05][00][CS]
                    │   │
                    │   └─ 新分配ID: 5
                    └───── 状态: 成功

ESP32 处理：
1. 日志: "指纹录入成功，ID=5"
2. 上报服务器:
   {
     "type": "user_mgmt_result",
     "category": "finger",
     "command": "add",
     "result": true,
     "val": 5,
     "msg": "Success"
   }
```

#### 4.1.3 操作失败

```
STM32: [AA][03][11][04][04][00][CS]
                    │   │
                    │   └─ 错误码: 0x04 (指纹库已满)
                    └───── 状态: 失败

ESP32 处理：
1. 日志(WARN): "指纹操作失败，错误码=0x04"
2. 上报服务器:
   {
     "type": "user_mgmt_result",
     "category": "finger",
     "command": "add",
     "result": false,
     "val": 4,
     "msg": "Failed"
   }
```

#### 4.1.4 数量查询

```
STM32: [AA][03][11][05][08][00][CS]
                    │   │
                    │   └─ 总数: 8
                    └───── 状态: 数量响应

ESP32 处理：
1. 日志: "指纹数量：8"
2. 上报服务器:
   {
     "type": "user_mgmt_result",
     "category": "finger",
     "command": "query",
     "result": true,
     "val": 8,
     "msg": "Success"
   }
```

---

### 4.2 NFC_RESP (0x21) - NFC 管理反馈

**帧格式**：`[AA][03][21][状态][参数1][参数2][CS]`

状态码与指纹相同，处理逻辑类似：

```
STM32: [AA][03][21][03][02][00][CS]  (NFC 录入成功，ID=2)

ESP32 处理：
1. 日志: "NFC 录入成功，ID=2"
2. 上报服务器:
   {
     "type": "user_mgmt_result",
     "category": "nfc",
     "command": "add",
     "result": true,
     "val": 2,
     "msg": "Success"
   }
```

---

## 5. 人脸识别完整流程

### 5.1 触发流程

```
┌─────────┐         ┌─────────┐         ┌─────────┐
│  STM32  │         │  ESP32  │         │ Server  │
└────┬────┘         └────┬────┘         └────┬────┘
     │                   │                   │
     │ RPT_EVENT(门铃)   │                   │
     │──────────────────►│                   │
     │                   │                   │
     │                   │ 检查状态/内存     │
     │                   │ 拍照+JPEG编码     │
     │                   │                   │
     │                   │ BinaryProtocol2   │
     │                   │ (type=2, JPEG)    │
     │                   │──────────────────►│
     │                   │                   │
     │                   │ event_report      │
     │                   │ {"event":"bell"}  │
     │                   │──────────────────►│
     │                   │                   │
     │                   │                   │ AI识别
     │                   │                   │
     │                   │ face_result       │
     │                   │◄──────────────────│
     │                   │                   │
     │                   │ ack               │
     │                   │──────────────────►│
     │                   │                   │
     │ CMD_LOCK(开锁)    │                   │
     │◄──────────────────│ (若授权通过)      │
     │                   │                   │
```

### 5.2 TriggerFaceRecognition() 详细步骤

| 步骤 | 操作 | 失败处理 |
|------|------|----------|
| 1 | 检查 `face_recognition_in_progress_` | 忽略本次触发 |
| 2 | 检查设备状态（Idle/Listening） | 忽略触发 |
| 3 | 设置 `face_recognition_in_progress_ = true` | - |
| 4 | 检查 PSRAM ≥ 100KB | 拒绝，清除标志 |
| 5 | 获取摄像头实例 | 中止，清除标志 |
| 6 | 确保音频通道已打开 | 打开通道 |
| 7 | 调用 `camera->Capture()` | 中止，清除标志 |
| 8 | JPEG 编码 (质量=80) | 中止，清除标志 |
| 9 | 调用 `protocol_->SendFaceRecognition()` | 释放内存，清除标志 |
| 10 | 释放 JPEG 内存 | - |
| 11 | 清除 `face_recognition_in_progress_` | - |


---

## 6. 服务器下发消息处理

ESP32 通过 `HandleSmartLockJsonMessage()` 处理服务器下发的智能门锁相关消息。

### 6.1 face_result - 人脸识别结果

**服务器下发**：
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

**ESP32 处理**：
```
1. 发送 ACK: {"type":"ack","msg_id":"face_001","code":0,"msg":"OK"}
2. 调用 HandleFaceRecognitionResult()
3. 解析 result 和 access.granted
4. 若 result="known" 且 granted=true:
   - 调用 lock_control_->SendUnlock()
   - 发送 UART: [AA][02][10][01][00][00][CS]
```

---

### 6.2 lock_control - 锁控命令

#### 6.2.1 开锁命令

**服务器下发**：
```json
{
  "type": "lock_control",
  "msg_id": "cmd_1001",
  "command": "unlock",
  "duration": 5
}
```

**ESP32 处理**：
```
1. 解析 command="unlock", duration=5
2. 调用 lock_control_->SendUnlock(5)
3. 发送 UART: [AA][02][10][01][05][00][CS]
4. 发送 ACK: {"type":"ack","msg_id":"cmd_1001","code":0,"msg":"OK"}
```

#### 6.2.2 关锁命令

**服务器下发**：
```json
{
  "type": "lock_control",
  "msg_id": "cmd_1002",
  "command": "lock"
}
```

**ESP32 处理**：
```
1. 调用 lock_control_->SendLockDoor()
2. 发送 UART: [AA][02][10][02][00][00][CS]
3. 发送 ACK
```

#### 6.2.3 临时密码

**服务器下发**：
```json
{
  "type": "lock_control",
  "msg_id": "cmd_1003",
  "command": "temp_code",
  "code": "123456",
  "expires": 3600
}
```

**ESP32 处理**：
```
1. 解析 code="123456" → 123456, expires=3600
2. 调用 lock_control_->SetTempPassword(123456, 3600)
3. 发送 UART 第1包: [AA][03][32][01][E2][40][CS] (密码)
4. 延时 50ms
5. 发送 UART 第2包: [AA][03][33][00][0E][10][CS] (有效期 3600秒)
6. 发送 ACK
```

---

### 6.3 dev_control - 硬件外设控制

#### 6.3.1 蜂鸣器控制

**服务器下发**：
```json
{
  "type": "dev_control",
  "msg_id": "cmd_2001",
  "target": "beep",
  "count": 3,
  "mode": "alarm"
}
```

**ESP32 处理**：
```
1. 解析 target="beep", count=3, mode="alarm"
2. 映射 mode: "short"→BEEP_SHORT, "long"→BEEP_LONG, "alarm"→BEEP_ALARM
3. 调用 lock_control_->SendBeep(3, BEEP_ALARM)
4. 发送 UART: [AA][02][12][03][03][00][CS]
5. 发送 ACK
```

#### 6.3.2 OLED 显示控制

**服务器下发**：
```json
{
  "type": "dev_control",
  "msg_id": "cmd_2002",
  "target": "oled",
  "icon": 3
}
```

**ESP32 处理**：
```
1. 解析 target="oled", icon=3
2. 调用 lock_control_->SendOledIcon(ICON_RECOGNIZING)
3. 发送 UART: [AA][02][11][03][00][00][CS]
4. 发送 ACK
```

**图标映射**：

| icon | 枚举 | 说明 |
|------|------|------|
| 0 | ICON_CLEAR | 清屏/待机 |
| 1 | ICON_WIFI_OK | WiFi 已连接 |
| 2 | ICON_CLOUD_OK | 云端已连接 |
| 3 | ICON_RECOGNIZING | 识别中 |
| 4 | ICON_SUCCESS | 识别成功 |
| 5 | ICON_FAILED | 识别失败 |

#### 6.3.3 补光灯控制

**服务器下发**：
```json
{
  "type": "dev_control",
  "msg_id": "cmd_2003",
  "target": "light",
  "action": "on"
}
```

**ESP32 处理**：
```
1. 解析 target="light", action="on"
2. 映射 action: "on"→SendLightOn(), "off"→SendLightOff(), "auto"→SendLightAuto()
3. 调用 lock_control_->SendLightOn()
4. 发送 UART: [AA][02][14][01][00][00][CS]
5. 发送 ACK
```

---

### 6.4 user_mgmt - 用户管理

#### 6.4.1 指纹录入

**服务器下发**：
```json
{
  "type": "user_mgmt",
  "msg_id": "cmd_4001",
  "category": "finger",
  "command": "add",
  "user_id": 0
}
```

**ESP32 处理**：
```
1. 解析 category="finger", command="add", user_id=0
2. 调用 lock_control_->FingerprintEnroll(0)
3. 发送 UART: [AA][03][10][01][00][00][CS]
4. 发送 ACK
5. 等待 STM32 反馈 FP_RESP，再上报 user_mgmt_result
```

#### 6.4.2 指纹删除

**服务器下发**：
```json
{
  "type": "user_mgmt",
  "msg_id": "cmd_4002",
  "category": "finger",
  "command": "del",
  "user_id": 5
}
```

**ESP32 处理**：
```
1. 调用 lock_control_->FingerprintDelete(5)
2. 发送 UART: [AA][03][10][02][05][00][CS]
3. 发送 ACK
```

#### 6.4.3 指纹清空

**服务器下发**：
```json
{
  "type": "user_mgmt",
  "msg_id": "cmd_4003",
  "category": "finger",
  "command": "clear"
}
```

**ESP32 处理**：
```
1. 调用 lock_control_->FingerprintClear()
2. 发送 UART: [AA][03][10][03][00][00][CS]
3. 发送 ACK
```

#### 6.4.4 指纹数量查询

**服务器下发**：
```json
{
  "type": "user_mgmt",
  "msg_id": "cmd_4004",
  "category": "finger",
  "command": "query"
}
```

**ESP32 处理**：
```
1. 调用 lock_control_->FingerprintQueryCount()
2. 发送 UART: [AA][03][10][04][00][00][CS]
3. 发送 ACK
4. 等待 STM32 反馈 FP_RESP(0x05)，再上报 user_mgmt_result
```

#### 6.4.5 NFC 管理

NFC 管理命令与指纹类似，category="nfc"：

| command | ESP32 调用 | UART TYPE |
|---------|------------|-----------|
| add | NfcEnroll() | 0x20 |
| del | NfcDelete(id) | 0x20 |
| clear | NfcClear() | 0x20 |
| query | NfcQueryCount() | 0x20 |

#### 6.4.6 密码管理

**设置密码**：
```json
{
  "type": "user_mgmt",
  "msg_id": "cmd_4010",
  "category": "password",
  "command": "set",
  "payload": "654321"
}
```

**ESP32 处理**：
```
1. 解析 payload="654321" → 654321
2. 调用 lock_control_->SetPassword(654321)
3. 编码: 654321 = 0x09FBF1 → [09][FB][F1]
4. 发送 UART: [AA][03][30][09][FB][F1][CS]
5. 发送 ACK
```

**查询密码**：
```json
{
  "type": "user_mgmt",
  "msg_id": "cmd_4011",
  "category": "password",
  "command": "query"
}
```

**ESP32 处理**：
```
1. 调用 lock_control_->QueryPassword()
2. 发送 UART: [AA][03][31][00][00][00][CS]
3. 发送 ACK
4. 等待 STM32 反馈 RPT_PWD
```

---

### 6.5 heartbeat_ack - 心跳响应

**服务器下发**：
```json
{
  "type": "heartbeat_ack",
  "ts": 1702234567891,
  "server_time": 1702234567891
}
```

**ESP32 处理**：
```
1. 日志(DEBUG): "收到心跳响应"
2. 无其他操作
```


---

## 7. 消息处理汇总表

### 7.1 STM32 → ESP32 消息

| CAT | TYPE | 名称 | ESP32 本地处理 | 上报服务器 |
|-----|------|------|----------------|------------|
| 0x01 | 0xA0 | RPT_EVENT | 根据事件ID执行动作 | event_report |
| 0x01 | 0xA1 | RPT_UNLOCK | 日志记录 | log_report |
| 0x01 | 0xB0 | RPT_ENV | 保存环境数据 | status_report (变化时) |
| 0x01 | 0xB1 | RPT_STATE | 保存状态数据 | status_report |
| 0x01 | 0xC0 | RPT_PWD | 日志记录 | 不上报 |
| 0x00 | 0x01 | ACK_OK | 日志记录 | 不上报 |
| 0x00 | 0x00 | ACK_ERR | 日志记录 | 不上报 |
| 0x00 | 0xF1 | SYS_PONG | 日志记录 | 不上报 |
| 0x03 | 0x11 | FP_RESP | 日志记录 | user_mgmt_result (最终状态) |
| 0x03 | 0x21 | NFC_RESP | 日志记录 | user_mgmt_result (最终状态) |

### 7.2 Server → ESP32 → STM32 消息

| Server type | ESP32 处理 | STM32 UART |
|-------------|------------|------------|
| face_result | HandleFaceRecognitionResult() | CMD_LOCK (若授权) |
| lock_control (unlock) | SendUnlock() | [AA][02][10][01]... |
| lock_control (lock) | SendLockDoor() | [AA][02][10][02]... |
| lock_control (temp_code) | SetTempPassword() | [AA][03][32]... + [AA][03][33]... |
| dev_control (beep) | SendBeep() | [AA][02][12]... |
| dev_control (oled) | SendOledIcon() | [AA][02][11]... |
| dev_control (light) | SendLight() | [AA][02][14]... |
| user_mgmt (finger) | FingerprintXxx() | [AA][03][10]... |
| user_mgmt (nfc) | NfcXxx() | [AA][03][20]... |
| user_mgmt (password) | SetPassword()/QueryPassword() | [AA][03][30/31]... |

---

## 8. 错误码映射

### 8.1 STM32 ACK 错误码 → ESP32 日志

| STM32 错误码 | 宏定义 | ESP32 日志 |
|--------------|--------|------------|
| 0x01 | ERR_BUSY | "设备忙" |
| 0x02 | ERR_UNSUPPORT | "不支持的命令" |
| 0x03 | ERR_PARAM | "参数错误" |
| 0x04 | ERR_FP_FULL | "指纹库已满" |
| 0x05 | ERR_NFC_FULL | "NFC 库已满" |
| 0x06 | ERR_HARDWARE | "硬件错误" |
| 0xFF | ERR_TIMEOUT | "操作超时" |

### 8.2 ESP32 ACK 错误码 → Server

| ESP32 code | 含义 | 触发条件 |
|------------|------|----------|
| 0 | 成功 | 正常处理 |
| 2 | 参数错误 | 未知 command |
| 3 | 硬件故障 | lock_control_ 为空 |
| 7 | 不支持 | 未知 target |

---

## 9. 注意事项

### 9.1 线程安全

- STM32 消息在 UART 接收任务中解析
- 通过 `Schedule()` 调度到主循环处理，避免阻塞 UART 任务
- 服务器消息在 WebSocket 回调中解析，同样通过 `Schedule()` 调度

### 9.2 监控模式互斥

- 监控模式下 (`IsMonitorMode() == true`)，锁控事件被忽略
- 避免人脸识别与视频流冲突

### 9.3 人脸识别防重入

- 使用 `face_recognition_in_progress_` 标志防止重复触发
- 门铃和 PIR 事件可能短时间内连续触发

### 9.4 内存管理

- JPEG 数据使用 PSRAM 分配 (`heap_caps_malloc`)
- 发送完成后必须释放 (`heap_caps_free`)
- 触发前检查可用内存 ≥ 100KB

---

## 10. 版本兼容性说明

### 10.1 当前实现 (v2.1 协议)

- 空值使用 0x00
- RPT_UNLOCK 同时承担开锁和开门日志
- 无 EVT_LOCK_STATUS 事件

### 10.2 待升级 (v2.6 协议)

需要修改的内容：

| 项目 | 当前 | 目标 |
|------|------|------|
| 空值 | 0x00 | 0xFF |
| RPT_UNLOCK D1 | ID/0x00 | 用户ID (1-40/0xFF) |
| RPT_DOOR_OPENED | 无 | 新增 TYPE=0xA2 |
| EVT_LOCK_STATUS | 无 | 新增事件ID=0x06 |
| 指纹反馈 0x06/0x07 | 无 | 新增状态码 |

---

**文档维护者**：毕业设计项目组  
**最后更新**：2026-01-13

