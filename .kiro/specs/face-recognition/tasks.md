# 实施任务清单 - 人脸识别功能

## 任务概述

本任务清单将人脸识别功能的设计转化为可执行的实施步骤。任务按照自底向上的顺序组织，从协议层开始，逐步构建服务层和应用层，最后进行集成测试。

---

## 阶段 1：协议层实现

- [x] 1. 创建 lock_control 模块基础结构



  - 创建 `main/lock_control/` 目录
  - 创建 `main/lock_control/CMakeLists.txt` 文件
  - 配置组件依赖：driver, esp_common, freertos
  - _需求: Requirements 1_

- [x] 1.1 实现协议常量和枚举定义


  - 在 `lock_protocol.h` 中定义协议常量（HEADER, LENGTH, DATA_LEN）
  - 定义 MsgCategory 枚举（EVENT, CONTROL, STATUS, QUERY, ACK）
  - 定义 EventType 枚举（DOORBELL_PRESSED, HUMAN_DETECTED 等）
  - 定义 ControlType 枚举（UNLOCK, ALARM_ON, SET_TEMP_CODE 等）
  - 定义 LockMessage 结构体
  - _需求: Requirements 1.2, 2, 3_

- [x] 1.2 实现消息构建函数


  - 实现 `BuildMessage(cat, type, data)` 函数
  - 实现消息头部填充（0xAA, category, type）
  - 实现数据字段复制（3字节）
  - 实现校验和计算并填充
  - _需求: Requirements 1.2_

- [x] 1.3 实现消息解析函数


  - 实现 `ParseMessage(data, len)` 函数
  - 验证消息长度（必须为7字节）
  - 验证帧头（必须为0xAA）
  - 验证校验和
  - 提取 category, type, data 字段
  - 返回 LockMessage 结构体
  - _需求: Requirements 1.3_

- [x] 1.4 实现校验和计算函数


  - 实现 `CalculateChecksum(cat, type, data)` 函数
  - 计算公式：(CAT + TYPE + DATA0 + DATA1 + DATA2) & 0xFF
  - _需求: Requirements 1.3_

- [x] 1.5 实现 ACK 消息构建函数


  - 实现 `BuildAck(orig_cat, orig_type, success)` 函数
  - 设置 category = 0x0F (ACK)
  - 设置 type = 0x00 (ACK_OK) 或 0x01 (ACK_FAIL)
  - 设置 data[0] = orig_cat, data[1] = orig_type
  - _需求: Requirements 1.4_

- [x] 1.6 实现密码编码函数（BCD格式）


  - 实现 `EncodePassword(password)` 函数
  - 验证密码长度为6位
  - 验证所有字符为数字
  - 使用 BCD 编码：data[0] = (d1<<4)|d2, data[1] = (d3<<4)|d4, data[2] = (d5<<4)|d6
  - 返回 std::array<uint8_t, 3>
  - _需求: Requirements 4.1, 4.2, 4.4, 4.5_

- [x] 1.7 实现密码解码函数


  - 实现 `DecodePassword(data)` 函数
  - 从每个字节提取高4位和低4位
  - 转换为6位数字字符串
  - _需求: Requirements 4.3_

- [ ]* 1.8 编写协议层属性测试
  - **Property 1: 消息编码解码往返一致性**
  - **验证: Requirements 1.2, 1.3**
  - 生成随机的 category, type, data
  - 编码后解码，验证所有字段相同

- [ ]* 1.9 编写协议层属性测试
  - **Property 2: 校验和验证**
  - **验证: Requirements 1.3, 1.5**
  - 生成随机消息
  - 验证正确的校验和被接受，错误的校验和被拒绝

- [ ]* 1.10 编写协议层属性测试
  - **Property 3: 密码编码往返一致性**
  - **验证: Requirements 4.1, 4.2, 4.3**
  - 生成随机6位密码
  - 编码后解码，验证密码相同

---

## 阶段 2：服务层实现

- [x] 2. 实现 LockControlService 类基础结构




  - 在 `lock_control.h` 中定义 LockControlService 类
  - 定义 EventCallback 类型
  - 声明成员变量：uart_port_, running_, rx_task_handle_, event_callback_
  - 声明公共方法：Start, Stop, IsRunning
  - _需求: Requirements 1.1_

- [x] 2.1 实现 UART 初始化




  - 实现 `Start(port, tx_pin, rx_pin)` 函数
  - 配置 UART 参数：9600 baud, 8N1
  - 调用 `uart_param_config()` 和 `uart_set_pin()`
  - 调用 `uart_driver_install()` 安装驱动（RX/TX buffer = 256）
  - 设置 running_ = true
  - _需求: Requirements 1.1_

