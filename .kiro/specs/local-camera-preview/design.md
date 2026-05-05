# 设计文档 - 本地监控画面实时显示功能

## 概述 (Overview)

本地监控画面实时显示功能为智能猫眼门锁系统提供了在 ESP32-S3 的 LCD 屏幕上实时查看摄像头画面的能力。该功能基于零拷贝优化方案，摄像头直接输出 RGB565 原始数据用于本地预览，避免 JPEG 编解码开销，实现低延迟（<50ms）、高帧率（15 FPS）的本地监控画面显示。

### 核心目标

- **实时性**：15 FPS 帧率，端到端延迟 <50ms
- **低开销**：CPU 占用率 <25%，内存增量 <500KB
- **零拷贝**：RGB565 原始数据直接渲染到 LVGL Canvas，无需编解码
- **用户友好**：支持 WebSocket 命令和语音命令控制
- **系统兼容**：与监控模式、人脸识别功能互斥，不影响其他功能

### 技术方案

采用**零拷贝优化方案**（方案 B）：

1. **摄像头配置**：输出 RGB565 原始数据（16 位色彩，每像素 2 字节）
2. **LVGL 集成**：使用 `lv_canvas` 组件直接显示 RGB565 数据
3. **双任务架构**：Capture Task（捕获帧）+ Display Task（刷新显示）
4. **队列缓冲**：FreeRTOS 队列平衡生产和消费速度

### 与现有架构的关系

本功能参考现有监控模式（`MonitorService` + `VideoStreamService`）的架构设计，但针对本地预览进行了优化：

| 特性     | 监控模式         | 本地预览模式   |
| -------- | ---------------- | -------------- |
| 数据格式 | JPEG（编码）     | RGB565（原始） |
| 数据流向 | 网络发送到服务器 | 本地显示到 LCD |
| 帧率     | 10 FPS           | 15 FPS         |
| 延迟     | ~100ms（含网络） | <50ms（本地）  |
| CPU 开销 | ~30%（含编码）   | <25%（无编码） |

---

## 架构 (Architecture)

### 系统架构图

```
┌─────────────────────────────────────────────────────────────┐
│                      Application                            │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  StartLocalPreview() / StopLocalPreview()            │  │
│  │  - 创建/销毁任务                                      │  │
│  │  - 管理状态标志                                       │  │
│  │  - 处理 WebSocket/语音命令                           │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                    │                        │
                    ▼                        ▼
    ┌───────────────────────┐    ┌───────────────────────┐
    │   Capture Task        │    │   Display Task        │
    │  (FreeRTOS Task)      │    │  (FreeRTOS Task)      │
    │                       │    │                       │
    │  - 15 FPS 捕获循环    │    │  - 从队列获取帧       │
    │  - 调用 Camera API    │───>│  - 更新 LVGL Canvas   │
    │  - 推送到队列         │    │  - 触发屏幕刷新       │
    └───────────────────────┘    └───────────────────────┘
                │                            │
                ▼                            ▼
    ┌───────────────────────┐    ┌───────────────────────┐
    │  FreeRTOS Queue       │    │   LCD_Display         │
    │  (深度: 2 帧)         │    │  (LVGL 集成)          │
    │                       │    │                       │
    │  - PreviewFrame 结构  │    │  - lv_canvas 对象     │
    │  - PSRAM 缓冲区       │    │  - RGB565 渲染        │
    └───────────────────────┘    └───────────────────────┘
                │                            │
                ▼                            ▼
    ┌───────────────────────┐    ┌───────────────────────┐
    │   Esp32Camera         │    │   SPI LCD 硬件        │
    │  (摄像头抽象)         │    │  (240x320 屏幕)       │
    │                       │    │                       │
    │  - CaptureForPreview()│    │  - SPI 传输           │
    │  - RGB565 输出        │    │  - 硬件刷新           │
    └───────────────────────┘    └───────────────────────┘
```

### 模块职责

#### 1. Application 模块

**职责**：

- 生命周期管理：启动/停止本地预览
- 命令处理：WebSocket 和语音命令
- 状态管理：维护 `local_preview_active_` 标志
- 互斥控制：与监控模式、人脸识别互斥

**关键方法**：

```cpp
bool StartLocalPreview();
void StopLocalPreview();
bool IsLocalPreviewActive() const;
```

#### 2. Capture Task

**职责**：

- 定期从摄像头捕获 RGB565 帧（15 FPS）
- 将帧数据推送到队列
- 队列满时丢弃旧帧

**实现细节**：

