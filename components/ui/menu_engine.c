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
#include "esp_log.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "MENU_ENGINE";

// Ссылка на менеджер дисплея для поворота экрана "на лету"
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
    
    // --- Климат ---
    M_CLIM_AUTO, M_CLIM_MANUAL, M_CLIM_GUITAR, M_CLIM_SYS,
    M_GUITAR_LIST, M_GUITAR_WEIGHT_MAN, M_GUITAR_WEIGHT_AUTO, 
    M_SYS_DEADZONE, M_SYS_MIN_CHANGE, M_SYS_MAX_TIME, M_SYS_COOLDOWN, 
    M_SYS_MAX_SAFE, M_SYS_RES_DIFF, M_SYS_HYSTERESIS, M_SYS_RES_LOW, M_SYS_RES_EMPTY,

    // --- Настройки ---
    M_SET_LOCK_TIME, M_SET_MENU_TIME, M_SET_SCREEN_TIME, M_SET_BTN_HOLD, 
    M_SET_ROTATION, M_SET_TOUCH_ROT, // <-- Положение экрана и сенсора
    M_SET_SHT_SENSORS, M_SET_SOUNDS, M_SET_WATER_HEATER,
    
    M_SHT_ADJUST, 
    M_SND_DOOR, M_SND_RES, 
    M_HEAT_TOGGLE, M_HEAT_MAX_TEMP, 

    // --- Пароль ---
    M_PASS_TOGGLE, M_PASS_SET, M_PASS_RESET,

    M_NODE_COUNT 
} menu_node_id_t;

typedef struct {
    menu_node_id_t id;
    menu_node_id_t parent_id;
    menu_node_type_t type;
    const char* title;
} menu_node_t;

// Дерево меню (плоский массив, связи через parent_id)
static const menu_node_t menu_db[M_NODE_COUNT] = {
    {M_ROOT,               M_ROOT,          NODE_FOLDER,      "Главное меню"},
    
    {M_CLIMATE,            M_ROOT,          NODE_FOLDER,      "1. Климат"},
    {M_ACOUSTIC_TEST,      M_ROOT,          NODE_ACTION,      "2. Акустич. тест"},
    {M_SETTINGS,           M_ROOT,          NODE_FOLDER,      "3. Настройки"},
    {M_PASSWORD,           M_ROOT,          NODE_FOLDER,      "4. Пароль"},
    {M_FACTORY_RESET,      M_ROOT,          NODE_ACTION,      "5. Сброс настроек"},

    // Климат
    {M_CLIM_AUTO,          M_CLIMATE,       NODE_ACTION,      "Авто-режим"},
    {M_CLIM_MANUAL,        M_CLIMATE,       NODE_EDIT_INT,    "Ручная настр."},
    {M_CLIM_GUITAR,        M_CLIMATE,       NODE_FOLDER,      "Гитара"},
    {M_CLIM_SYS,           M_CLIMATE,       NODE_FOLDER,      "Системные настр."},

    // Гитара
    {M_GUITAR_LIST,        M_CLIM_GUITAR,   NODE_ACTION,      "Список гитар"},
    {M_GUITAR_WEIGHT_MAN,  M_CLIM_GUITAR,   NODE_EDIT_INT,    "Указать вес"},
    {M_GUITAR_WEIGHT_AUTO, M_CLIM_GUITAR,   NODE_ACTION,      "Взвесить гитару"},

    // Системные настройки климата
    {M_SYS_DEADZONE,       M_CLIM_SYS,      NODE_EDIT_INT,    "Мертвая зона"},
    {M_SYS_MIN_CHANGE,     M_CLIM_SYS,      NODE_EDIT_INT,    "Мин. изменение"},
    {M_SYS_MAX_TIME,       M_CLIM_SYS,      NODE_EDIT_INT,    "Макс. время раб."},
    {M_SYS_COOLDOWN,       M_CLIM_SYS,      NODE_EDIT_INT,    "Время отдыха"},
    {M_SYS_MAX_SAFE,       M_CLIM_SYS,      NODE_EDIT_INT,    "Макс. безоп. влаж."},
    {M_SYS_RES_DIFF,       M_CLIM_SYS,      NODE_EDIT_INT,    "Порог разн. ресурс."},
    {M_SYS_HYSTERESIS,     M_CLIM_SYS,      NODE_EDIT_INT,    "Гистерезис влаж."},
    {M_SYS_RES_LOW,        M_CLIM_SYS,      NODE_EDIT_INT,    "Порог мало ресурс."},
    {M_SYS_RES_EMPTY,      M_CLIM_SYS,      NODE_EDIT_INT,    "Порог нет ресурс."},

    // Настройки системы
    {M_SET_LOCK_TIME,      M_SETTINGS,      NODE_EDIT_ENUM,   "Время блокировки"},
    {M_SET_MENU_TIME,      M_SETTINGS,      NODE_EDIT_ENUM,   "Выход из меню"},
    {M_SET_SCREEN_TIME,    M_SETTINGS,      NODE_EDIT_ENUM,   "Откл. экрана"},
    {M_SET_BTN_HOLD,       M_SETTINGS,      NODE_EDIT_INT,    "Удержание замка"},
    {M_SET_ROTATION,       M_SETTINGS,      NODE_EDIT_ENUM,   "Положение экрана"},
    {M_SET_TOUCH_ROT,      M_SETTINGS,      NODE_EDIT_ENUM,   "Ориентация сенсора"},
    {M_SET_SHT_SENSORS,    M_SETTINGS,      NODE_FOLDER,      "Датчики SHT"},
    {M_SET_SOUNDS,         M_SETTINGS,      NODE_FOLDER,      "Звуки"},
    {M_SET_WATER_HEATER,   M_SETTINGS,      NODE_FOLDER,      "Подогрев воды"},

    // Внутри настроек
    {M_SHT_ADJUST,         M_SET_SHT_SENSORS, NODE_ACTION,    "Корректировка SHT"},
    {M_SND_DOOR,           M_SET_SOUNDS,      NODE_EDIT_TOGGLE, "Дверь"},
    {M_SND_RES,            M_SET_SOUNDS,      NODE_EDIT_TOGGLE, "Ресурсы"},
    {M_HEAT_TOGGLE,        M_SET_WATER_HEATER, NODE_EDIT_TOGGLE, "Подогрев"},
    {M_HEAT_MAX_TEMP,      M_SET_WATER_HEATER, NODE_EDIT_INT,    "Макс. темп."},

    // Пароль
    {M_PASS_TOGGLE,        M_PASSWORD,      NODE_EDIT_TOGGLE, "Пароль Вкл/Откл"},
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
    STATE_EDITING_VALUE
} engine_state_t;

