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

// AH49HZ3 VOUT — GPIO 13 or 14 (ADC2); 13 recommended (was A3144, now free)
#define GAUSS_ADC_PIN 13

// --- LCD ---
#define LCD_I2C_ADDR 0x27
#define LCD_COLS 20
#define LCD_ROWS 4

// --- MKS SERVO42ES RS485 ---
#define SERVO42_ADDR 0x01
#define RS485_BAUD 38400
#define SERVO42_MAX_RPM 3000
#define SERVO42_DEFAULT_ACC 2
#define SERVO42_QUICK_STOP_ACC 10
#define SERVO42_STOP_RPM_THRESHOLD 5
#define SERVO42_STOP_HOLD_MS 1500
// Manual 0x31: one revolution = 0x4000 encoder pulses
#define SERVO42_PULSES_PER_REV 0x4000
#define SERVO42_INIT_ON_BOOT 1

// --- Gauss (AH49HZ3 on ADC) ---
#define GAUSS_MV_PER_G 2.5f
#define GAUSS_ADC_SAMPLES 8
#define GAUSS_ACTIVATE_G 50.0f
#define GAUSS_WARMUP_MS 1500
#define GAUSS_CALIB_QUIET_MS 800
#define GAUSS_CALIB_MAX_G 25.0f
#define GAUSS_CALIB_SAMPLES 16
#define GAUSS_REZERO_ON_FIELD_REMOVE 1

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

// --- Live manual mode (RPM from encoder during winding) ---
#define LIVE_RPM_STEP 25
#define LIVE_RPM_START 0
