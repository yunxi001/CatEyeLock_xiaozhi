# 智能猫眼门锁功能实现计划文档 (V6.0 最终完整详细版)

## 1. 总体目标

将 `xiaozhi-esp32` 项目从一个通用的 AI 语音助手平台，扩展为一个功能完整的 **双芯片智能猫眼门锁** 应用。ESP32-S3 作为“智能主脑”，负责视频、音频、显示、网络通信和用户交互，并通过串口与一个专用的“锁控 MCU”通信，以控制门锁的开关。

**目标硬件平台**: 基于 `bread-compact-wifi-s3cam` 配置，核心芯片为 **`esp32s3n16r8`** (16MB Flash, 8MB PSRAM)。

---

## 2. 核心开发原则与方法论

### 2.1 原则一：优先新增，慎重修改 (Prioritize Addition, Modify with Caution)

-   **阐述**: 优先通过创建新文件、新类、新模块的方式来实现新功能，最大限度地避免修改项目中已经稳定运行的核心文件。
-   **目的**: 降低风险、保持模块化、易于维护与回归。
-   **承诺**: 本计划的所有设计和步骤都将严格遵循此原则。

### 2.2 原则二：聚焦单点，迭代开发 (Focus on a Single Point, Iterate and Develop)

-   **阐述**: 在理解项目整体架构后，每次的开发工作都应聚焦于一个独立的、可验证的功能点或任务。按照“详细实施计划”中划分的阶段和步骤，逐一完成。
-   **目的**: 避免因同时处理过多内容而导致注意力分散、逻辑混乱或引入潜在错误。确保每一步的完成都是稳固和高质量的。
-   **方法**:
    1.  **领会全局**: 在动手前，先完整阅读并理解本计划文档的全部内容。
    2.  **分解任务**: 将一个阶段性目标（如“阶段二：整合到应用核心与 UI”）分解为更小的子任务（如“2.1 修改 Application”、“2.2 修改 UI”）。
    3.  **专注执行**: 一次只处理一个子任务，完成后进行充分测试。
    4.  **持续集成**: 在当前子任务稳定后，再开始下一个子任务的开发。

---

## 3. 架构设计与可行性分析

### 3.1 核心设计思想：继承与扩展

为了最大程度地复用现有代码并保持项目结构的清晰，我们不直接修改 `bread-compact-wifi-s3cam` 的代码，而是采用**继承和扩展**的策略：

1.  **创建新的板型定义**: 我们将创建一个新的板型，例如 `cat_eye_lock_s3`。
2.  **继承基础板型**: 这个新的板型类 `CatEyeLockS3Board` 将继承自 `CompactWifiBoardS3Cam`。这样做的好处是，所有摄像头、显示屏、音频、按钮等已有的硬件初始化和驱动逻辑将**被完全复用**。
3.  **扩展新功能**: 在新的 `CatEyeLockS3Board` 类中，我们将专注于实现新增的功能——即与锁控 MCU 的通信。

这种方法是本项目预期的标准扩展方式，具有高内聚、低耦合的优点。

### 3.2 新增核心模块：锁控器通信模块 (`LockController`)

为了解耦通信协议和应用逻辑，我们将创建一个全新的、可复用的模块 `LockController`。

-   **职责**:
-   **UART 通信**: 负责初始化和管理与锁控 MCU 通信的 UART 端口（根据之前的分析，使用 `GPIO43` 和 `GPIO44`）。
-   **协议封装**: 定义并实现与锁控 MCU 通信的协议。协议可以设计得非常简单，例如基于 JSON 的文本格式或固定长度的二进制格式。
-   **指令发送**: 提供高级 API，如 `UnlockDoor()`、`RequestStatus()`，内部会将这些指令按协议格式化后通过 UART 发送出去。
-   **数据接收与解析**: 创建一个独立的 FreeRTOS 任务，在后台持续监听 UART 端口，接收来自锁控 MCU 的数据（如状态上报、操作结果），解析后通过回调或事件通知上层应用。
-   **状态管理**: 维护门锁的当前状态（如：`LOCKED`, `UNLOCKED`, `AJAR`, `UNKNOWN`）。

