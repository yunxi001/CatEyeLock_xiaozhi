/**
 * @file compact_wifi_board_s3cam.cc
 * @brief bread-compact-wifi-s3cam 板级的具体实现。
 * @author 78
 * @date 2024-07-20
 * 
 * @details
 * 该文件定义了 `CompactWifiBoardS3Cam` 类，它继承自 `WifiBoard`，
 * 实现了特定于 "面包板 + ESP32-S3-CAM + LCD" 硬件组合的初始化逻辑。
 * 主要包括：
 * 1. SPI 总线初始化 (用于屏幕)
 * 2. LCD 显示屏初始化
 * 3. DVP 摄像头初始化
 * 4. 板载按钮初始化
 * 5. 提供获取 LED、音频编解码器、显示屏、背光和摄像头实例的接口。
 */

#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "esp32_camera.h"
#include "lock_control/lock_control.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>

// 根据 menuconfig 中选择的 LCD 类型，包含对应的驱动头文件
#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
// GC9A01/GC9107 屏幕的厂商特定初始化指令序列
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif
 
#define TAG "CompactWifiBoardS3Cam"

/**
 * @brief CompactWifiBoardS3Cam 类的定义，代表一个带摄像头的面包板 WiFi 开发板。
 */
class CompactWifiBoardS3Cam : public WifiBoard {
private:
    Button boot_button_;      // GPIO0 上的启动/功能按钮
    LcdDisplay* display_;     // LCD 显示屏对象指针
    Esp32Camera* camera_;     // 摄像头对象指针
    xiaozhi::LockControlService* lock_control_; // 锁控服务对象指针

    /**
     * @brief 初始化用于 LCD 的 SPI 总线。
     */
    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC; // MISO 不使用
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t); // 最大传输大小
        // 初始化 SPI3 主机，使用 DMA
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    /**
     * @brief 初始化 LCD 显示屏。
     */
    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        
        // 1. 配置并创建 SPI IO 句柄
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000; // SPI 时钟频率 40MHz
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 2. 配置并创建 LCD 驱动面板句柄
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;

        // 根据 menuconfig 的选择，实例化不同的 LCD 驱动
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        // 如果是 GC9A01，则应用特定的厂商初始化指令
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };        
#else
        // 默认使用 ST7789 驱动
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        
        // 3. 初始化并配置 LCD 面板
        esp_lcd_panel_reset(panel); // 复位 LCD
        esp_lcd_panel_init(panel);  // 初始化
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR); // 设置颜色反转
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);           // 交换 XY 坐标
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y); // 设置镜像

