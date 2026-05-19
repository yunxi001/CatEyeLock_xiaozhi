// ============================================================================
// 自动连接功能实现（添加到 application.cc 文件末尾）
// ============================================================================

// 需要的头文件（如果在 application.cc 中没有包含）
// #include "display/lcd_display.h"

/**
 * @brief 自动连接任务循环
 *
 * 该任务在后台持续运行，负责：
 * 1. 检查是否需要连接服务器
 * 2. 尝试建立连接（显示 UI）
 * 3. 连接失败后使用指数退避策略重试
 * 4. 连接成功后监控连接状态
 * 5. 连接成功前阻止其他功能使用
 * 6. 连接成功后隐藏 UI（门锁模式）
 */
void Application::AutoConnectLoop() {
  ESP_LOGI(TAG, "自动连接任务已启动");

  auto display = Board::GetInstance().GetDisplay();

  // 等待 5 秒，确保系统初始化完成
  vTaskDelay(pdMS_TO_TICKS(5000));

  while (true) {
    // 1. 检查是否需要自动连接
    if (ShouldAutoConnect()) {
      ESP_LOGI(TAG, "尝试自动连接服务器 (重试次数: %d)",
               connection_retry_count_);

      // 显示连接中的 UI（类似联网时的显示）
      display->ShowNotification(Lang::Strings::CONNECTING_TO_SERVER, 30000);

      // 2. 尝试连接服务器
      bool connect_success = false;
      if (protocol_) {
        // 使用 Schedule 在主线程中执行连接操作
        Schedule([this, &connect_success]() {
          if (protocol_->OpenAudioChannel()) {
            connect_success = true;
            connection_retry_count_ = 0;         // 重置重试计数
            user_manually_disconnected_ = false; // 清除手动断开标志
            ESP_LOGI(TAG, "自动连接成功");
          } else {
            ESP_LOGE(TAG, "自动连接失败");
          }
        });

        // 等待连接操作完成（最多等待 15 秒）
        for (int i = 0; i < 30; i++) {
          vTaskDelay(pdMS_TO_TICKS(500));
          if (connect_success || protocol_->IsAudioChannelOpened()) {
            break;
          }
        }
      }

      // 3. 根据连接结果决定下一步
      if (protocol_ && protocol_->IsAudioChannelOpened()) {
        // 连接成功，显示成功通知
        ESP_LOGI(TAG, "连接已建立，开始监控连接状态");
        display->ShowNotification(Lang::Strings::CONNECTED_TO_SERVER, 3000);

        // 播放成功音效
        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);

        // 确保音频输出通路已完全激活（等效于唤醒后的初始化效果）
        audio_service_.ResetDecoder();

        // ========== 连接成功后隐藏 UI（门锁模式）==========
        auto lcd_display = dynamic_cast<LcdDisplay *>(display);
        if (lcd_display) {
          lcd_display->HideAllUI();
          ESP_LOGI(TAG, "已切换到门锁模式（UI 隐藏）");
        }
        // ========================================================

        while (protocol_->IsAudioChannelOpened()) {
          vTaskDelay(pdMS_TO_TICKS(5000)); // 每 5 秒检查一次
        }

        ESP_LOGW(TAG, "连接已断开");
        connection_retry_count_ = 0; // 重置重试计数

        // 等待 2 秒后再尝试重连
        vTaskDelay(pdMS_TO_TICKS(2000));

      } else {
        // 连接失败，使用指数退避
        connection_retry_count_++;
        int delay_ms = CalculateRetryDelay(connection_retry_count_);
        int delay_seconds = delay_ms / 1000;

        ESP_LOGW(TAG, "自动连接失败，等待 %d 秒后重试 (重试次数: %d)",
                 delay_seconds, connection_retry_count_);

        // 显示重试倒计时（类似联网失败的显示）
        char retry_msg[64];
        snprintf(retry_msg, sizeof(retry_msg),
                 Lang::Strings::SERVER_CONNECT_FAILED, delay_seconds);
        display->ShowNotification(retry_msg, delay_ms);

        // 分段延迟，以便及时响应状态变化
        int delay_steps = delay_ms / 1000; // 每秒检查一次
        for (int i = 0; i < delay_steps; i++) {
          vTaskDelay(pdMS_TO_TICKS(1000));

          // 如果用户主动断开或状态不适合连接，提前退出延迟
          if (user_manually_disconnected_ || !ShouldAutoConnect()) {
            ESP_LOGI(TAG, "检测到状态变化，取消重试延迟");
            break;
          }
        }
      }

    } else {
      // 不需要连接，等待 5 秒后再检查
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
  }
}

/**
 * @brief 判断是否应该自动连接
 * @return 如果满足自动连接条件返回 true
 */
bool Application::ShouldAutoConnect() {
  // 1. 检查自动连接功能是否启用
  if (!auto_connect_enabled_) {
    return false;
  }

  // 2. 检查 Protocol 是否已初始化
  if (!protocol_) {
    return false;
  }

  // 3. 检查是否已连接服务器
  if (protocol_->IsAudioChannelOpened()) {
    return false;
  }

  // 4. 检查用户是否主动断开连接
  if (user_manually_disconnected_) {
    return false;
  }

  // 5. 检查设备状态是否适合连接
  // 只在 Idle 状态下自动连接
  if (device_state_ != kDeviceStateIdle) {
    return false;
  }

  // 6. 检查网络是否已连接
  auto &board = Board::GetInstance();
  auto network = board.GetNetwork();
  if (!network) {
    return false;
  }

  // 7. 检查是否处于监控模式
  if (IsMonitorMode()) {
    return false;
  }

  // 所有条件都满足，可以自动连接
  return true;
}

/**
 * @brief 计算指数退避延迟时间
 * @param retry_count 当前重试次数
 * @return 延迟时间（毫秒）
 */
int Application::CalculateRetryDelay(int retry_count) {
  // 指数退避：1s, 2s, 4s, 8s, 16s, 32s, 60s(max)
  int delay_ms = INITIAL_RETRY_DELAY_MS * (1 << retry_count);

  // 限制最大延迟
  if (delay_ms > MAX_RETRY_DELAY_MS) {
    delay_ms = MAX_RETRY_DELAY_MS;
  }

  // 添加随机抖动 (±20%)，避免多设备同时重连
  int jitter_range = delay_ms / 5; // 20% 的范围
  int jitter = (esp_random() % (jitter_range * 2)) - jitter_range;

  int final_delay = delay_ms + jitter;

  // 确保延迟至少为 1 秒
  if (final_delay < 1000) {
    final_delay = 1000;
  }

  return final_delay;
}