- 任务优先级：5（与 VideoStreamService 相同）
- 栈大小：4096 字节
- 帧间隔：66ms（1000ms / 15fps）

#### 3. Display Task

**职责**：

- 从队列获取最新帧
- 更新 LVGL Canvas 缓冲区
- 触发 LVGL 重绘

**实现细节**：

- 任务优先级：5
- 栈大小：4096 字节
- 队列超时：100ms

#### 4. Esp32Camera 扩展

**新增方法**：

```cpp
bool CaptureForPreview();  // 捕获 RGB565 原始帧
```

**与现有方法的区别**：

- `Capture()`：用于人脸识别，捕获并显示预览
- `CaptureForStream()`：用于监控模式，捕获 JPEG 编码
- `CaptureForPreview()`：用于本地预览，捕获 RGB565 原始数据

#### 5. LCD_Display 扩展

**新增方法**：

```cpp
bool EnterPreviewMode();   // 进入预览模式
void ExitPreviewMode();    // 退出预览模式
bool UpdatePreviewCanvas(const uint8_t* rgb565_data, uint16_t width, uint16_t height);
```

**UI 状态切换**：

- **Normal Mode**：显示 LVGL UI（状态栏、表情、消息）
- **Preview Mode**：全屏显示 Canvas，隐藏其他 UI 组件

---

## 组件和接口 (Components and Interfaces)

### 核心数据结构

#### PreviewFrame

```cpp
/**
 * @brief 本地预览帧结构
 *
 * 存储一帧 RGB565 原始图像数据及其元数据。
 * 内存分配在 PSRAM 中以节省内部 RAM。
 */
struct PreviewFrame {
    uint8_t* data;          ///< RGB565 数据指针（PSRAM）
    size_t data_size;       ///< 数据大小（字节）
    uint16_t width;         ///< 图像宽度
    uint16_t height;        ///< 图像高度
    TickType_t timestamp;   ///< 捕获时间戳（FreeRTOS tick）

    /**
     * @brief 构造函数，分配 PSRAM 缓冲区
     */
    PreviewFrame(uint16_t w, uint16_t h);

    /**
     * @brief 析构函数，释放 PSRAM 缓冲区
     */
    ~PreviewFrame();

    // 禁止拷贝，只允许移动
    PreviewFrame(const PreviewFrame&) = delete;
    PreviewFrame& operator=(const PreviewFrame&) = delete;
    PreviewFrame(PreviewFrame&&) = default;
    PreviewFrame& operator=(PreviewFrame&&) = default;
};
```

### 接口定义

#### Camera 接口扩展

```cpp
class Esp32Camera : public Camera {
public:
    /**
     * @brief 捕获一帧用于本地预览
     *
     * 与 CaptureForStream() 类似，但输出 RGB565 原始数据。
     * 不在屏幕上显示预览，不丢弃前两帧。
     *
     * @return bool 捕获成功返回 true
     */
    bool CaptureForPreview();

    /**
     * @brief 获取当前帧的 RGB565 数据指针
     *
     * 必须在 CaptureForPreview() 成功后调用。
     * 数据有效期直到下一次 Capture*() 调用。
     *
     * @return const uint8_t* RGB565 数据指针
     */
    const uint8_t* GetRgb565Data() const;

    /**
     * @brief 获取当前帧的数据大小
     *
     * @return size_t 数据大小（字节）
     */
    size_t GetRgb565DataSize() const;
};
```

#### Display 接口扩展

```cpp
class LcdDisplay : public LvglDisplay {
public:
    /**
     * @brief 进入预览模式
     *
     * 隐藏所有 LVGL UI 组件，创建并显示全屏 Canvas。
     *
     * @return bool 成功返回 true
     */
    bool EnterPreviewMode();

    /**
     * @brief 退出预览模式
     *
     * 销毁 Canvas，恢复 LVGL UI 组件。
     */
    void ExitPreviewMode();

    /**
     * @brief 更新预览 Canvas
     *
     * 将 RGB565 数据复制到 Canvas 缓冲区并触发重绘。
     *
     * @param rgb565_data RGB565 数据指针
     * @param width 图像宽度
     * @param height 图像高度
     * @return bool 成功返回 true
     */
    bool UpdatePreviewCanvas(const uint8_t* rgb565_data,
                            uint16_t width, uint16_t height);

    /**
     * @brief 检查是否处于预览模式
     *
     * @return bool 预览模式返回 true
     */
    bool IsPreviewMode() const;

private:
    lv_obj_t* preview_canvas_;      ///< 预览 Canvas 对象
    void* preview_canvas_buffer_;   ///< Canvas 缓冲区（PSRAM）
    bool preview_mode_active_;      ///< 预览模式标志
};
```

