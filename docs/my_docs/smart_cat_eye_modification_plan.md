# 智能猫眼项目修改方案 (Smart Cat Eye Modification Plan)

## 1. 引言

### 1.1. 目标
本文档旨在为“小智 AI 聊天机器人”项目（以下简称“原项目”）提供一个详细的修改方案，以将其适配为一个功能完善的、基于 ESP32-S3 的智能猫眼系统。

### 1.2. 核心思路
严格遵循原项目的优秀架构和设计哲学，以“最少侵入，最大扩展”为原则。
- **不修改核心逻辑**: 除非必要，不改动 `application.cc`、`mcp_server.cc` 等核心文件。
- **新增板级支持**: 将所有与新硬件相关的改动封装在新的板级定义中。
- **扩展 MCP 工具**: 所有新增功能均通过新的 MCP 工具暴露给服务器，由云端进行业务编排。

### 1.3. 技术基础
本次开发将基于原项目中已有的 `bread-compact-wifi-s3cam` 板型配置，因为它已经包含了摄像头和显示屏的驱动，为我们的开发提供了一个良好的起点。

---

## 2. 第一阶段：硬件抽象与基础通信

此阶段的目标是建立新硬件的软件抽象层，并打通 ESP32 与 STM32 之间的通信。

### 2.1. 任务：创建 `smart_cat_eye` 板级定义
1.  在 `main/boards/` 目录下创建一个新目录 `smart_cat_eye`。
2.  将 `main/boards/bread-compact-wifi-s3cam/` 目录下的所有文件 (`.cc`, `.h`, `.md`) 复制到 `smart_cat_eye` 目录中。
3.  将 `smart_cat_eye/compact_wifi_board_s3cam.cc` 重命名为 `board.cc`。
4.  修改 `board.cc` 文件，将类名 `CompactWifiBoardS3Cam` 更改为 `SmartCatEyeBoard`，并同步更新文件末尾的 `DECLARE_BOARD(SmartCatEyeBoard)` 宏。

### 2.2. 任务：更新硬件引脚配置
1.  打开 `smart_cat_eye/config.h` 文件。
2.  在保留摄像头、屏幕等现有引脚配置的基础上，添加以下宏定义，用于和 STM32 通信及唤醒：
    ```c
    // --- 与 STM32 通信 (使用 UART 唤醒) ---
    #define STM32_UART_PORT      UART_NUM_1
    #define STM32_UART_TX_PIN    GPIO_NUM_3  // TX -> STM32 RX
    #define STM32_UART_RX_PIN    GPIO_NUM_14 // RX <- STM32 TX (从此引脚唤醒)
    ```

### 2.3. 任务：实现 STM32 通信模块
1.  在 `main/` 目录下创建 `stm32_controller.h` 和 `stm32_controller.cc` 文件。
2.  创建 `Stm32Controller` 类，并将其设计为单例模式。
3.  **核心职责**:
    - **发送**: 提供 `void sendCommand(const std::string& command, const cJSON* params)` 等接口，用于向 STM32 发送指令。函数内部将指令封装为 JSON 格式（如 `{"cmd":"unlock"}`）并通过 UART 发送。
    - **接收**: 在 `Stm32Controller` 内部创建一个 FreeRTOS 任务，专门用于循环监听和读取 UART 数据。
    - **分发**: 接收任务在收到来自 STM32 的完整消息（如 `{"event":"pir_detected"}`）后，解析内容，并调用 `Application::GetInstance().Schedule()` 将一个具体的处理函数（例如 `[](){ app.HandlePirEvent(); }`）调度到主应用线程去执行。

---

## 3. 第二阶段：核心逻辑与 MCP 工具集成

此阶段的目标是实现产品的核心业务逻辑，并将其封装为 MCP 工具。

### 3.1. 任务：实现低功耗与唤醒逻辑
1.  **睡眠**: 在 `Application::MainEventLoop` 或状态机中增加逻辑，当设备处于 `kDeviceStateIdle` 状态且一段时间（例如5秒）没有事件时，自动进入 **Light-sleep** 睡眠模式。
2.  **唤醒源配置**: 在进入睡眠前，调用 ESP-IDF 提供的函数配置唤醒源：
    - `esp_sleep_enable_uart_wakeup(STM32_UART_PORT)`: 配置 UART 串口唤醒。当 `STM32_UART_RX_PIN` (GPIO14) 上有数据时唤醒设备。
    - 保持项目中已有的语音唤醒在睡眠模式下可用。
3.  **唤醒处理**: 在应用的唤醒入口处，通过 `esp_sleep_get_wakeup_cause()` 获取唤醒原因，并执行相应的业务流程：
    - **UART 唤醒 (来自STM32)**: 调用 `Stm32Controller` 查询具体事件，然后触发人脸识别等流程。
    - **语音唤醒**: 触发人脸识别流程。
    - **服务器唤醒**: 执行服务器下发的指令。

### 3.2. 任务：扩展 MCP 工具集
在 `smart_cat_eye/board.cc` 中添加 `InitializeTools()` 方法，并注册以下新工具：
- **`self.door.unlock`**:
  - **功能**: 请求开锁。
  - **实现**: 回调函数中调用 `Stm32Controller::GetInstance().sendCommand("unlock", nullptr)`。
- **`self.door.get_status`**:
  - **功能**: 查询门锁、传感器等状态。
  - **实现**: 调用 `Stm32Controller` 的相应方法获取状态并返回。
- **`self.vision.recognize_face`**:
  - **功能**: 执行人脸识别流程。
  - **实现**: 基于现有的 `self.camera.take_photo` 逻辑，拍照后将图片数据发送给服务器的特定 AI 分析接口。
- **`self.stream.start_video`**:
  - **功能**: 请求开启实时音视频流。
  - **实现**: 回调函数中设置一个标志位或启动一个专门的流媒体任务。
- **`self.stream.stop_video`**:
  - **功能**: 请求停止实时音视频流。
  - **实现**: 停止流媒体任务或清除标志位。

---

## 4. 第三阶段：高级功能实现

此阶段专注于攻克技术难点，完善产品体验。

### 4.1. 任务：实现实时音视频流 (WebSocket)
1.  **设计思路**: 创建一个 `StreamManager` 类来管理流媒体会话。
2.  **协议**: 当 `self.stream.start_video` 被调用时，`StreamManager` 将主动与服务器建立一个新的 WebSocket 连接，专用于音视频传输。
3.  **数据格式**: 定义清晰的 WebSocket 消息格式。例如，使用 JSON 封装：
    - 视频帧: `{"type": "video", "format": "jpeg", "data": "base64-encoded-data"}`
    - 音频帧: `{"type": "audio", "format": "opus", "data": "base64-encoded-data"}`
4.  **并发处理**:
    - **上传**: 创建一个高优先级任务，循环从 `AudioService` 和 `Camera` 获取数据，编码/压缩后通过 WebSocket 发送。
    - **下载 (通话场景)**: 在同一个 WebSocket 连接上监听下行音频数据，并送入 `AudioService` 进行解码播放。

---

## 5. 第四阶段：测试与验证

1.  **模块测试**: 编写独立的测试用例，模拟 STM32 发送各种串口消息，验证 `Stm32Controller` 的响应是否正确。
2.  **集成测试**: 搭建一个简单的 WebSocket 服务器和 Mock MCP 服务器，模拟云端指令，验证所有新增 MCP 工具的功能是否符合预期。
3.  **端到端测试**: 将 ESP32 与真实的 STM32 控制板连接，进行全流程测试，包括唤醒、识别、开锁、报警、视频通话等。
