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

// НОВЫЕ ФУНКЦИИ ЭКРАНА РЕДАКТИРОВАНИЯ
void ui_screens_show_edit(const char* title, const char* value_str);
void ui_screens_update_edit_value(const char* value_str);

// НОВАЯ ФУНКЦИЯ ПЕРЕСТРОЕНИЯ МАКЕТА
void ui_screens_update_layout(bool is_landscape);

// Обновление телеметрии на Главном экране и в Статус-баре
void ui_screens_update_telemetry(float temp, float hum, uint8_t rssi, bool ble, bool locked, bool guitar_present, int32_t weight_g);

// ИСПРАВЛЕНО: Обновление строк меню (теперь всего 3 аргумента)
void ui_screens_render_menu(const char* items[5], int count, int selected_idx);

#ifdef __cplusplus
}
#endif