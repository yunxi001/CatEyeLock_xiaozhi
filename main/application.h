/**
 * @file application.h
 * @brief 定义了应用程序的核心类 Application。
 * @author 78
 * @date 2024-07-20
 *
 * @details
 * 该文件是整个项目的核心，定义了 `Application` 单例类。
 * `Application` 类负责管理应用的生命周期、主事件循环、状态机以及
 * 协调各个模块（如音频服务、通信协议、OTA更新等）的工作。
 */

#ifndef _APPLICATION_H_
#define _APPLICATION_H_

// FreeRTOS 和 ESP-IDF 相关头文件
#include <esp_timer.h> // ESP32 高精度定时器
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h> // FreeRTOS 事件组，用于任务间同步
#include <freertos/task.h>

// C++ 标准库
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>

// 项目内部模块头文件
#include "audio_service.h"             // 音频服务模块
#include "device_state_event.h"        // 设备状态和事件定义
#include "lock_control/lock_control.h" // 锁控服务模块
#include "monitor_service.h"           // 监控服务模块
#include "ota.h"                       // OTA (Over-the-Air) 更新模块
#include "protocol.h"                  // 通信协议模块

// ======================= 主事件循环事件位定义 =======================
// 使用 FreeRTOS 事件组的位（bit）来触发不同的事件处理
#define MAIN_EVENT_SCHEDULE (1 << 0)           // 调度执行一个回调任务
#define MAIN_EVENT_SEND_AUDIO (1 << 1)         // 发送音频数据事件
#define MAIN_EVENT_WAKE_WORD_DETECTED (1 << 2) // 检测到唤醒词事件
#define MAIN_EVENT_VAD_CHANGE (1 << 3) // VAD (语音活动检测) 状态改变事件
#define MAIN_EVENT_ERROR (1 << 4)      // 发生错误事件
#define MAIN_EVENT_CHECK_NEW_VERSION_DONE (1 << 5) // 检查新版本完成事件
#define MAIN_EVENT_CLOCK_TICK (1 << 6) // 时钟节拍事件 (用于定时任务)

// ======================= 待处理命令队列相关定义 =======================

/**
 * @brief 待处理命令类型枚举
 *
 * 用于区分不同类型的命令，以便确定何时发送最终 ack：
 * - IMMEDIATE: 即时命令，收到 STM32 ACK 后即可发送 ack
 * - QUERY: 查询命令，需要等待 STM32 ACK + 数据帧后发送 ack
 * - LONG_FLOW: 长流程命令，需要等待 STM32 ACK + 最终结果后发送 ack
 */
enum class PendingCommandType {
  IMMEDIATE, ///< 即时命令：等待 STM32 ACK
  QUERY,     ///< 查询命令：等待 STM32 ACK + 数据帧
  LONG_FLOW, ///< 长流程命令：等待 STM32 ACK + 最终结果
};

/**
 * @brief 待处理命令信息结构体
 *
 * 用于保存服务器下发命令的上下文信息，以便在收到 STM32 响应时
 * 能够关联原始 seq_id 并发送正确的 ack 响应。
 */
struct PendingCommand {
  std::string seq_id;      ///< 原始消息 ID（来自服务器）
  PendingCommandType type; ///< 命令类型
  std::string category;    ///< 类别：finger/nfc/password/lock/dev/query
  std::string command;     ///< 命令：add/del/clear/query/unlock/lock/beep/...
  uint8_t uart_type;       ///< UART 命令 TYPE
  uint8_t uart_subtype;    ///< UART 子命令（用于指纹/NFC）
  int64_t timestamp_ms;    ///< 发送时间戳（毫秒）
  bool esp32_ack_sent;     ///< 是否已发送 esp32_ack
  bool stm32_ack_received; ///< 是否已收到 STM32 ACK
  int stm32_error_code;    ///< STM32 ACK 错误码（0 表示成功）
};

/**
 * @brief 声学回声消除 (AEC) 模式枚举。
 */
enum AecMode {
  kAecOff,          // 关闭 AEC
  kAecOnDeviceSide, // 在设备端进行 AEC
  kAecOnServerSide, // 在服务器端进行 AEC
};

/**
 * @brief 应用程序主类 (单例)。
 *
 * 管理整个应用程序的生命周期和状态。
 */
