# Requirements Document - Face Recognition Feature

## Introduction

This document specifies the requirements for adding face recognition functionality to the xiaozhi-esp32 smart lock system. The feature enables the ESP32-S3 device to capture photos when triggered by the lock control MCU (STM32C8T6), send them to a server for face recognition, and respond with appropriate actions (unlock, voice feedback) based on the recognition results.

The face recognition feature operates in normal mode (not monitor mode) and integrates with the existing voice interaction system without disrupting the current functionality.

---

## Glossary

- **ESP32-S3**: The main application processor running the xiaozhi-esp32 firmware
- **STM32C8T6**: The lock control MCU that manages physical lock operations and sensors
- **Lock Control Service**: The ESP32 service that handles UART communication with STM32
- **Face Recognition**: The process of capturing a photo, sending it to the server, and receiving identification results
- **Normal Mode**: The default operating mode with voice interaction and wake word detection
- **Monitor Mode**: A separate mode for real-time video streaming (not used for face recognition)
- **TTS**: Text-to-Speech audio feedback
- **UART Protocol**: The 7-byte serial communication protocol between ESP32 and STM32
- **BinaryProtocol2**: The existing binary protocol for sending video frames to the server
- **Trigger Event**: An event from STM32 that initiates face recognition (doorbell, human detection)
- **Access Control**: The decision to grant or deny access based on face recognition results

---

## Requirements

### Requirement 1: UART Communication Protocol

**User Story:** As a system integrator, I want a reliable serial communication protocol between ESP32 and STM32, so that events and commands can be exchanged efficiently.

#### Acceptance Criteria

1. WHEN the Lock Control Service initializes THEN the system SHALL configure UART with 9600 baud rate, 8 data bits, no parity, and 1 stop bit
2. WHEN a message is transmitted THEN the system SHALL use a fixed 7-byte protocol format with header 0xAA, category, type, 3 data bytes, and checksum
3. WHEN a message is received THEN the system SHALL validate the header byte equals 0xAA and checksum matches the calculated value
4. WHEN a valid message is received THEN the system SHALL send an ACK message within 100 milliseconds
5. WHEN an invalid message is received THEN the system SHALL discard the message and log an error

---

### Requirement 2: Event Reception from STM32

**User Story:** As the ESP32 system, I want to receive and process events from the STM32 lock controller, so that I can respond to doorbell presses, human detection, and other lock-related events.

#### Acceptance Criteria

1. WHEN the STM32 sends a doorbell pressed event (CAT=0x01, TYPE=0x01) THEN the ESP32 SHALL trigger the face recognition process
2. WHEN the STM32 sends a human detected event (CAT=0x01, TYPE=0x02) THEN the ESP32 SHALL trigger the face recognition process
3. WHEN the STM32 sends a lock tamper event (CAT=0x01, TYPE=0x03) THEN the ESP32 SHALL activate the alarm and report to the server
4. WHEN the STM32 sends a person left event (CAT=0x01, TYPE=0x04) THEN the ESP32 SHALL play a goodbye voice message
5. WHEN the STM32 sends a person entered event (CAT=0x01, TYPE=0x05) THEN the ESP32 SHALL play a welcome voice message
6. WHEN the STM32 sends a door not closed event (CAT=0x01, TYPE=0x06) THEN the ESP32 SHALL report the status to the server
7. WHEN the STM32 sends a password error event (CAT=0x01, TYPE=0x07) THEN the ESP32 SHALL play a password error voice message
8. WHEN the STM32 sends a lock locked event (CAT=0x01, TYPE=0x08) THEN the ESP32 SHALL play a lock locked voice message

---

### Requirement 3: Control Commands to STM32

**User Story:** As the ESP32 system, I want to send control commands to the STM32 lock controller, so that I can unlock the door, control alarms, and set temporary access codes.

#### Acceptance Criteria

1. WHEN the system needs to unlock the door THEN the ESP32 SHALL send an unlock command (CAT=0x02, TYPE=0x01) to the STM32
2. WHEN the system needs to activate an alarm THEN the ESP32 SHALL send an alarm on command (CAT=0x02, TYPE=0x02) with the alarm level in DATA0
3. WHEN the system needs to deactivate an alarm THEN the ESP32 SHALL send an alarm off command (CAT=0x02, TYPE=0x03) to the STM32
4. WHEN the system needs to set a temporary access code THEN the ESP32 SHALL send a set temp code command (CAT=0x02, TYPE=0x04) with the 6-digit code encoded in 3 data bytes using BCD format
5. WHEN the system needs to control the LED THEN the ESP32 SHALL send an LED control command (CAT=0x02, TYPE=0x05) with mode, color, and brightness parameters

---

