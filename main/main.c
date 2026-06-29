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

        // 6. Отправляем СТАТУС ЗАМКА И ДВЕРИ (K10)
        bool is_door_closed = (gpio_get_level(PIN_DOOR_SENSOR) == 0);
        bool is_lock_active = gesture_is_lock_pressed(); 
        snprintf(uart_buf, sizeof(uart_buf), "K10_STAT:LOCK:%s,DOOR:%s,HOLD:1000\n", 
                 is_lock_active ? "active" : "inactive",
                 is_door_closed ? "closed" : "open");
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
    }
}