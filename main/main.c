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
        // Главный цикл телеметрии: раз в 3 секунды
        vTaskDelay(pdMS_TO_TICKS(3000)); 

        settings_lock();
        cabinet_settings_t s = sys_settings;
        settings_unlock();

         // --- 1. Чтение климатических данных и применение КАЛИБРОВКИ ---
        float current_temp = 0.0f, current_hum = 0.0f;
        if (sht40_read(MUX_CH_SHT40_TOP, &current_temp, &current_hum) == ESP_OK) {
            
            // Получаем смещения из памяти
            settings_lock();
            int8_t t_offset = sys_settings.tempOffsetTop;
            int8_t h_offset = sys_settings.humOffsetTop;
            settings_unlock();

            // Применяем калибровку
            current_temp += (float)t_offset;
            current_hum += (float)h_offset;

            // Ограничиваем влажность 0..100 после калибровки
            if (current_hum > 100.0f) current_hum = 100.0f;
            if (current_hum < 0.0f) current_hum = 0.0f;

            // Отправляем в Gateway
            snprintf(uart_buf, sizeof(uart_buf), "T:%.1f\n", current_temp);
            uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
            vTaskDelay(pdMS_TO_TICKS(150));

            snprintf(uart_buf, sizeof(uart_buf), "H:%.1f\n", current_hum);
            uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
            vTaskDelay(pdMS_TO_TICKS(150));
        }

        // 3. Отправка общих настроек (В точном формате Arduino для BLE_CHAR_GENERAL_SETTINGS_UUID)
        snprintf(uart_buf, sizeof(uart_buf), 
                 "targetHumidity=%d,lockHoldTime=%u,lockTimeIndex=%d,menuTimeoutOptionIndex=%d,"
                 "screenTimeoutOptionIndex=%d,doorSoundEnabled=%d,waterSilicaSoundEnabled=%d,"
                 "waterHeaterEnabled=%d,waterHeaterMaxTemp=%d\n", 
                 s.targetHumidity, s.lockHoldTime, s.lockTimeIndex, s.menuTimeoutOptionIndex,
                 s.screenTimeoutOptionIndex, s.doorSoundEnabled ? 1 : 0, s.waterSilicaSoundEnabled ? 1 : 0,
                 s.waterHeaterEnabled ? 1 : 0, s.waterHeaterMaxTemp);
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(150));

        // 4. Отправка информации о системе (Для BLE_CHAR_SYS_INFO_UUID)
        bool is_door_closed = (gpio_get_level(PIN_DOOR_SENSOR) == 0);
        snprintf(uart_buf, sizeof(uart_buf), 
                 "E:0.0,RES_W:OK,RES_S:OK,HUM_RELAY:OFF,VENT_RELAY:OFF,WATER_HEATER:OFF,WH_SAFE_SHUTDOWN:0\n");
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
        vTaskDelay(pdMS_TO_TICKS(150));

        // 5. Отправка статуса замка (Для BLE_CHAR_K10_UUID)
        snprintf(uart_buf, sizeof(uart_buf), "LOCK:%s,DOOR:%s,HOLD:%u\n", 
                 gesture_is_lock_pressed() ? "active" : "inactive",
                 is_door_closed ? "closed" : "open",
                 s.lockHoldTime);
        uart_link_send((const uint8_t*)uart_buf, strlen(uart_buf));
    }
    
    // Защита от выхода из цикла (стандарт FreeRTOS)
    vTaskDelete(NULL);
}