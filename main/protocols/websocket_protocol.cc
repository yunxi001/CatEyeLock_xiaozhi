/**
 * @file websocket_protocol.cc
 * @brief WebSocket 通信协议实现
 *
 * 实现基于 WebSocket 的双向通信，支持：
 * - 音频流：OPUS 编码，BinaryProtocol2/3 格式
 * - 视频流：JPEG 编码，仅监控模式
 * - 人脸识别：JPEG 图像，type=2 标识
 * - JSON 消息：文本格式
 */

#include "websocket_protocol.h"
#include "application.h"
#include "board.h"
#include "settings.h"
#include "system_info.h"

#include "assets/lang_config.h"
#include <arpa/inet.h>
#include <cJSON.h>
#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>

#define TAG "WS"

// ============================================================================
// 构造与析构
// ============================================================================

WebsocketProtocol::WebsocketProtocol() {
  event_group_handle_ = xEventGroupCreate();
}

WebsocketProtocol::~WebsocketProtocol() {
  vEventGroupDelete(event_group_handle_);
}

// ============================================================================
// 生命周期管理
// ============================================================================

bool WebsocketProtocol::Start() {
  // WebSocket 连接在需要音频通道时才建立
  return true;
}

// ============================================================================
// 音频发送
// ============================================================================

/**
 * @brief 发送音频数据
 *
 * 根据协议版本和模式选择不同的封装格式：
 * - 监控模式：强制使用 BinaryProtocol2
 * - v2：使用 BinaryProtocol2（支持 AEC）
 * - v3：使用 BinaryProtocol3（简化格式）
 * - v1：直接发送原始数据
 */
bool WebsocketProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
  if (websocket_ == nullptr || !websocket_->IsConnected()) {
    return false;
  }

  // 监控模式：强制使用 BinaryProtocol2 格式
  if (monitor_mode_) {
    std::string serialized;
    serialized.resize(sizeof(BinaryProtocol2) + packet->payload.size());
    auto bp2 = (BinaryProtocol2 *)serialized.data();
    bp2->version = htons(2);
    bp2->type = 0;
    bp2->reserved = 0; // 音频：reserved = 0
    bp2->timestamp = htonl(packet->timestamp);
    bp2->payload_size = htonl(packet->payload.size());
    memcpy(bp2->payload, packet->payload.data(), packet->payload.size());

    return websocket_->Send(serialized.data(), serialized.size(), true);
  }

  // 正常模式：根据版本选择格式
  if (version_ == 2) {
    std::string serialized;
    serialized.resize(sizeof(BinaryProtocol2) + packet->payload.size());
    auto bp2 = (BinaryProtocol2 *)serialized.data();
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
    auto bp3 = (BinaryProtocol3 *)serialized.data();
    bp3->type = 0;
    bp3->reserved = 0;
    bp3->payload_size = htons(packet->payload.size());
    memcpy(bp3->payload, packet->payload.data(), packet->payload.size());

    return websocket_->Send(serialized.data(), serialized.size(), true);
  } else {
    // v1：直接发送原始数据
    return websocket_->Send(packet->payload.data(), packet->payload.size(),
                            true);
  }
}

// ============================================================================
// 视频发送
// ============================================================================

/**
 * @brief 发送视频帧（仅监控模式）
 *
 * 使用 BinaryProtocol2 格式，通过 reserved 字段编码宽高信息。
 */
