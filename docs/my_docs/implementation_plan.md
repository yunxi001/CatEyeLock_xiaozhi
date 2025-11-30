# 实时视频对讲功能实施计划

## 项目概述

基于 `xiaozhi-esp32` 项目，新增监控模式（实时视频对讲）功能。

- **目标**：发送音视频流，接收音频流
- **硬件**：ESP32-S3-N16R8 (512KB SRAM + 8MB PSRAM)
- **原则**：最小修改、最大复用

---

## 阶段一：基础设施准备（1天）

### 任务 1.1：创建目录结构

```
main/
├── video/
│   ├── video_stream_service.h
│   ├── video_stream_service.cc
│   └── jpeg_frame.h
├── monitor/
│   ├── monitor_service.h
│   └── monitor_service.cc
```

**具体步骤：**
- [ ] 1.1.1 创建 `main/video/` 目录
- [ ] 1.1.2 创建 `main/monitor/` 目录
- [ ] 1.1.3 创建空的头文件和源文件
- [ ] 1.1.4 修改 `main/CMakeLists.txt` 添加新文件到编译

### 任务 1.2：扩展设备状态定义

**文件**: `main/device_state.h`

**修改内容：**
```cpp
enum DeviceState {
    // ... 现有状态保持不变 ...
    
    // 新增监控模式状态
    kDeviceStateMonitorConnecting,  // 监控模式-连接中
    kDeviceStateMonitorStreaming,   // 监控模式-流媒体传输中
};
```

**具体步骤：**
- [ ] 1.2.1 在 `device_state.h` 中新增两个状态枚举值
- [ ] 1.2.2 在 `application.cc` 的 `STATE_STRINGS` 数组中添加对应字符串

### 任务 1.3：定义 JpegFrame 结构

**文件**: `main/video/jpeg_frame.h`

```cpp
#ifndef JPEG_FRAME_H
#define JPEG_FRAME_H

#include <vector>
#include <cstdint>

struct JpegFrame {
    std::vector<uint8_t> data;  // JPEG数据
    uint32_t timestamp;          // 时间戳（毫秒）
    uint16_t width;
    uint16_t height;
    
    JpegFrame() : timestamp(0), width(0), height(0) {}
    JpegFrame(JpegFrame&&) = default;
    JpegFrame& operator=(JpegFrame&&) = default;
    
    // 禁止拷贝
    JpegFrame(const JpegFrame&) = delete;
    JpegFrame& operator=(const JpegFrame&) = delete;
};

#endif // JPEG_FRAME_H
```

**具体步骤：**
- [ ] 1.3.1 创建 `jpeg_frame.h` 文件
- [ ] 1.3.2 实现 JpegFrame 结构体

---

## 阶段二：Esp32Camera 扩展（0.5天）

### 任务 2.1：新增 CaptureToBuffer 接口

**文件**: `main/boards/common/esp32_camera.h`

**新增接口：**
```cpp
class Esp32Camera : public Camera {
public:
    // ... 现有接口保持不变 ...
    
    // 新增：直接捕获到外部缓冲区
    bool CaptureToBuffer(uint8_t* buffer, size_t buffer_size, size_t& actual_size);
    
    // 新增：获取帧参数
    uint16_t GetFrameWidth() const;
    uint16_t GetFrameHeight() const;
    uint32_t GetFrameFormat() const;
    size_t GetFrameSize() const;
};
```

**具体步骤：**
- [ ] 2.1.1 在 `esp32_camera.h` 中声明新接口
- [ ] 2.1.2 在 `esp32_camera.cc` 中实现 `CaptureToBuffer()`
- [ ] 2.1.3 实现帧参数获取方法
- [ ] 2.1.4 编译测试，确保不影响现有功能


---

## 阶段三：VideoStreamService 实现（2-3天）

### 任务 3.1：VideoStreamService 类框架

**文件**: `main/video/video_stream_service.h`

