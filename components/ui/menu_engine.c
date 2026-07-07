/**
 * @file menu_engine.c
 * @brief Движок древовидного меню и обработка жестов навигации.
 */

#include "menu_engine.h"
#include "ui_screens.h"
#include "settings_manager.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "MENU_ENGINE";

typedef enum {
    NODE_TYPE_FOLDER,
    NODE_TYPE_ACTION,
    NODE_TYPE_VALUE
} menu_node_type_t;

typedef enum {
    MENU_ROOT = 0,
    MENU_CLIMATE, MENU_ACOUSTIC_TEST, MENU_SETTINGS, MENU_PASSWORD, MENU_FACTORY_RESET,
    MENU_CLIM_AUTO, MENU_CLIM_MANUAL, MENU_CLIM_GUITAR, MENU_CLIM_SYS_SET,
    MENU_CLIM_SYS_DEADZONE, MENU_CLIM_SYS_MIN_CHANGE, MENU_CLIM_SYS_MAX_TIME,
    MENU_SET_LOCK_TIME, MENU_SET_MENU_TIMEOUT, MENU_SET_SCREEN_TIMEOUT, MENU_SET_SHT_LIST, MENU_SET_SOUNDS, MENU_SET_HEATER,
    MENU_NODE_COUNT
} menu_node_id_t;

typedef struct {
    menu_node_id_t id;
    menu_node_id_t parent_id;
    menu_node_type_t type;
    const char* title;
} menu_node_t;

static const menu_node_t menu_db[MENU_NODE_COUNT] = {
    {MENU_ROOT,          MENU_ROOT,     NODE_TYPE_FOLDER, "Главное меню"},
    {MENU_CLIMATE,       MENU_ROOT,     NODE_TYPE_FOLDER, "1. Климат"},
    {MENU_ACOUSTIC_TEST, MENU_ROOT,     NODE_TYPE_FOLDER, "2. Акустич. тест"},
    {MENU_SETTINGS,      MENU_ROOT,     NODE_TYPE_FOLDER, "3. Настройки"},
    {MENU_PASSWORD,      MENU_ROOT,     NODE_TYPE_FOLDER, "4. Пароль"},
    {MENU_FACTORY_RESET, MENU_ROOT,     NODE_TYPE_ACTION, "5. Сброс настроек"},
    {MENU_CLIM_AUTO,     MENU_CLIMATE,  NODE_TYPE_ACTION, "Авто-режим"},
    {MENU_CLIM_MANUAL,   MENU_CLIMATE,  NODE_TYPE_VALUE,  "Ручная настр."},
    {MENU_CLIM_GUITAR,   MENU_CLIMATE,  NODE_TYPE_FOLDER, "Гитара"},
    {MENU_CLIM_SYS_SET,  MENU_CLIMATE,  NODE_TYPE_FOLDER, "Системные настр."},
    {MENU_CLIM_SYS_DEADZONE,   MENU_CLIM_SYS_SET, NODE_TYPE_VALUE, "Мертвая зона"},
    {MENU_CLIM_SYS_MIN_CHANGE, MENU_CLIM_SYS_SET, NODE_TYPE_VALUE, "Мин. изменение"},
    {MENU_CLIM_SYS_MAX_TIME,   MENU_CLIM_SYS_SET, NODE_TYPE_VALUE, "Макс. время раб."},
    {MENU_SET_LOCK_TIME,      MENU_SETTINGS, NODE_TYPE_VALUE,  "Время блок."},
    {MENU_SET_MENU_TIMEOUT,   MENU_SETTINGS, NODE_TYPE_VALUE,  "Таймаут меню"},
    {MENU_SET_SCREEN_TIMEOUT, MENU_SETTINGS, NODE_TYPE_VALUE,  "Таймаут экрана"},
    {MENU_SET_SHT_LIST,       MENU_SETTINGS, NODE_TYPE_FOLDER, "Датчики SHT"},
    {MENU_SET_SOUNDS,         MENU_SETTINGS, NODE_TYPE_FOLDER, "Звуки"},
    {MENU_SET_HEATER,         MENU_SETTINGS, NODE_TYPE_FOLDER, "Подогрев воды"}
};

