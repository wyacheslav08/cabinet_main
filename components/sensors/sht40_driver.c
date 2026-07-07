/**
 * @file sht40_driver.c
 * @brief Драйвер высокоточных датчиков температуры и влажности SHT40.
 * @note Реализует неблокирующий опрос через I2C мультиплексор PCA9548A.
 */

#include "sht40_driver.h"
#include "hw_config.h"
#include "i2c_manager.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SHT40_DRV";

// Таблица соответствия ID датчика и канала мультиплексора PCA9548A
static const uint8_t SENSOR_MUX_CHANNELS[SHT40_MAX_SENSORS] = {
    [SHT40_MAIN]         = MUX_CH_SHT_MAIN,
    [SHT40_HUMIDIFIER]   = MUX_CH_SHT_HUMIDIFIER,
    [SHT40_DEHUMIDIFIER] = MUX_CH_SHT_DEHUMIDIFIER,
    [SHT40_EXTERNAL]     = MUX_CH_SHT_EXTERNAL
};

// Названия датчиков для удобного логирования
static const char* SENSOR_NAMES[SHT40_MAX_SENSORS] = {
    "MAIN (Guitar)",
    "HUMIDIFIER",
    "DEHUMIDIFIER",
    "EXTERNAL (Room)"
};

// =========================================================================
// ВНУТРЕННИЕ МАТЕМАТИЧЕСКИЕ ФУНКЦИИ
// =========================================================================

/**
 * @brief Вычисление контрольной суммы CRC-8 по стандарту Sensirion.
 */
static uint8_t calculate_crc8(const uint8_t *data, int len) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 8; bit > 0; --bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = (crc << 1);
            }
        }
    }
    return crc;
}

/**
 * @brief Преобразование сырых данных АЦП в физические величины.
 */
static void convert_raw_data(uint16_t raw_temp, uint16_t raw_hum, float *out_temp, float *out_hum) {
    *out_temp = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
    
    float rh = -6.0f + 125.0f * ((float)raw_hum / 65535.0f);
    if (rh < 0.0f) rh = 0.0f;
    if (rh > 100.0f) rh = 100.0f;
    *out_hum = rh;
}

// =========================================================================
// ПУБЛИЧНЫЙ API ДРАЙВЕРА
// =========================================================================

esp_err_t sht40_driver_init(void) {
    ESP_LOGI(TAG, "Initializing 4x SHT40 climate sensors...");
    int active_sensors = 0;

    for (int i = 0; i < SHT40_MAX_SENSORS; i++) {
        i2c_manager_lock();
        
        // 1. Переключаем мультиплексор
        esp_err_t err = i2c_manager_set_mux(MUX_ADDR_SENSORS, SENSOR_MUX_CHANNELS[i]);
        if (err == ESP_OK) {
            // 2. Проверяем присутствие чипа (отправка адреса без данных)
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (SHT40_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
            i2c_master_stop(cmd);
            
            err = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(50));
            i2c_cmd_link_delete(cmd);
            
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Sensor [%d] %s -> DETECTED (MUX CH: %d)", i, SENSOR_NAMES[i], SENSOR_MUX_CHANNELS[i]);
                active_sensors++;
            } else {
                ESP_LOGW(TAG, "Sensor [%d] %s -> NOT RESPONDING", i, SENSOR_NAMES[i]);
            }
        }
        i2c_manager_unlock();
    }

    if (active_sensors == 0) {
        ESP_LOGE(TAG, "CRITICAL: No SHT40 sensors detected! Check wiring.");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t sht40_read_sensor(sht40_sensor_id_t sensor_id, sht40_reading_t *out_reading) {
    if (sensor_id >= SHT40_MAX_SENSORS || out_reading == NULL) return ESP_ERR_INVALID_ARG;

    out_reading->is_valid = false;
    uint8_t cmd_meas = SHT40_CMD_MEAS_HIGH_PREC;

    // --- ЭТАП 1: Отправка команды на измерение ---
    i2c_manager_lock();
    esp_err_t err = i2c_manager_set_mux(MUX_ADDR_SENSORS, SENSOR_MUX_CHANNELS[sensor_id]);
    if (err == ESP_OK) {
        err = i2c_master_write_to_device(I2C_MASTER_NUM, SHT40_I2C_ADDR, 
                                         &cmd_meas, 1, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    }
    i2c_manager_unlock(); // ВАЖНО: Освобождаем шину!

    if (err != ESP_OK) return err;

    // --- ЭТАП 2: Ожидание замера АЦП датчиком (8-10 мс) ---
    // Процессор не блокируется, шина I2C свободна для других устройств
    vTaskDelay(pdMS_TO_TICKS(10));

    // --- ЭТАП 3: Чтение результата ---
    uint8_t rx_buf[6] = {0};
    
    i2c_manager_lock();
    // Повторно подтверждаем канал MUX (защита от переключения другой задачей за эти 10 мс)
    err = i2c_manager_set_mux(MUX_ADDR_SENSORS, SENSOR_MUX_CHANNELS[sensor_id]);
    if (err == ESP_OK) {
        err = i2c_master_read_from_device(I2C_MASTER_NUM, SHT40_I2C_ADDR, 
                                          rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    }
    i2c_manager_unlock();

    if (err != ESP_OK) return err;

    // --- ЭТАП 4: Валидация CRC ---
    if (calculate_crc8(&rx_buf[0], 2) != rx_buf[2] || calculate_crc8(&rx_buf[3], 2) != rx_buf[5]) {
        ESP_LOGW(TAG, "CRC mismatch on sensor [%s]", SENSOR_NAMES[sensor_id]);
        return ESP_ERR_INVALID_CRC;
    }

    // --- ЭТАП 5: Конвертация ---
    uint16_t raw_temp = (rx_buf[0] << 8) | rx_buf[1];
    uint16_t raw_hum  = (rx_buf[3] << 8) | rx_buf[4];

    convert_raw_data(raw_temp, raw_hum, &out_reading->temperature, &out_reading->humidity);
    out_reading->is_valid = true;

    return ESP_OK;
}

// [КРИТИЧНО] Та самая функция, которую искал линковщик!
esp_err_t sht40_read_all(cabinet_climate_data_t *out_data) {
    if (out_data == NULL) return ESP_ERR_INVALID_ARG;

    for (int i = 0; i < SHT40_MAX_SENSORS; i++) {
        esp_err_t err = sht40_read_sensor((sht40_sensor_id_t)i, &out_data->sensors[i]);
        if (err != ESP_OK) {
            out_data->sensors[i].is_valid = false;
        }
    }
    
    out_data->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}