class Application {
public:
  /**
   * @brief 获取 Application 类的唯一实例。
   * @return Application& 对单例对象的引用。
   */
  static Application &GetInstance() {
    static Application instance;
    return instance;
  }
  // 删除拷贝构造函数和赋值运算符，确保单例模式
  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;

  /** @brief 启动应用程序，初始化并创建主事件循环任务。 */
  void Start();
  /** @brief 主事件循环，等待并处理来自事件组的各种事件。 */
  void MainEventLoop();
  /** @brief 获取当前设备状态。 */
  DeviceState GetDeviceState() const { return device_state_; }
  /** @brief 检查当前是否检测到语音活动。 */
  bool IsVoiceDetected() const { return audio_service_.IsVoiceDetected(); }
  /** @brief 调度一个函数在主事件循环中执行。线程安全。 */
  void Schedule(std::function<void()> callback);
  /** @brief 设置设备状态，并触发状态变更事件。 */
  void SetDeviceState(DeviceState state);
  /** @brief 显示一个提醒/警报。通常会显示在屏幕上并播放提示音。 */
  void Alert(const char *status, const char *message, const char *emotion = "",
             const std::string_view &sound = "");
  /** @brief 取消当前的提醒/警报。 */
  void DismissAlert();
  /** @brief 终止当前的 TTS 播放。 */
  void AbortSpeaking(AbortReason reason);
  /** @brief 切换聊天状态（开始或停止聆听）。 */
  void ToggleChatState();
  /** @brief 开始聆听用户语音。 */
  void StartListening();
  /** @brief 停止聆听用户语音。 */
  void StopListening();
  /** @brief 重启设备。 */
  void Reboot();
  /** @brief 通过外部调用（如按钮）来触发一次唤醒。 */
  void WakeWordInvoke(const std::string &wake_word);
  /** @brief 执行固件升级。 */
  bool UpgradeFirmware(Ota &ota, const std::string &url = "");
  /** @brief 检查设备是否可以进入睡眠模式。 */
  bool CanEnterSleepMode();
  /** @brief 发送 MCP (Multi-End Control Protocol) 消息。 */
  void SendMcpMessage(const std::string &payload);
  /** @brief 设置 AEC 模式。 */
  void SetAecMode(AecMode mode);
  /** @brief 获取当前 AEC 模式。 */
  AecMode GetAecMode() const { return aec_mode_; }
  /** @brief 播放一个指定的音效。 */
  void PlaySound(const std::string_view &sound);
  /** @brief 获取音频服务对象的引用。 */
  AudioService &GetAudioService() { return audio_service_; }
  /** @brief 启动监控模式。 */
  bool StartMonitorMode();
  /** @brief 停止监控模式。 */
  void StopMonitorMode();
  /** @brief 检查是否处于监控模式。 */
  bool IsMonitorMode() const;
  /** @brief 启动本地预览。 */
  bool StartLocalPreview();
  /** @brief 停止本地预览。 */
  void StopLocalPreview();
  /** @brief 检查本地预览是否活动。 */
  bool IsLocalPreviewActive() const;

private:
  /** @brief 私有构造函数，用于单例模式。 */
  Application();
  /** @brief 私有析构函数。 */
  ~Application();

  std::mutex mutex_; // 互斥锁，用于保护共享资源（如 main_tasks_）
  std::deque<std::function<void()>>
      main_tasks_;                     // 主任务队列，用于存储待执行的回调函数
  std::unique_ptr<Protocol> protocol_; // 通信协议处理对象的智能指针
  EventGroupHandle_t event_group_ = nullptr;        // FreeRTOS 事件组句柄
  esp_timer_handle_t clock_timer_handle_ = nullptr; // ESP 高精度定时器句柄
  volatile DeviceState device_state_ =
      kDeviceStateUnknown; // 当前设备状态（volatile
                           // 关键字提示编译器该变量可能在外部被意外修改）
  ListeningMode listening_mode_ = kListeningModeAutoStop; // 聆听模式
  AecMode aec_mode_ = kAecOff;                            // AEC 模式
  std::string last_error_message_;                        // 最后一次的错误信息
  AudioService audio_service_;                            // 音频服务对象
  std::unique_ptr<MonitorService> monitor_service_;       // 监控服务对象
  xiaozhi::LockControlService *lock_control_;             // 锁控服务对象指针

  // =========================================================================
  // 待处理命令队列（两级确认机制）
  // =========================================================================

