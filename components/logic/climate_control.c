#include "climate_control.h"
#include "sht40_driver.h"
#include "display_manager.h"
#include "settings_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "uart_link.h"

static const char *TAG = "CLIMATE_CTRL";

// Используем Mutex (FreeRTOS) вместо Spinlock!
static SemaphoreHandle_t climate_mutex = NULL;
static cabinet_climate_data_t g_latest_climate_data = {0};

void climate_get_latest_data(cabinet_climate_data_t *out_data) {
    if(out_data && climate_mutex) {
        xSemaphoreTake(climate_mutex, portMAX_DELAY);
        *out_data = g_latest_climate_data;
        xSemaphoreGive(climate_mutex);
    }
}

extern display_handle_t g_display_handle;

static void climate_control_task(void *pvParameters) {
    ESP_LOGI(TAG, "Climate Control Task started on Core 1");

    vTaskDelay(pdMS_TO_TICKS(1500));

    if (sht40_driver_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SHT40 sensors. Halting climate control.");
        vTaskDelete(NULL);
    }
    
    vTaskDelay(pdMS_TO_TICKS(500));

    cabinet_climate_data_t climate_data;
    ui_status_data_t ui_data = {
        .weight_grams = 0,
        .is_guitar_present = false,
        .wifi_rssi_percent = 0,
        .is_ble_connected = false,
        .is_locked = true
    };

    while (1) {
        // 1. Опрос датчиков (ЗАНИМАЕТ 40 мс)
        sht40_read_all(&climate_data);
        
        // 2. БЕЗОПАСНОЕ СОХРАНЕНИЕ ДЛЯ МЕНЮ (Мгновенно, без блокировки LVGL)
        xSemaphoreTake(climate_mutex, portMAX_DELAY);
        g_latest_climate_data = climate_data;
        xSemaphoreGive(climate_mutex);

        // 3. Подготовка данных для статус-бара
        sht40_reading_t *main_sensor = &climate_data.sensors[SHT40_MAIN];
        if (main_sensor->is_valid) {
            ui_data.temperature = main_sensor->temperature;
            ui_data.humidity = main_sensor->humidity;

            settings_lock();
            int target_hum = sys_settings.targetHumidity;
            settings_unlock();

            ui_data.is_humidifying = (main_sensor->humidity < (target_hum - 2.0f));
            ui_data.is_dehumidifying = (main_sensor->humidity > (target_hum + 2.0f));
            ui_data.is_heating = false; 
        } else {
            ui_data.temperature = -100.0f;
            ui_data.humidity = -100.0f;
        }

        // 4. Обновление экрана (Теперь безопасно, LVGL не заблокирует климат)
        if (g_display_handle != NULL) {
            display_manager_update_status(g_display_handle, &ui_data);
        }

        uart_link_send_telemetry(
            ui_data.temperature, ui_data.humidity, ui_data.weight_grams, 
            ui_data.is_locked, ui_data.is_guitar_present
        );

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

esp_err_t climate_control_init(void) {
    // Создаем мьютекс ДО запуска задачи
    if (climate_mutex == NULL) {
        climate_mutex = xSemaphoreCreateMutex();
    }

    BaseType_t res = xTaskCreatePinnedToCore(
        climate_control_task, 
        "climate_task", 
        4096, 
        NULL, 
        3, 
        NULL, 
        1 
    );
    return (res == pdPASS) ? ESP_OK : ESP_FAIL;
}