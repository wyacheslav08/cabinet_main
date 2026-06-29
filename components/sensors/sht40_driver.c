#include "sht40_driver.h"
#include "i2c_manager.h"
#include "hw_config.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "SHT40";

// Функция вычисления контрольной суммы (из даташита SHT4x)
static uint8_t calc_crc8(const uint8_t *data, int len) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x31;
            else crc <<= 1;
        }
    }
    return crc;
}

esp_err_t sht40_read(uint8_t mux_channel, float *temperature, float *humidity) {
    uint8_t cmd = 0xFD; // Команда: High precision measurement
    uint8_t rx_data[6];
    
    i2c_manager_lock(); // Захватываем шину I2C
    
    // 1. Переключаем мультиплексор на нужный канал
    esp_err_t err = i2c_manager_set_mux(MUX_ADDR_SENSORS, mux_channel);
    if (err != ESP_OK) {
        i2c_manager_unlock();
        return err;
    }

    // 2. Отправляем команду измерения
    err = i2c_master_write_to_device(I2C_MASTER_NUM, SHT40_I2C_ADDRESS, &cmd, 1, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        i2c_manager_unlock();
        return err;
    }
    
    // 3. Ждем 10 мс (время измерения SHT40 на High precision)
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // 4. Читаем 6 байт результата
    err = i2c_master_read_from_device(I2C_MASTER_NUM, SHT40_I2C_ADDRESS, rx_data, 6, pdMS_TO_TICKS(100));
    i2c_manager_unlock(); // Освобождаем шину
    
    if (err != ESP_OK) return err;

    // 5. Проверяем CRC
    if (calc_crc8(&rx_data[0], 2) != rx_data[2] || calc_crc8(&rx_data[3], 2) != rx_data[5]) {
        ESP_LOGE(TAG, "CRC Error on channel %d", mux_channel);
        return ESP_ERR_INVALID_CRC;
    }

    // 6. Вычисляем физические значения
    uint16_t t_ticks = (rx_data[0] << 8) | rx_data[1];
    uint16_t rh_ticks = (rx_data[3] << 8) | rx_data[4];

    *temperature = -45.0f + 175.0f * ((float)t_ticks / 65535.0f);
    *humidity = -6.0f + 125.0f * ((float)rh_ticks / 65535.0f);

    // Ограничиваем влажность 0-100% (требование даташита)
    if (*humidity > 100.0f) *humidity = 100.0f;
    if (*humidity < 0.0f) *humidity = 0.0f;

    return ESP_OK;
}