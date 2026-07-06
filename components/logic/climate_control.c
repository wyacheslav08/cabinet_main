#include "climate_control.h"
#include "sht40_driver.h"
#include "display_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "CLIMATE_TEST";

// Внешний хэндл дисплея, который инициализируется в main.c
extern display_handle_t s_display_handle;

/**
 * @brief Вспомогательная функция для форматирования данных датчика в строку.
 * Если датчик отвечает (is_valid == true) — выводит значения.
 * Если датчик отключен или сбой CRC — выводит "NON".
 */
static void format_sensor_data(const sht40_reading_t *reading, char *out_buffer, size_t max_len) {
    if (reading->is_valid) {
        snprintf(out_buffer, max_len, "%.1f°C / %.0f%%", reading->temperature, reading->humidity);
    } else {
        snprintf(out_buffer, max_len, "NON");
    }
}

/**
 * @brief Тестовая задача опроса климата (Без логики управления ШИМ).
 * Привязана к Core 1 (APP_CPU). Опрашивает 4 датчика и шлет данные в UI.
 */
void climate_control_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting Climate Control Sensor Test Task on Core 1...");

    // 1. Инициализация подсистемы датчиков SHT40 (проверка шины и мультиплексора)
    sht40_driver_init();

    cabinet_climate_data_t climate_data;
    char str_main[24], str_hum[24], str_dehum[24], str_ext[24];

    // Статическая структура для UI, чтобы не заполнять мусором остальные поля (весы, wifi)
    static ui_status_data_t ui_data = {
        .weight_grams = 0,
        .is_guitar_present = false,
        .wifi_rssi_percent = 100,
        .is_ble_connected = false,
        .is_locked = true,
        .is_heating = false,
        .is_humidifying = false,
        .is_dehumidifying = false
    };

    while (1) {
        // 2. Потокобезопасный опрос всех 4-х датчиков через PCA9548A
        sht40_read_all(&climate_data);

        // 3. Форматируем результаты в строки (с проверкой на "NON")
        format_sensor_data(&climate_data.sensors[SHT40_MAIN],         str_main,  sizeof(str_main));
        format_sensor_data(&climate_data.sensors[SHT40_HUMIDIFIER],   str_hum,   sizeof(str_hum));
        format_sensor_data(&climate_data.sensors[SHT40_DEHUMIDIFIER], str_dehum, sizeof(str_dehum));
        format_sensor_data(&climate_data.sensors[SHT40_EXTERNAL],     str_ext,   sizeof(str_ext));

        // 4. Вывод таблицы показаний всех 4-х датчиков в терминал (Монитор порта)
        ESP_LOGI(TAG, "==================================================");
        ESP_LOGI(TAG, " [1] MAIN (Guitar)   : %s", str_main);
        ESP_LOGI(TAG, " [2] HUMIDIFIER      : %s", str_hum);
        ESP_LOGI(TAG, " [3] DEHUMIDIFIER    : %s", str_dehum);
        ESP_LOGI(TAG, " [4] EXTERNAL (Room) : %s", str_ext);
        ESP_LOGI(TAG, "==================================================");

        // 5. Подготовка данных для ЖК-экрана ST7735
        // Передаем показания ОСНОВНОГО датчика (SHT40_MAIN) в статус-бар экрана.
        if (climate_data.sensors[SHT40_MAIN].is_valid) {
            ui_data.temperature = climate_data.sensors[SHT40_MAIN].temperature;
            ui_data.humidity    = climate_data.sensors[SHT40_MAIN].humidity;
        } else {
            // Архитектурный флаг: передаем заведомо невозможную температуру (-100.0),
            // чтобы модуль дисплея понял, что датчик в состоянии "NON"
            ui_data.temperature = -100.0f; 
            ui_data.humidity    = -100.0f;
        }

        // 6. Потокобезопасная отправка данных на экран (защищено мьютексом LVGL внутри)
        if (s_display_handle != NULL) {
            display_manager_update_status(s_display_handle, &ui_data);
        }

        // Пауза 2 секунды перед следующим циклом опроса
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}