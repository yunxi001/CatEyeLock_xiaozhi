# xiaozhi-esp32 通用硬件抽象层 (`main/boards/common`) 分析

`main/boards/common/` 目录包含了一系列可被多种硬件开发板复用的通用组件和基类。这些代码构成了项目硬件抽象层的基础，旨在提供一致的接口来操作不同的硬件模块，如电源管理、网络连接、用户输入和显示等。

## 核心抽象类

### `Board` (board.h / board.cc)
- **职责**: 定义了所有具体开发板实现必须遵循的通用接口。它是一个抽象基类，不能被直接实例化。
- **关键设计**:
    - 使用 `Board::GetInstance()` 单例模式，通过 `create_board()` 工厂函数（由具体板型实现）创建唯一的实例。
    - 定义了一系列纯虚函数或虚函数，如 `GetBoardType()`, `GetAudioCodec()`, `GetDisplay()`, `StartNetwork()` 等，强制子类提供具体实现。
    - 提供了生成设备唯一标识符（UUID）和组装系统信息JSON的通用功能。

### `I2cDevice` (i2c_device.h / i2c_device.cc)
- **职责**: 为基于I2C通信的设备提供一个简单的基类。
- **功能**: 封装了 `i2c_master_bus_add_device` 的初始化过程，并提供了 `WriteReg` 和 `ReadReg` 等受保护的方法，简化了与I2C设备寄存器的交互。`Axp2101` 和 `Sy6970` 都继承自此类。

### `Camera` (camera.h)
- **职责**: 定义了摄像头功能的抽象接口。
- **关键方法**:
    - `Capture()`: 捕获一帧图像。
    - `SetHMirror() / SetVFlip()`: 设置水平/垂直翻转。
    - `Explain()`: 将图像发送到AI服务进行分析。

## 电源与电池管理

### `AdcBatteryMonitor` (adc_battery_monitor.h / .cc)
- **职责**: 使用ADC（模数转换器）来监测电池电压，并估算电量和充电状态。
- **功能**:
    - 封装了 `adc_battery_estimation` 组件。
    - 通过分压电阻配置来读取电池电压。
    - 可选地通过一个GPIO引脚检测物理充电状态。
    - 提供 `GetBatteryLevel()` 和 `IsCharging()` 等接口。
    - 使用 `esp_timer` 定期检查状态变化，并提供回调。

### `Axp2101` (axp2101.h / .cc)
- **职责**: AXP2101电源管理芯片的驱动程序。
- **功能**: 提供了检测充电状态、读取电池电量、获取温度和关闭电源等功能。

### `Sy6970` (sy6970.h / .cc)
- **职责**: SY6970充电管理芯片的驱动程序。
- **功能**: 提供了检测充电状态、判断电源是否良好、读取电池电压和估算电量、以及关闭电源等功能。

### `PowerSaveTimer` (power_save_timer.h / .cc)
- **职责**: 一个用于管理CPU频率和轻度睡眠（Light Sleep）的定时器。
- **功能**:
    - 当设备在一段时间内处于空闲状态 (`Application::CanEnterSleepMode()` 返回 `true`)，它会降低CPU频率以节省功耗。
    - 提供了 `OnEnterSleepMode` 和 `OnExitSleepMode` 回调。
    - `WakeUp()` 方法可以被外部事件（如按键）调用，以恢复CPU性能。

### `SleepTimer` (sleep_timer.h / .cc)
- **职责**: 管理设备的轻度睡眠（Light Sleep）和深度睡眠（Deep Sleep）。
- **功能**:
    - 在设备空闲时触发，比 `PowerSaveTimer` 更进一步，会暂停LVGL渲染并调用 `esp_light_sleep_start()`。
    - 可以通过GPIO或定时器从Light Sleep中唤醒。
    - 也可以配置在更长时间的空闲后进入Deep Sleep。

## 网络连接

### `WifiBoard` (wifi_board.h / .cc)
- **职责**: 提供基于WiFi的通用网络连接实现。
- **功能**:
    - 封装了 `WifiStation` 和 `WifiConfigurationAp` 组件。
    - **启动流程**:
        1. 检查 `force_ap` 标志或NVS中是否无已存SSID。如果是，则进入配网模式。
        2. 否则，尝试连接已保存的WiFi。
        3. 如果连接失败，则自动进入配网模式。
    - **配网模式**:
        - 启动一个AP热点（SSID前缀为"Xiaozhi"）。
        - 启动一个Web服务器让用户配置WiFi。
        - （可选）启动声波配网 (`afsk_demod`)。
    - 提供了获取网络状态图标（如信号强度）的功能。

### `Ml307Board` (ml307_board.h / .cc)
- **职责**: 提供基于ML307 4G蜂窝模块的网络连接实现。
- **功能**:
    - 封装了 `AtModem` 组件。
    - **启动流程**:
        1. 通过AT指令检测并初始化ML307模块。
        2. 等待模块注册到蜂窝网络。
        3. 处理SIM卡错误、注册失败等情况，并通过UI提示用户。
    - 提供了获取运营商名称、信号强度（CSQ）、IMEI等蜂窝网络特定信息的功能。

