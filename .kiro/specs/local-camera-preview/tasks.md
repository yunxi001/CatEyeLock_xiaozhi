# 实现计划：本地监控画面实时显示功能

## 概述

本实现计划将本地监控画面实时显示功能分解为一系列可执行的编码任务。该功能基于零拷贝优化方案，摄像头直接输出 RGB565 原始数据用于本地预览，实现低延迟（<50ms）、高帧率（15 FPS）的本地监控画面显示。

**技术方案**：

- 数据格式：RGB565 原始数据（16 位色彩，每像素 2 字节）
- 架构：双任务架构（Capture Task + Display Task）
- 队列缓冲：FreeRTOS 队列（深度 1）
- 显示：LVGL Canvas 组件直接渲染 RGB565 数据
- 控制：WebSocket 命令 + 语音命令（服务器端 STT + LLM）

**性能目标**：

- 帧率：15 FPS
- 延迟：<50ms
- CPU 占用：<25%
- 内存占用：<400KB（PSRAM）

---

## 任务列表

- [x] 1. 创建核心数据结构和头文件
  - [x] 1.1 创建 PreviewFrame 数据结构
    - 在 `main/local_preview/preview_frame.h` 中定义 `PreviewFrame` 结构体
    - 包含 RGB565 数据指针、尺寸、时间戳等字段
    - 实现构造函数（PSRAM 分配）和析构函数（PSRAM 释放）
    - 禁止拷贝，只允许移动语义
    - _需求：1.1, 9.1, 12.1_

  - [x] 1.2 创建本地预览服务头文件
    - 在 `main/local_preview/local_preview_service.h` 中定义服务接口
    - 声明启动/停止方法、状态查询方法
    - 声明 FreeRTOS 任务函数和队列句柄
    - 定义常量配置（帧率、队列深度、任务优先级等）
    - _需求：7.1, 8.1, 9.1_

- [x] 2. 扩展 Camera 接口
  - [x] 2.1 在 Esp32Camera 类中添加 CaptureForPreview() 方法
    - 在 `main/boards/common/esp32_camera.h` 中声明方法
    - 在 `main/boards/common/esp32_camera.cc` 中实现方法
    - 配置摄像头输出 RGB565 格式
    - 捕获一帧原始数据（不丢弃前两帧，不显示预览）
    - 返回捕获成功/失败状态
    - _需求：1.1, 1.2, 1.3, 2.1, 2.2, 2.3_

  - [x] 2.2 添加 RGB565 数据访问方法
    - 在 `main/boards/common/esp32_camera.h` 中声明 `GetRgb565Data()` 和 `GetRgb565DataSize()`
    - 在 `main/boards/common/esp32_camera.cc` 中实现方法
    - 返回当前帧的 RGB565 数据指针和大小
    - 添加数据有效性检查
    - _需求：2.4, 2.5_

- [ ] 3. 扩展 LCD Display 接口
  - [x] 3.1 在 LcdDisplay 类中添加预览模式相关成员变量
    - 在 `main/display/lcd_display.h` 中添加私有成员变量
    - `lv_obj_t* preview_canvas_` - 预览 Canvas 对象
    - `void* preview_canvas_buffer_` - Canvas 缓冲区（PSRAM）
    - `bool preview_mode_active_` - 预览模式标志
    - _需求：3.1, 3.2, 4.1_

  - [x] 3.2 实现 EnterPreviewMode() 方法
    - 在 `main/display/lcd_display.h` 中声明方法
    - 在 `main/display/lcd_display.cc` 中实现方法
    - 隐藏所有 LVGL UI 组件（状态栏、表情、消息）
    - 创建全屏 Canvas 对象，配置为 RGB565 格式
    - 分配 PSRAM 缓冲区（240x320x2 字节）
    - 设置预览模式标志为 true
    - _需求：3.3, 4.1, 4.2, 4.3_

  - [x] 3.3 实现 ExitPreviewMode() 方法
    - 在 `main/display/lcd_display.h` 中声明方法
    - 在 `main/display/lcd_display.cc` 中实现方法
    - 销毁 Canvas 对象
    - 释放 PSRAM 缓冲区
    - 恢复 LVGL UI 组件显示
    - 设置预览模式标志为 false
    - _需求：3.5, 4.4, 4.5_

  - [x] 3.4 实现 UpdatePreviewCanvas() 方法
    - 在 `main/display/lcd_display.h` 中声明方法
    - 在 `main/display/lcd_display.cc` 中实现方法
    - 将 RGB565 数据复制到 Canvas 缓冲区
    - 使用 `lv_canvas_set_buffer()` 更新 Canvas
    - 触发 LVGL 重绘
    - 添加错误处理（数据为空、尺寸不匹配等）
    - _需求：8.3, 8.4_

  - [x] 3.5 实现 IsPreviewMode() 方法
    - 在 `main/display/lcd_display.h` 中声明方法
    - 在 `main/display/lcd_display.cc` 中实现方法
    - 返回 `preview_mode_active_` 标志
    - _需求：15.3_

