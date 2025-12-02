# 视频传输协议规范

## 概述

本文档描述了 xiaozhi-esp32 设备与服务器之间的视频数据传输协议。该协议基于现有的 BinaryProtocol2 格式，通过 `reserved` 字段来区分音频和视频数据。

---

## 1. 协议格式

### 1.1 BinaryProtocol2 结构

```c
struct BinaryProtocol2 {
    uint16_t version;      // 协议版本，固定为 2
    uint16_t type;         // 消息类型，固定为 1 (原 JSON 类型)
    uint32_t reserved;     // 保留字段，用于区分数据类型和存储额外信息
    uint32_t timestamp;    // 时间戳（毫秒）
    uint32_t payload_size; // 负载数据大小（字节）
    uint8_t payload[];     // 负载数据
} __attribute__((packed));
```

**总大小：** 16 字节（头部）+ payload_size（负载）

---

## 2. Reserved 字段定义

`reserved` 字段（32位）的使用方式：

### 2.1 音频数据（OPUS）
```
reserved = 0x00000000  // 全零表示音频数据
```

### 2.2 视频数据（JPEG）
```
reserved = (width << 16) | height
```
- **高16位：** 视频宽度（像素）
- **低16位：** 视频高度（像素）

**示例：**
- 640x480 视频：`reserved = (640 << 16) | 480 = 0x028001E0`
- 320x240 视频：`reserved = (320 << 16) | 240 = 0x014000F0`
- 800x600 视频：`reserved = (800 << 16) | 600 = 0x03200258`

---

## 3. 消息类型识别

服务器端可以通过以下逻辑识别消息类型：

```python
def identify_message_type(header):
    if header.reserved == 0:
        return "audio"  # OPUS 音频数据
    else:
        return "video"  # JPEG 视频数据

def parse_video_info(header):
    if header.reserved != 0:
        width = (header.reserved >> 16) & 0xFFFF
        height = header.reserved & 0xFFFF
        return width, height
    return None, None
```

---

## 4. 完整消息格式

### 4.1 音频消息

```
+----------------+----------------+----------------+----------------+
|   version(2)   |    type(2)     |         reserved(4) = 0         |
+----------------+----------------+----------------+----------------+
|           timestamp(4)          |        payload_size(4)          |
+----------------+----------------+----------------+----------------+
|                    OPUS audio data...                            |
+-------------------------------------------------------------------+
```

**字段值：**
- `version`: 0x0002
- `type`: 0x0001
- `reserved`: 0x00000000
- `timestamp`: 音频帧时间戳（毫秒）
- `payload_size`: OPUS 数据大小
- `payload`: OPUS 编码的音频数据

### 4.2 视频消息

```
+----------------+----------------+----------------+----------------+
|   version(2)   |    type(2)     |         reserved(4)             |
|                                 |   width(2)   |   height(2)    |
+----------------+----------------+----------------+----------------+
|           timestamp(4)          |        payload_size(4)          |
+----------------+----------------+----------------+----------------+
|                    JPEG video data...                            |
+-------------------------------------------------------------------+
```

**字段值：**
- `version`: 0x0002
- `type`: 0x0001
- `reserved`: `(width << 16) | height`
- `timestamp`: 视频帧时间戳（毫秒）
- `payload_size`: JPEG 数据大小
- `payload`: JPEG 编码的视频数据

---

## 5. 数据流示例

### 5.1 示例1：640x480 视频帧

**二进制数据（十六进制）：**
```
02 00 01 00 80 02 E0 01 34 12 00 00 00 40 00 00 FF D8 FF E0 ...
│  │  │  │  │     │     │        │  │        │  │  └─ JPEG 数据开始
│  │  │  │  │     │     │        │  │        │  └─ payload_size (16384)
│  │  │  │  │     │     │        │  │        └─ payload_size 高位
│  │  │  │  │     │     │        │  └─ timestamp (0x00001234)
│  │  │  │  │     │     │        └─ timestamp 高位
│  │  │  │  │     │     └─ height (480 = 0x01E0)
│  │  │  │  │     └─ width (640 = 0x0280)
│  │  │  │  └─ reserved 字段
│  │  │  └─ type (1)
│  │  └─ type 高位
│  └─ version (2)
└─ version 高位
```

### 5.2 示例2：OPUS 音频帧

