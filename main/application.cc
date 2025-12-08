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
#include "esp32_camera.h"

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>

#define TAG "Application"


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
    "monitor_connecting",
    "monitor_streaming",
    "invalid_state"
};

Application::Application() : lock_control_(nullptr) {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

void Application::CheckAssetsVersion() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    // 检查是否有新的资产需要下载

    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // 等待音频服务空闲3秒

        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveMode(false);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets.Download(download_url, [display](int progress, size_t speed) -> void {
            std::thread([display, progress, speed]() {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                display->SetChatMessage("system", buffer);
            }).detach();
        });

        board.SetPowerSaveMode(true);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            return;
        }
    }

    // 应用资产

    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion(Ota& ota) {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒" => "初始重试延迟为10秒


    auto& board = Board::GetInstance();
    while (true) {
        SetDeviceState(kDeviceStateActivating);
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        esp_err_t err = ota.CheckVersion();
        if (err != ESP_OK) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char error_message[128];
            snprintf(error_message, sizeof(error_message), "code=%d, url=%s", err, ota.GetCheckVersionUrl().c_str());
            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, error_message);
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // 每次重试后延迟时间加倍

            continue;
        }
        retry_count = 0;
        retry_delay = 10; // 重置重试延迟时间" => "重置重试的延迟时间


        if (ota.HasNewVersion()) {
            if (UpgradeFirmware(ota)) {
                return; // 重启后永远不会到达这一行

            }
            // 如果升级失败，继续正常操作（不要中断，只是继续执行）

        }

        // 没有新版本，标记当前版本为有效

        ota.MarkCurrentVersionValid();
        if (!ota.HasActivationCode() && !ota.HasActivationChallenge()) {
            xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
            // 检查新版本完成后退出循环

            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // 激活码显示给用户，等待用户输入

        if (ota.HasActivationCode()) {
            ShowActivationCode(ota.GetActivationCode(), ota.GetActivationMessage());
        }

        // 这将在激活完成或超时之前阻塞循环

        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota.Activate();
            if (err == ESP_OK) {
                xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
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

    // 这句话使用了9KB的SRAM，因此我们需要等待它完成。

    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

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

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            protocol_->CloseAudioChannel();
        });
    }
}

void Application::StartListening() {
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
    
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(kListeningModeManualStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop);
        });
    }
}

