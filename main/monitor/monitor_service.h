#ifndef MONITOR_SERVICE_H
#define MONITOR_SERVICE_H

#include <memory>
#include <string>
#include <functional>
#include "video_stream_service.h"

class Protocol;
class Camera;
class AudioService;

/**
 * @class MonitorService
 * @brief 监控模式服务，管理实时视频对讲功能
 * 
 * 该服务负责：
 * - 启动和停止监控模式
 * - 协调视频流和音频流
 * - 管理与服务器的连接
 * - 处理视频帧的网络传输
 */
class MonitorService {
public:
    MonitorService();
    ~MonitorService();

    /**
     * @brief 启动监控模式
     * @param protocol 协议实例（用于网络通信）
     * @param camera 摄像头实例
     * @param audio_service 音频服务实例
     * @return bool 启动成功返回 true
     */
    bool Start(Protocol* protocol, Camera* camera, AudioService* audio_service);

    /**
     * @brief 停止监控模式
     */
    void Stop();

    /**
     * @brief 检查监控服务是否正在运行
     * @return bool 运行中返回 true
     */
    bool IsRunning() const { return running_; }

    /**
     * @brief 设置状态变化回调
     * @param callback 状态变化时调用的回调函数
     */
    void SetStateChangeCallback(std::function<void(bool connected)> callback);

private:
    /**
     * @brief 视频传输任务
     */
    static void VideoTransmitTask(void* param);

    /**
     * @brief 执行视频传输循环
     */
    void VideoTransmitLoop();

    /**
     * @brief 发送视频帧到服务器
     * @param frame JPEG 帧数据
     * @return bool 发送成功返回 true
     */
    bool SendVideoFrame(const JpegFrame& frame);

    Protocol* protocol_;                          ///< 协议实例
    Camera* camera_;                              ///< 摄像头实例
    AudioService* audio_service_;                 ///< 音频服务实例
    
    std::unique_ptr<VideoStreamService> video_stream_;  ///< 视频流服务
    TaskHandle_t transmit_task_handle_;           ///< 传输任务句柄
    bool running_;                                ///< 运行状态标志
    
    std::function<void(bool)> state_change_callback_;  ///< 状态变化回调
    
    static constexpr int kDefaultFps = 10;        ///< 默认帧率
};

#endif // MONITOR_SERVICE_H
