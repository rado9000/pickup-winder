#pragma once

#include <stdint.h>

// =============================================================================
// Pickup Winder — central configuration (ESP32-S3 + MKS SERVO42ES)
// =============================================================================

// --- GPIO (single source of truth) ---
#define PIN_RS485_RX              17
#define PIN_RS485_TX              18
#define PIN_RS485_RE_DE           4

#define PIN_LCD_SDA               8
#define PIN_LCD_SCL               9

#define PIN_ENC_CLK               10
#define PIN_ENC_DT                11
#define PIN_ENC_SW                12

// Future Gauss ADC (DISABLED). GPIO13 is intentionally free.
#define PIN_GAUSS_ADC             1
#define PIN_UNUSED_13             13

#define ENABLE_GAUSS_METER        0

// --- LCD ---
#define LCD_I2C_ADDR              0x27
#define LCD_COLS                  20
#define LCD_ROWS                  4

// --- SERVO42ES RS485 (manual V1.0.1) ---
#define SERVO_ADDR                0x01
#define SERVO_BAUD                38400
#define SERVO_COUNTS_PER_REV      16384LL   // 0x4000 per manual cmd 0x31
#define SERVO_MAX_HARDWARE_RPM    3000

// Bus closed-loop FOC (cmd 0x82, mode 0x05)
#define SERVO_MODE_BUS_CLOSED_FOC 0x05

// Internal driver ACC (cmd F6 / F4). Manual: Δt per 1 RPM = (256-acc)*50 µs.
// High ACC ≈ driver tracks ESP32 software ramp closely without distorting it.
// ACC=250 → 300 µs / RPM step. Tune on hardware if tracking lags.
#define SERVO_INTERNAL_ACC        250

// Soft / quick stop ACC for F6 stop (acc≠0 decelerates; acc=0 = immediate)
#define SERVO_SOFT_STOP_ACC       200
#define SERVO_QUICK_STOP_ACC      240

// Heartbeat protection (cmd 0x89), ms. 0 = off. Active only while winding.
#define SERVO_HEARTBEAT_MS        2000

#define SERVO_RESPONSE_TIMEOUT_MS 150
#define SERVO_COMM_RETRIES        5
#define SERVO_POS_LOSS_FAULT_MS   800

// --- Winder limits ---
#define MAX_WINDER_RPM            2500
#define MIN_WINDER_RPM            1
#define MAX_TURNS                 99999UL
#define MIN_TURNS                 1UL

#define DEFAULT_TURNS             8000UL
#define DEFAULT_RPM               1000
#define DEFAULT_RAMP_UP_MS        2000
#define DEFAULT_RAMP_DOWN_MS      3000
#define RAMP_TIME_MIN_MS          100
#define RAMP_TIME_MAX_MS          20000
#define RAMP_TIME_STEP_MS         100

// --- Final approach / stop tuning (encoder counts) ---
// Start FINAL_APPROACH this many counts early (beyond predicted stop distance).
#define STOP_COMPENSATION_COUNTS  512

// Relative-coordinate (F4) approach speed.
#define FINAL_APPROACH_RPM        40

// Accept target when |remaining| <= this many counts (~0.03 rev).
#define FINAL_POSITION_TOLERANCE_COUNTS  64

// If remaining after ramp-down is tiny, skip approach and declare complete.
#define FINAL_APPROACH_SKIP_COUNTS  32

// --- Scheduler intervals (ms) ---
#define INPUT_POLL_MS             1
#define MOTOR_COMMAND_UPDATE_MS  30
#define POSITION_POLL_MS          20
#define RPM_POLL_MS               100
#define STATUS_POLL_MS            200
#define LCD_UPDATE_MS             150
#define DEBUG_LOG_MS              500

// --- UI / input ---
#define BUTTON_LONG_PRESS_MS      900
#define PRESET_MAX_COUNT          32
#define PRESET_NAME_LEN           12
#define COUNTDOWN_SECONDS         3

// KY-040 / 20 PPR mechanical encoder
#define ENC_DEBOUNCE_US           2500

#define TURNS_DIGITS              5
#define RPM_DIGITS                4
#define RAMP_TENTHS_DIGITS        3   // 0.1..20.0 s as 001..200 tenths

