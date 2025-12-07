# ESP32 人脸识别模块实现规划

## 📋 需求概述

### 功能定位
- **不是监控模式**，是在**正常模式**上扩展的功能
- 通过串口与锁控 MCU (STM32C8T6) 通信
- 串口触发人脸识别流程
- 服务器返回 TTS 音频直接播放

### 硬件配置
- **ESP32-S3** (bread-compact-wifi-s3cam 板子)
- **STM32C8T6** (锁控 MCU，性能较低)
- **串口引脚：** TX=GPIO3, RX=GPIO14

---

## 🔌 ESP32 ↔ STM32 串口通信协议设计

### 设计原则
1. **简洁高效** - STM32C8T6 性能有限
2. **固定长度** - 便于解析，减少缓冲区管理
3. **校验机制** - 确保数据完整性
4. **可扩展** - 消息分类 + 消息类型，便于未来扩展

### 硬件配置
- **串口：** UART2
- **波特率：** 9600
- **TX 引脚：** GPIO3
- **RX 引脚：** GPIO14

### 协议格式

```
+--------+--------+--------+--------+--------+--------+--------+--------+--------+
| HEADER | CAT    | TYPE   | DATA0  | DATA1  | DATA2  | DATA3  | DATA4  | CHKSUM |
| 0xAA   | 1 byte | 1 byte | 1 byte | 1 byte | 1 byte | 1 byte | 1 byte | 1 byte |
+--------+--------+--------+--------+--------+--------+--------+--------+--------+
```

**总长度：9 字节（固定）**

### 字段说明

| 字段 | 大小 | 说明 |
|------|------|------|
| HEADER | 1 byte | 固定 0xAA，帧起始标识 |
| CAT | 1 byte | 消息分类（Category） |
| TYPE | 1 byte | 消息类型（在分类内唯一） |
| DATA0-4 | 5 bytes | 数据字段（可存储6位数字密码等） |
| CHKSUM | 1 byte | 校验和 = (CAT + TYPE + DATA0 + DATA1 + DATA2 + DATA3 + DATA4) & 0xFF |

### 消息分类定义 (CAT)

| CAT | 名称 | 说明 | 方向 |
|-----|------|------|------|
| 0x01 | EVENT | 事件上报 | STM32 → ESP32 |
| 0x02 | CONTROL | 控制命令 | ESP32 → STM32 |
| 0x03 | STATUS | 状态消息 | 双向 |
| 0x04 | QUERY | 查询消息 | 双向 |
| 0x0F | ACK | 确认响应 | 双向 |

### 消息类型定义

#### CAT=0x01 事件消息 (STM32 → ESP32)

| TYPE | 名称 | DATA0-4 | 说明 |
|------|------|---------|------|
| 0x01 | DOORBELL_PRESSED | - | 门铃被按下 |
| 0x02 | HUMAN_DETECTED | - | 传感器检测到人体 |
| 0x03 | LOCK_TAMPER | [level, 0, 0, 0, 0] | 锁被暴力破坏 |
| 0x04 | PERSON_LEFT | - | 有人离开 |
| 0x05 | PERSON_ENTERED | - | 有人进门 |
| 0x06 | DOOR_NOT_CLOSED | - | 门未关严实 |
| 0x07 | PASSWORD_ERROR | [count, 0, 0, 0, 0] | 密码错误 |
| 0x08 | LOCK_LOCKED | - | 密码错误过多被锁定 |
| 0x10 | HEARTBEAT | - | 心跳包 |

#### CAT=0x02 控制命令 (ESP32 → STM32)

| TYPE | 名称 | DATA0-4 | 说明 |
|------|------|---------|------|
| 0x01 | UNLOCK | - | 开锁命令 |
| 0x02 | ALARM_ON | [level, 0, 0, 0, 0] | 开启警报 |
| 0x03 | ALARM_OFF | - | 关闭警报 |
| 0x04 | SET_TEMP_CODE | [d0, d1, d2, d3, d4, d5] | 设置临时开锁码（6位数字，d5在高4位） |
| 0x05 | LED_CTRL | [mode, r, g, b, 0] | LED 控制 |

#### CAT=0x03 状态消息 (双向)

