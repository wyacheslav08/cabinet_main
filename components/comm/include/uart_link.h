#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t uart_link_init(void);
esp_err_t uart_link_send(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif