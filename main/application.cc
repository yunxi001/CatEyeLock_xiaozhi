#include "application.h"
#include "assets.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "display.h"
#include "esp32_camera.h"
#include "lcd_display.h"
#include "local_preview/preview_frame.h"
#include "mcp_server.h"
#include "mqtt_protocol.h"
#include "settings.h"
#include "system_info.h"
#include "websocket_protocol.h"

#include <arpa/inet.h>
#include <cJSON.h>
#include <cstring>
#include <ctime>
#include <driver/gpio.h>
#include <esp_log.h>
#include <font_awesome.h>
#include <vector>

#define TAG "Application"

static const char *const STATE_STRINGS[] = {
    "unknown",           "starting",      "configuring", "idle",
    "connecting",        "listening",     "speaking",    "upgrading",
    "activating",        "audio_testing", "fatal_error", "monitor_connecting",
    "monitor_streaming", "invalid_state"};

Application::Application() : lock_control_(nullptr) {
  event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error                                                                         \
    "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
  aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
  aec_mode_ = kAecOnServerSide;
#else
  aec_mode_ = kAecOff;
#endif

  esp_timer_create_args_t clock_timer_args = {
      .callback =
          [](void *arg) {
            Application *app = (Application *)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
          },
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "clock_timer",
      .skip_unhandled_events = true};
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
  auto &board = Board::GetInstance();
  auto display = board.GetDisplay();
  auto &assets = Assets::GetInstance();

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
    snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS,
             download_url.c_str());
    Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down",
          Lang::Sounds::OGG_UPGRADE);

    // 等待音频服务空闲3秒

    vTaskDelay(pdMS_TO_TICKS(3000));
    SetDeviceState(kDeviceStateUpgrading);
    board.SetPowerSaveMode(false);
    display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

    bool success = assets.Download(
        download_url, [display](int progress, size_t speed) -> void {
          std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress,
                     speed / 1024);
            display->SetChatMessage("system", buffer);
          }).detach();
        });

    board.SetPowerSaveMode(true);
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (!success) {
      Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED,
            "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
      vTaskDelay(pdMS_TO_TICKS(2000));
      return;
    }
  }

  // 应用资产

  assets.Apply();
  display->SetChatMessage("system", "");
  display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion(Ota &ota) {
  const int MAX_RETRY = 10;
  int retry_count = 0;
  int retry_delay = 10; // 初始重试延迟为10秒" => "初始重试延迟为10秒

  auto &board = Board::GetInstance();
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
      snprintf(error_message, sizeof(error_message), "code=%d, url=%s", err,
               ota.GetCheckVersionUrl().c_str());
      char buffer[256];
      snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED,
               retry_delay, error_message);
      Alert(Lang::Strings::ERROR, buffer, "cloud_slash",
            Lang::Sounds::OGG_EXCLAMATION);

      ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)",
               retry_delay, retry_count, MAX_RETRY);
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

void Application::ShowActivationCode(const std::string &code,
                                     const std::string &message) {
  struct digit_sound {
    char digit;
    const std::string_view &sound;
  };
  static const std::array<digit_sound, 10> digit_sounds{
      {digit_sound{'0', Lang::Sounds::OGG_0},
       digit_sound{'1', Lang::Sounds::OGG_1},
       digit_sound{'2', Lang::Sounds::OGG_2},
       digit_sound{'3', Lang::Sounds::OGG_3},
       digit_sound{'4', Lang::Sounds::OGG_4},
       digit_sound{'5', Lang::Sounds::OGG_5},
       digit_sound{'6', Lang::Sounds::OGG_6},
       digit_sound{'7', Lang::Sounds::OGG_7},
       digit_sound{'8', Lang::Sounds::OGG_8},
       digit_sound{'9', Lang::Sounds::OGG_9}}};

  // 这句话使用了9KB的SRAM，因此我们需要等待它完成。

  Alert(Lang::Strings::ACTIVATION, message.c_str(), "link",
        Lang::Sounds::OGG_ACTIVATION);

  for (const auto &digit : code) {
    auto it = std::find_if(
        digit_sounds.begin(), digit_sounds.end(),
        [digit](const digit_sound &ds) { return ds.digit == digit; });
    if (it != digit_sounds.end()) {
      audio_service_.PlaySound(it->sound);
    }
  }
}

void Application::Alert(const char *status, const char *message,
                        const char *emotion, const std::string_view &sound) {
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

      SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop
                                            : kListeningModeRealtime);
    });
  } else if (device_state_ == kDeviceStateSpeaking) {
    Schedule([this]() { AbortSpeaking(kAbortReasonNone); });
  } else if (device_state_ == kDeviceStateListening) {
    // 用户主动断开连接
    user_manually_disconnected_ = true;
    Schedule([this]() { protocol_->CloseAudioChannel(); });
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

  if (std::find(valid_states.begin(), valid_states.end(), device_state_) ==
      valid_states.end()) {
    return;
  }

  // 用户主动停止监听
  user_manually_disconnected_ = true;

  Schedule([this]() {
    if (device_state_ == kDeviceStateListening) {
      protocol_->SendStopListening();
      SetDeviceState(kDeviceStateIdle);
    }
  });
}

