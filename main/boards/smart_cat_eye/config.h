#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// ========================== 音频配置 ==========================
#define AUDIO_INPUT_SAMPLE_RATE  16000 // 音频输入（麦克风）采样率
#define AUDIO_OUTPUT_SAMPLE_RATE 24000 // 音频输出（扬声器）采样率

// 定义 I2S 的工作模式。Simplex 为单工模式，麦克风和扬声器使用独立的 I2S 时钟线。
// 如果使用 Duplex (双工) I2S 模式，请注释下面一行。
#define AUDIO_I2S_METHOD_SIMPLEX
//使用屏幕为ST7789的240x320
#define CONFIG_LCD_ST7789_240X320
// ========================== 音频引脚定义 ==========================
#ifdef AUDIO_I2S_METHOD_SIMPLEX
// --- 单工 I2S 模式引脚定义 ---
// 麦克风 I2S 引脚
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_1  // Word Select
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_2  // Serial Clock
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_42 // Serial Data In

// 扬声器 I2S 引脚
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_39 // Serial Data Out
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_40 // Bit Clock
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_41 // Left/Right Clock

#else
// --- 双工 I2S 模式引脚定义 ---
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_4  // Word Select (LRCK)
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_5  // Bit Clock (SCK)
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6  // Serial Data In (MIC)
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_7  // Serial Data Out (Speaker)

#endif


// ========================== 板载元件引脚定义 ==========================


#define BUILTIN_LED_GPIO        GPIO_NUM_48 // 板载 LED 引脚


#define BOOT_BUTTON_GPIO        GPIO_NUM_0  // 启动/功能按钮引脚 (Strapping Pin)


#define TOUCH_BUTTON_GPIO       GPIO_NUM_NC // 触摸按钮 (未连接)


#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC // 音量加 (未连接)


#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC // 音量减 (未连接)





// ======================= 与STM32通信引脚定义 (使用UART唤醒) =======================


#define STM32_UART_PORT         UART_NUM_1  // 与STM32通信所用的UART端口号


#define STM32_UART_TX_PIN       GPIO_NUM_3  // TX -> STM32 RX


#define STM32_UART_RX_PIN       GPIO_NUM_14 // RX <- STM32 TX (从此引脚唤醒)





// ========================== 摄像头 (DVP 接口) 引脚定义 ==========================
#define CAMERA_PIN_D0 GPIO_NUM_11      // 摄像头数据引脚 0
#define CAMERA_PIN_D1 GPIO_NUM_9       // 摄像头数据引脚 1
#define CAMERA_PIN_D2 GPIO_NUM_8       // 摄像头数据引脚 2
#define CAMERA_PIN_D3 GPIO_NUM_10      // 摄像头数据引脚 3
#define CAMERA_PIN_D4 GPIO_NUM_12      // 摄像头数据引脚 4
#define CAMERA_PIN_D5 GPIO_NUM_18      // 摄像头数据引脚 5
#define CAMERA_PIN_D6 GPIO_NUM_17      // 摄像头数据引脚 6
#define CAMERA_PIN_D7 GPIO_NUM_16      // 摄像头数据引脚 7
#define CAMERA_PIN_XCLK GPIO_NUM_15    // 摄像头外部时钟 (XCLK)
#define CAMERA_PIN_PCLK GPIO_NUM_13    // 摄像头像素时钟 (PCLK)
#define CAMERA_PIN_VSYNC GPIO_NUM_6    // 垂直同步信号
#define CAMERA_PIN_HREF GPIO_NUM_7     // 水平参考信号
#define CAMERA_PIN_SIOC GPIO_NUM_5     // SCCB (I2C) 时钟
#define CAMERA_PIN_SIOD GPIO_NUM_4     // SCCB (I2C) 数据
#define CAMERA_PIN_PWDN GPIO_NUM_NC    // 电源使能 (未连接)
#define CAMERA_PIN_RESET GPIO_NUM_NC   // 复位 (未连接)
#define XCLK_FREQ_HZ 20000000           // 摄像头外部时钟频率 (20MHz)


// ========================== 显示屏 (SPI 接口) 引脚定义 ==========================
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_38 // 背光控制引脚
#define DISPLAY_MOSI_PIN      GPIO_NUM_20 // SPI MOSI (数据)
#define DISPLAY_CLK_PIN       GPIO_NUM_19 // SPI CLK (时钟)
#define DISPLAY_DC_PIN        GPIO_NUM_47 // 数据/命令控制
#define DISPLAY_RST_PIN       GPIO_NUM_21 // 复位
#define DISPLAY_CS_PIN        GPIO_NUM_45 // 片选


// =================================================================================
// ========================== 多种 LCD 屏幕适配 =====================================
// =================================================================================
// 以下配置块使用 menuconfig 中选择的屏幕型号 (CONFIG_LCD_...) 来定义屏幕的物理参数和驱动行为。
// 只有被选中的屏幕型号所对应的 #ifdef 块会参与编译。

#ifdef CONFIG_LCD_ST7789_240X320
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_240X320_NO_IPS
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_170X320
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   170
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  35
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_172X320
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   172
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  34
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_240X280
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  280
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  20
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_240X240
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_240X240_7PIN
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 3
#endif

#ifdef CONFIG_LCD_ST7789_240X135
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  135
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY true
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  40
#define DISPLAY_OFFSET_Y  53
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7735_128X160
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  160
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7735_128X128
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  128
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR  false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  32
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7796_320X480
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  480
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7796_320X480_NO_IPS
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  480
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ILI9341_240X320
#define LCD_TYPE_ILI9341_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ILI9341_240X320_NO_IPS
#define LCD_TYPE_ILI9341_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_GC9A01_240X240
#define LCD_TYPE_GC9A01_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_CUSTOM
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif


// ========================== 其他定义 ==========================
// 一个用于 MCP 协议测试的 GPIO，用于控制一个灯
#define LAMP_GPIO GPIO_NUM_14

#endif // _BOARD_CONFIG_H_
