#include "app.h"
#include "config.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

void App::setState(AppState s) {
  state_ = s;
  lastLcdMs_ = 0;
}

void App::begin() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("[BOOT] ESP32-S3 clean-sheet winder"));

  ui_.begin();
  ui_.drawBootProgress(5);
  input_.begin();
  presets_.begin();
  lang_ = presets_.loadLanguage();
  servo_.begin();
  motor_.begin(&servo_);
  winding_.begin(&motor_);

  draft_ = WindingProgram{};
  bootStep_ = 0;
  bootStepMs_ = millis();
  setState(AppState::Boot);
}

void App::enterManualEdit() {
  draft_ = WindingProgram{};
  editField_ = 0;
  digitPos_ = 0;
  editingPreset_ = false;
  saveAsNew_ = false;
  setState(AppState::ManualEdit);
}

void App::enterPresetEditNew() {
  draft_ = WindingProgram{};
  editField_ = 0;
  digitPos_ = 0;
  editingPreset_ = false;
  saveAsNew_ = true;
  setState(AppState::PresetEdit);
}

void App::enterPresetEditExisting() {
  PresetRecord p{};
  if (!presets_.get(presetIndex_, p)) {
    setState(AppState::PresetList);
    return;
  }
  draft_ = p.program;
  strncpy(nameBuf_, p.name, PRESET_NAME_LEN);
  nameBuf_[PRESET_NAME_LEN] = 0;
  editField_ = 0;
  digitPos_ = 0;
  editingPreset_ = true;
  editingPresetIndex_ = presetIndex_;
  saveAsNew_ = false;
  setState(AppState::PresetEdit);
}

void App::startCountdown() {
  countdown_ = COUNTDOWN_SECONDS;
  countdownAtMs_ = millis();
  setState(AppState::Countdown);
}

uint8_t App::digitsForField(uint8_t field) const {
  switch (field) {
    case 0:
      return TURNS_DIGITS;
    case 1:
      return RPM_DIGITS;
    case 3:
    case 5:
      return RAMP_TENTHS_DIGITS;
    default:
      return 0;
  }
}

void App::formatTurnsDigits(char* out, unsigned n, bool blink) const {
  snprintf(out, n, "%05lu", static_cast<unsigned long>(draft_.targetTurns));
  if (blink && editField_ == 0 && ((millis() / 400) & 1)) {
    const int idx = TURNS_DIGITS - 1 - digitPos_;
    if (idx >= 0 && idx < TURNS_DIGITS && static_cast<unsigned>(idx) + 1 < n) {
      out[idx] = '_';
    }
  }
}

void App::formatRpmDigits(char* out, unsigned n, bool blink) const {
  snprintf(out, n, "%04u", draft_.targetRpm);
  if (blink && editField_ == 1 && ((millis() / 400) & 1)) {
    const int idx = RPM_DIGITS - 1 - digitPos_;
    if (idx >= 0 && idx < RPM_DIGITS && static_cast<unsigned>(idx) + 1 < n) {
      out[idx] = '_';
    }
  }
}

void App::formatRampTenthsDigits(char* out, unsigned n, uint16_t ms, bool blink) const {
  snprintf(out, n, "%0.1fs", ms / 1000.0f);
  if (blink && ((millis() / 400) & 1)) {
    int idx = -1;
    if (digitPos_ == 0) {
      idx = 3;
    } else if (digitPos_ == 1) {
      idx = 1;
    } else if (digitPos_ == 2) {
      idx = 0;
    }
    if (idx >= 0 && static_cast<unsigned>(idx) + 1 < n) {
      out[idx] = '_';
    }
  }
}