**二进制数据（十六进制）：**
```
02 00 01 00 00 00 00 00 56 12 00 00 80 00 00 00 FC 00 A4 ...
│  │  │  │  │           │  │        │  │        │  │  └─ OPUS 数据开始
│  │  │  │  │           │  │        │  │        │  └─ payload_size (128)
│  │  │  │  │           │  │        │  │        └─ payload_size 高位
│  │  │  │  │           │  │        │  └─ timestamp (0x00001256)
│  │  │  │  │           │  │        └─ timestamp 高位
│  │  │  │  │           │  └─ reserved = 0 (音频标识)
│  │  │  │  │           └─ reserved 高位
│  │  │  │  └─ reserved 字段
│  │  │  └─ type (1)
│  │  └─ type 高位
│  └─ version (2)
└─ version 高位
```

---

## 6. 字节序

**所有多字节字段使用小端序（Little-Endian）**

示例：
- `version = 2` → 字节序列：`02 00`
- `width = 640` → 字节序列：`80 02`
- `timestamp = 0x12345678` → 字节序列：`78 56 34 12`

---

## 7. 服务器端实现参考

### 7.1 Python 实现

```python
import struct

class BinaryProtocol2Parser:
    def __init__(self):
        self.header_format = '<HHIII'  # 小端序: version, type, reserved, timestamp, payload_size
        self.header_size = struct.calcsize(self.header_format)  # 16 字节
    
    def parse_message(self, data):
        """解析 BinaryProtocol2 消息"""
        if len(data) < self.header_size:
            raise ValueError(f"Data too short for header: {len(data)} < {self.header_size}")
        
        # 解析头部
        header = struct.unpack(self.header_format, data[:self.header_size])
        version, msg_type, reserved, timestamp, payload_size = header
        
        # 验证协议版本
        if version != 2:
            raise ValueError(f"Unsupported protocol version: {version}")
        
        # 验证消息类型
        if msg_type != 1:
            raise ValueError(f"Unsupported message type: {msg_type}")
        
        # 提取负载数据
        payload = data[self.header_size:self.header_size + payload_size]
        
        if len(payload) != payload_size:
            raise ValueError(f"Payload size mismatch: expected {payload_size}, got {len(payload)}")
        
        # 识别数据类型
        if reserved == 0:
            return self._parse_audio(payload, timestamp)
        else:
            return self._parse_video(payload, timestamp, reserved)
    
    def _parse_audio(self, payload, timestamp):
        """解析音频数据"""
        return {
            'type': 'audio',
            'format': 'opus',
            'timestamp': timestamp,
            'data': payload,
            'size': len(payload)
        }
    
    def _parse_video(self, payload, timestamp, reserved):
        """解析视频数据"""
        width = (reserved >> 16) & 0xFFFF
        height = reserved & 0xFFFF
        
        return {
            'type': 'video',
            'format': 'jpeg',
            'timestamp': timestamp,
            'width': width,
            'height': height,
            'data': payload,
            'size': len(payload)
        }

# 使用示例
parser = BinaryProtocol2Parser()

def on_websocket_binary_message(data):
    """WebSocket 二进制消息处理"""
    try:
        message = parser.parse_message(data)
        
        if message['type'] == 'video':
            print(f"Received video frame: {message['width']}x{message['height']}, "
                  f"{message['size']} bytes, timestamp={message['timestamp']}")
            # 处理 JPEG 数据
            process_video_frame(message['data'], message['width'], message['height'])
            
        elif message['type'] == 'audio':
            print(f"Received audio frame: {message['size']} bytes, "
                  f"timestamp={message['timestamp']}")
            # 处理 OPUS 数据
            process_audio_frame(message['data'])
            
    except Exception as e:
        print(f"Failed to parse message: {e}")

def process_video_frame(jpeg_data, width, height):
    """处理视频帧"""
    # 保存到文件
    with open(f'frame_{width}x{height}.jpg', 'wb') as f:
        f.write(jpeg_data)
    
    # 或者使用 PIL 显示
    from PIL import Image
    import io
    image = Image.open(io.BytesIO(jpeg_data))
    image.show()

def process_audio_frame(opus_data):
    """处理音频帧"""
    # 使用 opus 解码器处理
    pass
```

### 7.2 JavaScript 实现

