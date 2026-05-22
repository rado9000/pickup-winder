#pragma once

// --- Pins (RP2040 / Pico) ---
#define I2C_SDA_PIN 0
#define I2C_SCL_PIN 1
#define STEP_PIN 2
#define DIR_PIN 3
#define TMC_UART_TX_PIN 4
#define TMC_UART_RX_PIN 5
#define ENC_A_PIN 6
#define ENC_B_PIN 7
#define ENC_BTN_PIN 8
#define A3144_PIN 9
#define EN_PIN 10
#define HALL_ADC_PIN 26

#define LCD_I2C_ADDRESS 0x27
#define LCD_COLS 20
#define LCD_ROWS 4

// --- Motor / driver ---
// 1 = konfiguracja prądu i mikrokroku przez UART (jak w działającym starym projekcie)
#define USE_TMC_UART 1
// 0 = TMC2208, 1 = TMC2209 (stary projekt)
#define USE_TMC2209 0

#define MICROSTEP 8
#define STEPS_PER_REV (200 * MICROSTEP)
#define COUNT_STEPS_PER_REV STEPS_PER_REV

// CW: stary projekt używa HIGH; jeśli kierunek jest odwrotny, zmień na 0
#define DIR_CW_LEVEL HIGH

#define MIN_RPM 1
#define MAX_RPM_USER 1500
#define TMC_R_SENSE 0.11f
#define TMC_RUN_CURRENT_MA 1200
#define TMC_UART_ADDRESS 0

// Kroki: analogWrite (działa na earlephilhower) zamiast PWM slice na GP2
#define USE_ANALOG_STEP 1

#define USE_SOFT_START 1
#define USE_SOFT_STOP 1
#define RAMP_MIN_MS 2500
#define RAMP_MAX_MS 6000
#define RAMP_BASE_MS 1500
#define RAMP_MS_PER_RPM 2

// --- A3144: magnes diametryczny na osi (jedno zbocze S→N na obrót) ---
#define A3144_PULSES_PER_REV 1
#define A3144_DEBOUNCE_US 2500
// FALLING = przejście w stan aktywny (LOW przy pull-up)
#define A3144_COUNT_ON_FALLING 1
// Minimalny odstęp między impulsami (~połowa okresu przy max RPM)
#define A3144_MIN_INTERVAL_US 15000

// --- Gauss (AH49HZ3) ---
#define USE_GAUSS_MONITOR 1
#define HALL_ADC_REF_V 3.3f
#define HALL_ADC_MAX 4095
#define HALL_ZERO_V (HALL_ADC_REF_V / 2.0f)
#define HALL_MV_PER_GAUSS 0.92f
#define GAUSS_ENTER_THRESHOLD 50.0f
#define GAUSS_EXIT_THRESHOLD 50.0f
#define GAUSS_ENTER_HOLD_MS 250
#define GAUSS_EXIT_HOLD_MS 250
#define GAUSS_CALIB_SAMPLES 64

// --- Presets / UI ---
#define MAX_PRESETS 32
#define MAX_TURNS 99999L
#define TURN_DIGITS 5
#define RPM_DIGITS 4
#define PRESET_NAME_LEN 12
#define EEPROM_SIZE 1024
#define LONG_PRESS_MS 800
#define BLINK_MS 500
#define WINDING_UI_INTERVAL_MS 200
#define ENCODER_DETENTS_PER_REV 20
#define PREWIND_STEP_INTERVAL_US 800
#define PREWIND_STEP_PULSE_US 6
