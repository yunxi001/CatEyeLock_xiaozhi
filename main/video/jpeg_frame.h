#ifndef JPEG_FRAME_H
#define JPEG_FRAME_H

#include <vector>
#include <cstdint>

/**
 * @struct JpegFrame
 * @brief JPEG编码后的视频帧
 */
struct JpegFrame {
    std::vector<uint8_t> data;  // JPEG数据
    uint32_t timestamp;          // 时间戳（毫秒）
    uint16_t width;              // 图像宽度
    uint16_t height;             // 图像高度
    
    JpegFrame() : timestamp(0), width(0), height(0) {}
    
    // 支持移动语义
    JpegFrame(JpegFrame&&) = default;
    JpegFrame& operator=(JpegFrame&&) = default;
    
    // 禁止拷贝
    JpegFrame(const JpegFrame&) = delete;
    JpegFrame& operator=(const JpegFrame&) = delete;
};

/**
 * @enum DeviceMode
 * @brief 设备运行模式
 */
enum DeviceMode {
    kModeNormal,      // 普通模式：AI语音对话
    kModeMonitor,     // 监控模式：实时视频对讲
};

#endif // JPEG_FRAME_H
