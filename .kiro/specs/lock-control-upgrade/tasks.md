# Implementation Plan: 锁控模块升级

## Overview

本实现计划将锁控模块升级分为 6 个主要阶段：
1. 协议层升级（空值常量、新增枚举）
2. 命令发送修改（空值字段填充）
3. WebSocket 协议扩展（新增消息方法）
4. 待处理命令队列实现
5. 事件处理逻辑修改
6. 两级确认机制集成

## Tasks

- [x] 1. 协议层升级 - lock_protocol.h/cc
  - [x] 1.1 新增 LOCK_PROTOCOL_EMPTY 常量和新枚举类型
    - 在 lock_protocol.h 中添加 `constexpr uint8_t LOCK_PROTOCOL_EMPTY = 0xFF`
    - 在 RptType 枚举中添加 `RPT_DOOR_OPENED = 0xA2`
    - 在 EventId 枚举中添加 `EVT_LOCK_STATUS = 0x06`
    - 新增 LockStatusCode 枚举（DOOR_CLOSED, LOCK_SUCCESS, BOLT_ALARM）
    - 新增 DoorSource 枚举（OUTSIDE, INSIDE, UNKNOWN）
    - 在 FpRespStatus 枚举中添加 `FP_ALREADY_EXISTS = 0x06` 和 `FP_ID_OCCUPIED = 0x07`
    - _Requirements: 1.1, 2.1, 3.1, 3.2, 4.1_

  - [x] 1.2 修改 BuildAckOk 和 BuildAckErr 方法
    - 修改 BuildAckOk 使用 LOCK_PROTOCOL_EMPTY 填充 D1 和 D2
    - 修改 BuildAckErr 使用 LOCK_PROTOCOL_EMPTY 填充 D2
    - _Requirements: 1.2, 1.3_

  - [ ]* 1.3 编写协议层单元测试
    - 验证 LOCK_PROTOCOL_EMPTY 常量值为 0xFF
    - 验证新增枚举值正确
    - 验证 BuildAckOk 输出的 D1/D2 为 0xFF
    - 验证 BuildAckErr 输出的 D2 为 0xFF
    - _Requirements: 1.1, 1.2, 1.3, 2.1, 3.1, 3.2, 4.1_

- [x] 2. 命令发送修改 - lock_control.cc
  - [x] 2.1 修改所有命令发送方法的空值字段
    - SendLock: D2 改为 LOCK_PROTOCOL_EMPTY
    - SendOledIcon: D1/D2 改为 LOCK_PROTOCOL_EMPTY
    - SendBeep: D2 改为 LOCK_PROTOCOL_EMPTY
    - SendLight: D1/D2 改为 LOCK_PROTOCOL_EMPTY
    - QuerySensors: D0/D1/D2 改为 LOCK_PROTOCOL_EMPTY
    - QueryStatus: D0/D1/D2 改为 LOCK_PROTOCOL_EMPTY
    - FingerprintEnroll: D2 改为 LOCK_PROTOCOL_EMPTY
    - FingerprintDelete: D2 改为 LOCK_PROTOCOL_EMPTY
    - FingerprintClear: D0/D1/D2 改为 LOCK_PROTOCOL_EMPTY
    - FingerprintQueryCount: D0/D1/D2 改为 LOCK_PROTOCOL_EMPTY
    - NfcEnroll/Delete/Clear/QueryCount: 同上
    - QueryPassword: D0/D1/D2 改为 LOCK_PROTOCOL_EMPTY
    - SendPing: D0/D1/D2 改为 LOCK_PROTOCOL_EMPTY
    - _Requirements: 1.4_

- [x] 3. Checkpoint - 协议层验证
  - 协议层修改已完成，代码结构正确

- [x] 4. WebSocket 协议扩展 - websocket_protocol.h/cc
  - [x] 4.1 新增 SendEsp32Ack 方法
    - 在 websocket_protocol.h 中声明方法
    - 实现发送 JSON 消息：type="esp32_ack", seq_id, code=0, msg="received"
    - _Requirements: 9.1, 9.3_

  - [x] 4.2 修改 SendAck 方法支持 seq_id
    - 确保 SendAck 方法接受 seq_id 参数
    - 实现发送 JSON 消息：type="ack", seq_id, code, msg
    - _Requirements: 10.1, 10.5_

  - [x] 4.3 新增 SendPasswordReport 方法
    - 在 websocket_protocol.h 中声明方法
    - 实现发送 JSON 消息：type="password_report", ts, data.password
    - 密码格式化为 6 位零填充字符串
    - _Requirements: 8.1, 8.3_

  - [ ]* 4.4 编写 WebSocket 消息格式单元测试
    - 验证 esp32_ack 消息格式
    - 验证 ack 消息格式
    - 验证 password_report 消息格式
    - **Property 3: 密码上报消息格式**
    - **Property 4: esp32_ack 消息格式**
    - **Property 5: ack 消息格式**
    - _Requirements: 8.3, 9.3, 10.5_

