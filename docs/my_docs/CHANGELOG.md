# 变更日志 (CHANGELOG)

本文件记录项目的重要变更历史。

---

## 2025-01-XX

### 修改文件：`main/mcp_server.cc`

**修改位置**：`McpServer::AddCommonTools()` 函数中的摄像头工具部分

**修改时间**：2025-01-XX（具体时间待确认）

**变更内容**：

- 新增 MCP 工具函数 `self.camera.show_peephole`（猫眼画面显示控制）
- 该工具允许 AI 助手控制设备屏幕上的猫眼摄像头画面显示/隐藏

**实现功能**：

1. **猫眼画面显示控制**：
   - 通过 `enable` 参数控制显示（true）或隐藏（false）
   - 启动时调用 `Application::StartLocalPreview()` 启动本地摄像头预览
   - 关闭时调用 `Application::StopLocalPreview()` 停止预览
2. **状态检查与互斥**：
   - 检测是否已经在显示猫眼画面（避免重复启动）
   - 检测是否处于远程监控模式（监控模式与本地预览互斥）
   - 提供友好的错误提示和状态反馈

3. **使用场景**：
   - 用户询问"谁在门口"、"查看摄像头"、"显示监控画面"时触发
   - 集成到智能门锁的 AI 语音交互流程中

**技术细节**：

- 仅在 LCD 显示屏设备上启用（通过 `dynamic_cast<LcdDisplay*>` 检查）
- 使用 `Application` 单例管理预览状态
- 错误处理：摄像头忙碌时抛出 `std::runtime_error` 异常

**备注**：

- 该功能是智能门锁人脸识别系统的重要组成部分
- 与 `local-camera-preview` 规范相关联

---

## 2025-05-10

### 修改文件：`main/mcp_server.cc`

**修改位置**：文件头部 include 区域

**修改时间**：2025-05-10

**变更内容**：

- 新增头文件引用：`#include "lcd_display.h"`
- 代码格式优化：删除多余空行

**实现功能**：

1. **LCD 显示支持增强**：
   - 为 MCP 服务器模块添加 LCD 显示器的直接访问能力
   - 使得 MCP 协议处理过程中可以调用 LCD 显示相关接口
   - 可能用于显示 MCP 连接状态、调试信息或协议交互提示

2. **代码质量提升**：
   - 清理冗余空行，提高代码可读性
   - 保持头文件引用的整洁性

**技术细节**：

- 引入 `lcd_display.h` 使得可以使用 `LcdDisplay` 类的功能
- 配合已有的 `lvgl_display.h` 和 `oled_display.h`，形成完整的显示抽象层支持

**备注**：

- 此修改为后续 MCP 服务器在 LCD 屏幕上显示状态信息提供基础
- 与显示子系统的集成更加紧密

---

## 2025-05-10

### 修改文件：`main/protocols/protocol.cc`

**修改位置**：`Protocol::IsTimeout()` 函数

**修改时间**：2025-05-10

**变更内容**：

- 禁用了原有的 120 秒超时断开逻辑
- 函数现在直接返回 `false`，不再检测超时
- 原超时检测代码已注释保留（包括时间计算和日志输出）

**实现功能**：

1. **连接持久化**：
   - WebSocket 连接将保持活跃状态，直到主动关闭
   - 防止因长时间无数据传输而自动断开连接
   - 提升待机状态下的连接稳定性

2. **适用场景**：
   - 智能门锁系统需要保持长期在线，随时响应门铃事件
   - 避免因超时断开导致的人脸识别延迟或失败
   - 减少频繁重连带来的网络开销和状态同步问题

**技术细节**：

- 原逻辑使用 `std::chrono::steady_clock` 计算距离上次接收数据的时间
- 超时阈值为 120 秒（2 分钟）
- 超时时会记录 `ESP_LOGE` 日志

**备注**：

- 如需恢复超时功能，取消注释原代码并删除 `return false;` 即可
- 建议配合服务器端心跳机制使用，确保连接有效性
- 此修改可能影响异常连接的清理，需在服务器端实现连接健康检查

---

## 2025-05-10

### 修改文件：`main/application.h`

**修改位置**：事件定义区域（`MAIN_EVENT_*` 宏定义部分）

**修改时间**：2025-05-10

**变更内容**：

- 新增事件宏定义：`MAIN_EVENT_AUTO_CONNECT_SERVER (1 << 7)`
- 代码格式优化：对齐 `MAIN_EVENT_CLOCK_TICK` 的注释

**实现功能**：

1. **自动连接服务器事件支持**：
   - 定义了新的应用层事件类型，用于触发自动连接服务器的逻辑
   - 事件位为第 7 位，与其他事件互不冲突
   - 可通过 FreeRTOS 事件组机制触发和监听

2. **适用场景**：
   - 应用启动时自动连接到 WebSocket 服务器
   - 网络连接恢复后自动重连服务器
   - 定时检查连接状态并触发重连逻辑
   - 配合 `Protocol` 层实现连接持久化和自动恢复

3. **代码质量提升**：
   - 统一事件定义的注释格式，提高代码可读性
   - 为后续实现自动重连机制提供事件基础

**技术细节**：

- 使用位掩码方式定义事件（`1 << 7`），可与其他事件组合使用
- 配合 `xEventGroupSetBits()` 和 `xEventGroupWaitBits()` 使用
- 事件将在 `Application::Run()` 主循环中被处理

**备注**：

- 此事件定义为后续实现自动重连功能奠定基础
- 需要在 `application.cc` 中实现对应的事件处理逻辑
- 与智能门锁系统的长期在线需求相关联

---

## 2025-05-10

### 修改文件：`main/application.cc`

**修改位置**：`Application::Run()` 函数的事件处理循环

**修改时间**：2025-05-10

**变更内容**：

- 从事件等待掩码中移除 `MAIN_EVENT_AUTO_CONNECT_SERVER` 事件
- 删除整个自动连接服务器的事件处理代码块（约 48 行）
- 移除的逻辑包括：
  - 自动连接重试机制
  - 指数退避延迟策略（初始 5 秒，最大 60 秒）
  - 连接状态显示更新
  - 最大重试次数限制（3 次）
  - 重试计数器和延迟时间管理

**实现功能**：

1. **移除自动重连功能**：
   - 应用程序不再自动尝试重新连接服务器
   - 简化了主事件循环的逻辑
   - 减少了后台自动重连带来的资源消耗

2. **影响范围**：
   - 网络断开后不会自动重连，需要手动触发或重启设备
   - 移除了重连过程中的 UI 状态提示（"连接服务器 (x/3)"）
   - 移除了重连失败后的错误提示（"无法连接服务器，请检查网络"）

3. **可能原因**：
   - 与之前禁用的 120 秒超时断开逻辑配合，简化连接管理策略
   - 可能改为由服务器端或其他机制主动管理连接状态
   - 减少设备端的自动重连逻辑，避免频繁重连带来的问题

**技术细节**：

- 原逻辑使用 `auto_connect_retry_count_` 和 `auto_connect_retry_delay_` 成员变量
- 指数退避算法：每次失败后延迟时间翻倍（5s → 10s → 20s，最大 60s）
- 最大重试 3 次后禁用自动连接（`auto_connect_enabled_ = false`）
- 连接过程中会切换设备状态：`kDeviceStateIdle` → `kDeviceStateConnecting` → `kDeviceStateIdle`

**备注**：

- 此修改与之前的 `Protocol::IsTimeout()` 禁用超时断开逻辑相呼应
- 建议检查是否有其他连接管理机制替代此功能
- 如需恢复自动重连，需同时恢复事件掩码和处理逻辑
- 可能需要更新相关的成员变量定义（如果不再使用可以清理）

---

## 2025-05-10

### 修改文件：`main/application.h`

**修改位置**：`Application` 类的私有成员变量区域（类定义末尾）

**修改时间**：2025-05-10

**变更内容**：

- 新增自动连接相关成员变量区块（共 5 个成员变量 + 3 个常量）：
  - `auto_connect_task_handle_`：自动连接任务句柄
  - `auto_connect_enabled_`：自动连接启用标志（默认 true）
  - `user_manually_disconnected_`：用户主动断开标志（默认 false）
  - `connection_retry_count_`：连接重试计数器（默认 0）
  - `INITIAL_RETRY_DELAY_MS`：初始重试延迟常量（1000ms = 1秒）
  - `MAX_RETRY_DELAY_MS`：最大重试延迟常量（60000ms = 60秒）
  - `MAX_RETRY_COUNT`：最大重试次数常量（-1 表示无限重试）

**实现功能**：

1. **自动重连机制基础设施**：
   - 为应用程序添加完整的自动重连状态管理能力
   - 支持独立的后台任务处理连接重试逻辑
   - 区分用户主动断开和异常断开场景

2. **指数退避策略支持**：
   - 初始重试延迟 1 秒，避免立即重连造成服务器压力
   - 最大延迟 60 秒，防止过长等待影响用户体验
   - 无限重试策略（`MAX_RETRY_COUNT = -1`），确保设备长期在线

3. **智能重连控制**：
   - `auto_connect_enabled_` 标志允许动态启用/禁用自动重连
   - `user_manually_disconnected_` 标志防止用户主动断开后自动重连
   - `connection_retry_count_` 记录重试次数，支持统计和日志记录

4. **适用场景**：
   - 智能门锁系统需要保持长期在线，随时响应门铃事件
   - 网络波动或服务器重启后自动恢复连接
   - 避免因临时网络故障导致的服务中断

**技术细节**：

- 使用 `TaskHandle_t` 管理独立的重连任务，避免阻塞主循环
- 常量使用 `static constexpr` 定义，编译时确定，节省内存
- 延迟时间单位为毫秒（ms），配合 FreeRTOS 的 `vTaskDelay()` 使用
- 重试计数器为 `int` 类型，支持负值表示特殊含义（无限重试）

**备注**：

- 此修改为后续实现完整的自动重连功能提供数据结构基础
- 需要在 `application.cc` 中实现对应的任务创建和重连逻辑
- 与之前移除的事件驱动重连机制不同，采用独立任务方式实现
- 配合 `Protocol::IsTimeout()` 禁用超时断开，形成完整的连接持久化方案

---

## 2025-05-10

### 修改文件：`main/application.cc`

**修改位置**：文件末尾新增自动连接功能实现区块（第 3070-3236 行）

**修改时间**：2025-05-10

**变更内容**：

- 新增三个成员函数实现（共约 170 行代码）：
  1. `Application::AutoConnectLoop()`：自动连接任务主循环
  2. `Application::ShouldAutoConnect()`：自动连接条件判断
  3. `Application::CalculateRetryDelay()`：指数退避延迟计算

**实现功能**：

1. **自动连接任务循环（`AutoConnectLoop()`）**：
   - 后台持续运行的独立任务，负责服务器连接管理
   - 启动时等待 5 秒确保系统初始化完成
   - 循环检查是否需要连接，满足条件时尝试建立连接
   - 使用 `Schedule()` 在主线程中执行连接操作，确保线程安全
   - 连接成功后监控连接状态，每 5 秒检查一次
   - 连接断开后等待 2 秒自动重连
   - 连接失败时使用指数退避策略，避免频繁重试

2. **自动连接条件判断（`ShouldAutoConnect()`）**：
   - 多重条件检查，确保只在合适的时机自动连接：
     - 自动连接功能已启用（`auto_connect_enabled_`）
     - Protocol 对象已初始化
     - 当前未连接服务器
     - 用户未主动断开连接（`user_manually_disconnected_`）
     - 设备处于空闲状态（`kDeviceStateIdle`）
     - 网络已连接
     - 未处于监控模式
   - 所有条件满足时返回 `true`，触发自动连接

3. **指数退避延迟计算（`CalculateRetryDelay()`）**：
   - 实现标准的指数退避算法：1s → 2s → 4s → 8s → 16s → 32s → 60s（最大）
   - 添加 ±20% 的随机抖动（jitter），避免多设备同时重连造成服务器压力
   - 使用 `esp_random()` 生成随机数，确保抖动的随机性
   - 限制最小延迟为 1 秒，最大延迟为 60 秒（`MAX_RETRY_DELAY_MS`）
   - 返回最终计算的延迟时间（毫秒）

