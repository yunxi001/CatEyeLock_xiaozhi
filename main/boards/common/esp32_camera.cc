#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <unistd.h>
#include <errno.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>

#include "esp_imgfx_color_convert.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "linux/videodev2.h"

#include "board.h"
#include "display.h"
#include "esp32_camera.h"
#include "esp_jpeg_common.h"
#include "jpg/image_to_jpeg.h"
#include "jpg/jpeg_to_image.h"
#include "lvgl_display.h"
#include "mcp_server.h"
#include "system_info.h"

#ifdef CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL MAX(CONFIG_LOG_DEFAULT_LEVEL, ESP_LOG_DEBUG)
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
#include <esp_log.h> // should be after LOCAL_LOG_LEVEL definition

#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
#ifdef CONFIG_IDF_TARGET_ESP32P4
#include "driver/ppa.h"
#if defined(CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE_90)
#define IMAGE_ROTATION_ANGLE (PPA_SRM_ROTATION_ANGLE_270)
#elif defined(CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE_270)
#define IMAGE_ROTATION_ANGLE (PPA_SRM_ROTATION_ANGLE_90)
#else
#error "CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE is not set"
#endif  // angle
#else   // target
#include "esp_imgfx_rotate.h"
#if defined(CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE_90)
#define IMAGE_ROTATION_ANGLE (90)
#elif defined(CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE_270)
#define IMAGE_ROTATION_ANGLE (270)
#else
#error "CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE is not set"
#endif  // angle
#endif  // target
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE


#define TAG "Esp32Camera"

#if defined(CONFIG_CAMERA_SENSOR_SWAP_PIXEL_BYTE_ORDER) || defined(CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP)
#warning \
    "CAMERA_SENSOR_SWAP_PIXEL_BYTE_ORDER or CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP is enabled, which may cause image corruption in YUV422 format!"
#endif

#if CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
#define CAM_PRINT_FOURCC(pixelformat)       \
    char fourcc[5];                         \
    fourcc[0] = pixelformat & 0xFF;         \
    fourcc[1] = (pixelformat >> 8) & 0xFF;  \
    fourcc[2] = (pixelformat >> 16) & 0xFF; \
    fourcc[3] = (pixelformat >> 24) & 0xFF; \
    fourcc[4] = '\0';                       \
    ESP_LOGD(TAG, "FOURCC: '%c%c%c%c'", fourcc[0], fourcc[1], fourcc[2], fourcc[3]);

// for compatibility with old esp_video version
#ifndef MAP_FAILED
#define MAP_FAILED nullptr
#endif

__attribute__((weak)) esp_err_t esp_video_deinit(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
// end of for compatibility with old esp_video version

static void log_available_video_devices() {
    for (int i = 0; i < 50; i++) {
        char path[16];
        snprintf(path, sizeof(path), "/dev/video%d", i);
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            ESP_LOGD(TAG, "found video device: %s", path);
            close(fd);
        }
    }
}
#else
#define CAM_PRINT_FOURCC(pixelformat) (void)0;
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE

