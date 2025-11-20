# 智能猫眼事件驱动框架最终设计文档

## 1. 设计哲学与核心原则

本文档旨在最终确立智能猫眼项目的核心软件架构——一个基于中央消息队列的、异步、事件驱动的框架。此架构旨在解决传统嵌入式开发中，在处理多任务并发、复杂异步 I/O（如网络请求）和多步骤业务逻辑时常见的竞态条件、逻辑耦合和代码可维护性差等问题。

### 1.1 核心组件

整个框架由以下几个关键技术组件构成：

1.  **`AppEvent` 统一事件结构体**: 这是系统内部通信的“标准数据包”。它包含了事件的`类型(type)`和该事件附带的`数据(payload)`，将所有内部和外部信号标准化。

2.  **`g_app_event_queue` 中央消息队列**: 这是系统的“中央枢纽”和“主动脉”。它是一个线程安全的 FIFO（先进先出）队列，负责接收所有事件，并保证事件按序被处理。它彻底解耦了事件的生产者和消费者。

3.  **事件生产者 (Event Producers)**: 系统中任何能创建并发送`AppEvent`的模块（如`LockController`、`McpServer`）。它们遵循“发后即忘”原则，只负责将外部世界的变化转换成标准事件并送入队列，不关心后续如何处理。

4.  **唯一的事件消费者 (The Single Consumer)**: `Application::MainEventLoop`是系统中唯一从消息队列读取事件的任务。它扮演“总调度员”，保证了所有业务决策的入口都是**串行**的，从而根除了业务逻辑层面的并发冲突。

5.  **功能处理器 (Functional Handlers)**: 以`ActionManager`为代表的一系列管理类。它们是实际的“业务专家”，由`MainEventLoop`根据事件类型分发任务给它们。每个处理器负责一个内聚的功能领域（如锁控、流媒体），并维护该功能所需的状态。

### 1.2 核心设计原则

1.  **决策串行化**: 所有业务决策都在`MainEventLoop`这个单一任务的上下文中做出，保证了状态变更的顺序一致性和原子性。

2.  **异步非阻塞**: 主循环永不等待耗时操作。所有慢速 I/O（网络、文件）都必须异步化，将主循环的 CPU 时间释放出来，以保持系统对新事件的快速响应。

3.  **闭环事件流**: 异步操作的“结果”必须被重新封装成一个新的`RESULT_*`事件，并再次发送回中央队列。这是处理复杂长任务链、避免“回调地狱”的关键所在。

4.  **高内聚、低耦合**: 业务逻辑被封装在各自的功能处理器中（高内聚）。处理器之间不直接调用，而是通过向主队列发送`CMD_*`（命令）事件来通信（低耦合）。

## 2. 最终事件清单与详解 (`AppEventType` V3.0)

以下是根据您的最终需求（移除设备端的用户管理流程）精简和优化后的事件枚举定义。每一个事件都附有详细的中文注释，说明其用途、来源和所需的数据负载。

