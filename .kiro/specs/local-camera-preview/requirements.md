# 需求文档 - 本地监控画面实时显示功能

## 简介

本文档定义了为智能猫眼门锁系统添加本地监控画面实时显示功能的需求。该功能允许用户在 ESP32-S3 的 LCD 屏幕上实时查看摄像头画面，并支持通过手机 APP 或语音命令控制显示/隐藏。

本功能基于零拷贝优化方案，摄像头直接输出 RGB565 原始数据用于本地预览，避免 JPEG 编解码开销，实现低延迟、高帧率的本地监控画面显示。该功能与现有的监控模式（视频流发送到服务器）和人脸识别功能兼容，不会影响系统的其他功能。

---

## 术语表

- **ESP32-S3**: 主应用处理器，运行 xiaozhi-esp32 固件，型号为 ESP32-S3-N16R8（8MB PSRAM）
- **LCD_Display**: LCD 显示屏，通过 SPI 接口连接，分辨率为 240x320 或其他配置
- **LVGL**: 轻量级图形库，用于 LCD 显示 UI 渲染
- **Camera**: DVP 接口摄像头模块，支持 RGB565 原始数据输出
- **RGB565**: 16 位色彩格式，每像素 2 字节，5 位红色、6 位绿色、5 位蓝色
- **Preview_Mode**: 预览模式，LCD 全屏显示摄像头画面
- **Normal_Mode**: 正常模式，LCD 显示 LVGL UI（状态、表情、消息）
- **Monitor_Mode**: 监控模式，视频流发送到服务器进行远程查看
- **WebSocket_Protocol**: WebSocket 通信协议，用于接收 APP 控制命令
- **Voice_Command**: 语音命令，用户通过语音控制显示/隐藏监控画面
- **Zero_Copy**: 零拷贝优化，摄像头原始数据直接用于显示，无需编解码
- **Frame_Buffer**: 帧缓冲区，存储摄像头捕获的一帧图像数据
- **LVGL_Canvas**: LVGL 画布组件，用于显示原始图像数据
- **LVGL_Image**: LVGL 图像组件，用于显示图像
- **Display_Task**: 显示任务，FreeRTOS 任务，负责定期刷新 LCD 画面
- **Capture_Task**: 捕获任务，FreeRTOS 任务，负责定期从摄像头捕获帧

---

## 需求

### 需求 1: 摄像头 RGB565 原始数据捕获

**用户故事:** 作为系统开发者，我希望摄像头能够输出 RGB565 原始数据，以便本地预览时实现零拷贝优化。

#### 验收标准

1. WHEN 摄像头初始化时，THE Camera SHALL 配置输出格式为 RGB565
2. WHEN 捕获一帧用于本地预览时，THE Camera SHALL 返回 RGB565 格式的原始数据
3. WHEN 捕获一帧用于网络发送时，THE Camera SHALL 返回 JPEG 编码的数据
4. WHEN 摄像头配置分辨率时，THE Camera SHALL 支持 240x240、240x320、320x240 等常见 LCD 分辨率
5. WHEN 摄像头不可用时，THE Camera SHALL 返回错误状态并记录日志

---

### 需求 2: 本地预览帧捕获接口

**用户故事:** 作为系统开发者，我希望有专用的 API 捕获用于本地预览的帧，以便与监控模式的捕获逻辑分离。

#### 验收标准

1. THE Camera SHALL 提供 CaptureForPreview() 函数用于本地预览
2. WHEN 调用 CaptureForPreview() 时，THE Camera SHALL 返回 RGB565 格式的帧数据
3. WHEN 调用 CaptureForPreview() 时，THE Camera SHALL 不在屏幕上显示预览（避免重复渲染）
4. WHEN 调用 CaptureForPreview() 时，THE Camera SHALL 只捕获一帧（不丢弃前两帧）
5. WHEN CaptureForPreview() 失败时，THE Camera SHALL 返回 nullptr 并记录错误日志

---

### 需求 3: LVGL 画布组件集成

**用户故事:** 作为系统开发者，我希望使用 LVGL 画布组件显示 RGB565 原始数据，以便实现零拷贝渲染。

#### 验收标准

