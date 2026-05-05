# ESP32 串口消息处理分析

本文档详细分析 ESP32 通过 UART 串口接收 STM32 消息后的回应和执行内容。

## 协议概述

- **通信方式**：UART 串口，9600 波特率
- **帧格式**：7 字节固定长度
- **帧结构**：`[帧头 0xAA][类别 CAT][类型 TYPE][D0][D1][D2][校验和]`
- **校验和**：`(CAT + TYPE + D0 + D1 + D2) & 0xFF`

## 消息类别 (CAT)

| CAT 值 | 类别名称 | 方向 | 说明 |
|--------|----------|------|------|
| 0x00 | SYS | 双向 | 系统握手（ACK、心跳） |
| 0x01 | RPT | STM32 → ESP32 | 状态上报 |
| 0x02 | CMD | ESP32 → STM32 | 控制命令 |
| 0x03 | USER | 双向 | 用户管理 |

---

## 一、上报消息处理 (CAT = 0x01, RPT)

### 1.1 事件上报 (TYPE = 0xA0, RPT_EVENT)

**数据格式**：`D0=事件ID, D1=参数, D2=保留`

#### 1.1.1 门铃按下 (EVT_DOORBELL, D0=0x01)

**ESP32 处理流程**：
1. 记录日志：`"门铃按下 - 触发人脸识别"`
2. 调用 `TriggerFaceRecognition()` 触发人脸识别流程：
   - 检查是否已在进行人脸识别（防止重复触发）
   - 检查设备状态（仅空闲/聆听状态允许）
   - 检查 PSRAM 可用内存（最小 100KB）
   - 检查摄像头是否可用
   - 确保 WebSocket 音频通道已打开
   - 调用摄像头拍照
   - JPEG 编码（质量 80%）
   - 通过 WebSocket 发送图像到服务器
3. 上报事件到服务器：`protocol_->SendEventReport("bell", param)`

#### 1.1.2 PIR 人体检测 (EVT_PIR, D0=0x02)

**ESP32 处理流程**：
1. 记录日志：`"PIR 检测到人体 (持续 %d 秒) - 触发人脸识别"`，D1 为持续秒数
2. 调用 `TriggerFaceRecognition()` 触发人脸识别（流程同上）
3. 上报事件到服务器：`protocol_->SendEventReport("pir_trigger", param)`

#### 1.1.3 撬锁报警 (EVT_TAMPER, D0=0x03)

**ESP32 处理流程**：
1. 记录警告日志：`"撬锁报警 (级别 %d)"`，D1 为报警级别
2. **不执行本地报警**（v2.7 协议升级后，STM32 已负责蜂鸣器报警）
3. 上报事件到服务器：`protocol_->SendEventReport("tamper", param)`

#### 1.1.4 门未关超时 (EVT_DOOR_OPEN, D0=0x04)

**ESP32 处理流程**：
1. 记录警告日志：`"门未关超时 (%d 分钟)"`，D1 为超时分钟数
2. 播放警告音效：`audio_service_.PlaySound(Lang::Sounds::OGG_EXCLAMATION)`
3. **不显示警告弹窗**（v2.7 协议升级后，屏幕仅显示摄像头画面）
4. 上报事件到服务器：`protocol_->SendEventReport("door_open", param)`

#### 1.1.5 低电量警告 (EVT_LOW_BATTERY, D0=0x05)

**ESP32 处理流程**：
1. 记录警告日志：`"低电量警告: %d%%"`，D1 为电量百分比
2. **不显示警告弹窗**（v2.7 协议升级后，仅保留日志和服务器上报）
3. 上报事件到服务器：`protocol_->SendEventReport("low_battery", param)`

#### 1.1.6 关门/上锁状态 (EVT_LOCK_STATUS, D0=0x06) [v2.7+]

**数据格式**：`D1=状态码`

| D1 值 | 状态 | ESP32 处理 |
|-------|------|------------|
| 0x00 | 门关闭 | 记录日志，上报 `"door_closed"` |
| 0x01 | 上锁成功 | 记录日志，上报 `"lock_success"` |
| 0x02 |   | 记录警告日志，上报 `"bolt_alarm"` |