void Application::StopListening() {
    if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // 如果不有效，什么也不做

    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end()) {
        return;
    }

    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            protocol_->SendStopListening();
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* 设置显示 */
    auto display = board.GetDisplay();

    // 打印电路板名称/版本信息

    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    /* 设置音频服务 */
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

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

    // 以优先级3启动主事件循环任务

    xTaskCreate([](void* arg) {
        ((Application*)arg)->MainEventLoop();
        vTaskDelete(NULL);
    }, "main_event_loop", 2048 * 4, this, 3, &main_event_loop_task_handle_);

    /* 开始时钟计时器以更新状态栏 */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    /* 等待网络准备就绪 */
    board.StartNetwork();

    // 立即更新状态栏以显示网络状态

    display->UpdateStatusBar(true);

    // 检查新资产版本

    CheckAssetsVersion();

    // 检查新固件版本或获取MQTT代理地址

    Ota ota;
    CheckNewVersion(ota);

    // 初始化协议

    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // 在初始化协议之前添加MCP通用工具

    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    if (ota.HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota.HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        // 在 Speaking 状态或监控模式下播放音频
        if (device_state_ == kDeviceStateSpeaking || device_state_ == kDeviceStateMonitorStreaming) {
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
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // 解析JSON数据

        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
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
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([this, display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // 如果用户请求OTA更新，则进行重启

                    Schedule([this]() {
                        Reboot();
                    });
                } else if (strcmp(command->valuestring, "start_monitor") == 0) {
                    // 启动监控模式
                    Schedule([this]() {
                        if (!IsMonitorMode()) {
                            ESP_LOGI(TAG, "Starting monitor mode");
                            if (StartMonitorMode()) {
                                ESP_LOGI(TAG, "Monitor mode started successfully");
                            } else {
                                ESP_LOGE(TAG, "Failed to start monitor mode");
                            }
                        }
                    });
                } else if (strcmp(command->valuestring, "stop_monitor") == 0) {
                    // 停止监控模式
                    Schedule([this]() {
                        if (IsMonitorMode()) {
                            ESP_LOGI(TAG, "Stopping monitor mode");
                            StopMonitorMode();
                        }
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "face_recognition") == 0) {
            // 处理人脸识别结果
            Schedule([this, root_copy = cJSON_Duplicate(root, 1)]() {
                HandleFaceRecognitionResult(root_copy);
                cJSON_Delete(root_copy);
            });
        } else if (strcmp(type->valuestring, "lock_control") == 0) {
            // 处理锁控命令
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "Lock control command: %s", command->valuestring);
                Schedule([this, cmd = std::string(command->valuestring), root_copy = cJSON_Duplicate(root, 1)]() {
                    if (!lock_control_) {
                        ESP_LOGW(TAG, "Lock control service not available");
                        cJSON_Delete(root_copy);
                        return;
                    }
                    
                    if (cmd == "unlock") {
                        lock_control_->SendUnlock();
                    } else if (cmd == "temp_code") {
                        auto code = cJSON_GetObjectItem(root_copy, "code");
                        if (cJSON_IsString(code)) {
                            lock_control_->SendTempCode(code->valuestring);
                        }
                    } else if (cmd == "alarm_on") {
                        auto level = cJSON_GetObjectItem(root_copy, "level");
                        uint8_t alarm_level = cJSON_IsNumber(level) ? level->valueint : 1;
                        lock_control_->SendAlarm(alarm_level);
                    } else if (cmd == "alarm_off") {
                        lock_control_->SendAlarmOff();
                    }
                    cJSON_Delete(root_copy);
                });
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
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

    // 初始化锁控服务（仅在支持的板子上）
    // 注意：这里需要根据实际板子类型来获取锁控服务
    // 暂时设置为 nullptr，后续在板子初始化时会设置
    lock_control_ = nullptr;

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota.HasServerTime();
    if (protocol_started) {
        std::string message = std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // 播放成功音效以指示设备已准备好

        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    }
}

// 添加异步任务到主循环

void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

// 主事件循环控制聊天状态和WebSocket连接
// 如果其他任务需要访问WebSocket或聊天状态，
// 它们应该使用调度来调用此函数

void Application::MainEventLoop() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK |
            MAIN_EVENT_ERROR, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            OnWakeWordDetected();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (device_state_ == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();
        
            // 每10秒打印调试信息

            if (clock_ticks_ % 10 == 0) {
                SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
                SystemInfo::PrintTaskList();
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

void Application::OnWakeWordDetected() {
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

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
        // 将唤醒词数据编码并发送到服务器

        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // 将聊天状态设置为“检测到唤醒词”

        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        // 播放弹出声音以指示已检测到唤醒词

        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) {
        return;
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

    // 发送状态改变事件

    DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state, state);

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
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
                // 发送开始监听命令

                protocol_->SendStartListening(listening_mode_);
                audio_service_.EnableVoiceProcessing(true);
                audio_service_.EnableWakeWordDetection(false);
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // 仅能检测到说话模式下的AFE唤醒词

                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;
        default:
            // 什么都不做

            break;
    }
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // 断开音频通道

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(Ota& ota, const std::string& url) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    
    // 使用提供的URL或从OTA对象获取

    std::string upgrade_url = url.empty() ? ota.GetFirmwareUrl() : url;
    std::string version_info = url.empty() ? ota.GetFirmwareVersion() : "(Manual upgrade)";
    
    // 如果已打开，请关闭音频通道

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
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = ota.StartUpgradeFromUrl(upgrade_url, [display](int progress, size_t speed) {
        std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            display->SetChatMessage("system", buffer);
        }).detach();
    });

    if (!upgrade_success) {
        // 升级失败，重启音频服务并继续运行

        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start(); // 重新启动音频服务

        board.SetPowerSaveMode(true); // 恢复省电模式

        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // 升级成功，立即重启

        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // 短暂暂停以显示消息

        Reboot();
        return true;
    }
}

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
        // 将唤醒词数据编码并发送到服务器

        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // 将聊天状态设置为“检测到唤醒词”

        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        // 播放弹出声音以指示已检测到唤醒词

        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

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

    // 现在可以安全地进入睡眠模式

    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    if (protocol_ == nullptr) {
        return;
    }

    // 确保您正在使用主线程发送MCP消息

    if (xTaskGetCurrentTaskHandle() == main_event_loop_task_handle_) {
        protocol_->SendMcpMessage(payload);
    } else {
        Schedule([this, payload = std::move(payload)]() {
            protocol_->SendMcpMessage(payload);
        });
    }
}

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

        // 如果更改了AEC模式，请关闭音频通道

        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}