-   **协议示例 (JSON over UART)**:
-   ESP32-S3 -> 锁控 MCU: `{\"command\": \"UNLOCK\"}\
`
-   锁控 MCU -> ESP32-S3: `{\"status\": \"UNLOCKED\", \"battery\": 85}\
`

### 3.3 现有模块的职责扩展

-   **`Application` (应用核心)**:
    -   需要持有 `LockController` 的实例（通过 `Board` 基类获取）。
    -   在主事件循环中处理来自 `LockController` 的事件（如门锁状态变化），并更新 UI。
    -   响应来自其他模块的请求（如 MCP 远程开锁），并调用 `LockController` 的方法执行相应操作。
-   **`McpServer` (远程控制)**:
    -   需要注册新的“工具”，以允许远程 App 控制门锁。例如：
    -   `door.unlock`: 调用 `lock_controller->UnlockDoor()`。
    -   `door.get_status`: 返回 `lock_controller->GetLockStatus()`。
-   **UI (LVGL)**:
    -   需要在主界面上增加一个用于显示门锁状态的图标（例如一个锁的标志）。
    -   当 `Application` 收到门锁状态更新事件时，调用 UI 函数改变该图标的状态（开/关）。

### 3.4 可行性分析

-   **硬件可行性**: `bread-compact-wifi-s3cam` 平台已验证拥有摄像头、屏幕、音频等所有基础硬件能力。关键的 **UART 通信引脚 (GPIO 43, 44) 是空闲且可用的**，完全满足与第二颗芯片通信的需求。
-   **软件可行性**: 项目的软件架构设计出色。`Board` 硬件抽象层、`Application` 状态机、`DeviceStateEventManager` 事件系统以及 `McpServer` 远程工具框架，为我们添加新功能提供了所有必要的“钩子”和清晰的模式，无需对核心代码进行破坏性修改。
-   **工作量评估**: 工作量中等。主要集中在新 `LockController` 模块的开发、`Application` 状态逻辑的调整以及新 MCP 工具的添加。由于大量基础功能可复用，开发周期是可控的。

---

## 4. 详细实施计划

### 4.1 阶段一：创建新板型与锁控通信模块

**目标**: 搭建基础框架，实现 ESP32-S3 能通过 UART 发送指令。

1.  **创建目录和文件**:

    -   在 `main/boards/` 下创建新目录 `cat_eye_lock_s3`。
    -   在该目录下创建 `cat_eye_lock_s3_board.h` 和 `cat_eye_lock_s3_board.cc`。
    -   在 `main/` 下创建新目录 `modules`，并在其中创建 `lock_controller` 子目录及对应的 `.h`/`.cc` 文件。

2.  **实现 `CatEyeLockS3Board`**:

    -   在 `cat_eye_lock_s3_board.h` 中，定义 `CatEyeLockS3Board` 类，使其公有继承自 `CompactWifiBoardS3Cam`。
    -   增加一个 `LockController` 的 `std::unique_ptr` 成员。
    -   重写 `Board` 的虚函数，如 `GetBoardType()` 返回 `"cat_eye_lock_s3"`。
    -   在构造函数中，初始化父类后，创建并初始化 `lock_controller_` 实例。
    -   提供一个新的 `GetLockController()` 方法。

3.  **实现 `LockController` (初版)**:

    -   在 `lock_controller.h` 中定义 `LockController` 类和门锁状态枚举。
    -   在 `lock_controller.cc` 中，实现构造函数，在其中完成 UART 的初始化 (`uart_driver_install`, `uart_param_config`)。
    -   实现 `UnlockDoor()` 方法，该方法仅通过 `uart_write_bytes` 发送一个简单的开锁指令（如 `"UNLOCK\n"`）。
    -   此时，接收任务可以暂时不实现。

