# 人脸识别功能开发交接文档

## 文档信息

| 项目 | 内容 |
|-----|------|
| **项目名称** | xiaozhi-esp32 智能门锁人脸识别功能 |
| **分支名称** | `cat-eye-lock-feature-2` |
| **文档版本** | 3.0 |
| **创建日期** | 2025-12-08 |
| **功能状态** | 核心功能已完成，编译通过 |

---

## 一、项目概述

### 1.1 项目目标

为 xiaozhi-esp32 智能门锁系统添加两大核心功能：

1. **实时视频对讲功能（监控模式）**
   - 实时视频流传输（JPEG 编码）
   - 双向音频对讲（OPUS 编码）
   - 灵活的模式切换（普通模式 ↔ 监控模式）

2. **人脸识别功能**
   - ESP32-S3 通过 UART 与锁控 MCU (STM32C8T6) 通信
   - 当检测到门铃按下或人体接近时，自动拍照
   - 将照片发送到服务器进行人脸识别
   - 根据识别结果决定是否开锁
   - 通过 TTS 语音反馈识别结果

### 1.2 硬件平台

- **芯片：** ESP32-S3-N16R8
- **SRAM：** 512KB
- **PSRAM：** 8MB
- **摄像头：** OV2640 或类似
- **锁控MCU：** STM32C8T6

### 1.3 技术栈

- **视频编码：** JPEG
- **音频编码：** OPUS
- **通信协议：** WebSocket / MQTT
- **串口协议：** 自定义7字节协议
- **操作系统：** FreeRTOS


---

## 二、Git 提交历史

### 2.1 已提交的修改 (5个commit)

| Commit | 说明 |
|--------|------|
| `e153467` | 新的开始，之前的 cat-eye-lock-feature 分支废弃 |
| `9e61cdf` | 整理了一下文档资料 |
| `ff33336` | 实施规划完成，准备开始修改代码 |
| `13ed6cf` | 音视频传输功能 ESP32 端实现，准备开始实现服务器端 |
| `fb9e0b3` | 监控模式功能基本实现，性能待优化 (HEAD) |

### 2.2 未提交的修改

**修改的文件 (8个)**:
- `docs/my_docs/face_recognition_complete_design.md`
- `docs/my_docs/log.txt`
- `main/CMakeLists.txt`
- `main/application.cc`
- `main/application.h`
- `main/boards/bread-compact-wifi-s3cam/compact_wifi_board_s3cam.cc`
- `main/boards/bread-compact-wifi-s3cam/config.h`
- `main/lock_control/lock_protocol.h`

**新增的文件 (6个)**:
- `.kiro/specs/face-recognition/` (整个目录)
  - `requirements.md` - 需求文档
  - `design.md` - 设计文档
  - `tasks.md` - 任务清单
- `docs/my_docs/face_recognition_design_changes.md`
- `docs/my_docs/face_recognition_handover.md`
- `main/lock_control/lock_control.cc`
- `main/lock_control/lock_control.h`
- `main/lock_control/lock_protocol.cc`

---

## 三、功能一：实时视频对讲（监控模式）

### 3.1 功能描述

监控模式允许服务器远程查看设备摄像头画面，并进行双向音频对讲。

### 3.2 架构设计

```
Application
    ├── MonitorService
    │   └── VideoStreamService
    │       └── Esp32Camera
    └── Protocol (WebSocket/MQTT)
```

**数据流：**
```
摄像头 → Esp32Camera → VideoStreamService → MonitorService → Protocol → 服务器
```

### 3.3 新增/修改的文件

**新增文件：**
```
main/
├── video/
│   ├── video_stream_service.h      # 视频流服务头文件
│   ├── video_stream_service.cc     # 视频流服务实现
│   └── jpeg_frame.h                # JPEG 帧结构定义
├── monitor/
│   ├── monitor_service.h           # 监控服务头文件
│   └── monitor_service.cc          # 监控服务实现
```

**修改文件：**
- `main/device_state.h` - 新增监控模式状态枚举
- `main/application.h/.cc` - 新增监控模式管理方法
- `main/protocols/protocol.h` - 新增 SendVideo() 虚方法
- `main/protocols/websocket_protocol.h/.cc` - 实现视频发送
- `main/protocols/mqtt_protocol.h/.cc` - 实现视频发送
- `main/boards/common/esp32_camera.h/.cc` - 新增 CaptureJpeg() 等方法

### 3.4 视频传输协议

使用 BinaryProtocol2 格式，通过 `reserved` 字段区分音频和视频：

