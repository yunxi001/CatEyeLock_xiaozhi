# 锁控系统

**状态**: ✅ 已完成  
**最后更新**: 2025-12-12  
**代码位置**: `main/lock_control/`

---

## 功能概述

锁控系统负责 ESP32 与 STM32 锁控 MCU 之间的 UART 通信，实现远程开锁、用户管理、状态查询等功能。

### 核心特性

- ✅ UART 通信（9600 波特率，7字节协议）
- ✅ 远程开锁/关锁
- ✅ 用户管理（指纹/NFC/密码）
- ✅ 状态查询（传感器、锁状态）
- ✅ 事件上报（门铃、PIR、撬锁等）
- ✅ 硬件控制（蜂鸣器、OLED、补光灯）

---

## UART 协议

### 协议格式（7字节固定长度）

```
[0xAA][CAT][TYPE][DATA0][DATA1][DATA2][CHECKSUM]
  │     │    │      │      │      │       │
  帧头  类别  类型   ────数据(3字节)────   校验和

校验和 = (CAT + TYPE + DATA0 + DATA1 + DATA2) & 0xFF
```

### 消息类别

| CAT  | 名称  | 方向          | 说明                         |
| ---- | ----- | ------------- | ---------------------------- |
| 0x00 | SYS   | 双向          | 系统消息（ACK、PING/PONG）   |
| 0x01 | RPT   | STM32 → ESP32 | 上报消息（事件、日志、状态） |
| 0x02 | CMD   | ESP32 → STM32 | 控制命令（开锁、蜂鸣器等）   |
| 0x03 | USER  | 双向          | 用户管理（指纹/NFC/密码）    |
| 0x04 | QUERY | ESP32 → STM32 | 查询命令（传感器、状态）     |

---

## 主要功能

### 1. 远程开锁

**命令**: `CMD_LOCK (0x10)`

```cpp
// 开锁
lock_control->SendUnlock();

// 关锁
lock_control->SendLock();

// 带持续时间的开锁
lock_control->SendUnlock(300);  // 保持5分钟
```

**协议格式**:

```
[0xAA][0x02][0x10][0x01][0x00][0x00][CHECKSUM]  // 开锁
[0xAA][0x02][0x10][0x02][0x00][0x00][CHECKSUM]  // 关锁
```

### 2. 用户管理

#### 指纹管理

```cpp
// 录入指纹
lock_control->FingerprintEnroll(0);  // 0=自动分配ID

// 删除指纹
lock_control->FingerprintDelete(5);  // 删除ID=5的指纹

// 清空指纹
lock_control->FingerprintClear();

// 查询指纹数量
lock_control->FingerprintQuery();
```

#### NFC 管理

```cpp
// 录入 NFC 卡
lock_control->NfcEnroll(0);

// 删除 NFC 卡
lock_control->NfcDelete(3);

// 清空 NFC 卡
lock_control->NfcClear();

// 查询 NFC 卡数量
lock_control->NfcQuery();
```

#### 密码管理

```cpp
// 设置密码
lock_control->SetPassword(123456);

// 查询密码
lock_control->QueryPassword();
```

### 3. 硬件控制

#### 蜂鸣器

```cpp
// 短滴声
lock_control->SendBeep(1, BeepFreq::SHORT);

// 长鸣
lock_control->SendBeep(3, BeepFreq::LONG);

// 报警声
lock_control->SendBeep(5, BeepFreq::ALARM);
```

#### OLED 显示

```cpp
// 显示图标
lock_control->SendOledIcon(OledIcon::WIFI_CONNECTED);
lock_control->SendOledIcon(OledIcon::CLOUD_CONNECTED);
lock_control->SendOledIcon(OledIcon::RECOGNIZING);
lock_control->SendOledIcon(OledIcon::SUCCESS);
lock_control->SendOledIcon(OledIcon::FAILED);
```

#### 补光灯

