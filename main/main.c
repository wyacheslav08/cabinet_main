/**
 * @file main.c
 * @brief Точка входа прошивки гитарного климат-кабинета (Main Board).
 * @architecture ESP-IDF v5.x / FreeRTOS (Dual-Core SMP).
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

// Архитектурные модули проекта
#include "hw_config.h"
#include "display_manager.h"

static const char *TAG = "CABINET_MAIN";

/* =========================================================================
 * ТИПЫ ДАННЫХ И ОЧЕРЕДИ (Имитация интерфейса gesture_manager.h для тестов)
 * ========================================================================= */

/**
 * @brief Типы жестов, распознаваемые сенсорной панелью MPR121.
 */
typedef enum {
    GESTURE_NONE = 0,
    GESTURE_SWIPE_UP,
    GESTURE_SWIPE_DOWN,
    GESTURE_SWIPE_LEFT,
    GESTURE_SWIPE_RIGHT,
    GESTURE_TAP
} hmi_gesture_t;

// Глобальные хэндлы (Opaque Pointers)
static display_handle_t s_display_handle = NULL;
static QueueHandle_t    s_gesture_queue  = NULL;

/* =========================================================================
 * ВСПОМОГАТЕЛЬНЫЕ СИСТЕМНЫЕ ФУНКЦИИ
 * ========================================================================= */

/**
 * @brief Инициализация энергонезависимой памяти NVS Flash.
 * Критически необходима для работы Wi-Fi/BLE стека и сохранения настроек.
 */
