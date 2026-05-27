#pragma once

#define I2C_SDA_PIN 0
#define I2C_SCL_PIN 1
#define STEP_PIN 2
#define DIR_PIN 3
#define ENC_A_PIN 6
#define ENC_B_PIN 7
#define ENC_BTN_PIN 8
#define A3144_PIN 9
#define EN_PIN 10
#define HALL_ADC_PIN 26

#define LCD_I2C_ADDRESS 0x27
#define LCD_COLS 20
#define LCD_ROWS 4

// 17HS4401 + TMC2209 MS1=MS2=LOW => 1/8 microstep
#define MICROSTEP 8
#define STEPS_PER_REV (200 * MICROSTEP)
#define COUNT_STEPS_PER_REV STEPS_PER_REV
#define DIR_CW_LEVEL HIGH

#define MIN_RPM 1
#define MAX_RPM 1500

// Jedna stala rampa FastAccelStepper (kroki/s^2). ~8 s do 1500 RPM — plynnie.
#define MOTOR_ACCEL_STEPS_S2 4000
#define MOTOR_DIR_SETUP_US 5

#define A3144_PULSES_PER_REV 1
#define A3144_COUNT_ON_FALLING 1
#define A3144_PERIOD_US(rpm) (60000000UL / (uint32_t)(rpm))
#define A3144_DEBOUNCE_US 800
#define A3144_MIN_INTERVAL_US ((A3144_PERIOD_US(MAX_RPM) * 45UL) / 100UL)

#define USE_GAUSS_MONITOR 0

#define MAX_PRESETS 32
#define MAX_TURNS 99999L
#define TURN_DIGITS 5
#define RPM_DIGITS 4
#define PRESET_NAME_LEN 12
#define EEPROM_SIZE 1024
#define LONG_PRESS_MS 800
#define BLINK_MS 500
#define WINDING_UI_INTERVAL_MS 500
#define ENCODER_DETENTS_PER_REV 20
#define PREWIND_STEP_INTERVAL_US 800
