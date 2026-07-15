#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PASSWORD_LENGTH 9

typedef struct {
    uint16_t version;

    // --- Общие настройки (GEN_SET) ---
    int targetHumidity;
    uint16_t lockHoldTime;
    int lockTimeIndex;
    int menuTimeoutOptionIndex;
    int screenTimeoutOptionIndex;
    bool doorSoundEnabled;
    bool waterSilicaSoundEnabled;
    bool waterHeaterEnabled;
    uint8_t waterHeaterMaxTemp;
    
    // Новые параметры для Фазы 2
    uint8_t screenBrightnessIdx;   // Индекс яркости (0=10%, 1=30%, 2=50%, 3=70%, 4=100%)
    bool passwordEnabled;          // Включена ли защита паролем
    uint8_t passwordLen;           // Текущая длина пароля (0 = не установлен)
    int password[MAX_PASSWORD_LENGTH]; // Массив жестов пароля

    // --- Логика влажности (HUM_LOG) ---
    float deadZonePercent;
    float minHumidityChangeForTimeout;
    uint32_t maxOperationDuration; // В миллисекундах
    uint32_t operationCooldown;    // В миллисекундах
    float maxSafeHumidity;
    float resourceCheckDiff;
    float humidityHysteresis;
    uint8_t resourceLowFaultThreshold;
    uint8_t resourceEmptyFaultThreshold;

    // --- Калибровка датчиков (CALIB) ---
    int8_t sht_temp_adj[4]; // 0=Main, 1=Hum, 2=Deh, 3=Ext
    int8_t sht_hum_adj[4];

    // --- Статистика (STAT) ---
    uint32_t resetCount;
    uint32_t wdtResetCount;
    uint32_t autoRebootCounter;
    uint32_t totalRebootCounter;
    
    // --- Настройки интерфейса ---
    uint8_t screenRotationIndex;   // 0 = 0°, 1 = 90°, 2 = 180°, 3 = 270°

    // --- НОВОЕ: Настройки Железа (HW_TUNE) ---
    float hx711ScaleFactor;        // Коэффициент весов
    int32_t hx711TareOffset;       // Тара весов
    uint32_t dspPingDurationMs;    // Длительность удара по деке
    float dspDryResonanceHz;       // Эталонная частота сухой гитары
    float dspWetResonanceHz;       // Эталонная частота влажной гитары
    uint8_t touchRotationIndex;    //  0 = Норма, 1 = Инверсия (180°)

} cabinet_settings_t;

extern cabinet_settings_t sys_settings;

esp_err_t settings_init(void);
esp_err_t settings_save(void);
esp_err_t settings_reset_to_defaults(void);
void settings_lock(void);
void settings_unlock(void);

#ifdef __cplusplus
}
#endif