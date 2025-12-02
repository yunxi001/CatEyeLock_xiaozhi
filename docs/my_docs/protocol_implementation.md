# Protocol 层视频数据传输实现文档

## 概述

本文档详细说明了如何使用 BinaryProtocol2 的 `reserved` 字段来区分音频和视频数据，实现高效的视频流传输。

---

## 设计方案

### 核心思想

**复用现有协议结构，使用 reserved 字段区分数据类型**

- 保持 `type` 字段不变（type = 0）
- 使用 `reserved` 字段编码额外信息
- 向后兼容现有的音频传输

### 优势

1. **向后兼容**：不改变现有的 type 定义，老版本服务器仍可处理音频
2. **高效**：直接二进制传输，无需 base64 编码，节省 ~33% 带宽
3. **简单**：服务器端只需检查一个字段即可区分数据类型
4. **完整**：元数据（宽高）和图像数据在同一消息中

---

## 协议结构

### BinaryProtocol2 定义

```cpp
struct BinaryProtocol2 {
    uint16_t version;       // 协议版本 (2)
    uint16_t type;          // 消息类型 (0: OPUS/VIDEO, 1: JSON)
    uint32_t reserved;      // 保留字段 - 用于区分音频和视频
    uint32_t timestamp;     // 时间戳（毫秒）
    uint32_t payload_size;  // 负载大小（字节）
    uint8_t payload[];      // 负载数据
} __attribute__((packed));
```

### Reserved 字段编码规则

```cpp
// 音频数据（OPUS）
type = 0, reserved = 0x00000000

// 视频数据（JPEG）
type = 0, reserved = (width << 16) | height
```

**示例：**
- 640x480 视频：`reserved = 0x028001E0`
- 320x240 视频：`reserved = 0x014000F0`
- 音频：`reserved = 0x00000000`

---

## 设备端实现

### 1. Protocol 基类

```cpp
// main/protocols/protocol.h
class Protocol {
public:
    // 新增虚方法
    virtual bool SendVideo(const uint8_t* data, size_t size, 
                          uint32_t timestamp, uint16_t width, uint16_t height) = 0;
};
```

### 2. WebsocketProtocol 实现

```cpp
// main/protocols/websocket_protocol.cc
bool WebsocketProtocol::SendVideo(const uint8_t* data, size_t size, 
                                   uint32_t timestamp, uint16_t width, uint16_t height) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }

    if (version_ == 2) {
        // 创建 BinaryProtocol2 消息
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol2) + size);
        auto bp2 = (BinaryProtocol2*)serialized.data();
        
        // 填充头部
        bp2->version = htons(2);
        bp2->type = 0;  // 与音频相同
        
        // 关键：使用 reserved 字段编码宽高
        bp2->reserved = htonl(((uint32_t)width << 16) | (uint32_t)height);
        
        bp2->timestamp = htonl(timestamp);
        bp2->payload_size = htonl(size);
        
        // 复制 JPEG 数据
        memcpy(bp2->payload, data, size);

        // 发送二进制数据
        return websocket_->Send(serialized.data(), serialized.size(), true);
    }
    
    return false;
}
```

### 3. MqttProtocol 实现

```cpp
// main/protocols/mqtt_protocol.cc
bool MqttProtocol::SendVideo(const uint8_t* data, size_t size, 
                              uint32_t timestamp, uint16_t width, uint16_t height) {
    if (publish_topic_.empty()) {
        return false;
    }

    // 创建 BinaryProtocol2 消息（与 WebSocket 相同）
    std::string serialized;
    serialized.resize(sizeof(BinaryProtocol2) + size);
    auto bp2 = (BinaryProtocol2*)serialized.data();
    
    bp2->version = htons(2);
    bp2->type = 0;
    bp2->reserved = htonl(((uint32_t)width << 16) | (uint32_t)height);
    bp2->timestamp = htonl(timestamp);
    bp2->payload_size = htonl(size);
    memcpy(bp2->payload, data, size);

    // 通过 MQTT 发布到视频主题
    std::string video_topic = publish_topic_ + "/video";
    return mqtt_->Publish(video_topic, serialized, false);
}
```

### 4. MonitorService 使用

```cpp
// main/monitor/monitor_service.cc
bool MonitorService::SendVideoFrame(const JpegFrame& frame) {
    if (!protocol_) {
        return false;
    }

    // 直接调用 Protocol 的 SendVideo 方法
    return protocol_->SendVideo(
        frame.data.data(),
        frame.data.size(),
        frame.timestamp,
        frame.width,
        frame.height
    );
}
```

---

## 服务器端实现

### 1. 消息解析（Python）

```python
import struct

def parse_binary_protocol2(data):
    """解析 BinaryProtocol2 格式消息"""
    # 解析头部（16字节）
    header = struct.unpack('<HHIII', data[:16])
    version, msg_type, reserved, timestamp, payload_size = header
    
    # 提取负载
    payload = data[16:16+payload_size]
    
    if msg_type == 0:
        if reserved == 0:
            # 音频数据
            return {
                'type': 'audio',
                'format': 'opus',
                'timestamp': timestamp,
                'data': payload
            }
        else:
            # 视频数据
            width = (reserved >> 16) & 0xFFFF
            height = reserved & 0xFFFF
            
            return {
                'type': 'video',
                'format': 'jpeg',
                'timestamp': timestamp,
                'width': width,
                'height': height,
                'data': payload
            }
    
    elif msg_type == 1:
        # JSON 消息
        return {
            'type': 'json',
            'data': json.loads(payload.decode('utf-8'))
        }
```

### 2. WebSocket 处理

