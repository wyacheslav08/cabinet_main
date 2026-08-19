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

/* =========================================================================
 * СЛОВАРЬ ИКОНОК (FONT AWESOME 6 SOLID)
 * Цвета указаны в формате BGR (так как большинство дисплеев используют BGR порядок)
 * ========================================================================= */
#define SYM_SWIPE_UP      "\uF0A6" // Рука указывает вверх
#define SYM_SWIPE_DOWN    "\uF0A7" // Рука указывает вниз
#define SYM_SWIPE_LEFT    "\uF0A5" // Рука указывает влево
#define SYM_SWIPE_RIGHT   "\uF0A4" // Рука указывает вправо
#define SYM_TAP           "\uF25A" // Жест нажатия (Тап)
#define SYM_PERCENT       "%"      // Знак процента
#define SYM_LOCK_CLOSED   "\uF023" // Замок закрыт (белый 0xFFFFFF)
#define SYM_LOCK_OPEN     "\uF09C" // Замок открыт (белый 0xFFFFFF)
#define SYM_WIFI          "\uF1EB" // Wi-Fi сигнал (белый 0xFFFFFF)
#define SYM_BLE           "\uF294" // Bluetooth BLE (белый 0xFFFFFF)
#define SYM_SCALES        "\uF24E" // Весы/Баланс (светло-голубой 0x00CCFF в BGR = 0xFFCC00 в RGB)
#define SYM_SOUND         "\uF028" // Звуковой сигнал/Динамик (синий 0xFF0000 в BGR = 0x0000FF в RGB)
#define SYM_MUSIC         "\uF001" // Нотный ключ/Нота (фиолетовый 0xAA00FF - без изменений, симметричный)
#define SYM_VIBRO         "\uF3CD" // Вибрация/Телефон с волнами (серый 0x888888)
#define SYM_GUITAR        "\uF7A6" // Гитара (светло-зеленый 0x99FF99 в BGR)
#define SYM_BRIGHTNESS    "\uF185" // Подсветка/Солнце (синий 0xFF0000 в BGR = 0x0000FF в RGB)
#define SYM_HUMIDIFIER    "\uF043" // Увлажнитель/Капля воды (оранжевый 0x0080FF в BGR = 0xFF8000 в RGB)
#define SYM_DEHUMIDIFIER  "\uF5C7" // Осушитель/Перечеркнутая капля (циан 0xFFFF00 в BGR = 0x00FFFF в RGB)
#define SYM_HEATING       "\uF06D" // Нагрев/Пламя (синий 0x0000FF в BGR = 0xFF0000 в RGB)

static lv_obj_t* cont_splash = NULL;
static lv_obj_t* cont_main = NULL;
static lv_obj_t* cont_menu = NULL;
static lv_obj_t* cont_edit = NULL;
static lv_obj_t* lbl_popup = NULL;

// Иконки статус-бара главного экрана (правый верхний угол) - системные
static lv_obj_t* lbl_icon_wifi = NULL;   // Иконка WiFi (уровень сигнала) - белый
static lv_obj_t* lbl_icon_ble = NULL;    // Иконка Bluetooth (подключение) - белый
static lv_obj_t* lbl_icon_lock = NULL;   // Иконка замка (заблокировано/открыто) - белый

// Иконки климат-контроля (левая часть главного экрана)
static lv_obj_t* lbl_icon_heating = NULL;    // Иконка обогрева/пламя - оранжево-красный
static lv_obj_t* lbl_icon_humidifier = NULL; // Иконка увлажнителя/капля - голубой
static lv_obj_t* lbl_icon_dehumidifier = NULL; // Иконка осушителя/перечеркнутая капля - циан

// Иконка присутствия гитары (низ по центру)
static lv_obj_t* lbl_icon_guitar = NULL;     // Иконка гитары - зеленый

// Дополнительные иконки для отладки (центральный ряд)
static lv_obj_t* lbl_icon_scales = NULL;     // Весы/Баланс - желтый
static lv_obj_t* lbl_icon_sound = NULL;      // Звук/Динамик - оранжевый
static lv_obj_t* lbl_icon_music = NULL;      // Нота/Музыка - фиолетовый
static lv_obj_t* lbl_icon_vibro = NULL;      // Вибро - серый
static lv_obj_t* lbl_icon_brightness = NULL; // Подсветка/Солнце - желтый

// Иконки жестов (для справки)
static lv_obj_t* lbl_icon_swipe_up = NULL;    // Свайп вверх
static lv_obj_t* lbl_icon_swipe_down = NULL;  // Свайп вниз
static lv_obj_t* lbl_icon_swipe_left = NULL;  // Свайп влево
static lv_obj_t* lbl_icon_swipe_right = NULL; // Свайп вправо
static lv_obj_t* lbl_icon_tap = NULL;         // Тап

static lv_timer_t* lock_blink_timer = NULL;



// --- ДОБАВЛЯЕМ ПЕРЕМЕННЫЕ ДЛЯ АНИМАЦИИ ---
static lv_obj_t* lbl_loading_dots = NULL;
static lv_timer_t* splash_timer = NULL;

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

// Коллбек вызывается движком LVGL каждые 500 мс
static void splash_anim_cb(lv_timer_t * timer) {
    static uint8_t dot_count = 0;
    dot_count++;
    if (dot_count > 3) dot_count = 0;

    switch(dot_count) {
        case 0: lv_label_set_text(lbl_loading_dots, ""); break;
        case 1: lv_label_set_text(lbl_loading_dots, "."); break;
        case 2: lv_label_set_text(lbl_loading_dots, ".."); break;
        case 3: lv_label_set_text(lbl_loading_dots, "..."); break;
    }
}