```cpp
#ifndef VIDEO_STREAM_SERVICE_H
#define VIDEO_STREAM_SERVICE_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>
#include <mutex>
#include <deque>
#include <memory>
#include <array>

#include "jpeg_frame.h"

class VideoStreamService {
public:
    static VideoStreamService& GetInstance();
    
    // 生命周期
    bool Initialize();
    void Deinitialize();
    bool Start();
    void Stop();
    bool IsRunning() const { return running_; }
    
    // 配置
    void SetFrameRate(int fps);
    void SetQuality(int quality);
    int GetFrameRate() const { return target_fps_; }
    int GetQuality() const { return jpeg_quality_; }
    
    // 获取编码后的JPEG帧
    std::unique_ptr<JpegFrame> PopFrame();
    bool HasFrame() const;
    
    // 统计
    int GetActualFps() const { return actual_fps_; }
    size_t GetTotalBytesSent() const { return total_bytes_; }
    
private:
    VideoStreamService();
    ~VideoStreamService();
    VideoStreamService(const VideoStreamService&) = delete;
    VideoStreamService& operator=(const VideoStreamService&) = delete;
    
    void CaptureTask();
    void EncodeTask();
    bool AllocateBuffers();
    void FreeBuffers();
    
    // 配置
    int target_fps_ = 15;
    int jpeg_quality_ = 80;
    
    // 状态
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::atomic<int> actual_fps_{0};
    std::atomic<size_t> total_bytes_{0};
    
    // 任务
    TaskHandle_t capture_task_handle_ = nullptr;
    TaskHandle_t encode_task_handle_ = nullptr;
    
    // 帧缓冲（PSRAM）
    static constexpr int FRAME_BUFFER_COUNT = 3;
    struct FrameBuffer {
        uint8_t* data = nullptr;
        size_t capacity = 0;
        size_t size = 0;
        uint32_t timestamp = 0;
        std::atomic<bool> ready{false};
    };
    std::array<FrameBuffer, FRAME_BUFFER_COUNT> frame_buffers_;
    std::atomic<int> capture_index_{0};
    std::atomic<int> encode_index_{0};
    
    // JPEG输出队列
    std::mutex jpeg_mutex_;
    std::deque<std::unique_ptr<JpegFrame>> jpeg_queue_;
    static constexpr int MAX_JPEG_QUEUE_SIZE = 2;
    
    // 同步
    SemaphoreHandle_t frame_ready_sem_ = nullptr;
};

#endif // VIDEO_STREAM_SERVICE_H
```

**具体步骤：**
- [ ] 3.1.1 创建 `video_stream_service.h`
- [ ] 3.1.2 实现单例模式
- [ ] 3.1.3 定义所有成员变量和方法声明

### 任务 3.2：缓冲区管理实现

**文件**: `main/video/video_stream_service.cc`

**具体步骤：**
- [ ] 3.2.1 实现构造函数和析构函数
- [ ] 3.2.2 实现 `AllocateBuffers()` - 在PSRAM中分配帧缓冲
- [ ] 3.2.3 实现 `FreeBuffers()` - 释放帧缓冲
- [ ] 3.2.4 实现 `Initialize()` 和 `Deinitialize()`

### 任务 3.3：视频捕获任务实现

**具体步骤：**
- [ ] 3.3.1 实现 `CaptureTask()` 函数
- [ ] 3.3.2 实现帧率控制逻辑
- [ ] 3.3.3 调用 `Esp32Camera::CaptureToBuffer()` 获取帧数据
- [ ] 3.3.4 实现双缓冲/三缓冲切换逻辑
- [ ] 3.3.5 使用信号量通知编码任务

### 任务 3.4：JPEG编码任务实现

**具体步骤：**
- [ ] 3.4.1 实现 `EncodeTask()` 函数
- [ ] 3.4.2 复用现有的 `image_to_jpeg_cb()` 函数进行编码
- [ ] 3.4.3 实现编码结果入队逻辑
- [ ] 3.4.4 实现队列满时的丢帧策略

### 任务 3.5：对外接口实现

**具体步骤：**
- [ ] 3.5.1 实现 `Start()` - 创建并启动任务
- [ ] 3.5.2 实现 `Stop()` - 停止任务并等待退出
- [ ] 3.5.3 实现 `PopFrame()` - 从队列获取JPEG帧
- [ ] 3.5.4 实现 `SetFrameRate()` 和 `SetQuality()`
- [ ] 3.5.5 实现统计信息获取方法

### 任务 3.6：单元测试

**具体步骤：**
- [ ] 3.6.1 测试缓冲区分配和释放
- [ ] 3.6.2 测试视频捕获任务启动和停止
- [ ] 3.6.3 测试JPEG编码输出
- [ ] 3.6.4 测试帧率控制
- [ ] 3.6.5 测试长时间运行稳定性

---

## 阶段四：Protocol 扩展（1天）

### 任务 4.1：Protocol 基类扩展

**文件**: `main/protocols/protocol.h`

**新增接口：**
```cpp
class Protocol {
public:
    // ... 现有接口保持不变 ...
    
    // 新增：发送视频帧
    virtual bool SendVideo(std::unique_ptr<JpegFrame> frame);
};
```

**具体步骤：**
- [ ] 4.1.1 在 `protocol.h` 中声明 `SendVideo()` 虚方法
- [ ] 4.1.2 在 `protocol.cc` 中提供默认实现（返回false）

### 任务 4.2：WebSocket 协议实现

**文件**: `main/protocols/websocket_protocol.h` 和 `.cc`