4.  **修改 `Board` 基类**:
    -   在 `main/boards/common/board.h` 的 `Board` 类中，添加一个虚函数 `virtual LockController* GetLockController() { return nullptr; }`。这使得 `Application` 可以安全地从任何板型实例中尝试获取 `LockController`。

### 4.2 阶段二：整合到应用核心与 UI

**目标**: 将门锁状态集成到应用主逻辑和 UI 显示中。

1.  **修改 `Application`**:

    -   在 `Application::Start()` 中，通过 `Board::GetInstance()->GetLockController()` 获取 `lock_controller` 实例。如果实例存在，则进行后续操作。
    -   为 `LockController` 添加状态变更的回调函数，并在 `Application` 中注册一个回调，当收到锁状态更新时，使用 `Schedule()` 将 UI 更新任务抛给主循环。

2.  **修改 UI**:

    -   在 `ui_main.cc` (或相关 UI 文件) 中，添加一个 `lv_obj_t*` 用于表示锁图标。
    -   创建一个 `ui_update_lock_status(LockStatus status)` 函数，根据传入的状态切换图标样式。
    -   `Application` 的回调函数最终会调用此 UI 函数。

3.  **实现 `LockController` 接收任务**:
    -   在 `LockController` 中，创建一个 `uart_rx_task` FreeRTOS 任务。
    -   该任务循环等待 `uart_read_bytes`，读取来自锁控 MCU 的数据，解析协议，并在状态变化时调用已注册的回调函数。

### 4.3 阶段三：实现远程控制

**目标**: 能够通过手机 App 等远程客户端开锁。

1.  **创建 MCP 工具类**:

    -   创建一个新文件 `lock_mcp_tool.h`/`.cc`。
    -   定义 `LockMcpTool` 类，其构造函数接收 `LockController` 实例。
    -   在构造函数中，使用 `McpServer::GetInstance()->AddTool()` 注册 `door.unlock` 工具。该工具的回调函数直接调用 `lock_controller_->UnlockDoor()`。

2.  **在 `Application` 中实例化工具**:
    -   在 `Application::Start()` 中，如果 `lock_controller` 实例有效，则创建 `LockMcpTool` 的实例。

### 4.4 阶段四：编译、配置与测试

**目标**: 使新板型可被编译和烧录。

1.  **修改 `main/Kconfig.projbuild`**:

    -   在 `BOARD_TYPE` 的 `choice` 中，增加一个新的 `config BOARD_CAT_EYE_LOCK_S3` 选项，并设置其描述为 "智能猫眼门锁 (ESP32-S3)"。

2.  **修改 `main/CMakeLists.txt`**:

    -   在 `if (CONFIG_BOARD_BREAD_COMPACT_WIFI_S3CAM)` 的逻辑块之后，增加一个类似的 `elseif (CONFIG_BOARD_CAT_EYE_LOCK_S3)` 块。
    -   在这个新块中，添加 `cat_eye_lock_s3` 板卡的源文件和所有新模块的源文件到 `main_srcs` 变量中。
    -   设置 `BOARD_NAME` 宏定义为 `"cat_eye_lock_s3"`。

3.  **编译与烧录**:
    -   运行 `idf.py menuconfig`。
    -   导航至 `Xiaozhi Assistant -> Board Type`，选择新增的 `智能猫眼门锁 (ESP32-S3)`。
    -   保存配置，然后运行 `idf.py build flash`。

---

## 5. 核心功能难点详析

### 5.1 实时音视频传输：基于 WebSocket

为了实现流畅的远程监控和双向对讲，我们需要一个高效、低延迟的音视频同步传输方案。WebSocket 是实现这一目标的理想选择。

#### 5.1.1 方案选择：为何使用 WebSocket？

