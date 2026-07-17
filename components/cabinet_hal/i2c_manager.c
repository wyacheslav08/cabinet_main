/**
 * @file i2c_manager.c
 * @brief Потокобезопасный драйвер шины I2C с функцией самовосстановления.
 */

#include "i2c_manager.h"
#include "hw_config.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "rom/ets_sys.h" // Для ets_delay_us

static const char *TAG = "I2C_MGR";
static SemaphoreHandle_t i2c_mutex = NULL;
static uint8_t current_mux_channel = 255; 

// ================== АППАРАТНОЕ ВОССТАНОВЛЕНИЕ ШИНЫ ==================

static void i2c_hw_bus_clear(void) {
    ESP_LOGW(TAG, "Starting hardware I2C bus clear...");
    
    // Переводим пины в режим обычных GPIO (Открытый коллектор)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << I2C_MASTER_SDA_IO) | (1ULL << I2C_MASTER_SCL_IO),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Устанавливаем SCL и SDA в HIGH
    gpio_set_level(I2C_MASTER_SDA_IO, 1);
    gpio_set_level(I2C_MASTER_SCL_IO, 1);
    ets_delay_us(10);

    // Дрыгаем SCL до 9 раз, пока SDA не поднимется в HIGH (Slave отпустил шину)
    for (int i = 0; i < 9; i++) {
        if (gpio_get_level(I2C_MASTER_SDA_IO) == 1) {
            break; // Шина свободна
        }
        gpio_set_level(I2C_MASTER_SCL_IO, 0);
        ets_delay_us(10);
        gpio_set_level(I2C_MASTER_SCL_IO, 1);
        ets_delay_us(10);
    }

    // Генерируем сигнал STOP (SCL HIGH, SDA LOW -> HIGH)
    gpio_set_level(I2C_MASTER_SDA_IO, 0);
    ets_delay_us(10);
    gpio_set_level(I2C_MASTER_SCL_IO, 1);
    ets_delay_us(10);
    gpio_set_level(I2C_MASTER_SDA_IO, 1);
    ets_delay_us(10);
    
    ESP_LOGW(TAG, "Hardware I2C bus clear completed.");
}

esp_err_t i2c_manager_recovery(void) {
    ESP_LOGE(TAG, "!!! I2C RECOVERY INITIATED !!!");
    i2c_manager_lock();
    
    // 1. Удаляем драйвер
    i2c_driver_delete(I2C_MASTER_NUM);
    
    // 2. Аппаратно сбрасываем шину
    i2c_hw_bus_clear();
    
    // 3. Заново инициализируем драйвер
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
    esp_err_t err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    
    if (err == ESP_OK) {
        i2c_set_timeout(I2C_MASTER_NUM, 0xFFFFF);
        current_mux_channel = 255; // Сбрасываем кэш мультиплексора
        ESP_LOGI(TAG, "I2C Recovery SUCCESS.");
    } else {
        ESP_LOGE(TAG, "I2C Recovery FAILED: %s", esp_err_to_name(err));
    }
    
    i2c_manager_unlock();
    return err;
}

// ================== СТАНДАРТНЫЕ ФУНКЦИИ ==================

esp_err_t i2c_manager_set_mux(uint8_t mux_addr, uint8_t channel) {
    if (channel > 7) return ESP_ERR_INVALID_ARG;
    if (mux_addr == MUX_ADDR_SENSORS && current_mux_channel == channel) return ESP_OK; 

    uint8_t mux_cmd = (1 << channel);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (mux_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, mux_cmd, true);
    i2c_master_stop(cmd);
    
    esp_err_t err = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
                                               
    if (err == ESP_OK) {
        if (mux_addr == MUX_ADDR_SENSORS) current_mux_channel = channel;
        vTaskDelay(pdMS_TO_TICKS(2)); 
    } else {
        if (mux_addr == MUX_ADDR_SENSORS) current_mux_channel = 255; 
        ESP_LOGE(TAG, "MUX switch failed (ADDR: 0x%02X, CH: %d, ERR: %s)", mux_addr, channel, esp_err_to_name(err));
        i2c_reset_tx_fifo(I2C_MASTER_NUM);
        i2c_reset_rx_fifo(I2C_MASTER_NUM);
    }
    return err;
}

esp_err_t i2c_manager_init(void) {
    i2c_mutex = xSemaphoreCreateMutex();
    if (i2c_mutex == NULL) return ESP_ERR_NO_MEM;

    // Первичная инициализация теперь просто вызывает Recovery
    // Это гарантирует, что при перезагрузке ESP32 шина будет очищена
    return i2c_manager_recovery();
}

void i2c_manager_lock(void) { xSemaphoreTake(i2c_mutex, portMAX_DELAY); }
void i2c_manager_unlock(void) { xSemaphoreGive(i2c_mutex); }