**具体步骤：**
- [ ] 4.2.1 在 `websocket_protocol.h` 中声明 `SendVideo()` override
- [ ] 4.2.2 实现 `SendVideo()` 方法
- [ ] 4.2.3 使用 BinaryProtocol2 格式封装视频数据（type=1）
- [ ] 4.2.4 测试视频数据发送

### 任务 4.3：MQTT 协议实现（可选）

**文件**: `main/protocols/mqtt_protocol.h` 和 `.cc`

**具体步骤：**
- [ ] 4.3.1 在 `mqtt_protocol.h` 中声明 `SendVideo()` override
- [ ] 4.3.2 实现 `SendVideo()` 方法
- [ ] 4.3.3 测试视频数据发送

---

## 阶段五：MonitorService 实现（1-2天）

### 任务 5.1：MonitorService 类框架

**文件**: `main/monitor/monitor_service.h`

```cpp
#ifndef MONITOR_SERVICE_H
#define MONITOR_SERVICE_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cstdint>

class MonitorService {
public:
    static MonitorService& GetInstance();
    
    // 生命周期
    bool Start();
    void Stop();
    bool IsRunning() const { return running_; }
    
    // 统计信息
    struct Stats {
        int video_fps = 0;
        int audio_fps = 0;
        size_t video_bitrate = 0;
        size_t audio_bitrate = 0;
    };
    Stats GetStats() const;
    void ResetStats();
    
private:
    MonitorService();
    ~MonitorService();
    MonitorService(const MonitorService&) = delete;
    MonitorService& operator=(const MonitorService&) = delete;
    
    void StreamTask();
    void UpdateStats();
    
    std::atomic<bool> running_{false};
    TaskHandle_t stream_task_handle_ = nullptr;
    
    // 统计
    Stats stats_;
    int64_t stats_start_time_ = 0;
    size_t video_bytes_ = 0;
    size_t audio_bytes_ = 0;
    int video_frames_ = 0;
    int audio_frames_ = 0;
};

#endif // MONITOR_SERVICE_H
```

**具体步骤：**
- [ ] 5.1.1 创建 `monitor_service.h`
- [ ] 5.1.2 实现单例模式
- [ ] 5.1.3 定义统计结构体

### 任务 5.2：流媒体发送任务实现

**文件**: `main/monitor/monitor_service.cc`

**具体步骤：**
- [ ] 5.2.1 实现构造函数和析构函数
- [ ] 5.2.2 实现 `StreamTask()` 主循环
- [ ] 5.2.3 从 AudioService 获取音频包并发送
- [ ] 5.2.4 从 VideoStreamService 获取视频帧并发送
- [ ] 5.2.5 实现音视频交替发送逻辑（音频优先）

### 任务 5.3：统计功能实现

**具体步骤：**
- [ ] 5.3.1 实现 `UpdateStats()` - 每秒更新统计
- [ ] 5.3.2 实现 `GetStats()` - 获取当前统计
- [ ] 5.3.3 实现 `ResetStats()` - 重置统计

### 任务 5.4：Start/Stop 实现

**具体步骤：**
- [ ] 5.4.1 实现 `Start()` - 创建流媒体任务
- [ ] 5.4.2 实现 `Stop()` - 停止任务并清理


---

## 阶段六：Application 集成（1天）

### 任务 6.1：新增模式管理成员

**文件**: `main/application.h`

**新增内容：**
```cpp
class Application {
public:
    // ... 现有接口保持不变 ...
    
    // 新增：模式管理
    DeviceMode GetDeviceMode() const { return device_mode_; }
    bool SwitchToMonitorMode();
    bool SwitchToNormalMode();
    bool IsMonitorMode() const { return device_mode_ == kModeMonitor; }
    
private:
    // 新增成员
    DeviceMode device_mode_ = kModeNormal;
    
    // 新增方法
    void HandleMonitorCommand(const cJSON* root);
};
```

**具体步骤：**
- [ ] 6.1.1 在 `application.h` 中添加 DeviceMode 枚举（或引用）
- [ ] 6.1.2 添加 `device_mode_` 成员变量
- [ ] 6.1.3 声明模式切换方法
- [ ] 6.1.4 声明监控命令处理方法

### 任务 6.2：实现模式切换逻辑

**文件**: `main/application.cc`

**具体步骤：**
- [ ] 6.2.1 实现 `SwitchToMonitorMode()`
  - 禁用唤醒词检测
  - 确保AEC启用
  - 初始化并启动 VideoStreamService
  - 启动 MonitorService
  - 更新状态为 kDeviceStateMonitorStreaming
  
- [ ] 6.2.2 实现 `SwitchToNormalMode()`
  - 停止 MonitorService
  - 停止 VideoStreamService
  - 恢复AEC设置
  - 启用唤醒词检测
  - 更新状态为 kDeviceStateIdle