static engine_state_t current_state = STATE_SPLASH_SCREEN;

//static engine_state_t current_state = STATE_MAIN_SCREEN;
static menu_node_id_t current_folder_id = M_ROOT;
static int cursor_idx = 0;
static int scroll_offset = 0;
static int edit_value = 0;

// Текстовые списки для Enum-настроек
static const char* enum_time_opts[] = {"30 сек", "1 мин", "2 мин", "5 мин", "ОТКЛ"};
static const char* enum_menu_opts[] = {"15 сек", "30 сек", "1 мин", "2 мин", "ОТКЛ"};
static const char* enum_screen_opts[] = {"30 сек", "1 мин", "5 мин", "10 мин", "ОТКЛ"};
static const char* enum_rotation_opts[] = {"0°", "90°", "180°", "270°"};
static const char* enum_touch_opts[] = {"Норма", "Инверсия"};

// =========================================================================
// 3. ФУНКЦИИ ЧТЕНИЯ/ЗАПИСИ ЗНАЧЕНИЙ (СВЯЗЬ С settings_manager.h)
// =========================================================================

static void load_edit_value(menu_node_id_t id) {
    settings_lock();
    switch (id) {
        case M_CLIM_MANUAL:     edit_value = sys_settings.targetHumidity; break;
        case M_SYS_DEADZONE:    edit_value = (int)sys_settings.deadZonePercent; break;
        case M_SYS_MIN_CHANGE:  edit_value = (int)sys_settings.minHumidityChangeForTimeout; break;
        case M_SYS_MAX_TIME:    edit_value = sys_settings.maxOperationDuration / 60000; break; 
        case M_SYS_COOLDOWN:    edit_value = sys_settings.operationCooldown / 60000; break;
        case M_SYS_MAX_SAFE:    edit_value = (int)sys_settings.maxSafeHumidity; break;
        case M_SYS_RES_DIFF:    edit_value = (int)sys_settings.resourceCheckDiff; break;
        case M_SYS_HYSTERESIS:  edit_value = (int)sys_settings.humidityHysteresis; break;
        case M_SYS_RES_LOW:     edit_value = sys_settings.resourceLowFaultThreshold; break;
        case M_SYS_RES_EMPTY:   edit_value = sys_settings.resourceEmptyFaultThreshold; break;
        
        case M_SET_LOCK_TIME:   edit_value = sys_settings.lockTimeIndex; break;
        case M_SET_MENU_TIME:   edit_value = sys_settings.menuTimeoutOptionIndex; break;
        case M_SET_SCREEN_TIME: edit_value = sys_settings.screenTimeoutOptionIndex; break;
        case M_SET_BTN_HOLD:    edit_value = sys_settings.lockHoldTime / 1000; break;
        case M_SET_ROTATION:    edit_value = sys_settings.screenRotationIndex; break;
        case M_SET_TOUCH_ROT:   edit_value = sys_settings.touchRotationIndex; break;
        
        case M_SND_DOOR:        edit_value = sys_settings.doorSoundEnabled ? 1 : 0; break;
        case M_SND_RES:         edit_value = sys_settings.waterSilicaSoundEnabled ? 1 : 0; break;
        case M_HEAT_TOGGLE:     edit_value = sys_settings.waterHeaterEnabled ? 1 : 0; break;
        case M_HEAT_MAX_TEMP:   edit_value = sys_settings.waterHeaterMaxTemp; break;
        default: edit_value = 0; break;
    }
    settings_unlock();
}

