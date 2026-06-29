#include "i2c_manager.h"
#include "hw_config.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "I2C_MGR";
static SemaphoreHandle_t i2c_mutex = NULL;
static uint8_t current_mux_channel[2] = {255, 255}; // Несуществующий канал для старта

esp_err_t i2c_manager_set_mux(uint8_t mux_addr, uint8_t channel) {
    if (channel > 7) return ESP_ERR_INVALID_ARG;
    
    // Определяем индекс мультиплексора (0 для 0x70, 1 для 0x71)
    int mux_idx = (mux_addr == MUX_ADDR_TOUCH) ? 0 : 1;
    
    // Оптимизация: если на этом мультиплексоре уже выбран этот канал, выходим
    if (current_mux_channel[mux_idx] == channel) {
        return ESP_OK; 
    }

    uint8_t mux_cmd = (1 << channel);
    
    esp_err_t err = i2c_master_write_to_device(I2C_MASTER_NUM, mux_addr, 
                                               &mux_cmd, 1, 
                                               pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
                                               
    if (err == ESP_OK) {
        current_mux_channel[mux_idx] = channel;
    } else {
        //ESP_LOGE(TAG, "Failed to switch MUX 0x%02X to channel %d", mux_addr, channel);
        current_mux_channel[mux_idx] = 255;
    }
    return err;
}

esp_err_t i2c_manager_init(void) {
    i2c_mutex = xSemaphoreCreateMutex();
    if (i2c_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C mutex");
        return ESP_ERR_NO_MEM;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) return err;

    err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "I2C Master initialized successfully");
    }
    return err;
}

void i2c_manager_lock(void) {
    xSemaphoreTake(i2c_mutex, portMAX_DELAY);
}

void i2c_manager_unlock(void) {
    xSemaphoreGive(i2c_mutex);
}
