# 人脸识别设计方案修改说明

## 📝 修改内容

### 1. 串口协议修改

#### 修改前（9字节）
```
+--------+--------+--------+--------+--------+--------+--------+--------+--------+
| HEADER | CAT    | TYPE   | DATA0  | DATA1  | DATA2  | DATA3  | DATA4  | CHKSUM |
| 0xAA   | 1 byte | 1 byte | 1 byte | 1 byte | 1 byte | 1 byte | 1 byte | 1 byte |
+--------+--------+--------+--------+--------+--------+--------+--------+--------+
```

#### 修改后（7字节）
```
+--------+--------+--------+--------+--------+--------+--------+
| HEADER | CAT    | TYPE   | DATA0  | DATA1  | DATA2  | CHKSUM |
| 0xAA   | 1 byte | 1 byte | 1 byte | 1 byte | 1 byte | 1 byte |
+--------+--------+--------+--------+--------+--------+--------+
```

**修改原因：** 3字节数据字段足够使用，减少协议长度可以提高传输效率。

---

### 2. 密码编码方式修改

#### 修改前（5字节存储6位密码）
```
DATA0 = 第1位数字 (0-9)
DATA1 = 第2位数字 (0-9)
DATA2 = 第3位数字 (0-9)
DATA3 = 第4位数字 (0-9)
DATA4 = 低4位=第5位，高4位=第6位

示例："123456"
DATA0 = 0x01, DATA1 = 0x02, DATA2 = 0x03, DATA3 = 0x04, DATA4 = 0x65
```

#### 修改后（3字节存储6位密码）
```
DATA0 = 高4位=第1位，低4位=第2位
DATA1 = 高4位=第3位，低4位=第4位
DATA2 = 高4位=第5位，低4位=第6位

示例："123456"
DATA0 = 0x12, DATA1 = 0x34, DATA2 = 0x56
```

**修改原因：** 每个字节可以存储2位数字（BCD编码），3字节正好存储6位密码，更加紧凑高效。

---

### 3. 服务器 JSON 协议修改

#### 修改前（错误的理解）
```json
{
  "type": "face_recognition",
  "result": "known",
  "person": {...},
  "access": {...},
  "tts": {
    "text": "您好，张三，欢迎回家！",
    "audio_url": "https://server.com/tts/12345.mp3"
  }
}
```

#### 修改后（正确的理解）

**服务器会发送多个独立的 JSON 消息：**

**消息1：人脸识别结果**
```json
{
  "type": "face_recognition",
  "result": "known",
  "person": {"id": 1, "name": "张三", "relation": "owner"},
  "access": {"granted": true, "action": "open_door"}
}
```

**消息2：TTS 开始**
```json
{
  "type": "tts",
  "state": "start"
}
```

**消息3：TTS 文本（可选）**
```json
{
  "type": "tts",
  "state": "sentence_start",
  "text": "您好，张三，欢迎回家！"
}
```

**消息4：TTS 音频流**
- 通过 `OnIncomingAudio` 回调接收 OPUS 音频数据

**消息5：TTS 结束**
```json
{
  "type": "tts",
  "state": "stop"
}
```

**修改原因：** 参考 `application.cc` 中的实际实现，TTS 是通过独立的消息和音频流传输的，不是在一个 JSON 包里。

---

## 🔄 代码修改影响

### 1. LockProtocol 类

```cpp
// 修改前
constexpr size_t LOCK_PROTOCOL_LENGTH = 9;
constexpr size_t LOCK_PROTOCOL_DATA_LEN = 5;
static std::array<uint8_t, 5> EncodePassword(const char* password);

// 修改后
constexpr size_t LOCK_PROTOCOL_LENGTH = 7;
constexpr size_t LOCK_PROTOCOL_DATA_LEN = 3;
static std::array<uint8_t, 3> EncodePassword(const char* password);
```

### 2. 密码编码实现

```cpp
// 修改前
std::array<uint8_t, 5> LockProtocol::EncodePassword(const char* password) {
    std::array<uint8_t, 5> data = {0, 0, 0, 0, 0};
    if (!password || strlen(password) != 6) return data;
    
    for (int i = 0; i < 4; i++) {
        data[i] = password[i] - '0';
    }
    data[4] = ((password[5] - '0') << 4) | (password[4] - '0');
    return data;
}

// 修改后
std::array<uint8_t, 3> LockProtocol::EncodePassword(const char* password) {
    std::array<uint8_t, 3> data = {0, 0, 0};
    if (!password || strlen(password) != 6) return data;
    
    // BCD 编码：每个字节存储2位数字
    data[0] = ((password[0] - '0') << 4) | (password[1] - '0');
    data[1] = ((password[2] - '0') << 4) | (password[3] - '0');
    data[2] = ((password[4] - '0') << 4) | (password[5] - '0');
    return data;
}
```

### 3. 密码解码实现

