/**
 * @file menu_engine.c
 * @brief Движок древовидного меню, State Machine и обработка жестов HMI.
 */

#include "menu_engine.h"
#include "ui_screens.h"
#include "settings_manager.h"
#include "display_manager.h"
#include "climate_control.h"
#include "audio_analyzer.h"
#include "sht40_driver.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h>
#include "pwm_manager.h"
#include "display_manager.h"

static const char __attribute__((unused)) *TAG = "MENU_ENGINE";

extern display_handle_t g_display_handle;

// =========================================================================
// 1. ТИПЫ УЗЛОВ И ИДЕНТИФИКАТОРЫ (NODE DB)
// =========================================================================
typedef enum {
    NODE_FOLDER = 0,
    NODE_ACTION,
    NODE_EDIT_INT,
    NODE_EDIT_ENUM,
    NODE_EDIT_TOGGLE
} menu_node_type_t;

typedef enum {
    M_ROOT = 0,
    M_CLIMATE, M_ACOUSTIC_TEST, M_SETTINGS, M_PASSWORD, M_FACTORY_RESET,
    
    M_CLIM_AUTO, M_CLIM_MANUAL, M_CLIM_GUITAR, M_CLIM_SYS,
    M_GUITAR_LIST, M_GUITAR_WEIGHT_MAN, M_GUITAR_WEIGHT_AUTO, 
    M_SYS_DEADZONE, M_SYS_MIN_CHANGE, M_SYS_MAX_TIME, M_SYS_COOLDOWN, 
    M_SYS_MAX_SAFE, M_SYS_RES_DIFF, M_SYS_HYSTERESIS, M_SYS_RES_LOW, M_SYS_RES_EMPTY,

    M_SET_SCREEN,          
    M_SET_SHT_SENSORS, 
    M_SET_SOUNDS, 
    M_SET_BTN_HOLD, 
    M_SET_TOUCH_ROT, 
    M_SET_WATER_HEATER,

    M_SCR_BRIGHTNESS, 
    M_SCR_TIMEOUT, 
    M_SCR_ROTATION, 
    M_SCR_MENU_EXIT, 

    M_SND_DOOR, M_SND_RES, 
    M_HEAT_TOGGLE, M_HEAT_MAX_TEMP, 

    M_PASS_TOGGLE, 
    M_PASS_LOCK_TIME,      
    M_PASS_SET, 
    M_PASS_RESET,

    M_NODE_COUNT 
} menu_node_id_t;

typedef struct {
    menu_node_id_t id;
    menu_node_id_t parent_id;
    menu_node_type_t type;
    const char* title;
} menu_node_t;

