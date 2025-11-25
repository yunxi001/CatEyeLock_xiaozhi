/**
 * @file application.cc
 * @brief Application 类的实现文件。
 * @author 78
 * @date 2024-07-20
 * 
 * @details
 * 包含了 Application 类的所有方法的具体实现，是整个项目的核心逻辑所在。
 * 它处理设备状态机、事件循环、模块交互（音频、协议、显示等）以及业务流程（如激活、升级）。
 */

#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "assets.h"
#include "settings.h"

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>

#define TAG "Application" // 日志标签


// 将设备状态枚举映射为字符串，方便日志输出
static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state"
};

/**
 * @brief Application 类的构造函数。
 * 
 * @details
 * - 创建 FreeRTOS 事件组用于任务间通信。
 * - 根据 menuconfig 中的配置决定 AEC (回声消除) 的默认模式。
 * - 创建一个周期性定时器，用于触发时钟节拍事件 (MAIN_EVENT_CLOCK_TICK)。
 */
Application::Application() {
    // 创建事件组
    event_group_ = xEventGroupCreate();

// 编译时检查 AEC 配置，确保设备端和服务器端 AEC 不会同时启用
#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide; // 设备端 AEC
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide; // 服务器端 AEC
#else
    aec_mode_ = kAecOff;          // 关闭 AEC
#endif

    // 配置并创建时钟定时器
    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            // 定时器回调函数：设置时钟节拍事件位
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this, // 将当前 Application 实例作为参数传给回调
        .dispatch_method = ESP_TIMER_TASK, // 通过任务分派回调，避免在 ISR 中执行过长操作
        .name = "clock_timer",
        .skip_unhandled_events = true // 如果事件未被及时处理，则跳过
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

/**
 * @brief Application 类的析构函数。
 * 
 * @details
 * 负责释放构造函数中申请的资源，如定时器和事件组。
 */
Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

/**
 * @brief 检查并应用新的资源包 (Assets)。
 * 
 * @details
 * 从设置中读取 "download_url"，如果存在，则下载、更新资源包（如字体、图片、音效），
 * 并在屏幕上显示下载进度。
 */
void Application::CheckAssetsVersion() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    // 如果当前板型禁用了资源分区，则直接返回
    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    // 从 "assets" 命名空间读取设置
    Settings settings("assets", true);
    // 检查是否存在新的资源下载链接
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        // 下载前先擦除该键，避免重复下载
        settings.EraseKey("download_url");

        // 准备并显示提示信息
        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // 等待提示音播放完毕
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading); // 进入升级状态
        board.SetPowerSaveMode(false); // 升级期间禁用省电模式
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        // 开始下载，并提供一个用于显示进度的回调函数
        bool success = assets.Download(download_url, [display](int progress, size_t speed) -> void {
            // 在新线程中更新 UI，避免阻塞下载任务
            std::thread([display, progress, speed]() {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                display->SetChatMessage("system", buffer);
            }).detach();
        });

        board.SetPowerSaveMode(true); // 恢复省电模式
        vTaskDelay(pdMS_TO_TICKS(1000));

        // 处理下载失败的情况
        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            return;
        }
    }

    // 应用资源包（即使没有下载，也需要加载一次默认资源）
    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

/**
 * @brief 检查新固件版本并处理设备激活流程。
 * 
 * @param ota Ota 类的实例，用于执行版本检查和升级。
 * @details
 * 这是一个阻塞性的循环，直到版本检查和激活流程完成。
 * 1. 尝试检查新版本，如果失败则带指数退避策略重试。
 * 2. 如果有新版本，则执行 `UpgradeFirmware`。
 * 3. 如果没有新版本，则标记当前版本为有效。
 * 4. 如果需要激活，则显示激活码并轮询激活状态。
 */
