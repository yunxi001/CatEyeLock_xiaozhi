# 智能猫眼门禁系统 - 文档中心

## 📋 项目状态

| 项目 | 状态 |
|------|------|
| **功能** | 人脸识别门禁 + 实时视频对讲 |
| **编译** | ✅ 通过 |
| **硬件测试** | ⏳ 待进行 |
| **更新日期** | 2025-12-10 |

---

## 📚 文档导航

### 核心文档

| 文档 | 说明 |
|------|------|
| **[开发交接文档](face_recognition_handover.md)** | 🌟 项目概述、架构、API、使用指南 |
| **[变更日志](CHANGELOG.md)** | 代码变更历史记录 |
| **[快速参考](quick_reference.md)** | 命令、API、配置速查 |

### 规范文档（权威来源）

| 文档 | 说明 |
|------|------|
| [需求文档](../../.kiro/specs/face-recognition/requirements.md) | EARS 格式完整需求 |
| [设计文档](../../.kiro/specs/face-recognition/design.md) | 技术架构和接口设计 |
| [任务清单](../../.kiro/specs/face-recognition/tasks.md) | 实施进度跟踪 |

### 服务器端开发

| 文档 | 说明 |
|------|------|
| **[服务器端协议规范](server_protocol.md)** | 通信协议和注意事项 |

### 参考资料

| 文档 | 说明 |
|------|------|
| [事件及功能](事件及功能.txt) | 原始需求参考 |

---

## 🚀 快速开始

### 编译固件

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash
idf.py monitor
```

### 启动监控模式

```json
{"type": "system", "command": "start_monitor"}
```

### 停止监控模式

```json
{"type": "system", "command": "stop_monitor"}
```

---

## 🏗️ 系统架构

```
ESP32-S3
├── Application (状态机、模式切换)
├── LockControlService (UART 通信、人脸识别触发)
├── MonitorService (监控模式管理)
├── VideoStreamService (视频流捕获)
├── Protocol (WebSocket/MQTT 通信)
└── Esp32Camera (摄像头驱动)

STM32C8T6 (锁控MCU)
├── 门铃检测
├── 人体检测
├── 门锁控制
└── LED/警报控制
```

---

## 📊 功能完成度

### 人脸识别门禁

| 模块 | 状态 |
|------|------|
| UART 协议层 | ✅ 完成 |
| 锁控服务层 | ✅ 完成 |
| 板级集成 | ✅ 完成 |
| Application 集成 | ✅ 完成 |
| 错误处理 | ✅ 完成 |

### 实时视频对讲

| 模块 | 状态 |
|------|------|
| VideoStreamService | ✅ 完成 |
| MonitorService | ✅ 完成 |
| Protocol 扩展 | ✅ 完成 |
| Application 集成 | ✅ 完成 |

---

## ⚠️ 注意事项

1. **内存管理**: JPEG 数据使用 PSRAM 分配，确保正确释放
2. **状态保护**: 人脸识别进行中会忽略新的触发事件
3. **模式互斥**: 监控模式下会忽略锁控事件
4. **摄像头选择**: 人脸识别用 `Capture()`，监控用 `CaptureForStream()`

---

## 📞 问题排查

遇到问题时：
1. 查看 [开发交接文档](face_recognition_handover.md) 的错误处理章节
2. 查看 [快速参考](quick_reference.md) 的故障排查部分
3. 检查 `.kiro/specs/face-recognition/` 下的规范文档
4. 查看代码注释

---

**最后更新**: 2025-12-10
