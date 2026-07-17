#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pwm_manager_init(void);

// Управление климатическими устройствами
esp_err_t pwm_set_regen_heater(uint8_t percent);
esp_err_t pwm_set_hum_heater(uint8_t percent);
esp_err_t pwm_set_hum_fan(uint8_t percent);
esp_err_t pwm_set_dehum_fan(uint8_t percent);
esp_err_t pwm_set_exhaust_fan(uint8_t percent);
esp_err_t pwm_set_vibrator(uint8_t percent);

// --- УПРАВЛЕНИЕ И МОНИТОРИНГ ДВЕРИ ---
esp_err_t door_sensor_init(void);
bool door_sensor_is_open(void);

esp_err_t pwm_set_door_lock(bool locked);
bool pwm_get_door_lock_state(void);

bool pwm_is_door_safe_to_unlock(void);

#ifdef __cplusplus
}
#endif