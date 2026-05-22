#pragma once

// --- Pin map (RP2040 / Pico) ---
#define LCD_SDA_PIN 0
#define LCD_SCL_PIN 1
#define STEP_PIN 2
#define DIR_PIN 3
#define TMC_UART_TX_PIN 4
#define TMC_UART_RX_PIN 5
#define ENCODER_CLK_PIN 6
#define ENCODER_DT_PIN 7
#define ENCODER_SW_PIN 8
#define A3144_PIN 9
#define MOTOR_EN_PIN 10

#define GAUSS_ADC_PIN 26  // ADC0

// --- LCD ---
#define LCD_I2C_ADDR 0x27
#define LCD_COLS 20
#define LCD_ROWS 4

// --- Motor / TMC2208 ---
#define USE_TMC2208_UART 0
#define MICROSTEP 1
#define STEPS_PER_REV (200 * MICROSTEP)
#define MAX_RPM_USER 1500
#define USE_SOFT_START 1
#define SOFT_RAMP_MS 800

#define TMC_R_SENSE 0.11f
#define TMC_RUN_CURRENT_MA 800

// --- A3144 rotation sensor ---
#define A3144_PULSES_PER_REV 1
#define A3144_DEBOUNCE_US 3000

// --- Gauss (AH49HZ3 on ADC) ---
#define GAUSS_ACTIVATE_G 50.0f
#define GAUSS_ADC_SAMPLES 8

// --- Presets ---
#define PRESET_MAX_COUNT 32
#define PRESET_NAME_LEN 16

// --- UI timing ---
#define BLINK_MS 400
#define LONG_PRESS_MS 800
#define COUNTDOWN_START 3

// --- Winding limits ---
#define MIN_TURNS 1
#define MAX_TURNS 99999
#define MIN_RPM 1