4. **智能重连策略**：
   - 连接失败后递增重试计数器（`connection_retry_count_`）
   - 延迟期间分段检查状态（每秒一次），及时响应状态变化
   - 检测到用户主动断开或状态不适合连接时，提前退出延迟
   - 连接成功后重置重试计数器和手动断开标志

5. **适用场景**：
   - 智能门锁系统需要保持长期在线，随时响应门铃和人脸识别请求
   - 网络波动或服务器重启后自动恢复连接
   - 避免因临时网络故障导致的服务中断
   - 减少用户手动重连的操作负担

**技术细节**：

- 使用 FreeRTOS 任务机制（`vTaskDelay()`）实现非阻塞延迟
- 连接操作通过 `Schedule()` 调度到主线程，避免多线程竞争
- 连接超时设置为 15 秒（30 次 × 500ms 检查）
- 日志级别：
  - `ESP_LOGI`：正常流程（启动、连接成功、状态变化）
  - `ESP_LOGW`：警告信息（连接断开、重试延迟）
  - `ESP_LOGE`：错误信息（连接失败）
- 延迟分段检查机制确保及时响应状态变化，避免长时间阻塞

**备注**：

- 此实现完成了之前在 `application.h` 中定义的自动连接成员变量的功能闭环
- 与之前移除的事件驱动重连机制不同，采用独立后台任务方式实现
- 配合 `Protocol::IsTimeout()` 禁用超时断开，形成完整的连接持久化方案
- 需要在 `Application::Initialize()` 中创建并启动 `AutoConnectLoop()` 任务
- 指数退避 + 随机抖动策略符合网络重连的最佳实践
- 无限重试策略（`MAX_RETRY_COUNT = -1`）确保设备长期在线能力

---

## 2025-05-10

### 修改文件：`main/assets/lang_config.h`

**修改位置**：文件头部（第 5 行）

**修改时间**：2025-05-10

**变更内容**：

- 新增头文件引用：`#include <string_view>`
- 该文件为自动生成的语言配置文件，包含中文（zh-CN）字符串资源和音效资源定义

**实现功能**：

1. **修复编译错误**：
   - 解决 `std::string_view` 类型未声明的编译错误
   - 修复 `size_t` 类型未定义的问题（`<string_view>` 间接包含了 `<cstddef>`）
   - 使得音效资源的 `std::string_view` 定义能够正常编译

2. **音效资源管理**：
   - 文件中定义了大量音效资源（OGG 格式），使用 `std::string_view` 包装二进制数据
   - 包括门锁相关音效：
     - `OGG_AUTH_FAIL_PREFIX/SUFFIX`：认证失败提示音
     - `OGG_DOOR_NOT_CLOSED`：门未关闭提示音
     - `OGG_ENROLL_SUCCESS/FAIL`：录入成功/失败提示音
     - `OGG_LOCKED_PREFIX/SUFFIX`：上锁提示音
     - `OGG_TAMPER_ALERT`：防撬报警音
     - `OGG_WELCOME`：欢迎音
   - 以及通用音效：数字 0-9、激活、成功、错误、升级等

3. **字符串资源定义**：
   - 包含完整的中文字符串资源（`Lang::Strings` 命名空间）
   - 涵盖网络连接、电池状态、系统升级、错误提示等各类文本
   - 支持格式化字符串（如 `CHECK_NEW_VERSION_FAILED` 包含 `%d` 和 `%s` 占位符）

**技术细节**：

- 使用 `asm()` 内联汇编语法引用嵌入式二进制资源（`_binary_*_start/end`）
- `std::string_view` 提供零拷贝的字符串视图，适合嵌入式资源管理
- 文件头部定义了 `zh_cn` 宏和 `Lang::CODE = "zh-CN"` 语言标识
- 所有资源定义在 `Lang` 命名空间下，分为 `Strings` 和 `Sounds` 两个子命名空间

**备注**：

- 此文件为自动生成文件（注释标注 "Auto-generated language config"）
- 修改应通过语言资源生成工具进行，避免手动编辑
- 与智能门锁系统的多语言支持和音效反馈功能相关
- 修复后的文件应能正常编译，解决之前的 20+ 个编译错误

---

## 2025-05-10

### 新增文件：`main/auto_connect_impl.cc`

**文件位置**：`main/` 目录下的独立实现文件

**修改时间**：2025-05-10

**变更内容**：

- 新增独立的自动连接功能实现文件（约 170 行代码）
- 包含三个 `Application` 类成员函数的实现：
  1. `AutoConnectLoop()`：自动连接任务主循环
  2. `ShouldAutoConnect()`：自动连接条件判断
  3. `CalculateRetryDelay()`：指数退避延迟计算

**实现功能**：

1. **模块化代码组织**：
   - 将自动连接相关功能从 `application.cc` 中分离出来
   - 提高代码可维护性和可读性
   - 便于独立测试和调试自动连接逻辑

2. **自动连接任务循环（`AutoConnectLoop()`）**：
   - 后台持续运行的独立 FreeRTOS 任务
   - 启动时等待 5 秒确保系统初始化完成
   - 循环检查连接条件，满足时尝试建立服务器连接
   - 使用 `Schedule()` 在主线程中执行连接操作，确保线程安全
   - 连接成功后监控连接状态（每 5 秒检查一次）
   - 连接断开后等待 2 秒自动重连
   - 连接失败时使用指数退避策略，避免频繁重试
   - 显示连接状态 UI 提示（连接中、成功、失败、重试倒计时）
   - 播放成功音效（`Lang::Sounds::OGG_SUCCESS`）

3. **智能连接条件判断（`ShouldAutoConnect()`）**：
   - 多重条件检查，确保只在合适的时机自动连接：
     - 自动连接功能已启用（`auto_connect_enabled_`）
     - Protocol 对象已初始化
     - 当前未连接服务器
     - 用户未主动断开连接（`user_manually_disconnected_`）
     - 设备处于空闲状态（`kDeviceStateIdle`）
     - 网络已连接
     - 未处于监控模式
   - 所有条件满足时返回 `true`，触发自动连接

4. **指数退避延迟计算（`CalculateRetryDelay()`）**：
   - 实现标准的指数退避算法：1s → 2s → 4s → 8s → 16s → 32s → 60s（最大）
   - 添加 ±20% 的随机抖动（jitter），避免多设备同时重连造成服务器压力
   - 使用 `esp_random()` 生成随机数，确保抖动的随机性
   - 限制最小延迟为 1 秒，最大延迟为 60 秒（`MAX_RETRY_DELAY_MS`）
   - 返回最终计算的延迟时间（毫秒）

5. **智能重连策略**：
   - 连接失败后递增重试计数器（`connection_retry_count_`）
   - 延迟期间分段检查状态（每秒一次），及时响应状态变化
   - 检测到用户主动断开或状态不适合连接时，提前退出延迟
   - 连接成功后重置重试计数器和手动断开标志

6. **适用场景**：
   - 智能门锁系统需要保持长期在线，随时响应门铃和人脸识别请求
   - 网络波动或服务器重启后自动恢复连接
   - 避免因临时网络故障导致的服务中断
   - 减少用户手动重连的操作负担

**技术细节**：

- 使用 FreeRTOS 任务机制（`vTaskDelay()`）实现非阻塞延迟
- 连接操作通过 `Schedule()` 调度到主线程，避免多线程竞争
- 连接超时设置为 15 秒（30 次 × 500ms 检查）
- 日志级别：
  - `ESP_LOGI`：正常流程（启动、连接成功、状态变化）
  - `ESP_LOGW`：警告信息（连接断开、重试延迟）
  - `ESP_LOGE`：错误信息（连接失败）
- 延迟分段检查机制确保及时响应状态变化，避免长时间阻塞
- 显示通知使用 `Lang::Strings` 中的本地化字符串资源

**编译问题**：

- 当前文件存在编译错误：`Use of undeclared identifier 'Application'`
- 原因：缺少 `application.h` 头文件引用
- 解决方案：需要在文件开头添加 `#include "application.h"`

**备注**：

- 此文件实现了之前在 `application.h` 中定义的自动连接成员变量的功能闭环
- 需要在 `application.cc` 的 `Application::Start()` 中创建并启动 `AutoConnectLoop()` 任务
- 配合 `Protocol::IsTimeout()` 禁用超时断开，形成完整的连接持久化方案
- 指数退避 + 随机抖动策略符合网络重连的最佳实践
- 无限重试策略（`MAX_RETRY_COUNT = -1`）确保设备长期在线能力
- 模块化设计便于后续功能扩展和维护

---

## 2025-05-10

### 修改文件：`main/display/lcd_display.h`

**修改位置**：`LcdDisplay` 类的私有成员变量区域（预览模式相关变量之后）

**修改时间**：2025-05-10 23:45

**变更内容**：

- 新增门锁模式相关成员变量：
  - `bool ui_hidden_`：UI 隐藏状态标志（默认 false）
- 添加注释说明该变量用于门锁模式

**实现功能**：

1. **门锁模式 UI 控制基础设施**：
   - 为 LCD 显示类添加 UI 隐藏状态标志
   - 用于标记当前是否处于门锁模式下的 UI 隐藏状态
   - 为后续实现门锁模式时隐藏常规 UI 界面提供状态管理能力

2. **适用场景**：
   - 门锁模式激活时，隐藏常规的聊天界面、状态栏等 UI 元素
   - 仅显示门锁相关的专用界面（如人脸识别提示、开锁状态等）
   - 门锁模式退出时，恢复常规 UI 显示

3. **设计考虑**：
   - 与现有的 `preview_mode_active_` 标志并列，形成多种显示模式的状态管理
   - 布尔标志简单高效，适合快速状态检查
   - 为后续实现 `HideUI()` 和 `ShowUI()` 等方法提供状态存储

**技术细节**：

- 成员变量类型：`bool`
- 默认值：`false`（未隐藏）
- 访问权限：`private`（通过公共方法访问和修改）
- 位置：紧随预览模式相关变量之后，保持代码组织的逻辑性

**备注**：

- 此修改为智能门锁系统的 UI 交互提供基础设施
- 需要在 `lcd_display.cc` 中实现对应的 UI 隐藏/显示逻辑
- 与门锁模式的状态机管理相配合
- 后续可能需要添加更多门锁模式相关的 UI 元素管理变量

---

## 2025-05-10

### 修改文件：`main/display/lcd_display.cc`

**修改位置**：文件末尾新增门锁模式实现区块（第 1478-1580 行）

**修改时间**：2025-05-10 23:50

**变更内容**：

- 新增三个公共成员函数实现（共约 106 行代码）：
  1. `LcdDisplay::HideAllUI()`：隐藏所有 UI 组件
  2. `LcdDisplay::ShowAllUI()`：恢复所有 UI 组件显示
  3. `LcdDisplay::IsUIHidden()`：查询 UI 隐藏状态

**实现功能**：

1. **隐藏所有 UI（`HideAllUI()`）**：
   - 使用 `DisplayLockGuard` 确保线程安全的 UI 操作
   - 检查 `ui_hidden_` 标志，避免重复隐藏
   - 隐藏所有 LVGL UI 组件：
     - 顶部栏（`top_bar_`）
     - 状态栏（`status_bar_`）
     - 内容区域（`content_`）
     - 底部栏（`bottom_bar_`）
     - 表情框（`emoji_box_`、`emoji_label_`、`emoji_image_`）
     - 预览图像（`preview_image_`）
     - 容器（`container_`）
     - 侧边栏（`side_bar_`）
   - 将屏幕背景色设置为黑色（`0x000000`）
   - 设置 `ui_hidden_` 标志为 `true`
   - 记录日志：`ESP_LOGI` 和 `ESP_LOGD`