#### Application 接口扩展

```cpp
class Application {
public:
    /**
     * @brief 启动本地预览
     *
     * 执行以下操作：
     * 1. 检查摄像头可用性
     * 2. 检查与监控模式/人脸识别的互斥
     * 3. 创建 FreeRTOS 队列
     * 4. 创建 Capture Task 和 Display Task
     * 5. 切换 LCD 到预览模式
     *
     * @return bool 成功返回 true
     */
    bool StartLocalPreview();

    /**
     * @brief 停止本地预览
     *
     * 执行以下操作：
     * 1. 设置停止标志
     * 2. 等待任务退出
     * 3. 清空队列
     * 4. 恢复 LCD 到正常模式
     */
    void StopLocalPreview();

    /**
     * @brief 检查本地预览是否活动
     *
     * @return bool 活动返回 true
     */
    bool IsLocalPreviewActive() const;

private:
    // 本地预览相关成员变量
    bool local_preview_active_;             ///< 预览活动标志
    TaskHandle_t preview_capture_task_;     ///< 捕获任务句柄
    TaskHandle_t preview_display_task_;     ///< 显示任务句柄
    QueueHandle_t preview_frame_queue_;     ///< 帧队列句柄

    // 任务函数
    static void PreviewCaptureTask(void* param);
    static void PreviewDisplayTask(void* param);

    // 任务循环
    void PreviewCaptureLoop();
    void PreviewDisplayLoop();
};
```

### WebSocket 命令格式

#### 启动本地预览

```json
{
  "type": "local_preview",
  "action": "start"
}
```

**响应**（成功）：

```json
{
  "type": "local_preview",
  "action": "start",
  "status": "success"
}
```

**响应**（失败）：

```json
{
  "type": "local_preview",
  "action": "start",
  "status": "error",
  "error": "Camera not available"
}
```

#### 停止本地预览

```json
{
  "type": "local_preview",
  "action": "stop"
}
```

**响应**：

```json
{
  "type": "local_preview",
  "action": "stop",
  "status": "success"
}
```

---

## 数据模型 (Data Models)

### 内存布局

#### PreviewFrame 内存分配

```
┌─────────────────────────────────────────────────────────┐
│  PreviewFrame 对象（栈或堆）                             │
│  ┌───────────────────────────────────────────────────┐  │
│  │  data: uint8_t* ──────────────┐                   │  │
│  │  data_size: size_t            │                   │  │
│  │  width: uint16_t              │                   │  │
│  │  height: uint16_t             │                   │  │
│  │  timestamp: TickType_t        │                   │  │
│  └───────────────────────────────┼───────────────────┘  │
└────────────────────────────────────┼──────────────────────┘
                                     │
                                     ▼
                    ┌────────────────────────────────────┐
                    │  PSRAM 缓冲区                      │
                    │  ┌──────────────────────────────┐ │
                    │  │  RGB565 数据                 │ │
                    │  │  (width * height * 2 字节)   │ │
                    │  │                              │ │
                    │  │  例如：240x320 = 153,600 字节│ │
                    │  └──────────────────────────────┘ │
                    └────────────────────────────────────┘
```

#### 队列数据流

```
Capture Task                Queue                Display Task
     │                        │                        │
     │  PreviewFrame*         │                        │
     ├───────────────────────>│                        │
     │                        │  PreviewFrame*         │
     │                        ├───────────────────────>│
     │                        │                        │
     │  PreviewFrame*         │                        │
     ├───────────────────────>│                        │
     │                        │                        │
     │  PreviewFrame*         │  (队列满，丢弃旧帧)    │
     ├───────────────────────>│                        │
     │                        │  PreviewFrame*         │
     │                        ├───────────────────────>│
```

### 状态机

#### 本地预览状态转换

```
                    ┌─────────────┐
                    │   Idle      │
                    │  (未启动)   │
                    └──────┬──────┘
                           │
                           │ StartLocalPreview()
                           │ - 检查互斥条件
                           │ - 创建任务和队列
                           ▼
                    ┌─────────────┐
                    │   Active    │
                    │  (运行中)   │
                    └──────┬──────┘
                           │
                           │ StopLocalPreview()
                           │ - 停止任务
                           │ - 清空队列
                           ▼
                    ┌─────────────┐
                    │   Idle      │
                    └─────────────┘
```

#### 与其他模式的互斥

