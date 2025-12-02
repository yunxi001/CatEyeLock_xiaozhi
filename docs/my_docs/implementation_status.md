# 实时视频对讲功能实施状态检查

## 阶段完成情况总览

| 阶段 | 状态 | 完成度 | 备注 |
|------|------|--------|------|
| 阶段一：基础设施准备 | ✅ 完成 | 100% | 所有任务已完成 |
| 阶段二：Esp32Camera 扩展 | ✅ 完成 | 100% | 已实现 CaptureJpeg 等方法 |
| 阶段三：VideoStreamService | ✅ 完成 | 100% | 已实现视频流服务 |
| 阶段四：Protocol 扩展 | ✅ 完成 | 100% | 已实现完整的视频数据传输 |
| 阶段五：MonitorService | ✅ 完成 | 100% | 已实现监控服务 |
| 阶段六：Application 集成 | ✅ 完成 | 100% | 已集成到 Application |
| 阶段七：集成测试 | ⏳ 待进行 | 0% | 需要硬件测试 |

---

## 详细检查结果

### ✅ 阶段一：基础设施准备（100%）

#### 任务 1.1：创建目录结构
- ✅ 1.1.1 创建 `main/video/` 目录
- ✅ 1.1.2 创建 `main/monitor/` 目录
- ✅ 1.1.3 创建头文件和源文件
- ✅ 1.1.4 修改 `main/CMakeLists.txt`

**验证：**
- `main/video/video_stream_service.h` ✅
- `main/video/video_stream_service.cc` ✅
- `main/video/jpeg_frame.h` ✅
- `main/monitor/monitor_service.h` ✅
- `main/monitor/monitor_service.cc` ✅

#### 任务 1.2：扩展设备状态定义
- ✅ 1.2.1 在 `device_state.h` 中新增状态枚举
- ✅ 1.2.2 在 `application.cc` 的 `STATE_STRINGS` 数组中添加字符串

**验证：**
```cpp
kDeviceStateMonitorConnecting,  // 已添加
kDeviceStateMonitorStreaming,   // 已添加
```

#### 任务 1.3：定义 JpegFrame 结构
- ✅ 1.3.1 创建 `jpeg_frame.h` 文件
- ✅ 1.3.2 实现 JpegFrame 结构体（使用 std::vector<uint8_t>）

---

### ✅ 阶段二：Esp32Camera 扩展（100%）

#### 任务 2.1：新增接口
- ✅ 在 `esp32_camera.h` 中声明新接口
- ✅ 实现 `CaptureJpeg()` 方法
- ✅ 实现 `IsAvailable()` 方法
- ✅ 实现 `GetFrameWidth()` 和 `GetFrameHeight()` 方法
- ✅ 编译测试通过

**实现的接口：**
```cpp
bool CaptureJpeg(uint8_t** jpeg_data, size_t* jpeg_size, int quality = 80);
bool IsAvailable() const;
uint16_t GetFrameWidth() const { return frame_.width; }
uint16_t GetFrameHeight() const { return frame_.height; }
```

---

### ✅ 阶段三：VideoStreamService 实现（100%）

#### 已实现功能：
- ✅ 单例模式（通过构造函数实现）
- ✅ 视频捕获任务 `CaptureTask()`
- ✅ 帧队列管理（使用 std::queue）
- ✅ 帧率控制（通过 vTaskDelay）
- ✅ JPEG 编码集成
- ✅ Start/Stop 生命周期管理
- ✅ GetNextFrame() 接口
- ✅ 队列大小管理（最大3帧）

**关键实现：**
- 使用 FreeRTOS 任务进行视频捕获
- 使用互斥锁保护帧队列
- 自动丢弃最旧帧当队列满时
- 支持配置帧率和质量

---

### ✅ 阶段四：Protocol 扩展（100%）

#### 已完成：
- ✅ 扩展 `BinaryProtocol2` 结构，使用 `reserved` 字段区分音频和视频
- ✅ 添加 `SendVideo()` 虚方法到 Protocol 基类
- ✅ 在 WebsocketProtocol 中实现视频数据发送
- ✅ 在 MqttProtocol 中实现视频数据发送
- ✅ 更新 MonitorService 使用新的 SendVideo 方法

