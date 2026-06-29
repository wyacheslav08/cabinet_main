#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация дисплеев и запуск задачи отрисовки
esp_err_t display_manager_init(void);

#ifdef __cplusplus
}
#endif