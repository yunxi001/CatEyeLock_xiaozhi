#include "stm32_controller.h"
#include "application.h" // For Application::GetInstance().Schedule()
#include "board.h"       // For Board::GetInstance().GetDisplay() etc.
#include "config.h"      // For UART pin definitions

#include <esp_log.h>
#include <driver/uart.h>
#include <string.h> // For memset

#define TAG "Stm32Controller"

// UART buffer sizes
#define UART_BUF_SIZE (1024)
#define UART_QUEUE_SIZE (20)

// Private utility function to send data
static void uart_send_data(const std::string& data) {
    uart_write_bytes(STM32_UART_PORT, data.c_str(), data.length());
}

void Stm32Controller::Initialize() {
    if (initialized_) {
        ESP_LOGW(TAG, "Stm32Controller already initialized.");
        return;
    }

    // Configure UART pins
    uart_config_t uart_config = {
        .baud_rate = 115200, // Standard baud rate, can be adjusted
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_param_config(STM32_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(STM32_UART_PORT, STM32_UART_TX_PIN, STM32_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(STM32_UART_PORT, UART_BUF_SIZE * 2, UART_BUF_SIZE * 2, UART_QUEUE_SIZE, NULL, 0));

    // Create UART listener task
    xTaskCreate(uart_listener_task, "uart_listener", 2048 * 2, this, 10, NULL);

    initialized_ = true;
    ESP_LOGI(TAG, "Stm32Controller initialized on UART%d (TX:%d, RX:%d)",
             STM32_UART_PORT, STM32_UART_TX_PIN, STM32_UART_RX_PIN);
}

void Stm32Controller::sendCommand(const std::string& command, const cJSON* params) {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to create JSON root object for command: %s", command.c_str());
        return;
    }
    cJSON_AddStringToObject(root, "cmd", command.c_str());
    if (params) {
        cJSON_AddItemToObject(root, "params", cJSON_Duplicate(params, 1)); // Duplicate to avoid params being deleted with root
    }

    char* json_str = cJSON_PrintUnformatted(root);
    if (json_str == nullptr) {
        ESP_LOGE(TAG, "Failed to print JSON string for command: %s", command.c_str());
        cJSON_Delete(root);
        return;
    }

    std::string message = json_str;
    message += "\n"; // Add newline as message delimiter for STM32
    
    uart_send_data(message);

    ESP_LOGD(TAG, "Sent to STM32: %s", message.c_str());
    cJSON_free(json_str);
    cJSON_Delete(root);
}

void Stm32Controller::uart_listener_task(void* arg) {
    // Stm32Controller* self = (Stm32Controller*)arg;
    uint8_t* data = (uint8_t*) malloc(UART_BUF_SIZE);
    if (data == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate UART buffer.");
        vTaskDelete(NULL);
        return;
    }
    
    // Simple buffer to accumulate incoming data until a newline
    char line_buffer[UART_BUF_SIZE];
    int line_idx = 0;

    while (1) {
        int len = uart_read_bytes(STM32_UART_PORT, data, (UART_BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
        if (len > 0) {
            data[len] = '\0'; // Null-terminate the received data
            ESP_LOGD(TAG, "Received %d bytes from STM32: '%s'", len, (char*)data);

            for (int i = 0; i < len; ++i) {
                if (data[i] == '\n') { // Newline delimiter
                    line_buffer[line_idx] = '\0'; // Null-terminate the accumulated line
                    ESP_LOGI(TAG, "STM32 Event: %s", line_buffer);
                    
                    std::string json_payload(line_buffer);
                    ESP_LOGI(TAG, "STM32 Event: %s", json_payload.c_str());
                    
                                                // Schedule the event handling on the main application loop
                                                Application::GetInstance().Schedule([json_payload]() {
                                                    Application::GetInstance().HandleStm32Event(json_payload);
                                                });
                    line_idx = 0; // Reset buffer for next line
                    memset(line_buffer, 0, UART_BUF_SIZE);
                } else if (line_idx < (UART_BUF_SIZE - 1)) {
                    line_buffer[line_idx++] = data[i];
                } else {
                    ESP_LOGW(TAG, "UART line buffer overflow, resetting.");
                    line_idx = 0; // Buffer overflow, reset
                    memset(line_buffer, 0, UART_BUF_SIZE);
                }
            }
        }
    }
    free(data);
    vTaskDelete(NULL);
}