```cpp
// 修改前
std::string LockProtocol::DecodePassword(const std::array<uint8_t, 5>& data) {
    std::string password;
    password.reserve(6);
    
    for (int i = 0; i < 4; i++) {
        password += ('0' + (data[i] & 0x0F));
    }
    password += ('0' + (data[4] & 0x0F));
    password += ('0' + ((data[4] >> 4) & 0x0F));
    return password;
}

// 修改后
std::string LockProtocol::DecodePassword(const std::array<uint8_t, 3>& data) {
    std::string password;
    password.reserve(6);
    
    // BCD 解码：每个字节解析出2位数字
    password += ('0' + ((data[0] >> 4) & 0x0F));  // 第1位
    password += ('0' + (data[0] & 0x0F));         // 第2位
    password += ('0' + ((data[1] >> 4) & 0x0F));  // 第3位
    password += ('0' + (data[1] & 0x0F));         // 第4位
    password += ('0' + ((data[2] >> 4) & 0x0F));  // 第5位
    password += ('0' + (data[2] & 0x0F));         // 第6位
    return password;
}
```

### 4. Application 中的 TTS 处理

**不需要修改！** 现有的 `OnIncomingJson` 和 `OnIncomingAudio` 回调已经正确处理 TTS：

```cpp
protocol_->OnIncomingJson([this, display](const cJSON* root) {
    auto type = cJSON_GetObjectItem(root, "type");
    
    // 处理人脸识别结果
    if (strcmp(type->valuestring, "face_recognition") == 0) {
        // 解析识别结果
        // 如果有权限，发送开锁命令
    }
    
    // TTS 处理（已存在，不需要修改）
    else if (strcmp(type->valuestring, "tts") == 0) {
        auto state = cJSON_GetObjectItem(root, "state");
        if (strcmp(state->valuestring, "start") == 0) {
            // TTS 开始
        } else if (strcmp(state->valuestring, "stop") == 0) {
            // TTS 结束
        } else if (strcmp(state->valuestring, "sentence_start") == 0) {
            // 显示 TTS 文本
        }
    }
});

// TTS 音频自动通过这个回调播放
protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
    audio_service_.PushPacketToDecodeQueue(std::move(packet));
});
```

---

## ✅ 修改优点

### 1. 协议更紧凑
- 从9字节减少到7字节
- 减少22%的传输开销
- 对 STM32C8T6 更友好

### 2. 密码编码更高效
- 使用标准的 BCD 编码
- 每个字节存储2位数字
- 更容易理解和实现

### 3. 服务器协议更清晰
- 符合现有的实现方式
- TTS 通过独立的消息和音频流传输
- 不需要额外的 TTS 处理逻辑

---

## 📋 更新后的通信示例

### 门铃按下事件
```
STM32 → ESP32: AA 01 01 00 00 00 02
ESP32 → STM32: AA 0F 00 01 01 00 11  (ACK)
```

### 开锁命令
```
ESP32 → STM32: AA 02 01 00 00 00 03
STM32 → ESP32: AA 0F 00 02 01 00 12  (ACK)
```

### 设置临时开锁码 "123456"
```
ESP32 → STM32: AA 02 04 12 34 56 A2
                │  │  │  │  │  │  └─ CHKSUM = (02+04+12+34+56) & 0xFF = 0xA2
                │  │  │  │  │  └─ DATA2 = 0x56 (5,6)
                │  │  │  │  └─ DATA1 = 0x34 (3,4)
                │  │  │  └─ DATA0 = 0x12 (1,2)
                │  │  └─ TYPE = SET_TEMP_CODE
                │  └─ CAT = CONTROL
                └─ HEADER
```

### 人脸识别完整流程

**1. ESP32 发送视频帧（BinaryProtocol2）**
```
ESP32 → Server: [16字节头部] + [JPEG数据]
```

**2. 服务器返回识别结果**
```json
Server → ESP32: {"type": "face_recognition", "result": "known", ...}
```

**3. 服务器发送 TTS 开始**
```json
Server → ESP32: {"type": "tts", "state": "start"}
```

**4. 服务器发送 TTS 文本**
```json
Server → ESP32: {"type": "tts", "state": "sentence_start", "text": "您好，张三"}
```

**5. 服务器发送 TTS 音频流**
```
Server → ESP32: [OPUS 音频数据包1]
Server → ESP32: [OPUS 音频数据包2]
...
```

**6. 服务器发送 TTS 结束**
```json
Server → ESP32: {"type": "tts", "state": "stop"}
```

**7. ESP32 发送开锁命令（如果有权限）**
```
ESP32 → STM32: AA 02 01 00 00 00 03
```

---

## 🎯 总结

修改后的设计方案：
1. ✅ 串口协议更紧凑（7字节 vs 9字节）
2. ✅ 密码编码更高效（BCD编码）
3. ✅ 服务器协议符合实际实现
4. ✅ 不需要额外的 TTS 处理逻辑
5. ✅ 完全兼容现有的 Application 实现

**下一步：** 审查修改后的设计方案，确认无误后开始实施。
