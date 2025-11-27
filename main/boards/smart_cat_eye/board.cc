/**
 * @file board.cc
 * @brief smart_cat_eye 板级的具体实现。
 * @author 78 (modified by Gemini)
 * @date 2024-11-25
 * 
 * @details
 * 该文件定义了 `SmartCatEyeBoard` 类，它继承自 `WifiBoard`，
 * 实现了智能猫眼项目的硬件初始化逻辑。
 * 主要复用了原有的摄像头和显示屏功能，并为后续添加STM32通信、低功耗管理等功能提供了基础。
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
#include "stm32_controller.h" // 包含 STM32 控制器头文件
#include "freertos/task.h"    // 包含 FreeRTOS 任务相关头文件

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
 
#define TAG "SmartCatEyeBoard"
#define VIDEO_STREAM_DELAY_MS 100 // 视频流每帧延迟，单位毫秒 (10 FPS)

// Forward declaration
class SmartCatEyeBoard;

// 视频音频流任务的参数结构体
struct video_audio_stream_task_args {
    Protocol* protocol;
    Camera* camera;
    AudioService* audio_service;
};

// 视频音频流任务
static void video_audio_stream_task(void* pvParameters) {
    video_audio_stream_task_args* args = (video_audio_stream_task_args*)pvParameters;
    Protocol* protocol = args->protocol;
    Camera* camera = args->camera;
    AudioService* audio_service = args->audio_service;

    ESP_LOGI(TAG, "Video/Audio stream task started.");

    while (protocol->IsStreamingAvMode()) {
        // --- 视频捕获与发送 ---
        if (camera && camera->Capture()) {
            // 假设 Capture() 成功后，可以通过 camera->GetLastFrame() 获取 JPEG 数据
            std::vector<uint8_t> jpeg_data = camera->GetLastFrame(); // 需要相机提供获取最后一帧数据的方法
            if (!jpeg_data.empty()) {
                protocol->SendBinary(jpeg_data.data(), jpeg_data.size());
                // ESP_LOGD(TAG, "Sent video frame: %zu bytes", jpeg_data.size());
            }
        }

        // --- 音频捕获与发送 ---
        // 从 AudioService 获取待发送的音频包
        if (audio_service) {
            auto audio_packet = audio_service->PopPacketFromSendQueue();
            if (audio_packet) {
                // 假设 SendBinary 可以直接发送 AudioStreamPacket 的 payload
                protocol->SendBinary(audio_packet->payload.data(), audio_packet->payload.size());
                // ESP_LOGD(TAG, "Sent audio frame: %zu bytes", audio_packet->payload.size());
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(VIDEO_STREAM_DELAY_MS));
    }

    ESP_LOGI(TAG, "Video/Audio stream task stopped.");
    delete args; // 释放参数结构体
    vTaskDelete(NULL);
}


/**
 * @brief 初始化智能猫眼板特定的 MCP 工具。
 * @param board 指向 SmartCatEyeBoard 实例的指针。
 */
static void InitializeTools(SmartCatEyeBoard* board) {
    auto& mcp_server = McpServer::GetInstance();

    // 工具1: door.unlock (开锁)
    mcp_server.AddTool("door.unlock", 
        "Unlocks the door.",
        PropertyList(),
        [board](const PropertyList& properties) -> ReturnValue {
            // 发送开锁指令到 STM32
            Stm32Controller::GetInstance().sendCommand("unlock", nullptr);
            ESP_LOGI(TAG, "MCP tool 'door.unlock' called, command sent to STM32.");
            return true; 
        });

    // 工具2: vision.recognize_face (人脸识别)
    auto camera = board->GetCamera();
    if (camera) {
        mcp_server.AddTool("vision.recognize_face",
            "Recognize a face at the door. Takes a photo and sends it for analysis.",
            PropertyList({
                // 将 'question' 设置为可选参数
                Property("question", kPropertyTypeString, "A question about the photo, can be empty.", true, "")
            }),
            [camera](const PropertyList& properties) -> ReturnValue {
                // 降低任务优先级以进行摄像头捕捉，避免影响其他实时任务
                TaskPriorityReset priority_reset(1);
                if (!camera->Capture()) {
                    throw std::runtime_error("Failed to capture photo for face recognition");
                }
                ESP_LOGI(TAG, "Photo captured for face recognition.");
                
                // 如果调用者提供了问题，则使用该问题；否则，使用默认问题。
                auto question = properties.Has("question") ? properties["question"].value<std::string>() : "Is there a person in this photo?";
                
                // 复用现有的 Explain 机制将图片上传到服务器并获取结果
                return camera->Explain(question);
            });
    }

    // 工具3: stream.start_video (启动音视频流)
    mcp_server.AddTool("stream.start_video",
        "Starts a real-time audio and video stream to the server.",
        PropertyList(),
        [board](const PropertyList& properties) -> ReturnValue {
            Protocol* protocol = Application::GetInstance().GetProtocol();
            Camera* camera = board->GetCamera();
            AudioService* audio_service = &Application::GetInstance().GetAudioService();

            if (!protocol || !camera || !audio_service) {
                throw std::runtime_error("Required services not available for streaming.");
            }

            if (protocol->IsStreamingAvMode()) {
                ESP_LOGW(TAG, "Already in AV streaming mode.");
                return true;
            }

            protocol->SetStreamingMode(true);

            // 为任务动态分配参数
            video_audio_stream_task_args* args = new video_audio_stream_task_args{
                .protocol = protocol,
                .camera = camera,
                .audio_service = audio_service
            };

            // 创建并启动音视频流任务
            xTaskCreate(video_audio_stream_task, "av_stream_task", 4096 * 2, args, 5, NULL); // 提高任务优先级和栈空间
            ESP_LOGI(TAG, "MCP tool 'stream.start_video' called. Starting AV streaming task.");
            return true;
        });

    // 工具4: stream.stop_video (停止音视频流)
    mcp_server.AddTool("stream.stop_video",
        "Stops the real-time audio and video stream to the server.",
        PropertyList(),
        [board](const PropertyList& properties) -> ReturnValue {
            Protocol* protocol = Application::GetInstance().GetProtocol();
            if (!protocol) {
                throw std::runtime_error("Protocol service not available.");
            }

            if (!protocol->IsStreamingAvMode()) {
                ESP_LOGW(TAG, "Not in AV streaming mode, no need to stop.");
                return true;
            }

            protocol->SetStreamingMode(false);
            ESP_LOGI(TAG, "MCP tool 'stream.stop_video' called. Signaled AV streaming task to stop.");
            // 任务会自动检测到 streaming_av_mode_ 变为 false 并退出
            return true;
        });
}