Esp32Camera::Esp32Camera(const esp_video_init_config_t& config) {
    // 1. 初始化底层硬件和驱动
    // 根据传入的配置（如DVP/MIPI的引脚、时钟等）初始化摄像头接口和I2C总线。
    if (esp_video_init(&config) != ESP_OK) {
        ESP_LOGE(TAG, "esp_video_init failed");
        return;
    }

#ifdef CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    // 2. 确定并打开V4L2设备文件
    // esp_video框架会根据启用的接口类型（DVP, MIPI-CSI, USB_UVC等）确定设备名称。
    const char* video_device_name = nullptr;
    if (false) { /* 用于构建 else if */
    }
#if CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE
    else if (config.csi != nullptr) {
        video_device_name = ESP_VIDEO_MIPI_CSI_DEVICE_NAME;
    }
#endif
#if CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE
    else if (config.dvp != nullptr) {
        video_device_name = ESP_VIDEO_DVP_DEVICE_NAME;
    }
#endif
#if CONFIG_ESP_VIDEO_ENABLE_HW_JPEG_VIDEO_DEVICE
    else if (config.jpeg != nullptr) {
        video_device_name = ESP_VIDEO_JPEG_DEVICE_NAME;
    }
#endif
#if CONFIG_ESP_VIDEO_ENABLE_SPI_VIDEO_DEVICE
    else if (config.spi != nullptr) {
        video_device_name = ESP_VIDEO_SPI_DEVICE_NAME;
    }
#endif
#if CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE
    else if (config.usb_uvc != nullptr) {
        video_device_name = ESP_VIDEO_USB_UVC_DEVICE_NAME(0);
    }
#endif

    if (video_device_name == nullptr) {
        ESP_LOGE(TAG, "no video device is enabled");
        return;
    }

    // 打开设备文件，获取文件描述符，后续所有操作都通过此描述符进行。
    video_fd_ = open(video_device_name, O_RDWR);
    if (video_fd_ < 0) {
        ESP_LOGE(TAG, "open %s failed, errno=%d(%s)", video_device_name, errno, strerror(errno));
#if CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
        log_available_video_devices();
#endif
        return;
    }

    // 3. 查询设备能力 (VIDIOC_QUERYCAP)
    // 获取驱动名称、版本、支持的操作等信息。
    struct v4l2_capability cap = {};
    if (ioctl(video_fd_, VIDIOC_QUERYCAP, &cap) != 0) {
        ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed, errno=%d(%s)", errno, strerror(errno));
        close(video_fd_);
        video_fd_ = -1;
        return;
    }
    ESP_LOGD(TAG, "VIDIOC_QUERYCAP: driver=%s, card=%s, bus_info=%s, version=0x%08lx, capabilities=0x%08lx, device_caps=0x%08lx",
        cap.driver, cap.card, cap.bus_info, cap.version, cap.capabilities, cap.device_caps);

    // 4. 格式协商 (Format Negotiation)
    // a. 获取当前默认格式 (VIDIOC_G_FMT)
    struct v4l2_format format = {};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd_, VIDIOC_G_FMT, &format) != 0) {
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed, errno=%d(%s)", errno, strerror(errno));
        close(video_fd_);
        video_fd_ = -1;
        return;
    }
    ESP_LOGD(TAG, "VIDIOC_G_FMT: pixelformat=0x%08lx, width=%ld, height=%ld", format.fmt.pix.pixelformat,
             format.fmt.pix.width, format.fmt.pix.height);
    CAM_PRINT_FOURCC(format.fmt.pix.pixelformat);

    // b. 枚举所有支持的格式并选择最优格式 (VIDIOC_ENUM_FMT)
    struct v4l2_format setformat = {};
    setformat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    sensor_width_ = format.fmt.pix.width;
    sensor_height_ = format.fmt.pix.height;
#endif
    setformat.fmt.pix.width = format.fmt.pix.width;
    setformat.fmt.pix.height = format.fmt.pix.height;

    struct v4l2_fmtdesc fmtdesc = {};
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index = 0;
    uint32_t best_fmt = 0;
    int best_rank = 1 << 30; // 初始为一个很大的数

    // 定义一个lambda函数，根据后续处理的便利性为不同像素格式评分。
    // 分数越小，优先级越高。
#if defined(CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE) && defined(CONFIG_SOC_PPA_SUPPORTED)
    // 如果启用了PPA硬件旋转，优先选择PPA支持的格式。
    auto get_rank = [](uint32_t fmt) -> int {
        switch (fmt) {
            case V4L2_PIX_FMT_RGB24:
                return 0;
            case V4L2_PIX_FMT_RGB565:
                return 1;
#ifdef CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_ENCODER
            case V4L2_PIX_FMT_YUV420:  // 软件 JPEG 编码器不支持 YUV420 格式
                return 2;
<<<<<<< HEAD
#else
                [[fallthrough]];
#endif
            default: return 1 << 29; // 不支持的格式
=======
        }
    };
