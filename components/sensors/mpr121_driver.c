#include "mpr121_driver.h"
#include "i2c_manager.h"
#include "hw_config.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "MPR121_DRV";

#define I2C_CHECK(x) do { esp_err_t _err = (x); if (_err != ESP_OK) return _err; } while(0)

static esp_err_t write_reg(uint8_t mux_channel, uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    i2c_manager_lock();
    esp_err_t err = i2c_manager_set_mux(MUX_ADDR_TOUCH, mux_channel);
    if (err == ESP_OK) {
        err = i2c_master_write_to_device(I2C_MASTER_NUM, MPR121_I2C_ADDRESS, data, 2, pdMS_TO_TICKS(20));
    }
    i2c_manager_unlock();
    return err;
}

esp_err_t mpr121_init(uint8_t mux_channel, uint8_t touch_thresh, uint8_t release_thresh) {
    // 1. Программный сброс чипа
    I2C_CHECK(write_reg(mux_channel, 0x80, 0x63));
    vTaskDelay(pdMS_TO_TICKS(10)); 

    // 2. Перевод чипа в Stop Mode (отключение электродов для настройки)
    I2C_CHECK(write_reg(mux_channel, 0x5E, 0x00));

    // 3. Настройка порогов для всех 12 электродов
    for (uint8_t i = 0; i < 12; i++) {
        I2C_CHECK(write_reg(mux_channel, 0x41 + (2 * i), touch_thresh));
        I2C_CHECK(write_reg(mux_channel, 0x42 + (2 * i), release_thresh));
    }

    // 4. Фильтрация шумов
    I2C_CHECK(write_reg(mux_channel, 0x2B, 0x01)); I2C_CHECK(write_reg(mux_channel, 0x2C, 0x01));
    I2C_CHECK(write_reg(mux_channel, 0x2D, 0x00)); I2C_CHECK(write_reg(mux_channel, 0x2E, 0x00));
    I2C_CHECK(write_reg(mux_channel, 0x2F, 0x01)); I2C_CHECK(write_reg(mux_channel, 0x30, 0x01));
    I2C_CHECK(write_reg(mux_channel, 0x31, 0xFF)); I2C_CHECK(write_reg(mux_channel, 0x32, 0x02));

#if MPR121_USE_AUTO_CONFIG
    // 5. Включение автоконфигурации (Оптимально для финального устройства)
    I2C_CHECK(write_reg(mux_channel, 0x7B, 0x0B)); 
    I2C_CHECK(write_reg(mux_channel, 0x7C, 0x00)); 
    I2C_CHECK(write_reg(mux_channel, 0x7D, 0xC8)); 
    I2C_CHECK(write_reg(mux_channel, 0x7E, 0x82)); 
    I2C_CHECK(write_reg(mux_channel, 0x7F, 0xB4)); 

    // 6. Настройка AFE (0x00 = ток подбирается автоматически алгоритмом чипа)
    I2C_CHECK(write_reg(mux_channel, 0x5C, 0x00)); 
    I2C_CHECK(write_reg(mux_channel, 0x5D, 0x20)); 
#else
    // 5. Отключение автоконфигурации (Для шумного тестового стенда)
    I2C_CHECK(write_reg(mux_channel, 0x7B, 0x00)); 
    
    // 6. Установка жестко заданных статичных токов и таймингов заряда
    I2C_CHECK(write_reg(mux_channel, 0x5C, MPR121_MANUAL_CDC)); 
    I2C_CHECK(write_reg(mux_channel, 0x5D, MPR121_MANUAL_CDT)); 
#endif

    // 7. Включение 12 электродов и настройка Baseline Tracking
#if MPR121_BASELINE_TRACKING
    // 0x8F: CL=10 (Baseline Tracking ВКЛ, старт с текущего значения), ELE=1111 (12 электродов)
    esp_err_t err = write_reg(mux_channel, 0x5E, 0x8F);
#else
    // 0x4F: CL=01 (Baseline Tracking ВЫКЛ, жесткая статика), ELE=1111 (12 электродов)
    esp_err_t err = write_reg(mux_channel, 0x5E, 0x4F);
#endif

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MPR121 [MUX Ch %d] init OK (AutoCfg: %d, Track: %d)", 
                 mux_channel, MPR121_USE_AUTO_CONFIG, MPR121_BASELINE_TRACKING);
    } else {
        ESP_LOGE(TAG, "MPR121 [MUX Ch %d] init failed", mux_channel);
    }
    return err;
}

esp_err_t mpr121_get_touched(uint8_t mux_channel, uint16_t *touched_mask) {
    uint8_t reg = 0x00;
    uint8_t rx_data[2] = {0, 0};

    i2c_manager_lock();
    esp_err_t err = i2c_manager_set_mux(MUX_ADDR_TOUCH, mux_channel);
    if (err == ESP_OK) {
        err = i2c_master_write_read_device(I2C_MASTER_NUM, MPR121_I2C_ADDRESS, 
                                           &reg, 1, rx_data, 2, pdMS_TO_TICKS(20));
    }
    i2c_manager_unlock();

    if (err == ESP_OK) {
        *touched_mask = (rx_data[1] << 8) | rx_data[0];
    } else {
        *touched_mask = 0; 
    }
    return err;
}