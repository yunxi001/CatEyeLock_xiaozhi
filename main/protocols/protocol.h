/**
 * @file protocol.h
 * @brief 通信协议抽象基类定义
 *
 * 定义了设备与服务器之间通信的抽象接口，支持：
 * - 音频流传输（OPUS 编码）
 * - 视频流传输（JPEG 编码，监控模式）
 * - 人脸识别图像上传
 * - JSON 消息交互
 * - v5.0 协议扩展（状态上报、事件上报、用户管理等）
 *
 * 具体实现类：
 * - WebsocketProtocol: WebSocket 协议实现（主要）
 * - MqttProtocol: MQTT 协议实现（备选）
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cJSON.h>
#include <chrono>
#include <functional>
#include <string>
#include <vector>


// ============================================================================
// 数据结构定义
// ============================================================================

/**
 * @brief 音频流数据包
 */
struct AudioStreamPacket {
  int sample_rate = 0;          ///< 采样率（Hz）
  int frame_duration = 0;       ///< 帧时长（ms）
  uint32_t timestamp = 0;       ///< 时间戳（ms），用于服务端 AEC
  std::vector<uint8_t> payload; ///< 音频数据（OPUS 编码）
};

/**
 * @brief 二进制协议 v2 格式（16 字节头 + 变长载荷）
 *
 * 用于音频和视频数据传输：
 * - 音频：type=0, reserved=0
 * - 视频：type=0, reserved=(width<<16)|height
 * - 人脸识别：type=2, reserved=(width<<16)|height
 */
struct BinaryProtocol2 {
  uint16_t version;      ///< 协议版本（网络字节序）
  uint16_t type;         ///< 消息类型：0=OPUS/视频, 1=JSON, 2=人脸识别
  uint32_t reserved;     ///< 保留字段：视频时编码宽高（高16位:宽, 低16位:高）
  uint32_t timestamp;    ///< 时间戳（ms）
  uint32_t payload_size; ///< 载荷大小（字节）
  uint8_t payload[];     ///< 载荷数据
} __attribute__((packed));

/**
 * @brief 二进制协议 v3 格式（4 字节头 + 变长载荷）
 *
 * 简化版协议，用于低带宽场景。
 */
struct BinaryProtocol3 {
  uint8_t type;          ///< 消息类型
  uint8_t reserved;      ///< 保留字段
  uint16_t payload_size; ///< 载荷大小（网络字节序）
  uint8_t payload[];     ///< 载荷数据
} __attribute__((packed));

// ============================================================================
// 枚举定义
// ============================================================================

/**
 * @brief 中止说话的原因
 */
enum AbortReason {
  kAbortReasonNone,            ///< 无原因
  kAbortReasonWakeWordDetected ///< 检测到唤醒词
};

/**
 * @brief 监听模式
 */
enum ListeningMode {
  kListeningModeAutoStop,   ///< 自动停止（VAD 检测静音后停止）
  kListeningModeManualStop, ///< 手动停止（用户主动结束）
  kListeningModeRealtime    ///< 实时模式（需要 AEC 支持）
};

// ============================================================================
// 协议抽象基类
// ============================================================================

/**
 * @brief 通信协议抽象基类
 *
 * 定义了设备与服务器通信的统一接口，子类需实现具体的传输逻辑。
 */
class Protocol {
public:
  virtual ~Protocol() = default;

  // =========================================================================
  // 属性访问
  // =========================================================================

  /** 获取服务器音频采样率 */
  inline int server_sample_rate() const { return server_sample_rate_; }

  /** 获取服务器音频帧时长 */
  inline int server_frame_duration() const { return server_frame_duration_; }

  /** 获取会话 ID */
  inline const std::string &session_id() const { return session_id_; }

  // =========================================================================
  // 回调注册
  // =========================================================================

  /** 注册音频数据接收回调 */
  void OnIncomingAudio(
      std::function<void(std::unique_ptr<AudioStreamPacket> packet)> callback);

  /** 注册 JSON 消息接收回调 */
  void OnIncomingJson(std::function<void(const cJSON *root)> callback);

  /** 注册音频通道打开回调 */
  void OnAudioChannelOpened(std::function<void()> callback);

  /** 注册音频通道关闭回调 */
  void OnAudioChannelClosed(std::function<void()> callback);

  /** 注册网络错误回调 */
  void OnNetworkError(std::function<void(const std::string &message)> callback);

  /** 注册连接成功回调 */
  void OnConnected(std::function<void()> callback);

  /** 注册断开连接回调 */
  void OnDisconnected(std::function<void()> callback);

  // =========================================================================
  // 核心接口（纯虚函数，子类必须实现）
  // =========================================================================

  /** 启动协议 */
  virtual bool Start() = 0;

  /** 打开音频通道 */
  virtual bool OpenAudioChannel() = 0;

  /** 关闭音频通道 */
  virtual void CloseAudioChannel() = 0;

  /** 检查音频通道是否已打开 */
  virtual bool IsAudioChannelOpened() const = 0;

