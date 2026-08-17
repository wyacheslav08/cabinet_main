#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PASSWORD_LENGTH 9

/**
 * @brief Структура системных настроек, хранимых в NVS.
 * @note Все поля выровнены по 4 байта для эффективного доступа.
 */
typedef struct {
    uint16_t version;

    // === Общие настройки (GEN_SET) ===
    int      target_humidity;             ///< Целевая влажность (%)
    uint16_t lock_hold_time_ms;           ///< Время удержания замка открытым (мс)
    int      lock_time_index;             ///< Индекс времени автоблокировки
    int      menu_timeout_index;          ///< Индекс таймаута меню
    int      screen_timeout_index;        ///< Индекс таймаута экрана
    bool     door_sound_enabled;          ///< Звук двери
    bool     water_silica_sound_enabled;  ///< Звук ресурсов
    bool     water_heater_enabled;        ///< Подогрев воды
    uint8_t  water_heater_max_temp;       ///< Макс. температура подогрева (°C)
    
    // === Настройки интерфейса (UI) ===
    uint8_t  screen_brightness_idx;       ///< Индекс яркости (0=10%, 4=100%)
    bool     password_enabled;            ///< Защита паролем включена
    uint8_t  password_len;                ///< Длина пароля (0 = не установлен)
    int      password[MAX_PASSWORD_LENGTH];///< Массив жестов пароля

    // === Логика влажности (HUM_LOG) ===
    float    dead_zone_percent;                   ///< Мертвая зона (%)
    float    min_humidity_change_for_timeout;     ///< Мин. изменение влажности
    uint32_t max_operation_duration_ms;           ///< Макс. время работы (мс)
    uint32_t operation_cooldown_ms;               ///< Время отдыха (мс)
    float    max_safe_humidity;                   ///< Макс. безопасная влажность
    float    resource_check_diff;                 ///< Порог разницы ресурса
    float    humidity_hysteresis;                 ///< Гистерезис влажности
    uint8_t  resource_low_fault_threshold;        ///< Порог "мало ресурса"
    uint8_t  resource_empty_fault_threshold;      ///< Порог "нет ресурса"

    // === Калибровка датчиков (CALIB) ===
    int8_t   sht_temp_adj[4];             ///< Коррекция температуры [Main, Hum, Deh, Ext]
    int8_t   sht_hum_adj[4];              ///< Коррекция влажности [Main, Hum, Deh, Ext]

    // === Статистика (STAT) ===
    uint32_t reset_count;                 ///< Счетчик сбросов
    uint32_t wdt_reset_count;             ///< Счетчик WDT сбросов
    uint32_t auto_reboot_counter;         ///< Счетчик авто-ребут
    uint32_t total_reboot_counter;        ///< Общий счетчик ребут

    // === Настройки экрана и ввода ===
    uint8_t  screen_rotation_index;       ///< 0=0°, 1=90°, 2=180°, 3=270°
    uint8_t  touch_rotation_index;        ///< 0=Норма, 1=Инверсия

    // === Настройки железа (HW_TUNE) ===
    float    hx711_scale_factor;          ///< Коэффициент весов
    int32_t  hx711_tare_offset;           ///< Тара весов
    uint32_t dsp_ping_duration_ms;        ///< Длительность удара по деке (мс)
    float    dsp_dry_resonance_hz;        ///< Частота сухой гитары (Гц)
    float    dsp_wet_resonance_hz;        ///< Частота влажной гитары (Гц)

} cabinet_settings_t;

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * @brief Инициализация подсистемы настроек (NVS).
 * @return ESP_OK при успехе, код ошибки иначе.
 */
esp_err_t settings_init(void);

/**
 * @brief Сохранение текущих настроек в NVS.
 * @return ESP_OK при успехе, код ошибки иначе.
 */
esp_err_t settings_save(void);

/**
 * @brief Сброс настроек к заводским значениям по умолчанию.
 * @return ESP_OK при успехе, код ошибки иначе.
 */
esp_err_t settings_reset_to_defaults(void);

/**
 * @brief Захват мьютекса настроек (для потокобезопасного доступа).
 * @note Используйте парно с settings_unlock().
 */
void settings_lock(void);

/**
 * @brief Освобождение мьютекса настроек.
 */
void settings_unlock(void);

/**
 * @brief Получение указателя на структуру настроек (только чтение).
 * @return Указатель на константную структуру настроек.
 * @warning Не модифицируйте данные напрямую! Используйте setter-функции.
 */
const cabinet_settings_t* settings_get_readonly(void);

/**
 * @brief Быстрое получение целевой влажности.
 * @param[out] out_value Указатель для записи значения.
 * @return ESP_OK при успехе, ESP_ERR_INVALID_ARG если out_value NULL.
 */
esp_err_t settings_get_target_humidity(int *out_value);

/**
 * @brief Установка целевой влажности с автосохранением.
 * @param[in] value Новое значение (0-100%).
 * @return ESP_OK при успехе, код ошибки иначе.
 */
esp_err_t settings_set_target_humidity(int value);

/**
 * @brief Быстрое получение времени удержания замка.
 * @param[out] out_value Указатель для записи значения (мс).
 * @return ESP_OK при успехе, ESP_ERR_INVALID_ARG если out_value NULL.
 */
esp_err_t settings_get_lock_hold_time(uint32_t *out_value);

#ifdef __cplusplus
}
#endif