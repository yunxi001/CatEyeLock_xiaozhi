# 人脸识别功能完整设计方案

## 📋 需求总结

### 核心需求
1. **正常模式扩展** - 在现有正常模式基础上添加人脸识别功能
2. **STM32 触发** - 通过串口接收 STM32 锁控 MCU 的触发信号
3. **人脸识别流程** - 拍照 → 发送服务器 → 接收识别结果 → 播放 TTS
4. **双向通信** - ESP32 ↔ STM32 串口通信，ESP32 ↔ 服务器 WebSocket 通信
5. **可扩展性** - 支持未来添加更多锁控功能

### 触发场景
- 门铃被按下 → 触发人脸识别
- 传感器检测到人体 → 触发人脸识别
- 语音唤醒 → 触发人脸识别

### 非触发场景（其他事件）
- 锁被暴力破坏 → 警报 + 上报服务器
- 有人离开 → 固定语音回应
- 有人进门 → 固定语音回应
- 门未关严实 → 上报服务器
- 密码错误 → 固定语音回应
- 密码错误过多被锁定 → 固定语音回应

---

## 🏗️ 系统架构

### 整体架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                         ESP32-S3                                 │
│                                                                  │
│  ┌──────────────┐      ┌──────────────┐      ┌──────────────┐ │
│  │ Application  │◄────►│ LockControl  │◄────►│ UART Driver  │ │
│  │              │      │   Service    │      │              │ │
│  └──────┬───────┘      └──────────────┘      └──────┬───────┘ │
│         │                                             │         │
│         │ 触发人脸识别                                │ 串口    │
│         ▼                                             ▼         │
│  ┌──────────────┐      ┌──────────────┐      ┌──────────────┐ │
│  │ Esp32Camera  │      │   Protocol   │      │   STM32C8T6  │ │
│  │              │      │  (WebSocket) │      │   (锁控MCU)  │ │
│  └──────┬───────┘      └──────┬───────┘      └──────────────┘ │
│         │                     │                                │
│         │ JPEG                │ 视频帧                         │
│         └────────────────────►│                                │
│                               │                                │
└───────────────────────────────┼────────────────────────────────┘
                                │
                                │ WebSocket
                                ▼
                        ┌──────────────┐
                        │    Server    │
                        │ (人脸识别)    │
                        └──────┬───────┘
                                │
                                │ TTS 音频
                                ▼
                        ┌──────────────┐
                        │ AudioService │
                        │  (播放TTS)   │
                        └──────────────┘
```

### 数据流图

```
触发事件流程：
STM32 → UART → LockControl → Application → Camera → Protocol → Server
                                                                    │
                                                                    ▼
                                                            人脸识别处理
                                                                    │
                                                                    ▼
