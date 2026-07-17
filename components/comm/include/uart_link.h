#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t uart_link_init(void);
esp_err_t uart_link_send(const uint8_t *data, size_t len);

// Отправка телеметрии на шлюз (для BLE/Wi-Fi)
esp_err_t uart_link_send_telemetry(float temp, float hum, int32_t weight_g, bool is_locked, bool is_guitar_present);

// [НОВОЕ] Отправка команды управления исполнительным устройством на Gateway
esp_err_t uart_link_set_actuator(const char* device, int value);

#ifdef __cplusplus
}
#endif
