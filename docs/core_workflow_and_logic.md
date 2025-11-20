# 智能猫眼核心运行流程与逻辑详解

## 第1部分：系统启动及初始化流程

本部分详细描述了智能猫眼设备从上电瞬间到进入`STATE_IDLE`（空闲待机）状态的全过程。这个过程是系统能否正常工作的基石，它负责有序地“唤醒”所有硬件和服务。

### 1.1 启动入口: `app_main`

与所有 ESP-IDF 项目一样，`app_main()` 函数是代码的执行入口。它的职责非常专一和高层：

1.  **初始化底层服务**:
    -   `nvs_flash_init()`: 初始化非易失性存储（NVS）。用于存放Wi-Fi凭据、设备配置等需要持久化的数据。代码中包含了对NVS损坏的自动修复逻辑（擦除并重新初始化）。
    -   `esp_event_loop_create_default()`: 创建系统默认的事件循环。这是ESP-IDF中Wi-Fi、TCP/IP等底层组件进行异步事件通知的基础。

2.  **启动应用层**:
    -   获取 `Application` 类的单例对象。
    -   调用其核心的 `Start()` 方法，将控制权正式移交给应用层。

```cpp
// main/main.cc 伪代码
extern "C" void app_main(void)
{
    // 初始化 NVS，用于持久化存储
    ESP_ERROR_CHECK(nvs_flash_init());

    // 创建系统默认事件循环，供底层组件使用
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 关键：获取应用单例并启动整个应用层
    Application::GetInstance().Start();
}
```

### 1.2 核心初始化序列: `Application::Start()`

`Application::Start()` 方法是整个系统的“点火开关”，它严格按照依赖关系，一步步地初始化所有模块和服务。

```cpp
// main/application.cc 伪代码
void Application::Start()
{
    // --- 步骤 1: 设置初始状态并初始化基础硬件 ---
    setSystemState(STATE_BOOTING); // 明确告知系统，当前处于“正在启动”状态
    
    // 初始化板级支持包（BSP），它会进一步初始化屏幕、摄像头、音频Codec等
    auto& board = Board::GetInstance();
    board.Initialize(); 
    auto display = board.GetDisplay();
    display->showBootScreen(SystemInfo::GetFirmwareVersion()); // 在屏幕上显示启动画面和版本号

    // --- 步骤 2: 创建中央消息队列 ---
    // 这是整个事件驱动架构的核心，后续所有模块都将依赖它
    g_app_event_queue = xQueueCreate(QUEUE_LENGTH, sizeof(AppEvent));

    // --- 步骤 3: 初始化核心服务与管理器 ---
    // 此时硬件已就绪，队列已创建，可以初始化依赖它们的服务了
    m_audio_service = new AudioService(board.GetAudioCodec());
    m_storage_manager = new StorageManager();
    m_action_manager = new ActionManager(this); // 将 Application 自身指针传入，方便 ActionManager 回调
    // ... 其他管理器的初始化

    // --- 步骤 4: 启动核心后台任务 ---
    // 4.1 启动主事件循环任务，这是系统的“心脏”
    xTaskCreate(
        [](void* arg) { ((Application*)arg)->MainEventLoop(); },
        "main_event_loop", 
        8192, // 较大的栈空间以应对复杂的事件处理
        this, 
        5,    // 中等优先级
        &m_main_event_loop_handle
    );

    // 4.2 启动其他需要独立任务的模块，例如与锁控MCU通信的LockController
    m_lock_controller = new LockController();
    m_lock_controller->Start(); // LockController内部会创建自己的UART接收任务

    // --- 步骤 5: 启动网络并检查初始状态 ---
    // 启动网络连接，这是一个异步过程。连接结果将通过一个事件来通知。
    board.StartNetwork(); 

    // 检查SD卡状态，这也是一个异步过程（或者快速的同步过程）
    // 检查完毕后，StorageManager会发送第一个事件到队列
    m_storage_manager->CheckStorageState(); // 这会发送 SYS_NOTIFY_STORAGE_STATE_CHANGED 事件

    // --- 步骤 6: 进入主循环，等待系统稳定 ---
    // Start() 函数到此基本执行完毕。但系统还未完全就绪。
    // MainEventLoop 已经开始运行，它会处理队列中的网络和存储状态事件。
    // ActionManager 在处理这些事件时，会判断所有启动条件是否满足（如网络已连接、SD卡正常）。
    // 当所有条件都满足后，ActionManager 会最终调用 setSystemState(STATE_IDLE)。
    
    ESP_LOGI(TAG, "System initialization sequence started. Waiting for services to become ready...");
    // 此时，屏幕上可能仍在显示“正在启动...”，直到收到状态切换到 IDLE 的指令。
}
```