### 任务 6.3：扩展消息处理

**文件**: `main/application.cc`

**具体步骤：**
- [ ] 6.3.1 在 `OnIncomingJson` 回调中添加 "monitor" 类型处理
- [ ] 6.3.2 实现 `HandleMonitorCommand()` 方法
- [ ] 6.3.3 解析 start/stop 命令
- [ ] 6.3.4 解析配置参数（fps, quality等）

### 任务 6.4：扩展状态显示

**文件**: `main/application.cc`

**具体步骤：**
- [ ] 6.4.1 在 `SetDeviceState()` 中添加监控模式状态处理
- [ ] 6.4.2 设置监控模式的显示状态和表情
- [ ] 6.4.3 更新 LED 状态（如果需要）

### 任务 6.5：扩展音频发送事件

**具体步骤：**
- [ ] 6.5.1 在 `MainEventLoop` 的 `MAIN_EVENT_SEND_AUDIO` 处理中
- [ ] 6.5.2 检查当前模式，监控模式下由 MonitorService 处理发送
- [ ] 6.5.3 或者保持现有逻辑，MonitorService 独立获取音频包

---

## 阶段七：集成测试（2-3天）

### 任务 7.1：基础功能测试

**具体步骤：**
- [ ] 7.1.1 测试普通模式功能是否正常（回归测试）
- [ ] 7.1.2 测试监控模式启动
- [ ] 7.1.3 测试视频流输出
- [ ] 7.1.4 测试音频流输出
- [ ] 7.1.5 测试音频接收和播放

### 任务 7.2：模式切换测试

**具体步骤：**
- [ ] 7.2.1 测试普通模式 → 监控模式切换
- [ ] 7.2.2 测试监控模式 → 普通模式切换
- [ ] 7.2.3 测试快速连续切换
- [ ] 7.2.4 测试切换过程中的资源释放

### 任务 7.3：性能测试

**具体步骤：**
- [ ] 7.3.1 测试视频帧率稳定性
- [ ] 7.3.2 测试音视频延迟
- [ ] 7.3.3 测试CPU使用率
- [ ] 7.3.4 测试内存使用情况
- [ ] 7.3.5 测试网络带宽使用

### 任务 7.4：稳定性测试

**具体步骤：**
- [ ] 7.4.1 监控模式长时间运行测试（1小时+）
- [ ] 7.4.2 网络波动测试
- [ ] 7.4.3 内存泄漏检测
- [ ] 7.4.4 异常恢复测试

### 任务 7.5：服务器端配合

**具体步骤：**
- [ ] 7.5.1 确认服务器支持视频流接收
- [ ] 7.5.2 确认服务器支持 monitor 消息类型
- [ ] 7.5.3 端到端测试

---

## 时间线总结

| 阶段 | 任务 | 预计时间 | 依赖 |
|------|------|----------|------|
| 一 | 基础设施准备 | 1天 | 无 |
| 二 | Esp32Camera 扩展 | 0.5天 | 阶段一 |
| 三 | VideoStreamService | 2-3天 | 阶段二 |
| 四 | Protocol 扩展 | 1天 | 阶段一 |
| 五 | MonitorService | 1-2天 | 阶段三、四 |
| 六 | Application 集成 | 1天 | 阶段五 |
| 七 | 集成测试 | 2-3天 | 阶段六 |
| **总计** | | **8-11天** | |

---

## 风险与缓解

| 风险 | 可能性 | 影响 | 缓解措施 |
|------|--------|------|----------|
| JPEG编码性能不足 | 中 | 帧率下降 | 降低分辨率或质量 |
| 内存不足 | 低 | 系统崩溃 | 减少缓冲区数量 |
| 网络带宽不足 | 中 | 延迟增加 | 自适应帧率/质量 |
| 音视频不同步 | 中 | 体验差 | 时间戳校准 |
| 影响正常模式 | 低 | 回归问题 | 充分回归测试 |

---

## 验收标准

### 功能验收

- [ ] 服务器可发送 monitor start 命令启动监控模式
- [ ] 服务器可发送 monitor stop 命令停止监控模式
- [ ] 监控模式下视频流正常传输
- [ ] 监控模式下音频双向传输正常
- [ ] 监控模式下AEC正常工作
- [ ] 模式切换平滑，无明显卡顿
- [ ] 普通模式功能不受影响

### 性能验收

- [ ] 视频帧率 ≥ 15fps (VGA)
- [ ] 视频延迟 < 500ms
- [ ] 音频延迟 < 300ms
- [ ] 模式切换时间 < 500ms
- [ ] 内存使用 < 5MB PSRAM

### 稳定性验收

- [ ] 监控模式连续运行1小时无崩溃
- [ ] 模式切换100次无异常
- [ ] 无内存泄漏
