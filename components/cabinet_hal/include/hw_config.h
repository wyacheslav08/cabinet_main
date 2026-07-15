#pragma once

// =========================================================================
// [HAL] ПОЛНАЯ КАРТА АППАРАТНЫХ РЕСУРСОВ ПРОЕКТА (ESP32-WROOM-32)
// =========================================================================

// --- 1. ШИНА I2C (Два мультиплексора PCA9548A) ---
#define I2C_MASTER_SDA_IO       21          // I2C SDA
#define I2C_MASTER_SCL_IO       22          // I2C SCL
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      400000      // 400 кГц (Fast Mode)
#define I2C_MASTER_TIMEOUT_MS   100

// --- 2. МУЛЬТИПЛЕКСОРЫ I2C И СЕНСОРНЫЕ ПАНЕЛИ ---

// [MUX 1] Мультиплексор климатических датчиков (SHT40)
#define MUX_ADDR_SENSORS        0x70        // I2C адрес MUX 1
#define MUX_CH_SHT_MAIN         7           // Канал 0: Основной
#define MUX_CH_SHT_HUMIDIFIER   6           // Канал 1: Внутри увлажнителя
#define MUX_CH_SHT_DEHUMIDIFIER 5           // Канал 2: Внутри осушителя
#define MUX_CH_SHT_EXTERNAL     4           // Канал 3: Внешний (комнатный)

// [СЕНСОРНЫЕ ПАНЕЛИ] Прямое подключение к I2C (без мультиплексора)
#define MPR121_ADDR_1           0x5A        // Панель 1 (Верхняя, ADDR -> VSS)
#define MPR121_ADDR_2           0x5C        // Панель 2 (Нижняя, ADDR -> SDA)

// --- АДРЕСА И КОМАНДЫ ДАТЧИКОВ ---
#define SHT40_I2C_ADDR          0x44        // Адрес SHT40
#define SHT40_CMD_MEAS_HIGH_PREC 0xFD       // Команда SHT40: Измерение высокой точности

// --- 3. ШИНА SPI И НАСТРОЙКИ ДИСПЛЕЯ ST7735 (128x160) ---
#define LCD_SPI_HOST            VSPI_HOST
#define PIN_LCD_SCLK            18          // Аппаратный VSPI CLK
#define PIN_LCD_MOSI            23          // Аппаратный VSPI MOSI
#define PIN_LCD_MISO            -1          // Не используется (экран только принимает)
#define PIN_LCD_CS              5           // Аппаратный VSPI CS0
#define PIN_LCD_DC              19          // Data/Command
#define PIN_LCD_RST             4           // Reset дисплея
#define PIN_LCD_BCKL            2           // ШИМ подсветки (LEDC Timer 0)

#define LCD_H_RES               128         // Горизонтальное разрешение
#define LCD_V_RES               160         // Вертикальное разрешение
#define LCD_PIXEL_CLOCK_HZ      (10 * 1000 * 1000) // Частота SPI 10 МГц
#define LCD_GAP_X               1           // Смещение матрицы в стекле ST7735
#define LCD_GAP_Y               2

// --- 4. ШИНА I2S (Цифровой микрофон INMP441 для DSP анализа дерева) ---
#define PIN_I2S_BCLK            26          // Bit Clock
#define PIN_I2S_WS              25          // Word Select (LRCK)
#define PIN_I2S_DIN             39          // Data IN (Input-Only пин VN)

// --- 5. ИНТЕРФЕЙС ВЕСОВ (HX711 - АЦП тензодатчика) ---
#define PIN_HX711_SCK           32          // Тактирование АЦП (Выход)
#define PIN_HX711_DT            35          // Данные с АЦП (Input-Only пин)

// --- 6. ДАТЧИКИ БЕЗОПАСНОСТИ И ПОЛОЖЕНИЯ (Входы с внешней подтяжкой 10кОм) ---
#define PIN_DOOR_SENSOR         36          // 0 - закрыто, 1 - открыто (Input-Only)
#define PIN_ORIENTATION_SENSOR  34          // 0 - перевернуто, 1 - норма (Input-Only VP)

// --- 7. СВЯЗЬ С GATEWAY (UART2 Bridge для BLE/Wi-Fi шлюза) ---
#define COMM_UART_NUM           UART_NUM_2
#define COMM_UART_TX_PIN        17          // TX2 -> RX шлюза Gateway
#define COMM_UART_RX_PIN        16          // RX2 <- TX шлюза Gateway
#define COMM_UART_BAUD_RATE     115200
#define COMM_UART_RX_BUF_SIZE   512

// =========================================================================
// НАСТРОЙКИ АППАРАТНЫХ ТАЙМЕРОВ ШИМ (LEDC)
// =========================================================================

// Таймер 0: Высокочастотный (5 кГц, 8 бит) для подсветки, ТЭНов и вибро
#define LEDC_TIMER_FANS         LEDC_TIMER_0
#define LEDC_MODE_FANS          LEDC_LOW_SPEED_MODE
#define LEDC_FREQ_FANS_HZ       5000
#define LEDC_RES_FANS           LEDC_TIMER_8_BIT

// Таймер 1: Низкочастотный (50 Гц, 14 бит) для сервопривода замка/заслонки
#define LEDC_TIMER_SERVO        LEDC_TIMER_1
#define LEDC_MODE_SERVO         LEDC_LOW_SPEED_MODE
#define LEDC_FREQ_SERVO_HZ      50

// Распределение каналов ШИМ
#define LEDC_CH_LCD_BCKL        LEDC_CHANNEL_0    // Подсветка экрана (Пин 2)
#define LEDC_CH_HEATER          LEDC_CHANNEL_1    // ТЭН осушителя (Пин 27)
#define LEDC_CH_HUMIDIFIER      LEDC_CHANNEL_2    // ТЭН увлажнителя (Пин 14)
#define LEDC_CH_VIBRATOR        LEDC_CHANNEL_3    // Вибродинамик (Пин 13)
#define LEDC_CH_SERVO           LEDC_CHANNEL_4    // Сервопривод (Пин 15)

// =========================================================================
// РАСПРЕДЕЛЕНИЕ ИСПОЛНИТЕЛЬНЫХ УСТРОЙСТВ (ACTUATORS)
// =========================================================================

// --- АППАРАТНЫЙ ШИМ НА MAIN BOARD (Аналоговое управление 0-100% / Серво) ---
#define PIN_MOSFET_REGEN_HEATER 27          // ТЭН осушителя (ШИМ LEDC)
#define PIN_MOSFET_HUM_HEATER   14          // ТЭН / Пьезо увлажнителя (ШИМ LEDC)
#define PIN_MOSFET_VIBRATOR     13          // Вибродинамик для акустического теста
#define PIN_SERVO_DOOR          15          // Сервопривод заслонки (50 Гц ШИМ)

// --- ЦИФРОВЫЕ УСТРОЙСТВА 0/1 (ПЕРЕНЕСЕНЫ НА ПЛАТУ GATEWAY VIA UART) ---
#define PIN_MOSFET_HUM_FAN      0xF1        // Вентилятор увлажнителя -> Gateway
#define PIN_MOSFET_DEHUM_FAN    0xF2        // Вентилятор осушителя   -> Gateway
#define PIN_MOSFET_EXHAUST_FAN  0xF3        // Вентилятор вытяжки     -> Gateway
#define PIN_SOLENOID_DOOR       0xF4        // Электромагнитный замок -> Gateway

#define PIN_RESET_BTN           0           // Аппаратная кнопка сброса пароля (BOOT на ESP32)