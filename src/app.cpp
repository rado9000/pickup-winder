#include "app.h"
#include "config.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

void App::setState(AppState s) {
  state_ = s;
  lastLcdMs_ = 0;  // force redraw
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
  editingPreset_ = false;
  saveAsNew_ = false;
  setState(AppState::ManualEdit);
}

void App::enterPresetEditNew() {
  draft_ = WindingProgram{};
  editField_ = 0;
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
  editingPreset_ = true;
  editingPresetIndex_ = presetIndex_;
  saveAsNew_ = false;
  setState(AppState::PresetEdit);
}

int App::editStepTurns(uint32_t nowMs) const {
  const uint32_t dt = nowMs - lastEditMs_;
  if (dt < EDIT_FASTER_THRESHOLD_MS) {
    return EDIT_FASTER_STEP_TURNS;
  }
  if (dt < EDIT_FAST_THRESHOLD_MS) {
    return EDIT_FAST_STEP_TURNS;
  }
  return 1;
}

int App::editStepRpm(uint32_t nowMs) const {
  const uint32_t dt = nowMs - lastEditMs_;
  if (dt < EDIT_FASTER_THRESHOLD_MS) {
    return EDIT_FASTER_STEP_RPM;
  }
  if (dt < EDIT_FAST_THRESHOLD_MS) {
    return EDIT_FAST_STEP_RPM;
  }
  return 1;
}

void App::adjustTurns(int dir, uint32_t nowMs) {
  const int step = editStepTurns(nowMs);
  int64_t v = static_cast<int64_t>(draft_.targetTurns) + dir * step;
  if (v < static_cast<int64_t>(MIN_TURNS)) {
    v = MIN_TURNS;
  }
  if (v > static_cast<int64_t>(MAX_TURNS)) {
    v = MAX_TURNS;
  }
  draft_.targetTurns = static_cast<uint32_t>(v);
  lastEditMs_ = nowMs;
}

void App::adjustRpm(int dir, uint32_t nowMs) {
  const int step = editStepRpm(nowMs);
  int32_t v = static_cast<int32_t>(draft_.targetRpm) + dir * step;
  if (v < MIN_WINDER_RPM) {
    v = MIN_WINDER_RPM;
  }
  if (v > MAX_WINDER_RPM) {
    v = MAX_WINDER_RPM;
  }
  draft_.targetRpm = static_cast<uint16_t>(v);
  lastEditMs_ = nowMs;
}

void App::adjustRampMs(uint16_t& ms, int dir) {
  int32_t v = static_cast<int32_t>(ms) + dir * static_cast<int32_t>(RAMP_TIME_STEP_MS);
  ms = clampRampMs(static_cast<uint32_t>(v < 0 ? 0 : v));
}

void App::startCountdown() {
  countdown_ = COUNTDOWN_SECONDS;
  countdownAtMs_ = millis();
  setState(AppState::Countdown);
}

