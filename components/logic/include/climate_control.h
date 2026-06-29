#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Состояния климатической системы
typedef enum {
    CLIMATE_STATE_IDLE = 0,         // Ожидание (климат в норме)
    CLIMATE_STATE_HUMIDIFYING,      // Увлажнение
    CLIMATE_STATE_DEHUMIDIFYING,    // Осушение
    CLIMATE_STATE_REGENERATION,     // Прокалка силикагеля (серво открыт, ТЭН греет)
    CLIMATE_STATE_ERROR,            // Ошибка датчиков
    CLIMATE_STATE_DOOR_OPEN         // Дверь открыта (система на паузе)
} climate_state_t;

// Инициализация и запуск фоновой задачи
esp_err_t climate_control_init(void);

// Получить текущее состояние (для UI и BLE)
climate_state_t climate_get_state(void);

#ifdef __cplusplus
}
#endif