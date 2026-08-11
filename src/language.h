#pragma once

#include "types.h"

enum class StrId : uint8_t {
  AppTitle,
  Initializing,
  SystemReady,
  Auto,           // renamed from Manual
  ManualMode,     // new TRUE manual mode
  Presets,
  Settings,
  Language,
  Diagnostics,
  Back,
  Turns,
  Rpm,
  RampUp,
  RampDown,
  UpTime,
  DownTime,
  Direction,
  Start,
  SCurve,
  Linear,
  Cw,
  Ccw,
  ClickStart,
  ClickAgain,
  HoldBack,
  HoldPause,
  ClickGoHoldStop,
  Paused,
  Complete,
  Stopped,
  Run,
  NewPreset,
  Save,
  Rename,
  Edit,
  Delete,
  ConfirmDelete,
  Yes,
  No,
  MotorError,
  NoRs485,
  ClickRetry,
  Polski,
  English,
  MotorOk,
  Rs485Ok,
  Firmware,
  HoldStop,
  Cancel,
  // Manual mode specific
  ManualSet,
  ManualAct,
  ManualTurns,
  ManualClickDir,
  ManualClickStop,
  ManualHoldBack,
  COUNT
};

const char* tr(Language lang, StrId id);
void formatRampType(Language lang, RampType t, char* out, int n);
void formatDir(Language lang, WindDir d, char* out, int n);
