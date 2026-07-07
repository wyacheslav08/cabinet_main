#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация всех графических элементов (вызывать под мьютексом LVGL!)
void ui_screens_init(void);

// Переключение видимости экранов
void ui_screens_show_main(void);
void ui_screens_show_menu(void);

// Обновление телеметрии на Главном экране и в Статус-баре
void ui_screens_update_telemetry(float temp, float hum, uint8_t rssi, bool ble, bool locked, bool guitar_present, int32_t weight_g);

// Обновление строк меню (визуализация скроллинга и выделения)
void ui_screens_render_menu(const char* items[5], int count, int selected_idx, bool is_editing, const char* edit_text);

#ifdef __cplusplus
}
#endif