- [x] 2.2 创建 UART 接收任务




  - 在 `Start()` 中创建 RX 任务
  - 任务名称："lock_rx"
  - 栈大小：2048 字节
  - 优先级：5
  - 实现 `RxTask()` 静态函数和 `RxLoop()` 成员函数
  - _需求: Requirements 2_

- [x] 2.3 实现消息接收循环




  - 在 `RxLoop()` 中实现接收逻辑
  - 逐字节读取，寻找帧头 0xAA
  - 收集7字节完整消息
  - 调用 `LockProtocol::ParseMessage()` 解析
  - 如果有效，调用事件回调
  - 如果无效，丢弃并记录错误
  - _需求: Requirements 1.3, 1.5, 2_

- [x] 2.4 实现自动 ACK 响应




  - 在接收到有效的非 ACK 消息后
  - 调用 `SendAck(msg.category, msg.type, true)`
  - 在 100ms 内发送
  - _需求: Requirements 1.4_

- [x] 2.5 实现通用消息发送函数




  - 实现 `SendMessage(cat, type, data)` 私有函数
  - 调用 `LockProtocol::BuildMessage()` 构建消息
  - 调用 `uart_write_bytes()` 发送
  - 记录发送的消息（DEBUG 级别）
  - 返回发送是否成功
  - _需求: Requirements 3_

- [x] 2.6 实现开锁命令




  - 实现 `SendUnlock()` 函数
  - 调用 `SendMessage(0x02, 0x01, {0, 0, 0})`
  - _需求: Requirements 3.1_

- [x] 2.7 实现警报控制命令



  - 实现 `SendAlarm(level)` 函数
  - 调用 `SendMessage(0x02, 0x02, {level, 0, 0})`
  - 实现 `SendAlarmOff()` 函数
  - 调用 `SendMessage(0x02, 0x03, {0, 0, 0})`
  - _需求: Requirements 3.2, 3.3_

- [x] 2.8 实现临时密码设置命令



  - 实现 `SendTempCode(password)` 函数
  - 验证密码长度和格式
  - 调用 `LockProtocol::EncodePassword()` 编码
  - 调用 `SendMessage(0x02, 0x04, encoded_data)`
  - _需求: Requirements 3.4_

- [x] 2.9 实现 LED 控制命令



  - 实现 `SendLedControl(mode, color, brightness)` 函数
  - 调用 `SendMessage(0x02, 0x05, {mode, color, brightness})`
  - _需求: Requirements 3.5_

- [x] 2.10 实现查询命令



  - 实现 `QueryLockState()` 函数
  - 实现 `QueryDoorState()` 函数
  - 实现 `QueryBattery()` 函数
  - 分别发送对应的查询消息（CAT=0x04）
  - _需求: Requirements 3_

- [x] 2.11 实现服务停止函数



  - 实现 `Stop()` 函数
  - 设置 running_ = false
  - 等待 RX 任务结束
  - 调用 `uart_driver_delete()` 卸载驱动
  - _需求: Requirements 1_

- [ ]* 2.12 编写服务层单元测试
  - 测试 UART 初始化
  - 测试命令发送（使用 mock UART）
  - 测试消息接收（注入测试数据）
  - 测试事件回调调用
  - 测试错误处理

- [ ]* 2.13 编写服务层属性测试
  - **Property 9: UART ACK 响应**
  - **验证: Requirements 1.4**
  - 发送随机有效消息
  - 验证在 100ms 内收到 ACK

---

## 阶段 3：板级集成

- [x] 3. 修改板级配置文件



  - 编辑 `main/boards/bread-compact-wifi-s3cam/config.h`
  - 添加 UART 引脚定义：
    ```cpp
    #define LOCK_UART_PORT      UART_NUM_1
    #define LOCK_UART_TX_PIN    GPIO_NUM_3
    #define LOCK_UART_RX_PIN    GPIO_NUM_14
    ```
  - _需求: Requirements 1.1_

- [x] 3.1 修改板级实现添加锁控服务


  - 编辑 `main/boards/bread-compact-wifi-s3cam/compact_wifi_board_s3cam.cc`
  - 在类中添加 `LockControlService* lock_control_` 成员
  - 在构造函数或初始化函数中创建服务
  - 调用 `lock_control_->Start(LOCK_UART_PORT, LOCK_UART_TX_PIN, LOCK_UART_RX_PIN)`
  - _需求: Requirements 1.1_

