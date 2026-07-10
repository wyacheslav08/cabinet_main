/**
 * @file main.c
 * @brief Точка входа прошивки гитарного климат-кабинета (Main Board).
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

// Аппаратные абстракции и настройки
#include "hw_config.h"
#include "settings_manager.h"
#include "i2c_manager.h"
#include "pwm_manager.h"
#include "uart_link.h"

// Логика и UI
#include "display_manager.h"
#include "gesture_manager.h"
#include "climate_control.h"

static const char *TAG = "CABINET_MAIN";

// Глобальный хэндл дисплея
display_handle_t g_display_handle = NULL;

/**
 * @brief Инициализация NVS Flash (Критично для настроек).
 */
static esp_err_t system_nvs_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS Flash corrupted, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

/**
 * @brief Задача-маршрутизатор HMI. Читает жесты от MPR121 и передает в UI.
 * Привязана к Core 0 (вместе с LVGL) для мгновенного отклика.
 */
static void hmi_router_task(void *pvParameters) {
    hmi_msg_t msg;
    ESP_LOGI(TAG, "HMI Router Task started on Core 0");

    while (1) {
        // Ожидание события жеста из очереди (блокировка без нагрузки на CPU)
        if (xQueueReceive(hmi_event_queue, &msg, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Routing HMI Event: %d", msg.type);
            
            // Защита: передаем жест в менеджер дисплея
            if (g_display_handle != NULL) {
                display_manager_process_gesture(g_display_handle, msg.type);
            }

            // Глобальная обработка экстренных событий
            if (msg.type == EVENT_DOOR_UNLOCK) {
                ESP_LOGW(TAG, "DOOR UNLOCK COMMAND RECEIVED!");
                // Здесь будет вызов pwm_set_servo_angle()
            }
        }
    }
    vTaskDelete(NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "=== GUITAR CABINET SYSTEM BOOT ===");

    // 1. Инициализация базовых подсистем
    ESP_ERROR_CHECK(system_nvs_init());
    ESP_ERROR_CHECK(settings_init());
    
    // 2. Инициализация аппаратных шин
    ESP_ERROR_CHECK(uart_link_init());
    ESP_ERROR_CHECK(i2c_manager_init());
    ESP_ERROR_CHECK(pwm_manager_init());

    // 3. Инициализация пользовательского интерфейса (LVGL + ST7735)
    ESP_ERROR_CHECK(display_manager_init(&g_display_handle));
    configASSERT(g_display_handle != NULL);

    // 4. Запуск подсистемы жестов (Создает задачу на Core 1 и очередь hmi_event_queue)
    ESP_ERROR_CHECK(gesture_manager_init());
    configASSERT(hmi_event_queue != NULL);

    // 5. Запуск климат-контроля (Создает задачу на Core 1, опрашивает SHT40)
    ESP_ERROR_CHECK(climate_control_init());

        // 6. Запуск маршрутизатора интерфейса на Core 0
    BaseType_t res = xTaskCreatePinnedToCore(
        hmi_router_task, 
        "hmi_router", 
        4096, 
        NULL, 
        5, 
        NULL, 
        0
    );
    configASSERT(res == pdPASS);

    ESP_LOGI(TAG, "=== BOOT COMPLETE. SCHEDULER RUNNING. ===");

    // ==============================================================
    // 7. ОЖИДАНИЕ СТАБИЛИЗАЦИИ ЕМКОСТНЫХ СЕНСОРОВ (22 СЕКУНДЫ)
    // В это время на дисплее висит красивый экран "GUITAR CABINET"
    // ==============================================================
    ESP_LOGI(TAG, "Waiting 22 seconds for sensors to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(22000));

    // Команда UI переключиться на главный экран
    if (g_display_handle) {
        display_manager_boot_complete(g_display_handle);
    }
    ESP_LOGI(TAG, "=== SYSTEM FULLY READY ===");
}