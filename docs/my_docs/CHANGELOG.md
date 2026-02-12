# 变更日志

本文档记录项目的代码变更历史。

---

## 2025-12-10

### 文档整理

- 整理 `docs/my_docs/` 目录，删除冗余文档
- 更新 `README.md` 作为文档中心入口
- 更新 `face_recognition_handover.md` 为主交接文档
- 以 `.kiro/specs/face-recognition/` 为权威来源同步文档内容

---

## 2025-12-09

### 人脸识别功能实现

#### 新增模块: lock_control

| 文件                                 | 说明                                |
| ------------------------------------ | ----------------------------------- |
| `main/lock_control/lock_protocol.h`  | UART 协议常量、枚举、消息结构定义   |
| `main/lock_control/lock_protocol.cc` | 消息编解码、校验和计算、BCD密码编码 |
| `main/lock_control/lock_control.h`   | 锁控服务类接口定义                  |
| `main/lock_control/lock_control.cc`  | UART 通信、事件回调、命令发送实现   |
| `main/lock_control/CMakeLists.txt`   | 组件构建配置                        |

#### 修改文件

| 文件                                                               | 修改内容                                   |
| ------------------------------------------------------------------ | ------------------------------------------ |
| `main/application.h`                                               | 添加锁控服务成员和人脸识别相关方法声明     |
| `main/application.cc`                                              | 实现人脸识别触发、结果处理、事件处理逻辑   |
| `main/boards/bread-compact-wifi-s3cam/config.h`                    | 添加 UART 引脚定义                         |
| `main/boards/bread-compact-wifi-s3cam/compact_wifi_board_s3cam.cc` | 添加锁控服务初始化和 GetLockControl() 方法 |
| `main/CMakeLists.txt`                                              | 添加 lock_control 组件依赖                 |

#### 具体变更

**GetLockControl() 方法添加 override 关键字**

- 文件: `compact_wifi_board_s3cam.cc`
- 变更: `xiaozhi::LockControlService* GetLockControl()` → `xiaozhi::LockControlService* GetLockControl() override`

**人脸识别触发状态检查**

- 文件: `main/application.cc`
- 位置: `TriggerFaceRecognition()` 函数
- 说明: 允许在 Idle 或 Listening 状态时触发人脸识别

**WebSocket 人脸识别图像发送优化**

- 文件: `main/protocols/websocket_protocol.cc`
- 变更: 移除 Base64 编码，改用 BinaryProtocol2 二进制格式直接发送
- 优势: 减少约 33% 数据膨胀，提高传输效率

**MQTT 人脸识别图像发送优化**

- 文件: `main/protocols/mqtt_protocol.cc`
- 变更: 与 WebSocket 保持一致，使用 BinaryProtocol2 格式

---

## 2025-12-08

### 实时视频对讲功能实现

#### 新增模块

| 目录            | 说明         |
| --------------- | ------------ |
| `main/video/`   | 视频流服务   |
| `main/monitor/` | 监控模式服务 |

#### 新增文件

| 文件                                   | 说明                 |
| -------------------------------------- | -------------------- |
| `main/video/video_stream_service.h/cc` | 视频流捕获和队列管理 |
| `main/video/jpeg_frame.h`              | JPEG 帧结构定义      |
| `main/monitor/monitor_service.h/cc`    | 监控模式管理         |

#### 修改文件

| 文件                                     | 修改内容                  |
| ---------------------------------------- | ------------------------- |
| `main/device_state.h`                    | 新增监控模式状态枚举      |
| `main/application.h/cc`                  | 新增监控模式管理方法      |
| `main/protocols/protocol.h`              | 新增 SendVideo() 虚方法   |
| `main/protocols/websocket_protocol.h/cc` | 实现视频发送              |
| `main/protocols/mqtt_protocol.h/cc`      | 实现视频发送              |
| `main/boards/common/esp32_camera.h/cc`   | 新增 CaptureJpeg() 等方法 |

#### 协议扩展

- 使用 BinaryProtocol2 的 `reserved` 字段区分音频和视频
- `reserved = 0`: 音频数据（OPUS）
- `reserved = (width << 16) | height`: 视频数据（JPEG）

---

## 2025-12-10 (续)

### 锁控协议 v2.0 重构

#### 修改文件

| 文件                                 | 修改内容               |
| ------------------------------------ | ---------------------- |
| `main/lock_control/lock_protocol.cc` | 协议实现重构，简化代码 |

#### 具体变更

**代码简化**

- 位置: `LockProtocol` 类全部方法
- 变更: 移除冗长的函数注释，保留简洁实现
- 说明: 代码逻辑清晰，注释精简化

**删除旧版 ACK 函数**

- 位置: `BuildAck()` 函数
- 变更: 删除旧版 ACK 构建函数（使用 `MsgCategory::ACK` 类别）
- 说明: 新版协议 v2.0 使用 `CAT_SYS (0x00)` 类别，需实现 `BuildAckOk()` 和 `BuildAckErr()`

**删除 BCD 密码编解码**

- 位置: `EncodePassword()` / `DecodePassword()` 函数
- 变更: 删除 BCD 格式密码编解码实现
- 说明: 新版协议使用 Hex 整数格式（如 123456 → 0x01E240），需实现 `EncodePasswordHex()` 和 `DecodePasswordHex()`

#### ⚠️ 待补充实现

头文件 `lock_protocol.h` 中声明但 `.cc` 中缺失的函数：

- `BuildAckOk(uint8_t orig_type)` - 构建成功应答
- `BuildAckErr(uint8_t orig_type, AckError error)` - 构建错误应答
- `EncodePasswordHex(uint32_t password)` - Hex 格式密码编码
- `DecodePasswordHex(const std::array<uint8_t, 3>& data)` - Hex 格式密码解码

---

## Git 提交历史

| Commit    | 说明                                    |
| --------- | --------------------------------------- |
| `fb9e0b3` | 监控模式功能基本实现，性能待优化 (HEAD) |
| `13ed6cf` | 音视频传输功能 ESP32 端实现             |
| `ff33336` | 实施规划完成，准备开始修改代码          |
| `9e61cdf` | 整理了一下文档资料                      |
| `e153467` | 新的开始，之前的分支废弃                |

---

## 版本说明

- **当前分支**: `cat-eye-lock-feature-2`
- **编译状态**: ✅ 通过
- **功能状态**: 核心功能完成，待硬件测试

---

## 2025-12-10 (补光灯控制)

### 修改文件

| 文件                                | 修改内容               |
| ----------------------------------- | ---------------------- |
| `main/lock_control/lock_protocol.h` | 新增补光灯控制命令枚举 |

### 具体变更

**新增 CMD_LIGHT 命令**

- 位置: `CmdType` 枚举（第 77 行）
- 变更: 新增 `CMD_LIGHT = 0x14` 补光灯控制命令
- 说明: 对应 STM32 协议中的 `CMD_LIGHT (0x14)` 命令

### 功能说明

支持 ESP32 向 STM32 发送补光灯控制指令：

- `D0 = 0x00`: 自动模式（恢复光照传感器控制）
- `D0 = 0x01`: 强制开灯
- `D0 = 0x02`: 强制关灯

对应服务器端 `dev_control` 消息：

```json
{
  "type": "dev_control",
  "msg_id": "cmd_2003",
  "target": "light",
  "action": "on" // "on", "off", "auto"
}
```

---

## 2025-12-11

### v5.0 协议接口扩展

#### 修改文件

| 文件                        | 修改内容                 |
| --------------------------- | ------------------------ |
| `main/protocols/protocol.h` | 新增 v5.0 协议虚函数声明 |

#### 具体变更

**新增 ACK 响应方法**

- 位置: `Protocol` 类公共方法区域
- 方法: `virtual void SendAck(const std::string& msg_id, int code = 0, const std::string& msg = "OK")`
- 说明: 用于响应服务器下发的控制命令

**新增状态与事件上报方法**

- 位置: `Protocol` 类公共方法区域
- 方法:
  - `SendStatusReport(int battery, int lux, int lock_state, int light_state)` - 设备状态上报
  - `SendEventReport(const std::string& event, int param = 0)` - 事件上报（门铃、人体感应等）
  - `SendLogReport(const std::string& method, int uid, bool result, int fail_count = 0)` - 开锁日志上报

**新增心跳方法**

- 位置: `Protocol` 类公共方法区域
- 方法: `virtual void SendHeartbeat()`
- 说明: 预留功能，用于保持连接活跃

#### 功能说明

为 v5.0 通信协议规范提供基类接口定义，子类（`WebSocketProtocol`、`MqttProtocol`）需实现具体的消息构建和发送逻辑。

#### ⚠️ 待补充实现

需要在以下文件中实现这些虚函数：

- `main/protocols/protocol.cc` - 基类默认实现
- `main/protocols/websocket_protocol.cc` - WebSocket 实现
- `main/protocols/mqtt_protocol.cc` - MQTT 实现

---

## 2025-12-11 (用户管理结果上报)

### 修改文件

| 文件                        | 修改内容                       |
| --------------------------- | ------------------------------ |
| `main/protocols/protocol.h` | 新增用户管理结果上报虚函数声明 |

### 具体变更

**新增 SendUserMgmtResult 方法**

- 位置: `Protocol` 类公共方法区域（第 97-99 行，`SendHeartbeat()` 之后）
- 方法签名: `virtual void SendUserMgmtResult(const std::string& category, const std::string& command, bool result, int val, const std::string& msg)`
- 说明: v5.0 协议新增，用于上报用户管理操作（指纹/NFC/密码）的执行结果

### 功能说明

支持 ESP32 向服务器上报用户管理操作结果：

- `category`: 操作类别（`finger`/`nfc`/`password`）
- `command`: 操作命令（`add`/`del`/`clear`/`query`/`set`）
- `result`: 操作是否成功
- `val`: 返回值（如指纹数量、用户ID等）
- `msg`: 结果描述信息

对应服务器端期望的响应格式：

```json
{
  "type": "user_mgmt_result",
  "category": "finger",
  "command": "add",
  "result": true,
  "val": 5,
  "msg": "指纹录入成功"
}
```

### 备注

该方法已在 `WebsocketProtocol` 类中实现（`websocket_protocol.cc` 第 380-396 行）。

---

## 2025-12-11 (智能门锁 JSON 消息处理)

### 修改文件

| 文件                 | 修改内容                           |
| -------------------- | ---------------------------------- |
| `main/application.h` | 新增智能门锁 JSON 消息处理方法声明 |

### 具体变更

**新增 HandleSmartLockJsonMessage 方法**

- 位置: `Application` 类私有方法区域（第 168-170 行，`HandleDoorNotClosed()` 之后）
- 方法签名: `bool HandleSmartLockJsonMessage(const cJSON* root, const char* type)`
- 返回值: `true` 表示消息已处理，`false` 表示未识别的消息类型

### 功能说明

用于集中处理 v5.0 协议中服务器下发的智能门锁扩展 JSON 消息类型：

- `face_result` - 人脸识别结果
- `lock_control` - 开锁控制命令
- `dev_control` - 设备控制命令（补光灯等）
- `user_mgmt` - 用户管理命令（指纹/NFC/密码）
- `heartbeat_ack` - 心跳响应

### 备注

该方法在 `application.cc` 的 `OnIncomingJson` 回调中被调用，用于分离智能门锁相关消息的处理逻辑，提高代码可维护性。

---

## 2025-12-11 (锁控协议文档优化)

### 修改文件

| 文件                                | 修改内容                  |
| ----------------------------------- | ------------------------- |
| `main/lock_control/lock_protocol.h` | 添加 Doxygen 风格文档注释 |

### 具体变更

**文件头注释格式优化**

- 位置: 文件头部（第 1-19 行）
- 变更: 版本号独立为 `@version 2.0` 标签，协议帧格式改用 ASCII 表格形式展示
- 说明: 提高文档可读性

**枚举类型文档注释**

- 位置: 所有 `enum class` 定义
- 变更: 为每个枚举类型添加 `@brief` 说明，每个枚举值添加 `///< 说明` 行内注释
- 涉及枚举:
  - `MsgCategory` - 消息类别
  - `SysType` - 系统消息类型
  - `AckError` - ACK 错误码
  - `CmdType` - 控制命令类型
  - `LockMode` - 锁控模式
  - `OledIcon` - OLED 显示图标
  - `BeepFreq` - 蜂鸣器频率
  - `LightMode` - 补光灯控制模式
  - `RptType` - 上报消息类型
  - `EventId` - 事件 ID
  - `UnlockMethod` - 开锁方式
  - `UserFpCmd` / `UserNfcCmd` / `UserPwdCmd` - 用户管理命令
  - `FpSubCmd` - 指纹/NFC 子命令
  - `FpRespStatus` - 指纹录入响应状态

**结构体文档注释**

- 位置: `LockMessage` 结构体（第 230-265 行）
- 变更: 添加结构体说明和每个成员的 `///< 说明` 注释，辅助方法添加 `/** 说明 */` 注释

**类方法文档注释**

- 位置: `LockProtocol` 类（第 267-353 行）
- 变更: 为所有静态方法添加完整的 Doxygen 文档
- 包含: `@brief`、`@param`、`@return`、`@deprecated` 等标签
- 涉及方法:
  - `BuildMessage()` - 构建协议消息
  - `ParseMessage()` - 解析协议消息
  - `CalculateChecksum()` - 计算校验和
  - `BuildAckOk()` / `BuildAckErr()` - 构建应答消息
  - `EncodePasswordHex()` / `DecodePasswordHex()` - Hex 格式密码编解码
  - `EncodePassword()` / `DecodePassword()` - BCD 格式密码编解码（标记为 deprecated）

### 功能说明

本次变更为纯文档优化，不涉及任何功能代码修改。主要目的：

1. 提高代码可读性和可维护性
2. 支持 Doxygen 自动生成 API 文档
3. 方便 IDE 智能提示显示参数说明
4. 为 STM32 端开发提供清晰的协议参考

---

## 2025-12-11 (锁控服务文档优化)

### 修改文件

| 文件                               | 修改内容                  |
| ---------------------------------- | ------------------------- |
| `main/lock_control/lock_control.h` | 添加 Doxygen 风格文档注释 |

### 具体变更

**文件头注释格式优化**

- 位置: 文件头部（第 1-20 行）
- 变更: `@version` 独立为标签，新增 `@code` 使用示例代码块
- 说明: 提供快速上手参考

**类级别文档注释**

