#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация микрофона и DSP
esp_err_t audio_analyzer_init(void);

// Запустить тест: удар -> запись -> БПФ -> возврат резонансной частоты
esp_err_t audio_analyze_resonance(float *peak_frequency);

#ifdef __cplusplus
}
#endif