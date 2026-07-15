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

// Принудительный сброс на главный экран (для таймера бездействия)
void menu_engine_force_main_screen(void);

// Сигнал об окончании загрузки
void menu_engine_boot_complete(void);

// Макрос времени отображения сообщения "Сохранено" (в миллисекундах)
#define UI_SAVED_POPUP_DURATION_MS 1000 
// Узнать, заблокирован ли сейчас экран (для иконки)
bool menu_engine_is_locked(void);

#ifdef __cplusplus
}
#endif