### 1.3 初始化完成与进入待机

`Application::Start()` 函数本身是同步执行的，但它启动了多个**异步**的过程（如Wi-Fi连接）。系统并不是在 `Start()` 返回后就立刻可用。

真正的“初始化完成”是在 `MainEventLoop` 中，由 `ActionManager` 在确认所有必要服务（特别是网络）都就绪后，通过调用 `setSystemState(STATE_IDLE)` 来完成的。

这个调用的发生，标志着系统所有部分各就各位，初始化流程正式结束，系统进入第一个稳定的工作状态——**空闲待机**，并开始响应来自外部世界的各种事件。

## 第2部分：主事件循环与状态机核心机制

如果说初始化是系统的“序曲”，那么本部分将深入剖析系统的“主歌”——即系统在日常运行时，是如何通过主事件循环和状态机来协同工作的。

### 2.1 MainEventLoop: 系统的脉搏与调度中心

`MainEventLoop` 是一个死循环，但它通过阻塞式地等待消息队列，实现了极高的运行效率。它是系统所有活动的唯一入口，确保了决策的串行化。

```cpp
// main/application.cc 伪代码 - 最终版
void Application::MainEventLoop() {
    AppEvent event;

    while (true) {
        // 1. 阻塞式等待新事件。只要队列为空，该任务就处于休眠状态，不消耗CPU。
        if (xQueueReceive(g_app_event_queue, &event, portMAX_DELAY) == pdPASS) {
            
            // 2. 收到事件后，进行日志记录，这对于调试至关重要。
            ESP_LOGD(TAG, "Event Dispatched: %s", eventToString(event.type));

            // 3.【关键】作为“智能调度中心”，根据事件类型将事件分发给最合适的“专家”模块。
            switch (event.type) {
                // =========================================================
                // 核心安防与业务决策类事件 -> 交给 ActionManager
                // =========================================================
                case HW_NOTIFY_DOORBELL_PRESSED:
                case HW_NOTIFY_PIR_MOTION_DETECTED:
                case HW_NOTIFY_TAMPER_ALARM:
                case VOICE_NOTIFY_WAKE_WORD_DETECTED:
                case RESULT_FACE_RECOGNITION_COMPLETED: // 识别结果需要 ActionManager 做最终决策
                case REMOTE_CMD_UNLOCK: // 远程开锁是核心安防指令
                // 状态通知也由 ActionManager 统一处理，以便更新UI或上报
                case HW_NOTIFY_LOCK_STATE_CHANGED:
                case SYS_NOTIFY_NETWORK_STATE_CHANGED:
                case HW_NOTIFY_LOW_BATTERY_ALERT:
                    m_action_manager->processEvent(event);
                    break;

                // =========================================================
                // 音视频流控制类事件 -> 交给 StreamManager
                // =========================================================
                case REMOTE_CMD_START_STREAM:
                case REMOTE_CMD_STOP_STREAM:
                case SYS_NOTIFY_STREAMING_STOPPED:
                    m_stream_manager->processEvent(event);
                    break;

                // =========================================================
                // 存储控制类事件 -> 交给 StorageManager
                // =========================================================
                case CMD_VIDEO_START_RECORDING:
                case CMD_VIDEO_STOP_RECORDING:
                case RESULT_VIDEO_RECORDING_COMPLETED:
                case SYS_NOTIFY_STORAGE_ERROR:
                    m_storage_manager->processEvent(event);
                    break;

                // ... 其他事件可以路由给其他专门的管理器 ...

                default:
                    // 对于未被路由的事件，进行日志记录
                    ESP_LOGW(TAG, "Unhandled event route in MainEventLoop: %d", event.type);
                    break;
            }
        }
    }
}
```
这个循环的核心思想是**“保持主循环的极度专一，使其成为一个高效的智能路由器”**。它根据事件的“主题”将“信件”派发给不同的“部门”，每个部门都是处理自己领域事务的专家。这种方式实现了高度的“关注点分离”，是构建大型、可维护系统的关键。

