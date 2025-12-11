/**
 * @file websocket_protocol.h
 * @brief WebSocket 通信协议实现
 * 
 * 基于 WebSocket 实现的通信协议，支持：
 * - 双向音频流传输（OPUS 编码）
 * - 视频流传输（JPEG 编码，监控模式）
 * - 人脸识别图像上传
 * - JSON 消息交互
 * - v5.0 协议扩展（状态上报、事件上报、用户管理等）
 * 
 * 协议版本：
 * - v1: 基础音频传输
 * - v2: 支持 AEC 和视频（BinaryProtocol2 格式）
 * - v3: 简化协议（BinaryProtocol3 格式）
 */

#ifndef _WEBSOCKET_PROTOCOL_H_
#define _WEBSOCKET_PROTOCOL_H_

#include "protocol.h"

#include <web_socket.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <deque>
#include <set>

/** 服务器 Hello 消息接收事件位 */
#define WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT (1 << 0)

/** msg_id 防重放缓存大小 */
constexpr size_t MSG_ID_CACHE_SIZE = 100;

/**
 * @brief WebSocket 协议实现类
 * 
 * 继承自 Protocol 基类，实现基于 WebSocket 的通信。
 */
class WebsocketProtocol : public Protocol {
public:
    WebsocketProtocol();
    ~WebsocketProtocol();

    // =========================================================================
    // Protocol 接口实现
    // =========================================================================
    
    bool Start() override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;
    bool SendVideo(const uint8_t* data, size_t size, uint32_t timestamp, 
                   uint16_t width, uint16_t height) override;
    bool SendFaceRecognition(const uint8_t* jpeg_data, size_t jpeg_size,
                             uint16_t width, uint16_t height) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;
    
    // =========================================================================
    // v5.0 协议扩展方法
    // =========================================================================
    
    /**
     * @brief 发送 ACK 响应
     * @param msg_id 消息 ID
     * @param code   响应码（0=成功）
     * @param msg    响应消息
     */
    void SendAck(const std::string& msg_id, int code = 0, const std::string& msg = "OK") override;
    
    /**
     * @brief 发送状态上报
     * @param battery     电量百分比
     * @param lux         光照强度
     * @param lock_state  锁状态
     * @param light_state 灯状态
     */
    void SendStatusReport(int battery, int lux, int lock_state, int light_state) override;
    
    /**
     * @brief 发送事件上报
     * @param event 事件名称
     * @param param 事件参数
     */
    void SendEventReport(const std::string& event, int param = 0) override;
    
    /**
     * @brief 发送开锁日志上报
     * @param method     开锁方式
     * @param uid        用户 ID
     * @param result     是否成功
     * @param fail_count 失败次数
     */
    void SendLogReport(const std::string& method, int uid, bool result, int fail_count = 0) override;
    
    /** 发送心跳 */
    void SendHeartbeat() override;
    
    /**
     * @brief 发送用户管理结果上报
     * @param category 类别
     * @param command  命令
     * @param result   是否成功
     * @param val      返回值
     * @param msg      消息说明
     */
    void SendUserMgmtResult(const std::string& category, const std::string& command,
                            bool result, int val, const std::string& msg) override;

private:
    EventGroupHandle_t event_group_handle_;     ///< FreeRTOS 事件组句柄
    std::unique_ptr<WebSocket> websocket_;      ///< WebSocket 连接对象
    int version_ = 1;                           ///< 协议版本
    
    // msg_id 防重放缓存
    std::deque<std::string> msg_id_queue_;      ///< FIFO 队列，用于淘汰旧 ID
    std::set<std::string> msg_id_set_;          ///< 集合，用于快速查找

    /**
     * @brief 解析服务器 Hello 消息
     * @param root JSON 根节点
     */
    void ParseServerHello(const cJSON* root);
    
    /**
     * @brief 发送文本消息
     * @param text 文本内容
     * @return 发送成功返回 true
     */
    bool SendText(const std::string& text) override;
    
    /**
     * @brief 构建客户端 Hello 消息
     * @return Hello 消息 JSON 字符串
     */
    std::string GetHelloMessage();
    
    /**
     * @brief 检查 msg_id 是否重复
     * @param msg_id 消息 ID
     * @return 重复返回 true
     */
    bool IsDuplicateMsgId(const std::string& msg_id);
    
    /**
     * @brief 添加 msg_id 到缓存
     * @param msg_id 消息 ID
     */
    void AddMsgIdToCache(const std::string& msg_id);
};

#endif
