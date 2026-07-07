/**
 * @file mpr121_driver.c
 * @brief Драйвер 12-канального емкостного сенсора MPR121.
 * @note Реализует потокобезопасную работу через I2C мультиплексор PCA9548A.
 */

#include "mpr121_driver.h"
#include "hw_config.h"
#include "i2c_manager.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MPR121";

// =========================================================================
// РЕГИСТРЫ MPR121
// =========================================================================
#define MPR121_TOUCH_STATUS_L   0x00
#define MPR121_ELE0_TOUCH_TH    0x41
#define MPR121_ELE0_REL_TH      0x42
#define MPR121_MHD_R            0x2B
#define MPR121_NHD_R            0x2C
#define MPR121_NCL_R            0x2D
#define MPR121_FDL_R            0x2E
#define MPR121_MHD_F            0x2F
#define MPR121_NHD_F            0x30
#define MPR121_NCL_F            0x31
#define MPR121_FDL_F            0x32
#define MPR121_ELE_CFG          0x5E
#define MPR121_AFE_CFG          0x5C
#define MPR121_FILTER_CFG       0x5D
#define MPR121_AUTO_CFG_0       0x7B
#define MPR121_AUTO_CFG_1       0x7C
#define MPR121_USL              0x7D
#define MPR121_LSL              0x7E
#define MPR121_TL               0x7F
#define MPR121_SOFT_RESET       0x80

// =========================================================================
// ВНУТРЕННИЕ ФУНКЦИИ (Без мьютекса, вызываются только когда шина захвачена)
// =========================================================================

/**
 * @brief Запись значения в регистр MPR121.
 * ВАЖНО: Вызывать только между i2c_manager_lock() и i2c_manager_unlock()
 */
static esp_err_t write_reg_nolock(uint8_t reg, uint8_t val) {
    uint8_t data[2] = {reg, val};
    return i2c_master_write_to_device(I2C_MASTER_NUM, MPR121_I2C_ADDRESS, data, 2, pdMS_TO_TICKS(50));
}

// =========================================================================
// ПУБЛИЧНЫЙ API
// =========================================================================

esp_err_t mpr121_init(uint8_t mux_channel, uint8_t touch_thresh, uint8_t release_thresh) {
    esp_err_t err;

    // 1. ПРОГРАММНЫЙ СБРОС (Soft Reset)
    i2c_manager_lock();
    if (i2c_manager_set_mux(MUX_ADDR_TOUCH, mux_channel) == ESP_OK) {
        write_reg_nolock(MPR121_SOFT_RESET, 0x63);
    }
    i2c_manager_unlock();

    // Даем чипу 2 мс на перезагрузку (В это время I2C шина свободна для дисплея/климата)
    vTaskDelay(pdMS_TO_TICKS(2));

    // 2. ОСНОВНАЯ КОНФИГУРАЦИЯ
    i2c_manager_lock();
    err = i2c_manager_set_mux(MUX_ADDR_TOUCH, mux_channel);
    if (err != ESP_OK) {
        i2c_manager_unlock();
        ESP_LOGE(TAG, "Failed to switch MUX to channel %d for init", mux_channel);
        return err;
    }

    // Перевод в режим Stop (Необходимо для изменения настроек)
    write_reg_nolock(MPR121_ELE_CFG, 0x00);

    // Настройка порогов срабатывания для всех 12 электродов
    for (int i = 0; i < 12; i++) {
        write_reg_nolock(MPR121_ELE0_TOUCH_TH + (i * 2), touch_thresh);
        write_reg_nolock(MPR121_ELE0_REL_TH + (i * 2), release_thresh);
    }

    // Настройка фильтров базовой линии (Baseline Tracking)
    write_reg_nolock(MPR121_MHD_R, 0x01);
    write_reg_nolock(MPR121_NHD_R, 0x01);
    write_reg_nolock(MPR121_NCL_R, 0x00);
    write_reg_nolock(MPR121_FDL_R, 0x00);

    write_reg_nolock(MPR121_MHD_F, 0x01);
    write_reg_nolock(MPR121_NHD_F, 0x01);
    write_reg_nolock(MPR121_NCL_F, 0xFF);
    write_reg_nolock(MPR121_FDL_F, 0x02);

    // Настройка токов и времени заряда (Берется из mpr121_driver.h)
    write_reg_nolock(MPR121_AFE_CFG, MPR121_MANUAL_CDC);
    write_reg_nolock(MPR121_FILTER_CFG, MPR121_MANUAL_CDT);

    // Настройка Авто-конфигурации
    if (MPR121_USE_AUTO_CONFIG) {
        write_reg_nolock(MPR121_AUTO_CFG_0, 0x0B); 
        write_reg_nolock(MPR121_AUTO_CFG_1, 0x00);
        // Лимиты для 3.3V
        write_reg_nolock(MPR121_USL, 0xC8); 
        write_reg_nolock(MPR121_LSL, 0x82); 
        write_reg_nolock(MPR121_TL,  0xB4); 
    }

    // Включение чипа и электродов (Запуск Run Mode)
    // 0x0C = Включены все 12 электродов.
    // Если MPR121_BASELINE_TRACKING = 0, добавляем биты отключения трекинга (0xC0)
    uint8_t ele_cfg_val = 0x0C;
    if (!MPR121_BASELINE_TRACKING) {
        ele_cfg_val |= 0xC0; 
    }
    write_reg_nolock(MPR121_ELE_CFG, ele_cfg_val);

    i2c_manager_unlock();
    
    ESP_LOGD(TAG, "MPR121 on MUX %d initialized", mux_channel);
    return ESP_OK;
}

esp_err_t mpr121_get_touched(uint8_t mux_channel, uint16_t *touched_mask) {
    if (touched_mask == NULL) return ESP_ERR_INVALID_ARG;

    i2c_manager_lock();
    
    // Переключаем мультиплексор на сенсорную панель
    esp_err_t err = i2c_manager_set_mux(MUX_ADDR_TOUCH, mux_channel);
    if (err == ESP_OK) {
        uint8_t reg = MPR121_TOUCH_STATUS_L;
        uint8_t data[2] = {0, 0};
        
        // Читаем 2 байта (статус 12 электродов) за одну I2C транзакцию
        err = i2c_master_write_read_device(I2C_MASTER_NUM, MPR121_I2C_ADDRESS, 
                                           &reg, 1, 
                                           data, 2, 
                                           pdMS_TO_TICKS(50));
        if (err == ESP_OK) {
            *touched_mask = (data[1] << 8) | data[0];
            *touched_mask &= 0x0FFF; // Отсекаем старшие 4 бита (они не используются)
        }
    }
    
    i2c_manager_unlock();
    return err;
}