#else
    // 默认情况下，优先选择YUV格式，因为它易于被软件JPEG编码器处理。
    auto get_rank = [](uint32_t fmt) -> int {
<<<<<<< HEAD
            case V4L2_PIX_FMT_YUV422P: return 0; // 注意：esp_video中此格式实际输出为YUYV
            case V4L2_PIX_FMT_RGB565: return 1;
            case V4L2_PIX_FMT_RGB24: return 2;
#ifdef CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_ENCODER
            case V4L2_PIX_FMT_YUV420: return 3;
#endif
            case V4L2_PIX_FMT_GREY: return 4;
            default: return 1 << 29; // 不支持的格式
=======
            case V4L2_PIX_FMT_YUV422P:
                return 10;
            case V4L2_PIX_FMT_RGB565:
                return 11;
            case V4L2_PIX_FMT_RGB24:
                return 12;
#ifdef CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_ENCODER
            case V4L2_PIX_FMT_YUV420:
                return 13;
#endif  // CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_ENCODER
#ifdef CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT
            case V4L2_PIX_FMT_JPEG:
                return 5;
#endif  // CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT
            case V4L2_PIX_FMT_GREY:
                return 20;
            default:
                return 1 << 29;  // unsupported
>>>>>>> origin/main
        }
    };
#endif
    // 循环枚举所有格式，找到评分最高的那个。
    while (ioctl(video_fd_, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        ESP_LOGD(TAG, "VIDIOC_ENUM_FMT: pixelformat=0x%08lx, description=%s", fmtdesc.pixelformat, fmtdesc.description);
        CAM_PRINT_FOURCC(fmtdesc.pixelformat);
        int rank = get_rank(fmtdesc.pixelformat);
        if (rank < best_rank) {
            best_rank = rank;
            best_fmt = fmtdesc.pixelformat;
        }
        fmtdesc.index++;
    }
    if (best_rank < (1 << 29)) {
        setformat.fmt.pix.pixelformat = best_fmt;
        sensor_format_ = best_fmt; // 保存选择的格式
    }

    if (!setformat.fmt.pix.pixelformat) {
        ESP_LOGE(TAG, "no supported pixel format found");
        close(video_fd_);
        video_fd_ = -1;
        sensor_format_ = 0;
        return;
    }
    ESP_LOGD(TAG, "selected pixel format: 0x%08lx", setformat.fmt.pix.pixelformat);

    // c. 将选定的最优格式设置给设备 (VIDIOC_S_FMT)
    if (ioctl(video_fd_, VIDIOC_S_FMT, &setformat) != 0) {
        ESP_LOGE(TAG, "VIDIOC_S_FMT failed, errno=%d(%s)", errno, strerror(errno));
        close(video_fd_);
        video_fd_ = -1;
        sensor_format_ = 0;
        return;
    }

    // 根据是否旋转，设置最终的帧尺寸
#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    frame_.width = setformat.fmt.pix.height;
    frame_.height = setformat.fmt.pix.width;
#else
    frame_.width = setformat.fmt.pix.width;
    frame_.height = setformat.fmt.pix.height;
#endif

    // 5. 申请并管理缓冲区 (Buffer Management)
    // a. 请求缓冲区 (VIDIOC_REQBUFS)
    struct v4l2_requestbuffers req = {};
    req.count = strcmp(video_device_name, ESP_VIDEO_MIPI_CSI_DEVICE_NAME) == 0 ? 2 : 1; // MIPI-CSI使用双缓冲
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP; // 使用内存映射模式
    if (ioctl(video_fd_, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");
        close(video_fd_);
        video_fd_ = -1;
        sensor_format_ = 0;
        return;
    }

    // b. 查询并内存映射缓冲区 (VIDIOC_QUERYBUF & mmap)
    mmap_buffers_.resize(req.count);
    for (uint32_t i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(video_fd_, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QUERYBUF failed");
            // ... 错误处理 ...
            return;
        }
        // 将驱动的内核空间缓冲区映射到本应用的用户空间
        void* start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, video_fd_, buf.m.offset);
        if (start == MAP_FAILED) {
            ESP_LOGE(TAG, "mmap failed");
            // ... 错误处理 ...
            return;
        }
        mmap_buffers_[i].start = start;
        mmap_buffers_[i].length = buf.length;

        // c. 将缓冲区入队 (VIDIOC_QBUF)，使其可用于捕获
        if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF failed");
            // ... 错误处理 ...
            return;
        }
    }

    // 6. 开启视频流 (VIDIOC_STREAMON)
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd_, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(TAG, "VIDIOC_STREAMON failed");
        close(video_fd_);
        video_fd_ = -1;
        sensor_format_ = 0;
        return;
    }

    // 7. ISP预热 (可选)
    // 如果启用了ISP，其自动白平衡、自动曝光等算法需要时间稳定。
    // 因此创建一个临时任务，在后台抓取几秒钟的图像并丢弃，以确保首次正式捕获的图像质量。
