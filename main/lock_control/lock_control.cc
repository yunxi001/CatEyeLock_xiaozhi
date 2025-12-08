#include "lock_control.h"

#include "esp_log.h"
#include "driver/gpio.h"

static const char* TAG = "LockControl";

namespace xiaozhi {

LockControlService::LockControlService()
    : uart_port_(UART_NUM_0), running_(false), rx_task_handle_(nullptr), event_callback_(nullptr) {}

LockControlService::~LockControlService() {
  Stop();
}

bool LockControlService::Start(uart_port_t port, int tx_pin, int rx_pin) {
  if (running_) {
    ESP_LOGW(TAG, "Service already running");
    return true;
  }

  uart_port_ = port;

  // Configure UART parameters
  uart_config_t uart_config = {
      .baud_rate = 9600,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 0,
      .source_clk = UART_SCLK_DEFAULT,
  };

  // Configure UART
  esp_err_t err = uart_param_config(uart_port_, &uart_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(err));
    return false;
  }

  // Set UART pins
  err = uart_set_pin(uart_port_, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
    return false;
  }

  // Install UART driver (RX buffer = 256, TX buffer = 256)
  err = uart_driver_install(uart_port_, 256, 256, 0, nullptr, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
    return false;
  }

  running_ = true;

  // Create RX task
  BaseType_t result = xTaskCreate(RxTask, "lock_rx", 2048, this, 5, &rx_task_handle_);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "Failed to create RX task");
    uart_driver_delete(uart_port_);
    running_ = false;
    return false;
  }

  ESP_LOGI(TAG, "UART initialized on port %d (TX=%d, RX=%d)", uart_port_, tx_pin, rx_pin);
  return true;
}

void LockControlService::Stop() {
  if (!running_) {
    return;
  }

  running_ = false;

  // Wait for RX task to finish
  if (rx_task_handle_ != nullptr) {
    // Give task time to exit
    vTaskDelay(pdMS_TO_TICKS(100));
    rx_task_handle_ = nullptr;
  }

  // Uninstall UART driver
  uart_driver_delete(uart_port_);

  ESP_LOGI(TAG, "Service stopped");
}

void LockControlService::SetEventCallback(EventCallback callback) {
  event_callback_ = callback;
}

void LockControlService::RxTask(void* param) {
  LockControlService* service = static_cast<LockControlService*>(param);
  service->RxLoop();
  vTaskDelete(nullptr);
}

void LockControlService::RxLoop() {
  ESP_LOGI(TAG, "RX task started");
  
  uint8_t buffer[LOCK_PROTOCOL_LENGTH];
  size_t pos = 0;
  bool found_header = false;
  int64_t last_byte_time = 0;
  const int64_t TIMEOUT_MS = 1000; // 1 second timeout
  
  while (running_) {
    uint8_t byte;
    int len = uart_read_bytes(uart_port_, &byte, 1, pdMS_TO_TICKS(100));
    
    if (len <= 0) {
      // Check for timeout if we're in the middle of receiving a message
      if (found_header && pos > 0) {
        int64_t now = esp_timer_get_time() / 1000; // Convert to ms
        if (now - last_byte_time > TIMEOUT_MS) {
          ESP_LOGE(TAG, "RX buffer overflow: timeout waiting for complete message (pos=%zu)", pos);
          // Discard incomplete message and resynchronize
          pos = 0;
          found_header = false;
        }
      }
      continue;
    }
    
    last_byte_time = esp_timer_get_time() / 1000;
    
    // Look for header byte
    if (!found_header) {
      if (byte == LOCK_PROTOCOL_HEADER) {
        buffer[0] = byte;
        pos = 1;
        found_header = true;
      }
      continue;
    }
    
    // Check for buffer overflow
    if (pos >= LOCK_PROTOCOL_LENGTH) {
      ESP_LOGE(TAG, "RX buffer overflow: pos=%zu", pos);
      // Discard and resynchronize
      pos = 0;
      found_header = false;
      continue;
    }
    
    // Collect message bytes
    buffer[pos++] = byte;
    
    // Check if we have a complete message
    if (pos >= LOCK_PROTOCOL_LENGTH) {
      // Parse message
      LockMessage msg = LockProtocol::ParseMessage(buffer, LOCK_PROTOCOL_LENGTH);
      
      if (msg.valid) {
        ESP_LOGD(TAG, "Received valid message: CAT=0x%02X, TYPE=0x%02X", msg.category, msg.type);
        
        // Send ACK for non-ACK messages
        if (!msg.IsAck()) {
          SendAck(msg.category, msg.type, true);
        }
        
        // Invoke callback if set
        if (event_callback_) {
          event_callback_(msg);
        }
      } else {
        ESP_LOGE(TAG, "Received invalid message (checksum mismatch)");
      }
      
      // Reset for next message
      pos = 0;
      found_header = false;
    }
  }
  
  ESP_LOGI(TAG, "RX task stopped");
}

