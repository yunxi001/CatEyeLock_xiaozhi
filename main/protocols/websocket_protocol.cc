#include "websocket_protocol.h"
#include "board.h"
#include "system_info.h"
#include "application.h"
#include "settings.h"

#include <cstring>
#include <cJSON.h>
#include <esp_log.h>
#include <arpa/inet.h>
#include "assets/lang_config.h"

#define TAG "WS"

WebsocketProtocol::WebsocketProtocol() {
    event_group_handle_ = xEventGroupCreate();
}

WebsocketProtocol::~WebsocketProtocol() {
    vEventGroupDelete(event_group_handle_);
}

bool WebsocketProtocol::Start() {
    // Only connect to server when audio channel is needed
    return true;
}

bool WebsocketProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }

    if (version_ == 2) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol2) + packet->payload.size());
        auto bp2 = (BinaryProtocol2*)serialized.data();
        bp2->version = htons(version_);
        bp2->type = 0;
        bp2->reserved = 0;
        bp2->timestamp = htonl(packet->timestamp);
        bp2->payload_size = htonl(packet->payload.size());
        memcpy(bp2->payload, packet->payload.data(), packet->payload.size());

        return websocket_->Send(serialized.data(), serialized.size(), true);
    } else if (version_ == 3) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol3) + packet->payload.size());
        auto bp3 = (BinaryProtocol3*)serialized.data();
        bp3->type = 0;
        bp3->reserved = 0;
        bp3->payload_size = htons(packet->payload.size());
        memcpy(bp3->payload, packet->payload.data(), packet->payload.size());

        return websocket_->Send(serialized.data(), serialized.size(), true);
    } else {
        return websocket_->Send(packet->payload.data(), packet->payload.size(), true);
    }
}

bool WebsocketProtocol::SendText(const std::string& text) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }

    if (!websocket_->Send(text)) {
        ESP_LOGE(TAG, "Failed to send text: %s", text.c_str());
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }

    return true;
}

// 实现 SetStreamingMode 方法
void WebsocketProtocol::SetStreamingMode(bool enabled) {
    streaming_av_mode_ = enabled;
    ESP_LOGI(TAG, "Streaming AV mode set to: %s", enabled ? "true" : "false");
}

// 实现 SendBinary 方法
bool WebsocketProtocol::SendBinary(const uint8_t* data, size_t len) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }
    return websocket_->Send(data, len, true); // 作为二进制帧发送
}


bool WebsocketProtocol::IsAudioChannelOpened() const {
    return websocket_ != nullptr && websocket_->IsConnected() && !error_occurred_ && !IsTimeout();
}

void WebsocketProtocol::CloseAudioChannel() {
    websocket_.reset();
    // 确保在关闭音频通道时也关闭流模式
    streaming_av_mode_ = false; 
}