2. **恢复所有 UI（`ShowAllUI()`）**：
   - 使用 `DisplayLockGuard` 确保线程安全
   - 检查 `ui_hidden_` 标志，避免重复恢复
   - 仅在非预览模式下恢复 UI 组件（`!preview_mode_active_`）
   - 恢复所有 UI 组件的显示（移除 `LV_OBJ_FLAG_HIDDEN` 标志）
   - 从当前主题（`LvglTheme`）恢复屏幕背景色
   - 设置 `ui_hidden_` 标志为 `false`
   - 记录日志：`ESP_LOGI` 和 `ESP_LOGD`

3. **查询 UI 隐藏状态（`IsUIHidden()`）**：
   - 简单的 getter 方法，返回 `ui_hidden_` 标志
   - 用于外部模块查询当前 UI 是否处于隐藏状态

4. **适用场景**：
   - 智能门锁场景：人脸识别时隐藏常规 UI，仅显示识别提示
   - 监控模式：全屏显示摄像头画面，隐藏聊天界面
   - 省电模式：长时间无操作时隐藏 UI，降低功耗
   - 专注模式：特定任务执行时隐藏干扰元素

5. **设计考虑**：
   - 与预览模式互斥：恢复 UI 时检查 `preview_mode_active_` 标志
   - 线程安全：使用 `DisplayLockGuard` 保护 LVGL 操作
   - 空指针检查：所有 UI 组件操作前检查 `!= nullptr`
   - 主题兼容：恢复背景色时使用当前主题的配置
   - 日志分级：正常流程使用 `ESP_LOGI`，重复操作使用 `ESP_LOGD`

**技术细节**：

- 使用 LVGL 的 `lv_obj_add_flag()` 和 `lv_obj_remove_flag()` 控制组件可见性
- 标志类型：`LV_OBJ_FLAG_HIDDEN`
- 背景色设置：`lv_obj_set_style_bg_color(screen, color, 0)`
- 主题类型转换：`static_cast<LvglTheme *>(current_theme_)`
- 日志标签：`TAG`（定义在文件开头）

**备注**：

- 此实现完成了之前在 `lcd_display.h` 中定义的 `ui_hidden_` 成员变量的功能闭环
- 需要在 `application.cc` 中调用这些方法来实现门锁模式的 UI 切换
- 与门锁模式的状态机管理相配合
- 恢复 UI 时考虑了预览模式的互斥关系，避免状态冲突
- 为智能门锁系统的用户体验优化提供基础设施

---

## 2025-05-10

### 修改文件：`main/display/lcd_display.cc`

**修改位置**：`LcdDisplay::HideAllUI()` 函数（第 1495-1530 行）

**修改时间**：2025-05-10 23:55

**变更内容**：

- 优化 UI 组件隐藏逻辑，利用 LVGL 父子关系特性：
  - 隐藏 `container_` 会自动隐藏其所有子对象（`top_bar_`、`content_` 等）
  - 移除了对 `top_bar_` 和 `content_` 的冗余单独隐藏操作
- 新增对独立组件的隐藏：
  - `side_bar_`：侧边栏组件
  - `low_battery_popup_`：低电量弹窗
- 代码结构优化：
  - 添加注释说明 `container_` 的自动隐藏特性
  - 将独立组件的隐藏操作分组，提高代码可读性
  - 调整代码顺序，先隐藏主容器，再隐藏独立组件

**实现功能**：

1. **减少冗余操作**：
   - 利用 LVGL 的对象树结构，隐藏父对象时自动隐藏子对象
   - 避免重复调用 `lv_obj_add_flag()`，提高执行效率
   - 减少不必要的 UI 操作，降低 CPU 占用

2. **完善组件覆盖**：
   - 确保所有独立于 `container_` 的 UI 组件都被隐藏
   - `side_bar_` 和 `low_battery_popup_` 可能是独立创建的顶层对象
   - 避免门锁模式下残留 UI 元素影响用户体验

3. **代码可维护性提升**：
   - 清晰的注释说明设计意图
   - 逻辑分组使代码结构更清晰
   - 便于后续添加新的独立 UI 组件

**技术细节**：

- LVGL 对象树特性：父对象隐藏时，所有子对象自动继承隐藏状态
- `container_` 的子对象包括：`top_bar_`、`content_`、`bottom_bar_` 等
- 独立组件需要单独处理：`status_bar_`、`side_bar_`、`emoji_box_`、`preview_image_`、`low_battery_popup_`
- 使用 `lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN)` 隐藏对象

**备注**：

- 此优化提高了门锁模式 UI 切换的性能和可靠性
- 确保所有 UI 元素在门锁模式下都被正确隐藏
- 与 `ShowAllUI()` 函数配合，实现完整的 UI 显示/隐藏功能
- 为智能门锁系统的用户体验优化提供更好的基础

---

## 2025-05-11

### 修改文件：`main/display/lcd_display.cc`

**修改位置**：`LcdDisplay::EnterPreviewMode()` 函数中的 UI 隐藏逻辑（第 1264-1295 行）

**修改时间**：2025-05-11

**变更内容**：

- 优化进入预览模式时的 UI 隐藏逻辑：
  - 将原来逐个隐藏 `top_bar_`、`content_` 的方式改为直接隐藏 `container_`
  - 利用 LVGL 父子关系特性：隐藏 `container_` 会自动隐藏其所有子对象
  - 移除了对 `top_bar_` 和 `content_` 的冗余单独隐藏操作
- 新增对独立组件的隐藏：
  - `side_bar_`：侧边栏组件
  - `low_battery_popup_`：低电量弹窗
- 代码结构优化：
  - 添加注释说明 `container_` 的自动隐藏特性
  - 将独立组件的隐藏操作分组，提高代码可读性

**实现功能**：

1. **减少冗余操作**：
   - 利用 LVGL 的对象树结构，隐藏父对象时自动隐藏子对象
   - 避免重复调用 `lv_obj_add_flag()`，提高执行效率
   - 减少不必要的 UI 操作，降低 CPU 占用

2. **完善组件覆盖**：
   - 确保所有独立于 `container_` 的 UI 组件都被隐藏
   - `side_bar_` 和 `low_battery_popup_` 可能是独立创建的顶层对象
   - 避免预览模式下残留 UI 元素影响摄像头画面显示

3. **修复潜在 Bug**：
   - 之前可能遗漏了 `side_bar_` 和 `low_battery_popup_` 的隐藏
   - 这些组件可能在预览模式下仍然可见，影响用户体验
   - 现在确保所有 UI 元素在预览模式下都被正确隐藏

4. **代码可维护性提升**：
   - 清晰的注释说明设计意图
   - 逻辑分组使代码结构更清晰
   - 便于后续添加新的独立 UI 组件

**技术细节**：

- LVGL 对象树特性：父对象隐藏时，所有子对象自动继承隐藏状态
- `container_` 的子对象包括：`top_bar_`、`content_`、`bottom_bar_` 等
- 独立组件需要单独处理：`status_bar_`、`bottom_bar_`、`preview_image_`、`side_bar_`、`low_battery_popup_`
- 使用 `lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN)` 隐藏对象

**备注**：

- 此优化提高了预览模式 UI 切换的性能和可靠性
- 确保所有 UI 元素在预览模式下都被正确隐藏
- 与 `ExitPreviewMode()` 函数配合，实现完整的预览模式 UI 管理
- 为智能门锁系统的本地摄像头预览功能提供更好的用户体验
- 与之前的 `HideAllUI()` 函数优化思路一致，保持代码风格统一

---

## 2025-05-11

### 修改文件：`main/audio/audio_service.cc`

**修改位置**：`AudioService` 构造函数末尾（`esp_timer_create` 之后，`Start()` 之前）

**修改时间**：2025-05-11

**变更内容**：

- 新增音频输出通路预初始化逻辑（约 18 行代码）：
  - 检查当前 OPUS 解码器的采样率和帧时长是否与本地音效参数匹配（16000Hz）
  - 若不匹配，重建 `OpusDecoderWrapper` 实例（16000Hz, 单声道, OPUS_FRAME_DURATION_MS）
  - 检查解码器采样率与 codec 输出采样率是否一致
  - 若不一致，预配置 `output_resampler_`（从 16000Hz 重采样到 codec 输出采样率）

**实现功能**：

1. **修复首次播放音效丢失问题**：
   - 本地音效（如开机提示音、连接成功音等）采样率为 16000Hz
   - 若 codec 输出采样率不同（如 48000Hz），首次 `PlaySound` 时需要重建解码器和配置重采样器
   - 重建过程有延迟，导致第一帧音频数据丢失，用户感知为"首次音效缺失"
   - 预初始化在构造阶段完成解码器和重采样器配置，避免运行时重建延迟

2. **提升音频播放响应速度**：
   - 解码器和重采样器在系统启动时即准备就绪
   - 首次播放本地音效时无需等待初始化，立即输出音频
   - 改善用户体验，尤其是开机后第一个音效的完整性

3. **适用场景**：
   - 系统开机后首次播放提示音（如"连接成功"音效）
   - 门锁模式下的人脸识别结果音效反馈
   - 任何首次触发的本地 OGG 音效播放

**技术细节**：

- 本地音效采样率：16000Hz（`local_sound_sample_rate`）
- 帧时长：`OPUS_FRAME_DURATION_MS`（通常 20ms 或 60ms）
- 解码器：`OpusDecoderWrapper`（单声道）
- 重采样器：`output_resampler_.Configure(src_rate, dst_rate)`
- 条件判断避免不必要的重建（如果已经匹配则跳过）

**备注**：

- 此修改解决了用户反馈的"开机后第一个音效听不到"的问题
- 不影响后续通过 WebSocket 接收的远程音频播放逻辑
- 远程音频可能使用不同的采样率，届时解码器会按需重建
- 预初始化的开销极小（仅一次解码器创建和重采样器配置）

---

## 2025-05-18

### 修改文件：`main/display/lcd_display.h`

**修改位置**：`LcdDisplay` 类的 public 成员函数声明区域

**修改时间**：2025-05-18

**变更内容**：

- 新增 3 个虚函数 override 声明：
  1. `virtual void SetStatus(const char *status) override;`
  2. `virtual void ShowNotification(const char *notification, int duration_ms = 3000) override;`
  3. `virtual void UpdateStatusBar(bool update_all = false) override;`

**实现功能**：

1. **状态文本显示（`SetStatus`）**：
   - 覆盖基类 `LvglDisplay` 的 `SetStatus` 虚函数
   - 允许 `LcdDisplay` 提供自定义的状态文本显示实现
   - 用于在 LCD 屏幕上显示当前设备状态（如"连接中"、"已连接"等）

2. **通知消息显示（`ShowNotification`）**：
   - 覆盖基类的 `ShowNotification` 虚函数
   - 支持在 LCD 屏幕上显示临时通知消息
   - 默认持续时间 3000ms（3 秒），可自定义
   - 适用于门锁事件提示、连接状态变化等场景

3. **状态栏更新（`UpdateStatusBar`）**：
   - 覆盖基类的 `UpdateStatusBar` 虚函数
   - 支持刷新状态栏信息（WiFi 信号、电池电量、时间等）
   - `update_all` 参数控制是否全量更新所有状态栏元素

**技术细节**：

- 使用 `virtual ... override` 确保正确覆盖基类虚函数
- `ShowNotification` 的 `duration_ms` 参数提供默认值 3000ms
- `UpdateStatusBar` 的 `update_all` 参数提供默认值 false（增量更新）

**备注**：

- 此修改补充了 `LcdDisplay` 类对基类接口的完整覆盖
- 需要在 `lcd_display.cc` 中提供对应的函数实现
- 与自动连接功能配合，用于显示连接状态和通知消息

---

## 2025-05-18

### 修改文件：`main/application.cc`

**修改位置**：`Application::AutoConnectLoop()` 函数中，连接成功后的监控循环之前（约第 3130 行）

**修改时间**：2025-05-18

**变更内容**：

