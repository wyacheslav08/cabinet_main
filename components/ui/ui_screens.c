/**
 * @file ui_screens.c
 * @brief Модуль верстки: создание и отрисовка виджетов LVGL.
 */

#include "ui_screens.h"
#include "lvgl.h"
#include <stdio.h>
#include <math.h>

LV_FONT_DECLARE(font_cyrillic_12);
LV_FONT_DECLARE(font_cyrillic_16);
LV_FONT_DECLARE(font_cyrillic_20);
LV_FONT_DECLARE(font_cyrillic_48);

#define SYM_WIFI          "\uF1EB"
#define SYM_BLE           "\uF294"
#define SYM_LOCK_CLOSED   "\uF023"
#define SYM_LOCK_OPEN     "\uF09C"

static lv_obj_t* cont_splash = NULL;
static lv_obj_t* cont_main = NULL;
static lv_obj_t* cont_menu = NULL;
static lv_obj_t* cont_edit = NULL;

// Главный экран
static lv_obj_t* lbl_hum_int;
static lv_obj_t* lbl_hum_frac;
static lv_obj_t* lbl_hum_unit;
static lv_obj_t* lbl_temp_int;
static lv_obj_t* lbl_temp_frac;
static lv_obj_t* lbl_temp_unit;

// Меню
static lv_obj_t* lbl_status_icons;
static lv_obj_t* lbl_status_climate;
static lv_obj_t* menu_items[5];

// Экран редактирования
static lv_obj_t* lbl_edit_title;
static lv_obj_t* lbl_edit_val;
static lv_obj_t* lbl_edit_hints;

