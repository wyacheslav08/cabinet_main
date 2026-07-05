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
#include <stdlib.h>

static const char *TAG = "UART_LINK";
static QueueHandle_t uart_queue;

// =========================================================================
// ВНУТРЕННИЙ ПАРСЕР KEY=VALUE
// =========================================================================
static void apply_key_value(const char *key, const char *val) {
    settings_lock();

    // --- ОБЩИЕ НАСТРОЙКИ ---
    if (strcmp(key, "targetHumidity") == 0) sys_settings.targetHumidity = atoi(val);
    else if (strcmp(key, "lockHoldTime") == 0) sys_settings.lockHoldTime = atoi(val);
    else if (strcmp(key, "lockTimeIndex") == 0) sys_settings.lockTimeIndex = atoi(val);
    else if (strcmp(key, "menuTimeoutOptionIndex") == 0) sys_settings.menuTimeoutOptionIndex = atoi(val);
    else if (strcmp(key, "screenTimeoutOptionIndex") == 0) sys_settings.screenTimeoutOptionIndex = atoi(val);
    else if (strcmp(key, "doorSoundEnabled") == 0) sys_settings.doorSoundEnabled = (atoi(val) == 1);
    else if (strcmp(key, "waterSilicaSoundEnabled") == 0) sys_settings.waterSilicaSoundEnabled = (atoi(val) == 1);
    else if (strcmp(key, "waterHeaterEnabled") == 0) sys_settings.waterHeaterEnabled = (atoi(val) == 1);
    else if (strcmp(key, "waterHeaterMaxTemp") == 0) sys_settings.waterHeaterMaxTemp = atoi(val);

    // --- ЛОГИКА ВЛАЖНОСТИ ---
    else if (strcmp(key, "deadZonePercent") == 0) sys_settings.deadZonePercent = atof(val);
    else if (strcmp(key, "minHumidityChangeForTimeout") == 0) sys_settings.minHumidityChangeForTimeout = atof(val);
    else if (strcmp(key, "maxOperationDuration") == 0) sys_settings.maxOperationDuration = atoi(val) * 60000; // Web шлет минуты, храним мс
    else if (strcmp(key, "operationCooldown") == 0) sys_settings.operationCooldown = atoi(val) * 60000;
    else if (strcmp(key, "maxSafeHumidity") == 0) sys_settings.maxSafeHumidity = atof(val);
    else if (strcmp(key, "resourceCheckDiff") == 0) sys_settings.resourceCheckDiff = atof(val);
    else if (strcmp(key, "humidityHysteresis") == 0) sys_settings.humidityHysteresis = atof(val);
    else if (strcmp(key, "resourceLowFaultThreshold") == 0) sys_settings.resourceLowFaultThreshold = atoi(val);
    else if (strcmp(key, "resourceEmptyFaultThreshold") == 0) sys_settings.resourceEmptyFaultThreshold = atoi(val);

    // --- КАЛИБРОВКА SHT40 ---
    else if (strcmp(key, "tempOffsetTop") == 0) sys_settings.tempOffsetTop = atoi(val);
    else if (strcmp(key, "humOffsetTop") == 0) sys_settings.humOffsetTop = atoi(val);
    else if (strcmp(key, "tempOffsetHum") == 0) sys_settings.tempOffsetHum = atoi(val);
    else if (strcmp(key, "humOffsetHum") == 0) sys_settings.humOffsetHum = atoi(val);

    // --- АВТО-ПЕРЕЗАГРУЗКА ---
    // (Если добавишь логику таймера в будущем)
    // else if (strcmp(key, "autoRebootEnabled") == 0) ...

    settings_unlock();
}

// =========================================================================
// ОБРАБОТЧИК ВХОДЯЩИХ СТРОК
// =========================================================================
static void process_incoming_command(char* cmd_str) {
    // Удаляем \r и \n в конце строки
    cmd_str[strcspn(cmd_str, "\r\n")] = 0;
    if (strlen(cmd_str) == 0) return;
    
    ESP_LOGI(TAG, "RX: %s", cmd_str);

    // 1. ОБРАБОТКА ПАКЕТОВ НАСТРОЕК (SET_GEN:, SET_HUMLOG:, SET_CALIB:)
    if (strncmp(cmd_str, "SET_", 4) == 0) {
        char *data_start = strchr(cmd_str, ':');
        if (data_start) {
            data_start++; // Пропускаем двоеточие
            
            char *saveptr_comma;
            // Разбиваем строку по запятым (пример: "targetHumidity=50,lockHoldTime=1000")
            char *pair = strtok_r(data_start, ",", &saveptr_comma);
            
            while (pair != NULL) {
                char *saveptr_eq;
                // Разбиваем пару по знаку равно
                char *key = strtok_r(pair, "=", &saveptr_eq);
                char *val = strtok_r(NULL, "=", &saveptr_eq);
                
                if (key && val) {
                    apply_key_value(key, val);
                }
                pair = strtok_r(NULL, ",", &saveptr_comma);
            }
            // Автоматически сохраняем во Flash после пачки изменений
            settings_save(); 
            ESP_LOGI(TAG, "Settings updated and saved to NVS.");
        }
    } 
    // 2. ОБРАБОТКА СИСТЕМНЫХ КОМАНД (Кнопки в Web UI)
    else if (strncmp(cmd_str, "CMD:", 4) == 0) {
        char *cmd = cmd_str + 4;
        
        if (strcmp(cmd, "REBOOT") == 0) {
            ESP_LOGW(TAG, "Reboot command received from Web UI!");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        } 
        else if (strcmp(cmd, "RESET_TO_DEFAULTS") == 0) {
            ESP_LOGW(TAG, "Factory Reset command received from Web UI!");
            settings_reset_to_defaults();
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
        else if (strcmp(cmd, "SAVE") == 0) {
            settings_save();
        }
    }
}

// =========================================================================
// ЗАДАЧА СЛУШАТЕЛЯ UART
// =========================================================================
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
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(COMM_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(COMM_UART_NUM, COMM_UART_TX_PIN, COMM_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(COMM_UART_NUM, COMM_UART_RX_BUF_SIZE * 2, COMM_UART_RX_BUF_SIZE * 2, 20, &uart_queue, 0));

    xTaskCreate(uart_event_task, "uart_rx_task", 4096, NULL, 5, NULL);
    return ESP_OK;
}

esp_err_t uart_link_send(const uint8_t *data, size_t len) {
    int written = uart_write_bytes(COMM_UART_NUM, (const char*)data, len);
    return (written == len) ? ESP_OK : ESP_FAIL;
}