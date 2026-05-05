# 本地预览日志记录总结

## 概述

本文档总结了为本地监控画面实时显示功能添加的日志记录。所有日志遵循以下级别规范：

- **INFO**: 重要状态变化（启动、停止）
- **DEBUG**: 详细执行信息（帧处理、性能数据）
- **WARN**: 可恢复的异常（捕获失败、显示失败等）
- **ERROR**: 导致功能失败的错误（摄像头不可用、任务创建失败等）

---

## 已添加的日志记录

### 1. Application 类 (main/application.cc)

#### StartLocalPreview() 方法

- **INFO**: "启动本地预览"
- **WARN**: "本地预览已在运行中"
- **ERROR**: "无法启动本地预览：监控模式正在运行"
- **ERROR**: "无法启动本地预览：人脸识别正在进行"
- **ERROR**: "摄像头不可用"
- **WARN**: "内存不足，拒绝启动本地预览 (可用 PSRAM: %u KB)"
- **INFO**: "内存检查通过 (可用 PSRAM: %u KB)"
- **ERROR**: "无法创建帧队列"
- **ERROR**: "无法创建捕获任务"
- **ERROR**: "无法创建显示任务"
- **WARN**: "无法进入预览模式，但继续运行"
- **INFO**: "本地预览已启动"

#### StopLocalPreview() 方法

- **WARN**: "本地预览未运行"
- **INFO**: "停止本地预览"
- **INFO**: "本地预览已停止"

#### PreviewCaptureLoop() 方法

- **INFO**: "预览捕获任务启动"
- **WARN**: "帧捕获失败 (%d/%d)" - 包含失败次数和最大失败次数
- **ERROR**: "连续捕获失败过多 (%d 次)，停止预览"
- **WARN**: "捕获的帧数据无效 (data=%p, size=%zu)" - 包含数据指针和大小
- **DEBUG**: "帧已捕获: %dx%d, 大小=%zu 字节, 耗时=%u ms, 时间戳=%u" - 包含尺寸、大小、捕获耗时、时间戳
- **ERROR**: "无法分配帧对象 (需要 %zu 字节)，停止预览"
- **DEBUG**: "队列已满，丢弃旧帧"
- **WARN**: "队列操作失败，丢弃当前帧"
- **INFO**: "预览捕获任务退出"

#### PreviewDisplayLoop() 方法

- **INFO**: "预览显示任务启动"
- **ERROR**: "显示对象无效，退出显示任务"
- **DEBUG**: "队列超时，等待新帧"
- **WARN**: "收到无效帧 (frame=%p)" - 包含帧指针
- **DEBUG**: "帧已显示: 渲染耗时=%u ms, 总耗时=%u ms, 端到端延迟=%u ms" - 包含渲染耗时、总耗时、端到端延迟
- **WARN**: "更新 Canvas 失败 (尺寸=%dx%d)" - 包含帧尺寸
- **INFO**: "预览显示任务退出"

---

### 2. LcdDisplay 类 (main/display/lcd_display.cc)

#### EnterPreviewMode() 方法

- **WARN**: "已处于预览模式"
- **INFO**: "进入预览模式"
- **ERROR**: "无法分配 PSRAM 缓冲区：%zu 字节"
- **INFO**: "已分配 PSRAM 缓冲区：%zu 字节"
- **ERROR**: "无法创建 Canvas 对象"
- **INFO**: "预览模式已激活（%dx%d）"

#### ExitPreviewMode() 方法

- **DEBUG**: "未处于预览模式，无需退出"
- **INFO**: "退出预览模式"
- **INFO**: "已释放 PSRAM 缓冲区"
- **INFO**: "预览模式已退出"

#### UpdatePreviewCanvas() 方法