- [x] 3.2 添加获取锁控服务的接口


  - 在板级类中添加 `GetLockControl()` 公共方法
  - 返回 `lock_control_` 指针
  - _需求: Requirements 16_

- [x] 3.3 更新主 CMakeLists.txt


  - 编辑 `main/CMakeLists.txt`
  - 添加 `lock_control` 到组件依赖列表
  - _需求: Requirements 1_

---

## 阶段 4：Application 层集成


- [x] 4. 在 Application 中添加锁控服务引用


  - 编辑 `main/application.h`
  - 添加 `#include "lock_control.h"`
  - 添加私有成员：`LockControlService* lock_control_`
  - 声明事件处理函数：`HandleLockEvent(const LockMessage& msg)`
  - _需求: Requirements 2, 5_

- [x] 4.1 初始化锁控服务


  - 在 `Application::Application()` 或 `Initialize()` 中
  - 获取锁控服务：`lock_control_ = Board::GetInstance().GetLockControl()`
  - 设置事件回调：
    ```cpp
    lock_control_->SetEventCallback([this](const LockMessage& msg) {
        Schedule([this, msg]() { HandleLockEvent(msg); });
    });
    ```
  - _需求: Requirements 2_

- [x] 4.2 实现事件处理函数框架



  - 实现 `HandleLockEvent(const LockMessage& msg)` 函数
  - 使用 switch 语句根据 msg.type 分发事件
  - 记录接收到的事件（INFO 级别）
  - _需求: Requirements 2, 15.1_

- [x] 4.3 实现门铃和人体检测事件处理


  - 在 `HandleLockEvent()` 中处理 DOORBELL_PRESSED (0x01)
  - 在 `HandleLockEvent()` 中处理 HUMAN_DETECTED (0x02)
  - 两者都调用 `TriggerFaceRecognition()`
  - _需求: Requirements 2.1, 2.2, 5_

- [x] 4.4 实现其他事件处理


  - 处理 LOCK_TAMPER (0x03)：调用 `HandleTamperAlert(msg.data[0])`
  - 处理 PERSON_LEFT (0x04)：播放"再见"语音
  - 处理 PERSON_ENTERED (0x05)：播放"欢迎回家"语音
  - 处理 DOOR_NOT_CLOSED (0x06)：调用 `HandleDoorNotClosed()`
  - 处理 PASSWORD_ERROR (0x07)：播放"密码错误"语音
  - 处理 LOCK_LOCKED (0x08)：播放"已锁定"语音
  - _需求: Requirements 2.3, 2.4, 2.5, 2.6, 2.7, 2.8_

- [x] 4.5 实现人脸识别触发函数


  - 实现 `TriggerFaceRecognition()` 函数
  - 检查设备状态是否为 Idle
  - 如果不是 Idle，记录警告并返回
  - 检查摄像头是否可用
  - 如果不可用，记录错误并返回
  - 检查音频通道是否打开
  - 如果未打开，调用 `protocol_->OpenAudioChannel()`
  - _需求: Requirements 5.1, 5.2, 5.3, 5.4, 5.5, 15.2_

- [x] 4.6 实现照片捕获


  - 在 `TriggerFaceRecognition()` 中
  - 调用 `camera->Capture()` 拍照
  - 如果失败，记录错误并返回
  - _需求: Requirements 6.1, 6.4, 16.3_

- [x] 4.7 实现 JPEG 编码


  - 调用 `camera->CaptureJpeg(&jpeg_data, &jpeg_size, 80)`
  - 如果失败，记录错误并返回
  - _需求: Requirements 6.5, 15.3_

- [x] 4.8 实现视频帧发送


  - 获取当前时间戳
  - 调用 `protocol_->SendVideo(jpeg_data, jpeg_size, timestamp, width, height)`
  - 如果失败，记录错误
  - 释放 JPEG 内存：`heap_caps_free(jpeg_data)`
  - _需求: Requirements 7.1, 7.2, 7.3, 7.4, 7.5, 13.2, 16.4_

- [x] 4.9 实现人脸识别结果处理


  - 在 `OnIncomingJson()` 回调中添加处理
  - 检查 `type == "face_recognition"`
  - 实现 `HandleFaceRecognitionResult(root)` 函数
  - 解析 result 字段（"known", "unknown", "no_face"）
  - 解析 access.granted 字段
  - _需求: Requirements 8.1, 15.4_

