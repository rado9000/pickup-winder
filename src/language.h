#pragma once

#include "types.h"

enum class StrId : uint8_t {
  AppTitle,
  Initializing,
  SystemReady,
  Auto,              // menu item — "AUTOMATYCZNY" / "AUTO"
  AutoTitle,         // screen header — "TRYB AUTOMATYCZNY" / "AUTO MODE"
  ManualMode,        // menu item — "RECZNY" / "MANUAL"
  ManualTitle,       // screen header — "TRYB RECZNY" / "MANUAL"
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
  Stop,              // used for Manual at zero RPM
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
  PresetName,        // "NAZWA PRESETU" / "PRESET NAME"
  RotClickNext,      // "OBROT/KLIK DALEJ" / "ROT/CLICK NEXT"
  HoldSave,          // "HOLD=ZAPISZ" / "HOLD=SAVE"
  ManualSet,
  ManualAct,
  ManualTurns,
  ManualClickStop,   // hint while running
  ManualHoldBack,    // hint while stopped (long-press = exit)
  ManualTurnLimit,   // "LIMIT ZWOJOW" / "TURN LIMIT"
  Unlimited,         // "BEZ LIMITU" / "UNLIMITED"
  MagnetMeasurement, // "POMIAR MAGNESU" / "GAUSS METER"
  MagnetStrength,    // "SILA MAGNESU" / "MAGNET STRENGTH"
  MagnetPole,        // "BIEGUN" / "POLE"
  GaussCalibration,  // "KALIBRACJA GAUSSA" / "GAUSS CALIBRATION"
  RemoveMagnet,      // "USUN MAGNES" / "REMOVE MAGNET"
  GaussZeroOk,       // "ZERO OK" / "ZERO OK"
  ZeroGauss,         // settings — "ZERO GAUSSA" / "ZERO GAUSS"
  COUNT
};

const char* tr(Language lang, StrId id);
void formatRampType(Language lang, RampType t, char* out, int n);
void formatDir(Language lang, WindDir d, char* out, int n);