#ifdef  LCD_TYPE_GC9A01_SERIAL
        // 注入厂商特定配置
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        // 4. 创建 LcdDisplay 实例以供上层应用使用
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    /**
     * @brief 初始化 DVP 摄像头。
     */
    void InitializeCamera() {
        // 1. 配置摄像头 DVP 接口的引脚
        // DVP (Digital Video Port) 是摄像头与 ESP32 之间的并行数据接口
        static esp_cam_ctlr_dvp_pin_config_t dvp_pin_config = {
            .data_width = CAM_CTLR_DATA_WIDTH_8, // 设置数据总线宽度为8位
            .data_io = {
                // 配置8位数据线引脚映射
                [0] = CAMERA_PIN_D0, [1] = CAMERA_PIN_D1, [2] = CAMERA_PIN_D2, [3] = CAMERA_PIN_D3,
                [4] = CAMERA_PIN_D4, [5] = CAMERA_PIN_D5, [6] = CAMERA_PIN_D6, [7] = CAMERA_PIN_D7,
            },
            .vsync_io = CAMERA_PIN_VSYNC,  // 垂直同步信号引脚
            .de_io = CAMERA_PIN_HREF,      // 数据有效信号引脚(HREF)
            .pclk_io = CAMERA_PIN_PCLK,    // 像素时钟信号引脚
            .xclk_io = CAMERA_PIN_XCLK,    // 外部时钟输出引脚(用于驱动摄像头时钟)
        };

        // 2. 配置摄像头 SCCB (I2C) 接口
        // SCCB (Serial Camera Control Bus) 是用于配置摄像头寄存器的串行接口，基于I2C协议
        esp_video_init_sccb_config_t sccb_config = {
            .init_sccb = true,             // 启用SCCB接口初始化
            .i2c_config = {
                .port = 0,                 // 使用I2C端口0
                .scl_pin = CAMERA_PIN_SIOC, // I2C时钟线引脚
                .sda_pin = CAMERA_PIN_SIOD, // I2C数据线引脚
            },
            .freq = 100000, // I2C 时钟频率设置为100kHz(标准模式)
        };

        // 3. 组合 DVP 和 SCCB 配置
        // 将DVP接口和SCCB接口配置整合到摄像头控制器配置中
        esp_video_init_dvp_config_t dvp_config = {
            .sccb_config = sccb_config,    // 包含上面定义的SCCB配置
            .reset_pin = CAMERA_PIN_RESET, // 摄像头复位引脚
            .pwdn_pin = CAMERA_PIN_PWDN,   // 摄像头电源关闭引脚
            .dvp_pin = dvp_pin_config,     // 包含上面定义的DVP引脚配置
            .xclk_freq = XCLK_FREQ_HZ,     // 外部时钟频率
        };

        // 4. 创建视频初始化总配置
        // 构建完整的摄像头初始化配置结构体
        esp_video_init_config_t video_config = {
            .dvp = &dvp_config,            // 指向DVP配置的指针
        };

        // 5. 创建 Esp32Camera 实例
        // 使用上述配置创建摄像头对象实例
        camera_ = new Esp32Camera(video_config);
        
        // 设置摄像头水平不镜像
        // false表示图像不会左右翻转
        camera_->SetHMirror(false); 
    }

    /**
     * @brief 初始化板载按钮。
     */
    void InitializeButtons() {
        // 为启动按钮注册一个点击事件回调
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            // 如果设备正在启动且 WiFi 未连接，则长按此按钮可重置 WiFi 配置
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            // 切换聊天状态（开始/停止录音）
            app.ToggleChatState();
        });
    }

    /**
     * @brief 初始化锁控服务。
     */
    void InitializeLockControl() {
        lock_control_ = new xiaozhi::LockControlService();
        bool success = lock_control_->Start(LOCK_UART_PORT, LOCK_UART_TX_PIN, LOCK_UART_RX_PIN);
        if (!success) {
            ESP_LOGE(TAG, "Failed to start lock control service");
        } else {
            ESP_LOGI(TAG, "Lock control service started successfully");
        }
    }

public:
    /**
     * @brief CompactWifiBoardS3Cam 类的构造函数。
     * 在这里按顺序调用各个外设的初始化函数。
     */
    CompactWifiBoardS3Cam() :
        boot_button_(BOOT_BUTTON_GPIO), lock_control_(nullptr) {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeCamera();
        InitializeLockControl();
        // 如果定义了背光引脚，则恢复上次保存的亮度
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
    }

    /**
     * @brief 获取板载 LED 的实例。
     * @return Led* 指向 LED 实例的指针。
     */
    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    /**
     * @brief 获取音频编解码器的实例。
     * @return AudioCodec* 指向音频编解码器实例的指针。
     */
    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        // 单工模式：麦克风和扬声器使用不同的 I2S 引脚
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        // 双工模式：麦克风和扬声器共享部分 I2S 引脚
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    /**
     * @brief 获取显示屏的实例。
     * @return Display* 指向显示屏实例的指针。
     */
    virtual Display* GetDisplay() override {
        return display_;
    }

    /**
     * @brief 获取背光控制的实例。
     * @return Backlight* 指向背光控制实例的指针。
     */
    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

    /**
     * @brief 获取摄像头的实例。
     * @return Camera* 指向摄像头实例的指针。
     */
    virtual Camera* GetCamera() override {
        return camera_;
    }

    /**
     * @brief 获取锁控服务的实例。
     * @return xiaozhi::LockControlService* 指向锁控服务实例的指针。
     */
    xiaozhi::LockControlService* GetLockControl() override {
        return lock_control_;
    }
};

// 宏，用于在板型列表中声明并注册该板型
DECLARE_BOARD(CompactWifiBoardS3Cam);