void Application::CheckNewVersion(Ota& ota) {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    auto& board = Board::GetInstance();
    while (true) {
        SetDeviceState(kDeviceStateActivating); // 进入激活中状态
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        esp_err_t err = ota.CheckVersion();
        if (err != ESP_OK) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

<<<<<<< HEAD
            // 显示重试信息
=======
            char error_message[128];
            snprintf(error_message, sizeof(error_message), "code=%d, url=%s", err, ota.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            // 等待指定时间后重试
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle) { // 如果用户手动退出，则中断等待
                    break;
                }
            }
            retry_delay *= 2; // 每次重试后延迟时间翻倍
            continue;
        }
        // 成功后重置重试计数器
        retry_count = 0;
        retry_delay = 10;

        // 如果有新版本，则开始升级
        if (ota.HasNewVersion()) {
            if (UpgradeFirmware(ota)) {
                return; // 升级成功后会重启，此行不会执行
            }
            // 如果升级失败，则继续正常流程
        }

        // 没有新版本，标记当前固件为有效，避免下次重复检查
        ota.MarkCurrentVersionValid();
        // 如果不需要激活，则设置事件位并退出循环
        if (!ota.HasActivationCode() && !ota.HasActivationChallenge()) {
            xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // 如果有激活码，则显示给用户
        if (ota.HasActivationCode()) {
            ShowActivationCode(ota.GetActivationCode(), ota.GetActivationMessage());
        }

        // 循环轮询激活状态，直到成功或超时
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota.Activate();
            if (err == ESP_OK) { // 激活成功
                xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
                break;
            } else if (err == ESP_ERR_TIMEOUT) { // 超时，继续轮询
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else { // 其他错误，等待更长时间
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle) { // 用户手动退出
                break;
            }
        }
    }
}

/**
 * @brief 在屏幕上显示激活码，并用语音播报数字。
 * 
 * @param code 要显示的激活码字符串。
 * @param message 伴随激活码显示的提示信息。
 */
void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    // 定义数字与其对应音效的映射
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // 首先显示提示信息并播放提示音
    // 注意：此处的提示信息可能较长，会占用较多内存，需要确保系统有足够资源
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    // 依次播放激活码中每个数字的读音
    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

/**
 * @brief 显示一个全局警报/提示。
 * 
 * @param status 状态栏要显示的文本。
 * @param message 聊天框要显示的文本。
 * @param emotion 要显示的表情。
 * @param sound 要播放的提示音。
 */
void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

/**
 * @brief 取消当前的警报/提示，恢复到空闲状态的显示。
 */
void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

/**
 * @brief 切换聊天状态。
 * 
 * @details
 * 这是响应用户主要交互（如按键）的核心函数。
 * 它根据当前设备状态决定执行何种操作，如开始聆听、停止播报等。
 */
void Application::ToggleChatState() {
    // 在激活状态下，按键则退出激活流程，进入空闲状态
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    // 在 WiFi 配置状态下，按键进入/退出音频测试模式
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    // 确保通信协议已初始化
    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    // 根据当前状态执行不同操作
    if (device_state_ == kDeviceStateIdle) { // 空闲时 -> 开始聆听
        // 使用 Schedule 将任务抛到主事件循环中执行，以保证线程安全
        Schedule([this]() {
            // 如果音频通道未打开，则先连接
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return; // 连接失败则返回
                }
            }
            // 根据 AEC 模式决定是自动停止还是实时流
            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        });
    } else if (device_state_ == kDeviceStateSpeaking) { // 播报时 -> 中断播报
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) { // 聆听时 -> 停止聆听
        Schedule([this]() {
            protocol_->CloseAudioChannel();
        });
    }
}

/**
 * @brief 强制开始聆听（通常用于长按等场景）。
 */
void Application::StartListening() {
    // 处理激活和 WiFi 配置状态下的特殊逻辑
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    // 在空闲或播报状态下，都可以切换到手动聆听模式
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }
            // 设置为手动停止模式
            SetListeningMode(kListeningModeManualStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop);
        });
    }
}

/**
 * @brief 强制停止聆听（通常用于松开长按等场景）。
 */
void Application::StopListening() {
    // 退出音频测试
    if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    // 仅在特定状态下有效
    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // 如果当前状态无效，则不执行任何操作
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end()) {
        return;
    }

    // 在聆听状态下，发送停止指令并切换到空闲
    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            protocol_->SendStopListening();
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

/**
 * @brief 启动应用程序的主流程。
 * 
 * @details
 * 这是应用程序的核心初始化函数，在 app_main 中被调用。
 * 它按顺序执行以下操作：
 * 1. 设置初始设备状态为 `kDeviceStateStarting`。
 * 2. 初始化并启动显示和音频服务。
 * 3. 为音频服务设置回调函数，以响应唤醒、VAD等事件。
 * 4. 创建并启动主事件循环任务。
 * 5. 启动网络连接。
 * 6. 检查资源包和固件版本。
 * 7. 根据配置初始化通信协议 (MQTT 或 WebSocket)。
 * 8. 为通信协议设置各种事件的回调函数（如连接、断开、收到消息等）。
 * 9. 完成所有初始化后，将设备状态设置为 `kDeviceStateIdle`。
 */
