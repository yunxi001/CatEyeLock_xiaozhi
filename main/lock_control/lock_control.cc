/**
 * @file lock_control.cc
 * @brief 锁控服务类实现
 * @version 2.0
 */

#include "lock_control.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char* TAG = "LockControl";

namespace xiaozhi {

// ============================================================================
// 构造与析构
// ============================================================================

LockControlService::LockControlService()
    : uart_port_(UART_NUM_0),
      running_(false),
      rx_task_handle_(nullptr),
      event_callback_(nullptr) {
}

LockControlService::~LockControlService() {
  Stop();
}

// ============================================================================
// 生命周期管理
// ============================================================================

bool LockControlService::Start(uart_port_t port, int tx_pin, int rx_pin) {
  if (running_) {
    ESP_LOGW(TAG, "服务已在运行");
    return true;
  }

  uart_port_ = port;

  // 配置 UART 参数：9600 波特率，8N1
  uart_config_t uart_config = {
      .baud_rate = 9600,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  // 安装 UART 驱动
  esp_err_t err = uart_driver_install(uart_port_, 256, 256, 0, NULL, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "安装 UART 驱动失败: %s", esp_err_to_name(err));
    return false;
  }

  // 配置 UART 参数
  err = uart_param_config(uart_port_, &uart_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "配置 UART 失败: %s", esp_err_to_name(err));
    return false;
  }

  // 设置 UART 引脚
  err = uart_set_pin(uart_port_, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "设置 UART 引脚失败: %s", esp_err_to_name(err));
    return false;
  }

  running_ = true;

  // 创建接收任务
  BaseType_t result = xTaskCreate(RxTask, "lock_rx", 2048, this, 5, &rx_task_handle_);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "创建接收任务失败");
    uart_driver_delete(uart_port_);
    running_ = false;
    return false;
  }

  ESP_LOGI(TAG, "UART 初始化完成，端口 %d (TX=%d, RX=%d)", uart_port_, tx_pin, rx_pin);
  return true;
}

void LockControlService::Stop() {
  if (!running_) {
    return;
  }

  running_ = false;

  // 等待接收任务退出
  if (rx_task_handle_ != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(100));
    rx_task_handle_ = nullptr;
  }

  // 删除 UART 驱动
  uart_driver_delete(uart_port_);
  ESP_LOGI(TAG, "服务已停止");
}

void LockControlService::SetEventCallback(EventCallback callback) {
  event_callback_ = callback;
}

// ============================================================================
// 接收任务
// ============================================================================

void LockControlService::RxTask(void* param) {
  LockControlService* service = static_cast<LockControlService*>(param);
  service->RxLoop();
  vTaskDelete(nullptr);
}

/**
 * @brief 接收循环
 * 
 * 从 UART 读取数据，按协议格式解析消息。
 * 使用状态机方式处理：先找帧头，再接收完整帧，最后校验解析。
 */
void LockControlService::RxLoop() {
  ESP_LOGI(TAG, "接收任务已启动");
  
  uint8_t buffer[LOCK_PROTOCOL_LENGTH];
  size_t pos = 0;
  bool found_header = false;
  int64_t last_byte_time = 0;
  const int64_t TIMEOUT_MS = 1000;  // 接收超时时间
  
  while (running_) {
    uint8_t byte;
    int len = uart_read_bytes(uart_port_, &byte, 1, pdMS_TO_TICKS(100));
    
    if (len <= 0) {
      // 检查接收超时
      if (found_header && pos > 0) {
        int64_t now = esp_timer_get_time() / 1000;
        if (now - last_byte_time > TIMEOUT_MS) {
          ESP_LOGW(TAG, "接收超时，丢弃不完整消息 (pos=%zu)", pos);
          pos = 0;
          found_header = false;
        }
      }
      continue;
    }
    
    last_byte_time = esp_timer_get_time() / 1000;
    
    // 查找帧头
    if (!found_header) {
      if (byte == LOCK_PROTOCOL_HEADER) {
        buffer[0] = byte;
        pos = 1;
        found_header = true;
      }
      continue;
    }
    
    // 缓冲区溢出保护
    if (pos >= LOCK_PROTOCOL_LENGTH) {
      ESP_LOGE(TAG, "缓冲区溢出: pos=%zu", pos);
      pos = 0;
      found_header = false;
      continue;
    }
    
    buffer[pos++] = byte;
    
    // 接收完整帧，进行解析
    if (pos >= LOCK_PROTOCOL_LENGTH) {
      LockMessage msg = LockProtocol::ParseMessage(buffer, LOCK_PROTOCOL_LENGTH);
      
      if (msg.valid) {
        ESP_LOGI(TAG, "收到消息: CAT=0x%02X, TYPE=0x%02X, D=[0x%02X,0x%02X,0x%02X]",
                 msg.category, msg.type, msg.data[0], msg.data[1], msg.data[2]);
        
        // 注意：新协议中 ESP32 不需要回复 ACK
        // STM32 的 CAT_RPT 上报不需要 ACK
        // 只有 STM32 收到 ESP32 的 CAT_CMD/CAT_USER 才需要回复 ACK
        
        // 触发事件回调
        if (event_callback_) {
          event_callback_(msg);
        }
      } else {
        ESP_LOGE(TAG, "收到无效消息（校验和错误）");
      }
      
      // 重置状态，准备接收下一帧
      pos = 0;
      found_header = false;
    }
  }
  
  ESP_LOGI(TAG, "接收任务已停止");
}