```c
struct BinaryProtocol2 {
    uint16_t version;      // 协议版本 = 2
    uint16_t type;         // 消息类型 = 0
    uint32_t reserved;     // 音频: 0, 视频: (width << 16) | height
    uint32_t timestamp;    // 时间戳（毫秒）
    uint32_t payload_size; // 负载大小（字节）
    uint8_t payload[];     // 负载数据
};
```

### 3.5 命令格式

**启动监控模式：**
```json
{"type": "system", "command": "start_monitor"}
```

**停止监控模式：**
```json
{"type": "system", "command": "stop_monitor"}
```

### 3.6 完成状态

| 阶段 | 状态 | 完成度 |
|------|------|--------|
| 阶段一：基础设施准备 | ✅ 完成 | 100% |
| 阶段二：Esp32Camera 扩展 | ✅ 完成 | 100% |
| 阶段三：VideoStreamService | ✅ 完成 | 100% |
| 阶段四：Protocol 扩展 | ✅ 完成 | 100% |
| 阶段五：MonitorService | ✅ 完成 | 100% |
| 阶段六：Application 集成 | ✅ 完成 | 100% |
| 阶段七：集成测试 | ⏳ 待进行 | 0% |


---

## 四、功能二：人脸识别

### 4.1 功能描述

当 STM32 锁控 MCU 检测到门铃按下或人体接近时，通过串口通知 ESP32，ESP32 自动拍照并发送到服务器进行人脸识别，根据识别结果决定是否开锁。

### 4.2 核心工作流程

```
STM32 → UART事件 → ESP32 → 拍照 → JPEG编码 → 服务器
                                                  │
                                                  ▼
                                            人脸识别处理
                                                  │
                                                  ▼
STM32 ← 开锁命令 ← ESP32 ← 识别结果+TTS ← 服务器
```

### 4.3 新增模块: lock_control

**目录结构：**
```
main/lock_control/
├── lock_protocol.h      # 协议定义（常量、枚举、消息结构）
├── lock_protocol.cc     # 协议实现（编码、解码、校验和、BCD密码）
├── lock_control.h       # 锁控服务接口
└── lock_control.cc      # 锁控服务实现（UART通信、事件回调）
```

### 4.4 UART 通信协议

#### 协议格式（7字节固定长度）

```
[0xAA][CAT][TYPE][DATA0][DATA1][DATA2][CHECKSUM]
  │     │    │      │      │      │       │
  帧头  类别  类型   ────数据(3字节)────   校验和
```

**校验和计算：**
```
CHECKSUM = (CAT + TYPE + DATA0 + DATA1 + DATA2) & 0xFF
```

#### 消息类别（CAT）

| 类别 | 值 | 方向 | 说明 |
|-----|-----|------|------|
| EVENT | 0x01 | STM32 → ESP32 | 事件通知 |
| CONTROL | 0x02 | ESP32 → STM32 | 控制命令 |
| STATUS | 0x03 | 双向 | 状态信息 |
| QUERY | 0x04 | 双向 | 查询命令 |
| ACK | 0x0F | 双向 | 确认响应 |

#### 事件类型（CAT=0x01，STM32 → ESP32）

| 类型 | 值 | 说明 | ESP32 处理方式 |
|-----|-----|------|---------------|
| DOORBELL_PRESSED | 0x01 | 门铃按下 | 触发人脸识别 |
| HUMAN_DETECTED | 0x02 | 人体检测 | 触发人脸识别 |
| LOCK_TAMPER | 0x03 | 暴力破坏 | 激活警报 + 上报服务器 |
| PERSON_LEFT | 0x04 | 人员离开 | 播放"再见" |
| PERSON_ENTERED | 0x05 | 人员进入 | 播放"欢迎回家" |
| DOOR_NOT_CLOSED | 0x06 | 门未关严 | 上报服务器 |
| PASSWORD_ERROR | 0x07 | 密码错误 | 播放提示 |
| LOCK_LOCKED | 0x08 | 已锁定 | 播放提示 |
| HEARTBEAT | 0x10 | 心跳 | 无操作 |

#### 控制命令（CAT=0x02，ESP32 → STM32）

| 类型 | 值 | 说明 | 数据格式 |
|-----|-----|------|---------|
| UNLOCK | 0x01 | 开锁 | 无数据 |
| ALARM_ON | 0x02 | 开启警报 | DATA0=警报级别 |
| ALARM_OFF | 0x03 | 关闭警报 | 无数据 |
| SET_TEMP_CODE | 0x04 | 设置临时密码 | BCD编码的6位密码 |
| LED_CTRL | 0x05 | LED控制 | DATA0=模式, DATA1=颜色, DATA2=亮度 |

#### BCD 密码编码

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

### 4.5 服务器通信协议

