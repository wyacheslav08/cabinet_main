#include "gesture_manager.h"
#include "mpr121_driver.h"
#include "hw_config.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "settings_manager.h"
#include <math.h>

static const char *TAG = "GESTURE_MGR";

// ================= НАСТРОЙКИ ЖЕСТОВ =================
#define MAX_SWIPE_LEN               64
#define MIN_SWIPE_LEN               3
#define DOOR_UNLOCK_TIME_MS         1500
#define POLL_RATE_MS                50
#define MAX_SWIPE_TOUCHES           3 

static const uint8_t possible_addresses[] = {MPR121_ADDR_1, MPR121_ADDR_2};
#define MAX_POSSIBLE_SENSORS (sizeof(possible_addresses) / sizeof(possible_addresses[0]))

// ================= ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ =================
QueueHandle_t hmi_event_queue = NULL;
static volatile bool lock_button_state = false;

// Управление панелью
static volatile bool g_panel_enabled = true;
static volatile bool g_panel_inverted = false;

// Состояние сенсоров
static uint8_t active_addresses[MAX_POSSIBLE_SENSORS];
static uint8_t active_sensor_count = 0;

static uint8_t swipe_buffer[MAX_SWIPE_LEN];
static uint8_t swipe_len = 0;

// ================= ФУНКЦИИ API =================

bool gesture_is_lock_pressed(void) { return lock_button_state; }
void gesture_set_panel_enabled(bool enabled) { g_panel_enabled = enabled; }
void gesture_set_panel_inverted(bool inverted) { g_panel_inverted = inverted; }

static void send_hmi_event(hmi_msg_t *msg) {
    if (hmi_event_queue != NULL) {
        if (xQueueSend(hmi_event_queue, msg, 0) != pdPASS) {
            hmi_msg_t dummy;
            xQueueReceive(hmi_event_queue, &dummy, 0); 
            xQueueSend(hmi_event_queue, msg, 0);
        }
    }
}

// ================= МАТЕМАТИКА ЖЕСТОВ =================

static hmi_event_type_t analyze_swipe(void) {
    if (swipe_len < MIN_SWIPE_LEN) return EVENT_NONE;

    int unwrapped_y[MAX_SWIPE_LEN];
    unwrapped_y[0] = 0;
    
    int min_uy = 0, max_uy = 0;
    int total_rows = active_sensor_count * 4;

    for (int i = 0; i < swipe_len - 1; i++) {
        int y1 = swipe_buffer[i] / 3;
        int y2 = swipe_buffer[i+1] / 3;

        int diff = y2 - y1;
        if (diff < -(total_rows / 2)) diff += total_rows;
        else if (diff > (total_rows / 2)) diff -= total_rows;

        if (abs(diff) > 1) {
            ESP_LOGD(TAG, "Отмена: перескок через ряд");
            return EVENT_NONE; 
        }

        unwrapped_y[i+1] = unwrapped_y[i] + diff;

        if (unwrapped_y[i+1] < min_uy) min_uy = unwrapped_y[i+1];
        if (unwrapped_y[i+1] > max_uy) max_uy = unwrapped_y[i+1];
    }

    int row_span = max_uy - min_uy + 1;
    int x_first = swipe_buffer[0] % 3;
    int x_last = swipe_buffer[swipe_len - 1] % 3;

    hmi_event_type_t detected_event = EVENT_NONE;

    if (row_span <= 2) {
        if (x_first == 0 && x_last == 2) detected_event = EVENT_SWIPE_RIGHT;
        if (x_first == 2 && x_last == 0) detected_event = EVENT_SWIPE_LEFT;
    } 
    else if (row_span >= 3) {
        int net_y = unwrapped_y[swipe_len - 1] - unwrapped_y[0];
        if (net_y > 0) detected_event = EVENT_SWIPE_DOWN;
        if (net_y < 0) detected_event = EVENT_SWIPE_UP;
    }

    // Инверсия ориентации
    if (g_panel_inverted && detected_event != EVENT_NONE) {
        switch (detected_event) {
            case EVENT_SWIPE_UP:    detected_event = EVENT_SWIPE_DOWN; break;
            case EVENT_SWIPE_DOWN:  detected_event = EVENT_SWIPE_UP; break;
            case EVENT_SWIPE_LEFT:  detected_event = EVENT_SWIPE_RIGHT; break;
            case EVENT_SWIPE_RIGHT: detected_event = EVENT_SWIPE_LEFT; break;
            default: break;
        }
    }

    return detected_event;
}

// ================= ОСНОВНОЙ ТАСК =================