**实现方案：**
```cpp
// BinaryProtocol2 结构
struct BinaryProtocol2 {
    uint16_t version;
    uint16_t type;          // 0: OPUS/VIDEO (通过 reserved 区分)
    uint32_t reserved;      // 音频: 0
                            // 视频: (width << 16) | height
    uint32_t timestamp;
    uint32_t payload_size;
    uint8_t payload[];
};

// Protocol 基类
virtual bool SendVideo(const uint8_t* data, size_t size, 
                      uint32_t timestamp, uint16_t width, uint16_t height);

// WebsocketProtocol 实现
bool WebsocketProtocol::SendVideo(...) {
    bp2->type = 0;
    bp2->reserved = htonl(((uint32_t)width << 16) | (uint32_t)height);
    // 发送 JPEG 数据
}
```

**区分规则：**
- `type = 0, reserved = 0` → 音频数据（OPUS）
- `type = 0, reserved != 0` → 视频数据（JPEG），reserved 包含宽高信息

---

### ✅ 阶段五：MonitorService 实现（100%）

#### 已实现功能：
- ✅ MonitorService 类框架
- ✅ Start/Stop 生命周期管理
- ✅ 视频传输任务 `VideoTransmitTask()`
- ✅ 与 VideoStreamService 集成
- ✅ 状态变化回调机制
- ✅ 视频帧发送逻辑

**关键实现：**
- 创建独立的传输任务
- 从 VideoStreamService 获取帧
- 通过 Protocol 发送帧数据
- 支持状态回调通知

---

### ✅ 阶段六：Application 集成（100%）

#### 已实现功能：
- ✅ 在 Application.h 中添加 MonitorService 成员
- ✅ 实现 `StartMonitorMode()` 方法
- ✅ 实现 `StopMonitorMode()` 方法
- ✅ 实现 `IsMonitorMode()` 方法
- ✅ 在消息处理中添加 "start_monitor" 命令
- ✅ 在消息处理中添加 "stop_monitor" 命令
- ✅ 状态变化回调集成

**命令处理：**
```cpp
// 支持的系统命令：
{"type": "system", "command": "start_monitor"}
{"type": "system", "command": "stop_monitor"}
```

---

### ⏳ 阶段七：集成测试（0%）

#### 待进行的测试：
- ⏳ 基础功能测试
- ⏳ 模式切换测试
- ⏳ 性能测试
- ⏳ 稳定性测试
- ⏳ 服务器端配合测试

**需要硬件设备才能进行测试**

---

## 编译状态

✅ **所有代码编译通过，无错误**

```bash
idf.py build  # 成功
```

---

## 已完成的优化

### 1. Protocol 层视频数据传输（已完成）✅

**实现方案：** 使用 `reserved` 字段区分音频和视频

**技术细节：**
- 复用 BinaryProtocol2 结构，保持 `type = 0`
- 使用 `reserved` 字段编码视频元数据：
  - 音频：`reserved = 0`
  - 视频：`reserved = (width << 16) | height`
- 服务器端通过检查 `reserved` 字段判断数据类型

**优势：**
- 向后兼容：不改变现有的 type 定义
- 高效：直接使用二进制传输，无需 base64 编码
- 简单：服务器端只需检查一个字段即可区分

**实现代码：**
```cpp
// WebsocketProtocol::SendVideo()
bp2->type = 0;  // 保持与音频相同
bp2->reserved = htonl(((uint32_t)width << 16) | (uint32_t)height);
bp2->timestamp = htonl(timestamp);
bp2->payload_size = htonl(size);
memcpy(bp2->payload, data, size);
```

### 2. 性能优化（优先级：中）

- 考虑使用硬件 JPEG 编码器（如果 ESP32-S3 支持）
- 优化帧缓冲区大小
- 实现自适应帧率和质量

### 3. 错误处理（优先级：中）

- 添加更完善的错误恢复机制
- 网络断开时的重连逻辑
- 摄像头异常时的处理

---

## 总结

### 已完成的核心功能：
1. ✅ 完整的视频捕获和编码流程
2. ✅ 视频流服务管理
3. ✅ 监控服务框架
4. ✅ 应用层集成
5. ✅ 命令处理和状态管理

### 需要完善的部分：
1. ⚠️ 视频数据的网络传输（当前只发送元数据）
2. ⏳ 硬件测试和性能验证
3. ⏳ 服务器端配合开发

### 下一步建议：
1. **立即：** 完善 Protocol 层的视频数据传输
2. **然后：** 进行硬件测试
3. **最后：** 根据测试结果进行性能优化