static const menu_node_t menu_db[M_NODE_COUNT] = {
    {M_ROOT,               M_ROOT,          NODE_FOLDER,      "Главное меню"},
    {M_CLIMATE,            M_ROOT,          NODE_FOLDER,      "1. Климат"},
    {M_ACOUSTIC_TEST,      M_ROOT,          NODE_ACTION,      "2. Акустич. тест"},
    {M_SETTINGS,           M_ROOT,          NODE_FOLDER,      "3. Настройки"},
    {M_PASSWORD,           M_ROOT,          NODE_FOLDER,      "4. Пароль"},
    {M_FACTORY_RESET,      M_ROOT,          NODE_ACTION,      "5. Сброс настроек"},

    {M_CLIM_AUTO,          M_CLIMATE,       NODE_ACTION,      "Авто-режим"},
    {M_CLIM_MANUAL,        M_CLIMATE,       NODE_EDIT_INT,    "Ручная настр."},
    {M_CLIM_GUITAR,        M_CLIMATE,       NODE_FOLDER,      "Гитара"},
    {M_CLIM_SYS,           M_CLIMATE,       NODE_FOLDER,      "Системные настр."},

    {M_GUITAR_LIST,        M_CLIM_GUITAR,   NODE_ACTION,      "Список гитар"},
    {M_GUITAR_WEIGHT_MAN,  M_CLIM_GUITAR,   NODE_EDIT_INT,    "Указать вес"},
    {M_GUITAR_WEIGHT_AUTO, M_CLIM_GUITAR,   NODE_ACTION,      "Взвесить гитару"},

    {M_SYS_DEADZONE,       M_CLIM_SYS,      NODE_EDIT_INT,    "Мертвая зона"},
    {M_SYS_MIN_CHANGE,     M_CLIM_SYS,      NODE_EDIT_INT,    "Мин. изменение"},
    {M_SYS_MAX_TIME,       M_CLIM_SYS,      NODE_EDIT_INT,    "Макс. время раб."},
    {M_SYS_COOLDOWN,       M_CLIM_SYS,      NODE_EDIT_INT,    "Время отдыха"},
    {M_SYS_MAX_SAFE,       M_CLIM_SYS,      NODE_EDIT_INT,    "Макс. безоп. влаж."},
    {M_SYS_RES_DIFF,       M_CLIM_SYS,      NODE_EDIT_INT,    "Порог разн. ресурс."},
    {M_SYS_HYSTERESIS,     M_CLIM_SYS,      NODE_EDIT_INT,    "Гистерезис влаж."},
    {M_SYS_RES_LOW,        M_CLIM_SYS,      NODE_EDIT_INT,    "Порог мало ресурс."},
    {M_SYS_RES_EMPTY,      M_CLIM_SYS,      NODE_EDIT_INT,    "Порог нет ресурс."},

    {M_SET_SCREEN,         M_SETTINGS,      NODE_FOLDER,      "Экран"},
    {M_SET_SHT_SENSORS,    M_SETTINGS,      NODE_ACTION,      "Датчики SHT"},
    {M_SET_SOUNDS,         M_SETTINGS,      NODE_FOLDER,      "Звуки"},
    {M_SET_BTN_HOLD,       M_SETTINGS,      NODE_EDIT_INT,    "Удержание замка"},
    {M_SET_TOUCH_ROT,      M_SETTINGS,      NODE_EDIT_ENUM,   "Ориентация сенсора"},
    {M_SET_WATER_HEATER,   M_SETTINGS,      NODE_FOLDER,      "Подогрев воды"},

    {M_SCR_BRIGHTNESS,     M_SET_SCREEN,    NODE_EDIT_ENUM,   "Яркость экрана"},
    {M_SCR_TIMEOUT,        M_SET_SCREEN,    NODE_EDIT_ENUM,   "Отключение экрана"},
    {M_SCR_ROTATION,       M_SET_SCREEN,    NODE_EDIT_ENUM,   "Положение экрана"},
    {M_SCR_MENU_EXIT,      M_SET_SCREEN,    NODE_EDIT_ENUM,   "Выход из меню"},

    {M_SND_DOOR,           M_SET_SOUNDS,      NODE_EDIT_TOGGLE, "Дверь"},
    {M_SND_RES,            M_SET_SOUNDS,      NODE_EDIT_TOGGLE, "Ресурсы"},
    {M_HEAT_TOGGLE,        M_SET_WATER_HEATER, NODE_EDIT_TOGGLE, "Подогрев"},
    {M_HEAT_MAX_TEMP,      M_SET_WATER_HEATER, NODE_EDIT_INT,    "Макс. темп."},

    {M_PASS_TOGGLE,        M_PASSWORD,      NODE_EDIT_TOGGLE, "Пароль Вкл/Откл"},
    {M_PASS_LOCK_TIME,     M_PASSWORD,      NODE_EDIT_ENUM,   "Время блокировки"},
    {M_PASS_SET,           M_PASSWORD,      NODE_ACTION,      "Установить пароль"},
    {M_PASS_RESET,         M_PASSWORD,      NODE_ACTION,      "Сброс пароля"}
};

// =========================================================================
// 2. СОСТОЯНИЕ ДВИЖКА (STATE MACHINE)
// =========================================================================
typedef enum {
    STATE_SPLASH_SCREEN = -1,
    STATE_MAIN_SCREEN = 0,
    STATE_IN_MENU,
    STATE_EDITING_VALUE,
    STATE_SHT_TABLE,
    STATE_PASS_INSTRUCT,
    STATE_PASS_INPUT,
    STATE_PASS_CHECK     // <--- ДОБАВЛЕНО СОСТОЯНИЕ ПРОВЕРКИ ПАРОЛЯ
} engine_state_t;

static engine_state_t current_state = STATE_SPLASH_SCREEN;
static menu_node_id_t current_folder_id = M_ROOT;
static int cursor_idx = 0;
static int scroll_offset = 0;
static int edit_value = 0;

static bool is_system_locked = false; // Флаг блокировки экрана

// Данные для таблицы SHT
static int sht_cursor_idx = 0; 
static int sht_temp_adj[4] = {0};
static int sht_hum_adj[4] = {0};

// Данные для ввода пароля
static int temp_pass[5];
static int temp_pass_len = 0;

