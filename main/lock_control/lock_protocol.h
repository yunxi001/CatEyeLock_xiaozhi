#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace xiaozhi {

// Protocol constants
constexpr uint8_t LOCK_PROTOCOL_HEADER = 0xAA;
constexpr size_t LOCK_PROTOCOL_LENGTH = 7;
constexpr size_t LOCK_PROTOCOL_DATA_LEN = 3;

// Message categories
enum class MsgCategory : uint8_t {
  EVENT = 0x01,    // STM32 → ESP32
  CONTROL = 0x02,  // ESP32 → STM32
  STATUS = 0x03,   // Bidirectional
  QUERY = 0x04,    // Bidirectional
  ACK = 0x0F,      // Bidirectional
};

// Event types (CAT=0x01)
enum class EventType : uint8_t {
  DOORBELL_PRESSED = 0x01,  // Trigger face recognition
  HUMAN_DETECTED = 0x02,    // Trigger face recognition
  LOCK_TAMPER = 0x03,       // Alarm + report
  PERSON_LEFT = 0x04,       // Voice feedback
  PERSON_ENTERED = 0x05,    // Voice feedback
  DOOR_NOT_CLOSED = 0x06,   // Report to server
  PASSWORD_ERROR = 0x07,    // Voice feedback
  LOCK_LOCKED = 0x08,       // Voice feedback
  HEARTBEAT = 0x10,         // Keep-alive
};

// Control types (CAT=0x02)
enum class ControlType : uint8_t {
  UNLOCK = 0x01,
  ALARM_ON = 0x02,
  ALARM_OFF = 0x03,
  SET_TEMP_CODE = 0x04,
  LED_CTRL = 0x05,
};

// ACK types (CAT=0x0F)
enum class AckType : uint8_t {
  ACK_OK = 0x00,
  ACK_FAIL = 0x01,
};

// Parsed message structure
struct LockMessage {
  uint8_t category;
  uint8_t type;
  std::array<uint8_t, 3> data;
  bool valid;

  // Helper methods
  MsgCategory GetCategory() const { return static_cast<MsgCategory>(category); }
  bool IsEvent() const { return category == static_cast<uint8_t>(MsgCategory::EVENT); }
  bool IsControl() const { return category == static_cast<uint8_t>(MsgCategory::CONTROL); }
  bool IsStatus() const { return category == static_cast<uint8_t>(MsgCategory::STATUS); }
  bool IsQuery() const { return category == static_cast<uint8_t>(MsgCategory::QUERY); }
  bool IsAck() const { return category == static_cast<uint8_t>(MsgCategory::ACK); }
};

class LockProtocol {
 public:
  // Message construction
  static std::vector<uint8_t> BuildMessage(uint8_t cat, uint8_t type,
                                           const std::array<uint8_t, 3>& data = {0, 0, 0});

  // Message parsing
  static LockMessage ParseMessage(const uint8_t* data, size_t len);

  // Checksum calculation
  static uint8_t CalculateChecksum(uint8_t cat, uint8_t type, const uint8_t* data);

  // ACK message construction
  static std::vector<uint8_t> BuildAck(uint8_t orig_cat, uint8_t orig_type, bool success);

  // Password encoding/decoding (BCD format)
  static std::array<uint8_t, 3> EncodePassword(const char* password);
  static std::string DecodePassword(const std::array<uint8_t, 3>& data);
};

}  // namespace xiaozhi
