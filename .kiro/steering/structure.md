---
inclusion: always
---

# 项目结构

## 目录概览

```
xiaozhi-esp32/
├── main/                      # 主应用源码
│   ├── main.cc               # 入口点 (app_main)
│   ├── application.cc/h      # Application 单例，核心状态机
│   ├── settings.cc/h         # NVS 持久化设置
│   ├── ota.cc/h              # OTA 升级
│   ├── mcp_server.cc/h       # MCP 协议服务器
│   ├── audio/                # 音频子系统（编解码、唤醒词、处理器）
│   ├── display/              # 显示子系统（LCD/OLED/LVGL）
│   ├── protocols/            # 通信协议（WebSocket/MQTT）
│   ├── video/                # 视频流服务（JPEG 编码、帧队列）
│   ├── monitor/              # 监控模式服务（视频对讲）
│   ├── lock_control/         # 锁控模块（UART 协议、STM32 通信）
│   ├── led/                  # LED 控制
│   ├── boards/               # 开发板硬件抽象
│   │   ├── common/           # 共享基类（Board、WifiBoard、Esp32Camera）
│   │   └── bread-compact-wifi-s3cam/  # 当前使用的开发板
│   └── assets/               # 嵌入式资源（语言包、音效）
├── managed_components/       # ESP-IDF 组件（自动管理，勿手动修改）
├── partitions/               # 分区表
├── docs/my_docs/             # 项目文档（设计、交接、协议规范）
└── scripts/                  # 构建工具脚本
```

## 核心架构模式

### 单例模式
- `Application::GetInstance()` - 主应用控制器，管理状态机
- `Board::GetInstance()` - 硬件抽象层入口

### 开发板抽象层
- 基类：`main/boards/common/board.h`
- 派生类需实现：`GetAudioCodec()`、`GetDisplay()`、`GetLed()`、`GetNetwork()` 等
- 注册宏：`DECLARE_BOARD(ClassName)`
- 编译选择：Kconfig `BOARD_TYPE`

### 服务层
- `AudioService` - 音频采集/播放/编码
- `Protocol` - 通信抽象（WebSocket/MQTT 实现）
- `MonitorService` - 监控模式（视频对讲）
- `VideoStreamService` - 视频流捕获和传输
- `LockControl` - 锁控服务（UART 通信、事件处理）

## 开发规范

### 添加新开发板
1. 创建 `main/boards/<board-name>/` 目录
2. 添加 `config.h` 定义引脚和硬件配置
3. 实现 Board 派生类，继承 `WifiBoard` 或 `Ml307Board`
4. 在 `main/Kconfig.projbuild` 添加选项
5. 在 `main/CMakeLists.txt` 添加条件编译映射

### 文件命名约定
- 头文件/源文件：`snake_case`（如 `audio_service.cc`）
- 类名：`PascalCase`（如 `AudioService`）
- 开发板目录：`kebab-case`（如 `esp-box-3`）

### 关键配置文件
- `main/Kconfig.projbuild` - 菜单配置项定义
- `main/idf_component.yml` - 组件依赖声明
- `sdkconfig.defaults.*` - 芯片特定默认配置
