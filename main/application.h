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
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h> // FreeRTOS 事件组，用于任务间同步
#include <freertos/task.h>
#include <esp_timer.h>             // ESP32 高精度定时器

// C++ 标准库
#include <string>
#include <mutex>
#include <deque>
#include <memory>

// 项目内部模块头文件
#include "protocol.h"           // 通信协议模块
#include "ota.h"                // OTA (Over-the-Air) 更新模块
#include "audio_service.h"      // 音频服务模块
#include "device_state_event.h" // 设备状态和事件定义
#include "monitor_service.h"    // 监控服务模块
#include "lock_control/lock_control.h" // 锁控服务模块

// ======================= 主事件循环事件位定义 =======================
// 使用 FreeRTOS 事件组的位（bit）来触发不同的事件处理
#define MAIN_EVENT_SCHEDULE             (1 << 0) // 调度执行一个回调任务
#define MAIN_EVENT_SEND_AUDIO           (1 << 1) // 发送音频数据事件
#define MAIN_EVENT_WAKE_WORD_DETECTED   (1 << 2) // 检测到唤醒词事件
#define MAIN_EVENT_VAD_CHANGE           (1 << 3) // VAD (语音活动检测) 状态改变事件
#define MAIN_EVENT_ERROR                (1 << 4) // 发生错误事件
#define MAIN_EVENT_CHECK_NEW_VERSION_DONE (1 << 5) // 检查新版本完成事件
#define MAIN_EVENT_CLOCK_TICK           (1 << 6) // 时钟节拍事件 (用于定时任务)

/**
 * @brief 声学回声消除 (AEC) 模式枚举。
 */
enum AecMode {
    kAecOff,            // 关闭 AEC
    kAecOnDeviceSide,   // 在设备端进行 AEC
    kAecOnServerSide,   // 在服务器端进行 AEC
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
    static Application& GetInstance() {
        static Application instance;
        return instance;
    }
    // 删除拷贝构造函数和赋值运算符，确保单例模式
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

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
    void Alert(const char* status, const char* message, const char* emotion = "", const std::string_view& sound = "");
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
    void WakeWordInvoke(const std::string& wake_word);
    /** @brief 执行固件升级。 */
    bool UpgradeFirmware(Ota& ota, const std::string& url = "");
    /** @brief 检查设备是否可以进入睡眠模式。 */
    bool CanEnterSleepMode();
    /** @brief 发送 MCP (Multi-End Control Protocol) 消息。 */
    void SendMcpMessage(const std::string& payload);
    /** @brief 设置 AEC 模式。 */
    void SetAecMode(AecMode mode);
    /** @brief 获取当前 AEC 模式。 */
    AecMode GetAecMode() const { return aec_mode_; }
    /** @brief 播放一个指定的音效。 */
    void PlaySound(const std::string_view& sound);
    /** @brief 获取音频服务对象的引用。 */
    AudioService& GetAudioService() { return audio_service_; }
    /** @brief 启动监控模式。 */
    bool StartMonitorMode();
    /** @brief 停止监控模式。 */
    void StopMonitorMode();
    /** @brief 检查是否处于监控模式。 */
    bool IsMonitorMode() const;

private:
    /** @brief 私有构造函数，用于单例模式。 */
    Application();
    /** @brief 私有析构函数。 */
    ~Application();

    std::mutex mutex_;                                  // 互斥锁，用于保护共享资源（如 main_tasks_）
    std::deque<std::function<void()>> main_tasks_;      // 主任务队列，用于存储待执行的回调函数
    std::unique_ptr<Protocol> protocol_;                // 通信协议处理对象的智能指针
    EventGroupHandle_t event_group_ = nullptr;          // FreeRTOS 事件组句柄
    esp_timer_handle_t clock_timer_handle_ = nullptr;   // ESP 高精度定时器句柄
    volatile DeviceState device_state_ = kDeviceStateUnknown; // 当前设备状态（volatile 关键字提示编译器该变量可能在外部被意外修改）
    ListeningMode listening_mode_ = kListeningModeAutoStop; // 聆听模式
    AecMode aec_mode_ = kAecOff;                        // AEC 模式
    std::string last_error_message_;                    // 最后一次的错误信息
    AudioService audio_service_;                        // 音频服务对象
    std::unique_ptr<MonitorService> monitor_service_;   // 监控服务对象
    xiaozhi::LockControlService* lock_control_;         // 锁控服务对象指针

    bool has_server_time_ = false;                      // 是否已从服务器获取时间
    bool aborted_ = false;                              // 是否已中止 TTS 播放
    int clock_ticks_ = 0;                               // 时钟节拍计数
    bool face_recognition_in_progress_ = false;         // 人脸识别是否正在进行
    TaskHandle_t check_new_version_task_handle_ = nullptr; // 检查新版本任务的句柄
    TaskHandle_t main_event_loop_task_handle_ = nullptr;   // 主事件循环任务的句柄

    // --- 私有方法 ---
    void OnWakeWordDetected();                          // 唤醒词检测到的处理函数
    void CheckNewVersion(Ota& ota);                     // 检查新固件版本
    void CheckAssetsVersion();                          // 检查资源文件版本
    void ShowActivationCode(const std::string& code, const std::string& message); // 显示激活码
    void SetListeningMode(ListeningMode mode);          // 设置聆听模式
    
    // 锁控相关方法
    void HandleLockEvent(const xiaozhi::LockMessage& msg); // 处理锁控事件
    void TriggerFaceRecognition();                      // 触发人脸识别
    void HandleFaceRecognitionResult(cJSON* root);      // 处理人脸识别结果
    void HandleTamperAlert(uint8_t level);              // 处理暴力破坏警报
    void HandleDoorNotClosed();                         // 处理门未关严实
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
