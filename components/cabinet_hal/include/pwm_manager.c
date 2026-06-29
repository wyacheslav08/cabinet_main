#include "pwm_manager.h"
#include "hw_config.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "PWM_MGR";

// Макросы для сервопривода (50Hz, 14-bit resolution = 16384 шагов)
#define SERVO_MIN_PULSEVALUE    819
#define SERVO_MAX_PULSEVALUE    1638

esp_err_t pwm_manager_init(void) {
    // 1. Таймер для ТЭНов, вентиляторов и вибродинамика (1 кГц, 10 бит)
    ledc_timer_config_t timer_general = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_10_BIT,
        .freq_hz          = 1000, 
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_general));

    // 2. Таймер для сервопривода (строго 50 Гц, 14 бит)
    ledc_timer_config_t timer_servo = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_1,
        .duty_resolution  = LEDC_TIMER_14_BIT,
        .freq_hz          = 50,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_servo));

    // 3. Настройка всех 7 каналов
    ledc_channel_config_t channels[] = {
        { .gpio_num = PIN_MOSFET_REGEN_HEATER, .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .speed_mode = LEDC_LOW_SPEED_MODE, .duty = 0, .hpoint = 0 },
        { .gpio_num = PIN_MOSFET_HUM_HEATER,   .channel = LEDC_CHANNEL_1, .timer_sel = LEDC_TIMER_0, .speed_mode = LEDC_LOW_SPEED_MODE, .duty = 0, .hpoint = 0 },
        { .gpio_num = PIN_MOSFET_HUM_FAN,      .channel = LEDC_CHANNEL_2, .timer_sel = LEDC_TIMER_0, .speed_mode = LEDC_LOW_SPEED_MODE, .duty = 0, .hpoint = 0 },
        { .gpio_num = PIN_MOSFET_DEHUM_FAN,    .channel = LEDC_CHANNEL_3, .timer_sel = LEDC_TIMER_0, .speed_mode = LEDC_LOW_SPEED_MODE, .duty = 0, .hpoint = 0 },
        { .gpio_num = PIN_MOSFET_EXHAUST_FAN,  .channel = LEDC_CHANNEL_4, .timer_sel = LEDC_TIMER_0, .speed_mode = LEDC_LOW_SPEED_MODE, .duty = 0, .hpoint = 0 },
        { .gpio_num = PIN_MOSFET_VIBRATOR,     .channel = LEDC_CHANNEL_5, .timer_sel = LEDC_TIMER_0, .speed_mode = LEDC_LOW_SPEED_MODE, .duty = 0, .hpoint = 0 },
        { .gpio_num = PIN_SERVO_DOOR,          .channel = LEDC_CHANNEL_6, .timer_sel = LEDC_TIMER_1, .speed_mode = LEDC_LOW_SPEED_MODE, .duty = 0, .hpoint = 0 }
    };

    for (int i = 0; i < 7; i++) {
        ESP_ERROR_CHECK(ledc_channel_config(&channels[i]));
    }

    ESP_LOGI(TAG, "PWM Manager initialized for 7 channels");
    return ESP_OK;
}

// Внутренняя функция для установки ШИМ 0-100%
static esp_err_t set_general_pwm(ledc_channel_t channel, uint8_t percent) {
    if (percent > 100) percent = 100;
    uint32_t duty = (percent * 1023) / 100; // 10-bit resolution = 1023 max
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

// Экспортные функции
esp_err_t pwm_set_regen_heater(uint8_t percent) { return set_general_pwm(LEDC_CHANNEL_0, percent); }
esp_err_t pwm_set_hum_heater(uint8_t percent)   { return set_general_pwm(LEDC_CHANNEL_1, percent); }
esp_err_t pwm_set_hum_fan(uint8_t percent)      { return set_general_pwm(LEDC_CHANNEL_2, percent); }
esp_err_t pwm_set_dehum_fan(uint8_t percent)    { return set_general_pwm(LEDC_CHANNEL_3, percent); }
esp_err_t pwm_set_exhaust_fan(uint8_t percent)  { return set_general_pwm(LEDC_CHANNEL_4, percent); }
esp_err_t pwm_set_vibrator(uint8_t percent)     { return set_general_pwm(LEDC_CHANNEL_5, percent); }

esp_err_t pwm_set_servo_angle(uint8_t angle) {
    if (angle > 180) angle = 180;
    uint32_t duty = SERVO_MIN_PULSEVALUE + ((SERVO_MAX_PULSEVALUE - SERVO_MIN_PULSEVALUE) * angle) / 180;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_6, duty);
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_6);
}