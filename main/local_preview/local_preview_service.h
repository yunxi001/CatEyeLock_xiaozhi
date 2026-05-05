#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace xiaozhi {

// 前向声明
class Application;

/**
 * @brief 本地预览服务
 *
 * 提供本地监控画面实时显示功能，基于零拷贝优化方案：
 * - 摄像头直接输出 RGB565 原始数据
 * - 双任务架构：Capture Task + Display Task
 * - FreeRTOS 队列缓冲平衡生产和消费速度
 * - LVGL Canvas 直接渲染 RGB565 数据
 *
 * 性能目标：
 * - 帧率：15 FPS
 * - 延迟：<50ms
 * - CPU 占用：<25%
 * - 内存占用：<400KB（PSRAM）
 *
 * 使用说明：
 * @code
 * // 启动本地预览
 * if (StartLocalPreview()) {
 *     ESP_LOGI(TAG, "本地预览已启动");
 * }
 *
 * // 停止本地预览
 * StopLocalPreview();
 * @endcode
 */
class LocalPreviewService {
public:
  /**
   * @brief 获取服务单例
   *
   * @return LocalPreviewService& 服务实例引用
   */
  static LocalPreviewService &GetInstance();

  /**
   * @brief 启动本地预览
   *
   * 执行以下操作：
   * 1. 检查摄像头可用性
   * 2. 检查与监控模式/人脸识别的互斥
   * 3. 创建 FreeRTOS 队列（深度 1）
   * 4. 创建 Capture Task（优先级 5，栈 4096）
   * 5. 创建 Display Task（优先级 5，栈 4096）
   * 6. 切换 LCD 到预览模式
   *
   * @return true 启动成功
   * @return false 启动失败（摄像头不可用、互斥冲突、资源不足等）
   */
  bool Start();

  /**
   * @brief 停止本地预览
   *
   * 执行以下操作：
   * 1. 设置停止标志
   * 2. 等待任务退出
   * 3. 清空队列并释放所有帧内存
   * 4. 删除队列
   * 5. 恢复 LCD 到正常模式
   */
  void Stop();

  /**
   * @brief 检查本地预览是否活动
   *
   * @return true 预览活动中
   * @return false 预览未启动
   */
  bool IsActive() const;

private:
  // 单例模式：私有构造函数和析构函数
  LocalPreviewService();
  ~LocalPreviewService();

  // 禁止拷贝和赋值
  LocalPreviewService(const LocalPreviewService &) = delete;
  LocalPreviewService &operator=(const LocalPreviewService &) = delete;

  /**
   * @brief Capture Task 入口函数（静态）
   *
   * @param param 指向 LocalPreviewService 实例的指针
   */
  static void CaptureTaskEntry(void *param);

  /**
   * @brief Display Task 入口函数（静态）
   *
   * @param param 指向 LocalPreviewService 实例的指针
   */
  static void DisplayTaskEntry(void *param);

  /**
   * @brief Capture Task 循环逻辑
   *
   * 以 15 FPS 频率捕获帧：
   * 1. 调用 CaptureForPreview() 获取 RGB565 数据
   * 2. 分配 PreviewFrame 对象并复制数据
   * 3. 推送到 FreeRTOS 队列（非阻塞）
   * 4. 队列满时丢弃旧帧并插入新帧
   * 5. 连续失败检测和错误恢复
   */
  void CaptureLoop();

  /**
   * @brief Display Task 循环逻辑
   *
   * 从队列获取帧并显示：
   * 1. 从队列获取最新帧（超时 100ms）
   * 2. 调用 UpdatePreviewCanvas() 更新显示
   * 3. 释放帧内存
   * 4. 错误处理（显示失败、队列超时等）
   */
  void DisplayLoop();

  // 成员变量
  bool active_;               ///< 预览活动标志
  TaskHandle_t capture_task_; ///< 捕获任务句柄
  TaskHandle_t display_task_; ///< 显示任务句柄
  QueueHandle_t frame_queue_; ///< 帧队列句柄

  // 常量配置
  static constexpr uint8_t kTargetFps = 15; ///< 目标帧率（FPS）
  static constexpr uint32_t kFrameIntervalMs =
      1000 / kTargetFps;                                 ///< 帧间隔（毫秒）
  static constexpr uint8_t kQueueDepth = 1;              ///< 队列深度（帧数）
  static constexpr uint8_t kTaskPriority = 5;            ///< 任务优先级
  static constexpr uint16_t kTaskStackSize = 4096;       ///< 任务栈大小（字节）
  static constexpr uint32_t kQueueTimeoutMs = 100;       ///< 队列超时（毫秒）
  static constexpr uint8_t kMaxConsecutiveFailures = 10; ///< 最大连续失败次数
};

} // namespace xiaozhi