void App::handleBoot(uint32_t nowMs) {
  // Slower boot steps — give SERVO42ES time after RS485 begin (test used ~1.5 s).
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
  if (nowMs - lastLcdMs_ < LCD_UPDATE_MS && state_ != AppState::Countdown) {
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
      menuWindow_ = 0;
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
      // scrolling 8 fields: turns,rpm,upType,upTime,downType,downTime,dir,start/save
      const uint8_t fieldCount = 8;
      uint8_t top = (editField_ < 3) ? 0 : static_cast<uint8_t>(editField_ - 2);
      if (top > fieldCount - 4) {
        top = fieldCount - 4;
      }
      for (uint8_t row = 0; row < 4; row++) {
        const uint8_t f = static_cast<uint8_t>(top + row);
        char val[16];
        const char* label = "";
        switch (f) {
          case 0:
            label = tr(lang_, StrId::Turns);
            snprintf(val, sizeof val, "%05lu", static_cast<unsigned long>(draft_.targetTurns));
            break;
          case 1:
            label = tr(lang_, StrId::Rpm);
            snprintf(val, sizeof val, "%4u", draft_.targetRpm);
            break;
          case 2:
            label = tr(lang_, StrId::RampUp);
            formatRampType(lang_, draft_.rampUpType, val, sizeof val);
            break;
          case 3:
            label = tr(lang_, StrId::UpTime);
            snprintf(val, sizeof val, "%0.1fs", draft_.rampUpMs / 1000.0f);
            break;
          case 4:
            label = tr(lang_, StrId::RampDown);
            formatRampType(lang_, draft_.rampDownType, val, sizeof val);
            break;
          case 5:
            label = tr(lang_, StrId::DownTime);
            snprintf(val, sizeof val, "%0.1fs", draft_.rampDownMs / 1000.0f);
            break;
          case 6:
            label = tr(lang_, StrId::Direction);
            formatDir(lang_, draft_.direction, val, sizeof val);
            break;
          default:
            label = (state_ == AppState::PresetEdit && (saveAsNew_ || editingPreset_))
                        ? tr(lang_, StrId::Save)
                        : tr(lang_, StrId::Start);
            val[0] = 0;
            break;
        }
        char line[21];
        if (f == 7) {
          snprintf(line, sizeof line, "%c%s", (editField_ == f) ? '>' : ' ', label);
        } else {
          snprintf(line, sizeof line, "%c%s:%s", (editField_ == f) ? '>' : ' ', label, val);
        }
        ui_.setLine(row, line);
      }
      break;
    }
    case AppState::PresetList: {
      const uint8_t total = static_cast<uint8_t>(presets_.count() + 1);
      char names[33][21];
      const char* items[33];
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
      clampMenuWindow(menuIndex_, total, menuWindow_);
      ui_.drawMenu(lang_, items, total, menuIndex_, menuWindow_);
      break;
    }
    case AppState::PresetActions: {
      const char* items[4] = {tr(lang_, StrId::Start), tr(lang_, StrId::Edit),
                              tr(lang_, StrId::Rename), tr(lang_, StrId::Delete)};
      clampMenuWindow(actionIndex_, 4, menuWindow_);
      ui_.drawMenu(lang_, items, 4, actionIndex_, menuWindow_);
      break;
    }
    case AppState::PresetName: {
      char line[21];
      snprintf(line, sizeof line, "NAME:%s", nameBuf_);
      if (namePos_ >= 0 && namePos_ < PRESET_NAME_LEN) {
        const size_t pos = 5 + static_cast<size_t>(namePos_);
        if (pos < 20) {
          // blink underscore handled simply
          if ((nowMs / 400) & 1) {
            if (line[pos] == 0 || line[pos] == ' ') {
              line[pos] = '_';
              if (line[pos + 1] == 0) {
                // ensure null
              }
            } else {
              line[pos] = '_';
            }
          }
        }
      }
      ui_.setLine(0, tr(lang_, StrId::Rename));
      ui_.setLine(1, line);
      ui_.setLine(2, "ROT=CHAR CLICK=NEXT");
      ui_.setLine(3, tr(lang_, StrId::HoldBack));
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
  // Priority: while winding, consume long-press ASAP
  InputEvent ev = InputEvent::None;
  int delta = 0;

  if (state_ == AppState::Winding || state_ == AppState::Paused ||
      state_ == AppState::Countdown) {
    ev = input_.takeEvent();
    // Also drain rotation for live feel if needed later
    delta = input_.takeDelta();
  } else {
    delta = input_.takeDelta();
    ev = input_.takeEvent();
  }

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
        const uint8_t total = static_cast<uint8_t>(presets_.count() + 1);
        if (total == 0) {
          break;
        }
        if (dir > 0) {
          menuIndex_ = static_cast<uint8_t>((menuIndex_ + 1) % total);
        } else {
          menuIndex_ = static_cast<uint8_t>((menuIndex_ + total - 1) % total);
        }
        break;
      }
      case AppState::PresetActions:
        if (dir > 0) {
          actionIndex_ = static_cast<uint8_t>((actionIndex_ + 1) % 4);
        } else {
          actionIndex_ = static_cast<uint8_t>((actionIndex_ + 3) % 4);
        }
        break;
      case AppState::PresetDeleteConfirm:
        deleteYes_ = !deleteYes_;
        break;
      case AppState::ManualEdit:
      case AppState::PresetEdit:
        switch (editField_) {
          case 0:
            adjustTurns(dir, nowMs);
            break;
          case 1:
            adjustRpm(dir, nowMs);
            break;
          case 2:
            draft_.rampUpType =
                (draft_.rampUpType == RampType::SCurve) ? RampType::Linear : RampType::SCurve;
            break;
          case 3:
            adjustRampMs(draft_.rampUpMs, dir);
            break;
          case 4:
            draft_.rampDownType =
                (draft_.rampDownType == RampType::SCurve) ? RampType::Linear : RampType::SCurve;
            break;
          case 5:
            adjustRampMs(draft_.rampDownMs, dir);
            break;
          case 6:
            draft_.direction = (draft_.direction == WindDir::CW) ? WindDir::CCW : WindDir::CW;
            break;
          default:
            break;
        }
        break;
      case AppState::PresetName: {
        char c = nameBuf_[namePos_];
        if (c == 0) {
          c = 'A';
        }
        // charset A-Z 0-9 space _
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

  if (delta != 0) {
    // Apply each step for menus; for value edits use sign once with acceleration
    if (state_ == AppState::ManualEdit || state_ == AppState::PresetEdit ||
        state_ == AppState::PresetName) {
      onRotate(delta > 0 ? 1 : -1);
    } else {
      onRotate(delta > 0 ? 1 : -1);
    }
  }

  if (ev == InputEvent::LongPress) {
    switch (state_) {
      case AppState::MainMenu:
        break;
      case AppState::Settings:
      case AppState::Language:
      case AppState::Diagnostics:
        menuIndex_ = 0;
        setState(AppState::MainMenu);
        break;
      case AppState::ManualEdit:
        setState(AppState::MainMenu);
        break;
      case AppState::PresetList:
      case AppState::PresetActions:
      case AppState::PresetEdit:
      case AppState::PresetDeleteConfirm:
        menuIndex_ = 0;
        setState(AppState::PresetList);
        break;
      case AppState::PresetName: {
        // Long press = save name / preset
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
        if (editField_ < 7) {
          editField_++;
        } else {
          // Start or Save
          if (state_ == AppState::PresetEdit) {
            // go to name editor then save
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
        break;
      case AppState::PresetList:
        if (menuIndex_ == 0) {
          enterPresetEditNew();
        } else {
          presetIndex_ = static_cast<uint8_t>(menuIndex_ - 1);
          actionIndex_ = 0;
          setState(AppState::PresetActions);
        }
        break;
      case AppState::PresetActions: {
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
          // rename only
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
        // Long workflow: after finishing name via long-press save — also allow click at end
        // Use long press to save; click advances char. If user clicks on last char repeatedly,
        // save when namePos hits end twice — simpler: long press saves (already).
        // Additional: if click when namePos at last and buffer filled, save.
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
        // again
        startCountdown();
        break;
      default:
        break;
    }
  }

  // Preset name save on long-press already goes back — handle save there
  if (ev == InputEvent::LongPress && state_ == AppState::PresetName) {
    // overwritten above — fix: save then leave
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

  // Winding engine tick
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
