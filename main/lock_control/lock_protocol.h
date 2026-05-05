/**
 * @file lock_protocol.h
 * @brief 锁控模块 UART 通信协议定义
 * @version 2.0
 *
 * 本文件定义了 ESP32 与 STM32 锁控 MCU 之间的 UART 通信协议。
 * 协议采用 7 字节固定长度格式，波特率 9600。
 *
 * 协议帧格式：
 * +------+------+------+------+------+------+------+
 * | 帧头 | 类别 | 类型 | D0   | D1   | D2   | 校验 |
 * +------+------+------+------+------+------+------+
 * | 0xAA | CAT  | TYPE | DATA[0-2]          | CHK  |
 * +------+------+------+------+------+------+------+
 *
 * 校验和计算：(CAT + TYPE + D0 + D1 + D2) & 0xFF
 */

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace xiaozhi {

// ============================================================================
// 协议常量
// ============================================================================

/** 协议帧头标识 */
constexpr uint8_t LOCK_PROTOCOL_HEADER = 0xAA;

/** 协议帧总长度（字节） */
constexpr size_t LOCK_PROTOCOL_LENGTH = 7;

/** 数据域长度（字节） */
constexpr size_t LOCK_PROTOCOL_DATA_LEN = 3;

/** 协议空值（未使用字段填充），用于 v2.4+ 协议兼容 */
constexpr uint8_t LOCK_PROTOCOL_EMPTY = 0xFF;

// ============================================================================
// 消息类别枚举 (CAT 字段)
// ============================================================================

/**
 * @brief 消息类别定义
 *
 * 用于区分不同类型的消息：
 * - SYS:  系统握手（ACK、心跳）
 * - RPT:  STM32 主动上报（事件、状态）
 * - CMD:  ESP32 下发控制/查询命令
 * - USER: 用户管理（指纹、NFC、密码）
 */
enum class MsgCategory : uint8_t {
  SYS = 0x00,  ///< 系统握手
  RPT = 0x01,  ///< 状态上报
  CMD = 0x02,  ///< 控制命令
  USER = 0x03, ///< 用户管理
};

// ============================================================================
// 系统握手类型 (CAT = 0x00)
// ============================================================================

/**
 * @brief 系统消息类型
 */
enum class SysType : uint8_t {
  ACK_ERR = 0x00,  ///< 错误应答，D0=原TYPE，D1=错误码
  ACK_OK = 0x01,   ///< 成功应答，D0=原TYPE
  SYS_PING = 0xF0, ///< 心跳请求（ESP32 -> STM32）
  SYS_PONG = 0xF1, ///< 心跳响应（STM32 -> ESP32）
};

/**
 * @brief ACK 错误码定义
 */
enum class AckError : uint8_t {
  ERR_BUSY = 0x01,      ///< 设备忙
  ERR_UNSUPPORT = 0x02, ///< 不支持的命令
  ERR_PARAM = 0x03,     ///< 参数错误
  ERR_FP_FULL = 0x04,   ///< 指纹库已满
  ERR_NFC_FULL = 0x05,  ///< NFC 库已满
  ERR_HARDWARE = 0x06,  ///< 硬件错误
  ERR_TIMEOUT = 0xFF,   ///< 操作超时
};

// ============================================================================
// 控制/查询命令类型 (CAT = 0x02, ESP32 -> STM32)
// ============================================================================

/**
 * @brief 控制命令类型
 */
enum class CmdType : uint8_t {
  CMD_LOCK = 0x10,   ///< 开锁/关锁控制
  CMD_OLED = 0x11,   ///< OLED 显示控制
  CMD_BEEP = 0x12,   ///< 蜂鸣器控制
  CMD_SYNC_T = 0x13, ///< 时间同步
  CMD_LIGHT = 0x14,  ///< 补光灯控制
  Q_SENSORS = 0x80,  ///< 查询传感器（电量、光照）
  Q_STATUS = 0x81,   ///< 查询状态（锁状态、灯状态）
};

/**
 * @brief 锁控模式
 */
enum class LockMode : uint8_t {
  UNLOCK = 0x01, ///< 开锁
  LOCK = 0x02,   ///< 关锁
};

/**
 * @brief OLED 显示图标
 */
enum class OledIcon : uint8_t {
  ICON_CLEAR = 0x00,       ///< 清屏
  ICON_WIFI_OK = 0x01,     ///< WiFi 已连接
  ICON_CLOUD_OK = 0x02,    ///< 云端已连接
  ICON_RECOGNIZING = 0x03, ///< 识别中
  ICON_SUCCESS = 0x04,     ///< 识别成功
  ICON_FAILED = 0x05,      ///< 识别失败
};

/**
 * @brief 蜂鸣器频率/模式
 */
enum class BeepFreq : uint8_t {
  BEEP_SHORT = 0x01, ///< 短鸣
  BEEP_LONG = 0x02,  ///< 长鸣
  BEEP_ALARM = 0x03, ///< 报警音
};

/**
 * @brief 补光灯控制模式
 */
enum class LightMode : uint8_t {
  LIGHT_AUTO = 0x00, ///< 自动模式（根据光照自动控制）
  LIGHT_ON = 0x01,   ///< 强制开灯
  LIGHT_OFF = 0x02,  ///< 强制关灯
};

// ============================================================================
// 上报类型 (CAT = 0x01, STM32 -> ESP32)
// ============================================================================

/**
 * @brief 上报消息类型
 */
enum class RptType : uint8_t {
  RPT_EVENT = 0xA0,       ///< 事件上报（门铃、PIR、防拆等）
  RPT_UNLOCK = 0xA1,      ///< 开锁事件上报
  RPT_DOOR_OPENED = 0xA2, ///< 【新增】开门日志（v2.7+）
  RPT_ENV = 0xB0,         ///< 环境数据上报（电量、光照）
  RPT_STATE = 0xB1,       ///< 状态上报（锁状态、灯状态）
  RPT_PWD = 0xC0,         ///< 密码查询响应
};

/**
 * @brief 事件 ID 定义
 */
enum class EventId : uint8_t {
  EVT_DOORBELL = 0x01,    ///< 门铃按下
  EVT_PIR = 0x02,         ///< PIR 人体检测
  EVT_TAMPER = 0x03,      ///< 防拆报警
  EVT_DOOR_OPEN = 0x04,   ///< 门已打开
  EVT_LOW_BATTERY = 0x05, ///< 低电量警告
  EVT_LOCK_STATUS = 0x06, ///< 【新增】关门/上锁状态（v2.7+）
};

/**
 * @brief 锁状态码枚举（v2.7+）
 *
 * 用于 EVT_LOCK_STATUS 事件的 D1 字段
 *
 * @note STM32 v2.8+ 已移除霍尔传感器，不再发送 BOLT_ALARM 事件
 *       保留此枚举值以兼容旧版 STM32 (v2.7-)
 */
enum class LockStatusCode : uint8_t {
  DOOR_CLOSED = 0x00,  ///< 门关闭
  LOCK_SUCCESS = 0x01, ///< 上锁成功
  BOLT_ALARM = 0x02,   ///< 锁舌未到位报警 (仅旧版 STM32 v2.7-)
};

/**
 * @brief 开门来源枚举（v2.7+）
 *
 * 用于 RPT_DOOR_OPENED 消息的 D1 字段
 */
enum class DoorSource : uint8_t {
  OUTSIDE = 0x00, ///< 室外开门
  INSIDE = 0x01,  ///< 室内开门
  UNKNOWN = 0xFF, ///< 未知
};

/**
 * @brief 开锁方式
 *
 * 与服务器协议 v5.0 对应的 method 字段：
 * - finger: 指纹开锁
 * - nfc: NFC 开锁
 * - pwd: 密码开锁
 * - remote: 远程开锁(App)
 * - key: 机械钥匙
 * - temp_pwd: 临时密码开锁
 * - face: 人脸开锁
 */
enum class UnlockMethod : uint8_t {
  UNLOCK_FINGERPRINT = 0x01, ///< 指纹开锁 (finger)
  UNLOCK_NFC = 0x02,         ///< NFC 开锁 (nfc)
  UNLOCK_PASSWORD = 0x03,    ///< 密码开锁 (pwd)
  UNLOCK_REMOTE = 0x04,      ///< 远程开锁 (remote)
  UNLOCK_KEY = 0x05,         ///< 钥匙开锁 (key)
  UNLOCK_TEMP_PWD = 0x06,    ///< 临时密码开锁 (temp_pwd)
  UNLOCK_FACE = 0x07,        ///< 人脸开锁 (face)
};

// ============================================================================
// 用户管理类型 (CAT = 0x03)
// ============================================================================

/**
 * @brief 指纹管理命令
 */
enum class UserFpCmd : uint8_t {
  FP_CMD = 0x10,  ///< 指纹命令（ESP32 -> STM32）
  FP_RESP = 0x11, ///< 指纹响应（STM32 -> ESP32）
};

/**
 * @brief NFC 管理命令
 */
enum class UserNfcCmd : uint8_t {
  NFC_CMD = 0x20,  ///< NFC 命令（ESP32 -> STM32）
  NFC_RESP = 0x21, ///< NFC 响应（STM32 -> ESP32）
};

/**
 * @brief 密码管理命令
 */
enum class UserPwdCmd : uint8_t {
  PWD_SET = 0x30,      ///< 设置全局密码
  PWD_QUERY = 0x31,    ///< 查询全局密码
  TEMP_PWD_SET = 0x32, ///< 设置临时密码（第1包：密码）
  TEMP_PWD_EXP = 0x33, ///< 设置临时密码（第2包：有效期秒数）
};

/**
 * @brief 指纹/NFC 子命令
 */
enum class FpSubCmd : uint8_t {
  FP_ENROLL = 0x01, ///< 录入
  FP_DELETE = 0x02, ///< 删除指定 ID
  FP_CLEAR = 0x03,  ///< 清空全部
  FP_COUNT = 0x04,  ///< 查询数量
};

/**
 * @brief 指纹录入响应状态
 */
enum class FpRespStatus : uint8_t {
  FP_PRESS_FINGER = 0x01,   ///< 请按压手指
  FP_LIFT_FINGER = 0x02,    ///< 请抬起手指
  FP_SUCCESS = 0x03,        ///< 录入成功
  FP_FAILED = 0x04,         ///< 录入失败
  FP_COUNT_RESP = 0x05,     ///< 数量查询响应
  FP_ALREADY_EXISTS = 0x06, ///< 【新增】已存在（v2.7+）
  FP_ID_OCCUPIED = 0x07,    ///< 【新增】ID 被占用（v2.7+）
};

/**
 * @brief NFC 子命令
 */
enum class NfcSubCmd : uint8_t {
  NFC_ENROLL = 0x01, ///< 录入
  NFC_DELETE = 0x02, ///< 删除指定 ID
  NFC_CLEAR = 0x03,  ///< 清空全部
  NFC_COUNT = 0x04,  ///< 查询数量
};

/**
 * @brief NFC 录入响应状态
 */
enum class NfcRespStatus : uint8_t {
  NFC_TAP = 0x01,            ///< 请刷卡（录入中）
  NFC_REMOVE_CARD = 0x02,    ///< 请移开卡片
  NFC_SUCCESS = 0x03,        ///< 录入成功
  NFC_FAILED = 0x04,         ///< 录入失败
  NFC_COUNT_RESP = 0x05,     ///< 数量查询响应
  NFC_ALREADY_EXISTS = 0x06, ///< 已存在（v2.7+）
  NFC_ID_OCCUPIED = 0x07,    ///< ID 被占用（v2.7+）
};

/**
 * @brief 开锁结果码
 *
 * 用于 RPT_UNLOCK 消息的 D2 字段
 */
enum class UnlockResult : uint8_t {
  UNLOCK_SUCCESS = 0x00, ///< 开锁成功
  UNLOCK_FAIL_1 = 0x01,  ///< 失败 1 次
  UNLOCK_FAIL_2 = 0x02,  ///< 失败 2 次
  UNLOCK_FAIL_3 = 0x03,  ///< 失败 3 次
  UNLOCK_FAIL_4 = 0x04,  ///< 失败 4 次
  UNLOCK_FAIL_5 = 0x05,  ///< 失败 5 次（触发锁定）
  UNLOCK_LOCKED = 0x06,  ///< 已锁定（D1=剩余锁定时间，单位分钟）
};

/** 最大连续失败次数 */
constexpr uint8_t MAX_AUTH_FAIL_COUNT = 5;

// ============================================================================
// 消息结构体
// ============================================================================

/**
 * @brief 锁控消息结构体
 *
 * 用于存储解析后的协议消息。
 */
struct LockMessage {
  uint8_t category;            ///< 消息类别
  uint8_t type;                ///< 消息类型
  std::array<uint8_t, 3> data; ///< 数据域
  bool valid;                  ///< 消息是否有效

  /** 获取消息类别枚举 */
  MsgCategory GetCategory() const { return static_cast<MsgCategory>(category); }

  /** 判断是否为系统消息 */
  bool IsSys() const {
    return category == static_cast<uint8_t>(MsgCategory::SYS);
  }

  /** 判断是否为上报消息 */
  bool IsRpt() const {
    return category == static_cast<uint8_t>(MsgCategory::RPT);
  }

  /** 判断是否为控制命令 */
  bool IsCmd() const {
    return category == static_cast<uint8_t>(MsgCategory::CMD);
  }

  /** 判断是否为用户管理消息 */
  bool IsUser() const {
    return category == static_cast<uint8_t>(MsgCategory::USER);
  }

  /** 判断是否为成功应答 */
  bool IsAckOk() const {
    return IsSys() && type == static_cast<uint8_t>(SysType::ACK_OK);
  }

  /** 判断是否为错误应答 */
  bool IsAckErr() const {
    return IsSys() && type == static_cast<uint8_t>(SysType::ACK_ERR);
  }
};

// ============================================================================
// 协议处理类
// ============================================================================

/**
 * @brief 锁控协议处理类
 *
 * 提供协议消息的构建、解析、校验等功能。
 */
class LockProtocol {
public:
  /**
   * @brief 构建协议消息
   * @param cat  消息类别
   * @param type 消息类型
   * @param data 数据域（3 字节）
   * @return 完整的 7 字节协议帧
   */
  static std::vector<uint8_t>
  BuildMessage(uint8_t cat, uint8_t type,
               const std::array<uint8_t, 3> &data = {0, 0, 0});

  /**
   * @brief 解析协议消息
   * @param data 原始数据指针
   * @param len  数据长度
   * @return 解析后的消息结构体，valid 字段表示是否解析成功
   */
  static LockMessage ParseMessage(const uint8_t *data, size_t len);

  /**
   * @brief 计算校验和
   * @param cat  消息类别
   * @param type 消息类型
   * @param data 数据域指针
   * @return 校验和值
   */
  static uint8_t CalculateChecksum(uint8_t cat, uint8_t type,
                                   const uint8_t *data);

  /**
   * @brief 构建成功应答消息
   * @param orig_type 原始命令类型
   * @return ACK_OK 消息帧
   */
  static std::vector<uint8_t> BuildAckOk(uint8_t orig_type);

  /**
   * @brief 构建错误应答消息
   * @param orig_type 原始命令类型
   * @param error     错误码
   * @return ACK_ERR 消息帧
   */
  static std::vector<uint8_t> BuildAckErr(uint8_t orig_type, AckError error);

  /**
   * @brief 编码密码（Hex 整数格式）
   *
   * 将 6 位数字密码编码为 3 字节大端格式。
   * 例如：123456 = 0x01E240 -> {0x01, 0xE2, 0x40}
   *
   * @param password 密码值（0 ~ 999999）
   * @return 编码后的 3 字节数组
   */
  static std::array<uint8_t, 3> EncodePasswordHex(uint32_t password);

  /**
   * @brief 解码密码（Hex 整数格式）
   * @param data 3 字节编码数据
   * @return 解码后的密码值
   */
  static uint32_t DecodePasswordHex(const std::array<uint8_t, 3> &data);

  /**
   * @brief 编码密码（BCD 格式，旧版兼容）
   * @param password 6 位数字字符串
   * @return 编码后的 3 字节数组
   * @deprecated 建议使用 EncodePasswordHex
   */
  static std::array<uint8_t, 3> EncodePassword(const char *password);

  /**
   * @brief 解码密码（BCD 格式，旧版兼容）
   * @param data 3 字节编码数据
   * @return 解码后的 6 位数字字符串
   * @deprecated 建议使用 DecodePasswordHex
   */
  static std::string DecodePassword(const std::array<uint8_t, 3> &data);
};

} // namespace xiaozhi