#ifdef CONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE
    xTaskCreate(
        [](void* arg) {
            Esp32Camera* self = static_cast<Esp32Camera*>(arg);
            uint16_t capture_count = 0;
            TickType_t start = xTaskGetTickCount();
            TickType_t duration = 5000 / portTICK_PERIOD_MS;  // 5秒
            while ((xTaskGetTickCount() - start) < duration) {
                struct v4l2_buffer buf = {};
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                if (ioctl(self->video_fd_, VIDIOC_DQBUF, &buf) != 0) { // 出队
                    ESP_LOGE(TAG, "VIDIOC_DQBUF failed during init");
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                    continue;
                }
                if (ioctl(self->video_fd_, VIDIOC_QBUF, &buf) != 0) { // 立即入队
                    ESP_LOGE(TAG, "VIDIOC_QBUF failed during init");
                }
                capture_count++;
            }
            ESP_LOGI(TAG, "Camera init success, captured %d frames in %dms", capture_count,
                     (xTaskGetTickCount() - start) * portTICK_PERIOD_MS);
            self->streaming_on_ = true;
            vTaskDelete(NULL);
        },
        "CameraInitTask", 4096, this, 5, nullptr);
#else
    ESP_LOGI(TAG, "Camera init success");
    streaming_on_ = true;
#endif
}

Esp32Camera::~Esp32Camera() {
    // 1. 停止视频流
    // 这是最重要的一步，确保驱动不再向缓冲区写入数据。
    if (streaming_on_ && video_fd_ >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(video_fd_, VIDIOC_STREAMOFF, &type);
    }
    // 2. 解除内存映射
    // 释放所有通过mmap映射的缓冲区。
    for (auto& b : mmap_buffers_) {
        if (b.start && b.length) {
            munmap(b.start, b.length);
        }
    }
    // 3. 关闭设备文件描述符
    if (video_fd_ >= 0) {
        close(video_fd_);
        video_fd_ = -1;
    }
    // 4. 重置内部状态
    sensor_format_ = 0;
    // 5. 反初始化esp_video
    // 释放esp_video_init分配的资源。
    esp_video_deinit();
}

void Esp32Camera::SetExplainUrl(const std::string& url, const std::string& token) {
    explain_url_ = url;
    explain_token_ = token;
}