static const uint8_t brightness_opts[] = {10, 30, 50, 70, 100};
static const char* enum_time_opts[] = {"30 сек", "1 мин", "2 мин", "5 мин", "ОТКЛ"};
static const char* enum_menu_opts[] = {"15 сек", "30 сек", "1 мин", "2 мин", "ОТКЛ"};
static const char* enum_screen_opts[] = {"30 сек", "1 мин", "5 мин", "10 мин", "ОТКЛ"};
static const char* enum_rotation_opts[] = {"0°", "90°", "180°", "270°"};
static const char* enum_touch_opts[] = {"Норма", "Инверсия"};

// =========================================================================
// 3. ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// =========================================================================

bool menu_engine_is_locked(void) {
    return is_system_locked;
}

static void load_edit_value(menu_node_id_t id) {
    const cabinet_settings_t* cfg = settings_get_readonly();
    
    switch (id) {
        case M_CLIM_MANUAL:     edit_value = cfg->target_humidity; break;
        case M_SYS_DEADZONE:    edit_value = (int)cfg->dead_zone_percent; break;
        case M_SYS_MIN_CHANGE:  edit_value = (int)cfg->min_humidity_change_for_timeout; break;
        case M_SYS_MAX_TIME:    edit_value = cfg->max_operation_duration_ms / 60000; break; 
        case M_SYS_COOLDOWN:    edit_value = cfg->operation_cooldown_ms / 60000; break;
        case M_SYS_MAX_SAFE:    edit_value = (int)cfg->max_safe_humidity; break;
        case M_SYS_RES_DIFF:    edit_value = (int)cfg->resource_check_diff; break;
        case M_SYS_HYSTERESIS:  edit_value = (int)cfg->humidity_hysteresis; break;
        case M_SYS_RES_LOW:     edit_value = cfg->resource_low_fault_threshold; break;
        case M_SYS_RES_EMPTY:   edit_value = cfg->resource_empty_fault_threshold; break;
        case M_SCR_BRIGHTNESS:  edit_value = cfg->screen_brightness_idx; break;
        case M_SCR_TIMEOUT:     edit_value = cfg->screen_timeout_index; break;
        case M_SCR_ROTATION:    edit_value = cfg->screen_rotation_index; break;
        case M_SCR_MENU_EXIT:   edit_value = cfg->menu_timeout_index; break;
        case M_SET_BTN_HOLD:    edit_value = cfg->lock_hold_time_ms / 1000; break;
        case M_SET_TOUCH_ROT:   edit_value = cfg->touch_rotation_index; break;
        case M_SND_DOOR:        edit_value = cfg->door_sound_enabled ? 1 : 0; break;
        case M_SND_RES:         edit_value = cfg->water_silica_sound_enabled ? 1 : 0; break;
        case M_HEAT_TOGGLE:     edit_value = cfg->water_heater_enabled ? 1 : 0; break;
        case M_HEAT_MAX_TEMP:   edit_value = cfg->water_heater_max_temp; break;
        case M_PASS_TOGGLE:     edit_value = cfg->password_enabled ? 1 : 0; break;
        case M_PASS_LOCK_TIME:  edit_value = cfg->lock_time_index; break;
        default: edit_value = 0; break;
    }
}