```cpp
// 开灯
lock_control->SendLight(LightMode::ON);

// 关灯
lock_control->SendLight(LightMode::OFF);

// 自动模式（光照传感器控制）
lock_control->SendLight(LightMode::AUTO);
```

### 4. 状态查询

```cpp
// 查询传感器数据（电量、光照）
lock_control->QuerySensors();

// 查询设备状态（锁状态、补光灯状态）
lock_control->QueryStatus();
```

---

## 事件处理

### 事件类型

| 事件       | 代码                     | 说明                  |
| ---------- | ------------------------ | --------------------- |
| 门铃按下   | `EVT_DOORBELL (0x01)`    | 触发人脸识别          |
| PIR 检测   | `EVT_PIR (0x02)`         | 触发人脸识别          |
| 撬锁报警   | `EVT_TAMPER (0x03)`      | 激活警报 + 上报服务器 |
| 门未关超时 | `EVT_DOOR_OPEN (0x04)`   | 上报服务器            |
| 低电量     | `EVT_LOW_BATTERY (0x05)` | 上报服务器            |
| 锁状态变化 | `EVT_LOCK_STATUS (0x06)` | 门关闭/上锁成功       |

### 事件回调

```cpp
lock_control->SetEventCallback([](const LockMessage& msg) {
    switch (msg.category) {
        case MsgCategory::CAT_RPT:
            // 处理上报消息
            HandleReportMessage(msg);
            break;

        case MsgCategory::CAT_SYS:
            // 处理系统消息
            HandleSystemMessage(msg);
            break;

        case MsgCategory::CAT_USER:
            // 处理用户管理反馈
            HandleUserMessage(msg);
            break;
    }
});
```

---

## 协议详细说明

完整的协议规范请参考：

- [ESP32与STM32锁控协议 v2.0](../protocols/esp32-stm32-v2.0.md)

---

## 配置选项

### UART 配置

**位置**: `main/boards/bread-compact-wifi-s3cam/config.h`

```c
#define LOCK_UART_PORT      UART_NUM_1
#define LOCK_UART_TX_PIN    GPIO_NUM_3
#define LOCK_UART_RX_PIN    GPIO_NUM_14
#define LOCK_UART_BAUD_RATE 9600
```

---

## 错误处理

| 错误码 | 说明         | 处理方式           |
| ------ | ------------ | ------------------ |
| 0x01   | 设备忙       | 稍后重试           |
| 0x02   | 不支持的命令 | 检查协议版本       |
| 0x03   | 参数错误     | 检查参数范围       |
| 0x04   | 指纹库已满   | 提示用户删除旧指纹 |
| 0x05   | NFC 库已满   | 提示用户删除旧卡片 |
| 0x06   | 硬件故障     | 提示用户检查硬件   |
| 0xFF   | 超时         | 重试或提示用户     |

---

## 调试技巧

### 1. 查看 UART 通信

```bash
# 监控日志，过滤锁控相关
idf.py monitor | grep "LockControl"
```

### 2. 手动发送命令

```cpp
// 构建消息
LockMessage msg;
msg.category = MsgCategory::CAT_CMD;
msg.type = static_cast<uint8_t>(CmdType::CMD_LOCK);
msg.data[0] = 0x01;  // 开锁
msg.data[1] = 0x00;
msg.data[2] = 0x00;

// 发送
lock_control->SendMessage(msg);
```

### 3. 查看接收数据

```cpp
// 在 RxLoop() 中添加日志
ESP_LOGI(TAG, "收到消息: CAT=0x%02X TYPE=0x%02X",
    msg.category, msg.type);
```

---

## 相关文档

- [ESP32与STM32锁控协议 v2.0](../protocols/esp32-stm32-v2.0.md)
- [ESP32与服务器通信协议 v5.2](../protocols/esp32-server-v5.2.md)
- [人脸识别功能](face-recognition.md)
- [变更日志](../reference/CHANGELOG.md)

---

**最后更新**: 2025-12-12