  /** 待处理命令映射表，key 为 UART TYPE */
  std::map<uint8_t, PendingCommand> pending_commands_;

  /** 即时命令超时时间（毫秒） */
  static constexpr int64_t IMMEDIATE_TIMEOUT_MS = 3000;

  /** 查询命令超时时间（毫秒） */
  static constexpr int64_t QUERY_TIMEOUT_MS = 5000;

  /** 长流程命令超时时间（毫秒） */
  static constexpr int64_t LONG_FLOW_TIMEOUT_MS = 60000;

  bool has_server_time_ = false;              // 是否已从服务器获取时间
  bool aborted_ = false;                      // 是否已中止 TTS 播放
  int clock_ticks_ = 0;                       // 时钟节拍计数
  bool face_recognition_in_progress_ = false; // 人脸识别是否正在进行
  TaskHandle_t check_new_version_task_handle_ = nullptr; // 检查新版本任务的句柄
  TaskHandle_t main_event_loop_task_handle_ = nullptr;   // 主事件循环任务的句柄

  // =========================================================================
  // 自动连接相关成员变量
  // =========================================================================
  TaskHandle_t auto_connect_task_handle_ = nullptr; // 自动连接任务句柄
  bool auto_connect_enabled_ = true;                // 是否启用自动连接
  bool user_manually_disconnected_ = false;         // 用户是否主动断开连接
  int connection_retry_count_ = 0;                  // 连接重试计数

  // 指数退避参数
  static constexpr int INITIAL_RETRY_DELAY_MS = 1000; // 初始重试延迟：1秒
  static constexpr int MAX_RETRY_DELAY_MS = 60000;    // 最大重试延迟：60秒
  static constexpr int MAX_RETRY_COUNT = -1; // 最大重试次数：-1表示无限重试

  // 本地预览相关成员变量
  bool local_preview_active_ = false;           // 预览活动标志
  TaskHandle_t preview_capture_task_ = nullptr; // 捕获任务句柄
  TaskHandle_t preview_display_task_ = nullptr; // 显示任务句柄
  QueueHandle_t preview_frame_queue_ = nullptr; // 帧队列句柄

  // v5.0 协议：状态数据缓存（用于状态上报）
  int last_battery_ = 0;     // 最后一次电量
  int last_lux_ = 0;         // 最后一次光照值
  int last_lock_state_ = 0;  // 最后一次锁状态
  int last_light_state_ = 0; // 最后一次灯状态

  // --- 私有方法 ---
  void OnWakeWordDetected();      // 唤醒词检测到的处理函数
  void CheckNewVersion(Ota &ota); // 检查新固件版本
  void CheckAssetsVersion();      // 检查资源文件版本
  void ShowActivationCode(const std::string &code,
                          const std::string &message); // 显示激活码
  void SetListeningMode(ListeningMode mode);           // 设置聆听模式

  // =========================================================================
  // 自动连接相关方法
  // =========================================================================

  /**
   * @brief 自动连接任务循环
   *
   * 独立的 FreeRTOS 任务，负责：
   * 1. 检查网络状态和连接条件
   * 2. 尝试连接服务器
   * 3. 连接失败后使用指数退避策略重试
   * 4. 连接成功后监控连接状态
   */
  void AutoConnectLoop();

  /**
   * @brief 判断是否应该自动连接
   * @return 如果满足自动连接条件返回 true
   *
   * 检查条件：
   * - 自动连接功能已启用
   * - Protocol 已初始化
   * - 当前未连接服务器
   * - 网络已连接
   * - 设备处于合适的状态（Idle 或 Listening）
   * - 用户未主动断开连接
   */
  bool ShouldAutoConnect();

  /**
   * @brief 计算指数退避延迟时间
   * @param retry_count 当前重试次数
   * @return 延迟时间（毫秒）
   *
   * 使用指数退避算法：delay = INITIAL_DELAY * 2^retry_count
   * 最大延迟不超过 MAX_RETRY_DELAY_MS
   * 添加 ±20% 随机抖动避免多设备同时重连
   */
  int CalculateRetryDelay(int retry_count);

  // =========================================================================
  // 锁控相关方法（智能门锁扩展功能）
  // =========================================================================

  /** 处理 STM32 锁控模块上报的事件 */
  void HandleLockEvent(const xiaozhi::LockMessage &msg);

