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

| 文件 | 说明 |
|------|------|
| `main/lock_control/lock_protocol.h` | UART 协议常量、枚举、消息结构定义 |
| `main/lock_control/lock_protocol.cc` | 消息编解码、校验和计算、BCD密码编码 |
| `main/lock_control/lock_control.h` | 锁控服务类接口定义 |
| `main/lock_control/lock_control.cc` | UART 通信、事件回调、命令发送实现 |
| `main/lock_control/CMakeLists.txt` | 组件构建配置 |

#### 修改文件

| 文件 | 修改内容 |
|------|----------|
| `main/application.h` | 添加锁控服务成员和人脸识别相关方法声明 |
| `main/application.cc` | 实现人脸识别触发、结果处理、事件处理逻辑 |
| `main/boards/bread-compact-wifi-s3cam/config.h` | 添加 UART 引脚定义 |
| `main/boards/bread-compact-wifi-s3cam/compact_wifi_board_s3cam.cc` | 添加锁控服务初始化和 GetLockControl() 方法 |
| `main/CMakeLists.txt` | 添加 lock_control 组件依赖 |

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

| 目录 | 说明 |
|------|------|
| `main/video/` | 视频流服务 |
| `main/monitor/` | 监控模式服务 |

#### 新增文件

| 文件 | 说明 |
|------|------|
| `main/video/video_stream_service.h/cc` | 视频流捕获和队列管理 |
| `main/video/jpeg_frame.h` | JPEG 帧结构定义 |
| `main/monitor/monitor_service.h/cc` | 监控模式管理 |

#### 修改文件

| 文件 | 修改内容 |
|------|----------|
| `main/device_state.h` | 新增监控模式状态枚举 |
| `main/application.h/cc` | 新增监控模式管理方法 |
| `main/protocols/protocol.h` | 新增 SendVideo() 虚方法 |
| `main/protocols/websocket_protocol.h/cc` | 实现视频发送 |
| `main/protocols/mqtt_protocol.h/cc` | 实现视频发送 |
| `main/boards/common/esp32_camera.h/cc` | 新增 CaptureJpeg() 等方法 |

#### 协议扩展

- 使用 BinaryProtocol2 的 `reserved` 字段区分音频和视频
- `reserved = 0`: 音频数据（OPUS）
- `reserved = (width << 16) | height`: 视频数据（JPEG）

---

## 2025-12-10 (续)

### 锁控协议 v2.0 重构

#### 修改文件

| 文件 | 修改内容 |
|------|----------|
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

| Commit | 说明 |
|--------|------|
| `fb9e0b3` | 监控模式功能基本实现，性能待优化 (HEAD) |
| `13ed6cf` | 音视频传输功能 ESP32 端实现 |
| `ff33336` | 实施规划完成，准备开始修改代码 |
| `9e61cdf` | 整理了一下文档资料 |
| `e153467` | 新的开始，之前的分支废弃 |

---

## 版本说明

- **当前分支**: `cat-eye-lock-feature-2`
- **编译状态**: ✅ 通过
- **功能状态**: 核心功能完成，待硬件测试


---

## 2025-12-10 (补光灯控制)

### 修改文件

| 文件 | 修改内容 |
|------|----------|
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
    "action": "on"  // "on", "off", "auto"
}
```


---

## 2025-12-11

### v5.0 协议接口扩展

#### 修改文件

| 文件 | 修改内容 |
|------|----------|
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

| 文件 | 修改内容 |
|------|----------|
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

| 文件 | 修改内容 |
|------|----------|
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

| 文件 | 修改内容 |
|------|----------|
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

| 文件 | 修改内容 |
|------|----------|
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

| 文件 | 修改内容 |
|------|----------|
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

| 文件 | 修改内容 |
|------|----------|
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

| 文件 | 修改内容 |
|------|----------|
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

| 文件 | 修改内容 |
|------|----------|
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

| 文件 | 修改内容 |
|------|----------|
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

| 指标 | 重构前 | 重构后 |
|------|--------|--------|
| 主函数行数 | ~246 行 | ~40 行 |
| 函数数量 | 1 个 | 5 个 |
| 嵌套层级 | 最深 4 层 | 最深 2 层 |
| 代码复用 | 无 | `GetUnlockMethodString()` 可复用 |


---

## 2025-12-11 (人脸识别函数文档优化)

### 修改文件

| 文件 | 修改内容 |
|------|----------|
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

| 文件 | 修改内容 |
|------|----------|
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

| 文件 | 修改内容 |
|------|----------|
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