- 位置: `LockControlService` 类定义前（第 27-37 行）
- 变更: 添加类功能概述，列出主要能力分类
- 说明: 便于理解类的整体职责

**公共方法文档注释**

- 位置: 所有 `public` 方法声明
- 变更: 为每个方法添加完整的 Doxygen 文档
- 包含: `@brief`（简述）、`@param`（参数）、`@return`（返回值）
- 涉及方法:
  - 生命周期: `Start()`, `Stop()`, `IsRunning()`
  - 控制命令: `SendLock()`, `SendUnlock()`, `SendOledIcon()`, `SendBeep()`, `SendLight()` 等
  - 查询命令: `QuerySensors()`, `QueryStatus()`
  - 用户管理: `FingerprintEnroll()`, `NfcEnroll()`, `SetPassword()` 等
  - 心跳: `SendPing()`
  - 回调: `SetEventCallback()`

**私有成员文档注释**

- 位置: `private` 成员区域
- 变更: 为成员变量和私有方法添加 `///< 说明` 注释
- 涉及成员: `uart_port_`, `running_`, `rx_task_handle_`, `event_callback_`, `SendMessage()`, `RxTask()`, `RxLoop()`

### 功能说明

本次变更为纯文档优化，不涉及任何功能代码修改。主要目的：

1. 提高代码可读性和可维护性
2. 支持 Doxygen 自动生成 API 文档
3. 便于 IDE 智能提示显示参数说明
4. 与 `lock_protocol.h` 文档风格保持一致

---

## 2025-12-11 (锁控服务代码风格优化)

### 修改文件

| 文件                                | 修改内容                           |
| ----------------------------------- | ---------------------------------- |
| `main/lock_control/lock_control.cc` | 代码风格优化，添加注释和区域分隔符 |

### 具体变更

**文件头注释格式调整**

- 位置: 文件头部（第 1-4 行）
- 变更: `@brief` 和 `@version` 分离为独立标签
- 说明: 与 `lock_protocol.cc` 和 `lock_control.h` 保持一致

**添加区域分隔注释**

- 位置: 各功能区域开始处
- 变更: 使用 `// ============` 风格分隔符划分代码区域
- 涉及区域:
  - 构造与析构
  - 生命周期管理
  - 接收任务
  - 消息发送
  - 控制命令实现
  - 用户管理 - 指纹/NFC/密码
  - 心跳

**添加行内注释**

- 位置: `Start()` 方法内部
- 变更: 为 UART 配置、驱动安装、引脚设置、任务创建等关键步骤添加注释

**添加 Doxygen 文档注释**

- 位置: `RxLoop()` 方法（第 118-123 行）、`SendMessage()` 方法（第 207-211 行）
- 变更: 添加 `@brief` 功能说明

**统一分隔符格式**

- 位置: 所有区域分隔符
- 变更: `// =========` → `// ============`（76 个等号）
- 说明: 与 `lock_control.h` 风格保持一致

### 功能说明

本次变更为纯代码风格优化，不涉及任何功能代码修改。主要目的：

1. 提高代码可读性
2. 与 `lock_protocol.h`、`lock_control.h` 文档风格保持一致
3. 便于代码导航和维护

---

## 2025-12-11 (通信协议基类文档优化)

### 修改文件

| 文件                        | 修改内容                  |
| --------------------------- | ------------------------- |
| `main/protocols/protocol.h` | 添加 Doxygen 风格文档注释 |

### 具体变更

**文件头注释新增**

- 位置: 文件头部（第 1-15 行）
- 变更: 新增 `@file`、`@brief` 标签，说明文件功能和具体实现类
- 说明: 描述协议支持的功能（音频/视频/人脸识别/JSON/v5.0 扩展）

**添加区域分隔注释**

- 位置: 各功能区域开始处
- 变更: 使用 `// ============` 风格分隔符划分代码区域
- 涉及区域: 数据结构定义、枚举定义、协议抽象基类

**结构体文档注释**

- 位置: `AudioStreamPacket`、`BinaryProtocol2`、`BinaryProtocol3` 结构体
- 变更: 添加 `@brief` 说明和成员 `///< 说明` 注释
- 说明: `BinaryProtocol2` 详细说明 type 字段含义（0=音频/视频, 1=JSON, 2=人脸识别）

**枚举文档注释**

- 位置: `AbortReason`、`ListeningMode` 枚举
- 变更: 添加 `@brief` 和枚举值 `///< 说明` 注释

**Protocol 类文档注释**

- 位置: `Protocol` 类定义（第 89-307 行）
- 变更:
  - 类级别 `@brief` 说明
  - 方法按功能分组（属性访问、回调注册、核心接口、语音交互、v5.0 扩展、监控模式）
  - 公共方法添加 `/** 说明 */` 或完整 Doxygen 文档
  - 保护成员添加 `///< 说明` 注释
- 涉及方法: 所有公共和保护成员

### 功能说明

本次变更为纯文档优化，不涉及任何功能代码修改。主要目的：

1. 提高代码可读性和可维护性
2. 支持 Doxygen 自动生成 API 文档
3. 便于 IDE 智能提示显示参数说明
4. 与 `lock_protocol.h`、`lock_control.h` 文档风格保持一致

---

## 2025-12-11 (WebSocket 协议头文件文档优化)

### 修改文件

| 文件                                  | 修改内容                  |
| ------------------------------------- | ------------------------- |
| `main/protocols/websocket_protocol.h` | 添加 Doxygen 风格文档注释 |

### 具体变更

**文件头注释新增**

- 位置: 文件头部（第 1-16 行）
- 变更: 新增 `@file`、`@brief` 标签，说明协议支持的功能和版本历史
- 说明: 描述 WebSocket 协议支持的功能（音频/视频/人脸识别/JSON/v5.0 扩展）

**添加区域分隔注释**

- 位置: 类方法声明区域
- 变更: 使用 `// =========` 风格分隔符划分代码区域
- 涉及区域: Protocol 接口实现、v5.0 协议扩展方法

**常量和宏文档注释**

- 位置: `WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT`、`MSG_ID_CACHE_SIZE`
- 变更: 添加 `/** 说明 */` 注释

**类级别文档注释**

- 位置: `WebsocketProtocol` 类定义前（第 32-36 行）
- 变更: 添加 `@brief` 说明类功能

**公共方法文档注释**

- 位置: 所有 v5.0 协议扩展方法
- 变更: 为每个方法添加完整的 Doxygen 文档
- 包含: `@brief`（简述）、`@param`（参数）、`@return`（返回值）
- 涉及方法: `SendAck()`、`SendStatusReport()`、`SendEventReport()`、`SendLogReport()`、`SendHeartbeat()`、`SendUserMgmtResult()`

**私有成员文档注释**

- 位置: `private` 成员区域
- 变更: 为成员变量添加 `///< 说明` 注释，为私有方法添加完整 Doxygen 文档
- 涉及成员: `event_group_handle_`、`websocket_`、`version_`、`msg_id_queue_`、`msg_id_set_`
- 涉及方法: `ParseServerHello()`、`SendText()`、`GetHelloMessage()`、`IsDuplicateMsgId()`、`AddMsgIdToCache()`

### 功能说明

本次变更为纯文档优化，不涉及任何功能代码修改。主要目的：

1. 提高代码可读性和可维护性
2. 支持 Doxygen 自动生成 API 文档
3. 便于 IDE 智能提示显示参数说明
4. 与 `protocol.h`、`lock_protocol.h`、`lock_control.h` 文档风格保持一致

---

## 2025-12-11 (WebSocket 协议实现文件文档优化)

### 修改文件

| 文件                                   | 修改内容                                |
| -------------------------------------- | --------------------------------------- |
| `main/protocols/websocket_protocol.cc` | 添加 Doxygen 风格文档注释，统一中文注释 |

### 具体变更

**文件头注释新增**

- 位置: 文件头部（第 1-10 行）
- 变更: 新增 `@file`、`@brief` 标签，说明实现的功能（音频流、视频流、人脸识别、JSON 消息）

**添加区域分隔注释**

- 位置: 各功能区域开始处
- 变更: 使用 `// ============` 风格分隔符划分代码区域
- 涉及区域:
  - 构造与析构
  - 生命周期管理
  - 音频发送
  - 视频发送
  - 人脸识别图像发送
  - 文本消息发送
  - 通道管理
  - 握手消息
  - msg_id 防重放
  - v5.0 协议扩展方法

**方法文档注释**

- 位置: 关键方法定义前
- 变更: 添加 `@brief` 功能说明
- 涉及方法:
  - `SendAudio()` - 说明不同协议版本和模式的封装格式选择
  - `SendVideo()` - 说明 BinaryProtocol2 格式和 reserved 字段编码
  - `SendFaceRecognition()` - 说明 type=2 标识和服务器区分机制
  - `OpenAudioChannel()` - 说明握手流程（NVS 配置、连接、Hello 消息）
  - `GetHelloMessage()` - 说明客户端信息内容
  - `ParseServerHello()` - 说明会话 ID 和音频参数提取

**注释和日志中文化**

- 位置: 全文件
- 变更: 将所有英文注释和 `ESP_LOG*` 日志消息翻译为中文
- 示例:
  - `"Cannot send video: websocket not connected"` → `"无法发送视频: WebSocket 未连接"`
  - `"Connecting to websocket server"` → `"连接 WebSocket 服务器"`
  - `"Failed to receive server hello"` → `"等待服务器 Hello 超时"`

### 功能说明

本次变更为纯文档和代码风格优化，不涉及任何功能代码修改。主要目的：

1. 提高代码可读性和可维护性
2. 支持 Doxygen 自动生成 API 文档
3. 与 `protocol.h`、`websocket_protocol.h` 文档风格保持一致
4. 统一使用中文注释和日志，符合项目规范

---

## 2025-12-11 (锁控事件处理代码重构)

### 修改文件

| 文件                  | 修改内容                                                  |
| --------------------- | --------------------------------------------------------- |
| `main/application.cc` | 重构 `HandleLockEvent()` 函数，拆分为多个职责单一的子函数 |

### 具体变更

**HandleLockEvent() 函数重构**

- 位置: `Application` 类锁控相关函数区域（第 1042-1327 行）
- 变更: 将原本 246 行的大型函数拆分为 4 个独立函数
- 说明: 提高代码可读性和可维护性

**新增 HandleLockReportMessage() 函数**

- 位置: 第 1085-1160 行
- 功能: 处理 STM32 上报消息 (CAT = 0x01)
- 包含: 事件上报、开锁日志、环境数据、状态上报、密码查询结果

**新增 HandleLockSystemMessage() 函数**

- 位置: 第 1168-1178 行
- 功能: 处理系统消息 (CAT = 0x00)
- 包含: ACK_OK、ACK_ERR、PONG 响应

**新增 HandleLockUserMessage() 函数**

- 位置: 第 1186-1260 行
- 功能: 处理用户管理反馈消息 (CAT = 0x03)
- 包含: 指纹录入反馈、NFC 录入反馈
- 说明: v5.0 协议转发用户管理结果到服务器

**新增 GetUnlockMethodString() 函数**

- 位置: 第 1268-1283 行
- 功能: 将开锁方式枚举值转换为字符串
- 返回值: `finger`/`nfc`/`pwd`/`remote`/`key`/`unknown`
- 说明: 用于服务器上报时的方法字段

**添加 Doxygen 文档注释**

- 位置: 所有新增函数定义前
- 变更: 添加 `@brief`、`@param`、`@return` 等标签
- 说明: 详细描述函数功能和参数含义

**日志消息中文化**

- 位置: `HandleLockEvent()` 函数
- 变更:
  - `"Received lock event"` → `"收到锁控事件"`
  - `"Ignoring lock event in monitor mode"` → `"监控模式中，忽略锁控事件"`

### 功能说明

本次变更为纯代码重构，不涉及任何功能逻辑修改。主要目的：

1. 将大型函数拆分为职责单一的小函数，符合单一职责原则
2. 提高代码可读性和可维护性
3. 便于单元测试和代码复用
4. 添加详细的中文注释和 Doxygen 文档
5. 与项目其他模块的代码风格保持一致

### 重构前后对比

| 指标       | 重构前    | 重构后                           |
| ---------- | --------- | -------------------------------- |
| 主函数行数 | ~246 行   | ~40 行                           |
| 函数数量   | 1 个      | 5 个                             |
| 嵌套层级   | 最深 4 层 | 最深 2 层                        |
| 代码复用   | 无        | `GetUnlockMethodString()` 可复用 |

---

## 2025-12-11 (人脸识别函数文档优化)

### 修改文件

| 文件                  | 修改内容                                                |
| --------------------- | ------------------------------------------------------- |
| `main/application.cc` | `TriggerFaceRecognition()` 函数文档优化和代码健壮性改进 |

### 具体变更

**添加区域分隔注释**

- 位置: `TriggerFaceRecognition()` 函数前（第 1325-1327 行）
- 变更: 新增 `// ============ 人脸识别功能 ============` 区域分隔符

**添加 Doxygen 文档注释**

- 位置: `TriggerFaceRecognition()` 函数定义前（第 1329-1341 行）
- 变更: 添加完整的函数文档，描述人脸识别完整流程（5 个步骤）
- 说明: 包含注意事项，提示应在主循环中调用避免阻塞 UART 任务

**日志消息中文化**

- 位置: `TriggerFaceRecognition()` 函数内部
- 变更: 将所有 `ESP_LOG*` 日志消息从英文翻译为中文
- 示例:
  - `"Face recognition triggered"` → `"触发人脸识别"`
  - `"Camera not available"` → `"摄像头不可用，中止人脸识别"`
  - `"Photo captured successfully"` → `"拍照成功"`

**注释中文化**

- 位置: 函数内部所有注释
- 变更: 将英文注释翻译为中文，并精简表述

**修复内存泄漏/状态标志问题**

- 位置: 多处错误返回路径
- 变更: 在以下位置添加 `face_recognition_in_progress_ = false;`：
  - 内存不足时返回前（第 1361 行）
  - 摄像头类型不支持时返回前（第 1390 行）
  - ESP32 不支持时返回前（第 1414 行）
- 说明: 确保异常退出时正确清除进行中标志，避免后续触发被永久阻塞

### 功能说明

本次变更为代码质量优化，主要目的：

1. 添加详细的函数文档，便于理解人脸识别完整流程
2. 统一使用中文注释和日志，符合项目规范
3. 修复状态标志未正确清除的问题，提高代码健壮性
4. 与项目其他模块的代码风格保持一致

