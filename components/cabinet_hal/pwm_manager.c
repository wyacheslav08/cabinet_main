#include "pwm_manager.h"
#include "hw_config.h"
#include "uart_link.h" // Для отправки команд 0/1 на Gateway
#include "driver/ledc.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "PWM_MGR";

// Настройки сервопривода (50 Гц, 14-бит = 16384 шагов)
#define SERVO_MIN_PULSEVALUE    819
#define SERVO_MAX_PULSEVALUE    1638

esp_err_t pwm_manager_init(void) {
    // 1. Таймер для ТЭНов и вибродинамика (5 кГц, 8-бит разрешение)
    ledc_timer_config_t timer_general = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .freq_hz          = 5000, 
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_general));

    // 2. Таймер для сервопривода заслонки (Строго 50 Гц, 14-бит)
    ledc_timer_config_t timer_servo = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_1,
        .duty_resolution  = LEDC_TIMER_14_BIT,
        .freq_hz          = 50,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_servo));

    // 3. Инициализируем ТОЛЬКО 4 аппаратных ШИМ-канала на Main Board
    ledc_channel_config_t channels[] = {
        { .gpio_num = PIN_MOSFET_REGEN_HEATER, .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .speed_mode = LEDC_LOW_SPEED_MODE, .duty = 0, .hpoint = 0 },
        { .gpio_num = PIN_MOSFET_HUM_HEATER,   .channel = LEDC_CHANNEL_1, .timer_sel = LEDC_TIMER_0, .speed_mode = LEDC_LOW_SPEED_MODE, .duty = 0, .hpoint = 0 },
        { .gpio_num = PIN_MOSFET_VIBRATOR,     .channel = LEDC_CHANNEL_2, .timer_sel = LEDC_TIMER_0, .speed_mode = LEDC_LOW_SPEED_MODE, .duty = 0, .hpoint = 0 },
        { .gpio_num = PIN_SERVO_DOOR,          .channel = LEDC_CHANNEL_3, .timer_sel = LEDC_TIMER_1, .speed_mode = LEDC_LOW_SPEED_MODE, .duty = 0, .hpoint = 0 }
    };

    for (int i = 0; i < 4; i++) {
        ESP_ERROR_CHECK(ledc_channel_config(&channels[i]));
    }

    ESP_LOGI(TAG, "PWM Manager initialized (4 Hardware PWM channels on Main Board)");
    return ESP_OK;
}

// Внутренняя функция для установки ШИМ (0-100%)
static esp_err_t set_hardware_pwm(ledc_channel_t channel, uint8_t percent) {
    if (percent > 100) percent = 100;
    uint32_t duty = (percent * 255) / 100; // Для 8-битного таймера
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

// --- АППАРАТНЫЕ ШИМ-УСТРОЙСТВА (MAIN BOARD) ---
esp_err_t pwm_set_regen_heater(uint8_t percent) { return set_hardware_pwm(LEDC_CHANNEL_0, percent); }
esp_err_t pwm_set_hum_heater(uint8_t percent)   { return set_hardware_pwm(LEDC_CHANNEL_1, percent); }
esp_err_t pwm_set_vibrator(uint8_t percent)     { return set_hardware_pwm(LEDC_CHANNEL_2, percent); }

esp_err_t pwm_set_servo_angle(uint8_t angle) {
    if (angle > 180) angle = 180;
    uint32_t duty = SERVO_MIN_PULSEVALUE + ((SERVO_MAX_PULSEVALUE - SERVO_MIN_PULSEVALUE) * angle) / 180;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, duty);
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3);
}

// --- ЦИФРОВЫЕ УСТРОЙСТВА 0/1 (ОТПРАВКА НА GATEWAY VIA UART) ---
// Сохраняем полную обратную совместимость с кодом climate_control.c!
static esp_err_t send_gateway_relay_cmd(const char* device_name, uint8_t state) {
    char cmd_buf[64];
    int len = snprintf(cmd_buf, sizeof(cmd_buf), "CMD:RELAY:%s=%d\r\n", device_name, (state > 0) ? 1 : 0);
    ESP_LOGD(TAG, "Routing digital actuator to Gateway: %s", cmd_buf);
    return uart_link_send((const uint8_t*)cmd_buf, len);
}

esp_err_t pwm_set_hum_fan(uint8_t percent)     { return send_gateway_relay_cmd("HUM_FAN", percent); }
esp_err_t pwm_set_dehum_fan(uint8_t percent)   { return send_gateway_relay_cmd("DEHUM_FAN", percent); }
esp_err_t pwm_set_exhaust_fan(uint8_t percent) { return send_gateway_relay_cmd("EXHAUST_FAN", percent); }