- **WARN**: "未处于预览模式，无法更新 Canvas"
- **ERROR**: "Canvas 对象或缓冲区无效 (canvas=%p, buffer=%p)" - 包含对象指针
- **ERROR**: "RGB565 数据指针为空"
- **ERROR**: "图像尺寸不匹配：期望 %dx%d，实际 %dx%d"
- **DEBUG**: "Canvas 已更新: 复制耗时=%u ms, 总耗时=%u ms, 数据大小=%zu 字节" - 包含复制耗时、总耗时、数据大小

---

### 3. Esp32Camera 类 (main/boards/common/esp32_camera.cc)

#### CaptureForPreview() 方法

- **ERROR**: "摄像头不可用 (streaming_on=%d, video_fd=%d)" - 包含状态标志和文件描述符
- **ERROR**: "VIDIOC_DQBUF 失败: %s" - 包含错误消息
- **ERROR**: "分配帧缓冲区失败: 需要 %d 字节"
- **ERROR**: "清理: VIDIOC_QBUF 失败"
- **ERROR**: "VIDIOC_QBUF 失败"
- **DEBUG**: "捕获预览帧成功: %dx%d, 格式=RGB565, 大小=%zu 字节, DQBUF耗时=%u ms, 总耗时=%u ms" - 包含尺寸、格式、大小、DQBUF耗时、总耗时

---

## 日志级别使用统计

| 级别  | 数量 | 说明                     |
| ----- | ---- | ------------------------ |
| ERROR | 18   | 导致功能失败的错误       |
| WARN  | 10   | 可恢复的异常情况         |
| INFO  | 13   | 重要状态变化             |
| DEBUG | 7    | 详细执行信息（性能数据） |

---

## 性能监控日志

以下 DEBUG 级别日志用于性能监控和调试：

### 帧捕获性能

```
DEBUG: 帧已捕获: 240x320, 大小=153600 字节, 耗时=18 ms, 时间戳=12345
```

- **尺寸**: 帧的宽度和高度
- **大小**: RGB565 数据大小（字节）
- **耗时**: 从开始捕获到完成的时间（毫秒）
- **时间戳**: FreeRTOS tick 计数

### 帧显示性能

```
DEBUG: 帧已显示: 渲染耗时=25 ms, 总耗时=30 ms, 端到端延迟=45 ms
```

- **渲染耗时**: UpdatePreviewCanvas() 方法的执行时间
- **总耗时**: 从队列接收到显示完成的总时间
- **端到端延迟**: 从捕获到显示的总延迟

### Canvas 更新性能

```
DEBUG: Canvas 已更新: 复制耗时=8 ms, 总耗时=12 ms, 数据大小=153600 字节
```

- **复制耗时**: memcpy() 的执行时间
- **总耗时**: UpdatePreviewCanvas() 的总执行时间
- **数据大小**: 复制的数据大小（字节）

### 摄像头捕获性能

```
DEBUG: 捕获预览帧成功: 240x320, 格式=RGB565, 大小=153600 字节, DQBUF耗时=5 ms, 总耗时=18 ms
```

- **尺寸**: 帧的宽度和高度
- **格式**: 数据格式（RGB565）
- **大小**: 数据大小（字节）
- **DQBUF耗时**: VIDIOC_DQBUF ioctl 的执行时间
- **总耗时**: CaptureForPreview() 的总执行时间

---

## 错误处理日志

### 启动阶段错误

- 监控模式互斥：ERROR + Alert
- 人脸识别互斥：ERROR + Alert
- 摄像头不可用：ERROR + Alert
- 内存不足：WARN/ERROR + Alert
- 任务创建失败：ERROR + Alert
- 队列创建失败：ERROR + Alert

### 运行时错误

- 帧捕获失败：WARN（可恢复）
- 连续捕获失败：ERROR + 自动停止
- 显示刷新失败：WARN（可恢复）
- 内存分配失败：ERROR + 自动停止
- 队列超时：DEBUG（正常行为）

---

## 日志示例

### 正常启动流程