### Requirement 4: Password Encoding

**User Story:** As a developer, I want a compact encoding scheme for 6-digit passwords, so that they can be transmitted efficiently in the 3-byte data field.

#### Acceptance Criteria

1. WHEN encoding a 6-digit password THEN the system SHALL use BCD encoding with each byte storing two digits
2. WHEN encoding password "123456" THEN the system SHALL produce DATA0=0x12, DATA1=0x34, DATA2=0x56
3. WHEN decoding 3 data bytes THEN the system SHALL extract 6 decimal digits by separating high and low nibbles of each byte
4. WHEN the password contains non-digit characters THEN the system SHALL reject the encoding and return an error
5. WHEN the password length is not exactly 6 digits THEN the system SHALL reject the encoding and return an error

---

### Requirement 5: Face Recognition Trigger

**User Story:** As a user, I want the system to automatically capture my photo when I press the doorbell or approach the door, so that I can be identified without manual interaction.

#### Acceptance Criteria

1. WHEN a trigger event is received AND the device state is Idle THEN the system SHALL initiate the face recognition process
2. WHEN a trigger event is received AND the device state is not Idle THEN the system SHALL ignore the trigger and log a warning
3. WHEN the face recognition process starts THEN the system SHALL check if the camera is available
4. WHEN the camera is not available THEN the system SHALL abort the process and log an error
5. WHEN the audio channel is not open THEN the system SHALL open the audio channel before proceeding

---

### Requirement 6: Photo Capture

**User Story:** As the system, I want to capture a high-quality photo for face recognition, so that the server can accurately identify the person.

#### Acceptance Criteria

1. WHEN capturing a photo for face recognition THEN the system SHALL use the Capture() function to ensure image quality
2. WHEN the Capture() function is called THEN the system SHALL acquire 3 frames and retain the last frame
3. WHEN the photo is captured THEN the system SHALL display a preview on the screen
4. WHEN the photo capture fails THEN the system SHALL log an error and abort the face recognition process
5. WHEN the photo is captured successfully THEN the system SHALL encode it as JPEG with quality level 80

---

### Requirement 7: Video Frame Transmission

**User Story:** As the system, I want to send the captured photo to the server using the existing video protocol, so that the server can perform face recognition.

#### Acceptance Criteria

1. WHEN sending a photo for face recognition THEN the system SHALL use the BinaryProtocol2 format with version=2 and type=1
2. WHEN constructing the BinaryProtocol2 message THEN the system SHALL set the reserved field to (width << 16) | height
3. WHEN the JPEG encoding completes THEN the system SHALL send the video frame with the current timestamp
4. WHEN the transmission fails THEN the system SHALL log an error and free the allocated memory
5. WHEN the transmission succeeds THEN the system SHALL free the allocated memory and wait for the server response

---

### Requirement 8: Face Recognition Result Processing

**User Story:** As the system, I want to process face recognition results from the server, so that I can grant or deny access and provide appropriate feedback.

#### Acceptance Criteria

1. WHEN the server sends a face recognition result message THEN the system SHALL parse the JSON with type="face_recognition"
2. WHEN the result is "known" AND access is granted THEN the system SHALL send an unlock command to the STM32
3. WHEN the result is "known" AND access is denied THEN the system SHALL not send an unlock command
4. WHEN the result is "unknown" THEN the system SHALL not send an unlock command
5. WHEN the result is "no_face" THEN the system SHALL not send an unlock command

---

### Requirement 9: TTS Audio Feedback

**User Story:** As a user, I want to hear voice feedback after face recognition, so that I know whether I have been identified and granted access.

#### Acceptance Criteria

1. WHEN the server sends a TTS start message (type="tts", state="start") THEN the system SHALL transition to the Speaking state
2. WHEN the server sends TTS audio packets THEN the system SHALL decode and play them through the audio service
3. WHEN the server sends a TTS sentence start message THEN the system SHALL display the text on the screen
4. WHEN the server sends a TTS stop message (type="tts", state="stop") THEN the system SHALL transition back to Idle or Listening state
5. WHEN TTS playback is interrupted by user action THEN the system SHALL stop playback and transition to the appropriate state

---

### Requirement 10: Server Command Processing

**User Story:** As a remote administrator, I want to send commands to the ESP32 through the server, so that I can remotely unlock the door or set temporary access codes.

#### Acceptance Criteria

1. WHEN the server sends a lock control message with command="unlock" THEN the ESP32 SHALL send an unlock command to the STM32
2. WHEN the server sends a lock control message with command="temp_code" THEN the ESP32 SHALL send a set temp code command to the STM32 with the provided 6-digit code
3. WHEN the server sends a lock control message with command="alarm_on" THEN the ESP32 SHALL send an alarm on command to the STM32
4. WHEN the server sends a lock control message with command="alarm_off" THEN the ESP32 SHALL send an alarm off command to the STM32
5. WHEN the server sends an invalid command THEN the ESP32 SHALL log a warning and ignore the command