| TYPE | 名称 | DATA0-4 | 说明 |
|------|------|---------|------|
| 0x01 | LOCK_STATE | [state, 0, 0, 0, 0] | 锁状态 (0=锁定, 1=解锁) |
| 0x02 | DOOR_STATE | [state, 0, 0, 0, 0] | 门状态 (0=关闭, 1=打开) |
| 0x03 | BATTERY_LEVEL | [level, 0, 0, 0, 0] | 电池电量 (0-100) |
| 0x04 | ALARM_STATE | [state, 0, 0, 0, 0] | 警报状态 |

#### CAT=0x04 查询消息 (双向)

| TYPE | 名称 | DATA0-4 | 说明 |
|------|------|---------|------|
| 0x01 | QUERY_LOCK_STATE | - | 查询锁状态 |
| 0x02 | QUERY_DOOR_STATE | - | 查询门状态 |
| 0x03 | QUERY_BATTERY | - | 查询电池电量 |
| 0x04 | QUERY_ALL | - | 查询所有状态 |

#### CAT=0x0F 确认响应 (双向)

| TYPE | 名称 | DATA0-4 | 说明 |
|------|------|---------|------|
| 0x00 | ACK_OK | [orig_cat, orig_type, 0, 0, 0] | 确认成功 |
| 0x01 | ACK_FAIL | [orig_cat, orig_type, err_code, 0, 0] | 确认失败 |

### 6位临时开锁码编码

由于 DATA 字段只有 5 字节，6位数字密码编码方式：
- DATA0: 第1位数字 (0-9)
- DATA1: 第2位数字 (0-9)
- DATA2: 第3位数字 (0-9)
- DATA3: 第4位数字 (0-9)
- DATA4: 低4位=第5位数字, 高4位=第6位数字

**示例：** 密码 "123456"
```
DATA0 = 0x01 (1)
DATA1 = 0x02 (2)
DATA2 = 0x03 (3)
DATA3 = 0x04 (4)
DATA4 = 0x65 (高4位=6, 低4位=5)
```

### 通信示例

**门铃按下：**
```
STM32 → ESP32: AA 01 01 00 00 00 00 00 02
                │  │  │  └──────────────┴─ DATA0-4 = 0
                │  │  └─ TYPE = DOORBELL_PRESSED
                │  └─ CAT = EVENT
                └─ HEADER

ESP32 → STM32: AA 0F 00 01 01 00 00 00 11
                │  │  │  │  │  └──────┴─ 保留
                │  │  │  │  └─ orig_type = 0x01
                │  │  │  └─ orig_cat = 0x01
                │  │  └─ TYPE = ACK_OK
                │  └─ CAT = ACK
                └─ HEADER
```

**开锁命令：**
```
ESP32 → STM32: AA 02 01 00 00 00 00 00 03
                │  │  │  └──────────────┴─ DATA0-4 = 0
                │  │  └─ TYPE = UNLOCK
                │  └─ CAT = CONTROL
                └─ HEADER

STM32 → ESP32: AA 0F 00 02 01 00 00 00 12
                │  │  │  │  │
                │  │  │  │  └─ orig_type = 0x01
                │  │  │  └─ orig_cat = 0x02
                │  │  └─ TYPE = ACK_OK
                │  └─ CAT = ACK
                └─ HEADER
```

**设置临时开锁码 "123456"：**
```
ESP32 → STM32: AA 02 04 01 02 03 04 65 75
                │  │  │  │  │  │  │  │  └─ CHKSUM
                │  │  │  │  │  │  │  └─ DATA4 = 0x65 (5,6)
                │  │  │  │  │  │  └─ DATA3 = 4
                │  │  │  │  │  └─ DATA2 = 3
                │  │  │  │  └─ DATA1 = 2
                │  │  │  └─ DATA0 = 1
                │  │  └─ TYPE = SET_TEMP_CODE
                │  └─ CAT = CONTROL
                └─ HEADER
```

**查询锁状态：**
```
ESP32 → STM32: AA 04 01 00 00 00 00 00 05
STM32 → ESP32: AA 03 01 01 00 00 00 00 05  (锁状态=解锁)
```

---

## 🏗️ 系统架构

### 模块结构

```
main/
├── lock_control/                    # 新增：锁控模块
│   ├── lock_control.h               # 锁控服务头文件
│   ├── lock_control.cc              # 锁控服务实现
│   ├── lock_protocol.h              # 串口协议定义
│   └── lock_protocol.cc             # 串口协议实现
├── boards/
│   └── bread-compact-wifi-s3cam/
│       ├── compact_wifi_board_s3cam.cc  # 修改：添加串口初始化
│       └── config.h                     # 修改：添加串口引脚定义
└── application.cc                   # 修改：集成锁控服务
```

