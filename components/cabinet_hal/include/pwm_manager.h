#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Инициализация всех таймеров и каналов аппаратного ШИМ (LEDC).
 */
esp_err_t pwm_manager_init(void);

/**
 * @brief Управление исполнительными устройствами (0 - 100%)
 */
esp_err_t pwm_set_heater(uint8_t percent);
esp_err_t pwm_set_humidifier(uint8_t percent);
esp_err_t pwm_set_fan(uint8_t percent);
esp_err_t pwm_set_vibrator(uint8_t percent);

/**
 * @brief Управление сервоприводом замка дверцы (0 - 180 градусов)
 */
esp_err_t pwm_set_servo_angle(uint8_t angle);

#ifdef __cplusplus
}
#endif