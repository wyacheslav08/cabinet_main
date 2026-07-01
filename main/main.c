#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

// Подключение модулей нашей архитектуры
#include "settings_manager.h"
#include "i2c_manager.h"
#include "pwm_manager.h"
#include "hx711_driver.h"
#include "uart_link.h"
#include "hw_config.h"
#include "climate_control.h"
#include "audio_analyzer.h"
#include "display_manager.h"
#include "weight_manager.h"
#include "gesture_manager.h"
#include "sht40_driver.h"

static const char *TAG = "MAIN";

void app_main(void) {
    ESP_LOGI(TAG, "=== Guitar Cabinet Main Board Booting ===");

    // 1. Инициализация подсистемы памяти (NVS)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(settings_init());

    // 2. Инициализация аппаратного уровня (HAL)
    ESP_ERROR_CHECK(i2c_manager_init());
    ESP_ERROR_CHECK(uart_link_init());
    ESP_ERROR_CHECK(pwm_manager_init());
    ESP_ERROR_CHECK(hx711_init());
    
    // 3. Инициализация бизнес-логики и DSP
    ESP_ERROR_CHECK(weight_manager_init());
    
    // DSP микрофона может отсутствовать аппаратно, логируем ошибку, но не роняем систему
    if (audio_analyzer_init() != ESP_OK) {
        ESP_LOGE(TAG, "Audio DSP init failed. Resonance analyzer disabled.");
    }

    // 4. Инициализация UI и сенсоров (запуск задач FreeRTOS)
    ESP_ERROR_CHECK(gesture_manager_init()); 
    ESP_ERROR_CHECK(climate_control_init()); 
    ESP_ERROR_CHECK(display_manager_init()); 

    ESP_LOGI(TAG, "=== System fully operational! Entering telemetry loop ===");

    // Статический буфер в стеке задачи (избегаем фрагментации кучи)
    char uart_buf[512]; 

    while (1) {
        // Ожидание 5 секунд между отправкой полных пакетов телеметрии
        vTaskDelay(pdMS_TO_TICKS(5000)); 
        
        // --- 1. Чтение климатических данных ---
        float current_temp = 0.0f; 
        float current_hum = 0.0f;
        esp_err_t sht_err = sht40_read(MUX_CH_SHT40_TOP, &current_temp, &current_hum);

        if (sht_err == ESP_OK) {
            snprintf(uart_buf, sizeof(uart_buf), "STATUS:T:%d.%d,H:%d.%d\n", 
                     (int)current_temp, (int)(current_temp * 10) % 10,
                     (int)current_hum, (int)(current_hum * 10) % 10);
        } else {
            snprintf(uart_buf, sizeof(uart_buf), "STATUS:T:ERR,H:ERR\n");
            ESP_LOGW(TAG, "SHT40 read failed, sent ERR status to Gateway");
        }
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(50)); // Пауза для разгрузки FIFO UART-а Gateway

        // --- 2. Создание локальной копии настроек (потокобезопасно) ---
        settings_lock();
        cabinet_settings_t s = sys_settings;
        settings_unlock();

        // --- 3. Отправка общих настроек (GEN_SET) ---
        snprintf(uart_buf, sizeof(uart_buf), 
                 "GEN_SET:targetHumidity=%d,lockHoldTime=%u,lockTimeIndex=%d,menuTimeoutOptionIndex=%d,"
                 "screenTimeoutOptionIndex=%d,doorSoundEnabled=%d,waterSilicaSoundEnabled=%d,"
                 "waterHeaterEnabled=%d,waterHeaterMaxTemp=%d\n", 
                 s.targetHumidity, s.lockHoldTime, s.lockTimeIndex, s.menuTimeoutOptionIndex,
                 s.screenTimeoutOptionIndex, s.doorSoundEnabled ? 1 : 0, s.waterSilicaSoundEnabled ? 1 : 0,
                 s.waterHeaterEnabled ? 1 : 0, s.waterHeaterMaxTemp);
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(50));

        // --- 4. Отправка логики влажности (HUM_LOG) ---
        // Конвертация миллисекунд в минуты для Web App
        snprintf(uart_buf, sizeof(uart_buf), 
                 "HUM_LOG:deadZonePercent=%.1f,minHumidityChangeForTimeout=%.1f,maxOperationDuration=%lu,"
                 "operationCooldown=%lu,maxSafeHumidity=%.1f,resourceCheckDiff=%.1f,humidityHysteresis=%.1f,"
                 "resourceLowFaultThreshold=%d,resourceEmptyFaultThreshold=%d\n", 
                 s.deadZonePercent, s.minHumidityChangeForTimeout, s.maxOperationDuration / 60000,
                 s.operationCooldown / 60000, s.maxSafeHumidity, s.resourceCheckDiff, s.humidityHysteresis,
                 s.resourceLowFaultThreshold, s.resourceEmptyFaultThreshold);
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(50));

        // --- 5. Отправка калибровочных данных (CALIB) ---
        snprintf(uart_buf, sizeof(uart_buf), 
                 "CALIB:tempOffsetTop=%d,humOffsetTop=%d,tempOffsetHum=%d,humOffsetHum=%d\n",
                 s.tempOffsetTop, s.humOffsetTop, s.tempOffsetHum, s.humOffsetHum);
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(50));

        // --- 6. Отправка статистики работы (STAT) ---
        snprintf(uart_buf, sizeof(uart_buf), 
                 "STAT:resetCount=%lu,wdtResetCount=%lu,autoRebootCounter=%lu,totalRebootCounter=%lu,lastRebootTimestamp=%lu\n",
                 s.resetCount, s.wdtResetCount, s.autoRebootCounter, s.totalRebootCounter, s.lastRebootTimestamp);
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(50));

        // --- 7. Отправка аппаратных настроек DSP и Весов (HW_TUNE) ---
        snprintf(uart_buf, sizeof(uart_buf), 
                 "HW_TUNE:hxScale=%.1f,hxTare=%ld,dspPing=%lu,dspDry=%.1f,dspWet=%.1f\n",
                 s.hx711ScaleFactor, s.hx711TareOffset, s.dspPingDurationMs, s.dspDryResonanceHz, s.dspWetResonanceHz);
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(50));
        
        // --- 8. Отправка статуса интерфейса и двери (K10_STAT) ---
        bool is_door_closed = (gpio_get_level(PIN_DOOR_SENSOR) == 0);
        bool is_lock_active = gesture_is_lock_pressed(); 
        
        snprintf(uart_buf, sizeof(uart_buf), "K10_STAT:LOCK:%s,DOOR:%s,HOLD:%u\n", 
                 is_lock_active ? "active" : "inactive",
                 is_door_closed ? "closed" : "open",
                 s.lockHoldTime);
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
    }
    
    // Защита от выхода из цикла (стандарт FreeRTOS)
    vTaskDelete(NULL);
}