### 2.2 ActionManager: 业务逻辑的决策大脑

`ActionManager` 是所有核心业务逻辑的汇集点。它接收 `MainEventLoop` 转发来的所有事件，并根据**“当前系统状态”**和**“事件内容”**共同决定下一步的行动。

```cpp
// main/action_manager.cc 伪代码
void ActionManager::processEvent(const AppEvent& event) {
    // 可以在这里做一个顶层的、对所有事件都有效的预处理。
    // 例如，如果设备处于致命错误状态，则忽略几乎所有事件。
    if (m_current_state == STATE_FATAL_ERROR && event.type != REMOTE_CMD_REBOOT) {
        return; 
    }

    // 根据事件类型，调用具体的处理函数
    switch (event.type) {
        case HW_NOTIFY_DOORBELL_PRESSED:
            handleDoorbellPressed();
            break;
        case RESULT_FACE_RECOGNITION_COMPLETED:
            handleFaceRecognitionResult(event.payload.recognition_result);
            break;
        // ... 其他事件的处理
    }
}
```

### 2.3 setSystemState: 状态切换的原子操作与“守门人”

`setSystemState` 函数是整个状态机设计的精髓所在。它不仅仅是改变一个变量的值，而是**定义和执行状态切换时的“仪式”**，确保系统在进入和退出一个状态时，其所需的硬件资源和软件行为都得到正确地配置。这使得每个状态都成为一个边界清晰、行为可预测的“沙箱”。

```cpp
// main/action_manager.cc 伪代码
void ActionManager::setSystemState(SystemState newState) {
    // 如果目标状态与当前状态相同，则无需执行任何操作，直接返回。
    if (m_current_state == newState) {
        return;
    }

    ESP_LOGI(TAG, "State Transition: from %s to %s", stateToString(m_current_state), stateToString(newState));

    // =============================================================
    // 阶段一：执行“退出”当前状态 (Exit-Actions) 的清理工作
    // 目的是确保将要离开的状态所占用的资源被正确释放或停用。
    // =============================================================
    switch (m_current_state) {
        case STATE_STREAMING:
            // 如果我们正要结束“通话”状态，那么必须关闭摄像头和音频流。
            m_camera->stopStreaming();
            m_audio_service->stopStreaming();
            ESP_LOGI(TAG, "Exit action from STATE_STREAMING: Camera and audio stream stopped.");
            break;
        
        case STATE_LISTENING_FOR_COMMAND:
            // 如果我们正要结束“聆听指令”状态，需要关闭麦克风上传。
            m_audio_service->stopCloudStreaming();
            break;
        
        // ... 其他状态的退出逻辑
        default:
            break;
    }

    // =============================================================
    // 阶段二：原子地更新状态变量
    // 这是状态切换的真正瞬间。
    // =============================================================
    m_current_state = newState;
    
    // （可选）可以发送一个系统通知事件，让其他非核心模块（如日志、统计）知道状态变了
    // AppEvent state_change_event = { .type = SYS_NOTIFY_STATE_CHANGED, .payload = { .new_state = newState } };
    // xQueueSend(g_app_event_queue, &state_change_event, 0);

    // =============================================================
    // 阶段三：执行“进入”新状态 (Entry-Actions) 的准备工作
    // 目的是为新状态的正常运作，准备好所有必要的硬件和软件条件。
    // =============================================================
    switch (newState) {
        case STATE_IDLE:
            // 当系统进入“空闲”状态时，
            // 1. 开启低功耗的唤醒词检测。
            m_audio_service->enableWakeWordDetection(true);
            // 2. 关闭摄像头电源，以节省功耗。
            m_camera->powerDown();
            // 3. 更新UI到待机界面。
            m_ui->showIdleScreen();
            ESP_LOGI(TAG, "Entry action for STATE_IDLE: Wake word enabled, camera powered down.");
            break;

        case STATE_STREAMING:
            // 当系统准备要进入“通话”状态时，
            // 1. 关闭唤醒词检测，避免通话时误触发。
            m_audio_service->enableWakeWordDetection(false);
            // 2. 开启摄像头电源。
            m_camera->powerUp();
            // 3. 准备好音频的编码和解码器。
            m_audio_service->prepareForStreaming();
            // 4. 更新UI到通话界面。
            m_ui->showStreamingScreen();
            ESP_LOGI(TAG, "Entry action for STATE_STREAMING: Wake word disabled, camera powered up.");
            break;
        
        case STATE_FATAL_ERROR:
            // 当系统进入“致命错误”状态时，
            // 1. 停止所有后台任务。
            stopAllBackgroundTasks();
            // 2. 在屏幕上显示一个无法关闭的错误信息。
            m_ui->showFatalError("System Core Failure. Please reboot.");
            break;

        // ... 其他状态的进入逻辑
        default:
            break;
    }
}
```
通过这种**“退出旧态 -> 切换状态 -> 进入新态”**的三段式原子操作，`setSystemState`函数成为了系统行为的“守门人”。它确保了无论状态如何跳转，系统的资源配置和行为模式总是一致且可预测的，极大地增强了系统的健壮性。

