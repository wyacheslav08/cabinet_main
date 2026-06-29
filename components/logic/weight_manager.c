#include "weight_manager.h"
#include "hx711_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WEIGHT_MGR";

// Настройки калибровки (в продакшене их нужно хранить в NVS)
static int32_t tare_offset = 8400000; // Примерное значение пустого датчика
static float scale_factor = 420.0f;   // Сколько тиков АЦП в 1 грамме

// Наша "База данных" гитар
static const guitar_profile_t guitar_db[] = {
    {"Fender Stratocaster", 3500, 50, 45}, // 3.5 кг, +-50г, влажность 45%
    {"Gibson Les Paul",     4200, 60, 48}, // 4.2 кг, +-60г, влажность 48%
    {"Martin D-28",         2100, 40, 50}  // 2.1 кг, +-40г, влажность 50%
};
static const int db_size = sizeof(guitar_db) / sizeof(guitar_db[0]);

esp_err_t weight_manager_init(void) {
    ESP_LOGI(TAG, "Weight Manager Initialized. DB Size: %d guitars", db_size);
    // В будущем здесь будем читать tare_offset и scale_factor из NVS
    return ESP_OK;
}

esp_err_t weight_tare(void) {
    int32_t raw;
    esp_err_t err = hx711_read_raw(&raw);
    if (err == ESP_OK) {
        tare_offset = raw;
        ESP_LOGI(TAG, "Tared successfully. New offset: %ld", tare_offset);
    } else {
        ESP_LOGE(TAG, "Tare failed!");
    }
    return err;
}

esp_err_t weight_get_grams(int32_t *grams) {
    int32_t raw_val;
    esp_err_t err = hx711_read_raw(&raw_val);
    
    if (err == ESP_OK) {
        // Формула: (Сырое значение - Тара) / Коэффициент
        float weight_f = (float)(raw_val - tare_offset) / scale_factor;
        *grams = (int32_t)weight_f;
    }
    return err;
}

const guitar_profile_t* weight_identify_guitar(int32_t current_weight_g) {
    // Если вес меньше 500 грамм - скорее всего шкаф пустой
    if (current_weight_g < 500) {
        return NULL;
    }

    for (int i = 0; i < db_size; i++) {
        int32_t min_w = guitar_db[i].ref_weight_g - guitar_db[i].tolerance_g;
        int32_t max_w = guitar_db[i].ref_weight_g + guitar_db[i].tolerance_g;

        if (current_weight_g >= min_w && current_weight_g <= max_w) {
            ESP_LOGI(TAG, "Identified Guitar: %s", guitar_db[i].name);
            return &guitar_db[i];
        }
    }
    
    ESP_LOGW(TAG, "Unknown guitar detected! Weight: %ld g", current_weight_g);
    return NULL;
}