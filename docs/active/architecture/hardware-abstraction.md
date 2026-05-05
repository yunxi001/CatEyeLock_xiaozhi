# 硬件抽象层

**最后更新**: 2026-05-05

---

## 概述

硬件抽象层（HAL）通过 Board 类提供统一的硬件访问接口，隔离不同开发板的硬件差异。

---

## Board 类层次结构

```
Board (抽象基类)
  │
  ├─> WifiBoard (WiFi 连接)
  │     │
  │     └─> CompactWifiBoardS3Cam (具体实现)
  │
  ├─> Ml307Board (4G 连接)
  │
  └─> DualNetworkBoard (WiFi + 4G)
```

---

## 核心接口

### Board 基类

```cpp
class Board {
public:
    static Board* GetInstance();

    // 音频
    virtual AudioCodec* GetAudioCodec() = 0;

    // 显示
    virtual Display* GetDisplay() = 0;
    virtual Backlight* GetBacklight() = 0;

    // LED
    virtual Led* GetLed() = 0;

    // 网络
    virtual Network* GetNetwork() = 0;

    // 摄像头
    virtual Camera* GetCamera() = 0;

    // 锁控
    virtual LockControlService* GetLockControl() = 0;
};
```

---

## 添加新开发板

### 1. 创建开发板目录

```bash
mkdir main/boards/my-custom-board
```

### 2. 创建配置文件

**config.h**:

```c
#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

// 音频配置
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_10
// ...

// 显示配置
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  240
// ...

// 锁控配置
#define LOCK_UART_PORT      UART_NUM_1
#define LOCK_UART_TX_PIN    GPIO_NUM_3
#define LOCK_UART_RX_PIN    GPIO_NUM_14
// ...

#endif
```

### 3. 实现 Board 类

**my_custom_board.cc**:

```cpp
#include "wifi_board.h"
#include "config.h"

class MyCustomBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    LcdDisplay* display_;
    LockControlService* lock_control_;

public:
    MyCustomBoard() {
        InitializeI2c();
        InitializeSpi();
        InitializeDisplay();
        InitializeLockControl();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(...);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual LockControlService* GetLockControl() override {
        return lock_control_;
    }

    // ... 其他接口实现
};

// 注册开发板
DECLARE_BOARD(MyCustomBoard);
```

### 4. 添加到构建系统

**main/Kconfig.projbuild**:

```kconfig
config BOARD_TYPE_MY_CUSTOM_BOARD
    bool "My Custom Board"
    depends on IDF_TARGET_ESP32S3
```

**main/CMakeLists.txt**:

```cmake
elseif(CONFIG_BOARD_TYPE_MY_CUSTOM_BOARD)
    set(BOARD_TYPE "my-custom-board")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
endif()
```

---

## 硬件组件

### 1. 音频编解码器

支持的编解码器：

- ES8311 (常用)
- ES7210 (麦克风阵列)
- ES8388
- AW88298 (功放)

### 2. 显示屏

支持的显示屏驱动：

- ST7789 (SPI)
- ILI9341 (SPI)
- GC9A01 (SPI)
- SH8601 (QSPI)

### 3. 摄像头

支持的摄像头：

- OV2640
- OV3660
- OV5640

### 4. 网络

支持的网络方式：

- WiFi (ESP32 内置)
- 4G (ML307 模块)
- WiFi + 4G (双网络)

---

## 配置选项

### 开发板选择

```bash
idf.py menuconfig
# Xiaozhi Assistant → Board Type
```

### 引脚配置

在 `config.h` 中定义所有引脚：

```c
// I2S 引脚
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_10
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_12
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_8
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_7
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_11

// I2C 引脚
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_0
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_1

// SPI 引脚
#define DISPLAY_SPI_SCK_PIN     GPIO_NUM_3
#define DISPLAY_SPI_MOSI_PIN    GPIO_NUM_5
#define DISPLAY_DC_PIN          GPIO_NUM_6
#define DISPLAY_SPI_CS_PIN      GPIO_NUM_4

// UART 引脚
#define LOCK_UART_TX_PIN    GPIO_NUM_3
#define LOCK_UART_RX_PIN    GPIO_NUM_14
```

---

## 调试技巧

### 1. 查看当前开发板

```cpp
ESP_LOGI(TAG, "Board: %s", Board::GetInstance()->GetName());
```

### 2. 检查硬件可用性

```cpp
auto camera = Board::GetInstance()->GetCamera();
if (camera && camera->IsAvailable()) {
    ESP_LOGI(TAG, "摄像头可用");
} else {
    ESP_LOGE(TAG, "摄像头不可用");
}
```

### 3. 测试硬件功能

```cpp
// 测试显示屏
auto display = Board::GetInstance()->GetDisplay();
if (display) {
    display->Clear();
    display->DrawText(0, 0, "Hello World");
}

// 测试 LED
auto led = Board::GetInstance()->GetLed();
if (led) {
    led->SetColor(255, 0, 0);  // 红色
}

// 测试锁控
auto lock_control = Board::GetInstance()->GetLockControl();
if (lock_control) {
    lock_control->SendBeep(1, BeepFreq::SHORT);
}
```

---

## 相关文档

- [自定义开发板指南](../guides/custom-board.md)
- [系统架构概览](system-overview.md)

---

**最后更新**: 2026-05-05
