/**
 * @file video_stream_service.h
 * @brief 视频流服务类定义
 * 
 * 本文件定义了视频流服务类 VideoStreamService，负责管理摄像头视频流的
 * 捕获、编码和分发。该服务是监控模式的核心组件。
 * 
 * 架构说明：
 * - 生产者：CaptureLoop 任务定期从摄像头捕获帧并编码为 JPEG
 * - 消费者：MonitorService 通过 GetNextFrame() 获取帧并发送到服务器
 * - 缓冲区：使用有界队列（最大 3 帧）平衡生产和消费速度
 * 
 * 使用示例：
 * @code
 * VideoStreamService video_service;
 * 
 * // 启动服务，10 fps
 * video_service.Start(camera, 10);
 * 
 * // 获取帧
 * while (video_service.IsRunning()) {
 *     auto frame = video_service.GetNextFrame();
 *     if (frame) {
 *         // 处理帧数据
 *     }
 * }
 * 
 * // 停止服务
 * video_service.Stop();
 * @endcode
 */

#pragma once

#include <memory>
#include <queue>
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "jpeg_frame.h"

// 前向声明
class Camera;

/**
 * @class VideoStreamService
 * @brief 视频流服务类
 * 
 * 该服务负责：
 * - 定期从摄像头捕获原始图像
 * - 将图像编码为 JPEG 格式
 * - 管理帧队列，控制内存使用
 * - 提供帧给消费者（如监控服务）
 * 
 * 性能参数：
 * - 默认帧率：10 fps
 * - 默认 JPEG 质量：60%
 * - 最大队列深度：3 帧
 * - 典型分辨率：640x480
 * 
 * 内存管理：
 * - JPEG 数据使用 PSRAM 分配
 * - 队列满时自动丢弃最旧的帧
 * - 帧使用 unique_ptr 管理，自动释放
 */
class VideoStreamService {
public:
    /**
     * @brief 构造函数
     * 
     * 初始化服务状态，不启动捕获任务。
     */
    VideoStreamService();

    /**
     * @brief 析构函数
     * 
     * 自动停止服务并释放所有资源。
     */
    ~VideoStreamService();

    // =========================================================================
    // 生命周期管理
    // =========================================================================

    /**
     * @brief 启动视频流服务
     * 
     * 创建捕获任务，开始定期从摄像头获取帧。
     * 
     * @param camera 摄像头实例指针（必须在服务运行期间保持有效）
     * @param fps 目标帧率（帧/秒），默认 10 fps
     * @return bool 启动成功返回 true，失败返回 false
     * 
     * @note 摄像头必须已初始化且可用
     * @note 实际帧率可能因编码耗时而略低于目标值
     */
    bool Start(Camera* camera, int fps = 10);

    /**
     * @brief 停止视频流服务
     * 
     * 停止捕获任务并清空帧队列。
     * 调用后 GetNextFrame() 将返回 nullptr。
     */
    void Stop();

    /**
     * @brief 检查服务是否正在运行
     * @return bool 运行中返回 true
     */
    bool IsRunning() const { return running_; }

    // =========================================================================
    // 帧获取
    // =========================================================================

    /**
     * @brief 获取下一帧（非阻塞）
     * 
     * 从帧队列中取出最早的一帧。如果队列为空，立即返回 nullptr。
     * 返回的帧所有权转移给调用者，使用完毕后自动释放。
     * 
     * @return std::unique_ptr<JpegFrame> 帧数据，队列为空时返回 nullptr
     * 
     * @note 此方法是线程安全的
     * @note 调用者获得帧的所有权，无需手动释放
     */
    std::unique_ptr<JpegFrame> GetNextFrame();

    /**
     * @brief 获取当前队列中的帧数量
     * 
     * 用于监控队列状态，判断是否存在积压。
     * 
     * @return size_t 队列中的帧数量
     */
    size_t GetQueueSize() const;

private:
    // -------------------------------------------------------------------------
    // 私有方法
    // -------------------------------------------------------------------------

    /**
     * @brief 捕获任务入口函数（静态）
     * 
     * FreeRTOS 任务入口点，调用实例的 CaptureLoop() 方法。
     * 
     * @param param 任务参数（VideoStreamService 实例指针）
     */
    static void CaptureTask(void* param);

    /**
     * @brief 捕获循环
     * 
     * 持续执行以下操作：
     * 1. 从摄像头捕获原始图像
     * 2. 编码为 JPEG 格式
     * 3. 添加到帧队列
     * 4. 控制帧率
     * 
     * 当队列满时，丢弃最旧的帧以保持实时性。
     */
    void CaptureLoop();

    // -------------------------------------------------------------------------
    // 私有成员变量
    // -------------------------------------------------------------------------

    Camera* camera_;                   ///< 摄像头实例指针
    TaskHandle_t capture_task_handle_; ///< 捕获任务句柄
    bool running_;                     ///< 服务运行状态标志
    int target_fps_;                   ///< 目标帧率
    
    std::queue<std::unique_ptr<JpegFrame>> frame_queue_;  ///< 帧队列（FIFO）
    mutable std::mutex queue_mutex_;   ///< 队列互斥锁，保护并发访问
    
    // -------------------------------------------------------------------------
    // 常量配置
    // -------------------------------------------------------------------------

    /// 最大队列大小（帧数）
    /// 限制内存使用，超过时丢弃旧帧
    static constexpr size_t kMaxQueueSize = 3;

    /// 默认 JPEG 压缩质量（0-100）
    /// 60% 在质量和大小之间取得平衡
    static constexpr int kDefaultQuality = 60;
};
