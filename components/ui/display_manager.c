/**
 * @file display_manager.c
 * @brief Аппаратный драйвер дисплея ST7735 и управление жизненным циклом LVGL.
 */

#include "display_manager.h"
#include "ui_screens.h"
#include "menu_engine.h"
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

static const char *TAG = "DISPLAY_MGR";

struct display_manager_t {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t    panel_handle;
    lv_display_t*             lv_disp;
    uint8_t current_brightness;
    bool    is_power_on;
};

static esp_err_t ledc_backlight_init(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE_FANS, .timer_num = LEDC_TIMER_FANS,
        .duty_resolution = LEDC_RES_FANS, .freq_hz = LEDC_FREQ_FANS_HZ, .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE_FANS, .channel = LEDC_CH_LCD_BCKL,
        .timer_sel = LEDC_TIMER_FANS, .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PIN_LCD_BCKL, .duty = 0, .hpoint = 0
    };
    return ledc_channel_config(&ledc_channel);
}

static void set_backlight_duty(uint8_t brightness_pct) {
    uint32_t duty = (255 * (brightness_pct > 100 ? 100 : brightness_pct)) / 100;
    ledc_set_duty(LEDC_MODE_FANS, LEDC_CH_LCD_BCKL, duty);
    ledc_update_duty(LEDC_MODE_FANS, LEDC_CH_LCD_BCKL);
}

esp_err_t display_manager_init(display_handle_t *out_handle) {
    if (!out_handle) return ESP_ERR_INVALID_ARG;

    struct display_manager_t* mgr = heap_caps_calloc(1, sizeof(struct display_manager_t), MALLOC_CAP_DEFAULT);
    if (!mgr) return ESP_ERR_NO_MEM;

    ledc_backlight_init();
    set_backlight_duty(0);

    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_LCD_SCLK, .mosi_io_num = PIN_LCD_MOSI, .miso_io_num = PIN_LCD_MISO,
        .quadhd_io_num = -1, .quadwp_io_num = -1, .max_transfer_sz = LCD_H_RES * LCD_V_RES * 2
    };
    spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = PIN_LCD_CS, .dc_gpio_num = PIN_LCD_DC, .spi_mode = 0,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ, .trans_queue_depth = 10, .lcd_cmd_bits = 8, .lcd_param_bits = 8
    };
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &mgr->io_handle);

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST, .rgb_endian = LCD_RGB_ENDIAN_BGR, .bits_per_pixel = 16
    };
    esp_lcd_new_panel_st7735(mgr->io_handle, &panel_config, &mgr->panel_handle);
    esp_lcd_panel_reset(mgr->panel_handle);
    esp_lcd_panel_init(mgr->panel_handle);
    esp_lcd_panel_set_gap(mgr->panel_handle, LCD_GAP_X, LCD_GAP_Y);
    esp_lcd_panel_invert_color(mgr->panel_handle, false);
    esp_lcd_panel_disp_on_off(mgr->panel_handle, true);

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_affinity = 0; 
    lvgl_port_init(&lvgl_cfg);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = mgr->io_handle, .panel_handle = mgr->panel_handle,
        .buffer_size = LCD_H_RES * 20, .double_buffer = true,
        .hres = LCD_H_RES, .vres = LCD_V_RES, .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565, .flags = { .buff_dma = true, .swap_bytes = true }
    };
    mgr->lv_disp = lvgl_port_add_disp(&disp_cfg);

    // Инициализация верстки и логики под мьютексом
    if (lvgl_port_lock(portMAX_DELAY)) {
        ui_screens_init();
        menu_engine_init();
        ui_screens_show_main();
        lvgl_port_unlock();
    }

    mgr->current_brightness = 100;
    mgr->is_power_on = true;
    set_backlight_duty(mgr->current_brightness);

    *out_handle = mgr;
    ESP_LOGI(TAG, "Hardware Display Driver initialized");
    return ESP_OK;
}

esp_err_t display_manager_process_gesture(display_handle_t handle, hmi_event_type_t event) {
    if (!handle) return ESP_ERR_INVALID_ARG;

    // Если мы на главном экране и сделали свайп влево — гасим экран (сбережение энергии)
    if (menu_engine_is_on_main_screen() && event == EVENT_SWIPE_LEFT) {
        return display_manager_set_power(handle, false);
    }
    
    // Если экран был погашен, любое касание включает его обратно
    if (!handle->is_power_on) {
        return display_manager_set_power(handle, true);
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

    if (lvgl_port_lock(pdMS_TO_TICKS(50))) {
        ui_screens_update_telemetry(data->temperature, data->humidity, data->wifi_rssi_percent, 
                                    data->is_ble_connected, data->is_locked, 
                                    data->is_guitar_present, data->weight_grams);
        lvgl_port_unlock();
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
        if (lvgl_port_lock(portMAX_DELAY)) { esp_lcd_panel_disp_on_off(handle->panel_handle, true); lvgl_port_unlock(); }
        vTaskDelay(pdMS_TO_TICKS(50));
        set_backlight_duty(handle->current_brightness);
    } else {
        set_backlight_duty(0);
        vTaskDelay(pdMS_TO_TICKS(20));
        if (lvgl_port_lock(portMAX_DELAY)) { esp_lcd_panel_disp_on_off(handle->panel_handle, false); lvgl_port_unlock(); }
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