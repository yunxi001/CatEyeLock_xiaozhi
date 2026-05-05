# 智能猫眼门锁系统 - 文档中心

## 📊 项目状态

**当前版本**: v5.3  
**最后更新**: 2026-05-05  
**开发板**: bread-compact-wifi-s3cam  
**编译状态**: ✅ 通过

### 核心功能状态

| 功能模块     | 状态      | 文档                                        | 备注                    |
| ------------ | --------- | ------------------------------------------- | ----------------------- |
| 人脸识别门禁 | ✅ 已完成 | [查看](active/features/face-recognition.md) | ESP32 ↔ STM32 UART 通信 |
| 实时视频对讲 | ✅ 已完成 | [查看](active/features/video-intercom.md)   | 监控模式，双向音视频    |
| 锁控系统     | ✅ 已完成 | [查看](active/features/lock-control.md)     | 远程开锁、用户管理      |
| 本地预览     | ✅ 已完成 | [查看](active/features/local-preview.md)    | LCD 本地显示摄像头画面  |
| 语音交互     | ✅ 已完成 | -                                           | 继承自小智语音助手      |

---

## 🚀 快速开始

### 我是新手，想要...

- **🔧 开始开发** → [快速开始指南](active/guides/getting-started.md)
- **📖 了解架构** → [系统架构文档](active/architecture/system-overview.md)
- **🔌 查看协议** → [通信协议](active/protocols/)
- **📝 查看 API** → [快速参考](active/reference/quick-reference.md)

### 我想了解...

- **通信协议** → [ESP32与服务器通信协议 v5.2](active/protocols/esp32-server-v5.2.md)
- **锁控协议** → [ESP32与STM32锁控协议 v2.0](active/protocols/esp32-stm32-v2.0.md)
- **开发指南** → [开发指南目录](active/guides/)
- **变更历史** → [变更日志](active/reference/CHANGELOG.md)

---

## 📁 文档结构

```
项目根目录/
├── README.md                    # 项目主 README
├── 项目总结.md                  # 项目总结文档
├── GEMINI.md                    # Gemini 相关说明
│
├── .kiro/                       # Kiro 配置和规格文档
│   ├── specs/                   # 功能规格文档（需求、设计、任务）
│   └── steering/                # 开发指导文档
│
└── docs/                        # 📚 文档中心
    ├── README.md (本文件)       # 📍 文档导航中心
    ├── active/                  # ✅ 活跃文档（当前正在使用）
    │   ├── protocols/          # 当前有效的通信协议
    │   ├── features/           # 已实现的功能模块
    │   ├── guides/             # 开发指南
    │   ├── architecture/       # 架构设计
    │   └── reference/          # 参考资料
    ├── wip/                    # 🚧 进行中（正在开发）
    ├── planned/                # 📋 计划中（未开始）
    ├── deprecated/             # ⚠️ 已废弃（不再使用）
    └── assets/                 # 📁 资源文件
```

---

## 📚 文档分类

### ✅ 活跃文档 (active/)

**当前正在使用的文档，这是你最常访问的目录。**

#### 通信协议 (protocols/)

- [ESP32与服务器通信协议 v5.2](active/protocols/esp32-server-v5.2.md) - 最新版本
- [ESP32与STM32锁控协议 v2.0](active/protocols/esp32-stm32-v2.0.md) - UART 通信
- [WebSocket 协议说明](active/protocols/websocket.md) - 实时通信

#### 功能模块 (features/)

- [人脸识别门禁](active/features/face-recognition.md) - 完整的人脸识别流程
- [实时视频对讲](active/features/video-intercom.md) - 监控模式实现
- [锁控系统](active/features/lock-control.md) - 远程控制和用户管理
- [本地预览](active/features/local-preview.md) - LCD 本地显示

#### 开发指南 (guides/)