Server → Protocol → AudioService → 播放 TTS 语音
```

---

## 📡 通信协议设计

### 1. ESP32 ↔ STM32 串口协议

#### 协议格式（7字节固定长度）

```
+--------+--------+--------+--------+--------+--------+--------+
| HEADER | CAT    | TYPE   | DATA0  | DATA1  | DATA2  | CHKSUM |
| 0xAA   | 1 byte | 1 byte | 1 byte | 1 byte | 1 byte | 1 byte |
+--------+--------+--------+--------+--------+--------+--------+
```

**字段说明：**
- `HEADER`: 固定 0xAA，帧起始标识
- `CAT`: 消息分类（Category）
- `TYPE`: 消息类型（在分类内唯一）
- `DATA0-2`: 3字节数据字段
- `CHKSUM`: 校验和 = (CAT + TYPE + DATA0 + DATA1 + DATA2) & 0xFF

#### 消息分类（CAT）

| CAT | 名称 | 说明 | 方向 |
|-----|------|------|------|
| 0x01 | EVENT | 事件上报 | STM32 → ESP32 |
| 0x02 | CONTROL | 控制命令 | ESP32 → STM32 |
| 0x03 | STATUS | 状态消息 | 双向 |
| 0x04 | QUERY | 查询消息 | 双向 |
| 0x0F | ACK | 确认响应 | 双向 |

#### 事件类型（CAT=0x01，STM32 → ESP32）

| TYPE | 名称 | DATA 字段 | 说明 | 触发动作 |
|------|------|-----------|------|----------|
| 0x01 | DOORBELL_PRESSED | - | 门铃被按下 | **触发人脸识别** |
| 0x02 | HUMAN_DETECTED | - | 传感器检测到人体 | **触发人脸识别** |
| 0x03 | LOCK_TAMPER | [level, 0, 0, 0, 0] | 锁被暴力破坏 | 警报 + 上报服务器 |
| 0x04 | PERSON_LEFT | - | 有人离开 | 固定语音："再见" |
| 0x05 | PERSON_ENTERED | - | 有人进门 | 固定语音："欢迎回家" |
| 0x06 | DOOR_NOT_CLOSED | - | 门未关严实 | 上报服务器 |
| 0x07 | PASSWORD_ERROR | [count, 0, 0, 0, 0] | 密码错误 | 固定语音："密码错误" |
| 0x08 | LOCK_LOCKED | - | 密码错误过多被锁定 | 固定语音："已锁定" |
| 0x10 | HEARTBEAT | - | 心跳包 | 无 |

#### 控制命令（CAT=0x02，ESP32 → STM32）

| TYPE | 名称 | DATA 字段 | 说明 |
|------|------|-----------|------|
| 0x01 | UNLOCK | - | 开锁命令 |
| 0x02 | ALARM_ON | [level, 0, 0] | 开启警报 |
| 0x03 | ALARM_OFF | - | 关闭警报 |
| 0x04 | SET_TEMP_CODE | [d0, d1, d2] | 设置临时开锁码（6位数字） |
| 0x05 | LED_CTRL | [mode, color, brightness] | LED 控制 |

**6位密码编码方式（3字节存储）：**
- DATA0: 高4位=第1位数字，低4位=第2位数字
- DATA1: 高4位=第3位数字，低4位=第4位数字
- DATA2: 高4位=第5位数字，低4位=第6位数字

示例：密码 "123456"
```
DATA0 = 0x12  (高4位=1, 低4位=2)
DATA1 = 0x34  (高4位=3, 低4位=4)
DATA2 = 0x56  (高4位=5, 低4位=6)
```

#### 通信示例

**门铃按下事件：**
```
STM32 → ESP32: AA 01 01 00 00 00 02
                │  │  │  └────┴─ DATA0-2 = 0
                │  │  └─ TYPE = DOORBELL_PRESSED
                │  └─ CAT = EVENT
                └─ HEADER

ESP32 → STM32: AA 0F 00 01 01 00 11  (ACK)
```

**开锁命令：**
```
ESP32 → STM32: AA 02 01 00 00 00 03
STM32 → ESP32: AA 0F 00 02 01 00 12  (ACK)
```

**设置临时开锁码 "123456"：**
```
ESP32 → STM32: AA 02 04 12 34 56 A2
                │  │  │  │  │  │  └─ CHKSUM
                │  │  │  │  │  └─ DATA2 = 0x56 (5,6)
                │  │  │  │  └─ DATA1 = 0x34 (3,4)
                │  │  │  └─ DATA0 = 0x12 (1,2)
                │  │  └─ TYPE = SET_TEMP_CODE
                │  └─ CAT = CONTROL
                └─ HEADER
```

---

### 2. ESP32 ↔ 服务器协议

#### 人脸识别请求（ESP32 → 服务器）

**使用现有的 BinaryProtocol2 + reserved 字段：**

```c
struct BinaryProtocol2 {
    uint16_t version;      // 2
    uint16_t type;         // 1
    uint32_t reserved;     // (width << 16) | height
    uint32_t timestamp;    // 时间戳
    uint32_t payload_size; // JPEG 大小
    uint8_t payload[];     // JPEG 数据
};
```

**服务器解析：**
```python
# 解析头部
version = struct.unpack('<H', data[0:2])[0]
msg_type = struct.unpack('<H', data[2:4])[0]
reserved = struct.unpack('<I', data[4:8])[0]
timestamp = struct.unpack('<I', data[8:12])[0]
payload_size = struct.unpack('<I', data[12:16])[0]

# 提取图像尺寸
width = (reserved >> 16) & 0xFFFF
height = reserved & 0xFFFF

