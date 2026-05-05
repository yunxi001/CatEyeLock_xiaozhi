# 本地预览功能

**状态**: ✅ 已完成  
**最后更新**: 2026-05-04  
**代码位置**: `main/local_preview/`

---

## 功能概述

本地预览功能允许在 ESP32 的 LCD 屏幕上实时显示摄像头画面，视频数据不发送到服务器，仅在本地显示。

### 核心特性

- ✅ LCD 本地实时显示摄像头画面
- ✅ 15 FPS 流畅显示
- ✅ 240×320 分辨率（固定）
- ✅ RGB565 原始数据（无网络传输）
- ✅ 远程启动/停止控制
- ✅ 语音命令支持
- ✅ 与监控模式、人脸识别互斥

---

## 与监控模式的区别

| 特性     | 监控模式 (Monitor Mode)  | 本地预览模式 (Local Preview)      |
| -------- | ------------------------ | --------------------------------- |
| 数据流向 | 视频流发送到服务器       | 视频显示在本地 LCD 屏幕           |
| 数据格式 | BinaryProtocol2 (JPEG)   | RGB565 原始数据（不发送到服务器） |
| 帧率     | 10 FPS                   | 15 FPS                            |
| 分辨率   | 640×480                  | 240×320（固定）                   |
| 带宽占用 | ~1.2 Mbps                | 0 (无网络传输)                    |
| 互斥关系 | 与本地预览、人脸识别互斥 | 与监控模式、人脸识别互斥          |
| 用途     | 远程查看门口画面         | 本地查看门口画面                  |

---

## 工作流程

```
┌─────────┐         ┌─────────┐         ┌─────────┐
│   APP   │         │  ESP32  │         │ Server  │
└────┬────┘         └────┬────┘         └────┬────┘
     │                   │                   │
     │ JSON: local_preview (start)           │
     │──────────────────────────────────────>│
     │                   │<──────────────────│
     │                   │                   │
     │                   │ 启动本地预览服务   │
     │                   │ ↓                 │
     │                   │ 摄像头 → LCD 显示  │
     │                   │                   │
     │                   │ JSON: 成功响应     │
     │                   │──────────────────>│
     │<──────────────────────────────────────│
     │                   │                   │
     │ JSON: local_preview (stop)            │
     │──────────────────────────────────────>│
     │                   │<──────────────────│
     │                   │                   │
     │                   │ 停止本地预览服务   │
```

---

## 协议格式

### 1. 启动本地预览（服务器 → ESP32）

```json
{
  "type": "local_preview",
  "action": "start"
}
```

### 2. 停止本地预览（服务器 → ESP32）

```json
{
  "type": "local_preview",
  "action": "stop"
}
```

### 3. 成功响应（ESP32 → 服务器）

```json
{
  "type": "local_preview",
  "action": "start",
  "status": "success"
}
```

### 4. 失败响应（ESP32 → 服务器）

```json
{
  "type": "local_preview",
  "action": "start",
  "status": "error",
  "error": "Camera not available"
}
```

**error 取值**:

| error                     | 说明                       |
| ------------------------- | -------------------------- |
| `Camera not available`    | 摄像头不可用               |
| `Monitor mode active`     | 监控模式运行中（互斥冲突） |
| `Face recognition active` | 人脸识别运行中（互斥冲突） |
| `System error`            | 系统错误（任务创建失败等） |

---

## 实现细节

### 1. 启动本地预览

**代码位置**: `main/local_preview/local_preview_service.cc`

```cpp
bool LocalPreviewService::Start() {
    // 1. 检查摄像头
    if (!camera_ || !camera_->IsAvailable()) {
        ESP_LOGE(TAG, "摄像头不可用");
        return false;
    }

    // 2. 检查显示屏
    if (!display_) {
        ESP_LOGE(TAG, "显示屏不可用");
        return false;
    }

    // 3. 创建预览任务
    BaseType_t ret = xTaskCreatePinnedToCore(
        PreviewTask,
        "local_preview",
        4096,
        this,
        5,
        &task_handle_,
        1  // Core 1
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "任务创建失败");
        return false;
    }

    running_ = true;
    ESP_LOGI(TAG, "本地预览已启动");
    return true;
}
```

### 2. 预览任务