1. THE LCD_Display SHALL 创建 lv_canvas 对象用于显示摄像头画面
2. WHEN 创建画布时，THE LCD_Display SHALL 配置画布格式为 LV_COLOR_FORMAT_RGB565
3. WHEN 创建画布时，THE LCD_Display SHALL 分配 PSRAM 缓冲区存储画布数据
4. WHEN 更新画布时，THE LCD_Display SHALL 使用 lv_canvas_set_buffer() 设置 RGB565 数据
5. WHEN 销毁画布时，THE LCD_Display SHALL 释放 PSRAM 缓冲区

---

### 需求 4: 预览模式切换

**用户故事:** 作为用户，我希望能够在正常 UI 模式和全屏预览模式之间切换，以便查看门口实时画面。

#### 验收标准

1. WHEN 收到"显示监控画面"命令时，THE LCD_Display SHALL 切换到 Preview_Mode
2. WHEN 切换到 Preview_Mode 时，THE LCD_Display SHALL 隐藏所有 LVGL UI 组件（状态栏、表情、消息）
3. WHEN 切换到 Preview_Mode 时，THE LCD_Display SHALL 显示画布组件并设置为全屏
4. WHEN 收到"关闭监控画面"命令时，THE LCD_Display SHALL 切换回 Normal_Mode
5. WHEN 切换回 Normal_Mode 时，THE LCD_Display SHALL 隐藏画布组件并恢复 LVGL UI 组件

---

### 需求 5: WebSocket 控制命令处理

**用户故事:** 作为用户，我希望通过手机 APP 控制本地监控画面的显示/隐藏，以便远程查看门口情况。

#### 验收标准

1. WHEN 服务器发送 type="local_preview" 且 action="start" 的 JSON 消息时，THE Application SHALL 启动本地预览
2. WHEN 服务器发送 type="local_preview" 且 action="stop" 的 JSON 消息时，THE Application SHALL 停止本地预览
3. WHEN 收到启动命令时，THE Application SHALL 检查摄像头是否可用
4. WHEN 摄像头不可用时，THE Application SHALL 返回错误响应并记录日志
5. WHEN 收到停止命令时，THE Application SHALL 停止捕获任务并恢复正常 UI

---

### 需求 6: 语音命令处理

**用户故事:** 作为用户，我希望通过语音命令控制本地监控画面的显示/隐藏，以便免提操作。

**技术说明:** 语音命令通过服务器端 STT + LLM 意图识别实现。用户语音发送到服务器，服务器识别意图后返回 `{"type": "local_preview", "action": "start/stop"}` 命令。

#### 验收标准

1. WHEN 服务器识别到"显示监控画面"或"打开监控画面"意图时，THE Application SHALL 接收 type="local_preview" 且 action="start" 的 JSON 消息并启动本地预览
2. WHEN 服务器识别到"关闭监控画面"或"隐藏监控画面"意图时，THE Application SHALL 接收 type="local_preview" 且 action="stop" 的 JSON 消息并停止本地预览
3. WHEN 收到启动命令时，THE Application SHALL 播放确认音效
4. WHEN 本地预览已启动时，THE Application SHALL 忽略重复的启动命令
5. WHEN 本地预览未启动时，THE Application SHALL 忽略停止命令

---

### 需求 7: 帧捕获任务

**用户故事:** 作为系统开发者，我希望有独立的任务定期捕获摄像头帧，以便实现流畅的本地预览。

**性能说明:** 基于当前监控模式的实测数据，捕获 RGB565 原始帧耗时约 20ms，支持 15-20 FPS 的目标帧率。

#### 验收标准

1. WHEN 本地预览启动时，THE Application SHALL 创建 Capture_Task 任务
2. WHEN Capture_Task 运行时，THE Capture_Task SHALL 以 15 FPS 的频率捕获帧
3. WHEN 捕获一帧时，THE Capture_Task SHALL 调用 CaptureForPreview() 获取 RGB565 数据
4. WHEN 捕获成功时，THE Capture_Task SHALL 将帧数据传递给 Display_Task
5. WHEN 本地预览停止时，THE Application SHALL 删除 Capture_Task 任务

---

### 需求 8: 显示刷新任务

**用户故事:** 作为系统开发者，我希望有独立的任务定期刷新 LCD 画面，以便显示最新的摄像头帧。

**性能说明:** LVGL 渲染 RGB565 数据到 Canvas 耗时约 10-15ms，SPI 屏幕刷新耗时约 20-30ms，总计 30-45ms/帧，支持 15-20 FPS。

