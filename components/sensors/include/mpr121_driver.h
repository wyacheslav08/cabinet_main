#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MPR121_I2C_ADDRESS 0x5A

/**
 * @brief Инициализация MPR121 с включением автоконфигурации для работы через дерево.
 * @param mux_channel Канал I2C мультиплексора PCA9548A (0-7)
 * @param touch_thresh Порог срабатывания касания (рекомендуется 10)
 * @param release_thresh Порог отпускания (рекомендуется 4)
 */
esp_err_t mpr121_init(uint8_t mux_channel, uint8_t touch_thresh, uint8_t release_thresh);

/**
 * @brief Чтение 16-битной маски состояния электродов (0..11 биты).
 */
esp_err_t mpr121_get_touched(uint8_t mux_channel, uint16_t *touched_mask);

#ifdef __cplusplus
}
#endif