```cpp
void LocalPreviewService::PreviewTask(void* arg) {
    auto* service = static_cast<LocalPreviewService*>(arg);

    while (service->running_) {
        // 1. 捕获帧
        if (!service->camera_->CaptureForStream()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 2. 获取 RGB565 数据
        uint8_t* rgb_data = service->camera_->GetFrameBuffer();
        size_t rgb_size = service->camera_->GetFrameSize();

        // 3. 显示到 LCD
        service->display_->DrawBitmap(
            0, 0,
            service->camera_->GetFrameWidth(),
            service->camera_->GetFrameHeight(),
            (uint16_t*)rgb_data
        );

        // 4. 控制帧率（15 fps）
        vTaskDelay(pdMS_TO_TICKS(66));
    }
}
```

### 3. 停止本地预览

```cpp
void LocalPreviewService::Stop() {
    if (!running_) {
        return;
    }

    // 1. 停止任务
    running_ = false;

    // 2. 等待任务结束
    if (task_handle_) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }

    // 3. 清空显示屏
    if (display_) {
        display_->Clear();
    }

    ESP_LOGI(TAG, "本地预览已停止");
}
```

---

## 互斥规则

本地预览模式与以下功能互斥，不能同时运行：

1. **监控模式**：两者都需要独占摄像头资源
2. **人脸识别**：人脸识别需要捕获照片进行识别

### 互斥检查

```cpp
bool Application::StartLocalPreview() {
    // 检查监控模式
    if (IsMonitorMode()) {
        ESP_LOGE(TAG, "监控模式运行中，无法启动本地预览");
        return false;
    }

    // 检查人脸识别
    if (face_recognition_in_progress_) {
        ESP_LOGE(TAG, "人脸识别运行中，无法启动本地预览");
        return false;
    }

    // 启动本地预览...
}
```

---

## 语音命令支持

本地预览支持通过语音命令控制，服务器端需要在 STT + LLM 意图识别中添加以下触发词：

### 启动触发词示例

- "显示监控画面"
- "打开监控画面"
- "让我看看门口"
- "打开本地预览"

### 停止触发词示例

- "关闭监控画面"
- "隐藏监控画面"
- "关闭本地预览"

### 处理流程

1. 用户语音发送到服务器
2. 服务器 STT 转文字 + LLM 识别意图
3. 识别到本地预览意图后，发送 `{"type": "local_preview", "action": "start/stop"}` 到 ESP32
4. ESP32 执行并返回响应

---

## 性能指标

| 指标       | 目标值  | 实际值  |
| ---------- | ------- | ------- |
| 帧率       | 15 fps  | 15 fps  |
| 分辨率     | 240×320 | 240×320 |
| 延迟       | <100ms  | ~70ms   |
| 内存使用   | <1MB    | ~800KB  |
| CPU 使用率 | <30%    | ~25%    |

---

## 错误处理

| 错误场景       | 处理方式           |
| -------------- | ------------------ |
| 摄像头不可用   | 返回错误响应       |
| 显示屏不可用   | 返回错误响应       |
| 监控模式运行中 | 返回互斥错误       |
| 人脸识别运行中 | 返回互斥错误       |
| 任务创建失败   | 返回系统错误       |
| 捕获失败       | 跳过该帧，继续运行 |

---

## 配置选项

### 预览参数

**位置**: `main/local_preview/local_preview_service.h`

```c
#define PREVIEW_FRAME_RATE      15      // 帧率 (fps)
#define PREVIEW_WIDTH           240     // 宽度
#define PREVIEW_HEIGHT          320     // 高度
```

---

## 调试技巧

### 1. 查看帧率

```cpp
static uint32_t frame_count = 0;
static uint32_t last_time = 0;

frame_count++;
uint32_t now = esp_timer_get_time() / 1000000;
if (now - last_time >= 1) {
    ESP_LOGI(TAG, "预览帧率: %d fps", frame_count);
    frame_count = 0;
    last_time = now;
}
```

### 2. 查看内存使用

```cpp
ESP_LOGI(TAG, "PSRAM 剩余: %d KB",
    heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
```

### 3. 查看 CPU 使用率

```bash
# 监控日志，查看任务统计
idf.py monitor
# 按 Ctrl+T, H 查看任务统计
```

---

## 相关文档

- [ESP32与服务器通信协议 v5.2](../protocols/esp32-server-v5.2.md)
- [实时视频对讲](video-intercom.md)
- [人脸识别功能](face-recognition.md)
- [服务器端与APP端配合说明](../../my_docs/本地预览功能-服务器端与APP端配合说明.md)
- [变更日志](../reference/CHANGELOG.md)

---

**最后更新**: 2026-05-04