#### 验收标准

1. WHEN 本地预览启动时，THE Application SHALL 创建 Display_Task 任务
2. WHEN Display_Task 运行时，THE Display_Task SHALL 从队列中获取最新的帧数据
3. WHEN 获取到新帧时，THE Display_Task SHALL 更新 LVGL 画布并触发重绘
4. WHEN 队列为空时，THE Display_Task SHALL 等待新帧到达
5. WHEN 本地预览停止时，THE Application SHALL 删除 Display_Task 任务

---

### 需求 9: 帧队列管理

**用户故事:** 作为系统开发者，我希望使用队列缓冲摄像头帧，以便平衡捕获和显示速度。

#### 验收标准

1. THE Application SHALL 创建 FreeRTOS 队列用于传递帧数据
2. WHEN 创建队列时，THE Application SHALL 设置队列深度为 2 帧
3. WHEN 队列已满时，THE Capture_Task SHALL 丢弃最旧的帧并插入新帧
4. WHEN 队列为空时，THE Display_Task SHALL 阻塞等待新帧（超时 100ms）
5. WHEN 本地预览停止时，THE Application SHALL 清空队列并释放所有帧数据

---

### 需求 11: 性能优化

**用户故事:** 作为用户，我希望本地预览流畅且低延迟，以便实时查看门口情况。

#### 验收标准

1. WHEN 本地预览运行时，THE Application SHALL 实现 15 FPS 的帧率
2. WHEN 捕获一帧时，THE Capture_Task SHALL 在 30ms 内完成捕获
3. WHEN 显示一帧时，THE Display_Task SHALL 在 50ms 内完成渲染
4. WHEN 本地预览运行时，THE Application SHALL 保持 CPU 占用率低于 25%
5. WHEN 本地预览运行时，THE Application SHALL 保持内存占用增量低于 500KB

---

### 需求 12: 内存管理

**用户故事:** 作为系统开发者，我希望本地预览功能正确管理内存，以便避免内存泄漏和溢出。

#### 验收标准

1. WHEN 分配帧缓冲区时，THE Application SHALL 使用 PSRAM（MALLOC_CAP_SPIRAM）
2. WHEN 帧数据不再使用时，THE Application SHALL 立即释放内存
3. WHEN 队列中的帧被替换时，THE Application SHALL 释放旧帧的内存
4. WHEN 本地预览停止时，THE Application SHALL 释放所有分配的内存
5. WHEN 内存不足时，THE Application SHALL 拒绝启动本地预览并记录错误

---

### 需求 13: 与监控模式的互斥

**用户故事:** 作为系统架构师，我希望本地预览与监控模式互斥，以便避免摄像头资源冲突。

#### 验收标准

1. WHEN 监控模式运行时，THE Application SHALL 拒绝启动本地预览
2. WHEN 本地预览运行时，THE Application SHALL 拒绝启动监控模式
3. WHEN 收到启动本地预览命令且监控模式运行时，THE Application SHALL 返回错误响应
4. WHEN 收到启动监控模式命令且本地预览运行时，THE Application SHALL 返回错误响应
5. WHEN 本地预览停止时，THE Application SHALL 允许启动监控模式

---

### 需求 12: 与人脸识别的互斥

**用户故事:** 作为系统架构师，我希望本地预览与人脸识别功能互斥，以便避免摄像头资源冲突。

#### 验收标准

1. WHEN 本地预览运行时，THE Application SHALL 拒绝触发人脸识别
2. WHEN 人脸识别运行时，THE Application SHALL 拒绝启动本地预览
3. WHEN 收到人脸识别触发请求且本地预览运行时，THE Application SHALL 返回错误响应并记录日志
4. WHEN 收到启动本地预览命令且人脸识别运行时，THE Application SHALL 返回错误响应并记录日志
5. WHEN 本地预览停止时，THE Application SHALL 允许触发人脸识别

---

### 需求 14: 错误处理

**用户故事:** 作为系统开发者，我希望本地预览功能具有完善的错误处理，以便系统能够优雅地恢复。

#### 验收标准