-   **全双工通信**: 允许服务器（ESP32）向客户端（App）主动、连续地推送音视频数据的同时，也接收客户端发来的音频数据或控制指令。
-   **低开销**: 一旦连接建立，后续的数据帧传输开销远小于 HTTP 请求/响应模型，延迟更低。
-   **穿透性好**: 与 HTTP 一样工作在 80/443 端口，能很好地穿透大多数网络防火墙。

#### 5.1.2 数据压缩策略

-   **视频压缩：JPEG**: ESP32-S3 对 JPEG 编码有良好的硬件加速支持，传输 JPEG 帧序列（MJPEG）是目前最现实、最高效的方案。
-   **音频压缩：Opus**: 项目已集成`esp-opus-encoder`，其低延迟、高压缩率的特性是实时语音的绝佳选择。

#### 5.1.3 WebSocket 通信协议设计

**消息帧格式 (Binary Frame):**
| 字节偏移 | 长度 (Bytes) | 字段名 | 描述 |
| :------- | :----------- | :------------ | :------------------------------------------- |
| 0 | 1 | `FrameType` | `0x01`=视频(JPEG), `0x02`=音频(Opus) |
| 1 | 4 | `Timestamp` | 32 位毫秒时间戳，用于音视频同步。 |
| 5 | 4 | `PayloadSize` | 32 位无符号整数，表示后续载荷的字节长度。 |
| 9 | `PayloadSize`| `Payload` | 实际的 JPEG 或 Opus 数据。 |

#### 5.1.4 性能与带宽评估

-   **视频部分**:
-   **分辨率**: 推荐使用 **QVGA (320x240)**。此分辨率下，一张中等质量的 JPEG 图像约 **8-15 KB**。
-   **帧率**: 目标设定在 **10-15 FPS**。
-   **带宽估算**: `12 KB/frame * 15 FPS * 8 bits/byte ≈ 1.44 Mbps`。

-   **音频部分**:
-   **采样率**: 16kHz, 单声道。
-   **Opus 码率**: 约 **32 kbps**。

-   **总计与结论**:
-   **总上传带宽**: `1.44 Mbps (视频) + 0.032 Mbps (音频) ≈ 1.5 Mbps`。
-   **性能评估**: ESP32-S3 的 CPU 需要同时处理 Wi-Fi 协议栈、捕获摄像头、JPEG 编码、捕获麦克风、Opus 编码和 WebSocket 封包。**QVGA @ 10-15 FPS** 是一个性能和体验之间比较均衡的、可以努力实现的现实目标。若追求更高分辨率如 VGA(640x480)，帧率可能会下降到 5-8 FPS，且对 Wi-Fi 信号质量要求更高，可能导致延迟和卡顿。

#### 5.1.5 实施步骤更新

1. **创建`AVStreamer`模块**: 新建`av_streamer.h/.cc`，负责管理 WebSocket 服务器和音视频流任务。
2. **实现 WebSocket 服务器**:

-   在`AVStreamer`中，使用`esp_http_server`组件创建 HTTP 服务器，并添加一个 WebSocket 升级端点（如`/ws`）。
-   设置`httpd_ws_frame_t`的处理回调，用于接收来自 App 的音频数据或控制信令。

3. **创建`streaming_task`核心任务**:

-   当 WebSocket 连接建立后，`AVStreamer`创建此核心任务。
-   **任务循环**:
-   **视频**: 定时（如每秒 15 次）从`Esp32Camera`获取一帧图像，进行 JPEG 编码，封装成上述协议的视频帧，通过`httpd_ws_send_frame()`发送。
-   **音频**: 从`AudioService`获取一个音频块（如 20ms 的 PCM 数据），进行 Opus 编码，封装成音频帧，发送出去。
-   两个过程需要在一个任务中协调进行，以避免资源争抢。

4. **与`Application`集成**:

-   `Application`中增加`kDeviceStateStreaming`和`kDeviceStateTwoWayAudio`状态。
-   当需要开始推流时（如收到远程 MCP 指令`stream.start`），`Application`调用`AVStreamer::Start()`并进入相应状态。
-   当 WebSocket 断开时，`AVStreamer`的任务结束，并通知`Application`返回`kDeviceStateIdle`状态。

#### 5.1.6 健壮性设计 (错误处理)

-   **发送阻塞**: `httpd_ws_send_frame()` 可能因网络拥堵而阻塞或失败。策略应为设置一个合理的超时时间，若超时则主动丢弃当前帧，以保证后续帧的实时性。
-   **客户端断连**: 必须正确处理 WebSocket 的关闭事件。在连接关闭时，确保`streaming_task`被终止，所有相关内存被释放，并通知`Application`更新状态，防止内存泄漏。
-   **心跳机制**: 可增加一个心跳机制，服务器定时向客户端发送 ping 帧，若在规定时间内未收到 pong 回应，则认为连接已死，主动关闭连接以释放资源。

### 5.2 多源触发与统一事件处理模型 (深化版)

智能猫眼的核心价值在于能够对各种内外部事件做出正确、智能的响应。为此，我们需要建立一个健壮、可扩展、逻辑闭环的事件处理系统。

#### 5.2.1 详细触发源定义

我们将所有事件的源头归为三类，并定义出具体的、可操作的事件类型：

**第一类：物理/传感器触发 (来自锁控 MCU)**
这些是最高优先级的硬件事件，由锁控 MCU 通过 UART 上报。

| 事件名 (Enum)               | 触发条件                      | 附带数据 (Payload)                  | 预期动作                                      |
| :-------------------------- | :---------------------------- | :---------------------------------- | :-------------------------------------------- |
| `EVENT_DOORBELL_PRESSED`    | 门铃物理按键被按下            | 无                                  | **核心场景**: 抓拍、人脸识别、呼叫 App        |
| `EVENT_LOCK_UNLOCKED`       | 门锁被打开                    | `method` (指纹/密码/钥匙), `userId` | 记录开锁日志，向 App 推送通知                 |
| `EVENT_LOCK_LOCKED`         | 门锁被关闭/反锁               | `method` (自动/手动)                | 记录日志，更新 UI 状态                        |
| `EVENT_DOOR_AJAR_DETECTED`  | 门未关严（门磁触发）          | 无                                  | 播放“门未关好”提示音，向 App 推送警报         |
| `EVENT_TAMPER_ALARM`        | 锁体被暴力撬动                | 无                                  | 立即本地高音警报，抓拍并向 App 推送最高级警报 |
| `EVENT_PIR_MOTION_DETECTED` | 门口 PIR 传感器检测到人形移动 | 无                                  | 低功耗唤醒，准备抓拍或进入警戒状态            |

**第二类：语音指令触发 (本地)**
由`AudioService`在识别到特定语音后产生。

| 事件名 (Enum)                | 触发条件                 | 声纹要求                     | 预期动作                           |
| :--------------------------- | :----------------------- | :--------------------------- | :--------------------------------- |
| `EVENT_COMMAND_UNLOCK`       | 识别到“开门”等指令       | **必须**是已注册的“主人”声纹 | 调用`LockController`开锁           |
| `EVENT_COMMAND_START_VIEW`   | 识别到“看看门口”等指令   | “主人”声纹                   | 启动屏幕，显示摄像头画面           |
| `EVENT_COMMAND_QUERY_STATUS` | 识别到“门锁好了吗”等指令 | “主人”声纹                   | 查询`LockController`状态并语音播报 |

**第三类：远程指令触发 (来自云端 App)**
由`McpServer`或 WebSocket 连接接收到远程指令后产生。

