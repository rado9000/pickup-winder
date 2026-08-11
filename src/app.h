#pragma once

#include "input.h"
#include "language.h"
#include "motor_controller.h"
#include "presets.h"
#include "servo42.h"
#include "turn_counter.h"
#include "types.h"
#include "ui.h"
#include "winding_controller.h"

class App {
 public:
  void begin();
  void loop();

 private:
  Servo42 servo_;
  MotorController motor_;
  WindingController winding_;
  Input input_;
  Ui ui_;
  PresetStore presets_;

  AppState state_ = AppState::Boot;
  Language lang_ = Language::Polish;
  WindingProgram draft_{};
  char nameBuf_[PRESET_NAME_LEN + 1] = "PRESET";
  int8_t namePos_ = 0;

  uint8_t menuIndex_ = 0;
  uint8_t menuWindow_ = 0;
  uint8_t editField_ = 0;
  uint8_t digitPos_ = 0;
  uint8_t presetIndex_ = 0;
  uint8_t actionIndex_ = 0;
  bool deleteYes_ = false;
  bool editingPreset_ = false;
  uint8_t editingPresetIndex_ = 0;
  bool saveAsNew_ = false;

  int countdown_ = 0;
  uint32_t countdownAtMs_ = 0;
  uint32_t lastLcdMs_ = 0;
  uint32_t lastInputMs_ = 0;
  uint32_t bootStepMs_ = 0;
  uint8_t bootStep_ = 0;
  const char* errorLine_ = nullptr;

  // ── True Manual mode state ──────────────────────────────────────
  uint16_t manualSetRpm_ = 0;        // commanded RPM
  WindDir manualDir_ = WindDir::CW;
  bool manualRunning_ = false;       // motor commanded at >0 RPM
  bool manualStopping_ = false;      // stop requested, waiting for actual=0
  int64_t manualEncBaseline_ = 0;    // encoder at manual session start
  TurnCounter manualTurns_;
  uint32_t lastManualCmdMs_ = 0;
  uint32_t lastManualTelMs_ = 0;

  void setState(AppState s);
  void render(uint32_t nowMs);
  void handleInput(uint32_t nowMs);
  void handleBoot(uint32_t nowMs);
  void startCountdown();

  void enterAutoEdit();
  void enterManualMode();
  void tickManualMode(uint32_t nowMs);
  void exitManualMode(bool immediate);
  void manualChangeDirection(WindDir newDir);

  void enterPresetEditNew();
  void enterPresetEditExisting();

  // ── Digit editor helpers ────────────────────────────────────────
  void formatTurnsDigits(char* out, unsigned n, bool blink) const;
  void formatRpmDigits(char* out, unsigned n, bool blink) const;
  void formatRampTenthsDigits(char* out, unsigned n, uint16_t ms, bool blink) const;
  void adjustActiveDigit(int dir);
  void onEditClick();
  uint8_t digitsForField(uint8_t field) const;

  // ── Encoder acceleration helpers ───────────────────────────────
  int accelStepMenu(EncSpeed spd) const;
  int accelStepDigit(EncSpeed spd) const;
  int accelStepManualRpm(EncSpeed spd) const;
  int accelStepName(EncSpeed spd) const;
};
