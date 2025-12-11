/**
 * @file jpeg_frame.h
 * @brief JPEG 视频帧数据结构定义
 * 
 * 本文件定义了视频流服务中使用的 JPEG 帧数据结构和设备运行模式枚举。
 * JPEG 帧用于在视频捕获、编码和网络传输之间传递数据。
 */

#ifndef JPEG_FRAME_H
#define JPEG_FRAME_H

#include <vector>
#include <cstdint>

/**
 * @struct JpegFrame
 * @brief JPEG 编码后的视频帧结构
 * 
 * 存储单帧 JPEG 编码的图像数据及其元信息。
 * 该结构支持移动语义以提高性能，禁止拷贝以避免大数据块的意外复制。
 * 
 * 内存管理说明：
 * - data 成员使用 std::vector 自动管理内存
 * - 对于大帧数据，建议使用 PSRAM 分配（通过 heap_caps_malloc）
 * - 帧队列中的帧应使用 std::unique_ptr 管理生命周期
 */
struct JpegFrame {
    std::vector<uint8_t> data;  ///< JPEG 压缩数据，通常为几十 KB
    uint32_t timestamp;          ///< 时间戳（毫秒），用于同步和帧率控制
    uint16_t width;              ///< 图像宽度（像素），如 640
    uint16_t height;             ///< 图像高度（像素），如 480
    
    /**
     * @brief 默认构造函数
     * 
     * 初始化空帧，时间戳和尺寸均为 0。
     */
    JpegFrame() : timestamp(0), width(0), height(0) {}
    
    // -------------------------------------------------------------------------
    // 移动语义支持
    // -------------------------------------------------------------------------
    
    /**
     * @brief 移动构造函数
     * 
     * 允许高效地转移帧数据所有权，避免大数据块的复制。
     */
    JpegFrame(JpegFrame&&) = default;
    
    /**
     * @brief 移动赋值运算符
     * 
     * 允许高效地转移帧数据所有权。
     */
    JpegFrame& operator=(JpegFrame&&) = default;
    
    // -------------------------------------------------------------------------
    // 禁止拷贝
    // -------------------------------------------------------------------------
    
    /**
     * @brief 禁用拷贝构造函数
     * 
     * JPEG 数据通常较大（几十 KB），禁止拷贝以避免性能问题。
     * 如需传递帧数据，请使用移动语义或指针。
     */
    JpegFrame(const JpegFrame&) = delete;
    
    /**
     * @brief 禁用拷贝赋值运算符
     */
    JpegFrame& operator=(const JpegFrame&) = delete;
};

/**
 * @enum DeviceMode
 * @brief 设备运行模式枚举
 * 
 * 定义了设备的两种主要运行模式，用于控制系统行为和资源分配。
 * 两种模式互斥，同一时间只能处于一种模式。
 */
enum DeviceMode {
    kModeNormal,   ///< 普通模式：AI 语音对话，摄像头用于人脸识别
    kModeMonitor,  ///< 监控模式：实时视频对讲，摄像头用于视频流传输
};

#endif // JPEG_FRAME_H
