#include "i2c_manager.h"
#include "hw_config.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h" // Для esp_rom_delay_us
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "I2C_MGR";
static SemaphoreHandle_t i2c_mutex = NULL;
static uint8_t current_mux_channel[2] = {255, 255}; // Несуществующий канал для старта

// Счетчик последовательных ошибок для детекта зависания шины
static uint8_t consecutive_i2c_errors = 0; 
#define MAX_I2C_ERRORS_BEFORE_RESET 3

// ==============================================================================
// ПРОЦЕДУРА ВОССТАНОВЛЕНИЯ ШИНЫ I2C (9-Clock Pulse Bit-Banging)
// ==============================================================================
static void i2c_bus_clear_and_reinit(void) {
    ESP_LOGW(TAG, "I2C bus stuck! Initiating hardware bus clear (9-clock pulse)...");

    // 1. Удаляем драйвер I2C, чтобы освободить пины
    i2c_driver_delete(I2C_MASTER_NUM);

    // 2. Настраиваем пины как GPIO с открытым стоком (Open-Drain) для безопасности!
    gpio_set_direction(I2C_MASTER_SDA_IO, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_direction(I2C_MASTER_SCL_IO, GPIO_MODE_INPUT_OUTPUT_OD);
    
    gpio_set_level(I2C_MASTER_SDA_IO, 1);
    gpio_set_level(I2C_MASTER_SCL_IO, 1);
    esp_rom_delay_us(20); // Ждем стабилизации линии

    // 3. Генерируем до 9 тактовых импульсов
    for (int i = 0; i < 9; i++) {
        // Если линия SDA отпущена (стала High), значит Slave отвис
        if (gpio_get_level(I2C_MASTER_SDA_IO) == 1) {
            break; 
        }
        
        gpio_set_level(I2C_MASTER_SCL_IO, 0);
        esp_rom_delay_us(5); // ~100 kHz
        gpio_set_level(I2C_MASTER_SCL_IO, 1);
        esp_rom_delay_us(5);
    }

    // 4. Генерируем STOP condition (SDA: Low -> High при SCL: High)
    gpio_set_level(I2C_MASTER_SDA_IO, 0);
    esp_rom_delay_us(5);
    gpio_set_level(I2C_MASTER_SCL_IO, 1);
    esp_rom_delay_us(5);
    gpio_set_level(I2C_MASTER_SDA_IO, 1);
    esp_rom_delay_us(20);

    // 5. Возвращаем управление аппаратному контроллеру ESP-IDF
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
        .clk_flags = 0,
    };
    
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);

    // 6. Сбрасываем стейт-машину
    current_mux_channel[0] = 255;
    current_mux_channel[1] = 255;
    consecutive_i2c_errors = 0;

    ESP_LOGI(TAG, "I2C Bus Clear complete. Driver reinitialized.");
}
// ==============================================================================

esp_err_t i2c_manager_set_mux(uint8_t mux_addr, uint8_t channel) {
    if (channel > 7) return ESP_ERR_INVALID_ARG;
    
    int mux_idx = (mux_addr == MUX_ADDR_TOUCH) ? 0 : 1;
    
    if (current_mux_channel[mux_idx] == channel) {
        return ESP_OK; 
    }

    uint8_t mux_cmd = (1 << channel);
    
    esp_err_t err = i2c_master_write_to_device(I2C_MASTER_NUM, mux_addr, 
                                               &mux_cmd, 1, 
                                               pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
                                               
    if (err == ESP_OK) {
        current_mux_channel[mux_idx] = channel;
        consecutive_i2c_errors = 0; // Сброс счетчика ошибок при успехе
    } else {
        current_mux_channel[mux_idx] = 255; // Инвалидация кэша
        consecutive_i2c_errors++;
        
        ESP_LOGE(TAG, "MUX switch failed (Err: %s). Strike %d/%d", 
                 esp_err_to_name(err), consecutive_i2c_errors, MAX_I2C_ERRORS_BEFORE_RESET);

        // Если шина легла плотно — запускаем реанимацию
        if (consecutive_i2c_errors >= MAX_I2C_ERRORS_BEFORE_RESET) {
            i2c_bus_clear_and_reinit();
        }
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