### 类图

```
┌─────────────────────────────────────────────────────────────┐
│                      Application                             │
│  - lock_control_service_: LockControlService*               │
│  + HandleLockEvent(event)                                   │
│  + TriggerFaceRecognition()                                 │
└─────────────────────────────────────────────────────────────┘
                              │
                              │ uses
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                   LockControlService                         │
│  - uart_port_: uart_port_t                                  │
│  - protocol_: LockProtocol                                  │
│  - event_callback_: EventCallback                           │
│  + Start()                                                  │
│  + Stop()                                                   │
│  + SendUnlock()                                             │
│  + SendAlarm(level)                                         │
│  + SendTempCode(code)                                       │
│  + SetEventCallback(callback)                               │
└─────────────────────────────────────────────────────────────┘
                              │
                              │ uses
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                      LockProtocol                            │
│  + BuildMessage(type, data1, data2, data3) -> vector<uint8>│
│  + ParseMessage(data, len) -> LockMessage                   │
│  + CalculateChecksum(type, data1, data2, data3) -> uint8   │
└─────────────────────────────────────────────────────────────┘
```

### 数据流

```
┌─────────┐    串口     ┌─────────────────┐    回调     ┌─────────────┐
│ STM32   │ ─────────▶ │ LockControlSvc  │ ─────────▶ │ Application │
│ (锁控)  │            │ (解析事件)       │            │ (处理逻辑)   │
└─────────┘            └─────────────────┘            └─────────────┘
                                                            │
                                                            │ 触发人脸识别
                                                            ▼
┌─────────┐    WebSocket  ┌─────────────────┐    JPEG    ┌─────────────┐
│ Server  │ ◀─────────── │ Protocol        │ ◀──────── │ Esp32Camera │
│ (识别)  │              │ (SendVideo)     │            │ (拍照)       │
└─────────┘              └─────────────────┘            └─────────────┘
     │
     │ TTS 音频
     ▼
┌─────────────────┐    播放     ┌─────────────────┐
│ AudioService    │ ◀───────── │ Protocol        │
│ (音频播放)       │            │ (OnIncomingAudio)│
└─────────────────┘            └─────────────────┘
```

---

## 📝 实现步骤

### 阶段 1：串口协议层 (2小时)

#### 1.1 创建协议定义文件

**文件：** `main/lock_control/lock_protocol.h`