void App::adjustActiveDigit(int dir) {
  auto bumpDigit = [&](uint32_t& value, uint8_t place /*0=units*/, uint32_t maxVal) {
    uint32_t placeVal = 1;
    for (uint8_t i = 0; i < place; i++) {
      placeVal *= 10;
    }
    int32_t digit = static_cast<int32_t>((value / placeVal) % 10);
    digit += dir;
    if (digit > 9) {
      digit = 0;
    }
    if (digit < 0) {
      digit = 9;
    }
    value = (value / (placeVal * 10)) * (placeVal * 10) + (value % placeVal) +
            static_cast<uint32_t>(digit) * placeVal;
    if (value > maxVal) {
      value = maxVal;
    }
    if (value < 1 && maxVal >= 1) {
      value = 1;
    }
  };

  if (editField_ == 0) {
    uint32_t v = draft_.targetTurns;
    bumpDigit(v, digitPos_, MAX_TURNS);
    draft_.targetTurns = clampTurns(v);
  } else if (editField_ == 1) {
    uint32_t v = draft_.targetRpm;
    bumpDigit(v, digitPos_, MAX_WINDER_RPM);
    draft_.targetRpm = clampRpm(v);
    if (draft_.targetRpm < MIN_WINDER_RPM) {
      draft_.targetRpm = MIN_WINDER_RPM;
    }
  } else if (editField_ == 3 || editField_ == 5) {
    uint16_t& ms = (editField_ == 3) ? draft_.rampUpMs : draft_.rampDownMs;
    uint32_t tenths = ms / 100;
    bumpDigit(tenths, digitPos_, RAMP_TIME_MAX_MS / 100);
    if (tenths < RAMP_TIME_MIN_MS / 100) {
      tenths = RAMP_TIME_MIN_MS / 100;
    }
    ms = clampRampMs(tenths * 100);
  } else if (editField_ == 2) {
    draft_.rampUpType =
        (draft_.rampUpType == RampType::SCurve) ? RampType::Linear : RampType::SCurve;
  } else if (editField_ == 4) {
    draft_.rampDownType =
        (draft_.rampDownType == RampType::SCurve) ? RampType::Linear : RampType::SCurve;
  } else if (editField_ == 6) {
    draft_.direction = (draft_.direction == WindDir::CW) ? WindDir::CCW : WindDir::CW;
  }
}

void App::onEditClick() {
  const uint8_t digs = digitsForField(editField_);
  if (digs > 0) {
    digitPos_++;
    if (digitPos_ >= digs) {
      digitPos_ = 0;
      if (editField_ < 7) {
        editField_++;
      }
    }
    return;
  }

  // Non-digit fields: click advances
  if (editField_ < 7) {
    editField_++;
    digitPos_ = 0;
    return;
  }

  // Start / Save
  if (state_ == AppState::PresetEdit) {
    namePos_ = 0;
    if (!editingPreset_) {
      strncpy(nameBuf_, "PRESET", PRESET_NAME_LEN);
      nameBuf_[PRESET_NAME_LEN] = 0;
    }
    setState(AppState::PresetName);
  } else {
    setState(AppState::StartConfirm);
  }
}

void App::handleBoot(uint32_t nowMs) {
  if (nowMs - bootStepMs_ < 300) {
    return;
  }
  bootStepMs_ = nowMs;
  bootStep_++;
  ui_.drawBootProgress(static_cast<uint8_t>(bootStep_ * 10));

  if (bootStep_ == 2) {
    Serial.println(F("[BOOT] waiting for motor bus..."));
  }
  if (bootStep_ == 4) {
    Serial.println(F("[BOOT] probing SERVO42ES"));
    if (!motor_.detect()) {
      Serial.println(F("[BOOT] SERVO42ES NOT FOUND"));
      errorLine_ = "NO RS485 RESPONSE";
      setState(AppState::BootError);
      return;
    }
    Serial.println(F("[MOTOR] SERVO42ES FOUND"));
    Serial.printf("[MOTOR] ENC=%lld RPM=%d AL=%u\n", static_cast<long long>(motor_.encoder()),
                  motor_.actualRpmSigned(), motor_.alarmStatus());
  }
  if (bootStep_ >= 7) {
    ui_.setLine(0, tr(lang_, StrId::AppTitle));
    ui_.setLine(1, tr(lang_, StrId::SystemReady));
    ui_.setLine(2, "");
    ui_.setLine(3, "");
    delay(300);
    menuIndex_ = 0;
    menuWindow_ = 0;
    setState(AppState::MainMenu);
  }
}

static void clampMenuWindow(uint8_t selected, uint8_t count, uint8_t& window) {
  if (count == 0) {
    window = 0;
    return;
  }
  if (selected < window) {
    window = selected;
  }
  if (selected >= window + 4) {
    window = selected - 3;
  }
  if (count <= 4) {
    window = 0;
  }
}