| 事件名 (Enum)               | 触发条件             | 附带数据 (Payload)              | 预期动作                 |
| :-------------------------- | :------------------- | :------------------------------ | :----------------------- |
| `EVENT_REMOTE_UNLOCK`       | App 点击“开锁”按钮   | `token` (用于鉴权)              | 调用`LockController`开锁 |
| `EVENT_REMOTE_START_STREAM` | App 请求开始实时对讲 | `token`                         | 调用`AVStreamer`开始推流 |
| `EVENT_REMOTE_STOP_STREAM`  | App 挂断对讲         | `token`                         | 调用`AVStreamer`停止推流 |
| `EVENT_REMOTE_GET_LOGS`     | App 请求访客记录     | `token`, `startTime`, `endTime` | 查询存储的日志并返回     |

#### 5.2.2 逻辑闭环的事件处理流程

整个系统的运行逻辑，是围绕一个位于`Application`类中、**基于 FreeRTOS 消息队列的中央事件循环**来构建的。此模型确保了所有事件都被异步、顺序地处理，形成一个完美的逻辑闭环。

**流程图解:**
`[事件源] -> [模块] -> [Event Queue] -> [Application Loop] -> [Action Manager] -> [执行/状态变更] -> (产生新事件) -> [Event Queue]`

**Step 1: 事件产生与入队 (解耦)**

-   **触发**: 任意一个事件源被触发（如 UART 收到`"doorbell_pressed"`数据）。
-   **封装**: 对应的模块（如`LockController`）**只负责解析，不处理业务逻辑**。它的唯一任务是创建一个包含事件类型和数据的结构体（如 `AppEvent event = { .type = EVENT_DOORBELL_PRESSED };`）。
-   **入队**: 该模块立即将此`AppEvent`结构体发送到全局唯一的、线程安全的 FreeRTOS 消息队列中 (`xQueueSend`)。然后该模块的任务结束，继续等待下一次触发。

**Step 2: 事件分发 (`Application`主循环)**

-   **消费**: `Application`的主任务是该消息队列的**唯一消费者**。它在`while(1)`循环中永久阻塞等待 (`xQueueReceive`)。
-   **分发**: 一旦收到事件，主循环被唤醒。一个巨大的`switch (event.type)`语句开始工作，它像一个交通警察，将事件分发给正确的处理者。
-   **委托**: 对于需要复杂决策的场景（几乎所有猫眼核心场景），`Application` **将事件委托给`CatEyeActionManager`处理**，以保持自身的整洁。
    ```cpp
    // Application::MainEventLoop() 伪代码
    while(true) {
      AppEvent event;
      if (xQueueReceive(g_app_event_queue, &event, portMAX_DELAY)) {
        switch (event.type) {
          case EVENT_DOORBELL_PRESSED:
            m_action_manager->HandleDoorbellPressed(); // 委托
            break;
          // ... 其他事件
        }
      }
    }
    ```

**Step 3: 业务决策 (`CatEyeActionManager`状态机)**

-   `CatEyeActionManager`是猫眼业务逻辑的“大脑”，它内部维护着一个状态机（如 `m_current_state`）。
-   **示例 `HandleDoorbellPressed()`**:
    1. 检查当前状态，如果正在通话中 (`STATE_STREAMING`)，则直接忽略或播放忙音后返回。
    2. 将设备状态切换为 `STATE_RECOGNIZING`。
    3. 调用 UI 模块，在屏幕上显示“正在识别...”。
    4. 调用`FaceRecognizer`模块的异步识别接口 `Recognize()`，并传入一个**回调函数 (Lambda)**。`Recognize()`会立即返回，不会阻塞。

**Step 4: 异步回调与闭环的关键 (再次入队)**

