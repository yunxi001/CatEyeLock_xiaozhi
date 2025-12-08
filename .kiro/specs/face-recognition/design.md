# Design Document - Face Recognition Feature

## Overview

This document describes the technical design for adding face recognition functionality to the xiaozhi-esp32 smart lock system. The design integrates a new Lock Control Service that communicates with the STM32 lock controller via UART, captures photos when triggered, sends them to the server for recognition, and responds with appropriate actions based on the results.

The design follows a modular architecture with clear separation of concerns:
- **Protocol Layer**: Handles UART message encoding/decoding
- **Service Layer**: Manages UART communication and event callbacks
- **Application Layer**: Orchestrates face recognition workflow and integrates with existing services

---

## Architecture

### System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         ESP32-S3                                 │
│                                                                  │
│  ┌──────────────┐      ┌──────────────┐      ┌──────────────┐ │
│  │ Application  │◄────►│ LockControl  │◄────►│ UART Driver  │ │
│  │              │      │   Service    │      │              │ │
│  └──────┬───────┘      └──────────────┘      └──────┬───────┘ │
│         │                                             │         │
│         │ triggers                                    │ serial  │
│         ▼                                             ▼         │
│  ┌──────────────┐      ┌──────────────┐      ┌──────────────┐ │
│  │ Esp32Camera  │      │   Protocol   │      │   STM32C8T6  │ │
│  │              │      │  (WebSocket) │      │   (锁控MCU)  │ │
│  └──────┬───────┘      └──────┬───────┘      └──────────────┘ │
│         │                     │                                │
│         │ JPEG                │ video frame                    │
│         └────────────────────►│                                │
│                               │                                │
└───────────────────────────────┼────────────────────────────────┘
                                │
                                │ WebSocket
                                ▼
                        ┌──────────────┐
                        │    Server    │
                        │ (人脸识别)    │
                        └──────┬───────┘
                                │
                                │ TTS audio
                                ▼
                        ┌──────────────┐
                        │ AudioService │
                        │  (播放TTS)   │
                        └──────────────┘
```

### Module Structure

```
main/
├── lock_control/                    # New module
│   ├── lock_control.h               # Service interface
│   ├── lock_control.cc              # Service implementation
│   ├── lock_protocol.h              # Protocol definitions
│   ├── lock_protocol.cc             # Protocol implementation
│   └── CMakeLists.txt               # Build configuration
├── boards/
│   └── bread-compact-wifi-s3cam/
│       ├── compact_wifi_board_s3cam.cc  # Modified: add UART init
│       └── config.h                     # Modified: add pin definitions
├── application.h                    # Modified: add face recognition
├── application.cc                   # Modified: integrate lock control
└── CMakeLists.txt                   # Modified: add lock_control
```

---

## Components and Interfaces

### 1. LockProtocol (Protocol Layer)

**Responsibility**: Encode and decode UART messages according to the 7-byte protocol.

**Interface**:
```cpp
class LockProtocol {
public:
    // Message construction
    static std::vector<uint8_t> BuildMessage(
        uint8_t cat, uint8_t type,
        const std::array<uint8_t, 3>& data = {}
    );
    
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
```

**Key Algorithms**:
- **Checksum**: `(CAT + TYPE + DATA0 + DATA1 + DATA2) & 0xFF`
- **BCD Encoding**: Each byte stores 2 digits (high nibble, low nibble)

---

### 2. LockControlService (Service Layer)

**Responsibility**: Manage UART communication, handle incoming events, send control commands.

**Interface**:
```cpp
class LockControlService {
public:
    using EventCallback = std::function<void(const LockMessage&)>;
    
    // Lifecycle
    bool Start(uart_port_t port, int tx_pin, int rx_pin);
    void Stop();
    bool IsRunning() const;
    
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
```

**Threading Model**:
- Main thread: Sends commands
- RX task: Receives and parses messages, invokes callback

---

### 3. Application (Application Layer)

**Responsibility**: Orchestrate face recognition workflow, integrate with existing services.

**New Methods**:
```cpp
class Application {
private:
    LockControlService* lock_control_;
    
    // Event handling
    void HandleLockEvent(const LockMessage& msg);
    
    // Face recognition workflow
    void TriggerFaceRecognition();
    void HandleFaceRecognitionResult(cJSON* root);
    
