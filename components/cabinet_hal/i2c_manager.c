#include "i2c_manager.h"
#include "hw_config.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "I2C_MGR";
static SemaphoreHandle_t i2c_mutex = NULL;
static uint8_t current_mux_channel[2] = {255, 255}; 

static uint8_t consecutive_i2c_errors = 0; 
#define MAX_I2C_ERRORS_BEFORE_RESET 3

static void i2c_bus_clear_and_reinit(void) {
    ESP_LOGW(TAG, "I2C bus stuck! Initiating hardware bus clear...");
    i2c_driver_delete(I2C_MASTER_NUM);

    gpio_set_direction(I2C_MASTER_SDA_IO, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_direction(I2C_MASTER_SCL_IO, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(I2C_MASTER_SDA_IO, 1);
    gpio_set_level(I2C_MASTER_SCL_IO, 1);
    esp_rom_delay_us(20);

    for (int i = 0; i < 9; i++) {
        if (gpio_get_level(I2C_MASTER_SDA_IO) == 1) break; 
        gpio_set_level(I2C_MASTER_SCL_IO, 0);
        esp_rom_delay_us(5);
        gpio_set_level(I2C_MASTER_SCL_IO, 1);
        esp_rom_delay_us(5);
    }

    gpio_set_level(I2C_MASTER_SDA_IO, 0);
    esp_rom_delay_us(5);
    gpio_set_level(I2C_MASTER_SCL_IO, 1);
    esp_rom_delay_us(5);
    gpio_set_level(I2C_MASTER_SDA_IO, 1);
    esp_rom_delay_us(20);

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

    current_mux_channel[0] = 255;
    current_mux_channel[1] = 255;
    consecutive_i2c_errors = 0;
    ESP_LOGI(TAG, "I2C Bus Clear complete.");
}

esp_err_t i2c_manager_set_mux(uint8_t mux_addr, uint8_t channel) {
    if (channel > 7) return ESP_ERR_INVALID_ARG;
    
    int mux_idx = (mux_addr == MUX_ADDR_TOUCH) ? 0 : 1;
    if (current_mux_channel[mux_idx] == channel) return ESP_OK; 

    uint8_t mux_cmd = (1 << channel);
    
    // В ТОЧНОСТИ КАК В ВАШЕМ ТЕСТОВОМ КОДЕ: Ручная сборка транзакции для MUX
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (mux_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, mux_cmd, true);
    i2c_master_stop(cmd);
    
    esp_err_t err = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
                                               
    if (err == ESP_OK) {
        current_mux_channel[mux_idx] = channel;
        consecutive_i2c_errors = 0; 
        
        // КРИТИЧНО: Даем мультиплексору 100 микросекунд на физическое переключение транзисторов!
        esp_rom_delay_us(100); 
    } else {
        current_mux_channel[mux_idx] = 255; 
        consecutive_i2c_errors++;
        ESP_LOGE(TAG, "MUX switch failed (ADDR: 0x%02X, CH: %d, ERR: %s). Strike %d/%d", 
                 mux_addr, channel, esp_err_to_name(err), consecutive_i2c_errors, MAX_I2C_ERRORS_BEFORE_RESET);

        if (consecutive_i2c_errors >= MAX_I2C_ERRORS_BEFORE_RESET) {
            i2c_bus_clear_and_reinit();
        }
    }
    return err;
}

esp_err_t i2c_manager_init(void) {
    i2c_mutex = xSemaphoreCreateMutex();
    if (i2c_mutex == NULL) return ESP_ERR_NO_MEM;

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
    if (err == ESP_OK) ESP_LOGI(TAG, "I2C Master initialized");
    return err;
}

void i2c_manager_lock(void)   { xSemaphoreTake(i2c_mutex, portMAX_DELAY); }
void i2c_manager_unlock(void) { xSemaphoreGive(i2c_mutex); }