bool WebsocketProtocol::SendVideo(const uint8_t *data, size_t size,
                                  uint32_t timestamp, uint16_t width,
                                  uint16_t height) {
  if (websocket_ == nullptr || !websocket_->IsConnected()) {
    ESP_LOGW(TAG, "无法发送视频: WebSocket 未连接");
    return false;
  }

  if (data == nullptr || size == 0) {
    ESP_LOGW(TAG, "无法发送视频: 数据无效");
    return false;
  }

  // 视频仅在监控模式下发送
  if (!monitor_mode_) {
    ESP_LOGW(TAG, "无法发送视频: 非监控模式");
    return false;
  }

  // 构建 BinaryProtocol2 格式
  std::string serialized;
  serialized.resize(sizeof(BinaryProtocol2) + size);
  auto bp2 = (BinaryProtocol2 *)serialized.data();
  bp2->version = htons(2);
  bp2->type = 0;
  // 视频：reserved 字段编码宽高（高16位:宽, 低16位:高）
  bp2->reserved = htonl(((uint32_t)width << 16) | (uint32_t)height);
  bp2->timestamp = htonl(timestamp);
  bp2->payload_size = htonl(size);
  memcpy(bp2->payload, data, size);

  ESP_LOGD(TAG, "发送视频帧: %dx%d, 大小=%zu, 时间戳=%u", width, height, size,
           timestamp);
  return websocket_->Send(serialized.data(), serialized.size(), true);
}

// ============================================================================
// 人脸识别图像发送
// ============================================================================

/**
 * @brief 发送人脸识别图像（正常模式）
 *
 * 使用 BinaryProtocol2 格式，type=2 标识人脸识别图像。
 * 服务器通过 type 字段区分音频、视频和人脸识别数据。
 */
bool WebsocketProtocol::SendFaceRecognition(const uint8_t *jpeg_data,
                                            size_t jpeg_size, uint16_t width,
                                            uint16_t height) {
  if (websocket_ == nullptr || !websocket_->IsConnected()) {
    ESP_LOGW(TAG, "无法发送人脸识别图像: WebSocket 未连接");
    return false;
  }

  if (jpeg_data == nullptr || jpeg_size == 0) {
    ESP_LOGW(TAG, "无法发送人脸识别图像: 数据无效");
    return false;
  }

  // 构建 BinaryProtocol2 格式，type=2 表示人脸识别
  std::string serialized;
  serialized.resize(sizeof(BinaryProtocol2) + jpeg_size);
  auto bp2 = (BinaryProtocol2 *)serialized.data();
  bp2->version = htons(2);
  bp2->type = htons(2); // type=2: 人脸识别图像
  // reserved 字段编码宽高信息
  bp2->reserved = htonl(((uint32_t)width << 16) | (uint32_t)height);
  bp2->timestamp = htonl((uint32_t)(esp_timer_get_time() / 1000));
  bp2->payload_size = htonl(jpeg_size);
  memcpy(bp2->payload, jpeg_data, jpeg_size);

  ESP_LOGI(TAG, "发送人脸识别图像: %dx%d, 大小=%zu 字节", width, height,
           jpeg_size);

  bool success = websocket_->Send(serialized.data(), serialized.size(), true);
  if (!success) {
    ESP_LOGE(TAG, "发送人脸识别请求失败");
  }
  return success;
}

// ============================================================================
// 文本消息发送
// ============================================================================

bool WebsocketProtocol::SendText(const std::string &text) {
  if (websocket_ == nullptr || !websocket_->IsConnected()) {
    return false;
  }

  if (!websocket_->Send(text)) {
    ESP_LOGE(TAG, "发送文本失败: %s", text.c_str());
    SetError(Lang::Strings::SERVER_ERROR);
    return false;
  }

  return true;
}

// ============================================================================
// 通道管理
// ============================================================================

bool WebsocketProtocol::IsAudioChannelOpened() const {
  return websocket_ != nullptr && websocket_->IsConnected() &&
         !error_occurred_ && !IsTimeout();
}

void WebsocketProtocol::CloseAudioChannel() { websocket_.reset(); }

/**
 * @brief 打开音频通道
 *
 * 建立 WebSocket 连接，完成握手流程：
 * 1. 从 NVS 读取配置（URL、Token、版本）
 * 2. 创建 WebSocket 连接
 * 3. 发送客户端 Hello 消息
 * 4. 等待服务器 Hello 响应
 */
