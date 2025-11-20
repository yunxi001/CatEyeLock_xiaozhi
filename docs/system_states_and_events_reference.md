# 智能猫眼系统状态与事件最终参考文档

## 1. 设计哲学与核心原则

本文档是智能猫眼项目核心软件架构的最终参考，详细定义了系统的所有“状态”和“事件”。

系统架构遵循“**云端智能，端侧执行**”和“**事件驱动，状态机控制**”的设计哲学，构建了一个分层、解耦、高内聚的软件体系。其核心是通过一个**中央消息队列**来串行化处理所有业务决策，同时通过**异步任务**处理耗时操作，并利用**闭环事件流**来响应异步结果，从而在保证系统高响应性的同时，优雅地处理复杂业务逻辑。

---

## 2. 系统核心运行状态 (SystemState) 详解

“状态”定义了系统在特定时刻的行为模式、资源使用情况以及对外部事件的响应策略。状态的切换由`ActionManager`通过`setSystemState()`函数统一管理。

### 2.1 异常情况处理的设计思路

在定义状态前，需特别说明系统对异常情况的处理策略：

-   **网络中断**: “网络连接”本身不作为一个独立的顶层状态，而是作为一个可被随时查询的**系统属性**（如 `bool is_network_connected`）。该属性由 `SYS_NOTIFY_NETWORK_STATE_CHANGED` 事件来更新。所有需要网络的工作流（如人脸识别、远程呼叫）在启动前，都**必须**检查此属性，若网络不可用，则应立即转入离线处理逻辑（如播放本地提示音、将录像存至 SD 卡），而不是启动一个注定会失败的网络操作。

-   **错误处理**: 系统采用“事件”和“状态”结合的方式处理错误。
    -   **错误事件 (`RESULT_SYSTEM_ERROR`)**: 用于处理**可恢复的、瞬态的**错误（如一次网络请求超时）。系统在收到此类事件后，通常会播报提示并可能尝试重试，但其主体状态不发生改变。
    -   **错误状态 (`STATE_FATAL_ERROR`)**: 用于处理**不可恢复的、致命的**错误（如摄像头初始化失败）。系统一旦进入此状态，将停止所有正常功能，仅等待重启指令，以确保安全。

### 2.2 `SystemState` 枚举定义 (V2.0)

```cpp
/**
 * @brief SystemState V2.0 - 定义了系统主状态机的所有核心运行状态
 * @details 此版本新增了错误、升级等关键状态，使状态机更完整、更健壮。
 */
enum SystemState {
    /** @brief 正在启动。系统上电后、进入待机前的初始状态。*/
    STATE_BOOTING,

    /** @brief 空闲/待机。系统的默认状态，功耗最低，仅监听关键的唤醒事件。*/
    STATE_IDLE,

    /** @brief 正在聆听指令。由语音唤醒触发，此时设备开启音频流上传至云端NLU服务。*/
    STATE_LISTENING_FOR_COMMAND,

    /** @brief 正在识别。由门铃或PIR等高优先级事件触发，正在进行人脸识别等后台任务。*/
    STATE_RECOGNIZING,

    /** @brief 正在通话/推流。与手机App正在进行实时音视频通话。*/
    STATE_STREAMING,

    /** @brief 正在播报。正在播放云端下发的TTS语音或本地提示音。*/
    STATE_SPEAKING,

    /** @brief 正在录像。正在将事件视频片段写入本地存储。*/
    STATE_RECORDING,

    /** @brief 警报中。本地正在发出声音或视觉警报（如门未关、防撬）。*/
    STATE_ALERTING,

    /** @brief 正在进行固件升级(OTA)。此状态下系统将暂停所有其他服务。*/
    STATE_UPGRADING,

    /** @brief 致命错误。系统核心功能故障，必须重启才能恢复。*/
    STATE_FATAL_ERROR,

    /** @brief 低功耗休眠。由锁控MCU指令触发，系统进入深度睡眠。*/
    STATE_LOW_POWER,
};
```

### 2.3 各状态详解

#### `STATE_IDLE` (空闲/待机)

-   **描述**: 系统的基准状态。CPU 处于低频或等待状态，屏幕变暗或关闭。
-   **进入时动作**: 开启语音唤醒检测；开启 PIR 人体侦测；关闭摄像头等非必要外设的电源。
-   **可响应的事件**: `HW_NOTIFY_DOORBELL_PRESSED`, `HW_NOTIFY_PIR_MOTION_DETECTED`, `VOICE_NOTIFY_WAKE_WORD_DETECTED`, `REMOTE_CMD_*` 系列远程指令, `HW_NOTIFY_ENTER_LOW_POWER_MODE`。

#### `STATE_LISTENING_FOR_COMMAND` (正在聆听指令)