```javascript
class BinaryProtocol2Parser {
    constructor() {
        this.headerSize = 16; // 2+2+4+4+4 bytes
    }
    
    parseMessage(arrayBuffer) {
        /**
         * 解析 BinaryProtocol2 消息
         * @param {ArrayBuffer} arrayBuffer - 二进制数据
         * @returns {Object} 解析后的消息对象
         */
        if (arrayBuffer.byteLength < this.headerSize) {
            throw new Error(`Data too short for header: ${arrayBuffer.byteLength} < ${this.headerSize}`);
        }
        
        const view = new DataView(arrayBuffer);
        
        // 解析头部（小端序）
        const version = view.getUint16(0, true);
        const type = view.getUint16(2, true);
        const reserved = view.getUint32(4, true);
        const timestamp = view.getUint32(8, true);
        const payloadSize = view.getUint32(12, true);
        
        // 验证协议
        if (version !== 2 || type !== 1) {
            throw new Error(`Invalid protocol: version=${version}, type=${type}`);
        }
        
        // 提取负载
        const payload = arrayBuffer.slice(this.headerSize, this.headerSize + payloadSize);
        
        if (payload.byteLength !== payloadSize) {
            throw new Error(`Payload size mismatch: expected ${payloadSize}, got ${payload.byteLength}`);
        }
        
        // 识别数据类型
        if (reserved === 0) {
            return this.parseAudio(payload, timestamp);
        } else {
            return this.parseVideo(payload, timestamp, reserved);
        }
    }
    
    parseAudio(payload, timestamp) {
        /**
         * 解析音频数据
         */
        return {
            type: 'audio',
            format: 'opus',
            timestamp: timestamp,
            data: payload,
            size: payload.byteLength
        };
    }
    
    parseVideo(payload, timestamp, reserved) {
        /**
         * 解析视频数据
         */
        const width = (reserved >>> 16) & 0xFFFF;
        const height = reserved & 0xFFFF;
        
        return {
            type: 'video',
            format: 'jpeg',
            timestamp: timestamp,
            width: width,
            height: height,
            data: payload,
            size: payload.byteLength
        };
    }
}

// 使用示例
const parser = new BinaryProtocol2Parser();

// WebSocket 使用
websocket.onmessage = function(event) {
    if (event.data instanceof ArrayBuffer) {
        try {
            const message = parser.parseMessage(event.data);
            
            if (message.type === 'video') {
                console.log(`Received video frame: ${message.width}x${message.height}, ` +
                           `${message.size} bytes, timestamp=${message.timestamp}`);
                
                // 创建 Blob 并显示图像
                const blob = new Blob([message.data], { type: 'image/jpeg' });
                const url = URL.createObjectURL(blob);
                document.getElementById('videoFrame').src = url;
                
                // 记得释放 URL
                setTimeout(() => URL.revokeObjectURL(url), 1000);
                
            } else if (message.type === 'audio') {
                console.log(`Received audio frame: ${message.size} bytes, ` +
                           `timestamp=${message.timestamp}`);
                // 处理音频数据
                processAudioFrame(message.data);
            }
            
        } catch (error) {
            console.error('Failed to parse message:', error);
        }
    }
};

function processAudioFrame(opusData) {
    // 使用 Web Audio API 处理 OPUS 数据
    // ...
}
```

---

## 8. 性能考虑

### 8.1 带宽使用

**视频流（640x480, 10fps, JPEG质量60）：**
- 单帧大小：~15KB
- 帧率：10fps
- 视频带宽：15KB × 10fps = 150KB/s = 1.2Mbps
- 协议开销：16字节/帧 × 10fps = 160字节/s（0.1%，可忽略）

**音频流（双向，OPUS 24kHz）：**
- 单帧大小：~2KB
- 帧率：~16.7fps（60ms/帧）
- 音频带宽：2KB × 16.7fps × 2方向 = 67KB/s = 0.54Mbps

**总带宽：** ~1.7Mbps

### 8.2 内存使用

**设备端：**
- 视频帧缓冲：3帧 × 15KB = 45KB（PSRAM）
- 传输缓冲：1帧 × (16字节 + 15KB) ≈ 15KB（PSRAM）
- 总计：~60KB PSRAM

**服务器端：**
- 解析缓冲：~15KB/连接
- 帧缓冲：根据需要（建议3-5帧）

---

## 9. 测试和验证

### 9.1 单元测试