static void save_edit_value(menu_node_id_t id) {
    settings_lock();
    switch (id) {
        case M_CLIM_MANUAL:     sys_settings.targetHumidity = edit_value; break;
        case M_SYS_DEADZONE:    sys_settings.deadZonePercent = (float)edit_value; break;
        case M_SYS_MIN_CHANGE:  sys_settings.minHumidityChangeForTimeout = (float)edit_value; break;
        case M_SYS_MAX_TIME:    sys_settings.maxOperationDuration = edit_value * 60000; break;
        case M_SYS_COOLDOWN:    sys_settings.operationCooldown = edit_value * 60000; break;
        case M_SYS_MAX_SAFE:    sys_settings.maxSafeHumidity = (float)edit_value; break;
        case M_SYS_RES_DIFF:    sys_settings.resourceCheckDiff = (float)edit_value; break;
        case M_SYS_HYSTERESIS:  sys_settings.humidityHysteresis = (float)edit_value; break;
        case M_SYS_RES_LOW:     sys_settings.resourceLowFaultThreshold = edit_value; break;
        case M_SYS_RES_EMPTY:   sys_settings.resourceEmptyFaultThreshold = edit_value; break;

        case M_SET_LOCK_TIME:   sys_settings.lockTimeIndex = edit_value; break;
        case M_SET_MENU_TIME:   sys_settings.menuTimeoutOptionIndex = edit_value; break;
        case M_SET_SCREEN_TIME: sys_settings.screenTimeoutOptionIndex = edit_value; break;
        case M_SET_BTN_HOLD:    sys_settings.lockHoldTime = edit_value * 1000; break;
        
        case M_SET_ROTATION:    
            sys_settings.screenRotationIndex = edit_value; 
            if (g_display_handle) display_manager_set_rotation(g_display_handle, edit_value);
            break;
            
        case M_SET_TOUCH_ROT:   sys_settings.touchRotationIndex = edit_value; break;

        case M_SND_DOOR:        sys_settings.doorSoundEnabled = (edit_value > 0); break;
        case M_SND_RES:         sys_settings.waterSilicaSoundEnabled = (edit_value > 0); break;
        case M_HEAT_TOGGLE:     sys_settings.waterHeaterEnabled = (edit_value > 0); break;
        case M_HEAT_MAX_TEMP:   sys_settings.waterHeaterMaxTemp = edit_value; break;
        default: break;
    }
    settings_unlock();
    settings_save(); 
    ESP_LOGI(TAG, "Value saved for Node ID %d: %d", id, edit_value);
}

// Форматирует ТОЛЬКО значение (без заголовка, так как заголовок теперь выводится отдельно)
static void format_edit_text(menu_node_id_t id, char* buf, size_t max_len) {
    switch (id) {
        case M_CLIM_MANUAL:
        case M_SYS_MAX_SAFE:
        case M_SYS_HYSTERESIS:
        case M_SYS_DEADZONE:    snprintf(buf, max_len, "%d%%", edit_value); break;
        case M_SYS_MAX_TIME:
        case M_SYS_COOLDOWN:    snprintf(buf, max_len, "%d мин", edit_value); break;
        case M_SET_LOCK_TIME:   snprintf(buf, max_len, "%s", enum_time_opts[edit_value]); break;
        case M_SET_MENU_TIME:   snprintf(buf, max_len, "%s", enum_menu_opts[edit_value]); break;
        case M_SET_SCREEN_TIME: snprintf(buf, max_len, "%s", enum_screen_opts[edit_value]); break;
        case M_SET_ROTATION:    snprintf(buf, max_len, "%s", enum_rotation_opts[edit_value]); break;
        case M_SET_TOUCH_ROT:   snprintf(buf, max_len, "%s", enum_touch_opts[edit_value]); break;
        case M_SET_BTN_HOLD:    snprintf(buf, max_len, "%d сек", edit_value); break;
        case M_SND_DOOR:
        case M_SND_RES:
        case M_HEAT_TOGGLE:     snprintf(buf, max_len, "%s", edit_value ? "ВКЛ" : "ОТКЛ"); break;
        case M_HEAT_MAX_TEMP:   snprintf(buf, max_len, "%d°C", edit_value); break;
        default:                snprintf(buf, max_len, "%d", edit_value); break;
    }
}

