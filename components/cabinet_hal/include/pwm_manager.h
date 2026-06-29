#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pwm_manager_init(void);

// Управление мощностью (0 - 100%)
esp_err_t pwm_set_regen_heater(uint8_t percent);
esp_err_t pwm_set_hum_heater(uint8_t percent);
esp_err_t pwm_set_hum_fan(uint8_t percent);
esp_err_t pwm_set_dehum_fan(uint8_t percent);
esp_err_t pwm_set_exhaust_fan(uint8_t percent);
esp_err_t pwm_set_vibrator(uint8_t percent);

// Управление сервоприводом (угол 0 - 180 градусов)
esp_err_t pwm_set_servo_angle(uint8_t angle);

#ifdef __cplusplus
}
#endif