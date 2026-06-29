#include "gesture_manager.h"
#include "mpr121_driver.h"
#include "hw_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "GESTURE";
static volatile gesture_t last_gesture = GESTURE_NONE;
static volatile bool lock_button_state = false;

// Храним последовательность касаний колонок и строк для распознавания свайпа
#define SEQ_MAX_LEN 16
static int col_sequence[SEQ_MAX_LEN];
static int row_sequence[SEQ_MAX_LEN];
static int seq_idx = 0;

gesture_t gesture_get_last(void) {
    gesture_t g = last_gesture;
    last_gesture = GESTURE_NONE; // Сбрасываем после чтения
    return g;
}

bool gesture_is_lock_pressed(void) {
    return lock_button_state;
}

// Анализ собранной траектории пальца
static void analyze_sequence(void) {
    if (seq_idx < 2) {
        last_gesture = GESTURE_CLICK;
        return;
    }

    int start_col = col_sequence[0];
    int end_col = col_sequence[seq_idx - 1];
    int start_row = row_sequence[0];
    int end_row = row_sequence[seq_idx - 1];

    int col_diff = end_col - start_col;
    int row_diff = end_row - start_row;

    // Что изменилось сильнее: колонка (горизонталь) или строка (вертикаль)?
    if (abs(col_diff) > abs(row_diff)) {
        if (col_diff > 3) last_gesture = GESTURE_SWIPE_RIGHT;
        else if (col_diff < -3) last_gesture = GESTURE_SWIPE_LEFT;
    } else {
        if (row_diff > 1) last_gesture = GESTURE_SWIPE_DOWN;
        else if (row_diff < -1) last_gesture = GESTURE_SWIPE_UP;
    }

    if (last_gesture != GESTURE_NONE) {
        ESP_LOGI(TAG, "Detected Gesture: %d (Cols: %d->%d, Rows: %d->%d)", 
                 last_gesture, start_col, end_col, start_row, end_row);
    }
}

// Фоновая задача опроса 4-х чипов MPR121
static void gesture_scan_task(void *pvParameters) {
    uint8_t mux_channels[4] = {MUX_CH_MPR_1, MUX_CH_MPR_2, MUX_CH_MPR_3, MUX_CH_MPR_4};
    uint64_t full_mask = 0;
    uint64_t prev_mask = 0;

    while (1) {
        full_mask = 0;

        // Опрашиваем все 4 чипа и склеиваем их в один 48-битный битовый массив
        for (int i = 0; i < 4; i++) {
            uint16_t touch_val = 0;
            if (mpr121_get_touched(mux_channels[i], &touch_val) == ESP_OK) {
                full_mask |= ((uint64_t)touch_val << (i * 12));
            }
        }

        // 1. Проверяем 3 кнопки замка (последние пины: 45, 46, 47)
        lock_button_state = ((full_mask & (7ULL << 45)) != 0);

        // 2. Обработка матрицы 3x15 (пины 0..44)
        uint64_t grid_mask = full_mask & 0x1FFFFFFFFFFULL; // Маска первых 45 бит

        if (grid_mask != 0) {
            // Палец касается панели! Находим координаты (Row, Col)
            for (int i = 0; i < 45; i++) {
                if ((grid_mask & (1ULL << i)) && !(prev_mask & (1ULL << i))) {
                    // Новый нажатый контакт
                    int col = i / 3; // Колонка 0..14
                    int row = i % 3; // Строка 0..2

                    if (seq_idx < SEQ_MAX_LEN) {
                        col_sequence[seq_idx] = col;
                        row_sequence[seq_idx] = row;
                        seq_idx++;
                    }
                }
            }
        } else if (prev_mask != 0 && grid_mask == 0) {
            // Палец убрали от панели -> Анализируем жест!
            analyze_sequence();
            seq_idx = 0; // Сброс записи
        }

        prev_mask = grid_mask;
        vTaskDelay(pdMS_TO_TICKS(30)); // Опрос ~33 раза в секунду
    }
}

esp_err_t gesture_manager_init(void) {
    uint8_t mux_channels[4] = {MUX_CH_MPR_1, MUX_CH_MPR_2, MUX_CH_MPR_3, MUX_CH_MPR_4};
    
    // Инициализируем все 4 чипа
    for (int i = 0; i < 4; i++) {
        mpr121_init(mux_channels[i], 10, 4);
    }

    BaseType_t res = xTaskCreate(gesture_scan_task, "gesture_task", 4096, NULL, 6, NULL);
    return (res == pdPASS) ? ESP_OK : ESP_FAIL;
}