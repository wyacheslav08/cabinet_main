#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Отправка полного пакета телеметрии на шлюз (для трансляции по BLE/Wi-Fi)
 */
esp_err_t uart_link_send_telemetry(float temp, float hum, int32_t weight_g, bool is_locked, bool is_guitar_present);

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t uart_link_init(void);
esp_err_t uart_link_send(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif