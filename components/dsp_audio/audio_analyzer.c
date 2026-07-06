#include "audio_analyzer.h"
#include "hw_config.h"
#include "pwm_manager.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dsps_fft2r.h"
#include "dsps_wind.h"
#include "dsps_math.h"

static const char *TAG = "AUDIO_DSP";

#define SAMPLE_RATE 16000
#define FFT_SAMPLES 1024 

// Статические буферы в .bss (выровненные для инструкций ESP32 FPU)
__attribute__((aligned(16))) static float fft_data[FFT_SAMPLES * 2];
__attribute__((aligned(16))) static float window_hann[FFT_SAMPLES];

static i2s_chan_handle_t rx_chan = NULL; 
static int32_t *dma_raw_samples = NULL; // Буфер для чтения из I2S

esp_err_t audio_analyzer_init(void) {
    // 1. Выделяем память строго во внутренней SRAM для работы с DMA I2S
    dma_raw_samples = (int32_t *)heap_caps_malloc(FFT_SAMPLES * sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (dma_raw_samples == NULL) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
    if (err != ESP_OK) return err;
    
    dsps_wind_hann_f32(window_hann, FFT_SAMPLES);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    err = i2s_new_channel(&chan_cfg, NULL, &rx_chan);
    if (err != ESP_OK) return err;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = PIN_I2S_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    err = i2s_channel_init_std_mode(rx_chan, &std_cfg);
    if (err != ESP_OK) return err;

    err = i2s_channel_enable(rx_chan);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "I2S Microphone and DSP initialized");
    }
    return err;
}

esp_err_t audio_analyze_resonance(float *peak_frequency) {
    if (peak_frequency == NULL || dma_raw_samples == NULL) return ESP_ERR_INVALID_ARG;

    // 1. Удар по деке
    pwm_set_vibrator(100);
    vTaskDelay(pdMS_TO_TICKS(50));
    pwm_set_vibrator(0);
    vTaskDelay(pdMS_TO_TICKS(10)); // Игнорируем механический звон

    // 2. Чтение из DMA
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(rx_chan, dma_raw_samples, FFT_SAMPLES * sizeof(int32_t), &bytes_read, pdMS_TO_TICKS(1000));
    if (err != ESP_OK) return err;

    // 3. Нормализация и окно Ханна
    for (int i = 0; i < FFT_SAMPLES; i++) {
        float sample_val = (float)(dma_raw_samples[i] >> 8) / 8388608.0f; 
        fft_data[i * 2] = sample_val * window_hann[i]; 
        fft_data[i * 2 + 1] = 0.0f;                    
    }

    // 4. Вычисление FFT
    dsps_fft2r_fc32(fft_data, FFT_SAMPLES);
    dsps_bit_rev_fc32(fft_data, FFT_SAMPLES);
    dsps_cplx2reC_fc32(fft_data, FFT_SAMPLES); 

    // 5. Поиск пика (отсекаем < 100 Гц)
    float max_val = 0.0f;
    int max_index = 0;
    int start_bin = (100 * FFT_SAMPLES) / SAMPLE_RATE; 

    for (int i = start_bin; i < FFT_SAMPLES / 2; i++) {
        if (fft_data[i * 2] > max_val) {
            max_val = fft_data[i * 2];
            max_index = i;
        }
    }

    *peak_frequency = (float)max_index * (float)SAMPLE_RATE / (float)FFT_SAMPLES;
    ESP_LOGI(TAG, "Resonance Peak: %.1f Hz (Magnitude: %f)", *peak_frequency, max_val);
    
    return ESP_OK;
}