- [x] 4. 实现本地预览服务核心逻辑
  - [x] 4.1 实现 PreviewFrame 构造函数和析构函数
    - 在 `main/local_preview/preview_frame.h` 中实现
    - 构造函数：使用 `heap_caps_malloc(MALLOC_CAP_SPIRAM)` 分配 PSRAM
    - 计算数据大小：`width * height * 2`（RGB565 每像素 2 字节）
    - 析构函数：使用 `heap_caps_free()` 释放 PSRAM
    - 添加内存分配失败检查
    - _需求：12.1, 12.2_

  - [x] 4.2 实现 Capture Task 任务函数
    - 在 `main/local_preview/local_preview_service.cc` 中实现 `PreviewCaptureTask()`
    - 创建 FreeRTOS 任务入口函数（静态）
    - 调用实例的 `PreviewCaptureLoop()` 方法
    - _需求：7.1_

  - [x] 4.3 实现 Capture Task 循环逻辑
    - 在 `main/local_preview/local_preview_service.cc` 中实现 `PreviewCaptureLoop()`
    - 以 15 FPS 频率捕获帧（66ms 间隔）
    - 调用 `CaptureForPreview()` 获取 RGB565 数据
    - 分配 `PreviewFrame` 对象并复制数据
    - 推送到 FreeRTOS 队列（非阻塞）
    - 队列满时丢弃旧帧并插入新帧
    - 添加连续失败检测和错误恢复
    - _需求：7.2, 7.3, 7.4, 9.3, 14.2_

  - [x] 4.4 实现 Display Task 任务函数
    - 在 `main/local_preview/local_preview_service.cc` 中实现 `PreviewDisplayTask()`
    - 创建 FreeRTOS 任务入口函数（静态）
    - 调用实例的 `PreviewDisplayLoop()` 方法
    - _需求：8.1_

  - [x] 4.5 实现 Display Task 循环逻辑
    - 在 `main/local_preview/local_preview_service.cc` 中实现 `PreviewDisplayLoop()`
    - 从队列获取最新帧（超时 100ms）
    - 调用 `UpdatePreviewCanvas()` 更新显示
    - 释放帧内存
    - 添加错误处理（显示失败、队列超时等）
    - _需求：8.2, 8.3, 8.4, 12.2, 14.3_

- [x] 5. 集成到 Application 类
  - [x] 5.1 在 Application 类中添加本地预览相关成员变量
    - 在 `main/application.h` 中添加私有成员变量
    - `bool local_preview_active_` - 预览活动标志
    - `TaskHandle_t preview_capture_task_` - 捕获任务句柄
    - `TaskHandle_t preview_display_task_` - 显示任务句柄
    - `QueueHandle_t preview_frame_queue_` - 帧队列句柄
    - _需求：15.1, 15.2_

  - [x] 5.2 实现 StartLocalPreview() 方法
    - 在 `main/application.h` 中声明方法
    - 在 `main/application.cc` 中实现方法
    - 检查摄像头可用性
    - 检查与监控模式的互斥（`IsMonitorMode()`）
    - 检查与人脸识别的互斥（`face_recognition_in_progress_`）
    - 创建 FreeRTOS 队列（深度 1）
    - 创建 Capture Task（优先级 5，栈 4096）
    - 创建 Display Task（优先级 5，栈 4096）
    - 调用 `EnterPreviewMode()` 切换显示模式
    - 设置 `local_preview_active_` 为 true
    - 播放确认音效
    - 添加完整的错误处理和资源清理
    - _需求：5.3, 7.1, 8.1, 9.1, 9.2, 13.1, 12.1, 12.2, 14.1, 14.5, 15.1, 17.1_

  - [x] 5.3 实现 StopLocalPreview() 方法
    - 在 `main/application.h` 中声明方法
    - 在 `main/application.cc` 中实现方法
    - 设置 `local_preview_active_` 为 false
    - 等待任务退出（延迟 200ms）
    - 清空队列并释放所有帧内存
    - 删除队列
    - 调用 `ExitPreviewMode()` 恢复显示模式
    - 播放确认音效
    - _需求：7.5, 8.5, 9.5, 12.3, 12.4, 15.2, 17.2_

  - [x] 5.4 实现 IsLocalPreviewActive() 方法
    - 在 `main/application.h` 中声明方法
    - 在 `main/application.cc` 中实现方法
    - 返回 `local_preview_active_` 标志
    - _需求：15.3_

