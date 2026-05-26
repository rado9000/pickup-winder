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

// Silnik: 17HS4401 NEMA17, 1.8 deg, 200 krokow/obrot (cewka 1.5 Ohm)
// Kroki na obrót silnika = 200 * MICROSTEP. Musi zgadzać się z MS1/MS2 na module
// (MS3 na tym module nie jest używany — zostaw LOW).
// MS1  MS2  => microstep  => MICROSTEP
// LOW  LOW  => 1/8        => 8   (obecne okablowanie)
// LOW  HIGH => 1/4        => 4
// HIGH LOW  => 1/2        => 2
// HIGH HIGH => 1/16       => 16
#define MICROSTEP 8
#define STEPS_PER_REV (200 * MICROSTEP)
#define COUNT_STEPS_PER_REV STEPS_PER_REV
#define DIR_CW_LEVEL HIGH

#define MIN_RPM 1
#define MAX_RPM_A3144 2000
#define MAX_RPM_USER MAX_RPM_A3144

#define USE_SOFT_START 1
#define USE_SOFT_STOP 0

// Rampa schodkowa: kazdy prog RPM + postoj (hold) — bez szarpniecia na koncu segmentu
#define RAMP_LADDER_TABLE \
  { 50, 100, 150, 200, 250, 300, 350, 400, 450, 500, 600, 700, 800, 900, 1000 }
#define RAMP_SEG_BASE_MS 2500
#define RAMP_SEG_MS_PER_RPM 45
#define RAMP_SEG_MIN_MS 2000
#define RAMP_SEG_MAX_MS 12000
#define RAMP_HOLD_MS 2500

// STEP jak w dzialajacym starym softie (analogWriteFreq + niski duty)
#define STEP_PWM_DUTY 64
#define PREWIND_PULSE_US 5

#define A3144_PULSES_PER_REV 1
#define A3144_COUNT_ON_FALLING 1
#define A3144_PERIOD_US(rpm) (60000000UL / (uint32_t)(rpm))
#define A3144_DEBOUNCE_US 800
#define A3144_MIN_INTERVAL_US ((A3144_PERIOD_US(MAX_RPM_A3144) * 45UL) / 100UL)

// Testowo wyłączone — włącz z powrotem ustawiając na 1
#define USE_GAUSS_MONITOR 0
#define HALL_ADC_REF_V 3.3f
#define HALL_ADC_MAX 4095
#define HALL_ZERO_V (HALL_ADC_REF_V / 2.0f)
#define HALL_MV_PER_GAUSS 0.92f
#define GAUSS_ENTER_THRESHOLD 50.0f
#define GAUSS_EXIT_THRESHOLD 50.0f
#define GAUSS_ENTER_HOLD_MS 250
#define GAUSS_EXIT_HOLD_MS 250
#define GAUSS_CALIB_SAMPLES 10

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