---

### 1.2 开锁日志 (TYPE = 0xA1, RPT_UNLOCK)

**数据格式**：`D0=开锁方式, D1=用户ID, D2=结果`

**ESP32 处理流程**：
1. 记录日志：`"开锁日志: 方式=%d, ID=%d, 结果=%d"`
2. 转换开锁方式为字符串（见下表）
3. 上报到服务器：`protocol_->SendLogReport(method_str, id, success, fail_count)`

**开锁方式映射**：

| D0 值 | 方式 | 服务器字符串 |
|-------|------|--------------|
| 0x01 | 指纹 | `"finger"` |
| 0x02 | NFC | `"nfc"` |
| 0x03 | 密码 | `"pwd"` |
| 0x04 | 远程 | `"remote"` |
| 0x05 | 钥匙 | `"key"` |
| 0x06 | 临时密码 | `"temp_pwd"` |
| 0x07 | 人脸 | `"face"` |

---

### 1.3 开门日志 (TYPE = 0xA2, RPT_DOOR_OPENED) [v2.7+]

**数据格式**：`D0=开锁方式, D1=开门来源`

**开门来源**：

| D1 值 | 来源 | 服务器字符串 |
|-------|------|--------------|
| 0x00 | 室外开门 | `"outside"` |
| 0x01 | 室内开门 | `"inside"` |
| 0xFF | 未知 | `"unknown"` |

**ESP32 处理流程**：
1. 记录日志：`"开门日志: 方式=%s, 来源=%s"`
2. 上报到服务器：`protocol_->SendLogReport(method_str, source, true, 0)`

---

### 1.4 环境数据上报 (TYPE = 0xB0, RPT_ENV)

**数据格式**：`D0=电量百分比, D1-D2=光照值(大端)`

**ESP32 处理流程**：
1. 解析数据：
   - 电量：`battery = D0`
   - 光照：`lux = (D1 << 8) | D2`
2. 记录日志：`"环境数据: 电量=%d%%, 光照=%d Lux"`
3. 检查数据是否变化
4. 保存到成员变量：`last_battery_`, `last_lux_`
5. 如果数据变化，上报到服务器：`protocol_->SendStatusReport(...)`
6. **两级确认机制**：如果是查询命令的响应，发送 ack 到服务器

---

### 1.5 状态上报 (TYPE = 0xB1, RPT_STATE)

**数据格式**：`D0=锁状态, D1=灯状态`

| 字段 | 值 | 含义 |
|------|-----|------|
| D0 | 0x01 | 锁已开 |
| D0 | 其他 | 锁已关 |
| D1 | 0x01 | 灯亮 |
| D1 | 其他 | 灯灭 |

**ESP32 处理流程**：
1. 解析状态
2. 记录日志：`"状态: 锁=%s, 灯=%s"`
3. 保存到成员变量：`last_lock_state_`, `last_light_state_`
4. 上报到服务器：`protocol_->SendStatusReport(...)`
5. **两级确认机制**：如果是查询命令的响应，发送 ack 到服务器

---

### 1.6 密码查询响应 (TYPE = 0xC0, RPT_PWD)

**数据格式**：`D0-D2=密码(Hex大端格式)`

**ESP32 处理流程**：
1. 解码密码：`pwd = LockProtocol::DecodePasswordHex(data)`
2. 记录日志：`"当前密码: %06lu"`
3. 上报到服务器：`ws_protocol->SendPasswordReport(pwd)`
4. **两级确认机制**：发送 ack 到服务器

---

## 二、系统消息处理 (CAT = 0x00, SYS)

### 2.1 成功应答 (TYPE = 0x01, ACK_OK)

**数据格式**：`D0=原命令TYPE, D1=0xFF, D2=0xFF`

**ESP32 处理流程**：
1. 记录调试日志：`"收到 ACK_OK，原指令 TYPE=0x%02X"`
2. 查找 `pending_commands_` 中的待处理命令
3. 更新命令状态：`stm32_ack_received = true`
4. **根据命令类型决定后续处理**：
   - **即时命令 (IMMEDIATE)**：立即发送 ack 到服务器，清理待处理命令
   - **查询/长流程命令**：继续等待数据帧/最终结果