bool Esp32Camera::Capture() {
    // 如果上一次的JPEG编码线程还在运行，则等待其结束。
    if (encoder_thread_.joinable()) {
        encoder_thread_.join();
    }

    if (!streaming_on_ || video_fd_ < 0) {
        return false;
    }

    // 循环3次以丢弃旧帧，确保获取到最新的图像。
    for (int i = 0; i < 3; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        // 1. 从驱动队列中取出一个已填充数据的缓冲区 (出队)
        if (ioctl(video_fd_, VIDIOC_DQBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_DQBUF failed");
            return false;
        }

        // 只处理最后一次（最新）的帧
        if (i == 2) {
            // 2. 将图像数据从mmap缓冲区拷贝到PSRAM中的内部帧缓冲区(frame_)
            // a. 释放旧的帧数据
            if (frame_.data) {
                heap_caps_free(frame_.data);
                frame_.data = nullptr;
                frame_.format = 0;
            }
            // b. 分配新内存并拷贝
            frame_.len = buf.bytesused;
            frame_.data = (uint8_t*)heap_caps_malloc(frame_.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!frame_.data) {
<<<<<<< HEAD
                ESP_LOGE(TAG, "alloc frame copy failed");
                ioctl(video_fd_, VIDIOC_QBUF, &buf); // 即使失败也要尝试归还缓冲区
=======
                ESP_LOGE(TAG, "alloc frame copy failed: need allocate %d bytes", buf.bytesused);
                if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                }
>>>>>>> origin/main
                return false;
            }

            // c. 根据传感器格式处理数据
            switch (sensor_format_) {
                case V4L2_PIX_FMT_RGB565:
                case V4L2_PIX_FMT_RGB24:
                case V4L2_PIX_FMT_YUYV:
                case V4L2_PIX_FMT_YUV420:
                case V4L2_PIX_FMT_GREY:
#ifdef CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT
                case V4L2_PIX_FMT_JPEG:
#endif  // CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT
#ifdef CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP
                    // 如果配置了字节序交换，则手动进行转换
                    {
                        auto src16 = (uint16_t*)mmap_buffers_[buf.index].start;
                        auto dst16 = (uint16_t*)frame_.data;
                        size_t count = (size_t)mmap_buffers_[buf.index].length / 2;
                        for (size_t i = 0; i < count; i++) {
                            dst16[i] = __builtin_bswap16(src16[i]);
                        }
                    }
#else
                    // 否则直接内存拷贝
                    memcpy(frame_.data, mmap_buffers_[buf.index].start,
                           MIN(mmap_buffers_[buf.index].length, frame_.len));
#endif
                    frame_.format = sensor_format_;
                    break;
                case V4L2_PIX_FMT_YUV422P: {
                    // 特殊处理：esp_video驱动中的YUV422P实际上是YUYV打包格式
                    frame_.format = V4L2_PIX_FMT_YUYV;
                    memcpy(frame_.data, mmap_buffers_[buf.index].start,
                           MIN(mmap_buffers_[buf.index].length, frame_.len));
                    break;
                }
                // 其他格式...
                default:
                    ESP_LOGE(TAG, "unsupported sensor format: 0x%08x", sensor_format_);
                    ioctl(video_fd_, VIDIOC_QBUF, &buf);
                    return false;
            }

            // 3. (可选) 图像旋转
#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
#ifndef CONFIG_SOC_PPA_SUPPORTED
            // a. 使用软件旋转 (esp_imgfx)
            // ... 分配旋转目标缓冲区 ...
            // ... 配置旋转参数（输入分辨率、角度、格式）...
            // ... 调用 esp_imgfx_rotate_process 进行旋转 ...
            // ... 将 frame_.data 指向旋转后的新缓冲区，并释放旧的 ...
#else
            // b. 使用硬件PPA（像素处理加速器）进行旋转 (仅ESP32-P4)
            // ... 根据原始格式准备PPA输入（可能需要先软件转为RGB）...
            // ... 配置PPA的输入、输出、缩放、旋转等参数 ...
            // ... 调用 ppa_do_scale_rotate_mirror 执行硬件操作 ...
            // ... 将 frame_.data 指向旋转后的新缓冲区，并释放旧的 ...
#endif
#endif
        }

        // 4. 将处理完的缓冲区归还给驱动 (入队)
        if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF failed");
        }
    }

    // 5. 在LVGL上显示预览图像
    auto display = dynamic_cast<LvglDisplay*>(Board::GetInstance().GetDisplay());
    if (display != nullptr) {
        if (!frame_.data) {
            ESP_LOGE(TAG, "frame.data is null");
            return false;
        }
        uint16_t w = frame_.width;
        uint16_t h = frame_.height;
        uint8_t* data = nullptr;

        // a. 颜色空间转换
        // 由于LVGL对YUV格式支持不佳，统一将YUV/RGB24等格式转换为RGB565。
        switch (frame_.format) {
            case V4L2_PIX_FMT_YUYV:
            case V4L2_PIX_FMT_YUV420:
            case V4L2_PIX_FMT_RGB24: {
                // ... 分配RGB565缓冲区 ...
                // ... 配置并调用 esp_imgfx_color_convert_process 进行转换 ...
                // ... 将 data 指向转换后的缓冲区 ...
                break;
            }
            case V4L2_PIX_FMT_RGB565:
                // 如果已经是RGB565，直接拷贝一份用于显示
                data = (uint8_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (data) memcpy(data, frame_.data, frame_.len);
                break;
<<<<<<< HEAD
=======

#ifdef CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT
            case V4L2_PIX_FMT_JPEG: {
                uint8_t* out_data = nullptr;  // out data is allocated by jpeg_to_image
                size_t out_len = 0;
                size_t out_width = 0;
                size_t out_height = 0;
                size_t out_stride = 0;

                esp_err_t ret =
                    jpeg_to_image(frame_.data, frame_.len, &out_data, &out_len, &out_width, &out_height, &out_stride);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to decode JPEG image: %d (%s)", (int)ret, esp_err_to_name(ret));
                    if (out_data) {
                        heap_caps_free(out_data);
                        out_data = nullptr;
                    }
                    return false;
                }

                data = out_data;
                w = out_width;
                h = out_height;
                lvgl_image_size = out_len;
                stride = out_stride;
                break;
            }
#endif
>>>>>>> origin/main
            default:
                ESP_LOGE(TAG, "unsupported frame format for display: 0x%08lx", frame_.format);
                return false;
        }

        // b. 创建LVGL图像对象并设置给显示驱动
        if (data) {
            auto image = std::make_unique<LvglAllocatedImage>(data, w * h * 2, w, h, ((w * 2) + 3) & ~3, LV_COLOR_FORMAT_RGB565);
            display->SetPreviewImage(std::move(image));
        }
    }
    return true;
}
/**
 * @brief 设置图像水平镜像。
 * @param enabled true为开启，false为关闭。
 * @return bool 成功返回true，失败返回false。
 */
