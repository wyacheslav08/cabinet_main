#include "climate_control.h"
#include "hw_config.h"
#include "pwm_manager.h"
#include "sht40_driver.h"
#include "settings_manager.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "weight_manager.h"

static const char *TAG = "CLIMATE";
static volatile climate_state_t current_state = CLIMATE_STATE_IDLE;

climate_state_t climate_get_state(void) {
    return current_state;
}

// Выключает все исполнительные устройства
static void stop_all_actuators(void) {
    pwm_set_hum_heater(0);
    pwm_set_regen_heater(0);
    pwm_set_hum_fan(0);
    pwm_set_dehum_fan(0);
    pwm_set_exhaust_fan(0);
    pwm_set_servo_angle(0); // Закрыть заслонку
}

// Главный конечный автомат (FreeRTOS Task)
static void climate_task(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(3000); // Строгий цикл 3 секунды

    float temp = 0.0f, hum = 0.0f;
    
    while (1) {
        // 1. Читаем датчик двери
        bool is_door_open = (gpio_get_level(PIN_DOOR_SENSOR) == 1);
        static bool was_door_open = false;

        if (is_door_open) {
            if (current_state != CLIMATE_STATE_DOOR_OPEN) {
                ESP_LOGW(TAG, "Door opened! Suspending climate control.");
                stop_all_actuators();
                current_state = CLIMATE_STATE_DOOR_OPEN;
                current_state = CLIMATE_STATE_DOOR_OPEN;
                was_door_open = true;
            }
        } else {
            if (was_door_open) {
                ESP_LOGI(TAG, "Door closed! Analyzing contents...");
                was_door_open = false;
                
                // Даем датчику веса 1 секунду успокоиться после хлопка дверью
                vTaskDelay(pdMS_TO_TICKS(1000));
                
                int32_t current_weight = 0;
                if (weight_get_grams(&current_weight) == ESP_OK) {
                    ESP_LOGI(TAG, "Measured weight: %ld g", current_weight);
                    
                    const guitar_profile_t* guitar = weight_identify_guitar(current_weight);
                    if (guitar != NULL) {
                        // Меняем целевую влажность под конкретную гитару!
                        settings_lock();
                        if (sys_settings.targetHumidity != guitar->target_humidity) {
                            sys_settings.targetHumidity = guitar->target_humidity;
                            ESP_LOGI(TAG, "Auto-adjusted target humidity to %d%% for %s", 
                                     guitar->target_humidity, guitar->name);
                            // Сохраняем во flash
                            settings_unlock();
                            settings_save();
                        } else {
                            settings_unlock();
                        }
                    }
                }
            }
            // 2. Читаем датчик климата
            esp_err_t err = sht40_read(MUX_CH_SHT40_TOP, &temp, &hum);
            
            if (err != ESP_OK) {
                if (current_state != CLIMATE_STATE_ERROR) {
                    ESP_LOGE(TAG, "Sensor error! Stopping actuators.");
                    stop_all_actuators();
                    current_state = CLIMATE_STATE_ERROR;
                }
            } else {
                // 3. Получаем настройки пользователя (потокобезопасно)
                settings_lock();
                int target_hum = sys_settings.targetHumidity;
                float dead_zone = sys_settings.deadZonePercent;
                settings_unlock();

                // 4. Логика Конечного Автомата
                if (current_state == CLIMATE_STATE_DOOR_OPEN || current_state == CLIMATE_STATE_ERROR) {
                    current_state = CLIMATE_STATE_IDLE; // Сброс ошибки/двери
                }

                // Включаем увлажнение
                if (hum < (target_hum - dead_zone) && current_state != CLIMATE_STATE_HUMIDIFYING) {
                    ESP_LOGI(TAG, "Start Humidifying (Current: %.1f%%, Target: %d%%)", hum, target_hum);
                    stop_all_actuators(); // Безопасное переключение
                    pwm_set_hum_heater(100);  // ТЭН увлажнителя на 100%
                    pwm_set_hum_fan(50);      // Вентилятор на 50%
                    current_state = CLIMATE_STATE_HUMIDIFYING;
                }
                // Включаем осушение
                else if (hum > (target_hum + dead_zone) && current_state != CLIMATE_STATE_DEHUMIDIFYING) {
                    ESP_LOGI(TAG, "Start Dehumidifying (Current: %.1f%%, Target: %d%%)", hum, target_hum);
                    stop_all_actuators();
                    pwm_set_dehum_fan(100);   // Прогоняем воздух через силикагель
                    current_state = CLIMATE_STATE_DEHUMIDIFYING;
                }
                // Достигли цели - выключаем
                else if (hum >= (target_hum - 0.5f) && hum <= (target_hum + 0.5f) && current_state != CLIMATE_STATE_IDLE) {
                    ESP_LOGI(TAG, "Target reached (%.1f%%). Idling.", hum);
                    stop_all_actuators();
                    current_state = CLIMATE_STATE_IDLE;
                }
            }
        }

        // Засыпаем ровно до следующего 3-секундного тика (экономим CPU)
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

esp_err_t climate_control_init(void) {
    // Настраиваем пин двери
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_DOOR_SENSOR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    stop_all_actuators();

    // Запускаем задачу (Приоритет 5 - выше среднего, стек 4КБ)
    BaseType_t res = xTaskCreate(climate_task, "climate_task", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create climate task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Climate control initialized");
    return ESP_OK;
}