# 服务器端协议兼容性修改需求

## 概述

为了支持 ESP32 设备的实时视频对讲功能，服务器端需要进行以下修改以兼容新的协议。

---

## 1. 新增系统命令支持

### 1.1 启动监控模式命令

**命令格式：**
```json
{
  "type": "system",
  "command": "start_monitor"
}
```

**功能：** 指示设备进入监控模式，开始发送视频流

**设备响应：**
- 设备状态变更为 `monitor_streaming`
- 开始发送视频帧数据
- 继续发送音频数据（双向）

### 1.2 停止监控模式命令

**命令格式：**
```json
{
  "type": "system",
  "command": "stop_monitor"
}
```

**功能：** 指示设备退出监控模式，停止发送视频流

**设备响应：**
- 停止发送视频帧
- 设备状态变更为 `idle`
- 恢复正常对话模式

---

## 2. 新增消息类型：video_frame

### 2.1 视频帧数据传输（已实现）

**传输方式：** 使用 BinaryProtocol2 二进制格式

**数据格式：**
- 协议：BinaryProtocol2
- 类型：`type = 0, reserved != 0`
- 内容：JPEG 编码的图像数据
- 元数据：宽度和高度编码在 `reserved` 字段中

**示例：**
```
版本: 2
类型: 0
Reserved: 0x028001E0  (640 << 16 | 480)
时间戳: 12345678
负载大小: 15360
负载: [JPEG 数据...]
```

**优势：**
- ✅ 高效：直接二进制传输，无需 base64 编码
- ✅ 完整：元数据和图像数据在同一消息中
- ✅ 兼容：复用现有的 BinaryProtocol2 结构

---

## 3. 二进制数据传输协议（已实现）

### 3.1 BinaryProtocol2 结构

**协议结构：**
```c
struct BinaryProtocol2 {
    uint16_t version;      // 协议版本 (2)
    uint16_t type;         // 消息类型 (0: OPUS/VIDEO, 1: JSON)
    uint32_t reserved;     // 保留字段 - 用于区分音频和视频
    uint32_t timestamp;    // 时间戳（毫秒）
    uint32_t payload_size; // 负载大小（字节）
    uint8_t payload[];     // 负载数据
} __attribute__((packed));
```

**数据类型区分（通过 reserved 字段）：**
```c
// 音频数据
type = 0, reserved = 0  → OPUS 音频

// 视频数据
type = 0, reserved != 0 → JPEG 视频
  - High 16 bits: width (图像宽度)
  - Low 16 bits: height (图像高度)
```

**设计优势：**
- ✅ 向后兼容：不改变现有的 type 定义
- ✅ 简单高效：只需检查 reserved 字段
- ✅ 无需新增消息类型

### 3.2 视频帧二进制消息格式

**完整消息结构：**
```
+----------------+----------------+--------------------------------+
|   version(2)   |    type(2)     |         reserved(4)            |
|     0x0002     |     0x0000     |  (width << 16) | height        |
+----------------+----------------+--------------------------------+
|           timestamp(4)          |        payload_size(4)         |
+----------------+----------------+--------------------------------+
|                    JPEG payload data...                          |
+------------------------------------------------------------------+
```

**字段说明：**
- `version`: 0x0002 (协议版本 2)
- `type`: 0x0000 (与音频相同)
- `reserved`: 
  - 音频时为 0x00000000
  - 视频时为 `(width << 16) | height`
  - 例如：640x480 → 0x02800 1E0
- `timestamp`: 帧捕获时间戳（毫秒）
- `payload_size`: JPEG 数据大小（字节）
- `payload`: JPEG 编码的图像数据

### 3.3 服务器端解析示例（Python）

