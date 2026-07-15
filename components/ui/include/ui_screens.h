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
void ui_screens_show_splash(void); // Приветственный экран
void ui_screens_show_main(void);
void ui_screens_show_menu(void);

// НОВЫЕ ФУНКЦИИ ЭКРАНА РЕДАКТИРОВАНИЯ
void ui_screens_show_edit(const char* title, const char* value_str);
void ui_screens_update_edit_value(const char* value_str);

// НОВАЯ ФУНКЦИЯ ПЕРЕСТРОЕНИЯ МАКЕТА
void ui_screens_update_layout(bool is_landscape);

// Обновление телеметрии на Главном экране и в Статус-баре
void ui_screens_update_telemetry(float temp, float hum, uint8_t rssi, bool ble, bool locked, bool guitar_present, int32_t weight_g);

// Обновление строк меню (теперь всего 3 аргумента)
void ui_screens_render_menu(const char* items[5], int count, int selected_idx);

// --- НОВЫЕ ЭКРАНЫ ДЛЯ ФАЗЫ 2 ---
// Всплывающее сообщение "Сохранено"
void ui_screens_show_popup(const char* text);

// Экран таблицы SHT
void ui_screens_show_sht_table(void);
// Обновление данных в таблице SHT. 
// selected_row: 0..3 (выбранный датчик). 
// is_editing: true (мигает значение), false (просто навигация)
void ui_screens_update_sht_table(int selected_row, int edit_mode, 
                                 float t_main, float h_main, int adj_t_main, int adj_h_main,
                                 float t_hum, float h_hum, int adj_t_hum, int adj_h_hum,
                                 float t_deh, float h_deh, int adj_t_deh, int adj_h_deh,
                                 float t_ext, float h_ext, int adj_t_ext, int adj_h_ext);

// Экраны пароля
void ui_screens_show_pass_inst(bool is_not_set); // is_not_set = true покажет "Пароль не установлен"
void ui_screens_show_pass_input(const char* gestures_str); // gestures_str - строка со стрелочками

#ifdef __cplusplus
}
#endif