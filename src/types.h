#pragma once

#include <stdint.h>
#include "config.h"

enum class Language : uint8_t { Polish = 0, English = 1 };

enum class RampType : uint8_t { SCurve = 0, Linear = 1 };

enum class WindDir : uint8_t { CW = 0, CCW = 1 };

enum class AppState : uint8_t {
  Boot,
  BootError,
  MainMenu,
  Settings,
  Language,
  Diagnostics,
  ManualEdit,
  PresetList,
  PresetActions,
  PresetEdit,
  PresetName,
  PresetDeleteConfirm,
  StartConfirm,
  Countdown,
  Winding,
  Paused,
  Complete,
  Aborted,
  ErrorState,
};

enum class WindPhase : uint8_t {
  Idle,
  RampUp,
  Cruise,
  RampDown,
  FinalApproach,
  Pausing,
  Paused,
  Complete,
  Aborted,
  Fault,
};

struct WindingProgram {
  uint32_t targetTurns = DEFAULT_TURNS;
  uint16_t targetRpm = DEFAULT_RPM;
  WindDir direction = WindDir::CW;
  RampType rampUpType = RampType::SCurve;
  uint16_t rampUpMs = DEFAULT_RAMP_UP_MS;
  RampType rampDownType = RampType::SCurve;
  uint16_t rampDownMs = DEFAULT_RAMP_DOWN_MS;
};

inline uint16_t clampRpm(uint32_t rpm) {
  if (rpm == 0) {
    return 0;
  }
  if (rpm < MIN_WINDER_RPM) {
    return MIN_WINDER_RPM;
  }
  if (rpm > MAX_WINDER_RPM) {
    return MAX_WINDER_RPM;
  }
  return static_cast<uint16_t>(rpm);
}

inline uint32_t clampTurns(uint32_t t) {
  if (t < MIN_TURNS) {
    return MIN_TURNS;
  }
  if (t > MAX_TURNS) {
    return MAX_TURNS;
  }
  return t;
}

inline uint16_t clampRampMs(uint32_t ms) {
  if (ms < RAMP_TIME_MIN_MS) {
    return RAMP_TIME_MIN_MS;
  }
  if (ms > RAMP_TIME_MAX_MS) {
    return RAMP_TIME_MAX_MS;
  }
  // Snap to 0.1 s
  ms = (ms / RAMP_TIME_STEP_MS) * RAMP_TIME_STEP_MS;
  if (ms < RAMP_TIME_MIN_MS) {
    return RAMP_TIME_MIN_MS;
  }
  return static_cast<uint16_t>(ms);
}
