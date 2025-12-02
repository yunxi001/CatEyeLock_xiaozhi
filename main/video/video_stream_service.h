#pragma once

#include <memory>
#include <queue>
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "jpeg_frame.h"

class Camera;

/**
 * @class VideoStreamService
 * @brief 管理摄像头视频流的捕获和分发
 * 
 * 该服务负责：
 * - 定期从摄像头捕获 JPEG 帧
 * - 管理帧队列
 * - 控制帧率
 * - 提供帧给消费者（如监控服务）
 */
class VideoStreamService {
public:
    VideoStreamService();
    ~VideoStreamService();

    /**
     * @brief 启动视频流服务
     * @param camera 摄像头实例指针
     * @param fps 目标帧率（帧/秒）
     * @return bool 启动成功返回 true
     */
    bool Start(Camera* camera, int fps = 10);

    /**
     * @brief 停止视频流服务
     */
    void Stop();

    /**
     * @brief 获取下一帧（非阻塞）
     * @return std::unique_ptr<JpegFrame> 如果有可用帧返回帧数据，否则返回 nullptr
     */
    std::unique_ptr<JpegFrame> GetNextFrame();

    /**
     * @brief 检查服务是否正在运行
     * @return bool 运行中返回 true
     */
    bool IsRunning() const { return running_; }

    /**
     * @brief 获取当前队列中的帧数量
     * @return size_t 队列大小
     */
    size_t GetQueueSize() const;

private:
    /**
     * @brief 捕获线程的主循环
     */
    static void CaptureTask(void* param);

    /**
     * @brief 执行实际的帧捕获
     */
    void CaptureLoop();

    Camera* camera_;                              ///< 摄像头实例
    TaskHandle_t capture_task_handle_;            ///< 捕获任务句柄
    bool running_;                                ///< 运行状态标志
    int target_fps_;                              ///< 目标帧率
    
    std::queue<std::unique_ptr<JpegFrame>> frame_queue_;  ///< 帧队列
    mutable std::mutex queue_mutex_;              ///< 队列互斥锁
    
    static constexpr size_t kMaxQueueSize = 3;    ///< 最大队列大小
    static constexpr int kDefaultQuality = 60;    ///< 默认 JPEG 质量
};