typedef enum {
    STATE_MAIN_SCREEN = 0,
    STATE_IN_MENU,
    STATE_EDITING_VALUE
} engine_state_t;

static engine_state_t current_state = STATE_MAIN_SCREEN;
static menu_node_id_t current_folder_id = MENU_ROOT;
static int cursor_idx = 0;
static int scroll_offset = 0;
static int edit_value = 0;

static int get_children_nodes(menu_node_id_t parent, const menu_node_t* out_nodes[15]) {
    int count = 0;
    for (int i = 0; i < MENU_NODE_COUNT; i++) {
        if (menu_db[i].parent_id == parent && menu_db[i].id != parent) {
            out_nodes[count++] = &menu_db[i];
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

    char edit_buf[64] = {0};
    if (current_state == STATE_EDITING_VALUE) {
        snprintf(edit_buf, sizeof(edit_buf), "%s: %d", children[cursor_idx]->title, edit_value);
    }

    ui_screens_render_menu(titles_to_render, visible_count, cursor_idx - scroll_offset, (current_state == STATE_EDITING_VALUE), edit_buf);
}

void menu_engine_init(void) {
    current_state = STATE_MAIN_SCREEN;
    current_folder_id = MENU_ROOT;
    cursor_idx = 0;
    scroll_offset = 0;
}

bool menu_engine_is_on_main_screen(void) {
    return (current_state == STATE_MAIN_SCREEN);
}

esp_err_t menu_engine_process_gesture(hmi_event_type_t event) {
    if (current_state == STATE_MAIN_SCREEN) {
        if (event == EVENT_SWIPE_RIGHT || event == EVENT_TAP) {
            current_state = STATE_IN_MENU;
            current_folder_id = MENU_ROOT;
            cursor_idx = 0;
            scroll_offset = 0;
            ui_screens_show_menu();
            update_view();
        }
    }
    else if (current_state == STATE_IN_MENU) {
        const menu_node_t* children[15];
        int count = get_children_nodes(current_folder_id, children);

        if (event == EVENT_SWIPE_DOWN) cursor_idx = (cursor_idx + 1) % count;
        else if (event == EVENT_SWIPE_UP) { cursor_idx--; if (cursor_idx < 0) cursor_idx = count - 1; }
        else if (event == EVENT_SWIPE_LEFT) {
            if (current_folder_id == MENU_ROOT) {
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
            if (selected->type == NODE_TYPE_FOLDER) {
                current_folder_id = selected->id;
                cursor_idx = 0;
                scroll_offset = 0;
                update_view();
            }
            else if (selected->type == NODE_TYPE_VALUE) {
                if (selected->id == MENU_CLIM_MANUAL) {
                    settings_lock();
                    edit_value = sys_settings.targetHumidity;
                    settings_unlock();
                }
                current_state = STATE_EDITING_VALUE;
                update_view();
            }
            else if (selected->type == NODE_TYPE_ACTION) {
                ESP_LOGW(TAG, "Action executed: %s", selected->title);
            }
        }
        if (current_state == STATE_IN_MENU) update_view();
    }
    else if (current_state == STATE_EDITING_VALUE) {
        if (event == EVENT_SWIPE_UP) edit_value++;
        else if (event == EVENT_SWIPE_DOWN) edit_value--;
        else if (event == EVENT_SWIPE_LEFT) {
            current_state = STATE_IN_MENU; // Отмена
            update_view();
        }
        else if (event == EVENT_SWIPE_RIGHT || event == EVENT_TAP) {
            const menu_node_t* children[15];
            get_children_nodes(current_folder_id, children);
            if (children[cursor_idx]->id == MENU_CLIM_MANUAL) {
                settings_lock();
                sys_settings.targetHumidity = edit_value;
                settings_unlock();
                settings_save();
                ESP_LOGI(TAG, "Saved Target Humidity: %d", edit_value);
            }
            current_state = STATE_IN_MENU;
            update_view();
        }
        if (current_state == STATE_EDITING_VALUE) update_view();
    }
    return ESP_OK;
}