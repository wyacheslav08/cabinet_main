#pragma once

// =========================================================================
// [HAL] ПОЛНАЯ КАРТА АППАРАТНЫХ РЕСУРСОВ MAIN BOARD (ESP32-WROOM-32)
// =========================================================================

// --- 1. ШИНА I2C (Два мультиплексора PCA9548A) ---
#define I2C_MASTER_SDA_IO       21
#define I2C_MASTER_SCL_IO       22
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      400000
#define I2C_MASTER_TIMEOUT_MS   100

// [MUX 1] Мультиплексор климатических датчиков (SHT40)
#define MUX_ADDR_SENSORS        0x70
#define MUX_CH_SHT_MAIN         7
#define MUX_CH_SHT_HUMIDIFIER   6
#define MUX_CH_SHT_DEHUMIDIFIER 5
#define MUX_CH_SHT_EXTERNAL     4

// [СЕНСОРНЫЕ ПАНЕЛИ] 
#define MPR121_ADDR_1           0x5A
#define MPR121_ADDR_2           0x5C

#define SHT40_I2C_ADDR          0x44
#define SHT40_CMD_MEAS_HIGH_PREC 0xFD

// --- 2. ШИНА SPI И ДИСПЛЕЙ ST7735 (128x160) ---
#define LCD_SPI_HOST            VSPI_HOST
#define PIN_LCD_SCLK            18
#define PIN_LCD_MOSI            23
#define PIN_LCD_MISO            -1
#define PIN_LCD_CS              5
#define PIN_LCD_DC              19
#define PIN_LCD_RST             4
#define PIN_LCD_BCKL            2           // ШИМ подсветки (Локальный)

#define LCD_H_RES               128
#define LCD_V_RES               160
#define LCD_PIXEL_CLOCK_HZ      (10 * 1000 * 1000)
#define LCD_GAP_X               1
#define LCD_GAP_Y               2

// --- 3. ШИНА I2S (Микрофон INMP441) ---
#define PIN_I2S_BCLK            26
#define PIN_I2S_WS              25
#define PIN_I2S_DIN             39

// --- 4. ИНТЕРФЕЙС ВЕСОВ (HX711) ---
#define PIN_HX711_SCK           32
#define PIN_HX711_DT            35

// --- 5. ДАТЧИКИ БЕЗОПАСНОСТИ И ПОЛОЖЕНИЯ ---
#define PIN_DOOR_SENSOR         36          // 0 - закрыто, 1 - открыто (Input-Only)
#define PIN_ORIENTATION_SENSOR  34          // 0 - перевернуто, 1 - норма (Input-Only)
#define PIN_RESET_BTN           0           // Аппаратная кнопка сброса (BOOT)

// --- 6. СВЯЗЬ С GATEWAY (UART2) ---
#define COMM_UART_NUM           UART_NUM_2
#define COMM_UART_TX_PIN        17
#define COMM_UART_RX_PIN        16
#define COMM_UART_BAUD_RATE     115200
#define COMM_UART_RX_BUF_SIZE   512

// =========================================================================
// НАСТРОЙКИ АППАРАТНЫХ ТАЙМЕРОВ ШИМ (ЛОКАЛЬНЫЕ)
// =========================================================================
#define LEDC_TIMER_FANS         LEDC_TIMER_0
#define LEDC_MODE_FANS          LEDC_LOW_SPEED_MODE
#define LEDC_FREQ_FANS_HZ       5000
#define LEDC_RES_FANS           LEDC_TIMER_8_BIT
#define LEDC_CH_LCD_BCKL        LEDC_CHANNEL_0    // Только подсветка!

// =========================================================================
// ВИРТУАЛЬНЫЕ УСТРОЙСТВА (ФИЗИЧЕСКИ НАХОДЯТСЯ НА GATEWAY)
// =========================================================================
#define VIRTUAL_PIN_HUM_FAN      0xF1
#define VIRTUAL_PIN_DEHUM_FAN    0xF2
#define VIRTUAL_PIN_EXHAUST_FAN  0xF3
#define VIRTUAL_PIN_SOLENOID     0xF4
#define VIRTUAL_PIN_SERVO_DOOR   0xF5
#define VIRTUAL_PIN_REGEN_HEATER 0xF6
#define VIRTUAL_PIN_HUM_HEATER   0xF7
#define VIRTUAL_PIN_VIBRATOR     0xF8