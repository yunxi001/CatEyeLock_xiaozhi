# 智能猫眼门禁系统 - 开发交接文档

## 文档信息

| 项目 | 内容 |
|-----|------|
| **项目名称** | xiaozhi-esp32 智能猫眼门禁系统 |
| **分支名称** | `cat-eye-lock-feature-2` |
| **文档版本** | 4.0 |
| **更新日期** | 2025-12-10 |
| **功能状态** | 核心功能已完成，编译通过，待硬件测试 |

---

## 一、项目概述

### 1.1 项目目标

基于 xiaozhi-esp32 语音助手项目，扩展实现智能猫眼门禁系统，包含两大核心功能：

1. **人脸识别门禁**
   - ESP32-S3 通过 UART 与锁控 MCU (STM32C8T6) 通信
   - 门铃按下或人体检测时自动拍照
   - 照片发送服务器进行人脸识别
   - 根据识别结果自动开锁或拒绝
   - TTS 语音反馈识别结果

2. **实时视频对讲（监控模式）**
   - 实时视频流传输（JPEG 编码，640x480@10fps）
   - 双向音频对讲（OPUS 编码）
   - 普通模式 ↔ 监控模式灵活切换

### 1.2 硬件平台

| 组件 | 规格 |
|------|------|
| 主控芯片 | ESP32-S3-N16R8 |
| SRAM | 512KB |
| PSRAM | 8MB |
| 摄像头 | OV2640 或兼容型号 |
| 锁控MCU | STM32C8T6 |
| 开发板 | bread-compact-wifi-s3cam |

### 1.3 技术栈

| 技术 | 用途 |
|------|------|
| ESP-IDF v5.4+ | 开发框架 |
| FreeRTOS | 实时操作系统 |
| JPEG | 视频编码 |
| OPUS | 音频编解码 |
| WebSocket/MQTT | 服务器通信 |
| UART (9600 baud) | ESP32 ↔ STM32 通信 |

---

## 二、系统架构

### 2.1 整体架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                         ESP32-S3                                 │
│                                                                  │
│  ┌──────────────┐      ┌──────────────┐      ┌──────────────┐  │
│  │ Application  │◄────►│ LockControl  │◄────►│ UART Driver  │  │
│  │              │      │   Service    │      │              │  │
│  └──────┬───────┘      └──────────────┘      └──────┬───────┘  │
│         │                                            │          │
│         │ 触发人脸识别                               │ 串口     │
│         ▼                                            ▼          │
│  ┌──────────────┐      ┌──────────────┐      ┌──────────────┐  │
│  │ Esp32Camera  │      │   Protocol   │      │   STM32C8T6  │  │
│  │              │      │  (WebSocket) │      │   (锁控MCU)  │  │
│  └──────┬───────┘      └──────┬───────┘      └──────────────┘  │
│         │                     │                                 │
│         │ JPEG                │ 视频帧/音频                     │
│         └────────────────────►│                                 │
└───────────────────────────────┼─────────────────────────────────┘
                                │ WebSocket
                                ▼
                        ┌──────────────┐
                        │    Server    │
                        │ (人脸识别)   │
                        └──────────────┘
```

### 2.2 模块结构

```
main/
├── application.cc/h          # 应用主控，状态机，模式切换
├── lock_control/             # 锁控模块（人脸识别相关）
│   ├── lock_protocol.h/cc    # UART 协议编解码
│   └── lock_control.h/cc     # 锁控服务，事件处理
├── video/                    # 视频模块（监控模式相关）
│   ├── video_stream_service.h/cc
│   └── jpeg_frame.h
├── monitor/                  # 监控服务
│   └── monitor_service.h/cc
├── protocols/                # 通信协议
│   ├── protocol.h            # 协议基类
│   ├── websocket_protocol.h/cc
│   └── mqtt_protocol.h/cc
└── boards/bread-compact-wifi-s3cam/
    ├── config.h              # 硬件引脚配置
    └── compact_wifi_board_s3cam.cc