---

## 2025-12-11 (智能门锁消息处理函数文档优化)

### 修改文件

| 文件                  | 修改内容                                              |
| --------------------- | ----------------------------------------------------- |
| `main/application.cc` | `HandleSmartLockJsonMessage()` 函数文档和代码风格优化 |

### 具体变更

**添加 Doxygen 文档注释**

- 位置: `HandleSmartLockJsonMessage()` 函数定义前（第 1551-1570 行）
- 变更: 添加完整的函数文档，列出支持的 5 种消息类型
- 说明: 包含 `@brief`、`@param`、`@return` 标签

**添加区域分隔注释**

- 位置: 函数内部各消息类型处理逻辑前
- 变更: 使用 `// ---------` 风格分隔符划分代码区域
- 涉及区域:
  - 人脸识别结果 (face_result / face_recognition)
  - 锁控命令 (lock_control)
  - 硬件外设控制 (dev_control)
  - 用户管理 (user_mgmt)
  - 心跳响应 (heartbeat_ack)

**日志消息中文化**

- 位置: 函数内部所有 `ESP_LOG*` 调用
- 变更:
  - `"Lock control command"` → `"锁控命令"`
  - `"Dev control"` → `"外设控制"`
  - `"User mgmt"` → `"用户管理"`
  - `"Lock control service not available"` → `"锁控服务不可用"`
  - `"Received heartbeat ack"` → `"收到心跳响应"`

**添加行内注释**

- 位置: 各命令处理分支
- 变更: 为每个命令添加参数说明注释
- 示例:
  - `// 开锁命令，可选 duration 参数`
  - `// 蜂鸣器控制：count（次数）、mode（short/long/alarm）`
  - `// 指纹管理：add/del/clear/query`

### 功能说明

本次变更为纯代码风格和文档优化，不涉及任何功能逻辑修改。主要目的：

1. 添加详细的函数文档，便于理解消息处理流程
2. 使用区域分隔符提高代码可读性
3. 统一使用中文日志，符合项目规范
4. 添加行内注释说明各命令参数含义
5. 与项目其他模块的代码风格保持一致

---

## 2025-12-11 (锁控方法声明补充)

### 修改文件

| 文件                 | 修改内容                                    |
| -------------------- | ------------------------------------------- |
| `main/application.h` | 补充锁控相关方法声明，添加 Doxygen 文档注释 |

### 具体变更

**添加区域分隔注释**

- 位置: 锁控相关方法区域（第 162-164 行）
- 变更: 新增 `// =========` 风格分隔符和区域标题

**新增方法声明**

- 位置: `Application` 类私有方法区域（第 166-180 行）
- 变更: 补充以下 4 个方法声明（与 `.cc` 文件中已实现的函数对应）：
  - `HandleLockReportMessage(const xiaozhi::LockMessage& msg)` - 处理上报消息 (CAT = 0x01)
  - `HandleLockSystemMessage(const xiaozhi::LockMessage& msg)` - 处理系统消息 (CAT = 0x00)
  - `HandleLockUserMessage(const xiaozhi::LockMessage& msg)` - 处理用户管理反馈消息 (CAT = 0x03)
  - `GetUnlockMethodString(uint8_t method)` - 获取开锁方式字符串

**添加 Doxygen 文档注释**

- 位置: 所有锁控相关方法声明
- 变更: 为每个方法添加 `/** 说明 */` 注释
- 涉及方法: `HandleLockEvent()`、`TriggerFaceRecognition()`、`HandleFaceRecognitionResult()`、`HandleTamperAlert()`、`HandleDoorNotClosed()`、`HandleSmartLockJsonMessage()`

**HandleSmartLockJsonMessage 文档增强**

- 位置: 第 191-197 行
- 变更: 添加完整 Doxygen 文档，包含 `@brief`、`@param`、`@return` 标签

### 功能说明

本次变更为头文件声明补充和文档优化：

1. 补充 `.cc` 文件中已实现但 `.h` 文件中缺失的方法声明
2. 添加 Doxygen 风格文档注释，与项目其他模块保持一致
3. 使用区域分隔符提高代码可读性

---

## 2025-12-11 (msg_id 防重放检查)

### 修改文件

| 文件                                   | 修改内容                                 |
| -------------------------------------- | ---------------------------------------- |
| `main/protocols/websocket_protocol.cc` | 在 JSON 消息处理中添加 msg_id 防重放检查 |

### 具体变更

**新增 msg_id 防重放检查逻辑**

- 位置: `OnData` 回调函数，JSON 消息处理分支（非 hello 消息），第 314-326 行
- 变更: 在调用 `on_incoming_json_` 回调前，新增 msg_id 重复检查
- 逻辑:
  1. 检查 JSON 消息是否包含 `msg_id` 字段
  2. 如果 msg_id 已存在于缓存中，记录警告日志并丢弃该消息
  3. 如果是新的 msg_id，将其添加到缓存中
  4. 只有通过检查的消息才会传递给 `on_incoming_json_` 回调

**新增代码片段**

```cpp
// v5.0 协议：msg_id 防重放检查
auto msg_id = cJSON_GetObjectItem(root, "msg_id");
if (cJSON_IsString(msg_id)) {
    std::string msg_id_str = msg_id->valuestring;
    if (IsDuplicateMsgId(msg_id_str)) {
        ESP_LOGW(TAG, "重复的 msg_id，忽略消息: %s", msg_id_str.c_str());
        cJSON_Delete(root);
        return;
    }
    // 添加到缓存
    AddMsgIdToCache(msg_id_str);
}
```

### 功能说明

实现 v5.0 通信协议规范中的 msg_id 防重放机制：

- 服务器下发的关键指令携带唯一 `msg_id`
- ESP32 维护最近 100 条 msg_id 缓存（FIFO 淘汰策略）
- 重复的 msg_id 消息直接丢弃，防止指令被重复执行
- 符合协议规范第 7.1 节"防重放攻击"安全要求

### 相关代码

| 方法                 | 位置                  | 说明                            |
| -------------------- | --------------------- | ------------------------------- |
| `IsDuplicateMsgId()` | websocket_protocol.cc | 检查 msg_id 是否在缓存中        |
| `AddMsgIdToCache()`  | websocket_protocol.cc | 添加 msg_id 到缓存（FIFO 淘汰） |
| `MSG_ID_CACHE_SIZE`  | websocket_protocol.h  | 缓存大小常量（100）             |
| `msg_id_queue_`      | websocket_protocol.h  | FIFO 队列，用于淘汰旧 ID        |
| `msg_id_set_`        | websocket_protocol.h  | 集合，用于快速查找              |

---

## 2025-12-12 (开锁方式枚举扩展)

### 修改文件

| 文件                                | 修改内容                                             |
| ----------------------------------- | ---------------------------------------------------- |
| `main/lock_control/lock_protocol.h` | 扩展 `UnlockMethod` 枚举，新增人脸和临时密码开锁方式 |

### 具体变更

**枚举文档注释增强**

- 位置: `UnlockMethod` 枚举定义前（第 168-178 行）
- 变更: 添加与服务器协议 v5.0 对应的 method 字段说明
- 说明: 列出所有开锁方式与服务器协议字段的映射关系

**新增枚举值**

- 位置: `UnlockMethod` 枚举（第 179-187 行）
- 变更:
  - 新增 `UNLOCK_FACE = 0x06` - 人脸开锁 (对应服务器 `face`)
  - 新增 `UNLOCK_TEMP_PWD = 0x07` - 临时密码开锁 (对应服务器 `temp_pwd`)

**现有枚举值注释更新**

- 位置: `UnlockMethod` 枚举各成员
- 变更: 在注释中添加对应的服务器协议字段名
- 示例:
  - `UNLOCK_FINGERPRINT` → `///< 指纹开锁 (finger)`
  - `UNLOCK_NFC` → `///< NFC 开锁 (nfc)`
  - `UNLOCK_PASSWORD` → `///< 密码开锁 (pwd)`
  - `UNLOCK_REMOTE` → `///< 远程开锁 (remote)`
  - `UNLOCK_KEY` → `///< 钥匙开锁 (key)`

### 功能说明

完善开锁方式枚举，与服务器协议 v5.0 规范对齐：

1. 支持人脸识别开锁日志上报（`method: "face"`）
2. 支持临时密码开锁日志上报（`method: "temp_pwd"`）
3. 便于 `GetUnlockMethodString()` 函数扩展，正确转换所有开锁方式

### 协议对应关系

| 枚举值               | 十六进制 | 服务器 method |
| -------------------- | -------- | ------------- |
| `UNLOCK_FINGERPRINT` | 0x01     | `finger`      |
| `UNLOCK_NFC`         | 0x02     | `nfc`         |
| `UNLOCK_PASSWORD`    | 0x03     | `pwd`         |
| `UNLOCK_REMOTE`      | 0x04     | `remote`      |
| `UNLOCK_KEY`         | 0x05     | `key`         |
| `UNLOCK_FACE`        | 0x06     | `face`        |
| `UNLOCK_TEMP_PWD`    | 0x07     | `temp_pwd`    |

---

## 2025-12-12 (锁控协议文件完整性修复)

### 修改文件

| 文件                                | 修改内容             |
| ----------------------------------- | -------------------- |
| `main/lock_control/lock_protocol.h` | 修复文件末尾截断问题 |

### 具体变更

**修复 DecodePassword() 方法文档注释**

- 位置: `LockProtocol` 类末尾（第 356-365 行）
- 变更: 补全被截断的 Doxygen 文档注释
- 内容:
  - `@brief` - 解码密码（BCD 格式，旧版兼容）
  - `@param data` - 3 字节编码数据
  - `@return` - 解码后的 6 位数字字符串
  - `@deprecated` - 建议使用 DecodePasswordHex

**修复文件结构**

- 变更: 添加缺失的类结束大括号 `};` 和命名空间结束标记 `}  // namespace xiaozhi`
- 说明: 确保文件语法完整，可正常编译

### 功能说明

本次变更为文件完整性修复，不涉及功能逻辑修改。修复了文件末尾被意外截断导致的语法不完整问题。

---

## 2025-12-12 (ERR_TIMEOUT 错误码修正)

### 修改文件

| 文件                                | 修改内容                    |
| ----------------------------------- | --------------------------- |
| `main/lock_control/lock_protocol.h` | 修正 `ERR_TIMEOUT` 错误码值 |

### 具体变更

**ERR_TIMEOUT 值修正**

- 位置: `AckError` 枚举（第 85 行）
- 变更: `ERR_TIMEOUT = 0x07` → `ERR_TIMEOUT = 0xFF`
- 说明: 与 STM32 端协议规范对齐

### 功能说明

修正 ACK 错误码定义，使其与 `智能猫眼门锁系统-STM32端.md` 文档中的协议规范保持一致：

| 错误码 | 宏定义          | 说明         |
| ------ | --------------- | ------------ |
| 0x01   | `ERR_BUSY`      | 设备忙       |
| 0x02   | `ERR_UNSUPPORT` | 不支持的指令 |
| 0x03   | `ERR_PARAM`     | 参数错误     |
| 0x04   | `ERR_FP_FULL`   | 指纹库已满   |
| 0x05   | `ERR_NFC_FULL`  | NFC 卡库已满 |
| 0x06   | `ERR_HARDWARE`  | 硬件故障     |
| 0xFF   | `ERR_TIMEOUT`   | 操作超时     |

此修正确保 ESP32 与 STM32 之间的错误码解析一致，避免通信时错误码误判。

---

## 2025-12-12 (人脸开锁用户ID记录)

### 修改文件

| 文件                 | 修改内容                           |
| -------------------- | ---------------------------------- |
| `main/application.h` | 新增成员变量用于记录人脸识别用户ID |

### 具体变更

**新增 last*face_user_id* 成员变量**

- 位置: `Application` 类私有成员区域（第 146 行，`face_recognition_in_progress_` 之后）
- 类型: `int`
- 初始值: `0`
- 注释: `///< 最近一次人脸识别的用户ID（用于补充开锁日志）`

### 功能说明

实现 v5.0 协议中人脸开锁日志的用户 ID 补充机制：

1. **背景**: STM32 上报人脸开锁日志时，D1 字段固定为 0x00（无法识别具体用户）
2. **解决方案**: ESP32 在收到服务器人脸识别结果（`face_result`）时，将 `user_id` 保存到 `last_face_user_id_`
3. **使用场景**: 当 STM32 上报 `RPT_UNLOCK`（开锁方式为人脸）时，ESP32 使用 `last_face_user_id_` 补充 `uid` 字段后再上报服务器

### 协议对应

参考 `智能猫眼门锁系统-ESP32与服务器通信协议规范-v5.0.md` 第 3.2.3 节：

> **人脸开锁特殊处理：** STM32 上报 D1=0x00，ESP32 根据最近一次人脸识别结果补充 user_id

### 待实现

需要在以下位置添加相关逻辑：

1. `HandleSmartLockJsonMessage()` - 处理 `face_result` 时保存 `user_id` 到 `last_face_user_id_`
2. `HandleLockReportMessage()` - 处理 `RPT_UNLOCK` 人脸开锁时使用 `last_face_user_id_`

---

## 2025-12-12 (人脸识别用户ID缓存实现)

### 修改文件

| 文件                  | 修改内容                                                |
| --------------------- | ------------------------------------------------------- |
| `main/application.cc` | 在 `HandleFaceRecognitionResult()` 中实现用户ID缓存逻辑 |

### 具体变更

**人脸识别授权通过时保存 user_id**

- 位置: `HandleFaceRecognitionResult()` 函数，授权通过分支（第 1523-1530 行）
- 变更: 从服务器返回的 `face_result` 中提取 `user_id`，保存到 `last_face_user_id_` 成员变量
- 日志: 添加 `ESP_LOGI` 记录保存的用户ID

**授权拒绝时清除缓存**

- 位置: `HandleFaceRecognitionResult()` 函数，授权拒绝分支（第 1540-1541 行）
- 变更: 将 `last_face_user_id_` 重置为 0
- 说明: 避免错误的用户ID被用于后续开锁日志

### 功能说明

实现 v5.0 协议中人脸开锁日志的用户 ID 补充机制：

