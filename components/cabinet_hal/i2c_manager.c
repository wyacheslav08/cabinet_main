/**
 * @file i2c_manager.c
 * @brief Потокобезопасный драйвер шины I2C (Адаптирован под 1 мультиплексор).
 */

#include "i2c_manager.h"
#include "hw_config.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "I2C_MGR";
static SemaphoreHandle_t i2c_mutex = NULL;

// Кэш текущего канала единственного мультиплексора SHT40 (255 = состояние неизвестно)
static uint8_t current_mux_channel = 255; 

esp_err_t i2c_manager_set_mux(uint8_t mux_addr, uint8_t channel) {
    if (channel > 7) return ESP_ERR_INVALID_ARG;
    
    // Оптимизация: не переключаем, если уже на нужном канале
    if (mux_addr == MUX_ADDR_SENSORS && current_mux_channel == channel) {
        return ESP_OK; 
    }

    uint8_t mux_cmd = (1 << channel);
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (mux_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, mux_cmd, true);
    i2c_master_stop(cmd);
    
    esp_err_t err = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
                                               
    if (err == ESP_OK) {
        if (mux_addr == MUX_ADDR_SENSORS) {
            current_mux_channel = channel;
        }
        // Даем транзисторам внутри PCA9548A время (2 мс) на физическое переключение
        vTaskDelay(pdMS_TO_TICKS(2)); 
    } else {
        if (mux_addr == MUX_ADDR_SENSORS) {
            current_mux_channel = 255; // Сбрасываем кэш при ошибке
        }
        ESP_LOGE(TAG, "MUX switch failed (ADDR: 0x%02X, CH: %d, ERR: %s)", mux_addr, channel, esp_err_to_name(err));
        
        // Безопасный сброс аппаратных буферов контроллера ESP32
        i2c_reset_tx_fifo(I2C_MASTER_NUM);
        i2c_reset_rx_fifo(I2C_MASTER_NUM);
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
        // [КРИТИЧЕСКИ ВАЖНО] Увеличиваем аппаратный таймаут шины I2C до максимума
        i2c_set_timeout(I2C_MASTER_NUM, 0xFFFFF);
        
        // Даем шине 100 мс на стабилизацию питания после инициализации
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "I2C Master initialized successfully at %d Hz", I2C_MASTER_FREQ_HZ);
    }
    return err;
}

void i2c_manager_lock(void) {
    xSemaphoreTake(i2c_mutex, portMAX_DELAY);
}

void i2c_manager_unlock(void) {
    xSemaphoreGive(i2c_mutex);
}