```
┌──────────────────────────────────────────────────────────┐
│                    设备模式状态                           │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  ┌────────────┐     ┌────────────┐     ┌────────────┐  │
│  │  Normal    │────>│  Monitor   │     │  Local     │  │
│  │  Mode      │<────│  Mode      │     │  Preview   │  │
│  └────────────┘     └────────────┘     └────────────┘  │
│       │                   │                   │         │
│       │                   │                   │         │
│       │                   └───────X───────────┘         │
│       │                      (互斥)                     │
│       │                                                 │
│       └─────────────────────X───────────────────────────┘
│                        (人脸识别互斥)                    │
└──────────────────────────────────────────────────────────┘

互斥规则：
1. Monitor Mode ⊗ Local Preview（不能同时运行）
2. Face Recognition ⊗ Local Preview（不能同时运行）
3. Normal Mode 可与 Local Preview 共存（但 UI 被隐藏）
```

### 性能指标

#### 内存使用

| 组件              | 大小          | 位置      | 说明                       |
| ----------------- | ------------- | --------- | -------------------------- |
| PreviewFrame 对象 | ~32 字节      | 栈        | 结构体本身                 |
| RGB565 缓冲区     | 153,600 字节  | PSRAM     | 240x320x2                  |
| Canvas 缓冲区     | 153,600 字节  | PSRAM     | LVGL Canvas                |
| 队列（2 帧）      | ~307,200 字节 | PSRAM     | 2 个 PreviewFrame          |
| **总计**          | **~614 KB**   | **PSRAM** | **符合 <500KB 目标需调整** |

**优化方案**：

- 队列深度从 2 降低到 1（节省 ~153KB）
- 或降低分辨率到 240x240（节省 ~76KB/帧）

#### 时序分析

```
时间轴（毫秒）
0        20       40       60       80      100
│────────│────────│────────│────────│────────│
│ Capture│        │        │        │        │  Capture Task
│  (20ms)│        │ Capture│        │        │  (66ms 间隔)
│────────│────────│  (20ms)│────────│────────│
         │ Display│        │ Display│        │  Display Task
         │ (30ms) │        │ (30ms) │        │  (从队列获取)
         │────────│        │────────│        │

说明：
- Capture 耗时：~20ms（RGB565 捕获）
- Display 耗时：~30ms（Canvas 更新 + SPI 刷新）
- 帧间隔：66ms（15 FPS）
- 端到端延迟：<50ms（Capture + Display）
```

---

## 错误处理 (Error Handling)

### 错误分类

#### 1. 启动阶段错误

| 错误场景       | 错误码                        | 处理策略                 | 用户反馈                         |
| -------------- | ----------------------------- | ------------------------ | -------------------------------- |
| 摄像头不可用   | `ERR_CAMERA_UNAVAILABLE`      | 拒绝启动，记录错误日志   | 播放错误音效，显示"摄像头不可用" |
| 监控模式运行中 | `ERR_MONITOR_ACTIVE`          | 拒绝启动，返回错误响应   | 显示"监控模式运行中"             |
| 人脸识别运行中 | `ERR_FACE_RECOGNITION_ACTIVE` | 拒绝启动，返回错误响应   | 显示"人脸识别运行中"             |
| 内存不足       | `ERR_OUT_OF_MEMORY`           | 拒绝启动，记录错误日志   | 播放错误音效，显示"内存不足"     |
| 任务创建失败   | `ERR_TASK_CREATE_FAILED`      | 清理已创建资源，返回错误 | 播放错误音效，显示"系统错误"     |
| 队列创建失败   | `ERR_QUEUE_CREATE_FAILED`     | 清理已创建资源，返回错误 | 播放错误音效，显示"系统错误"     |

#### 2. 运行时错误

| 错误场景     | 错误码               | 处理策略                 | 恢复机制               |
| ------------ | -------------------- | ------------------------ | ---------------------- |
| 帧捕获失败   | `ERR_CAPTURE_FAILED` | 记录警告日志，继续下一帧 | 跳过当前帧，不中断服务 |
| 显示刷新失败 | `ERR_DISPLAY_FAILED` | 记录警告日志，继续下一帧 | 跳过当前帧，不中断服务 |
| 队列超时     | `ERR_QUEUE_TIMEOUT`  | 记录调试日志，继续等待   | 正常行为，无需恢复     |
| 内存分配失败 | `ERR_MALLOC_FAILED`  | 记录错误日志，停止服务   | 自动停止预览，释放资源 |

### 错误处理流程

#### 启动失败处理