bool LockControlService::SendMessage(uint8_t cat, uint8_t type, const std::array<uint8_t, 3>& data) {
  if (!running_) {
    ESP_LOGW(TAG, "Cannot send message: service not running");
    return false;
  }
  
  // Build message
  std::vector<uint8_t> msg = LockProtocol::BuildMessage(cat, type, data);
  
  // Retry up to 3 times
  const int MAX_RETRIES = 3;
  for (int retry = 0; retry < MAX_RETRIES; retry++) {
    // Send via UART
    int len = uart_write_bytes(uart_port_, msg.data(), msg.size());
    
    if (len == msg.size()) {
      ESP_LOGD(TAG, "Sent message: CAT=0x%02X, TYPE=0x%02X, DATA=[0x%02X, 0x%02X, 0x%02X]",
               cat, type, data[0], data[1], data[2]);
      return true;
    }
    
    // Failed, retry after delay
    if (retry < MAX_RETRIES - 1) {
      ESP_LOGW(TAG, "Failed to send message (attempt %d/%d), retrying...", retry + 1, MAX_RETRIES);
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  
  ESP_LOGE(TAG, "Failed to send message after %d attempts: CAT=0x%02X, TYPE=0x%02X", 
           MAX_RETRIES, cat, type);
  return false;
}

bool LockControlService::SendUnlock() {
  return SendMessage(static_cast<uint8_t>(MsgCategory::CONTROL),
                     static_cast<uint8_t>(ControlType::UNLOCK),
                     {0, 0, 0});
}

bool LockControlService::SendAlarm(uint8_t level) {
  return SendMessage(static_cast<uint8_t>(MsgCategory::CONTROL),
                     static_cast<uint8_t>(ControlType::ALARM_ON),
                     {level, 0, 0});
}

bool LockControlService::SendAlarmOff() {
  return SendMessage(static_cast<uint8_t>(MsgCategory::CONTROL),
                     static_cast<uint8_t>(ControlType::ALARM_OFF),
                     {0, 0, 0});
}

bool LockControlService::SendTempCode(const char* password) {
  // Encode password using BCD format
  std::array<uint8_t, 3> encoded = LockProtocol::EncodePassword(password);
  
  // Check if encoding was successful (all zeros indicates error)
  if (encoded[0] == 0 && encoded[1] == 0 && encoded[2] == 0) {
    ESP_LOGE(TAG, "Failed to encode password: invalid format");
    return false;
  }
  
  return SendMessage(static_cast<uint8_t>(MsgCategory::CONTROL),
                     static_cast<uint8_t>(ControlType::SET_TEMP_CODE),
                     encoded);
}

bool LockControlService::SendLedControl(uint8_t mode, uint8_t color, uint8_t brightness) {
  return SendMessage(static_cast<uint8_t>(MsgCategory::CONTROL),
                     static_cast<uint8_t>(ControlType::LED_CTRL),
                     {mode, color, brightness});
}

bool LockControlService::QueryLockState() {
  return SendMessage(static_cast<uint8_t>(MsgCategory::QUERY), 0x01, {0, 0, 0});
}

bool LockControlService::QueryDoorState() {
  return SendMessage(static_cast<uint8_t>(MsgCategory::QUERY), 0x02, {0, 0, 0});
}

bool LockControlService::QueryBattery() {
  return SendMessage(static_cast<uint8_t>(MsgCategory::QUERY), 0x03, {0, 0, 0});
}

bool LockControlService::SendAck(uint8_t orig_cat, uint8_t orig_type, bool success) {
  std::vector<uint8_t> ack_msg = LockProtocol::BuildAck(orig_cat, orig_type, success);
  
  int len = uart_write_bytes(uart_port_, ack_msg.data(), ack_msg.size());
  
  if (len != ack_msg.size()) {
    ESP_LOGE(TAG, "Failed to send ACK");
    return false;
  }
  
  ESP_LOGD(TAG, "Sent ACK for CAT=0x%02X, TYPE=0x%02X, success=%d", orig_cat, orig_type, success);
  return true;
}

}  // namespace xiaozhi