```cpp
#pragma once
#include <cstdint>
#include <vector>
#include <array>

// 协议常量
constexpr uint8_t LOCK_PROTOCOL_HEADER = 0xAA;
constexpr size_t LOCK_PROTOCOL_LENGTH = 9;  // 9字节固定长度
constexpr size_t LOCK_PROTOCOL_DATA_LEN = 5; // 5字节数据字段

// 消息分类 (CAT)
enum class MsgCategory : uint8_t {
    EVENT   = 0x01,  // 事件上报 (STM32 → ESP32)
    CONTROL = 0x02,  // 控制命令 (ESP32 → STM32)
    STATUS  = 0x03,  // 状态消息 (双向)
    QUERY   = 0x04,  // 查询消息 (双向)
    ACK     = 0x0F,  // 确认响应 (双向)
};

// 事件类型 (CAT=0x01)
enum class EventType : uint8_t {
    DOORBELL_PRESSED = 0x01,
    HUMAN_DETECTED   = 0x02,
    LOCK_TAMPER      = 0x03,
    PERSON_LEFT      = 0x04,
    PERSON_ENTERED   = 0x05,
    DOOR_NOT_CLOSED  = 0x06,
    PASSWORD_ERROR   = 0x07,
    LOCK_LOCKED      = 0x08,
    HEARTBEAT        = 0x10,
};

// 控制命令类型 (CAT=0x02)
enum class ControlType : uint8_t {
    UNLOCK        = 0x01,
    ALARM_ON      = 0x02,
    ALARM_OFF     = 0x03,
    SET_TEMP_CODE = 0x04,
    LED_CTRL      = 0x05,
};

// 状态类型 (CAT=0x03)
enum class StatusType : uint8_t {
    LOCK_STATE    = 0x01,
    DOOR_STATE    = 0x02,
    BATTERY_LEVEL = 0x03,
    ALARM_STATE   = 0x04,
};

// 查询类型 (CAT=0x04)
enum class QueryType : uint8_t {
    QUERY_LOCK_STATE = 0x01,
    QUERY_DOOR_STATE = 0x02,
    QUERY_BATTERY    = 0x03,
    QUERY_ALL        = 0x04,
};

// ACK 类型 (CAT=0x0F)
enum class AckType : uint8_t {
    ACK_OK   = 0x00,
    ACK_FAIL = 0x01,
};

// 解析后的消息结构
struct LockMessage {
    uint8_t category;                              // 消息分类
    uint8_t type;                                  // 消息类型
    std::array<uint8_t, LOCK_PROTOCOL_DATA_LEN> data; // 数据字段
    bool valid;                                    // 是否有效
    
    // 便捷方法
    MsgCategory GetCategory() const { return static_cast<MsgCategory>(category); }
    bool IsEvent() const { return category == static_cast<uint8_t>(MsgCategory::EVENT); }
    bool IsControl() const { return category == static_cast<uint8_t>(MsgCategory::CONTROL); }
    bool IsStatus() const { return category == static_cast<uint8_t>(MsgCategory::STATUS); }
    bool IsQuery() const { return category == static_cast<uint8_t>(MsgCategory::QUERY); }
    bool IsAck() const { return category == static_cast<uint8_t>(MsgCategory::ACK); }
};

class LockProtocol {
public:
    // 构建消息
    static std::vector<uint8_t> BuildMessage(uint8_t cat, uint8_t type,
                                              const uint8_t* data = nullptr,
                                              size_t data_len = 0);
    
    // 构建消息（使用 array）
    static std::vector<uint8_t> BuildMessage(uint8_t cat, uint8_t type,
                                              const std::array<uint8_t, LOCK_PROTOCOL_DATA_LEN>& data);
    
    // 解析消息
    static LockMessage ParseMessage(const uint8_t* data, size_t len);
    
    // 计算校验和
    static uint8_t CalculateChecksum(uint8_t cat, uint8_t type, const uint8_t* data);
    
    // 构建 ACK 消息
    static std::vector<uint8_t> BuildAck(uint8_t orig_cat, uint8_t orig_type, bool success = true);
    
    // 编码6位密码到5字节数据
    static std::array<uint8_t, LOCK_PROTOCOL_DATA_LEN> EncodePassword(const char* password);
    
    // 解码5字节数据到6位密码
    static std::string DecodePassword(const std::array<uint8_t, LOCK_PROTOCOL_DATA_LEN>& data);
};
```

#### 1.2 实现协议

**文件：** `main/lock_control/lock_protocol.cc`

