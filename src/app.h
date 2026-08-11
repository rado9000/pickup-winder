#pragma once

#include "input.h"
#include "language.h"
#include "motor_controller.h"
#include "presets.h"
#include "servo42.h"
#include "types.h"
#include "ui.h"
#include "winding_controller.h"

class App {
 public:
  void begin();
  void loop();

 private:
  Servo42          servo_;
  MotorController  motor_;
  WindingController winding_;
  Input            input_;
  Ui               ui_;
  PresetStore      presets_;

  AppState  state_      = AppState::Boot;
  Language  lang_       = Language::Polish;
  WindingProgram draft_{};

  // ── Preset name editor ──────────────────────────────────────────
  // Fixed-width buffer: positions 0..PRESET_NAME_LEN-1 hold editable chars
  // (spaces for empty slots), position PRESET_NAME_LEN is permanently '\0'.
  char    nameBuf_[PRESET_NAME_LEN + 1]{};
  int8_t  namePos_  = 0;   // 0 .. PRESET_NAME_LEN-1

  // ── Menu / edit state ───────────────────────────────────────────
  uint8_t menuIndex_  = 0;
  uint8_t menuWindow_ = 0;
  uint8_t editField_  = 0;   // 0-7 in auto-edit
  uint8_t digitPos_   = 0;   // 0 = units, 1 = tens, ...
  uint8_t presetIndex_    = 0;
  uint8_t actionIndex_    = 0;
  bool    deleteYes_      = false;
  bool    editingPreset_  = false;
  uint8_t editingPresetIndex_ = 0;
  bool    saveAsNew_      = false;

  // ── Winding countdown ───────────────────────────────────────────
  int      countdown_    = 0;
  uint32_t countdownAtMs_= 0;

  // ── Timers ─────────────────────────────────────────────────────
  uint32_t lastLcdMs_  = 0;
  uint32_t lastInputMs_= 0;
  uint32_t bootStepMs_ = 0;
  uint8_t  bootStep_   = 0;
  const char* errorLine_ = nullptr;

  // ── TRUE MANUAL MODE state ──────────────────────────────────────
  // Signed user target: positive=CW, negative=CCW, 0=stop.
  // Range: -MAX_WINDER_RPM .. +MAX_WINDER_RPM.
  int16_t  manualTargetSigned_ = 0;

  // Turn limit (0 = unlimited). Set in ManualTurnsSetup before each session.
  uint32_t manualTargetTurns_   = DEFAULT_MANUAL_TURNS;
  uint64_t manualTargetCounts_  = 0;
  bool     manualTargetEnabled_ = false;  // false when turns == 0 (unlimited)
  bool     manualApproachIssued_ = false;

  // Internal FSM for physical motor state during reversal / target stop.
  enum class ManualPhase : uint8_t {
    Idle,            // motor stopped, no command pending
    Running,         // motor commanded in same direction as target
    Braking,         // motor braking toward zero (user direction change / click)
    Reversing,       // waiting for actual RPM ≤ threshold, then switch direction
    TargetBraking,   // turn-limit safety: decelerating toward target
    TargetApproach,  // low-speed relative move to finish remaining counts
  };
  ManualPhase manualPhase_ = ManualPhase::Idle;

  // Current physical direction the motor is running (may differ from target sign).
  WindDir  manualMotorDir_  = WindDir::CW;

  // Accumulated |travel| since entering Manual (CW + CCW combined).
  int64_t  manualEncStart_  = 0;   // encoder at session start
  int64_t  manualEncPrev_   = 0;   // encoder at last poll
  uint64_t manualTravelCounts_ = 0;// absolute accumulated counts

  // Pending safe exit: set on long-press; firmware exits once motor stops.
  bool     manualExitPending_ = false;

  // Dedicated Manual RPM accel confidence (independent of EncSpeed streak).
  int8_t   manualAccelDir_    = 0;
  uint8_t  manualAccelStreak_ = 0;

  uint32_t lastManualCmdMs_  = 0;
  uint32_t lastManualTelMs_  = 0;

  // ── Helpers ─────────────────────────────────────────────────────
  void setState(AppState s);
  void render(uint32_t nowMs);
  void handleInput(uint32_t nowMs);
  void handleBoot(uint32_t nowMs);
  void startCountdown();

  void enterAutoEdit();
  void enterManualTurnsSetup();
  void enterManualMode();
  void tickManualMode(uint32_t nowMs);
  void finishManualTarget();
  bool manualShouldStartTargetBrake(uint64_t remainingCounts, uint16_t actualRpm) const;
  uint64_t manualRemainingCounts() const;

  void enterPresetEditNew();
  void enterPresetEditExisting();
  void savePresetName();

  // Digit editor
  void formatTurnsDigits(char* out, unsigned n, bool blink) const;
  void formatManualTurnsDigits(char* out, unsigned n, bool blink) const;
  void formatRpmDigits(char* out, unsigned n, bool blink) const;
  void formatRampTenthsDigits(char* out, unsigned n, uint16_t ms, bool blink) const;
  void adjustActiveDigit(int dir);
  void adjustManualTurnsDigit(int dir);
  void onEditClick();
  void onManualTurnsSetupClick();
  uint8_t digitsForField(uint8_t field) const;

  // Encoder UI policies
  int manualRpmStep(const EncDetent& det);
  int accelStepName(EncSpeed spd) const;

  // Name buffer helpers
  void initNameBuf(const char* src);
};
