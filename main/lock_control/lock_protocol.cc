#include "lock_protocol.h"

#include <cstring>

namespace xiaozhi {

std::vector<uint8_t> LockProtocol::BuildMessage(uint8_t cat, uint8_t type,
                                                const std::array<uint8_t, 3>& data) {
  std::vector<uint8_t> msg(LOCK_PROTOCOL_LENGTH);
  
  // Fill header
  msg[0] = LOCK_PROTOCOL_HEADER;
  msg[1] = cat;
  msg[2] = type;
  
  // Copy data (3 bytes)
  msg[3] = data[0];
  msg[4] = data[1];
  msg[5] = data[2];
  
  // Calculate and fill checksum
  msg[6] = CalculateChecksum(cat, type, data.data());
  
  return msg;
}

LockMessage LockProtocol::ParseMessage(const uint8_t* data, size_t len) {
  LockMessage msg = {0, 0, {0, 0, 0}, false};
  
  // Validate length
  if (len != LOCK_PROTOCOL_LENGTH) {
    return msg;
  }
  
  // Validate header
  if (data[0] != LOCK_PROTOCOL_HEADER) {
    return msg;
  }
  
  // Extract fields
  uint8_t cat = data[1];
  uint8_t type = data[2];
  uint8_t data_bytes[3] = {data[3], data[4], data[5]};
  uint8_t checksum = data[6];
  
  // Validate checksum
  uint8_t calculated_checksum = CalculateChecksum(cat, type, data_bytes);
  if (checksum != calculated_checksum) {
    return msg;
  }
  
  // Fill message structure
  msg.category = cat;
  msg.type = type;
  msg.data[0] = data_bytes[0];
  msg.data[1] = data_bytes[1];
  msg.data[2] = data_bytes[2];
  msg.valid = true;
  
  return msg;
}

uint8_t LockProtocol::CalculateChecksum(uint8_t cat, uint8_t type, const uint8_t* data) {
  return (cat + type + data[0] + data[1] + data[2]) & 0xFF;
}

std::vector<uint8_t> LockProtocol::BuildAck(uint8_t orig_cat, uint8_t orig_type, bool success) {
  std::array<uint8_t, 3> data = {
    orig_cat,
    orig_type,
    0
  };
  
  uint8_t ack_type = success ? static_cast<uint8_t>(AckType::ACK_OK) 
                             : static_cast<uint8_t>(AckType::ACK_FAIL);
  
  return BuildMessage(static_cast<uint8_t>(MsgCategory::ACK), ack_type, data);
}

std::array<uint8_t, 3> LockProtocol::EncodePassword(const char* password) {
  std::array<uint8_t, 3> result = {0, 0, 0};
  
  // Validate password length
  size_t len = strlen(password);
  if (len != 6) {
    // Return error indicator (all zeros)
    return result;
  }
  
  // Validate all characters are digits
  for (size_t i = 0; i < 6; i++) {
    if (password[i] < '0' || password[i] > '9') {
      // Return error indicator (all zeros)
      return result;
    }
  }
  
  // Encode using BCD format
  // data[0] = (d1<<4)|d2, data[1] = (d3<<4)|d4, data[2] = (d5<<4)|d6
  result[0] = ((password[0] - '0') << 4) | (password[1] - '0');
  result[1] = ((password[2] - '0') << 4) | (password[3] - '0');
  result[2] = ((password[4] - '0') << 4) | (password[5] - '0');
  
  return result;
}

std::string LockProtocol::DecodePassword(const std::array<uint8_t, 3>& data) {
  std::string password;
  password.reserve(6);
  
  // Extract 6 digits from 3 bytes
  // Each byte contains 2 digits (high nibble, low nibble)
  password += ('0' + ((data[0] >> 4) & 0x0F));  // d1
  password += ('0' + (data[0] & 0x0F));         // d2
  password += ('0' + ((data[1] >> 4) & 0x0F));  // d3
  password += ('0' + (data[1] & 0x0F));         // d4
  password += ('0' + ((data[2] >> 4) & 0x0F));  // d5
  password += ('0' + (data[2] & 0x0F));         // d6
  
  return password;
}

}  // namespace xiaozhi