bool WebsocketProtocol::OpenAudioChannel() {
  // 从 NVS 读取配置
  Settings settings("websocket", false);
  std::string url = settings.GetString("url");
  std::string token = settings.GetString("token");
  int version = settings.GetInt("version");
  if (version != 0) {
    version_ = version;
  }

  error_occurred_ = false;

  // 创建 WebSocket 连接
  auto network = Board::GetInstance().GetNetwork();
  websocket_ = network->CreateWebSocket(1);
  if (websocket_ == nullptr) {
    ESP_LOGE(TAG, "创建 WebSocket 失败");
    return false;
  }

  // 设置请求头
  if (!token.empty()) {
    // 如果 token 没有空格，添加 "Bearer " 前缀
    if (token.find(" ") == std::string::npos) {
      token = "Bearer " + token;
    }
    websocket_->SetHeader("Authorization", token.c_str());
  }
  websocket_->SetHeader("Protocol-Version", std::to_string(version_).c_str());
  websocket_->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
  websocket_->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());

  // 设置数据接收回调
  websocket_->OnData([this](const char *data, size_t len, bool binary) {
    if (binary) {
      // 处理二进制数据（音频）
      if (on_incoming_audio_ != nullptr) {
        if (version_ == 2) {
          BinaryProtocol2 *bp2 = (BinaryProtocol2 *)data;
          bp2->version = ntohs(bp2->version);
          bp2->type = ntohs(bp2->type);
          bp2->timestamp = ntohl(bp2->timestamp);
          bp2->payload_size = ntohl(bp2->payload_size);
          auto payload = (uint8_t *)bp2->payload;
          on_incoming_audio_(std::make_unique<AudioStreamPacket>(
              AudioStreamPacket{.sample_rate = server_sample_rate_,
                                .frame_duration = server_frame_duration_,
                                .timestamp = bp2->timestamp,
                                .payload = std::vector<uint8_t>(
                                    payload, payload + bp2->payload_size)}));
        } else if (version_ == 3) {
          BinaryProtocol3 *bp3 = (BinaryProtocol3 *)data;
          bp3->type = bp3->type;
          bp3->payload_size = ntohs(bp3->payload_size);
          auto payload = (uint8_t *)bp3->payload;
          on_incoming_audio_(std::make_unique<AudioStreamPacket>(
              AudioStreamPacket{.sample_rate = server_sample_rate_,
                                .frame_duration = server_frame_duration_,
                                .timestamp = 0,
                                .payload = std::vector<uint8_t>(
                                    payload, payload + bp3->payload_size)}));
        } else {
          on_incoming_audio_(std::make_unique<AudioStreamPacket>(
              AudioStreamPacket{.sample_rate = server_sample_rate_,
                                .frame_duration = server_frame_duration_,
                                .timestamp = 0,
                                .payload = std::vector<uint8_t>(
                                    (uint8_t *)data, (uint8_t *)data + len)}));
        }
      }
    } else {
      // 处理 JSON 数据
      auto root = cJSON_Parse(data);
      auto type = cJSON_GetObjectItem(root, "type");
      if (cJSON_IsString(type)) {
        if (strcmp(type->valuestring, "hello") == 0) {
          ParseServerHello(root);
        } else {
          // v5.0 协议：msg_id 防重放检查
          auto msg_id = cJSON_GetObjectItem(root, "msg_id");
          if (cJSON_IsString(msg_id)) {
            std::string msg_id_str = msg_id->valuestring;
            if (IsDuplicateMsgId(msg_id_str)) {
              ESP_LOGW(TAG, "重复的 msg_id，忽略消息: %s", msg_id_str.c_str());
              cJSON_Delete(root);
              return;
            }
            // 添加到缓存
            AddMsgIdToCache(msg_id_str);
          }

          if (on_incoming_json_ != nullptr) {
            on_incoming_json_(root);
          }
        }
      } else {
        ESP_LOGE(TAG, "消息缺少 type 字段: %s", data);
      }
      cJSON_Delete(root);
    }
    last_incoming_time_ = std::chrono::steady_clock::now();
  });

  // 设置断开连接回调
  websocket_->OnDisconnected([this]() {
    ESP_LOGI(TAG, "WebSocket 已断开");
    if (on_audio_channel_closed_ != nullptr) {
      on_audio_channel_closed_();
    }
  });

  // 建立连接
  ESP_LOGI(TAG, "连接 WebSocket 服务器: %s, 版本: %d", url.c_str(), version_);
  if (!websocket_->Connect(url.c_str())) {
    ESP_LOGE(TAG, "连接 WebSocket 服务器失败, 错误码=%d",
             websocket_->GetLastError());
    SetError(Lang::Strings::SERVER_NOT_CONNECTED);
    return false;
  }

  // 发送客户端 Hello 消息
  auto message = GetHelloMessage();
  if (!SendText(message)) {
    return false;
  }

  // 等待服务器 Hello 响应
  EventBits_t bits = xEventGroupWaitBits(event_group_handle_,
                                         WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT,
                                         pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
  if (!(bits & WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT)) {
    ESP_LOGE(TAG, "等待服务器 Hello 超时");
    SetError(Lang::Strings::SERVER_TIMEOUT);
    return false;
  }

  // 触发通道打开回调
  if (on_audio_channel_opened_ != nullptr) {
    on_audio_channel_opened_();
  }

  return true;
}

