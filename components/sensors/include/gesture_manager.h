#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// Перечисление всех возможных событий HMI
typedef enum {
    EVENT_NONE = 0,
    EVENT_SWIPE_UP,
    EVENT_SWIPE_DOWN,
    EVENT_SWIPE_LEFT,
    EVENT_SWIPE_RIGHT,
    EVENT_TAP,
    EVENT_FIFTH_BTN_PRESS,     // <--- Мгновенное касание "Пятой кнопки"
    EVENT_FIFTH_BTN_HOLD,      // <--- Долгое удержание "Пятой кнопки"
    EVENT_ERROR_SENSOR_OFFLINE,
    EVENT_INFO_SENSOR_RESTORED,
    EVENT_SYSTEM_IDLE_TIMEOUT,
    EVENT_SYSTEM_LOCK,
    EVENT_HARDWARE_PASS_RESET,
} hmi_event_type_t;

// Структура сообщения для очереди
typedef struct {
    hmi_event_type_t type;
    uint8_t sensor_index;
} hmi_msg_t;

// [КРИТИЧНО] Глобальная очередь событий (Экспортируется для main и ui)
extern QueueHandle_t hmi_event_queue;

// [КРИТИЧНО] Функция инициализации
esp_err_t gesture_manager_init(void);

// ... старый код ...
bool gesture_is_lock_pressed(void);

// Новые функции управления состоянием панели
void gesture_set_panel_enabled(bool enabled);
void gesture_set_panel_inverted(bool inverted);

#ifdef __cplusplus
}
#endif