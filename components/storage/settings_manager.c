/**
 * @file settings_manager.c
 * @brief Менеджер энергонезависимых настроек системы (NVS).
 * @version 2.0 - Production Ready для ESP-IDF v5.5+
 */

#include "settings_manager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdalign.h>

static const char *TAG = "STORAGE";

#define NVS_NAMESPACE           "cabinet"
#define NVS_KEY_CONFIG          "sys_cfg"
#define CURRENT_SETTINGS_VERSION 2

// Критическая секция для защиты глобальных данных
static SemaphoreHandle_t s_settings_mutex = NULL;

// Выравниваем структуру по границе 4 байт для эффективного доступа
static alignas(4) cabinet_settings_t s_sys_settings = {0};

// ============================================================================
// PRIVATE API: Внутренние функции (не экспортируются)
// ============================================================================

/**
 * @brief Заполняет структуру настройками по умолчанию.
 * @note Вызывать ТОЛЬКО при захваченном мьютексе!
 */
static void settings_load_defaults_impl(cabinet_settings_t *settings)
{
    configASSERT(settings != NULL);
    
    memset(settings, 0, sizeof(cabinet_settings_t));
    settings->version = CURRENT_SETTINGS_VERSION;

    // === Общие настройки (GEN_SET) ===
    settings->target_humidity             = 50;
    settings->lock_hold_time_ms           = 2000;
    settings->lock_time_index             = 0;
    settings->menu_timeout_index          = 1;
    settings->screen_timeout_index        = 0;
    settings->door_sound_enabled          = true;
    settings->water_silica_sound_enabled  = true;
    settings->water_heater_enabled        = true;
    settings->water_heater_max_temp       = 40;
    settings->screen_brightness_idx       = 4;   // 100%
    settings->password_enabled            = false;
    settings->password_len                = 0;
    memset(settings->password, 0, sizeof(settings->password));

    // === Калибровка датчиков ===
    memset(settings->sht_temp_adj, 0, sizeof(settings->sht_temp_adj));
    memset(settings->sht_hum_adj, 0, sizeof(settings->sht_hum_adj));

    // === Логика климат-контроля (HUM_LOG) ===
    settings->dead_zone_percent                  = 1.0f;
    settings->min_humidity_change_for_timeout    = 1.0f;
    settings->max_operation_duration_ms          = 2 * 60 * 1000;
    settings->operation_cooldown_ms              = 1 * 60 * 1000;
    settings->max_safe_humidity                  = 65.0f;
    settings->resource_check_diff                = 3.0f;
    settings->humidity_hysteresis                = 1.0f;
    settings->resource_low_fault_threshold       = 2;
    settings->resource_empty_fault_threshold     = 4;
    settings->screen_rotation_index              = 0;
    settings->touch_rotation_index               = 0;

    // === Настройки железа (HW_TUNE) ===
    settings->hx711_scale_factor      = 420.0f;
    settings->hx711_tare_offset       = 8400000;
    settings->dsp_ping_duration_ms    = 50;
    settings->dsp_dry_resonance_hz    = 850.0f;
    settings->dsp_wet_resonance_hz    = 750.0f;
}

// ============================================================================
// PUBLIC API
// ============================================================================

esp_err_t settings_init(void)
{
    ESP_LOGI(TAG, "Initializing NVS storage...");
    
    // Создаем мьютекс рекурсивным для безопасности
    if (s_settings_mutex == NULL) {
        s_settings_mutex = xSemaphoreCreateRecursiveMutex();
        if (s_settings_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create settings mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t required_size = sizeof(cabinet_settings_t);
    err = nvs_get_blob(nvs_handle, NVS_KEY_CONFIG, &s_sys_settings, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "No settings found in NVS, loading defaults...");
        nvs_close(nvs_handle);
        return settings_reset_to_defaults();
    } 
    
    if (err == ESP_OK) {
        if (required_size != sizeof(cabinet_settings_t)) {
            ESP_LOGW(TAG, "Settings size mismatch (got %zu, expected %zu)", 
                     required_size, sizeof(cabinet_settings_t));
            err = ESP_ERR_INVALID_SIZE;
        } else if (s_sys_settings.version != CURRENT_SETTINGS_VERSION) {
            ESP_LOGW(TAG, "Settings version mismatch (got %u, expected %u)", 
                     s_sys_settings.version, CURRENT_SETTINGS_VERSION);
            err = ESP_ERR_INVALID_VERSION;
        }
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Loading default settings due to error: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return settings_reset_to_defaults();
    }

    ESP_LOGI(TAG, "Settings loaded: target_humidity=%d%%, version=%u", 
             s_sys_settings.target_humidity, s_sys_settings.version);
    
    nvs_close(nvs_handle);
    return ESP_OK;
}

esp_err_t settings_reset_to_defaults(void)
{
    if (s_settings_mutex == NULL) {
        ESP_LOGE(TAG, "Settings not initialized!");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTakeRecursive(s_settings_mutex, portMAX_DELAY);
    
    settings_load_defaults_impl(&s_sys_settings);
    
    xSemaphoreGiveRecursive(s_settings_mutex);

    ESP_LOGW(TAG, "Settings reset to factory defaults (v%d)", CURRENT_SETTINGS_VERSION);
    return settings_save();
}

esp_err_t settings_save(void)
{
    if (s_settings_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open for save failed: %s", esp_err_to_name(err));
        return err;
    }

    xSemaphoreTakeRecursive(s_settings_mutex, portMAX_DELAY);
    
    err = nvs_set_blob(nvs_handle, NVS_KEY_CONFIG, &s_sys_settings, sizeof(cabinet_settings_t));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Settings saved to flash successfully");
        } else {
            ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "NVS set_blob failed: %s", esp_err_to_name(err));
    }
    
    xSemaphoreGiveRecursive(s_settings_mutex);
    nvs_close(nvs_handle);
    
    return err;
}

void settings_lock(void)
{
    if (s_settings_mutex != NULL) {
        xSemaphoreTakeRecursive(s_settings_mutex, portMAX_DELAY);
    }
}

void settings_unlock(void)
{
    if (s_settings_mutex != NULL) {
        xSemaphoreGiveRecursive(s_settings_mutex);
    }
}

const cabinet_settings_t* settings_get_readonly(void)
{
    return &s_sys_settings;
}

esp_err_t settings_get_target_humidity(int *out_value)
{
    if (out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_settings_mutex != NULL) {
        xSemaphoreTakeRecursive(s_settings_mutex, portMAX_DELAY);
    }
    
    *out_value = s_sys_settings.target_humidity;
    
    if (s_settings_mutex != NULL) {
        xSemaphoreGiveRecursive(s_settings_mutex);
    }
    
    return ESP_OK;
}

esp_err_t settings_set_target_humidity(int value)
{
    if (value < 0 || value > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_settings_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    xSemaphoreTakeRecursive(s_settings_mutex, portMAX_DELAY);
    s_sys_settings.target_humidity = value;
    xSemaphoreGiveRecursive(s_settings_mutex);
    
    return settings_save();
}

esp_err_t settings_get_lock_hold_time(uint32_t *out_value)
{
    if (out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_settings_mutex != NULL) {
        xSemaphoreTakeRecursive(s_settings_mutex, portMAX_DELAY);
    }
    
    *out_value = s_sys_settings.lock_hold_time_ms;
    
    if (s_settings_mutex != NULL) {
        xSemaphoreGiveRecursive(s_settings_mutex);
    }
    
    return ESP_OK;
}