- [x] 6. 实现 WebSocket 命令处理
  - [x] 6.1 在 Protocol 类中添加本地预览命令处理
    - 在 `main/protocols/protocol.cc` 的 JSON 消息处理函数中添加分支
    - 检测 `type == "local_preview"`
    - 解析 `action` 字段（"start" 或 "stop"）
    - 调用 `Application::StartLocalPreview()` 或 `StopLocalPreview()`
    - 构造并发送响应 JSON（包含 status 和 error 字段）
    - _需求：5.1, 5.2, 5.4_

  - [ ]\* 6.2 编写 WebSocket 命令处理单元测试
    - 测试启动命令的正确处理
    - 测试停止命令的正确处理
    - 测试错误响应（摄像头不可用、监控模式运行中等）
    - 测试重复命令的处理
    - _需求：5.1, 5.2, 5.4, 5.5, 6.4, 6.5_

- [x] 7. 实现互斥控制
  - [x] 7.1 在 StartLocalPreview() 中添加监控模式互斥检查
    - 检查 `IsMonitorMode()` 返回值
    - 如果监控模式运行中，拒绝启动并返回错误
    - 记录 ERROR 级别日志
    - 播放错误音效并显示错误消息
    - _需求：13.1, 13.3, 14.1_

  - [x] 7.2 在 StartMonitorMode() 中添加本地预览互斥检查
    - 检查 `IsLocalPreviewActive()` 返回值
    - 如果本地预览运行中，拒绝启动并返回错误
    - 记录 ERROR 级别日志
    - 播放错误音效并显示错误消息
    - _需求：13.2, 13.4_

  - [x] 7.3 在 TriggerFaceRecognition() 中添加本地预览互斥检查
    - 检查 `IsLocalPreviewActive()` 返回值
    - 如果本地预览运行中，拒绝触发并返回错误
    - 记录 ERROR 级别日志
    - 播放错误音效并显示错误消息
    - _需求：12.1, 12.3_

  - [x] 7.4 在 StartLocalPreview() 中添加人脸识别互斥检查
    - 检查 `face_recognition_in_progress_` 标志
    - 如果人脸识别运行中，拒绝启动并返回错误
    - 记录 ERROR 级别日志
    - 播放错误音效并显示错误消息
    - _需求：12.2, 12.4_

- [x] 8. 添加日志记录
  - [x] 8.1 在关键操作点添加日志
    - 启动/停止：INFO 级别（"Local preview started/stopped"）
    - 帧捕获：DEBUG 级别（包含尺寸、时间戳、耗时）
    - 帧显示：DEBUG 级别（包含渲染耗时）
    - 错误：ERROR 级别（包含错误消息和上下文）
    - 警告：WARN 级别（捕获失败、显示失败等）
    - _需求：16.1, 16.2, 16.3, 16.4, 16.5_

- [x] 9. 添加用户反馈
  - [x] 9.1 实现启动/停止音效播放
    - 在 `StartLocalPreview()` 成功时播放确认音效
    - 在 `StopLocalPreview()` 时播放确认音效
    - 在启动失败时播放错误音效
    - _需求：17.1, 17.2, 17.3_

  - [x] 9.2 实现"预览中"指示器（可选）
    - 在 `EnterPreviewMode()` 中在屏幕角落显示指示器
    - 在 `ExitPreviewMode()` 中隐藏指示器
    - 使用 LVGL label 组件，半透明背景
    - _需求：17.4, 17.5_

- [x] 10. 性能优化和内存管理
  - [ ] 10.1 优化队列深度
    - 将队列深度从 2 降低到 1（节省 ~153KB PSRAM）
    - 验证帧率和延迟仍满足要求
    - _需求：9.2, 11.5_

  - [ ] 10.2 添加内存使用监控
    - 在启动时记录 PSRAM 空闲大小
    - 在运行时定期检查 PSRAM 使用情况
    - 在停止时验证内存已完全释放
    - _需求：12.1, 12.4, 12.5_

  - [ ] 10.3 优化帧捕获性能
    - 确保 `CaptureForPreview()` 耗时 <30ms
    - 使用 DMA 传输（如果硬件支持）
    - 避免不必要的内存拷贝
    - _需求：11.2_

  - [ ] 10.4 优化显示刷新性能
    - 确保 `UpdatePreviewCanvas()` 耗时 <50ms
    - 使用 LVGL 的异步刷新机制
    - 优化 SPI 传输速度
    - _需求：11.3_