- 在自动连接成功后、进入连接状态监控循环之前，新增门锁模式 UI 隐藏逻辑（约 8 行代码）：
  - 通过 `dynamic_cast<LcdDisplay *>` 获取 LCD 显示器实例
  - 调用 `lcd_display->HideAllUI()` 隐藏所有 UI 组件
  - 记录日志："已切换到门锁模式（UI 隐藏）"

**实现功能**：

1. **自动连接成功后自动进入门锁模式**：
   - 设备成功连接服务器后，自动隐藏所有常规 UI 界面
   - 屏幕变为纯黑色，进入门锁待机状态
   - 无需用户手动操作，实现无人值守的门锁模式

2. **适用场景**：
   - 智能门锁系统上电后自动连接服务器并进入待机状态
   - 网络恢复重连后自动恢复门锁模式
   - 设备作为门锁使用时，不需要显示聊天界面等常规 UI

3. **用户体验优化**：
   - 门锁设备无需显示屏交互，隐藏 UI 降低功耗
   - 纯黑屏幕减少对门口环境的光污染
   - 保持设备低调运行，符合门锁产品形态

**技术细节**：

- 使用 `dynamic_cast` 安全转换，仅在 LCD 显示器存在时执行
- 调用已实现的 `HideAllUI()` 方法，复用门锁模式 UI 隐藏逻辑
- 位于连接成功日志之后、状态监控循环之前，确保时序正确
- 使用 `ESP_LOGI` 记录模式切换日志，便于调试

**备注**：

- 此修改将门锁模式 UI 隐藏与自动连接功能绑定
- 依赖之前实现的 `LcdDisplay::HideAllUI()` 方法
- 如需在连接断开后恢复 UI，需在断开处理逻辑中调用 `ShowAllUI()`
- 与智能门锁系统的无人值守运行模式相关


---

## 2025-05-19

### 修改文件：`main/video/video_stream_service.cc`

**修改位置**：`VideoStreamService` 的视频流捕获任务循环中（帧捕获、编码、入队流程）

**修改时间**：2025-05-19

**变更内容**：

- 在视频流捕获循环中新增性能计时代码（约 12 行）：
  - 在 `CaptureForStream()` 调用前后记录时间戳（`t_capture_start`、`t_capture_done`）
  - 在 `CaptureJpeg()` 编码完成后记录时间戳（`t_encode_done`）
  - 在帧数据拷贝和内存释放完成后记录时间戳（`t_copy_done`）
  - 使用 `ESP_LOGI` 输出性能日志，包含各阶段耗时、帧大小和分辨率信息

**实现功能**：

1. **视频流性能监控**：
   - 实时输出每帧的捕获耗时、JPEG 编码耗时、数据拷贝耗时
   - 记录每帧的 JPEG 数据大小（bytes）和分辨率（width×height）
   - 便于定位视频流卡顿或延迟的瓶颈环节

2. **性能分析与调优**：
   - 通过日志数据可以判断是摄像头捕获慢、JPEG 编码慢还是内存拷贝慢
   - 帧大小信息有助于评估 JPEG 压缩质量和网络带宽需求
   - 分辨率信息确认当前视频流的实际输出参数

3. **适用场景**：
   - 调试视频流帧率不达标的问题
   - 评估不同分辨率/质量设置下的性能表现
   - 监控长时间运行时的性能稳定性

**技术细节**：

- 使用 `xTaskGetTickCount()` 获取 FreeRTOS tick 计数
- 通过 `portTICK_PERIOD_MS` 转换为毫秒
- 日志格式：`[性能] 捕获=%dms, 编码=%dms, 拷贝=%dms, 帧大小=%zu bytes, 分辨率=%dx%d`
- 计时点覆盖完整的帧处理流水线（捕获→编码→拷贝）

**备注**：

- 此为调试/性能分析用途的日志代码
- 生产环境中可考虑降低日志级别为 `ESP_LOGD` 或通过宏开关控制
- 不影响视频流的正常功能，仅增加少量日志输出开销


---

## 2025-05-19

### 修改文件：`main/application.cc`

**修改位置**：`protocol_->OnAudioChannelOpened()` 回调函数内部（采样率检查逻辑之后，回调闭合之前）

**修改时间**：2025-05-19

**变更内容**：

- 在音频通道成功打开的回调中新增门锁模式 UI 隐藏逻辑（约 6 行代码）：
  - 通过 `Board::GetInstance().GetDisplay()` 获取显示器实例
  - 使用 `dynamic_cast<LcdDisplay *>` 安全转换为 LCD 显示器
  - 调用 `lcd_display->HideAllUI()` 隐藏所有 UI 组件
  - 记录日志："已切换到门锁模式（UI 隐藏）"

**实现功能**：

1. **音频通道建立后自动进入门锁模式**：
   - 当设备与服务器成功建立 WebSocket 音频通道后，自动隐藏所有常规 UI
   - 屏幕变为纯黑色，进入门锁待机状态
   - 补充了 `OnAudioChannelOpened` 回调中的门锁模式切换逻辑

2. **与自动连接功能配合**：
   - 之前在 `AutoConnectLoop()` 中已有连接成功后隐藏 UI 的逻辑
   - 此处补充了通过正常流程（非自动连接）建立音频通道时的 UI 隐藏
   - 确保无论通过哪种方式连接服务器，都能正确进入门锁模式

3. **适用场景**：
   - 用户手动触发连接服务器后自动进入门锁待机
   - 语音唤醒后建立音频通道时隐藏 UI
   - 确保门锁设备在任何连接场景下都保持纯黑屏幕

**技术细节**：

- 使用 `dynamic_cast` 安全转换，仅在 LCD 显示器存在时执行
- 复用已实现的 `HideAllUI()` 方法
- 位于采样率不匹配警告日志之后，`OnAudioChannelClosed` 回调之前
- 使用 `ESP_LOGI` 记录模式切换日志

**备注**：

- 此修改确保所有连接路径都能触发门锁模式 UI 隐藏
- 与 `AutoConnectLoop()` 中的同类逻辑形成双重保障
- 如需在音频通道关闭后恢复 UI，需在 `OnAudioChannelClosed` 回调中调用 `ShowAllUI()`


---

## 2025-05-19

### 修改文件：`main/display/lcd_display.cc`

**修改位置**：`LcdDisplay::HideAllUI()` 函数中的 UI 组件隐藏逻辑（约第 1526-1542 行）

**修改时间**：2025-05-19

**变更内容**：

- 修改 UI 组件隐藏策略，不再仅依赖 `container_` 父子关系自动隐藏子对象
- 新增对 `top_bar_` 和 `content_` 的显式单独隐藏操作
- 更新注释说明：在不同 UI 风格下，`top_bar_` 可能是 `container_` 的子对象，也可能是独立对象

**实现功能**：

1. **修复不同 UI 风格下的隐藏不完全问题**：
   - 某些 UI 主题/风格中，`top_bar_` 和 `content_` 并非 `container_` 的子对象
   - 之前仅隐藏 `container_` 时，这些独立组件不会被自动隐藏
   - 现在逐个显式隐藏，确保在所有 UI 风格下都能完全隐藏界面

2. **提升门锁模式的兼容性**：
   - 门锁模式下需要完全黑屏，不能有任何 UI 元素残留
   - 此修复确保切换不同 LVGL 主题时，`HideAllUI()` 行为一致
   - 避免因 UI 风格差异导致门锁待机时屏幕上残留顶部栏或内容区域

3. **防御性编程**：
   - 即使 `top_bar_` 和 `content_` 已经是 `container_` 的子对象，重复隐藏也不会产生副作用
   - 确保代码在 UI 结构变化时仍然正确工作

**技术细节**：

- 对 `container_`、`top_bar_`、`content_` 分别调用 `lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN)`
- 每个操作前都有 `!= nullptr` 空指针检查
- LVGL 中对已隐藏的对象重复设置隐藏标志是安全的（幂等操作）

**备注**：

- 此修复与 UI 主题系统的多样性相关
- 确保门锁模式在所有支持的 UI 风格下都能正确工作
- 后续添加新的 UI 风格时，需注意组件的父子关系是否一致


---

## 2025-05-19

### 修改文件：`main/display/lcd_display.cc`

**修改位置**：`LcdDisplay::ShowAllUI()` 函数中的 UI 组件恢复显示逻辑（约第 1391-1415 行）

**修改时间**：2025-05-19

**变更内容**：

- 修改 UI 组件恢复显示策略，新增对 `top_bar_` 的显式单独恢复操作
- 移除了两条冗余注释（"恢复 container_（会自动恢复其子对象）"和"恢复独立于 container_ 的组件"）
- 移除了 GIF 控制器相关的注释（"如果有 GIF 控制器，显示 emoji_image_，否则显示 emoji_label_"）
- 代码结构更加紧凑，逻辑更清晰

**实现功能**：

1. **修复不同 UI 风格下恢复显示不完全的问题**：
   - 与之前 `HideAllUI()` 的修复思路一致
   - 在某些 UI 主题/风格中，`top_bar_` 并非 `container_` 的子对象，而是独立的顶层组件
   - 之前仅恢复 `container_` 时，独立的 `top_bar_` 不会被自动恢复显示
   - 现在显式恢复 `top_bar_`，确保在所有 UI 风格下都能完整恢复界面

2. **与 `HideAllUI()` 保持对称性**：
   - `HideAllUI()` 中已对 `top_bar_` 进行显式隐藏
   - `ShowAllUI()` 中也需要对应的显式恢复，保持隐藏/恢复逻辑的对称一致
   - 避免隐藏了 `top_bar_` 但恢复时遗漏的 Bug

3. **代码质量提升**：
   - 移除不再准确的注释（因为不再仅依赖父子关系自动恢复）
   - 减少误导性注释，代码本身即文档
   - 保持与 `HideAllUI()` 一致的防御性编程风格

**技术细节**：

- 对 `top_bar_` 调用 `lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_HIDDEN)` 恢复显示
- 操作前有 `!= nullptr` 空指针检查
- LVGL 中对已显示的对象重复移除隐藏标志是安全的（幂等操作）
- 恢复顺序：`container_` → `top_bar_` → `status_bar_` → `bottom_bar_` → emoji 组件

**备注**：

- 此修复与之前 `HideAllUI()` 中新增 `top_bar_` 显式隐藏的修改相呼应
- 确保门锁模式退出后，所有 UI 元素都能正确恢复显示
- 适用于所有支持的 UI 风格/主题配置


---

## 2025-05-19

### 修改文件：`main/display/lcd_display.cc`

**修改位置**：`LcdDisplay::HideAllUI()` 函数中的 UI 组件隐藏逻辑（约第 1524-1542 行）

**修改时间**：2025-05-19

**变更内容**：

- 重构 UI 隐藏策略，将逐个组件隐藏改为基于 LVGL screen 子对象遍历的通用方案
- 移除了对所有具体 UI 组件（`container_`、`top_bar_`、`content_`、`status_bar_`、`bottom_bar_`、`side_bar_`、`emoji_box_`、`emoji_label_`、`emoji_image_`、`preview_image_`、`low_battery_popup_`）的逐个隐藏操作
- 新增通用遍历逻辑：获取当前活动 screen 的所有子对象，统一添加 `LV_OBJ_FLAG_HIDDEN` 标志
- 保留屏幕背景色设置为黑色的逻辑

**实现功能**：

1. **通用化 UI 隐藏方案**：
   - 不再依赖对每个 UI 组件的显式引用，而是遍历 screen 的所有子对象统一隐藏
   - 无论 UI 风格如何变化、新增了哪些组件，都能确保完全隐藏
   - 彻底解决了不同 UI 主题下组件父子关系不一致导致的隐藏不完全问题

2. **代码简化与可维护性提升**：
   - 原来约 30 行的逐个组件隐藏代码缩减为约 6 行的遍历循环
   - 后续新增 UI 组件时无需修改 `HideAllUI()` 函数
   - 消除了遗漏新组件的风险