1. **背景**: STM32 上报人脸开锁日志时，D1 字段固定为 0x00（无法识别具体用户）
2. **解决方案**:
   - 服务器返回 `face_result` 时，ESP32 将 `user_id` 缓存到 `last_face_user_id_`
   - 当 STM32 上报 `RPT_UNLOCK`（人脸开锁）时，使用缓存的 ID 补充 `uid` 字段
3. **安全处理**: 授权失败时清除缓存，防止错误关联

### 协议对应

参考 `智能猫眼门锁系统-ESP32与服务器通信协议规范-v5.0.md` 第 3.2.3 节：

> **人脸开锁特殊处理：** STM32 上报 D1=0x00，ESP32 根据最近一次人脸识别结果补充 user_id

### 待完成

需要在 `HandleLockReportMessage()` 中添加逻辑：当处理 `RPT_UNLOCK` 且开锁方式为人脸时，使用 `last_face_user_id_` 替换 `uid` 字段。

---

## 2025-12-12 (临时密码设置功能实现)

### 修改文件

| 文件                                | 修改内容                          |
| ----------------------------------- | --------------------------------- |
| `main/lock_control/lock_control.cc` | 新增 `SetTempPassword()` 方法实现 |

### 具体变更

**新增 SetTempPassword() 方法**

- 位置: `LockControlService` 类，`QueryPassword()` 函数之后（第 384-429 行）
- 功能: 设置临时密码及其有效期

**实现逻辑**

1. 验证密码范围（0-999999），超出则返回错误
2. 验证有效期范围（最大 16777215 秒），超出则截断并警告
3. 第1包：发送密码值（TYPE = 0x32），使用 `EncodePasswordHex()` 编码
4. 等待 50ms 让 STM32 处理
5. 第2包：发送有效期（TYPE = 0x33），3 字节大端格式

### 功能说明

实现 v5.0 协议中服务器下发临时密码的功能：

```json
{
  "type": "lock_control",
  "msg_id": "cmd_1002",
  "command": "temp_code",
  "code": "123456",
  "expires": 3600
}
```

对应 STM32 协议：

- `TEMP_PWD_SET` (0x32): 密码值，Hex 编码
- `TEMP_PWD_EXP` (0x33): 有效期秒数，3 字节大端

STM32 收到两包后启动倒计时，到期自动清除临时密码。用户使用临时密码开锁时，STM32 上报 `RPT_UNLOCK` (D0=0x06)。

### 协议对应

| 参数     | 范围            | 编码方式             |
| -------- | --------------- | -------------------- |
| password | 0 ~ 999999      | Hex 整数，3 字节大端 |
| expires  | 0 ~ 16777215 秒 | 3 字节大端           |

---

## 2025-12-12 (临时密码功能移除)

### 修改文件

| 文件                                | 修改内容                          |
| ----------------------------------- | --------------------------------- |
| `main/lock_control/lock_control.cc` | 删除 `SetTempPassword()` 方法实现 |

### 具体变更

**删除 SetTempPassword() 方法**

- 位置: `LockControlService` 类，原第 384-429 行
- 变更: 完全移除该方法的实现代码（43 行）
- 说明: 该方法用于设置临时密码及其有效期

**被删除的功能**

- 验证密码范围（0-999999）
- 验证有效期范围（最大 16777215 秒）
- 第1包：发送密码值（TYPE = 0x32，`TEMP_PWD_SET`）
- 第2包：发送有效期（TYPE = 0x33，`TEMP_PWD_EXP`）

### 功能说明

移除临时密码设置功能的实现。该功能原本对应 v5.0 协议中服务器下发临时密码的命令：

```json
{
  "type": "lock_control",
  "command": "temp_code",
  "code": "123456",
  "expires": 3600
}
```

### 备注

- 头文件 `lock_control.h` 中的方法声明可能仍然存在，需要同步删除或保留为未实现状态
- 如需恢复此功能，可参考 CHANGELOG 2025-12-12 (临时密码设置功能实现) 条目中的实现逻辑

---

## 2025-12-12 (临时密码设置功能恢复)

### 修改文件

| 文件                                | 修改内容                          |
| ----------------------------------- | --------------------------------- |
| `main/lock_control/lock_control.cc` | 恢复 `SetTempPassword()` 方法实现 |

### 具体变更

**恢复 SetTempPassword() 方法**

- 位置: `LockControlService` 类，`QueryPassword()` 方法之后（第 384-429 行）
- 功能: 设置临时密码及其有效期

**实现逻辑**

1. 验证密码范围（0-999999），超出则返回错误
2. 验证有效期范围（最大 16777215 秒），超出则返回错误
3. 第1包：发送密码值（TYPE = 0x32 `TEMP_PWD_SET`），使用 `EncodePasswordHex()` 编码
4. 等待 50ms 让 STM32 处理
5. 第2包：发送有效期（TYPE = 0x33 `TEMP_PWD_EXP`），3 字节大端格式

### 功能说明

恢复 v5.0 协议中服务器下发临时密码的功能：

```json
{
  "type": "lock_control",
  "msg_id": "cmd_1002",
  "command": "temp_code",
  "code": "123456",
  "expires": 3600
}
```

对应 STM32 协议：

- `TEMP_PWD_SET` (0x32): 密码值，Hex 编码（3 字节大端）
- `TEMP_PWD_EXP` (0x33): 有效期秒数（3 字节大端）

STM32 收到两包后启动倒计时，到期自动清除临时密码。

### 协议对应

| 参数     | 范围            | 编码方式             |
| -------- | --------------- | -------------------- |
| password | 0 ~ 999999      | Hex 整数，3 字节大端 |
| expires  | 0 ~ 16777215 秒 | 3 字节大端           |

---

## 2025-12-12 (临时密码命令处理修复)

### 修改文件

| 文件                  | 修改内容                           |
| --------------------- | ---------------------------------- |
| `main/application.cc` | 修复临时密码设置逻辑，调整枚举顺序 |

### 具体变更

**GetUnlockMethodString() 枚举顺序调整**

- 位置: 第 1335-1340 行
- 变更: 将 `UNLOCK_TEMP_PWD` (0x06) 和 `UNLOCK_FACE` (0x07) 的 case 顺序交换
- 说明: 使代码顺序与 `lock_protocol.h` 中枚举定义顺序一致

**临时密码命令处理修复**

- 位置: `HandleSmartLockJsonMessage()` 函数，`lock_control` 消息处理分支，`temp_code` 命令
- 变更:
  - 修复: 原代码错误调用 `SetPassword()` 设置全局密码
  - 修复后: 正确调用 `SetTempPassword(pwd, exp_seconds)` 设置临时密码
  - 新增: 解析 `expires` 参数，默认值 3600 秒

### 功能说明

修复服务器下发临时密码命令的处理逻辑：

```json
{
  "type": "lock_control",
  "msg_id": "cmd_1002",
  "command": "temp_code",
  "code": "123456",
  "expires": 3600
}
```

修复前：错误调用 `SetPassword()` 覆盖全局密码
修复后：正确调用 `SetTempPassword()` 设置临时密码及有效期

临时密码通过两包 UART 消息发送给 STM32：

- 第1包 (TYPE=0x32): 密码值
- 第2包 (TYPE=0x33): 有效期秒数

---

## 2026-01-14 (锁控协议空值常量)

### 修改文件

| 文件                                | 修改内容                               |
| ----------------------------------- | -------------------------------------- |
| `main/lock_control/lock_protocol.h` | 新增协议空值常量 `LOCK_PROTOCOL_EMPTY` |

### 具体变更

**新增 LOCK_PROTOCOL_EMPTY 常量**

- 位置: 协议常量定义区域（`LOCK_PROTOCOL_DATA_LEN` 之后，约第 41 行）
- 类型: `constexpr uint8_t`
- 值: `0xFF`
- 注释: `///< 协议空值（未使用字段填充），用于 v2.4+ 协议兼容`

### 功能说明

这是锁控协议升级（v2.1 → v2.7）的第一步实现，对应 `.kiro/specs/lock-control-upgrade/tasks.md` 中的任务 1.1。

根据 STM32 协议 v2.4 的变更，未使用的数据字段从 `0x00` 改为 `0xFF`，以便区分有效数据和未使用字段。此常量将用于：

- `BuildAckOk()` / `BuildAckErr()` 方法中填充 D1/D2 字段
- 所有发送命令方法中填充未使用的数据字段

### 协议对应

参考 `docs/my_docs/ESP32锁控协议升级需求.md` 第 3.1.6 节和 `docs/my_docs/智能猫眼门锁系统-STM32端 - 副本.md` v2.4 版本说明：

> v2.4 | 2026-01-12 | 协议空值统一改为 0xFF（原 0x00），便于区分有效数据和未使用字段

---

## 2026-01-14 (SendLock 空值字段修复)

### 修改文件

| 文件                                | 修改内容                                            |
| ----------------------------------- | --------------------------------------------------- |
| `main/lock_control/lock_control.cc` | `SendLock()` 方法 D2 字段改用 `LOCK_PROTOCOL_EMPTY` |

### 具体变更

**SendLock() 方法空值字段修复**

- 位置: `SendLock()` 函数，第 247 行
- 变更: `{static_cast<uint8_t>(mode), hold_seconds, 0x00}` → `{static_cast<uint8_t>(mode), hold_seconds, LOCK_PROTOCOL_EMPTY}`
- 说明: D2 字段从 `0x00` 改为 `0xFF`，符合 STM32 协议 v2.4+ 规范

### 功能说明

这是锁控协议升级（v2.1 → v2.7）任务 2.1 的一部分实现，对应 `.kiro/specs/lock-control-upgrade/tasks.md` 中的 `SendLock: D2 改为 LOCK_PROTOCOL_EMPTY`。

根据 STM32 协议 v2.4 的变更，未使用的数据字段统一使用 `0xFF` 填充，以便区分有效数据和未使用字段。

### 协议对应

参考 `docs/my_docs/ESP32锁控协议升级需求.md` 第 3.3.1 节：

> 所有发送命令时，未使用的字段从 `0x00` 改为 `0xFF`

---

## 2026-01-14 (两级确认机制接口声明)

### 修改文件

| 文件                                  | 修改内容                                                  |
| ------------------------------------- | --------------------------------------------------------- |
| `main/protocols/websocket_protocol.h` | 新增 `SendEsp32Ack()` 方法声明，更新 `SendAck()` 方法注释 |

### 具体变更

**SendAck() 方法注释更新**

- 位置: `WebsocketProtocol` 类 v5.0 协议扩展方法区域（第 64-70 行）
- 变更:
  - 注释从"发送 ACK 响应"改为"发送 ACK 响应（第二级确认：命令执行完成）"
  - 参数名从 `msg_id` 改为 `seq_id`，与协议规范统一
- 说明: 明确 `ack` 消息的语义为"命令执行完成"

**新增 SendEsp32Ack() 方法声明**

- 位置: `SendAck()` 方法之后（第 72-78 行）
- 方法签名: `void SendEsp32Ack(const std::string& seq_id, int code = 0, const std::string& msg = "received")`
- 功能: 发送 esp32_ack 响应（第一级确认：命令已收到）
- 参数:
  - `seq_id`: 消息序列号
  - `code`: 响应码（默认 0）
  - `msg`: 响应消息（默认 "received"）

### 功能说明

这是锁控协议升级任务 4.1 的实现，对应 `.kiro/specs/lock-control-upgrade/tasks.md` 中的"新增 SendEsp32Ack 方法"。

实现两级确认机制的接口声明：

- **esp32_ack（第一级）**: ESP32 收到服务器命令时立即发送，表示"命令已收到，开始处理"
- **ack（第二级）**: STM32 执行完成后发送，表示"命令执行完成"

### 协议对应

参考 `docs/my_docs/ESP32消息ID追溯机制改进需求.md` 第 2.2 节：

```json
// esp32_ack：命令已收到，开始处理
{
    "type": "esp32_ack",
    "seq_id": "1702234567890_0",
    "code": 0,
    "msg": "received"
}

// ack：执行完成
{
    "type": "ack",
    "seq_id": "1702234567890_0",
    "code": 0,
    "msg": "OK"
}
```

### 待实现

需要在 `websocket_protocol.cc` 中实现 `SendEsp32Ack()` 方法的具体逻辑。

---

## 2026-01-14 (密码上报方法声明)

### 修改文件

| 文件                                  | 修改内容                             |
| ------------------------------------- | ------------------------------------ |
| `main/protocols/websocket_protocol.h` | 新增 `SendPasswordReport()` 方法声明 |

### 具体变更

**新增 SendPasswordReport() 方法声明**

- 位置: `WebsocketProtocol` 类，`SendUserMgmtResult()` 方法之后，`private` 区域之前（第 122-130 行）
- 方法签名: `void SendPasswordReport(uint32_t password)`
- 功能: 发送密码上报消息到服务器
- 参数:
  - `password`: 密码值（0-999999）

**Doxygen 文档注释**

- `@brief`: 发送密码上报
- `@param password`: 密码值（0-999999）
- 说明: 发送 JSON 消息：type="password_report", ts, data.password；密码格式化为 6 位零填充字符串

### 功能说明

这是锁控协议升级任务 4.3 的实现，对应 `.kiro/specs/lock-control-upgrade/tasks.md` 中的"新增 SendPasswordReport 方法"。

用于将 STM32 返回的密码查询结果（RPT_PWD）上报到服务器，实现密码同步功能。

### 协议对应

参考 `docs/my_docs/ESP32锁控事件处理修改需求.md` 第 3.1 节：

```json
{
  "type": "password_report",
  "ts": 1702234567890,
  "data": {
    "password": "123456"
  }
}
```

### 待实现

需要在 `websocket_protocol.cc` 中实现 `SendPasswordReport()` 方法的具体逻辑。

---

## 2026-01-14 (待处理命令队列数据结构)

### 修改文件

| 文件                 | 修改内容                           |
| -------------------- | ---------------------------------- |
| `main/application.h` | 新增待处理命令队列相关数据结构定义 |

### 具体变更

**新增 PendingCommandType 枚举**

- 位置: `Application` 类定义之前，事件宏定义之后（约第 46-58 行）
- 功能: 定义待处理命令的类型，用于区分不同类型命令的 ack 发送时机
- 枚举值:
  - `IMMEDIATE`: 即时命令，收到 STM32 ACK 后即可发送 ack
  - `QUERY`: 查询命令，需要等待 STM32 ACK + 数据帧后发送 ack
  - `LONG_FLOW`: 长流程命令，需要等待 STM32 ACK + 最终结果后发送 ack

**新增 PendingCommand 结构体**

