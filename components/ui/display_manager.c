/**
 * @file display_manager.c
 * @brief Аппаратный драйвер дисплея ST7735 и управление жизненным циклом LVGL.
 */

#include "display_manager.h"
#include "ui_screens.h"
#include "menu_engine.h"
#include "settings_manager.h" // Для чтения sys_settings.screenRotationIndex при старте
#include "hw_config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7735.h"
#include "esp_lvgl_port.h"
#include <stdio.h>
#include "esp_timer.h" 
#include "settings_manager.h" 

// Глобальная переменная для времени последней активности
static uint32_t last_activity_time_ms = 0;
static bool is_timeout_event_sent = false;
static const char *TAG = "DISPLAY_MGR";

struct display_manager_t {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t    panel_handle;
    lv_display_t*             lv_disp;
    uint8_t current_brightness;
    bool    is_power_on;
};

// =========================================================================
// АППАРАТНАЯ ПОДСВЕТКА (LEDC)
// =========================================================================
static esp_err_t ledc_backlight_init(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE_FANS, 
        .timer_num = LEDC_TIMER_FANS,
        .duty_resolution = LEDC_RES_FANS, 
        .freq_hz = LEDC_FREQ_FANS_HZ, 
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE_FANS, 
        .channel = LEDC_CH_LCD_BCKL,
        .timer_sel = LEDC_TIMER_FANS, 
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PIN_LCD_BCKL, 
        .duty = 0, 
        .hpoint = 0
    };
    return ledc_channel_config(&ledc_channel);
}

static void set_backlight_duty(uint8_t brightness_pct) {
    uint32_t duty = (255 * (brightness_pct > 100 ? 100 : brightness_pct)) / 100;
    ledc_set_duty(LEDC_MODE_FANS, LEDC_CH_LCD_BCKL, duty);
    ledc_update_duty(LEDC_MODE_FANS, LEDC_CH_LCD_BCKL);
}

// =========================================================================
// ПУБЛИЧНЫЙ API
// =========================================================================

// Функции конвертации индексов настроек в миллисекунды:
static uint32_t get_menu_timeout_ms(int index) {
    switch(index) {
        case 0: return 15000;  // 15 сек
        case 1: return 30000;  // 30 сек
        case 2: return 60000;  // 1 мин
        case 3: return 120000; // 2 мин
        default: return 0;     // ОТКЛ
    }
}

static uint32_t get_screen_timeout_ms(int index) {
    switch(index) {
        case 0: return 30000;  // 30 сек
        case 1: return 60000;  // 1 мин
        case 2: return 300000; // 5 мин
        case 3: return 600000; // 10 мин
        default: return 0;     // ОТКЛ
    }
}

// Функция для таймера
static void inactivity_timer_cb(void* arg) {
    display_handle_t handle = (display_handle_t)arg;
    if (!handle) return;

    settings_lock();
    uint32_t m_timeout = get_menu_timeout_ms(sys_settings.menuTimeoutOptionIndex);
    uint32_t s_timeout = get_screen_timeout_ms(sys_settings.screenTimeoutOptionIndex);
    settings_unlock();

    uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000ULL);
    uint32_t idle_time = current_time - last_activity_time_ms;

    // Отправляем событие ТОЛЬКО ОДИН РАЗ
    if (m_timeout > 0 && idle_time >= m_timeout) {
        if (!is_timeout_event_sent) {
            hmi_msg_t msg = { .type = 9 /* EVENT_SYSTEM_IDLE_TIMEOUT */, .sensor_index = 0 };
            xQueueSend(hmi_event_queue, &msg, 0);
            is_timeout_event_sent = true; // Блокируем спам в очередь
        }
    }

    // Отключение экрана
    if (s_timeout > 0 && idle_time >= s_timeout) {
        if (handle->is_power_on) {
            display_manager_set_power(handle, false);
        }
    }
}