  /** 处理上报消息 (CAT = 0x01) */
  void HandleLockReportMessage(const xiaozhi::LockMessage &msg);

  /** 处理系统消息 (CAT = 0x00) */
  void HandleLockSystemMessage(const xiaozhi::LockMessage &msg);

  /** 处理用户管理反馈消息 (CAT = 0x03) */
  void HandleLockUserMessage(const xiaozhi::LockMessage &msg);

  /** 获取开锁方式字符串 */
  std::string GetUnlockMethodString(uint8_t method);

  /** 触发人脸识别流程 */
  void TriggerFaceRecognition();

  /** 处理服务器返回的人脸识别结果 */
  void HandleFaceRecognitionResult(cJSON *root);

  /** 处理撬锁报警 */
  void HandleTamperAlert(uint8_t level);

  /** 处理门未关提醒 */
  void HandleDoorNotClosed();

  /**
   * @brief 播放认证失败语音（拼接方式）
   * @param remaining 剩余尝试次数
   */
  void PlayAuthFailVoice(uint8_t remaining);

  /**
   * @brief 播放设备锁定语音（拼接方式）
   * @param lock_minutes 剩余锁定时间（分钟）
   */
  void PlayLockedVoice(uint8_t lock_minutes);

  /**
   * @brief 播放数字语音
   * @param number 要播放的数字（0-99）
   */
  void PlayNumberVoice(uint8_t number);

  /**
   * @brief 处理智能门锁扩展 JSON 消息
   * @param root JSON 根节点
   * @param type 消息类型字符串
   * @return 如果消息被处理返回 true，否则返回 false
   */
  bool HandleSmartLockJsonMessage(const cJSON *root, const char *type);

  // =========================================================================
  // 待处理命令队列相关方法（两级确认机制）
  // =========================================================================

  /**
   * @brief 确定命令类型
   * @param category 命令类别（finger/nfc/password/lock/dev/query）
   * @param command 命令名称（add/del/clear/query/unlock/lock/beep/...）
   * @return 命令类型枚举
   */
  PendingCommandType DetermineCommandType(const std::string &category,
                                          const std::string &command);

  /**
   * @brief 获取 UART TYPE
   * @param category 命令类别
   * @param command 命令名称
   * @return UART 命令 TYPE 值
   */
  uint8_t GetUartType(const std::string &category, const std::string &command);

  /**
   * @brief 映射 STM32 错误码到统一错误码
   * @param stm32_err STM32 返回的错误码
   * @return 统一错误码
   */
  int MapStm32ErrorCode(uint8_t stm32_err);

  /**
   * @brief 清理超时的待处理命令
   *
   * 遍历 pending_commands_，检查是否有超时的命令，
   * 如果有则发送 ack(code=5) 并移除。
   */
  void CleanupPendingCommands();

  /**
   * @brief 获取命令类型对应的超时时间
   * @param type 命令类型
   * @return 超时时间（毫秒）
   */
  int64_t GetTimeoutForType(PendingCommandType type);

  // =========================================================================
  // 本地预览相关方法
  // =========================================================================

  /**
   * @brief 预览捕获任务循环
   *
   * 以 15 FPS 频率捕获帧（66ms 间隔）
   */
  void PreviewCaptureLoop();

  /**
   * @brief 预览显示任务循环
   *
   * 从队列获取最新帧并更新显示
   */
  void PreviewDisplayLoop();
};

/**
 * @brief 一个 RAII 风格的辅助类，用于临时提升任务优先级。
 *
 * @details
 * 在构造时，它会保存当前任务的原始优先级，然后将任务优先级设置为指定的新优先级。
 * 在析构时（即对象离开作用域时），它会自动将任务优先级恢复为原始优先级。
 * 这对于需要临时高优先级执行以保证实时性的代码块非常有用。
 */
class TaskPriorityReset {
public:
  TaskPriorityReset(BaseType_t priority) {
    original_priority_ = uxTaskPriorityGet(NULL); // 获取当前任务优先级
    vTaskPrioritySet(NULL, priority);             // 设置新优先级
  }
  ~TaskPriorityReset() {
    vTaskPrioritySet(NULL, original_priority_); // 恢复原始优先级
  }

private:
  BaseType_t original_priority_; // 保存原始任务优先级
};

#endif // _APPLICATION_H_
