---
inclusion: always
---

# 智能猫眼与门禁系统

基于大语言模型的智能猫眼与门禁系统，在小智 ESP32 语音助手基础上扩展，实现人脸识别门禁和实时视频对讲功能。

## 项目定位

| 属性 | 说明 |
|------|------|
| 类型 | 毕业设计 - 智能门禁系统 |
| 硬件 | ESP32-S3-N16R8 + STM32C8T6 锁控 MCU |
| 核心能力 | 人脸识别开锁 + 实时视频对讲 + AI 语音交互 |
| 开发板 | `bread-compact-wifi-s3cam` |

## 核心功能

### 1. 人脸识别门禁
- STM32 检测门铃/人体 → UART 通知 ESP32 → 拍照 → 服务器识别 → 开锁
- UART 协议：7字节固定格式，9600 波特率
- 关键模块：`main/lock_control/`

### 2. 实时视频对讲（监控模式）
- 远程查看摄像头画面 + 双向音频
- 视频：JPEG 编码，640x480@10fps
- 音频：OPUS 编码
- 关键模块：`main/video/`、`main/monitor/`

### 3. AI 语音交互（继承自小智）
- 离线唤醒 + 云端 LLM 对话
- TTS 语音反馈识别结果

## 关键文件

| 模块 | 路径 | 说明 |
|------|------|------|
| 锁控协议 | `main/lock_control/lock_protocol.*` | UART 通信协议 |
| 锁控服务 | `main/lock_control/lock_control.*` | 事件处理、命令发送 |
| 视频流 | `main/video/video_stream_service.*` | 视频捕获和传输 |
| 监控服务 | `main/monitor/monitor_service.*` | 监控模式管理 |
| 应用主控 | `main/application.*` | 状态机、模式切换 |
| 开发板配置 | `main/boards/bread-compact-wifi-s3cam/` | 硬件引脚配置 |

## 开发指引

### 当前状态
- 核心功能已实现，编译通过
- 待完成：锁控服务初始化、集成测试、服务器端开发
- 详细交接文档：`docs/my_docs/face_recognition_handover.md`

### 开发原则
- 目标平台：ESP32-S3（需 PSRAM）
- JPEG 数据使用 PSRAM 分配，注意内存释放
- 监控模式与锁控事件互斥
- 人脸识别用 `Capture()`，监控用 `CaptureForStream()`

### 通信协议
- WebSocket：主要实时通信
- UART：ESP32 ↔ STM32 锁控通信（7字节协议）
- 视频：BinaryProtocol2 格式，reserved 字段区分音视频
