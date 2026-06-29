#include "display_manager.h"
#include "u8g2.h"
#include "i2c_manager.h"
#include "hw_config.h"
#include "climate_control.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rom/ets_sys.h"
#include <string.h>

static const char *TAG = "UI_MGR";

// Глобальный объект дисплея
static u8g2_t u8g2_main;

// 1. Функция задержек для U8g2 (интеграция с FreeRTOS)
static uint8_t u8g2_gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch(msg) {
        case U8X8_MSG_DELAY_MILLI:
            vTaskDelay(pdMS_TO_TICKS(arg_int));
            break;
        case U8X8_MSG_DELAY_10MICRO:
            ets_delay_us(arg_int * 10);
            break;
        case U8X8_MSG_DELAY_100NANO:
            ets_delay_us(1);
            break;
    }
    return 1;
}

// 2. Функция передачи данных по I2C (Интеграция с нашим Менеджером и Мультиплексором)
static uint8_t u8g2_byte_hw_i2c_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    static uint8_t buffer[128]; // Буфер для пакета I2C
    static uint8_t buf_idx = 0;

    switch(msg) {
        case U8X8_MSG_BYTE_SEND:
            memcpy(&buffer[buf_idx], arg_ptr, arg_int);
            buf_idx += arg_int;
            break;
            
        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_idx = 0;
            break;
            
        case U8X8_MSG_BYTE_END_TRANSFER:
            i2c_manager_lock();
            // Переключаем мультиплексор на канал Главного OLED
            if (i2c_manager_set_mux(MUX_ADDR_SENSORS, MUX_CH_OLED_MAIN) == ESP_OK) {
                // Отправляем буфер на адрес дисплея
                uint8_t i2c_addr = u8x8_GetI2CAddress(u8x8) >> 1; 
                i2c_master_write_to_device(I2C_MASTER_NUM, i2c_addr, buffer, buf_idx, pdMS_TO_TICKS(100));
            }
            i2c_manager_unlock();
            break;
    }
    return 1;
}

// 3. Главная задача отрисовки интерфейса (UI Task)
static void ui_task(void *pvParameters) {
    ESP_LOGI(TAG, "UI Task Started");

    // Инициализация структуры дисплея SSD1306 (128x64)
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2_main, U8G2_R0, u8g2_byte_hw_i2c_cb, u8g2_gpio_and_delay_cb);
    u8g2_main.u8x8.i2c_address = 0x3C << 1; // U8g2 ожидает сдвинутый адрес
    
    u8g2_InitDisplay(&u8g2_main);
    u8g2_SetPowerSave(&u8g2_main, 0);

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(200); // Обновление экрана 5 раз в секунду (200 мс)

    char status_buf[32];

    while (1) {
        // Получаем данные от других систем
        climate_state_t state = climate_get_state();

        // Отрисовка кадра
        u8g2_ClearBuffer(&u8g2_main);
        
        // Настройка шрифта
        u8g2_SetFont(&u8g2_main, u8g2_font_ncenB08_tr); 
        
        // Шапка
        u8g2_DrawStr(&u8g2_main, 2, 10, "Guitar Cabinet OS");
        u8g2_DrawHLine(&u8g2_main, 0, 12, 128);

        // Вывод текущего состояния климата
        switch (state) {
            case CLIMATE_STATE_IDLE: snprintf(status_buf, sizeof(status_buf), "State: IDLE"); break;
            case CLIMATE_STATE_HUMIDIFYING: snprintf(status_buf, sizeof(status_buf), "State: HUMIDIFY (+)"); break;
            case CLIMATE_STATE_DEHUMIDIFYING: snprintf(status_buf, sizeof(status_buf), "State: DEHUMIDIFY (-)"); break;
            case CLIMATE_STATE_DOOR_OPEN: snprintf(status_buf, sizeof(status_buf), "State: DOOR OPEN!"); break;
            default: snprintf(status_buf, sizeof(status_buf), "State: ERROR"); break;
        }
        u8g2_DrawStr(&u8g2_main, 2, 30, status_buf);

        // Отправка буфера на дисплей
        u8g2_SendBuffer(&u8g2_main);

        // Засыпаем до следующего кадра
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

esp_err_t display_manager_init(void) {
    // Создаем задачу UI (Приоритет 3 - ниже логики, но достаточно для плавности)
    BaseType_t res = xTaskCreate(ui_task, "ui_task", 6144, NULL, 3, NULL);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UI task");
        return ESP_FAIL;
    }
    return ESP_OK;
}