## 第3部分：“事件”的生命周期 - 流转与处理机制

前一部分我们聚焦于“状态”，这一部分我们将聚焦于“事件”本身。我们将跟踪一个事件从硬件信号到被最终消费的全过程，来深入理解事件驱动的“驱动”二字是如何体现的。

### 3.1 事件的产生 (Production) - 将物理信号转化为标准数据

事件的生命周期始于“生产者”模块。这些模块是系统的感觉器官，负责感知外部或内部的变化，并将其“翻译”成标准的`AppEvent`结构体。以`LockController`为例，它负责与锁控MCU通信。

```cpp
// modules/lock_controller.cc 伪代码

/**
 * @brief LockController的核心任务，在后台独立运行。
 */
void LockController::uartRxTask() {
    // 设置一个缓冲区用于接收来自MCU的串口数据
    char buffer[UART_BUFFER_SIZE];

    while (true) {
        // 1. 阻塞式地等待串口数据。任务在此处挂起，不消耗CPU。
        int length = uart_read_bytes(m_uart_port, buffer, UART_BUFFER_SIZE - 1, portMAX_DELAY);

        if (length > 0) {
            buffer[length] = '\0'; // 确保字符串结束

            // 2. 解析收到的数据（例如，一个JSON字符串）
            // 假设收到的数据为: {"event": "doorbell_pressed"}
            JsonDocument doc;
            if (deserializeJson(doc, buffer) == DeserializationError::Ok) {
                const char* event_name = doc["event"];

                // 3. 将原始信号“翻译”为标准的 AppEvent
                if (strcmp(event_name, "doorbell_pressed") == 0) {
                    // a. 创建一个事件结构体实例
                    AppEvent event;
                    event.type = HW_NOTIFY_DOORBELL_PRESSED;
                    // b. （可选）填充 payload 数据
                    // 对于门铃事件，payload可能为空

                    // c.【关键】将标准化的事件发送到全局中央队列
                    xQueueSend(g_app_event_queue, &event, 0);
                } 
                else if (strcmp(event_name, "lock_state_changed") == 0) {
                    AppEvent event;
                    event.type = HW_NOTIFY_LOCK_STATE_CHANGED;
                    // 从JSON中解析详细信息并填充payload
                    event.payload.lock_state.is_locked = doc["locked"];
                    event.payload.lock_state.battery_percent = doc["battery"];
                    
                    xQueueSend(g_app_event_queue, &event, 0);
                }
                // ... 处理其他来自MCU的事件
            }
        }
    }
}
```
**核心思想**：生产者的职责非常纯粹，它作为系统与其他硬件/网络模块的“绝缘层”，只负责**感知->翻译->发送**，从不关心这个事件后续会如何被处理。

### 3.2 事件的调度 (Dispatching) - 智能路由

`MainEventLoop` 在从队列中取出事件后，将扮演一个“智能路由器”的角色。它检查事件的类型，并根据预设的规则，将其分发给最适合处理该事件的“专家”模块。这一步保证了无论事件来自何处，都会被分发到唯一正确的处理模块，实现了清晰的责任划分。