- [快速开始](active/guides/getting-started.md) - 编译、烧录、调试
- [自定义开发板](active/guides/custom-board.md) - 添加新开发板支持
- [OTA 升级指南](active/guides/ota-update.md) - 固件升级流程
- [语音资源开发](active/guides/audio-resources.md) - 语音资源集成
- [消息ID机制](active/guides/message-id-mechanism.md) - 防重放机制

#### 架构设计 (architecture/)

- [系统概览](active/architecture/system-overview.md) - 整体架构
- [硬件抽象层](active/architecture/hardware-abstraction.md) - Board 抽象

#### 参考资料 (reference/)

- [快速参考](active/reference/quick-reference.md) - 命令和 API 速查
- [变更日志](active/reference/CHANGELOG.md) - 完整的变更历史

### 🚧 进行中 (wip/)

**正在开发的功能，文档可能不完整。**

目前无进行中的功能。

### 📋 计划中 (planned/)

**计划开发但尚未开始的功能。**

目前无计划功能。

### ⚠️ 已废弃 (deprecated/)

**不再使用的旧版本文档，仅供参考。**

- [旧版本协议](deprecated/protocols/) - v5.0, v5.1 等旧版本
- [历史分析文档](deprecated/analysis/) - 开发过程中的分析文档

---

## 🔍 按场景查找

### 场景 1: 我要开始开发

1. 阅读 [快速开始指南](active/guides/getting-started.md)
2. 了解 [系统架构](active/architecture/system-overview.md)
3. 查看 [通信协议](active/protocols/esp32-server-v5.2.md)
4. 参考 [快速参考](active/reference/quick-reference.md)

### 场景 2: 我要添加新功能

1. 查看 [系统架构](active/architecture/system-overview.md) 了解模块划分
2. 参考现有 [功能模块文档](active/features/)
3. 查看 [通信协议](active/protocols/) 了解消息格式
4. 查看 [变更日志](active/reference/CHANGELOG.md) 了解最新变更

### 场景 3: 我要调试问题

1. 查看 [快速参考](active/reference/quick-reference.md) 的故障排查部分
2. 查看相关 [功能模块文档](active/features/) 的错误处理章节
3. 查看 [变更日志](active/reference/CHANGELOG.md) 确认是否有相关修复

### 场景 4: 我要添加新开发板

1. 阅读 [自定义开发板指南](active/guides/custom-board.md)
2. 了解 [硬件抽象层](active/architecture/hardware-abstraction.md)
3. 参考现有开发板实现 (`main/boards/`)

---

## 📊 协议版本对照

### ESP32 ↔ 服务器通信协议

| 版本 | 状态      | 文档                                              | 主要变更         |
| ---- | --------- | ------------------------------------------------- | ---------------- |
| v5.3 | ✅ 当前   | [查看](active/protocols/esp32-server-v5.2.md)     | 新增本地预览模式 |
| v5.2 | ✅ 当前   | [查看](active/protocols/esp32-server-v5.2.md)     | 新增查询命令     |
| v5.1 | ⚠️ 已废弃 | [查看](deprecated/protocols/esp32-server-v5.1.md) | 两级确认机制     |
| v5.0 | ⚠️ 已废弃 | [查看](deprecated/protocols/esp32-server-v5.0.md) | 整合协议         |

### ESP32 ↔ STM32 锁控协议

| 版本 | 状态      | 文档                                         | 主要变更 |
| ---- | --------- | -------------------------------------------- | -------- |
| v2.0 | ✅ 当前   | [查看](active/protocols/esp32-stm32-v2.0.md) | 简化协议 |
| v1.x | ⚠️ 已废弃 | -                                            | 旧版本   |

---

## 📂 其他重要文档

### 项目根目录文档

项目根目录包含以下重要文档：

- **README.md** - 项目主 README（英文）
- **README_zh.md** - 项目主 README（中文）
- **README_ja.md** - 项目主 README（日文）
- **项目总结.md** - 项目总结文档
- **GEMINI.md** - Gemini 相关说明