```

---

## 三、功能一：人脸识别门禁

### 3.1 工作流程

```
STM32 检测门铃/人体
        │
        ▼ UART 事件
ESP32 LockControlService 接收
        │
        ▼ 事件回调
Application::HandleLockEvent()
        │
        ▼ 触发人脸识别
TriggerFaceRecognition()
  ├── 检查设备状态（必须为 Idle 或 Listening）
  ├── 检查摄像头可用性
  ├── 检查内存（PSRAM > 100KB）
  ├── 拍照 Capture()
  ├── JPEG 编码
  └── SendFaceRecognition() 发送到服务器（type=2）
        │
        ▼ 服务器处理
人脸识别 + TTS 生成
        │
        ▼ 返回结果
HandleFaceRecognitionResult()
  ├── result="known" && granted=true → SendUnlock()
  └── 其他情况 → 不开锁
        │
        ▼ TTS 播放
AudioService 播放语音反馈
```

### 3.2 UART 通信协议

#### 协议格式（7字节固定长度）

```
[0xAA][CAT][TYPE][DATA0][DATA1][DATA2][CHECKSUM]
  │     │    │      │      │      │       │
  帧头  类别  类型   ────数据(3字节)────   校验和

校验和 = (CAT + TYPE + DATA0 + DATA1 + DATA2) & 0xFF
```

#### 消息类别

| CAT | 名称 | 方向 | 说明 |
|-----|------|------|------|
| 0x01 | EVENT | STM32 → ESP32 | 事件通知 |
| 0x02 | CONTROL | ESP32 → STM32 | 控制命令 |
| 0x03 | STATUS | 双向 | 状态信息 |
| 0x04 | QUERY | 双向 | 查询命令 |
| 0x0F | ACK | 双向 | 确认响应 |

#### 事件类型（CAT=0x01）

| TYPE | 名称 | ESP32 处理 |
|------|------|-----------|
| 0x01 | DOORBELL_PRESSED | 触发人脸识别 |
| 0x02 | HUMAN_DETECTED | 触发人脸识别 |
| 0x03 | LOCK_TAMPER | 激活警报 + 上报服务器 |
| 0x04 | PERSON_LEFT | 播放"再见" |
| 0x05 | PERSON_ENTERED | 播放"欢迎回家" |
| 0x06 | DOOR_NOT_CLOSED | 上报服务器 |
| 0x07 | PASSWORD_ERROR | 播放"密码错误" |
| 0x08 | LOCK_LOCKED | 播放"已锁定" |

#### 控制命令（CAT=0x02）

| TYPE | 名称 | 数据格式 |
|------|------|---------|
| 0x01 | UNLOCK | 无数据 |
| 0x02 | ALARM_ON | DATA0=警报级别 |
| 0x03 | ALARM_OFF | 无数据 |
| 0x04 | SET_TEMP_CODE | BCD编码6位密码 |
| 0x05 | LED_CTRL | 模式/颜色/亮度 |

#### BCD 密码编码示例

```
密码 "123456" 编码为:
  DATA0 = 0x12 (第1、2位)
  DATA1 = 0x34 (第3、4位)
  DATA2 = 0x56 (第5、6位)
```

#### UART 配置

```cpp
波特率: 9600
数据位: 8
校验位: 无
停止位: 1
端口: UART_NUM_1
TX引脚: GPIO_NUM_3
RX引脚: GPIO_NUM_14
```

### 3.3 服务器通信协议

#### 人脸识别结果（服务器 → ESP32）

```json
{
    "type": "face_recognition",
    "result": "known",      // "known" | "unknown" | "no_face"
    "access": {
        "granted": true     // true | false
    }
}
```

#### 锁控命令（服务器 → ESP32）

```json
{
    "type": "lock_control",
    "command": "unlock",    // "unlock" | "temp_code" | "alarm_on" | "alarm_off"
    "code": "123456",       // 仅 temp_code 命令
    "level": 1              // 仅 alarm_on 命令
}
```

### 3.4 实现状态

| 阶段 | 状态 |
|------|------|
| 协议层实现 | ✅ 完成 |
| 服务层实现 | ✅ 完成 |
| 板级集成 | ✅ 完成 |
| Application层集成 | ✅ 完成 |
| 性能优化和错误处理 | ✅ 完成 |
| 编译测试 | ✅ 通过 |
| 硬件测试 | ⏳ 待进行 |

---

## 四、功能二：实时视频对讲

### 4.1 监控模式架构

```
Application
    ├── MonitorService
    │   └── VideoStreamService
    │       └── Esp32Camera
    └── Protocol (WebSocket/MQTT)
