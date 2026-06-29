#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Профиль конкретной гитары
typedef struct {
    char name[32];          // Название (например, "Fender Stratocaster")
    int32_t ref_weight_g;   // Эталонный вес в граммах
    int32_t tolerance_g;    // Допуск (погрешность, например +- 50 грамм)
    int target_humidity;    // Идеальная влажность для этого дерева (%)
} guitar_profile_t;

// Инициализация (чтение тары из NVS, настройка)
esp_err_t weight_manager_init(void);

// Получить текущий вес в граммах
esp_err_t weight_get_grams(int32_t *grams);

// Найти гитару в базе по весу (возвращает указатель на профиль или NULL, если не найдена)
const guitar_profile_t* weight_identify_guitar(int32_t current_weight_g);

// Установить нулевую точку (Тарирование) - пустой шкаф
esp_err_t weight_tare(void);

#ifdef __cplusplus
}
#endif