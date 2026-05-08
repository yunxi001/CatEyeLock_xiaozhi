/**
 * @file protocol.cc
 * @brief 通信协议抽象基类实现
 *
 * 提供 Protocol 基类的默认实现，包括：
 * - 回调函数注册
 * - 通用消息发送方法
 * - v5.0 协议扩展方法的默认实现（子类需覆盖）
 */

#include "protocol.h"

#include <esp_log.h>

#define TAG "Protocol"

// ============================================================================
// 回调注册
// ============================================================================

void Protocol::OnIncomingJson(std::function<void(const cJSON *root)> callback) {
  on_incoming_json_ = callback;
}

void Protocol::OnIncomingAudio(
    std::function<void(std::unique_ptr<AudioStreamPacket> packet)> callback) {
  on_incoming_audio_ = callback;
}

void Protocol::OnAudioChannelOpened(std::function<void()> callback) {
  on_audio_channel_opened_ = callback;
}

void Protocol::OnAudioChannelClosed(std::function<void()> callback) {
  on_audio_channel_closed_ = callback;
}

void Protocol::OnNetworkError(
    std::function<void(const std::string &message)> callback) {
  on_network_error_ = callback;
}

void Protocol::OnConnected(std::function<void()> callback) {
  on_connected_ = callback;
}

void Protocol::OnDisconnected(std::function<void()> callback) {
  on_disconnected_ = callback;
}

// ============================================================================
// 错误处理
// ============================================================================

void Protocol::SetError(const std::string &message) {
  error_occurred_ = true;
  if (on_network_error_ != nullptr) {
    on_network_error_(message);
  }
}

// ============================================================================
// 语音交互消息
// ============================================================================

void Protocol::SendAbortSpeaking(AbortReason reason) {
  std::string message =
      "{\"session_id\":\"" + session_id_ + "\",\"type\":\"abort\"";
  if (reason == kAbortReasonWakeWordDetected) {
    message += ",\"reason\":\"wake_word_detected\"";
  }
  message += "}";
  SendText(message);
}

void Protocol::SendWakeWordDetected(const std::string &wake_word) {
  std::string json = "{\"session_id\":\"" + session_id_ +
                     "\",\"type\":\"listen\",\"state\":\"detect\",\"text\":\"" +
                     wake_word + "\"}";
  SendText(json);
}

void Protocol::SendStartListening(ListeningMode mode) {
  std::string message = "{\"session_id\":\"" + session_id_ + "\"";
  message += ",\"type\":\"listen\",\"state\":\"start\"";
  if (mode == kListeningModeRealtime) {
    message += ",\"mode\":\"realtime\"";
  } else if (mode == kListeningModeAutoStop) {
    message += ",\"mode\":\"auto\"";
  } else {
    message += ",\"mode\":\"manual\"";
  }
  message += "}";
  SendText(message);
}

void Protocol::SendStopListening() {
  std::string message = "{\"session_id\":\"" + session_id_ +
                        "\",\"type\":\"listen\",\"state\":\"stop\"}";
  SendText(message);
}

void Protocol::SendMcpMessage(const std::string &payload) {
  std::string message = "{\"session_id\":\"" + session_id_ +
                        "\",\"type\":\"mcp\",\"payload\":" + payload + "}";
  SendText(message);
}

// ============================================================================
// 超时检测
// ============================================================================

bool Protocol::IsTimeout() const {
  const int kTimeoutSeconds = 120;
  auto now = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::seconds>(
      now - last_incoming_time_);
  bool timeout = duration.count() > kTimeoutSeconds;
  if (timeout) {
    ESP_LOGE(TAG, "Channel timeout %d seconds", (int)duration.count());
  }
  return timeout;
}

// ============================================================================
// v5.0 协议扩展方法默认实现
// ============================================================================

void Protocol::SendAck(int code, const std::string &msg) {
  ESP_LOGW(TAG, "SendAck not implemented in base class");
}

void Protocol::SendStatusReport(int battery, int lux, int lock_state,
                                int light_state) {
  ESP_LOGW(TAG, "SendStatusReport not implemented in base class");
}

void Protocol::SendEventReport(const std::string &event, int param) {
  ESP_LOGW(TAG, "SendEventReport not implemented in base class");
}

void Protocol::SendLogReport(const std::string &method,
                             const std::string &status, int uid, int fail_count,
                             int lock_time) {
  ESP_LOGW(TAG, "SendLogReport not implemented in base class");
}

void Protocol::SendDoorOpenedReport(const std::string &method,
                                    const std::string &source) {
  ESP_LOGW(TAG, "SendDoorOpenedReport not implemented in base class");
}

void Protocol::SendUserMgmtResult(const std::string &category,
                                  const std::string &command, bool result,
                                  int val, const std::string &msg) {
  ESP_LOGW(TAG, "SendUserMgmtResult not implemented in base class");
}
