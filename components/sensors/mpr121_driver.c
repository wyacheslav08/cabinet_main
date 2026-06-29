#include "mpr121_driver.h"
#include "i2c_manager.h"
#include "hw_config.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "MPR121";

// Вспомогательная функция записи регистра
static esp_err_t write_reg(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(I2C_MASTER_NUM, MPR121_I2C_ADDRESS, data, 2, pdMS_TO_TICKS(100));
}

esp_err_t mpr121_init(uint8_t mux_channel, uint8_t touch_threshold, uint8_t release_threshold) {
    i2c_manager_lock();
    esp_err_t err = i2c_manager_set_mux(MUX_ADDR_TOUCH, mux_channel);
    if (err != ESP_OK) goto exit;

    // Soft reset
    write_reg(0x80, 0x63);
    vTaskDelay(pdMS_TO_TICKS(10)); // Ждем перезагрузки чипа

    // Отключаем электроды перед настройкой (ECR = 0x00)
    write_reg(0x5E, 0x00);

    // Настройка порогов для всех 12 электродов
    for (uint8_t i = 0; i < 12; i++) {
        write_reg(0x41 + (2 * i), touch_threshold);
        write_reg(0x42 + (2 * i), release_threshold);
    }

    // Базовые настройки фильтрации из даташита
    write_reg(0x2B, 0x01); // MHD Rising
    write_reg(0x2C, 0x01); // NHD Amount Rising
    write_reg(0x2D, 0x00); // NCL Rising
    write_reg(0x2E, 0x00); // FDL Rising

    write_reg(0x2F, 0x01); // MHD Falling
    write_reg(0x30, 0x01); // NHD Amount Falling
    write_reg(0x31, 0xFF); // NCL Falling
    write_reg(0x32, 0x02); // FDL Falling

    // Включаем электроды (0..11) и базовое отслеживание
    err = write_reg(0x5E, 0x8F);

exit:
    i2c_manager_unlock();
    if (err == ESP_OK) ESP_LOGI(TAG, "MPR121 initialized on channel %d", mux_channel);
    else ESP_LOGE(TAG, "Failed to init MPR121 on channel %d", mux_channel);
    
    return err;
}

esp_err_t mpr121_get_touched(uint8_t mux_channel, uint16_t *touched_mask) {
    uint8_t reg = 0x00; // Регистр статуса касаний (LSB)
    uint8_t rx_data[2];

    i2c_manager_lock();
    esp_err_t err = i2c_manager_set_mux(MUX_ADDR_TOUCH, mux_channel);
    if (err == ESP_OK) {
        err = i2c_master_write_read_device(I2C_MASTER_NUM, MPR121_I2C_ADDRESS, 
                                           &reg, 1, rx_data, 2, pdMS_TO_TICKS(100));
    }
    i2c_manager_unlock();

    if (err == ESP_OK) {
        *touched_mask = (rx_data[1] << 8) | rx_data[0];
    }
    return err;
}