bool Esp32Camera::SetHMirror(bool enabled) {
    if (video_fd_ < 0)
        return false;
    // 使用V4L2的扩展控制接口来设置水平翻转
    struct v4l2_ext_controls ctrls = {};
    struct v4l2_ext_control ctrl = {};
    ctrl.id = V4L2_CID_HFLIP; // 控制ID
    ctrl.value = enabled ? 1 : 0;
    ctrls.ctrl_class = V4L2_CTRL_CLASS_USER;
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    if (ioctl(video_fd_, VIDIOC_S_EXT_CTRLS, &ctrls) != 0) {
        ESP_LOGE(TAG, "set HFLIP failed");
        return false;
    }
    return true;
}

/**
 * @brief 设置图像垂直翻转。
 * @param enabled true为开启，false为关闭。
 * @return bool 成功返回true，失败返回false。
 */
bool Esp32Camera::SetVFlip(bool enabled) {
    if (video_fd_ < 0)
        return false;
    // 使用V4L2的扩展控制接口来设置垂直翻转
    struct v4l2_ext_controls ctrls = {};
    struct v4l2_ext_control ctrl = {};
    ctrl.id = V4L2_CID_VFLIP; // 控制ID
    ctrl.value = enabled ? 1 : 0;
    ctrls.ctrl_class = V4L2_CTRL_CLASS_USER;
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    if (ioctl(video_fd_, VIDIOC_S_EXT_CTRLS, &ctrls) != 0) {
        ESP_LOGE(TAG, "set VFLIP failed");
        return false;
    }
    return true;
}