```cpp
bool Application::StartLocalPreview() {
    // 1. 检查互斥条件
    if (IsMonitorMode()) {
        ESP_LOGE(TAG, "Cannot start preview: monitor mode active");
        Alert("错误", "监控模式运行中", "error", "error");
        return false;
    }

    if (face_recognition_in_progress_) {
        ESP_LOGE(TAG, "Cannot start preview: face recognition active");
        Alert("错误", "人脸识别运行中", "error", "error");
        return false;
    }

    // 2. 检查摄像头
    auto* camera = Board::GetInstance().GetCamera();
    auto* esp32_camera = dynamic_cast<Esp32Camera*>(camera);
    if (!esp32_camera || !esp32_camera->IsAvailable()) {
        ESP_LOGE(TAG, "Camera not available");
        Alert("错误", "摄像头不可用", "error", "error");
        return false;
    }

    // 3. 创建队列
    preview_frame_queue_ = xQueueCreate(1, sizeof(PreviewFrame*));
    if (preview_frame_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create frame queue");
        Alert("错误", "系统错误", "error", "error");
        return false;
    }

    // 4. 创建捕获任务
    BaseType_t ret = xTaskCreate(
        PreviewCaptureTask, "preview_capture", 4096, this, 5,
        &preview_capture_task_
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create capture task");
        vQueueDelete(preview_frame_queue_);  // 清理队列
        preview_frame_queue_ = nullptr;
        Alert("错误", "系统错误", "error", "error");
        return false;
    }

    // 5. 创建显示任务
    ret = xTaskCreate(
        PreviewDisplayTask, "preview_display", 4096, this, 5,
        &preview_display_task_
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display task");
        // 清理已创建的资源
        local_preview_active_ = false;
        vTaskDelete(preview_capture_task_);
        preview_capture_task_ = nullptr;
        vQueueDelete(preview_frame_queue_);
        preview_frame_queue_ = nullptr;
        Alert("错误", "系统错误", "error", "error");
        return false;
    }

    // 6. 切换显示模式
    auto* display = Board::GetInstance().GetDisplay();
    auto* lcd_display = dynamic_cast<LcdDisplay*>(display);
    if (lcd_display && !lcd_display->EnterPreviewMode()) {
        ESP_LOGW(TAG, "Failed to enter preview mode");
        // 继续运行，但可能显示异常
    }

    local_preview_active_ = true;
    ESP_LOGI(TAG, "Local preview started");
    PlaySound("confirm");
    return true;
}
```

#### 运行时错误恢复

```cpp
void Application::PreviewCaptureLoop() {
    int consecutive_failures = 0;
    const int MAX_FAILURES = 10;

    while (local_preview_active_) {
        auto* camera = Board::GetInstance().GetCamera();
        auto* esp32_camera = dynamic_cast<Esp32Camera*>(camera);

        // 捕获帧
        if (!esp32_camera || !esp32_camera->CaptureForPreview()) {
            consecutive_failures++;
            ESP_LOGW(TAG, "Capture failed (%d/%d)",
                     consecutive_failures, MAX_FAILURES);

            // 连续失败过多，停止服务
            if (consecutive_failures >= MAX_FAILURES) {
                ESP_LOGE(TAG, "Too many capture failures, stopping preview");
                Schedule([this]() {
                    StopLocalPreview();
                    Alert("错误", "摄像头异常", "error", "error");
                });
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 重置失败计数
        consecutive_failures = 0;

        // 分配帧对象
        PreviewFrame* frame = new (std::nothrow) PreviewFrame(
            esp32_camera->GetFrameWidth(),
            esp32_camera->GetFrameHeight()
        );

        if (frame == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate frame, stopping preview");
            Schedule([this]() {
                StopLocalPreview();
                Alert("错误", "内存不足", "error", "error");
            });
            break;
        }

        // 复制数据
        memcpy(frame->data, esp32_camera->GetRgb565Data(),
               frame->data_size);
        frame->timestamp = xTaskGetTickCount();

        // 发送到队列（非阻塞）
        if (xQueueSend(preview_frame_queue_, &frame, 0) != pdTRUE) {
            // 队列满，丢弃旧帧
            PreviewFrame* old_frame = nullptr;
            if (xQueueReceive(preview_frame_queue_, &old_frame, 0) == pdTRUE) {
                delete old_frame;
            }
            // 重新发送
            xQueueSend(preview_frame_queue_, &frame, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(66));  // 15 FPS
    }
}
```

### 资源清理

#### 停止服务时的清理流程