### 规格文档 (.kiro/specs/)

功能规格文档，包含需求、设计和任务定义：

| 功能                       | 位置                                       | 状态      |
| -------------------------- | ------------------------------------------ | --------- |
| 人脸识别门禁               | `.kiro/specs/face-recognition/`            | ✅ 已完成 |
| 本地摄像头预览             | `.kiro/specs/local-camera-preview/`        | ✅ 已完成 |
| 锁控系统升级               | `.kiro/specs/lock-control-upgrade/`        | ✅ 已完成 |
| 实时视频对讲               | `.kiro/specs/realtime-video-intercom/`     | ✅ 已完成 |
| ESP32与服务器通信协议 v5.2 | `.kiro/specs/communication-protocol-v5.2/` | ✅ 已完成 |

每个规格目录包含：

- `requirements.md` - 需求文档
- `design.md` - 设计文档
- `tasks.md` - 任务清单

### 开发指导文档 (.kiro/steering/)

AI 助手开发指导文档，定义开发规范和项目信息：

| 文档           | 说明               | 位置                            |
| -------------- | ------------------ | ------------------------------- |
| product.md     | 产品定位和功能说明 | `.kiro/steering/product.md`     |
| tech.md        | 技术栈和开发规范   | `.kiro/steering/tech.md`        |
| structure.md   | 项目结构说明       | `.kiro/steering/structure.md`   |
| interaction.md | AI 助手交互规范    | `.kiro/steering/interaction.md` |

### 待整理文档 (docs/my_docs/)

⚠️ **注意**: 此目录中的文档已整理到 `docs/active/` 和 `docs/deprecated/`，待确认后将删除。

包含的文档：

- 本地语音播放开发指南.md → `active/guides/local-audio-playback.md`
- 消息ID机制与工作流程.md → `active/guides/message-id-mechanism.md`
- 语音资源开发完整指南.md → `active/guides/audio-resources-complete.md`
- 语音资源清单.md → `active/reference/audio-resources-list.md`
- 智能猫眼门锁系统-ESP32与服务器通信协议规范-v5.2.md → `active/protocols/esp32-server-v5.2.md`
- CHANGELOG.md → `active/reference/CHANGELOG.md`
- quick_reference.md → `active/reference/quick-reference.md`
- 其他历史文档 → `deprecated/analysis/`

### 整理状态文档

- **MIGRATION_GUIDE.md** - 文档迁移指南，说明如何从旧位置找到新位置
- **COMPLETE_REORGANIZATION_SUMMARY.md** - 完整的文档整理总结，包含所有文档清单

---

## 🛠️ 开发工具

### 编译和烧录

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 编译
idf.py build

# 烧录
idf.py flash

# 监控日志
idf.py monitor
```

### 使用脚本编译

```bash
# 编译指定开发板
python scripts/release.py bread-compact-wifi-s3cam
```

---

## 📞 获取帮助

### 遇到问题？

1. 查看 [快速参考](active/reference/quick-reference.md) 的故障排查部分
2. 查看 [变更日志](active/reference/CHANGELOG.md) 确认是否有相关修复
3. 查看代码注释和文档

### 文档问题？

如果发现文档有误或需要补充，请：

1. 检查是否查看了正确版本的文档
2. 查看 [变更日志](active/reference/CHANGELOG.md) 确认最新状态

---

## 📝 文档约定

### 状态标识

- ✅ **已完成** - 功能已实现并测试通过
- 🚧 **进行中** - 正在开发，可能不稳定
- 📋 **计划中** - 已规划但未开始
- ⚠️ **已废弃** - 不再使用，仅供参考
- 🔴 **必须修改** - 需要立即处理
- 🟡 **建议修改** - 建议优化
- 🟢 **可选修改** - 可以考虑

### 文档版本

每个文档都包含版本信息和更新日期，请注意查看。

---

**最后更新**: 2026-05-05