// ============================================================================
// 握手消息
// ============================================================================

/**
 * @brief 构建客户端 Hello 消息
 *
 * 包含客户端信息：版本、特性、传输方式、音频参数等。
 */
std::string WebsocketProtocol::GetHelloMessage() {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "hello");
  cJSON_AddNumberToObject(root, "version", version_);

  // 特性声明
  cJSON *features = cJSON_CreateObject();
#if CONFIG_USE_SERVER_AEC
  cJSON_AddBoolToObject(features, "aec", true);
#endif
  cJSON_AddBoolToObject(features, "mcp", true);
  cJSON_AddItemToObject(root, "features", features);

  cJSON_AddStringToObject(root, "transport", "websocket");

  // 音频参数
  cJSON *audio_params = cJSON_CreateObject();
  cJSON_AddStringToObject(audio_params, "format", "opus");
  cJSON_AddNumberToObject(audio_params, "sample_rate", 16000);
  cJSON_AddNumberToObject(audio_params, "channels", 1);
  cJSON_AddNumberToObject(audio_params, "frame_duration",
                          OPUS_FRAME_DURATION_MS);
  cJSON_AddItemToObject(root, "audio_params", audio_params);

  auto json_str = cJSON_PrintUnformatted(root);
  std::string message(json_str);
  cJSON_free(json_str);
  cJSON_Delete(root);
  return message;
}

/**
 * @brief 解析服务器 Hello 消息
 *
 * 提取会话 ID 和服务器音频参数。
 */
void WebsocketProtocol::ParseServerHello(const cJSON *root) {
  // 验证传输方式
  auto transport = cJSON_GetObjectItem(root, "transport");
  if (transport == nullptr ||
      strcmp(transport->valuestring, "websocket") != 0) {
    ESP_LOGE(TAG, "不支持的传输方式: %s", transport->valuestring);
    return;
  }

  // 提取会话 ID
  auto session_id = cJSON_GetObjectItem(root, "session_id");
  if (cJSON_IsString(session_id)) {
    session_id_ = session_id->valuestring;
    ESP_LOGI(TAG, "会话 ID: %s", session_id_.c_str());
  }

  // 提取音频参数
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

  // 通知握手完成
  xEventGroupSetBits(event_group_handle_,
                     WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT);
}