esp_err_t display_manager_init(display_handle_t *out_handle) {
    if (!out_handle) return ESP_ERR_INVALID_ARG;

    struct display_manager_t* mgr = heap_caps_calloc(1, sizeof(struct display_manager_t), MALLOC_CAP_DEFAULT);
    if (!mgr) return ESP_ERR_NO_MEM;

    // 1. Инициализация ШИМ подсветки
    ledc_backlight_init();
    set_backlight_duty(0);

    // 2. Инициализация шины SPI
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_LCD_SCLK, 
        .mosi_io_num = PIN_LCD_MOSI, 
        .miso_io_num = PIN_LCD_MISO,
        .quadhd_io_num = -1, 
        .quadwp_io_num = -1, 
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * 2
    };
    spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);

    // 3. Инициализация интерфейса панели (ESP LCD)
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = PIN_LCD_CS, 
        .dc_gpio_num = PIN_LCD_DC, 
        .spi_mode = 0,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ, 
        .trans_queue_depth = 10, 
        .lcd_cmd_bits = 8, 
        .lcd_param_bits = 8
    };
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &mgr->io_handle);

    // 4. Инициализация драйвера ST7735
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST, 
        .rgb_endian = LCD_RGB_ENDIAN_BGR, 
        .bits_per_pixel = 16
    };
    esp_lcd_new_panel_st7735(mgr->io_handle, &panel_config, &mgr->panel_handle);
    esp_lcd_panel_reset(mgr->panel_handle);
    esp_lcd_panel_init(mgr->panel_handle);
    esp_lcd_panel_set_gap(mgr->panel_handle, LCD_GAP_X, LCD_GAP_Y);
    esp_lcd_panel_invert_color(mgr->panel_handle, false);
    esp_lcd_panel_disp_on_off(mgr->panel_handle, true);

    // 5. Инициализация порта LVGL
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_affinity = 0; // Строго Core 0
    lvgl_port_init(&lvgl_cfg);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = mgr->io_handle, 
        .panel_handle = mgr->panel_handle,
        .buffer_size = LCD_H_RES * 20, 
        .double_buffer = true,
        .hres = LCD_H_RES, 
        .vres = LCD_V_RES, 
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565, 
        .flags = { .buff_dma = true, .swap_bytes = true }
    };
    mgr->lv_disp = lvgl_port_add_disp(&disp_cfg);

    // 6. Инициализация верстки и логики меню под мьютексом
    if (lvgl_port_lock(portMAX_DELAY)) {
        ui_screens_init();
        menu_engine_init();
        ui_screens_show_main();
        lvgl_port_unlock();
    }

    // 7. ВАЖНО: Стартуем систему с выключенной подсветкой!
    mgr->current_brightness = 100;
    mgr->is_power_on = false; // <--- Было true
    set_backlight_duty(0);    // <--- Было set_backlight_duty(mgr->current_brightness)

    *out_handle = mgr;
    
    // Применяем сохраненный поворот экрана
    display_manager_set_rotation(*out_handle, sys_settings.screenRotationIndex);

    // Инициализируем время старта
    last_activity_time_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    // Создаем и запускаем фоновый таймер бездействия (каждую 1 секунду)
    esp_timer_create_args_t timer_args = {
        .callback = &inactivity_timer_cb,
        .arg = mgr,
        .name = "inactivity_timer"
    };
    esp_timer_handle_t inactivity_timer;
    esp_timer_create(&timer_args, &inactivity_timer);
    esp_timer_start_periodic(inactivity_timer, 1000000); // 1 000 000 мкс = 1 сек

    ESP_LOGI(TAG, "Hardware Display Driver initialized");
    return ESP_OK;
}

