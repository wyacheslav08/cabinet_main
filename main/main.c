/**
 * @file main.c
 * @brief Точка входа прошивки гитарного климат-кабинета (Main Board).
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

// Аппаратные абстракции
#include "settings_manager.h"
#include "i2c_manager.h"
#include "pwm_manager.h"
#include "uart_link.h"
#include "weight_manager.h"

// Логика и UI
#include "display_manager.h"
#include "gesture_manager.h"
#include "climate_control.h"
#include "menu_engine.h"

static const char *TAG = "CABINET_MAIN";
display_handle_t g_display_handle = NULL;

static esp_err_t system_nvs_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS Flash corrupted, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

void app_main(void) {
    ESP_LOGI(TAG, "=== GUITAR CABINET SYSTEM BOOT ===");

    // 1. Базовые подсистемы
    ESP_ERROR_CHECK(system_nvs_init());
    ESP_ERROR_CHECK(settings_init());
    
    // 2. Аппаратные шины и драйверы (внутри запускают свои фоновые задачи)
    ESP_ERROR_CHECK(uart_link_init());
    ESP_ERROR_CHECK(i2c_manager_init());
    ESP_ERROR_CHECK(pwm_manager_init()); 
    ESP_ERROR_CHECK(weight_manager_init());

    // 3. Пользовательский интерфейс
    ESP_ERROR_CHECK(display_manager_init(&g_display_handle));
    configASSERT(g_display_handle != NULL);

    // 4. Подсистема жестов и ввода
    ESP_ERROR_CHECK(gesture_manager_init());

    // 5. Климат-контроль
    ESP_ERROR_CHECK(climate_control_init());

    // 6. Запуск маршрутизатора бизнес-логики
    menu_engine_start_router();

    ESP_LOGI(TAG, "=== BOOT COMPLETE. SCHEDULER RUNNING. ===");

    // Ожидание стабилизации датчиков перед показом главного экрана
    ESP_LOGI(TAG, "Waiting 2.5 seconds for sensors to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(2500));

    if (g_display_handle) {
        display_manager_boot_complete(g_display_handle);
    }
    
    ESP_LOGI(TAG, "=== SYSTEM FULLY READY ===");
}