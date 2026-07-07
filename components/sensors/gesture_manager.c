/**
 * @file gesture_manager.c
 * @brief Реализация менеджера опроса тач-панелей и распознавания жестов.
 */

#include "gesture_manager.h"
#include "mpr121_driver.h"
#include "hw_config.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "GESTURE_MGR";

#define MAX_SWIPE_LEN               32
#define MIN_SWIPE_LEN               3
#define DOOR_UNLOCK_TIME_MS         1500  
#define POLL_RATE_MS                50    

static const uint8_t mux_channels[] = {MUX_CH_MPR_1, MUX_CH_MPR_2, MUX_CH_MPR_3, MUX_CH_MPR_4};
#define NUM_SENSORS (sizeof(mux_channels) / sizeof(mux_channels[0]))

static bool mpr121_online[NUM_SENSORS] = {false};
static volatile bool lock_button_state = false;

// [КРИТИЧНО] Определение глобальной переменной (без слова static!)
QueueHandle_t hmi_event_queue = NULL;

static uint8_t swipe_buffer[MAX_SWIPE_LEN];
static uint8_t swipe_len = 0;

bool gesture_is_lock_pressed(void) {
    return lock_button_state;
}

static void send_hmi_event(hmi_msg_t *msg) {
    if (hmi_event_queue != NULL) {
        if (xQueueSend(hmi_event_queue, msg, pdMS_TO_TICKS(10)) != pdPASS) {
            ESP_LOGW(TAG, "HMI Queue FULL! Dropping event %d", msg->type);
        }
    }
}

static uint8_t get_global_id(uint8_t mux_index, uint8_t mpr_pin) {
    return (mux_index * 12) + mpr_pin + 1;
}

static void get_xy_from_id(uint8_t id, int *x, int *y, bool is_flipped) {
    *x = (id - 1) % 3;
    *y = (id - 1) / 3;
    if (is_flipped) {
        *x = 2 - *x;
        *y = 15 - *y;
    }
}

static bool is_door_sensor(uint8_t id) {
    return (id >= 22 && id <= 27);
}

static hmi_event_type_t analyze_swipe(bool is_flipped) {
    if (swipe_len < MIN_SWIPE_LEN) return EVENT_TAP; // Если касание короткое — это ТАП

    int x_first, y_first, x_last, y_last;
    get_xy_from_id(swipe_buffer[0], &x_first, &y_first, is_flipped);
    get_xy_from_id(swipe_buffer[swipe_len - 1], &x_last, &y_last, is_flipped);

    int min_y = 99, max_y = -1;
    for (int i = 0; i < swipe_len; i++) {
        int x, y;
        get_xy_from_id(swipe_buffer[i], &x, &y, is_flipped);
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
    }

    int y_spread = max_y - min_y;

    if (y_spread <= 1) {
        if (x_first == 0 && x_last == 2) return EVENT_SWIPE_LEFT;
        if (x_first == 2 && x_last == 0) return EVENT_SWIPE_RIGHT;
    }
    if (y_spread >= 2) {
        if (y_last > y_first) return EVENT_SWIPE_DOWN;
        if (y_last < y_first) return EVENT_SWIPE_UP;
    }
    return EVENT_TAP;
}

