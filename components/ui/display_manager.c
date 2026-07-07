/**
 * @file display_manager.c
 * @brief Реализация модуля управления дисплеем ST7735 и графическим интерфейсом LVGL v9.
 * @note Модуль обеспечивает потокобезопасную отрисовку и обработку жестов через стейт-машину.
 */

#include "display_manager.h"
#include "hw_config.h"
#include "settings_manager.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7735.h"
#include "esp_lvgl_port.h"
#include <stdio.h>

static const char *TAG = "DISPLAY_UI";

// --- ОБЪЯВЛЕНИЕ ВНЕШНИХ ШРИФТОВ LVGL ---
LV_FONT_DECLARE(font_cyrillic_12);
LV_FONT_DECLARE(font_cyrillic_16);

// --- СИМВОЛЫ ИКОНОК (Юникод / FontAwesome) ---
#define SYM_WIFI          "\uF1EB"
#define SYM_BLE           "\uF294"
#define SYM_LOCK_CLOSED   "\uF023"
#define SYM_LOCK_OPEN     "\uF09C"
#define SYM_SCALES        "\uF24E"
#define SYM_GUITAR        "\uF7A6"
#define SYM_HUMIDIFIER    "\uF043"
#define SYM_HEATING       "\uF06D"

// Названия пунктов главного меню
static const char* MENU_NAMES[MENU_ITEM_MAX] = {
    "1. Климат (Влажность)",
    "2. Весы и гитара",
    "3. Акустич. тест",
    "4. Подсветка",
    "5. Замок дверцы"
};

/**
 * @brief Внутренние состояния интерфейса (State Machine).
 */
typedef enum {
    UI_STATE_MENU = 0,      // Режим навигации по меню
    UI_STATE_EDIT_CLIMATE,  // Режим редактирования целевой влажности
    UI_STATE_EDIT_LIGHTING  // Режим редактирования яркости экрана
} ui_state_t;

/**
 * @brief Внутренняя структура хэндла дисплея (Opaque Pointer).
 */
struct display_manager_t {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t    panel_handle;
    lv_display_t*             lv_disp;
    
    // Виджеты статус-бара (Верхняя панель)
    lv_obj_t* lbl_status_icons;
    lv_obj_t* lbl_status_climate;
    
    // Виджеты меню (Центральная панель)
    lv_obj_t* menu_items[MENU_ITEM_MAX];
    int       current_menu_idx;
    
    // Виджет нижней панели (Инфо о весах)
    lv_obj_t* lbl_footer_info;
    
    // Состояние конечного автомата
    ui_state_t current_state;
    int        temp_target_humidity; // Временное значение при редактировании
    
    uint8_t   current_brightness;
    bool      is_power_on;
};

// =========================================================================
// АППАРАТНЫЙ ШИМ ПОДСВЕТКИ (LEDC)
// =========================================================================

/**
 * @brief Инициализация таймера и канала LEDC для управления подсветкой экрана.
 * Используется высокая частота (5 кГц), чтобы исключить мерцание на камеру или глазами.
 */
static esp_err_t ledc_backlight_init(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE_FANS,
        .timer_num        = LEDC_TIMER_FANS,
        .duty_resolution  = LEDC_RES_FANS,
        .freq_hz          = LEDC_FREQ_FANS_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&ledc_timer), TAG, "LEDC timer config failed");

    ledc_channel_config_t ledc_channel = {
        .speed_mode       = LEDC_MODE_FANS,
        .channel          = LEDC_CH_LCD_BCKL,
        .timer_sel        = LEDC_TIMER_FANS,
        .intr_type        = LEDC_INTR_DISABLE,
        .gpio_num         = PIN_LCD_BCKL,
        .duty             = 0,
        .hpoint           = 0
    };
    return ledc_channel_config(&ledc_channel);
}

/**
 * @brief Установка коэффициента заполнения ШИМ подсветки (0..100%).
 */
static void set_backlight_duty(uint8_t brightness_pct) {
    if (brightness_pct > 100) brightness_pct = 100;
    // Для 8-битного разрешения (LEDC_TIMER_8_BIT) максимальное значение 255
    uint32_t duty = (255 * brightness_pct) / 100;
    ledc_set_duty(LEDC_MODE_FANS, LEDC_CH_LCD_BCKL, duty);
    ledc_update_duty(LEDC_MODE_FANS, LEDC_CH_LCD_BCKL);
}

