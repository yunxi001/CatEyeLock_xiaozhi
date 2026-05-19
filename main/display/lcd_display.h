#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "gif/lvgl_gif.h"
#include "lvgl_display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <font_emoji.h>

#include <atomic>
#include <memory>

#define PREVIEW_IMAGE_DURATION_MS 5000

class LcdDisplay : public LvglDisplay {
protected:
  esp_lcd_panel_io_handle_t panel_io_ = nullptr;
  esp_lcd_panel_handle_t panel_ = nullptr;

  lv_draw_buf_t draw_buf_;
  lv_obj_t *top_bar_ = nullptr;
  lv_obj_t *status_bar_ = nullptr;
  lv_obj_t *content_ = nullptr;
  lv_obj_t *container_ = nullptr;
  lv_obj_t *side_bar_ = nullptr;
  lv_obj_t *bottom_bar_ = nullptr;
  lv_obj_t *preview_image_ = nullptr;
  lv_obj_t *emoji_label_ = nullptr;
  lv_obj_t *emoji_image_ = nullptr;
  std::unique_ptr<LvglGif> gif_controller_ = nullptr;
  lv_obj_t *emoji_box_ = nullptr;
  lv_obj_t *chat_message_label_ = nullptr;
  esp_timer_handle_t preview_timer_ = nullptr;
  std::unique_ptr<LvglImage> preview_image_cached_ = nullptr;
  bool hide_subtitle_ =
      false; // Control whether to hide chat messages/subtitles

  // 本地预览模式相关成员变量
  lv_obj_t *preview_canvas_ = nullptr;    // 预览 Canvas 对象
  void *preview_canvas_buffer_ = nullptr; // Canvas 缓冲区（PSRAM）
  bool preview_mode_active_ = false;      // 预览模式标志
  lv_obj_t *preview_indicator_ = nullptr; // "预览中"指示器 label

  // 门锁模式相关成员变量
  bool ui_hidden_ = false; // UI 隐藏状态标志（门锁模式）

  void InitializeLcdThemes();
  void SetupUI();
  virtual bool Lock(int timeout_ms = 0) override;
  virtual void Unlock() override;

protected:
  // Add protected constructor
  LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
             int width, int height);

public:
  ~LcdDisplay();
  virtual void SetStatus(const char *status) override;
  virtual void ShowNotification(const char *notification, int duration_ms = 3000) override;
  virtual void SetEmotion(const char *emotion) override;
  virtual void SetChatMessage(const char *role, const char *content) override;
  virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
  virtual void UpdateStatusBar(bool update_all = false) override;

  // Add theme switching function
  virtual void SetTheme(Theme *theme) override;

  // Set whether to hide chat messages/subtitles
  void SetHideSubtitle(bool hide);

  // 本地预览模式相关方法
  /**
   * @brief 进入预览模式
   *
   * 隐藏所有 LVGL UI 组件，创建并显示全屏 Canvas。
   *
   * @return bool 成功返回 true
   */
  bool EnterPreviewMode();

  /**
   * @brief 退出预览模式
   *
   * 销毁 Canvas，恢复 LVGL UI 组件。
   */
  void ExitPreviewMode();

  /**
   * @brief 更新预览 Canvas
   *
   * 将 RGB565 数据复制到 Canvas 缓冲区并触发重绘。
   *
   * @param rgb565_data RGB565 数据指针
   * @param width 图像宽度
   * @param height 图像高度
   * @return bool 成功返回 true
   */
  bool UpdatePreviewCanvas(const uint8_t *rgb565_data, uint16_t width,
                           uint16_t height);

  /**
   * @brief 检查是否处于预览模式
   *
   * @return bool 预览模式返回 true
   */
  bool IsPreviewMode() const;

  // 门锁模式相关方法
  /**
   * @brief 隐藏所有 UI（门锁模式）
   *
   * 隐藏所有 LVGL UI 组件，屏幕显示黑色。
   * 适用于智能门锁等无需常驻显示的场景。
   */
  void HideAllUI();

  /**
   * @brief 恢复所有 UI
   *
   * 恢复所有 LVGL UI 组件的显示。
   */
  void ShowAllUI();

  /**
   * @brief 检查 UI 是否隐藏
   *
   * @return bool UI 隐藏返回 true
   */
  bool IsUIHidden() const;
};

// SPI LCD display
class SpiLcdDisplay : public LcdDisplay {
public:
  SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io,
                esp_lcd_panel_handle_t panel, int width, int height,
                int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                bool swap_xy);
};

// RGB LCD display
class RgbLcdDisplay : public LcdDisplay {
public:
  RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io,
                esp_lcd_panel_handle_t panel, int width, int height,
                int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                bool swap_xy);
};

// MIPI LCD display
class MipiLcdDisplay : public LcdDisplay {
public:
  MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io,
                 esp_lcd_panel_handle_t panel, int width, int height,
                 int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                 bool swap_xy);
};

#endif // LCD_DISPLAY_H