```

### 4.2 视频传输协议

使用 BinaryProtocol2 格式，通过 `type` 和 `reserved` 字段区分数据类型：

```c
struct BinaryProtocol2 {
    uint16_t version;      // 协议版本 = 2
    uint16_t type;         // 消息类型（见下文）
    uint32_t reserved;     // 扩展字段（见下文）
    uint32_t timestamp;    // 时间戳（毫秒）
    uint32_t payload_size; // 负载大小（字节）
    uint8_t payload[];     // 负载数据
};
```

**区分规则：**

| type | reserved | 数据类型 | 负载格式 |
|------|----------|----------|----------|
| 0 | 0 | 音频 | OPUS 编码 |
| 0 | 非0 | 监控视频流 | JPEG 编码 |
| 2 | 非0 | 人脸识别图像 | JPEG 编码 |

**reserved 字段编码（视频）：**
```
reserved = (width << 16) | height
```

### 4.3 命令格式

**启动监控模式：**
```json
{"type": "system", "command": "start_monitor"}
```

**停止监控模式：**
```json
{"type": "system", "command": "stop_monitor"}
```

### 4.4 实现状态

| 阶段 | 状态 |
|------|------|
| 基础设施准备 | ✅ 完成 |
| Esp32Camera 扩展 | ✅ 完成 |
| VideoStreamService | ✅ 完成 |
| Protocol 扩展 | ✅ 完成 |
| MonitorService | ✅ 完成 |
| Application 集成 | ✅ 完成 |
| 集成测试 | ⏳ 待进行 |

---

## 五、关键 API

### 5.1 Application 类

```cpp
// 监控模式
bool StartMonitorMode();
void StopMonitorMode();
bool IsMonitorMode() const;

// 锁控相关（私有方法）
void HandleLockEvent(const LockMessage& msg);
void TriggerFaceRecognition();
void HandleFaceRecognitionResult(cJSON* root);
```

### 5.2 LockControlService 类

```cpp
// 生命周期
bool Start(uart_port_t port, int tx_pin, int rx_pin);
void Stop();
bool IsRunning() const;

// 控制命令
bool SendUnlock();
bool SendAlarm(uint8_t level);
bool SendAlarmOff();
bool SendTempCode(const char* password);
bool SendLedControl(uint8_t mode, uint8_t color, uint8_t brightness);

// 查询命令
bool QueryLockState();
bool QueryDoorState();
bool QueryBattery();