    // Other event handlers
    void HandleTamperAlert(uint8_t level);
    void HandleDoorNotClosed();
    
public:
    void InitializeLockControl();
};
```

**Integration Points**:
- Uses existing `Esp32Camera::Capture()` for photo capture
- Uses existing `Protocol::SendVideo()` for frame transmission
- Uses existing `OnIncomingJson()` for result processing
- Uses existing `OnIncomingAudio()` for TTS playback

---

## Data Models

### UART Protocol Message

```cpp
// Message structure (7 bytes)
struct UartMessage {
    uint8_t header;    // 0xAA
    uint8_t category;  // Message category
    uint8_t type;      // Message type
    uint8_t data[3];   // Data payload
    uint8_t checksum;  // Checksum
} __attribute__((packed));

// Parsed message
struct LockMessage {
    uint8_t category;
    uint8_t type;
    std::array<uint8_t, 3> data;
    bool valid;
    
    // Helper methods
    MsgCategory GetCategory() const;
    bool IsEvent() const;
    bool IsControl() const;
    bool IsStatus() const;
    bool IsQuery() const;
    bool IsAck() const;
};
```

### Message Categories

```cpp
enum class MsgCategory : uint8_t {
    EVENT   = 0x01,  // STM32 → ESP32
    CONTROL = 0x02,  // ESP32 → STM32
    STATUS  = 0x03,  // Bidirectional
    QUERY   = 0x04,  // Bidirectional
    ACK     = 0x0F,  // Bidirectional
};
```

### Event Types

```cpp
enum class EventType : uint8_t {
    DOORBELL_PRESSED = 0x01,  // Trigger face recognition
    HUMAN_DETECTED   = 0x02,  // Trigger face recognition
    LOCK_TAMPER      = 0x03,  // Alarm + report
    PERSON_LEFT      = 0x04,  // Voice feedback
    PERSON_ENTERED   = 0x05,  // Voice feedback
    DOOR_NOT_CLOSED  = 0x06,  // Report to server
    PASSWORD_ERROR   = 0x07,  // Voice feedback
    LOCK_LOCKED      = 0x08,  // Voice feedback
    HEARTBEAT        = 0x10,  // Keep-alive
};
```

### Control Types

```cpp
enum class ControlType : uint8_t {
    UNLOCK        = 0x01,
    ALARM_ON      = 0x02,
    ALARM_OFF     = 0x03,
    SET_TEMP_CODE = 0x04,
    LED_CTRL      = 0x05,
};
```

### Face Recognition Result

```cpp
struct FaceRecognitionResult {
    std::string result;      // "known", "unknown", "no_face"
    std::string person_name;
    int person_id;
    std::string relation;
    bool access_granted;
    std::string action;      // "open_door"
    std::string deny_reason;
};
```

---

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system—essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: UART Message Round-Trip Consistency
*For any* valid message with category, type, and data, encoding then parsing should produce an equivalent message with the same category, type, and data values.

**Validates: Requirements 1.2, 1.3**

### Property 2: Checksum Validation
*For any* received message, if the calculated checksum matches the received checksum, then the message should be marked as valid; otherwise, it should be marked as invalid.

**Validates: Requirements 1.3, 1.5**

### Property 3: Password Encoding Round-Trip
*For any* 6-digit password string, encoding to BCD format then decoding should produce the original password string.

**Validates: Requirements 4.1, 4.2, 4.3**

### Property 4: Trigger Event Recognition
*For any* event message with type DOORBELL_PRESSED or HUMAN_DETECTED, the system should trigger the face recognition process if and only if the device state is Idle.

**Validates: Requirements 2.1, 2.2, 5.1, 5.2**

### Property 5: Access Control Decision
*For any* face recognition result with result="known" and access.granted=true, the system should send an unlock command to the STM32.

**Validates: Requirements 8.2**

### Property 6: Access Denial
*For any* face recognition result where result="unknown" OR result="no_face" OR (result="known" AND access.granted=false), the system should not send an unlock command.

**Validates: Requirements 8.3, 8.4, 8.5**

### Property 7: Memory Cleanup
*For any* face recognition attempt, all allocated memory (JPEG buffer, frame data) should be freed regardless of whether the attempt succeeds or fails.

**Validates: Requirements 13.2, 13.4**

### Property 8: State Transition Consistency
*For any* TTS start message, if the device state is Idle or Listening, then the state should transition to Speaking.

**Validates: Requirements 9.1, 11.4**

### Property 9: UART ACK Response
*For any* valid non-ACK message received from STM32, the ESP32 should send an ACK message within 100 milliseconds.

**Validates: Requirements 1.4**

### Property 10: Error Logging
*For any* error condition (camera unavailable, encoding failure, transmission failure), the system should log an error message at ERROR level.

**Validates: Requirements 12.1, 12.2, 12.3, 15.5**

---

## Error Handling

### UART Communication Errors

**Scenario**: Invalid header byte
- **Detection**: First byte != 0xAA
- **Action**: Discard byte, continue searching for header
- **Logging**: DEBUG level

**Scenario**: Checksum mismatch
- **Detection**: Calculated checksum != received checksum
- **Action**: Discard message, log error
- **Logging**: ERROR level

**Scenario**: Transmission failure
- **Detection**: `uart_write_bytes()` returns error
- **Action**: Retry up to 3 times, log error
- **Logging**: ERROR level

### Face Recognition Errors

**Scenario**: Camera unavailable
- **Detection**: `camera->IsAvailable()` returns false
- **Action**: Abort process, log error
- **Logging**: ERROR level

**Scenario**: JPEG encoding failure
- **Detection**: `CaptureJpeg()` returns false
- **Action**: Free memory, abort process, log error
- **Logging**: ERROR level

**Scenario**: Network transmission failure
- **Detection**: `SendVideo()` returns false
- **Action**: Free memory, abort process, log error
- **Logging**: ERROR level

### Memory Errors

**Scenario**: Memory allocation failure
- **Detection**: `heap_caps_malloc()` returns nullptr
- **Action**: Abort process, log error
- **Logging**: ERROR level

**Scenario**: Memory leak detection
- **Prevention**: Use RAII patterns, ensure cleanup in all code paths
- **Monitoring**: Log memory usage periodically

---

## Testing Strategy

### Unit Testing

**LockProtocol Tests**:
- Test message encoding with various categories, types, and data
- Test message parsing with valid and invalid inputs
- Test checksum calculation
- Test password encoding/decoding with valid and invalid inputs
- Test ACK message construction

**LockControlService Tests**:
- Test UART initialization
- Test command sending (mock UART)
- Test message reception (inject test data)
- Test event callback invocation
- Test error handling (invalid messages, transmission failures)

**Application Tests**:
- Test event handling for each event type
- Test face recognition trigger conditions
- Test state transitions
- Test memory cleanup

### Property-Based Testing

**Property 1: Message Round-Trip**
- Generate random valid messages
- Encode then parse
- Verify all fields match

**Property 2: Checksum Validation**
- Generate random messages
- Calculate checksum
- Verify validation logic

**Property 3: Password Round-Trip**
- Generate random 6-digit passwords
- Encode then decode
- Verify password matches

**Property 4: Access Control**
- Generate random face recognition results
- Verify unlock command is sent only when appropriate

**Property 5: Memory Cleanup**
- Simulate various failure scenarios
- Verify all memory is freed

### Integration Testing

**End-to-End Face Recognition**:
1. Inject doorbell event from STM32
2. Verify photo capture
3. Verify JPEG encoding
4. Verify network transmission
5. Inject server response
6. Verify unlock command sent (if access granted)
7. Verify TTS playback
8. Verify state transitions

**UART Communication**:
1. Send command from ESP32
2. Verify STM32 receives correct bytes
3. Send event from STM32
4. Verify ESP32 receives and processes event
5. Verify ACK sent

### Performance Testing

**Response Time**:
- Measure time from trigger event to photo capture start
- Measure time from photo capture to JPEG encoding complete
- Measure time from encoding to network transmission start
- Measure time from server response to unlock command sent
- Verify all times meet requirements (< 100ms for most steps)

**Memory Usage**:
- Monitor PSRAM usage during face recognition
- Verify no memory leaks over 1000 iterations
- Verify system remains stable under low memory conditions

**Stress Testing**:
- Send rapid trigger events
- Verify system handles gracefully (ignores when busy)
- Verify no crashes or deadlocks

---

## Performance Considerations

### Memory Allocation

**JPEG Buffer**:
- Size: ~15KB for 640x480 image
- Location: PSRAM (MALLOC_CAP_SPIRAM)
- Lifetime: Allocated during encoding, freed after transmission

**UART Buffers**:
- RX buffer: 256 bytes
- TX buffer: 256 bytes
- Location: Internal RAM

**Frame Buffer**:
- Size: Depends on camera format
- Location: PSRAM
- Lifetime: Managed by Esp32Camera

### CPU Usage

**UART RX Task**:
- Priority: 5
- Stack: 2048 bytes
- CPU: < 1% (idle most of the time)

**Face Recognition**:
- Photo capture: ~100ms
- JPEG encoding: ~500ms
- Network transmission: ~100ms
- Total: ~700ms per recognition

### Network Bandwidth

**Video Frame**:
- Size: ~15KB (640x480, quality 80)
- Frequency: On-demand (not continuous)
- Bandwidth: Negligible impact

**TTS Audio**:
- Codec: OPUS
- Bitrate: ~24kbps
- Duration: 2-5 seconds typical
- Bandwidth: ~6-15KB per response

---

## Security Considerations

### UART Communication

**Threat**: Message injection or tampering
- **Mitigation**: Checksum validation
- **Limitation**: No encryption (physical access required)

**Threat**: Replay attacks
- **Mitigation**: Not implemented (low risk for physical access)
- **Future**: Add sequence numbers or timestamps

### Face Recognition

**Threat**: Photo spoofing (printed photo)
- **Mitigation**: Server-side liveness detection (out of scope)

**Threat**: Unauthorized access via compromised server
- **Mitigation**: Server authentication (existing WebSocket security)

### Password Transmission

**Threat**: Temporary code interception
- **Mitigation**: Encrypted WebSocket connection to server
- **Limitation**: UART transmission is unencrypted

---

## Deployment Considerations

### Hardware Requirements

**ESP32-S3**:
- UART pins: TX=GPIO3, RX=GPIO14
- Camera: OV2640 or compatible
- PSRAM: 8MB (for JPEG buffers)

**STM32C8T6**:
- UART: 9600 baud, 8N1
- Must implement 7-byte protocol

### Configuration

**UART Settings**:
```cpp
#define LOCK_UART_PORT      UART_NUM_1
#define LOCK_UART_TX_PIN    GPIO_NUM_3
#define LOCK_UART_RX_PIN    GPIO_NUM_14
#define LOCK_UART_BAUD      9600
```

**Face Recognition Settings**:
```cpp
#define FACE_RECOGNITION_JPEG_QUALITY  80
#define FACE_RECOGNITION_TIMEOUT_MS    5000
```

### Logging

**Log Levels**:
- ERROR: Critical failures (camera unavailable, encoding failure)
- WARN: Non-critical issues (trigger ignored due to state)
- INFO: Normal operations (trigger received, recognition result)
- DEBUG: Detailed information (message bytes, timing)

**Log Tags**:
- "LockProtocol": Protocol encoding/decoding
- "LockControl": UART communication
- "Application": Face recognition workflow

---

## Future Enhancements

### Phase 2 Features

1. **Liveness Detection**: Prevent photo spoofing
2. **Multiple Face Recognition**: Identify multiple people in one photo
3. **Face Database Management**: Add/remove faces via app
4. **Recognition History**: Log all recognition attempts
5. **Offline Recognition**: Local face database for network outages

### Phase 3 Features

1. **Encrypted UART**: Secure communication with STM32
2. **Biometric Fusion**: Combine face + fingerprint
3. **Adaptive Lighting**: Adjust camera settings based on ambient light
4. **Video Recording**: Record video clips on trigger events
5. **Cloud Backup**: Backup recognition logs to cloud

---

## Dependencies

### External Libraries

- **ESP-IDF**: v5.5.1 or later
- **FreeRTOS**: Included in ESP-IDF
- **cJSON**: Included in ESP-IDF
- **UART Driver**: ESP-IDF component

### Internal Dependencies

- **Esp32Camera**: Existing camera abstraction
- **Protocol**: Existing WebSocket/MQTT protocol
- **AudioService**: Existing audio playback service
- **Board**: Existing board abstraction

### Build System

- **CMake**: 3.16 or later
- **Component**: New `lock_control` component
- **Dependencies**: `driver`, `esp_common`, `freertos`

---

## Summary

This design document provides a comprehensive technical specification for the face recognition feature. The design follows a modular architecture with clear separation between protocol, service, and application layers. All components have well-defined interfaces and responsibilities.

Key design decisions:
1. **7-byte UART protocol**: Compact and efficient for STM32C8T6
2. **BCD password encoding**: Standard encoding, easy to implement
3. **Event-driven architecture**: Callbacks for loose coupling
4. **Reuse existing services**: Minimal changes to existing code
5. **Property-based testing**: Comprehensive correctness verification

The design addresses all 16 requirements with 80 acceptance criteria, and defines 10 correctness properties for testing. Performance, security, and deployment considerations are thoroughly documented.
