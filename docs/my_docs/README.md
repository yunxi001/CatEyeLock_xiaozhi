# xiaozhi-esp32 实时视频对讲功能 - 文档中心

## 📚 文档导航

### 快速开始
- **[快速参考](quick_reference.md)** - 命令、API、配置的快速查询手册
- **[最终检查清单](final_checklist.md)** - 完整的实施检查清单和项目状态

### 规划和设计
- **[实施计划](implementation_plan.md)** - 详细的7阶段实施计划
- **[模式切换设计](mode_switching_design_v2.md)** - 普通模式和监控模式的切换机制
- **[实时视频对讲分析](realtime_video_intercom_analysis.md)** - 可行性分析和技术方案

### 实施和总结
- **[实施状态](implementation_status.md)** - 各阶段完成情况的详细检查
- **[实施总结](implementation_summary.md)** - 完整的实施总结和技术架构
- **[实施完成报告](protocol_implementation_complete.md)** - Protocol 层实施完成报告

### 服务器端文档
- **[文档使用指南](SUMMARY.md)** 🌟 告诉你需要用哪些文档以及如何使用
- **[服务器端实施指南](server_implementation_guide.md)** ⭐ 服务器端开发人员快速入门指南
- **[视频协议规范](video_protocol_specification.md)** - 详细的视频传输协议规范和实现示例
- **[服务器端需求](server_side_requirements.md)** - 服务器端协议兼容性修改需求

---

## 🎯 项目概述

### 目标
为 xiaozhi-esp32 项目添加实时视频对讲功能，支持：
- 实时视频流传输（JPEG 编码）
- 双向音频对讲（OPUS 编码）
- 灵活的模式切换（普通模式 ↔ 监控模式）

### 硬件平台
- **芯片：** ESP32-S3-N16R8
- **SRAM：** 512KB
- **PSRAM：** 8MB
- **摄像头：** OV2640 或类似

### 技术栈
- **视频编码：** JPEG
- **音频编码：** OPUS
- **通信协议：** WebSocket / MQTT
- **操作系统：** FreeRTOS

---

## 📊 项目状态

### 整体进度：100%（核心功能完成）

| 阶段 | 状态 | 完成度 |
|------|------|--------|
| 阶段一：基础设施准备 | ✅ 完成 | 100% |
| 阶段二：Esp32Camera 扩展 | ✅ 完成 | 100% |
| 阶段三：VideoStreamService | ✅ 完成 | 100% |
| 阶段四：Protocol 扩展 | ✅ 完成 | 100% |
| 阶段五：MonitorService | ✅ 完成 | 100% |
| 阶段六：Application 集成 | ✅ 完成 | 100% |
| 阶段七：集成测试 | ⏳ 待进行 | 0% |

### 编译状态
✅ **所有代码编译通过，无错误**

### 最新更新
✅ **Protocol 层视频数据传输已完成**（使用 reserved 字段区分音频和视频）

---

## 🚀 快速开始

### 1. 编译固件

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

### 2. 启动监控模式

通过 WebSocket 或 MQTT 发送命令：

```json
{
  "type": "system",
  "command": "start_monitor"
}
```

### 3. 停止监控模式

```json
{
  "type": "system",
  "command": "stop_monitor"
}
```

---

## 📖 核心概念

### 设备模式

**普通模式（Normal Mode）**
- AI 语音对话
- 唤醒词检测
- 语音识别和合成
- 不发送视频流

**监控模式（Monitor Mode）**
- 实时视频流传输
- 双向音频对讲
- 禁用唤醒词检测
- 启用 AEC（回声消除）

### 架构设计

```
Application
    ├── MonitorService
    │   └── VideoStreamService
    │       └── Esp32Camera
    └── Protocol (WebSocket/MQTT)
```

### 数据流

```
摄像头 → Esp32Camera → VideoStreamService → MonitorService → Protocol → 服务器
```

---

## 🔧 核心组件

### 1. Esp32Camera
- 视频捕获
- JPEG 编码
- 帧参数查询

### 2. VideoStreamService
- 视频流管理
- 帧队列管理
- 帧率控制

### 3. MonitorService
- 监控模式管理
- 音视频协调
- 网络传输

### 4. Application
- 模式切换
- 命令处理
- 状态管理

---

## 📝 API 参考

### 启动监控模式

```cpp
Application& app = Application::GetInstance();
if (app.StartMonitorMode()) {
    // 监控模式已启动
}
```

### 停止监控模式

```cpp
app.StopMonitorMode();
```

### 检查当前模式

```cpp
if (app.IsMonitorMode()) {
    // 当前处于监控模式
}
```

---

## 🔍 故障排查

### 常见问题

**Q: 编译失败？**
```bash
# 清理并重新编译
idf.py fullclean
idf.py build
```

**Q: 摄像头初始化失败？**
- 检查硬件连接
- 检查引脚配置
- 查看日志：`idf.py monitor | grep "Esp32Camera"`

**Q: 视频流无输出？**
- 确认监控模式已启动
- 检查网络连接
- 查看日志：`idf.py monitor | grep "VideoStreamService"`

---

## 📈 性能指标

### 预期性能

| 指标 | 目标值 |
|------|--------|
| 视频分辨率 | 640x480 (VGA) |
| 视频帧率 | 10 fps |
| JPEG 质量 | 60 |
| 视频带宽 | ~1.2 Mbps |
| 音频带宽 | ~0.5 Mbps |
| 总带宽 | ~1.7 Mbps |
| 端到端延迟 | <500ms |
| 内存使用 | <5MB PSRAM |

---

## ⚠️ 已知限制

### 当前限制

1. ~~**视频数据传输不完整**~~ ✅ **已解决**
   - ✅ 已实现完整的二进制数据传输
   - ✅ 使用 BinaryProtocol2 的 reserved 字段

2. **性能未验证**
   - 需要硬件测试
   - 实际帧率和延迟待测量

3. **错误处理不完善**
   - 异常恢复机制需要加强
   - 边界情况处理需要完善

---

## 🎯 下一步计划

### 立即行动
1. ✅ 完成核心功能实现
2. ⏳ 完善 Protocol 层视频数据传输
3. ⏳ 进行硬件测试

### 短期计划
4. ⏳ 性能优化
5. ⏳ 错误处理完善
6. ⏳ 服务器端开发

### 长期计划
7. 📝 添加高级功能（录制、回放等）
8. 📝 实现自适应码率
9. 📝 支持多种分辨率

---

## 📞 支持和反馈

### 文档问题
如果发现文档有误或不清楚的地方，请：
1. 检查相关的详细文档
2. 查看代码注释
3. 查看日志输出

### 技术问题
1. 查看故障排查部分
2. 检查日志输出
3. 查看相关源代码

---

## 📄 许可证

本项目遵循 xiaozhi-esp32 项目的许可证。

---

## 🙏 致谢

感谢 xiaozhi-esp32 项目提供的优秀基础架构。

---

**最后更新：** 2024年  
**文档版本：** 1.0  
**项目状态：** 核心功能完成，待硬件测试