1. WHEN 摄像头不可用时，THE Application SHALL 拒绝启动本地预览并记录错误
2. WHEN 帧捕获失败时，THE Capture_Task SHALL 记录错误并继续尝试捕获下一帧
3. WHEN 显示刷新失败时，THE Display_Task SHALL 记录错误并继续处理下一帧
4. WHEN 内存分配失败时，THE Application SHALL 停止本地预览并释放已分配的资源
5. WHEN 任务创建失败时，THE Application SHALL 清理已创建的资源并返回错误

---

### 需求 15: 状态管理

**用户故事:** 作为系统开发者，我希望本地预览功能正确管理设备状态，以便与其他功能协调。

#### 验收标准

1. WHEN 本地预览启动时，THE Application SHALL 设置内部标志 local*preview_active* 为 true
2. WHEN 本地预览停止时，THE Application SHALL 设置内部标志 local*preview_active* 为 false
3. WHEN 查询本地预览状态时，THE Application SHALL 返回 local*preview_active* 的值
4. WHEN 设备进入睡眠模式时，THE Application SHALL 自动停止本地预览
5. WHEN 设备从睡眠模式唤醒时，THE Application SHALL 不自动恢复本地预览

---

### 需求 16: 日志记录

**用户故事:** 作为系统开发者，我希望本地预览功能提供详细的日志，以便调试和监控系统行为。

#### 验收标准

1. WHEN 本地预览启动时，THE Application SHALL 记录 INFO 级别日志"Local preview started"
2. WHEN 本地预览停止时，THE Application SHALL 记录 INFO 级别日志"Local preview stopped"
3. WHEN 捕获一帧时，THE Capture_Task SHALL 记录 DEBUG 级别日志包含帧尺寸和时间戳
4. WHEN 显示一帧时，THE Display_Task SHALL 记录 DEBUG 级别日志包含渲染耗时
5. WHEN 发生错误时，THE Application SHALL 记录 ERROR 级别日志包含错误消息和上下文

---

### 需求 17: 用户界面反馈

**用户故事:** 作为用户，我希望在本地预览启动/停止时得到视觉和听觉反馈，以便确认操作成功。

#### 验收标准

1. WHEN 本地预览启动时，THE Application SHALL 播放确认音效
2. WHEN 本地预览停止时，THE Application SHALL 播放确认音效
3. WHEN 本地预览启动失败时，THE Application SHALL 播放错误音效并显示错误消息
4. WHEN 本地预览运行时，THE LCD_Display SHALL 在屏幕角落显示"预览中"指示器
5. WHEN 本地预览停止时，THE LCD_Display SHALL 隐藏"预览中"指示器

---

### 需求 20: 电源管理

**状态**: 已移除（不需要）

---

## 总结

本需求文档定义了 17 个需求，共 85 条验收标准，涵盖本地监控画面实时显示功能的以下方面：

- **摄像头集成**（需求 1-2）：RGB565 原始数据捕获和专用预览接口
- **显示渲染**（需求 3-4）：LVGL 画布集成和模式切换
- **控制接口**（需求 5-6）：WebSocket 命令和语音命令处理（基于服务器端 STT + LLM）
- **任务管理**（需求 7-9）：帧捕获任务（15 FPS）、显示任务和队列管理
- **性能优化**（需求 10-11）：性能指标（15 FPS, <25% CPU）和内存管理
- **系统集成**（需求 12-13）：与监控模式互斥、与人脸识别互斥
- **质量属性**（需求 14-17）：错误处理、状态管理、日志记录、用户反馈

所有需求遵循 EARS 模式，符合 INCOSE 质量规则。每个需求都是可测试的、明确的，并且可追溯到设计和实现。

**技术方案**: 零拷贝优化（方案 B），摄像头直接输出 RGB565 原始数据用于本地预览，避免 JPEG 编解码开销，实现低延迟（<50ms）、高帧率（15 FPS）的本地监控画面显示。

---

## 特殊说明

### 解析器和序列化器需求

本功能不涉及解析器或序列化器，因此无需包含解析器相关的需求。

### 属性测试建议

以下验收标准适合使用属性测试（Property-Based Testing）：

1. **需求 9.3**：队列满时丢弃旧帧 - 测试不同队列深度和帧速率
2. **需求 11.1-11.5**：性能指标 - 测试各种负载条件下的性能表现

其他验收标准更适合使用集成测试或单元测试。4. **需求 12.1-12.4**：内存管理 - 测试各种内存分配和释放场景

其他验收标准更适合使用集成测试或单元测试。
