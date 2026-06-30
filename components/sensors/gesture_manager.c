#include "gesture_manager.h"
#include "mpr121_driver.h"
#include "hw_config.h"
#include "settings_manager.h" // Для чтения времени удержания замка
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "GESTURE";
static volatile gesture_t last_gesture = GESTURE_NONE;
static volatile bool lock_button_state = false;

#define SEQ_MAX_LEN 32
static int col_sequence[SEQ_MAX_LEN];
static int row_sequence[SEQ_MAX_LEN];
static int seq_idx = 0;

// Маска контактов замка (Индексы 21, 22, 23)
#define LOCK_PADS_MASK ((1ULL << 21) | (1ULL << 22) | (1ULL << 23))

gesture_t gesture_get_last(void) {
    gesture_t g = last_gesture;
    last_gesture = GESTURE_NONE; 
    return g;
}

bool gesture_is_lock_pressed(void) {
    return lock_button_state;
}

// Функция преобразования физического пина (0..47) в логический индекс матрицы (0..44)
static int map_to_matrix(int phys_pin) {
    if (phys_pin >= 21 && phys_pin <= 23) return -1; // Это пины замка, они вне матрицы
    if (phys_pin < 21) return phys_pin;              // Пины до замка остаются как есть
    return phys_pin - 3;                             // Пины после замка сдвигаем на 3 вниз
}

static void analyze_sequence(void) {
    if (seq_idx < 2) {
        last_gesture = GESTURE_CLICK;
        ESP_LOGI(TAG, "=> Detected: CLICK at [Col: %d, Row: %d]", col_sequence[0], row_sequence[0]);
        return;
    }

    int start_col = col_sequence[0];
    int end_col = col_sequence[seq_idx - 1];
    int start_row = row_sequence[0];
    int end_row = row_sequence[seq_idx - 1];

    int col_diff = end_col - start_col;
    int row_diff = end_row - start_row;

    if (abs(col_diff) > abs(row_diff)) {
        if (col_diff > 2) last_gesture = GESTURE_SWIPE_RIGHT;
        else if (col_diff < -2) last_gesture = GESTURE_SWIPE_LEFT;
    } else {
        if (row_diff > 1) last_gesture = GESTURE_SWIPE_DOWN;
        else if (row_diff < -1) last_gesture = GESTURE_SWIPE_UP;
    }

    // --- ОТЛАДОЧНЫЙ ВЫВОД (ТРАЕКТОРИЯ И ЖЕСТ) ---
    if (last_gesture != GESTURE_NONE) {
        const char* gesture_name = "UNKNOWN";
        switch (last_gesture) {
            case GESTURE_SWIPE_RIGHT: gesture_name = "SWIPE RIGHT"; break;
            case GESTURE_SWIPE_LEFT:  gesture_name = "SWIPE LEFT"; break;
            case GESTURE_SWIPE_UP:    gesture_name = "SWIPE UP"; break;
            case GESTURE_SWIPE_DOWN:  gesture_name = "SWIPE DOWN"; break;
            default: break;
        }

        // Собираем траекторию в одну строку для красивого вывода
        char path_buf[128] = {0};
        int offset = 0;
        for (int i = 0; i < seq_idx; i++) {
            offset += snprintf(path_buf + offset, sizeof(path_buf) - offset, 
                               "[%d,%d] ", col_sequence[i], row_sequence[i]);
            if (offset >= sizeof(path_buf) - 5) break; // Защита от переполнения буфера
        }

        ESP_LOGW(TAG, "========================================");
        ESP_LOGW(TAG, "=> Detected: %s", gesture_name);
        ESP_LOGW(TAG, "=> Path: %s", path_buf);
        ESP_LOGW(TAG, "=> Total points: %d (Delta Col: %d, Delta Row: %d)", seq_idx, col_diff, row_diff);
        ESP_LOGW(TAG, "========================================");
    }

    if (last_gesture != GESTURE_NONE) {
        ESP_LOGI(TAG, "Gesture: %d (Cols: %d->%d, Rows: %d->%d)", last_gesture, start_col, end_col, start_row, end_row);
    }
}

static void gesture_scan_task(void *pvParameters) {
    uint8_t mux_channels[4] = {MUX_CH_MPR_1, MUX_CH_MPR_2, MUX_CH_MPR_3, MUX_CH_MPR_4};
    uint64_t full_mask = 0;
    uint64_t prev_mask = 0;
    uint32_t lock_press_start_time = 0;

    while (1) {
        full_mask = 0;

        // ОТКАЗОУСТОЙЧИВЫЙ ОПРОС: Если чип не ответил, его биты просто останутся нулями!
        for (int i = 0; i < 4; i++) {
            uint16_t touch_val = 0;
            mpr121_get_touched(mux_channels[i], &touch_val); // Игнорируем ошибку, продолжаем опрос остальных
            full_mask |= ((uint64_t)touch_val << (i * 12));
        }

        // --- ЛОГИКА ЗАМКА (Двойное назначение) ---
        bool only_lock_touched = ((full_mask & LOCK_PADS_MASK) != 0) && ((full_mask & ~LOCK_PADS_MASK) == 0);
        
        if (only_lock_touched) {
            // Касаются ТОЛЬКО замка
            if (lock_press_start_time == 0) {
                lock_press_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            } else {
                settings_lock();
                uint32_t hold_time = sys_settings.lockHoldTime;
                settings_unlock();
                
                if ((xTaskGetTickCount() * portTICK_PERIOD_MS) - lock_press_start_time >= hold_time) {
                    lock_button_state = true; // Замок активирован!
                }
            }
        } else {
            // Либо ничего не нажато, либо нажато что-то еще (свайп)
            lock_press_start_time = 0;
            lock_button_state = false;
        }

        // --- ЛОГИКА МАТРИЦЫ (Свайпы) ---
        if (full_mask != 0) {
            for (int i = 0; i < 48; i++) {
                if ((full_mask & (1ULL << i)) && !(prev_mask & (1ULL << i))) {
                    // Обрабатываем только новые касания
                    int matrix_idx = map_to_matrix(i);
                    
                    if (matrix_idx != -1 && seq_idx < SEQ_MAX_LEN) { // Если это не пин замка
                        col_sequence[seq_idx] = matrix_idx / 3; // Колонка 0..14
                        row_sequence[seq_idx] = matrix_idx % 3; // Строка 0..2
                        seq_idx++;
                    }
                }
            }
        } else if (prev_mask != 0 && full_mask == 0) {
            // Палец убрали -> Анализируем жест
            if (lock_press_start_time == 0) { // Игнорируем, если это было чистое нажатие на замок
                analyze_sequence();
            }
            seq_idx = 0; 
        }

        prev_mask = full_mask;
        vTaskDelay(pdMS_TO_TICKS(30)); // 33 FPS
    }
}

esp_err_t gesture_manager_init(void) {
    uint8_t mux_channels[4] = {MUX_CH_MPR_1, MUX_CH_MPR_2, MUX_CH_MPR_3, MUX_CH_MPR_4};
    
    // Инициализируем чипы (если какой-то не подключен, он выдаст ошибку в лог, но система пойдет дальше)
    for (int i = 0; i < 4; i++) {
        mpr121_init(mux_channels[i], 10, 4);
    }

    xTaskCreate(gesture_scan_task, "gesture_task", 4096, NULL, 6, NULL);
    return ESP_OK;
}