```cpp
/**
 * @file app_event_type.h
 * @brief AppEventType V3.0 - 智能猫眼系统事件类型的最终定义
 * @version 3.0
 * @date 2025-11-19
 * @details
 *   此版本根据最终讨论，移除了设备端的用户注册管理流程事件，
 *   并整合了电源管理、混合存储等功能需求。
 *
 *   命名规范: [类别/来源]_[动作]_[对象]_[结果]
 *   - HW_NOTIFY: 来自硬件/传感器的通知
 *   - SYS_NOTIFY: 来自系统内部模块状态的通知
 *   - REMOTE_CMD: 来自云端/App的远程指令
 *   - VOICE_CMD: 来自本地语音识别的指令
 *   - CMD: 模块间请求服务的内部指令
 *   - RESULT: 异步操作完成后的结果反馈
 */
enum AppEventType {
    // ====================================================================
    // 类别 1: 硬件与传感器通知 (Hardware & Sensor Notifications)
    // 来源: 主要由 LockController 模块根据锁控MCU的串口数据产生
    // ====================================================================

    /**
     * @brief 门铃被按下。
     * @details 这是一个高优先级的用户交互事件，通常会触发人脸识别和远程呼叫流程。
     * @payload 无。
     */
    HW_NOTIFY_DOORBELL_PRESSED,

    /**
     * @brief 锁的物理状态发生改变。
     * @payload lock_state: {
     *     bool is_locked;
     *     enum { CONTEXT_NONE, CONTEXT_LEAVING, CONTEXT_RETURNING } context; // 上下文：是家人出门还是回家
     *     uint8_t battery_percent;
     * }
     */
    HW_NOTIFY_LOCK_STATE_CHANGED,

    /**
     * @brief 门磁检测到门未关严。
     * @payload 无。
     */
    HW_NOTIFY_DOOR_AJAR,

    /**
     * @brief 锁体遭受暴力撬动，触发防撬警报。
     * @payload 无。
     */
    HW_NOTIFY_TAMPER_ALARM,

    /**
     * @brief PIR (被动红外) 传感器侦测到人体移动。
     * @payload 无。
     */
    HW_NOTIFY_PIR_MOTION_DETECTED,

    /**
     * @brief 连续多次输入错误密码。
     * @payload password_attempts: { uint8_t attempt_count; }
     */
    HW_NOTIFY_PASSWORD_ATTEMPTS_EXCEEDED,

    /**
     * @brief 由于密码错误次数过多，锁系统已被暂时锁定。
     * @payload lockout_info: { uint32_t lockout_duration_seconds; }
     */
    HW_NOTIFY_LOCK_SYSTEM_LOCKED_OUT,

    /** @brief 电池电量低于临界值（例如15%）的硬件警报。*/
    HW_NOTIFY_LOW_BATTERY_ALERT,          // payload: { uint8_t battery_percent; }


    // ====================================================================
    // 类别 2: 系统与模块通知 (System & Module Notifications)
    // ====================================================================

    /** @brief 网络连接状态发生改变。*/
    SYS_NOTIFY_NETWORK_STATE_CHANGED,     // payload: { bool is_connected; char ip_address[16]; }

    /** @brief TTS 音频播放完毕。*/
    SYS_NOTIFY_TTS_PLAYBACK_FINISHED,

    /** @brief 远程音视频流已确认停止。*/
    SYS_NOTIFY_STREAMING_STOPPED,

    /** @brief 系统功耗模式已发生改变。*/
    SYS_NOTIFY_POWER_MODE_CHANGED,        // payload: { PowerMode new_mode; }

    /** @brief 存储介质（如SD卡）状态发生变化。*/
    SYS_NOTIFY_STORAGE_STATE_CHANGED,     // payload: { bool is_present, is_healthy, free_space_bytes }

    /** @brief 存储介质发生错误（如SD卡已满、写入失败）。*/
    SYS_NOTIFY_STORAGE_ERROR,             // payload: { StorageError error_code; }


    // ====================================================================
    // 类别 3: 外部指令 (External Commands)
    // ====================================================================

    /** @brief 来自云端/App 的远程开锁指令。*/
    REMOTE_CMD_UNLOCK,

    /** @brief 来自云端/App 的请求，要求开启实时音视频流 (远程查看摄像头/通话)。*/
    REMOTE_CMD_START_STREAM,

    /** @brief 来自云端/App 的指令，要求停止当前正在进行的任务（如通话、警报）。*/
    REMOTE_CMD_STOP_CURRENT_ACTION,

    /** @brief 来自云端/App 的请求，要求生成一个临时开锁码。*/
    REMOTE_CMD_GENERATE_TEMP_CODE, // payload: { validity_minutes, user_info }

    /** @brief 本地语音唤醒成功。*/
    VOICE_NOTIFY_WAKE_WORD_DETECTED,


    // ====================================================================
    // 类别 4: 内部模块间指令 (Internal Commands)
    // ====================================================================

    /** @brief 命令：请求人脸识别模块执行一次识别。*/
    CMD_FACE_RECOGNIZE, // payload: { AppEventType trigger_event; }

    /** @brief 命令：请求意图识别模块执行一次识别。*/
    CMD_INTENT_RECOGNIZE, // payload: { AppEventType trigger_event; }

    /** @brief 命令：请求音频服务模块播放一个指定的音效或TTS语音。*/
    CMD_AUDIO_PLAY, // payload: { enum { TYPE_SOUND, TYPE_TTS } type; const char* content; }

    /** @brief 命令：请求锁控模块执行开锁。*/
    CMD_LOCK_UNLOCK,

    /** @brief 命令：请求查询锁控MCU的硬件信息。*/
    CMD_QUERY_LOCK_MCU_INFO,

    /** @brief 命令：命令系统进入低功耗/休眠模式。*/
    CMD_SYSTEM_ENTER_LOW_POWER,

    /** @brief 命令：请求视频模块开始一次事件录像。*/
    CMD_VIDEO_START_RECORDING,            // payload: { duration_ms, trigger_event }

    /** @brief 命令：请求视频模块立即停止当前的录像。*/
    CMD_VIDEO_STOP_RECORDING,


    // ====================================================================
    // 类别 5: 异步操作结果 (Asynchronous Results)
    // ====================================================================

    /**
     * @brief 结果：一次人脸识别流程已完成。
     * @payload recognition_result: {
     *     bool success;
     *     enum { ROLE_HOST, ROLE_FRIEND, ROLE_DELIVERY, ROLE_STRANGER } role;
     *     char user_id[32];
     *     AppEventType trigger_event;
     * }
     */
    RESULT_FACE_RECOGNITION_COMPLETED,

    /**
     * @brief 结果：一次意图识别流程已完成。
     * @payload intent_result: { bool success; char intent_name[64]; }
     */
    RESULT_INTENT_RECOGNITION_COMPLETED,

    /**
     * @brief 结果：查询锁控MCU信息已有返回。
     * @payload lock_mcu_info: { uint8_t battery_percent; int detection_distance_cm; }
     */
    RESULT_LOCK_MCU_INFO,

    /**
     * @brief 结果：一段视频已成功录制并保存。
     * @payload video_record_result: { bool success; char file_path[128]; }
     */
    RESULT_VIDEO_RECORDING_COMPLETED,

    /** @brief 结果：通用系统级错误，如网络超时等。*/
    RESULT_SYSTEM_ERROR,                  // payload: { int error_code, char message[128] }
};
```

