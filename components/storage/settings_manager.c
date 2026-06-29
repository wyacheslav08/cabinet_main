#include "settings_manager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "STORAGE";
#define NVS_NAMESPACE "cabinet"
#define NVS_KEY_CONFIG "sys_cfg"
#define CURRENT_SETTINGS_VERSION 2

cabinet_settings_t sys_settings;
static SemaphoreHandle_t settings_mutex = NULL;

void settings_lock(void) {
    if (settings_mutex) xSemaphoreTake(settings_mutex, portMAX_DELAY);
}

void settings_unlock(void) {
    if (settings_mutex) xSemaphoreGive(settings_mutex);
}

esp_err_t settings_reset_to_defaults(void) {
    settings_lock();
    memset(&sys_settings, 0, sizeof(cabinet_settings_t));
    
    sys_settings.version = CURRENT_SETTINGS_VERSION;
    
    // Общие
    sys_settings.targetHumidity = 50;
    sys_settings.lockHoldTime = 1000;
    sys_settings.lockTimeIndex = 0;
    sys_settings.menuTimeoutOptionIndex = 1;
    sys_settings.screenTimeoutOptionIndex = 0;
    sys_settings.doorSoundEnabled = true;
    sys_settings.waterSilicaSoundEnabled = true;
    sys_settings.waterHeaterEnabled = true;
    sys_settings.waterHeaterMaxTemp = 40;
    
    // Логика
    sys_settings.deadZonePercent = 1.0f;
    sys_settings.minHumidityChangeForTimeout = 1.0f;
    sys_settings.maxOperationDuration = 2 * 60 * 1000; // 2 минуты
    sys_settings.operationCooldown = 1 * 60 * 1000;    // 1 минута
    sys_settings.maxSafeHumidity = 65.0f;
    sys_settings.resourceCheckDiff = 3.0f;
    sys_settings.humidityHysteresis = 1.0f;
    sys_settings.resourceLowFaultThreshold = 2;
    sys_settings.resourceEmptyFaultThreshold = 4;

    // Калибровка и Статистика (нули)
    // ... memset уже занулил их ...

    // Железо (HW_TUNE)
    sys_settings.hx711ScaleFactor = 420.0f;
    sys_settings.hx711TareOffset = 8400000;
    sys_settings.dspPingDurationMs = 50;
    sys_settings.dspDryResonanceHz = 850.0f;
    sys_settings.dspWetResonanceHz = 750.0f;

    settings_unlock();
    
    ESP_LOGW(TAG, "Settings reset to factory defaults");
    return settings_save();
}

esp_err_t settings_init(void) {
    settings_mutex = xSemaphoreCreateMutex();
    if (settings_mutex == NULL) return ESP_ERR_NO_MEM;

    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    size_t required_size = sizeof(cabinet_settings_t);
    err = nvs_get_blob(my_handle, NVS_KEY_CONFIG, &sys_settings, &required_size);
    
    if (err == ESP_ERR_NVS_NOT_FOUND || sys_settings.version != CURRENT_SETTINGS_VERSION) {
        ESP_LOGW(TAG, "Settings not found or version mismatch. Loading defaults...");
        nvs_close(my_handle);
        return settings_reset_to_defaults();
    } else if (err == ESP_OK) {
        ESP_LOGI(TAG, "Settings loaded successfully. Target Humidity: %d%%", sys_settings.targetHumidity);
    } else {
        ESP_LOGE(TAG, "Error reading NVS: %s", esp_err_to_name(err));
    }
    
    nvs_close(my_handle);
    return err;
}

esp_err_t settings_save(void) {
    nvs_handle_t my_handle;
    esp_err_t err;

    settings_lock();
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(my_handle, NVS_KEY_CONFIG, &sys_settings, sizeof(cabinet_settings_t));
        if (err == ESP_OK) {
            err = nvs_commit(my_handle);
            ESP_LOGI(TAG, "Settings saved to flash");
        }
        nvs_close(my_handle);
    }
    settings_unlock();
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings: %s", esp_err_to_name(err));
    }
    return err;
}