```cpp
#include "lock_protocol.h"
#include <cstring>

std::vector<uint8_t> LockProtocol::BuildMessage(uint8_t cat, uint8_t type,
                                                 const uint8_t* data, size_t data_len) {
    std::vector<uint8_t> msg(LOCK_PROTOCOL_LENGTH, 0);
    msg[0] = LOCK_PROTOCOL_HEADER;
    msg[1] = cat;
    msg[2] = type;
    
    // 复制数据字段
    if (data && data_len > 0) {
        size_t copy_len = std::min(data_len, LOCK_PROTOCOL_DATA_LEN);
        std::memcpy(&msg[3], data, copy_len);
    }
    
    msg[8] = CalculateChecksum(cat, type, &msg[3]);
    return msg;
}

std::vector<uint8_t> LockProtocol::BuildMessage(uint8_t cat, uint8_t type,
                                                 const std::array<uint8_t, LOCK_PROTOCOL_DATA_LEN>& data) {
    return BuildMessage(cat, type, data.data(), data.size());
}

LockMessage LockProtocol::ParseMessage(const uint8_t* data, size_t len) {
    LockMessage msg = {};
    msg.valid = false;
    msg.data.fill(0);
    
    if (len < LOCK_PROTOCOL_LENGTH) return msg;
    if (data[0] != LOCK_PROTOCOL_HEADER) return msg;
    
    uint8_t checksum = CalculateChecksum(data[1], data[2], &data[3]);
    if (checksum != data[8]) return msg;
    
    msg.category = data[1];
    msg.type = data[2];
    std::memcpy(msg.data.data(), &data[3], LOCK_PROTOCOL_DATA_LEN);
    msg.valid = true;
    return msg;
}

uint8_t LockProtocol::CalculateChecksum(uint8_t cat, uint8_t type, const uint8_t* data) {
    uint8_t sum = cat + type;
    for (size_t i = 0; i < LOCK_PROTOCOL_DATA_LEN; i++) {
        sum += data[i];
    }
    return sum & 0xFF;
}

std::vector<uint8_t> LockProtocol::BuildAck(uint8_t orig_cat, uint8_t orig_type, bool success) {
    std::array<uint8_t, LOCK_PROTOCOL_DATA_LEN> data = {orig_cat, orig_type, 0, 0, 0};
    uint8_t ack_type = success ? static_cast<uint8_t>(AckType::ACK_OK) 
                               : static_cast<uint8_t>(AckType::ACK_FAIL);
    return BuildMessage(static_cast<uint8_t>(MsgCategory::ACK), ack_type, data);
}

std::array<uint8_t, LOCK_PROTOCOL_DATA_LEN> LockProtocol::EncodePassword(const char* password) {
    std::array<uint8_t, LOCK_PROTOCOL_DATA_LEN> data = {0, 0, 0, 0, 0};
    
    if (!password || strlen(password) != 6) return data;
    
    // 前4位直接存储
    for (int i = 0; i < 4; i++) {
        data[i] = password[i] - '0';
    }
    // 第5、6位压缩到 DATA4：低4位=第5位，高4位=第6位
    data[4] = ((password[5] - '0') << 4) | (password[4] - '0');
    
    return data;
}

std::string LockProtocol::DecodePassword(const std::array<uint8_t, LOCK_PROTOCOL_DATA_LEN>& data) {
    std::string password;
    password.reserve(6);
    
    // 前4位
    for (int i = 0; i < 4; i++) {
        password += ('0' + (data[i] & 0x0F));
    }
    // 第5、6位
    password += ('0' + (data[4] & 0x0F));        // 低4位 = 第5位
    password += ('0' + ((data[4] >> 4) & 0x0F)); // 高4位 = 第6位
    
    return password;
}
```

### 阶段 2：锁控服务层 (3小时)

#### 2.1 创建锁控服务

**文件：** `main/lock_control/lock_control.h`

```cpp
#pragma once
#include <functional>
#include <memory>
#include "driver/uart.h"
#include "lock_protocol.h"

class LockControlService {
public:
    using EventCallback = std::function<void(const LockMessage&)>;
    
    LockControlService();
    ~LockControlService();
    
    // 初始化并启动服务
    bool Start(uart_port_t port, int tx_pin, int rx_pin);
    void Stop();
    bool IsRunning() const { return running_; }
    
    // 发送控制命令 (CAT=0x02)
    bool SendUnlock();
    bool SendAlarm(uint8_t level);
    bool SendAlarmOff();
    bool SendTempCode(const char* password);  // 6位数字密码
    bool SendLedControl(uint8_t mode, uint8_t r, uint8_t g, uint8_t b);
    
    // 发送查询命令 (CAT=0x04)
    bool QueryLockState();
    bool QueryDoorState();
    bool QueryBattery();
    bool QueryAll();
    
    // 发送 ACK (CAT=0x0F)
    bool SendAck(uint8_t orig_cat, uint8_t orig_type, bool success = true);
    
    // 设置事件回调
    void SetEventCallback(EventCallback callback) { event_callback_ = callback; }
    
private:
    uart_port_t uart_port_;
    bool running_ = false;
    TaskHandle_t rx_task_handle_ = nullptr;
    EventCallback event_callback_;
    
    bool SendMessage(uint8_t cat, uint8_t type, 
                     const std::array<uint8_t, LOCK_PROTOCOL_DATA_LEN>& data = {});
    static void RxTask(void* param);
    void RxLoop();
};
```

#### 2.2 实现锁控服务

**文件：** `main/lock_control/lock_control.cc`

