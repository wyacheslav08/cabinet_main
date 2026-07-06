#include "display_manager.h"
#include "u8g2.h"
#include "i2c_manager.h"
#include "hw_config.h"
#include "climate_control.h"
#include "gesture_manager.h" // Здесь лежат EVENT_SWIPE_UP и очередь hmi_event_queue
#include "settings_manager.h"
#include "uart_link.h"       // Для отправки сообщений в Gateway
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rom/ets_sys.h"
#include <string.h>

static const char *TAG = "UI_MGR";
static u8g2_t u8g2_main;

// --- Состояния меню ---
typedef enum {
    SCREEN_MAIN,
    SCREEN_MENU_HUMIDITY,
    SCREEN_MENU_HEATER,
    SCREEN_MENU_SOUND,
    SCREEN_MENU_CALIB,
    SCREEN_MENU_REBOOT
} screen_state_t;

static screen_state_t current_screen = SCREEN_MAIN;
static bool is_editing = false; 
static int edit_value = 0;      

// --- Системные функции U8g2 ---
static uint8_t u8g2_gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch(msg) {
        case U8X8_MSG_DELAY_MILLI: vTaskDelay(pdMS_TO_TICKS(arg_int)); break;
        case U8X8_MSG_DELAY_10MICRO: ets_delay_us(arg_int * 10); break;
        case U8X8_MSG_DELAY_100NANO: ets_delay_us(1); break;
    }
    return 1;
}

static uint8_t u8g2_byte_hw_i2c_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    // Используем выделение в куче, так как размер I2C пакета U8G2 может достигать 128 байт.
    // Так как это вызывается только из одной задачи, фрагментация минимизируется.
    static uint8_t *buffer = NULL; 
    static uint8_t buf_idx = 0;

    switch(msg) {
        case U8X8_MSG_BYTE_START_TRANSFER:
            if (buffer == NULL) buffer = malloc(128); // Выделяем один раз
            buf_idx = 0;
            break;
        case U8X8_MSG_BYTE_SEND:
            if (buffer && (buf_idx + arg_int <= 128)) {
                memcpy(&buffer[buf_idx], arg_ptr, arg_int);
                buf_idx += arg_int;
            }
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:
            if (buffer) {
                i2c_manager_lock();
                if (i2c_manager_set_mux(MUX_ADDR_SENSORS, MUX_CH_OLED_MAIN) == ESP_OK) {
                    uint8_t i2c_addr = u8x8_GetI2CAddress(u8x8) >> 1; 
                    i2c_master_write_to_device(I2C_MASTER_NUM, i2c_addr, buffer, buf_idx, pdMS_TO_TICKS(100));
                }
                i2c_manager_unlock();
            }
            break;
    }
    return 1;
}

// --- Отрисовка Экранов ---
static void draw_screen_main(void) {
    u8g2_SetFont(&u8g2_main, u8g2_font_ncenB14_tr); 
    
    settings_lock();
    int target_h = sys_settings.targetHumidity;
    settings_unlock();

    char buf[32];
    snprintf(buf, sizeof(buf), "HUM: %d%%", target_h);
    u8g2_DrawStr(&u8g2_main, 10, 30, buf);

    u8g2_SetFont(&u8g2_main, u8g2_font_ncenB08_tr);
    climate_state_t state = climate_get_state();
    if (state == CLIMATE_STATE_HUMIDIFYING) u8g2_DrawStr(&u8g2_main, 10, 50, "Status: HUMIDIFY");
    else if (state == CLIMATE_STATE_DEHUMIDIFYING) u8g2_DrawStr(&u8g2_main, 10, 50, "Status: DEHUMIDIFY");
    else if (state == CLIMATE_STATE_DOOR_OPEN) u8g2_DrawStr(&u8g2_main, 10, 50, "Status: DOOR OPEN");
    else u8g2_DrawStr(&u8g2_main, 10, 50, "Status: IDLE");
}

static void draw_screen_menu(const char* title, int value, const char* unit) {
    u8g2_SetFont(&u8g2_main, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&u8g2_main, 5, 15, title);
    u8g2_DrawHLine(&u8g2_main, 0, 20, 128);

    u8g2_SetFont(&u8g2_main, u8g2_font_ncenB14_tr);
    char buf[32];
    
    if (is_editing) {
        if ((xTaskGetTickCount() * portTICK_PERIOD_MS) % 1000 > 500) {
            snprintf(buf, sizeof(buf), "[ %d %s ]", value, unit);
        } else {
            snprintf(buf, sizeof(buf), "  %d %s  ", value, unit);
        }
    } else {
        snprintf(buf, sizeof(buf), "%d %s", value, unit);
    }
    
    u8g2_DrawStr(&u8g2_main, 20, 45, buf);
}