static void mpr121_polling_task(void *pvParameters) {
    // 1. Динамическое сканирование
    for (int i = 0; i < MAX_POSSIBLE_SENSORS; i++) {
        if (mpr121_init(possible_addresses[i], MPR121_TOUCH_THRESH, MPR121_RELEASE_THRESH) == ESP_OK) {
            active_addresses[active_sensor_count++] = possible_addresses[i];
        }
    }

    if (active_sensor_count == 0) {
        ESP_LOGE(TAG, "Сенсоры не найдены! Остановка таска HMI.");
        vTaskDelete(NULL);
    }

    uint32_t last_status = 0;
    int release_cycles = 0; 
    int hold_door_cycles = 0;
    bool ignore_next_release = false; 
    bool palm_rejected = false; 

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(POLL_RATE_MS);

    while (1) {
        // Чтение ориентации из железа и софта
        bool hw_flipped = (gpio_get_level(PIN_ORIENTATION_SENSOR) == 1); 
        settings_lock();
        bool sw_flipped = (sys_settings.touchRotationIndex == 1);
        settings_unlock();
        g_panel_inverted = hw_flipped ^ sw_flipped;

        uint32_t current_status = 0;
        int total_active_touches = 0;
        uint16_t touch_mask = 0;

        // Читаем сенсоры
        for (int i = 0; i < active_sensor_count; i++) {
            if (mpr121_get_touched(active_addresses[i], &touch_mask) == ESP_OK) {
                current_status |= ((uint32_t)touch_mask << (i * 12));
                for(int b = 0; b < 12; b++) {
                    if (touch_mask & (1 << b)) total_active_touches++;
                }
            }
        }

        // --- БЛОКИРОВКА ПАНЕЛИ (MUTE) ---
        if (!g_panel_enabled) {
            last_status = current_status;
            swipe_len = 0;
            hold_door_cycles = 0;
            palm_rejected = false;
            ignore_next_release = false;
            lock_button_state = false;
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
            continue; 
        }

        // --- ЛОГИКА ЗАМКА ДВЕРИ ---
        bool unlock_combo_active = false;
        if (active_sensor_count == 2) {
            bool r3_touched = (current_status & 0x00000E00) != 0; 
            bool r4_touched = (current_status & 0x00007000) != 0; 
            bool r0_touched = (current_status & 0x00000007) != 0; 
            bool r7_touched = (current_status & 0x00E00000) != 0; 

            if ((r3_touched && r4_touched) || (r0_touched && r7_touched)) {
                unlock_combo_active = true;
            }
        }

        if (unlock_combo_active) {
            lock_button_state = true;
            hold_door_cycles++;
            if (hold_door_cycles >= (DOOR_UNLOCK_TIME_MS / POLL_RATE_MS)) {
                hmi_msg_t msg = {.type = EVENT_DOOR_UNLOCK};
                send_hmi_event(&msg);
                hold_door_cycles = 0;
                ignore_next_release = true; 
                swipe_len = 0; 
                palm_rejected = false; 
            }
        } else {
            lock_button_state = false;
            hold_door_cycles = 0; 
        }

        // --- ПРАВИЛО "ЗМЕЙКИ" ---
        if (total_active_touches > MAX_SWIPE_TOUCHES && !unlock_combo_active) {
            if (!palm_rejected) {
                ESP_LOGD(TAG, "Palm rejected!");
                palm_rejected = true;
            }
        }

        // --- ЗАПИСЬ СВАЙПА ---
        if (current_status != last_status) {
            uint32_t changed_bits = current_status ^ last_status;
            for (int bit = 0; bit < (active_sensor_count * 12); bit++) {
                if ((changed_bits & (1 << bit)) && (current_status & (1 << bit))) {
                    if (swipe_len < MAX_SWIPE_LEN && !ignore_next_release && !palm_rejected && !unlock_combo_active) {
                        if (swipe_len == 0 || swipe_buffer[swipe_len - 1] != bit) {
                            swipe_buffer[swipe_len++] = bit;
                        }
                    }
                }
            }
            last_status = current_status;
        }

        // --- ОБРАБОТКА ОТПУСКАНИЯ ---
        if (total_active_touches == 0) {
            if (ignore_next_release) {
                ignore_next_release = false;
                release_cycles = 0;
            } else if (swipe_len > 0) {
                release_cycles++;
                if (release_cycles >= 3) { 
                    if (!palm_rejected) {
                        hmi_msg_t msg;
                        msg.type = analyze_swipe();
                        if (msg.type != EVENT_NONE) send_hmi_event(&msg); 
                    }
                    swipe_len = 0;
                    release_cycles = 0;
                }
            }
            palm_rejected = false; 
        } else {
            release_cycles = 0;
        }

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

esp_err_t gesture_manager_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_ORIENTATION_SENSOR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    hmi_event_queue = xQueueCreate(10, sizeof(hmi_msg_t));
    if (!hmi_event_queue) return ESP_ERR_NO_MEM;

    if (xTaskCreatePinnedToCore(mpr121_polling_task, "mpr_poll", 4096, NULL, 6, NULL, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}