```cpp
#include "lock_control.h"
#include <esp_log.h>
#include <cstring>

#define TAG "LockControl"

LockControlService::LockControlService() : uart_port_(UART_NUM_2) {}

LockControlService::~LockControlService() {
    Stop();
}

bool LockControlService::Start(uart_port_t port, int tx_pin, int rx_pin) {
    if (running_) return false;
    
    uart_port_ = port;
    
    // 配置 UART
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    ESP_ERROR_CHECK(uart_param_config(uart_port_, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_port_, tx_pin, rx_pin, 
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(uart_port_, 256, 256, 0, nullptr, 0));
    
    running_ = true;
    
    // 创建接收任务
    xTaskCreate(RxTask, "lock_rx", 2048, this, 5, &rx_task_handle_);
    
    ESP_LOGI(TAG, "Lock control service started on UART%d (TX=%d, RX=%d)", 
             uart_port_, tx_pin, rx_pin);
    return true;
}

void LockControlService::Stop() {
    if (!running_) return;
    
    running_ = false;
    
    if (rx_task_handle_) {
        vTaskDelay(pdMS_TO_TICKS(100));
        rx_task_handle_ = nullptr;
    }
    
    uart_driver_delete(uart_port_);
    ESP_LOGI(TAG, "Lock control service stopped");
}

bool LockControlService::SendMessage(uint8_t cat, uint8_t type,
                                      const std::array<uint8_t, LOCK_PROTOCOL_DATA_LEN>& data) {
    if (!running_) return false;
    
    auto msg = LockProtocol::BuildMessage(cat, type, data);
    int written = uart_write_bytes(uart_port_, msg.data(), msg.size());
    
    ESP_LOGD(TAG, "Sent: %02X %02X %02X [%02X %02X %02X %02X %02X] %02X", 
             msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
    
    return written == static_cast<int>(msg.size());
}

// ========== 控制命令 (CAT=0x02) ==========

bool LockControlService::SendUnlock() {
    return SendMessage(static_cast<uint8_t>(MsgCategory::CONTROL),
                       static_cast<uint8_t>(ControlType::UNLOCK));
}

bool LockControlService::SendAlarm(uint8_t level) {
    std::array<uint8_t, LOCK_PROTOCOL_DATA_LEN> data = {level, 0, 0, 0, 0};
    return SendMessage(static_cast<uint8_t>(MsgCategory::CONTROL),
                       static_cast<uint8_t>(ControlType::ALARM_ON), data);
}

bool LockControlService::SendAlarmOff() {
    return SendMessage(static_cast<uint8_t>(MsgCategory::CONTROL),
                       static_cast<uint8_t>(ControlType::ALARM_OFF));
}

bool LockControlService::SendTempCode(const char* password) {
    if (!password || strlen(password) != 6) {
        ESP_LOGE(TAG, "Invalid password: must be 6 digits");
        return false;
    }
    
    auto data = LockProtocol::EncodePassword(password);
    return SendMessage(static_cast<uint8_t>(MsgCategory::CONTROL),
                       static_cast<uint8_t>(ControlType::SET_TEMP_CODE), data);
}

bool LockControlService::SendLedControl(uint8_t mode, uint8_t r, uint8_t g, uint8_t b) {
    std::array<uint8_t, LOCK_PROTOCOL_DATA_LEN> data = {mode, r, g, b, 0};
    return SendMessage(static_cast<uint8_t>(MsgCategory::CONTROL),
                       static_cast<uint8_t>(ControlType::LED_CTRL), data);
}

// ========== 查询命令 (CAT=0x04) ==========

bool LockControlService::QueryLockState() {
    return SendMessage(static_cast<uint8_t>(MsgCategory::QUERY),
                       static_cast<uint8_t>(QueryType::QUERY_LOCK_STATE));
}

bool LockControlService::QueryDoorState() {
    return SendMessage(static_cast<uint8_t>(MsgCategory::QUERY),
                       static_cast<uint8_t>(QueryType::QUERY_DOOR_STATE));
}

bool LockControlService::QueryBattery() {
    return SendMessage(static_cast<uint8_t>(MsgCategory::QUERY),
                       static_cast<uint8_t>(QueryType::QUERY_BATTERY));
}

bool LockControlService::QueryAll() {
    return SendMessage(static_cast<uint8_t>(MsgCategory::QUERY),
                       static_cast<uint8_t>(QueryType::QUERY_ALL));
}

// ========== ACK (CAT=0x0F) ==========

bool LockControlService::SendAck(uint8_t orig_cat, uint8_t orig_type, bool success) {
    auto msg = LockProtocol::BuildAck(orig_cat, orig_type, success);
    int written = uart_write_bytes(uart_port_, msg.data(), msg.size());
    return written == static_cast<int>(msg.size());
}

// ========== 接收任务 ==========

void LockControlService::RxTask(void* param) {
    auto* service = static_cast<LockControlService*>(param);
    service->RxLoop();
    vTaskDelete(nullptr);
}

void LockControlService::RxLoop() {
    uint8_t buffer[LOCK_PROTOCOL_LENGTH];
    size_t buf_pos = 0;
    
    while (running_) {
        uint8_t byte;
        int len = uart_read_bytes(uart_port_, &byte, 1, pdMS_TO_TICKS(100));
        
        if (len <= 0) continue;
        
        // 寻找帧头
        if (buf_pos == 0 && byte != LOCK_PROTOCOL_HEADER) {
            continue;
        }
        
        buffer[buf_pos++] = byte;
        
        // 收到完整帧
        if (buf_pos >= LOCK_PROTOCOL_LENGTH) {
            auto msg = LockProtocol::ParseMessage(buffer, buf_pos);
            buf_pos = 0;
            
            if (msg.valid) {
                ESP_LOGI(TAG, "Received: cat=0x%02X, type=0x%02X, data=[%02X %02X %02X %02X %02X]",
                         msg.category, msg.type, 
                         msg.data[0], msg.data[1], msg.data[2], msg.data[3], msg.data[4]);
                
                // 对非 ACK 消息发送 ACK
                if (!msg.IsAck()) {
                    SendAck(msg.category, msg.type, true);
                }
                
                // 回调处理
                if (event_callback

### 阶段 3：板级集成 (1小时)

#### 3.1 修改 config.h 添加串口引脚

**文件：** `main/boards/bread-compact-wifi-s3cam/config.h`

```cpp
// 锁控串口引脚
#define LOCK_UART_TX_PIN    GPIO_NUM_3
#define LOCK_UART_RX_PIN    GPIO_NUM_14
#define LOCK_UART_PORT      UART_NUM_1
```

#### 3.2 修改板级实现

**文件：** `main/boards/bread-compact-wifi-s3cam/compact_wifi_board_s3cam.cc`

在类中添加：
```cpp
private:
    LockControlService* lock_control_ = nullptr;
    
    void InitializeLockControl() {
        lock_control_ = new LockControlService();
        lock_control_->Start(LOCK_UART_PORT, LOCK_UART_TX_PIN, LOCK_UART_RX_PIN);
    }