// =========================================================================
// ВНУТРЕННИЕ ФУНКЦИИ ОТРИСОВКИ LVGL (Вызывать СТРОГО под мьютексом!)
// =========================================================================

/**
 * @brief Обновляет визуальные стили пунктов меню в зависимости от текущего состояния.
 */
static void update_menu_render(struct display_manager_t* mgr) {
    for (int i = 0; i < MENU_ITEM_MAX; i++) {
        if (i == mgr->current_menu_idx) {
            if (mgr->current_state == UI_STATE_EDIT_CLIMATE && i == MENU_ITEM_CLIMATE) {
                // Режим редактирования: Красный фон, выводим редактируемое значение
                char buf[32];
                snprintf(buf, sizeof(buf), "Цель: %d %%", mgr->temp_target_humidity);
                lv_label_set_text(mgr->menu_items[i], buf);
                lv_obj_set_style_bg_color(mgr->menu_items[i], lv_color_hex(0xCC0000), 0);
            } else {
                // Обычное выделение пункта меню: Синий фон
                lv_label_set_text(mgr->menu_items[i], MENU_NAMES[i]);
                lv_obj_set_style_bg_color(mgr->menu_items[i], lv_color_hex(0x0066CC), 0);
            }
            lv_obj_set_style_bg_opa(mgr->menu_items[i], LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(mgr->menu_items[i], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_translate_x(mgr->menu_items[i], 4, 0); // Визуальный сдвиг вправо
        } else {
            // Неактивный пункт меню: Прозрачный фон, серый текст
            lv_label_set_text(mgr->menu_items[i], MENU_NAMES[i]);
            lv_obj_set_style_bg_opa(mgr->menu_items[i], LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(mgr->menu_items[i], lv_color_hex(0xAAAAAA), 0);
            lv_obj_set_style_translate_x(mgr->menu_items[i], 0, 0);
        }
    }
}

/**
 * @brief Построение дерева виджетов LVGL.
 * Разделяет экран 128x160 на 3 зоны: Статус-бар (28px), Меню (104px), Подвал (22px).
 */
static void build_functional_ui(struct display_manager_t* mgr) {
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 2, 0);

    // --- 1. ВЕРХНИЙ СТАТУС-БАР ---
    lv_obj_t* top_bar = lv_obj_create(screen);
    lv_obj_set_size(top_bar, lv_pct(100), 28);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 4, 0);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);

    mgr->lbl_status_icons = lv_label_create(top_bar);
    lv_obj_set_style_text_font(mgr->lbl_status_icons, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(mgr->lbl_status_icons, lv_color_hex(0xFFFF00), 0);
    lv_label_set_text(mgr->lbl_status_icons, SYM_WIFI " " SYM_LOCK_CLOSED);
    lv_obj_align(mgr->lbl_status_icons, LV_ALIGN_LEFT_MID, 0, 0);

    mgr->lbl_status_climate = lv_label_create(top_bar);
    lv_obj_set_style_text_font(mgr->lbl_status_climate, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(mgr->lbl_status_climate, lv_color_hex(0x00FFFF), 0);
    lv_label_set_text(mgr->lbl_status_climate, "--.-°C --%");
    lv_obj_align(mgr->lbl_status_climate, LV_ALIGN_RIGHT_MID, 0, 0);

    // --- 2. ЦЕНТРАЛЬНОЕ МЕНЮ ---
    lv_obj_t* menu_cont = lv_obj_create(screen);
    lv_obj_set_size(menu_cont, lv_pct(100), 104);
    lv_obj_align(menu_cont, LV_ALIGN_CENTER, 0, 2);
    lv_obj_set_style_bg_opa(menu_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(menu_cont, 0, 0);
    lv_obj_set_style_pad_all(menu_cont, 2, 0);
    lv_obj_set_layout(menu_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(menu_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menu_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    for (int i = 0; i < MENU_ITEM_MAX; i++) {
        mgr->menu_items[i] = lv_label_create(menu_cont);
        lv_obj_set_width(mgr->menu_items[i], lv_pct(95));
        lv_obj_set_style_text_font(mgr->menu_items[i], &font_cyrillic_12, 0);
        lv_obj_set_style_pad_all(mgr->menu_items[i], 4, 0);
        lv_obj_set_style_radius(mgr->menu_items[i], 3, 0);
        lv_label_set_text(mgr->menu_items[i], MENU_NAMES[i]);
        lv_label_set_long_mode(mgr->menu_items[i], LV_LABEL_LONG_MODE_DOTS);
    }
    
    mgr->current_menu_idx = 0;
    mgr->current_state = UI_STATE_MENU;
    update_menu_render(mgr);

    // --- 3. НИЖНЯЯ ПАНЕЛЬ ИНФО ---
    lv_obj_t* bottom_bar = lv_obj_create(screen);
    lv_obj_set_size(bottom_bar, lv_pct(100), 22);
    lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bottom_bar, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bottom_bar, 0, 0);

    mgr->lbl_footer_info = lv_label_create(bottom_bar);
    lv_obj_set_style_text_font(mgr->lbl_footer_info, &font_cyrillic_12, 0);
    lv_obj_set_style_text_color(mgr->lbl_footer_info, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(mgr->lbl_footer_info, SYM_SCALES " Весы: 0.00 кг");
    lv_obj_align(mgr->lbl_footer_info, LV_ALIGN_CENTER, 0, 0);
}

// =========================================================================
// ПУБЛИЧНЫЙ API ДРАЙВЕРА
// =========================================================================

esp_err_t display_manager_init(display_handle_t *out_handle) {
    if (!out_handle) return ESP_ERR_INVALID_ARG;

    // Выделение памяти под хэндл во внутренней RAM
    struct display_manager_t* mgr = heap_caps_calloc(1, sizeof(struct display_manager_t), MALLOC_CAP_DEFAULT);
    if (!mgr) {
        ESP_LOGE(TAG, "Failed to allocate memory for display manager");
        return ESP_ERR_NO_MEM;
    }

    // 1. Инициализация подсветки (выключена до окончания настройки экрана)
    ESP_ERROR_CHECK(ledc_backlight_init());
    set_backlight_duty(0);

    // 2. Инициализация шины SPI с DMA
    spi_bus_config_t buscfg = {
        .sclk_io_num     = PIN_LCD_SCLK,
        .mosi_io_num     = PIN_LCD_MOSI,
        .miso_io_num     = PIN_LCD_MISO,
        .quadhd_io_num   = -1,
        .quadwp_io_num   = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t)
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 3. Подключение панели к шине SPI
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num        = PIN_LCD_CS,
        .dc_gpio_num        = PIN_LCD_DC,
        .spi_mode           = 0,
        .pclk_hz            = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth  = 10,
        .lcd_cmd_bits       = 8,
        .lcd_param_bits     = 8
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &mgr->io_handle));

    // 4. Инициализация драйвера ST7735
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_endian     = LCD_RGB_ENDIAN_BGR,
        .bits_per_pixel = 16
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7735(mgr->io_handle, &panel_config, &mgr->panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(mgr->panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(mgr->panel_handle));
    
    // Смещение матрицы для конкретного стекла ST7735 (128x160)
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(mgr->panel_handle, LCD_GAP_X, LCD_GAP_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(mgr->panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(mgr->panel_handle, true));

    // 5. Инициализация порта LVGL (Жесткая привязка к Core 0)
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_affinity = 0; 
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // 6. Добавление дисплея в LVGL с двойной буферизацией в DMA-памяти
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = mgr->io_handle,
        .panel_handle  = mgr->panel_handle,
        .buffer_size   = LCD_H_RES * 20,
        .double_buffer = true,
        .hres          = LCD_H_RES,
        .vres          = LCD_V_RES,
        .monochrome    = false,
        .color_format  = LV_COLOR_FORMAT_RGB565,
        .flags         = { .buff_dma = true, .swap_bytes = true }
    };
    mgr->lv_disp = lvgl_port_add_disp(&disp_cfg);

    // 7. Построение интерфейса (Под защитой мьютекса LVGL)
    if (lvgl_port_lock(portMAX_DELAY)) {
        build_functional_ui(mgr);
        lvgl_port_unlock();
    }

    // 8. Плавное включение подсветки
    mgr->current_brightness = 100;
    mgr->is_power_on = true;
    set_backlight_duty(mgr->current_brightness);

    *out_handle = mgr;
    ESP_LOGI(TAG, "Display Manager & LVGL v9 successfully initialized");
    return ESP_OK;
}

esp_err_t display_manager_process_gesture(display_handle_t handle, hmi_event_type_t event) {
    if (!handle) return ESP_ERR_INVALID_ARG;

    // Захватываем мьютекс LVGL на время изменения виджетов и состояний
    if (!lvgl_port_lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }

    switch (handle->current_state) {
        // =================================================================
        // СОСТОЯНИЕ 1: НАВИГАЦИЯ ПО МЕНЮ
        // =================================================================
        case UI_STATE_MENU:
            if (event == EVENT_SWIPE_DOWN) {
                handle->current_menu_idx = (handle->current_menu_idx + 1) % MENU_ITEM_MAX;
            } 
            else if (event == EVENT_SWIPE_UP) {
                handle->current_menu_idx--;
                if (handle->current_menu_idx < 0) handle->current_menu_idx = MENU_ITEM_MAX - 1;
            } 
            else if (event == EVENT_SWIPE_RIGHT || event == EVENT_TAP) {
                // Вход в подменю или режим редактирования
                if (handle->current_menu_idx == MENU_ITEM_CLIMATE) {
                    settings_lock();
                    handle->temp_target_humidity = sys_settings.targetHumidity;
                    settings_unlock();
                    handle->current_state = UI_STATE_EDIT_CLIMATE;
                    ESP_LOGI(TAG, "Entered climate edit mode. Current target: %d%%", handle->temp_target_humidity);
                }
                else if (handle->current_menu_idx == MENU_ITEM_LIGHTING) {
                    handle->current_state = UI_STATE_EDIT_LIGHTING;
                }
            }
            else if (event == EVENT_SWIPE_LEFT) {
                // В главном меню свайп влево гасит экран (сбережение энергии)
                lvgl_port_unlock();
                return display_manager_set_power(handle, false);
            }
            break;

        // =================================================================
        // СОСТОЯНИЕ 2: РЕДАКТИРОВАНИЕ ЦЕЛЕВОЙ ВЛАЖНОСТИ
        // =================================================================
        case UI_STATE_EDIT_CLIMATE:
            if (event == EVENT_SWIPE_UP) {
                handle->temp_target_humidity += 1;
                // Ограничение физически разумными пределами для древесины гитар
                if (handle->temp_target_humidity > 70) handle->temp_target_humidity = 70;
            } 
            else if (event == EVENT_SWIPE_DOWN) {
                handle->temp_target_humidity -= 1;
                if (handle->temp_target_humidity < 30) handle->temp_target_humidity = 30;
            } 
            else if (event == EVENT_SWIPE_LEFT) {
                // Отмена редактирования (возврат в меню без сохранения)
                handle->current_state = UI_STATE_MENU;
                ESP_LOGI(TAG, "Climate edit cancelled");
            } 
            else if (event == EVENT_SWIPE_RIGHT || event == EVENT_TAP) {
                // Подтверждение и сохранение в NVS Flash
                settings_lock();
                sys_settings.targetHumidity = handle->temp_target_humidity;
                settings_unlock();
                
                // Сохраняем в энергонезависимую память
                if (settings_save() == ESP_OK) {
                    ESP_LOGI(TAG, "New Target Humidity saved to NVS: %d%%", handle->temp_target_humidity);
                } else {
                    ESP_LOGE(TAG, "Failed to save settings to NVS!");
                }
                handle->current_state = UI_STATE_MENU;
            }
            break;

        // =================================================================
        // СОСТОЯНИЕ 3: РЕДАКТИРОВАНИЕ ЯРКОСТИ ПОДСВЕТКИ
        // =================================================================
        case UI_STATE_EDIT_LIGHTING:
            if (event == EVENT_SWIPE_UP) {
                int new_bright = handle->current_brightness + 20;
                if (new_bright > 100) new_bright = 100;
                set_backlight_duty((uint8_t)new_bright);
                handle->current_brightness = (uint8_t)new_bright;
            }
            else if (event == EVENT_SWIPE_DOWN) {
                int new_bright = handle->current_brightness - 20;
                if (new_bright < 10) new_bright = 10; // Не гасим полностью
                set_backlight_duty((uint8_t)new_bright);
                handle->current_brightness = (uint8_t)new_bright;
            }
            else if (event == EVENT_SWIPE_LEFT || event == EVENT_SWIPE_RIGHT || event == EVENT_TAP) {
                handle->current_state = UI_STATE_MENU;
            }
            break;

        default:
            break;
    }

    // Применяем изменения к графическим виджетам
    update_menu_render(handle);
    
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t display_manager_update_status(display_handle_t handle, const ui_status_data_t *data) {
    if (!handle || !data) return ESP_ERR_INVALID_ARG;

    // Форматирование иконок статус-бара
    char str_icons[32];
    snprintf(str_icons, sizeof(str_icons), "%s %s %s",
             data->wifi_rssi_percent > 0 ? SYM_WIFI : " ",
             data->is_ble_connected ? SYM_BLE : " ",
             data->is_locked ? SYM_LOCK_CLOSED : SYM_LOCK_OPEN);

    // Форматирование климатических данных (с защитой от вывода мусора при сбое датчика)
    char str_climate[32];
    if (data->temperature > -90.0f && data->humidity > -90.0f) {
        snprintf(str_climate, sizeof(str_climate), "%.1f°C %.0f%%", data->temperature, data->humidity);
    } else {
        snprintf(str_climate, sizeof(str_climate), "SHT ERR");
    }

    // Форматирование строки весов и наличия гитары
    char str_footer[64];
    float weight_kg = (float)data->weight_grams / 1000.0f;
    snprintf(str_footer, sizeof(str_footer), "%s %.2f кг | %s",
             SYM_SCALES, weight_kg,
             data->is_guitar_present ? SYM_GUITAR " Внутри" : "Пусто");

    // Используем короткий таймаут (50 мс), чтобы фоновая задача климата не зависла,
    // если UI в данный момент активно перерисовывается
    if (lvgl_port_lock(pdMS_TO_TICKS(50))) {
        if (handle->lbl_status_icons)   lv_label_set_text(handle->lbl_status_icons, str_icons);
        if (handle->lbl_status_climate) lv_label_set_text(handle->lbl_status_climate, str_climate);
        if (handle->lbl_footer_info)    lv_label_set_text(handle->lbl_footer_info, str_footer);
        lvgl_port_unlock();
    } else {
        ESP_LOGD(TAG, "LVGL port busy, skipping status update");
    }

    return ESP_OK;
}

esp_err_t display_manager_menu_navigate(display_handle_t handle, int direction) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    return display_manager_process_gesture(handle, (direction < 0) ? EVENT_SWIPE_UP : EVENT_SWIPE_DOWN);
}

esp_err_t display_manager_menu_select(display_handle_t handle, display_menu_item_t *selected_item) {
    if (!handle || !selected_item) return ESP_ERR_INVALID_ARG;
    
    esp_err_t err = display_manager_process_gesture(handle, EVENT_TAP);
    if (err == ESP_OK) {
        *selected_item = (display_menu_item_t)handle->current_menu_idx;
    }
    return err;
}

esp_err_t display_manager_set_brightness(display_handle_t handle, uint8_t brightness_pct) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (brightness_pct > 100) brightness_pct = 100;
    
    handle->current_brightness = brightness_pct;
    if (handle->is_power_on) {
        set_backlight_duty(brightness_pct);
    }
    return ESP_OK;
}

esp_err_t display_manager_set_power(display_handle_t handle, bool power_on) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (handle->is_power_on == power_on) return ESP_OK;
    
    handle->is_power_on = power_on;

    if (power_on) {
        // Включение: сначала активируем контроллер ЖК, затем плавно включаем подсветку
        if (lvgl_port_lock(portMAX_DELAY)) {
            esp_lcd_panel_disp_on_off(handle->panel_handle, true);
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        set_backlight_duty(handle->current_brightness);
        ESP_LOGI(TAG, "Display powered ON");
    } else {
        // Выключение: сначала гасим ШИМ подсветки, затем переводим ЖК в Sleep
        set_backlight_duty(0);
        vTaskDelay(pdMS_TO_TICKS(20));
        if (lvgl_port_lock(portMAX_DELAY)) {
            esp_lcd_panel_disp_on_off(handle->panel_handle, false);
            lvgl_port_unlock();
        }
        ESP_LOGI(TAG, "Display powered OFF (Sleep mode)");
    }
    return ESP_OK;
}

esp_err_t display_manager_destroy(display_handle_t handle) {
    if (!handle) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Destroying display manager...");
    set_backlight_duty(0);

    if (handle->lv_disp) {
        lvgl_port_remove_disp(handle->lv_disp);
    }
    if (handle->panel_handle) {
        esp_lcd_panel_del(handle->panel_handle);
    }
    if (handle->io_handle) {
        esp_lcd_panel_io_del(handle->io_handle);
    }
    
    spi_bus_free(LCD_SPI_HOST);
    heap_caps_free(handle);
    return ESP_OK;
}