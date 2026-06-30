#pragma once

#include "driver/i2c.h"
#include "driver/uart.h"

// --- I2C Configuration ---
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_SDA_IO           21
#define I2C_MASTER_SCL_IO           22
#define I2C_MASTER_FREQ_HZ          400000 
#define I2C_MASTER_TIMEOUT_MS       100

// Адреса двух мультиплексоров на одной шине
#define MUX_ADDR_TOUCH              0x71    // Mux 1: Сенсорная панель (MPR121)
#define MUX_ADDR_SENSORS            0x70    // Mux 2: Климат и Дисплеи

// Каналы Mux 1 (0x70) - 4 контроллера MPR121
#define MUX_CH_MPR_1                4
#define MUX_CH_MPR_2                5
#define MUX_CH_MPR_3                6
#define MUX_CH_MPR_4                7

// Каналы Mux 2 (0x71) - Климат и Экран
#define MUX_CH_SHT40_TOP            4
#define MUX_CH_SHT40_HUM            5
#define MUX_CH_OLED_MAIN            2
#define MUX_CH_OLED_SEC             3

// --- UART (Связь с Gateway ESP32) ---
#define COMM_UART_NUM               UART_NUM_1
#define COMM_UART_TX_PIN            17
#define COMM_UART_RX_PIN            16
#define COMM_UART_BAUD_RATE         115200
#define COMM_UART_RX_BUF_SIZE       1024

// --- Power Electronics (7 Channels) ---
#define PIN_MOSFET_REGEN_HEATER     4   // 1. ТЭН осушителя
#define PIN_MOSFET_HUM_HEATER       15  // 2. ТЭН увлажнителя (или пьезо)
#define PIN_MOSFET_HUM_FAN          18  // 3. Вентилятор увлажнителя
#define PIN_MOSFET_DEHUM_FAN        25  // 4. Вентилятор осушителя
#define PIN_MOSFET_EXHAUST_FAN      26  // 5. Вентилятор вытяжки
#define PIN_SERVO_DOOR              27  // 6. Сервопривод заслонки
#define PIN_MOSFET_VIBRATOR         14  // 7. Вибродинамик
#define PIN_DOOR_SENSOR             12  // Геркон двери (LOW = Закрыто)

// --- Weight Sensor (HX711) ---
#define PIN_HX711_DT                19  // Data
#define PIN_HX711_SCK               23  // Clock

// --- Audio (INMP441 I2S Microphone) ---
#define PIN_I2S_BCLK                13  // SCK (Serial Clock)
#define PIN_I2S_WS                  32  // WS (Word Select / L-R Clock)
#define PIN_I2S_DIN                 33  // SD (Serial Data)