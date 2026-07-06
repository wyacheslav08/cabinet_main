#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct display_manager_t* display_handle_t;

/**
 * @brief Перечисление пунктов главного меню кабинета.
 */
typedef enum {
    MENU_ITEM_CLIMATE = 0,    // Настройка влажности и температуры
    MENU_ITEM_WEIGHT,         // Калибровка весов и инфо о гитаре
    MENU_ITEM_ACOUSTIC_TEST,  // Запуск DSP-анализа резонанса дерева
    MENU_ITEM_LIGHTING,       // Управление подсветкой кабинета
    MENU_ITEM_LOCK,           // Управление сервоприводом замка
    MENU_ITEM_MAX             // Количество пунктов меню
} display_menu_item_t;

/**
 * @brief Полная структура состояния кабинета для отрисовки на экране.
 */
typedef struct {
    // Климат
    float temperature;
    float humidity;
    bool is_heating;
    bool is_humidifying;
    bool is_dehumidifying;
    
    // Весы и гитара
    int32_t weight_grams;
    bool is_guitar_present;
    
    // Система
    uint8_t wifi_rssi_percent;
    bool is_ble_connected;
    bool is_locked;
} ui_status_data_t;

/**
 * @brief Инициализирует дисплей ST7735, LVGL и строит рабочий интерфейс.
 */
esp_err_t display_manager_init(display_handle_t *out_handle);

/**
 * @brief Обновляет показания датчиков в верхнем статус-баре и нижней панели.
 * Потокобезопасна (вызывается из задачи климата или телеметрии).
 */
esp_err_t display_manager_update_status(display_handle_t handle, const ui_status_data_t *data);

/**
 * @brief Перемещает курсор меню (реакция на свайпы ВВЕРХ / ВНИЗ от MPR121).
 * 
 * @param handle Хэндл дисплея.
 * @param direction Направление: -1 (вверх), +1 (вниз).
 * @return ESP_OK при успехе.
 */
esp_err_t display_manager_menu_navigate(display_handle_t handle, int direction);

/**
 * @brief Активирует выбранный пункт меню (реакция на ТАП / свайп ВПРАВО от MPR121).
 * 
 * @param handle Хэндл дисплея.
 * @param[out] selected_item Возвращает ID пункта меню, который был выбран.
 * @return ESP_OK при успехе.
 */
esp_err_t display_manager_menu_select(display_handle_t handle, display_menu_item_t *selected_item);

/**
 * @brief Управление яркостью и питанием экрана.
 */
esp_err_t display_manager_set_brightness(display_handle_t handle, uint8_t brightness_pct);
esp_err_t display_manager_set_power(display_handle_t handle, bool power_on);
esp_err_t display_manager_destroy(display_handle_t handle);

#ifdef __cplusplus
}
#endif