# 提取 JPEG 数据
jpeg_data = data[16:16+payload_size]
```

#### 人脸识别响应（服务器 → ESP32）

**重要：** 服务器会发送多个独立的 JSON 消息，不是一个包含所有信息的 JSON。

**消息1：人脸识别结果**
```json
{
  "type": "face_recognition",
  "result": "known",
  "person": {
    "id": 1,
    "name": "张三",
    "relation": "owner"
  },
  "access": {
    "granted": true,
    "action": "open_door"
  }
}
```

**消息2：TTS 开始**
```json
{
  "type": "tts",
  "state": "start"
}
```

**消息3：TTS 文本（可选，用于显示）**
```json
{
  "type": "tts",
  "state": "sentence_start",
  "text": "您好，张三，欢迎回家！"
}
```

**消息4：TTS 音频流**
- 通过 `OnIncomingAudio` 回调接收 OPUS 音频数据
- ESP32 自动播放音频

**消息5：TTS 结束**
```json
{
  "type": "tts",
  "state": "stop"
}
```

---

**不同场景的识别结果：**

**场景1：成功识别 + 有权限**
```json
{
  "type": "face_recognition",
  "result": "known",
  "person": {"id": 1, "name": "张三", "relation": "owner"},
  "access": {"granted": true, "action": "open_door"}
}
```

**场景2：成功识别 + 无权限**
```json
{
  "type": "face_recognition",
  "result": "known",
  "person": {"id": 2, "name": "李四", "relation": "visitor"},
  "access": {"granted": false, "reason": "不在允许时段"}
}
```

**场景3：未识别（陌生人）**
```json
{
  "type": "face_recognition",
  "result": "unknown",
  "access": {"granted": false}
}
```

**场景4：识别失败（无人脸）**
```json
{
  "type": "face_recognition",
  "result": "no_face"
}
```

---

## 💻 代码实现

### 模块结构

```
main/
├── lock_control/                    # 新增：锁控模块
│   ├── lock_control.h               # 锁控服务头文件
│   ├── lock_control.cc              # 锁控服务实现
│   ├── lock_protocol.h              # 串口协议定义
│   ├── lock_protocol.cc             # 串口协议实现
│   └── CMakeLists.txt               # 构建配置
├── boards/
│   └── bread-compact-wifi-s3cam/
│       ├── compact_wifi_board_s3cam.cc  # 修改：添加串口初始化
│       └── config.h                     # 修改：添加串口引脚定义
├── application.h                    # 修改：添加人脸识别处理
├── application.cc                   # 修改：集成锁控服务
└── CMakeLists.txt                   # 修改：添加 lock_control 目录
```

### 关键类设计

#### 1. LockProtocol 类（协议层）

```cpp
class LockProtocol {
public:
    // 构建消息
    static std::vector<uint8_t> BuildMessage(
        uint8_t cat, uint8_t type,
        const std::array<uint8_t, 3>& data = {}
    );
    
    // 解析消息
    static LockMessage ParseMessage(const uint8_t* data, size_t len);
    
    // 计算校验和
    static uint8_t CalculateChecksum(uint8_t cat, uint8_t type, const uint8_t* data);
    
    // 构建 ACK
    static std::vector<uint8_t> BuildAck(uint8_t orig_cat, uint8_t orig_type, bool success);
    
    // 编码/解码6位密码（3字节存储）
    static std::array<uint8_t, 3> EncodePassword(const char* password);
    static std::string DecodePassword(const std::array<uint8_t, 3>& data);
};
```

#### 2. LockControlService 类（服务层）

```cpp
class LockControlService {
public:
    using EventCallback = std::function<void(const LockMessage&)>;
    
    // 初始化
    bool Start(uart_port_t port, int tx_pin, int rx_pin);
    void Stop();
    
    // 发送控制命令
    bool SendUnlock();
    bool SendAlarm(uint8_t level);
    bool SendAlarmOff();
    bool SendTempCode(const char* password);
    bool SendLedControl(uint8_t mode, uint8_t r, uint8_t g, uint8_t b);
    
    // 发送查询命令
    bool QueryLockState();
    bool QueryDoorState();
    bool QueryBattery();
    
    // 设置事件回调
    void SetEventCallback(EventCallback callback);
    
private:
    uart_port_t uart_port_;
    bool running_ = false;
    TaskHandle_t rx_task_handle_ = nullptr;
    EventCallback event_callback_;
    
    bool SendMessage(uint8_t cat, uint8_t type, const std::array<uint8_t, 3>& data);
    static void RxTask(void* param);
    void RxLoop();
};
```

#### 3. Application 类（应用层）

```cpp
class Application {
private:
    LockControlService* lock_control_ = nullptr;
    
    // 处理锁控事件
    void HandleLockEvent(const LockMessage& msg);
    
    // 触发人脸识别
    void TriggerFaceRecognition();
    