/**
 * @brief 将摄像头捕获的图像发送到远程服务器进行AI分析和解释。
 *
 * @details
 * 该函数实现了一套精巧的“边编码边上传”的流式处理方案，以在内存有限的MCU上
 * 高效地处理并上传大尺寸图像。
 *
 * 工作流程:
 * 1.  **创建JPEG数据队列**: 创建一个FreeRTOS队列(`jpeg_queue`)，作为生产者(编码线程)和
 *     消费者(上传任务)之间的数据通道。
 * 2.  **启动编码线程**: 启动一个独立的`encoder_thread_`线程，该线程专门负责将`frame_`中
 *     的原始图像数据(如YUV, RGB)编码为JPEG格式。编码过程是分块的，每生成一小块
 *     JPEG数据，就将其放入队列。
 * 3.  **配置HTTP流式上传**: 在主任务中，配置HTTP客户端使用`Transfer-Encoding: chunked`
 *     (分块传输编码)。这允许在不知道最终文件大小的情况下开始上传。
 * 4.  **构建并发送请求头**: 发送`multipart/form-data`的请求头，包括`question`字段和
 *     `file`字段的元数据。
 * 5.  **消费并上传数据**: 主任务从`jpeg_queue`循环接收JPEG数据块，并立即通过HTTP连接
 *     发送出去，然后释放该数据块的内存。这个过程持续进行，直到收到一个空数据块，
 *     标志着编码过程结束。
 * 6.  **完成上传并获取结果**: 发送`multipart`的结束边界，关闭HTTP请求，然后读取并返回
 *     服务器的响应。
 *
 * 优势:
 * - **低内存占用**: 无需在内存中缓存整个JPEG文件，极大地降低了峰值内存消耗。
 * - **高效率**: JPEG编码和网络上传两个耗时操作并行进行，缩短了总处理时间。
 *
 * @param question 要向AI提出的关于图像的问题，将作为表单字段发送。
 * @return std::string 服务器返回的JSON格式响应字符串。
 * @throws std::runtime_error 如果URL未设置、队列创建失败或网络连接失败。
 */