- [x] 4.10 实现访问控制决策

  - 在 `HandleFaceRecognitionResult()` 中
  - 如果 result == "known" AND access.granted == true
  - 调用 `lock_control_->SendUnlock()`
  - 记录开锁操作（INFO 级别）
  - 否则不发送开锁命令
  - _需求: Requirements 8.2, 8.3, 8.4, 8.5_

- [x] 4.11 实现暴力破坏警报处理


  - 实现 `HandleTamperAlert(level)` 函数
  - 调用 `lock_control_->SendAlarm(level)` 激活警报
  - 构建 JSON 消息上报服务器
  - 调用 `protocol_->SendMcpMessage()` 发送
  - _需求: Requirements 2.3_

- [x] 4.12 实现门未关严实处理


  - 实现 `HandleDoorNotClosed()` 函数
  - 构建 JSON 消息上报服务器
  - 调用 `protocol_->SendMcpMessage()` 发送
  - _需求: Requirements 2.6_

- [x] 4.13 实现服务器命令处理

  - 在 `OnIncomingJson()` 回调中添加处理
  - 检查 `type == "lock_control"`
  - 解析 command 字段
  - 如果 command == "unlock"，调用 `lock_control_->SendUnlock()`
  - 如果 command == "temp_code"，解析 code 并调用 `lock_control_->SendTempCode()`
  - 如果 command == "alarm_on"，调用 `lock_control_->SendAlarm()`
  - 如果 command == "alarm_off"，调用 `lock_control_->SendAlarmOff()`
  - _需求: Requirements 10.1, 10.2, 10.3, 10.4_

- [ ]* 4.14 编写应用层单元测试
  - 测试事件处理函数
  - 测试人脸识别触发条件
  - 测试状态转换
  - 测试内存清理

- [ ]* 4.15 编写应用层属性测试
  - **Property 4: 触发事件识别**
  - **验证: Requirements 2.1, 2.2, 5.1, 5.2**
  - 生成随机事件和设备状态
  - 验证只在 Idle 状态触发人脸识别

- [ ]* 4.16 编写应用层属性测试
  - **Property 5: 访问控制决策**
  - **验证: Requirements 8.2**
  - 生成随机识别结果
  - 验证只在 known + granted 时发送开锁命令

- [ ]* 4.17 编写应用层属性测试
  - **Property 6: 访问拒绝**
  - **验证: Requirements 8.3, 8.4, 8.5**
  - 生成各种拒绝场景
  - 验证不发送开锁命令

- [ ]* 4.18 编写应用层属性测试
  - **Property 7: 内存清理**
  - **验证: Requirements 13.2, 13.4**
  - 模拟各种失败场景
  - 验证所有内存都被释放

---

## 阶段 5：性能优化和错误处理

- [x] 5. 实现 UART 传输重试机制


  - 在 `SendMessage()` 中添加重试逻辑
  - 如果 `uart_write_bytes()` 失败，重试最多3次
  - 每次重试间隔 10ms
  - 记录重试次数（WARN 级别）
  - _需求: Requirements 12.4_

- [x] 5.1 实现 UART 接收缓冲区溢出处理


  - 在 `RxLoop()` 中检测不完整消息
  - 如果超时未收到完整消息，丢弃已接收字节
  - 重新同步到下一个帧头 0xAA
  - 记录溢出事件（ERROR 级别）
  - _需求: Requirements 12.5_

- [x] 5.2 实现内存分配失败处理


  - 在所有 `heap_caps_malloc()` 调用后检查返回值
  - 如果返回 nullptr，记录错误并中止操作
  - 确保已分配的资源被释放
  - _需求: Requirements 13.1, 13.5_

- [x] 5.3 添加性能监控日志


  - 在人脸识别流程的关键点添加时间戳
  - 记录从触发到拍照的时间
  - 记录从拍照到编码完成的时间
  - 记录从编码到发送的时间
  - 记录从服务器响应到开锁的时间
  - 验证所有时间符合性能要求（< 100ms）
  - _需求: Requirements 14.1, 14.2, 14.3, 14.4, 14.5_

- [x] 5.4 实现低内存保护


  - 在触发人脸识别前检查可用内存
  - 如果 PSRAM 可用空间 < 100KB，拒绝请求
  - 记录警告并返回错误
  - _需求: Requirements 13.5_

- [x] 5.5 实现状态保护


  - 在 `TriggerFaceRecognition()` 中添加状态检查
  - 如果人脸识别正在进行，忽略新的触发
  - 如果监控模式激活，忽略锁控事件
  - 记录被忽略的事件（WARN 级别）
  - _需求: Requirements 11.2, 11.3_