- 位置: `PendingCommandType` 枚举之后（约第 60-83 行）
- 功能: 保存服务器下发命令的上下文信息，用于关联 STM32 响应与原始 seq_id
- 成员变量:
  - `seq_id`: 原始消息 ID（来自服务器）
  - `type`: 命令类型（PendingCommandType 枚举）
  - `category`: 类别（finger/nfc/password/lock/dev/query）
  - `command`: 命令（add/del/clear/query/unlock/lock/beep/...）
  - `uart_type`: UART 命令 TYPE
  - `uart_subtype`: UART 子命令（用于指纹/NFC）
  - `timestamp_ms`: 发送时间戳（毫秒）
  - `esp32_ack_sent`: 是否已发送 esp32_ack
  - `stm32_ack_received`: 是否已收到 STM32 ACK
  - `stm32_error_code`: STM32 ACK 错误码（0 表示成功）

### 功能说明

这是锁控协议升级任务 5.1 的实现，对应 `.kiro/specs/lock-control-upgrade/tasks.md` 中的"新增数据结构定义"。

实现两级确认机制（seq_id 追溯）的基础数据结构：

- `PendingCommandType` 用于确定何时发送最终 ack
- `PendingCommand` 用于保存命令上下文，在收到 STM32 响应时能够关联原始 seq_id

### 协议对应

参考 `docs/my_docs/ESP32消息ID追溯机制改进需求.md` 第 4 节数据结构设计：

| 命令类型  | STM32 策略    | esp32_ack 时机 | ack 时机          |
| --------- | ------------- | -------------- | ----------------- |
| IMMEDIATE | 执行后 ACK    | 收到命令时     | 收到 STM32 ACK 时 |
| QUERY     | 先 ACK 后数据 | 收到命令时     | 收到数据帧时      |
| LONG_FLOW | 先 ACK 后上报 | 收到命令时     | 收到最终结果时    |

### 待实现

需要在 `Application` 类中添加以下成员和方法：

- `pending_commands_` 成员变量（`std::map<uint8_t, PendingCommand>`）
- `DetermineCommandType()` 方法
- `GetUartType()` 方法
- `CleanupPendingCommands()` 方法
- `GetTimeoutForType()` 方法
- `MapStm32ErrorCode()` 方法

---

## 2026-01-14 (EVT_TAMPER 事件处理简化)

### 修改文件

| 文件                  | 修改内容                               |
| --------------------- | -------------------------------------- |
| `main/application.cc` | 简化 `EVT_TAMPER` 撬锁报警事件处理逻辑 |

### 具体变更

**EVT_TAMPER 事件处理修改**

- 位置: `HandleLockReportMessage()` 函数，`EVT_TAMPER` case 分支（约第 1120-1125 行）
- 变更:
  - 移除 `HandleTamperAlert(param)` 调用
  - 日志级别从 `ESP_LOGI` 改为 `ESP_LOGW`
  - 添加注释说明变更原因

**修改前**:

```cpp
case static_cast<uint8_t>(xiaozhi::EventId::EVT_TAMPER):
    event_name = "tamper";
    ESP_LOGI(TAG, "撬锁报警 (级别 %d)", param);
    HandleTamperAlert(param);
    break;
```

**修改后**:

```cpp
case static_cast<uint8_t>(xiaozhi::EventId::EVT_TAMPER):
    event_name = "tamper";
    // v2.7 协议升级：移除本地报警处理，仅保留日志和服务器上报
    // STM32 已负责蜂鸣器报警，ESP32 不再重复处理
    ESP_LOGW(TAG, "撬锁报警 (级别 %d)", param);
    break;
```

### 功能说明

这是锁控协议升级（v2.1 → v2.7）任务 7.1 的实现，对应 `.kiro/specs/lock-control-upgrade/tasks.md` 中的 `修改 EVT_TAMPER 处理逻辑`。

根据 `docs/my_docs/ESP32锁控事件处理修改需求.md` 的设计原则：

- ESP32 屏幕仅用于显示摄像头画面，不显示其他状态信息
- ESP32 扬声器用于语音播报，不播放警报音效
- STM32 端负责处理蜂鸣器警报

**变更效果**：

- ❌ 移除：`HandleTamperAlert()` 调用（蜂鸣器报警 + 显示警告）
- ✅ 保留：日志记录（级别改为 WARN）
- ✅ 保留：上报服务器 `event_report: tamper`

---

## 2026-01-14 (EVT_DOOR_OPEN 事件处理修改)

### 修改文件

| 文件                  | 修改内容                          |
| --------------------- | --------------------------------- |
| `main/application.cc` | 修改 `EVT_DOOR_OPEN` 事件处理逻辑 |

### 具体变更

**EVT_DOOR_OPEN 处理逻辑修改**

- 位置: `HandleLockReportMessage()` 函数，`EVT_DOOR_OPEN` 分支
- 变更:
  - 移除 `HandleDoorNotClosed()` 调用（不再显示警告弹窗）
  - 日志级别从 `ESP_LOGI` 改为 `ESP_LOGW`
  - 新增语音播报：`audio_service_.PlaySound(Lang::Sounds::OGG_EXCLAMATION)`
  - 添加注释说明这是 v2.7 协议升级的一部分

### 功能说明

这是锁控协议升级（v2.1 → v2.7）任务 7.2 的实现，对应 `.kiro/specs/lock-control-upgrade/tasks.md` 中的 `修改 EVT_DOOR_OPEN 处理逻辑`。

根据 `docs/my_docs/ESP32锁控事件处理修改需求.md` 的设计原则：

- ESP32 屏幕仅用于显示摄像头画面，不显示其他状态信息
- ESP32 扬声器用于语音播报

**变更效果**：

- ❌ 移除：`HandleDoorNotClosed()` 调用（显示警告弹窗）
- ✅ 新增：语音播报（使用 `OGG_EXCLAMATION` 警告音效）
- ✅ 保留：日志记录（级别改为 WARN）
- ✅ 保留：上报服务器 `event_report: door_open`

**备注**：当前使用 `OGG_EXCLAMATION` 作为临时音效，后续可替换为专用的 "门未关好，请检查" 语音提示（`OGG_DOOR_NOT_CLOSED`）。

---

## 2026-01-14 (lock_control 两级确认机制实现)

### 修改文件

| 文件                  | 修改内容                                     |
| --------------------- | -------------------------------------------- |
| `main/application.cc` | 在 `lock_control` 消息处理中实现两级确认机制 |

### 具体变更

**修改位置**: `HandleSmartLockJsonMessage()` 函数，`lock_control` 消息处理分支（约第 1744-1830 行）

**1. seq_id 字段解析**

- 新增 `seq_id` 字段解析，优先使用 `seq_id`，兼容旧版 `msg_id`
- 变量名从 `msg_id_str` 改为 `seq_id_str`

**2. 第一级确认（esp32_ack）**

- 收到命令后立即发送 `esp32_ack`，表示"命令已收到，开始处理"
- 通过 `dynamic_cast` 获取 `WebsocketProtocol` 指针调用 `SendEsp32Ack()`

**3. 待处理命令保存**

- 创建 `PendingCommand` 结构体，保存命令上下文信息
- 将命令保存到 `pending_commands_` 映射表，key 为 `uart_type`
- 用于后续收到 STM32 响应时关联原始 `seq_id`

**4. 错误码调整**

- 硬件故障（锁控服务不可用）：从 `3` 改为 `6`
- 未知命令：从 `2` 改为 `4`（不支持）
- 符合 `docs/my_docs/ESP32消息ID追溯机制改进需求.md` 第 7.3 节统一错误码定义

**5. ACK 发送逻辑调整**

- 移除原来在 Schedule 末尾统一发送 ACK 的逻辑
- 错误情况（锁控服务不可用、未知命令）直接发送 `ack`
- 正常命令等待 STM32 响应后通过 `pending_commands_` 机制发送 `ack`

### 功能说明

这是锁控协议升级任务 8.1 的实现，对应 `.kiro/specs/lock-control-upgrade/tasks.md` 中的 `修改服务器命令处理入口`。

实现 `lock_control` 消息的两级确认机制：

- **第一级（esp32_ack）**: 收到命令时立即发送，表示"命令已收到，开始处理"
- **第二级（ack）**: 等待 STM32 执行完成后发送，表示"命令执行完成"

### 协议对应

参考 `docs/my_docs/ESP32消息ID追溯机制改进需求.md` 第 6.1 节即时命令流程：

```
Server                  ESP32                   STM32
 │                       │                       │
 │ lock_control          │                       │
 │ seq_id=xxx            │                       │
 │──────────────────────►│                       │
 │                       │                       │
 │ esp32_ack             │ ← 第一级：命令已收到   │
 │ seq_id=xxx            │                       │
 │◄──────────────────────│                       │
 │                       │                       │
 │                       │ 保存 pending          │
 │                       │ uart_type=0x10        │
 │                       │                       │
 │                       │ CMD_LOCK              │
 │                       │──────────────────────►│
 │                       │                       │
 │                       │ ACK_OK (TYPE=0x10)    │
 │                       │◄──────────────────────│
 │                       │                       │
 │ ack (seq_id=xxx)      │ ← 第二级：执行完成     │
 │ code=0, msg=OK        │                       │
 │◄──────────────────────│                       │
```

### 待完成

需要在 `HandleLockSystemMessage()` 中实现 STM32 ACK 响应处理逻辑：

- 匹配 `pending_commands_` 中的待处理命令
- 发送最终 `ack` 响应
- 清理 `pending_commands_` 条目

---

## 2026-01-14 (密码格式化类型修复)

### 修改文件

| 文件                                   | 修改内容                                           |
| -------------------------------------- | -------------------------------------------------- |
| `main/protocols/websocket_protocol.cc` | 修复 `SendPasswordReport()` 方法中的类型格式化警告 |

### 具体变更

**SendPasswordReport() 类型格式化修复**

- 位置: `SendPasswordReport()` 函数，第 630 行
- 变更:
  - 格式化字符串从 `"%06u"` 改为 `"%06lu"`
  - 添加类型转换 `(unsigned long)(password % 1000000)`
- 原代码: `snprintf(password_str, sizeof(password_str), "%06u", password % 1000000);`
- 新代码: `snprintf(password_str, sizeof(password_str), "%06lu", (unsigned long)(password % 1000000));`

### 功能说明

修复编译器类型警告，确保 `uint32_t` 类型的密码值在不同平台上正确格式化：

- `%u` 对应 `unsigned int`，在某些平台上可能与 `uint32_t` 大小不匹配
- `%lu` 对应 `unsigned long`，配合显式类型转换确保跨平台兼容性
- 保持密码输出为 6 位零填充格式（如 "000123"、"123456"）

---

## 2026-01-16 (NFC 响应状态枚举独立 & 开锁结果码定义)

### 修改文件

| 文件                                | 修改内容                                                                    |
| ----------------------------------- | --------------------------------------------------------------------------- |
| `main/lock_control/lock_protocol.h` | 新增 `NfcRespStatus` 枚举、`UnlockResult` 枚举和 `MAX_AUTH_FAIL_COUNT` 常量 |

### 具体变更

**FpRespStatus 枚举注释修正**

- 位置: `FpRespStatus` 枚举定义前（第 257-259 行）
- 变更: 移除注释中"同时适用于指纹和 NFC 响应"的说明
- 说明: NFC 响应状态现在有独立的枚举定义

**新增 NfcRespStatus 枚举**

- 位置: `FpRespStatus` 枚举之后（第 271-282 行）
- 功能: NFC 录入响应状态码
- 枚举值:
  - `NFC_TAP = 0x01` - 请刷卡（录入中）
  - `NFC_TAP_AGAIN = 0x02` - 请再次刷卡
  - `NFC_SUCCESS = 0x03` - 录入成功
  - `NFC_FAILED = 0x04` - 录入失败
  - `NFC_COUNT_RESP = 0x05` - 数量查询响应
  - `NFC_ALREADY_EXISTS = 0x06` - 已存在（v2.7+）
  - `NFC_ID_OCCUPIED = 0x07` - ID 被占用（v2.7+）

**新增 UnlockResult 枚举**

- 位置: `NfcRespStatus` 枚举之后（第 284-298 行）
- 功能: 开锁结果码，用于 `RPT_UNLOCK` 消息的 D2 字段
- 枚举值:
  - `UNLOCK_SUCCESS = 0x00` - 开锁成功
  - `UNLOCK_FAIL_1 ~ UNLOCK_FAIL_5` - 失败 1~5 次
  - `UNLOCK_LOCKED = 0x06` - 已锁定（D1=剩余锁定时间，单位分钟）

**新增 MAX_AUTH_FAIL_COUNT 常量**

- 位置: `UnlockResult` 枚举之后（第 300 行）
- 类型: `constexpr uint8_t`
- 值: `5`
- 说明: 最大连续失败次数，超过后触发设备锁定

### 功能说明

本次变更为协议定义优化，主要目的：

1. 将 NFC 响应状态从指纹响应中分离，使协议定义更清晰
2. 定义开锁结果码，支持认证失败计数和设备锁定状态
3. 为认证失败语音提示和设备锁定功能提供协议支持

### 协议对应

参考 `docs/my_docs/语音资源清单.md` 中的认证失败与锁定类语音播放逻辑：

- 认证失败时播放剩余次数提示
- 设备锁定时播放锁定时间提示

---

## 2026-01-16 (开锁日志语音提示实现)

### 修改文件

| 文件                  | 修改内容                                                   |
| --------------------- | ---------------------------------------------------------- |
| `main/application.cc` | 在 `RPT_UNLOCK` 消息处理中实现认证失败和设备锁定的语音提示 |

### 具体变更

**修改位置**: `HandleLockReportMessage()` 函数，`RPT_UNLOCK` case 分支（约第 1180-1220 行）

**1. 变量名语义化**

- 变更: `id` → `id_or_lock_time`
- 说明: D1 字段在不同结果下含义不同（用户ID 或 剩余锁定时间）

**2. 新增 v2.8 协议语音提示逻辑**

- 位置: 日志记录之后，服务器上报之前
- 逻辑:
  - `UNLOCK_SUCCESS`: 开锁成功，仅记录日志，无语音
  - `UNLOCK_LOCKED`: 设备已锁定，调用 `PlayLockedVoice(lock_minutes)` 播放锁定语音
  - `UNLOCK_FAIL_1 ~ UNLOCK_FAIL_5`: 认证失败，调用 `PlayAuthFailVoice(remaining)` 播放失败语音