// ============================================================================
// msg_id 防重放
// ============================================================================

bool WebsocketProtocol::IsDuplicateMsgId(const std::string &msg_id) {
  return msg_id_set_.find(msg_id) != msg_id_set_.end();
}

void WebsocketProtocol::AddMsgIdToCache(const std::string &msg_id) {
  // 缓存已满时，移除最旧的 msg_id
  if (msg_id_queue_.size() >= MSG_ID_CACHE_SIZE) {
    const std::string &oldest = msg_id_queue_.front();
    msg_id_set_.erase(oldest);
    msg_id_queue_.pop_front();
  }

  // 添加新的 msg_id
  msg_id_queue_.push_back(msg_id);
  msg_id_set_.insert(msg_id);
}

// ============================================================================
// v5.0 协议扩展方法
// ============================================================================

void WebsocketProtocol::SendAck(const std::string &msg_id, int code,
                                const std::string &msg) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "ack");
  cJSON_AddStringToObject(root, "seq_id", msg_id.c_str());
  cJSON_AddNumberToObject(root, "code", code);
  cJSON_AddStringToObject(root, "msg", msg.c_str());

  auto json_str = cJSON_PrintUnformatted(root);
  std::string message(json_str);
  cJSON_free(json_str);
  cJSON_Delete(root);

  ESP_LOGI(TAG, "发送 ACK: seq_id=%s, code=%d", msg_id.c_str(), code);
  SendText(message);
}

void WebsocketProtocol::SendEsp32Ack(const std::string &seq_id, int code,
                                     const std::string &msg) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "esp32_ack");
  cJSON_AddStringToObject(root, "seq_id", seq_id.c_str());
  cJSON_AddNumberToObject(root, "code", code);
  cJSON_AddStringToObject(root, "msg", msg.c_str());

  auto json_str = cJSON_PrintUnformatted(root);
  std::string message(json_str);
  cJSON_free(json_str);
  cJSON_Delete(root);

  ESP_LOGI(TAG, "发送 esp32_ack: seq_id=%s, code=%d", seq_id.c_str(), code);
  SendText(message);
}

void WebsocketProtocol::SendStatusReport(int battery, int lux, int lock_state,
                                         int light_state) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "status_report");
  cJSON_AddNumberToObject(root, "ts", (double)(esp_timer_get_time() / 1000));

  cJSON *data = cJSON_CreateObject();
  cJSON_AddNumberToObject(data, "bat", battery);
  cJSON_AddNumberToObject(data, "lux", lux);
  cJSON_AddNumberToObject(data, "lock", lock_state);
  cJSON_AddNumberToObject(data, "light", light_state);
  cJSON_AddItemToObject(root, "data", data);

  auto json_str = cJSON_PrintUnformatted(root);
  std::string message(json_str);
  cJSON_free(json_str);
  cJSON_Delete(root);

  ESP_LOGI(TAG, "发送状态上报: bat=%d%%, lux=%d, lock=%d, light=%d", battery,
           lux, lock_state, light_state);
  SendText(message);
}

void WebsocketProtocol::SendEventReport(const std::string &event, int param) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "event_report");
  cJSON_AddNumberToObject(root, "ts", (double)(esp_timer_get_time() / 1000));
  cJSON_AddStringToObject(root, "event", event.c_str());
  cJSON_AddNumberToObject(root, "param", param);

  auto json_str = cJSON_PrintUnformatted(root);
  std::string message(json_str);
  cJSON_free(json_str);
  cJSON_Delete(root);

  ESP_LOGI(TAG, "发送事件上报: event=%s, param=%d", event.c_str(), param);
  SendText(message);
}