void ui_screens_init(void) {
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);

    // =========================================================================
    // 0. ЭКРАН ЗАГРУЗКИ (SPLASH SCREEN)
    // =========================================================================
    cont_splash = lv_obj_create(screen);
    lv_obj_set_size(cont_splash, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(cont_splash, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(cont_splash, 0, 0);

    lv_obj_t* lbl_logo = lv_label_create(cont_splash);
    lv_obj_set_style_text_font(lbl_logo, &font_cyrillic_20, 0); // Крупный шрифт
    lv_obj_set_style_text_color(lbl_logo, lv_color_hex(0xFFB800), 0); // Золотой цвет
    lv_label_set_text(lbl_logo, "GUITAR\nCABINET");
    lv_obj_set_style_text_align(lbl_logo, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_logo, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t* lbl_wait = lv_label_create(cont_splash);
    lv_obj_set_style_text_font(lbl_wait, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_wait, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(lbl_wait, "Калибровка\nсенсоров.\nПожалуйста,\nподождите");
    lv_obj_set_style_text_align(lbl_wait, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_wait, LV_ALIGN_CENTER, 0, 30);

    // =========================================================================
    // 1. ГЛАВНЫЙ ЭКРАН
    // =========================================================================
    cont_main = lv_obj_create(screen);
    lv_obj_set_size(cont_main, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(cont_main, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont_main, 0, 0);

    // Влажность
    lbl_hum_int = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_hum_int, &font_cyrillic_48, 0);
    lv_obj_set_style_text_color(lbl_hum_int, lv_color_hex(0xFF8800), 0);
    
    lbl_hum_frac = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_hum_frac, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_hum_frac, lv_color_hex(0xFF8800), 0);
    
    lbl_hum_unit = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_hum_unit, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_hum_unit, lv_color_hex(0xFF8800), 0);
    lv_label_set_text(lbl_hum_unit, "%н");

    // Температура
    lbl_temp_int = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_temp_int, &font_cyrillic_48, 0);
    lv_obj_set_style_text_color(lbl_temp_int, lv_color_hex(0xFF8800), 0);
    
    lbl_temp_frac = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_temp_frac, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_temp_frac, lv_color_hex(0xFF8800), 0);
    
    lbl_temp_unit = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_temp_unit, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_temp_unit, lv_color_hex(0xFF8800), 0);
    lv_label_set_text(lbl_temp_unit, "°С");

    // =========================================================================
    // 2. ЭКРАН МЕНЮ (Исправлено наложение текста)
    // =========================================================================
    cont_menu = lv_obj_create(screen);
    lv_obj_set_size(cont_menu, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(cont_menu, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_width(cont_menu, 0, 0);
    lv_obj_set_style_pad_all(cont_menu, 0, 0);
    lv_obj_add_flag(cont_menu, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* top_bar = lv_obj_create(cont_menu);
    lv_obj_set_size(top_bar, lv_pct(100), 24); // Высота статус-бара 24px
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 2, 0);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);

    lbl_status_icons = lv_label_create(top_bar);
    lv_obj_set_style_text_font(lbl_status_icons, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_status_icons, lv_color_hex(0xFFFF00), 0);
    lv_obj_align(lbl_status_icons, LV_ALIGN_LEFT_MID, 2, 0);

    lbl_status_climate = lv_label_create(top_bar);
    lv_obj_set_style_text_font(lbl_status_climate, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_status_climate, lv_color_hex(0x00FFFF), 0);
    lv_obj_align(lbl_status_climate, LV_ALIGN_RIGHT_MID, -2, 0);

    lv_obj_t* list_cont = lv_obj_create(cont_menu);
    // КРИТИЧНО: Отступаем сверху на 26px, чтобы не лезть на статус-бар
    lv_obj_set_size(list_cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_top(list_cont, 26, 0); 
    lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_cont, 0, 0);
    lv_obj_set_layout(list_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_cont, 0, 0); // Мелкий отступ между строками

    for (int i = 0; i < 5; i++) {
        menu_items[i] = lv_label_create(list_cont);
        lv_obj_set_width(menu_items[i], lv_pct(100));
        lv_obj_set_style_text_font(menu_items[i], &font_cyrillic_12, 0);
        lv_obj_set_style_pad_all(menu_items[i], 4, 0);
        lv_label_set_long_mode(menu_items[i], LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    }

    // =========================================================================
    // 3. НОВЫЙ ЭКРАН РЕДАКТИРОВАНИЯ
    // =========================================================================
    cont_edit = lv_obj_create(screen);
    lv_obj_set_size(cont_edit, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(cont_edit, lv_color_hex(0x001133), 0); // Темно-синий фон
    lv_obj_set_style_border_width(cont_edit, 0, 0);
    lv_obj_add_flag(cont_edit, LV_OBJ_FLAG_HIDDEN);

    lbl_edit_title = lv_label_create(cont_edit);
    lv_obj_set_style_text_font(lbl_edit_title, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_edit_title, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(lbl_edit_title, LV_ALIGN_TOP_MID, 0, 5);

    lbl_edit_val = lv_label_create(cont_edit);
    lv_obj_set_style_text_font(lbl_edit_val, &font_cyrillic_48, 0);
    lv_obj_set_style_text_color(lbl_edit_val, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_edit_val, LV_ALIGN_CENTER, 0, -10);

    lbl_edit_hints = lv_label_create(cont_edit);
    lv_obj_set_style_text_font(lbl_edit_hints, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_edit_hints, lv_color_hex(0x555555), 0);
    lv_label_set_text(lbl_edit_hints, "Вверх/Вниз: Изм.\nВлево: Отмена  Вправо: ОК");
    lv_obj_set_style_text_align(lbl_edit_hints, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_edit_hints, LV_ALIGN_BOTTOM_MID, 0, -5);
}

// ДИНАМИЧЕСКОЕ ПОЗИЦИОНИРОВАНИЕ ГЛАВНОГО ЭКРАНА
void ui_screens_update_layout(bool is_landscape) {
    if (is_landscape) {
        // Горизонтально (90/270 град) - По краям внизу
        lv_obj_align(lbl_hum_int, LV_ALIGN_BOTTOM_LEFT, -10, -5); //5,-5
        lv_obj_align(lbl_temp_int, LV_ALIGN_BOTTOM_RIGHT, -5, -5); // 45,-5
    } else {
        // Вертикально (0/180 град) - Друг над другом (Влажность вверху слева, Темп внизу справа)
        lv_obj_align(lbl_hum_int, LV_ALIGN_TOP_LEFT, 0, 25);    // 5,20
        lv_obj_align(lbl_temp_int, LV_ALIGN_BOTTOM_RIGHT, -25, 0);    //-45,-20
    }

    // Выравнивание дробей и символов относительно целых чисел (Одинаково для обоих режимов)
    lv_obj_align_to(lbl_hum_frac, lbl_hum_int, LV_ALIGN_OUT_RIGHT_BOTTOM, 0, -6);
    lv_obj_align_to(lbl_hum_unit, lbl_hum_int, LV_ALIGN_OUT_RIGHT_TOP, 0, 6);
    lv_obj_align_to(lbl_temp_frac, lbl_temp_int, LV_ALIGN_OUT_RIGHT_BOTTOM, 0, -6);
    lv_obj_align_to(lbl_temp_unit, lbl_temp_int, LV_ALIGN_OUT_RIGHT_TOP, 0, 6);
}

void ui_screens_show_splash(void) {
    lv_obj_remove_flag(cont_splash, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_main, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_edit, LV_OBJ_FLAG_HIDDEN);
}

void ui_screens_show_main(void) {
    lv_obj_add_flag(cont_splash, LV_OBJ_FLAG_HIDDEN); // <--- СКРЫВАЕМ SPLASH
    lv_obj_remove_flag(cont_main, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_edit, LV_OBJ_FLAG_HIDDEN);
}

void ui_screens_show_menu(void) {
    lv_obj_add_flag(cont_splash, LV_OBJ_FLAG_HIDDEN); // <--- СКРЫВАЕМ SPLASH
    lv_obj_add_flag(cont_main, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(cont_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_edit, LV_OBJ_FLAG_HIDDEN);
}


void ui_screens_show_edit(const char* title, const char* value_str) {
    lv_obj_add_flag(cont_splash, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lbl_edit_title, title);
    lv_label_set_text(lbl_edit_val, value_str);
    
    lv_obj_add_flag(cont_main, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(cont_edit, LV_OBJ_FLAG_HIDDEN);
}

void ui_screens_update_edit_value(const char* value_str) {
    lv_label_set_text(lbl_edit_val, value_str);
}

void ui_screens_render_menu(const char* items[5], int count, int selected_idx) {
    for (int i = 0; i < 5; i++) {
        if (i < count) {
            lv_obj_remove_flag(menu_items[i], LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(menu_items[i], items[i]);
            
            if (i == selected_idx) {
                lv_obj_set_style_bg_color(menu_items[i], lv_color_hex(0x0066CC), 0);
                lv_obj_set_style_bg_opa(menu_items[i], LV_OPA_COVER, 0);
                lv_obj_set_style_text_color(menu_items[i], lv_color_hex(0xFFFFFF), 0);
            } else {
                lv_obj_set_style_bg_opa(menu_items[i], LV_OPA_TRANSP, 0);
                lv_obj_set_style_text_color(menu_items[i], lv_color_hex(0xAAAAAA), 0);
            }
        } else {
            lv_obj_add_flag(menu_items[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// В функции ui_screens_update_telemetry заменим логику:

void ui_screens_update_telemetry(float temp, float hum, uint8_t rssi, bool ble, bool locked, bool guitar_present, int32_t weight_g) {
    char s_h_int[8], s_h_frac[8], s_t_int[8], s_t_frac[8];
    char str_climate_mini[32];

    // Если данные невалидны (ошибка датчика)
    if (temp <= -90.0f || hum <= -90.0f) {
        snprintf(s_h_int, sizeof(s_h_int), "--");
        snprintf(s_h_frac, sizeof(s_h_frac), ".--");
        snprintf(s_t_int, sizeof(s_t_int), "--");
        snprintf(s_t_frac, sizeof(s_t_frac), ".--");
        snprintf(str_climate_mini, sizeof(str_climate_mini), "--.-°C --%%");
    } else {
        int hum_int = (int)hum;
        int hum_frac = (int)(fabs(hum - hum_int) * 100.0f);
        int temp_int = (int)temp;
        int temp_frac = (int)(fabs(temp - temp_int) * 100.0f);

        snprintf(s_h_int, sizeof(s_h_int), "%d", hum_int);
        snprintf(s_h_frac, sizeof(s_h_frac), ".%02d", hum_frac);
        snprintf(s_t_int, sizeof(s_t_int), "%d", temp_int);
        snprintf(s_t_frac, sizeof(s_t_frac), ".%02d", temp_frac);
        snprintf(str_climate_mini, sizeof(str_climate_mini), "%.1f°C %.0f%%", temp, hum);
    }

    char str_icons[32];
    snprintf(str_icons, sizeof(str_icons), "%s %s %s", rssi > 0 ? SYM_WIFI : " ", ble ? SYM_BLE : " ", locked ? SYM_LOCK_CLOSED : SYM_LOCK_OPEN);

    // Обновляем текст
    if (lbl_hum_int) lv_label_set_text(lbl_hum_int, s_h_int);
    if (lbl_hum_frac) lv_label_set_text(lbl_hum_frac, s_h_frac);
    if (lbl_temp_int) lv_label_set_text(lbl_temp_int, s_t_int);
    if (lbl_temp_frac) lv_label_set_text(lbl_temp_frac, s_t_frac);
    
    if (lbl_status_icons) lv_label_set_text(lbl_status_icons, str_icons);
    if (lbl_status_climate) lv_label_set_text(lbl_status_climate, str_climate_mini);

        // КРИТИЧНО: Заставляем LVGL пересчитать привязки (ALIGN_OUT) после изменения текста.
    // В LVGL 9 для этого мы просто заново применяем правило выравнивания.
    if (lbl_hum_frac && lbl_hum_int) {
        lv_obj_align_to(lbl_hum_frac, lbl_hum_int, LV_ALIGN_OUT_RIGHT_BOTTOM, 0, -6);
    }
    if (lbl_hum_unit && lbl_hum_int) {
        lv_obj_align_to(lbl_hum_unit, lbl_hum_int, LV_ALIGN_OUT_RIGHT_TOP, 0, 6);
    }
    
    if (lbl_temp_frac && lbl_temp_int) {
        lv_obj_align_to(lbl_temp_frac, lbl_temp_int, LV_ALIGN_OUT_RIGHT_BOTTOM, 0, -6);
    }
    if (lbl_temp_unit && lbl_temp_int) {
        lv_obj_align_to(lbl_temp_unit, lbl_temp_int, LV_ALIGN_OUT_RIGHT_TOP, 0, 6);
    }
}