```python
async def on_websocket_binary(websocket, data):
    """处理 WebSocket 二进制消息"""
    message = parse_binary_protocol2(data)
    
    if message['type'] == 'video':
        # 处理视频帧
        await handle_video_frame(
            websocket,
            message['data'],
            message['width'],
            message['height'],
            message['timestamp']
        )
    
    elif message['type'] == 'audio':
        # 处理音频帧
        await handle_audio_frame(
            websocket,
            message['data'],
            message['timestamp']
        )
```

### 3. MQTT 处理

```python
def on_mqtt_message(client, userdata, msg):
    """处理 MQTT 消息"""
    if msg.topic.endswith('/video'):
        # 视频消息
        message = parse_binary_protocol2(msg.payload)
        if message['type'] == 'video':
            handle_video_frame(
                message['data'],
                message['width'],
                message['height'],
                message['timestamp']
            )
```

---

## 数据流示例

### 完整的视频帧传输流程

```
设备端：
1. 摄像头捕获图像 (640x480)
2. JPEG 编码 (~15KB)
3. 构造 BinaryProtocol2 消息
   - version: 2
   - type: 0
   - reserved: 0x028001E0 (640 << 16 | 480)
   - timestamp: 12345678
   - payload_size: 15360
   - payload: [JPEG 数据]
4. 通过 WebSocket/MQTT 发送

服务器端：
1. 接收二进制消息
2. 解析头部（16字节）
3. 检查 reserved 字段
   - reserved = 0 → 音频
   - reserved != 0 → 视频
4. 提取宽高：
   - width = (reserved >> 16) & 0xFFFF = 640
   - height = reserved & 0xFFFF = 480
5. 解码 JPEG 数据
6. 处理视频帧
```

---

## 性能分析

### 带宽对比

**方案 A：Base64 编码（未采用）**
- JPEG 原始大小：15KB
- Base64 编码后：20KB
- 开销：+33%

**方案 B：Reserved 字段（已采用）**
- JPEG 原始大小：15KB
- BinaryProtocol2 头部：16字节
- 总大小：15.016KB
- 开销：+0.1%

**结论：** 使用 reserved 字段方案节省约 33% 带宽

### 处理效率

**设备端：**
- 无需 base64 编码：节省 CPU 时间
- 直接内存复制：高效

**服务器端：**
- 无需 base64 解码：节省 CPU 时间
- 简单位运算提取宽高：高效

---

## 兼容性说明

### 向后兼容

**老版本服务器（不支持视频）：**
- 仍可正常处理音频数据（reserved = 0）
- 视频数据会被识别为 type = 0，但 reserved != 0
- 可以选择忽略或记录警告

**新版本服务器（支持视频）：**
- 完全兼容音频数据
- 正确识别和处理视频数据

### 协议版本

- **Version 1**：不支持视频
- **Version 2**：支持视频（通过 reserved 字段）
- **Version 3**：不支持视频（字段不足）

**推荐：** 使用 Version 2 协议

---

## 测试建议

### 单元测试

```python
def test_parse_audio_message():
    """测试音频消息解析"""
    # 构造音频消息（reserved = 0）
    data = struct.pack('<HHIII', 2, 0, 0, 12345, 100) + b'x' * 100
    message = parse_binary_protocol2(data)
    
    assert message['type'] == 'audio'
    assert message['timestamp'] == 12345
    assert len(message['data']) == 100

def test_parse_video_message():
    """测试视频消息解析"""
    # 构造视频消息（640x480）
    reserved = (640 << 16) | 480
    data = struct.pack('<HHIII', 2, 0, reserved, 12345, 100) + b'x' * 100
    message = parse_binary_protocol2(data)
    
    assert message['type'] == 'video'
    assert message['width'] == 640
    assert message['height'] == 480
    assert message['timestamp'] == 12345
    assert len(message['data']) == 100
```

### 集成测试

1. **音频流测试**：验证音频数据正常传输
2. **视频流测试**：验证视频数据正常传输
3. **混合流测试**：验证音视频同时传输
4. **边界测试**：测试极端分辨率（如 1920x1080）

---

## 故障排查

### 常见问题

**Q1: 服务器收到的视频数据宽高为 0？**
- 检查字节序转换（htonl/ntohl）
- 验证 reserved 字段是否正确编码

**Q2: 视频数据被识别为音频？**
- 检查 reserved 字段是否为 0
- 确认宽高值不为 0

**Q3: JPEG 数据损坏？**
- 检查 payload_size 是否正确
- 验证内存复制是否完整

### 调试日志

**设备端：**
```cpp
ESP_LOGD(TAG, "Sending video: %dx%d, size=%zu, reserved=0x%08X", 
         width, height, size, bp2->reserved);
```

**服务器端：**
```python
logger.debug(f"Received video: {width}x{height}, size={len(data)}, "
             f"reserved=0x{reserved:08X}")
```

---

## 总结

### 实现要点

1. ✅ 使用 `reserved` 字段区分音频和视频
2. ✅ 保持 `type = 0` 不变，向后兼容
3. ✅ 宽高编码：`(width << 16) | height`
4. ✅ 注意字节序转换（网络字节序）
5. ✅ WebSocket 和 MQTT 均已实现

### 优势总结

- **高效**：节省 33% 带宽
- **简单**：实现简洁，易于维护
- **兼容**：向后兼容现有协议
- **完整**：元数据和数据在同一消息

### 下一步

- ✅ 设备端实现完成
- ⏳ 服务器端开发
- ⏳ 硬件测试验证
- ⏳ 性能优化

---

**文档版本：** 1.0  
**最后更新：** 2024年  
**状态：** 实现完成，待测试