```python
import struct

def parse_binary_message(data):
    """解析 BinaryProtocol2 格式的消息"""
    # 解析头部（16字节）
    header = struct.unpack('<HHIII', data[:16])
    version, msg_type, reserved, timestamp, payload_size = header
    
    # 提取负载数据
    payload = data[16:16+payload_size]
    
    if msg_type == 0:
        # type = 0: 可能是音频或视频
        if reserved == 0:
            # reserved = 0: 音频数据（OPUS）
            return {
                'type': 'audio',
                'format': 'opus',
                'timestamp': timestamp,
                'data': payload
            }
        else:
            # reserved != 0: 视频数据（JPEG）
            # 从 reserved 字段提取宽高
            width = (reserved >> 16) & 0xFFFF
            height = reserved & 0xFFFF
            
            return {
                'type': 'video',
                'format': 'jpeg',
                'timestamp': timestamp,
                'width': width,
                'height': height,
                'size': payload_size,
                'data': payload
            }
    
    elif msg_type == 1:
        # type = 1: JSON 消息
        json_str = payload.decode('utf-8')
        return {
            'type': 'json',
            'data': json.loads(json_str)
        }
    
    else:
        raise ValueError(f"Unknown message type: {msg_type}")

# 使用示例
def on_websocket_binary(data):
    message = parse_binary_message(data)
    
    if message['type'] == 'video':
        # 处理视频帧
        print(f"Received video frame: {message['width']}x{message['height']}, "
              f"size={message['size']}, timestamp={message['timestamp']}")
        # 解码 JPEG
        image = decode_jpeg(message['data'])
        # 进一步处理...
    
    elif message['type'] == 'audio':
        # 处理音频帧
        print(f"Received audio frame: size={len(message['data'])}, "
              f"timestamp={message['timestamp']}")
        # 解码 OPUS
        audio = decode_opus(message['data'])
        # 进一步处理...
```

---

## 4. WebSocket 协议适配

### 4.1 消息类型

服务器需要处理两种 WebSocket 消息类型：

1. **文本消息（Text Frame）**
   - JSON 格式的控制消息
   - 包括：system 命令、video_frame 元数据等

2. **二进制消息（Binary Frame）**
   - BinaryProtocol2 格式的数据
   - 包括：音频数据（OPUS）、视频数据（JPEG）

### 4.2 消息处理流程

```
客户端 → 服务器
├─ 文本消息
│  ├─ {"type": "system", "command": "start_monitor"}
│  └─ {"type": "video_frame", ...}  (元数据)
│
└─ 二进制消息
   ├─ BinaryProtocol2 (type=0, OPUS 音频)
   └─ BinaryProtocol2 (type=2, JPEG 视频)  ← 新增

服务器 → 客户端
├─ 文本消息
│  ├─ {"type": "system", "command": "start_monitor"}
│  ├─ {"type": "tts", "state": "start"}
│  └─ {"type": "stt", "text": "..."}
│
└─ 二进制消息
   └─ BinaryProtocol2 (type=0, OPUS 音频)
```

---

## 5. MQTT 协议适配（可选）

### 5.1 Topic 结构

**建议的 Topic 结构：**
```
device/{device_id}/video/frame    # 视频帧数据
device/{device_id}/video/meta     # 视频元数据
device/{device_id}/audio/up       # 上行音频（设备→服务器）
device/{device_id}/audio/down     # 下行音频（服务器→设备）
device/{device_id}/control        # 控制命令
```

### 5.2 消息格式

**视频帧数据：**
- Topic: `device/{device_id}/video/frame`
- Payload: 原始 JPEG 二进制数据
- QoS: 0 (不保证送达，允许丢帧)

**视频元数据：**
- Topic: `device/{device_id}/video/meta`
- Payload: JSON 格式
```json
{
  "timestamp": 12345678,
  "width": 640,
  "height": 480,
  "size": 15360,
  "sequence": 123
}
```

---

## 6. 服务器端实现建议

### 6.1 视频流处理

```python
class VideoStreamHandler:
    def __init__(self):
        self.frame_buffer = []
        self.last_timestamp = 0
    
    def on_video_frame(self, frame_data):
        """处理接收到的视频帧"""
        # 解码 JPEG
        image = decode_jpeg(frame_data['data'])
        
        # 存储或转发
        self.frame_buffer.append({
            'timestamp': frame_data['timestamp'],
            'image': image,
            'width': frame_data['width'],
            'height': frame_data['height']
        })
        
        # 限制缓冲区大小
        if len(self.frame_buffer) > 30:  # 保留最近3秒（10fps）
            self.frame_buffer.pop(0)
    
    def get_latest_frame(self):
        """获取最新帧"""
        if self.frame_buffer:
            return self.frame_buffer[-1]
        return None
```

### 6.2 监控模式状态管理

```python
class MonitorModeManager:
    def __init__(self):
        self.active_sessions = {}
    
    def start_monitor(self, device_id):
        """启动监控模式"""
        if device_id not in self.active_sessions:
            self.active_sessions[device_id] = {
                'start_time': time.time(),
                'video_handler': VideoStreamHandler(),
                'audio_handler': AudioStreamHandler(),
                'stats': {
                    'video_frames': 0,
                    'audio_frames': 0,
                    'bytes_received': 0
                }
            }
        
        # 发送启动命令到设备
        self.send_command(device_id, {
            'type': 'system',
            'command': 'start_monitor'
        })
    
    def stop_monitor(self, device_id):
        """停止监控模式"""
        if device_id in self.active_sessions:
            # 发送停止命令
            self.send_command(device_id, {
                'type': 'system',
                'command': 'stop_monitor'
            })
            
            # 清理会话
            del self.active_sessions[device_id]
```