3. **门锁模式可靠性增强**：
   - 门锁待机模式下确保屏幕完全黑屏，无任何 UI 元素残留
   - 适用于所有 LVGL 主题和 UI 风格配置
   - 即使动态创建的临时 UI 对象也会被正确隐藏

**技术细节**：

- 使用 `lv_screen_active()` 获取当前活动屏幕对象
- 使用 `lv_obj_get_child_count(screen)` 获取子对象数量
- 使用 `lv_obj_get_child(screen, i)` 按索引遍历子对象
- 对每个非空子对象调用 `lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN)`
- 遍历前先设置屏幕背景色为黑色

**备注**：

- 此重构是对之前多次修补 `HideAllUI()` 函数的最终优化方案
- 从"逐个枚举组件"升级为"遍历所有子对象"，更加健壮和通用
- 需注意 `ShowAllUI()` 函数是否也需要对应的遍历恢复逻辑
- 与门锁模式的无人值守运行需求相关


---

## 2025-05-19

### 修改文件：`main/application.cc`

**修改位置**：`protocol_->OnAudioChannelOpened()` 回调函数内部，门锁模式 UI 隐藏逻辑（约第 466-480 行）

**修改时间**：2025-05-19

**变更内容**：

- 将门锁模式 UI 隐藏操作从直接在协议回调线程中执行，改为通过 `Schedule()` 调度到主线程执行
- 原代码直接在 `OnAudioChannelOpened` 回调（协议线程）中调用 `lcd_display->HideAllUI()`
- 修改后使用 `Schedule([this]() { ... })` 将整个 UI 操作包装为 lambda，调度到主线程执行

**实现功能**：

1. **修复 LVGL 线程安全问题**：
   - LVGL 不是线程安全的，所有 UI 操作必须在创建 LVGL 的线程（主线程）中执行
   - `OnAudioChannelOpened` 回调运行在 WebSocket 协议线程中
   - 直接在协议线程中调用 `HideAllUI()`（内部操作 LVGL 对象）可能导致竞态条件、UI 渲染异常或崩溃
   - 通过 `Schedule()` 将操作调度到主线程，确保 LVGL 操作的线程安全性

2. **提升系统稳定性**：
   - 避免多线程同时操作 LVGL 对象导致的内存损坏
   - 防止因线程竞争导致的随机崩溃（hard fault）
   - 确保 `DisplayLockGuard` 在正确的线程上下文中工作

3. **适用场景**：
   - 设备通过 WebSocket 连接服务器后自动进入门锁模式
   - 音频通道建立成功后隐藏常规 UI 界面
   - 任何从非主线程触发的 UI 操作都应使用此模式

**技术细节**：

- `Schedule()` 是 `Application` 类提供的线程调度方法，将任务投递到主事件循环
- lambda 捕获 `this` 指针，在主线程中访问 `Board::GetInstance()` 和 `LcdDisplay`
- 调度后的操作在下一次主循环迭代时执行，有微小延迟但确保线程安全
- 原有的 `dynamic_cast` 安全检查和日志输出逻辑保持不变

**备注**：

- 此修复属于线程安全类 Bug 修复，问题可能表现为偶发崩溃或 UI 异常
- 建议检查项目中其他从非主线程调用 LVGL 的代码，统一使用 `Schedule()` 模式
- 与 `AutoConnectLoop()` 中的 `Schedule()` 使用方式一致，保持代码风格统一


---

## 2025-05-19

### 修改文件：`main/display/lcd_display.cc`

**修改位置**：`LcdDisplay::HideAllUI()` 函数中的 UI 隐藏实现逻辑（约第 1522-1537 行）

**修改时间**：2025-05-19

**变更内容**：

- 移除了原有的基于 LVGL screen 子对象遍历的 UI 隐藏方案（设置背景黑色 + 遍历隐藏所有子对象）
- 替换为直接通过硬件背光控制实现黑屏：
  - 通过 `Board::GetInstance().GetBacklight()` 获取背光控制器
  - 调用 `backlight->SetBrightness(0)` 将背光亮度设为 0
- 更新日志信息：从"隐藏所有 UI（门锁模式）"改为"隐藏所有 UI（门锁模式）- 关闭背光"
- 完成日志从"UI 已隐藏，屏幕黑屏"改为"背光已关闭"

**实现功能**：

1. **硬件级黑屏替代软件级 UI 隐藏**：
   - 不再通过 LVGL 逐个隐藏 UI 组件或遍历子对象
   - 直接关闭 LCD 背光，从硬件层面实现完全黑屏
   - 更加彻底和可靠，不存在 UI 组件遗漏的问题

2. **简化代码逻辑**：
   - 原方案需要遍历 screen 所有子对象并逐个设置隐藏标志
   - 新方案仅需一行 `SetBrightness(0)` 调用
   - 代码从约 12 行缩减为约 4 行，更加简洁

3. **降低功耗**：
   - 关闭背光比仅隐藏 UI 组件更省电
   - LCD 背光是屏幕功耗的主要来源
   - 适合门锁设备长时间待机的低功耗需求

4. **避免 LVGL 相关问题**：
   - 不再操作 LVGL 对象，避免了线程安全和对象生命周期问题
   - 不受 UI 主题/风格变化的影响
   - 无需关心组件的父子关系或动态创建的临时对象

**技术细节**：

- 使用 `Board::GetInstance().GetBacklight()` 获取背光抽象接口
- `SetBrightness(0)` 将 PWM 占空比设为 0，完全关闭背光
- 保留了 `ui_hidden_` 标志设置，维持状态一致性
- 背光控制器可能为 nullptr（无背光硬件时），已做空指针检查

**备注**：

- 此修改是对之前多次迭代 `HideAllUI()` 实现方案的最终简化
- 从"逐个隐藏组件" → "遍历子对象" → "关闭背光"，逐步简化
- `ShowAllUI()` 函数可能也需要对应修改，恢复背光亮度
- 与门锁模式的无人值守低功耗运行需求高度契合


---

## 2025-05-19

### 修改文件：`main/display/lcd_display.cc`

**修改位置**：`LcdDisplay::ShowAllUI()` 函数（约第 1531-1549 行）

**修改时间**：2025-05-19

**变更内容**：

- 移除 `DisplayLockGuard lock(this);` — 不再在恢复 UI 时获取显示锁
- 移除早返回时的 `ESP_LOGD` 日志（"UI 已经显示"）
- 简化日志信息：`"恢复所有 UI - 打开背光"` → `"恢复背光"`
- 移除函数末尾的冗余日志 `ESP_LOGI(TAG, "背光已恢复")`

**实现功能**：

1. **与 `HideAllUI()` 背光方案保持一致**：
   - 之前 `HideAllUI()` 已改为纯背光控制（`SetBrightness(0)`），不再操作 LVGL 对象
   - 对应地，`ShowAllUI()` 也简化为纯背光恢复，无需 LVGL 显示锁保护
   - 隐藏/恢复逻辑对称统一，均通过硬件背光控制实现

2. **移除不必要的显示锁**：
   - `DisplayLockGuard` 用于保护 LVGL 对象操作的线程安全
   - 当前 `ShowAllUI()` 仅恢复背光亮度，不涉及 LVGL 对象操作
   - 移除显示锁避免不必要的互斥开销，减少潜在的死锁风险

3. **精简日志输出**：
   - 移除重复和冗余的日志，减少串口输出量
   - 保留一条关键日志（"恢复背光"）即可追踪功能执行
   - 降低日志噪音，便于调试时快速定位关键信息

**技术细节**：

- 函数核心逻辑保持不变：检查 `ui_hidden_` 标志 → 恢复背光 → 设置 `ui_hidden_ = false`
- 背光恢复通过 `Board::GetInstance().GetBacklight()->SetBrightness()` 实现
- 空指针检查保留，确保无背光硬件时不会崩溃

**备注**：

- 此修改是 `HideAllUI()` 改为背光控制方案后的配套简化
- 隐藏/恢复 UI 的完整流程：`HideAllUI()` 关闭背光 ↔ `ShowAllUI()` 恢复背光
- 代码更加简洁，职责更加明确


---

## 2025-05-19

### 修改文件：`main/boards/common/esp32_camera.cc`

**修改位置**：摄像头帧捕获函数中"确定帧数据大小"的逻辑（`buf.bytesused` 为 0 时的 fallback 处理，约第 1175-1199 行）

**修改时间**：2025-05-19

**变更内容**：

- 优化 JPEG 模式下 `buf.bytesused` 为 0 时的帧数据大小确定逻辑
- 原方案：直接使用 `mmap_buffers_[buf.index].length`（整个 buffer 长度）作为帧大小
- 新方案：对 JPEG 格式，从 buffer 末尾向前搜索 JPEG EOI 标记（`0xFF 0xD9`），以此确定实际数据大小
- 若未找到 EOI 标记，回退使用整个 buffer 长度并输出警告日志
- 非 JPEG 格式仍使用原有的 `mmap_buffers_[buf.index].length` 逻辑

**实现功能**：

1. **精确确定 JPEG 帧实际大小**：
   - V4L2 驱动在某些情况下 `buf.bytesused` 返回 0，无法直接获知 JPEG 数据的实际长度
   - JPEG 数据以 SOI（`0xFF 0xD8`）开头、EOI（`0xFF 0xD9`）结尾，EOI 之后为无效填充数据
   - 通过从 buffer 末尾向前搜索 EOI 标记，精确定位 JPEG 数据的真实结束位置

2. **减少无效数据传输**：
   - 原方案使用整个 mmap buffer 长度，可能包含大量 0x00 填充数据
   - 新方案仅传输 JPEG 有效数据（SOI 到 EOI），减少网络带宽占用
   - 对于视频流场景（640x480@10fps），每帧节省的数据量可能达到数 KB

3. **提升视频流质量**：
   - 避免将填充数据作为 JPEG 数据传输，防止接收端解码异常
   - 确保帧大小信息准确，便于接收端正确解析帧边界
   - 提升视频流的稳定性和兼容性

4. **适用场景**：
   - ESP32-S3 摄像头驱动在 JPEG 模式下 `bytesused` 返回 0 的情况
   - 视频流服务（`VideoStreamService`）和人脸识别拍照（`Capture()`）
   - 任何依赖准确 JPEG 帧大小的下游处理逻辑

**技术细节**：

- 搜索方向：从 buffer 末尾向前（`for (size_t i = max_len - 1; i > 0; i--)`）
- EOI 标记：两字节序列 `0xFF 0xD9`
- 数据长度：EOI 标记位置 + 1（包含 `0xD9` 字节本身）
- 格式判断：通过 `sensor_format_ == V4L2_PIX_FMT_JPEG` 区分 JPEG 和其他格式
- 异常处理：未找到 EOI 时使用整个 buffer 并输出 `ESP_LOGW` 警告

**备注**：

- 此优化针对 ESP32-S3 USB 摄像头驱动的特定行为
- 从末尾向前搜索比从头向前搜索更高效（JPEG 数据通常占 buffer 大部分空间）
- 不影响 `bytesused` 正常返回非零值的情况（优先使用 `bytesused`）
- 与视频流性能监控日志配合，可观察优化后的帧大小变化


---

## 2025-05-19

### 修改文件：`main/boards/common/esp32_camera.cc`

**修改位置**：摄像头帧捕获函数中 JPEG EOI 标记搜索逻辑（`buf.bytesused` 为 0 时的 JPEG 帧大小确定，约第 1189-1213 行）

**修改时间**：2025-05-19

**变更内容**：

- 将 JPEG EOI 标记的搜索方向从"从末尾向前搜索"改为"从头部（SOI 之后）向前搜索"
- 原方案：`for (size_t i = max_len - 1; i > 0; i--)` 从 buffer 末尾向前查找 `0xFF 0xD9`
- 新方案：`for (size_t i = 2; i < max_len - 1; i++)` 从 SOI 标记之后向后查找 `0xFF 0xD9`
- 修改匹配条件：原 `p[i] == 0xD9 && p[i-1] == 0xFF` → 新 `p[i] == 0xFF && p[i+1] == 0xD9`
- 修改数据长度计算：原 `data_len = i + 1` → 新 `data_len = i + 2`（包含完整的 EOI 两字节）
- 初始化 `data_len = 0`，确保未找到 EOI 时能正确进入 fallback 逻辑
- 新增成功找到 EOI 时的调试日志：输出 JPEG 帧实际大小和 buffer 总大小

