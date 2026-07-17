#include "pwm_manager.h"
#include "hw_config.h"
#include "uart_link.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DOOR_CLOSED_DEBOUNCE_MS 1000
static volatile bool g_is_door_safe_to_unlock = false;

static void door_monitor_task(void *pvParameters) {
    uint32_t closed_ticks = 0;
    const uint32_t debounce_ticks = pdMS_TO_TICKS(DOOR_CLOSED_DEBOUNCE_MS);
    const uint32_t poll_rate = 50;

    while (1) {
        if (door_sensor_is_open()) {
            g_is_door_safe_to_unlock = false;
            closed_ticks = 0;
        } else {
            if (closed_ticks < debounce_ticks) {
                closed_ticks += poll_rate;
                g_is_door_safe_to_unlock = false;
            } else {
                g_is_door_safe_to_unlock = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(poll_rate));
    }
}

bool pwm_is_door_safe_to_unlock(void) {
    return g_is_door_safe_to_unlock;
}

static const char *TAG = "PWM_PROXY";
static volatile bool g_is_door_locked = true; // По умолчанию замок закрыт


esp_err_t pwm_manager_init(void) {
    ESP_LOGI(TAG, "PWM Proxy initialized. Actuators routed to Gateway via UART.");
    xTaskCreatePinnedToCore(door_monitor_task, "door_mon", 2048, NULL, 4, NULL, 1);

    return door_sensor_init();
}

esp_err_t door_sensor_init(void) {
    // ВАЖНО: GPIO 36 (PIN_DOOR_SENSOR) - Input-Only! 
    // Внутренние подтяжки ПЕРЕКРЫТЫ, используем внешнюю подтяжку на плате.
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_DOOR_SENSOR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,   // <--- Исправляет ошибку gpio_pullup_en(85)
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    return gpio_config(&io_conf);
}

bool door_sensor_is_open(void) {
    // 1 = дверь открыта (геркон разомкнут, подтяжка к 3.3В)
    // 0 = дверь закрыта (геркон замкнут на землю)
    return (gpio_get_level(PIN_DOOR_SENSOR) == 1);
}

static esp_err_t send_gateway_actuator_cmd(const char* device_name, uint8_t value) {
    char cmd_buf[64];
    int len = snprintf(cmd_buf, sizeof(cmd_buf), "CMD:ACT:%s=%d\r\n", device_name, value);
    ESP_LOGD(TAG, "TX Gateway: %s", cmd_buf);
    return uart_link_send((const uint8_t*)cmd_buf, len);
}

esp_err_t pwm_set_regen_heater(uint8_t percent) { return send_gateway_actuator_cmd("HEATER_DEHUM", percent); }
esp_err_t pwm_set_hum_heater(uint8_t percent)   { return send_gateway_actuator_cmd("HEATER_HUM", percent); }
esp_err_t pwm_set_hum_fan(uint8_t percent)      { return send_gateway_actuator_cmd("FAN_HUM", percent); }
esp_err_t pwm_set_dehum_fan(uint8_t percent)    { return send_gateway_actuator_cmd("FAN_DEHUM", percent); }
esp_err_t pwm_set_exhaust_fan(uint8_t percent)  { return send_gateway_actuator_cmd("FAN_EXH", percent); }
esp_err_t pwm_set_vibrator(uint8_t percent)     { return send_gateway_actuator_cmd("VIBRO", percent); }

esp_err_t pwm_set_door_lock(bool locked) {
    g_is_door_locked = locked;
    return send_gateway_actuator_cmd("LOCK", locked ? 1 : 0);
}

bool pwm_get_door_lock_state(void) {
    return g_is_door_locked;
}