bool Application::StartMonitorMode() {
    if (monitor_service_ && monitor_service_->IsRunning()) {
        ESP_LOGW(TAG, "Monitor mode already running");
        return false;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return false;
    }

    auto& board = Board::GetInstance();
    auto camera = board.GetCamera();
    if (!camera) {
        ESP_LOGE(TAG, "Camera not available");
        return false;
    }

    // 创建监控服务
    monitor_service_ = std::make_unique<MonitorService>();
    
    // 设置状态变化回调
    monitor_service_->SetStateChangeCallback([this](bool connected) {
        if (connected) {
            SetDeviceState(kDeviceStateMonitorStreaming);
        } else {
            SetDeviceState(kDeviceStateIdle);
        }
    });

    // 启动监控服务
    if (!monitor_service_->Start(protocol_.get(), camera, &audio_service_)) {
        ESP_LOGE(TAG, "Failed to start monitor service");
        monitor_service_.reset();
        return false;
    }

    ESP_LOGI(TAG, "Monitor mode started");
    return true;
}

void Application::StopMonitorMode() {
    if (monitor_service_) {
        monitor_service_->Stop();
        monitor_service_.reset();
        ESP_LOGI(TAG, "Monitor mode stopped");
    }
}

bool Application::IsMonitorMode() const {
    return monitor_service_ && monitor_service_->IsRunning();
}

// ==================== 锁控相关函数 ====================

void Application::HandleLockEvent(const xiaozhi::LockMessage& msg) {
    ESP_LOGI(TAG, "Received lock event: CAT=0x%02X, TYPE=0x%02X", msg.category, msg.type);
    
    // 如果处于监控模式，忽略锁控事件
    if (IsMonitorMode()) {
        ESP_LOGW(TAG, "Ignoring lock event in monitor mode");
        return;
    }
    
    // 根据事件类型分发处理
    if (msg.IsEvent()) {
        switch (msg.type) {
            case static_cast<uint8_t>(xiaozhi::EventType::DOORBELL_PRESSED):
                ESP_LOGI(TAG, "Doorbell pressed - triggering face recognition");
                TriggerFaceRecognition();
                break;
                
            case static_cast<uint8_t>(xiaozhi::EventType::HUMAN_DETECTED):
                ESP_LOGI(TAG, "Human detected - triggering face recognition");
                TriggerFaceRecognition();
                break;
                
            case static_cast<uint8_t>(xiaozhi::EventType::LOCK_TAMPER):
                ESP_LOGI(TAG, "Lock tamper detected");
                HandleTamperAlert(msg.data[0]);
                break;
                
            case static_cast<uint8_t>(xiaozhi::EventType::PERSON_LEFT):
                ESP_LOGI(TAG, "Person left");
                Alert("", "再见", "", "");
                break;
                
            case static_cast<uint8_t>(xiaozhi::EventType::PERSON_ENTERED):
                ESP_LOGI(TAG, "Person entered");
                Alert("", "欢迎回家", "", "");
                break;
                
            case static_cast<uint8_t>(xiaozhi::EventType::DOOR_NOT_CLOSED):
                ESP_LOGI(TAG, "Door not closed");
                HandleDoorNotClosed();
                break;
                
            case static_cast<uint8_t>(xiaozhi::EventType::PASSWORD_ERROR):
                ESP_LOGI(TAG, "Password error");
                Alert("", "密码错误", "", "");
                break;
                
            case static_cast<uint8_t>(xiaozhi::EventType::LOCK_LOCKED):
                ESP_LOGI(TAG, "Lock locked");
                Alert("", "已锁定", "", "");
                break;
                
            default:
                ESP_LOGW(TAG, "Unknown event type: 0x%02X", msg.type);
                break;
        }
    }
}