static void save_edit_value(menu_node_id_t id) {
    settings_lock();
    cabinet_settings_t* cfg = (cabinet_settings_t*)settings_get_readonly();
    
    switch (id) {
        case M_CLIM_MANUAL:     cfg->target_humidity = edit_value; break;
        case M_SYS_DEADZONE:    cfg->dead_zone_percent = (float)edit_value; break;
        case M_SYS_MIN_CHANGE:  cfg->min_humidity_change_for_timeout = (float)edit_value; break;
        case M_SYS_MAX_TIME:    cfg->max_operation_duration_ms = edit_value * 60000; break;
        case M_SYS_COOLDOWN:    cfg->operation_cooldown_ms = edit_value * 60000; break;
        case M_SYS_MAX_SAFE:    cfg->max_safe_humidity = (float)edit_value; break;
        case M_SYS_RES_DIFF:    cfg->resource_check_diff = (float)edit_value; break;
        case M_SYS_HYSTERESIS:  cfg->humidity_hysteresis = (float)edit_value; break;
        case M_SYS_RES_LOW:     cfg->resource_low_fault_threshold = edit_value; break;
        case M_SYS_RES_EMPTY:   cfg->resource_empty_fault_threshold = edit_value; break;
        case M_SCR_BRIGHTNESS:  
            cfg->screen_brightness_idx = edit_value; 
            if (g_display_handle) display_manager_set_brightness(g_display_handle, brightness_opts[edit_value]);
            break;
        case M_SCR_TIMEOUT:     cfg->screen_timeout_index = edit_value; break;
        case M_SCR_ROTATION:    
            cfg->screen_rotation_index = edit_value; 
            if (g_display_handle) display_manager_set_rotation(g_display_handle, edit_value);
            break;
        case M_SCR_MENU_EXIT:   cfg->menu_timeout_index = edit_value; break;
        case M_SET_BTN_HOLD:    cfg->lock_hold_time_ms = edit_value * 1000; break;
        case M_SET_TOUCH_ROT:   cfg->touch_rotation_index = edit_value; break;
        case M_SND_DOOR:        cfg->door_sound_enabled = (edit_value > 0); break;
        case M_SND_RES:         cfg->water_silica_sound_enabled = (edit_value > 0); break;
        case M_HEAT_TOGGLE:     cfg->water_heater_enabled = (edit_value > 0); break;
        case M_HEAT_MAX_TEMP:   cfg->water_heater_max_temp = edit_value; break;
        case M_PASS_TOGGLE:     cfg->password_enabled = (edit_value > 0); break;
        case M_PASS_LOCK_TIME:  cfg->lock_time_index = edit_value; break;
        default: break;
    }
    settings_unlock();
    settings_save(); 
}

static void format_edit_text(menu_node_id_t id, char* buf, size_t max_len) {
    switch (id) {
        case M_CLIM_MANUAL:
        case M_SYS_MAX_SAFE:
        case M_SYS_HYSTERESIS:
        case M_SYS_DEADZONE:    snprintf(buf, max_len, "%d%%", edit_value); break;
        case M_SYS_MAX_TIME:
        case M_SYS_COOLDOWN:    snprintf(buf, max_len, "%d мин", edit_value); break;
        case M_SCR_BRIGHTNESS:  snprintf(buf, max_len, "%d%%", brightness_opts[edit_value]); break;
        case M_SCR_TIMEOUT:     snprintf(buf, max_len, "%s", enum_screen_opts[edit_value]); break;
        case M_SCR_ROTATION:    snprintf(buf, max_len, "%s", enum_rotation_opts[edit_value]); break;
        case M_SCR_MENU_EXIT:   snprintf(buf, max_len, "%s", enum_menu_opts[edit_value]); break;
        case M_SET_TOUCH_ROT:   snprintf(buf, max_len, "%s", enum_touch_opts[edit_value]); break;
        case M_SET_BTN_HOLD:    snprintf(buf, max_len, "%d сек", edit_value); break;
        case M_SND_DOOR:
        case M_SND_RES:
        case M_HEAT_TOGGLE:     
        case M_PASS_TOGGLE:     snprintf(buf, max_len, "%s", edit_value ? "ВКЛ" : "ОТКЛ"); break;
        case M_HEAT_MAX_TEMP:   snprintf(buf, max_len, "%d°C", edit_value); break;
        case M_PASS_LOCK_TIME:  snprintf(buf, max_len, "%s", enum_time_opts[edit_value]); break;
        default:                snprintf(buf, max_len, "%d", edit_value); break;
    }
}

static void enforce_edit_limits_circular(menu_node_id_t id, int direction) {
    int min = 0, max = 100;
    switch (id) {
        case M_CLIM_MANUAL:     min = 30; max = 70; break;
        case M_SYS_DEADZONE:    min = 1; max = 10; break;
        case M_SYS_MIN_CHANGE:  min = 1; max = 10; break;
        case M_SYS_MAX_TIME:    min = 1; max = 120; break;
        case M_SYS_COOLDOWN:    min = 1; max = 60; break;
        case M_SYS_MAX_SAFE:    min = 50; max = 80; break;
        case M_SYS_RES_DIFF:    min = 1; max = 20; break;
        case M_SYS_HYSTERESIS:  min = 1; max = 10; break;
        case M_SYS_RES_LOW:     min = 1; max = 10; break;
        case M_SYS_RES_EMPTY:   min = 1; max = 10; break;
        case M_SCR_BRIGHTNESS:  min = 0; max = 4; break;
        case M_SCR_TIMEOUT:     min = 0; max = 4; break;
        case M_SCR_ROTATION:    min = 0; max = 3; break;
        case M_SCR_MENU_EXIT:   min = 0; max = 4; break;
        case M_SET_BTN_HOLD:    min = 1; max = 5; break;
        case M_SET_TOUCH_ROT:   min = 0; max = 1; break;
        case M_SND_DOOR:
        case M_SND_RES:
        case M_HEAT_TOGGLE:     
        case M_PASS_TOGGLE:     min = 0; max = 1; break;
        case M_HEAT_MAX_TEMP:   min = 30; max = 85; break;
        case M_PASS_LOCK_TIME:  min = 0; max = 4; break;
        default: break;
    }
    edit_value += direction;
    if (edit_value > max) edit_value = min;
    else if (edit_value < min) edit_value = max;
}

