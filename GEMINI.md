## 项目概述

这是一个名为“小智 AI 聊天机器人”的开源项目，一个基于乐鑫 ESP32 系列微控制器（包括 ESP32-S3, ESP32-C3, 和 ESP32-P4）的 AI 语音助手。

该项目使用 ESP-IDF 框架和 C++ 语言编写。其核心功能包括离线语音唤醒、与大语言模型（如 Qwen/DeepSeek）的流式语音交互、声纹识别，并支持多种显示屏和 4G 模块。

与后端服务的通信由一个自定义的“MCP”（多端控制协议）处理，该协议可以通过 WebSocket 或混合的 MQTT+UDP 传输层运行。项目架构以一个主 `Application` 单例为中心，该单例管理设备状态、一个用于音频处理的 `AudioService` 以及一个用于通信的 `Protocol` 处理器。

该项目支持超过 70 种不同的硬件开发板，其配置在 `main/boards` 目录中进行管理。

## 构建与运行

项目使用 ESP-IDF 构建系统（版本 v5.4 或更高）。构建过程由一个 Python 脚本 (`scripts/release.py`) 编排，该脚本封装了标准的 `idf.py` 工具。

### 前提条件

1.  **ESP-IDF:** 安装并配置 ESP-IDF 5.4 或更新版本。请遵循乐鑫官方文档进行安装。
2.  **VSCode:** 推荐的集成开发环境是 VSCode，并安装官方的 Espressif IDF 插件。

### 构建命令

要构建一个特定的硬件变体，您首先需要从 `main/boards` 的子目录中确定 `board` 的名称。然后，构建命令遵循以下模式：

```bash
# 加载 ESP-IDF 环境变量
. /path/to/esp-idf/export.sh

# 运行 release 脚本来构建特定的板型
# 将 <board_name> 替换为目标板型，例如 "bread-compact-wifi-s3cam"
python scripts/release.py <board_name>
```

该脚本将自动：

1.  设置正确的 ESP32 目标芯片（例如 `esp32s3`）。
2.  应用特定于板型的 `sdkconfig` 设置。
3.  运行 `idf.py build` 命令。
4.  将引导加载程序（bootloader）、分区表和应用程序二进制文件合并为单个 `merged-binary.bin` 文件，并存放在 `build/` 目录中。

### 烧录固件

成功构建后，您可以使用 `idf.py` 来烧录固件：

```bash
# 烧录合并后的二进制文件
idf.py flash
```

## 开发规范

-   **语言:** C++
-   **框架:** ESP-IDF
-   **代码风格:** 项目旨在遵循 Google C++ 风格指南。
-   **目录结构:**
    -   `main/`: 包含主应用程序的源代码。
    -   `main/main.cc`: 应用程序入口点 (`app_main`)。
    -   `main/application.h` / `.cc`: 核心应用逻辑的单例。
    -   `main/boards/`: 包含每个受支持板型的硬件特定配置。
    -   `main/audio/`: 音频处理、唤醒词和编解码器逻辑。
    -   `main/protocols/`: 通信协议（WebSocket, MQTT）的实现。
    -   `docs/`: 项目文档，包括协议规范和硬件指南。
    -   `scripts/`: 用于构建、发布和资源管理的辅助脚本。
-   **配置:** 项目功能通过 `idf.py menuconfig` 进行配置，特定于板型的默认值和覆盖项位于各自的 `main/boards/<board_name>` 目录中。

## 重要文档

-   docs\my_docs\smart_cat_eye_implementation_plan.md
-   docs\my_docs\bread_compact_wifi_s3cam_analysis.md
-   docs\my_docs\bread_compact_wifi_s3cam_pin_details.md
-   docs\my_docs\camera_module_analysis.md
-   docs\my_docs\esp32s3_cat_eye_lock_analysis.md
-   docs\my_docs\main_directory_analysis.md

---
## 二次开发：智能猫眼项目

### 项目目标
在“小智 AI 聊天机器人”项目的基础上进行二次开发，将其改造为一个功能完善的**智能猫眼系统**。该系统将集成 AI 视觉、多模态语音交互、安防监控和低功耗管理等功能。

### 核心架构
为了实现复杂的业务逻辑并保证系统的稳定性和低功耗，我们将采用一个“云-边-端”三层分布式架构：
1.  **ESP32S3 (边缘计算核心 - “大脑”)**:
    *   **角色**: 作为系统的“大脑”和“通信中枢”。
    *   **任务**: 负责 Wi-Fi 连接、音视频流的采集与上传、接收和播放云端音频、作为云端与 STM32 芯片的桥梁，并管理自身的低功耗状态。

2.  **STM32F103C8T6 (终端实时控制器 - “双手”)**:
    *   **角色**: 作为系统的“双手”，专门负责所有底层、实时的硬件控制和传感。
    *   **任务**: 直接驱动门锁、读取密码/指纹/NFC、监测人体红外传感器（PIR）和陀螺仪等，并通过串口与 ESP32 通信。

3.  **服务器 (云端智能中心 - “云脑”)**:
    *   **角色**: 系统的“云脑”，负责所有需要强大计算能力的 AI 任务和复杂的业务逻辑。
    *   **任务**: 运行人脸识别、异常行为分析、自然语言处理（NLP）等算法，并存储视频数据、编排设备行为。

### 开发思路
我们将严格遵循原项目的优秀架构和“最少侵入，最大扩展”的设计哲学：
- **新增板级支持**: 所有与智能猫眼硬件相关的改动（引脚定义、外设初始化）将被封装在一个新的板级定义 `main/boards/smart_cat_eye` 中，不污染原有代码。
- **扩展 MCP 工具**: 所有新增的业务功能，如“开锁”、“查询门锁状态”、“开始视频通话”等，都将通过新增 MCP 工具的方式实现。云端服务器通过调用这些工具来驱动设备，而不是在设备端写死复杂的逻辑。
- **复用核心模块**: 充分复用原项目中成熟的 `Application` 状态机、`AudioService` 音频管线、LVGL 显示框架以及 `McpServer` 等核心模块。