```cpp
void Application::StopLocalPreview() {
    if (!local_preview_active_) {
        return;
    }

    ESP_LOGI(TAG, "Stopping local preview");

    // 1. 设置停止标志
    local_preview_active_ = false;

    // 2. 等待任务退出
    if (preview_capture_task_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(200));
        preview_capture_task_ = nullptr;
    }

    if (preview_display_task_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(200));
        preview_display_task_ = nullptr;
    }

    // 3. 清空队列并释放帧内存
    if (preview_frame_queue_ != nullptr) {
        PreviewFrame* frame = nullptr;
        while (xQueueReceive(preview_frame_queue_, &frame, 0) == pdTRUE) {
            delete frame;  // 析构函数会释放 PSRAM
        }
        vQueueDelete(preview_frame_queue_);
        preview_frame_queue_ = nullptr;
    }

    // 4. 恢复显示模式
    auto* display = Board::GetInstance().GetDisplay();
    auto* lcd_display = dynamic_cast<LcdDisplay*>(display);
    if (lcd_display) {
        lcd_display->ExitPreviewMode();
    }

    ESP_LOGI(TAG, "Local preview stopped");
    PlaySound("confirm");
}
```

### 日志策略

#### 日志级别使用

| 级别  | 使用场景           | 示例                            |
| ----- | ------------------ | ------------------------------- |
| ERROR | 导致功能失败的错误 | "Failed to create capture task" |
| WARN  | 可恢复的异常情况   | "Capture failed, retrying"      |
| INFO  | 重要状态变化       | "Local preview started"         |
| DEBUG | 详细执行信息       | "Frame captured: 240x320, 20ms" |

#### 关键日志点

```cpp
// 启动/停止
ESP_LOGI(TAG, "Local preview started");
ESP_LOGI(TAG, "Local preview stopped");

// 帧处理
ESP_LOGD(TAG, "Frame captured: %dx%d, size=%zu, time=%ums",
         width, height, data_size, capture_time_ms);
ESP_LOGD(TAG, "Frame displayed: render=%ums, total=%ums",
         render_time_ms, total_time_ms);

// 错误
ESP_LOGE(TAG, "Camera not available");
ESP_LOGE(TAG, "Failed to allocate frame: %zu bytes", required_size);
ESP_LOGW(TAG, "Capture failed (%d/%d)", failures, max_failures);

// 性能监控
ESP_LOGI(TAG, "Preview stats: fps=%.1f, queue_size=%d, cpu=%.1f%%",
         actual_fps, queue_size, cpu_usage);
```

---

## 测试策略 (Testing Strategy)

### 测试方法概述

由于本功能涉及嵌入式实时系统、硬件交互（摄像头、LCD）、FreeRTOS 任务调度和 UI 渲染，**不适合使用属性测试（Property-Based Testing）**。测试策略采用以下方法：

1. **单元测试**：测试独立的函数和模块
2. **集成测试**：测试模块间交互和硬件集成
3. **系统测试**：测试完整功能流程
4. **性能测试**：验证帧率、延迟、CPU 占用等指标

### 单元测试

#### 测试范围

| 模块         | 测试内容                               | 测试方法                  |
| ------------ | -------------------------------------- | ------------------------- |
| PreviewFrame | 构造/析构、内存分配/释放               | Mock PSRAM 分配器         |
| Camera 接口  | CaptureForPreview() 返回值             | Mock Camera 对象          |
| Display 接口 | EnterPreviewMode() / ExitPreviewMode() | Mock Display 对象         |
| 状态管理     | IsLocalPreviewActive() 状态转换        | 直接测试 Application 方法 |

#### 示例测试用例

```cpp
// 测试 PreviewFrame 内存管理
TEST_CASE("PreviewFrame allocates and frees PSRAM", "[preview]") {
    size_t free_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    {
        PreviewFrame frame(240, 320);
        REQUIRE(frame.data != nullptr);
        REQUIRE(frame.data_size == 240 * 320 * 2);
        REQUIRE(frame.width == 240);
        REQUIRE(frame.height == 320);
    }  // frame 析构

    size_t free_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    REQUIRE(free_after == free_before);  // 内存已释放
}

// 测试互斥条件
TEST_CASE("Cannot start preview when monitor mode active", "[preview]") {
    Application& app = Application::GetInstance();

    // 启动监控模式
    app.StartMonitorMode();

    // 尝试启动本地预览
    bool result = app.StartLocalPreview();
    REQUIRE(result == false);
    REQUIRE(app.IsLocalPreviewActive() == false);

    // 清理
    app.StopMonitorMode();
}
```