static void update_sht_view(void) {
    cabinet_climate_data_t clim = {0};
    climate_get_latest_data(&clim);

    int row = sht_cursor_idx / 2;
    int col = sht_cursor_idx % 2; 

    ui_screens_update_sht_table(
        row, col,
        clim.sensors[0].temperature, clim.sensors[0].humidity, sht_temp_adj[0], sht_hum_adj[0],
        clim.sensors[1].temperature, clim.sensors[1].humidity, sht_temp_adj[1], sht_hum_adj[1],
        clim.sensors[2].temperature, clim.sensors[2].humidity, sht_temp_adj[2], sht_hum_adj[2],
        clim.sensors[3].temperature, clim.sensors[3].humidity, sht_temp_adj[3], sht_hum_adj[3]
    );
}

static void render_password_string(char* buf, size_t max_len) {
    buf[0] = '\0';
    for(int i = 0; i < temp_pass_len; i++) {
        switch(temp_pass[i]) {
            case EVENT_SWIPE_UP:    strlcat(buf, "^ ", max_len); break;
            case EVENT_SWIPE_DOWN:  strlcat(buf, "v ", max_len); break;
            case EVENT_SWIPE_LEFT:  strlcat(buf, "< ", max_len); break;
            case EVENT_SWIPE_RIGHT: strlcat(buf, "> ", max_len); break;
            default: break;
        }
    }
}

// =========================================================================
// 4. ЛОГИКА ОТОБРАЖЕНИЯ И НАВИГАЦИИ
// =========================================================================
static int get_children_nodes(menu_node_id_t parent, const menu_node_t* out_nodes[15]) {
    int count = 0;
    for (int i = 0; i < M_NODE_COUNT; i++) {
        if (menu_db[i].parent_id == parent && menu_db[i].id != parent) {
            out_nodes[count++] = &menu_db[i];
            if (count >= 15) break; 
        }
    }
    return count;
}

static void update_view(void) {
    const menu_node_t* children[15];
    int total = get_children_nodes(current_folder_id, children);

    if (cursor_idx < scroll_offset) scroll_offset = cursor_idx;
    else if (cursor_idx >= scroll_offset + 5) scroll_offset = cursor_idx - 4;

    const char* titles_to_render[5] = {NULL};
    int visible_count = 0;
    
    for (int i = 0; i < 5; i++) {
        int item_idx = scroll_offset + i;
        if (item_idx < total) {
            titles_to_render[i] = children[item_idx]->title;
            visible_count++;
        }
    }
    ui_screens_render_menu(titles_to_render, visible_count, cursor_idx - scroll_offset);
}

void menu_engine_init(void) {
    current_state = STATE_SPLASH_SCREEN;
    current_folder_id = M_ROOT;
    cursor_idx = 0;
    scroll_offset = 0;
}

void menu_engine_boot_complete(void) {
    current_state = STATE_MAIN_SCREEN;
    const cabinet_settings_t* cfg = settings_get_readonly();
    is_system_locked = cfg->password_enabled; // Применяем защиту на старте
    ui_screens_show_main();
}

bool menu_engine_is_on_main_screen(void) {
    return (current_state == STATE_MAIN_SCREEN);
}

void menu_engine_force_main_screen(void) {
    if (current_state != STATE_MAIN_SCREEN) {
        current_state = STATE_MAIN_SCREEN;
        current_folder_id = M_ROOT;
        cursor_idx = 0;
        scroll_offset = 0;
        ui_screens_show_main();
    }
}

extern display_handle_t g_display_handle;