void Application::Start() {
  auto &board = Board::GetInstance();
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
  callbacks.on_wake_word_detected = [this](const std::string &wake_word) {
    xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
  };
  callbacks.on_vad_change = [this](bool speaking) {
    xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
  };
  audio_service_.SetCallbacks(callbacks);

  // 以优先级3启动主事件循环任务

  xTaskCreate(
      [](void *arg) {
        ((Application *)arg)->MainEventLoop();
        vTaskDelete(NULL);
      },
      "main_event_loop", 2048 * 4, this, 3, &main_event_loop_task_handle_);

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

  auto &mcp_server = McpServer::GetInstance();
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

  protocol_->OnConnected([this]() { DismissAlert(); });

  protocol_->OnNetworkError([this](const std::string &message) {
    last_error_message_ = message;
    xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
  });
  protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
    // 在 Speaking 状态或监控模式下播放音频
    if (device_state_ == kDeviceStateSpeaking ||
        device_state_ == kDeviceStateMonitorStreaming) {
      audio_service_.PushPacketToDecodeQueue(std::move(packet));
    }
  });
  protocol_->OnAudioChannelOpened([this, codec, &board]() {
    board.SetPowerSaveMode(false);
    if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
      ESP_LOGW(TAG,
               "Server sample rate %d does not match device output sample rate "
               "%d, resampling may cause distortion",
               protocol_->server_sample_rate(), codec->output_sample_rate());
    }

    // 连接服务器成功后关闭背光（门锁模式）
    auto display = Board::GetInstance().GetDisplay();
    auto lcd_display = dynamic_cast<LcdDisplay *>(display);
    if (lcd_display) {
      lcd_display->HideAllUI();
      ESP_LOGI(TAG, "已切换到门锁模式（背光关闭）");
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
  protocol_->OnIncomingJson([this, display](const cJSON *root) {
    // 解析JSON数据

    auto type = cJSON_GetObjectItem(root, "type");
    if (strcmp(type->valuestring, "tts") == 0) {
      auto state = cJSON_GetObjectItem(root, "state");
      if (strcmp(state->valuestring, "start") == 0) {
        Schedule([this]() {
          aborted_ = false;
          if (device_state_ == kDeviceStateIdle ||
              device_state_ == kDeviceStateListening) {
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
        Schedule(
            [this, display, emotion_str = std::string(emotion->valuestring)]() {
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

          Schedule([this]() { Reboot(); });
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
    } else if (HandleSmartLockJsonMessage(root, type->valuestring)) {
      // v5.0 协议：智能门锁扩展消息（face_result, lock_control, dev_control,
      // user_mgmt, heartbeat_ack） 已在 HandleSmartLockJsonMessage() 中处理
    } else if (strcmp(type->valuestring, "alert") == 0) {
      auto status = cJSON_GetObjectItem(root, "status");
      auto message = cJSON_GetObjectItem(root, "message");
      auto emotion = cJSON_GetObjectItem(root, "emotion");
      if (cJSON_IsString(status) && cJSON_IsString(message) &&
          cJSON_IsString(emotion)) {
        Alert(status->valuestring, message->valuestring, emotion->valuestring,
              Lang::Sounds::OGG_VIBRATION);
      } else {
        ESP_LOGW(TAG, "Alert command requires status, message and emotion");
      }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
    } else if (strcmp(type->valuestring, "custom") == 0) {
      auto payload = cJSON_GetObjectItem(root, "payload");
      ESP_LOGI(TAG, "Received custom message: %s",
               cJSON_PrintUnformatted(root));
      if (cJSON_IsObject(payload)) {
        Schedule(
            [this, display,
             payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
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

  // 初始化锁控服务（从板级获取）
  lock_control_ = Board::GetInstance().GetLockControl();
  if (lock_control_) {
    ESP_LOGI(TAG, "锁控服务已获取，设置事件回调");
    // 设置事件回调，将事件调度到主循环处理
    lock_control_->SetEventCallback([this](const xiaozhi::LockMessage &msg) {
      // 使用 Schedule 将事件处理调度到主循环，避免在 UART 任务中执行耗时操作
      Schedule([this, msg]() { HandleLockEvent(msg); });
    });
  } else {
    ESP_LOGW(TAG, "当前板级不支持锁控服务");
  }

  SystemInfo::PrintHeapStats();
  SetDeviceState(kDeviceStateIdle);

  has_server_time_ = ota.HasServerTime();

  // 如果成功获取服务器时间且锁控服务可用，向 STM32 同步当前时间
  if (has_server_time_ && lock_control_) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    lock_control_->SendSyncTime(
        (uint8_t)timeinfo.tm_hour,
        (uint8_t)timeinfo.tm_min,
        (uint8_t)timeinfo.tm_sec);
    ESP_LOGI(TAG, "已向 STM32 同步时间: %02d:%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  }

  if (protocol_started) {
    std::string message =
        std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
    display->ShowNotification(message.c_str());
    display->SetChatMessage("system", "");
    // 播放成功音效以指示设备已准备好

    audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
  }

  // =========================================================================
  // 启动自动连接任务
  // =========================================================================
  ESP_LOGI(TAG, "启动自动连接任务");
  xTaskCreate(
      [](void *arg) {
        ((Application *)arg)->AutoConnectLoop();
        vTaskDelete(NULL);
      },
      "auto_connect", // 任务名称
      2048 * 2,       // 栈大小 4KB
      this,           // 参数
      2,              // 优先级（低于主循环的 3）
      &auto_connect_task_handle_);
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
    auto bits = xEventGroupWaitBits(
        event_group_,
        MAIN_EVENT_SCHEDULE | MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED | MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK | MAIN_EVENT_ERROR,
        pdTRUE, pdFALSE, portMAX_DELAY);

    if (bits & MAIN_EVENT_ERROR) {
      SetDeviceState(kDeviceStateIdle);
      Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark",
            Lang::Sounds::OGG_EXCLAMATION);
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
      for (auto &task : tasks) {
        task();
      }
    }

    if (bits & MAIN_EVENT_CLOCK_TICK) {
      clock_ticks_++;
      auto display = Board::GetInstance().GetDisplay();
      display->UpdateStatusBar();

      // 每秒清理超时的待处理命令
      CleanupPendingCommands();

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
    SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop
                                          : kListeningModeRealtime);
#else
    SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop
                                          : kListeningModeRealtime);
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

  DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state,
                                                              state);

  auto &board = Board::GetInstance();
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

bool Application::UpgradeFirmware(Ota &ota, const std::string &url) {
  auto &board = Board::GetInstance();
  auto display = board.GetDisplay();

  // 使用提供的URL或从OTA对象获取

  std::string upgrade_url = url.empty() ? ota.GetFirmwareUrl() : url;
  std::string version_info =
      url.empty() ? ota.GetFirmwareVersion() : "(Manual upgrade)";

  // 如果已打开，请关闭音频通道

  if (protocol_ && protocol_->IsAudioChannelOpened()) {
    ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
    protocol_->CloseAudioChannel();
  }
  ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());

  Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download",
        Lang::Sounds::OGG_UPGRADE);
  vTaskDelay(pdMS_TO_TICKS(3000));

  SetDeviceState(kDeviceStateUpgrading);

  std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
  display->SetChatMessage("system", message.c_str());

  board.SetPowerSaveMode(false);
  audio_service_.Stop();
  vTaskDelay(pdMS_TO_TICKS(1000));

  bool upgrade_success = ota.StartUpgradeFromUrl(
      upgrade_url, [display](int progress, size_t speed) {
        std::thread([display, progress, speed]() {
          char buffer[32];
          snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress,
                   speed / 1024);
          display->SetChatMessage("system", buffer);
        }).detach();
      });

  if (!upgrade_success) {
    // 升级失败，重启音频服务并继续运行

    ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and "
                  "continuing operation...");
    audio_service_.Start(); // 重新启动音频服务

    board.SetPowerSaveMode(true); // 恢复省电模式

    Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark",
          Lang::Sounds::OGG_EXCLAMATION);
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

void Application::WakeWordInvoke(const std::string &wake_word) {
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
    SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop
                                          : kListeningModeRealtime);
#else
    SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop
                                          : kListeningModeRealtime);
    // 播放弹出声音以指示已检测到唤醒词

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

void Application::SendMcpMessage(const std::string &payload) {
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
    auto &board = Board::GetInstance();
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

void Application::PlaySound(const std::string_view &sound) {
  audio_service_.PlaySound(sound);
}

bool Application::StartMonitorMode() {
  if (monitor_service_ && monitor_service_->IsRunning()) {
    ESP_LOGW(TAG, "Monitor mode already running");
    return false;
  }

  // 检查与本地预览的互斥
  if (IsLocalPreviewActive()) {
    ESP_LOGE(TAG, "无法启动监控模式：本地预览正在运行");
    Alert("错误", "本地预览运行中", "circle_xmark",
          Lang::Sounds::OGG_EXCLAMATION);
    return false;
  }

  if (!protocol_) {
    ESP_LOGE(TAG, "Protocol not initialized");
    return false;
  }

  auto &board = Board::GetInstance();
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

// ============================================================================
// 锁控事件处理（智能门锁扩展功能）
// ============================================================================

/**
 * @brief 处理 STM32 锁控模块上报的事件
 *
 * 根据消息类别分发处理：
 * - RPT (0x01): 事件上报、开锁日志、环境数据、状态上报
 * - SYS (0x00): ACK 响应、心跳响应
 * - USER (0x03): 用户管理反馈（指纹、NFC）
 *
 * @param msg 锁控消息结构体
 */
void Application::HandleLockEvent(const xiaozhi::LockMessage &msg) {
  ESP_LOGI(TAG, "收到锁控事件: CAT=0x%02X, TYPE=0x%02X", msg.category,
           msg.type);

  // 监控模式下忽略锁控事件，避免冲突
  if (IsMonitorMode()) {
    ESP_LOGW(TAG, "监控模式中，忽略锁控事件");
    return;
  }

  // 根据消息类别分发处理
  if (msg.IsRpt()) {
    HandleLockReportMessage(msg);
  } else if (msg.IsSys()) {
    HandleLockSystemMessage(msg);
  } else if (msg.IsUser()) {
    HandleLockUserMessage(msg);
  }
}

/**
 * @brief 处理 STM32 上报消息 (CAT = 0x01)
 *
 * 包括：事件上报、开锁日志、环境数据、状态上报、密码查询结果
 */
void Application::HandleLockReportMessage(const xiaozhi::LockMessage &msg) {
  switch (msg.type) {
  case static_cast<uint8_t>(xiaozhi::RptType::RPT_EVENT): {
    // 事件上报：data[0]=事件ID, data[1]=参数
    uint8_t event_id = msg.data[0];
    uint8_t param = msg.data[1];

    std::string event_name;
    switch (event_id) {
    case static_cast<uint8_t>(xiaozhi::EventId::EVT_DOORBELL):
      event_name = "bell";
      ESP_LOGI(TAG, "门铃按下");
      // 不再自动触发人脸识别，由服务器通过 MCP 调用 camera.take_photo
      break;

    case static_cast<uint8_t>(xiaozhi::EventId::EVT_PIR):
      event_name = "pir_trigger";
      ESP_LOGI(TAG, "PIR 检测到人体 (持续 %d 秒)", param);
      // 不再自动触发人脸识别，由服务器通过 MCP 调用 camera.take_photo
      break;

    case static_cast<uint8_t>(xiaozhi::EventId::EVT_TAMPER):
      event_name = "tamper";
      // v2.8 协议升级：播放撬锁报警语音
      // STM32 负责蜂鸣器报警，ESP32 播放语音提示
      ESP_LOGW(TAG, "撬锁报警 (级别 %d)", param);
      audio_service_.PlaySound(Lang::Sounds::OGG_TAMPER_ALERT);
      break;

    case static_cast<uint8_t>(xiaozhi::EventId::EVT_DOOR_OPEN):
      event_name = "door_open";
      // v2.8 协议升级：播放门未关闭语音提示
      // 屏幕仅显示摄像头画面，不显示警告弹窗
      ESP_LOGW(TAG, "门未关超时 (%d 分钟)", param);
      audio_service_.PlaySound(Lang::Sounds::OGG_DOOR_NOT_CLOSED);
      break;

    case static_cast<uint8_t>(xiaozhi::EventId::EVT_LOW_BATTERY):
      event_name = "low_battery";
      // v2.7 协议升级：移除 Alert 调用，仅保留日志和服务器上报
      // 屏幕仅显示摄像头画面，不显示警告弹窗
      ESP_LOGW(TAG, "低电量警告: %d%%", param);
      break;

    case static_cast<uint8_t>(xiaozhi::EventId::EVT_LOCK_STATUS): {
      // v2.7 新增：关门/上锁状态事件
      // param (D1) 为状态码：0x00=门关闭, 0x01=上锁成功, 0x02=锁舌未到位报警
      // 注意: STM32 v2.8+ 已移除霍尔传感器，不再发送 BOLT_ALARM 事件
      // 保留此代码以兼容旧版 STM32 (v2.7-)
      xiaozhi::LockStatusCode status_code =
          static_cast<xiaozhi::LockStatusCode>(param);

      switch (status_code) {
      case xiaozhi::LockStatusCode::DOOR_CLOSED:
        event_name = "door_closed";
        ESP_LOGI(TAG, "门已关闭");
        break;
      case xiaozhi::LockStatusCode::LOCK_SUCCESS:
        event_name = "lock_success";
        ESP_LOGI(TAG, "自动上锁成功");
        break;
      case xiaozhi::LockStatusCode::BOLT_ALARM:
        event_name = "bolt_alarm";
        ESP_LOGW(TAG, "锁舌未到位报警 (仅旧版 STM32 v2.7-)");
        break;
      default:
        ESP_LOGW(TAG, "未知锁状态码: 0x%02X", param);
        break;
      }
      break;
    }

    default:
      ESP_LOGW(TAG, "未知事件 ID: 0x%02X", event_id);
      break;
    }

    // v5.0 协议：上报事件到服务器
    if (!event_name.empty() && protocol_ && protocol_->IsAudioChannelOpened()) {
      protocol_->SendEventReport(event_name, param);
    }
    break;
  }

  case static_cast<uint8_t>(xiaozhi::RptType::RPT_UNLOCK): {
    // 本地开锁日志：data[0]=方式, data[1]=用户ID/剩余锁定时间, data[2]=结果
    uint8_t method = msg.data[0];
    uint8_t d1 = msg.data[1];
    uint8_t result = msg.data[2];
    ESP_LOGI(TAG, "开锁日志: 方式=%d, D1=%d, 结果=%d", method, d1, result);

    // 解析状态和相关字段
    std::string status_str;
    int uid = 0;
    int fail_count = 0;
    int lock_time = 0;

    if (result == static_cast<uint8_t>(xiaozhi::UnlockResult::UNLOCK_SUCCESS)) {
      // 开锁成功：D1=用户 ID
      status_str = "success";
      uid = d1;
      ESP_LOGI(TAG, "开锁成功，用户 ID=%d", uid);
    } else if (result ==
               static_cast<uint8_t>(xiaozhi::UnlockResult::UNLOCK_LOCKED)) {
      // 已锁定：D1=剩余锁定时间（分钟）
      status_str = "locked";
      lock_time = d1;
      ESP_LOGW(TAG, "设备已锁定，剩余 %d 分钟", lock_time);
      // 播放锁定语音
      PlayLockedVoice(lock_time);
    } else if (result >=
                   static_cast<uint8_t>(xiaozhi::UnlockResult::UNLOCK_FAIL_1) &&
               result <=
                   static_cast<uint8_t>(xiaozhi::UnlockResult::UNLOCK_FAIL_5)) {
      // 认证失败：D1=用户 ID（0xFF 表示无法识别），D2=失败次数
      status_str = "fail";
      uid = d1;
      fail_count = result;
      uint8_t remaining = xiaozhi::MAX_AUTH_FAIL_COUNT - fail_count;
      ESP_LOGW(TAG, "认证失败，用户 ID=%d，已失败 %d 次，还剩 %d 次机会", uid,
               fail_count, remaining);
      // 播放失败语音
      PlayAuthFailVoice(remaining);
    } else {
      // 未知结果
      status_str = "fail";
      uid = d1;
      fail_count = result;
      ESP_LOGW(TAG, "未知开锁结果: %d", result);
    }

    // v5.0 协议：转发开锁日志到服务器
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
      std::string method_str = GetUnlockMethodString(method);
      protocol_->SendLogReport(method_str, status_str, uid, fail_count,
                               lock_time);
    }
    break;
  }

  case static_cast<uint8_t>(xiaozhi::RptType::RPT_DOOR_OPENED): {
    // v2.6+ 开门日志：data[0]=开锁方式, data[1]=开门来源
    uint8_t method = msg.data[0];
    uint8_t source = msg.data[1];

    std::string method_str = GetUnlockMethodString(method);
    std::string source_str;
    switch (static_cast<xiaozhi::DoorSource>(source)) {
    case xiaozhi::DoorSource::OUTSIDE:
      source_str = "outside";
      break;
    case xiaozhi::DoorSource::INSIDE:
      source_str = "inside";
      break;
    default:
      source_str = "unknown";
      break;
    }

    ESP_LOGI(TAG, "开门日志: 方式=%s, 来源=%s", method_str.c_str(),
             source_str.c_str());

    // v5.0 协议：上报开门日志到服务器
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
      protocol_->SendDoorOpenedReport(method_str, source_str);
    }
    break;
  }

  case static_cast<uint8_t>(xiaozhi::RptType::RPT_ENV): {
    // 环境数据：data[0]=电量, data[1-2]=光照(大端)
    uint8_t battery = msg.data[0];
    uint16_t lux = (msg.data[1] << 8) | msg.data[2];
    ESP_LOGI(TAG, "环境数据: 电量=%d%%, 光照=%d Lux", battery, lux);

    // 检查是否有变化
    bool env_changed = (battery != last_battery_) || (lux != last_lux_);

    // 保存环境数据
    last_battery_ = battery;
    last_lux_ = lux;

    // v5.0 协议：环境数据变化时上报状态到服务器
    if (env_changed && protocol_ && protocol_->IsAudioChannelOpened()) {
      protocol_->SendStatusReport(last_battery_, last_lux_, last_lock_state_,
                                  last_light_state_);
    }

    // 两级确认机制：查询命令收到数据帧后发送 ack
    uint8_t query_type = static_cast<uint8_t>(xiaozhi::CmdType::Q_SENSORS);
    auto it = pending_commands_.find(query_type);
    if (it != pending_commands_.end()) {
      const PendingCommand &cmd = it->second;
      if (cmd.type == PendingCommandType::QUERY && !cmd.seq_id.empty()) {
        ESP_LOGI(TAG, "查询命令完成，发送 ack: seq_id=%s", cmd.seq_id.c_str());
        protocol_->SendAck(cmd.seq_id, 0, "OK");
      }
      pending_commands_.erase(it);
    }
    break;
  }

  case static_cast<uint8_t>(xiaozhi::RptType::RPT_STATE): {
    // 状态上报：data[0]=锁状态, data[1]=灯状态
    bool lock_open = (msg.data[0] == 0x01);
    bool light_on = (msg.data[1] == 0x01);
    ESP_LOGI(TAG, "状态: 锁=%s, 灯=%s", lock_open ? "开" : "关",
             light_on ? "亮" : "灭");

    // 保存状态数据
    last_lock_state_ = lock_open ? 1 : 0;
    last_light_state_ = light_on ? 1 : 0;

    // v5.0 协议：上报状态到服务器
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
      protocol_->SendStatusReport(last_battery_, last_lux_, last_lock_state_,
                                  last_light_state_);
    }

    // 两级确认机制：查询命令收到数据帧后发送 ack
    uint8_t query_type = static_cast<uint8_t>(xiaozhi::CmdType::Q_STATUS);
    auto it = pending_commands_.find(query_type);
    if (it != pending_commands_.end()) {
      const PendingCommand &cmd = it->second;
      if (cmd.type == PendingCommandType::QUERY && !cmd.seq_id.empty()) {
        ESP_LOGI(TAG, "查询命令完成，发送 ack: seq_id=%s", cmd.seq_id.c_str());
        protocol_->SendAck(cmd.seq_id, 0, "OK");
      }
      pending_commands_.erase(it);
    }
    break;
  }

  case static_cast<uint8_t>(xiaozhi::RptType::RPT_PWD): {
    // 密码查询结果
    uint32_t pwd = xiaozhi::LockProtocol::DecodePasswordHex(msg.data);
    ESP_LOGI(TAG, "当前密码: %06lu", (unsigned long)pwd);

    // v2.7 协议升级：上报密码到服务器
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
      // 需要将 Protocol 转换为 WebsocketProtocol 才能调用 SendPasswordReport
      auto ws_protocol = dynamic_cast<WebsocketProtocol *>(protocol_.get());
      if (ws_protocol) {
        ws_protocol->SendPasswordReport(pwd);
      }
    }

    // 两级确认机制：查询命令收到数据帧后发送 ack
    uint8_t query_type = static_cast<uint8_t>(xiaozhi::UserPwdCmd::PWD_QUERY);
    auto it = pending_commands_.find(query_type);
    if (it != pending_commands_.end()) {
      const PendingCommand &cmd = it->second;
      if (cmd.type == PendingCommandType::QUERY && !cmd.seq_id.empty()) {
        ESP_LOGI(TAG, "密码查询完成，发送 ack: seq_id=%s", cmd.seq_id.c_str());
        protocol_->SendAck(cmd.seq_id, 0, "OK");
      }
      pending_commands_.erase(it);
    }
    break;
  }

  default:
    ESP_LOGW(TAG, "未知上报类型: 0x%02X", msg.type);
    break;
  }
}

/**
 * @brief 处理系统消息 (CAT = 0x00)
 *
 * 包括：ACK_OK、ACK_ERR、PONG
 *
 * 两级确认机制：
 * - 即时命令：收到 STM32 ACK 后立即发送 ack 到服务器
 * - 查询/长流程命令：收到 STM32 ACK 后更新状态，等待数据帧/最终结果
 */
void Application::HandleLockSystemMessage(const xiaozhi::LockMessage &msg) {
  if (msg.IsAckOk()) {
    uint8_t orig_type = msg.data[0];
    ESP_LOGD(TAG, "收到 ACK_OK，原指令 TYPE=0x%02X", orig_type);

    // 查找待处理命令
    auto it = pending_commands_.find(orig_type);
    if (it != pending_commands_.end()) {
      PendingCommand &cmd = it->second;
      cmd.stm32_ack_received = true;
      cmd.stm32_error_code = 0;

      ESP_LOGI(TAG, "匹配待处理命令: seq_id=%s, type=%d", cmd.seq_id.c_str(),
               static_cast<int>(cmd.type));

      // 根据命令类型决定是否发送最终 ack
      if (cmd.type == PendingCommandType::IMMEDIATE) {
        // 即时命令：收到 STM32 ACK 后立即发送 ack 到服务器
        if (protocol_ && !cmd.seq_id.empty()) {
          ESP_LOGI(TAG, "即时命令完成，发送 ack: seq_id=%s, code=0",
                   cmd.seq_id.c_str());
          protocol_->SendAck(cmd.seq_id, 0, "OK");
        }
        // 清理待处理命令
        pending_commands_.erase(it);
      } else {
        // 查询/长流程命令：更新状态，继续等待数据帧/最终结果
        ESP_LOGI(TAG, "查询/长流程命令，等待数据帧: seq_id=%s",
                 cmd.seq_id.c_str());
      }
    }
  } else if (msg.IsAckErr()) {
    uint8_t orig_type = msg.data[0];
    uint8_t error_code = msg.data[1];
    ESP_LOGW(TAG, "收到 ACK_ERR，原指令 TYPE=0x%02X，错误码=0x%02X", orig_type,
             error_code);

    // 查找待处理命令
    auto it = pending_commands_.find(orig_type);
    if (it != pending_commands_.end()) {
      PendingCommand &cmd = it->second;
      cmd.stm32_ack_received = true;
      cmd.stm32_error_code = error_code;

      // 映射错误码并发送 ack 到服务器
      int unified_code = MapStm32ErrorCode(error_code);
      if (protocol_ && !cmd.seq_id.empty()) {
        ESP_LOGI(TAG, "命令执行失败，发送 ack: seq_id=%s, code=%d",
                 cmd.seq_id.c_str(), unified_code);
        protocol_->SendAck(cmd.seq_id, unified_code, "STM32 error");
      }
      // 清理待处理命令
      pending_commands_.erase(it);
    }
  } else if (msg.type == static_cast<uint8_t>(xiaozhi::SysType::SYS_PONG)) {
    ESP_LOGD(TAG, "收到心跳响应");
  }
}

/**
 * @brief 处理用户管理反馈消息 (CAT = 0x03)
 *
 * 包括：指纹录入反馈、NFC 录入反馈
 * v5.0 协议：转发用户管理结果到服务器
 *
 * 两级确认机制：
 * - 查询命令（query）：收到数量响应后发送 ack
 * - 长流程命令（add）：收到最终结果（成功/失败/已存在/ID占用）后发送 ack
 */
void Application::HandleLockUserMessage(const xiaozhi::LockMessage &msg) {
  std::string category;
  std::string command;
  bool result = false;
  int val = 0;
  std::string result_msg;
  bool is_final_result = false; // 是否为最终结果（用于两级确认）
  uint8_t uart_type = 0;        // 用于查找待处理命令
  bool should_report = true;    // 是否需要上报服务器

  // 判断是指纹还是 NFC 反馈
  if (msg.type == static_cast<uint8_t>(xiaozhi::UserFpCmd::FP_RESP)) {
    category = "finger";
    uart_type = static_cast<uint8_t>(xiaozhi::UserFpCmd::FP_CMD);
    uint8_t status = msg.data[0];

    switch (status) {
    case static_cast<uint8_t>(xiaozhi::FpRespStatus::FP_PRESS_FINGER): {
      // v2.8：播放指纹录入语音提示
      uint8_t press_count = msg.data[1];
      ESP_LOGI(TAG, "指纹录入：请按手指 (第 %d 次)", press_count);
      if (press_count == 1) {
        audio_service_.PlaySound(Lang::Sounds::OGG_FP_PRESS);
      } else {
        audio_service_.PlaySound(Lang::Sounds::OGG_FP_PRESS_AGAIN);
      }
      return; // 中间状态，不上报，不发送 ack
    }
    case static_cast<uint8_t>(xiaozhi::FpRespStatus::FP_LIFT_FINGER):
      ESP_LOGI(TAG, "指纹录入：请抬起手指");
      audio_service_.PlaySound(Lang::Sounds::OGG_FP_LIFT);
      return; // 中间状态，不上报，不发送 ack
    case static_cast<uint8_t>(xiaozhi::FpRespStatus::FP_SUCCESS):
      command = "add";
      result = true;
      val = msg.data[1]; // 新分配的 ID
      result_msg = "Success";
      is_final_result = true;
      ESP_LOGI(TAG, "指纹录入成功，ID=%d", val);
      audio_service_.PlaySound(Lang::Sounds::OGG_ENROLL_SUCCESS);
      break;
    case static_cast<uint8_t>(xiaozhi::FpRespStatus::FP_FAILED):
      command = "add";
      result = false;
      val = msg.data[1]; // 错误码
      result_msg = "Failed";
      is_final_result = true;
      ESP_LOGW(TAG, "指纹操作失败，错误码=0x%02X", val);
      audio_service_.PlaySound(Lang::Sounds::OGG_ENROLL_FAIL);
      break;
    case static_cast<uint8_t>(xiaozhi::FpRespStatus::FP_COUNT_RESP):
      command = "query";
      result = true;
      val = msg.data[1]; // 总数
      result_msg = "Success";
      is_final_result = true;
      ESP_LOGI(TAG, "指纹数量：%d", val);
      break;
    case static_cast<uint8_t>(xiaozhi::FpRespStatus::FP_ALREADY_EXISTS):
      // v2.7 新增：指纹已存在，返回已有 ID
      command = "add";
      result = true;
      val = msg.data[1]; // 已存在的 ID
      result_msg = "AlreadyExists";
      is_final_result = true;
      ESP_LOGI(TAG, "指纹已存在，ID=%d", val);
      audio_service_.PlaySound(Lang::Sounds::OGG_ALREADY_EXISTS);
      break;
    case static_cast<uint8_t>(xiaozhi::FpRespStatus::FP_ID_OCCUPIED):
      // v2.7 新增：指定 ID 被占用，返回新分配的 ID
      command = "add";
      result = true;
      val = msg.data[1]; // 新分配的 ID
      result_msg = "IdOccupied";
      is_final_result = true;
      ESP_LOGI(TAG, "指定 ID 被占用，新分配 ID=%d", val);
      audio_service_.PlaySound(Lang::Sounds::OGG_ID_OCCUPIED);
      break;
    default:
      ESP_LOGW(TAG, "未知指纹反馈状态: 0x%02X", status);
      return;
    }
  } else if (msg.type == static_cast<uint8_t>(xiaozhi::UserNfcCmd::NFC_RESP)) {
    category = "nfc";
    uart_type = static_cast<uint8_t>(xiaozhi::UserNfcCmd::NFC_CMD);
    uint8_t status = msg.data[0];

    // v2.8：NFC 反馈状态码，包含中间状态
    switch (status) {
    case static_cast<uint8_t>(xiaozhi::NfcRespStatus::NFC_TAP):
      // 请刷卡（录入中）
      ESP_LOGI(TAG, "NFC 录入：请刷卡");
      audio_service_.PlaySound(Lang::Sounds::OGG_NFC_TAP);
      return; // 中间状态，不上报，不发送 ack
    case static_cast<uint8_t>(xiaozhi::NfcRespStatus::NFC_REMOVE_CARD):
      // 请移开卡片
      ESP_LOGI(TAG, "NFC 录入：请移开卡片");
      audio_service_.PlaySound(Lang::Sounds::OGG_NFC_TAP_AGAIN);
      return; // 中间状态，不上报，不发送 ack
    case static_cast<uint8_t>(xiaozhi::NfcRespStatus::NFC_SUCCESS):
      command = "add";
      result = true;
      val = msg.data[1];
      result_msg = "Success";
      is_final_result = true;
      ESP_LOGI(TAG, "NFC 录入成功，ID=%d", val);
      audio_service_.PlaySound(Lang::Sounds::OGG_ENROLL_SUCCESS);
      break;
    case static_cast<uint8_t>(xiaozhi::NfcRespStatus::NFC_FAILED):
      command = "add";
      result = false;
      val = msg.data[1];
      result_msg = "Failed";
      is_final_result = true;
      ESP_LOGW(TAG, "NFC 操作失败，错误码=0x%02X", val);
      audio_service_.PlaySound(Lang::Sounds::OGG_ENROLL_FAIL);
      break;
    case static_cast<uint8_t>(xiaozhi::NfcRespStatus::NFC_COUNT_RESP):
      command = "query";
      result = true;
      val = msg.data[1];
      result_msg = "Success";
      is_final_result = true;
      ESP_LOGI(TAG, "NFC 数量：%d", val);
      break;
    case static_cast<uint8_t>(xiaozhi::NfcRespStatus::NFC_ALREADY_EXISTS):
      command = "add";
      result = true;
      val = msg.data[1]; // 已存在的 ID
      result_msg = "AlreadyExists";
      is_final_result = true;
      ESP_LOGI(TAG, "NFC 已存在，ID=%d", val);
      audio_service_.PlaySound(Lang::Sounds::OGG_ALREADY_EXISTS);
      break;
    case static_cast<uint8_t>(xiaozhi::NfcRespStatus::NFC_ID_OCCUPIED):
      command = "add";
      result = true;
      val = msg.data[1]; // 新分配的 ID
      result_msg = "IdOccupied";
      is_final_result = true;
      ESP_LOGI(TAG, "NFC 指定 ID 被占用，新分配 ID=%d", val);
      audio_service_.PlaySound(Lang::Sounds::OGG_ID_OCCUPIED);
      break;
    default:
      ESP_LOGW(TAG, "未知 NFC 反馈状态: 0x%02X", status);
      return;
    }
  } else {
    ESP_LOGW(TAG, "未知用户管理反馈类型: 0x%02X", msg.type);
    return;
  }

  // 上报结果到服务器
  if (should_report && protocol_ && protocol_->IsAudioChannelOpened()) {
    protocol_->SendUserMgmtResult(category, command, result, val, result_msg);
  }

  // 两级确认机制：收到最终结果后发送 ack
  if (is_final_result && uart_type != 0) {
    auto it = pending_commands_.find(uart_type);
    if (it != pending_commands_.end()) {
      const PendingCommand &cmd = it->second;
      if (!cmd.seq_id.empty() && protocol_) {
        // 根据结果确定 ack code
        int ack_code = result ? 0 : 10; // 成功=0，失败=10（内部错误）
        ESP_LOGI(TAG, "用户管理命令完成，发送 ack: seq_id=%s, code=%d",
                 cmd.seq_id.c_str(), ack_code);
        protocol_->SendAck(cmd.seq_id, ack_code, result_msg);
      }
      pending_commands_.erase(it);
    }
  }
}

/**
 * @brief 获取开锁方式字符串
 * @param method 开锁方式枚举值
 * @return 开锁方式字符串（用于服务器上报）
 *
 * 与协议 v5.0 对应：
 * - finger: 指纹开锁
 * - nfc: NFC 开锁
 * - pwd: 密码开锁
 * - remote: 远程开锁(App)
 * - key: 机械钥匙
 * - face: 人脸开锁
 * - temp_pwd: 临时密码开锁
 */
std::string Application::GetUnlockMethodString(uint8_t method) {
  switch (method) {
  case static_cast<uint8_t>(xiaozhi::UnlockMethod::UNLOCK_FINGERPRINT):
    return "finger";
  case static_cast<uint8_t>(xiaozhi::UnlockMethod::UNLOCK_NFC):
    return "nfc";
  case static_cast<uint8_t>(xiaozhi::UnlockMethod::UNLOCK_PASSWORD):
    return "pwd";
  case static_cast<uint8_t>(xiaozhi::UnlockMethod::UNLOCK_REMOTE):
    return "remote";
  case static_cast<uint8_t>(xiaozhi::UnlockMethod::UNLOCK_KEY):
    return "key";
  case static_cast<uint8_t>(xiaozhi::UnlockMethod::UNLOCK_TEMP_PWD):
    return "temp_pwd";
  case static_cast<uint8_t>(xiaozhi::UnlockMethod::UNLOCK_FACE):
    return "face";
  default:
    return "unknown";
  }
}

// ============================================================================
// 人脸识别功能
// ============================================================================

/**
 * @brief 触发人脸识别流程
 *
 * 完整流程：
 * 1. 检查状态和内存
 * 2. 确保音频通道已打开
 * 3. 拍照并 JPEG 编码
 * 4. 发送图像到服务器
 * 5. 等待服务器返回识别结果
 *
 * 注意：此函数应在主循环中调用，避免阻塞 UART 任务
 */
void Application::TriggerFaceRecognition() {
  int64_t start_time = esp_timer_get_time();
  ESP_LOGI(TAG, "触发人脸识别");

  // 检查是否已经在进行人脸识别（防止重复触发）
  if (face_recognition_in_progress_) {
    ESP_LOGW(TAG, "人脸识别正在进行中，忽略本次触发");
    return;
  }

  // 检查与本地预览的互斥
  if (IsLocalPreviewActive()) {
    ESP_LOGE(TAG, "无法触发人脸识别：本地预览正在运行");
    Alert("错误", "本地预览运行中", "circle_xmark",
          Lang::Sounds::OGG_EXCLAMATION);
    return;
  }

  // 检查设备状态（仅在空闲或聆听状态下允许）
  if (device_state_ != kDeviceStateIdle &&
      device_state_ != kDeviceStateListening) {
    ESP_LOGW(TAG, "设备非空闲状态，忽略人脸识别触发 (state=%s)",
             STATE_STRINGS[device_state_]);
    return;
  }

  // 设置进行中标志
  face_recognition_in_progress_ = true;

  // 检查可用内存（JPEG 编码需要较大内存）
  size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t MIN_FREE_MEMORY = 100 * 1024; // 最小 100KB
  if (free_psram < MIN_FREE_MEMORY) {
    ESP_LOGW(TAG, "内存不足，拒绝人脸识别 (可用 PSRAM: %u KB)",
             (unsigned)(free_psram / 1024));
    face_recognition_in_progress_ = false;
    return;
  }
  ESP_LOGI(TAG, "内存检查通过 (可用 PSRAM: %u KB)",
           (unsigned)(free_psram / 1024));

  // 检查摄像头是否可用
  auto &board = Board::GetInstance();
  auto camera = board.GetCamera();
  if (!camera) {
    ESP_LOGE(TAG, "摄像头不可用，中止人脸识别");
    face_recognition_in_progress_ = false;
    return;
  }

  // 确保音频通道已打开（用于发送图像和接收结果）
  if (!protocol_->IsAudioChannelOpened()) {
    ESP_LOGI(TAG, "为人脸识别打开音频通道");
    protocol_->OpenAudioChannel();
  }

  int64_t capture_start = esp_timer_get_time();
  ESP_LOGI(TAG, "开始人脸识别流程 (触发->开始: %lld ms)",
           (capture_start - start_time) / 1000);

  // 拍照
  if (!camera->Capture()) {
    ESP_LOGE(TAG, "拍照失败");
    face_recognition_in_progress_ = false;
    return;
  }

  int64_t capture_end = esp_timer_get_time();
  ESP_LOGI(TAG, "拍照成功 (耗时: %lld ms)",
           (capture_end - capture_start) / 1000);

// JPEG 编码（仅 ESP32-S3 支持）
#ifndef CONFIG_IDF_TARGET_ESP32
  auto esp32_camera = dynamic_cast<Esp32Camera *>(camera);
  if (!esp32_camera) {
    ESP_LOGE(TAG, "摄像头类型不支持 JPEG 编码");
    face_recognition_in_progress_ = false;
    return;
  }

  uint8_t *jpeg_data = nullptr;
  size_t jpeg_size = 0;
  if (!esp32_camera->CaptureJpeg(&jpeg_data, &jpeg_size, 80)) {
    ESP_LOGE(TAG, "JPEG 编码失败");
    face_recognition_in_progress_ = false;
    return;
  }

  // 检查内存分配是否成功
  if (jpeg_data == nullptr || jpeg_size == 0) {
    ESP_LOGE(TAG, "JPEG 编码失败：未分配数据");
    face_recognition_in_progress_ = false;
    return;
  }

  int64_t encode_end = esp_timer_get_time();
  ESP_LOGI(TAG, "JPEG 编码完成: %u 字节 (耗时: %lld ms)", (unsigned)jpeg_size,
           (encode_end - capture_end) / 1000);

  // 发送人脸识别图像
  int64_t send_start = esp_timer_get_time();
  uint16_t width = esp32_camera->GetFrameWidth();
  uint16_t height = esp32_camera->GetFrameHeight();
#else
  ESP_LOGE(TAG, "ESP32 不支持人脸识别");
  face_recognition_in_progress_ = false;
  return;
#endif

  if (!protocol_->SendFaceRecognition(jpeg_data, jpeg_size, width, height)) {
    ESP_LOGE(TAG, "发送人脸识别图像失败");
    heap_caps_free(jpeg_data);
    face_recognition_in_progress_ = false;
    return;
  }

  int64_t send_end = esp_timer_get_time();
  ESP_LOGI(TAG, "人脸识别图像发送成功 (耗时: %lld ms)",
           (send_end - send_start) / 1000);

  // 释放 JPEG 数据内存
  heap_caps_free(jpeg_data);

  int64_t total_time = (send_end - start_time) / 1000;
  ESP_LOGI(TAG, "人脸识别流程完成 (总耗时: %lld ms)", total_time);

  // 清除进行中标志
  face_recognition_in_progress_ = false;
}

/**
 * @brief 处理服务器返回的人脸识别结果
 *
 * JSON 格式：
 * {
 *   "type": "face_result",
 *   "result": "known" | "unknown" | "no_face",
 *   "access": { "granted": true/false }
 * }
 *
 * @param root JSON 根节点
 */
void Application::HandleFaceRecognitionResult(cJSON *root) {
  ESP_LOGI(TAG, "处理人脸识别结果");

  // 解析 result 字段
  auto result = cJSON_GetObjectItem(root, "result");
  if (!cJSON_IsString(result)) {
    ESP_LOGW(TAG, "无效的人脸识别结果：缺少 result 字段");
    return;
  }

  ESP_LOGI(TAG, "人脸识别结果: %s", result->valuestring);

  // 解析 access 字段
  auto access = cJSON_GetObjectItem(root, "access");
  if (!cJSON_IsObject(access)) {
    ESP_LOGW(TAG, "无效的人脸识别结果：缺少 access 字段");
    return;
  }

  auto granted = cJSON_GetObjectItem(access, "granted");
  bool access_granted = cJSON_IsTrue(granted);

  // 如果识别为已知用户且授权通过，发送开锁命令
  if (strcmp(result->valuestring, "known") == 0 && access_granted) {
    ESP_LOGI(TAG, "授权通过，发送开锁命令");
    if (lock_control_) {
      lock_control_->SendUnlock();
    } else {
      ESP_LOGW(TAG, "锁控服务不可用");
    }
  } else {
    ESP_LOGI(TAG, "授权拒绝或未知人员");
  }
}

// ============================================================================
// 安全告警处理
// ============================================================================

/**
 * @brief 处理撬锁报警
 *
 * 触发蜂鸣器报警、上报服务器、显示警告。
 *
 * @param level 报警级别（影响蜂鸣器响铃次数）
 */
void Application::HandleTamperAlert(uint8_t level) {
  ESP_LOGI(TAG, "处理撬锁报警，级别=%d", level);

  // 激活蜂鸣器报警
  if (lock_control_) {
    uint8_t beep_count = level > 0 ? level * 3 : 3;
    lock_control_->SendBeep(beep_count, xiaozhi::BeepFreq::BEEP_ALARM);
  }

  // v5.0 协议：上报服务器
  if (protocol_ && protocol_->IsAudioChannelOpened()) {
    protocol_->SendEventReport("tamper", level);
  }

  // 显示警报
  Alert("警报", "检测到暴力破坏", "triangle_exclamation",
        Lang::Sounds::OGG_EXCLAMATION);
}

/**
 * @brief 处理门未关提醒
 *
 * 上报服务器并显示提示。
 */
void Application::HandleDoorNotClosed() {
  ESP_LOGI(TAG, "处理门未关提醒");

  // v5.0 协议：上报服务器
  if (protocol_ && protocol_->IsAudioChannelOpened()) {
    protocol_->SendEventReport("door_open", 0);
  }

  // 显示提示
  Alert("提示", "门未关严实", "door_open", "");
}

// ============================================================================
// v2.8 协议：语音播放辅助方法
// ============================================================================

/**
 * @brief 播放认证失败语音（拼接方式）
 *
 * 播放序列：前缀 + 数字 + 后缀
 * 例如："认证失败，还剩" + "4" + "次机会"
 *
 * @param remaining 剩余尝试次数（1-4）
 */
void Application::PlayAuthFailVoice(uint8_t remaining) {
  ESP_LOGI(TAG, "播放认证失败语音，剩余 %d 次机会", remaining);

  // 播放前缀："认证失败，还剩"
  audio_service_.PlaySound(Lang::Sounds::OGG_AUTH_FAIL_PREFIX);

  // 播放数字
  PlayNumberVoice(remaining);

  // 播放后缀："次机会"
  audio_service_.PlaySound(Lang::Sounds::OGG_AUTH_FAIL_SUFFIX);
}

/**
 * @brief 播放设备锁定语音（拼接方式）
 *
 * 播放序列：前缀 + 数字 + 后缀
 * 例如："设备已锁定，请" + "3" + "分钟后再试"
 *
 * @param lock_minutes 剩余锁定时间（分钟）
 */
void Application::PlayLockedVoice(uint8_t lock_minutes) {
  ESP_LOGI(TAG, "播放设备锁定语音，剩余 %d 分钟", lock_minutes);

  // 播放前缀："设备已锁定，请"
  audio_service_.PlaySound(Lang::Sounds::OGG_LOCKED_PREFIX);

  // 播放数字
  PlayNumberVoice(lock_minutes);

  // 播放后缀："分钟后再试"
  audio_service_.PlaySound(Lang::Sounds::OGG_LOCKED_SUFFIX);
}

/**
 * @brief 播放数字语音
 *
 * 支持 0-99 的数字播放：
 * - 0-9: 直接播放对应数字
 * - 10-99: 拆分为十位和个位分别播放
 *
 * @param number 要播放的数字（0-99）
 */
void Application::PlayNumberVoice(uint8_t number) {
  if (number > 99) {
    ESP_LOGW(TAG, "数字超出范围: %d", number);
    return;
  }

  // 数字语音映射表
  static const std::string_view *digit_sounds[] = {
      &Lang::Sounds::OGG_0, &Lang::Sounds::OGG_1, &Lang::Sounds::OGG_2,
      &Lang::Sounds::OGG_3, &Lang::Sounds::OGG_4, &Lang::Sounds::OGG_5,
      &Lang::Sounds::OGG_6, &Lang::Sounds::OGG_7, &Lang::Sounds::OGG_8,
      &Lang::Sounds::OGG_9};

  if (number < 10) {
    // 单个数字
    audio_service_.PlaySound(*digit_sounds[number]);
  } else {
    // 两位数：先播放十位，再播放个位
    uint8_t tens = number / 10;
    uint8_t ones = number % 10;

    audio_service_.PlaySound(*digit_sounds[tens]);
    audio_service_.PlaySound(*digit_sounds[ones]);
  }
}

// ============================================================================
// v5.0 协议：智能门锁扩展消息处理
// ============================================================================

/**
 * @brief 处理智能门锁扩展 JSON 消息
 *
 * 此函数处理智能猫眼门锁系统新增的 JSON 消息类型，
 * 与原有小智语音助手功能分离，便于维护。
 *
 * 支持的消息类型：
 * - face_result / face_recognition: 人脸识别结果
 * - lock_control: 锁控命令（开锁、关锁、临时密码）
 * - dev_control: 硬件外设控制（蜂鸣器、OLED、补光灯）
 * - user_mgmt: 用户管理（指纹、NFC、密码）
 * - heartbeat_ack: 心跳响应
 *
 * @param root JSON 根节点
 * @param type 消息类型字符串
 * @return 如果消息被处理返回 true，否则返回 false
 */
bool Application::HandleSmartLockJsonMessage(const cJSON *root,
                                             const char *type) {

  // -------------------------------------------------------------------------
  // 人脸识别结果（兼容旧版 face_recognition 和新版 face_result）
  // 说明：face_result 是服务器主动推送的识别结果，不是用户命令
  //       不需要 seq_id 和两级确认机制，开锁结果通过 log_report 上报
  // -------------------------------------------------------------------------
  if (strcmp(type, "face_recognition") == 0 ||
      strcmp(type, "face_result") == 0) {
    ESP_LOGI(TAG, "收到人脸识别结果");

    Schedule([this, root_copy = cJSON_Duplicate(root, 1)]() {
      // 处理人脸识别结果
      HandleFaceRecognitionResult(root_copy);
      cJSON_Delete(root_copy);
    });
    return true;
  }

  // -------------------------------------------------------------------------
  // 锁控命令：unlock（开锁）、lock（关锁）、temp_code（临时密码）
  // -------------------------------------------------------------------------
  if (strcmp(type, "lock_control") == 0) {
    auto seq_id = cJSON_GetObjectItem(root, "seq_id");
    auto msg_id = cJSON_GetObjectItem(root, "msg_id");
    auto command = cJSON_GetObjectItem(root, "command");
    // 优先使用 seq_id，兼容旧版 msg_id
    std::string seq_id_str = cJSON_IsString(seq_id)   ? seq_id->valuestring
                             : cJSON_IsString(msg_id) ? msg_id->valuestring
                                                      : "";

    if (cJSON_IsString(command)) {
      std::string cmd_str = command->valuestring;
      ESP_LOGI(TAG, "锁控命令: %s (seq_id=%s)", cmd_str.c_str(),
               seq_id_str.c_str());

      // 两级确认机制：立即发送 esp32_ack（第一级确认）
      if (!seq_id_str.empty()) {
        auto ws_protocol = dynamic_cast<WebsocketProtocol *>(protocol_.get());
        if (ws_protocol) {
          ws_protocol->SendEsp32Ack(seq_id_str, 0, "received");
        }
      }

      Schedule([this, cmd = cmd_str, root_copy = cJSON_Duplicate(root, 1),
                seq_id_str]() {
        int ack_code = 0;
        std::string ack_msg = "OK";

        if (!lock_control_) {
          ESP_LOGW(TAG, "锁控服务不可用");
          ack_code = 6; // 硬件故障
          ack_msg = "Lock control not available";
          // 直接发送 ack（无需等待 STM32 响应）
          if (!seq_id_str.empty()) {
            protocol_->SendAck(seq_id_str, ack_code, ack_msg);
          }
        } else {
          // 保存待处理命令到 pending_commands_
          uint8_t uart_type = static_cast<uint8_t>(xiaozhi::CmdType::CMD_LOCK);
          PendingCommand pending_cmd;
          pending_cmd.seq_id = seq_id_str;
          pending_cmd.type = PendingCommandType::IMMEDIATE;
          pending_cmd.category = "lock";
          pending_cmd.command = cmd;
          pending_cmd.uart_type = uart_type;
          pending_cmd.uart_subtype = 0;
          pending_cmd.timestamp_ms = esp_timer_get_time() / 1000;
          pending_cmd.esp32_ack_sent = true;
          pending_cmd.stm32_ack_received = false;
          pending_cmd.stm32_error_code = 0;

          if (!seq_id_str.empty()) {
            pending_commands_[uart_type] = pending_cmd;
          }

          if (cmd == "unlock") {
            // 开锁命令，可选 duration 参数
            auto duration = cJSON_GetObjectItem(root_copy, "duration");
            uint8_t hold_seconds =
                cJSON_IsNumber(duration) ? duration->valueint : 0;
            lock_control_->SendUnlock(hold_seconds);
          } else if (cmd == "lock") {
            // 关锁命令
            lock_control_->SendLockDoor();
          } else if (cmd == "temp_code") {
            // 设置临时密码（分两包发送给 STM32）
            auto code = cJSON_GetObjectItem(root_copy, "code");
            auto expires = cJSON_GetObjectItem(root_copy, "expires");
            if (cJSON_IsString(code)) {
              uint32_t pwd = atoi(code->valuestring);
              uint32_t exp_seconds =
                  cJSON_IsNumber(expires) ? expires->valueint : 3600;
              lock_control_->SetTempPassword(pwd, exp_seconds);
            }
          } else {
            ack_code = 4; // 不支持
            ack_msg = "Unknown command";
            // 未知命令，直接发送 ack
            if (!seq_id_str.empty()) {
              protocol_->SendAck(seq_id_str, ack_code, ack_msg);
              pending_commands_.erase(uart_type);
            }
          }
        }

        cJSON_Delete(root_copy);
      });
    }
    return true;
  }

  // -------------------------------------------------------------------------
  // 硬件外设控制：beep（蜂鸣器）、oled（显示）、light（补光灯）
  // -------------------------------------------------------------------------
  if (strcmp(type, "dev_control") == 0) {
    auto seq_id = cJSON_GetObjectItem(root, "seq_id");
    auto msg_id = cJSON_GetObjectItem(root, "msg_id");
    auto target = cJSON_GetObjectItem(root, "target");
    // 优先使用 seq_id，兼容旧版 msg_id
    std::string seq_id_str = cJSON_IsString(seq_id)   ? seq_id->valuestring
                             : cJSON_IsString(msg_id) ? msg_id->valuestring
                                                      : "";

    if (cJSON_IsString(target)) {
      std::string target_str = target->valuestring;
      ESP_LOGI(TAG, "外设控制: target=%s (seq_id=%s)", target_str.c_str(),
               seq_id_str.c_str());

      // 两级确认机制：立即发送 esp32_ack（第一级确认）
      if (!seq_id_str.empty()) {
        auto ws_protocol = dynamic_cast<WebsocketProtocol *>(protocol_.get());
        if (ws_protocol) {
          ws_protocol->SendEsp32Ack(seq_id_str, 0, "received");
        }
      }

      Schedule([this, target_str, root_copy = cJSON_Duplicate(root, 1),
                seq_id_str]() {
        int ack_code = 0;
        std::string ack_msg = "OK";
        uint8_t uart_type = 0;

        if (!lock_control_) {
          ack_code = 6; // 硬件故障
          ack_msg = "Lock control not available";
          // 直接发送 ack
          if (!seq_id_str.empty()) {
            protocol_->SendAck(seq_id_str, ack_code, ack_msg);
          }
        } else {
          // 确定 UART TYPE
          if (target_str == "beep") {
            uart_type = static_cast<uint8_t>(xiaozhi::CmdType::CMD_BEEP);
          } else if (target_str == "oled") {
            uart_type = static_cast<uint8_t>(xiaozhi::CmdType::CMD_OLED);
          } else if (target_str == "light") {
            uart_type = static_cast<uint8_t>(xiaozhi::CmdType::CMD_LIGHT);
          }

          // 保存待处理命令
          if (uart_type != 0 && !seq_id_str.empty()) {
            PendingCommand pending_cmd;
            pending_cmd.seq_id = seq_id_str;
            pending_cmd.type = PendingCommandType::IMMEDIATE;
            pending_cmd.category = "dev";
            pending_cmd.command = target_str;
            pending_cmd.uart_type = uart_type;
            pending_cmd.uart_subtype = 0;
            pending_cmd.timestamp_ms = esp_timer_get_time() / 1000;
            pending_cmd.esp32_ack_sent = true;
            pending_cmd.stm32_ack_received = false;
            pending_cmd.stm32_error_code = 0;
            pending_commands_[uart_type] = pending_cmd;
          }

          if (target_str == "beep") {
            // 蜂鸣器控制：count（次数）、mode（short/long/alarm）
            auto count = cJSON_GetObjectItem(root_copy, "count");
            auto mode = cJSON_GetObjectItem(root_copy, "mode");
            uint8_t beep_count = cJSON_IsNumber(count) ? count->valueint : 1;
            xiaozhi::BeepFreq freq = xiaozhi::BeepFreq::BEEP_SHORT;

            if (cJSON_IsString(mode)) {
              if (strcmp(mode->valuestring, "long") == 0) {
                freq = xiaozhi::BeepFreq::BEEP_LONG;
              } else if (strcmp(mode->valuestring, "alarm") == 0) {
                freq = xiaozhi::BeepFreq::BEEP_ALARM;
              }
            }
            lock_control_->SendBeep(beep_count, freq);
          } else if (target_str == "oled") {
            // OLED 图标显示：icon（图标编号）
            auto icon = cJSON_GetObjectItem(root_copy, "icon");
            if (cJSON_IsNumber(icon)) {
              lock_control_->SendOledIcon(
                  static_cast<xiaozhi::OledIcon>(icon->valueint));
            }
          } else if (target_str == "light") {
            // 补光灯控制：action（on/off/auto）
            auto action = cJSON_GetObjectItem(root_copy, "action");
            if (cJSON_IsString(action)) {
              if (strcmp(action->valuestring, "on") == 0) {
                lock_control_->SendLightOn();
              } else if (strcmp(action->valuestring, "off") == 0) {
                lock_control_->SendLightOff();
              } else if (strcmp(action->valuestring, "auto") == 0) {
                lock_control_->SendLightAuto();
              }
            }
          } else {
            ack_code = 4; // 不支持
            ack_msg = "Unknown target";
            // 未知目标，直接发送 ack
            if (!seq_id_str.empty()) {
              protocol_->SendAck(seq_id_str, ack_code, ack_msg);
            }
          }
        }

        cJSON_Delete(root_copy);
      });
    }
    return true;
  }

  // -------------------------------------------------------------------------
  // 用户管理：finger（指纹）、nfc、password（密码）
  // -------------------------------------------------------------------------
  if (strcmp(type, "user_mgmt") == 0) {
    auto seq_id = cJSON_GetObjectItem(root, "seq_id");
    auto msg_id = cJSON_GetObjectItem(root, "msg_id");
    auto category = cJSON_GetObjectItem(root, "category");
    auto command = cJSON_GetObjectItem(root, "command");
    // 优先使用 seq_id，兼容旧版 msg_id
    std::string seq_id_str = cJSON_IsString(seq_id)   ? seq_id->valuestring
                             : cJSON_IsString(msg_id) ? msg_id->valuestring
                                                      : "";

    if (cJSON_IsString(category) && cJSON_IsString(command)) {
      std::string cat_str = category->valuestring;
      std::string cmd_str = command->valuestring;
      ESP_LOGI(TAG, "用户管理: category=%s, command=%s (seq_id=%s)",
               cat_str.c_str(), cmd_str.c_str(), seq_id_str.c_str());

      // 两级确认机制：立即发送 esp32_ack（第一级确认）
      if (!seq_id_str.empty()) {
        auto ws_protocol = dynamic_cast<WebsocketProtocol *>(protocol_.get());
        if (ws_protocol) {
          ws_protocol->SendEsp32Ack(seq_id_str, 0, "received");
        }
      }

      Schedule([this, cat = cat_str, cmd = cmd_str,
                root_copy = cJSON_Duplicate(root, 1), seq_id_str]() {
        int ack_code = 0;
        std::string ack_msg = "OK";

        if (!lock_control_) {
          ack_code = 6; // 硬件故障
          ack_msg = "Lock control not available";
          // 直接发送 ack
          if (!seq_id_str.empty()) {
            protocol_->SendAck(seq_id_str, ack_code, ack_msg);
          }
        } else {
          auto user_id = cJSON_GetObjectItem(root_copy, "user_id");
          uint8_t uid = cJSON_IsNumber(user_id) ? user_id->valueint : 0;

          // 确定命令类型和 UART TYPE
          PendingCommandType cmd_type = DetermineCommandType(cat, cmd);
          uint8_t uart_type = GetUartType(cat, cmd);
          uint8_t uart_subtype = 0;

          // 确定子命令
          if (cmd == "add") {
            uart_subtype = static_cast<uint8_t>(xiaozhi::FpSubCmd::FP_ENROLL);
          } else if (cmd == "del") {
            uart_subtype = static_cast<uint8_t>(xiaozhi::FpSubCmd::FP_DELETE);
          } else if (cmd == "clear") {
            uart_subtype = static_cast<uint8_t>(xiaozhi::FpSubCmd::FP_CLEAR);
          } else if (cmd == "query") {
            uart_subtype = static_cast<uint8_t>(xiaozhi::FpSubCmd::FP_COUNT);
          }

          // 保存待处理命令
          if (uart_type != 0 && !seq_id_str.empty()) {
            PendingCommand pending_cmd;
            pending_cmd.seq_id = seq_id_str;
            pending_cmd.type = cmd_type;
            pending_cmd.category = cat;
            pending_cmd.command = cmd;
            pending_cmd.uart_type = uart_type;
            pending_cmd.uart_subtype = uart_subtype;
            pending_cmd.timestamp_ms = esp_timer_get_time() / 1000;
            pending_cmd.esp32_ack_sent = true;
            pending_cmd.stm32_ack_received = false;
            pending_cmd.stm32_error_code = 0;
            pending_commands_[uart_type] = pending_cmd;
          }

          if (cat == "finger") {
            // 指纹管理：add/del/clear/query
            if (cmd == "add") {
              lock_control_->FingerprintEnroll(uid);
            } else if (cmd == "del") {
              lock_control_->FingerprintDelete(uid);
            } else if (cmd == "clear") {
              lock_control_->FingerprintClear();
            } else if (cmd == "query") {
              lock_control_->FingerprintQueryCount();
            }
          } else if (cat == "nfc") {
            // NFC 管理：add/del/clear/query
            if (cmd == "add") {
              lock_control_->NfcEnroll();
            } else if (cmd == "del") {
              lock_control_->NfcDelete(uid);
            } else if (cmd == "clear") {
              lock_control_->NfcClear();
            } else if (cmd == "query") {
              lock_control_->NfcQueryCount();
            }
          } else if (cat == "password") {
            // 密码管理：set/query
            if (cmd == "set") {
              auto payload = cJSON_GetObjectItem(root_copy, "payload");
              if (cJSON_IsString(payload)) {
                uint32_t pwd = atoi(payload->valuestring);
                lock_control_->SetPassword(pwd);
              }
            } else if (cmd == "query") {
              lock_control_->QueryPassword();
            }
          }
        }

        cJSON_Delete(root_copy);
      });
    }
    return true;
  }

  // -------------------------------------------------------------------------
  // 查询命令：sensors（传感器数据）、status（设备状态）
  // v5.2 新增：支持服务器远程查询
  // -------------------------------------------------------------------------
  if (strcmp(type, "query") == 0) {
    auto seq_id = cJSON_GetObjectItem(root, "seq_id");
    auto msg_id = cJSON_GetObjectItem(root, "msg_id");
    auto command = cJSON_GetObjectItem(root, "command");
    // 优先使用 seq_id，兼容旧版 msg_id
    std::string seq_id_str = cJSON_IsString(seq_id)   ? seq_id->valuestring
                             : cJSON_IsString(msg_id) ? msg_id->valuestring
                                                      : "";

    if (cJSON_IsString(command)) {
      std::string cmd_str = command->valuestring;
      ESP_LOGI(TAG, "查询命令: %s (seq_id=%s)", cmd_str.c_str(),
               seq_id_str.c_str());

      // 两级确认机制：立即发送 esp32_ack（第一级确认）
      if (!seq_id_str.empty()) {
        auto ws_protocol = dynamic_cast<WebsocketProtocol *>(protocol_.get());
        if (ws_protocol) {
          ws_protocol->SendEsp32Ack(seq_id_str, 0, "received");
        }
      }

      Schedule([this, cmd = cmd_str, seq_id_str]() {
        int ack_code = 0;
        std::string ack_msg = "OK";

        if (!lock_control_) {
          ack_code = 6; // 硬件故障
          ack_msg = "Lock control not available";
          // 直接发送 ack
          if (!seq_id_str.empty()) {
            protocol_->SendAck(seq_id_str, ack_code, ack_msg);
          }
        } else {
          // 确定 UART TYPE
          uint8_t uart_type = 0;
          if (cmd == "sensors") {
            uart_type = static_cast<uint8_t>(xiaozhi::CmdType::Q_SENSORS);
          } else if (cmd == "status") {
            uart_type = static_cast<uint8_t>(xiaozhi::CmdType::Q_STATUS);
          }

          if (uart_type != 0) {
            // 保存待处理命令（type = QUERY）
            if (!seq_id_str.empty()) {
              PendingCommand pending_cmd;
              pending_cmd.seq_id = seq_id_str;
              pending_cmd.type = PendingCommandType::QUERY;
              pending_cmd.category = "query";
              pending_cmd.command = cmd;
              pending_cmd.uart_type = uart_type;
              pending_cmd.uart_subtype = 0;
              pending_cmd.timestamp_ms = esp_timer_get_time() / 1000;
              pending_cmd.esp32_ack_sent = true;
              pending_cmd.stm32_ack_received = false;
              pending_cmd.stm32_error_code = 0;
              pending_commands_[uart_type] = pending_cmd;
            }

            // 发送查询命令到 STM32
            if (cmd == "sensors") {
              lock_control_->QuerySensors();
            } else if (cmd == "status") {
              lock_control_->QueryStatus();
            }
            // ack 将在收到 STM32 数据帧（RPT_ENV/RPT_STATE）后发送
          } else {
            ack_code = 4; // 不支持
            ack_msg = "Unknown query command";
            // 未知命令，直接发送 ack
            if (!seq_id_str.empty()) {
              protocol_->SendAck(seq_id_str, ack_code, ack_msg);
            }
          }
        }
      });
    }
    return true;
  }

  // -------------------------------------------------------------------------
  // 心跳响应
  // -------------------------------------------------------------------------
  if (strcmp(type, "heartbeat_ack") == 0) {
    ESP_LOGD(TAG, "收到心跳响应");
    return true;
  }

  // 未识别的消息类型
  return false;
}

// ============================================================================
// 待处理命令队列相关方法（两级确认机制）
// ============================================================================

/**
 * @brief 确定命令类型
 *
 * 根据命令类别和命令名称确定命令类型：
 * - IMMEDIATE:
 * 即时命令（开锁、关锁、蜂鸣器、OLED、补光灯、删除、清空、设置密码）
 * - QUERY: 查询命令（查询传感器、状态、指纹数量、NFC数量、密码）
 * - LONG_FLOW: 长流程命令（指纹录入、NFC录入）
 *
 * @param category 命令类别
 * @param command 命令名称
 * @return 命令类型枚举
 */
PendingCommandType
Application::DetermineCommandType(const std::string &category,
                                  const std::string &command) {
  // 长流程命令：指纹/NFC 录入
  if ((category == "finger" || category == "nfc") && command == "add") {
    return PendingCommandType::LONG_FLOW;
  }

  // 查询命令
  if (category == "query") {
    return PendingCommandType::QUERY;
  }
  if ((category == "finger" || category == "nfc" || category == "password") &&
      command == "query") {
    return PendingCommandType::QUERY;
  }

  // 其他都是即时命令
  return PendingCommandType::IMMEDIATE;
}

/**
 * @brief 获取 UART TYPE
 *
 * 根据命令类别和命令名称返回对应的 UART TYPE 值。
 *
 * @param category 命令类别
 * @param command 命令名称
 * @return UART 命令 TYPE 值，如果无法确定则返回 0
 */
uint8_t Application::GetUartType(const std::string &category,
                                 const std::string &command) {
  // 锁控命令
  if (category == "lock") {
    return static_cast<uint8_t>(xiaozhi::CmdType::CMD_LOCK);
  }

  // 设备控制命令
  if (category == "dev") {
    if (command == "beep") {
      return static_cast<uint8_t>(xiaozhi::CmdType::CMD_BEEP);
    } else if (command == "oled") {
      return static_cast<uint8_t>(xiaozhi::CmdType::CMD_OLED);
    } else if (command == "light") {
      return static_cast<uint8_t>(xiaozhi::CmdType::CMD_LIGHT);
    }
  }

  // 查询命令
  if (category == "query") {
    if (command == "sensors") {
      return static_cast<uint8_t>(xiaozhi::CmdType::Q_SENSORS);
    } else if (command == "status") {
      return static_cast<uint8_t>(xiaozhi::CmdType::Q_STATUS);
    }
  }

  // 指纹管理命令
  if (category == "finger") {
    return static_cast<uint8_t>(xiaozhi::UserFpCmd::FP_CMD);
  }

  // NFC 管理命令
  if (category == "nfc") {
    return static_cast<uint8_t>(xiaozhi::UserNfcCmd::NFC_CMD);
  }

  // 密码管理命令
  if (category == "password") {
    if (command == "set") {
      return static_cast<uint8_t>(xiaozhi::UserPwdCmd::PWD_SET);
    } else if (command == "query") {
      return static_cast<uint8_t>(xiaozhi::UserPwdCmd::PWD_QUERY);
    }
  }

  // 未知命令
  ESP_LOGW(TAG, "未知命令类型: category=%s, command=%s", category.c_str(),
           command.c_str());
  return 0;
}

/**
 * @brief 映射 STM32 错误码到统一错误码
 *
 * 统一错误码表：
 * - 0: 成功
 * - 2: 设备忙 (STM32: 0x01 ERR_BUSY)
 * - 3: 参数错误 (STM32: 0x03 ERR_PARAM)
 * - 4: 不支持 (STM32: 0x02 ERR_UNSUPPORT)
 * - 5: 超时 (STM32: 0xFF ERR_TIMEOUT)
 * - 6: 硬件故障 (STM32: 0x06 ERR_HARDWARE)
 * - 7: 资源已满 (STM32: 0x04 ERR_FP_FULL, 0x05 ERR_NFC_FULL)
 * - 10: 内部错误（未知错误）
 *
 * @param stm32_err STM32 返回的错误码
 * @return 统一错误码
 */
int Application::MapStm32ErrorCode(uint8_t stm32_err) {
  switch (stm32_err) {
  case 0x00:
    // 成功（ACK_OK 时 data[1] 为 0xFF，但这里处理的是 ACK_ERR 的错误码）
    return 0;

  case static_cast<uint8_t>(xiaozhi::AckError::ERR_BUSY):
    // 0x01: 设备忙
    return 2;

  case static_cast<uint8_t>(xiaozhi::AckError::ERR_UNSUPPORT):
    // 0x02: 不支持的命令
    return 4;

  case static_cast<uint8_t>(xiaozhi::AckError::ERR_PARAM):
    // 0x03: 参数错误
    return 3;

  case static_cast<uint8_t>(xiaozhi::AckError::ERR_FP_FULL):
    // 0x04: 指纹库已满
    return 7;

  case static_cast<uint8_t>(xiaozhi::AckError::ERR_NFC_FULL):
    // 0x05: NFC 库已满
    return 7;

  case static_cast<uint8_t>(xiaozhi::AckError::ERR_HARDWARE):
    // 0x06: 硬件错误
    return 6;

  case static_cast<uint8_t>(xiaozhi::AckError::ERR_TIMEOUT):
    // 0xFF: 操作超时
    return 5;

  default:
    // 未知错误
    ESP_LOGW(TAG, "未知 STM32 错误码: 0x%02X，映射为内部错误", stm32_err);
    return 10;
  }
}

/**
 * @brief 获取命令类型对应的超时时间
 *
 * @param type 命令类型
 * @return 超时时间（毫秒）
 */
int64_t Application::GetTimeoutForType(PendingCommandType type) {
  switch (type) {
  case PendingCommandType::IMMEDIATE:
    return IMMEDIATE_TIMEOUT_MS;
  case PendingCommandType::QUERY:
    return QUERY_TIMEOUT_MS;
  case PendingCommandType::LONG_FLOW:
    return LONG_FLOW_TIMEOUT_MS;
  default:
    return IMMEDIATE_TIMEOUT_MS;
  }
}

/**
 * @brief 清理超时的待处理命令
 *
 * 遍历 pending_commands_，检查是否有超时的命令，
 * 如果有则发送 ack(code=5) 并移除。
 *
 * 此方法应在时钟节拍中定期调用（每秒一次）。
 */
void Application::CleanupPendingCommands() {
  if (pending_commands_.empty()) {
    return;
  }

  // 获取当前时间戳（毫秒）
  int64_t now_ms = esp_timer_get_time() / 1000;

  // 收集需要删除的命令（避免在遍历时修改容器）
  std::vector<uint8_t> expired_keys;

  for (const auto &pair : pending_commands_) {
    const PendingCommand &cmd = pair.second;
    int64_t timeout_ms = GetTimeoutForType(cmd.type);
    int64_t elapsed_ms = now_ms - cmd.timestamp_ms;

    if (elapsed_ms > timeout_ms) {
      ESP_LOGW(TAG, "命令超时: seq_id=%s, uart_type=0x%02X, elapsed=%lld ms",
               cmd.seq_id.c_str(), cmd.uart_type, elapsed_ms);
      expired_keys.push_back(pair.first);
    }
  }

  // 处理超时命令
  for (uint8_t key : expired_keys) {
    auto it = pending_commands_.find(key);
    if (it != pending_commands_.end()) {
      const PendingCommand &cmd = it->second;

      // 发送超时 ack（code=5 表示超时）
      if (protocol_ && !cmd.seq_id.empty()) {
        ESP_LOGI(TAG, "发送超时 ack: seq_id=%s, code=5", cmd.seq_id.c_str());
        protocol_->SendAck(cmd.seq_id, 5, "Timeout");
      }

      // 移除超时命令
      pending_commands_.erase(it);
    }
  }
}

// ============================================================================
// 本地预览功能
// ============================================================================

/**
 * @brief 启动本地预览
 *
 * 执行以下操作：
 * 1. 检查摄像头可用性
 * 2. 检查与监控模式的互斥（IsMonitorMode()）
 * 3. 检查与人脸识别的互斥（face_recognition_in_progress_）
 * 4. 创建 FreeRTOS 队列（深度 1）
 * 5. 创建 Capture Task（优先级 5，栈 4096）
 * 6. 创建 Display Task（优先级 5，栈 4096）
 * 7. 调用 EnterPreviewMode() 切换显示模式
 * 8. 设置 local_preview_active_ 为 true
 * 9. 播放确认音效
 *
 * @return bool 成功返回 true，失败返回 false
 */
bool Application::StartLocalPreview() {
  ESP_LOGI(TAG, "启动本地预览");

  // 1. 检查是否已经在运行
  if (local_preview_active_) {
    ESP_LOGW(TAG, "本地预览已在运行中");
    return false;
  }

  // 2. 检查与监控模式的互斥
  if (IsMonitorMode()) {
    ESP_LOGE(TAG, "无法启动本地预览：监控模式正在运行");
    Alert("错误", "监控模式运行中", "circle_xmark",
          Lang::Sounds::OGG_EXCLAMATION);
    return false;
  }

  // 3. 检查与人脸识别的互斥
  if (face_recognition_in_progress_) {
    ESP_LOGE(TAG, "无法启动本地预览：人脸识别正在进行");
    Alert("错误", "人脸识别运行中", "circle_xmark",
          Lang::Sounds::OGG_EXCLAMATION);
    return false;
  }

  // 4. 检查摄像头可用性
  auto &board = Board::GetInstance();
  auto camera = board.GetCamera();
  if (!camera) {
    ESP_LOGE(TAG, "摄像头不可用");
    Alert("错误", "摄像头不可用", "circle_xmark",
          Lang::Sounds::OGG_EXCLAMATION);
    return false;
  }

  // 5. 检查可用内存
  size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t MIN_FREE_MEMORY = 400 * 1024; // 最小 400KB
  if (free_psram < MIN_FREE_MEMORY) {
    ESP_LOGW(TAG, "内存不足，拒绝启动本地预览 (可用 PSRAM: %u KB)",
             (unsigned)(free_psram / 1024));
    Alert("错误", "内存不足", "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
    return false;
  }
  ESP_LOGI(TAG, "内存检查通过 (可用 PSRAM: %u KB)",
           (unsigned)(free_psram / 1024));

  // 6. 创建 FreeRTOS 队列（深度 1）
  preview_frame_queue_ = xQueueCreate(1, sizeof(PreviewFrame *));
  if (preview_frame_queue_ == nullptr) {
    ESP_LOGE(TAG, "无法创建帧队列");
    Alert("错误", "系统错误", "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
    return false;
  }

  // 7. 设置活动标志（必须在创建任务之前，否则任务循环条件不满足会立即退出）
  local_preview_active_ = true;

  // 8. 创建 Capture Task（优先级 5，栈 4096）
  BaseType_t ret = xTaskCreate(
      [](void *param) {
        Application *app = static_cast<Application *>(param);
        app->PreviewCaptureLoop();
        vTaskDelete(NULL);
      },
      "preview_capture", 4096, this, 5, &preview_capture_task_);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "无法创建捕获任务");
    local_preview_active_ = false;
    vQueueDelete(preview_frame_queue_);
    preview_frame_queue_ = nullptr;
    Alert("错误", "系统错误", "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
    return false;
  }

  // 9. 创建 Display Task（优先级 5，栈 4096）
  ret = xTaskCreate(
      [](void *param) {
        Application *app = static_cast<Application *>(param);
        app->PreviewDisplayLoop();
        vTaskDelete(NULL);
      },
      "preview_display", 4096, this, 5, &preview_display_task_);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "无法创建显示任务");
    // 清理已创建的资源
    local_preview_active_ = false;
    vTaskDelete(preview_capture_task_);
    preview_capture_task_ = nullptr;
    vQueueDelete(preview_frame_queue_);
    preview_frame_queue_ = nullptr;
    Alert("错误", "系统错误", "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
    return false;
  }

  // 10. 切换显示模式
  auto display = board.GetDisplay();
  auto lcd_display = dynamic_cast<LcdDisplay *>(display);
  if (lcd_display && !lcd_display->EnterPreviewMode()) {
    ESP_LOGW(TAG, "无法进入预览模式，但继续运行");
  }

  // 11. 播放确认音效
  ESP_LOGI(TAG, "本地预览已启动");
  PlaySound(Lang::Sounds::OGG_SUCCESS);

  return true;
}

/**
 * @brief 停止本地预览
 *
 * 执行以下操作：
 * 1. 设置 local_preview_active_ 为 false
 * 2. 等待任务退出（延迟 200ms）
 * 3. 清空队列并释放所有帧内存
 * 4. 删除队列
 * 5. 调用 ExitPreviewMode() 恢复显示模式
 * 6. 播放确认音效
 */
void Application::StopLocalPreview() {
  if (!local_preview_active_) {
    ESP_LOGW(TAG, "本地预览未运行");
    return;
  }

  ESP_LOGI(TAG, "停止本地预览");

  // 1. 设置停止标志
  local_preview_active_ = false;

  // 2. 等待任务退出
  if (preview_capture_task_ != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(200));
    preview_capture_task_ = nullptr;
  }

  if (preview_display_task_ != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(200));
    preview_display_task_ = nullptr;
  }

  // 3. 清空队列并释放所有帧内存
  if (preview_frame_queue_ != nullptr) {
    PreviewFrame *frame = nullptr;
    while (xQueueReceive(preview_frame_queue_, &frame, 0) == pdTRUE) {
      if (frame != nullptr) {
        delete frame; // 析构函数会释放 PSRAM
      }
    }
    // 4. 删除队列
    vQueueDelete(preview_frame_queue_);
    preview_frame_queue_ = nullptr;
  }

  // 5. 恢复显示模式
  auto &board = Board::GetInstance();
  auto display = board.GetDisplay();
  auto lcd_display = dynamic_cast<LcdDisplay *>(display);
  if (lcd_display) {
    lcd_display->ExitPreviewMode();
  }

  // 6. 播放确认音效
  ESP_LOGI(TAG, "本地预览已停止");
  PlaySound(Lang::Sounds::OGG_SUCCESS);
}

/**
 * @brief 检查本地预览是否活动
 *
 * @return bool 活动返回 true，否则返回 false
 */
bool Application::IsLocalPreviewActive() const { return local_preview_active_; }

/**
 * @brief 预览捕获任务循环
 *
 * 以 15 FPS 频率捕获帧（66ms 间隔）：
 * 1. 调用 CaptureForPreview() 获取 RGB565 数据
 * 2. 分配 PreviewFrame 对象并复制数据
 * 3. 推送到 FreeRTOS 队列（非阻塞）
 * 4. 队列满时丢弃旧帧并插入新帧
 * 5. 添加连续失败检测和错误恢复
 */
void Application::PreviewCaptureLoop() {
  ESP_LOGI(TAG, "预览捕获任务启动");

  int consecutive_failures = 0;
  const int MAX_FAILURES = 10;
  const TickType_t FRAME_INTERVAL = pdMS_TO_TICKS(100); // 10 FPS

  auto &board = Board::GetInstance();

  while (local_preview_active_) {
    TickType_t start_tick = xTaskGetTickCount();

    auto camera = board.GetCamera();
    auto esp32_camera = dynamic_cast<Esp32Camera *>(camera);

    // 捕获帧（包含 JPEG 解码 + RGB888→RGB565 转换）
    if (!esp32_camera || !esp32_camera->CaptureForPreview()) {
      consecutive_failures++;
      ESP_LOGW(TAG, "帧捕获失败 (%d/%d)", consecutive_failures, MAX_FAILURES);

      // 连续失败过多，停止服务
      if (consecutive_failures >= MAX_FAILURES) {
        ESP_LOGE(TAG, "连续捕获失败过多 (%d 次)，停止预览", MAX_FAILURES);
        Schedule([this]() {
          StopLocalPreview();
          Alert("错误", "摄像头异常", "circle_xmark",
                Lang::Sounds::OGG_EXCLAMATION);
        });
        break;
      }

      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // 重置失败计数
    consecutive_failures = 0;

    // 获取帧数据
    const uint8_t *rgb565_data = esp32_camera->GetRgb565Data();
    size_t data_size = esp32_camera->GetRgb565DataSize();
    uint16_t width = esp32_camera->GetFrameWidth();
    uint16_t height = esp32_camera->GetFrameHeight();

    if (rgb565_data == nullptr || data_size == 0) {
      ESP_LOGW(TAG, "捕获的帧数据无效 (data=%p, size=%d)", rgb565_data,
               (int)data_size);
      vTaskDelay(FRAME_INTERVAL);
      continue;
    }

    // 计算捕获耗时
    TickType_t capture_end_tick = xTaskGetTickCount();
    uint32_t capture_time_ms =
        (capture_end_tick - start_tick) * portTICK_PERIOD_MS;

    ESP_LOGI(TAG, "[预览性能] 捕获+解码=%ums, 帧=%dx%d, 数据=%d bytes",
             capture_time_ms, width, height, (int)data_size);

    // DEBUG 日志：帧捕获详情
    ESP_LOGD(TAG, "帧已捕获: %dx%d, 大小=%zu 字节, 耗时=%u ms, 时间戳=%u",
             width, height, data_size, capture_time_ms,
             (unsigned)capture_end_tick);

    // 分配帧对象
    PreviewFrame *frame = new (std::nothrow) PreviewFrame(width, height);

    if (frame == nullptr || !frame->IsValid()) {
      ESP_LOGE(TAG, "无法分配帧对象 (需要 %zu 字节)，停止预览", data_size);
      if (frame != nullptr) {
        delete frame;
      }
      Schedule([this]() {
        StopLocalPreview();
        Alert("错误", "内存不足", "circle_xmark",
              Lang::Sounds::OGG_EXCLAMATION);
      });
      break;
    }

    // 复制数据
    memcpy(frame->data, rgb565_data, data_size);
    frame->timestamp = capture_end_tick;

    // 发送到队列（非阻塞）
    if (xQueueSend(preview_frame_queue_, &frame, 0) != pdTRUE) {
      // 队列满，丢弃旧帧
      ESP_LOGD(TAG, "队列已满，丢弃旧帧");
      PreviewFrame *old_frame = nullptr;
      if (xQueueReceive(preview_frame_queue_, &old_frame, 0) == pdTRUE) {
        if (old_frame != nullptr) {
          delete old_frame;
        }
      }
      // 重新发送
      if (xQueueSend(preview_frame_queue_, &frame, 0) != pdTRUE) {
        ESP_LOGW(TAG, "队列操作失败，丢弃当前帧");
        delete frame;
      }
    }

    // 控制帧率
    TickType_t elapsed = xTaskGetTickCount() - start_tick;
    if (elapsed < FRAME_INTERVAL) {
      vTaskDelay(FRAME_INTERVAL - elapsed);
    }
  }

  ESP_LOGI(TAG, "预览捕获任务退出");
}

/**
 * @brief 预览显示任务循环
 *
 * 从队列获取最新帧并更新显示：
 * 1. 从队列获取最新帧（超时 100ms）
 * 2. 调用 UpdatePreviewCanvas() 更新显示
 * 3. 释放帧内存
 * 4. 添加错误处理（显示失败、队列超时等）
 */
void Application::PreviewDisplayLoop() {
  ESP_LOGI(TAG, "预览显示任务启动");

  auto &board = Board::GetInstance();
  auto display = board.GetDisplay();
  auto lcd_display = dynamic_cast<LcdDisplay *>(display);

  if (!lcd_display) {
    ESP_LOGE(TAG, "显示对象无效，退出显示任务");
    return;
  }

  while (local_preview_active_) {
    PreviewFrame *frame = nullptr;

    TickType_t receive_start = xTaskGetTickCount();

    // 从队列获取帧（超时 100ms）
    if (xQueueReceive(preview_frame_queue_, &frame, pdMS_TO_TICKS(100)) !=
        pdTRUE) {
      // 队列超时，继续等待
      ESP_LOGD(TAG, "队列超时，等待新帧");
      continue;
    }

    if (frame == nullptr || !frame->IsValid()) {
      ESP_LOGW(TAG, "收到无效帧 (frame=%p)", frame);
      if (frame != nullptr) {
        delete frame;
      }
      continue;
    }

    // 计算端到端延迟
    TickType_t display_start = xTaskGetTickCount();
    uint32_t latency_ms =
        (display_start - frame->timestamp) * portTICK_PERIOD_MS;

    // 更新 Canvas
    TickType_t render_start = xTaskGetTickCount();
    bool update_success = lcd_display->UpdatePreviewCanvas(
        frame->data, frame->width, frame->height);
    TickType_t render_end = xTaskGetTickCount();

    uint32_t render_time_ms = (render_end - render_start) * portTICK_PERIOD_MS;
    uint32_t total_time_ms = (render_end - receive_start) * portTICK_PERIOD_MS;

    if (!update_success) {
      ESP_LOGW(TAG, "更新 Canvas 失败 (尺寸=%dx%d)", frame->width,
               frame->height);
    } else {
      ESP_LOGI(TAG, "[预览性能] 缩放+显示=%ums, 端到端延迟=%ums",
               render_time_ms, latency_ms);
    }

    // 释放帧内存
    delete frame;
  }

  ESP_LOGI(TAG, "预览显示任务退出");
}

// ============================================================================
// 自动连接功能实现
// ============================================================================

/**
 * @brief 自动连接任务循环
 *
 * 该任务在后台持续运行，负责：
 * 1. 检查是否需要连接服务器
 * 2. 尝试建立连接
 * 3. 连接失败后使用指数退避策略重试
 * 4. 连接成功后监控连接状态
 */
void Application::AutoConnectLoop() {
  ESP_LOGI(TAG, "自动连接任务已启动");

  // 等待 5 秒，确保系统初始化完成
  vTaskDelay(pdMS_TO_TICKS(5000));

  while (true) {
    // 1. 检查是否需要自动连接
    if (ShouldAutoConnect()) {
      ESP_LOGI(TAG, "尝试自动连接服务器 (重试次数: %d)",
               connection_retry_count_);

      // 2. 尝试连接服务器
      bool connect_success = false;
      if (protocol_) {
        // 使用 Schedule 在主线程中执行连接操作
        Schedule([this, &connect_success]() {
          if (protocol_->OpenAudioChannel()) {
            connect_success = true;
            connection_retry_count_ = 0;         // 重置重试计数
            user_manually_disconnected_ = false; // 清除手动断开标志
            ESP_LOGI(TAG, "自动连接成功");
          } else {
            ESP_LOGE(TAG, "自动连接失败");
          }
        });

        // 等待连接操作完成（最多等待 15 秒）
        for (int i = 0; i < 30; i++) {
          vTaskDelay(pdMS_TO_TICKS(500));
          if (connect_success || protocol_->IsAudioChannelOpened()) {
            break;
          }
        }
      }

      // 3. 根据连接结果决定下一步
      if (protocol_ && protocol_->IsAudioChannelOpened()) {
        // 连接成功，监控连接状态
        ESP_LOGI(TAG, "连接已建立，开始监控连接状态");

        // ========== 连接成功后关闭背光（门锁模式）==========
        auto lcd_display = dynamic_cast<LcdDisplay *>(
            Board::GetInstance().GetDisplay());
        if (lcd_display) {
          lcd_display->HideAllUI();
          ESP_LOGI(TAG, "已切换到门锁模式（背光关闭）");
        }
        // ========================================================

        while (protocol_->IsAudioChannelOpened()) {
          vTaskDelay(pdMS_TO_TICKS(5000)); // 每 5 秒检查一次
        }

        ESP_LOGW(TAG, "连接已断开");
        connection_retry_count_ = 0; // 重置重试计数

        // 等待 2 秒后再尝试重连
        vTaskDelay(pdMS_TO_TICKS(2000));

      } else {
        // 连接失败，使用指数退避
        connection_retry_count_++;
        int delay_ms = CalculateRetryDelay(connection_retry_count_);

        ESP_LOGW(TAG, "自动连接失败，等待 %d 毫秒后重试 (重试次数: %d)",
                 delay_ms, connection_retry_count_);

        // 分段延迟，以便及时响应状态变化
        int delay_steps = delay_ms / 1000; // 每秒检查一次
        for (int i = 0; i < delay_steps; i++) {
          vTaskDelay(pdMS_TO_TICKS(1000));

          // 如果用户主动断开或状态不适合连接，提前退出延迟
          if (user_manually_disconnected_ || !ShouldAutoConnect()) {
            ESP_LOGI(TAG, "检测到状态变化，取消重试延迟");
            break;
          }
        }
      }

    } else {
      // 不需要连接，等待 5 秒后再检查
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
  }
}

/**
 * @brief 判断是否应该自动连接
 * @return 如果满足自动连接条件返回 true
 */
bool Application::ShouldAutoConnect() {
  // 1. 检查自动连接功能是否启用
  if (!auto_connect_enabled_) {
    return false;
  }

  // 2. 检查 Protocol 是否已初始化
  if (!protocol_) {
    return false;
  }

  // 3. 检查是否已连接服务器
  if (protocol_->IsAudioChannelOpened()) {
    return false;
  }

  // 4. 检查用户是否主动断开连接
  if (user_manually_disconnected_) {
    return false;
  }

  // 5. 检查设备状态是否适合连接
  // 只在 Idle 状态下自动连接
  if (device_state_ != kDeviceStateIdle) {
    return false;
  }

  // 6. 检查网络是否已连接
  auto &board = Board::GetInstance();
  auto network = board.GetNetwork();
  if (!network) {
    return false;
  }

  // 7. 检查是否处于监控模式
  if (IsMonitorMode()) {
    return false;
  }

  // 所有条件都满足，可以自动连接
  return true;
}

/**
 * @brief 计算指数退避延迟时间
 * @param retry_count 当前重试次数
 * @return 延迟时间（毫秒）
 */
int Application::CalculateRetryDelay(int retry_count) {
  // 指数退避：1s, 2s, 4s, 8s, 16s, 32s, 60s(max)
  int delay_ms = INITIAL_RETRY_DELAY_MS * (1 << retry_count);

  // 限制最大延迟
  if (delay_ms > MAX_RETRY_DELAY_MS) {
    delay_ms = MAX_RETRY_DELAY_MS;
  }

  // 添加随机抖动 (±20%)，避免多设备同时重连
  int jitter_range = delay_ms / 5; // 20% 的范围
  int jitter = (esp_random() % (jitter_range * 2)) - jitter_range;

  int final_delay = delay_ms + jitter;

  // 确保延迟至少为 1 秒
  if (final_delay < 1000) {
    final_delay = 1000;
  }

  return final_delay;
}