void WebsocketProtocol::SendLogReport(const std::string &method,
                                      const std::string &status, int uid,
                                      int fail_count, int lock_time) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "log_report");
  cJSON_AddNumberToObject(root, "ts", (double)(esp_timer_get_time() / 1000));

  cJSON *data = cJSON_CreateObject();
  cJSON_AddStringToObject(data, "method", method.c_str());
  cJSON_AddStringToObject(data, "status", status.c_str());
  cJSON_AddNumberToObject(data, "uid", uid);
  cJSON_AddNumberToObject(data, "fail_count", fail_count);
  cJSON_AddNumberToObject(data, "lock_time", lock_time);
  cJSON_AddItemToObject(root, "data", data);

  auto json_str = cJSON_PrintUnformatted(root);
  std::string message(json_str);
  cJSON_free(json_str);
  cJSON_Delete(root);

  ESP_LOGI(TAG,
           "发送开锁日志: method=%s, status=%s, uid=%d, fail_count=%d, "
           "lock_time=%d",
           method.c_str(), status.c_str(), uid, fail_count, lock_time);
  SendText(message);
}

void WebsocketProtocol::SendDoorOpenedReport(const std::string &method,
                                             const std::string &source) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "door_opened_report");
  cJSON_AddNumberToObject(root, "ts", (double)(esp_timer_get_time() / 1000));

  cJSON *data = cJSON_CreateObject();
  cJSON_AddStringToObject(data, "method", method.c_str());
  cJSON_AddStringToObject(data, "source", source.c_str());
  cJSON_AddItemToObject(root, "data", data);

  auto json_str = cJSON_PrintUnformatted(root);
  std::string message(json_str);
  cJSON_free(json_str);
  cJSON_Delete(root);

  ESP_LOGI(TAG, "发送开门日志: method=%s, source=%s", method.c_str(),
           source.c_str());
  SendText(message);
}

void WebsocketProtocol::SendHeartbeat() {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "heartbeat");
  cJSON_AddNumberToObject(root, "ts", (double)(esp_timer_get_time() / 1000));
  // uptime: 系统运行时间（秒）
  cJSON_AddNumberToObject(root, "uptime",
                          (double)(esp_timer_get_time() / 1000000));

  auto json_str = cJSON_PrintUnformatted(root);
  std::string message(json_str);
  cJSON_free(json_str);
  cJSON_Delete(root);

  ESP_LOGD(TAG, "发送心跳");
  SendText(message);
}

void WebsocketProtocol::SendUserMgmtResult(const std::string &category,
                                           const std::string &command,
                                           bool result, int val,
                                           const std::string &msg) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "user_mgmt_result");
  cJSON_AddStringToObject(root, "category", category.c_str());
  cJSON_AddStringToObject(root, "command", command.c_str());
  cJSON_AddBoolToObject(root, "result", result);
  cJSON_AddNumberToObject(root, "val", val);
  cJSON_AddStringToObject(root, "msg", msg.c_str());

  auto json_str = cJSON_PrintUnformatted(root);
  std::string message(json_str);
  cJSON_free(json_str);
  cJSON_Delete(root);

  ESP_LOGI(TAG, "发送用户管理结果: category=%s, command=%s, result=%s, val=%d",
           category.c_str(), command.c_str(), result ? "true" : "false", val);
  SendText(message);
}

void WebsocketProtocol::SendPasswordReport(uint32_t password) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "password_report");
  cJSON_AddNumberToObject(root, "ts", (double)(esp_timer_get_time() / 1000));

  cJSON *data = cJSON_CreateObject();
  // 密码格式化为 6 位零填充字符串
  char password_str[7];
  snprintf(password_str, sizeof(password_str), "%06lu",
           (unsigned long)(password % 1000000));
  cJSON_AddStringToObject(data, "password", password_str);
  cJSON_AddItemToObject(root, "data", data);

  auto json_str = cJSON_PrintUnformatted(root);
  std::string message(json_str);
  cJSON_free(json_str);
  cJSON_Delete(root);

  ESP_LOGI(TAG, "发送密码上报: password=%s", password_str);
  SendText(message);
}