static void hmi_router_task(void *pvParameters) {
    hmi_msg_t msg;
    ESP_LOGI(TAG, "HMI Router Task started on Core 0");

    while (1) {
        if (xQueueReceive(hmi_event_queue, &msg, portMAX_DELAY) == pdTRUE) {
            
            // 1. Аппаратный сброс пароля (обрабатываем напрямую)
            if (msg.type == EVENT_HARDWARE_PASS_RESET) {
                menu_engine_process_gesture(msg.type);
                continue;
            }

            // 2. СИСТЕМА БЕЗОПАСНОСТИ: ОТКРЫТИЕ ЗАМКА
            if (msg.type == EVENT_FIFTH_BTN_HOLD) {
                if (menu_engine_is_on_main_screen()) {
                    
                    if (door_sensor_is_open() || !pwm_is_door_safe_to_unlock()) {
                        ESP_LOGW(TAG, "Unlock ignored: Door open or debounce active.");
                        continue;
                    }

                    ESP_LOGI(TAG, "DOOR UNLOCK SEQUENCE STARTED");
                    
                    gesture_set_panel_enabled(false);
                    
                    // Включаем мигание замка в UI
                    if (g_display_handle) display_manager_set_lock_blink(g_display_handle, true);
                    
                    // Открываем замок
                    pwm_set_door_lock(false);
                    
                    uint32_t hold_time = 1000;
                    settings_get_lock_hold_time(&hold_time);
                    
                    vTaskDelay(pdMS_TO_TICKS(hold_time));
                    
                    // Закрываем замок
                    pwm_set_door_lock(true);
                    
                    // Выключаем мигание
                    if (g_display_handle) display_manager_set_lock_blink(g_display_handle, false);
                    
                    vTaskDelay(pdMS_TO_TICKS(200));
                    gesture_set_panel_enabled(true);
                }
                continue; // Жест удержания не передаем в UI
            }
            
            // 3. Обычные жесты передаем в UI
            if (g_display_handle != NULL) {
                display_manager_process_gesture(g_display_handle, msg.type);
            }
        }
    }
}

