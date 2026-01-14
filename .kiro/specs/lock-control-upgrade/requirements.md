# Requirements Document

## Introduction

本需求文档综合了三个 ESP32 锁控模块的升级需求：
1. **协议升级**：支持 STM32 端 UART 协议从 v2.1 升级到 v2.7
2. **事件处理修改**：调整 ESP32 对锁控事件的本地处理逻辑
3. **消息 ID 追溯机制**：实现两级确认机制（esp32_ack + ack）

这三个需求相互关联，需要统一设计和实施，以确保 ESP32 与 STM32、服务器之间的通信协议一致性和可追溯性。

## Glossary

- **ESP32**: 智能猫眼门锁系统的主控芯片，负责 WiFi 通信、摄像头、显示和协议转换
- **STM32**: 锁控 MCU，负责门锁硬件控制（开锁、指纹、NFC、蜂鸣器等）
- **UART_Protocol**: ESP32 与 STM32 之间的 7 字节固定长度串口通信协议
- **WebSocket_Protocol**: ESP32 与服务器之间的 JSON 格式实时通信协议
- **seq_id**: 消息序列号，由 App 生成，用于追踪命令执行状态
- **esp32_ack**: 第一级确认消息，表示 ESP32 已收到命令并开始处理
- **ack**: 第二级确认消息，表示命令已执行完成
- **PendingCommand**: 待处理命令结构体，用于关联 UART 响应与原始 seq_id
- **LOCK_PROTOCOL_EMPTY**: 协议空值常量 0xFF，用于填充未使用的数据字段
- **RptType**: 上报消息类型枚举（RPT_EVENT、RPT_UNLOCK、RPT_DOOR_OPENED 等）
- **EventId**: 事件 ID 枚举（EVT_DOORBELL、EVT_PIR、EVT_TAMPER 等）
- **LockStatusCode**: 锁状态码枚举（DOOR_CLOSED、LOCK_SUCCESS、BOLT_ALARM）
- **DoorSource**: 开门来源枚举（OUTSIDE、INSIDE、UNKNOWN）

## Requirements

### Requirement 1: 协议空值字段升级

**User Story:** As a developer, I want the ESP32 to use 0xFF instead of 0x00 for empty protocol fields, so that the communication is compatible with STM32 protocol v2.4+.

#### Acceptance Criteria

1. THE Lock_Protocol SHALL define a constant `LOCK_PROTOCOL_EMPTY` with value 0xFF
2. WHEN building ACK_OK response, THE Lock_Protocol SHALL fill D1 and D2 with LOCK_PROTOCOL_EMPTY
3. WHEN building ACK_ERR response, THE Lock_Protocol SHALL fill D2 with LOCK_PROTOCOL_EMPTY
4. WHEN sending any command with unused data fields, THE Lock_Control SHALL fill those fields with LOCK_PROTOCOL_EMPTY

### Requirement 2: 新增上报类型支持

**User Story:** As a developer, I want the ESP32 to handle new report types from STM32, so that the system can distinguish between unlock commands and actual door opening events.

#### Acceptance Criteria

1. THE Lock_Protocol SHALL define RPT_DOOR_OPENED (0xA2) in the RptType enumeration
2. WHEN receiving RPT_DOOR_OPENED message, THE Application SHALL extract unlock method (D0) and door source (D1)
3. WHEN receiving RPT_DOOR_OPENED message, THE Application SHALL report the event to the server with door source information

### Requirement 3: 新增事件类型支持

**User Story:** As a developer, I want the ESP32 to handle the new EVT_LOCK_STATUS event, so that the system can track door closing and auto-lock status.

#### Acceptance Criteria

1. THE Lock_Protocol SHALL define EVT_LOCK_STATUS (0x06) in the EventId enumeration
2. THE Lock_Protocol SHALL define LockStatusCode enumeration with DOOR_CLOSED (0x00), LOCK_SUCCESS (0x01), and BOLT_ALARM (0x02)
3. WHEN receiving EVT_LOCK_STATUS with status DOOR_CLOSED, THE Application SHALL log the event and report to server
4. WHEN receiving EVT_LOCK_STATUS with status LOCK_SUCCESS, THE Application SHALL log the auto-lock success and report to server
5. WHEN receiving EVT_LOCK_STATUS with status BOLT_ALARM, THE Application SHALL log a warning and report to server

### Requirement 4: 新增指纹/NFC 响应状态码

**User Story:** As a developer, I want the ESP32 to handle new fingerprint/NFC response status codes, so that the system can properly handle duplicate entries and ID conflict scenarios.

#### Acceptance Criteria

1. THE Lock_Protocol SHALL define FP_ALREADY_EXISTS (0x06) and FP_ID_OCCUPIED (0x07) in the FpRespStatus enumeration for both fingerprint and NFC responses
2. WHEN receiving fingerprint response with status FP_ALREADY_EXISTS, THE Application SHALL return the existing ID to the server
3. WHEN receiving fingerprint response with status FP_ID_OCCUPIED, THE Application SHALL return the newly allocated ID to the server
4. WHEN receiving NFC response with status FP_ALREADY_EXISTS (0x06), THE Application SHALL return the existing ID to the server
5. WHEN receiving NFC response with status FP_ID_OCCUPIED (0x07), THE Application SHALL return the newly allocated ID to the server

### Requirement 5: 事件处理逻辑修改 - 撬锁报警

**User Story:** As a developer, I want the ESP32 to only report tamper alerts to the server without local alarm handling, so that the STM32 handles all alarm sounds.

#### Acceptance Criteria

