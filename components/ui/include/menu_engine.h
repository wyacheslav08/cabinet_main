#pragma once
#include "esp_err.h"
#include "gesture_manager.h" // Для hmi_event_type_t

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация стейт-машины меню
void menu_engine_init(void);

// Обработка жеста (вызывает переключение экранов или изменение настроек)
esp_err_t menu_engine_process_gesture(hmi_event_type_t event);

// Получить текущее состояние (для понимания, на главном мы экране или нет)
bool menu_engine_is_on_main_screen(void);

#ifdef __cplusplus
}
#endif