```cpp
// main/application.cc MainEventLoop 伪代码 - 最终版
void Application::MainEventLoop() {
    AppEvent event;

    while (true) {
        // 阻塞式等待新事件
        if (xQueueReceive(g_app_event_queue, &event, portMAX_DELAY) == pdPASS) {
            
            ESP_LOGD(TAG, "Event Dispatched: %s", eventToString(event.type));

            // 作为“智能调度中心”，根据事件类型将事件分发给最合适的“专家”模块
            switch (event.type) {
                // =========================================================
                // 核心安防与业务决策类事件 -> 交给 ActionManager
                // =========================================================
                case HW_NOTIFY_DOORBELL_PRESSED:
                case HW_NOTIFY_PIR_MOTION_DETECTED:
                case HW_NOTIFY_TAMPER_ALARM:
                case VOICE_NOTIFY_WAKE_WORD_DETECTED:
                case RESULT_FACE_RECOGNITION_COMPLETED: // 识别结果需要 ActionManager 做决策
                case REMOTE_CMD_UNLOCK: // 远程开锁是核心安防指令
                // 状态通知也由 ActionManager 统一处理，以便更新UI或上报
                case HW_NOTIFY_LOCK_STATE_CHANGED:
                case SYS_NOTIFY_NETWORK_STATE_CHANGED:
                case HW_NOTIFY_LOW_BATTERY_ALERT:
                    m_action_manager->processEvent(event);
                    break;

                // =========================================================
                // 音视频流控制类事件 -> 交给 StreamManager
                // =========================================================
                case REMOTE_CMD_START_STREAM:
                case REMOTE_CMD_STOP_STREAM:
                case SYS_NOTIFY_STREAMING_STOPPED:
                    m_stream_manager->processEvent(event);
                    break;

                // =========================================================
                // 存储控制类事件 -> 交给 StorageManager
                // =========================================================
                case CMD_VIDEO_START_RECORDING:
                case CMD_VIDEO_STOP_RECORDING:
                case RESULT_VIDEO_RECORDING_COMPLETED:
                case SYS_NOTIFY_STORAGE_ERROR:
                    m_storage_manager->processEvent(event);
                    break;

                // ... 其他事件可以路由给其他专门的管理器 ...

                default:
                    ESP_LOGW(TAG, "Unhandled event route in MainEventLoop: %d", event.type);
                    break;
            }
        }
    }
}
```

### 3.3 事件的消费与再创造 (Consumption & Re-creation)

这是体现“工作流”和“模块解耦”的关键一步。`ActionManager`在消费一个事件后，并不总是自己完成所有工作，而是常常通过创造并发送一个新的`CMD_*`（命令）事件，来“委托”其他专业模块去执行下一步。

```cpp
// main/action_manager.cc handleDoorbellPressed() 伪代码片段

void ActionManager::handleDoorbellPressed() {
    // 1. 消费事件：通过“状态守卫”来决定是否处理当前事件
    if (m_current_state != STATE_IDLE) {
        // 消费掉了，但决定忽略。
        return;
    }
    
    // 2. 状态转换：这是消费事件后产生的第一个“副作用”
    setSystemState(STATE_RECOGNIZING);
    
    // 3. 事件的“再创造”：根据业务逻辑，创造一个全新的“命令”事件
    //    这里的业务逻辑是：“门铃响了，我需要进行人脸识别”。
    ESP_LOGI(TAG, "Doorbell pressed. Creating a 'CMD_FACE_RECOGNIZE' event.");

    AppEvent recognize_cmd;
    recognize_cmd.type = CMD_FACE_RECOGNIZE;
    // 在payload中附上上下文信息：是“门铃”触发了这次识别
    recognize_cmd.payload.face_recognize_cmd.trigger_event = HW_NOTIFY_DOORBELL_PRESSED;

    // 4. 将新创造的命令事件，发送回【同一个】中央队列
    xQueueSend(g_app_event_queue, &recognize_cmd, 0);

    // handleDoorbellPressed 函数到此结束。它已经完成了自己的阶段性使命：
    // 响应门铃 -> 改变状态 -> 发出下一步指令。
}
```
通过这个流程，我们看到：
- `HW_NOTIFY_DOORBELL_PRESSED`事件在这里被**消费**。
- `CMD_FACE_RECOGNIZE`事件在这里被**再创造**。

`MainEventLoop`在下一个循环中，就会从队列里取出这个新的`CMD_FACE_RECOGNIZE`事件，并根据`switch`规则，将其分发给专门处理人脸识别的`FaceManager`，从而驱动整个工作流向前滚动。

