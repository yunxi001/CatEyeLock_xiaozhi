/**
 * @file lock_protocol.cc
 * @brief 锁控模块 UART 通信协议实现
 * @version 2.0
 */

#include "lock_protocol.h"
#include <cstring>

namespace xiaozhi {

/**
 * @brief 构建协议消息
 *
 * 按照协议格式组装 7 字节消息帧：
 * [帧头][类别][类型][D0][D1][D2][校验和]
 */
std::vector<uint8_t>
LockProtocol::BuildMessage(uint8_t cat, uint8_t type,
                           const std::array<uint8_t, 3> &data) {
  std::vector<uint8_t> msg(LOCK_PROTOCOL_LENGTH);
  msg[0] = LOCK_PROTOCOL_HEADER;
  msg[1] = cat;
  msg[2] = type;
  msg[3] = data[0];
  msg[4] = data[1];
  msg[5] = data[2];
  msg[6] = CalculateChecksum(cat, type, data.data());
  return msg;
}

/**
 * @brief 解析协议消息
 *
 * 验证帧头、长度和校验和，解析成功返回 valid=true 的消息结构体。
 */
LockMessage LockProtocol::ParseMessage(const uint8_t *data, size_t len) {
  LockMessage msg = {0, 0, {0, 0, 0}, false};

  // 长度校验
  if (len != LOCK_PROTOCOL_LENGTH) {
    return msg;
  }

  // 帧头校验
  if (data[0] != LOCK_PROTOCOL_HEADER) {
    return msg;
  }

  uint8_t cat = data[1];
  uint8_t type = data[2];
  uint8_t data_bytes[3] = {data[3], data[4], data[5]};
  uint8_t checksum = data[6];

  // 校验和验证
  uint8_t calculated = CalculateChecksum(cat, type, data_bytes);
  if (checksum != calculated) {
    return msg;
  }

  // 填充消息结构体
  msg.category = cat;
  msg.type = type;
  msg.data[0] = data_bytes[0];
  msg.data[1] = data_bytes[1];
  msg.data[2] = data_bytes[2];
  msg.valid = true;

  return msg;
}

/**
 * @brief 计算校验和
 *
 * 校验和 = (CAT + TYPE + D0 + D1 + D2) & 0xFF
 */
uint8_t LockProtocol::CalculateChecksum(uint8_t cat, uint8_t type,
                                        const uint8_t *data) {
  return (cat + type + data[0] + data[1] + data[2]) & 0xFF;
}

/**
 * @brief 构建成功应答消息
 *
 * 格式：CAT=0x00, TYPE=0x01, D0=原TYPE, D1=0xFF, D2=0xFF
 * 注：v2.4+ 协议要求未使用字段填充 0xFF
 */
std::vector<uint8_t> LockProtocol::BuildAckOk(uint8_t orig_type) {
  std::array<uint8_t, 3> data = {orig_type, LOCK_PROTOCOL_EMPTY,
                                 LOCK_PROTOCOL_EMPTY};
  return BuildMessage(static_cast<uint8_t>(MsgCategory::SYS),
                      static_cast<uint8_t>(SysType::ACK_OK), data);
}

/**
 * @brief 构建错误应答消息
 *
 * 格式：CAT=0x00, TYPE=0x00, D0=原TYPE, D1=错误码, D2=0xFF
 * 注：v2.4+ 协议要求未使用字段填充 0xFF
 */
std::vector<uint8_t> LockProtocol::BuildAckErr(uint8_t orig_type,
                                               AckError error) {
  std::array<uint8_t, 3> data = {orig_type, static_cast<uint8_t>(error),
                                 LOCK_PROTOCOL_EMPTY};
  return BuildMessage(static_cast<uint8_t>(MsgCategory::SYS),
                      static_cast<uint8_t>(SysType::ACK_ERR), data);
}

/**
 * @brief 编码密码（Hex 整数格式，大端）
 *
 * 将数字密码转换为 3 字节大端格式。
 * 示例：123456 = 0x01E240 -> {0x01, 0xE2, 0x40}
 */
std::array<uint8_t, 3> LockProtocol::EncodePasswordHex(uint32_t password) {
  std::array<uint8_t, 3> result = {0, 0, 0};

  // 密码范围检查：0 ~ 999999
  if (password > 999999) {
    return result;
  }

  // 大端编码：高位在前
  result[0] = (password >> 16) & 0xFF;
  result[1] = (password >> 8) & 0xFF;
  result[2] = password & 0xFF;

  return result;
}

/**
 * @brief 解码密码（Hex 整数格式）
 *
 * 将 3 字节大端格式转换为数字密码。
 */
uint32_t LockProtocol::DecodePasswordHex(const std::array<uint8_t, 3> &data) {
  return (static_cast<uint32_t>(data[0]) << 16) |
         (static_cast<uint32_t>(data[1]) << 8) | static_cast<uint32_t>(data[2]);
}

/**
 * @brief 编码密码（BCD 格式，旧版兼容）
 *
 * 将 6 位数字字符串转换为 3 字节 BCD 编码。
 * 示例："123456" -> {0x12, 0x34, 0x56}
 */
std::array<uint8_t, 3> LockProtocol::EncodePassword(const char *password) {
  std::array<uint8_t, 3> result = {0, 0, 0};

  // 长度校验
  size_t len = strlen(password);
  if (len != 6) {
    return result;
  }

  // 字符校验（必须是数字）
  for (size_t i = 0; i < 6; i++) {
    if (password[i] < '0' || password[i] > '9') {
      return result;
    }
  }

  // BCD 编码：每字节存储两位数字
  result[0] = ((password[0] - '0') << 4) | (password[1] - '0');
  result[1] = ((password[2] - '0') << 4) | (password[3] - '0');
  result[2] = ((password[4] - '0') << 4) | (password[5] - '0');

  return result;
}

/**
 * @brief 解码密码（BCD 格式，旧版兼容）
 *
 * 将 3 字节 BCD 编码转换为 6 位数字字符串。
 */
std::string LockProtocol::DecodePassword(const std::array<uint8_t, 3> &data) {
  std::string password;
  password.reserve(6);

  // 逐字节解码
  password += ('0' + ((data[0] >> 4) & 0x0F));
  password += ('0' + (data[0] & 0x0F));
  password += ('0' + ((data[1] >> 4) & 0x0F));
  password += ('0' + (data[1] & 0x0F));
  password += ('0' + ((data[2] >> 4) & 0x0F));
  password += ('0' + (data[2] & 0x0F));

  return password;
}

} // namespace xiaozhi