public:
    LockControlService* GetLockControl() { return lock_control_; }
```

### 阶段 4：Application 集成 (2小时)

#### 4.1 添加人脸识别触发逻辑

**修改：** `main/application.cc`

```cpp
void Application::HandleLockEvent(const LockMessage& msg) {
    switch (static_cast<LockEventType>(msg.type)) {
        case LockEventType::DOORBELL_PRESSED:
        case LockEventType::HUMAN_DETECTED:
            // 触发人脸识别
            TriggerFaceRecognition();
            break;
            
        case LockEventType::LOCK_TAMPER:
            // 警报 + 上报服务器
            HandleTamperAlert(msg.data1);
            break;
            
        case LockEventType::PERSON_LEFT:
            // 固定语音
            audio_service_.PlaySound(Lang::Sounds::OGG_GOODBYE);
            break;
            
        case LockEventType::PERSON_ENTERED:
            // 固定语音
            audio_service_.PlaySound(Lang::Sounds::OGG_WELCOME);
            break;
            
        case LockEventType::DOOR_NOT_CLOSED:
            // 上报服务器
            ReportDoorNotClosed();
            break;
            
        case LockEventType::PASSWORD_ERROR:
            // 固定语音
            audio_service_.PlaySound(Lang::Sounds::OGG_PASSWORD_ERROR);
            break;
            
        case LockEventType::LOCK_LOCKED:
            // 固定语音
            audio_service_.PlaySound(Lang::Sounds::OGG_LOCK_LOCKED);
            break;
            
        default:
            break;
    }
}