void App::render(uint32_t nowMs) {
  if (nowMs - lastLcdMs_ < LCD_UPDATE_MS && state_ != AppState::Countdown &&
      state_ != AppState::ManualEdit && state_ != AppState::PresetEdit) {
    return;
  }
  // Digit blink needs faster refresh while editing numbers
  if ((state_ == AppState::ManualEdit || state_ == AppState::PresetEdit) &&
      nowMs - lastLcdMs_ < 100) {
    return;
  }
  lastLcdMs_ = nowMs;

  switch (state_) {
    case AppState::MainMenu: {
      const char* items[3] = {tr(lang_, StrId::Manual), tr(lang_, StrId::Presets),
                              tr(lang_, StrId::Settings)};
      clampMenuWindow(menuIndex_, 3, menuWindow_);
      ui_.drawMenu(lang_, items, 3, menuIndex_, menuWindow_);
      break;
    }
    case AppState::Settings: {
      const char* items[3] = {tr(lang_, StrId::Language), tr(lang_, StrId::Diagnostics),
                              tr(lang_, StrId::Back)};
      clampMenuWindow(menuIndex_, 3, menuWindow_);
      ui_.drawMenu(lang_, items, 3, menuIndex_, menuWindow_);
      break;
    }
    case AppState::Language: {
      const char* items[2] = {tr(lang_, StrId::Polski), tr(lang_, StrId::English)};
      ui_.drawMenu(lang_, items, 2, menuIndex_, 0);
      break;
    }
    case AppState::Diagnostics: {
      motor_.pollTelemetry(nowMs);
      ui_.drawDiagnostics(lang_, motor_.alarmOk(), motor_.encoderOk() || servo_.failStreak() == 0,
                          motor_.actualRpmAbs(), motor_.encoder(), motor_.alarmStatus());
      break;
    }
    case AppState::ManualEdit:
    case AppState::PresetEdit: {
      const uint8_t fieldCount = 8;
      uint8_t top = (editField_ < 3) ? 0 : static_cast<uint8_t>(editField_ - 2);
      if (top > fieldCount - 4) {
        top = fieldCount - 4;
      }
      for (uint8_t row = 0; row < 4; row++) {
        const uint8_t f = static_cast<uint8_t>(top + row);
        char val[16];
        const char* label = "";
        const bool sel = (editField_ == f);
        switch (f) {
          case 0:
            label = tr(lang_, StrId::Turns);
            formatTurnsDigits(val, sizeof val, sel);
            break;
          case 1:
            label = tr(lang_, StrId::Rpm);
            formatRpmDigits(val, sizeof val, sel);
            break;
          case 2:
            label = tr(lang_, StrId::RampUp);
            formatRampType(lang_, draft_.rampUpType, val, sizeof val);
            break;
          case 3:
            label = tr(lang_, StrId::UpTime);
            formatRampTenthsDigits(val, sizeof val, draft_.rampUpMs, sel);
            break;
          case 4:
            label = tr(lang_, StrId::RampDown);
            formatRampType(lang_, draft_.rampDownType, val, sizeof val);
            break;
          case 5:
            label = tr(lang_, StrId::DownTime);
            formatRampTenthsDigits(val, sizeof val, draft_.rampDownMs, sel);
            break;
          case 6:
            label = tr(lang_, StrId::Direction);
            formatDir(lang_, draft_.direction, val, sizeof val);
            break;
          default:
            label = (state_ == AppState::PresetEdit) ? tr(lang_, StrId::Save) : tr(lang_, StrId::Start);
            val[0] = 0;
            break;
        }
        char line[21];
        if (f == 7) {
          snprintf(line, sizeof line, "%c%s", sel ? '>' : ' ', label);
        } else {
          snprintf(line, sizeof line, "%c%s:%s", sel ? '>' : ' ', label, val);
        }
        ui_.setLine(row, line);
      }
      break;
    }
    case AppState::PresetList: {
      // +NEW, presets..., BACK
      const uint8_t total = static_cast<uint8_t>(presets_.count() + 2);
      char names[34][21];
      const char* items[34];
      snprintf(names[0], sizeof names[0], "%s", tr(lang_, StrId::NewPreset));
      items[0] = names[0];
      for (uint8_t i = 0; i < presets_.count(); i++) {
        PresetRecord p{};
        if (presets_.get(i, p)) {
          snprintf(names[i + 1], sizeof names[i + 1], "%s", p.name);
        } else {
          snprintf(names[i + 1], sizeof names[i + 1], "?");
        }
        items[i + 1] = names[i + 1];
      }
      snprintf(names[presets_.count() + 1], sizeof names[0], "%s", tr(lang_, StrId::Back));
      items[presets_.count() + 1] = names[presets_.count() + 1];
      clampMenuWindow(menuIndex_, total, menuWindow_);
      ui_.drawMenu(lang_, items, total, menuIndex_, menuWindow_);
      break;
    }
    case AppState::PresetActions: {
      const char* items[5] = {tr(lang_, StrId::Start), tr(lang_, StrId::Edit),
                              tr(lang_, StrId::Rename), tr(lang_, StrId::Delete),
                              tr(lang_, StrId::Back)};
      clampMenuWindow(actionIndex_, 5, menuWindow_);
      ui_.drawMenu(lang_, items, 5, actionIndex_, menuWindow_);
      break;
    }
    case AppState::PresetName: {
      char line[21];
      snprintf(line, sizeof line, "NAME:%s", nameBuf_);
      if (namePos_ >= 0 && namePos_ < PRESET_NAME_LEN) {
        const size_t pos = 5 + static_cast<size_t>(namePos_);
        if (pos < 20 && ((nowMs / 400) & 1)) {
          line[pos] = '_';
        }
      }
      ui_.setLine(0, tr(lang_, StrId::Rename));
      ui_.setLine(1, line);
      ui_.setLine(2, "ROT=CHAR CLICK=NEXT");
      ui_.setLine(3, "HOLD=SAVE");
      break;
    }
    case AppState::PresetDeleteConfirm: {
      ui_.setLine(0, tr(lang_, StrId::ConfirmDelete));
      char line[21];
      snprintf(line, sizeof line, "%c%s  %c%s", deleteYes_ ? '>' : ' ', tr(lang_, StrId::Yes),
               !deleteYes_ ? '>' : ' ', tr(lang_, StrId::No));
      ui_.setLine(1, line);
      ui_.setLine(2, "");
      ui_.setLine(3, tr(lang_, StrId::HoldBack));
      break;
    }
    case AppState::StartConfirm:
      ui_.drawStartConfirm(lang_, draft_);
      break;
    case AppState::Countdown:
      ui_.drawCountdown(countdown_);
      break;
    case AppState::Winding:
      ui_.drawRun(lang_, winding_.status());
      break;
    case AppState::Paused:
      ui_.drawPaused(lang_, winding_.status());
      break;
    case AppState::Complete:
      ui_.drawComplete(lang_, winding_.status());
      break;
    case AppState::Aborted:
      ui_.drawAborted(lang_, winding_.status());
      break;
    case AppState::BootError:
    case AppState::ErrorState:
      ui_.drawError(lang_, tr(lang_, StrId::MotorError),
                    errorLine_ ? errorLine_ : tr(lang_, StrId::NoRs485));
      break;
    default:
      break;
  }
}

