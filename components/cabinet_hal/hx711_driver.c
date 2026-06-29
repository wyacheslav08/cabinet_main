#include "hx711_driver.h"
#include "hw_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "rom/ets_sys.h" // Для ets_delay_us()

static const char *TAG = "HX711";
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

esp_err_t hx711_init(void) {
    gpio_config_t io_conf_out = {
        .pin_bit_mask = (1ULL << PIN_HX711_SCK),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0, .pull_down_en = 0, .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_out);

    gpio_config_t io_conf_in = {
        .pin_bit_mask = (1ULL << PIN_HX711_DT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1, .pull_down_en = 0, .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_in);

    gpio_set_level(PIN_HX711_SCK, 0);
    ESP_LOGI(TAG, "HX711 initialized");
    return ESP_OK;
}

esp_err_t hx711_read_raw(int32_t *raw_val) {
    // Проверка готовности (DT должен быть LOW)
    if (gpio_get_level(PIN_HX711_DT) == 1) {
        return ESP_ERR_INVALID_STATE; // Датчик еще не готов
    }

    uint32_t value = 0;
    
    // Начало критической секции: прерывания отключены!
    portENTER_CRITICAL(&mux);
    
    for (int i = 0; i < 24; i++) {
        gpio_set_level(PIN_HX711_SCK, 1);
        ets_delay_us(1);
        value = value << 1;
        gpio_set_level(PIN_HX711_SCK, 0);
        ets_delay_us(1);
        if (gpio_get_level(PIN_HX711_DT)) {
            value++;
        }
    }
    
    // 25-й импульс: усиление 128 (Канал А)
    gpio_set_level(PIN_HX711_SCK, 1);
    ets_delay_us(1);
    gpio_set_level(PIN_HX711_SCK, 0);
    ets_delay_us(1);

    // Конец критической секции: прерывания снова включены
    portEXIT_CRITICAL(&mux);

    // HX711 выдает 24-битное число в дополнительном коде (Two's complement)
    if (value & 0x800000) {
        value |= 0xFF000000;
    }
    
    *raw_val = (int32_t)value;
    return ESP_OK;
}