**实现功能**：

1. **修复从末尾搜索可能匹配到错误位置的问题**：
   - 从末尾向前搜索时，如果 buffer 尾部填充数据中恰好包含 `0xFF 0xD9` 字节序列，会导致帧大小计算错误（偏大）
   - 从头部向后搜索找到的是第一个 EOI 标记，即 JPEG 数据的真正结束位置
   - JPEG 标准规定 SOI 和 EOI 之间为有效数据，第一个 EOI 即为帧结束

2. **提升帧大小计算的准确性**：
   - 从 SOI（`0xFF 0xD8`，位于 buffer 开头前两字节）之后开始搜索，跳过 SOI 本身
   - 找到第一个 `0xFF 0xD9` 即为真正的 EOI，不会被后续填充数据干扰
   - `data_len = i + 2` 正确包含 EOI 的两个字节（`0xFF` 和 `0xD9`）

3. **新增调试日志辅助性能分析**：
   - 成功找到 EOI 时输出 `ESP_LOGD` 日志，显示 JPEG 帧实际大小和 buffer 总大小
   - 便于观察 JPEG 压缩率和 buffer 利用率
   - 使用 `ESP_LOGD` 级别，生产环境不会输出，仅调试时可见

**技术细节**：

- 搜索起始位置：`i = 2`（跳过 SOI 标记的两个字节 `0xFF 0xD8`）
- 搜索终止条件：`i < max_len - 1`（确保 `p[i+1]` 不越界）
- EOI 匹配：`p[i] == 0xFF && p[i+1] == 0xD9`（正序匹配，更直观）
- 数据长度：`i + 2`（包含 `0xFF` 在位置 i，`0xD9` 在位置 i+1）
- 未找到 EOI 时：`data_len` 保持为 0，触发 fallback 使用 `max_len`

**备注**：

- 此修改是对上一版本"从末尾向前搜索 EOI"方案的改进
- 从头向后搜索语义更正确：找到第一个 EOI 即为 JPEG 数据结束
- 从末尾向前搜索的风险：buffer 尾部可能存在与 EOI 相同的字节模式
- 性能影响：对于正常 JPEG 帧，从头搜索和从尾搜索的平均耗时相近；但从头搜索的正确性更有保障
- 与视频流性能监控日志配合，可通过 `ESP_LOGD` 观察每帧的实际大小


---

## 2025-05-19

### 修改文件：`main/video/video_stream_service.cc`

**修改位置**：视频流捕获任务循环中的帧处理流水线（`CaptureForStream()` → `CaptureJpeg()` → 帧入队之间的代码）

**修改时间**：2025-05-19

**变更内容**：

- 移除视频流捕获循环中的所有性能计时代码（约 15 行）：
  - 删除 `t_capture_start` 时间戳（捕获前）
  - 删除 `t_capture_done` 时间戳（捕获后）
  - 删除 `t_encode_done` 时间戳（编码后）
  - 删除 `t_copy_done` 时间戳（拷贝后）
  - 删除 `ESP_LOGI` 性能日志输出（格式：`[性能] 捕获=%dms, 编码=%dms, 拷贝=%dms, 帧大小=%zu bytes, 分辨率=%dx%d`）

**实现功能**：

1. **清理调试用性能日志**：
   - 性能计时代码为开发调试阶段使用，已完成性能分析和调优
   - 移除后减少每帧处理时的日志输出，降低串口 I/O 开销
   - 避免生产环境下大量重复日志占用串口带宽和 CPU 时间

2. **轻微提升视频流性能**：
   - 移除 4 次 `xTaskGetTickCount()` 调用和 1 次 `ESP_LOGI` 格式化输出
   - 减少每帧处理的额外开销（虽然很小，但在 10fps 下每秒执行 10 次）
   - 帧处理流水线更加精简

3. **代码整洁**：
   - 保留核心业务逻辑（捕获→编码→入队），移除辅助调试代码
   - 代码更加简洁，便于阅读和维护

**备注**：

- 此为之前添加的性能监控日志的清理操作
- 如需再次进行性能分析，可重新添加计时代码或使用 `ESP_LOGD` 级别
- 视频流核心功能不受影响


---

## 2025-05-19

### 修改文件：`main/boards/common/esp32_camera.cc`

**修改位置**：帧捕获函数中 `sensor_format_` 的 `switch` 分支，`default` 分支之前新增 `V4L2_PIX_FMT_JPEG` case（约第 1578-1624 行）

**修改时间**：2025-05-19

**变更内容**：

- 在条件编译宏 `CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT` 保护下，新增 `V4L2_PIX_FMT_JPEG` 格式的本地预览处理分支（约 41 行代码）
- 实现 JPEG → RGB565 的解码转换流程：
  1. 将 mmap buffer 中的 JPEG 数据拷贝到 `frame_.data`
  2. 调用 `jpeg_to_image()` 将 JPEG 解码为 RGB565 格式
  3. 解码成功后，释放原始 JPEG 数据，用 RGB565 数据替换帧内容
  4. 更新帧元数据（`width`、`height`、`len`、`format`）
- 完善的错误处理：
  - 解码失败时释放所有已分配内存（`frame_.data` 和 `rgb565_out`）
  - 将 buffer 归还 V4L2 队列（`VIDIOC_QBUF`）
  - 输出错误日志并返回 `false`

**实现功能**：

1. **支持 JPEG 格式摄像头的本地 LCD 预览**：
   - 某些摄像头传感器仅支持 JPEG 输出格式（如 OV2640 的 JPEG 模式）
   - LCD 显示需要 RGB565 格式的像素数据
   - 此分支在捕获 JPEG 帧后实时解码为 RGB565，使 JPEG 摄像头也能进行本地预览

2. **扩展摄像头兼容性**：
   - 之前本地预览仅支持 RGB565 和 YUV 等原始像素格式
   - 新增 JPEG 格式支持后，更多类型的摄像头模组可以用于本地预览功能
   - 通过 Kconfig 宏 `CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT` 控制是否启用

3. **适用场景**：
   - 智能门锁的猫眼预览功能：用户通过 LCD 屏幕查看门外画面
   - 使用 JPEG 模式摄像头的开发板进行本地画面显示
   - 需要在 LCD 上实时显示摄像头画面的任何场景

**技术细节**：

- 解码函数：`jpeg_to_image()`，输入 JPEG 数据，输出 RGB565 像素数据
- 输出参数：`rgb565_out`（像素指针）、`rgb565_len`（数据长度）、`out_width`/`out_height`（分辨率）、`out_stride`（行步长）
- 内存管理：解码输出使用 `heap_caps_malloc`（PSRAM），通过 `heap_caps_free` 释放
- 条件编译：`#ifdef CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT` 保护，默认不启用
- 错误处理：解码失败时确保无内存泄漏，正确归还 V4L2 buffer

**备注**：

- 此功能需要在 Kconfig 中启用 `CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT` 选项
- JPEG 解码有一定 CPU 开销，可能影响预览帧率（取决于分辨率和 JPEG 复杂度）
- 与 `local-camera-preview` 规范相关，扩展了预览功能的摄像头格式支持
- 解码后的 RGB565 数据可直接用于 LVGL 图像显示（`lv_img_set_src`）


---

## 2025-05-19

### 修改文件：`main/boards/bread-compact-wifi-s3cam/compact_wifi_board_s3cam.cc`

**修改位置**：`InitializeButtons()` 函数中，`boot_button_.OnClick()` 回调之后（约第 236-264 行）

**修改时间**：2025-05-19

**变更内容**：

- 新增 `boot_button_.OnDoubleClick()` 回调注册（约 27 行代码）
- 实现 Boot 按钮双击切换本地摄像头预览模式的功能
- 双击逻辑通过 `Application::Schedule()` 调度到主线程执行，确保线程安全

**实现功能**：

1. **双击 Boot 按钮切换本地预览模式**：
   - 双击 Boot 按钮时，检查当前是否正在本地预览
   - 若正在预览：调用 `app.StopLocalPreview()` 停止预览，并关闭 LCD 背光（`SetBrightness(0)`）
   - 若未在预览：先恢复 LCD 背光（`RestoreBrightness()`），再调用 `app.StartLocalPreview()` 启动预览
   - 启动预览失败时，自动关闭背光（避免亮屏但无画面的状态）

2. **背光联动控制**：
   - 预览开启时自动恢复背光亮度，确保用户能看到摄像头画面
   - 预览关闭时自动关闭背光，进入门锁待机低功耗状态
   - 启动失败时也关闭背光，保持一致的黑屏待机状态

3. **适用场景**：
   - 用户想快速查看门外画面时，双击 Boot 按钮即可开启本地预览
   - 查看完毕后再次双击关闭预览，设备恢复待机状态
   - 无需语音唤醒或远程操作，提供物理按键的快捷交互方式

**技术细节**：

- 使用 `boot_button_.OnDoubleClick()` 注册双击事件回调
- 通过 `app.Schedule()` 将操作调度到主线程，避免在按钮中断上下文中直接操作 UI
- `app.IsLocalPreviewActive()` 查询当前预览状态
- `Board::GetInstance().GetBacklight()` 获取背光控制器
- `backlight->SetBrightness(0)` 关闭背光，`backlight->RestoreBrightness()` 恢复上次亮度
- `app.StartLocalPreview()` 返回 `bool` 表示启动是否成功

**备注**：

- 此功能为智能门锁提供了物理按键快捷操作方式
- 与已有的单击切换聊天状态（`ToggleChatState()`）互不冲突
- 与 `local-camera-preview` 规范相关联
- 背光控制与门锁模式的低功耗设计一致


---

## 2025-05-19

### 修改文件：`main/application.cc`

**修改位置**：`Application::StartLocalPreview()` 函数中活动标志设置的时序（约第 2770-2825 行）

**修改时间**：2025-05-19

**变更内容**：

- 将 `local_preview_active_ = true` 的设置从原来的步骤 10（所有任务创建完成之后）提前到步骤 7（创建 Capture Task 之前）
- 在 Capture Task 创建失败时新增回滚逻辑：`local_preview_active_ = false`
- 更新步骤编号注释：原步骤 7→8（创建 Capture Task）、原步骤 8→9（创建 Display Task）、原步骤 9→10（切换显示模式）
- 移除了原步骤 10 中单独设置 `local_preview_active_ = true` 的代码块

**实现功能**：

1. **修复任务循环条件竞态问题**：
   - Capture Task 和 Display Task 的循环条件依赖 `local_preview_active_` 标志
   - 原方案在任务创建之后才设置标志，存在时序窗口：任务已启动但标志尚未设置
   - 在此窗口内，任务循环条件 `while (local_preview_active_)` 不满足，任务可能立即退出
   - 新方案在创建任务之前设置标志，确保任务启动时循环条件已满足

2. **失败回滚保护**：
   - 若 Capture Task 创建失败（`xTaskCreate` 返回非 `pdPASS`），立即将标志回滚为 `false`
   - 避免标志状态与实际运行状态不一致
   - 后续的资源清理（删除队列、弹出错误提示）在标志回滚之后执行

3. **提升本地预览启动的可靠性**：
   - 消除了任务启动与标志设置之间的竞态条件
   - 确保 Capture Task 和 Display Task 在启动后能正确进入工作循环
   - 避免偶发的"预览启动后立即退出"问题

**技术细节**：

