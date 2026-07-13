#include "mpr121_driver.h"
#include "hw_config.h"
#include "i2c_manager.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MPR121_DRV";

#define MPR121_TOUCH_STATUS_L   0x00
#define MPR121_ELE0_T           0x41
#define MPR121_ELE0_R           0x42
#define MPR121_ELE_CFG          0x5E
#define MPR121_SOFT_RESET       0x80

// Вспомогательная функция (вызывать только внутри i2c_manager_lock)
static esp_err_t write_reg_nolock(uint8_t addr, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(I2C_MASTER_NUM, addr, buf, 2, pdMS_TO_TICKS(50));
}

esp_err_t mpr121_init(uint8_t i2c_addr, uint8_t touch_thresh, uint8_t release_thresh) {
    esp_err_t ret;

    i2c_manager_lock();
    ret = write_reg_nolock(i2c_addr, MPR121_SOFT_RESET, 0x63);
    i2c_manager_unlock();

    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(5)); // Обязательная пауза после сброса

    i2c_manager_lock();
    write_reg_nolock(i2c_addr, MPR121_ELE_CFG, 0x00);
    for (int i = 0; i < 12; i++) {
        write_reg_nolock(i2c_addr, MPR121_ELE0_T + (i * 2), touch_thresh);
        write_reg_nolock(i2c_addr, MPR121_ELE0_R + (i * 2), release_thresh);
    }
    ret = write_reg_nolock(i2c_addr, MPR121_ELE_CFG, 0x0C);
    i2c_manager_unlock();
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "MPR121 initialized at 0x%02X", i2c_addr);
    }
    return ret;
}

esp_err_t mpr121_get_touched(uint8_t i2c_addr, uint16_t *touched_mask) {
    if (touched_mask == NULL) return ESP_ERR_INVALID_ARG;

    uint8_t reg = MPR121_TOUCH_STATUS_L;
    uint8_t data[2] = {0, 0};
    
    i2c_manager_lock();
    esp_err_t err = i2c_master_write_read_device(I2C_MASTER_NUM, i2c_addr, 
                                       &reg, 1, data, 2, pdMS_TO_TICKS(50));
    i2c_manager_unlock();

    if (err == ESP_OK) {
        *touched_mask = data[0] | ((data[1] & 0x0F) << 8);
    }
    return err;
}