这个**“消费->决策->再创造”**的模式，是整个事件驱动框架实现复杂、长链条、解耦业务逻辑的核心所在。

## 第4部分：“典型工作流”实例贯穿解析

本部分将通过两个最核心的、端到端的场景，将前述所有理论（初始化、状态机、事件流）融会贯通，展示系统在真实世界中的完整运作流程。

### 4.1 场景一：门铃呼叫（硬件触发 -> 异步云端 -> 远程呼叫）

这个场景是系统的“英雄场景”，它串联了硬件输入、状态切换、异步云端请求、内部指令、模块解耦和最终的远程交互，是检验整个架构的试金石。

**初始状态**: 系统处于 `STATE_IDLE`。

---

**Step 1: 事件产生 (`LockController`)**
- 用户按下门铃，`LockController`的`uartRxTask`接收到信号，创建并发送事件。

```cpp
// 伪代码: LockController::uartRxTask() 内部
// ...解析UART数据后...
AppEvent event;
event.type = HW_NOTIFY_DOORBELL_PRESSED;
xQueueSend(g_app_event_queue, &event, 0); // 发送到主队列
```

---

**Step 2: 首次分发与状态守卫 (MainEventLoop -> ActionManager)**
- `MainEventLoop` 从队列中取出事件，并将其分发给 `ActionManager`。

```cpp
// 伪代码: ActionManager::processEvent(event) 内部
case HW_NOTIFY_DOORBELL_PRESSED:
    handleDoorbellPressed(event);
    break;

// 伪代码: ActionManager::handleDoorbellPressed()
// 1. 状态守卫
if (m_current_state != STATE_IDLE) {
    ESP_LOGI(TAG, "Doorbell pressed while busy, ignoring.");
    return; // 系统正忙，忽略本次门铃
}

// 2. 检查网络连接（系统属性）
if (!m_is_network_connected) {
    // 网络不可用，执行离线逻辑（如播放本地提示音）
    queueEvent(CMD_AUDIO_PLAY, {.content="网络未连接"});
    return;
}

// 3. 通过检查，准备进入下一步
ESP_LOGI(TAG, "Doorbell event accepted. Transitioning to RECOGNIZING state.");
setSystemState(STATE_RECOGNIZING); // 状态切换，将触发摄像头上电等“进入”动作
```

---

**Step 3: 指令再创造 (ActionManager)**
- 状态切换后，`ActionManager` 发出内部指令，请求进行人脸识别。

```cpp
// 伪代码: ActionManager::handleDoorbellPressed() 接上一步
// 4. 创建一个“命令”事件，委托FaceManager工作
AppEvent cmd;
cmd.type = CMD_FACE_RECOGNIZE;
cmd.payload.face_recognize_cmd.trigger_event = HW_NOTIFY_DOORBELL_PRESSED; // 携带上下文
xQueueSend(g_app_event_queue, &cmd, 0); // 发送回主队列
```

---

**Step 4: 二次分发与异步执行 (MainEventLoop -> FaceManager)**
- `MainEventLoop` 取出 `CMD_FACE_RECOGNIZE` 事件，并分发给 `FaceManager`。

```cpp
// 伪代码: FaceManager::processEvent(event) 内部
case CMD_FACE_RECOGNIZE:
    // 启动一个异步的云端识别流程
    recognizeFaceAsync(event.payload.face_recognize_cmd.trigger_event);
    break;

// 伪代码: FaceManager::recognizeFaceAsync(trigger)
// 1. 抓取图像
auto image = m_camera->captureImage();
// 2. 发起非阻塞的云端API调用，并传入回调函数
m_cloud_client->postAsync("/face/recognize", image, 
    [trigger](bool success, const JsonResponse& resp) {
        // 【关键】这里的代码在网络任务的上下文中执行
        // a. 创建“结果”事件
        AppEvent result_event;
        result_event.type = RESULT_FACE_RECOGNITION_COMPLETED;
        // b. 填充结果
        result_event.payload.recognition_result.success = success;
        result_event.payload.recognition_result.role = success ? parseRole(resp) : ROLE_STRANGER;
        result_event.payload.recognition_result.trigger_event = trigger; // 将上下文传下去
        // c.【闭环】将结果事件发回主队列
        xQueueSend(g_app_event_queue, &result_event, 0);
    }
);
```

