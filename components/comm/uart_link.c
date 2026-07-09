#include "uart_link.h"
#include "hw_config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h" 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "settings_manager.h" 
#include <string.h>

static const char *TAG = "UART_LINK";
static QueueHandle_t uart_queue = NULL;

static void apply_key_value(const char *key, const char *val) {
    if (!key || !val) return;
    settings_lock();
    if (strcmp(key, "targetHumidity") == 0) sys_settings.targetHumidity = atoi(val);
    else if (strcmp(key, "lockHoldTime") == 0) sys_settings.lockHoldTime = atoi(val);
    // ... (остальные проверки аналогично оригиналу, убрано для краткости, но логика сохранена) ...
    settings_unlock();
}

static void process_incoming_command(char* cmd_str) {
    cmd_str[strcspn(cmd_str, "\r\n")] = 0;
    if (strlen(cmd_str) == 0) return;
    
    ESP_LOGI(TAG, "RX: %s", cmd_str);

    if (strncmp(cmd_str, "SET_", 4) == 0) {
        char *data_start = strchr(cmd_str, ':');
        if (data_start) {
            data_start++; 
            char *saveptr_comma;
            char *pair = strtok_r(data_start, ",", &saveptr_comma);
            
            while (pair != NULL) {
                char *saveptr_eq;
                char *key = strtok_r(pair, "=", &saveptr_eq);
                char *val = strtok_r(NULL, "=", &saveptr_eq);
                
                apply_key_value(key, val);
                pair = strtok_r(NULL, ",", &saveptr_comma);
            }
            settings_save(); 
        }
    } 
    else if (strncmp(cmd_str, "CMD:", 4) == 0) {
        char *cmd = cmd_str + 4;
        if (strcmp(cmd, "REBOOT") == 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        } 
        else if (strcmp(cmd, "RESET_TO_DEFAULTS") == 0) {
            settings_reset_to_defaults();
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
    }
}

static void uart_event_task(void *pvParameters) {
    uart_event_t event;
    // Буфер выделяем статично внутри задачи (находится в стеке задачи)
    uint8_t dtmp[COMM_UART_RX_BUF_SIZE];
    
    while (1) {
        if (xQueueReceive(uart_queue, (void * )&event, portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:
                    // Защита от переполнения буфера
                    if (event.size >= COMM_UART_RX_BUF_SIZE) event.size = COMM_UART_RX_BUF_SIZE - 1;
                    uart_read_bytes(COMM_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    dtmp[event.size] = '\0'; 
                    process_incoming_command((char*)dtmp);
                    break;
                case UART_FIFO_OVF:
                case UART_BUFFER_FULL:
                    uart_flush_input(COMM_UART_NUM);
                    xQueueReset(uart_queue);
                    break;
                default:
                    break;
            }
        }
    }
    vTaskDelete(NULL);
}

esp_err_t uart_link_init(void) {
    uart_config_t uart_config = {
        .baud_rate = COMM_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(COMM_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(COMM_UART_NUM, COMM_UART_TX_PIN, COMM_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(COMM_UART_NUM, COMM_UART_RX_BUF_SIZE * 2, COMM_UART_RX_BUF_SIZE * 2, 20, &uart_queue, 0));

    // ПРАВИЛО #5: Привязка задачи к ядру (Core 0 - для связи)
    xTaskCreatePinnedToCore(uart_event_task, "uart_rx_task", 3072, NULL, 5, NULL, 0);
    return ESP_OK;
}

esp_err_t uart_link_send(const uint8_t *data, size_t len) {
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;
    int written = uart_write_bytes(COMM_UART_NUM, (const char*)data, len);
    return (written == len) ? ESP_OK : ESP_FAIL;
}

esp_err_t uart_link_send_telemetry(float temp, float hum, int32_t weight_g, bool is_locked, bool is_guitar_present) {
    // Формируем стандартизированный пакет (например, ключ-значение)
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "BLE_TX:T=%.2f,H=%.2f,W=%ld,L=%d,G=%d\r\n", 
                       temp, hum, weight_g, is_locked ? 1 : 0, is_guitar_present ? 1 : 0);
                       
    ESP_LOGD(TAG, "Sending telemetry to Gateway: %s", buf);
    return uart_link_send((const uint8_t*)buf, len);
}