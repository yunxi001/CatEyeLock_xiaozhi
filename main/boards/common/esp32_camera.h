#pragma once
#include "sdkconfig.h"

// 仅在非 ESP32 目标上编译，因为原始 ESP32 缺少一些必要的驱动和库
#ifndef CONFIG_IDF_TARGET_ESP32
#include <lvgl.h>
#include <thread>
#include <memory>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "camera.h"
#include "jpg/image_to_jpeg.h"
#include "esp_video_init.h"

/**
 * @struct JpegChunk
 * @brief 用于在JPEG编码和上传过程中传递数据块的结构体。
 */
struct JpegChunk {
    uint8_t* data; ///< 指向数据块内存的指针。
    size_t len;    ///< 数据块的长度（字节）。
};

/**
 * @class Esp32Camera
 * @brief Camera 接口的ESP32平台具体实现。
 *
 * @details
 * 此类封装了 `esp_video` 驱动框架（基于V4L2），提供了从摄像头硬件
 * (如DVP、MIPI-CSI接口的摄像头) 捕获图像、进行预处理（旋转、格式转换）、
 * 在LVGL上显示预览，以及将图像编码并上传至AI服务器进行分析的全套功能。
 */
class Esp32Camera : public Camera {
private:
    /**
     * @struct FrameBuffer
     * @brief 存储一帧图像数据的内部结构体。
     */
    struct FrameBuffer {
        uint8_t *data = nullptr;      ///< 指向存储在PSRAM中的图像数据。
        size_t len = 0;               ///< 图像数据的总字节数。
        uint16_t width = 0;           ///< 图像宽度。
        uint16_t height = 0;          ///< 图像高度。
        v4l2_pix_fmt_t format = 0;    ///< 图像的像素格式 (V4L2_PIX_FMT_*)。
    } frame_;

    /// @brief 摄像头传感器输出的原始像素格式。
    v4l2_pix_fmt_t sensor_format_ = 0;

#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    uint16_t sensor_width_ = 0;       ///< 传感器原始宽度，用于旋转计算。
    uint16_t sensor_height_ = 0;      ///< 传感器原始高度，用于旋转计算。
#endif

    /// @brief V4L2视频设备的文件描述符。
    int video_fd_ = -1;

    /// @brief 标志位，表示视频流是否已开启。
    bool streaming_on_ = false;

    /**
     * @struct MmapBuffer
     * @brief 描述一个内存映射（mmap）的缓冲区。
     */
    struct MmapBuffer {
        void *start = nullptr;  ///< 映射到用户空间的起始地址。
        size_t length = 0;      ///< 缓冲区的长度。
    };

    /// @brief 存储所有mmap缓冲区的向量。
    std::vector<MmapBuffer> mmap_buffers_;

    /// @brief AI视觉解释服务的URL。
    std::string explain_url_;
    /// @brief AI视觉解释服务的认证令牌。
    std::string explain_token_;

    /// @brief 用于在后台执行JPEG编码的线程对象。
    std::thread encoder_thread_;

public:
    /**
     * @brief 构造函数，根据提供的配置初始化摄像头。
     * @param config esp_video 初始化配置，包含接口类型、引脚、时钟等信息。
     */
    Esp32Camera(const esp_video_init_config_t& config);

    /**
     * @brief 析构函数，释放所有资源。
     */
    ~Esp32Camera();

    /**
     * @brief 设置AI视觉解释服务的URL和认证令牌。
     * @param url 服务器的URL地址。
     * @param token 用于认证的Bearer Token。
     */
    virtual void SetExplainUrl(const std::string& url, const std::string& token) override;

    /**
     * @brief 捕获一帧图像并更新内部帧缓冲区，用于预览。
     * @return bool 如果捕获成功，返回true。
     */
    virtual bool Capture() override;

    /**
     * @brief 设置图像水平镜像。
     * @param enabled true表示开启镜像，false表示关闭。
     * @return bool 如果设置成功，返回true。
     */
    virtual bool SetHMirror(bool enabled) override;

    /**
     * @brief 设置图像垂直翻转。
     * @param enabled true表示开启翻转，false表示关闭。
     * @return bool 如果设置成功，返回true。
     */
    virtual bool SetVFlip(bool enabled) override;

    /**
     * @brief 将当前捕获的图像编码为JPEG，并上传到服务器进行AI分析。
     * @param question 用户提出的关于图像内容的问题。
     * @return std::string 服务器返回的JSON格式的分析结果。
     */
    virtual std::string Explain(const std::string& question) override;

    /**
     * @brief 捕获一帧JPEG图像用于监控模式
     * @param jpeg_data 输出参数，指向JPEG数据的指针
     * @param jpeg_size 输出参数，JPEG数据的大小
     * @param quality JPEG压缩质量 (1-100)
     * @return bool 如果捕获成功返回true
     */
    bool CaptureJpeg(uint8_t** jpeg_data, size_t* jpeg_size, int quality = 80);

    /**
     * @brief 高效捕获一帧图像用于视频流（监控模式专用）
     * @details 与 Capture() 不同，此函数：
     *          - 只捕获一帧（不丢弃前两帧）
     *          - 不在屏幕上显示预览
     *          - 专为高帧率视频流优化
     * @return bool 如果捕获成功返回true
     */
    bool CaptureForStream();

    /**
     * @brief 检查摄像头是否已初始化并可用
     * @return bool 如果摄像头可用返回true
     */
    bool IsAvailable() const;

    /**
     * @brief 获取当前帧缓冲区的宽度
     * @return uint16_t 帧宽度
     */
    uint16_t GetFrameWidth() const { return frame_.width; }

    /**
     * @brief 获取当前帧缓冲区的高度
     * @return uint16_t 帧高度
     */
    uint16_t GetFrameHeight() const { return frame_.height; }
};

#endif // ndef CONFIG_IDF_TARGET_ESP32