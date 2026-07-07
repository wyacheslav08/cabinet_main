#include "mpr121_driver.h"
#include "hw_config.h"
#include "i2c_manager.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MPR121";

#define MPR121_TOUCH_STATUS_L   0x00
#define MPR121_ELE0_T           0x41
#define MPR121_ELE0_R           0x42
#define MPR121_ELE_CFG          0x5E
#define MPR121_SOFT_RESET       0x80

// Функция записи аналогично i2c_write_reg из вашего теста
static esp_err_t write_reg_nolock(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(I2C_MASTER_NUM, MPR121_I2C_ADDRESS, buf, 2, pdMS_TO_TICKS(100));
}

esp_err_t mpr121_init(uint8_t mux_channel, uint8_t touch_thresh, uint8_t release_thresh) {
    esp_err_t ret;

    i2c_manager_lock();
    ret = i2c_manager_set_mux(MUX_ADDR_TOUCH, mux_channel);
    if (ret != ESP_OK) {
        i2c_manager_unlock();
        return ret;
    }

    // 1. Soft Reset
    write_reg_nolock(MPR121_SOFT_RESET, 0x63);
    i2c_manager_unlock(); // Освобождаем мьютекс на время паузы

    vTaskDelay(pdMS_TO_TICKS(5)); // Пауза в точности как в тесте

    i2c_manager_lock();
    i2c_manager_set_mux(MUX_ADDR_TOUCH, mux_channel); // Повторно подтверждаем канал

    // 2. Stop Mode для настройки
    write_reg_nolock(MPR121_ELE_CFG, 0x00);

    // 3. Настройка порогов
    for (int i = 0; i < 12; i++) {
        write_reg_nolock(MPR121_ELE0_T + (i * 2), touch_thresh);
        write_reg_nolock(MPR121_ELE0_R + (i * 2), release_thresh);
    }

    // 4. Включаем электроды
    ret = write_reg_nolock(MPR121_ELE_CFG, 0x0C);
    
    i2c_manager_unlock();
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "MPR121 initialized on MUX CH: %d", mux_channel);
    }
    return ret;
}

esp_err_t mpr121_get_touched(uint8_t mux_channel, uint16_t *touched_mask) {
    if (touched_mask == NULL) return ESP_ERR_INVALID_ARG;

    i2c_manager_lock();
    esp_err_t err = i2c_manager_set_mux(MUX_ADDR_TOUCH, mux_channel);
    if (err == ESP_OK) {
        uint8_t reg = MPR121_TOUCH_STATUS_L;
        uint8_t data[2] = {0, 0};
        
        err = i2c_master_write_read_device(I2C_MASTER_NUM, MPR121_I2C_ADDRESS, 
                                           &reg, 1, data, 2, pdMS_TO_TICKS(100));
        if (err == ESP_OK) {
            *touched_mask = data[0] | ((data[1] & 0x0F) << 8);
        }
    }
    i2c_manager_unlock();
    return err;
}