-   **回调触发**: 几百毫秒或几秒后，人脸识别完成（无论是成功、失败还是超时），`FaceRecognizer`会执行`Recognize()`时传入的回调函数。
-   **产生新事件**: 这个回调函数的**唯一职责**，是根据识别结果，创建并封装一个**新的事件**，例如 `AppEvent event = { .type = EVENT_FACE_RECOGNIZED, .user_id = "Host" };`。
-   **再次入队**: 将这个新事件再次推入**同一个全局消息队列** (`xQueueSend`)。**这是实现逻辑闭环最关键的一步**，它将异步操作的结果重新带回到了同步的、顺序的事件处理流程中。

**Step 5: 结果处理与状态落地**

-   **再次消费**: `Application`的主循环接收到这个新的`EVENT_FACE_RECOGNIZED`事件。
-   **再次分发**: `switch`语句将其分发给`m_action_manager->HandleFaceRecognized(event.user_id)`。
-   **最终执行**: `ActionManager`根据人脸识别结果，执行最终动作：
    -   如果是“主人”，则播放“欢迎主人回家”语音，并调用`LockController`开锁。
    -   如果是“陌生人”，则立即调用`AVStreamer`开始向主人的 App 进行视频推流。
-   **状态落地**: 所有动作执行完毕后，`ActionManager`将设备状态切换到一个新的稳定状态（如`STATE_IDLE`或`STATE_STREAMING`）。

至此，一个由“门铃按下”触发的复杂场景，经过队列、分发、异步调用、回调再入队、最终处理等一系列步骤，得到了完整的、非阻塞的处理。系统恢复平静，等待下一个事件的到来。这个流程确保了任何事件都能被妥善处理，并且逻辑清晰、易于扩展。

### 5.3 核心功能扩展：基于 MCP 的云端人脸识别

人脸识别将通过 MCP 调用云端服务完成，以获取更强的性能。

#### 5.3.1 架构职责划分

-   **ESP32-S3**: 负责采集图像、Base64 编码，并通过 MCP 调用云端工具，最后根据返回结果执行动作。
-   **云端/服务器**: 实现`face.recognize`和`face.enroll`等 MCP 工具，内部调用专业 AI 服务。

#### 5.3.2 `FaceRecognizer`模块重构

模块职责变为封装“拍照 -> 编码 -> 调用 MCP -> 解析结果”的**异步流程**，并向上层提供带有回调函数的简单接口。

#### 5.3.3 融入`Application`的异步事件处理流程

`Application`调用`FaceRecognizer::Recognize()`并发起识别后，不会阻塞等待，而是传入一个回调函数。当云端结果返回时，该回调函数被触发，并通过`Application::Schedule()`将后续的决策逻辑安全地抛回主事件循环中执行。

## 6\. 产品级增强方案

### 6.1 资源管理：充分利用 `esp32s3n16r8` 的硬件优势

目标硬件拥有 **16MB Flash** 和 **8MB PSRAM**，这是流畅运行视频应用的关键。

-   **Flash (16MB)**: 空间充裕，可轻松容纳大型固件、OTA 升级分区以及用于存储铃声、提示音、字体等资源的 SPIFFS/FAT 分区。
-   **PSRAM (8MB)**: 必须被高效利用，以减轻内部 SRAM（约 512KB）的巨大压力。

    -   **实施**: 所有大型内存块都应**显式地从 PSRAM 中分配**。

````cpp
        // 示例：为摄像头帧缓冲区分配内存
        uint8\_t\* frame\_buffer = (uint8\_t\*) heap\_caps\_malloc(buffer\_size, MALLOC\_CAP\_SPIRAM);
        ```

  * **建议分配在PSRAM中的对象**:

    * 摄像头帧缓冲区 (`camera frame buffer`)
    * JPEG编码的目标缓冲区
    * LVGL的显示缓冲区
    * `AVStreamer` 的WebSocket发送缓冲区

* **CPU**: `streaming\_task` 的任务优先级需仔细权衡，建议使用ESP-IDF性能分析工具进行监控和调优，确保实时性与系统稳定性的平衡。

---



##

````
