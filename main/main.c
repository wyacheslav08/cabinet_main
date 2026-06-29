#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
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

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(settings_init());

    ESP_ERROR_CHECK(i2c_manager_init());
    ESP_ERROR_CHECK(uart_link_init());
    ESP_ERROR_CHECK(pwm_manager_init());
    ESP_ERROR_CHECK(hx711_init());
    ESP_ERROR_CHECK(weight_manager_init());
    ESP_ERROR_CHECK(audio_analyzer_init());
    ESP_ERROR_CHECK(gesture_manager_init()); 
    ESP_ERROR_CHECK(climate_control_init()); 
    ESP_ERROR_CHECK(display_manager_init()); 

    ESP_LOGI(TAG, "=== System fully operational! Handing over to RTOS ===");

    char uart_buf[512]; // Увеличили буфер, так как строка будет длинной
    float fake_temp = 22.0f;
    float fake_hum = 45.0f;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); 
        
        // 1. Читаем реальные данные с датчика SHT40
        float current_temp = 0.0f; 
        float current_hum = 0.0f;
        if (sht40_read(MUX_CH_SHT40_TOP, &current_temp, &current_hum) != ESP_OK) {
            // Если датчик не подключен, генерируем "плавающие" фейковые данные для теста UI
            current_temp = fake_temp;
            current_hum = fake_hum;
            fake_temp += 0.2f;
            fake_hum += 0.5f;
            if (fake_temp > 30.0f) fake_temp = 22.0f;
            if (fake_hum > 60.0f) fake_hum = 45.0f;
        }

        // 2. Отправляем ТЕКУЩИЙ СТАТУС (Температура и Влажность) (используем жестко точку для дробных чисел)
        snprintf(uart_buf, sizeof(uart_buf), "STATUS:T:%d.%d,H:%d.%d\n", 
                 (int)current_temp, (int)(current_temp * 10) % 10,
                 (int)current_hum, (int)(current_hum * 10) % 10);
                 
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(50)); 

        // 3. Отправляем ПОЛНЫЕ ОБЩИЕ НАСТРОЙКИ (General Settings)
        settings_lock();
        snprintf(uart_buf, sizeof(uart_buf), 
                 "GEN_SET:targetHumidity=%d,"
                 "lockHoldTime=1000,"
                 "lockTimeIndex=0,"
                 "menuTimeoutOptionIndex=1,"
                 "screenTimeoutOptionIndex=0,"
                 "doorSoundEnabled=%d,"
                 "waterSilicaSoundEnabled=%d,"
                 "waterHeaterEnabled=%d,"
                 "waterHeaterMaxTemp=%d\n", 
                 sys_settings.targetHumidity,
                 sys_settings.doorSoundEnabled ? 1 : 0,
                 sys_settings.waterSilicaSoundEnabled ? 1 : 0,
                 sys_settings.waterHeaterEnabled ? 1 : 0,
                 sys_settings.waterHeaterMaxTemp);
        settings_unlock();
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(50));

        // 4. Отправляем ЛОГИКУ ВЛАЖНОСТИ (Humidity Logic)
        settings_lock();
        snprintf(uart_buf, sizeof(uart_buf), 
                 "HUM_LOG:deadZonePercent=%.1f,"
                 "minHumidityChangeForTimeout=1.0,"
                 "maxOperationDuration=2,"
                 "operationCooldown=1,"
                 "maxSafeHumidity=65.0,"
                 "resourceCheckDiff=3.0,"
                 "humidityHysteresis=1.0,"
                 "resourceLowFaultThreshold=2,"
                 "resourceEmptyFaultThreshold=4\n", 
                 sys_settings.deadZonePercent);
        settings_unlock();
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(50));

        // 5. Отправляем КАЛИБРОВКУ (Calibration)
        snprintf(uart_buf, sizeof(uart_buf), 
                 "CALIB:tempOffsetTop=0,"
                 "humOffsetTop=0,"
                 "tempOffsetHum=0,"
                 "humOffsetHum=0\n");
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(50));


        // Делаем снимок настроек
        settings_lock();
        cabinet_settings_t s = sys_settings;
        settings_unlock();

        // 3. Отправляем ПОЛНЫЕ ОБЩИЕ НАСТРОЙКИ (GEN_SET)
        snprintf(uart_buf, sizeof(uart_buf), 
                 "GEN_SET:targetHumidity=%d,lockHoldTime=%u,lockTimeIndex=%d,menuTimeoutOptionIndex=%d,"
                 "screenTimeoutOptionIndex=%d,doorSoundEnabled=%d,waterSilicaSoundEnabled=%d,"
                 "waterHeaterEnabled=%d,waterHeaterMaxTemp=%d\n", 
                 s.targetHumidity, s.lockHoldTime, s.lockTimeIndex, s.menuTimeoutOptionIndex,
                 s.screenTimeoutOptionIndex, s.doorSoundEnabled?1:0, s.waterSilicaSoundEnabled?1:0,
                 s.waterHeaterEnabled?1:0, s.waterHeaterMaxTemp);
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(50));

        // 4. Отправляем ЛОГИКУ ВЛАЖНОСТИ (HUM_LOG)
        // Внимание: мы переводим миллисекунды обратно в минуты для Web App (/ 60000)
        snprintf(uart_buf, sizeof(uart_buf), 
                 "HUM_LOG:deadZonePercent=%.1f,minHumidityChangeForTimeout=%.1f,maxOperationDuration=%lu,"
                 "operationCooldown=%lu,maxSafeHumidity=%.1f,resourceCheckDiff=%.1f,humidityHysteresis=%.1f,"
                 "resourceLowFaultThreshold=%d,resourceEmptyFaultThreshold=%d\n", 
                 s.deadZonePercent, s.minHumidityChangeForTimeout, s.maxOperationDuration / 60000,
                 s.operationCooldown / 60000, s.maxSafeHumidity, s.resourceCheckDiff, s.humidityHysteresis,
                 s.resourceLowFaultThreshold, s.resourceEmptyFaultThreshold);
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(50));

        // 5. Отправляем КАЛИБРОВКУ (CALIB)
        snprintf(uart_buf, sizeof(uart_buf), 
                 "CALIB:tempOffsetTop=%d,humOffsetTop=%d,tempOffsetHum=%d,humOffsetHum=%d\n",
                 s.tempOffsetTop, s.humOffsetTop, s.tempOffsetHum, s.humOffsetHum);
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(50));

        // 6. Отправляем СТАТИСТИКУ (STAT)
        snprintf(uart_buf, sizeof(uart_buf), 
                 "STAT:resetCount=%lu,wdtResetCount=%lu,autoRebootCounter=%lu,totalRebootCounter=%lu,lastRebootTimestamp=%lu\n",
                 s.resetCount, s.wdtResetCount, s.autoRebootCounter, s.totalRebootCounter, s.lastRebootTimestamp);
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(50));

        // 7. Отправляем НАСТРОЙКИ ЖЕЛЕЗА (HW_TUNE) - Новый пакет!
        snprintf(uart_buf, sizeof(uart_buf), 
                 "HW_TUNE:hxScale=%.1f,hxTare=%ld,dspPing=%lu,dspDry=%.1f,dspWet=%.1f\n",
                 s.hx711ScaleFactor, s.hx711TareOffset, s.dspPingDurationMs, s.dspDryResonanceHz, s.dspWetResonanceHz);
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(50));
        
        // 8. Отправляем СТАТУС ЗАМКА И ДВЕРИ (K10)
        bool is_door_closed = (gpio_get_level(PIN_DOOR_SENSOR) == 0);
        bool is_lock_active = gesture_is_lock_pressed(); 
        snprintf(uart_buf, sizeof(uart_buf), "K10_STAT:LOCK:%s,DOOR:%s,HOLD:%u\n", 
                 is_lock_active ? "active" : "inactive",
                 is_door_closed ? "closed" : "open",
                 s.lockHoldTime);
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
    }
}