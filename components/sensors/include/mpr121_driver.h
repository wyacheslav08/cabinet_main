#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MPR121_I2C_ADDRESS 0x5A

// Инициализация MPR121 на заданном канале с указанными порогами (Touch / Release)
esp_err_t mpr121_init(uint8_t mux_channel, uint8_t touch_threshold, uint8_t release_threshold);

// Чтение состояния 12 электродов (битовая маска)
esp_err_t mpr121_get_touched(uint8_t mux_channel, uint16_t *touched_mask);

#ifdef __cplusplus
}
#endif