### 2.2 错误应答 (TYPE = 0x00, ACK_ERR)

**数据格式**：`D0=原命令TYPE, D1=错误码, D2=0xFF`

**错误码定义**：

| 错误码 | 含义 |
|--------|------|
| 0x01 | 设备忙 |
| 0x02 | 不支持的命令 |
| 0x03 | 参数错误 |
| 0x04 | 指纹库已满 |
| 0x05 | NFC 库已满 |
| 0x06 | 硬件错误 |
| 0xFF | 操作超时 |

**ESP32 处理流程**：
1. 记录警告日志：`"收到 ACK_ERR，原指令 TYPE=0x%02X，错误码=0x%02X"`
2. 查找待处理命令
3. 映射错误码为统一错误码
4. 发送 ack 到服务器（包含错误信息）
5. 清理待处理命令

### 2.3 心跳响应 (TYPE = 0xF1, SYS_PONG)

**ESP32 处理流程**：
1. 记录调试日志：`"收到心跳响应"`
2. 无其他处理

---

## 三、用户管理反馈处理 (CAT = 0x03, USER)

### 3.1 指纹反馈 (TYPE = 0x11, FP_RESP)

**数据格式**：`D0=状态码, D1=参数, D2=保留`

| D0 状态码 | 含义 | ESP32 处理 |
|-----------|------|------------|
| 0x01 | 请按手指 | 记录日志，D1=第几次，**不上报** |
| 0x02 | 请抬起手指 | 记录日志，**不上报** |
| 0x03 | 录入成功 | 记录日志，D1=新ID，上报结果，发送 ack |
| 0x04 | 录入失败 | 记录警告，D1=错误码，上报结果，发送 ack |
| 0x05 | 数量查询响应 | 记录日志，D1=总数，上报结果，发送 ack |
| 0x06 | 已存在 [v2.7+] | 记录日志，D1=已有ID，上报 `"AlreadyExists"`，发送 ack |
| 0x07 | ID被占用 [v2.7+] | 记录日志，D1=新分配ID，上报 `"IdOccupied"`，发送 ack |

**上报格式**：`protocol_->SendUserMgmtResult(category, command, result, val, result_msg)`

### 3.2 NFC 反馈 (TYPE = 0x21, NFC_RESP)

**数据格式**：`D0=状态码, D1=参数, D2=保留`

| D0 状态码 | 含义 | ESP32 处理 |
|-----------|------|------------|
| 0x03 | 录入成功 | 记录日志，D1=新ID，上报结果，发送 ack |
| 0x04 | 录入失败 | 记录警告，D1=错误码，上报结果，发送 ack |
| 0x05 | 数量查询响应 | 记录日志，D1=总数，上报结果，发送 ack |
| 0x06 | 已存在 [v2.7+] | 记录日志，D1=已有ID，上报 `"AlreadyExists"`，发送 ack |
| 0x07 | ID被占用 [v2.7+] | 记录日志，D1=新分配ID，上报 `"IdOccupied"`，发送 ack |

---

## 四、服务器下发命令处理

当服务器通过 WebSocket 下发命令时，ESP32 会转发给 STM32 执行，并等待 STM32 的 ACK 响应。

### 4.1 锁控命令 (type = "lock_control")

| 命令 | ESP32 调用 | STM32 UART 命令 |
|------|------------|-----------------|
| unlock | `SendUnlock(duration)` | CAT=0x02, TYPE=0x10, D0=0x01 |
| lock | `SendLockDoor()` | CAT=0x02, TYPE=0x10, D0=0x02 |
| temp_code | `SetTempPassword(pwd, expires)` | 两包：TYPE=0x32 + TYPE=0x33 |

### 4.2 设备控制命令 (type = "dev_control")

