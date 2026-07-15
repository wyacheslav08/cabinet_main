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
#include "menu_engine.h"
#include "driver/gpio.h"

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
 * @brief Задача мониторинга аппаратной кнопки сброса пароля (10 секунд удержания)
 */
static void hardware_reset_task(void *pvParameters) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_RESET_BTN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // Кнопка замыкает пин на землю (GND)
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    int hold_time_ms = 0;

    while (1) {
        // Если кнопка нажата (0, так как подтяжка к питанию)
        if (gpio_get_level(PIN_RESET_BTN) == 0) {
            hold_time_ms += 100;
            
            // Если удерживаем ровно 10 секунд (10000 мс)
            if (hold_time_ms == 10000) {
                hmi_msg_t msg = {.type = EVENT_HARDWARE_PASS_RESET};
                xQueueSend(hmi_event_queue, &msg, 0);
            }
        } else {
            hold_time_ms = 0; // Сброс таймера, если отпустили раньше времени
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // Опрос каждые 100 мс
    }
    vTaskDelete(NULL);
}

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
// 2. Найди задачу hmi_router_task и обнови перехватчик:
static void hmi_router_task(void *pvParameters) {
    hmi_msg_t msg;
    ESP_LOGI(TAG, "HMI Router Task started on Core 0");

    while (1) {
        if (xQueueReceive(hmi_event_queue, &msg, portMAX_DELAY) == pdTRUE) {
            
            // Если это просто жест - отправляем в графический движок (LVGL)
            if (msg.type != EVENT_FIFTH_BTN_HOLD && g_display_handle != NULL) {
                display_manager_process_gesture(g_display_handle, msg.type);
            }

            // =================================================================
            // СИСТЕМА БЕЗОПАСНОСТИ: АКТИВАЦИЯ ЗАМКА ДВЕРИ
            // =================================================================
            // Срабатывает ТОЛЬКО по долгому удержанию Пятой кнопки И ТОЛЬКО на Главном экране!
            if (msg.type == EVENT_FIFTH_BTN_HOLD) {
                
                if (menu_engine_is_on_main_screen()) {
                    ESP_LOGW(TAG, "!!! DOOR UNLOCK COMMAND EXECUTED !!!");
                    
                    // 1. Аппаратно "глушим" сенсорную панель от ложных срабатываний
                    gesture_set_panel_enabled(false);
                    
                    // 2. Открываем замок (Подаем ток на соленоид)
                    // pwm_set_servo_angle(90); или gpio_set_level(PIN_SOLENOID_DOOR, 1);
                    
                    // 3. Ждем время удержания замка
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    
                    // 4. ЗАКРЫВАЕМ ЗАМОК (Снимаем ток)
                    // pwm_set_servo_angle(0); или gpio_set_level(PIN_SOLENOID_DOOR, 0);
                    
                    // 5. Ждем затухания ЭМИ индуктивности катушки
                    vTaskDelay(pdMS_TO_TICKS(200));
                    
                    // 6. Снова включаем панель
                    gesture_set_panel_enabled(true);
                } else {
                    // Если мы в меню, игнорируем удержание, так как Пятая кнопка 
                    // используется там только для коротких кликов (PRESS)
                    ESP_LOGI(TAG, "Fifth button held, but not on Main Screen. Hardware unlock blocked.");
                }
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

    // Запуск мониторинга кнопки аппаратного сброса
    xTaskCreatePinnedToCore(hardware_reset_task, "hw_reset", 2048, NULL, 2, NULL, 1);

    ESP_LOGI(TAG, "=== BOOT COMPLETE. SCHEDULER RUNNING. ===");

    // ==============================================================
    // 7. ОЖИДАНИЕ СТАБИЛИЗАЦИИ ЕМКОСТНЫХ СЕНСОРОВ (22 СЕКУНДЫ)
    // В это время на дисплее висит красивый экран "GUITAR CABINET"
    // ==============================================================
    ESP_LOGI(TAG, "Waiting 22 seconds for sensors to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Команда UI переключиться на главный экран
    if (g_display_handle) {
        display_manager_boot_complete(g_display_handle);
    }
    ESP_LOGI(TAG, "=== SYSTEM FULLY READY ===");
}