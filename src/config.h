#pragma once

// --- ESP32-S3 DevKit pin map ---
#define RS485_RX_PIN 17
#define RS485_TX_PIN 18
#define RS485_RE_DE_PIN 4

#define LCD_SDA_PIN 8
#define LCD_SCL_PIN 9

#define ENCODER_CLK_PIN 10
#define ENCODER_DT_PIN 11
#define ENCODER_SW_PIN 12

#define A3144_PIN 13
#define GAUSS_ADC_PIN 1

// --- LCD ---
#define LCD_I2C_ADDR 0x27
#define LCD_COLS 20
#define LCD_ROWS 4

// --- MKS SERVO42ES RS485 ---
#define SERVO42_ADDR 0x01
#define RS485_BAUD 38400
#define SERVO42_MAX_RPM 3000
#define SERVO42_DEFAULT_ACC 2
#define SERVO42_STOP_RPM_THRESHOLD 5
#define SERVO42_STOP_HOLD_MS 1500
#define SERVO42_PULSES_PER_REV 0x4000
#define SERVO42_INIT_ON_BOOT 1

// Turn counting: 1 = motor encoder via RS485, 0 = external A3144 Hall sensor
#define USE_MOTOR_ENCODER 1

// --- A3144 (when USE_MOTOR_ENCODER = 0) ---
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
#define MAX_RPM_USER 1500
