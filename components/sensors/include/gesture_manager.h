#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

bool gesture_is_lock_pressed(void);

// ================= ЛОГИКА ЖЕСТОВ =================

// Перечисление всех возможных событий системы
typedef enum {
    EVENT_NONE,
    EVENT_SWIPE_UP,
    EVENT_SWIPE_DOWN,
    EVENT_SWIPE_LEFT,
    EVENT_SWIPE_RIGHT,
    EVENT_DOOR_UNLOCK,
    EVENT_ERROR_SENSOR_OFFLINE,
    EVENT_INFO_SENSOR_RESTORED
} hmi_event_type_t;

// Структура сообщения для очереди
typedef struct {
    hmi_event_type_t type;
    uint8_t sensor_index; // Используется для передачи ID отвалившегося сенсора
} hmi_msg_t;

// Глобальная очередь событий (Доступна для UI и Логики)
extern QueueHandle_t hmi_event_queue;

// Инициализация и запуск задачи опроса
esp_err_t gesture_manager_init(void);

#ifdef __cplusplus
}
#endif