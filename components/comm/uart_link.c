#include "uart_link.h"
#include "hw_config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h" // Для esp_restart()
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "settings_manager.h" 
#include <string.h>
#include <stdlib.h>

static const char *TAG = "UART_MAIN";
static QueueHandle_t uart_queue;

// Функция разбора входящих команд от Gateway (от Web App)
static void process_incoming_command(char* cmd_str) {
    // Убираем невидимые символы переноса строки
    cmd_str[strcspn(cmd_str, "\r\n")] = 0;
    if (strlen(cmd_str) == 0) return; // Игнорируем пустые строки
    
    ESP_LOGI(TAG, "Gateway commanded: %s", cmd_str);

 // 1. Команда изменения целевой влажности (SET_HUM:45)
    if (strncmp(cmd_str, "SET_HUM:", 8) == 0) {
        int new_hum = atoi(cmd_str + 8);
        if (new_hum >= 10 && new_hum <= 90) {
            settings_lock();
            sys_settings.targetHumidity = new_hum;
            settings_unlock();
            settings_save();
            ESP_LOGI(TAG, "Target humidity updated to %d%%", new_hum);
        }
    }
    // 2. Общие настройки (SET_GEN:targetHumidity=55,waterHeaterEnabled=1,...)
    else if (strncmp(cmd_str, "SET_GEN:", 8) == 0) {
        char* params = cmd_str + 8;
        ESP_LOGI(TAG, "Parsing GEN_SET: %s", params);
        
        settings_lock();
        // Простой парсинг (ищем подстроки)
        char* ptr;
        if ((ptr = strstr(params, "targetHumidity=")) != NULL) sys_settings.targetHumidity = atoi(ptr + 15);
        if ((ptr = strstr(params, "waterHeaterEnabled=")) != NULL) sys_settings.waterHeaterEnabled = (atoi(ptr + 19) == 1);
        if ((ptr = strstr(params, "waterHeaterMaxTemp=")) != NULL) sys_settings.waterHeaterMaxTemp = atoi(ptr + 19);
        if ((ptr = strstr(params, "doorSoundEnabled=")) != NULL) sys_settings.doorSoundEnabled = (atoi(ptr + 17) == 1);
        if ((ptr = strstr(params, "waterSilicaSoundEnabled=")) != NULL) sys_settings.waterSilicaSoundEnabled = (atoi(ptr + 24) == 1);
        settings_unlock();
        settings_save();
    }
    // 3. Логика влажности (SET_HUMLOG:deadZonePercent=2.0,...)
    else if (strncmp(cmd_str, "SET_HUMLOG:", 11) == 0) {
        char* params = cmd_str + 11;
        ESP_LOGI(TAG, "Parsing HUM_LOG: %s", params);
        settings_lock();
        char* ptr;
        if ((ptr = strstr(params, "deadZonePercent=")) != NULL) sys_settings.deadZonePercent = atof(ptr + 16);
        settings_unlock();
        settings_save();
    }
    // 4. Управление замком (CMD_LOCK:PRESS / ACTIVATE)
    else if (strncmp(cmd_str, "CMD_LOCK:", 9) == 0) {
        char* action = cmd_str + 9;
        if (strcmp(action, "PRESS") == 0 || strcmp(action, "ACTIVATE") == 0) {
            ESP_LOGI(TAG, "Unlocking Magnetic Lock...");
            // gpio_set_level(PIN_MAG_LOCK, 1);
        }
        else if (strcmp(action, "RELEASE") == 0) {
            ESP_LOGI(TAG, "Locking Magnetic Lock...");
            // gpio_set_level(PIN_MAG_LOCK, 0);
        }
    }
}

// Задача, обрабатывающая входящие UART пакеты
static void uart_event_task(void *pvParameters) {
    uart_event_t event;
    uint8_t* dtmp = (uint8_t*) malloc(COMM_UART_RX_BUF_SIZE);
    
    while (1) {
        if (xQueueReceive(uart_queue, (void * )&event, (TickType_t)portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:
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
    free(dtmp);
    vTaskDelete(NULL);
}

esp_err_t uart_link_init(void) {
    uart_config_t uart_config = {
        .baud_rate = COMM_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(COMM_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(COMM_UART_NUM, COMM_UART_TX_PIN, COMM_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(COMM_UART_NUM, COMM_UART_RX_BUF_SIZE * 2, COMM_UART_RX_BUF_SIZE * 2, 20, &uart_queue, 0));

    xTaskCreate(uart_event_task, "uart_main_task", 4096, NULL, 5, NULL);
    return ESP_OK;
}

esp_err_t uart_link_send(const uint8_t *data, size_t len) {
    int written = uart_write_bytes(COMM_UART_NUM, (const char*)data, len);
    return (written == len) ? ESP_OK : ESP_FAIL;
}