// В функции display_manager_set_rotation добавьте вызов перестроения:
esp_err_t display_manager_set_rotation(display_handle_t handle, uint8_t rotation_idx) {
    if (!handle || !handle->lv_disp) return ESP_ERR_INVALID_ARG;

    lv_display_rotation_t lv_rot;
    bool is_landscape = false; // Флаг для верстки

    switch (rotation_idx) {
        case 1: lv_rot = LV_DISPLAY_ROTATION_90;  is_landscape = true; break;
        case 2: lv_rot = LV_DISPLAY_ROTATION_180; is_landscape = false; break;
        case 3: lv_rot = LV_DISPLAY_ROTATION_270; is_landscape = true; break;
        default: lv_rot = LV_DISPLAY_ROTATION_0;  is_landscape = false; break;
    }

    if (lvgl_port_lock(portMAX_DELAY)) {
        lv_display_set_rotation(handle->lv_disp, lv_rot);
        
        // ВЫЗЫВАЕМ ФУНКЦИЮ ПЕРЕСТРОЕНИЯ ЭЛЕМЕНТОВ
        ui_screens_update_layout(is_landscape);
        
        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "Screen rotation applied: %d", rotation_idx);
    return ESP_OK;
}

esp_err_t display_manager_process_gesture(display_handle_t handle, hmi_event_type_t event) {
    if (!handle) return ESP_ERR_INVALID_ARG;

    // КРИТИЧНОЕ ИСПРАВЛЕНИЕ: 
    // Физические жесты сбрасывают таймер простоя. 
    // Системное событие таймаута (возврат из меню) — НЕ СБРАСЫВАЕТ.
    if (event != EVENT_SYSTEM_IDLE_TIMEOUT) {
        last_activity_time_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        is_timeout_event_sent = false;

        // Если экран был погашен, ЛЮБОЙ физический жест включает его обратно
        if (!handle->is_power_on) {
            display_manager_set_power(handle, true);
            // Возвращаем ESP_OK без передачи жеста в меню. 
            // Это защита: первое касание просто будит систему.
            return ESP_OK; 
        }
    }

    // Если мы на главном экране и сделали свайп влево — гасим экран (сбережение энергии)
    if (menu_engine_is_on_main_screen() && event == EVENT_SWIPE_LEFT) {
        return display_manager_set_power(handle, false);
    }

    // Передаем жест в движок меню
    if (lvgl_port_lock(portMAX_DELAY)) {
        menu_engine_process_gesture(event);
        lvgl_port_unlock();
    }
    return ESP_OK;
}

esp_err_t display_manager_update_status(display_handle_t handle, const ui_status_data_t *data) {
    if (!handle || !data) return ESP_ERR_INVALID_ARG;

    // Добавляем статический флаг первого запуска
    static bool is_first_update = true;

    // Сначала обновляем текст на экране (в фоне)
    if (lvgl_port_lock(pdMS_TO_TICKS(50))) {
        ui_screens_update_telemetry(data->temperature, data->humidity, data->wifi_rssi_percent, 
                                    data->is_ble_connected, data->is_locked, 
                                    data->is_guitar_present, data->weight_grams);
        lvgl_port_unlock();
    }

    // Как только отрисовались реальные цифры — включаем подсветку
    if (is_first_update) {
        is_first_update = false;
        display_manager_set_power(handle, true);
    }

    return ESP_OK;
}

esp_err_t display_manager_set_brightness(display_handle_t handle, uint8_t brightness_pct) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    handle->current_brightness = (brightness_pct > 100) ? 100 : brightness_pct;
    if (handle->is_power_on) set_backlight_duty(handle->current_brightness);
    return ESP_OK;
}

esp_err_t display_manager_set_power(display_handle_t handle, bool power_on) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (handle->is_power_on == power_on) return ESP_OK;
    
    handle->is_power_on = power_on;

    if (power_on) {
        // Просто плавно включаем подсветку
        set_backlight_duty(handle->current_brightness);
    } else {
        // Просто гасим подсветку. 
        // ВАЖНО: Убрано esp_lcd_panel_disp_on_off, чтобы избежать "белого экрана"
        set_backlight_duty(0);
    }
    return ESP_OK;
}

esp_err_t display_manager_destroy(display_handle_t handle) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    set_backlight_duty(0);
    if (handle->lv_disp) lvgl_port_remove_disp(handle->lv_disp);
    if (handle->panel_handle) esp_lcd_panel_del(handle->panel_handle);
    if (handle->io_handle) esp_lcd_panel_io_del(handle->io_handle);
    spi_bus_free(LCD_SPI_HOST);
    heap_caps_free(handle);
    return ESP_OK;
}