### 集成测试

#### 测试场景

1. **完整启动/停止流程**
   - 启动本地预览
   - 验证任务创建
   - 验证队列创建
   - 验证显示模式切换
   - 停止本地预览
   - 验证资源释放

2. **帧捕获和显示流程**
   - 捕获任务获取帧
   - 帧推送到队列
   - 显示任务从队列获取帧
   - 更新 Canvas 并刷新屏幕

3. **WebSocket 命令处理**
   - 发送 start 命令
   - 验证响应
   - 验证预览启动
   - 发送 stop 命令
   - 验证预览停止

4. **错误恢复**
   - 模拟摄像头故障
   - 验证错误处理
   - 验证服务停止
   - 验证资源清理

#### 示例测试用例

```cpp
TEST_CASE("Complete preview lifecycle", "[preview][integration]") {
    Application& app = Application::GetInstance();
    auto* camera = Board::GetInstance().GetCamera();
    auto* display = Board::GetInstance().GetDisplay();

    // 确保摄像头可用
    REQUIRE(camera != nullptr);
    auto* esp32_camera = dynamic_cast<Esp32Camera*>(camera);
    REQUIRE(esp32_camera != nullptr);
    REQUIRE(esp32_camera->IsAvailable());

    // 启动预览
    bool started = app.StartLocalPreview();
    REQUIRE(started == true);
    REQUIRE(app.IsLocalPreviewActive() == true);

    // 验证任务创建
    REQUIRE(app.preview_capture_task_ != nullptr);
    REQUIRE(app.preview_display_task_ != nullptr);
    REQUIRE(app.preview_frame_queue_ != nullptr);

    // 验证显示模式
    auto* lcd_display = dynamic_cast<LcdDisplay*>(display);
    REQUIRE(lcd_display != nullptr);
    REQUIRE(lcd_display->IsPreviewMode() == true);

    // 运行一段时间
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 停止预览
    app.StopLocalPreview();
    REQUIRE(app.IsLocalPreviewActive() == false);

    // 验证资源释放
    REQUIRE(app.preview_capture_task_ == nullptr);
    REQUIRE(app.preview_display_task_ == nullptr);
    REQUIRE(app.preview_frame_queue_ == nullptr);
    REQUIRE(lcd_display->IsPreviewMode() == false);
}
```

### 系统测试

#### 测试场景

1. **用户场景测试**
   - 用户通过 APP 启动本地预览
   - 查看实时画面 30 秒
   - 通过 APP 停止本地预览
   - 验证 UI 恢复正常

2. **语音命令测试**
   - 用户说"显示监控画面"
   - 验证预览启动
   - 用户说"关闭监控画面"
   - 验证预览停止

3. **互斥测试**
   - 启动本地预览
   - 尝试启动监控模式（应失败）
   - 尝试触发人脸识别（应失败）
   - 停止本地预览
   - 启动监控模式（应成功）

4. **异常场景测试**
   - 摄像头断开时启动预览
   - 预览运行中拔掉摄像头
   - 内存不足时启动预览
   - 预览运行中设备进入睡眠

#### 测试检查点

| 检查点   | 验证内容                | 通过标准                  |
| -------- | ----------------------- | ------------------------- |
| 启动时间 | 从命令到画面显示的时间  | <500ms                    |
| 帧率     | 实际显示帧率            | ≥15 FPS                   |
| 延迟     | 捕获到显示的端到端延迟  | <50ms                     |
| CPU 占用 | 预览运行时的 CPU 使用率 | <25%                      |
| 内存占用 | 预览运行时的内存增量    | <500KB（需优化到 <400KB） |
| 稳定性   | 连续运行 10 分钟无崩溃  | 100% 成功                 |

### 性能测试

#### 测试指标

1. **帧率测试**

   ```cpp
   // 统计 10 秒内的实际帧数
   int frame_count = 0;
   TickType_t start = xTaskGetTickCount();

   while (xTaskGetTickCount() - start < pdMS_TO_TICKS(10000)) {
       PreviewFrame* frame = nullptr;
       if (xQueueReceive(queue, &frame, pdMS_TO_TICKS(100)) == pdTRUE) {
           frame_count++;
           delete frame;
       }
   }

   float fps = frame_count / 10.0f;
   ESP_LOGI(TAG, "Actual FPS: %.1f", fps);
   REQUIRE(fps >= 15.0f);
   ```