static esp_err_t system_nvs_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Если NVS раздел поврежден или изменилась версия таблицы — форматируем
        ESP_LOGW(TAG, "NVS Flash partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

/* =========================================================================
 * ЗАДАЧИ FREERTOS (TASKS)
 * ========================================================================= */

/**
 * @brief Задача обработки пользовательского ввода (HMI Interaction Task).
 * Привязана к Core 0 (PRO_CPU) вместе с графическим стеком LVGL.
 * Ожидает события от сенсорной панели и управляет меню дисплея.
 */
static void hmi_interaction_task(void *pvParameters) {
    hmi_gesture_t gesture = GESTURE_NONE;
    
    ESP_LOGI(TAG, "HMI Interaction task started on Core 0");

    while (1) {
        // Неблокирующее для процессора ожидание жеста из очереди (до бесконечности)
        if (xQueueReceive(s_gesture_queue, &gesture, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Gesture received: %d", gesture);

            switch (gesture) {
                case GESTURE_SWIPE_UP:
                    // Перемещение курсора меню вверх
                    display_manager_menu_navigate(s_display_handle, -1);
                    break;

                case GESTURE_SWIPE_DOWN:
                    // Перемещение курсора меню вниз
                    display_manager_menu_navigate(s_display_handle, 1);
                    break;

                case GESTURE_TAP:
                case GESTURE_SWIPE_RIGHT: {
                    // Активация выбранного пункта меню
                    display_menu_item_t selected_item;
                    if (display_manager_menu_select(s_display_handle, &selected_item) == ESP_OK) {
                        ESP_LOGI(TAG, "Action triggered for Menu ID: %d", selected_item);
                        
                        // Пример реакции бизнес-логики на выбор в меню
                        if (selected_item == MENU_ITEM_LIGHTING) {
                            // Включаем/выключаем подсветку кабинета (тест)
                            static bool light_state = true;
                            light_state = !light_state;
                            display_manager_set_brightness(s_display_handle, light_state ? 100 : 20);
                        }
                    }
                    break;
                }

                case GESTURE_SWIPE_LEFT:
                    // Возврат или гашение экрана
                    ESP_LOGI(TAG, "Returning to main screen / Dimming");
                    display_manager_set_brightness(s_display_handle, 50);
                    break;

                default:
                    break;
            }
        }
    }
    
    // Защита от выхода из бесконечного цикла (Правило #5)
    vTaskDelete(NULL);
}

/**
 * @brief Задача обновления телеметрии и климата (Telemetry Task).
 * Привязана к Core 1 (APP_CPU), чтобы не мешать отрисовке интерфейса.
 * Опрашивает датчики (в данном тесте — симулирует) и шлет данные на экран.
 */
static void telemetry_update_task(void *pvParameters) {
    ESP_LOGI(TAG, "Telemetry update task started on Core 1");

    ui_status_data_t status_data = {
        .temperature       = 22.0f,
        .humidity          = 45.0f,
        .is_heating        = false,
        .is_humidifying    = false,
        .is_dehumidifying  = false,
        .weight_grams      = 3450, // 3.45 кг (Гитара внутри)
        .is_guitar_present = true,
        .wifi_rssi_percent = 80,
        .is_ble_connected  = true,
        .is_locked         = true
    };

    float time_step = 0.0f;

    while (1) {
        // 1. Имитация плавного изменения температуры и влажности (для теста UI)
        time_step += 0.1f;
        status_data.temperature = 22.0f + sinf(time_step) * 1.5f;       // Колебания 20.5 - 23.5 °C
        status_data.humidity    = 45.0f + cosf(time_step) * 3.0f;       // Колебания 42 - 48 %
        
        // Включаем иконки нагрева или увлажнения в зависимости от значений
        status_data.is_heating     = (status_data.temperature < 21.5f);
        status_data.is_humidifying = (status_data.humidity < 44.0f);

        // 2. Потокобезопасная отправка данных в менеджер дисплея
        if (display_manager_update_status(s_display_handle, &status_data) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update display status");
        }

        // 3. Период обновления телеметрии — 1 секунда
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}

/**
 * @brief [ТЕСТОВАЯ ЗАДАЧА] Генератор жестов сенсорной панели.
 * Имитирует прикосновения пользователя к панели MPR121 с интервалом в 2.5 секунды
 * для проверки работоспособности вертикального меню на экране ST7735.
 */
static void gesture_test_sim_task(void *pvParameters) {
    ESP_LOGW(TAG, "--- STARTING HMI GESTURE SIMULATION TEST ---");
    
    // Даем системе 3 секунды на полную инициализацию и отрисовку стартового экрана
    vTaskDelay(pdMS_TO_TICKS(3000));

    const hmi_gesture_t test_sequence[] = {
        GESTURE_SWIPE_DOWN,  // Переход на пуннк 2 (Весы)
        GESTURE_SWIPE_DOWN,  // Переход на пуннк 3 (Акустич. тест)
        GESTURE_SWIPE_DOWN,  // Переход на пуннк 4 (Подсветка)
        GESTURE_TAP,         // Выбор пункта 4 (Изменит яркость экрана)
        GESTURE_SWIPE_UP,    // Возврат на пуннк 3
        GESTURE_SWIPE_UP,    // Возврат на пуннк 2
        GESTURE_SWIPE_UP,    // Возврат на пуннк 1 (Климат)
        GESTURE_TAP          // Выбор пункта 1
    };
    
    const size_t seq_len = sizeof(test_sequence) / sizeof(test_sequence[0]);
    size_t idx = 0;

    while (1) {
        hmi_gesture_t current_gesture = test_sequence[idx];
        
        ESP_LOGD(TAG, "Simulating gesture: %d", current_gesture);
        
        // Отправляем жест в очередь (аналогично тому, как это делает прерывание от MPR121)
        if (xQueueSend(s_gesture_queue, &current_gesture, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "Gesture queue full, dropping event");
        }

        idx = (idx + 1) % seq_len;
        
        // Ждем 2.5 секунды перед следующим тестовым жестом
        vTaskDelay(pdMS_TO_TICKS(2500));
    }

    vTaskDelete(NULL);
}

/* =========================================================================
 * ГЛАВНАЯ ТОЧКА ВХОДА (APP_MAIN)
 * ========================================================================= */

void app_main(void) {
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  GUITAR CABINET CONTROLLER FIRMWARE v2.0 (ST7735)");
    ESP_LOGI(TAG, "  Target Board: ESP32-WROOM-32 DevKit (Dual-Core)");
    ESP_LOGI(TAG, "==================================================");

    // 1. Инициализация системных ресурсов
    ESP_ERROR_CHECK(system_nvs_init());

    // 2. Создание очереди событий HMI (на 10 элементов, защита от переполнения)
    s_gesture_queue = xQueueCreate(10, sizeof(hmi_gesture_t));
    configASSERT(s_gesture_queue != NULL);
    ESP_LOGI(TAG, "HMI Event Queue created successfully");

    // 3. Инициализация дисплея ST7735 (Внутри поднимает SPI, LVGL v9 и ШИМ LEDC)
    ESP_LOGI(TAG, "Initializing ST7735 Display Manager...");
    ESP_ERROR_CHECK(display_manager_init(&s_display_handle));
    configASSERT(s_display_handle != NULL);

    // 4. Запуск задач FreeRTOS с жесткой привязкой к ядрам (Task Pinning)
    
    // Core 0 (PRO_CPU): Графика, UI, связь со шлюзом (UART/BLE)
    BaseType_t res_hmi = xTaskCreatePinnedToCore(
        hmi_interaction_task, 
        "hmi_task", 
        4096, 
        NULL, 
        5,              // Высокий приоритет для мгновенного отклика интерфейса
        NULL, 
        0               // Привязка к Core 0
    );
    configASSERT(res_hmi == pdPASS);

    // Core 1 (APP_CPU): Бизнес-логика, опрос датчиков, климат-контроль
    BaseType_t res_telemetry = xTaskCreatePinnedToCore(
        telemetry_update_task, 
        "telemetry_task", 
        4096, 
        NULL, 
        3,              // Средний приоритет для фонового опроса
        NULL, 
        1               // Привязка к Core 1
    );
    configASSERT(res_telemetry == pdPASS);

    // Запуск тестового симулятора жестов (Core 1, низкий приоритет)
    // В продакшене эта задача заменяется на mpr121_polling_task из gesture_manager.c
    BaseType_t res_sim = xTaskCreatePinnedToCore(
        gesture_test_sim_task, 
        "sim_gesture_task", 
        2048, 
        NULL, 
        2, 
        NULL, 
        1
    );
    configASSERT(res_sim == pdPASS);

    ESP_LOGI(TAG, "System initialization complete. Scheduler running.");
    
    // Главный поток app_main завершает свою работу, отдавая управление планировщику FreeRTOS.
}