#### 接收人脸识别结果

```json
{
    "type": "face_recognition",
    "result": "known",      // "known" | "unknown" | "no_face"
    "access": {
        "granted": true     // true | false
    }
}
```

**处理逻辑：**
- `result == "known" && access.granted == true` → 发送开锁命令
- 其他情况 → 不开锁

#### 接收锁控命令

```json
{
    "type": "lock_control",
    "command": "unlock",    // "unlock" | "temp_code" | "alarm_on" | "alarm_off"
    "code": "123456",       // 仅 temp_code 命令
    "level": 1              // 仅 alarm_on 命令
}
```

### 4.6 完成状态

| 阶段 | 任务 | 状态 |
|-----|------|------|
| 阶段1 | 协议层实现 | ✅ 完成 |
| 阶段2 | 服务层实现 | ✅ 完成 |
| 阶段3 | 板级集成 | ✅ 完成 |
| 阶段4 | Application层集成 | ✅ 完成 |
| 阶段5 | 性能优化和错误处理 | ✅ 完成 |
| 阶段6 | 编译测试 | ✅ 编译通过 |


---

## 五、摄像头性能优化

### 5.1 两种捕获模式

| 模式 | 函数 | 用途 | 特点 |
|------|------|------|------|
| 正常模式 | `Capture()` | 人脸识别拍照 | 取3帧保留最后一帧，显示预览，~150ms |
| 监控模式 | `CaptureForStream()` | 视频流传输 | 只取1帧，不显示预览，~40ms |

### 5.2 使用建议

- **人脸识别** → 使用 `Capture()`（需要高质量图像和预览）
- **监控视频流** → 使用 `CaptureForStream()`（需要高帧率）

---

## 六、错误处理机制

| 场景 | 处理方式 |
|-----|---------|
| UART 发送失败 | 重试3次，每次间隔10ms |
| UART 接收超时 | 1秒超时后丢弃不完整消息，重新同步到帧头 |
| 内存分配失败 | 记录错误，中止操作 |
| 低内存 (<100KB) | 拒绝新的人脸识别请求 |
| 摄像头不可用 | 记录错误，中止流程 |
| JPEG 编码失败 | 释放内存，中止流程 |
| 网络发送失败 | 释放内存，中止流程 |
| 监控模式下收到锁控事件 | 忽略事件 |
| 非 Idle 状态收到触发事件 | 忽略事件 |

---

## 七、待解决问题

### 7.1 锁控服务初始化

当前 `lock_control_` 在 `Application::Start()` 中被设置为 `nullptr`：

```cpp
// 当前代码 (application.cc 第 530 行附近)
lock_control_ = nullptr;
```

**需要修改为：**
```cpp
// 从板级获取锁控服务
// 注意：需要将 Board 基类添加 GetLockControl() 虚方法
// 或者使用 dynamic_cast 转换为具体的板级类
auto& board = Board::GetInstance();
// lock_control_ = board.GetLockControl();  // 需要实现
```

### 7.2 引脚冲突

`GPIO_NUM_14` 同时用于:
- `LAMP_GPIO` (MCP 协议测试用的灯)
- `LOCK_UART_RX_PIN` (锁控 UART 接收)

**需要确认硬件设计是否有冲突。**

### 7.3 Board 基类接口

需要在 `Board` 基类中添加 `GetLockControl()` 虚方法，或者使用其他方式让 `Application` 获取锁控服务。

---

## 八、编译和测试

### 8.1 编译命令

```bash
idf.py build
```

### 8.2 编译状态

✅ 编译通过，无错误

### 8.3 测试建议

**监控模式测试：**
1. 发送 `start_monitor` 命令
2. 验证视频流输出
3. 验证音频双向传输
4. 发送 `stop_monitor` 命令
5. 验证模式切换

**人脸识别测试：**
1. 模拟 STM32 发送门铃事件
2. 验证拍照、编码、发送流程
3. 验证服务器响应处理
4. 验证开锁命令发送

**UART 通信测试：**
1. 使用逻辑分析仪验证 UART 信号
2. 验证波特率为 9600
3. 验证消息格式正确
4. 验证 ACK 响应时间 < 100ms


---

## 九、相关文档索引

### 9.1 规划和设计文档

| 文档 | 路径 | 说明 |
|-----|------|------|
| 实施计划 | `docs/my_docs/implementation_plan.md` | 详细的7阶段实施计划 |
| 模式切换设计 | `docs/my_docs/mode_switching_design_v2.md` | 普通模式和监控模式的切换机制 |
| 人脸识别完整设计 | `docs/my_docs/face_recognition_complete_design.md` | 人脸识别功能详细设计 |
| 实时视频对讲分析 | `docs/my_docs/realtime_video_intercom_analysis.md` | 可行性分析和技术方案 |