void Application::TriggerFaceRecognition() {
    if (device_state_ != kDeviceStateIdle) {
        ESP_LOGW(TAG, "Cannot trigger face recognition in current state");
        return;
    }
    
    auto& board = Board::GetInstance();
    auto camera = dynamic_cast<Esp32Camera*>(board.GetCamera());
    if (!camera || !camera->IsAvailable()) {
        ESP_LOGE(TAG, "Camera not available");
        return;
    }
    
    // 确保音频通道已打开
    if (!protocol_->IsAudioChannelOpened()) {
        SetDeviceState(kDeviceStateConnecting);
        if (!protocol_->OpenAudioChannel()) {
            ESP_LOGE(TAG, "Failed to open audio channel");
            return;
        }
    }
    
    // 拍照
    if (!camera->CaptureForStream()) {
        ESP_LOGE(TAG, "Failed to capture image");
        return;
    }
    
    // 编码 JPEG
    uint8_t* jpeg_data = nullptr;
    size_t jpeg_size = 0;
    if (!camera->CaptureJpeg(&jpeg_data, &jpeg_size, 80)) {
        ESP_LOGE(TAG, "Failed to encode JPEG");
        return;
    }
    
    // 发送到服务器
    uint32_t timestamp = xTaskGetTickCount();
    protocol_->SendVideo(jpeg_data, jpeg_size, timestamp, 
                         camera->GetFrameWidth(), camera->GetFrameHeight());
    
    heap_caps_free(jpeg_data);
    
    ESP_LOGI(TAG, "Face recognition image sent, waiting for response...");
    // 服务器会返回 TTS 音频，通过现有的 OnIncomingAudio 回调播放
}
```

### 阶段 5：服务器命令处理 (1小时)

#### 5.1 处理服务器下发的命令

**修改：** `main/application.cc` 的 `OnIncomingJson` 回调

```cpp
// 在 OnIncomingJson 中添加
} else if (strcmp(type->valuestring, "lock_control") == 0) {
    auto command = cJSON_GetObjectItem(root, "command");
    if (cJSON_IsString(command)) {
        if (strcmp(command->valuestring, "unlock") == 0) {
            // 开锁
            auto lock = board.GetLockControl();
            if (lock) lock->SendUnlock();
        } else if (strcmp(command->valuestring, "temp_code") == 0) {
            // 临时开锁码
            auto code = cJSON_GetObjectItem(root, "code");
            if (cJSON_IsString(code) && strlen(code->valuestring) == 3) {
                auto lock = board.GetLockControl();
                if (lock) {
                    lock->SendTempCode(
                        code->valuestring[0] - '0',
                        code->valuestring[1] - '0',
                        code->valuestring[2] - '0'
                    );
                }
            }
        }
    }
}
```

---

## 📊 时间估算

| 阶段 | 任务 | 预计时间 |
|------|------|----------|
| 1 | 串口协议层 | 2小时 |
| 2 | 锁控服务层 | 3小时 |
| 3 | 板级集成 | 1小时 |
| 4 | Application 集成 | 2小时 |
| 5 | 服务器命令处理 | 1小时 |
| 6 | 测试调试 | 2小时 |
| **总计** | | **11小时** |

---

## ✅ 检查清单

### 协议层
- [ ] 创建 `lock_protocol.h`
- [ ] 创建 `lock_protocol.cc`
- [ ] 单元测试协议解析

### 服务层
- [ ] 创建 `lock_control.h`
- [ ] 创建 `lock_control.cc`
- [ ] UART 初始化
- [ ] 接收任务
- [ ] 发送命令

### 板级集成
- [ ] 修改 `config.h` 添加引脚定义
- [ ] 修改板级实现添加锁控服务

### Application 集成
- [ ] 添加事件处理函数
- [ ] 添加人脸识别触发
- [ ] 添加服务器命令处理

### 测试
- [ ] 串口通信测试
- [ ] 人脸识别流程测试
- [ ] 开锁命令测试
- [ ] 异常处理测试

---

## 🔍 注意事项

1. **不修改原有逻辑** - 人脸识别是扩展功能，不影响正常的语音对话流程
2. **串口波特率** - 使用 9600bps，STM32C8T6 性能有限
3. **协议简洁** - 固定 6 字节，便于 STM32 解析
4. **错误处理** - 校验和验证，ACK 确认机制
5. **状态检查** - 只在 Idle 状态触发人脸识别

---

## 📚 相关文档

- `video_protocol_specification.md` - 视频传输协议
- `implementation_summary.md` - 整体架构
- `事件及功能.txt` - 需求文档
