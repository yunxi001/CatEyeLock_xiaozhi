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

-   docs\my_docs\bread_compact_wifi_s3cam_analysis.md
-   docs\my_docs\bread_compact_wifi_s3cam_pin_details.md
-   docs\my_docs\camera_module_analysis.md
-   docs\my_docs\esp32s3_cat_eye_lock_analysis.md
-   docs\my_docs\main_directory_analysis.md