2. **延迟测试**

   ```cpp
   // 测量捕获到显示的延迟
   TickType_t capture_time = frame->timestamp;
   TickType_t display_time = xTaskGetTickCount();
   uint32_t latency_ms = (display_time - capture_time) * portTICK_PERIOD_MS;

   ESP_LOGI(TAG, "Latency: %u ms", latency_ms);
   REQUIRE(latency_ms < 50);
   ```

3. **CPU 占用测试**

   ```cpp
   // 使用 FreeRTOS 任务统计
   TaskStatus_t task_status;
   vTaskGetInfo(preview_capture_task_, &task_status, pdTRUE, eRunning);

   uint32_t total_runtime = task_status.ulRunTimeCounter;
   uint32_t total_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
   float cpu_usage = (total_runtime * 100.0f) / total_time;

   ESP_LOGI(TAG, "CPU usage: %.1f%%", cpu_usage);
   REQUIRE(cpu_usage < 25.0f);
   ```

4. **内存占用测试**

   ```cpp
   // 测量 PSRAM 使用
   size_t free_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

   app.StartLocalPreview();
   vTaskDelay(pdMS_TO_TICKS(1000));

   size_t free_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
   size_t used = free_before - free_after;

   ESP_LOGI(TAG, "PSRAM used: %zu bytes", used);
   REQUIRE(used < 500 * 1024);  // <500KB

   app.StopLocalPreview();
   vTaskDelay(pdMS_TO_TICKS(1000));

   size_t free_final = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
   REQUIRE(free_final == free_before);  // 内存已释放
   ```

### 测试环境

#### 硬件配置

- **开发板**：bread-compact-wifi-s3cam
- **芯片**：ESP32-S3-N16R8（8MB PSRAM）
- **摄像头**：DVP 接口，支持 RGB565 输出
- **LCD**：SPI 接口，240x320 分辨率

#### 软件配置

- **ESP-IDF**：v5.4+
- **LVGL**：9.x
- **测试框架**：Catch2 或 Unity

### 测试覆盖率目标

| 类型       | 目标覆盖率 | 说明             |
| ---------- | ---------- | ---------------- |
| 行覆盖率   | ≥80%       | 核心逻辑代码     |
| 分支覆盖率 | ≥70%       | 错误处理分支     |
| 功能覆盖率 | 100%       | 所有需求验收标准 |

### 测试执行计划

1. **开发阶段**：单元测试（每次提交）
2. **集成阶段**：集成测试（每日构建）
3. **发布前**：系统测试 + 性能测试（完整测试套件）
4. **回归测试**：所有测试（每次修改后）

---

## 总结

本设计文档定义了本地监控画面实时显示功能的完整技术方案，采用零拷贝优化架构，实现了以下目标：

### 核心特性

✅ **高性能**：15 FPS 帧率，<50ms 延迟，<25% CPU 占用  
✅ **零拷贝**：RGB565 原始数据直接渲染，无 JPEG 编解码开销  
✅ **双任务架构**：Capture Task + Display Task，队列缓冲平衡速度  
✅ **用户友好**：WebSocket 命令 + 语音命令控制  
✅ **系统兼容**：与监控模式、人脸识别互斥，不影响其他功能

### 技术亮点

- **参考现有架构**：借鉴 `MonitorService` + `VideoStreamService` 的设计模式
- **LVGL 集成**：使用 `lv_canvas` 组件高效渲染 RGB565 数据
- **内存优化**：PSRAM 分配，队列深度 1，内存占用 <400KB
- **错误处理**：完善的错误检测、恢复和资源清理机制
- **测试策略**：单元测试 + 集成测试 + 系统测试 + 性能测试

### 实现路径

1. **Phase 1**：扩展 Camera 和 Display 接口
2. **Phase 2**：实现 PreviewFrame 和队列管理
3. **Phase 3**：实现 Capture Task 和 Display Task
4. **Phase 4**：集成 WebSocket 命令处理
5. **Phase 5**：性能优化和测试验证

### 风险与缓解

| 风险         | 影响             | 缓解措施                      |
| ------------ | ---------------- | ----------------------------- |
| 内存超标     | 无法启动         | 队列深度降至 1，或降低分辨率  |
| 帧率不达标   | 画面卡顿         | 优化 LVGL 渲染，使用 DMA 传输 |
| CPU 占用过高 | 影响其他功能     | 降低任务优先级，优化捕获逻辑  |
| 硬件兼容性   | 部分开发板不支持 | 运行时检测，不支持则禁用功能  |

本设计文档为后续实现提供了清晰的技术路线和验证标准，确保功能的正确性、性能和可维护性。