    // 处理人脸识别结果
    void HandleFaceRecognitionResult(cJSON* root);
    
    // 注意：TTS 音频会通过现有的 OnIncomingJson 和 OnIncomingAudio 回调自动处理
    // 不需要额外的 TTS 处理函数
    
    // 处理其他事件
    void HandleTamperAlert(uint8_t level);
    void HandleDoorNotClosed();
    
public:
    // 初始化锁控服务
    void InitializeLockControl();
};
```

---

## 🔄 业务流程

### 1. 人脸识别完整流程

```
┌─────────┐
│ STM32   │ 门铃按下 / 人体检测
└────┬────┘
     │ 串口事件
     ▼
┌─────────────────┐
│ LockControl     │ 解析事件 → 回调
└────┬────────────┘
     │ EventCallback
     ▼
┌─────────────────┐
│ Application     │ HandleLockEvent()
└────┬────────────┘
     │ 判断事件类型
     ▼
┌─────────────────┐
│ Application     │ TriggerFaceRecognition()
└────┬────────────┘
     │ 1. 检查状态
     │ 2. 打开音频通道
     ▼
┌─────────────────┐
│ Esp32Camera     │ Capture() - 拍照
└────┬────────────┘
     │ 图像数据
     ▼
┌─────────────────┐
│ Esp32Camera     │ CaptureJpeg() - 编码
└────┬────────────┘
     │ JPEG 数据
     ▼
┌─────────────────┐
│ Protocol        │ SendVideo() - 发送
└────┬────────────┘
     │ WebSocket
     ▼
┌─────────────────┐
│ Server          │ 人脸识别处理
└────┬────────────┘
     │ JSON 响应
     ▼
┌─────────────────┐
│ Protocol        │ OnIncomingJson()
└────┬────────────┘
     │ 解析 JSON
     ▼
┌─────────────────┐
│ Application     │ HandleFaceRecognitionResult()
└────┬────────────┘
     │ 1. 判断识别结果
     │ 2. 执行开锁（如果有权限）
     │ 3. 播放 TTS
     ▼
┌─────────────────┐
│ AudioService    │ 播放 TTS 音频
└─────────────────┘
```

### 2. 其他事件处理流程

**锁被暴力破坏：**
```
STM32 → LockControl → Application → SendAlarm() → STM32
                                  → ReportToServer() → Server
                                  → PlaySound("警报")
