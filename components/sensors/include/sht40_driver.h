#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHT40_I2C_ADDRESS 0x44

// Чтение температуры и влажности с конкретного канала мультиплексора
esp_err_t sht40_read(uint8_t mux_channel, float *temperature, float *humidity);

#ifdef __cplusplus
}
#endif