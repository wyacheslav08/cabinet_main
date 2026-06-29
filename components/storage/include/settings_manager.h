#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PASSWORD_LENGTH 9

// Структура всех настроек системы
typedef struct {
    uint16_t version;
    int targetHumidity;
    int ventTempThreshold;
    int password[MAX_PASSWORD_LENGTH];
    bool doorSoundEnabled;
    bool waterSilicaSoundEnabled;
    bool waterHeaterEnabled;
    uint8_t waterHeaterMaxTemp;
    float deadZonePercent;
    uint32_t autoRebootCounter;
    // ... сюда в будущем добавим настройки для микрофона и гитар ...
} cabinet_settings_t;

// Глобальный экземпляр настроек в RAM
extern cabinet_settings_t sys_settings;

// Инициализация NVS и загрузка настроек
esp_err_t settings_init(void);

// Сохранение текущих настроек из RAM во Flash
esp_err_t settings_save(void);

// Сброс до заводских настроек
esp_err_t settings_reset_to_defaults(void);

// Потокобезопасный доступ (Мьютексы)
void settings_lock(void);
void settings_unlock(void);

#ifdef __cplusplus
}
#endif