- `local_preview_active_` 是 `Application` 类的成员变量，控制预览任务的运行/停止
- FreeRTOS 任务创建后可能立即被调度执行（取决于优先级和当前运行任务）
- 若新任务优先级高于当前任务，`xTaskCreate` 返回前新任务就可能已经开始执行
- 因此必须在 `xTaskCreate` 调用之前设置好任务所依赖的状态标志

**备注**：

- 此修复属于并发时序类 Bug 修复
- 问题可能表现为偶发的本地预览启动失败（任务创建成功但立即退出）
- 与 `local-camera-preview` 规范相关联
- 遵循 FreeRTOS 最佳实践：在创建任务前准备好任务所需的所有共享状态


---

## 2025-05-19

### 修改文件：`main/display/lcd_display.cc`

**修改位置**：`LcdDisplay::UpdatePreviewCanvas()` 函数中的图像数据处理逻辑（约第 1446-1493 行）

**修改时间**：2025-05-19

**变更内容**：

- 移除了原有的图像尺寸严格匹配检查（输入尺寸必须等于 Canvas 尺寸，否则返回失败）
- 新增最近邻缩放（Nearest Neighbor Scaling）逻辑：
  - 当输入图像尺寸与 Canvas 尺寸匹配时，直接 `memcpy` 复制（保持原有高效路径）
  - 当输入图像尺寸与 Canvas 尺寸不匹配时，使用最近邻插值算法将图像缩放到 Canvas 尺寸
- 更新性能日志格式：
  - 原日志输出数据大小（`data_size`）
  - 新日志输出输入分辨率（`width×height`）和输出分辨率（`width_×height_`），便于监控缩放行为
- 调整步骤编号注释（原 4-8 步改为 4-6 步）

**实现功能**：

1. **支持任意分辨率输入的预览显示**：
   - 摄像头捕获的图像分辨率可能与 LCD 屏幕分辨率不一致（如摄像头 640×480，屏幕 240×240）
   - 原方案在尺寸不匹配时直接返回失败，导致预览无法显示
   - 新方案自动将输入图像缩放到 Canvas 尺寸，确保任何分辨率的摄像头画面都能正确显示

2. **最近邻缩放算法**：
   - 对目标 Canvas 的每个像素，计算其在源图像中对应的坐标
   - 使用整数运算（`(uint32_t)dy * height / height_`）避免浮点计算开销
   - 逐行逐列遍历目标像素，从源图像中采样对应像素值
   - 适合嵌入式环境：无需额外内存、计算量可控、实现简单

3. **保持高效路径**：
   - 当输入尺寸恰好匹配 Canvas 尺寸时，仍使用 `memcpy` 直接复制
   - 避免不必要的缩放计算开销
   - 对于尺寸匹配的场景，性能与修改前完全一致

4. **增强性能监控**：
   - 日志中同时输出输入和输出分辨率，便于确认缩放是否生效
   - 耗时日志标签从"复制耗时"改为"缩放+复制耗时"，更准确反映实际操作

**技术细节**：

- 数据格式：RGB565（每像素 2 字节，`uint16_t`）
- 缩放公式：`sy = dy * src_height / dst_height`，`sx = dx * src_width / dst_width`
- 使用 `uint32_t` 中间变量避免 16 位整数溢出（最大支持 65535×65535 分辨率）
- 缩放结果直接写入 `preview_canvas_buffer_`，无需额外缓冲区
- 支持放大和缩小两种场景

**备注**：

- 此修改解决了摄像头分辨率与屏幕分辨率不匹配时预览无法显示的问题
- 最近邻算法在缩小时可能产生锯齿，但对于嵌入式实时预览场景可接受
- 如需更高画质，后续可考虑双线性插值（Bilinear Interpolation），但计算量更大
- 与 `local-camera-preview` 规范相关联


---

## 2025-05-19

### 修改文件：`main/display/lcd_display.cc`

**修改位置**：`LcdDisplay::UpdatePreviewCanvas()` 函数中的图像缩放逻辑（尺寸不匹配时的 else 分支，约第 1454-1486 行）

**修改时间**：2025-05-19

**变更内容**：

- 将原有的"最近邻拉伸缩放"改为"等比缩放 + 黑边填充"方案
- 原方案：直接将源图像拉伸到整个 Canvas 尺寸（`width_×height_`），不保持宽高比
- 新方案：
  1. 计算水平和垂直方向的缩放比例（`scale_x`、`scale_y`），取较小值作为统一缩放比
  2. 根据统一缩放比计算目标图像尺寸（`dst_w`、`dst_h`）
  3. 计算居中偏移量（`offset_x`、`offset_y`），使缩放后的图像在 Canvas 中居中
  4. 先用 `memset` 将整个 Canvas 清零（黑色背景），填充黑边区域
  5. 使用最近邻插值将源图像缩放到目标区域，写入 Canvas 的居中位置

**实现功能**：

1. **保持源图像宽高比**：
   - 原方案将图像拉伸到屏幕尺寸，当摄像头宽高比与屏幕不一致时（如 640×480 → 240×240），画面会变形
   - 新方案保持源图像的原始宽高比，避免画面拉伸变形
   - 例如 640×480（4:3）显示在 240×240 屏幕上，实际显示区域为 240×180，上下各留 30 像素黑边

2. **居中显示 + 黑边填充（Letterbox）**：
   - 缩放后的图像在 Canvas 中水平和垂直方向均居中
   - 未被图像覆盖的区域填充为黑色（RGB565 值 0x0000）
   - 类似电影在宽屏电视上的 Letterbox 显示效果

3. **提升预览画面视觉效果**：
   - 人物面部不再因拉伸而变形，有利于人脸识别场景的视觉确认
   - 画面比例正确，更符合用户对摄像头画面的预期
   - 黑边区域整洁统一，不会出现残留像素

**技术细节**：

- 缩放比计算：`scale = min(width_ / width, height_ / height)`（使用 float）
- 目标尺寸：`dst_w = width * scale`，`dst_h = height * scale`
- 居中偏移：`offset_x = (width_ - dst_w) / 2`，`offset_y = (height_ - dst_h) / 2`
- 目标行地址：`dst_row = dst + (dy + offset_y) * width_ + offset_x`
- 源像素采样：`sx = dx * width / dst_w`（整数运算，最近邻插值）
- 黑边填充：`memset(preview_canvas_buffer_, 0, width_ * height_ * 2)`

**备注**：

- 此修改优化了摄像头预览画面的显示效果，从"拉伸填满"升级为"等比缩放居中"
- 适用于摄像头分辨率与屏幕分辨率宽高比不一致的场景
- 当宽高比恰好一致时（如 320×240 → 240×180），效果与拉伸方案相同
- 与 `local-camera-preview` 规范相关联


---

## 2025-05-19

### 修改文件：`main/boards/common/esp32_camera.cc`

**修改位置**：摄像头帧捕获函数中 JPEG → RGB565 解码转换逻辑（`V4L2_PIX_FMT_RGB565` case 分支，约第 1585-1715 行）

**修改时间**：2025-05-19

**变更内容**：

- 将 JPEG 解码为 RGB565 的方案从单步 `jpeg_to_image()` 改为两步方案：
  1. 使用 `esp_new_jpeg`（`jpeg_dec_*` API）将 JPEG 解码为 RGB888
  2. 使用 `esp_imgfx_color_convert` 将 RGB888 转换为 RGB565_LE
- 移除了对 `jpeg_to_image()` 函数的调用
- 新增完整的错误处理链：解码器打开失败、头解析失败、RGB888 缓冲区分配失败、解码处理失败、RGB565 缓冲区分配失败、颜色转换器打开失败、转换处理失败，每个环节都有资源清理和 VIDIOC_QBUF 归还
- 使用 `jpeg_calloc_align()` 分配 16 字节对齐的 RGB888 中间缓冲区
- 使用 `esp_imgfx_color_convert_open/process/close` 完成色彩空间转换

**实现功能**：

1. **修复 JPEG 子采样模式下的色彩异常**：
   - 原 `jpeg_to_image()` 直接解码为 RGB565_LE 时，在某些 JPEG 子采样模式（如 4:2:0、4:2:2）下出现色彩失真
   - 新方案先解码为 RGB888（色彩精度更高），再转换为 RGB565_LE，避免了子采样到低色深直接转换的精度损失
   - 确保摄像头预览画面色彩准确，尤其对人脸识别场景的肤色还原至关重要

2. **提升解码可靠性**：
   - 使用 ESP-IDF 官方的 `esp_new_jpeg` 解码器 API，兼容性更好
   - 分步处理便于定位具体失败环节（解码失败 vs 转换失败）
   - 每个步骤都有独立的错误日志，便于调试

3. **完善资源管理**：
   - 每个失败路径都正确释放已分配的资源（RGB888 缓冲区、frame_.data、解码器句柄、转换器句柄）
   - 确保 V4L2 buffer 在失败时被正确归还（`VIDIOC_QBUF`）
   - 使用 `jpeg_free_align()` 释放对齐分配的内存

**技术细节**：

- 解码配置：`JPEG_PIXEL_FORMAT_RGB888`，`JPEG_ROTATE_0D`
- RGB888 缓冲区大小：`width × height × 3`，16 字节对齐（`jpeg_calloc_align`）
- RGB565 缓冲区大小：`width × height × 2`，分配在 PSRAM（`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`）
- 颜色转换配置：`ESP_IMGFX_PIXEL_FMT_RGB888` → `ESP_IMGFX_PIXEL_FMT_RGB565_LE`
- 分辨率信息从 `jpeg_dec_header_info_t` 中获取（`out_info.width`、`out_info.height`）

**备注**：

- 此修改解决了本地摄像头预览画面色彩异常的问题
- 代价是增加了一个 RGB888 中间缓冲区的内存开销（`width × height × 3` bytes）
- 对于 640×480 分辨率，RGB888 中间缓冲区约 900KB，需确保 PSRAM 有足够空间
- 两步方案的性能开销略高于单步直接解码，但色彩准确性显著提升
- 与 `local-camera-preview` 规范相关联


---

## 2025-05-19

### 修改文件：`main/application.cc`

**修改位置**：`Application` 类中本地摄像头预览任务循环函数（`LocalPreviewTask` 或类似的帧捕获循环），帧捕获和数据验证逻辑区域（约第 2917-2965 行）

**修改时间**：2025-05-19

**变更内容**：

- 更新帧捕获注释，补充说明处理流程：从"捕获帧"改为"捕获帧（包含 JPEG 解码 + RGB888→RGB565 转换）"
- 修复 `size_t` 格式化打印的跨平台兼容性问题：
  - 将 `%zu` 格式符改为 `%d`，并对 `data_size` 进行 `(int)` 强制类型转换
  - 避免在 ESP32 工具链中 `%zu` 可能导致的编译警告或输出异常
- 新增性能监控日志（`ESP_LOGI` 级别）：
  - 输出格式：`[预览性能] 捕获+解码=%ums, 帧=%dx%d, 数据=%d bytes`
  - 记录每帧的捕获+解码总耗时、帧分辨率、RGB565 数据大小
  - 位于帧捕获耗时计算之后、原有 DEBUG 日志之前

**实现功能**：

1. **本地预览性能实时监控**：
   - 每帧输出捕获+解码的总耗时，便于评估预览帧率是否达标
   - 记录帧分辨率和数据大小，确认预览参数是否正确
   - 使用 `ESP_LOGI` 级别，默认可见，无需调整日志等级即可观察

2. **辅助性能调优**：
   - 配合之前在 `video_stream_service.cc` 中添加的视频流性能日志
   - 可对比本地预览和远程视频流的性能差异
   - 帮助定位 JPEG 解码 + RGB888→RGB565 转换的性能瓶颈

3. **修复编译警告**：
   - ESP32 工具链（xtensa-esp32s3-elf-gcc）对 `%zu` 格式符的支持可能不完整
   - 使用 `%d` + `(int)` 转换是 ESP-IDF 项目中的常见做法
   - 消除潜在的格式化字符串警告

