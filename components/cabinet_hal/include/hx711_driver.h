#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hx711_init(void);

// Читает сырое 24-битное значение (возвращает ESP_OK, если датчик готов)
esp_err_t hx711_read_raw(int32_t *raw_val);

#ifdef __cplusplus
}
#endif