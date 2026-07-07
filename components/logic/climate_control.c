#include "climate_control.h"
#include "sht40_driver.h"
#include "display_manager.h"
#include "settings_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CLIMATE_CTRL";

// Доступ к глобальному хэндлу дисплея из main.c
extern display_handle_t g_display_handle;

static void climate_control_task(void *pvParameters) {
    ESP_LOGI(TAG, "Climate Control Task started on Core 1");

    // [ИСПРАВЛЕНИЕ 3]: Ждем 500 мс, чтобы MPR121 успели инициализироваться первыми
    vTaskDelay(pdMS_TO_TICKS(500));

    // Инициализация шины и датчиков SHT40
    if (sht40_driver_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SHT40 sensors. Halting climate control.");
        vTaskDelete(NULL);
    }

    cabinet_climate_data_t climate_data;
    ui_status_data_t ui_data = {
        .weight_grams = 0,
        .is_guitar_present = false,
        .wifi_rssi_percent = 0,
        .is_ble_connected = false,
        .is_locked = true
    };

    while (1) {
        // 1. Потокобезопасный опрос всех датчиков
        sht40_read_all(&climate_data);

        // 2. Берем данные с основного датчика (возле гитары)
        sht40_reading_t *main_sensor = &climate_data.sensors[SHT40_MAIN];

        if (main_sensor->is_valid) {
            ui_data.temperature = main_sensor->temperature;
            ui_data.humidity = main_sensor->humidity;

            // Читаем целевую влажность из NVS с защитой мьютексом
            settings_lock();
            int target_hum = sys_settings.targetHumidity;
            settings_unlock();

            // Простая логика визуализации (позже здесь будет ПИД-регулятор)
            ui_data.is_humidifying = (main_sensor->humidity < (target_hum - 2.0f));
            ui_data.is_dehumidifying = (main_sensor->humidity > (target_hum + 2.0f));
            ui_data.is_heating = false; // ТЭН пока выключен
        } else {
            // Флаг ошибки сенсора для UI
            ui_data.temperature = -100.0f;
            ui_data.humidity = -100.0f;
        }

        // 3. Обновляем экран
        if (g_display_handle != NULL) {
            display_manager_update_status(g_display_handle, &ui_data);
        }

        // Цикл ПИД-регулятора и опроса датчиков — 2 секунды
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

esp_err_t climate_control_init(void) {
    BaseType_t res = xTaskCreatePinnedToCore(
        climate_control_task, 
        "climate_task", 
        4096, 
        NULL, 
        3, 
        NULL, 
        1 // Core 1
    );
    return (res == pdPASS) ? ESP_OK : ESP_FAIL;
}