**3. 服务器上报逻辑优化**

- 变更: 使用 `UnlockResult::UNLOCK_SUCCESS` 枚举替代硬编码 `0` 判断成功
- 变更: `id` 参数改为 `id_or_lock_time`

### 功能说明

这是 v2.8 协议升级的一部分，实现开锁结果的语音提示功能：

| 结果      | D2 值     | D1 含义  | 语音提示                      |
| --------- | --------- | -------- | ----------------------------- |
| 成功      | 0x00      | 用户ID   | 无                            |
| 失败1~5次 | 0x01~0x05 | 用户ID   | "认证失败，还剩 N 次机会"     |
| 已锁定    | 0x06      | 剩余分钟 | "设备已锁定，请 N 分钟后再试" |

### 协议对应

参考 `docs/my_docs/语音资源清单.md` 第二节"认证失败与锁定类"：

- 认证失败语音：`auth_fail_prefix.ogg` + 数字 + `auth_fail_suffix.ogg`
- 设备锁定语音：`locked_prefix.ogg` + 数字 + `locked_suffix.ogg`

### 待实现

需要在 `Application` 类中实现以下方法：

- `PlayAuthFailVoice(uint8_t remaining)` - 播放认证失败语音
- `PlayLockedVoice(uint8_t minutes)` - 播放设备锁定语音

---

## 2026-01-16 (HandleLockUserMessage 代码结构修复)

### 修改文件

| 文件                  | 修改内容                                                         |
| --------------------- | ---------------------------------------------------------------- |
| `main/application.cc` | 修复 `HandleLockUserMessage()` 函数中 NFC 反馈处理的代码结构问题 |

### 具体变更

**修改位置**: `HandleLockUserMessage()` 函数，NFC 反馈处理末尾部分（约第 1588-1610 行）

**1. 删除重复/错误代码**

- 移除重复的 `protocol_->SendUserMgmtResult()` 调用
- 移除格式混乱的大括号和 `default` 分支残留代码
- 移除多余的 `else` 分支和函数末尾多余大括号

**2. 修复两级确认机制代码结构**

- 修正代码缩进，将两级确认逻辑正确嵌套在 NFC 反馈处理内部
- 添加 `protocol_` 空指针检查：`!cmd.seq_id.empty() && protocol_`
- 确保 `pending_commands_.erase(it)` 在正确的作用域内执行

**修改前（问题代码）**:

```cpp
  is_final_result = true;
  ESP_LOGI(TAG, "NFC 指定 ID 被占用，新分配 ID=%d", val);
  break;
default:
  ESP_LOGW(TAG, "未知 NFC 反馈状态: 0x%02X", status);
  return;
}
}
else {
  ESP_LOGW(TAG, "未知用户管理反馈类型: 0x%02X", msg.type);
  return;
}

// 上报结果到服务器
protocol_->SendUserMgmtResult(category, command, result, val, result_msg);

// 两级确认机制...
```

**修改后（正确代码）**:

```cpp
  if (should_report && protocol_ && protocol_->IsAudioChannelOpened()) {
    protocol_->SendUserMgmtResult(category, command, result, val, result_msg);
  }

  // 两级确认机制：收到最终结果后发送 ack
  if (is_final_result && uart_type != 0) {
    auto it = pending_commands_.find(uart_type);
    if (it != pending_commands_.end()) {
      const PendingCommand &cmd = it->second;
      if (!cmd.seq_id.empty() && protocol_) {
        // 根据结果确定 ack code
        int ack_code = result ? 0 : 10; // 成功=0，失败=10（内部错误）
        ESP_LOGI(TAG, "用户管理命令完成，发送 ack: seq_id=%s, code=%d",
                 cmd.seq_id.c_str(), ack_code);
        protocol_->SendAck(cmd.seq_id, ack_code, result_msg);
      }
      pending_commands_.erase(it);
    }
  }
}
```

### 功能说明

修复 `HandleLockUserMessage()` 函数中的代码结构问题：

1. 清理因编辑错误导致的重复代码和格式混乱
2. 确保两级确认机制在正确的作用域内执行
3. 添加空指针检查，提高代码健壮性
4. 修正代码缩进，提高可读性

---

## 2026-01-16 (v2.8 协议语音播放辅助方法)

### 修改文件

| 文件                  | 修改内容                                                   |
| --------------------- | ---------------------------------------------------------- |
| `main/application.cc` | 新增语音播放辅助方法，支持认证失败和设备锁定的语音拼接播放 |

### 具体变更

**新增 PlayAuthFailVoice() 方法**

- 位置: `Application` 类，v5.0 协议区域之前（约第 1888-1900 行）
- 功能: 播放认证失败语音（拼接方式）
- 播放序列: 前缀 + 数字 + 后缀
- 示例: "认证失败，还剩" + "4" + "次机会"

**新增 PlayLockedVoice() 方法**

- 位置: `Application` 类（约第 1908-1922 行）
- 功能: 播放设备锁定语音（拼接方式）
- 播放序列: 前缀 + 数字 + 后缀
- 示例: "设备已锁定，请" + "3" + "分钟后再试"

**新增 PlayNumberVoice() 方法**

- 位置: `Application` 类（约第 1930-1959 行）
- 功能: 播放数字语音（0-99）
- 实现逻辑:
  - 0-9: 直接播放对应数字音频
  - 10-99: 拆分为十位和个位分别播放
- 使用静态映射表 `digit_sounds[]` 关联 `Lang::Sounds::OGG_0` ~ `OGG_9`

### 功能说明

实现 v2.8 协议中认证失败和设备锁定的本地语音提示功能：

1. **认证失败提示**: 当用户指纹/密码/NFC 认证失败时，播放剩余尝试次数
2. **设备锁定提示**: 当连续认证失败导致设备锁定时，播放剩余锁定时间

采用数字拼接方式节省 Flash 空间，复用现有的数字语音资源（0-9）。

### 依赖资源

需要在 `Lang::Sounds` 命名空间中定义以下常量（参考 `docs/my_docs/语音资源清单.md`）：

- `OGG_AUTH_FAIL_PREFIX` - "认证失败，还剩"
- `OGG_AUTH_FAIL_SUFFIX` - "次机会"
- `OGG_LOCKED_PREFIX` - "设备已锁定，请"
- `OGG_LOCKED_SUFFIX` - "分钟后再试"
- `OGG_0` ~ `OGG_9` - 数字 0-9（已存在）

---

## 2026-01-16 (自定义语音播放临时禁用)

### 修改文件

| 文件                  | 修改内容                     |
| --------------------- | ---------------------------- |
| `main/application.cc` | 注释掉所有自定义语音播放调用 |

### 具体变更

**事件处理语音禁用**

- 位置: `HandleLockReportMessage()` 函数，事件上报处理分支
- 变更:
  - 注释 `OGG_TAMPER_ALERT` 播放（撬锁报警，第 1126 行）
  - 注释 `OGG_DOOR_NOT_CLOSED` 播放（门未关闭，第 1134 行）

**指纹录入语音禁用**

- 位置: `HandleLockUserMessage()` 函数，指纹反馈处理分支
- 变更:
  - 注释 `OGG_FP_PRESS` 播放（请按手指，第 1457 行）
  - 注释 `OGG_FP_PRESS_AGAIN` 播放（请再次按压，第 1459 行）
  - 注释 `OGG_FP_LIFT` 播放（请抬起手指，第 1464 行）
  - 注释 `OGG_ENROLL_SUCCESS` 播放（录入成功，第 1474 行）
  - 注释 `OGG_ENROLL_FAIL` 播放（录入失败，第 1483 行）
  - 注释 `OGG_ALREADY_EXISTS` 播放（已存在，第 1501 行）
  - 注释 `OGG_ID_OCCUPIED` 播放（ID被占用，第 1511 行）

**NFC 录入语音禁用**

- 位置: `HandleLockUserMessage()` 函数，NFC 反馈处理分支
- 变更:
  - 注释 `OGG_NFC_TAP` 播放（请刷卡，第 1527 行）
  - 注释 `OGG_NFC_TAP_AGAIN` 播放（请再次刷卡，第 1532 行）
  - 注释 `OGG_ENROLL_SUCCESS` 播放（录入成功，第 1541 行）
  - 注释 `OGG_ENROLL_FAIL` 播放（录入失败，第 1550 行）
  - 注释 `OGG_ALREADY_EXISTS` 播放（已存在，第 1567 行）
  - 注释 `OGG_ID_OCCUPIED` 播放（ID被占用，第 1576 行）

**认证失败/锁定语音禁用**

- 位置: `PlayAuthFailVoice()` 和 `PlayLockedVoice()` 函数
- 变更:
  - 注释 `OGG_AUTH_FAIL_PREFIX` 和 `OGG_AUTH_FAIL_SUFFIX` 播放（第 1891、1897 行）
  - 注释 `OGG_LOCKED_PREFIX` 和 `OGG_LOCKED_SUFFIX` 播放（第 1912、1918 行）

### 功能说明

临时禁用所有自定义语音播放功能。原因：

- 语音资源文件（`.ogg`）尚未添加到项目中
- 避免编译时找不到资源常量或运行时播放失败

### 恢复方法

待语音资源文件添加完成后，取消注释以下文件中的 `audio_service_.PlaySound()` 调用：

- `main/application.cc` 中所有被注释的 `PlaySound` 行

### 涉及语音资源

| 常量名                 | 语音内容                 | 状态   |
| ---------------------- | ------------------------ | ------ |
| `OGG_TAMPER_ALERT`     | "检测到异常，请注意安全" | 待添加 |
| `OGG_DOOR_NOT_CLOSED`  | "门未关闭，请注意关门"   | 待添加 |
| `OGG_FP_PRESS`         | "请按压手指"             | 待添加 |
| `OGG_FP_PRESS_AGAIN`   | "请再次按压"             | 待添加 |
| `OGG_FP_LIFT`          | "请抬起手指"             | 待添加 |
| `OGG_NFC_TAP`          | "请刷卡"                 | 待添加 |
| `OGG_NFC_TAP_AGAIN`    | "请再次刷卡"             | 待添加 |
| `OGG_ENROLL_SUCCESS`   | "录入成功"               | 待添加 |
| `OGG_ENROLL_FAIL`      | "录入失败，请重试"       | 待添加 |
| `OGG_ALREADY_EXISTS`   | "该特征已存在"           | 待添加 |
| `OGG_ID_OCCUPIED`      | "指定编号已占用"         | 待添加 |
| `OGG_AUTH_FAIL_PREFIX` | "认证失败，还剩"         | 待添加 |
| `OGG_AUTH_FAIL_SUFFIX` | "次机会"                 | 待添加 |
| `OGG_LOCKED_PREFIX`    | "设备已锁定，请"         | 待添加 |
| `OGG_LOCKED_SUFFIX`    | "分钟后再试"             | 待添加 |

---

## 2026-01-16 (NFC 子命令枚举与响应状态修正)

### 修改文件

| 文件                                | 修改内容                                           |
| ----------------------------------- | -------------------------------------------------- |
| `main/lock_control/lock_protocol.h` | 新增 `NfcSubCmd` 枚举，修正 `NfcRespStatus` 枚举值 |

### 具体变更

**新增 NfcSubCmd 枚举**

- 位置: `NfcRespStatus` 枚举定义之前（约第 272-280 行）
- 内容:
  - `NFC_ENROLL = 0x01` - 录入
  - `NFC_DELETE = 0x02` - 删除指定 ID
  - `NFC_CLEAR = 0x03` - 清空全部
  - `NFC_COUNT = 0x04` - 查询数量
- 说明: 与 `FingerprintSubCmd` 枚举对应，定义 NFC 管理的子命令

**NfcRespStatus 枚举值修正**

- 位置: `NfcRespStatus` 枚举（约第 285 行）
- 变更: `NFC_TAP_AGAIN = 0x02` → `NFC_REMOVE_CARD = 0x02`
- 说明: 修正命名，`0x02` 状态码含义是"请移开卡片"而非"请再次刷卡"，与 STM32 协议文档 v2.8 保持一致

### 功能说明

1. **NfcSubCmd 枚举**: 补充 NFC 子命令定义，与指纹子命令 `FingerprintSubCmd` 对应，便于代码中使用类型安全的枚举值
2. **NFC_REMOVE_CARD 修正**: 根据 STM32 协议文档 v2.8 第 4.4.B 节，NFC 录入反馈 `D0=0x02` 的含义是"请移开卡片"，而非"请再次刷卡"

### 协议对应

参考 `docs/my_docs/智能猫眼门锁系统-STM32端.md` 第 4.4.B 节：

| D0 状态码 | 含义             |
| --------- | ---------------- |
| 0x01      | 请刷卡（录入中） |
| 0x02      | 请移开卡片       |
| 0x03      | 录入成功         |
| 0x04      | 操作失败         |
| 0x05      | 数量反馈         |
| 0x06      | UID 已存在       |
| 0x07      | ID 被占用        |

---

## 2026-01-16 (开锁日志上报协议优化)

### 修改文件

| 文件                  | 修改内容                                                            |
| --------------------- | ------------------------------------------------------------------- |
| `main/application.cc` | 重构 `RPT_UNLOCK` 和 `RPT_DOOR_OPENED` 处理逻辑，优化服务器上报协议 |

### 具体变更

**RPT_UNLOCK 处理逻辑重构**

- 位置: `HandleLockReportMessage()` 函数，`RPT_UNLOCK` case 分支（约第 1182-1230 行）
- 变更:
  - 变量重命名: `id_or_lock_time` → `d1`，语义更清晰
  - 新增状态解析变量: `status_str`、`uid`、`fail_count`、`lock_time`
  - 根据结果类型分别解析字段含义:
    - 成功 (D2=0x00): `status_str="success"`, `uid=D1`
    - 锁定 (D2=0x06): `status_str="locked"`, `lock_time=D1`
    - 失败 (D2=0x01-0x05): `status_str="fail"`, `uid=D1`, `fail_count=D2`
  - 修改 `SendLogReport()` 调用签名: `(method_str, status_str, uid, fail_count, lock_time)`

**RPT_DOOR_OPENED 处理逻辑优化**

- 位置: `HandleLockReportMessage()` 函数，`RPT_DOOR_OPENED` case 分支（约第 1232-1265 行）
- 变更:
  - 简化注释
  - 将 `SendLogReport()` 替换为新方法 `SendDoorOpenedReport(method_str, source_str)`
  - 语义更清晰，开门日志与开锁日志分离