---

## 阶段 6：集成测试

- [x] 6. 编译项目



  - 运行 `idf.py build`
  - 确保没有编译错误
  - 确保没有编译警告
  - 确保所有测试通过

- [ ]* 6.1 端到端集成测试：门铃触发人脸识别
  - 模拟 STM32 发送门铃事件
  - 验证 ESP32 拍照
  - 验证 JPEG 编码
  - 验证网络传输
  - 模拟服务器返回识别结果
  - 验证开锁命令发送（如果有权限）
  - 验证 TTS 播放
  - 验证状态转换

- [ ]* 6.2 端到端集成测试：人体检测触发人脸识别
  - 与 6.1 类似，但使用人体检测事件

- [ ]* 6.3 端到端集成测试：陌生人识别
  - 模拟识别结果为 "unknown"
  - 验证不发送开锁命令
  - 验证播放"请问您找谁"TTS

- [ ]* 6.4 端到端集成测试：无人脸
  - 模拟识别结果为 "no_face"
  - 验证不发送开锁命令
  - 验证播放提示 TTS

- [ ]* 6.5 端到端集成测试：访问被拒绝
  - 模拟识别结果为 "known" 但 access.granted = false
  - 验证不发送开锁命令
  - 验证播放拒绝原因 TTS

- [ ]* 6.6 UART 通信测试
  - 使用示波器或逻辑分析仪验证 UART 信号
  - 验证波特率为 9600
  - 验证消息格式正确
  - 验证 ACK 响应时间 < 100ms

- [ ]* 6.7 性能测试
  - 测量人脸识别完整流程的时间
  - 验证响应时间符合要求
  - 测量内存使用峰值
  - 验证不超过 PSRAM 限制

- [ ]* 6.8 压力测试
  - 连续发送 100 次触发事件
  - 验证系统稳定运行
  - 验证没有内存泄漏
  - 验证没有崩溃或死锁

- [ ]* 6.9 错误恢复测试
  - 模拟摄像头不可用
  - 模拟网络断开
  - 模拟 UART 传输失败
  - 验证系统正确处理并恢复

- [ ]* 6.10 状态管理测试
  - 在非 Idle 状态触发人脸识别
  - 验证被正确忽略
  - 在监控模式下发送锁控事件
  - 验证被正确忽略

---

## 检查点

- [x] 7. 最终检查点：确保所有测试通过



  - 确保所有单元测试通过
  - 确保所有属性测试通过
  - 确保所有集成测试通过
  - 确保性能测试通过
  - 确保没有内存泄漏
  - 询问用户是否有问题

---

## 任务统计

**总任务数：** 60+
- 核心实现任务：40
- 测试任务：20+ (标记为可选 *)

**预计时间：**
- 阶段 1（协议层）：2-3 小时
- 阶段 2（服务层）：3-4 小时
- 阶段 3（板级集成）：1 小时
- 阶段 4（应用层）：3-4 小时
- 阶段 5（优化）：1-2 小时
- 阶段 6（测试）：2-3 小时
- **总计：** 12-17 小时

**依赖关系：**
- 阶段 2 依赖阶段 1
- 阶段 3 依赖阶段 2
- 阶段 4 依赖阶段 3
- 阶段 5 可以与阶段 4 并行
- 阶段 6 依赖所有前面的阶段

---

## 注意事项

1. **测试任务标记为可选（*）**
   - 可以先实现核心功能
   - 测试可以在实现完成后补充
   - 但强烈建议完成所有测试

2. **按顺序执行**
   - 从阶段 1 开始，逐步推进
   - 每个阶段完成后再进入下一阶段
   - 确保每个阶段的代码都能编译通过

3. **增量开发**
   - 每完成一个任务就编译测试
   - 不要等到所有代码都写完才测试
   - 及早发现问题，及早修复

4. **代码审查**
   - 完成每个阶段后进行代码审查
   - 确保代码质量和可维护性
   - 确保符合项目编码规范

5. **文档更新**
   - 如果实现与设计有偏差，及时更新设计文档
   - 记录重要的设计决策和权衡
   - 更新 README 和用户文档

---

## 参考文档

- **Requirements**: `.kiro/specs/face-recognition/requirements.md`
- **Design**: `.kiro/specs/face-recognition/design.md`
- **Complete Design**: `docs/my_docs/face_recognition_complete_design.md`
- **Protocol Spec**: `docs/my_docs/video_protocol_specification.md`

---

**准备好开始实施了吗？从阶段 1 的任务 1 开始吧！** 🚀
