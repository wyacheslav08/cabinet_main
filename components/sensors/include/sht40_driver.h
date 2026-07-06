#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Идентификаторы климатических датчиков в системе.
 */
typedef enum {
    SHT40_MAIN = 0,       // Основной датчик (у гитары)
    SHT40_HUMIDIFIER,     // Датчик в модуле увлажнения
    SHT40_DEHUMIDIFIER,   // Датчик в модуле осушения
    SHT40_EXTERNAL,       // Внешний датчик (комната)
    SHT40_MAX_SENSORS     // Количество датчиков (4)
} sht40_sensor_id_t;

/**
 * @brief Структура показаний одного датчика.
 */
typedef struct {
    float temperature;    // Температура в градусах Цельсия (-40.0 ... +125.0 °C)
    float humidity;       // Относительная влажность в процентах (0.0 ... 100.0 %)
    bool  is_valid;       // Флаг успешного чтения и совпадения CRC-8
} sht40_reading_t;

/**
 * @brief Сводная структура климата по всем 4 точкам кабинета.
 */
typedef struct {
    sht40_reading_t sensors[SHT40_MAX_SENSORS];
    uint32_t        timestamp_ms; // Время последнего опроса (от старта системы)
} cabinet_climate_data_t;

/**
 * @brief Инициализация подсистемы датчиков SHT40.
 * Проверяет наличие всех 4-х чипов на шине I2C через мультиплексор.
 * 
 * @return ESP_OK если хотя бы основной датчик отвечает, иначе код ошибки.
 */
esp_err_t sht40_driver_init(void);

/**
 * @brief Чтение показаний конкретного датчика с валидацией контрольной суммы CRC-8.
 * ВАЖНО: Функция неблокирующая для шины I2C (освобождает мьютекс на время замера АЦП чипа).
 * 
 * @param[in]  sensor_id Идентификатор датчика.
 * @param[out] out_reading Указатель на структуру для записи данных.
 * @return ESP_OK при успехе, ESP_ERR_TIMEOUT или ESP_ERR_INVALID_CRC при сбое.
 */
esp_err_t sht40_read_sensor(sht40_sensor_id_t sensor_id, sht40_reading_t *out_reading);

/**
 * @brief Последовательный опрос всех 4-х датчиков кабинета.
 * Рекомендуется вызывать из задачи климат-контроля с интервалом 1-2 секунды.
 * 
 * @param[out] out_data Указатель на сводную структуру климата.
 * @return ESP_OK при завершении опроса.
 */
esp_err_t sht40_read_all(cabinet_climate_data_t *out_data);

#ifdef __cplusplus
}
#endif