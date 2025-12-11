/**
 * @file monitor_service.h
 * @brief 监控服务类定义
 * 
 * 本文件定义了监控服务类 MonitorService，负责管理实时视频对讲功能。
 * 监控模式允许用户远程查看摄像头画面并进行双向音频通信。
 * 
 * 架构说明：
 * - MonitorService 是监控模式的顶层管理器
 * - 内部使用 VideoStreamService 进行视频捕获和编码
 * - 通过 Protocol 将视频帧发送到服务器
 * - 音频通过 AudioService 处理（双向）
 * 
 * 与其他模式的关系：
 * - 监控模式与普通模式（AI 语音对话）互斥
 * - 进入监控模式时，普通模式的语音识别暂停
 * - 退出监控模式后，恢复普通模式功能
 * 
 * 使用示例：
 * @code
 * MonitorService monitor;
 * 
 * // 设置状态回调
 * monitor.SetStateChangeCallback([](bool running) {
 *     if (running) {
 *         // 监控模式已启动
 *     } else {
 *         // 监控模式已停止
 *     }
 * });
 * 
 * // 启动监控模式
 * monitor.Start(protocol, camera, audio_service);
 * 
 * // ... 运行中 ...
 * 
 * // 停止监控模式
 * monitor.Stop();
 * @endcode
 */

#ifndef MONITOR_SERVICE_H
#define MONITOR_SERVICE_H

#include <memory>
#include <string>
#include <functional>
#include "video_stream_service.h"

// 前向声明
class Protocol;
class Camera;
class AudioService;

/**
 * @class MonitorService
 * @brief 监控模式服务类
 * 
 * 该服务负责：
 * - 启动和停止监控模式
 * - 协调视频流和音频流
 * - 管理与服务器的连接
 * - 处理视频帧的网络传输
 * 
 * 视频参数：
 * - 格式：JPEG
 * - 分辨率：640x480
 * - 帧率：10 fps
 * - 传输协议：WebSocket (BinaryProtocol2)
 * 
 * 音频参数：
 * - 编码：OPUS
 * - 采样率：16000 Hz
 * - 双向传输
 */
class MonitorService {
public:
    /**
     * @brief 构造函数
     * 
     * 初始化服务状态，不启动任何任务。
     */
    MonitorService();

    /**
     * @brief 析构函数
     * 
     * 自动停止服务并释放所有资源。
     */
    ~MonitorService();

    // =========================================================================
    // 生命周期管理
    // =========================================================================

    /**
     * @brief 启动监控模式
     * 
     * 初始化视频流服务，创建传输任务，开始向服务器发送视频帧。
     * 
     * @param protocol 协议实例（用于网络通信，必须已连接）
     * @param camera 摄像头实例（必须已初始化）
     * @param audio_service 音频服务实例（用于双向音频）
     * @return bool 启动成功返回 true，失败返回 false
     * 
     * @note 所有参数在服务运行期间必须保持有效
     * @note 启动前应确保网络连接正常
     */
    bool Start(Protocol* protocol, Camera* camera, AudioService* audio_service);

    /**
     * @brief 停止监控模式
     * 
     * 停止视频传输，释放资源，恢复协议到正常模式。
     * 调用后可以安全地切换回普通模式。
     */
    void Stop();

    /**
     * @brief 检查监控服务是否正在运行
     * @return bool 运行中返回 true
     */
    bool IsRunning() const { return running_; }

    // =========================================================================
    // 回调设置
    // =========================================================================

    /**
     * @brief 设置状态变化回调
     * 
     * 当监控服务启动或停止时，会调用此回调函数。
     * 可用于更新 UI 状态或执行其他联动操作。
     * 
     * @param callback 回调函数，参数为 true 表示已启动，false 表示已停止
     */
    void SetStateChangeCallback(std::function<void(bool connected)> callback);

private:
    // -------------------------------------------------------------------------
    // 私有方法
    // -------------------------------------------------------------------------

    /**
     * @brief 视频传输任务入口函数（静态）
     * 
     * FreeRTOS 任务入口点，调用实例的 VideoTransmitLoop() 方法。
     * 
     * @param param 任务参数（MonitorService 实例指针）
     */
    static void VideoTransmitTask(void* param);

    /**
     * @brief 视频传输循环
     * 
     * 持续从视频流服务获取帧并发送到服务器。
     * 循环直到 running_ 为 false。
     */
    void VideoTransmitLoop();

    /**
     * @brief 发送视频帧到服务器
     * 
     * 使用 BinaryProtocol2 格式将 JPEG 帧发送到服务器。
     * 
     * @param frame JPEG 帧数据
     * @return bool 发送成功返回 true
     */
    bool SendVideoFrame(const JpegFrame& frame);

    // -------------------------------------------------------------------------
    // 私有成员变量
    // -------------------------------------------------------------------------

    Protocol* protocol_;       ///< 协议实例，用于网络通信
    Camera* camera_;           ///< 摄像头实例
    AudioService* audio_service_;  ///< 音频服务实例
    
    std::unique_ptr<VideoStreamService> video_stream_;  ///< 视频流服务（内部管理）
    TaskHandle_t transmit_task_handle_;  ///< 传输任务句柄
    bool running_;             ///< 服务运行状态标志
    
    std::function<void(bool)> state_change_callback_;  ///< 状态变化回调函数
    
    // -------------------------------------------------------------------------
    // 常量配置
    // -------------------------------------------------------------------------

    /// 默认视频帧率（帧/秒）
    static constexpr int kDefaultFps = 10;
};

#endif // MONITOR_SERVICE_H