-   **描述**: 在语音唤醒后，等待用户说出具体指令的状态。
-   **进入时动作**: 开启麦克风，建立与云端 NLU 服务器的音频上传通道，UI 上显示聆听动画。
-   **可响应的事件**: `REMOTE_CMD_STOP_CURRENT_ACTION`（可打断聆听），云端 NLU 下发的指令或 TTS 播报请求。

#### `STATE_RECOGNIZING` (正在识别)

-   **描述**: 正在后台处理一个高优先级的识别任务，通常是人脸识别。
-   **进入时动作**: **检查网络连接属性**。若无网络，则转入离线逻辑；若有网络，则开启摄像头电源，UI 显示“正在识别...”。
-   **可响应的事件**: `RESULT_FACE_RECOGNITION_COMPLETED`（等待的核心结果事件）。
-   **将忽略的事件**: `HW_NOTIFY_DOORBELL_PRESSED`等新的触发类事件，以实现逻辑去抖。

#### `STATE_STREAMING` (正在通话/推流)

-   **描述**: 与手机 App 进行实时音视频通话。
-   **进入时动作**: 开启摄像头、麦克风和音频播放器，建立 WebSocket 连接，关闭本地唤醒词检测。
-   **可响应的事件**: `REMOTE_CMD_STOP_STREAM`（挂断通话）。

#### `STATE_SPEAKING` (正在播报)

-   **描述**: 正在通过扬声器播放语音。
-   **进入时动作**: 初始化音频解码器和播放器。
-   **可响应的事件**: `SYS_NOTIFY_TTS_STREAM_STATE_CHANGED`（播放完毕），`REMOTE_CMD_STOP_CURRENT_ACTION`（可打断播报）。

#### `STATE_UPGRADING` (正在升级)

-   **描述**: 正在进行 OTA 固件升级。这是一个临界状态，需要最高稳定性。
-   **进入时动作**: 停止所有非必需的后台任务，禁用所有外部事件的正常处理流程，UI 显示升级进度。
-   **可响应的事件**: 仅处理 OTA 模块内部的事件。

#### `STATE_FATAL_ERROR` (致命错误)

-   **描述**: 系统遇到不可恢复的错误。
-   **进入时动作**: 停止所有任务，在屏幕上显示永久性的错误信息和重启提示。
-   **可响应的事件**: 仅响应系统重启指令（如长按物理按键或特定的远程命令）。

#### `STATE_LOW_POWER` (低功耗休眠)

-   **描述**: 系统已进入深度睡眠，仅特定硬件中断可唤醒。
-   **进入时动作**: 关闭所有外设电源，保存必要上下文，调用`esp_deep_sleep_start()`。
-   **可响应的事件**: 无。由硬件中断唤醒后将重新进入`STATE_BOOTING`流程。

---

## 3. 最终事件清单与详解 (`AppEventType` V4.0)

此版本是根据所有讨论的最终版本，移除了不必要的事件，并对所有事件的功能和`payload`进行了详细注释。