### `DualNetworkBoard` (dual_network_board.h / .cc)
- **职责**: 一个高层级的板卡实现，它组合了 `WifiBoard` 和 `Ml307Board`，允许在两种网络模式之间切换。
- **功能**:
    - 在初始化时，根据NVS中的设置决定加载 `WifiBoard` 还是 `Ml307Board`。
    - 提供了 `SwitchNetworkType()` 方法，该方法会修改NVS设置并重启设备以切换网络模式。
    - 将所有 `Board` 接口的调用委托给当前活动的底层板卡实例 (`current_board_`)。

### `afsk_demod` (afsk_demod.h / .cc)
- **职责**: 实现声波配网的音频解调逻辑。
- **技术**:
    - 使用音频频移键控（AFSK）技术，通过音频信号传输WiFi的SSID和密码。
    - `FrequencyDetector` 类基于Goertzel算法，用于高效检测特定的"Mark"（1800Hz）和"Space"（1500Hz）频率。
    - `AudioSignalProcessor` 使用两个 `FrequencyDetector` 来处理音频流，并将其转换为0和1的概率流。
    - `AudioDataBuffer` 是一个状态机，负责从概率流中解码出完整的数据包（包括起始/结束符和校验和），最终得到文本格式的WiFi凭据。

## 用户输入与外设

### `Button` (button.h / .cc)
- **职责**: 通用的按键处理类。
- **功能**:
    - 封装了 `iot_button` 组件。
    - 支持GPIO按键和ADC按键（`AdcButton`）。
    - 提供了 `OnClick`, `OnDoubleClick`, `OnLongPress` 等事件的回调注册机制。
    - `PowerSaveButton` 是一个特殊子类，用于配置可从Deep Sleep唤醒系统的按键。

### `Knob` (knob.h / .cc)
- **职责**: 旋转编码器（旋钮）的驱动。
- **功能**:
    - 封装了 `iot_knob` 组件。
    - 提供了 `OnRotate` 回调，参数为布尔值，表示向右（true）或向左（false）旋转。

### `Backlight` (backlight.h / .cc)
- **职责**: 屏幕背光控制的基类。
- **功能**:
    - `PwmBacklight` 是其子类，使用LEDC（PWM控制器）实现亮度调节。
    - 实现了平滑的亮度过渡效果，通过 `esp_timer` 在短时间内逐步改变亮度值。
    - `RestoreBrightness()` 方法可以从NVS中恢复上次保存的亮度设置。

### `Esp32Camera` (esp32_camera.h / .cc)
- **职责**: `Camera` 接口的具体实现，适用于ESP32系列芯片。
- **技术**:
    - 深度封装了 `esp_video` V4L2（Video4Linux2）驱动框架。
    - **初始化**:
        - 探测并打开视频设备（如DVP、MIPI-CSI）。
        - 查询摄像头支持的像素格式，并自动选择最优格式（优先YUV422P或RGB）。
        - 请求并内存映射（mmap）摄像头驱动的缓冲区。
        - 启动视频流 (`VIDIOC_STREAMON`)。
    - **捕获 (`Capture`)**:
        - 从驱动队列中取出（DQBUF）一个已填充的缓冲区。
        - 将图像数据复制到PSRAM中。
        - （可选）支持通过 `esp_imgfx` 或PPA（像素处理加速器）进行硬件旋转。
        - 将缓冲区重新入队（QBUF）以供下次捕获。
        - 将捕获的图像（可能经过格式转换）更新到LVGL预览上。
    - **AI解释 (`Explain`)**:
        - 在一个单独的线程中将捕获的图像编码为JPEG格式。
        - 使用HTTP分块传输（chunked transfer）将JPEG数据流式上传到服务器，以优化内存使用。

## 系统与其他

### `SystemReset` (system_reset.h / .cc)
- **职责**: 提供通过GPIO引脚在设备启动时触发系统重置的功能。
- **功能**:
    - `CheckButtons()` 方法在启动时被调用。
    - 如果 `reset_nvs_pin` 被拉低，则擦除整个NVS分区。
    - 如果 `reset_factory_pin` 被拉低，则擦除NVS和OTA数据分区，使设备恢复到出厂固件。

### `PressToTalkMcpTool` (press_to_talk_mcp_tool.h / .cc)
- **职责**: 一个可复用的MCP（多端控制协议）工具，用于在"按住说话"和"单击说话"两种模式间切换。
- **功能**:
    - 向MCP服务器注册一个名为 `self.set_press_to_talk` 的工具。
    - 当工具被调用时，它会更新NVS中的 `press_to_talk` 设置。
    - 应用逻辑可以查询 `IsPressToTalkEnabled()` 来决定按键的具体行为。

### `lamp_controller.h`
- **职责**: 一个简单的示例，演示如何通过MCP工具控制一个连接到GPIO的灯。
- **功能**: 注册了 `self.lamp.turn_on`, `self.lamp.turn_off`, `self.lamp.get_state` 三个工具，用于控制GPIO高低电平并返回状态。