| target | ESP32 调用 | STM32 UART 命令 |
|--------|------------|-----------------|
| beep | `SendBeep(count, freq)` | CAT=0x02, TYPE=0x12 |
| oled | `SendOledIcon(icon)` | CAT=0x02, TYPE=0x11 |
| light | `SendLight(mode)` | CAT=0x02, TYPE=0x14 |

### 4.3 用户管理命令 (type = "user_mgmt")

| category | command | ESP32 调用 | STM32 UART 命令 |
|----------|---------|------------|-----------------|
| finger | add | `FingerprintEnroll(uid)` | CAT=0x03, TYPE=0x10, D0=0x01 |
| finger | del | `FingerprintDelete(uid)` | CAT=0x03, TYPE=0x10, D0=0x02 |
| finger | clear | `FingerprintClear()` | CAT=0x03, TYPE=0x10, D0=0x03 |
| finger | query | `FingerprintQueryCount()` | CAT=0x03, TYPE=0x10, D0=0x04 |
| nfc | add | `NfcEnroll()` | CAT=0x03, TYPE=0x20, D0=0x01 |
| nfc | del | `NfcDelete(uid)` | CAT=0x03, TYPE=0x20, D0=0x02 |
| nfc | clear | `NfcClear()` | CAT=0x03, TYPE=0x20, D0=0x03 |
| nfc | query | `NfcQueryCount()` | CAT=0x03, TYPE=0x20, D0=0x04 |
| password | set | `SetPassword(pwd)` | CAT=0x03, TYPE=0x30 |
| password | query | `QueryPassword()` | CAT=0x03, TYPE=0x31 |

---

## 五、两级确认机制

ESP32 实现了两级确认机制，确保命令执行的可靠性：

### 5.1 第一级确认 (esp32_ack)

- **时机**：ESP32 收到服务器命令后立即发送
- **含义**：ESP32 已收到命令，正在转发给 STM32
- **方法**：`ws_protocol->SendEsp32Ack(seq_id, 0, "received")`

### 5.2 第二级确认 (ack)

- **时机**：根据命令类型不同
  - **即时命令**：收到 STM32 ACK_OK 后发送
  - **查询命令**：收到 STM32 数据帧后发送
  - **长流程命令**：收到最终结果（成功/失败/已存在/ID占用）后发送
- **含义**：命令已执行完成
- **方法**：`protocol_->SendAck(seq_id, code, msg)`

### 5.3 命令类型分类

| 类型 | 命令示例 | 确认时机 |
|------|----------|----------|
| IMMEDIATE | 开锁、关锁、蜂鸣器、OLED、补光灯、删除、清空、设置密码 | 收到 STM32 ACK |
| QUERY | 查询传感器、状态、指纹数量、NFC数量、密码 | 收到数据帧 |
| LONG_FLOW | 指纹录入、NFC录入 | 收到最终结果 |

---

## 六、人脸识别结果处理

当服务器返回人脸识别结果时：

**JSON 格式**：
```json
{
  "type": "face_result",
  "result": "known" | "unknown" | "no_face",
  "access": { "granted": true/false }
}
```

**ESP32 处理流程**：
1. 解析 `result` 字段
2. 解析 `access.granted` 字段
3. 如果 `result == "known"` 且 `access.granted == true`：
   - 调用 `lock_control_->SendUnlock()` 发送开锁命令
4. 否则记录日志：`"授权拒绝或未知人员"`

---

## 七、监控模式互斥

当 ESP32 处于监控模式（视频对讲）时：
- `HandleLockEvent()` 会检查 `IsMonitorMode()`
- 如果为 true，记录警告日志并忽略所有锁控事件
- 避免监控模式与锁控事件处理冲突

---

## 八、总结

ESP32 作为智能猫眼系统的核心控制器，主要承担以下职责：

1. **事件响应**：接收 STM32 上报的门铃、PIR、报警等事件，触发人脸识别或上报服务器
2. **命令转发**：将服务器下发的控制命令转发给 STM32 执行
3. **状态同步**：收集 STM32 上报的环境数据和状态，同步到服务器
4. **确认机制**：实现两级确认机制，确保命令执行的可靠性
5. **人脸识别**：拍照、编码、发送图像，并根据识别结果控制开锁