## 3. 事件处理流程示例回顾

此事件清单是整个系统运作的“剧本”。我们以“门铃按下”为例，回顾其处理流程：

1.  **`HW_NOTIFY_DOORBELL_PRESSED` 入队**: `LockController`侦听到硬件信号，创建此事件并送入主队列`g_app_event_queue`。
2.  **分发给 `ActionManager`**: `MainEventLoop`取出此事件，`switch`判断后，调用`ActionManager`的相关方法。
3.  **`ActionManager`发出内部指令**: `ActionManager`判断需要进行人脸识别，于是创建并发送`CMD_FACE_RECOGNIZE`事件回主队列。
4.  **分发给 `FaceManager`**: `MainEventLoop`取出`CMD_FACE_RECOGNIZE`事件，将其分发给管理人脸识别的`FaceManager`。
5.  **异步操作与结果入队**: `FaceManager`执行与云端通信的异步操作。操作完成后，在回调中创建`RESULT_FACE_RECOGNITION_COMPLETED`事件（携带识别结果），并发送回主队列。
6.  **最终决策**: `MainEventLoop`取出结果事件，再次分发给`ActionManager`。`ActionManager`根据`payload`中的用户角色，最终决定是发出`CMD_LOCK_UNLOCK`事件（开锁），还是`REMOTE_CMD_START_STREAM`事件（呼叫主人）。

## 4. 总结

这份最终版的事件清单和其背后的设计框架，为您提供了一个强大、清晰且可扩展的软件架构基础。它将所有复杂的并发和异步问题，都收敛到-一个统一、有序的事件流中进行管理，使得后续的功能开发和维护工作都能事半功倍。
