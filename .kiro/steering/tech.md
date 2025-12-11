---
inclusion: always
---

# 技术栈与开发规范

## 构建环境
- ESP-IDF v5.4+
- CMake + Kconfig 配置系统
- 目标芯片：ESP32-S3-N16R8（8MB PSRAM）

## 编程语言与代码风格
- **C++**：主要语言，遵循 Google C++ 代码风格
- **C**：ESP-IDF 组件和底层驱动
- 文件命名：`snake_case.cc` / `snake_case.h`
- 类名：`PascalCase`
- 函数/变量：`snake_case`（C）或 `camelCase`（C++ 成员）
- 代码注释使用中文

## 核心框架依赖
| 框架 | 用途 |
|------|------|
| FreeRTOS | 任务调度、事件组、队列 |
| LVGL 9.x | LCD 显示 UI |
| ESP-SR | 语音识别、唤醒词 |
| OPUS | 音频编解码 |
| cJSON | JSON 解析 |
| mbedTLS | TLS 安全通信 |
| driver/uart | UART 串口通信（锁控模块） |

## 关键组件
- `esp_wifi` / `esp-wifi-connect` - WiFi
- `esp-ml307` - 4G 蜂窝模块
- `esp_codec_dev` - 音频编解码器（ES8311、ES8388）
- `esp_lcd_*` - 显示驱动（ST7789、ILI9341、GC9A01）
- `esp_lcd_touch_*` - 触摸屏
- `esp_video` / `esp_cam_sensor` - 摄像头
- `led_strip` - LED 灯带
- `nvs_flash` - NVS 存储

## 通信协议
- WebSocket（主要实时通信，音视频流）
- MQTT + UDP（备选）
- HTTP/HTTPS（OTA、配置）
- UART（ESP32 ↔ STM32 锁控通信，9600 波特率，7字节协议）

## AI 助手开发指南

### 代码生成规则
1. 新增代码必须兼容 ESP-IDF v5.4+ API
2. 使用 `ESP_LOG*` 宏进行日志输出，避免 `printf`
3. 动态内存优先使用 `heap_caps_malloc` 指定 PSRAM
4. 任务创建使用 `xTaskCreatePinnedToCore` 指定核心
5. 避免阻塞主任务，耗时操作放入独立 FreeRTOS 任务

### 硬件抽象
- 所有硬件访问通过 `Board::GetInstance()` 获取
- 不要硬编码 GPIO 引脚，使用 `config.h` 中的宏定义
- 新增外设支持需实现对应的抽象接口

### 配置管理
- 运行时配置存储于 NVS（`Settings` 类）
- 编译时配置通过 Kconfig（`main/Kconfig.projbuild`）
- 开发板选择：`menuconfig` → "Xiaozhi Assistant" → "Board Type"

### 组件依赖
- 组件声明在 `main/idf_component.yml`
- 托管组件位于 `managed_components/`（自动管理，勿手动修改）