// --- Главная задача ---
static void ui_task(void *pvParameters) {
    ESP_LOGI(TAG, "UI Task Started");

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2_main, U8G2_R0, u8g2_byte_hw_i2c_cb, u8g2_gpio_and_delay_cb);
    u8g2_main.u8x8.i2c_address = 0x3C << 1; 
    u8g2_InitDisplay(&u8g2_main);
    u8g2_SetPowerSave(&u8g2_main, 0);

    hmi_msg_t msg;

    while (1) {
        // Ожидаем события из очереди (максимум 100 мс)
        if (xQueueReceive(hmi_event_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            
            // Обработка Системных Событий
            if (msg.type == EVENT_DOOR_UNLOCK) {
                ESP_LOGW(TAG, "!!! UNLOCKING DOOR FROM HMI !!!");
                uart_link_send((const uint8_t*)"K10_STAT:LOCK:active\n", 21);
            }
            else if (msg.type == EVENT_ERROR_SENSOR_OFFLINE) {
                ESP_LOGE(TAG, "OLED MSG: Sensor %d Offline!", msg.sensor_index);
            }
            
            // Обработка Жестов Меню
            else if (current_screen == SCREEN_MAIN) {
                if (msg.type == EVENT_SWIPE_RIGHT) current_screen = SCREEN_MENU_HUMIDITY; 
            } 
            else if (!is_editing) {
                if (msg.type == EVENT_SWIPE_LEFT) current_screen = SCREEN_MAIN;
                else if (msg.type == EVENT_SWIPE_DOWN) {
                    if (current_screen < SCREEN_MENU_REBOOT) current_screen++;
                    else current_screen = SCREEN_MENU_HUMIDITY;
                } 
                else if (msg.type == EVENT_SWIPE_UP) {
                    if (current_screen > SCREEN_MENU_HUMIDITY) current_screen--;
                    else current_screen = SCREEN_MENU_REBOOT;
                } 
                else if (msg.type == EVENT_SWIPE_RIGHT) {
                    is_editing = true;
                    settings_lock();
                    if (current_screen == SCREEN_MENU_HUMIDITY) edit_value = sys_settings.targetHumidity;
                    else if (current_screen == SCREEN_MENU_HEATER) edit_value = sys_settings.waterHeaterEnabled ? 1 : 0;
                    settings_unlock();
                }
            } 
            else if (is_editing) {
                if (msg.type == EVENT_SWIPE_LEFT) is_editing = false; 
                else if (msg.type == EVENT_SWIPE_UP) edit_value++; 
                else if (msg.type == EVENT_SWIPE_DOWN) edit_value--; 
                else if (msg.type == EVENT_SWIPE_RIGHT) {
                    settings_lock();
                    if (current_screen == SCREEN_MENU_HUMIDITY) {
                        if (edit_value >= 10 && edit_value <= 90) sys_settings.targetHumidity = edit_value;
                    }
                    else if (current_screen == SCREEN_MENU_HEATER) {
                        sys_settings.waterHeaterEnabled = (edit_value > 0);
                    }
                    settings_unlock();
                    settings_save();
                    is_editing = false;
                }
            }
        }

        // Отрисовка кадра
        u8g2_ClearBuffer(&u8g2_main);

        settings_lock();
        int cur_hum = sys_settings.targetHumidity;
        int cur_heater = sys_settings.waterHeaterEnabled ? 1 : 0;
        settings_unlock();

        switch (current_screen) {
            case SCREEN_MAIN: draw_screen_main(); break;
            case SCREEN_MENU_HUMIDITY: draw_screen_menu("Target Humidity", is_editing ? edit_value : cur_hum, "%"); break;
            case SCREEN_MENU_HEATER: draw_screen_menu("Water Heater", is_editing ? edit_value : cur_heater, "(1=ON)"); break;
            case SCREEN_MENU_SOUND: draw_screen_menu("Sound Alerts", 1, "(ON)"); break;
            case SCREEN_MENU_CALIB: draw_screen_menu("Calibration", 0, "offset"); break;
            case SCREEN_MENU_REBOOT: draw_screen_menu("System Reboot", 0, ""); break;
        }

        u8g2_SendBuffer(&u8g2_main);
    }
}

esp_err_t display_manager_init(void) {
    xTaskCreate(ui_task, "ui_task", 6144, NULL, 3, NULL);
    return ESP_OK;
}