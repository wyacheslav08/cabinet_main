#include "audio_analyzer.h"
#include "hw_config.h"
#include "pwm_manager.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Подключаем модули ESP-DSP
#include "dsps_fft2r.h"
#include "dsps_wind.h"

static const char *TAG = "AUDIO_DSP";

#define SAMPLE_RATE 16000
#define FFT_SAMPLES 1024 // Должно быть степенью двойки

// Буферы для БПФ (выравниваем для аппаратного ускорения)
__attribute__((aligned(16))) static float fft_data[FFT_SAMPLES * 2];
__attribute__((aligned(16))) static float window_hann[FFT_SAMPLES];

static i2s_chan_handle_t rx_chan; // Хэндл канала приема

esp_err_t audio_analyzer_init(void) {
    // 1. Инициализация математики ESP-DSP
    esp_err_t err = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init FFT. Error: %s", esp_err_to_name(err));
        return err;
    }
    // Генерируем окно Ханна (чтобы убрать артефакты по краям выборки)
    dsps_wind_hann_f32(window_hann, FFT_SAMPLES);

    // 2. Инициализация I2S (Новый API v5.1+)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    err = i2s_new_channel(&chan_cfg, NULL, &rx_chan);
    if (err != ESP_OK) return err;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,    // INMP441 не требует MCLK
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = PIN_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false, .bclk_inv = false, .ws_inv = false
            },
        },
    };
    err = i2s_channel_init_std_mode(rx_chan, &std_cfg);
    if (err != ESP_OK) return err;

    err = i2s_channel_enable(rx_chan);
    if (err == ESP_OK) ESP_LOGI(TAG, "I2S Microphone and DSP initialized");
    
    return err;
}

esp_err_t audio_analyze_resonance(float *peak_frequency) {
    int32_t *raw_samples = malloc(FFT_SAMPLES * sizeof(int32_t));
    if (!raw_samples) return ESP_ERR_NO_MEM;

    // 1. Бьем по гитаре (Пинг)
    pwm_set_vibrator(100);             // Включаем вибродинамик на полную
    vTaskDelay(pdMS_TO_TICKS(50));     // Ждем 50 мс
    pwm_set_vibrator(0);               // Выключаем

    // Небольшая пауза, чтобы ушел механический лязг самого динамика
    vTaskDelay(pdMS_TO_TICKS(10)); 

    // 2. Записываем звук (Эхо)
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(rx_chan, raw_samples, FFT_SAMPLES * sizeof(int32_t), &bytes_read, pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        free(raw_samples);
        return err;
    }

    // 3. Подготовка данных для БПФ (Конвертация 32-bit int -> float и наложение окна)
    for (int i = 0; i < FFT_SAMPLES; i++) {
        // INMP441 выдает 24 бита, сдвинутые влево в 32-битном слоте. Делим на 2^24 для нормализации к -1.0 .. 1.0
        float sample_val = (float)(raw_samples[i] >> 8) / 8388608.0f; 
        
        fft_data[i * 2] = sample_val * window_hann[i]; // Реальная часть (умноженная на окно Ханна)
        fft_data[i * 2 + 1] = 0.0f;                    // Мнимая часть = 0
    }
    free(raw_samples);

    // 4. Выполняем БПФ
    dsps_fft2r_fc32(fft_data, FFT_SAMPLES);
    dsps_bit_rev_fc32(fft_data, FFT_SAMPLES);
    dsps_cplx2reC_fc32(fft_data, FFT_SAMPLES); // Вычисляем амплитуду

    // 5. Поиск пиковой частоты (пропускаем первые несколько бинов, чтобы отсечь низкочастотный гул < 100 Гц)
    float max_val = 0.0f;
    int max_index = 0;
    int start_bin = (100 * FFT_SAMPLES) / SAMPLE_RATE; // Начинаем поиск от 100 Гц

    for (int i = start_bin; i < FFT_SAMPLES / 2; i++) {
        if (fft_data[i * 2] > max_val) {
            max_val = fft_data[i * 2];
            max_index = i;
        }
    }

    // 6. Конвертация индекса массива в Герцы
    *peak_frequency = (float)max_index * (float)SAMPLE_RATE / (float)FFT_SAMPLES;
    
    ESP_LOGI(TAG, "Resonance Peak: %.1f Hz (Magnitude: %f)", *peak_frequency, max_val);
    
    return ESP_OK;
}