static void enforce_edit_limits(menu_node_id_t id) {
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
        
        case M_SET_LOCK_TIME:   
        case M_SET_MENU_TIME:   
        case M_SET_SCREEN_TIME: min = 0; max = 4; break;
        case M_SET_ROTATION:    min = 0; max = 3; break;
        case M_SET_TOUCH_ROT:   min = 0; max = 1; break;
        case M_SET_BTN_HOLD:    min = 1; max = 5; break;
        
        case M_SND_DOOR:
        case M_SND_RES:
        case M_HEAT_TOGGLE:     min = 0; max = 1; break;
        case M_HEAT_MAX_TEMP:   min = 30; max = 85; break;
        default: break;
    }
    if (edit_value < min) edit_value = min;
    if (edit_value > max) edit_value = max;
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

    // Вызываем функцию рендера (без аргументов редактирования, так как для этого есть отдельный экран)
    ui_screens_render_menu(titles_to_render, visible_count, cursor_idx - scroll_offset);
}

void menu_engine_init(void) {
    current_state = STATE_SPLASH_SCREEN;
    current_state = STATE_MAIN_SCREEN;
    current_folder_id = M_ROOT;
    cursor_idx = 0;
    scroll_offset = 0;
}

void menu_engine_boot_complete(void) {
    current_state = STATE_MAIN_SCREEN;
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

// =========================================================================
// 5. ОБРАБОТЧИК ЖЕСТОВ
// =========================================================================
esp_err_t menu_engine_process_gesture(hmi_event_type_t event) {
    // Игнорируем жесты, пока висит экран загрузки
    if (current_state == STATE_SPLASH_SCREEN) return ESP_OK; 
    if (current_state == STATE_MAIN_SCREEN) {
        if (event == EVENT_SWIPE_RIGHT || event == EVENT_TAP) {
            current_state = STATE_IN_MENU;
            current_folder_id = M_ROOT;
            cursor_idx = 0;
            scroll_offset = 0;
            ui_screens_show_menu();
            update_view();
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
                ui_screens_show_edit(selected->title, val_buf); // Переход на экран редактирования
            }
            else if (selected->type == NODE_ACTION) {
                ESP_LOGW(TAG, "Action executed: %s", selected->title);
                if (selected->id == M_FACTORY_RESET) {
                    settings_reset_to_defaults();
                    esp_restart();
                } else if (selected->id == M_ACOUSTIC_TEST) {
                    float freq = 0;
                    audio_analyze_resonance(&freq);
                }
            }
        }
        
        if (current_state == STATE_IN_MENU) update_view();
    }
    else if (current_state == STATE_EDITING_VALUE) {
        const menu_node_t* children[15];
        get_children_nodes(current_folder_id, children);
        menu_node_id_t active_id = children[cursor_idx]->id;

        if (event == EVENT_SWIPE_UP || event == EVENT_SWIPE_DOWN) {
            if (event == EVENT_SWIPE_UP) edit_value++;
            else edit_value--;
            
            enforce_edit_limits(active_id);
            
            char val_buf[32];
            format_edit_text(active_id, val_buf, sizeof(val_buf));
            ui_screens_update_edit_value(val_buf); // Обновляем цифру на экране
        }
        else if (event == EVENT_SWIPE_LEFT) {
            current_state = STATE_IN_MENU; // Отмена, возврат в меню
            ui_screens_show_menu();
            update_view();
        }
        else if (event == EVENT_SWIPE_RIGHT || event == EVENT_TAP) {
            save_edit_value(active_id);    // Сохранение и возврат в меню
            current_state = STATE_IN_MENU;
            ui_screens_show_menu();
            update_view();
        }
    }
    return ESP_OK;

        if (event == EVENT_SYSTEM_IDLE_TIMEOUT) {
        if (!menu_engine_is_on_main_screen()) {
            menu_engine_force_main_screen();
        }
        return ESP_OK;
    }
}