### 9.2 实施和总结文档

| 文档 | 路径 | 说明 |
|-----|------|------|
| 实施状态 | `docs/my_docs/implementation_status.md` | 各阶段完成情况的详细检查 |
| 实施总结 | `docs/my_docs/implementation_summary.md` | 完整的实施总结和技术架构 |
| 实施完成报告 | `docs/my_docs/protocol_implementation_complete.md` | Protocol 层实施完成报告 |
| 最终检查清单 | `docs/my_docs/final_checklist.md` | 完整的实施检查清单和项目状态 |

### 9.3 服务器端文档

| 文档 | 路径 | 说明 |
|-----|------|------|
| 文档使用指南 | `docs/my_docs/SUMMARY.md` | 告诉你需要用哪些文档以及如何使用 |
| 服务器端实施指南 | `docs/my_docs/server_implementation_guide.md` | 服务器端开发人员快速入门指南 |
| 视频协议规范 | `docs/my_docs/video_protocol_specification.md` | 详细的视频传输协议规范和实现示例 |
| 服务器端需求 | `docs/my_docs/server_side_requirements.md` | 服务器端协议兼容性修改需求 |

### 9.4 快速参考

| 文档 | 路径 | 说明 |
|-----|------|------|
| 快速参考 | `docs/my_docs/quick_reference.md` | 命令、API、配置的快速查询手册 |
| 摄像头性能优化 | `docs/my_docs/camera_performance_optimization.md` | 摄像头性能优化策略 |
| 事件及功能 | `docs/my_docs/事件及功能.txt` | 原始需求文档 |

### 9.5 Kiro Spec 文档

| 文档 | 路径 | 说明 |
|-----|------|------|
| 需求文档 | `.kiro/specs/face-recognition/requirements.md` | EARS 格式的完整需求 |
| 设计文档 | `.kiro/specs/face-recognition/design.md` | 技术架构和接口设计 |
| 任务清单 | `.kiro/specs/face-recognition/tasks.md` | 实施进度跟踪 (中文) |

---

## 十、注意事项

1. **内存管理**: JPEG 数据使用 PSRAM 分配，确保在所有代码路径中正确释放

2. **状态保护**: `face_recognition_in_progress_` 标志防止并发触发，需要确保在所有退出路径中重置

3. **监控模式互斥**: 监控模式下会忽略锁控事件，这是设计行为

4. **TTS 反馈**: 人脸识别结果的语音反馈由服务器通过现有的 TTS 机制发送，无需额外处理

5. **服务器端**: 服务器端的人脸识别处理逻辑需要单独实现

6. **摄像头选择**: 人脸识别使用 `Capture()` 函数（高质量），监控模式使用 `CaptureForStream()` 函数（高帧率）

---

## 十一、后续开发建议

### 11.1 立即行动（优先级：高）

1. **完成锁控服务初始化**: 解决 `lock_control_` 为 `nullptr` 的问题
2. **服务器端开发**: 实现人脸识别服务器端逻辑
3. **硬件测试**: 烧录固件到设备，测试基本功能

### 11.2 短期计划（优先级：中）

4. **性能优化**: 监控模式性能待优化
5. **硬件验证**: 与实际 STM32 硬件联调测试
6. **错误处理完善**: 添加更多边界情况处理

### 11.3 长期计划（优先级：低）

7. **添加单元测试**: 完成可选的测试任务
8. **添加高级功能**: 录制、回放等
9. **实现自适应码率**: 根据网络状况调整

---

## 十二、性能指标

### 12.1 预期性能

| 指标 | 目标值 | 说明 |
|------|--------|------|
| 视频分辨率 | 640x480 | VGA |
| 视频帧率 | 10 fps | 可配置 |
| JPEG 质量 | 60 | 可配置 |
| 视频带宽 | ~1.2 Mbps | 150KB/s |
| 音频带宽 | ~0.5 Mbps | 双向 |
| 总带宽 | ~1.7 Mbps | 视频+音频 |
| 端到端延迟 | <500ms | 目标 |
| 内存使用 | <5MB | PSRAM |

### 12.2 资源使用

**PSRAM：**
- 视频帧缓冲：3帧 × 20KB = 60KB
- JPEG 队列：3帧 × 15KB = 45KB
- 总计：~105KB

**CPU：**
- 视频捕获：~10%
- JPEG 编码：~15%
- 网络传输：~5%
- 总计：~30%

---

**文档结束**

如有问题，请参考以上文档或联系原开发人员。
