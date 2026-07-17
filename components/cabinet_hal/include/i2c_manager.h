#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация I2C контроллера ESP32
esp_err_t i2c_manager_init(void);

// Захват шины I2C (вызывать ПЕРЕД работой с любым I2C устройством)
void i2c_manager_lock(void);

// Освобождение шины I2C (вызывать ПОСЛЕ работы)
void i2c_manager_unlock(void);

// Переключение канала на PCA9548A (Шина должна быть уже захвачена!)
// esp_err_t i2c_manager_set_mux(uint8_t channel);
esp_err_t i2c_manager_set_mux(uint8_t mux_addr, uint8_t channel);

// [НОВОЕ] Жесткая аппаратная перезагрузка шины I2C при зависании
esp_err_t i2c_manager_recovery(void);

#ifdef __cplusplus
}
#endif