# 实时视频对讲功能 - 快速参考

## 快速开始

### 编译和烧录

```bash
# 编译
idf.py build

# 烧录
idf.py flash

# 监控
idf.py monitor
```

---

## 命令参考

### 启动监控模式

**WebSocket/MQTT 消息：**
```json
{
  "type": "system",
  "command": "start_monitor"
}
```

**效果：**
- 设备进入监控模式
- 开始发送视频流
- 继续双向音频传输

### 停止监控模式

**WebSocket/MQTT 消息：**
```json
{
  "type": "system",
  "command": "stop_monitor"
}
```

**效果：**
- 停止视频流
- 设备返回正常模式

---

## 设备状态

| 状态 | 说明 |
|------|------|
| `idle` | 空闲状态 |
| `monitor_connecting` | 监控模式连接中 |
| `monitor_streaming` | 监控模式流媒体传输中 |

---

## 消息格式

### 视频帧元数据（设备→服务器）

```json
{
  "type": "video_frame",
  "timestamp": 12345678,
  "width": 640,
  "height": 480,
  "size": 15360
}
```

### 视频帧二进制数据（设备→服务器）

**BinaryProtocol2 格式：**
```
Header (16 bytes):
  - version: 0x0002
  - type: 0x0002 (TYPE_VIDEO)
  - reserved: 0x00000000
  - timestamp: uint32_t
  - payload_size: uint32_t

Payload:
  - JPEG encoded image data
```

---

## API 参考

### Application 类

```cpp
// 启动监控模式
bool StartMonitorMode();

// 停止监控模式
void StopMonitorMode();

// 检查是否处于监控模式
bool IsMonitorMode() const;
```

### VideoStreamService 类

```cpp
// 启动视频流服务
bool Start(Camera* camera, int fps = 10);

// 停止视频流服务
void Stop();

// 获取下一帧
std::unique_ptr<JpegFrame> GetNextFrame();

// 检查是否运行中
bool IsRunning() const;

// 获取队列大小
size_t GetQueueSize() const;
```

### MonitorService 类

```cpp
// 启动监控服务
bool Start(Protocol* protocol, Camera* camera, AudioService* audio_service);

// 停止监控服务
void Stop();

// 检查是否运行中
bool IsRunning() const;

// 设置状态变化回调
void SetStateChangeCallback(std::function<void(bool)> callback);
```

### Esp32Camera 类

```cpp
// 捕获 JPEG 图像
bool CaptureJpeg(uint8_t** jpeg_data, size_t* jpeg_size, int quality = 80);

// 检查摄像头是否可用
bool IsAvailable() const;

// 获取帧宽度
uint16_t GetFrameWidth() const;

// 获取帧高度
uint16_t GetFrameHeight() const;
```

---

## 配置参数

### 视频流配置

```cpp
// 默认帧率
static constexpr int kDefaultFps = 10;

// 默认 JPEG 质量
static constexpr int kDefaultQuality = 60;

// 最大队列大小
static constexpr size_t kMaxQueueSize = 3;
```

### 性能参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| 分辨率 | 640x480 | VGA |
| 帧率 | 10 fps | 可配置 |
| JPEG 质量 | 60 | 1-100 |
| 队列大小 | 3 帧 | 固定 |

---

## 故障排查

### 问题：摄像头初始化失败

**检查：**
```bash
# 查看日志
idf.py monitor | grep "Esp32Camera"
```

**可能原因：**
- 摄像头硬件未连接
- 引脚配置错误
- 电源不足

### 问题：视频流无输出

**检查：**
```bash
# 查看 VideoStreamService 日志
idf.py monitor | grep "VideoStreamService"
```

**可能原因：**
- 摄像头未初始化
- 服务未启动
- 队列已满

### 问题：编译错误

**检查：**
```bash
# 清理并重新编译
idf.py fullclean
idf.py build
```

---

## 性能监控

### 查看内存使用

```cpp
// 在代码中添加
SystemInfo::PrintHeapStats();
```

### 查看任务状态

```cpp
// 在代码中添加
SystemInfo::PrintTaskList();
SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
```

### 日志级别

```bash
# 设置日志级别
idf.py menuconfig
# Component config → Log output → Default log verbosity
```

---

## 文档索引

| 文档 | 说明 |
|------|------|
| `implementation_plan.md` | 详细实施计划 |
| `implementation_status.md` | 实施状态检查 |
| `server_side_requirements.md` | 服务器端需求 |
| `implementation_summary.md` | 实施总结 |
| `quick_reference.md` | 本文档 |

---

## 联系和支持

**项目仓库：** xiaozhi-esp32  
**相关文档：** `docs/my_docs/`  
**日志标签：** `VideoStreamService`, `MonitorService`, `Esp32Camera`
