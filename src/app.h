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

  uint32_t lastEditMs_ = 0;

  void setState(AppState s);
  void render(uint32_t nowMs);
  void handleInput(uint32_t nowMs);
  void handleBoot(uint32_t nowMs);
  void startCountdown();
  void adjustTurns(int dir, uint32_t nowMs);
  void adjustRpm(int dir, uint32_t nowMs);
  void adjustRampMs(uint16_t& ms, int dir);
  int editStepTurns(uint32_t nowMs) const;
  int editStepRpm(uint32_t nowMs) const;

  void enterManualEdit();
  void enterPresetEditNew();
  void enterPresetEditExisting();
};
