#pragma once

#include <functional>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lock_protocol.h"

namespace xiaozhi {

class LockControlService {
 public:
  using EventCallback = std::function<void(const LockMessage&)>;

  LockControlService();
  ~LockControlService();

  // Lifecycle
  bool Start(uart_port_t port, int tx_pin, int rx_pin);
  void Stop();
  bool IsRunning() const { return running_; }

  // Control commands (CAT=0x02)
  bool SendUnlock();
  bool SendAlarm(uint8_t level);
  bool SendAlarmOff();
  bool SendTempCode(const char* password);
  bool SendLedControl(uint8_t mode, uint8_t color, uint8_t brightness);

  // Query commands (CAT=0x04)
  bool QueryLockState();
  bool QueryDoorState();
  bool QueryBattery();

  // ACK (CAT=0x0F)
  bool SendAck(uint8_t orig_cat, uint8_t orig_type, bool success);

  // Event callback
  void SetEventCallback(EventCallback callback);

 private:
  uart_port_t uart_port_;
  bool running_;
  TaskHandle_t rx_task_handle_;
  EventCallback event_callback_;

  bool SendMessage(uint8_t cat, uint8_t type, const std::array<uint8_t, 3>& data);
  static void RxTask(void* param);
  void RxLoop();
};

}  // namespace xiaozhi