### 6.3 性能优化建议

1. **帧缓冲管理**
   - 使用循环缓冲区
   - 限制缓冲区大小（避免内存溢出）
   - 实现丢帧策略（保留关键帧）

2. **网络优化**
   - 使用 WebSocket 二进制帧（减少开销）
   - 考虑使用 WebRTC（更低延迟）
   - 实现自适应码率控制

3. **并发处理**
   - 使用异步 I/O 处理多个设备
   - 视频解码使用独立线程池
   - 实现背压机制（防止过载）

---

## 7. 数据流示例

### 7.1 启动监控模式流程

```
服务器                                    设备
  |                                        |
  |  {"type":"system","command":"start_monitor"}
  |--------------------------------------->|
  |                                        |
  |                                   [启动视频流]
  |                                   [启动音频流]
  |                                        |
  |  {"type":"video_frame","timestamp":...}
  |<---------------------------------------|
  |                                        |
  |  [BinaryProtocol2: TYPE_VIDEO, JPEG]  |
  |<---------------------------------------|
  |                                        |
  |  [BinaryProtocol2: TYPE_OPUS, Audio]  |
  |<---------------------------------------|
  |                                        |
  |  [BinaryProtocol2: TYPE_OPUS, Audio]  |
  |--------------------------------------->|
  |                                        |
```

### 7.2 数据包频率估算

**假设配置：**
- 视频：640x480, JPEG 质量 60, 10 fps
- 音频：24kHz, 60ms 帧, OPUS 编码

**预估带宽：**
- 视频：~15KB/帧 × 10fps = 150 KB/s = 1.2 Mbps
- 音频上行：~2KB/帧 × 16.7fps = 33 KB/s = 0.26 Mbps
- 音频下行：~2KB/帧 × 16.7fps = 33 KB/s = 0.26 Mbps
- **总计：~216 KB/s = 1.7 Mbps**

---

## 8. 测试建议

### 8.1 功能测试

```python
def test_monitor_mode():
    # 1. 发送启动命令
    send_command({'type': 'system', 'command': 'start_monitor'})
    
    # 2. 等待视频帧
    frame = wait_for_video_frame(timeout=5)
    assert frame is not None
    assert frame['width'] > 0
    assert frame['height'] > 0
    
    # 3. 验证音频流
    audio = wait_for_audio_frame(timeout=1)
    assert audio is not None
    
    # 4. 发送停止命令
    send_command({'type': 'system', 'command': 'stop_monitor'})
    
    # 5. 验证流停止
    time.sleep(2)
    assert no_video_frames_received()
```

### 8.2 性能测试

- 测试持续运行 1 小时
- 监控内存使用
- 统计丢帧率
- 测量端到端延迟

---

## 9. 兼容性注意事项

### 9.1 向后兼容

- 新增的 `TYPE_VIDEO` 不影响现有的音频和 JSON 消息
- 未启动监控模式时，设备行为与之前完全一致
- 服务器可以选择性支持监控模式

### 9.2 版本检测

建议在设备连接时交换能力信息：

```json
{
  "type": "device_info",
  "capabilities": {
    "video": true,
    "video_formats": ["jpeg"],
    "max_resolution": "640x480",
    "max_fps": 15
  }
}
```

---

## 10. 总结

### 必须实现的功能：

1. ✅ **系统命令处理**
   - `start_monitor` 命令
   - `stop_monitor` 命令

2. ✅ **消息类型支持**
   - `video_frame` JSON 消息（元数据）
   - `TYPE_VIDEO` 二进制消息（JPEG 数据）

3. ✅ **WebSocket 二进制帧处理**
   - 解析 BinaryProtocol2 格式
   - 区分音频和视频数据

### 可选优化：

- 实现 MQTT 支持
- 添加自适应码率控制
- 实现 WebRTC 低延迟传输
- 添加视频录制功能

### 开发优先级：

1. **高优先级：** 实现基本的命令处理和视频帧接收
2. **中优先级：** 优化性能和稳定性
3. **低优先级：** 添加高级功能（录制、回放等）