---

### Requirement 11: State Management

**User Story:** As the system, I want to manage device states properly during face recognition, so that the process does not interfere with other operations.

#### Acceptance Criteria

1. WHEN face recognition is triggered THEN the system SHALL only proceed if the current state is Idle
2. WHEN face recognition is in progress THEN the system SHALL not respond to additional trigger events
3. WHEN monitor mode is active THEN the system SHALL not respond to lock control events
4. WHEN TTS playback starts THEN the system SHALL transition to the Speaking state
5. WHEN TTS playback ends THEN the system SHALL transition to Idle or Listening state based on the listening mode

---

### Requirement 12: Error Handling

**User Story:** As a developer, I want comprehensive error handling for face recognition, so that failures are logged and the system recovers gracefully.

#### Acceptance Criteria

1. WHEN the camera is unavailable THEN the system SHALL log an error and abort the face recognition process
2. WHEN JPEG encoding fails THEN the system SHALL log an error and free any allocated memory
3. WHEN the network transmission fails THEN the system SHALL log an error and free any allocated memory
4. WHEN the UART transmission fails THEN the system SHALL log an error and retry up to 3 times
5. WHEN the UART receive buffer overflows THEN the system SHALL discard incomplete messages and resynchronize on the next header byte

---

### Requirement 13: Memory Management

**User Story:** As a developer, I want proper memory management for face recognition, so that the system does not leak memory or run out of resources.

#### Acceptance Criteria

1. WHEN allocating memory for JPEG data THEN the system SHALL use PSRAM (MALLOC_CAP_SPIRAM)
2. WHEN JPEG encoding completes THEN the system SHALL free the allocated memory regardless of transmission success
3. WHEN the UART receive task allocates buffers THEN the system SHALL free them after processing
4. WHEN the face recognition process aborts THEN the system SHALL free all allocated resources
5. WHEN the system runs low on memory THEN the system SHALL log a warning and reject new face recognition requests

---

### Requirement 14: Performance

**User Story:** As a user, I want fast face recognition response times, so that I don't have to wait long at the door.

#### Acceptance Criteria

1. WHEN a trigger event is received THEN the system SHALL start photo capture within 100 milliseconds
2. WHEN the photo is captured THEN the system SHALL complete JPEG encoding within 500 milliseconds
3. WHEN the JPEG is encoded THEN the system SHALL start network transmission within 100 milliseconds
4. WHEN the server responds THEN the system SHALL process the result within 100 milliseconds
5. WHEN access is granted THEN the system SHALL send the unlock command within 100 milliseconds

---

### Requirement 15: Logging and Debugging

**User Story:** As a developer, I want comprehensive logging for face recognition, so that I can debug issues and monitor system behavior.

#### Acceptance Criteria

1. WHEN a trigger event is received THEN the system SHALL log the event type and timestamp at INFO level
2. WHEN face recognition starts THEN the system SHALL log "Face recognition triggered" at INFO level
3. WHEN the photo is captured THEN the system SHALL log the image dimensions and size at DEBUG level
4. WHEN the server responds THEN the system SHALL log the recognition result at INFO level
5. WHEN an error occurs THEN the system SHALL log the error message and context at ERROR level

---

### Requirement 16: Integration with Existing System

**User Story:** As a system architect, I want face recognition to integrate seamlessly with the existing xiaozhi-esp32 system, so that it does not disrupt normal voice interaction functionality.

#### Acceptance Criteria

1. WHEN face recognition is added THEN the system SHALL not modify the existing normal mode voice interaction logic
2. WHEN face recognition is added THEN the system SHALL not modify the existing monitor mode video streaming logic
3. WHEN face recognition uses the camera THEN the system SHALL use the existing Capture() function without modification
4. WHEN face recognition sends video frames THEN the system SHALL use the existing SendVideo() function without modification
5. WHEN face recognition receives TTS audio THEN the system SHALL use the existing OnIncomingJson and OnIncomingAudio callbacks without modification

---

## Summary

This requirements document defines 16 requirements with 80 acceptance criteria for the face recognition feature. The requirements cover:

- UART communication protocol (Requirements 1-4)
- Event and command handling (Requirements 2-3, 10)
- Face recognition workflow (Requirements 5-9)
- System integration (Requirements 11, 16)
- Quality attributes (Requirements 12-15)

All requirements follow the EARS pattern and comply with INCOSE quality rules. Each requirement is testable, unambiguous, and traceable to the design and implementation.