- [ ] 11. 集成测试和验证
  - [ ]\* 11.1 编写完整生命周期集成测试
    - 测试启动 → 运行 → 停止流程
    - 验证任务创建和销毁
    - 验证队列创建和清理
    - 验证显示模式切换
    - 验证资源释放
    - _需求：所有需求_

  - [ ]\* 11.2 编写性能测试
    - 测量实际帧率（目标 ≥15 FPS）
    - 测量端到端延迟（目标 <50ms）
    - 测量 CPU 占用率（目标 <25%）
    - 测量内存占用（目标 <400KB）
    - _需求：11.1, 11.2, 11.3, 11.4, 11.5_

  - [ ]\* 11.3 编写互斥测试
    - 测试与监控模式的互斥
    - 测试与人脸识别的互斥
    - 测试重复启动/停止
    - _需求：13.1, 13.2, 13.3, 13.4, 12.1, 12.2_

  - [ ]\* 11.4 编写错误恢复测试
    - 模拟摄像头故障
    - 模拟内存不足
    - 模拟显示刷新失败
    - 验证错误处理和资源清理
    - _需求：14.1, 14.2, 14.3, 14.4, 14.5_

- [ ] 12. 检查点 - 确保所有测试通过
  - [ ] 12.1 运行所有单元测试
    - 确保所有测试通过，无失败用例
    - 如有问题，修复后重新测试

  - [ ] 12.2 运行集成测试
    - 确保完整流程正常工作
    - 验证性能指标达标
    - 如有问题，修复后重新测试

  - [ ] 12.3 在实际硬件上验证
    - 在 `bread-compact-wifi-s3cam` 开发板上测试
    - 验证画面显示流畅，无卡顿
    - 验证音效播放正常
    - 验证 WebSocket 命令响应正常

  - [ ] 12.4 询问用户是否有问题或需要调整
    - 确保所有测试通过，询问用户是否有问题或需要调整

---

## 注意事项

### 任务标记说明

- `[ ]` - 核心实现任务，必须完成
- `[ ]*` - 可选测试任务，建议完成但可跳过以加快 MVP 开发

### 实现顺序

任务按照依赖关系排序，建议按顺序执行：

1. 数据结构和接口定义（任务 1-3）
2. 核心逻辑实现（任务 4-5）
3. 命令处理和互斥控制（任务 6-7）
4. 日志和用户反馈（任务 8-9）
5. 性能优化（任务 10）
6. 测试验证（任务 11-12）

### 关键技术点

- **PSRAM 分配**：所有帧缓冲区使用 `heap_caps_malloc(MALLOC_CAP_SPIRAM)`
- **零拷贝优化**：RGB565 数据直接从摄像头到 Canvas，无编解码
- **任务优先级**：Capture Task 和 Display Task 优先级设为 5（与 VideoStreamService 相同）
- **队列深度**：设为 1 以节省内存（~153KB）
- **错误恢复**：连续失败 10 次后自动停止服务

### 性能目标验证

- 帧率：使用 FreeRTOS tick 计数器测量实际 FPS
- 延迟：记录捕获时间戳和显示时间戳，计算差值
- CPU 占用：使用 `vTaskGetInfo()` 获取任务运行时间
- 内存占用：使用 `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` 测量

### 互斥关系

- 本地预览 ⊗ 监控模式（不能同时运行）
- 本地预览 ⊗ 人脸识别（不能同时运行）
- 启动前必须检查互斥条件，失败时返回错误

### 日志级别使用

- **ERROR**：导致功能失败的错误（摄像头不可用、任务创建失败等）
- **WARN**：可恢复的异常（捕获失败、显示失败等）
- **INFO**：重要状态变化（启动、停止）
- **DEBUG**：详细执行信息（帧处理、性能数据）

---

## 参考文档

- 需求文档：`.kiro/specs/local-camera-preview/requirements.md`
- 设计文档：`.kiro/specs/local-camera-preview/design.md`
- 现有监控模式实现：`main/monitor/monitor_service.*`、`main/video/video_stream_service.*`
- 摄像头接口：`main/boards/common/esp32_camera.*`
- 显示接口：`main/display/lcd_display.*`
- 应用主控：`main/application.*`