void Application::TriggerFaceRecognition() {
    int64_t start_time = esp_timer_get_time();
    ESP_LOGI(TAG, "Face recognition triggered");
    
    // 0. 检查是否已经在进行人脸识别
    if (face_recognition_in_progress_) {
        ESP_LOGW(TAG, "Face recognition already in progress, ignoring trigger");
        return;
    }
    
    // 1. 检查设备状态是否为 Idle
    if (device_state_ != kDeviceStateIdle) {
        ESP_LOGW(TAG, "Device not idle, ignoring face recognition trigger (state=%s)", 
                 STATE_STRINGS[device_state_]);
        return;
    }
    
    // 设置标志
    face_recognition_in_progress_ = true;
    
    // 1.5. 检查可用内存
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t MIN_FREE_MEMORY = 100 * 1024; // 100KB
    if (free_psram < MIN_FREE_MEMORY) {
        ESP_LOGW(TAG, "Low memory, rejecting face recognition (free PSRAM: %zu KB)", 
                 free_psram / 1024);
        return;
    }
    ESP_LOGI(TAG, "Memory check passed (free PSRAM: %zu KB)", free_psram / 1024);
    
    // 2. 检查摄像头是否可用
    auto& board = Board::GetInstance();
    auto camera = board.GetCamera();
    if (!camera) {
        ESP_LOGE(TAG, "Camera not available, aborting face recognition");
        face_recognition_in_progress_ = false;
        return;
    }
    
    // 3. 检查音频通道是否打开
    if (!protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Opening audio channel for face recognition");
        protocol_->OpenAudioChannel();
    }
    
    int64_t capture_start = esp_timer_get_time();
    ESP_LOGI(TAG, "Starting face recognition process (trigger->start: %lld ms)", 
             (capture_start - start_time) / 1000);
    
    // 4. 拍照
    if (!camera->Capture()) {
        ESP_LOGE(TAG, "Failed to capture photo");
        face_recognition_in_progress_ = false;
        return;
    }
    
    int64_t capture_end = esp_timer_get_time();
    ESP_LOGI(TAG, "Photo captured successfully (capture time: %lld ms)", 
             (capture_end - capture_start) / 1000);
    
    // 5. JPEG 编码
    // 注意：Camera 基类没有 CaptureJpeg 方法，需要转换为 Esp32Camera
    #ifndef CONFIG_IDF_TARGET_ESP32
    auto esp32_camera = dynamic_cast<Esp32Camera*>(camera);
    if (!esp32_camera) {
        ESP_LOGE(TAG, "Camera is not Esp32Camera, cannot encode JPEG");
        return;
    }
    
    uint8_t* jpeg_data = nullptr;
    size_t jpeg_size = 0;
    if (!esp32_camera->CaptureJpeg(&jpeg_data, &jpeg_size, 80)) {
        ESP_LOGE(TAG, "Failed to encode JPEG");
        face_recognition_in_progress_ = false;
        return;
    }
    
    // Check if memory allocation succeeded
    if (jpeg_data == nullptr || jpeg_size == 0) {
        ESP_LOGE(TAG, "JPEG encoding failed: no data allocated");
        face_recognition_in_progress_ = false;
        return;
    }
    
    int64_t encode_end = esp_timer_get_time();
    ESP_LOGI(TAG, "JPEG encoded: %zu bytes (encode time: %lld ms)", 
             jpeg_size, (encode_end - capture_end) / 1000);
    
    // 6. 发送视频帧
    int64_t send_start = esp_timer_get_time();
    int64_t timestamp = send_start / 1000; // 转换为毫秒
    int width = esp32_camera->GetFrameWidth();
    int height = esp32_camera->GetFrameHeight();
    #else
    ESP_LOGE(TAG, "Face recognition not supported on ESP32");
    return;
    #endif
    
    if (!protocol_->SendVideo(jpeg_data, jpeg_size, timestamp, width, height)) {
        ESP_LOGE(TAG, "Failed to send video frame");
        heap_caps_free(jpeg_data);
        face_recognition_in_progress_ = false;
        return;
    }
    
    int64_t send_end = esp_timer_get_time();
    ESP_LOGI(TAG, "Video frame sent successfully (send time: %lld ms)", 
             (send_end - send_start) / 1000);
    
    // 7. 释放内存
    heap_caps_free(jpeg_data);
    
    int64_t total_time = (send_end - start_time) / 1000;
    ESP_LOGI(TAG, "Face recognition complete (total time: %lld ms)", total_time);
    
    // 清除标志
    face_recognition_in_progress_ = false;
}