void App::handleInput(uint32_t nowMs) {
  const InputEvent ev = input_.takeEvent();
  int delta = input_.takeDelta();
  // Drain extra detents this frame as separate steps for menus (max 2)
  int steps = 0;
  if (delta != 0) {
    steps = delta > 0 ? 1 : -1;
  }
  while (true) {
    const int more = input_.takeDelta();
    if (more == 0) {
      break;
    }
    // ignore extras — one detent per poll already coalesced
    break;
  }
  (void)nowMs;

  auto onRotate = [&](int dir) {
    switch (state_) {
      case AppState::MainMenu:
        menuIndex_ = static_cast<uint8_t>((menuIndex_ + (dir > 0 ? 1 : 2)) % 3);
        break;
      case AppState::Settings:
        menuIndex_ = static_cast<uint8_t>((menuIndex_ + (dir > 0 ? 1 : 2)) % 3);
        break;
      case AppState::Language:
        menuIndex_ = (menuIndex_ == 0) ? 1 : 0;
        break;
      case AppState::PresetList: {
        const uint8_t total = static_cast<uint8_t>(presets_.count() + 2);
        if (dir > 0) {
          menuIndex_ = static_cast<uint8_t>((menuIndex_ + 1) % total);
        } else {
          menuIndex_ = static_cast<uint8_t>((menuIndex_ + total - 1) % total);
        }
        break;
      }
      case AppState::PresetActions:
        actionIndex_ = static_cast<uint8_t>((actionIndex_ + (dir > 0 ? 1 : 4)) % 5);
        break;
      case AppState::PresetDeleteConfirm:
        deleteYes_ = !deleteYes_;
        break;
      case AppState::ManualEdit:
      case AppState::PresetEdit:
        adjustActiveDigit(dir);
        break;
      case AppState::PresetName: {
        char c = nameBuf_[namePos_];
        if (c == 0) {
          c = 'A';
        }
        auto nextChar = [](char ch, int d) -> char {
          const char* set = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _";
          const int n = 38;
          int idx = 0;
          for (int i = 0; i < n; i++) {
            if (set[i] == ch) {
              idx = i;
              break;
            }
          }
          idx = (idx + (d > 0 ? 1 : n - 1)) % n;
          return set[idx];
        };
        nameBuf_[namePos_] = nextChar(c, dir);
        if (namePos_ + 1 <= PRESET_NAME_LEN) {
          nameBuf_[namePos_ + 1] = 0;
        }
        break;
      }
      default:
        break;
    }
  };

  if (steps != 0) {
    onRotate(steps);
  }

  if (ev == InputEvent::LongPress) {
    switch (state_) {
      case AppState::Settings:
      case AppState::Language:
      case AppState::Diagnostics:
      case AppState::ManualEdit:
        menuIndex_ = 0;
        setState(AppState::MainMenu);
        break;
      case AppState::PresetList:
        menuIndex_ = 0;
        setState(AppState::MainMenu);
        break;
      case AppState::PresetActions:
      case AppState::PresetEdit:
      case AppState::PresetDeleteConfirm:
        menuIndex_ = 0;
        setState(AppState::PresetList);
        break;
      case AppState::PresetName: {
        nameBuf_[PRESET_NAME_LEN] = 0;
        PresetRecord rec{};
        strncpy(rec.name, nameBuf_, PRESET_NAME_LEN);
        rec.name[PRESET_NAME_LEN] = 0;
        rec.program = draft_;
        rec.program.targetRpm = clampRpm(rec.program.targetRpm);
        rec.valid = 1;
        if (saveAsNew_) {
          presets_.saveNew(rec);
        } else if (editingPreset_) {
          presets_.update(editingPresetIndex_, rec);
        }
        saveAsNew_ = false;
        editingPreset_ = false;
        menuIndex_ = 0;
        setState(AppState::PresetList);
        break;
      }
      case AppState::StartConfirm:
      case AppState::Countdown:
        setState(editingPreset_ || saveAsNew_ ? AppState::PresetList : AppState::ManualEdit);
        break;
      case AppState::Winding:
        winding_.requestPause();
        break;
      case AppState::Paused:
        winding_.requestAbort();
        break;
      case AppState::Complete:
      case AppState::Aborted:
        setState(AppState::MainMenu);
        break;
      default:
        break;
    }
  }

  if (ev == InputEvent::Click) {
    switch (state_) {
      case AppState::BootError:
        bootStep_ = 0;
        setState(AppState::Boot);
        break;
      case AppState::ErrorState:
        setState(AppState::MainMenu);
        break;
      case AppState::MainMenu:
        if (menuIndex_ == 0) {
          enterManualEdit();
        } else if (menuIndex_ == 1) {
          menuIndex_ = 0;
          setState(AppState::PresetList);
        } else {
          menuIndex_ = 0;
          setState(AppState::Settings);
        }
        break;
      case AppState::Settings:
        if (menuIndex_ == 0) {
          menuIndex_ = (lang_ == Language::Polish) ? 0 : 1;
          setState(AppState::Language);
        } else if (menuIndex_ == 1) {
          motor_.detect();
          setState(AppState::Diagnostics);
        } else {
          setState(AppState::MainMenu);
        }
        break;
      case AppState::Language:
        lang_ = (menuIndex_ == 0) ? Language::Polish : Language::English;
        presets_.saveLanguage(lang_);
        setState(AppState::Settings);
        break;
      case AppState::Diagnostics:
        setState(AppState::Settings);
        break;
      case AppState::ManualEdit:
      case AppState::PresetEdit:
        onEditClick();
        break;
      case AppState::PresetList: {
        const uint8_t backIdx = static_cast<uint8_t>(presets_.count() + 1);
        if (menuIndex_ == 0) {
          enterPresetEditNew();
        } else if (menuIndex_ == backIdx) {
          menuIndex_ = 0;
          setState(AppState::MainMenu);
        } else {
          presetIndex_ = static_cast<uint8_t>(menuIndex_ - 1);
          actionIndex_ = 0;
          setState(AppState::PresetActions);
        }
        break;
      }
      case AppState::PresetActions: {
        if (actionIndex_ == 4) {
          setState(AppState::PresetList);
          break;
        }
        PresetRecord p{};
        if (!presets_.get(presetIndex_, p)) {
          setState(AppState::PresetList);
          break;
        }
        if (actionIndex_ == 0) {
          draft_ = p.program;
          setState(AppState::StartConfirm);
        } else if (actionIndex_ == 1) {
          enterPresetEditExisting();
        } else if (actionIndex_ == 2) {
          strncpy(nameBuf_, p.name, PRESET_NAME_LEN);
          nameBuf_[PRESET_NAME_LEN] = 0;
          namePos_ = 0;
          editingPreset_ = true;
          editingPresetIndex_ = presetIndex_;
          saveAsNew_ = false;
          draft_ = p.program;
          setState(AppState::PresetName);
        } else {
          deleteYes_ = false;
          setState(AppState::PresetDeleteConfirm);
        }
        break;
      }
      case AppState::PresetName: {
        if (nameBuf_[namePos_] == 0) {
          nameBuf_[namePos_] = 'A';
        }
        namePos_++;
        if (namePos_ >= PRESET_NAME_LEN) {
          namePos_ = PRESET_NAME_LEN - 1;
        }
        nameBuf_[PRESET_NAME_LEN] = 0;
        break;
      }
      case AppState::PresetDeleteConfirm:
        if (deleteYes_) {
          presets_.remove(presetIndex_);
        }
        menuIndex_ = 0;
        setState(AppState::PresetList);
        break;
      case AppState::StartConfirm:
        startCountdown();
        break;
      case AppState::Paused:
        winding_.requestResume();
        setState(AppState::Winding);
        break;
      case AppState::Complete:
        startCountdown();
        break;
      default:
        break;
    }
  }
}

