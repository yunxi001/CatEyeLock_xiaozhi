#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

/**
 * @brief 本地预览帧结构
 *
 * 存储一帧 RGB565 原始图像数据及其元数据。
 * 内存分配在 PSRAM 中以节省内部 RAM。
 *
 * 使用说明：
 * - 构造时自动分配 PSRAM 缓冲区
 * - 析构时自动释放 PSRAM 缓冲区
 * - 禁止拷贝，只允许移动语义
 *
 * 示例：
 * @code
 * PreviewFrame frame(240, 320);
 * if (frame.data != nullptr) {
 *     // 使用 frame.data 存储 RGB565 数据
 *     memcpy(frame.data, source_data, frame.data_size);
 * }
 * @endcode
 */
struct PreviewFrame {
  uint8_t *data;        ///< RGB565 数据指针（PSRAM）
  size_t data_size;     ///< 数据大小（字节）
  uint16_t width;       ///< 图像宽度
  uint16_t height;      ///< 图像高度
  TickType_t timestamp; ///< 捕获时间戳（FreeRTOS tick）

  /**
   * @brief 构造函数，分配 PSRAM 缓冲区
   *
   * @param w 图像宽度
   * @param h 图像高度
   *
   * @note 如果 PSRAM 分配失败，data 将为 nullptr
   */
  PreviewFrame(uint16_t w, uint16_t h)
      : data(nullptr), data_size(0), width(w), height(h), timestamp(0) {
    // 计算 RGB565 数据大小（每像素 2 字节）
    data_size = static_cast<size_t>(width) * height * 2;

    // 使用 PSRAM 分配内存
    data =
        static_cast<uint8_t *>(heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM));

    if (data == nullptr) {
      ESP_LOGE("PreviewFrame", "无法分配 PSRAM 缓冲区: %zu 字节", data_size);
      data_size = 0;
    } else {
      ESP_LOGD("PreviewFrame", "已分配 PSRAM 缓冲区: %dx%d, %zu 字节", width,
               height, data_size);
    }
  }

  /**
   * @brief 析构函数，释放 PSRAM 缓冲区
   */
  ~PreviewFrame() {
    if (data != nullptr) {
      heap_caps_free(data);
      ESP_LOGD("PreviewFrame", "已释放 PSRAM 缓冲区: %zu 字节", data_size);
      data = nullptr;
      data_size = 0;
    }
  }

  // 禁止拷贝构造和拷贝赋值
  PreviewFrame(const PreviewFrame &) = delete;
  PreviewFrame &operator=(const PreviewFrame &) = delete;

  // 允许移动构造和移动赋值
  PreviewFrame(PreviewFrame &&other) noexcept
      : data(other.data), data_size(other.data_size), width(other.width),
        height(other.height), timestamp(other.timestamp) {
    // 清空源对象，防止重复释放
    other.data = nullptr;
    other.data_size = 0;
    other.width = 0;
    other.height = 0;
    other.timestamp = 0;
  }

  PreviewFrame &operator=(PreviewFrame &&other) noexcept {
    if (this != &other) {
      // 释放当前对象的资源
      if (data != nullptr) {
        heap_caps_free(data);
      }

      // 移动资源
      data = other.data;
      data_size = other.data_size;
      width = other.width;
      height = other.height;
      timestamp = other.timestamp;

      // 清空源对象
      other.data = nullptr;
      other.data_size = 0;
      other.width = 0;
      other.height = 0;
      other.timestamp = 0;
    }
    return *this;
  }

  /**
   * @brief 检查帧数据是否有效
   *
   * @return true 数据有效（已成功分配内存）
   * @return false 数据无效（内存分配失败）
   */
  bool IsValid() const { return data != nullptr && data_size > 0; }
};