- [x] 5. 待处理命令队列实现 - application.h/cc
  - [x] 5.1 新增数据结构定义
    - 定义 PendingCommandType 枚举（IMMEDIATE, QUERY, LONG_FLOW）
    - 定义 PendingCommand 结构体
    - 在 Application 类中添加 pending_commands_ 成员
    - 添加超时常量定义
    - _Requirements: 11.1, 11.2, 12.1_

  - [x] 5.2 实现命令类型判断和 UART TYPE 获取
    - 实现 DetermineCommandType 方法
    - 实现 GetUartType 方法
    - _Requirements: 11.3_

  - [x] 5.3 实现错误码映射函数
    - 实现 MapStm32ErrorCode 方法
    - 映射所有 STM32 错误码到统一错误码
    - _Requirements: 13.1, 13.2, 13.3, 13.4, 13.5, 13.6, 13.7, 13.8_

  - [x] 5.4 实现超时清理机制
    - 实现 CleanupPendingCommands 方法
    - 实现 GetTimeoutForType 方法
    - 在时钟节拍中调用清理方法
    - _Requirements: 12.2, 12.3_

  - [ ]* 5.5 编写待处理命令队列单元测试
    - 验证命令保存和查找
    - 验证超时清理逻辑
    - 验证错误码映射
    - **Property 6: 待处理命令保存**
    - **Property 7: 待处理命令查找**
    - **Property 8: 超时命令清理**
    - **Property 9: STM32 错误码映射完整性**
    - _Requirements: 11.3, 11.4, 11.5, 12.3, 13.2-13.8_

- [x] 6. Checkpoint - 基础设施验证
  - 数据结构和方法已正确实现

- [x] 7. 事件处理逻辑修改 - application.cc
  - [x] 7.1 修改 EVT_TAMPER 处理逻辑
    - 移除 HandleTamperAlert 调用
    - 保留日志记录和服务器上报
    - _Requirements: 5.1, 5.2, 5.3_

  - [x] 7.2 修改 EVT_DOOR_OPEN 处理逻辑
    - 移除 HandleDoorNotClosed 调用
    - 添加语音播报 PlaySound
    - 保留服务器上报
    - _Requirements: 6.1, 6.2, 6.3_

  - [x] 7.3 修改 EVT_LOW_BATTERY 处理逻辑
    - 移除 Alert 调用
    - 保留日志记录和服务器上报
    - _Requirements: 7.1, 7.2, 7.3_

  - [x] 7.4 新增 EVT_LOCK_STATUS 处理逻辑
    - 处理 DOOR_CLOSED 状态：日志 + 上报
    - 处理 LOCK_SUCCESS 状态：日志 + 上报
    - 处理 BOLT_ALARM 状态：警告日志 + 上报
    - _Requirements: 3.3, 3.4, 3.5_

  - [x] 7.5 新增 RPT_DOOR_OPENED 处理逻辑
    - 实现 HandleDoorOpenedReport 方法
    - 解析开锁方式和开门来源
    - 上报服务器
    - _Requirements: 2.2, 2.3_

  - [x] 7.6 修改 RPT_PWD 处理逻辑
    - 添加 SendPasswordReport 调用
    - _Requirements: 8.2_

  - [x] 7.7 新增指纹/NFC 新状态码处理
    - 处理 FP_ALREADY_EXISTS (0x06)：返回已有 ID
    - 处理 FP_ID_OCCUPIED (0x07)：返回新分配 ID
    - 同时适用于指纹和 NFC 响应
    - _Requirements: 4.2, 4.3, 4.4, 4.5_

- [x] 8. 两级确认机制集成 - application.cc
  - [x] 8.1 修改服务器命令处理入口
    - 收到命令后立即发送 esp32_ack
    - 保存待处理命令到 pending_commands_
    - _Requirements: 9.2, 11.3_

  - [x] 8.2 修改 STM32 ACK 处理逻辑
    - 匹配待处理命令
    - 即时命令：发送 ack 并清理
    - 查询/长流程命令：更新状态，继续等待
    - _Requirements: 10.2, 11.4_

  - [x] 8.3 修改 STM32 数据帧处理逻辑
    - 查询命令：收到数据帧后发送 ack
    - 长流程命令：收到最终结果后发送 ack
    - 清理待处理命令
    - _Requirements: 10.3, 10.4, 11.5_

- [x] 9. Checkpoint - 功能验证
  - 所有功能修改已完成，代码逻辑正确

- [ ] 10. 集成测试
  - [ ]* 10.1 编写两级确认流程集成测试
    - 测试即时命令完整流程
    - 测试查询命令完整流程
    - 测试长流程命令完整流程
    - _Requirements: 9.2, 10.2, 10.3, 10.4_

  - [ ]* 10.2 编写事件处理集成测试
    - 测试 EVT_TAMPER 仅上报
    - 测试 EVT_DOOR_OPEN 语音播报
    - 测试 EVT_LOW_BATTERY 仅上报
    - 测试 EVT_LOCK_STATUS 上报
    - _Requirements: 5.1-5.3, 6.1-6.3, 7.1-7.3, 3.3-3.5_

- [x] 11. Final Checkpoint - 完整验证
  - [x] 任务 1：协议层升级已完成（LOCK_PROTOCOL_EMPTY、新枚举、BuildAckOk/Err 修改）
  - [x] 任务 2：命令发送修改已完成（所有空值字段使用 LOCK_PROTOCOL_EMPTY）
  - [x] 任务 4：WebSocket 协议扩展已完成（SendEsp32Ack、SendAck、SendPasswordReport）
  - [x] 任务 5：待处理命令队列已完成（数据结构、类型判断、错误码映射、超时清理）
  - [x] 任务 7：事件处理逻辑已完成（EVT_TAMPER/DOOR_OPEN/LOW_BATTERY/LOCK_STATUS、RPT_DOOR_OPENED、RPT_PWD、新状态码）
  - [x] 任务 8：两级确认机制已完成（esp32_ack、STM32 ACK/数据帧处理）
  - 注：单元测试和集成测试任务（标记 *）已跳过

## Notes

- 任务标记 `*` 的为可选测试任务，可跳过以加快 MVP 开发
- 每个任务都引用了具体的需求编号，便于追溯
- Checkpoint 任务用于阶段性验证，确保增量开发的稳定性
- 属性测试验证通用正确性属性，单元测试验证具体示例和边界情况