4. **代码文档化**：
   - 注释明确说明 `CaptureForPreview()` 内部包含 JPEG 解码和色彩空间转换
   - 帮助后续开发者理解该函数的性能开销来源
   - 与之前修改的 `esp32_camera.cc` 中两步解码方案相呼应

**技术细节**：

- 性能计时使用 `xTaskGetTickCount()` + `portTICK_PERIOD_MS` 转换为毫秒
- `capture_time_ms` 包含了 `CaptureForPreview()` 的完整耗时（V4L2 捕获 + JPEG 解码 + RGB888→RGB565 转换）
- 对于 640×480 分辨率，预期捕获+解码耗时约 50-150ms（取决于 JPEG 复杂度）
- 日志输出频率与帧率一致（约 10fps），每秒约 10 条日志

**备注**：

- 此为调试/性能分析用途的日志代码
- 生产环境中可考虑降低日志级别为 `ESP_LOGD` 或通过条件编译控制
- 与 `video_stream_service.cc` 中的性能日志格式保持一致
- 不影响预览功能的正常运行，仅增加少量日志输出开销


---

## 2025-05-19

### 修改文件：`main/application.cc`

**修改位置**：`Application::CheckNewVersion()` 函数末尾，`has_server_time_ = ota.HasServerTime();` 之后、`if (protocol_started)` 之前

**修改时间**：2025-05-19

**变更内容**：

- 新增 OTA 检查完成后向 STM32 同步当前时间的逻辑（约 15 行代码）：
  - 检查是否成功获取服务器时间（`has_server_time_`）且锁控服务可用（`lock_control_`）
  - 通过 `time()` 和 `localtime_r()` 获取当前本地时间
  - 调用 `lock_control_->SendSyncTime()` 将时、分、秒发送给 STM32
  - 使用 `ESP_LOGI` 记录同步的时间值

**实现功能**：

1. **ESP32 → STM32 时间同步**：
   - OTA 检查过程中会从服务器获取当前时间（NTP 或 HTTP 响应头）
   - 获取到准确时间后，立即通过 UART 协议同步给 STM32 锁控 MCU
   - STM32 可利用该时间实现定时功能（如定时上锁、时间段访问控制等）

2. **适用场景**：
   - 设备启动后首次连接服务器，获取网络时间并同步给 STM32
   - STM32 无网络能力，依赖 ESP32 提供准确时间源
   - 门锁系统需要时间信息用于访问日志记录、定时策略等

3. **条件保护**：
   - 仅在成功获取服务器时间时才同步（避免发送无效时间）
   - 仅在锁控服务已初始化时才执行（避免空指针访问）
   - 双重条件确保时间同步的可靠性

**技术细节**：

- 使用 POSIX `time()` 获取 Unix 时间戳
- 使用 `localtime_r()` 转换为本地时间（线程安全版本）
- 时间参数转换为 `uint8_t` 类型，匹配 UART 协议的字节格式
- `SendSyncTime()` 通过 7 字节 UART 协议发送时间数据给 STM32

**备注**：

- 此功能依赖 `LockControl::SendSyncTime()` 方法的实现
- 时间同步发生在 OTA 检查完成后，此时网络时间已校准
- 后续可考虑定时同步（如每小时一次），防止 STM32 时钟漂移
- 与智能门锁系统的时间相关功能（访问日志、定时策略）配合使用


---

## 2025-05-19

### 修改文件：`main/boards/common/esp32_camera.cc`

**修改位置**：JPEG 解码后 YUV422 → RGB565 颜色转换配置（`esp_imgfx_color_convert_cfg_t` 结构体，约第 1673 行）

**修改时间**：2025-05-19

**变更内容**：

- 修改颜色转换器的输入像素格式配置：
  - 原值：`ESP_IMGFX_PIXEL_FMT_YUYV`
  - 新值：`ESP_IMGFX_PIXEL_FMT_UYVY`
- 仅修改一行代码，其余转换参数（输出格式 RGB565_LE、色彩空间 BT601）保持不变

**实现功能**：

1. **修正 YUV422 像素格式匹配**：
   - JPEG 解码器（`esp_jpeg_decode`）输出的 YUV422 数据实际采用 UYVY 排列方式
   - UYVY 字节序：`U0 Y0 V0 Y1`（色度在前，亮度在后）
   - YUYV 字节序：`Y0 U0 Y1 V0`（亮度在前，色度在后）
   - 之前错误地将输入格式声明为 YUYV，导致颜色转换器按错误的字节偏移读取色度/亮度分量

2. **修复本地预览画面颜色异常**：
   - 输入格式不匹配会导致 RGB565 输出的颜色通道错位
   - 表现为画面色偏（如红蓝互换、颜色失真、画面偏绿/偏紫等）
   - 修正后颜色转换器能正确解析 UYVY 数据，输出正确的 RGB565 像素

3. **适用场景**：
   - 本地摄像头预览模式（`CaptureForPreview()`）中的帧显示
   - JPEG 帧解码后需要转换为 RGB565 格式供 LCD 显示的流程
   - 任何使用 `esp_imgfx_color_convert` 处理 JPEG 解码输出的场景

**技术细节**：

- `esp_imgfx_color_convert` 是 ESP-IDF 提供的图像格式转换组件
- JPEG 解码器输出 YUV422 数据时，ESP-IDF 的 JPEG 解码器默认输出 UYVY 排列
- RGB565_LE（Little Endian）适配 LVGL 的默认像素格式
- BT601 色彩空间标准适用于标准清晰度视频（SD）

**备注**：

- 此修复解决了本地预览画面颜色失真的问题
- 修改前画面可能呈现色偏或颜色通道错位的异常效果
- 修改后 LCD 屏幕上的摄像头预览画面颜色应恢复正常
- 与本地摄像头预览功能（`local-camera-preview` 规范）相关


---

## 2025-05-19

### 修改文件：`main/boards/common/esp32_camera.cc`

**修改位置**：`CaptureForPreview()` 函数中的 JPEG 解码和颜色转换流程（约第 1586-1710 行）

**修改时间**：2025-05-19

**变更内容**：

- 将 JPEG 解码器输出格式从 `JPEG_PIXEL_FORMAT_CbYCrY`（YUV422）改为 `JPEG_PIXEL_FORMAT_RGB565_LE`（RGB565 小端序）
- 移除了整个 YUV422 → RGB565 的手动颜色转换流程（约 55 行代码），包括：
  - `esp_imgfx_color_convert` 转换器的创建、配置、执行和关闭
  - 中间 YUV 缓冲区的分配和释放
  - 额外的 RGB565 输出缓冲区分配
  - 相关的错误处理和清理逻辑
- 变量重命名：`yuv_buf` / `yuv_size` → `rgb565_buf` / `rgb565_size`
- 解码完成后直接将 `rgb565_buf` 赋值给 `frame_.data`，无需额外内存拷贝
- 更新注释：从"解码为 YUV422，绕过解码器内部 YUV→RGB 转换的色彩 bug"改为"解码为 RGB565"

**实现功能**：

1. **简化本地预览帧处理流水线**：
   - 原流程：JPEG → YUV422（解码）→ RGB565（颜色转换）→ 显示
   - 新流程：JPEG → RGB565（解码直出）→ 显示
   - 减少一次完整的像素格式转换步骤，流程更简洁

2. **提升预览帧处理性能**：
   - 移除了 `esp_imgfx_color_convert` 颜色转换步骤的 CPU 开销
   - 减少一次 320×240×2 = 153,600 字节的内存分配和释放
   - 减少一次 153,600 字节的像素数据遍历和转换计算
   - 对于 10fps 的预览帧率，每秒节省 10 次颜色转换操作

3. **减少内存占用**：
   - 不再需要同时持有 YUV 中间缓冲区和 RGB565 输出缓冲区
   - 解码器直接输出 RGB565 到 `rgb565_buf`，该缓冲区直接作为帧数据使用
   - 峰值内存占用减少约 150KB（一个 320×240×2 的中间缓冲区）

4. **消除颜色转换相关的色彩问题**：
   - 之前的方案是为了"绕过解码器内部 YUV→RGB 转换的色彩 bug"而采用 YUV422 输出
   - 现在直接使用 `JPEG_PIXEL_FORMAT_RGB565_LE` 输出，说明解码器的 RGB565 直出功能已可正常工作
   - 避免了 UYVY/YUYV 格式混淆导致的色偏问题（之前已修复过一次）

**技术细节**：

- `JPEG_PIXEL_FORMAT_RGB565_LE`：RGB565 小端序，直接兼容 LVGL 的像素格式
- 解码器 scale 功能保持不变：仍将原始分辨率缩小到 320×240
- `jpeg_calloc_align()` 分配的缓冲区直接赋值给 `frame_.data`，无需 `heap_caps_malloc` 重新分配
- 移除了对 `esp_imgfx_color_convert` 组件的依赖（该函数内不再使用）

**备注**：

- 此修改是对之前多次修复颜色转换问题（YUYV/UYVY 格式混淆）的根本性解决方案
- 直接让 JPEG 解码器输出目标格式，从源头消除格式转换环节
- 如果后续发现 RGB565 直出存在色彩问题，可回退到 YUV422 + 手动转换方案
- 与本地摄像头预览功能（`local-camera-preview` 规范）相关


---

## 2025-05-19

### 修改文件：`main/display/lcd_display.cc`

**修改位置**：`LcdDisplay::UpdatePreviewCanvas()` 函数中的横屏显示分支（else 分支，约第 1456-1477 行）

**修改时间**：2025-05-19

**变更内容**：

- 简化注释：移除了关于 640×480 缩放到 320×240 的过时描述，更新为"将 320×240 缩放并旋转 90° 写入 240×320 的 Canvas"
- 移除了多行冗余的中间步骤注释（缩放比例说明、坐标映射公式等）
- 在像素写入循环中新增 RGB565 红蓝通道交换（R/B swap）逻辑：
  - 从源像素中提取 R（低 5 位）、G（中间 6 位）、B（高 5 位）
  - 重新组合为标准 RGB565 格式：`(R << 11) | (G << 5) | B`
- 保留了原有的缩放和旋转 90°（顺时针）逻辑不变

**实现功能**：

1. **修复横屏预览画面色彩偏差（红蓝互换）**：
   - 摄像头 JPEG 解码器输出的 RGB565 数据中，R 和 B 通道位置与 LCD 显示器期望的格式相反
   - 表现为画面中红色物体显示为蓝色、蓝色物体显示为红色
   - 通过在像素级别交换 R 和 B 通道，恢复正确的色彩显示

2. **适用场景**：
   - 本地摄像头预览功能中，横屏模式下的画面显示
   - 当 `width != width_`（源图像宽度与 Canvas 宽度不一致）时触发此分支
   - 典型场景：320×240 的摄像头帧显示在 240×320 的竖屏 LCD 上

3. **色彩格式说明**：
   - 源数据格式：`BBBBB_GGGGGG_RRRRR`（B 在高位，R 在低位）
   - 目标格式：`RRRRR_GGGGGG_BBBBB`（R 在高位，B 在低位，标准 RGB565）
   - 转换方式：提取各通道后重新按标准 RGB565 排列

**技术细节**：

- RGB565 格式：16 位像素，R 占 5 位、G 占 6 位、B 占 5 位
- 位操作：`pixel & 0x1F` 提取低 5 位，`(pixel >> 5) & 0x3F` 提取中间 6 位，`(pixel >> 11) & 0x1F` 提取高 5 位
- 此操作在每个像素的缩放+旋转循环内执行，增加少量计算开销
- 对于 240×320 = 76800 个像素，每像素增加约 5 次位操作，总体影响可忽略

**备注**：

- 此修复与之前将 JPEG 解码器改为 `JPEG_PIXEL_FORMAT_RGB565_LE` 直出相关
- RGB565_LE 的字节序可能导致 R/B 通道在不同硬件上的解释不同
- 竖屏直接 memcpy 的分支（`width == width_`）未做此转换，可能也需要检查
- 如果竖屏模式也出现色偏，需在 memcpy 之前或之后添加类似的 R/B 交换逻辑