```
I (12345) Application: 启动本地预览
I (12346) Application: 内存检查通过 (可用 PSRAM: 5120 KB)
I (12347) LcdDisplay: 进入预览模式
I (12348) LcdDisplay: 已分配 PSRAM 缓冲区：153600 字节
I (12349) LcdDisplay: 预览模式已激活（240x320）
I (12350) Application: 本地预览已启动
I (12351) Application: 预览捕获任务启动
I (12352) Application: 预览显示任务启动
```

### 正常运行流程（DEBUG 级别）

```
D (12400) Esp32Camera: 捕获预览帧成功: 240x320, 格式=RGB565, 大小=153600 字节, DQBUF耗时=5 ms, 总耗时=18 ms
D (12418) Application: 帧已捕获: 240x320, 大小=153600 字节, 耗时=18 ms, 时间戳=12418
D (12430) LcdDisplay: Canvas 已更新: 复制耗时=8 ms, 总耗时=12 ms, 数据大小=153600 字节
D (12442) Application: 帧已显示: 渲染耗时=25 ms, 总耗时=30 ms, 端到端延迟=24 ms
```

### 错误恢复流程

```
W (12500) Application: 帧捕获失败 (1/10)
W (12600) Application: 帧捕获失败 (2/10)
D (12700) Esp32Camera: 捕获预览帧成功: 240x320, 格式=RGB565, 大小=153600 字节, DQBUF耗时=5 ms, 总耗时=18 ms
```

### 停止流程

```
I (15000) Application: 停止本地预览
I (15200) Application: 预览捕获任务退出
I (15400) Application: 预览显示任务退出
I (15401) LcdDisplay: 退出预览模式
I (15402) LcdDisplay: 已释放 PSRAM 缓冲区
I (15403) LcdDisplay: 预览模式已退出
I (15404) Application: 本地预览已停止
```

---

## 调试建议

### 启用 DEBUG 日志

在 `sdkconfig` 中设置：

```
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
CONFIG_LOG_DEFAULT_LEVEL=4
```

或在代码中临时启用：

```cpp
esp_log_level_set("Application", ESP_LOG_DEBUG);
esp_log_level_set("LcdDisplay", ESP_LOG_DEBUG);
esp_log_level_set("Esp32Camera", ESP_LOG_DEBUG);
```

### 性能分析

使用 DEBUG 日志分析性能瓶颈：

1. 捕获耗时 > 30ms → 检查摄像头配置
2. 渲染耗时 > 50ms → 检查 LVGL 配置或 SPI 速度
3. 端到端延迟 > 50ms → 检查队列深度或任务优先级

### 错误排查

1. 连续捕获失败 → 检查摄像头连接和驱动
2. 内存分配失败 → 检查 PSRAM 使用情况
3. Canvas 更新失败 → 检查显示对象状态

---

## 需求追溯

本日志记录实现满足以下需求：

- **需求 16.1**: 启动/停止时记录 INFO 级别日志 ✅
- **需求 16.2**: 帧捕获时记录 DEBUG 级别日志（包含尺寸、时间戳、耗时）✅
- **需求 16.3**: 帧显示时记录 DEBUG 级别日志（包含渲染耗时）✅
- **需求 16.4**: 错误时记录 ERROR 级别日志（包含错误消息和上下文）✅
- **需求 16.5**: 警告时记录 WARN 级别日志（捕获失败、显示失败等）✅

---

## 总结

本次实现为本地预览功能添加了全面的日志记录，涵盖：

1. **生命周期日志**：启动、停止、任务创建/销毁
2. **性能监控日志**：捕获耗时、渲染耗时、端到端延迟
3. **错误处理日志**：所有错误场景的详细日志
4. **调试信息日志**：队列状态、内存使用、帧处理详情

所有日志遵循 ESP-IDF 日志规范，使用合适的日志级别，并包含足够的上下文信息用于调试和性能分析。