void App::loop() {
  const uint32_t now = millis();

  if (now - lastInputMs_ >= INPUT_POLL_MS) {
    lastInputMs_ = now;
    input_.update(now);
  }

  if (state_ == AppState::Boot) {
    handleBoot(now);
  }

  if (state_ == AppState::Winding || state_ == AppState::Paused) {
    winding_.tick(now);
    if (winding_.isPaused()) {
      setState(AppState::Paused);
    } else if (winding_.isComplete()) {
      setState(AppState::Complete);
    } else if (winding_.isAborted()) {
      setState(AppState::Aborted);
    } else if (winding_.isFault()) {
      errorLine_ = winding_.status().faultText;
      setState(AppState::ErrorState);
    } else if (state_ == AppState::Paused && !winding_.isPaused()) {
      setState(AppState::Winding);
    }
  }

  if (state_ == AppState::Countdown) {
    if (now - countdownAtMs_ >= 1000) {
      countdownAtMs_ = now;
      countdown_--;
      lastLcdMs_ = 0;
      if (countdown_ < 0) {
        if (!winding_.start(draft_)) {
          errorLine_ = winding_.status().faultText;
          setState(AppState::ErrorState);
        } else {
          setState(AppState::Winding);
        }
      }
    }
  }

  handleInput(now);
  render(now);
}
