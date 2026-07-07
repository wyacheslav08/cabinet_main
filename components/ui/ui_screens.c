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
#define SYM_SCALES        "\uF24E"
#define SYM_GUITAR        "\uF7A6"

// Внутренние ссылки на виджеты
static lv_obj_t* cont_main = NULL;
static lv_obj_t* cont_menu = NULL;

// Виджеты Главного экрана
static lv_obj_t* lbl_hum_int = NULL;
static lv_obj_t* lbl_hum_frac = NULL;
static lv_obj_t* lbl_hum_unit = NULL;
static lv_obj_t* lbl_temp_int = NULL;
static lv_obj_t* lbl_temp_frac = NULL;
static lv_obj_t* lbl_temp_unit = NULL;

// Виджеты Меню
static lv_obj_t* lbl_status_icons = NULL;
static lv_obj_t* lbl_status_climate = NULL;
static lv_obj_t* menu_items[5] = {NULL};

void ui_screens_init(void) {
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);

    // =========================================================================
    // 1. ГЛАВНЫЙ ЭКРАН (Крупная типографика по ТЗ)
    // =========================================================================
    cont_main = lv_obj_create(screen);
    lv_obj_set_size(cont_main, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(cont_main, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont_main, 0, 0);
    lv_obj_remove_flag(cont_main, LV_OBJ_FLAG_SCROLLABLE);

    // --- Влажность (Слева внизу) ---
    lbl_hum_int = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_hum_int, &font_cyrillic_48, 0);
    lv_obj_set_style_text_color(lbl_hum_int, lv_color_hex(0x00FFFF), 0);
    lv_label_set_text(lbl_hum_int, "55");
    lv_obj_align(lbl_hum_int, LV_ALIGN_BOTTOM_LEFT, 4, -15);

    lbl_hum_frac = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_hum_frac, &font_cyrillic_20, 0);
    lv_obj_set_style_text_color(lbl_hum_frac, lv_color_hex(0x00AAAA), 0);
    lv_label_set_text(lbl_hum_frac, ".00");
    lv_obj_align_to(lbl_hum_frac, lbl_hum_int, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, -6);

    lbl_hum_unit = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_hum_unit, &font_cyrillic_20, 0);
    lv_obj_set_style_text_color(lbl_hum_unit, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(lbl_hum_unit, "H%");
    lv_obj_align_to(lbl_hum_unit, lbl_hum_int, LV_ALIGN_OUT_RIGHT_TOP, 2, 8);

    // --- Температура (Справа внизу) ---
    lbl_temp_int = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_temp_int, &font_cyrillic_48, 0);
    lv_obj_set_style_text_color(lbl_temp_int, lv_color_hex(0xFF8800), 0);
    lv_label_set_text(lbl_temp_int, "25");
    lv_obj_align(lbl_temp_int, LV_ALIGN_BOTTOM_RIGHT, -32, -15);

    lbl_temp_frac = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_temp_frac, &font_cyrillic_20, 0);
    lv_obj_set_style_text_color(lbl_temp_frac, lv_color_hex(0xAA5500), 0);
    lv_label_set_text(lbl_temp_frac, ".00");
    lv_obj_align_to(lbl_temp_frac, lbl_temp_int, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, -6);

    lbl_temp_unit = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_temp_unit, &font_cyrillic_20, 0);
    lv_obj_set_style_text_color(lbl_temp_unit, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(lbl_temp_unit, "C°");
    lv_obj_align_to(lbl_temp_unit, lbl_temp_int, LV_ALIGN_OUT_RIGHT_TOP, 2, 8);

    // =========================================================================
    // 2. ЭКРАН МЕНЮ
    // =========================================================================
    cont_menu = lv_obj_create(screen);
    lv_obj_set_size(cont_menu, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(cont_menu, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(cont_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cont_menu, 0, 0);
    lv_obj_set_style_pad_all(cont_menu, 0, 0);
    lv_obj_add_flag(cont_menu, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* top_bar = lv_obj_create(cont_menu);
    lv_obj_set_size(top_bar, lv_pct(100), 26);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 4, 0);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);

    lbl_status_icons = lv_label_create(top_bar);
    lv_obj_set_style_text_font(lbl_status_icons, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_status_icons, lv_color_hex(0xFFFF00), 0);
    lv_label_set_text(lbl_status_icons, SYM_WIFI " " SYM_LOCK_CLOSED);
    lv_obj_align(lbl_status_icons, LV_ALIGN_LEFT_MID, 0, 0);

    lbl_status_climate = lv_label_create(top_bar);
    lv_obj_set_style_text_font(lbl_status_climate, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_status_climate, lv_color_hex(0x00FFFF), 0);
    lv_label_set_text(lbl_status_climate, "--.-°C --%");
    lv_obj_align(lbl_status_climate, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t* list_cont = lv_obj_create(cont_menu);
    lv_obj_set_size(list_cont, lv_pct(100), 134);
    lv_obj_align(list_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_cont, 0, 0);
    lv_obj_set_style_pad_all(list_cont, 2, 0);
    lv_obj_set_layout(list_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);

    for (int i = 0; i < 5; i++) {
        menu_items[i] = lv_label_create(list_cont);
        lv_obj_set_width(menu_items[i], lv_pct(95));
        lv_obj_set_style_text_font(menu_items[i], &font_cyrillic_12, 0);
        lv_obj_set_style_pad_all(menu_items[i], 6, 0);
        lv_obj_set_style_radius(menu_items[i], 3, 0);
        lv_label_set_long_mode(menu_items[i], LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    }
}

void ui_screens_show_main(void) {
    if (cont_main && cont_menu) {
        lv_obj_remove_flag(cont_main, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont_menu, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_screens_show_menu(void) {
    if (cont_main && cont_menu) {
        lv_obj_add_flag(cont_main, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(cont_menu, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_screens_update_telemetry(float temp, float hum, uint8_t rssi, bool ble, bool locked, bool guitar_present, int32_t weight_g) {
    int hum_int = (int)hum;
    int hum_frac = (int)(fabs(hum - hum_int) * 100.0f);
    int temp_int = (int)temp;
    int temp_frac = (int)(fabs(temp - temp_int) * 100.0f);

    char s_h_int[8], s_h_frac[8], s_t_int[8], s_t_frac[8];
    snprintf(s_h_int, sizeof(s_h_int), "%d", hum_int);
    snprintf(s_h_frac, sizeof(s_h_frac), ".%02d", hum_frac);
    snprintf(s_t_int, sizeof(s_t_int), "%d", temp_int);
    snprintf(s_t_frac, sizeof(s_t_frac), ".%02d", temp_frac);

    char str_icons[32];
    snprintf(str_icons, sizeof(str_icons), "%s %s %s", rssi > 0 ? SYM_WIFI : " ", ble ? SYM_BLE : " ", locked ? SYM_LOCK_CLOSED : SYM_LOCK_OPEN);

    char str_climate_mini[32];
    snprintf(str_climate_mini, sizeof(str_climate_mini), "%.1f°C %.0f%%", temp, hum);

    if (lbl_hum_int) lv_label_set_text(lbl_hum_int, s_h_int);
    if (lbl_hum_frac) lv_label_set_text(lbl_hum_frac, s_h_frac);
    if (lbl_temp_int) lv_label_set_text(lbl_temp_int, s_t_int);
    if (lbl_temp_frac) lv_label_set_text(lbl_temp_frac, s_t_frac);
    if (lbl_status_icons) lv_label_set_text(lbl_status_icons, str_icons);
    if (lbl_status_climate) lv_label_set_text(lbl_status_climate, str_climate_mini);
}

void ui_screens_render_menu(const char* items[5], int count, int selected_idx, bool is_editing, const char* edit_text) {
    for (int i = 0; i < 5; i++) {
        if (i < count) {
            lv_obj_remove_flag(menu_items[i], LV_OBJ_FLAG_HIDDEN);
            
            if (is_editing && i == selected_idx) {
                lv_label_set_text(menu_items[i], edit_text);
                lv_obj_set_style_bg_color(menu_items[i], lv_color_hex(0xCC0000), 0); // Красный при редактировании
                lv_obj_set_style_text_color(menu_items[i], lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_translate_x(menu_items[i], 4, 0);
            } 
            else if (i == selected_idx) {
                lv_label_set_text(menu_items[i], items[i]);
                lv_obj_set_style_bg_color(menu_items[i], lv_color_hex(0x0066CC), 0); // Синий курсор
                lv_obj_set_style_bg_opa(menu_items[i], LV_OPA_COVER, 0);
                lv_obj_set_style_text_color(menu_items[i], lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_translate_x(menu_items[i], 4, 0);
            } 
            else {
                lv_label_set_text(menu_items[i], items[i]);
                lv_obj_set_style_bg_opa(menu_items[i], LV_OPA_TRANSP, 0);
                lv_obj_set_style_text_color(menu_items[i], lv_color_hex(0xAAAAAA), 0);
                lv_obj_set_style_translate_x(menu_items[i], 0, 0);
            }
        } else {
            lv_obj_add_flag(menu_items[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}