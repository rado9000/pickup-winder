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

// Future Gauss ADC (DISABLED). GPIO13 intentionally free.
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

// Internal driver ACC. High value → driver tracks ESP32 ramp closely.
// Manual: Δt per 1 RPM step = (256-acc)*50 µs. ACC=250 → 300 µs/step.
#define SERVO_INTERNAL_ACC        250

// Soft / quick stop ACC
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
#define DEFAULT_MANUAL_TURNS      8000UL  // Manual turn-limit setup default
#define DEFAULT_RPM               1000
#define DEFAULT_RAMP_UP_MS        2000
#define DEFAULT_RAMP_DOWN_MS      3000
// Predicted Manual target-stop distance uses this ramp-down time with
// Ramp::stoppingTurns(), then soft-stop + final approach (same idea as Auto).
#define MANUAL_TARGET_RAMP_DOWN_MS  DEFAULT_RAMP_DOWN_MS
#define RAMP_TIME_MIN_MS          100
#define RAMP_TIME_MAX_MS          20000
#define RAMP_TIME_STEP_MS         100

// --- Final approach / stop tuning (encoder counts) ---
#define STOP_COMPENSATION_COUNTS  512
#define FINAL_APPROACH_RPM        40
#define FINAL_POSITION_TOLERANCE_COUNTS  64
#define FINAL_APPROACH_SKIP_COUNTS  32

// --- Scheduler intervals (ms) ---
#define INPUT_POLL_MS             1
#define MOTOR_COMMAND_UPDATE_MS   30
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

// --- KY-040 / 20 PPR mechanical encoder ---
// Debounce: minimum µs between accepted CLK edges.
// 4 ms suppresses contact bounce without missing slow detents.
#define ENC_DEBOUNCE_US           4000

// Invert logical detent direction once at the input layer.
// 1 = CW increases values / moves down menus; 0 = raw Gray-code sense.
#define ENCODER_INVERT_DIRECTION  1

// Velocity acceleration — inter-detent timing thresholds (ms).
// Used by menus / names / lists (NOT Manual RPM — see MANUAL_ACCEL_*).
// >ENC_SLOW_THRESHOLD_MS  → SLOW (x1); idle timeout counts as SLOW.
// 80-150 ms               → MEDIUM
// 40-80 ms                → FAST
// <40 ms                  → VERY_FAST
#define ENC_ACCEL_RESET_MS        500   // idle longer than this → reset streak+speed
#define ENC_SLOW_THRESHOLD_MS     150
#define ENC_MEDIUM_THRESHOLD_MS    80
#define ENC_FAST_THRESHOLD_MS      40

// Number of consecutive same-direction detents in a fast band before
// acceleration activates. Prevents a single quick bump from accelerating.
#define ENC_ACCEL_STREAK_REQUIRED   3

// Serial debug for encoder (set 1 to enable, 0 for silent)
#define ENC_DEBUG                 0

// --- Acceleration policy per context ---
// Menus: always x1 (short menus). Only preset-list may use mild accel.
#define MENU_ACCEL_NONE           0   // use 0 = disabled, 1 = enabled
#define PRESET_LIST_ACCEL_MAX     2   // max step in long preset list
#define NAME_ACCEL_MAX            2   // max chars per detent in name editor

// --- Digit editor ---
#define TURNS_DIGITS              5
#define RPM_DIGITS                4
#define RAMP_TENTHS_DIGITS        3

// --- Manual mode — dedicated human-hand RPM acceleration ---
// Tuned for finger rotation of a 20-detent mechanical encoder.
// Independent of ENC_* menu/name classifier thresholds.
//
//   SLOW:       dt >= MANUAL_ACCEL_SLOW_MS     → 1 RPM / detent
//   NORMAL:     MANUAL_ACCEL_NORMAL_MS .. SLOW → 5 RPM / detent
//   FAST:       MANUAL_ACCEL_FAST_MS .. NORMAL → 20 RPM / detent
//   VERY FAST: dt <  MANUAL_ACCEL_FAST_MS     → 50 RPM / detent
#define MANUAL_ACCEL_SLOW_MS          170
#define MANUAL_ACCEL_NORMAL_MS        100
#define MANUAL_ACCEL_FAST_MS           60

#define MANUAL_RPM_STEP_SLOW            1
#define MANUAL_RPM_STEP_NORMAL          5
#define MANUAL_RPM_STEP_FAST           20
#define MANUAL_RPM_STEP_VERY_FAST      50

// Consecutive same-direction non-slow detents before Manual accel engages.
#define MANUAL_ACCEL_STREAK_REQUIRED    2

// Minimum actual RPM considered "stopped" for direction-change safety.
#define MANUAL_STOPPED_RPM        8

// How often to push new speed command in manual mode (ms).
#define MANUAL_COMMAND_UPDATE_MS  40