// ============================================================================
// 消息发送
// ============================================================================

/**
 * @brief 发送消息到 STM32
 * 
 * 带重试机制，最多重试 3 次。
 */
bool LockControlService::SendMessage(uint8_t cat, uint8_t type, const std::array<uint8_t, 3>& data) {
  if (!running_) {
    ESP_LOGW(TAG, "无法发送消息：服务未运行");
    return false;
  }
  
  std::vector<uint8_t> msg = LockProtocol::BuildMessage(cat, type, data);
  
  const int MAX_RETRIES = 3;
  for (int retry = 0; retry < MAX_RETRIES; retry++) {
    int len = uart_write_bytes(uart_port_, msg.data(), msg.size());
    
    if (len == static_cast<int>(msg.size())) {
      ESP_LOGD(TAG, "发送消息: CAT=0x%02X, TYPE=0x%02X, D=[0x%02X,0x%02X,0x%02X]",
               cat, type, data[0], data[1], data[2]);
      return true;
    }
    
    if (retry < MAX_RETRIES - 1) {
      ESP_LOGW(TAG, "发送失败 (尝试 %d/%d)，重试中...", retry + 1, MAX_RETRIES);
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  
  ESP_LOGE(TAG, "发送消息失败: CAT=0x%02X, TYPE=0x%02X", cat, type);
  return false;
}

// ============================================================================
// 控制命令实现
// ============================================================================

bool LockControlService::SendLock(LockMode mode, uint8_t hold_seconds) {
  return SendMessage(static_cast<uint8_t>(MsgCategory::CMD),
                     static_cast<uint8_t>(CmdType::CMD_LOCK),
                     {static_cast<uint8_t>(mode), hold_seconds, 0x00});
}

bool LockControlService::SendUnlock(uint8_t hold_seconds) {
  return SendLock(LockMode::UNLOCK, hold_seconds);
}

bool LockControlService::SendLockDoor() {
  return SendLock(LockMode::LOCK, 0);
}

bool LockControlService::SendOledIcon(OledIcon icon) {
  return SendMessage(static_cast<uint8_t>(MsgCategory::CMD),
                     static_cast<uint8_t>(CmdType::CMD_OLED),
                     {static_cast<uint8_t>(icon), 0x00, 0x00});
}

bool LockControlService::SendBeep(uint8_t count, BeepFreq freq) {
  return SendMessage(static_cast<uint8_t>(MsgCategory::CMD),
                     static_cast<uint8_t>(CmdType::CMD_BEEP),
                     {count, static_cast<uint8_t>(freq), 0x00});
}

bool LockControlService::SendSyncTime(uint8_t hour, uint8_t minute, uint8_t second) {
  return SendMessage(static_cast<uint8_t>(MsgCategory::CMD),
                     static_cast<uint8_t>(CmdType::CMD_SYNC_T),
                     {hour, minute, second});
}

bool LockControlService::SendLight(LightMode mode) {
  return SendMessage(static_cast<uint8_t>(MsgCategory::CMD),
                     static_cast<uint8_t>(CmdType::CMD_LIGHT),
                     {static_cast<uint8_t>(mode), 0x00, 0x00});
}

bool LockControlService::SendLightOn() {
  return SendLight(LightMode::LIGHT_ON);
}

bool LockControlService::SendLightOff() {
  return SendLight(LightMode::LIGHT_OFF);
}

bool LockControlService::SendLightAuto() {
  return SendLight(LightMode::LIGHT_AUTO);
}

bool LockControlService::QuerySensors() {
  return SendMessage(static_cast<uint8_t>(MsgCategory::CMD),
                     static_cast<uint8_t>(CmdType::Q_SENSORS),
                     {0x00, 0x00, 0x00});
}

bool LockControlService::QueryStatus() {
  return SendMessage(static_cast<uint8_t>(MsgCategory::CMD),
                     static_cast<uint8_t>(CmdType::Q_STATUS),
                     {0x00, 0x00, 0x00});
}

// ============================================================================
// 用户管理 - 指纹
// ============================================================================

bool LockControlService::FingerprintEnroll(uint8_t expected_id) {
  return SendMessage(static_cast<uint8_t>(MsgCategory::USER),
                     static_cast<uint8_t>(UserFpCmd::FP_CMD),
                     {static_cast<uint8_t>(FpSubCmd::FP_ENROLL), expected_id, 0x00});
}

bool LockControlService::FingerprintDelete(uint8_t id) {
  return SendMessage(static_cast<uint8_t>(MsgCategory::USER),
                     static_cast<uint8_t>(UserFpCmd::FP_CMD),
                     {static_cast<uint8_t>(FpSubCmd::FP_DELETE), id, 0x00});
}

bool LockControlService::FingerprintClear() {
  return SendMessage(static_cast<uint8_t>(MsgCategory::USER),
                     static_cast<uint8_t>(UserFpCmd::FP_CMD),
                     {static_cast<uint8_t>(FpSubCmd::FP_CLEAR), 0x00, 0x00});
}

bool LockControlService::FingerprintQueryCount() {
  return SendMessage(static_cast<uint8_t>(MsgCategory::USER),
                     static_cast<uint8_t>(UserFpCmd::FP_CMD),
                     {static_cast<uint8_t>(FpSubCmd::FP_COUNT), 0x00, 0x00});
}

// ============================================================================
// 用户管理 - NFC
// ============================================================================

bool LockControlService::NfcEnroll() {
  return SendMessage(static_cast<uint8_t>(MsgCategory::USER),
                     static_cast<uint8_t>(UserNfcCmd::NFC_CMD),
                     {static_cast<uint8_t>(FpSubCmd::FP_ENROLL), 0x00, 0x00});
}

bool LockControlService::NfcDelete(uint8_t id) {
  return SendMessage(static_cast<uint8_t>(MsgCategory::USER),
                     static_cast<uint8_t>(UserNfcCmd::NFC_CMD),
                     {static_cast<uint8_t>(FpSubCmd::FP_DELETE), id, 0x00});
}

bool LockControlService::NfcClear() {
  return SendMessage(static_cast<uint8_t>(MsgCategory::USER),
                     static_cast<uint8_t>(UserNfcCmd::NFC_CMD),
                     {static_cast<uint8_t>(FpSubCmd::FP_CLEAR), 0x00, 0x00});
}

bool LockControlService::NfcQueryCount() {
  return SendMessage(static_cast<uint8_t>(MsgCategory::USER),
                     static_cast<uint8_t>(UserNfcCmd::NFC_CMD),
                     {static_cast<uint8_t>(FpSubCmd::FP_COUNT), 0x00, 0x00});
}

// ============================================================================
// 用户管理 - 密码
// ============================================================================

bool LockControlService::SetPassword(uint32_t password) {
  if (password > 999999) {
    ESP_LOGE(TAG, "密码超出范围（最大 999999）");
    return false;
  }
  
  std::array<uint8_t, 3> encoded = LockProtocol::EncodePasswordHex(password);
  return SendMessage(static_cast<uint8_t>(MsgCategory::USER),
                     static_cast<uint8_t>(UserPwdCmd::PWD_SET),
                     encoded);
}

bool LockControlService::QueryPassword() {
  return SendMessage(static_cast<uint8_t>(MsgCategory::USER),
                     static_cast<uint8_t>(UserPwdCmd::PWD_QUERY),
                     {0x00, 0x00, 0x00});
}

bool LockControlService::SetTempPassword(uint32_t password, uint32_t expires) {
  if (password > 999999) {
    ESP_LOGE(TAG, "临时密码超出范围（最大 999999）");
    return false;
  }
  
  if (expires > 16777215) {
    ESP_LOGE(TAG, "有效期超出范围（最大 16777215 秒）");
    return false;
  }
  
  // 第1包：发送密码
  std::array<uint8_t, 3> pwd_encoded = LockProtocol::EncodePasswordHex(password);
  bool ok = SendMessage(static_cast<uint8_t>(MsgCategory::USER),
                        static_cast<uint8_t>(UserPwdCmd::TEMP_PWD_SET),
                        pwd_encoded);
  if (!ok) {
    ESP_LOGE(TAG, "发送临时密码失败");
    return false;
  }
  
  // 短暂延时，确保 STM32 处理完第1包
  vTaskDelay(pdMS_TO_TICKS(50));
  
  // 第2包：发送有效期（3 字节大端）
  std::array<uint8_t, 3> exp_encoded = {
      static_cast<uint8_t>((expires >> 16) & 0xFF),
      static_cast<uint8_t>((expires >> 8) & 0xFF),
      static_cast<uint8_t>(expires & 0xFF)
  };
  ok = SendMessage(static_cast<uint8_t>(MsgCategory::USER),
                   static_cast<uint8_t>(UserPwdCmd::TEMP_PWD_EXP),
                   exp_encoded);
  if (!ok) {
    ESP_LOGE(TAG, "发送临时密码有效期失败");
    return false;
  }
  
  ESP_LOGI(TAG, "临时密码已设置: %06lu, 有效期: %lu 秒", 
           (unsigned long)password, (unsigned long)expires);
  return true;
}

// ============================================================================
// 心跳
// ============================================================================

bool LockControlService::SendPing() {
  return SendMessage(static_cast<uint8_t>(MsgCategory::SYS),
                     static_cast<uint8_t>(SysType::SYS_PING),
                     {0x00, 0x00, 0x00});
}

}  // namespace xiaozhi