void Application::HandleFaceRecognitionResult(cJSON* root) {
    ESP_LOGI(TAG, "Handling face recognition result");
    
    // 1. 解析 result 字段
    auto result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsString(result)) {
        ESP_LOGW(TAG, "Invalid face recognition result: missing result field");
        return;
    }
    
    ESP_LOGI(TAG, "Face recognition result: %s", result->valuestring);
    
    // 2. 解析 access 字段
    auto access = cJSON_GetObjectItem(root, "access");
    if (!cJSON_IsObject(access)) {
        ESP_LOGW(TAG, "Invalid face recognition result: missing access field");
        return;
    }
    
    auto granted = cJSON_GetObjectItem(access, "granted");
    bool access_granted = cJSON_IsTrue(granted);
    
    // 3. 如果授权，发送开锁命令
    if (strcmp(result->valuestring, "known") == 0 && access_granted) {
        ESP_LOGI(TAG, "Access granted, sending unlock command");
        if (lock_control_) {
            lock_control_->SendUnlock();
        } else {
            ESP_LOGW(TAG, "Lock control service not available");
        }
    } else {
        ESP_LOGI(TAG, "Access denied or unknown person");
    }
}

void Application::HandleTamperAlert(uint8_t level) {
    ESP_LOGI(TAG, "Handling tamper alert, level=%d", level);
    
    // 1. 激活警报
    if (lock_control_) {
        lock_control_->SendAlarm(level);
    }
    
    // 2. 上报服务器
    char payload[128];
    snprintf(payload, sizeof(payload), 
             "{\"type\":\"lock_alert\",\"event\":\"tamper\",\"level\":%d}", level);
    SendMcpMessage(payload);
    
    // 3. 显示警报
    Alert("警报", "检测到暴力破坏", "triangle_exclamation", Lang::Sounds::OGG_EXCLAMATION);
}

void Application::HandleDoorNotClosed() {
    ESP_LOGI(TAG, "Handling door not closed");
    
    // 1. 上报服务器
    const char* payload = "{\"type\":\"lock_alert\",\"event\":\"door_not_closed\"}";
    SendMcpMessage(payload);
    
    // 2. 显示提示
    Alert("提示", "门未关严实", "door_open", "");
}