```

**有人离开/进门：**
```
STM32 → LockControl → Application → PlaySound("再见/欢迎")
```

**门未关严实：**
```
STM32 → LockControl → Application → ReportToServer() → Server
```

**密码错误：**
```
STM32 → LockControl → Application → PlaySound("密码错误")
```

---

## 📝 实施步骤

### 阶段 1：串口协议层（2小时）

**任务：**
1. 创建 `main/lock_control/` 目录
2. 实现 `lock_protocol.h` 和 `lock_protocol.cc`
3. 编写单元测试验证协议解析

**验收标准：**
- ✅ 能正确构建9字节消息
- ✅ 能正确解析9字节消息
- ✅ 校验和计算正确
- ✅ 6位密码编码/解码正确

---

### 阶段 2：锁控服务层（3小时）

**任务：**
1. 实现 `lock_control.h` 和 `lock_control.cc`
2. 初始化 UART（波特率9600，TX=GPIO3，RX=GPIO14）
3. 创建接收任务
4. 实现发送命令函数
5. 实现事件回调机制

**验收标准：**
- ✅ UART 初始化成功
- ✅ 能接收并解析 STM32 消息
- ✅ 能发送命令到 STM32
- ✅ 事件回调正常工作
- ✅ ACK 机制正常

---

### 阶段 3：板级集成（1小时）

**任务：**
1. 修改 `config.h` 添加串口引脚定义
2. 修改板级实现添加锁控服务初始化
3. 添加 `GetLockControl()` 接口

**验收标准：**
- ✅ 串口引脚定义正确
- ✅ 锁控服务自动启动
- ✅ 可以通过 Board 获取锁控服务

---

### 阶段 4：Application 集成（2小时）

**任务：**
1. 添加 `HandleLockEvent()` 函数
2. 添加 `TriggerFaceRecognition()` 函数
3. 添加 `HandleFaceRecognitionResult()` 函数
4. 集成到 `OnIncomingJson()` 回调
5. 添加其他事件处理函数

**验收标准：**
- ✅ 门铃/人体检测触发人脸识别
- ✅ 能拍照并发送到服务器
- ✅ 能接收并解析识别结果
- ✅ 能播放 TTS 音频
- ✅ 有权限时能自动开锁
- ✅ 其他事件处理正确

---

### 阶段 5：服务器命令处理（1小时）

**任务：**
1. 处理开锁命令
2. 处理临时开锁码命令
3. 处理查看摄像头命令（已有监控模式）
4. 处理语音消息命令

**验收标准：**
- ✅ 服务器能远程开锁
- ✅ 服务器能设置临时开锁码
- ✅ 服务器能查看摄像头（监控模式）
- ✅ 服务器能发送语音消息

---

### 阶段 6：测试调试（2小时）

**任务：**
1. 串口通信测试
2. 人脸识别流程测试
3. 开锁命令测试
4. 异常处理测试
5. 性能测试

**验收标准：**
- ✅ 串口通信稳定
- ✅ 人脸识别流程完整
- ✅ 开锁命令响应及时
- ✅ 异常情况处理正确
- ✅ 不影响正常模式性能

---

## ⏱️ 时间估算

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
- [ ] 实现消息构建函数
- [ ] 实现消息解析函数
- [ ] 实现校验和计算
- [ ] 实现密码编码/解码
- [ ] 单元测试通过

### 服务层
- [ ] 创建 `lock_control.h`
- [ ] 创建 `lock_control.cc`
- [ ] UART 初始化
- [ ] 接收任务创建
- [ ] 发送命令实现
- [ ] 事件回调机制
- [ ] ACK 机制

### 板级集成
- [ ] 修改 `config.h` 添加引脚定义
- [ ] 修改板级实现添加锁控服务
- [ ] 添加 `GetLockControl()` 接口
- [ ] 编译通过

### Application 集成
- [ ] 添加事件处理函数
- [ ] 添加人脸识别触发
- [ ] 添加识别结果处理
- [ ] 添加服务器命令处理
- [ ] 添加其他事件处理
- [ ] 集成到现有流程

### 测试
- [ ] 串口通信测试
- [ ] 人脸识别流程测试
- [ ] 开锁命令测试
- [ ] 临时开锁码测试
- [ ] 异常处理测试
- [ ] 性能测试
- [ ] 稳定性测试

---

## 🔍 注意事项

### 1. 不修改原有逻辑
- ✅ 人脸识别是扩展功能
- ✅ 不影响正常的语音对话流程
- ✅ 不影响监控模式
- ✅ 只在 Idle 状态触发人脸识别

### 2. 串口通信
- ✅ 波特率 9600bps（STM32C8T6 性能有限）
- ✅ 固定9字节协议（便于解析）
- ✅ 校验和验证（确保数据完整性）
- ✅ ACK 确认机制（确保可靠传输）

### 3. 摄像头使用
- ✅ 使用 `Capture()` 函数（需要预览和高质量）
- ✅ 不使用 `CaptureForStream()`（监控模式专用）
- ✅ JPEG 质量设置为 80（平衡质量和大小）

### 4. 状态管理
- ✅ 只在 Idle 状态触发人脸识别
- ✅ 人脸识别期间不响应其他触发
- ✅ 监控模式下不响应锁控事件

### 5. 错误处理
- ✅ 摄像头不可用时返回错误
- ✅ 网络断开时缓存事件
- ✅ 串口通信失败时重试
- ✅ 超时处理

### 6. 性能考虑
- ✅ 人脸识别不阻塞主线程
- ✅ 串口接收使用独立任务
- ✅ JPEG 编码使用 PSRAM
- ✅ 避免内存泄漏

---

## 📚 相关文档

- `video_protocol_specification.md` - 视频传输协议规范
- `server_side_requirements.md` - 服务器端需求
- `implementation_summary.md` - 整体架构
- `camera_performance_optimization.md` - 摄像头性能优化
- `事件及功能.txt` - 原始需求文档

---

## 🎉 总结

本设计方案提供了一个完整的、可执行的人脸识别功能实现方案，包括：

1. **清晰的架构设计** - 模块化、可扩展
2. **详细的协议定义** - ESP32 ↔ STM32 串口协议，ESP32 ↔ 服务器 WebSocket 协议
3. **完整的实施步骤** - 6个阶段，11小时完成
4. **全面的检查清单** - 确保不遗漏任何细节
5. **充分的注意事项** - 避免常见问题

**下一步：** 按照实施步骤开始编码实现。
