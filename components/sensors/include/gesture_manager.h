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
    EVENT_DOOR_UNLOCK,
    EVENT_ERROR_SENSOR_OFFLINE,
    EVENT_INFO_SENSOR_RESTORED
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

bool gesture_is_lock_pressed(void);

#ifdef __cplusplus
}
#endif