static void mpr121_polling_task(void *pvParameters) {
    for (int i = 0; i < NUM_SENSORS; i++) {
        if (mpr121_init(mux_channels[i], MPR121_TOUCH_THRESH, MPR121_RELEASE_THRESH) == ESP_OK) {
            mpr121_online[i] = true;
        } else {
            ESP_LOGE(TAG, "Sensor MPR121 [%d] offline at boot", i);
        }
    }

    uint16_t current_status = 0;
    uint16_t last_status[NUM_SENSORS] = {0};
    uint8_t error_strikes[NUM_SENSORS] = {0}; 
    int release_cycles = 0; 
    int hold_door_cycles = 0;
    bool ignore_next_release = false; 
    int reconnect_timer = 0;
    hmi_msg_t msg;

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(POLL_RATE_MS);

    while (1) {
        int total_active_touches = 0;
        bool only_door_sensors_touched = true;
        bool is_flipped = (gpio_get_level(PIN_ORIENTATION_SENSOR) == 0);

        for (int i = 0; i < NUM_SENSORS; i++) {
            if (!mpr121_online[i]) continue; 

            if (mpr121_get_touched(mux_channels[i], &current_status) == ESP_OK) {
                error_strikes[i] = 0; 
                for (int bit = 0; bit < 12; bit++) {
                    if (current_status & (1 << bit)) {
                        total_active_touches++;
                        if (!is_door_sensor(get_global_id(i, bit))) {
                            only_door_sensors_touched = false; 
                        }
                    }
                }

                if (current_status != last_status[i]) {
                    uint16_t changed_bits = current_status ^ last_status[i];
                    for (int bit = 0; bit < 12; bit++) {
                        if ((changed_bits & (1 << bit)) && (current_status & (1 << bit))) {
                            uint8_t global_id = get_global_id(i, bit);
                            if (swipe_len < MAX_SWIPE_LEN && !ignore_next_release) {
                                if (swipe_len == 0 || swipe_buffer[swipe_len - 1] != global_id) {
                                    swipe_buffer[swipe_len++] = global_id;
                                }
                            }
                        }
                    }
                    last_status[i] = current_status;
                }
            } else {
                error_strikes[i]++;
                if (error_strikes[i] >= 3) {
                    mpr121_online[i] = false;
                    swipe_len = 0; 
                    msg.type = EVENT_ERROR_SENSOR_OFFLINE;
                    msg.sensor_index = i;
                    send_hmi_event(&msg);
                }
            }
        }

        if (total_active_touches > 0 && only_door_sensors_touched) {
            lock_button_state = true;
            hold_door_cycles++;
            if (hold_door_cycles >= (DOOR_UNLOCK_TIME_MS / POLL_RATE_MS)) {
                msg.type = EVENT_DOOR_UNLOCK;
                send_hmi_event(&msg);
                hold_door_cycles = 0;
                ignore_next_release = true; 
                swipe_len = 0; 
            }
        } else {
            lock_button_state = false;
            hold_door_cycles = 0; 
        }

        if (total_active_touches == 0) {
            if (ignore_next_release) {
                ignore_next_release = false;
                release_cycles = 0;
            } else if (swipe_len > 0) {
                release_cycles++;
                if (release_cycles >= 3) { 
                    msg.type = analyze_swipe(is_flipped);
                    if (msg.type != EVENT_NONE) send_hmi_event(&msg); 
                    swipe_len = 0;
                    release_cycles = 0;
                }
            }
        } else {
            release_cycles = 0;
        }

        reconnect_timer++;
        if (reconnect_timer >= (5000 / POLL_RATE_MS)) { 
            reconnect_timer = 0;
            for (int i = 0; i < NUM_SENSORS; i++) {
                if (!mpr121_online[i]) {
                    if (mpr121_init(mux_channels[i], MPR121_TOUCH_THRESH, MPR121_RELEASE_THRESH) == ESP_OK) {
                        mpr121_online[i] = true;
                        error_strikes[i] = 0; 
                        msg.type = EVENT_INFO_SENSOR_RESTORED;
                        msg.sensor_index = i;
                        send_hmi_event(&msg);
                    }
                }
            }
        }
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// [КРИТИЧНО] Реализация функции, которую ищет линковщик!
esp_err_t gesture_manager_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_ORIENTATION_SENSOR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    hmi_event_queue = xQueueCreate(10, sizeof(hmi_msg_t));
    if (!hmi_event_queue) {
        ESP_LOGE(TAG, "Failed to create HMI queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t res = xTaskCreatePinnedToCore(
        mpr121_polling_task, 
        "mpr121_task", 
        4096, 
        NULL, 
        6, 
        NULL, 
        1 // Core 1
    );
    
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MPR polling task");
        vQueueDelete(hmi_event_queue);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Gesture Manager initialized successfully");
    return ESP_OK;
}