---

**Step 5: 最终决策与执行 (MainEventLoop -> ActionManager -> StreamManager)**
- `MainEventLoop` 收到 `RESULT_FACE_RECOGNITION_COMPLETED`，再次分发给 `ActionManager`。

```cpp
// 伪代码: ActionManager::processEvent(event) 内部
case RESULT_FACE_RECOGNITION_COMPLETED:
    handleFaceRecognitionResult(event.payload.recognition_result);
    break;

// 伪代码: ActionManager::handleFaceRecognitionResult(result)
// 1. 检查结果是否成功，以及触发源是否是门铃
if (result.success && result.trigger_event == HW_NOTIFY_DOORBELL_PRESSED) {
    // 2. 根据角色进行最终决策
    if (result.role == ROLE_STRANGER) {
        ESP_LOGI(TAG, "Stranger detected. Initiating remote call.");
        // 3. 发送最终的执行指令：开始推流
        AppEvent stream_cmd;
        stream_cmd.type = REMOTE_CMD_START_STREAM;
        xQueueSend(g_app_event_queue, &stream_cmd, 0);
    } else if (result.role == ROLE_HOST) {
        // 如果是主人，则发送开锁指令
        AppEvent unlock_cmd;
        unlock_cmd.type = CMD_LOCK_UNLOCK;
        xQueueSend(g_app_event_queue, &unlock_cmd, 0);
    }
} else {
    // 识别失败或非门铃触发，执行其他逻辑...
    setSystemState(STATE_IDLE); // 例如直接返回空闲
}
```
- `MainEventLoop` 收到 `REMOTE_CMD_START_STREAM` 事件后，会将其分发给 `StreamManager`，`StreamManager` 最终调用 `setSystemState(STATE_STREAMING)`，完成状态迁移，整个工作流至此形成一个完整的闭环。

---

### 4.2 场景二：语音问答（语音触发 -> 云端NLU -> TTS播放）

这个场景展示了系统如何处理非指令的、纯对话式的交互。

**初始状态**: 系统处于 `STATE_IDLE`。

---

**Step 1: 语音唤醒**
- 用户说出唤醒词。`AudioService`检测到后，发送 `VOICE_NOTIFY_WAKE_WORD_DETECTED` 事件入队。

---

**Step 2: 进入聆听状态**
- `ActionManager` 收到该事件，发现当前状态是`IDLE`，于是调用 `setSystemState(STATE_LISTENING_FOR_COMMAND)`。
- `setSystemState` 的**进入动作 (Entry-Action)** 会自动启动麦克风，并开始向云端NLU服务传输音频流。

---

**Step 3: 云端处理与响应**
- 用户说：“今天天气怎么样？”。音频被实时传到云端。
- 云端进行STT（语音转文本）和NLU（自然语言理解），判断出这是一个“查询天气”的意图。
- 云端服务**自己**调用天气API，获取信息，并生成回答的文本：“今天天气晴，最高温度25度。”
- 云端服务调用TTS（文本转语音）引擎，将上述文本转为Opus音频流。

---

**Step 4: 处理云端TTS流**
- 云端首先通过MCP发送一条控制信令: `{"type": "tts", "state": "start"}`。
- `McpServer` 模块接收到后，将其转换为 `SYS_NOTIFY_TTS_STREAM_STATE_CHANGED` 事件入队，payload为 `{ state: TTS_START }`。
- `ActionManager` 收到此事件，调用 `setSystemState(STATE_SPEAKING)`。`setSystemState`的**进入动作**会准备好音频播放器。
- 随后，云端下发的Opus音频包，被`McpServer`直接送往`AudioService`的解码队列。由于当前是`STATE_SPEAKING`，音频被自动解码并播放。

---

**Step 5: 结束对话**
- TTS音频播放完毕后，云端发送 `{"type": "tts", "state": "stop"}`。
- `McpServer` 将其转换为 `SYS_NOTIFY_TTS_STREAM_STATE_CHANGED` 事件入队，payload为 `{ state: TTS_STOP }`。
- `ActionManager` 收到此事件，调用 `setSystemState(STATE_IDLE)`，系统恢复待机。

这个流程清晰地表明，设备端无需理解对话内容，只需响应云端下发的“开始播报”和“停止播报”两个标准化指令即可，实现了完美的云-端解耦。