void menu_engine_start_router(void) {
    xTaskCreatePinnedToCore(hmi_router_task, "hmi_router", 4096, NULL, 5, NULL, 0);
}
// =========================================================================
// 5. ОБРАБОТЧИК ЖЕСТОВ
// =========================================================================
esp_err_t menu_engine_process_gesture(hmi_event_type_t event) {
    if (event == EVENT_SYSTEM_IDLE_TIMEOUT) {
        if (!menu_engine_is_on_main_screen()) menu_engine_force_main_screen();
        return ESP_OK;
    }

     // --- АППАРАТНЫЙ СБРОС ПАРОЛЯ ---
    if (event == EVENT_HARDWARE_PASS_RESET) {
        ESP_LOGW(TAG, "Hardware password reset executed!");
        settings_lock();
        cabinet_settings_t* cfg = (cabinet_settings_t*)settings_get_readonly();
        cfg->password_len = 0;
        cfg->password_enabled = false;
        is_system_locked = false; // Сразу снимаем блокировку
        settings_unlock();
        settings_save();

        ui_screens_show_popup("Пароль сброшен");
        
        if (!menu_engine_is_on_main_screen()) {
            menu_engine_force_main_screen();
        }
        return ESP_OK;
    }

    if (event == EVENT_SYSTEM_LOCK) {
        is_system_locked = true;
        if (!menu_engine_is_on_main_screen()) menu_engine_force_main_screen();
        return ESP_OK;
    }

    if (current_state == STATE_SPLASH_SCREEN) return ESP_OK; 

    // =========================================================
    // ГЛАВНЫЙ ЭКРАН И МЕНЮ
    // =========================================================
    if (current_state == STATE_MAIN_SCREEN) {
        if (event == EVENT_SWIPE_RIGHT || event == EVENT_TAP) {
            if (is_system_locked) {
                current_state = STATE_PASS_CHECK;
                temp_pass_len = 0;
                ui_screens_show_pass_input("Введите пароль", "");
            } else {
                current_state = STATE_IN_MENU;
                current_folder_id = M_ROOT;
                cursor_idx = 0;
                scroll_offset = 0;
                ui_screens_show_menu();
                update_view();
            }
        }
    }
    else if (current_state == STATE_IN_MENU) {
        const menu_node_t* children[15];
        int count = get_children_nodes(current_folder_id, children);
        if (count == 0) return ESP_OK;

        if (event == EVENT_SWIPE_DOWN) {
            cursor_idx = (cursor_idx + 1) % count;
        }
        else if (event == EVENT_SWIPE_UP) { 
            cursor_idx--; 
            if (cursor_idx < 0) cursor_idx = count - 1; 
        }
        else if (event == EVENT_SWIPE_LEFT) {
            if (current_folder_id == M_ROOT) {
                current_state = STATE_MAIN_SCREEN;
                ui_screens_show_main();
            } else {
                current_folder_id = menu_db[current_folder_id].parent_id;
                cursor_idx = 0;
                scroll_offset = 0;
                update_view();
            }
        }
        else if (event == EVENT_SWIPE_RIGHT || event == EVENT_TAP) {
            const menu_node_t* selected = children[cursor_idx];
            
            if (selected->type == NODE_FOLDER) {
                current_folder_id = selected->id;
                cursor_idx = 0;
                scroll_offset = 0;
                update_view();
            }
            else if (selected->type == NODE_EDIT_INT || selected->type == NODE_EDIT_ENUM || selected->type == NODE_EDIT_TOGGLE) {
                load_edit_value(selected->id);
                current_state = STATE_EDITING_VALUE;
                
                char val_buf[32];
                format_edit_text(selected->id, val_buf, sizeof(val_buf));
                ui_screens_show_edit(selected->title, val_buf);
            }
            else if (selected->type == NODE_ACTION) {
                if (selected->id == M_FACTORY_RESET) {
                    settings_reset_to_defaults();
                    esp_restart();
                } 
                else if (selected->id == M_ACOUSTIC_TEST) {
                    float freq = 0;
                    audio_analyze_resonance(&freq);
                }
                else if (selected->id == M_SET_SHT_SENSORS) {
                    current_state = STATE_SHT_TABLE;
                    sht_cursor_idx = 0;
                    const cabinet_settings_t* cfg = settings_get_readonly();
                    for(int i=0; i<4; i++) {
                        sht_temp_adj[i] = cfg->sht_temp_adj[i];
                        sht_hum_adj[i] = cfg->sht_hum_adj[i];
                    }
                    ui_screens_show_sht_table();
                    update_sht_view();
                } 
                else if (selected->id == M_PASS_SET) {
                    current_state = STATE_PASS_INSTRUCT;
                    const cabinet_settings_t* cfg = settings_get_readonly();
                    bool is_not_set = (cfg->password_len == 0);
                    ui_screens_show_pass_inst(is_not_set);
                }
                else if (selected->id == M_PASS_RESET) {
                    settings_lock();
                    cabinet_settings_t* cfg = (cabinet_settings_t*)settings_get_readonly();
                    cfg->password_len = 0;
                    cfg->password_enabled = false;
                    is_system_locked = false;
                    settings_unlock();
                    settings_save();
                    ui_screens_show_popup("Пароль сброшен");
                }
            }
        }
        if (current_state == STATE_IN_MENU) update_view();
    }
    
    // =========================================================
    // РЕДАКТИРОВАНИЕ ОБЫЧНЫХ ЗНАЧЕНИЙ
    // =========================================================
    else if (current_state == STATE_EDITING_VALUE) {
        const menu_node_t* children[15];
        get_children_nodes(current_folder_id, children);
        menu_node_id_t active_id = children[cursor_idx]->id;

        if (event == EVENT_SWIPE_UP || event == EVENT_SWIPE_DOWN) {
            int dir = (event == EVENT_SWIPE_UP) ? 1 : -1;
            enforce_edit_limits_circular(active_id, dir);
            
            char val_buf[32];
            format_edit_text(active_id, val_buf, sizeof(val_buf));
            ui_screens_update_edit_value(val_buf);
        }
        else if (event == EVENT_SWIPE_LEFT) {
            current_state = STATE_IN_MENU; 
            ui_screens_show_menu();
            update_view();
        }
        else if (event == EVENT_SWIPE_RIGHT || event == EVENT_TAP) {
            save_edit_value(active_id);    
            ui_screens_show_popup("Сохранено");
            
            current_state = STATE_IN_MENU;
            ui_screens_show_menu();
            update_view();
        }
    }
    
    // =========================================================
    // ТАБЛИЦА SHT
    // =========================================================
    else if (current_state == STATE_SHT_TABLE) {
        int row = sht_cursor_idx / 2;
        int col = sht_cursor_idx % 2;

        if (event == EVENT_FIFTH_BTN_PRESS) {
            sht_cursor_idx = (sht_cursor_idx + 1) % 8;
            update_sht_view();
        }
        else if (event == EVENT_SWIPE_UP) {
            if (col == 0) sht_hum_adj[row]++;
            else sht_temp_adj[row]++;
            update_sht_view();
        }
        else if (event == EVENT_SWIPE_DOWN) {
            if (col == 0) sht_hum_adj[row]--;
            else sht_temp_adj[row]--;
            update_sht_view();
        }
        else if (event == EVENT_SWIPE_LEFT) {
            current_state = STATE_IN_MENU; 
            ui_screens_show_menu(); 
            update_view();
        }
        else if (event == EVENT_SWIPE_RIGHT) {
            settings_lock();
            cabinet_settings_t* cfg = (cabinet_settings_t*)settings_get_readonly();
            for(int i=0; i<4; i++) {
                cfg->sht_hum_adj[i] = sht_hum_adj[i];
                cfg->sht_temp_adj[i] = sht_temp_adj[i];
            }
            settings_unlock();
            settings_save();
            
            ui_screens_show_popup("Сохранено");
            current_state = STATE_IN_MENU; 
            ui_screens_show_menu();
            update_view();
        }
    }

    // =========================================================
    // ПАРОЛЬ (ИНСТРУКЦИЯ И УСТАНОВКА)
    // =========================================================
    else if (current_state == STATE_PASS_INSTRUCT) {
        if (event == EVENT_SWIPE_RIGHT) {
            current_state = STATE_PASS_INPUT;
            temp_pass_len = 0;
            ui_screens_show_pass_input("Введите новый пароль", "");
        } else if (event == EVENT_SWIPE_LEFT) {
            current_state = STATE_IN_MENU;
            ui_screens_show_menu();
            update_view();
        }
    }
    else if (current_state == STATE_PASS_INPUT) {
        if (event == EVENT_SWIPE_UP || event == EVENT_SWIPE_DOWN || event == EVENT_SWIPE_LEFT || event == EVENT_SWIPE_RIGHT) {
            if (temp_pass_len < 5) {
                temp_pass[temp_pass_len++] = event;
                char buf[32];
                render_password_string(buf, sizeof(buf));
                ui_screens_show_pass_input("Введите новый пароль", buf);
            }
        } 
        else if (event == EVENT_FIFTH_BTN_PRESS) { 
            if (temp_pass_len > 0) {
                settings_lock();
                cabinet_settings_t* cfg = (cabinet_settings_t*)settings_get_readonly();
                cfg->password_len = temp_pass_len;
                for(int i=0; i<temp_pass_len; i++) cfg->password[i] = temp_pass[i];
                cfg->password_enabled = true;
                settings_unlock();
                settings_save();
                ui_screens_show_popup("Сохранено");
            }
            current_state = STATE_IN_MENU;
            ui_screens_show_menu();
            update_view();
        } 
        else if (event == EVENT_SWIPE_LEFT && temp_pass_len == 0) {
           current_state = STATE_IN_MENU;
           ui_screens_show_menu();
           update_view();
        }
    }
    
    // =========================================================
    // ПРОВЕРКА ПАРОЛЯ ПРИ ВХОДЕ В МЕНЮ
    // =========================================================
    else if (current_state == STATE_PASS_CHECK) {
        if (event == EVENT_SWIPE_UP || event == EVENT_SWIPE_DOWN || event == EVENT_SWIPE_LEFT || event == EVENT_SWIPE_RIGHT) {
            if (temp_pass_len < 5) {
                temp_pass[temp_pass_len++] = event;
                char buf[32];
                render_password_string(buf, sizeof(buf));
                ui_screens_show_pass_input("Введите пароль", buf);
            }
        }
        else if (event == EVENT_FIFTH_BTN_PRESS) {
            bool is_match = false;
            const cabinet_settings_t* cfg = settings_get_readonly();
            if (temp_pass_len == cfg->password_len && temp_pass_len > 0) {
                is_match = true;
                for(int i = 0; i < temp_pass_len; i++) {
                    if (temp_pass[i] != cfg->password[i]) {
                        is_match = false;
                        break;
                    }
                }
            }

            if (is_match) {
                is_system_locked = false; 
                ui_screens_show_popup("Доступ разрешен");
                current_state = STATE_IN_MENU;
                current_folder_id = M_ROOT;
                cursor_idx = 0; scroll_offset = 0;
                ui_screens_show_menu();
                update_view();
            } else {
                ui_screens_show_popup("Неверный пароль");
                temp_pass_len = 0;
                ui_screens_show_pass_input("Введите пароль", "");
            }
        }
        else if (event == EVENT_SWIPE_LEFT && temp_pass_len == 0) {
            current_state = STATE_MAIN_SCREEN;
            ui_screens_show_main();
        }
    }

    return ESP_OK;
}