```python
import struct

def test_video_message_parsing():
    """测试视频消息解析"""
    # 构造测试数据
    width, height = 640, 480
    timestamp = 12345678
    jpeg_data = b'\xff\xd8\xff\xe0' + b'\x00' * 100  # 模拟 JPEG
    
    # 构造消息
    reserved = (width << 16) | height
    header = struct.pack('<HHIII', 2, 1, reserved, timestamp, len(jpeg_data))
    message = header + jpeg_data
    
    # 解析测试
    parser = BinaryProtocol2Parser()
    result = parser.parse_message(message)
    
    assert result['type'] == 'video'
    assert result['width'] == width
    assert result['height'] == height
    assert result['timestamp'] == timestamp
    assert result['data'] == jpeg_data
    print("✅ 视频消息解析测试通过")

def test_audio_message_parsing():
    """测试音频消息解析"""
    # 构造音频测试数据
    timestamp = 87654321
    opus_data = b'\xfc\x00\xa4' + b'\x00' * 50  # 模拟 OPUS
    
    # 构造消息
    header = struct.pack('<HHIII', 2, 1, 0, timestamp, len(opus_data))
    message = header + opus_data
    
    # 解析测试
    parser = BinaryProtocol2Parser()
    result = parser.parse_message(message)
    
    assert result['type'] == 'audio'
    assert result['timestamp'] == timestamp
    assert result['data'] == opus_data
    print("✅ 音频消息解析测试通过")

def test_width_height_extraction():
    """测试宽高提取"""
    test_cases = [
        (640, 480),
        (320, 240),
        (800, 600),
        (1280, 720),
    ]
    
    for width, height in test_cases:
        reserved = (width << 16) | height
        extracted_width = (reserved >> 16) & 0xFFFF
        extracted_height = reserved & 0xFFFF
        
        assert extracted_width == width
        assert extracted_height == height
        print(f"✅ {width}x{height} 提取测试通过")

# 运行测试
if __name__ == '__main__':
    test_video_message_parsing()
    test_audio_message_parsing()
    test_width_height_extraction()
    print("\n🎉 所有测试通过！")
```

### 9.2 集成测试

```python
def integration_test():
    """端到端集成测试"""
    import time
    
    # 模拟设备发送
    def simulate_device_send():
        width, height = 640, 480
        timestamp = int(time.time() * 1000)
        jpeg_data = b'\xff\xd8\xff\xe0' + b'\x00' * 1000
        
        reserved = (width << 16) | height
        header = struct.pack('<HHIII', 2, 1, reserved, timestamp, len(jpeg_data))
        return header + jpeg_data
    
    # 模拟服务器接收
    parser = BinaryProtocol2Parser()
    data = simulate_device_send()
    message = parser.parse_message(data)
    
    assert message['type'] == 'video'
    assert message['width'] == 640
    assert message['height'] == 480
    print("✅ 端到端集成测试通过")

integration_test()
```

---

## 10. 错误处理

### 10.1 常见错误

1. **协议版本不匹配**
   - 错误码：`PROTOCOL_VERSION_MISMATCH`
   - 处理：拒绝连接或降级处理

2. **负载大小不匹配**
   - 错误码：`PAYLOAD_SIZE_MISMATCH`
   - 处理：丢弃消息，记录错误

3. **无效的视频尺寸**
   - 错误码：`INVALID_VIDEO_DIMENSIONS`
   - 处理：使用默认尺寸或丢弃帧

4. **JPEG 解码失败**
   - 错误码：`JPEG_DECODE_ERROR`
   - 处理：丢弃帧，请求重传（可选）

### 10.2 错误恢复

```python
def handle_parse_error(error, data):
    """错误处理"""
    if isinstance(error, ProtocolVersionError):
        # 协议版本错误，断开连接
        disconnect_client()
        log_error(f"Protocol version mismatch: {error}")
    elif isinstance(error, PayloadSizeError):
        # 负载大小错误，丢弃消息
        log_warning(f"Payload size mismatch: {error}")
    else:
        # 其他错误，尝试恢复
        log_error(f"Parse error: {error}")
```

---

## 11. 版本历史

- **v1.0** (2024): 初始版本，基于 BinaryProtocol2，使用 reserved 字段区分音视频

---

## 12. 总结

本协议规范定义了一种向后兼容的方式来传输视频数据，通过复用现有的 BinaryProtocol2 格式和巧妙使用 reserved 字段，实现了音频和视频数据的统一传输。

**优点：**
- ✅ 向后兼容现有音频传输
- ✅ 协议开销小（仅16字节头部）
- ✅ 实现简单，易于调试
- ✅ 支持多种传输方式（WebSocket/MQTT）

**适用场景：**
- 实时视频监控
- 双向音视频对讲
- 智能家居设备
- 安防监控系统