1. WHEN receiving EVT_TAMPER event, THE Application SHALL NOT call HandleTamperAlert function
2. WHEN receiving EVT_TAMPER event, THE Application SHALL report the event to the server
3. WHEN receiving EVT_TAMPER event, THE Application SHALL log the event with alert level

### Requirement 6: 事件处理逻辑修改 - 门未关超时

**User Story:** As a developer, I want the ESP32 to play a voice prompt instead of displaying a warning when the door is not closed, so that the screen only shows camera feed.

#### Acceptance Criteria

1. WHEN receiving EVT_DOOR_OPEN event, THE Application SHALL NOT call HandleDoorNotClosed function
2. WHEN receiving EVT_DOOR_OPEN event, THE Application SHALL play a voice prompt "门未关好，请检查"
3. WHEN receiving EVT_DOOR_OPEN event, THE Application SHALL report the event to the server

### Requirement 7: 事件处理逻辑修改 - 低电量警告

**User Story:** As a developer, I want the ESP32 to only report low battery warnings to the server without displaying alerts, so that the screen only shows camera feed.

#### Acceptance Criteria

1. WHEN receiving EVT_LOW_BATTERY event, THE Application SHALL NOT call Alert function
2. WHEN receiving EVT_LOW_BATTERY event, THE Application SHALL report the event to the server
3. WHEN receiving EVT_LOW_BATTERY event, THE Application SHALL log the battery percentage

### Requirement 8: 密码查询结果上报

**User Story:** As a developer, I want the ESP32 to report password query results to the server, so that the server can sync passwords with the App.

#### Acceptance Criteria

1. THE WebSocket_Protocol SHALL implement SendPasswordReport method
2. WHEN receiving RPT_PWD message, THE Application SHALL call SendPasswordReport with the password
3. THE SendPasswordReport method SHALL send a JSON message with type "password_report" and the 6-digit password string

### Requirement 9: 两级确认机制 - esp32_ack

**User Story:** As a developer, I want the ESP32 to send an esp32_ack immediately upon receiving a command, so that the server knows the command was received.

#### Acceptance Criteria

1. THE WebSocket_Protocol SHALL implement SendEsp32Ack method
2. WHEN receiving a command from server, THE Application SHALL immediately send esp32_ack with the original seq_id
3. THE esp32_ack message SHALL contain type "esp32_ack", seq_id, code (0), and msg ("received")

### Requirement 10: 两级确认机制 - ack

**User Story:** As a developer, I want the ESP32 to send an ack when STM32 completes command execution, so that the server knows the command was executed.

#### Acceptance Criteria

1. THE WebSocket_Protocol SHALL implement SendAck method with seq_id, code, and msg parameters
2. WHEN receiving STM32 ACK for an immediate command, THE Application SHALL send ack with the original seq_id
3. WHEN receiving STM32 data frame for a query command, THE Application SHALL send ack with the original seq_id
4. WHEN receiving STM32 final result for a long-flow command, THE Application SHALL send ack with the original seq_id
5. THE ack message SHALL contain type "ack", seq_id, code (0 for success or error code), and msg

### Requirement 11: 待处理命令队列

**User Story:** As a developer, I want the ESP32 to maintain a pending command queue, so that STM32 responses can be correlated with original seq_ids.

#### Acceptance Criteria

1. THE Application SHALL define PendingCommandType enumeration with IMMEDIATE, QUERY, and LONG_FLOW types
2. THE Application SHALL define PendingCommand structure with seq_id, type, uart_type, timestamp, and status fields
3. WHEN sending a command to STM32, THE Application SHALL save the command info in pending_commands_ map with uart_type as key
4. WHEN receiving STM32 response, THE Application SHALL lookup pending command by uart_type and retrieve the seq_id
5. WHEN command completes or times out, THE Application SHALL remove the entry from pending_commands_

### Requirement 12: 命令超时处理

**User Story:** As a developer, I want the ESP32 to handle command timeouts, so that pending commands don't accumulate indefinitely.

#### Acceptance Criteria

1. THE Application SHALL define timeout constants: IMMEDIATE_TIMEOUT_MS (3000), QUERY_TIMEOUT_MS (5000), LONG_FLOW_TIMEOUT_MS (60000)
2. THE Application SHALL implement CleanupPendingCommands method to check for timed-out commands
3. WHEN a command times out, THE Application SHALL send ack with code 5 (timeout) and remove the pending command
4. THE CleanupPendingCommands method SHALL be called periodically (e.g., every second)

### Requirement 13: 统一错误码映射

**User Story:** As a developer, I want the ESP32 to map STM32 error codes to unified error codes, so that the server and App receive consistent error information.

#### Acceptance Criteria

1. THE Application SHALL implement MapStm32ErrorCode function
2. THE MapStm32ErrorCode function SHALL map STM32 ERR_BUSY (0x01) to code 2
3. THE MapStm32ErrorCode function SHALL map STM32 ERR_UNSUPPORT (0x02) to code 4
4. THE MapStm32ErrorCode function SHALL map STM32 ERR_PARAM (0x03) to code 3
5. THE MapStm32ErrorCode function SHALL map STM32 ERR_FP_FULL (0x04) and ERR_NFC_FULL (0x05) to code 7
6. THE MapStm32ErrorCode function SHALL map STM32 ERR_HARDWARE (0x06) to code 6
7. THE MapStm32ErrorCode function SHALL map STM32 ERR_TIMEOUT (0xFF) to code 5
8. THE MapStm32ErrorCode function SHALL map unknown errors to code 10

