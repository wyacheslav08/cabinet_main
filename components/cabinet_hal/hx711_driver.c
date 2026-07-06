#include "hx711_driver.h"
#include "hw_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h" 
#include "freertos/FreeRTOS.h"

static const char *TAG = "HX711";
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

esp_err_t hx711_init(void) {
    gpio_config_t io_conf_out = {
        .pin_bit_mask = (1ULL << PIN_HX711_SCK),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE, 
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf_out));

    gpio_config_t io_conf_in = {
        .pin_bit_mask = (1ULL << PIN_HX711_DT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE, 
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf_in));

    gpio_set_level(PIN_HX711_SCK, 0);
    ESP_LOGI(TAG, "HX711 initialized on SCK:%d, DT:%d", PIN_HX711_SCK, PIN_HX711_DT);
    return ESP_OK;
}

esp_err_t hx711_read_raw(int32_t *raw_val) {
    if (raw_val == NULL) return ESP_ERR_INVALID_ARG;

    // Проверка готовности с таймаутом (защита от зависания задачи)
    uint32_t timeout = 1000; 
    while (gpio_get_level(PIN_HX711_DT) == 1) {
        if (--timeout == 0) {
            return ESP_ERR_TIMEOUT;
        }
        esp_rom_delay_us(10);
    }

    uint32_t value = 0;
    
    // Читаем 24 бита данных. 
    // ВАЖНО: Захватываем спинлок только на момент тактирования (1-2 мкс)
    for (int i = 0; i < 24; i++) {
        portENTER_CRITICAL(&mux);
        gpio_set_level(PIN_HX711_SCK, 1);
        esp_rom_delay_us(1); // Минимальная задержка для HX711
        
        value = (value << 1) | gpio_get_level(PIN_HX711_DT);
        
        gpio_set_level(PIN_HX711_SCK, 0);
        esp_rom_delay_us(1);
        portEXIT_CRITICAL(&mux);
    }
    
    // 25-й импульс: усиление 128 (Канал А)
    portENTER_CRITICAL(&mux);
    gpio_set_level(PIN_HX711_SCK, 1);
    esp_rom_delay_us(1);
    gpio_set_level(PIN_HX711_SCK, 0);
    esp_rom_delay_us(1);
    portEXIT_CRITICAL(&mux);

    // Расширение знака (24-bit Two's complement -> 32-bit int)
    if (value & 0x800000) {
        value |= 0xFF000000;
    }
    
    *raw_val = (int32_t)value;
    return ESP_OK;
}