bool WebsocketProtocol::OpenAudioChannel() {
    Settings settings("websocket", false);
    std::string url = settings.GetString("url");
    std::string token = settings.GetString("token");
    int version = settings.GetInt("version");
    if (version != 0) {
        version_ = version;
    }

    error_occurred_ = false;

    auto network = Board::GetInstance().GetNetwork();
    websocket_ = network->CreateWebSocket(1);
    if (websocket_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create websocket");
        return false;
    }

    if (!token.empty()) {
        // If token not has a space, add "Bearer " prefix
        if (token.find(" ") == std::string::npos) {
            token = "Bearer " + token;
        }
        websocket_->SetHeader("Authorization", token.c_str());
    }
    websocket_->SetHeader("Protocol-Version", std::to_string(version_).c_str());
    websocket_->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    websocket_->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());

    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        if (binary) {
            if (streaming_av_mode_) { // 如果处于音视频流模式，所有二进制数据都视为音频
                if (on_incoming_audio_ != nullptr) {
                    // 在AV流模式下，假设直接接收裸音频数据或预定格式
                    // 这里简化处理，直接将接收到的二进制数据封装为 AudioStreamPacket
                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                        .sample_rate = server_sample_rate_, // 沿用协议协商的采样率
                        .frame_duration = server_frame_duration_, // 沿用协议协商的帧时长
                        .timestamp = 0, // AV流模式下时间戳可能由流本身提供或不严格要求
                        .payload = std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len)
                    }));
                }
            } else { // 否则，按照原有逻辑处理（TTS或聊天音频）
                if (on_incoming_audio_ != nullptr) {
                    if (version_ == 2) {
                        BinaryProtocol2* bp2 = (BinaryProtocol2*)data;
                        bp2->version = ntohs(bp2->version);
                        bp2->type = ntohs(bp2->type);
                        bp2->timestamp = ntohl(bp2->timestamp);
                        bp2->payload_size = ntohl(bp2->payload_size);
                        auto payload = (uint8_t*)bp2->payload;
                        on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                            .sample_rate = server_sample_rate_,
                            .frame_duration = server_frame_duration_,
                            .timestamp = bp2->timestamp,
                            .payload = std::vector<uint8_t>(payload, payload + bp2->payload_size)
                        }));
                    } else if (version_ == 3) {
                        BinaryProtocol3* bp3 = (BinaryProtocol3*)data;
                        bp3->type = bp3->type;
                        bp3->payload_size = ntohs(bp3->payload_size);
                        auto payload = (uint8_t*)bp3->payload;
                        on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                            .sample_rate = server_sample_rate_,
                            .frame_duration = server_frame_duration_,
                            .timestamp = 0,
                            .payload = std::vector<uint8_t>(payload, payload + bp3->payload_size)
                        }));
                    } else {
                        on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                            .sample_rate = server_sample_rate_,
                            .frame_duration = server_frame_duration_,
                            .timestamp = 0,
                            .payload = std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len)
                        }));
                    }
                }
            }
        } else { // Text frames (JSON)
            if (streaming_av_mode_) {
                // 在AV流模式下接收到文本帧，可能是停止流的命令或者意外数据
                ESP_LOGW(TAG, "Received text frame in AV streaming mode, data: %s", data);
            }
            // 无论是否在流模式，都尝试解析JSON，以防是停止流等重要控制命令
            auto root = cJSON_Parse(data);
            if (root == nullptr) {
                ESP_LOGE(TAG, "Failed to parse JSON data: %s", data);
                // If it's not JSON, then it's an error. We don't want to crash.
                // Just log and continue.
            } else {
                auto type = cJSON_GetObjectItem(root, "type");
                if (cJSON_IsString(type)) {
                    if (strcmp(type->valuestring, "hello") == 0) {
                        ParseServerHello(root);
                    } else {
                        if (on_incoming_json_ != nullptr) {
                            on_incoming_json_(root);
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "Missing message type, data: %s", data);
                }
                cJSON_Delete(root);
            }
        }
        last_incoming_time_ = std::chrono::steady_clock::now();
    });

    websocket_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "Websocket disconnected");
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
        streaming_av_mode_ = false; // 断开连接时，自动退出流模式
    });

    ESP_LOGI(TAG, "Connecting to websocket server: %s with version: %d", url.c_str(), version_);
    if (!websocket_->Connect(url.c_str())) {
        ESP_LOGE(TAG, "Failed to connect to websocket server, code=%d", websocket_->GetLastError());
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        return false;
    }

    // Send hello message to describe the client
    auto message = GetHelloMessage();
    if (!SendText(message)) {
        return false;
    }

    // Wait for server hello
    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT)) {
        ESP_LOGE(TAG, "Failed to receive server hello");
        SetError(Lang::Strings::SERVER_TIMEOUT);
        return false;
    }

    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }

    return true;
}

std::string WebsocketProtocol::GetHelloMessage() {
    // keys: message type, version, audio_params (format, sample_rate, channels)
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", version_);
    cJSON* features = cJSON_CreateObject();
#if CONFIG_USE_SERVER_AEC
    cJSON_AddBoolToObject(features, "aec", true);
#endif
    cJSON_AddBoolToObject(features, "mcp", true);
    cJSON_AddItemToObject(root, "features", features);
    cJSON_AddStringToObject(root, "transport", "websocket");
    cJSON* audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", "opus");
    cJSON_AddNumberToObject(audio_params, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio_params, "channels", 1);
    cJSON_AddNumberToObject(audio_params, "frame_duration", OPUS_FRAME_DURATION_MS);
    cJSON_AddItemToObject(root, "audio_params", audio_params);
    auto json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return message;
}

void WebsocketProtocol::ParseServerHello(const cJSON* root) {
    auto transport = cJSON_GetObjectItem(root, "transport");
    if (transport == nullptr || strcmp(transport->valuestring, "websocket") != 0) {
        ESP_LOGE(TAG, "Unsupported transport: %s", transport->valuestring);
        return;
    }

    auto session_id = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(session_id)) {
        session_id_ = session_id->valuestring;
        ESP_LOGI(TAG, "Session ID: %s", session_id_.c_str());
    }

    auto audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (cJSON_IsObject(audio_params)) {
        auto sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        if (cJSON_IsNumber(sample_rate)) {
            server_sample_rate_ = sample_rate->valueint;
        }
        auto frame_duration = cJSON_GetObjectItem(audio_params, "frame_duration");
        if (cJSON_IsNumber(frame_duration)) {
            server_frame_duration_ = frame_duration->valueint;
        }
    }

    xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT);
}