### 功能说明

优化 v5.0 协议的开锁日志上报，区分三种状态并传递更完整的信息：

| 状态 | status_str  | uid         | fail_count | lock_time |
| ---- | ----------- | ----------- | ---------- | --------- |
| 成功 | `"success"` | 用户ID      | 0          | 0         |
| 失败 | `"fail"`    | 用户ID/0xFF | 失败次数   | 0         |
| 锁定 | `"locked"`  | 0           | 0          | 剩余分钟  |

新增专用的开门日志上报方法 `SendDoorOpenedReport()`，与开锁日志分离，符合 v2.6+ 协议设计。

### 协议对应

参考 `docs/my_docs/智能猫眼门锁系统-STM32端.md` v2.8 版本：

- `RPT_UNLOCK (0xA1)`: D2 字段扩展，支持失败次数和锁定状态
- `RPT_DOOR_OPENED (0xA2)`: 独立的开门日志上报

### 待实现

需要在 `Protocol` 基类和 `WebsocketProtocol` 实现类中：

1. 更新 `SendLogReport()` 方法签名
2. 新增 `SendDoorOpenedReport()` 方法

---

## 2026-01-16 (NFC 录入状态枚举修正)

### 修改文件

| 文件                  | 修改内容                                  |
| --------------------- | ----------------------------------------- |
| `main/application.cc` | 修正 NFC 录入中间状态的枚举名称和提示信息 |

### 具体变更

**NFC 响应状态处理修正**

- 位置: `HandleNfcResponse()` 函数，NFC 中间状态处理分支（约第 1534-1540 行）
- 变更:
  - 枚举名称: `NfcRespStatus::NFC_TAP_AGAIN` → `NfcRespStatus::NFC_REMOVE_CARD`
  - 日志消息: `"NFC 录入：请再次刷卡"` → `"NFC 录入：请移开卡片"`
  - 语音资源注释: `OGG_NFC_TAP_AGAIN` → `OGG_NFC_REMOVE_CARD`

### 功能说明

修正 NFC 录入流程中间状态的语义，使其更符合实际操作流程：

| 原状态                       | 修正后状态                     | 说明                               |
| ---------------------------- | ------------------------------ | ---------------------------------- |
| `NFC_TAP_AGAIN` (请再次刷卡) | `NFC_REMOVE_CARD` (请移开卡片) | 录入过程中需要先移开卡片再重新刷卡 |

NFC 录入完整流程：

1. `NFC_TAP` (0x01): 请刷卡
2. `NFC_REMOVE_CARD` (0x02): 请移开卡片
3. `NFC_SUCCESS` (0x03): 录入成功

### 协议对应

需要同步更新 `main/lock_control/lock_protocol.h` 中的 `NfcRespStatus` 枚举定义，将 `NFC_TAP_AGAIN` 重命名为 `NFC_REMOVE_CARD`。

---

## 2026-01-16 (face_result 两级确认机制升级)

### 修改文件

| 文件                  | 修改内容                                                      |
| --------------------- | ------------------------------------------------------------- |
| `main/application.cc` | 升级 `face_result` 消息处理，添加两级确认机制和 `seq_id` 支持 |

### 具体变更

**face_result 消息处理升级**

- 位置: `HandleSmartLockJsonMessage()` 函数，`face_result` / `face_recognition` 处理分支（约第 1987-2023 行）
- 变更:
  1. **seq_id 优先支持**: 优先使用 `seq_id` 字段，兼容旧版 `msg_id`
  2. **第一级确认**: 收到消息后立即发送 `esp32_ack`（命令已收到）
  3. **第二级确认**: 处理完成后发送 `ack`（命令执行完成）
  4. **日志增强**: 添加 `seq_id` 信息到日志输出

**代码变更详情**

```cpp
// 旧代码
auto msg_id = cJSON_GetObjectItem(root, "msg_id");
std::string msg_id_str = cJSON_IsString(msg_id) ? msg_id->valuestring : "";
// 只发送 ack

// 新代码
auto seq_id = cJSON_GetObjectItem(root, "seq_id");
auto msg_id = cJSON_GetObjectItem(root, "msg_id");
std::string seq_id_str = cJSON_IsString(seq_id)   ? seq_id->valuestring
                         : cJSON_IsString(msg_id) ? msg_id->valuestring
                                                  : "";
// 立即发送 esp32_ack（第一级确认）
ws_protocol->SendEsp32Ack(seq_id_str, 0, "received");
// 处理完成后发送 ack（第二级确认）
protocol_->SendAck(seq_id_str, 0, "OK");
```

### 功能说明

完成 v5.2 协议升级需求中的 `face_result` 两级确认机制：

| 确认级别 | 消息类型    | 发送时机           | 含义                 |
| -------- | ----------- | ------------------ | -------------------- |
| 第一级   | `esp32_ack` | 收到消息后立即发送 | 命令已收到，开始处理 |
| 第二级   | `ack`       | 处理完成后发送     | 命令执行完成         |

采用方案 A：`ack` 表示"人脸识别结果已处理"，开锁结果通过 `log_report` 上报。

### 协议对应

参考 `docs/my_docs/ESP32消息处理升级需求-v5.2.md` 第 2.2 节：

> **face_result 两级确认机制**：添加 `esp32_ack` 发送，支持 `seq_id` 字段

### 相关文档

- `docs/my_docs/协议规范变更说明-v5.0到v5.1.md` - 两级确认机制说明
- `docs/my_docs/智能猫眼门锁系统-ESP32与服务器通信协议规范-v5.1.md` - 协议规范

---

## 2026-01-17 (face_result 两级确认机制移除)

### 修改文件

| 文件                  | 修改内容                                            |
| --------------------- | --------------------------------------------------- |
| `main/application.cc` | 移除 `face_result` 消息的两级确认机制，简化处理逻辑 |

### 具体变更

**face_result 消息处理简化**

- 位置: `HandleSmartLockJsonMessage()` 函数，`face_result` / `face_recognition` 处理分支（约第 1987-2023 行）
- 变更:
  1. **移除 seq_id 解析**: 删除 `seq_id` 和 `msg_id` 字段解析逻辑
  2. **移除第一级确认**: 删除 `SendEsp32Ack()` 调用
  3. **移除第二级确认**: 删除 `SendAck()` 调用
  4. **简化日志**: 日志从 `"人脸识别结果 (seq_id=%s)"` 改为 `"收到人脸识别结果"`
  5. **简化 Schedule 调用**: 移除 `seq_id_str` 参数传递

**代码变更详情**

```cpp
// 旧代码（v5.2 两级确认）
auto seq_id = cJSON_GetObjectItem(root, "seq_id");
auto msg_id = cJSON_GetObjectItem(root, "msg_id");
std::string seq_id_str = cJSON_IsString(seq_id)   ? seq_id->valuestring
                         : cJSON_IsString(msg_id) ? msg_id->valuestring
                                                  : "";
ESP_LOGI(TAG, "人脸识别结果 (seq_id=%s)", seq_id_str.c_str());
ws_protocol->SendEsp32Ack(seq_id_str, 0, "received");
Schedule([this, root_copy = cJSON_Duplicate(root, 1), seq_id_str]() {
    HandleFaceRecognitionResult(root_copy);
    protocol_->SendAck(seq_id_str, 0, "OK");
    cJSON_Delete(root_copy);
});

// 新代码（v5.2 修正）
ESP_LOGI(TAG, "收到人脸识别结果");
Schedule([this, root_copy = cJSON_Duplicate(root, 1)]() {
    HandleFaceRecognitionResult(root_copy);
    cJSON_Delete(root_copy);
});
```

### 功能说明

根据协议设计原则修正 `face_result` 消息处理逻辑：

**设计原则**：

- `face_result` 是服务器主动推送的识别结果，不是用户发起的命令
- 不需要 `seq_id` 字段
- 不需要 `esp32_ack` 和 `ack` 两级确认
- 开锁结果通过 `log_report` 上报

**变更效果**：

- ✅ 简化代码逻辑，提高可维护性
- ✅ 与协议规范 v5.2 保持一致
- ✅ 避免不必要的确认消息

### 协议对应

参考 `docs/my_docs/通信协议修改分析.md` 第三节：

> **face_result 修改方案**：移除不必要的两级确认机制
>
> - ❌ 移除 seq_id 和 msg_id 的解析
> - ❌ 移除 esp32_ack 的发送
> - ❌ 移除 ack 的发送
> - ✅ 简化日志输出
> - ✅ 保持原有的异步处理逻辑

### 相关文档

- `docs/my_docs/通信协议修改分析.md` - 修改方案详细说明
- `docs/my_docs/ESP32人脸识别通信流程分析.md` - 更新后的流程图
- `docs/my_docs/智能猫眼门锁系统-ESP32与服务器通信协议规范-v5.2.md` - 协议规范

---

## 2026-01-18 (语音文件批量转换脚本)

### 新增文件

| 文件                        | 说明                              |
| --------------------------- | --------------------------------- |
| `scripts/convert_voices.py` | 语音文件批量转换脚本（MP3 → OGG） |

### 具体变更

**新增 convert_voices.py 脚本**

- 位置: `scripts/` 目录（第 1-159 行）
- 功能: 自动化语音资源生成流程，批量转换 MP3 文件为 OGG (Opus 编码) 格式
- 实现内容:
  1. **文件名映射**: 定义 15 个中文命名 MP3 文件到英文命名 OGG 文件的映射关系
  2. **FFmpeg 检查**: 自动检测 FFmpeg 是否安装，提供安装指引
  3. **格式转换**: 使用 Opus 编码器，32kbps 比特率，16kHz 采样率，单声道
  4. **统计报告**: 输出转换成功/失败数量、文件大小、压缩率等信息
  5. **操作提示**: 转换完成后提示后续操作步骤

**文件名映射表**
| 中文文件名 | 英文文件名 | 用途 |
|-----------|-----------|------|
| `检测到异常，请注意安全.mp3` | `tamper_alert.ogg` | 撬锁报警 |
| `门未关闭，请注意关门.mp3` | `door_not_closed.ogg` | 门未关超时 |
| `认证失败，还剩.mp3` | `auth_fail_prefix.ogg` | 认证失败前缀 |
| `次机会.mp3` | `auth_fail_suffix.ogg` | 认证失败后缀 |
| `设备已锁定，请.mp3` | `locked_prefix.ogg` | 设备锁定前缀 |
| `分钟后再试.mp3` | `locked_suffix.ogg` | 设备锁定后缀 |
| `请按压手指.mp3` | `fp_press.ogg` | 指纹录入 |
| `请抬起手指.mp3` | `fp_lift.ogg` | 指纹录入 |
| `请再次按压.mp3` | `fp_press_again.ogg` | 指纹录入 |
| `请刷卡.mp3` | `nfc_tap.ogg` | NFC 录入 |
| `请再次刷卡.mp3` | `nfc_tap_again.ogg` | NFC 录入 |
| `录入成功.mp3` | `enroll_success.ogg` | 录入结果 |
| `录入失败，请重试.mp3` | `enroll_fail.ogg` | 录入结果 |
| `该特征已存在.mp3` | `already_exists.ogg` | 录入结果 |
| `指定编号已占用，已自动分配新编号.mp3` | `id_occupied.ogg` | 录入结果 |

**转换参数**

- 编码器: `libopus`
- 比特率: `32kbps`（适合语音）
- 采样率: `16kHz`
- 声道: 单声道
- 容器格式: OGG

**目录配置**

- 输入目录: `voice/`（项目根目录下的 MP3 文件）
- 输出目录: `main/assets/locales/zh-CN/`（嵌入固件的语音资源）

### 功能说明

实现语音资源开发完整指南中的"步骤 2：转换音频格式"自动化：

1. 简化语音资源生成流程，避免手动执行 FFmpeg 命令
2. 统一音频格式参数，确保所有语音文件符合 ESP32 固件要求
3. 提供详细的转换统计信息，便于验证转换结果
4. 自动检查依赖工具，提供友好的错误提示

### 使用方法

```bash
# 1. 将 MP3 文件放入 voice/ 目录
# 2. 运行转换脚本
python scripts/convert_voices.py

# 3. 按照提示完成后续操作：
#    - 更新 main/assets/lang_config.h 添加语音常量定义
#    - 取消 main/application.cc 中的播放代码注释
#    - 执行 idf.py build 编译
```

### 相关文档

- `docs/my_docs/语音资源开发完整指南.md` - 完整的语音资源生成与集成操作步骤
- `docs/my_docs/语音资源清单.md` - 语音资源清单和播放逻辑说明
- `docs/my_docs/本地语音播放开发指南.md` - 本地语音播放 API 使用参考

### 待完成

1. 生成 15 个 MP3 语音文件并放入 `voice/` 目录
2. 运行脚本转换为 OGG 格式
3. 更新 `main/assets/lang_config.h` 添加语音常量定义
4. 取消 `main/application.cc` 中的播放代码注释
5. 编译测试语音播放功能

---

## 2026-01-18 (门锁系统语音资源常量定义)

### 修改文件

| 文件                        | 修改内容                             |
| --------------------------- | ------------------------------------ |
| `main/assets/lang_config.h` | 新增 15 个门锁系统语音资源的常量定义 |

### 具体变更

**新增语音资源常量定义**

- 位置: `Lang::Sounds` 命名空间末尾（第 217-330 行）
- 变更: 在现有语音资源后新增 15 个门锁系统专用语音常量
- 分类:
  - **安全告警类（2个）**: `OGG_TAMPER_ALERT`（撬锁报警）、`OGG_DOOR_NOT_CLOSED`（门未关闭）
  - **认证失败类（2个）**: `OGG_AUTH_FAIL_PREFIX`（前缀）、`OGG_AUTH_FAIL_SUFFIX`（后缀）
  - **设备锁定类（2个）**: `OGG_LOCKED_PREFIX`（前缀）、`OGG_LOCKED_SUFFIX`（后缀）
  - **指纹录入类（3个）**: `OGG_FP_PRESS`（按压）、`OGG_FP_LIFT`（抬起）、`OGG_FP_PRESS_AGAIN`（再次按压）
  - **NFC 录入类（2个）**: `OGG_NFC_TAP`（刷卡）、`OGG_NFC_TAP_AGAIN`（再次刷卡）
  - **录入结果类（4个）**: `OGG_ENROLL_SUCCESS`（成功）、`OGG_ENROLL_FAIL`（失败）、`OGG_ALREADY_EXISTS`（已存在）、`OGG_ID_OCCUPIED`（ID占用）