  /** 发送音频数据 */
  virtual bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) = 0;

  /**
   * @brief 发送视频帧（监控模式）
   * @param data      JPEG 数据指针
   * @param size      数据大小
   * @param timestamp 时间戳（ms）
   * @param width     图像宽度
   * @param height    图像高度
   */
  virtual bool SendVideo(const uint8_t *data, size_t size, uint32_t timestamp,
                         uint16_t width, uint16_t height) = 0;

  /**
   * @brief 发送人脸识别图像（正常模式）
   * @param jpeg_data JPEG 数据指针
   * @param jpeg_size 数据大小
   * @param width     图像宽度
   * @param height    图像高度
   */
  virtual bool SendFaceRecognition(const uint8_t *jpeg_data, size_t jpeg_size,
                                   uint16_t width, uint16_t height) = 0;

  // =========================================================================
  // 语音交互消息
  // =========================================================================

  /** 发送唤醒词检测通知 */
  virtual void SendWakeWordDetected(const std::string &wake_word);

  /** 发送开始监听通知 */
  virtual void SendStartListening(ListeningMode mode);

  /** 发送停止监听通知 */
  virtual void SendStopListening();

  /** 发送中止说话通知 */
  virtual void SendAbortSpeaking(AbortReason reason);

  /** 发送 MCP 消息 */
  virtual void SendMcpMessage(const std::string &message);

  // =========================================================================
  // v5.0 协议扩展：ACK 响应
  // =========================================================================

  /**
   * @brief 发送 ACK 响应
   * @param code 响应码（0=成功）
   * @param msg  响应消息
   */
  virtual void SendAck(int code, const std::string &msg);

  // =========================================================================
  // v5.0 协议扩展：状态与事件上报
  // =========================================================================

  /**
   * @brief 发送状态上报
   * @param battery     电量百分比
   * @param lux         光照强度
   * @param lock_state  锁状态（0=关, 1=开）
   * @param light_state 灯状态（0=关, 1=开）
   */
  virtual void SendStatusReport(int battery, int lux, int lock_state,
                                int light_state);

  /**
   * @brief 发送事件上报
   * @param event 事件名称（doorbell/pir/tamper/door_open/low_battery）
   * @param param 事件参数
   */
  virtual void SendEventReport(const std::string &event, int param = 0);

  /**
   * @brief 发送开锁日志上报
   * @param method     开锁方式（finger/nfc/pwd/remote/key/temp_pwd/face）
   * @param status     状态（success/fail/locked）
   * @param uid        用户 ID（成功时有效，失败时可能为 0xFF 表示无法识别）
   * @param fail_count 失败次数（1-5，仅 fail 状态有效）
   * @param lock_time  剩余锁定时间（分钟，仅 locked 状态有效）
   */
  virtual void SendLogReport(const std::string &method,
                             const std::string &status, int uid,
                             int fail_count = 0, int lock_time = 0);

  /**
   * @brief 发送开门日志上报
   * @param method 开锁方式（finger/nfc/pwd/remote/key/temp_pwd/face）
   * @param source 开门来源（outside/inside/unknown）
   */
  virtual void SendDoorOpenedReport(const std::string &method,
                                    const std::string &source);


  /**
   * @brief 发送用户管理结果上报
   * @param category 类别（fingerprint/nfc/password）
   * @param command  命令（enroll/delete/clear/count）
   * @param result   是否成功
   * @param val      返回值（如 ID、数量）
   * @param msg      消息说明
   */
  virtual void SendUserMgmtResult(const std::string &category,
                                  const std::string &command, bool result,
                                  int val, const std::string &msg);

  // =========================================================================
  // 监控模式控制
  // =========================================================================

  /** 设置监控模式 */
  virtual void SetMonitorMode(bool enabled) { monitor_mode_ = enabled; }

  /** 检查是否处于监控模式 */
  virtual bool IsMonitorMode() const { return monitor_mode_; }

protected:
  // 回调函数
  std::function<void(const cJSON *root)> on_incoming_json_;
  std::function<void(std::unique_ptr<AudioStreamPacket> packet)>
      on_incoming_audio_;
  std::function<void()> on_audio_channel_opened_;
  std::function<void()> on_audio_channel_closed_;
  std::function<void(const std::string &message)> on_network_error_;
  std::function<void()> on_connected_;
  std::function<void()> on_disconnected_;

  // 服务器参数
  int server_sample_rate_ = 24000; ///< 服务器音频采样率
  int server_frame_duration_ = 60; ///< 服务器音频帧时长（ms）

  // 状态标志
  bool error_occurred_ = false; ///< 是否发生错误
  bool monitor_mode_ = false;   ///< 监控模式标志

  // 会话信息
  std::string session_id_; ///< 会话 ID
  std::chrono::time_point<std::chrono::steady_clock>
      last_incoming_time_; ///< 最后接收时间

  /**
   * @brief 发送文本消息（纯虚函数）
   * @param text 文本内容
   * @return 发送成功返回 true
   */
  virtual bool SendText(const std::string &text) = 0;

  /**
   * @brief 设置错误状态
   * @param message 错误消息
   */
  virtual void SetError(const std::string &message);

  /**
   * @brief 检查是否超时
   * @return 超时返回 true
   */
  virtual bool IsTimeout() const;
};

#endif // PROTOCOL_H
