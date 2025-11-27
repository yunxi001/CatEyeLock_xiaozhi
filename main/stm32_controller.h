#ifndef STM32_CONTROLLER_H
#define STM32_CONTROLLER_H

#include <string>
#include <cJSON.h>
#include <functional> // For std::function

/**
 * @brief Stm32Controller 类 (单例)
 * 
 * 负责与 STM32F103C8T6 微控制器进行 UART 串行通信。
 * 封装了指令发送和事件接收的逻辑。
 */
class Stm32Controller {
public:
    /**
     * @brief 获取 Stm32Controller 的唯一实例。
     */
    static Stm32Controller& GetInstance() {
        static Stm32Controller instance;
        return instance;
    }

    // 删除拷贝构造和赋值，确保单例
    Stm32Controller(const Stm32Controller&) = delete;
    Stm32Controller& operator=(const Stm32Controller&) = delete;

    /**
     * @brief 初始化 UART 端口并创建监听任务。
     */
    void Initialize();

    /**
     * @brief 向 STM32 发送一个指令。
     * 
     * @param command 指令名称 (例如 "unlock", "set_led")。
     * @param params 可选的 JSON 对象，包含指令所需的参数。
     */
    void sendCommand(const std::string& command, const cJSON* params = nullptr);

private:
    /**
     * @brief 私有构造函数。
     */
    Stm32Controller() = default;

    /**
     * @brief 监听 UART 端口，接收并处理来自 STM32 消息的后台任务。
     * @param arg 传递给任务的参数 (指向 Stm32Controller 实例)。
     */
    static void uart_listener_task(void* arg);

    bool initialized_ = false; // 初始化标志，防止重复初始化
};

#endif // STM32_CONTROLLER_H