void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* 1. 设置显示屏 */
    auto display = board.GetDisplay();
    // 在屏幕上显示版本等信息
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    /* 2. 设置音频服务 */
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    // 为音频服务设置回调，通过事件组将音频事件通知给主循环
    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    // 3. 创建主事件循环任务
    xTaskCreate([](void* arg) {
        ((Application*)arg)->MainEventLoop();
        vTaskDelete(NULL);
    }, "main_event_loop", 2048 * 4, this, 3, &main_event_loop_task_handle_);

    // 启动周期性定时器，用于更新状态栏等
    esp_timer_start_periodic(clock_timer_handle_, 1000000); // 1秒触发一次

    /* 4. 启动网络 */
    board.StartNetwork();

    // 立即更新一次状态栏以显示网络状态
    display->UpdateStatusBar(true);

    // 5. 检查资源和固件版本
    CheckAssetsVersion();
    Ota ota;
    CheckNewVersion(ota);

    /* 6. 初始化通信协议 */
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // 初始化 MCP (多端控制协议) 服务并添加工具
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    // 根据 OTA 配置决定使用 MQTT 还是 WebSocket
    if (ota.HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota.HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    /* 7. 为通信协议设置回调 */
    protocol_->OnConnected([this]() {
        DismissAlert();
    });
    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (device_state_ == kDeviceStateSpeaking) {
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveMode(false);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveMode(true);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    // 处理收到的 JSON 消息
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) { // 文本转语音消息
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    if (device_state_ == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([this, display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            } 
        } else if (strcmp(type->valuestring, "stt") == 0) { // 语音转文本消息
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([this, display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) { // 大语言模型相关消息
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) { // MCP 协议消息
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) { // 系统命令
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    Schedule([this]() { Reboot(); });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) { // 警报消息
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) { // 自定义消息
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    bool protocol_started = protocol_->Start();

    /* 8. 完成初始化 */
    SystemInfo::PrintHeapStats(); // 打印当前堆内存信息
    SetDeviceState(kDeviceStateIdle); // 进入空闲状态

    has_server_time_ = ota.HasServerTime();
    if (protocol_started) {
        std::string message = std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // 播放成功提示音，表示设备已就绪
        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    }
}

/**
 * @brief 调度一个任务到主事件循环中执行。
 * 
 * @param callback 要执行的回调函数。
 * @details 这是一个线程安全的函数。它将一个函数（闭包）加入到任务队列，
 *          然后设置 `MAIN_EVENT_SCHEDULE` 事件位来唤醒主事件循环任务。
 */
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

/**
 * @brief 主事件循环。
 * 
 * @details
 * 这是一个死循环，作为应用程序的核心调度器。
 * 它永久阻塞等待事件组中的任何事件位被设置。
 * 收到事件后，它会处理相应的逻辑，例如发送音频、处理唤醒、执行调度任务等。
 */
void Application::MainEventLoop() {
    while (true) {
        // 等待任何已定义的事件位
        auto bits = xEventGroupWaitBits(event_group_, 
            MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK |
            MAIN_EVENT_ERROR, 
            pdTRUE,    // 退出时清除被触发的事件位
            pdFALSE,   // 不需要等待所有位置位
            portMAX_DELAY); // 永久阻塞

        // 处理错误事件
        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        // 处理发送音频事件
        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        // 处理唤醒词检测事件
        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            OnWakeWordDetected();
        }

        // 处理 VAD 状态变化事件
        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (device_state_ == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged(); // 更新 LED 状态以反映语音活动
            }
        }

        // 处理调度任务事件
        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task(); // 执行所有已调度的任务
            }
        }

        // 处理时钟节拍事件
        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar(); // 更新状态栏（如时间、WiFi信号等）
        
            // 每10秒打印一次调试信息
            if (clock_ticks_ % 10 == 0) {
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

/**
 * @brief 唤醒词被检测到时的处理逻辑。
 */
void Application::OnWakeWordDetected() {
    if (!protocol_) {
        return;
    }

    if (device_state_ == kDeviceStateIdle) { // 只有在空闲状态下才响应唤醒
        audio_service_.EncodeWakeWord(); // 对唤醒词音频进行编码

        // 如果音频通道未打开，则先连接
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true); // 连接失败，重新使能唤醒
                return;
            }
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
        // 如果配置了发送唤醒词数据，则将编码后的数据发送到服务器
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        // 否则，仅进入聆听状态并播放提示音
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) { // 如果正在播报，则中断播报
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (device_state_ == kDeviceStateActivating) { // 如果在激活流程中，则退出流程
        SetDeviceState(kDeviceStateIdle);
    }
}

/**
 * @brief 中断 TTS 播报。
 * @param reason 中断原因。
 */
void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

/**
 * @brief 设置聆听模式并切换到聆听状态。
 * @param mode 聆听模式。
 */
void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

/**
 * @brief 设置并处理设备状态的变更。
 * 
 * @param state 要设置的新状态。
 * @details 这是应用的状态机核心。当状态变更时，会执行进入新状态所需的操作，
 *          例如更新UI、启用/禁用音频处理等。
 */
void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) {
        return; // 状态未改变，直接返回
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

    // 发布状态变更事件，供其他模块监听
    DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state, state);

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged(); // 通知 LED 更新状态

    // 根据新状态执行相应操作
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            audio_service_.EnableVoiceProcessing(false); // 禁用语音处理
            audio_service_.EnableWakeWordDetection(true);  // 启用唤醒词检测
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            // 确保音频处理器正在运行
            if (!audio_service_.IsAudioProcessorRunning()) {
                protocol_->SendStartListening(listening_mode_);
                audio_service_.EnableVoiceProcessing(true);  // 启用语音处理
                audio_service_.EnableWakeWordDetection(false); // 禁用唤醒词检测
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // 在播报时，只有 AFE 硬件唤醒能生效
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder(); // 重置音频解码器
            break;
        default:
            // 其他状态不做特殊处理
            break;
    }
}

/**
 * @brief 重启设备。
 */
void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // 安全地断开连接和停止服务
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

/**
 * @brief 从指定 URL 执行固件升级。
 * 
 * @param ota Ota 实例。
 * @param url 可选的固件 URL，如果为空则使用 ota 对象中的 URL。
 * @return bool 升级成功则设备会重启，不会返回 true。升级失败返回 false。
 */
bool Application::UpgradeFirmware(Ota& ota, const std::string& url) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    
    std::string upgrade_url = url.empty() ? ota.GetFirmwareUrl() : url;
    std::string version_info = url.empty() ? ota.GetFirmwareVersion() : "(Manual upgrade)";
    
    // 升级前关闭音频通道
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());
    
    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);
    
    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveMode(false);
    audio_service_.Stop(); // 停止音频服务以释放内存
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 开始 OTA 升级，并提供进度回调
    bool upgrade_success = ota.StartUpgradeFromUrl(upgrade_url, [display](int progress, size_t speed) {
        std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            display->SetChatMessage("system", buffer);
        }).detach();
    });

    if (!upgrade_success) {
        // 升级失败，恢复运行
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start();
        board.SetPowerSaveMode(true);
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // 升级成功，准备重启
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        Reboot();
        return true; // 实际上不会执行到这里
    }
}

