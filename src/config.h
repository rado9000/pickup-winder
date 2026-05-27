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

// 17HS4401 + TMC2209. MS1=MS2=LOW => 1/8 (dla 1/16 ustaw MS na plytce i MICROSTEP 16).
#define MICROSTEP 8
#define STEPS_PER_REV (200 * MICROSTEP)
#define COUNT_STEPS_PER_REV STEPS_PER_REV
#define DIR_CW_LEVEL HIGH

#define MIN_RPM 1
#define MAX_RPM 1500

// Rampa FAS: nizsze przyspieszenie w pasie rezonansu (~400-700 RPM).
#define MOTOR_ACCEL_STEPS_S2 3500
#define MOTOR_ACCEL_LOW_RPM_STEPS_S2 1600
#define MOTOR_LOW_RPM_THRESHOLD 700
// Dlugi liniowy start rampy (mniej „szarpniecia” na wejsciu w obroty).
#define MOTOR_LINEAR_ACCEL_STEPS 3000
#define MOTOR_DIR_SETUP_US 8

// 1 = SpreadCycle + bez interpolacji (tylko przy PIERWSZYM starcie nawijania, ~50 ms).
// Wymaga UART (GP4/GP5, zworka R8). 0 = tylko STEP/DIR (na plytce: SpreadCycle z jumperow).
#define USE_TMC2209_UART 0
#define TMC_UART_TX_PIN 4
#define TMC_UART_RX_PIN 5
#define TMC_UART_BAUD 115200
#define TMC_UART_READ_TIMEOUT_MS 2
#define TMC_DRIVER_ADDRESS 0b00
#define TMC_R_SENSE 0.11f
#define TMC_VERSION_OK 0x21

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
#define WINDING_UI_INTERVAL_MS 750
#define ENCODER_DETENTS_PER_REV 20
#define PREWIND_STEP_INTERVAL_US 800