```cpp
/**
 * @file app_event_type.h
 * @brief AppEventType V4.0 - 智能猫眼系统事件类型的最终参考定义
 * @version 4.0
 * @date 2025-11-19
 * @details
 *   根据最终讨论，此版本明确了：
 *   1. 语音识别的意图理解(NLU)完全在云端，设备端只负责唤醒和流式上传。
 *   2. 系统的低功耗模式由锁控MCU决策并控制。
 *   3. 用户管理（人脸录入等）由App和云端负责，设备端不参与注册流程。
 */
enum AppEventType {
    // ====================================================================
    // 类别 1: 硬件与传感器通知 (Hardware & Sensor Notifications)
    // 来源: 主要由 LockController 模块根据锁控MCU的串口数据产生
    // ====================================================================

    /**
     * @brief 门铃被按下。
     * @details 高优先级用户交互事件，通常触发人脸识别和远程呼叫。
     * @payload 无。
     */
    HW_NOTIFY_DOORBELL_PRESSED,

    /**
     * @brief 锁的物理状态改变。
     * @payload lock_state: { bool is_locked; enum { CTX_LEAVING, CTX_RETURNING } context; uint8_t battery; }
     */
    HW_NOTIFY_LOCK_STATE_CHANGED,

    /**
     * @brief 门磁检测到门未关严。
     * @payload 无。
     */
    HW_NOTIFY_DOOR_AJAR,

    /**
     * @brief 锁体遭受暴力撬动。
     * @payload 无。
     */
    HW_NOTIFY_TAMPER_ALARM,

    /**
     * @brief PIR传感器侦测到人体移动。
     * @payload 无。
     */
    HW_NOTIFY_PIR_MOTION_DETECTED,

    /**
     * @brief 连续多次输入错误密码。
     * @payload password_attempts: { uint8_t count; }
     */
    HW_NOTIFY_PASSWORD_ATTEMPTS_EXCEEDED,

    /**
     * @brief 由于密码错误次数过多，锁系统已被暂时锁定。
     * @payload lockout_info: { uint32_t duration_sec; }
     */
    HW_NOTIFY_LOCK_SYSTEM_LOCKED_OUT,

        /** @brief 电池电量低于临界值警报。*/
    HW_NOTIFY_LOW_BATTERY_ALERT,          // payload: { uint8_t battery_percent; }

    /** @brief 来自锁控MCU的指令，要求ESP32进入低功耗模式。*/
    HW_NOTIFY_ENTER_LOW_POWER_MODE,

    // ====================================================================
    // 类别 2: 系统与模块通知 (System & Module Notifications)
    // ====================================================================
    /** @brief 网络连接状态改变。*/
    SYS_NOTIFY_NETWORK_STATE_CHANGED,     // payload: { bool is_connected; }

    /** @brief 云端下发的TTS音频流状态改变（开始/结束）。*/
    SYS_NOTIFY_TTS_STREAM_STATE_CHANGED,  // payload: { enum { TTS_START, TTS_STOP } state; }

    /** @brief 远程音视频流已确认停止。*/
    SYS_NOTIFY_STREAMING_STOPPED,

    /** @brief 存储介质（SD卡）状态变化。*/
    SYS_NOTIFY_STORAGE_STATE_CHANGED,     // payload: { bool is_present, is_healthy, free_space }

    /** @brief 存储介质发生错误。*/
    SYS_NOTIFY_STORAGE_ERROR,             // payload: { StorageError code; }

    // ====================================================================
    // 类别 3: 外部指令 (External Commands)
    // ====================================================================
    /** @brief 来自云端/App 的远程开锁指令。*/
    REMOTE_CMD_UNLOCK,

    /** @brief 来自云端/App 的请求，要求开启实时音视频流。*/
    REMOTE_CMD_START_STREAM,

    /** @brief 来自云端/App 的指令，要求停止当前正在进行的任务。*/
    REMOTE_CMD_STOP_CURRENT_ACTION,

    /** @brief 来自云端/App 的请求，要求生成一个临时开锁码。*/
    REMOTE_CMD_GENERATE_TEMP_CODE,        // payload: { validity_minutes, user_info }

    /** @brief 本地语音唤醒成功。这是启动一次云端语音对话的信号。*/
    VOICE_NOTIFY_WAKE_WORD_DETECTED,

    // ====================================================================
    // 类别 4: 内部模块间指令 (Internal Commands)
    // ====================================================================
    /** @brief 命令：请求人脸识别模块执行一次识别。*/
    CMD_FACE_RECOGNIZE,                   // payload: { AppEventType trigger_event; }

    /** @brief 命令：请求意图识别模块执行一次识别。*/
    CMD_INTENT_RECOGNIZE,                 // payload: { AppEventType trigger_event; }

    /** @brief 命令：请求音频服务模块播放一个指定的音效或TTS语音。*/
    CMD_AUDIO_PLAY,                       // payload: { enum { SOUND, TTS } type; const char* content; }

    /** @brief 命令：请求锁控模块执行开锁。*/
    CMD_LOCK_UNLOCK,

    /** @brief 命令：请求查询锁控MCU的硬件信息。*/
    CMD_QUERY_LOCK_MCU_INFO,

    /** @brief 命令：请求视频模块开始一次事件录像。*/
    CMD_VIDEO_START_RECORDING,            // payload: { duration_ms, trigger_event }

    /** @brief 命令：请求视频模块立即停止当前的录像。*/
    CMD_VIDEO_STOP_RECORDING,

    // ====================================================================
    // 类别 5: 异步操作结果 (Asynchronous Results)
    // ====================================================================
    /**
     * @brief 结果：一次人脸识别流程已完成。
     * @payload recognition_result: { bool success; enum ROLE role; char user_id[32]; AppEventType trigger; }
     */
    RESULT_FACE_RECOGNITION_COMPLETED,

    /**
     * @brief 结果：一次意图识别流程已完成。
     * @payload intent_result: { bool success; char intent_name[64]; }
     */
    RESULT_INTENT_RECOGNITION_COMPLETED,

    /**
     * @brief 结果：查询锁控MCU信息已有返回。
     * @payload lock_mcu_info: { uint8_t battery, int distance_cm; }
     */
    RESULT_LOCK_MCU_INFO,

    /**
     * @brief 结果：一段视频已成功录制并保存。
     * @payload video_record_result: { bool success; char file_path[128]; }
     */
    RESULT_VIDEO_RECORDING_COMPLETED,

    /** @brief 结果：通用系统级错误，如网络超时等。*/
    RESULT_SYSTEM_ERROR,                  // payload: { int code, char message[128] }
};
```
