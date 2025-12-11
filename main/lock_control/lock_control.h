/**
 * @file lock_control.h
 * @brief 锁控服务类定义
 * @version 2.0
 * 
 * 负责管理 ESP32 与 STM32 锁控 MCU 之间的 UART 通信。
 * 提供开锁、OLED 控制、蜂鸣器、传感器查询、用户管理等功能。
 * 
 * 使用示例：
 * @code
 * LockControlService lock_service;
 * lock_service.Start(UART_NUM_1, GPIO_TX, GPIO_RX);
 * lock_service.SetEventCallback([](const LockMessage& msg) {
 *     // 处理 STM32 上报的事件
 * });
 * lock_service.SendUnlock(30);  // 开锁 30 秒
 * @endcode
 */

#pragma once

#include <functional>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lock_protocol.h"

namespace xiaozhi {

/**
 * @brief 锁控服务类
 * 
 * 封装与 STM32 锁控 MCU 的 UART 通信，提供：
 * - 生命周期管理（启动/停止）
 * - 控制命令（开锁、OLED、蜂鸣器、补光灯等）
 * - 查询命令（传感器、状态）
 * - 用户管理（指纹、NFC、密码）
 * - 事件回调（接收 STM32 上报）
 */
class LockControlService {
 public:
  /** 事件回调函数类型 */
  using EventCallback = std::function<void(const LockMessage&)>;

  LockControlService();
  ~LockControlService();

  // =========================================================================
  // 生命周期管理
  // =========================================================================
  
  /**
   * @brief 启动锁控服务
   * @param port   UART 端口号
   * @param tx_pin TX 引脚
   * @param rx_pin RX 引脚
   * @return 启动成功返回 true
   */
  bool Start(uart_port_t port, int tx_pin, int rx_pin);
  
  /**
   * @brief 停止锁控服务
   */
  void Stop();
  
  /**
   * @brief 检查服务是否运行中
   */
  bool IsRunning() const { return running_; }

  // =========================================================================
  // 控制命令 (CAT = 0x02, ESP32 -> STM32)
  // =========================================================================
  
  /**
   * @brief 发送开锁/关锁命令
   * @param mode         锁控模式（UNLOCK/LOCK）
   * @param hold_seconds 保持时间（秒），0 表示使用默认值（3 分钟）
   * @return 发送成功返回 true
   */
  bool SendLock(LockMode mode, uint8_t hold_seconds = 0);
  
  /**
   * @brief 发送开锁命令（便捷方法）
   * @param hold_seconds 保持时间（秒）
   */
  bool SendUnlock(uint8_t hold_seconds = 0);
  
  /**
   * @brief 发送关锁命令（便捷方法）
   */
  bool SendLockDoor();
  
  /**
   * @brief 发送 OLED 显示图标命令
   * @param icon 图标类型
   */
  bool SendOledIcon(OledIcon icon);
  
  /**
   * @brief 发送蜂鸣器控制命令
   * @param count 鸣叫次数
   * @param freq  频率/模式
   */
  bool SendBeep(uint8_t count, BeepFreq freq);
  
  /**
   * @brief 发送时间同步命令
   * @param hour   小时（0-23）
   * @param minute 分钟（0-59）
   * @param second 秒（0-59）
   */
  bool SendSyncTime(uint8_t hour, uint8_t minute, uint8_t second);
  
  /**
   * @brief 发送补光灯控制命令
   * @param mode 控制模式（AUTO/ON/OFF）
   */
  bool SendLight(LightMode mode);
  
  /** 强制开灯（便捷方法） */
  bool SendLightOn();
  
  /** 强制关灯（便捷方法） */
  bool SendLightOff();
  
  /** 恢复自动控制（便捷方法） */
  bool SendLightAuto();

  // =========================================================================
  // 查询命令 (CAT = 0x02)
  // =========================================================================
  
  /**
   * @brief 查询传感器数据
   * 
   * STM32 将回复 RPT_ENV 消息，包含电量和光照信息。
   */
  bool QuerySensors();
  
  /**
   * @brief 查询设备状态
   * 
   * STM32 将回复 RPT_STATE 消息，包含锁状态和灯状态。
   */
  bool QueryStatus();

  // =========================================================================
  // 用户管理 - 指纹 (CAT = 0x03, TYPE = 0x10)
  // =========================================================================
  
  /**
   * @brief 开始录入指纹
   * @param expected_id 期望的指纹 ID，0 表示自动分配
   */
  bool FingerprintEnroll(uint8_t expected_id = 0);
  
  /**
   * @brief 删除指定 ID 的指纹
   * @param id 指纹 ID
   */
  bool FingerprintDelete(uint8_t id);
  
  /**
   * @brief 清空所有指纹
   */
  bool FingerprintClear();
  
  /**
   * @brief 查询指纹数量
   * 
   * STM32 将回复 FP_RESP 消息。
   */
  bool FingerprintQueryCount();

  // =========================================================================
  // 用户管理 - NFC (CAT = 0x03, TYPE = 0x20)
  // =========================================================================
  
  /** 开始录入 NFC 卡 */
  bool NfcEnroll();
  
  /** 删除指定 ID 的 NFC 卡 */
  bool NfcDelete(uint8_t id);
  
  /** 清空所有 NFC 卡 */
  bool NfcClear();
  
  /** 查询 NFC 卡数量 */
  bool NfcQueryCount();

  // =========================================================================
  // 用户管理 - 密码 (CAT = 0x03, TYPE = 0x30/0x31)
  // =========================================================================
  
  /**
   * @brief 设置开锁密码
   * @param password 6 位数字密码（0 ~ 999999）
   */
  bool SetPassword(uint32_t password);
  
  /**
   * @brief 查询当前密码
   * 
   * STM32 将回复 RPT_PWD 消息。
   */
  bool QueryPassword();

  // =========================================================================
  // 心跳 (CAT = 0x00, TYPE = 0xF0)
  // =========================================================================
  
  /**
   * @brief 发送心跳请求
   * 
   * STM32 将回复 SYS_PONG 消息。
   */
  bool SendPing();

  // =========================================================================
  // 事件回调
  // =========================================================================
  
  /**
   * @brief 设置事件回调函数
   * 
   * 当收到 STM32 上报的消息时，将调用此回调函数。
   * 
   * @param callback 回调函数
   */
  void SetEventCallback(EventCallback callback);

 private:
  uart_port_t uart_port_;           ///< UART 端口号
  bool running_;                    ///< 服务运行状态
  TaskHandle_t rx_task_handle_;     ///< 接收任务句柄
  EventCallback event_callback_;    ///< 事件回调函数

  /**
   * @brief 发送消息到 STM32
   * @param cat  消息类别
   * @param type 消息类型
   * @param data 数据域
   * @return 发送成功返回 true
   */
  bool SendMessage(uint8_t cat, uint8_t type, const std::array<uint8_t, 3>& data);
  
  /** 接收任务入口（静态） */
  static void RxTask(void* param);
  
  /** 接收循环 */
  void RxLoop();
};

}  // namespace xiaozhi