**常量定义格式**

每个语音资源包含：

- `extern` 声明：`ogg_xxx_start` 和 `ogg_xxx_end` 符号（通过 `asm` 链接到嵌入的二进制数据）
- `std::string_view` 常量：使用 `static_cast` 将符号转换为字符串视图

### 功能说明

为智能门锁系统添加本地语音播放支持，使 ESP32 能够通过 TTS 语音反馈用户操作结果：

1. **安全告警**: 撬锁报警、门未关闭提醒
2. **认证失败**: 拼接播放"认证失败，还剩 N 次机会"
3. **设备锁定**: 拼接播放"设备已锁定，请 N 分钟后再试"
4. **指纹录入**: 引导用户完成指纹录入流程
5. **NFC 录入**: 引导用户完成 NFC 卡片录入流程
6. **录入结果**: 反馈录入成功/失败/已存在/ID占用等状态

### 依赖资源

需要在 `main/assets/locales/zh-CN/` 目录下添加对应的 15 个 OGG 音频文件：

| 文件名                 | 语音内容                           |
| ---------------------- | ---------------------------------- |
| `tamper_alert.ogg`     | "检测到异常，请注意安全"           |
| `door_not_closed.ogg`  | "门未关闭，请注意关门"             |
| `auth_fail_prefix.ogg` | "认证失败，还剩"                   |
| `auth_fail_suffix.ogg` | "次机会"                           |
| `locked_prefix.ogg`    | "设备已锁定，请"                   |
| `locked_suffix.ogg`    | "分钟后再试"                       |
| `fp_press.ogg`         | "请按压手指"                       |
| `fp_lift.ogg`          | "请抬起手指"                       |
| `fp_press_again.ogg`   | "请再次按压"                       |
| `nfc_tap.ogg`          | "请刷卡"                           |
| `nfc_tap_again.ogg`    | "请再次刷卡"                       |
| `enroll_success.ogg`   | "录入成功"                         |
| `enroll_fail.ogg`      | "录入失败，请重试"                 |
| `already_exists.ogg`   | "该特征已存在"                     |
| `id_occupied.ogg`      | "指定编号已占用，已自动分配新编号" |

### 使用方法

在代码中通过 `audio_service_.PlaySound(Lang::Sounds::OGG_XXX)` 调用播放，例如：

```cpp
// 撬锁报警
audio_service_.PlaySound(Lang::Sounds::OGG_TAMPER_ALERT);

// 认证失败（拼接播放）
audio_service_.PlaySound(Lang::Sounds::OGG_AUTH_FAIL_PREFIX);
PlayNumberVoice(remaining_attempts);
audio_service_.PlaySound(Lang::Sounds::OGG_AUTH_FAIL_SUFFIX);
```

### 相关文档

- `docs/my_docs/语音资源开发完整指南.md` - 语音资源生成和集成操作步骤
- `docs/my_docs/语音资源清单.md` - 完整的语音资源清单和使用场景
- `scripts/convert_voices.py` - 语音文件批量转换脚本

### 备注

当前语音播放调用已在 `main/application.cc` 中实现但被注释，待语音文件添加完成后取消注释即可启用。

## 2026-01-18 (指纹录入语音提示启用)

### 修改文件

| 文件                  | 修改内容                         |
| --------------------- | -------------------------------- |
| `main/application.cc` | 启用指纹录入过程中的语音提示功能 |

### 具体变更

**指纹录入语音提示启用**

- 位置: `HandleLockUserMessage()` 函数，指纹反馈处理分支（第 1465-1467 行）
- 变更: 取消两行语音播放代码的注释
  - 第一次按压: `audio_service_.PlaySound(Lang::Sounds::OGG_FP_PRESS);`
  - 后续按压: `audio_service_.PlaySound(Lang::Sounds::OGG_FP_PRESS_AGAIN);`

**修改前**:

```cpp
if (press_count == 1) {
    // audio_service_.PlaySound(Lang::Sounds::OGG_FP_PRESS);
} else {
    // audio_service_.PlaySound(Lang::Sounds::OGG_FP_PRESS_AGAIN);
}
```

**修改后**:

```cpp
if (press_count == 1) {
    audio_service_.PlaySound(Lang::Sounds::OGG_FP_PRESS);
} else {
    audio_service_.PlaySound(Lang::Sounds::OGG_FP_PRESS_AGAIN);
}
```

### 功能说明

启用指纹录入过程中的语音提示功能，提升用户体验：

- **第一次按压**: 播放"请按压手指"（`OGG_FP_PRESS`）
- **后续按压**: 播放"请再次按压"（`OGG_FP_PRESS_AGAIN`）

### 依赖资源

需要确保以下语音资源文件已添加到项目中：

- `main/assets/locales/zh-CN/fp_press.ogg` - "请按压手指"
- `main/assets/locales/zh-CN/fp_press_again.ogg` - "请再次按压"

### 相关文档

- `docs/my_docs/语音资源清单.md` - 语音资源列表
- `scripts/convert_voices.py` - 语音文件转换脚本

---

---

## 2026-01-18 (语音转换采样率修正)

### 修改文件

| 文件                        | 修改内容                                   |
| --------------------------- | ------------------------------------------ |
| `scripts/convert_voices.py` | 修正语音转换采样率，与项目原有语音保持一致 |

### 具体变更

**convert_to_ogg() 函数采样率修正**

- 位置: 第 48 行和第 56 行
- 变更:
  - 采样率从 `16000 Hz` (16kHz) 改为 `48000 Hz` (48kHz)
  - 注释从"16kHz 采样率"改为"48kHz 采样率（与原有语音一致）"
- 原代码: `"-ar", "16000",  # 16kHz 采样率`
- 新代码: `"-ar", "48000",  # 48kHz 采样率（与原有语音一致）`

### 功能说明

修正语音转换脚本的采样率参数，确保新生成的门锁系统语音与项目原有语音资源保持一致：

- **原有语音**: 48kHz 采样率（如 `welcome.ogg`、`success.ogg` 等）
- **新增语音**: 48kHz 采样率（门锁系统专用语音）
- **统一标准**: 避免采样率不一致导致的音质差异或播放问题

### 技术说明

**采样率选择原因**:

- 48kHz 是专业音频标准采样率，音质更好
- 与项目现有语音资源保持一致，便于统一管理
- Opus 编码器在 48kHz 下性能最优

**转换参数**:

| 参数     | 值                 | 说明               |
| -------- | ------------------ | ------------------ |
| 编码器   | `libopus`          | Opus 音频编码器    |
| 比特率   | `32kbps`           | 适合语音的压缩率   |
| 采样率   | `48000 Hz` (48kHz) | 与原有语音保持一致 |
| 声道     | 单声道             | 节省存储空间       |
| 容器格式 | OGG                | 开源音频容器格式   |

### 影响范围

此修改影响所有通过 `convert_voices.py` 脚本生成的语音文件：

- 安全告警类（2个）
- 认证失败类（2个）
- 设备锁定类（2个）
- 指纹录入类（3个）
- NFC 录入类（2个）
- 录入结果类（4个）

### 相关文档

- `docs/my_docs/语音资源开发完整指南.md` - 语音资源生成流程
- `docs/my_docs/语音资源清单.md` - 语音资源清单
- `docs/my_docs/本地语音播放开发指南.md` - 本地语音播放 API

---

**文档维护者**: 毕业设计项目组  
**最后更新**: 2026-01-18

## 2026-01-29 (语音转换帧时长修正)

### 修改文件

| 文件                        | 修改内容                                     |
| --------------------------- | -------------------------------------------- |
| `scripts/convert_voices.py` | 新增 Opus 帧时长参数，与项目原有语音保持一致 |

### 具体变更

**convert_to_ogg() 函数帧时长参数新增**

- 位置: 第 58 行（FFmpeg 参数列表）
- 变更:
  - 新增 `-frame_duration` 参数，值为 `"60"`
  - 注释说明"60ms 帧时长（与项目原有语音保持一致）"
- 新增代码: `"-frame_duration", "60",  # 60ms 帧时长（与原有语音一致）`

### 功能说明

修正语音转换脚本的 Opus 帧时长参数，解决新增语音播放沙哑问题：

**问题根源**:

- 项目原有语音使用 60ms 帧时长
- 新增语音未指定帧时长，FFmpeg 默认使用 20ms
- 帧时长不匹配导致解码器解析错误，音频失真

**解决方案**:

- 在 FFmpeg 转换命令中明确指定 `-frame_duration 60`
- 确保所有语音文件使用统一的 60ms 帧时长
- 与 `main/audio/audio_service.cc` 中硬编码的 `packet->frame_duration = 60` 保持一致

### 技术说明

**Opus 帧时长**:

- 标准值: 2.5, 5, 10, 20, 40, 60ms
- 项目选择: 60ms（平衡延迟和压缩效率）
- 影响: 帧时长不匹配会导致解码器错误解析数据，产生音频失真

**转换参数（完整）**:

| 参数               | 值                 | 说明                   |
| ------------------ | ------------------ | ---------------------- |
| 编码器             | `libopus`          | Opus 音频编码器        |
| 比特率             | `32kbps`           | 适合语音的压缩率       |
| 采样率             | `48000 Hz` (48kHz) | 与原有语音保持一致     |
| 声道               | 单声道             | 节省存储空间           |
| **帧时长（新增）** | `60ms`             | **与原有语音保持一致** |
| 容器格式           | OGG                | 开源音频容器格式       |

### 影响范围

此修改影响所有通过 `convert_voices.py` 脚本生成的语音文件：

- 安全告警类（2个）
- 认证失败类（2个）
- 设备锁定类（2个）
- 指纹录入类（3个）
- NFC 录入类（2个）
- 录入结果类（4个）

### 相关文档

- `docs/my_docs/语音沙哑问题分析与解决方案.md` - 问题分析和解决方案详细说明
- `docs/my_docs/语音资源开发完整指南.md` - 语音资源生成流程
- `docs/my_docs/语音资源清单.md` - 语音资源清单

### 后续操作

1. 使用修正后的脚本重新转换所有语音文件
2. 验证新生成的语音文件播放是否清晰流畅
3. 如需修改帧时长，需同步修改 `main/audio/audio_service.cc` 中的硬编码值

---

**文档维护者**: 毕业设计项目组  
**最后更新**: 2026-01-29

## 2026-01-29 (语音转换采样率策略修正)

### 修改文件

| 文件                        | 修改内容                                                  |
| --------------------------- | --------------------------------------------------------- |
| `scripts/convert_voices.py` | 修正语音转换采样率策略，使用 16kHz 输入避免不必要的重采样 |

### 具体变更

**convert_to_ogg() 函数采样率策略修正**

- 位置: 第 48-62 行（FFmpeg 参数列表）
- 变更:
  - 采样率从 `48000 Hz` (48kHz) 改为 `16000 Hz` (16kHz) 输入
  - 参数顺序调整：`-ar "16000"` 移到 `-c:a "libopus"` 之前
  - 注释更新：说明使用 16kHz 输入的原因
- 原代码: `"-ar", "48000",  # 48kHz 采样率（与原有语音一致）`
- 新代码: `"-ar", "16000",  # 16kHz 输入采样率（关键：让 OpusHead 字段为 16000）`

**注释说明更新**

- 位置: 函数文档字符串（第 45-53 行）
- 变更:
  - 采样率说明从"48kHz (与项目原有语音保持一致)"改为"16kHz 输入 → 48kHz 输出 (与项目原有语音保持一致)"
  - 新增注意事项：说明使用 16kHz 输入的目的

### 功能说明

修正语音转换脚本的采样率策略，解决与原有数字语音不一致的问题：

**问题背景**:

- 项目原有数字语音（0-9）的 OpusHead 采样率字段为 16000 Hz
- 新增门锁语音使用 48000 Hz 输入，导致 OpusHead 字段为 48000 Hz
- 采样率不一致可能触发 ESP32 音频服务的重采样逻辑，影响性能

**解决方案**:

- 使用 16kHz 输入采样率，让 OpusHead 中的采样率字段为 16000
- Opus 编码器内部仍会以 48kHz 处理（Opus 标准）
- 与原有数字语音保持一致，避免触发不必要的重采样

### 技术说明

**Opus 采样率机制**:

- Opus 编码器内部始终以 48kHz 处理音频数据
- OpusHead 中的 `Input Sample Rate` 字段记录输入采样率
- ESP32 解码时可能根据此字段决定是否重采样

**转换参数（完整）**:

| 参数       | 值                 | 说明                                 |
| ---------- | ------------------ | ------------------------------------ |
| 编码器     | `libopus`          | Opus 音频编码器                      |
| 比特率     | `32kbps`           | 适合语音的压缩率                     |
| 输入采样率 | `16000 Hz` (16kHz) | **让 OpusHead 字段为 16000（关键）** |
| 输出采样率 | `48000 Hz` (48kHz) | Opus 内部处理采样率                  |
| 声道       | 单声道             | 节省存储空间                         |
| 帧时长     | `60ms`             | 与原有语音保持一致                   |
| 容器格式   | OGG                | 开源音频容器格式                     |

### 影响范围

此修改影响所有通过 `convert_voices.py` 脚本生成的语音文件：

- 安全告警类（2个）
- 认证失败类（2个）
- 设备锁定类（2个）
- 指纹录入类（3个）
- NFC 录入类（2个）
- 录入结果类（4个）

### 验证方法

使用 `ffprobe` 检查生成的 OGG 文件：

```bash
ffprobe -v error -show_format -show_streams fp_press.ogg
```

预期输出：

- `sample_rate=48000`（Opus 内部处理采样率）
- OpusHead 中 `Input Sample Rate` 字段为 16000

### 相关文档

- `docs/my_docs/语音沙哑问题分析与解决方案.md` - 采样率问题分析
- `docs/my_docs/语音资源开发完整指南.md` - 语音资源生成流程
- `docs/my_docs/语音资源清单.md` - 语音资源清单

### 后续操作

1. 使用修正后的脚本重新转换所有语音文件
2. 验证新生成的语音文件 OpusHead 采样率字段为 16000
3. 测试语音播放是否清晰流畅，无重采样导致的性能问题

---

**文档维护者**: 毕业设计项目组  
**最后更新**: 2026-01-29
