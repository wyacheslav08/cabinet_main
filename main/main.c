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

    char uart_buf[256];

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Обновление каждые 5 секунд
        
        // 1. Читаем реальные данные с датчика SHT40
        float current_temp = 0.0f; 
        float current_hum = 0.0f;
        if (sht40_read(MUX_CH_SHT40_TOP, &current_temp, &current_hum) != ESP_OK) {
            current_temp = -99.0f; // Ошибка датчика
            current_hum = -99.0f;
        }

        // 2. Отправляем ТЕКУЩИЙ СТАТУС (Температура и Влажность)
        snprintf(uart_buf, sizeof(uart_buf), "STATUS:T:%.1f,H:%.1f\n", current_temp, current_hum);
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(100)); // Небольшая пауза между пакетами, чтобы не переполнить UART

        // 3. Отправляем ОБЩИЕ НАСТРОЙКИ (General Settings)
        settings_lock();
        snprintf(uart_buf, sizeof(uart_buf), "GEN_SET:targetHumidity=%d,waterHeaterEnabled=%d\n", 
                 sys_settings.targetHumidity, 
                 sys_settings.waterHeaterEnabled ? 1 : 0);
        settings_unlock();
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(100));

        // 4. Отправляем ЛОГИКУ ВЛАЖНОСТИ (Humidity Logic)
        settings_lock();
        snprintf(uart_buf, sizeof(uart_buf), "HUM_LOG:deadZonePercent=%.1f,humidityHysteresis=1.0\n", 
                 sys_settings.deadZonePercent);
        settings_unlock();
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(100));

        // 5. Отправляем СТАТИСТИКУ (Statistics)
        // В реальном проекте эти счетчики нужно инкрементировать и сохранять в NVS при загрузке
        snprintf(uart_buf, sizeof(uart_buf), "STAT:resetCount=%lu,autoRebootCounter=%lu\n", 
                 sys_settings.autoRebootCounter, // Заглушка, используем что есть
                 sys_settings.autoRebootCounter);
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(100));

        // 6. Отправляем СТАТУС ЗАМКА И ДВЕРИ (K10)
        // Читаем реальный пин двери
        bool is_door_closed = (gpio_get_level(PIN_DOOR_SENSOR) == 0);
        bool is_lock_active = gesture_is_lock_pressed(); // Или статус реле замка
        snprintf(uart_buf, sizeof(uart_buf), "K10_STAT:LOCK:%s,DOOR:%s,HOLD:1000\n", 
                 is_lock_active ? "active" : "inactive",
                 is_door_closed ? "closed" : "open");
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
    }
}