// 事件回调
void SetEventCallback(EventCallback callback);
```

### 5.3 Esp32Camera 类

```cpp
bool Capture();           // 正常模式拍照
bool CaptureForStream();  // 监控模式捕获
bool CaptureJpeg(uint8_t** jpeg_data, size_t* jpeg_size, int quality = 80);
bool IsAvailable() const;
uint16_t GetFrameWidth() const;
uint16_t GetFrameHeight() const;
```

**两种捕获模式：**

| 模式 | 函数 | 特点 | 适用场景 |
|------|------|------|----------|
| 正常模式 | `Capture()` | 取3帧保留最后一帧，显示预览，支持旋转，~150ms | 人脸识别、用户拍照 |
| 监控模式 | `CaptureForStream()` | 只取1帧，无预览，无旋转，~40ms | 视频流传输 |

**使用原则：**
- 人脸识别使用 `Capture()` - 需要高质量图像和预览
- 监控视频流使用 `CaptureForStream()` - 需要高帧率

---

## 六、错误处理机制

| 场景 | 处理方式 |
|------|---------|
| UART 发送失败 | 重试3次，每次间隔10ms |
| UART 接收超时 | 1秒超时后丢弃不完整消息，重新同步 |
| 内存分配失败 | 记录错误，中止操作 |
| 低内存 (<100KB) | 拒绝新的人脸识别请求 |
| 摄像头不可用 | 记录错误，中止流程 |
| JPEG 编码失败 | 释放内存，中止流程 |
| 网络发送失败 | 释放内存，中止流程 |
| 监控模式下收到锁控事件 | 忽略事件 |
| 非 Idle 状态收到触发事件 | 忽略事件 |

---

## 七、性能指标

### 7.1 预期性能

| 指标 | 目标值 |
|------|--------|
| 视频分辨率 | 640x480 (VGA) |
| 视频帧率 | 10 fps |
| JPEG 质量 | 60-80 |
| 视频带宽 | ~1.2 Mbps |
| 音频带宽 | ~0.5 Mbps |
| 端到端延迟 | <500ms |
| 内存使用 | <5MB PSRAM |

### 7.2 人脸识别响应时间

| 阶段 | 目标时间 |
|------|---------|
| 触发到拍照 | <100ms |
| JPEG 编码 | <500ms |
| 网络传输 | <100ms |
| 服务器响应处理 | <100ms |
| 开锁命令发送 | <100ms |

---

## 八、编译和测试

### 8.1 编译命令

```bash
# 配置目标芯片
idf.py set-target esp32s3

# 编译
idf.py build

# 烧录
idf.py flash

# 监控日志
idf.py monitor
```

### 8.2 编译状态

✅ **编译通过，无错误**

### 8.3 测试建议

**人脸识别测试：**
1. 模拟 STM32 发送门铃事件
2. 验证拍照、编码、发送流程
3. 验证服务器响应处理
4. 验证开锁命令发送

**监控模式测试：**
1. 发送 `start_monitor` 命令
2. 验证视频流输出
3. 验证音频双向传输
4. 发送 `stop_monitor` 命令
5. 验证模式切换

**UART 通信测试：**
1. 使用逻辑分析仪验证 UART 信号
2. 验证波特率为 9600
3. 验证消息格式正确
4. 验证 ACK 响应时间 < 100ms

---

## 九、待解决问题

### 9.1 已知问题

1. **引脚冲突风险**
   - `GPIO_NUM_14` 同时用于 `LAMP_GPIO` 和 `LOCK_UART_RX_PIN`
   - 需要确认硬件设计是否有冲突

2. **状态检查逻辑**
   - `TriggerFaceRecognition()` 中的状态检查条件需要验证
   - 当前允许在 Idle 或 Listening 状态触发

### 9.2 后续开发建议

**高优先级：**
1. 硬件测试验证
2. 服务器端人脸识别逻辑实现
3. 与实际 STM32 硬件联调

**中优先级：**
4. 监控模式性能优化
5. 错误处理完善
6. 添加单元测试

**低优先级：**
7. 添加高级功能（录制、回放）
8. 实现自适应码率
9. 支持多种分辨率

---

## 十、相关文档

### 规范文档（权威来源）

| 文档 | 路径 | 说明 |
|------|------|------|
| 需求文档 | `.kiro/specs/face-recognition/requirements.md` | EARS 格式完整需求 |
| 设计文档 | `.kiro/specs/face-recognition/design.md` | 技术架构和接口设计 |
| 任务清单 | `.kiro/specs/face-recognition/tasks.md` | 实施进度跟踪 |

### 参考文档

| 文档 | 路径 | 说明 |
|------|------|------|
| 服务器端协议 | `docs/my_docs/server_protocol.md` | 通信协议规范 |
| 快速参考 | `docs/my_docs/quick_reference.md` | 命令和 API 速查 |

---

## 十一、变更日志

详见 `docs/my_docs/CHANGELOG.md`

---

**文档结束**

如有问题，请参考 `.kiro/specs/face-recognition/` 目录下的规范文档，或查看代码注释。