std::string Esp32Camera::Explain(const std::string& question) {
    if (explain_url_.empty()) {
        throw std::runtime_error("Image explain URL or token is not set");
    }

    // 1. 创建用于在编码器和上传器之间传递JPEG数据块的队列
    QueueHandle_t jpeg_queue = xQueueCreate(40, sizeof(JpegChunk));
    if (jpeg_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create JPEG queue");
        throw std::runtime_error("Failed to create JPEG queue");
    }

    // 2. 启动一个新线程，在后台将帧数据编码为JPEG
    encoder_thread_ = std::thread([this, jpeg_queue]() {
        uint16_t w = frame_.width ? frame_.width : 320;
        uint16_t h = frame_.height ? frame_.height : 240;
        v4l2_pix_fmt_t enc_fmt = frame_.format;
<<<<<<< HEAD
        // image_to_jpeg_cb会分块编码JPEG，并通过回调函数将数据块送出
        image_to_jpeg_cb(
            frame_.data, frame_.len, w, h, enc_fmt, 80,
            [](void* arg, size_t index, const void* data, size_t len) -> size_t {
                auto jpeg_queue = (QueueHandle_t)arg;
                // 为数据块分配内存并拷贝，然后发送到队列
                JpegChunk chunk = {.data = (uint8_t*)heap_caps_aligned_alloc(16, len, MALLOC_CAP_SPIRAM), .len = len};
                memcpy(chunk.data, data, len);
=======
        bool ok = image_to_jpeg_cb(
            frame_.data, frame_.len, w, h, enc_fmt, 80,
            [](void* arg, size_t index, const void* data, size_t len) -> size_t {
                auto jpeg_queue = static_cast<QueueHandle_t>(arg);
                JpegChunk chunk = {.data = nullptr, .len = len};
                if (index == 0 && data != nullptr && len > 0) {
                    chunk.data = (uint8_t*)heap_caps_aligned_alloc(16, len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (chunk.data == nullptr) {
                        ESP_LOGE(TAG, "Failed to allocate %zu bytes for JPEG chunk", len);
                        chunk.len = 0;
                    } else {
                        memcpy(chunk.data, data, len);
                    }
                } else {
                    chunk.len = 0;  // Sentinel or error
                }
>>>>>>> origin/main
                xQueueSend(jpeg_queue, &chunk, portMAX_DELAY);
                return len;
            },
            jpeg_queue);

        if (!ok) {
            JpegChunk chunk = {.data = nullptr, .len = 0};
            xQueueSend(jpeg_queue, &chunk, portMAX_DELAY);
        }
    });

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(3);
    std::string boundary = "----ESP32_CAMERA_BOUNDARY";

    // 3. 配置HTTP客户端，使用分块传输
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    if (!explain_token_.empty()) {
        http->SetHeader("Authorization", "Bearer " + explain_token_);
    }
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http->SetHeader("Transfer-Encoding", "chunked"); // 关键：启用分块传输
    if (!http->Open("POST", explain_url_)) {
        ESP_LOGE(TAG, "Failed to connect to explain URL");
        // ... 清理队列和线程 ...
        throw std::runtime_error("Failed to connect to explain URL");
    }

    // 4. 发送multipart请求的各个部分
    {
        // part 1: question 字段
        std::string question_field;
        question_field += "--" + boundary + "\r\n";
        question_field += "Content-Disposition: form-data; name=\"question\"\r\n\r\n";
        question_field += question + "\r\n";
        http->Write(question_field.c_str(), question_field.size());
    }
    {
        // part 2: file 字段的头部
        std::string file_header;
        file_header += "--" + boundary + "\r\n";
        file_header += "Content-Disposition: form-data; name=\"file\"; filename=\"camera.jpg\"\r\n";
        file_header += "Content-Type: image/jpeg\r\n\r\n";
        http->Write(file_header.c_str(), file_header.size());
    }

    // 5. 从队列中消费JPEG数据块并立即发送
    size_t total_sent = 0;
    bool saw_terminator = false;
    while (true) {
        JpegChunk chunk;
        if (xQueueReceive(jpeg_queue, &chunk, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Failed to receive JPEG chunk");
            break;
        }
<<<<<<< HEAD
        if (chunk.data == nullptr) { // 收到空指针块，表示编码结束
            break;
=======
        if (chunk.data == nullptr) {
            saw_terminator = true;
            break;  // The last chunk
>>>>>>> origin/main
        }
        http->Write((const char*)chunk.data, chunk.len); // 发送数据块
        total_sent += chunk.len;
        heap_caps_free(chunk.data); // 释放内存
    }
    encoder_thread_.join(); // 确保编码线程已完全结束
    vQueueDelete(jpeg_queue); // 删除队列

<<<<<<< HEAD
    // 6. 发送multipart的结束边界和HTTP的结束块
=======
    if (!saw_terminator || total_sent == 0) {
        ESP_LOGE(TAG, "JPEG encoder failed or produced empty output");
        throw std::runtime_error("Failed to encode image to JPEG");
    }

>>>>>>> origin/main
    {
        std::string multipart_footer = "\r\n--" + boundary + "--\r\n";
        http->Write(multipart_footer.c_str(), multipart_footer.size());
    }
    http->Write("", 0); // 发送长度为0的块，表示HTTP请求体结束

    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "Failed to upload photo, status code: %d", http->GetStatusCode());
        throw std::runtime_error("Failed to upload photo");
    }

    // 7. 读取并返回服务器响应
    std::string result = http->ReadAll();
    http->Close();

    ESP_LOGI(TAG, "Explain image size=%d bytes, compressed size=%d, question=%s\n%s",
             (int)frame_.len, (int)total_sent, question.c_str(), result.c_str());
    return result;
}