/**
 * @brief 外部调用以触发唤醒。
 * @param wake_word 模拟的唤醒词。
 */
void Application::WakeWordInvoke(const std::string& wake_word) {
    if (!protocol_) {
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD || CONFIG_USE_CUSTOM_WAKE_WORD
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() { AbortSpeaking(kAbortReasonNone); });
    } else if (device_state_ == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

/**
 * @brief 判断设备当前是否可以进入睡眠模式。
 */
bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;
    }
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }
    if (!audio_service_.IsIdle()) {
        return false;
    }
    return true;
}

/**
 * @brief 发送 MCP 消息。
 * @param payload 要发送的 JSON 字符串。
 */
void Application::SendMcpMessage(const std::string& payload) {
    if (protocol_ == nullptr) {
        return;
    }

    // 确保在主事件循环任务中发送，如果不是，则调度过去
    if (xTaskGetCurrentTaskHandle() == main_event_loop_task_handle_) {
        protocol_->SendMcpMessage(payload);
    } else {
        Schedule([this, payload = std::move(payload)]() {
            protocol_->SendMcpMessage(payload);
        });
    }
}

/**
 * @brief 设置 AEC 模式。
 * @param mode 新的 AEC 模式。
 */
void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // 模式改变后，关闭当前音频通道
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

/**
 * @brief 播放一个音效。
 * @param sound 要播放的音效资源。
 */
void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}