// --- Добавить в начало ui_screens.c к глобальным переменным ---
LV_FONT_DECLARE(font_cyrillic_8); // Добавляем мелкий шрифт для таблицы

static lv_obj_t* cont_popup = NULL;
static lv_timer_t* popup_timer = NULL;

static lv_obj_t* cont_sht = NULL;
static lv_obj_t* sht_labels_name[4];
static lv_obj_t* sht_labels_data[4][3]; // 0=Факт, 1=Корр.Влаж, 2=Корр.Темп

static lv_obj_t* cont_pass_inst = NULL;
static lv_obj_t* lbl_pass_inst_status = NULL;

static lv_obj_t* cont_pass_input = NULL;
static lv_obj_t* lbl_pass_title = NULL;
static lv_obj_t* lbl_pass_input_val = NULL;


// --- Коллбэк для таймера всплывающего окна ---
static void popup_timer_cb(lv_timer_t * timer) {
    if (cont_popup) {
        lv_obj_add_flag(cont_popup, LV_OBJ_FLAG_HIDDEN);
    }
    // Ставим таймер на паузу, чтобы он не срабатывал каждую секунду,
    // но при этом НЕ УДАЛЯЛСЯ из памяти движком LVGL.
    lv_timer_pause(timer); 
}

// --- КОЛЛБЭК ТАЙМЕРА МИГАНИЯ ЗАМКА ---
static void lock_blink_cb(lv_timer_t * timer) {
    if (!lbl_icon_lock) return;
    if (lv_obj_has_flag(lbl_icon_lock, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(lbl_icon_lock, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(lbl_icon_lock, LV_OBJ_FLAG_HIDDEN);
    }
}

// --- ФУНКЦИЯ УПРАВЛЕНИЯ МИГАНИЕМ ---
void ui_screens_set_lock_blink(bool enable) {
    if (enable) {
        if (!lock_blink_timer) {
            lock_blink_timer = lv_timer_create(lock_blink_cb, 300, NULL);
        }
    } else {
        if (lock_blink_timer) {
            lv_timer_delete(lock_blink_timer);
            lock_blink_timer = NULL;
        }
        if (lbl_icon_lock) {
            lv_obj_remove_flag(lbl_icon_lock, LV_OBJ_FLAG_HIDDEN); // Оставляем видимым при выключении
        }
    }
}

void ui_screens_init(void) {
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    
    // ПРАВИЛЬНЫЙ СПОСОБ (LVGL 9): Скрываем полосу визуально, не ломая логику движка
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    // =========================================================================
    // 0. ЭКРАН ЗАГРУЗКИ (SPLASH SCREEN)
    // =========================================================================
    cont_splash = lv_obj_create(screen);
    lv_obj_set_size(cont_splash, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(cont_splash, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(cont_splash, 0, 0);
    lv_obj_set_scrollbar_mode(cont_splash, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* lbl_logo = lv_label_create(cont_splash);
    lv_obj_set_style_text_font(lbl_logo, &font_cyrillic_20, 0); 
    lv_obj_set_style_text_color(lbl_logo, lv_color_hex(0xFFB800), 0); 
    lv_label_set_text(lbl_logo, "GUITAR\nCABINET");
    lv_obj_set_style_text_align(lbl_logo, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_logo, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t* lbl_wait = lv_label_create(cont_splash);
    lv_obj_set_style_text_font(lbl_wait, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_wait, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(lbl_wait, "Калибровка\nсенсоров.\nПожалуйста,\nподождите");
    lv_obj_set_style_text_align(lbl_wait, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_wait, LV_ALIGN_CENTER, 0, 15);

    lbl_loading_dots = lv_label_create(cont_splash);
    lv_obj_set_style_text_font(lbl_loading_dots, &font_cyrillic_20, 0); 
    lv_obj_set_style_text_color(lbl_loading_dots, lv_color_hex(0xFFB800), 0); 
    lv_label_set_text(lbl_loading_dots, "");
    lv_obj_align_to(lbl_loading_dots, lbl_wait, LV_ALIGN_OUT_BOTTOM_MID, 0, -20);

    // =========================================================================
    // 1. ГЛАВНЫЙ ЭКРАН
    // =========================================================================
    cont_main = lv_obj_create(screen);
    lv_obj_set_size(cont_main, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(cont_main, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont_main, 0, 0);
    lv_obj_set_scrollbar_mode(cont_main, LV_SCROLLBAR_MODE_OFF);

    // Влажность - СИНИЙ ЦВЕТ (0x0088FF) для контраста с оранжевыми иконками климата
    lbl_hum_int = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_hum_int, &font_cyrillic_48, 0);
    lv_obj_set_style_text_color(lbl_hum_int, lv_color_hex(0x0088FF), 0);
    
    lbl_hum_frac = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_hum_frac, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_hum_frac, lv_color_hex(0x0088FF), 0);
    
    lbl_hum_unit = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_hum_unit, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_hum_unit, lv_color_hex(0x0088FF), 0);
    lv_label_set_text(lbl_hum_unit, "%н");

    // Температура - СИНИЙ ЦВЕТ (0x0088FF) для контраста с оранжевыми иконками климата
    lbl_temp_int = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_temp_int, &font_cyrillic_48, 0);
    lv_obj_set_style_text_color(lbl_temp_int, lv_color_hex(0x0088FF), 0);
    
    lbl_temp_frac = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_temp_frac, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_temp_frac, lv_color_hex(0x0088FF), 0);
    
    lbl_temp_unit = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_temp_unit, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_temp_unit, lv_color_hex(0x0088FF), 0);
    lv_label_set_text(lbl_temp_unit, "°С");

    /*/ =========================================================================
    // ИКОНКИ ГЛАВНОГО ЭКРАНА (Все возможные иконки из словаря)
    // =========================================================================
    
    // --- Контейнер системных иконок в правом верхнем углу (WiFi, BLE, Lock) ---
    lv_obj_t* icon_cont_right = lv_obj_create(cont_main);
    lv_obj_set_size(icon_cont_right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(icon_cont_right, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_layout(icon_cont_right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(icon_cont_right, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(icon_cont_right, 8, 0); // Отступ между иконками
    lv_obj_set_style_bg_opa(icon_cont_right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_cont_right, 0, 0);
    lv_obj_set_scrollbar_mode(icon_cont_right, LV_SCROLLBAR_MODE_OFF);

    // Иконка WiFi (уровень сигнала сети) - белый цвет
    lbl_icon_wifi = lv_label_create(icon_cont_right);
    lv_obj_set_style_text_font(lbl_icon_wifi, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_icon_wifi, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_icon_wifi, SYM_WIFI);

    // Иконка Bluetooth (подключение устройства) - белый цвет
    lbl_icon_ble = lv_label_create(icon_cont_right);
    lv_obj_set_style_text_font(lbl_icon_ble, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_icon_ble, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_icon_ble, SYM_BLE);

    // Иконка замка (блокировка кабинета) - белый цвет
    lbl_icon_lock = lv_label_create(icon_cont_right);
    lv_obj_set_style_text_font(lbl_icon_lock, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_icon_lock, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_icon_lock, SYM_LOCK_CLOSED);
   
    // --- Контейнер иконок климат-контроля в левом верхнем углу ---
    lv_obj_t* icon_cont_left = lv_obj_create(cont_main);
    lv_obj_set_size(icon_cont_left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(icon_cont_left, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_layout(icon_cont_left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(icon_cont_left, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(icon_cont_left, 8, 0); // Отступ между иконками
    lv_obj_set_style_bg_opa(icon_cont_left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_cont_left, 0, 0);
    lv_obj_set_scrollbar_mode(icon_cont_left, LV_SCROLLBAR_MODE_OFF);

    // Иконка обогрева/пламя - КРАСНЫЙ (0xFF0000 в RGB)
    lbl_icon_heating = lv_label_create(icon_cont_left);
    lv_obj_set_style_text_font(lbl_icon_heating, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_icon_heating, lv_color_hex(0xFF0000), 0);
    lv_label_set_text(lbl_icon_heating, SYM_HEATING);

    // Иконка увлажнителя/капля - ОРАНЖЕВЫЙ (0xFF8000 в RGB)
    lbl_icon_humidifier = lv_label_create(icon_cont_left);
    lv_obj_set_style_text_font(lbl_icon_humidifier, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_icon_humidifier, lv_color_hex(0xFF8000), 0);
    lv_label_set_text(lbl_icon_humidifier, SYM_HUMIDIFIER);

    // Иконка осушителя/перечеркнутая капля - СИНИЙ (0x0000FF в RGB)
    lbl_icon_dehumidifier = lv_label_create(icon_cont_left);
    lv_obj_set_style_text_font(lbl_icon_dehumidifier, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_icon_dehumidifier, lv_color_hex(0x0000FF), 0);
    lv_label_set_text(lbl_icon_dehumidifier, SYM_DEHUMIDIFIER);

    // --- Иконка присутствия гитары (нижняя часть, по центру) - СВЕТЛО-ЗЕЛЕНЫЙ (0x99FF99 в RGB) ---
    lbl_icon_guitar = lv_label_create(cont_main);
    lv_obj_set_style_text_font(lbl_icon_guitar, &font_cyrillic_20, 0);
    lv_obj_set_style_text_color(lbl_icon_guitar, lv_color_hex(0x99FF99), 0);
    lv_label_set_text(lbl_icon_guitar, SYM_GUITAR);
    lv_obj_align(lbl_icon_guitar, LV_ALIGN_BOTTOM_MID, 0, -50);

    // --- Центральный ряд: дополнительные иконки для отладки ---
    lv_obj_t* icon_cont_center = lv_obj_create(cont_main);
    lv_obj_set_size(icon_cont_center, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(icon_cont_center, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_layout(icon_cont_center, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(icon_cont_center, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(icon_cont_center, 12, 0);
    lv_obj_set_style_bg_opa(icon_cont_center, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_cont_center, 0, 0);
    lv_obj_set_scrollbar_mode(icon_cont_center, LV_SCROLLBAR_MODE_OFF);

    // Иконка весов/баланс - СВЕТЛО-ГОЛУБОЙ (0x00CCFF в RGB)
    lbl_icon_scales = lv_label_create(icon_cont_center);
    lv_obj_set_style_text_font(lbl_icon_scales, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_icon_scales, lv_color_hex(0x00CCFF), 0);
    lv_label_set_text(lbl_icon_scales, SYM_SCALES);

    // Иконка звука/динамик - СИНИЙ (0x0000FF в RGB)
    lbl_icon_sound = lv_label_create(icon_cont_center);
    lv_obj_set_style_text_font(lbl_icon_sound, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_icon_sound, lv_color_hex(0x0000FF), 0);
    lv_label_set_text(lbl_icon_sound, SYM_SOUND);

    // Иконка музыки/нота - ФИОЛЕТОВЫЙ (0xAA00FF в RGB)
    lbl_icon_music = lv_label_create(icon_cont_center);
    lv_obj_set_style_text_font(lbl_icon_music, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_icon_music, lv_color_hex(0xAA00FF), 0);
    lv_label_set_text(lbl_icon_music, SYM_MUSIC);

    // Иконка вибро - СЕРЫЙ (0x888888 в RGB, без изменений)
    lbl_icon_vibro = lv_label_create(icon_cont_center);
    lv_obj_set_style_text_font(lbl_icon_vibro, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_icon_vibro, lv_color_hex(0x888888), 0);
    lv_label_set_text(lbl_icon_vibro, SYM_VIBRO);

    // Иконка подсветки/солнце - СИНИЙ (0x0000FF в RGB)
    lbl_icon_brightness = lv_label_create(icon_cont_center);
    lv_obj_set_style_text_font(lbl_icon_brightness, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_icon_brightness, lv_color_hex(0x0000FF), 0);
    lv_label_set_text(lbl_icon_brightness, SYM_BRIGHTNESS);

    // --- Нижний ряд: иконки жестов (для справки) ---
    lv_obj_t* icon_cont_gestures = lv_obj_create(cont_main);
    lv_obj_set_size(icon_cont_gestures, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(icon_cont_gestures, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_layout(icon_cont_gestures, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(icon_cont_gestures, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(icon_cont_gestures, 10, 0);
    lv_obj_set_style_bg_opa(icon_cont_gestures, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_cont_gestures, 0, 0);
    lv_obj_set_scrollbar_mode(icon_cont_gestures, LV_SCROLLBAR_MODE_OFF);

    // Иконка свайпа вверх - белый цвет
    lbl_icon_swipe_up = lv_label_create(icon_cont_gestures);
    lv_obj_set_style_text_font(lbl_icon_swipe_up, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_icon_swipe_up, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_icon_swipe_up, SYM_SWIPE_UP);

    // Иконка свайпа вниз - белый цвет
    lbl_icon_swipe_down = lv_label_create(icon_cont_gestures);
    lv_obj_set_style_text_font(lbl_icon_swipe_down, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_icon_swipe_down, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_icon_swipe_down, SYM_SWIPE_DOWN);

    // Иконка свайпа влево - белый цвет
    lbl_icon_swipe_left = lv_label_create(icon_cont_gestures);
    lv_obj_set_style_text_font(lbl_icon_swipe_left, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_icon_swipe_left, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_icon_swipe_left, SYM_SWIPE_LEFT);

    // Иконка свайпа вправо - белый цвет
    lbl_icon_swipe_right = lv_label_create(icon_cont_gestures);
    lv_obj_set_style_text_font(lbl_icon_swipe_right, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_icon_swipe_right, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_icon_swipe_right, SYM_SWIPE_RIGHT);

    // Иконка тапа - белый цвет
    lbl_icon_tap = lv_label_create(icon_cont_gestures);
    lv_obj_set_style_text_font(lbl_icon_tap, &font_cyrillic_16, 0);
    lv_obj_set_style_text_color(lbl_icon_tap, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_icon_tap, SYM_TAP);
    */
   
    // =========================================================================
    // 2. ЭКРАН МЕНЮ
    // =========================================================================
    cont_menu = lv_obj_create(screen);
    lv_obj_set_size(cont_menu, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(cont_menu, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_width(cont_menu, 0, 0);
    lv_obj_set_style_pad_all(cont_menu, 0, 0);
    lv_obj_add_flag(cont_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_scrollbar_mode(cont_menu, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* top_bar = lv_obj_create(cont_menu);
    lv_obj_set_size(top_bar, lv_pct(100), 24); 
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 2, 0);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_scrollbar_mode(top_bar, LV_SCROLLBAR_MODE_OFF); // Защита для статус-бара

    lbl_status_icons = lv_label_create(top_bar);
    lv_obj_set_style_text_font(lbl_status_icons, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_status_icons, lv_color_hex(0xFFFF00), 0);
    lv_obj_align(lbl_status_icons, LV_ALIGN_LEFT_MID, 2, 0);

    lbl_status_climate = lv_label_create(top_bar);
    lv_obj_set_style_text_font(lbl_status_climate, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_status_climate, lv_color_hex(0x00FFFF), 0);
    lv_obj_align(lbl_status_climate, LV_ALIGN_RIGHT_MID, -2, 0);

    lv_obj_t* list_cont = lv_obj_create(cont_menu);
    lv_obj_set_size(list_cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_top(list_cont, 26, 0); 
    lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_cont, 0, 0);
    lv_obj_set_layout(list_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_cont, 0, 0); 
    lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_OFF); // Скрываем скроллбар списка

    for (int i = 0; i < 5; i++) {
        menu_items[i] = lv_label_create(list_cont);
        lv_obj_set_width(menu_items[i], lv_pct(100));
        lv_obj_set_style_text_font(menu_items[i], &font_cyrillic_12, 0);
        lv_obj_set_style_pad_all(menu_items[i], 4, 0);
        lv_label_set_long_mode(menu_items[i], LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    }

    // =========================================================================
    // 3. ЭКРАН РЕДАКТИРОВАНИЯ
    // =========================================================================
    cont_edit = lv_obj_create(screen);
    lv_obj_set_size(cont_edit, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(cont_edit, lv_color_hex(0x001133), 0); 
    lv_obj_set_style_border_width(cont_edit, 0, 0);
    lv_obj_add_flag(cont_edit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_scrollbar_mode(cont_edit, LV_SCROLLBAR_MODE_OFF);

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


    // =========================================================================
    // 4. ЭКРАН ТАБЛИЦЫ SHT
    // =========================================================================
    cont_sht = lv_obj_create(screen);
    lv_obj_set_size(cont_sht, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(cont_sht, lv_color_hex(0x001133), 0);
    lv_obj_set_style_border_width(cont_sht, 0, 0);
    lv_obj_set_style_pad_all(cont_sht, 0, 0); // Убрали лишние отступы
    lv_obj_add_flag(cont_sht, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_scrollbar_mode(cont_sht, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* lbl_sht_title = lv_label_create(cont_sht);
    lv_obj_set_style_text_font(lbl_sht_title, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_sht_title, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(lbl_sht_title, "Корректировка SHT");
    lv_obj_align(lbl_sht_title, LV_ALIGN_TOP_MID, 0, 2);

    // Сетка под 12-й шрифт (Ширина 128: 38 + 42 + 24 + 24)
    static int32_t col_dsc[] = {38, 42, 24, 24, LV_GRID_TEMPLATE_LAST};
    static int32_t row_dsc[] = {10, 7, 7, 7, 7, LV_GRID_TEMPLATE_LAST}; // Увеличен интервал строк

    lv_obj_t* grid_sht = lv_obj_create(cont_sht);
    lv_obj_set_size(grid_sht, lv_pct(100), 115);
    lv_obj_align(grid_sht, LV_ALIGN_TOP_MID, 0, 20); // Опустили ниже заголовка
    lv_obj_set_layout(grid_sht, LV_LAYOUT_GRID);
    lv_obj_set_style_grid_column_dsc_array(grid_sht, col_dsc, 0);
    lv_obj_set_style_grid_row_dsc_array(grid_sht, row_dsc, 0);
    lv_obj_set_style_bg_opa(grid_sht, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid_sht, 0, 0);
    lv_obj_set_style_pad_all(grid_sht, 0, 0);

    const char* headers[] = {"Датч", "%/C°", "%+", "C+"};
    for(int i = 0; i < 4; i++) {
        lv_obj_t* lbl = lv_label_create(grid_sht);
        lv_obj_set_style_text_font(lbl, &font_cyrillic_12, 0); // 12-й шрифт
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x00FFFF), 0);
        lv_label_set_text(lbl, headers[i]);
        lv_obj_set_grid_cell(lbl, LV_GRID_ALIGN_START, i, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    }

    const char* row_names[] = {"Главн", "Увлаж", "Осуш", "Внешн"};
    for(int r = 0; r < 4; r++) {
        sht_labels_name[r] = lv_label_create(grid_sht);
        lv_obj_set_style_text_font(sht_labels_name[r], &font_cyrillic_12, 0); // 12-й шрифт
        lv_obj_set_style_pad_all(sht_labels_name[r], 2, 0);
        lv_label_set_text(sht_labels_name[r], row_names[r]);
        lv_obj_set_grid_cell(sht_labels_name[r], LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, r+1, 1);

        for(int c = 1; c <= 3; c++) {
            sht_labels_data[r][c-1] = lv_label_create(grid_sht);
            lv_obj_set_style_text_font(sht_labels_data[r][c-1], &font_cyrillic_12, 0); // 12-й шрифт
            lv_obj_set_style_pad_all(sht_labels_data[r][c-1], 2, 0);
            lv_label_set_text(sht_labels_data[r][c-1], (c==1) ? "--/--" : "0");
            lv_obj_set_grid_cell(sht_labels_data[r][c-1], LV_GRID_ALIGN_START, c, 1, LV_GRID_ALIGN_CENTER, r+1, 1);
        }
    }

    lv_obj_t* lbl_sht_hints = lv_label_create(cont_sht);
    lv_obj_set_style_text_font(lbl_sht_hints, &font_cyrillic_8, 0);
    lv_obj_set_style_text_color(lbl_sht_hints, lv_color_hex(0x555555), 0);
    lv_label_set_text(lbl_sht_hints, "Ручка-выбор  Вверх/Вниз-изм.\n<- Отмена    -> Сохранить");
    lv_obj_set_style_text_align(lbl_sht_hints, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_sht_hints, LV_ALIGN_BOTTOM_MID, 0, 0);

    // =========================================================================
    // 5. ЭКРАН ИНСТРУКЦИИ ПАРОЛЯ
    // =========================================================================
    cont_pass_inst = lv_obj_create(screen);
    lv_obj_set_size(cont_pass_inst, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(cont_pass_inst, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_width(cont_pass_inst, 0, 0);
    lv_obj_add_flag(cont_pass_inst, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* lbl_inst_text = lv_label_create(cont_pass_inst);
    lv_obj_set_style_text_font(lbl_inst_text, &font_cyrillic_8, 0);
    lv_label_set_text(lbl_inst_text, "Для создания пароля\nсделайте от 1 до 5 жестов\nпо панели.\nДля сохранения коснитесь\nручки двери.\n\nПроведите ВПРАВО\nчтобы начать.");
    lv_obj_set_style_text_align(lbl_inst_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_inst_text, LV_ALIGN_TOP_MID, 0, 10);

    lbl_pass_inst_status = lv_label_create(cont_pass_inst);
    lv_obj_set_style_text_font(lbl_pass_inst_status, &font_cyrillic_8, 0);
    lv_obj_set_style_text_color(lbl_pass_inst_status, lv_color_hex(0xFF5555), 0);
    lv_label_set_text(lbl_pass_inst_status, "Пароль не установлен");
    lv_obj_align(lbl_pass_inst_status, LV_ALIGN_BOTTOM_MID, 0, -10);

    // =========================================================================
    // 6. ЭКРАН ВВОДА ПАРОЛЯ
    // =========================================================================
    cont_pass_input = lv_obj_create(screen);
    lv_obj_set_size(cont_pass_input, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(cont_pass_input, lv_color_hex(0x001133), 0);
    lv_obj_set_style_border_width(cont_pass_input, 0, 0);
    lv_obj_add_flag(cont_pass_input, LV_OBJ_FLAG_HIDDEN);

    lbl_pass_title = lv_label_create(cont_pass_input);
    lv_obj_set_style_text_font(lbl_pass_title, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_pass_title, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(lbl_pass_title, "Введите новый пароль");
    lv_obj_align(lbl_pass_title, LV_ALIGN_TOP_MID, 0, 10);

    lbl_pass_input_val = lv_label_create(cont_pass_input);
    lv_obj_set_style_text_font(lbl_pass_input_val, &font_cyrillic_20, 0); // Крупные стрелочки
    lv_obj_set_style_text_color(lbl_pass_input_val, lv_color_hex(0xFFB800), 0);
    lv_label_set_text(lbl_pass_input_val, ""); // Например: "v > ^"
    lv_obj_align(lbl_pass_input_val, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* lbl_pass_hints = lv_label_create(cont_pass_input);
    lv_obj_set_style_text_font(lbl_pass_hints, &font_cyrillic_8, 0);
    lv_obj_set_style_text_color(lbl_pass_hints, lv_color_hex(0x555555), 0);
    lv_label_set_text(lbl_pass_hints, "Сохранить - коснуться ручки");
    lv_obj_align(lbl_pass_hints, LV_ALIGN_BOTTOM_MID, 0, -10);

    // =========================================================================
    // 7. ВСПЛЫВАЮЩЕЕ ОКНО "СОХРАНЕНО" (Поверх всего)
    // =========================================================================
    cont_popup = lv_obj_create(screen);
    lv_obj_set_size(cont_popup, 100, 40);
    lv_obj_align(cont_popup, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(cont_popup, lv_color_hex(0x00AA00), 0); // Зеленый фон
    lv_obj_set_style_border_color(cont_popup, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(cont_popup, 2, 0);
    lv_obj_set_style_radius(cont_popup, 8, 0);
    lv_obj_add_flag(cont_popup, LV_OBJ_FLAG_HIDDEN); // Скрыто по умолчанию
    lv_obj_set_scrollbar_mode(cont_popup, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* lbl_popup = lv_label_create(cont_popup);
    lv_obj_set_style_text_font(lbl_popup, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(lbl_popup, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_popup, "Сохранено");
    lv_obj_align(lbl_popup, LV_ALIGN_CENTER, 0, 0);
}

// ДИНАМИЧЕСКОЕ ПОЗИЦИОНИРОВАНИЕ ГЛАВНОГО ЭКРАНА
void ui_screens_update_layout(bool is_landscape) {
    if (is_landscape) {
        // Горизонтально (90/270 град) - По краям внизу
        lv_obj_align(lbl_hum_int, LV_ALIGN_BOTTOM_LEFT, -10, -5); //5,-5
        lv_obj_align(lbl_temp_int, LV_ALIGN_BOTTOM_RIGHT, -10, -5); // 45,-5
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

    // Запускаем таймер анимации (500 мс)
    if (!splash_timer) {
        splash_timer = lv_timer_create(splash_anim_cb, 500, NULL);
    }
}

void ui_screens_show_main(void) {
    lv_obj_add_flag(cont_splash, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(cont_main, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_edit, LV_OBJ_FLAG_HIDDEN);
    
    // Прячем новые экраны Фазы 2
    if(cont_sht) lv_obj_add_flag(cont_sht, LV_OBJ_FLAG_HIDDEN);
    if(cont_pass_inst) lv_obj_add_flag(cont_pass_inst, LV_OBJ_FLAG_HIDDEN);
    if(cont_pass_input) lv_obj_add_flag(cont_pass_input, LV_OBJ_FLAG_HIDDEN);

    if (splash_timer) {
        lv_timer_delete(splash_timer);
        splash_timer = NULL;
    }
}

void ui_screens_show_menu(void) {
    lv_obj_add_flag(cont_splash, LV_OBJ_FLAG_HIDDEN); 
    lv_obj_add_flag(cont_main, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(cont_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_edit, LV_OBJ_FLAG_HIDDEN);
    
    // Прячем новые экраны Фазы 2
    if(cont_sht) lv_obj_add_flag(cont_sht, LV_OBJ_FLAG_HIDDEN);
    if(cont_pass_inst) lv_obj_add_flag(cont_pass_inst, LV_OBJ_FLAG_HIDDEN);
    if(cont_pass_input) lv_obj_add_flag(cont_pass_input, LV_OBJ_FLAG_HIDDEN);
}

void ui_screens_show_edit(const char* title, const char* value_str) {
    lv_obj_add_flag(cont_splash, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lbl_edit_title, title);
    lv_label_set_text(lbl_edit_val, value_str);
    
    lv_obj_add_flag(cont_main, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(cont_edit, LV_OBJ_FLAG_HIDDEN);
    
    // Прячем новые экраны Фазы 2
    if(cont_sht) lv_obj_add_flag(cont_sht, LV_OBJ_FLAG_HIDDEN);
    if(cont_pass_inst) lv_obj_add_flag(cont_pass_inst, LV_OBJ_FLAG_HIDDEN);
    if(cont_pass_input) lv_obj_add_flag(cont_pass_input, LV_OBJ_FLAG_HIDDEN);
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


void ui_screens_update_telemetry(float temp, float hum, uint8_t rssi, bool ble, bool locked, bool guitar_present, int32_t weight_g,
                                  bool is_heating, bool is_humidifying, bool is_dehumidifying) {
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
         int hum_frac = (int)(fabs(hum - hum_int) * 10.0f);
         int temp_int = (int)temp;
         int temp_frac = (int)(fabs(temp - temp_int) * 10.0f);
         snprintf(s_h_int, sizeof(s_h_int), "%d", hum_int);
         snprintf(s_h_frac, sizeof(s_h_frac), ".%01d", hum_frac);
         snprintf(s_t_int, sizeof(s_t_int), "%d", temp_int);
         snprintf(s_t_frac, sizeof(s_t_frac), ".%01d", temp_frac);
         snprintf(str_climate_mini, sizeof(str_climate_mini), "%.1f°C %.0f%%", temp, hum);
     }

    char str_icons[64];
    snprintf(str_icons, sizeof(str_icons), "%s %s %s", SYM_WIFI, SYM_BLE, SYM_LOCK_CLOSED);

    // Обновляем текст основных виджетов (температура/влажность временно отключены)
     if (lbl_hum_int) lv_label_set_text(lbl_hum_int, s_h_int);
     if (lbl_hum_frac) lv_label_set_text(lbl_hum_frac, s_h_frac);
     if (lbl_temp_int) lv_label_set_text(lbl_temp_int, s_t_int);
     if (lbl_temp_frac) lv_label_set_text(lbl_temp_frac, s_t_frac);
    
    // Старая строка иконок для меню (статус-бар экрана меню)
    if (lbl_status_icons) lv_label_set_text(lbl_status_icons, str_icons);
    // if (lbl_status_climate) lv_label_set_text(lbl_status_climate, str_climate_mini);

    // =========================================================================
    // ОБНОВЛЕНИЕ ВСЕХ ИКОНОК ГЛАВНОГО ЭКРАНА
    // Все иконки отображаются постоянно для проверки видимости и цветопередачи
    // =========================================================================
    
    // --- Иконки правого верхнего угла (системные) - белый цвет ---
    if (lbl_icon_wifi) lv_label_set_text(lbl_icon_wifi, SYM_WIFI);
    if (lbl_icon_ble)  lv_label_set_text(lbl_icon_ble, SYM_BLE);
    if (lbl_icon_lock) lv_label_set_text(lbl_icon_lock, SYM_LOCK_CLOSED);
    
    // --- Иконки климат-контроля (левый верхний угол) ---
    // Отображаем все иконки постоянно для проверки цветов
    if (lbl_icon_heating) lv_label_set_text(lbl_icon_heating, SYM_HEATING);      // Оранжево-красный (0xFF4400)
    if (lbl_icon_humidifier) lv_label_set_text(lbl_icon_humidifier, SYM_HUMIDIFIER); // Голубой (0x0088FF)
    if (lbl_icon_dehumidifier) lv_label_set_text(lbl_icon_dehumidifier, SYM_DEHUMIDIFIER); // Циан (0x00FFFF)
    
    // --- Иконка присутствия гитары (низ по центру) - зеленый цвет ---
    if (lbl_icon_guitar) lv_label_set_text(lbl_icon_guitar, SYM_GUITAR);

    // --- Центральный ряд: дополнительные иконки ---
    if (lbl_icon_scales) lv_label_set_text(lbl_icon_scales, SYM_SCALES);         // Желтый (0xFFFF00)
    if (lbl_icon_sound) lv_label_set_text(lbl_icon_sound, SYM_SOUND);            // Оранжевый (0xFFA500)
    if (lbl_icon_music) lv_label_set_text(lbl_icon_music, SYM_MUSIC);            // Фиолетовый (0xAA00FF)
    if (lbl_icon_vibro) lv_label_set_text(lbl_icon_vibro, SYM_VIBRO);            // Серый (0x888888)
    if (lbl_icon_brightness) lv_label_set_text(lbl_icon_brightness, SYM_BRIGHTNESS); // Желтый (0xFFDD00)

    // --- Нижний ряд: иконки жестов ---
    if (lbl_icon_swipe_up) lv_label_set_text(lbl_icon_swipe_up, SYM_SWIPE_UP);
    if (lbl_icon_swipe_down) lv_label_set_text(lbl_icon_swipe_down, SYM_SWIPE_DOWN);
    if (lbl_icon_swipe_left) lv_label_set_text(lbl_icon_swipe_left, SYM_SWIPE_LEFT);
    if (lbl_icon_swipe_right) lv_label_set_text(lbl_icon_swipe_right, SYM_SWIPE_RIGHT);
    if (lbl_icon_tap) lv_label_set_text(lbl_icon_tap, SYM_TAP);

    // КРИТИЧНО: Заставляем LVGL пересчитать привязки (ALIGN_OUT) для дробных значений
    // (временно отключено вместе с температурой/влажностью)
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

void ui_screens_show_popup(const char* text) {
    if (lbl_popup) lv_label_set_text(lbl_popup, text);
    
    lv_obj_remove_flag(cont_popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(cont_popup); 
    
    if (popup_timer) {
        // Если таймер уже был создан ранее, просто "будим" его и сбрасываем счетчик
        lv_timer_resume(popup_timer);
        lv_timer_reset(popup_timer);
    } else {
        // Создаем таймер при первом вызове (без repeat_count)
        popup_timer = lv_timer_create(popup_timer_cb, 1000, NULL);
    }
}

void ui_screens_show_sht_table(void) {
    lv_obj_add_flag(cont_splash, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_main, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_edit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_pass_inst, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_pass_input, LV_OBJ_FLAG_HIDDEN);

    lv_obj_remove_flag(cont_sht, LV_OBJ_FLAG_HIDDEN);
}

void ui_screens_update_sht_table(int selected_row, int edit_mode, 
                                 float t_main, float h_main, int adj_t_main, int adj_h_main,
                                 float t_hum, float h_hum, int adj_t_hum, int adj_h_hum,
                                 float t_deh, float h_deh, int adj_t_deh, int adj_h_deh,
                                 float t_ext, float h_ext, int adj_t_ext, int adj_h_ext) 
{
    char buf_val[16], buf_adj_h[8], buf_adj_t[8];

    float t_arr[4] = {t_main, t_hum, t_deh, t_ext};
    float h_arr[4] = {h_main, h_hum, h_deh, h_ext};
    int at_arr[4] = {adj_t_main, adj_t_hum, adj_t_deh, adj_t_ext};
    int ah_arr[4] = {adj_h_main, adj_h_hum, adj_h_deh, adj_h_ext};

    for(int r = 0; r < 4; r++) {
        if (t_arr[r] <= -90.0f) snprintf(buf_val, sizeof(buf_val), "--/--");
        else snprintf(buf_val, sizeof(buf_val), "%.0f/%.0f", h_arr[r], t_arr[r]);
        lv_label_set_text(sht_labels_data[r][0], buf_val);

        snprintf(buf_adj_h, sizeof(buf_adj_h), "%d", ah_arr[r]);
        lv_label_set_text(sht_labels_data[r][1], buf_adj_h);

        snprintf(buf_adj_t, sizeof(buf_adj_t), "%d", at_arr[r]);
        lv_label_set_text(sht_labels_data[r][2], buf_adj_t);

        // --- ЛОГИКА ПОДСВЕТКИ ---
        bool is_sel_row = (r == selected_row);
        
        // 1. Имя датчика (Синий фон, если выбрана его строка)
        lv_obj_set_style_bg_color(sht_labels_name[r], lv_color_hex(0x0066CC), 0);
        lv_obj_set_style_bg_opa(sht_labels_name[r], is_sel_row ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(sht_labels_name[r], is_sel_row ? lv_color_hex(0xFFFFFF) : lv_color_hex(0xAAAAAA), 0);

        // 2. Фактические значения (Никогда не подсвечиваются фоном)
        lv_obj_set_style_bg_opa(sht_labels_data[r][0], LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(sht_labels_data[r][0], lv_color_hex(0xAAAAAA), 0);

        // 3. Корректировка Влажности (Оранжевый фон, если курсор на ней)
        bool is_editing_h = (is_sel_row && edit_mode == 0);
        lv_obj_set_style_bg_color(sht_labels_data[r][1], lv_color_hex(0xFF6600), 0);
        lv_obj_set_style_bg_opa(sht_labels_data[r][1], is_editing_h ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(sht_labels_data[r][1], is_editing_h ? lv_color_hex(0xFFFFFF) : lv_color_hex(0xAAAAAA), 0);

        // 4. Корректировка Температуры (Оранжевый фон, если курсор на ней)
        bool is_editing_t = (is_sel_row && edit_mode == 1);
        lv_obj_set_style_bg_color(sht_labels_data[r][2], lv_color_hex(0xFF6600), 0);
        lv_obj_set_style_bg_opa(sht_labels_data[r][2], is_editing_t ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(sht_labels_data[r][2], is_editing_t ? lv_color_hex(0xFFFFFF) : lv_color_hex(0xAAAAAA), 0);
    }
}

void ui_screens_show_pass_inst(bool is_not_set) {
    lv_obj_add_flag(cont_splash, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_main, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_edit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_sht, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_pass_input, LV_OBJ_FLAG_HIDDEN);

    if (is_not_set) {
        lv_label_set_text(lbl_pass_inst_status, "Пароль не установлен");
        lv_obj_set_style_text_color(lbl_pass_inst_status, lv_color_hex(0xFF5555), 0); // Красный
    } else {
        lv_label_set_text(lbl_pass_inst_status, "Пароль установлен");
        lv_obj_set_style_text_color(lbl_pass_inst_status, lv_color_hex(0x00FF00), 0); // Зеленый
    }

    lv_obj_remove_flag(cont_pass_inst, LV_OBJ_FLAG_HIDDEN);
}

void ui_screens_show_pass_input(const char* title, const char* gestures_str) {
    lv_obj_add_flag(cont_pass_inst, LV_OBJ_FLAG_HIDDEN);
    
    // Обновляем заголовок и введенные жесты
    lv_label_set_text(lbl_pass_title, title);
    lv_label_set_text(lbl_pass_input_val, gestures_str);
    
    lv_obj_remove_flag(cont_pass_input, LV_OBJ_FLAG_HIDDEN);
}