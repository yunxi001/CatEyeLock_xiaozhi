/**
 * @file main.cc
 * @brief ESP32 应用程序的入口点。
 * @author 78
 * @date 2024-07-20
 * 
 * @details
 * 该文件包含了 ESP32 应用程序的 `app_main` 函数，这是 FreeRTOS 启动后的第一个任务。
 * 它负责初始化系统的核心组件，如默认事件循环、非易失性存储 (NVS)，
 * 然后启动主应用程序逻辑。
 */

// 包含 ESP-IDF 提供的标准库
#include <esp_log.h>        // 用于日志输出
#include <esp_err.h>        // 用于错误处理宏
#include <nvs.h>            // 用于非易失性存储 (NVS) 访问
#include <nvs_flash.h>      // 用于 NVS Flash 的初始化和管理
#include <driver/gpio.h>    // 用于 GPIO 驱动 (虽然在此文件中未直接使用，但通常是基础组件)
#include <esp_event.h>      // 用于 ESP-IDF 事件循环
#include <freertos/FreeRTOS.h> // FreeRTOS 核心功能
#include <freertos/task.h>  // FreeRTOS 任务管理

// 包含项目内部的头文件
#include "application.h"    // 应用程序主逻辑的头文件
#include "system_info.h"    // 系统信息相关的头文件 (在此文件中未直接使用，但可能被其他模块使用)

#define TAG "main" // 定义日志标签，用于标识来自 main.cc 的日志信息

/**
 * @brief ESP32 应用程序的主入口函数。
 * 
 * 这是 FreeRTOS 启动后执行的第一个任务。
 * 它负责初始化 ESP-IDF 的核心服务，并启动应用程序的主逻辑。
 */
extern "C" void app_main(void)
{
    // 1. 初始化默认事件循环
    // ESP-IDF 的许多组件（如 Wi-Fi、TCP/IP 栈）都依赖于事件循环来处理异步事件。
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 2. 初始化 NVS Flash
    // NVS (Non-Volatile Storage) 用于存储持久化数据，例如 Wi-Fi 配置、设备设置等。
    // 首次初始化 NVS Flash。
    esp_err_t ret = nvs_flash_init();
    // 检查 NVS 初始化结果，如果 NVS 分区出现错误（如没有空闲页或版本不匹配），
    // 则擦除整个 NVS 分区并重新初始化，以修复潜在的损坏。
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption"); // 记录警告信息
        ESP_ERROR_CHECK(nvs_flash_erase()); // 擦除 NVS 分区
        ret = nvs_flash_init();             // 重新初始化 NVS
    }
    // 再次检查 NVS 初始化结果，如果仍有错误则停止程序。
    ESP_ERROR_CHECK(ret);

    // 3. 启动应用程序主逻辑
    // 获取 Application 类的单例实例，并调用其 Start 方法，
    // 从而启动整个应用程序的核心功能（如 WiFi 连接、音频处理、摄像头、显示等）。
    auto& app = Application::GetInstance();
    app.Start();
}
