#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GESTURE_NONE = 0,
    GESTURE_CLICK,
    GESTURE_SWIPE_LEFT,
    GESTURE_SWIPE_RIGHT,
    GESTURE_SWIPE_UP,
    GESTURE_SWIPE_DOWN
} gesture_t;

// Инициализация всех 4-х чипов MPR121 и запуск задачи сканирования
esp_err_t gesture_manager_init(void);

// Получить последний распознанный жест (сбрасывается после чтения)
gesture_t gesture_get_last(void);

// Проверить, нажата ли кнопка замка
bool gesture_is_lock_pressed(void);

#ifdef __cplusplus
}
#endif