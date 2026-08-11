#include "app.h"
#include "config.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

// ─────────────────────────────────────────────────────────────────────────────
// Acceleration helpers — separate policy from raw encoder events
// ─────────────────────────────────────────────────────────────────────────────

int App::accelStepMenu(EncSpeed spd) const {
  // Menus: 1 normally, 2 only at very fast rotation (long lists).
  return (spd == EncSpeed::VeryFast) ? MENU_ACCEL_FAST : MENU_ACCEL_SLOW;
}

int App::accelStepDigit(EncSpeed spd) const {
  // Single-digit editor: always 1, wraps 0–9. Speed irrelevant for digit.
  (void)spd;
  return 1;
}

int App::accelStepManualRpm(EncSpeed spd) const {
  switch (spd) {
    case EncSpeed::Medium:   return MANUAL_RPM_STEP_MEDIUM;
    case EncSpeed::Fast:     return MANUAL_RPM_STEP_FAST;
    case EncSpeed::VeryFast: return MANUAL_RPM_STEP_VERY_FAST;
    default:                 return MANUAL_RPM_STEP_SLOW;
  }
}

int App::accelStepName(EncSpeed spd) const {
  // Character list: slow=1, faster=2–3. Never huge jumps.
  switch (spd) {
    case EncSpeed::Fast:     return 3;
    case EncSpeed::VeryFast: return 5;
    case EncSpeed::Medium:   return 2;
    default:                 return 1;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// State helpers
// ─────────────────────────────────────────────────────────────────────────────

void App::setState(AppState s) {
  state_ = s;
  lastLcdMs_ = 0;
}

void App::startCountdown() {
  countdown_ = COUNTDOWN_SECONDS;
  countdownAtMs_ = millis();
  setState(AppState::Countdown);
}

// ─────────────────────────────────────────────────────────────────────────────
// Boot
// ─────────────────────────────────────────────────────────────────────────────

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

void App::handleBoot(uint32_t nowMs) {
  if (nowMs - bootStepMs_ < 300) return;
  bootStepMs_ = nowMs;
  bootStep_++;
  ui_.drawBootProgress(static_cast<uint8_t>(bootStep_ * 10));

  if (bootStep_ == 2) Serial.println(F("[BOOT] waiting for motor bus..."));
  if (bootStep_ == 4) {
    Serial.println(F("[BOOT] probing SERVO42ES"));
    if (!motor_.detect()) {
      Serial.println(F("[BOOT] SERVO42ES NOT FOUND"));
      errorLine_ = "NO RS485 RESPONSE";
      setState(AppState::BootError);
      return;
    }
    Serial.println(F("[MOTOR] SERVO42ES FOUND"));
    Serial.printf("[MOTOR] ENC=%lld RPM=%d AL=%u\n",
                  static_cast<long long>(motor_.encoder()),
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

// ─────────────────────────────────────────────────────────────────────────────
// AUTO mode (target-based winding setup)
// ─────────────────────────────────────────────────────────────────────────────

void App::enterAutoEdit() {
  draft_ = WindingProgram{};
  editField_ = 0;
  digitPos_ = 0;
  editingPreset_ = false;
  saveAsNew_ = false;
  setState(AppState::AutoEdit);
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
  if (!presets_.get(presetIndex_, p)) { setState(AppState::PresetList); return; }
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

uint8_t App::digitsForField(uint8_t field) const {
  switch (field) {
    case 0: return TURNS_DIGITS;
    case 1: return RPM_DIGITS;
    case 3: case 5: return RAMP_TENTHS_DIGITS;
    default: return 0;
  }
}

void App::formatTurnsDigits(char* out, unsigned n, bool blink) const {
  snprintf(out, n, "%05lu", static_cast<unsigned long>(draft_.targetTurns));
  if (blink && editField_ == 0 && ((millis() / 400) & 1)) {
    const int idx = TURNS_DIGITS - 1 - static_cast<int>(digitPos_);
    if (idx >= 0 && static_cast<unsigned>(idx) + 1 < n) out[idx] = '_';
  }
}

void App::formatRpmDigits(char* out, unsigned n, bool blink) const {
  snprintf(out, n, "%04u", draft_.targetRpm);
  if (blink && editField_ == 1 && ((millis() / 400) & 1)) {
    const int idx = RPM_DIGITS - 1 - static_cast<int>(digitPos_);
    if (idx >= 0 && static_cast<unsigned>(idx) + 1 < n) out[idx] = '_';
  }
}

void App::formatRampTenthsDigits(char* out, unsigned n, uint16_t ms, bool blink) const {
  snprintf(out, n, "%0.1fs", ms / 1000.0f);
  // Format is e.g. "02.0s" — blink the active tenths place.
  if (blink && ((millis() / 400) & 1)) {
    int idx = -1;
    if (digitPos_ == 0)      idx = 3;   // tenths of second
    else if (digitPos_ == 1) idx = 1;   // seconds
    else if (digitPos_ == 2) idx = 0;   // tens of seconds
    if (idx >= 0 && static_cast<unsigned>(idx) + 1 < n) out[idx] = '_';
  }
}

void App::adjustActiveDigit(int dir) {
  // Bump the active digit of a number by dir (+1 or -1), wrapping 0–9.
  auto bumpDigit = [&](uint32_t& value, uint8_t place, uint32_t minVal, uint32_t maxVal) {
    uint32_t placeVal = 1;
    for (uint8_t i = 0; i < place; i++) placeVal *= 10;
    int32_t digit = static_cast<int32_t>((value / placeVal) % 10) + dir;
    if (digit > 9) digit = 0;
    if (digit < 0) digit = 9;
    value = (value / (placeVal * 10)) * (placeVal * 10) + (value % placeVal)
            + static_cast<uint32_t>(digit) * placeVal;
    if (value > maxVal) value = maxVal;
    if (value < minVal) value = minVal;
  };

  if (editField_ == 0) {
    uint32_t v = draft_.targetTurns;
    bumpDigit(v, digitPos_, MIN_TURNS, MAX_TURNS);
    draft_.targetTurns = clampTurns(v);
  } else if (editField_ == 1) {
    uint32_t v = draft_.targetRpm;
    bumpDigit(v, digitPos_, MIN_WINDER_RPM, MAX_WINDER_RPM);
    draft_.targetRpm = clampRpm(v);
  } else if (editField_ == 3 || editField_ == 5) {
    uint16_t& ms = (editField_ == 3) ? draft_.rampUpMs : draft_.rampDownMs;
    uint32_t tenths = ms / 100;
    bumpDigit(tenths, digitPos_, RAMP_TIME_MIN_MS / 100, RAMP_TIME_MAX_MS / 100);
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
      editField_++;
    }
    return;
  }
  // Non-digit field: click just advances.
  if (editField_ < 7) { editField_++; digitPos_ = 0; return; }
  // Field 7 = Start / Save.
  if (state_ == AppState::PresetEdit) {
    namePos_ = 0;
    if (!editingPreset_) { strncpy(nameBuf_, "PRESET", PRESET_NAME_LEN); nameBuf_[PRESET_NAME_LEN] = 0; }
    setState(AppState::PresetName);
  } else {
    setState(AppState::StartConfirm);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// TRUE MANUAL MODE
// ─────────────────────────────────────────────────────────────────────────────

void App::enterManualMode() {
  manualSetRpm_ = 0;
  manualDir_ = WindDir::CW;
  manualRunning_ = false;
  manualStopping_ = false;
  motor_.setDirection(WindDir::CW);
  motor_.prepareForWinding();  // enable + mode set
  // Capture encoder baseline for this session.
  manualEncBaseline_ = motor_.encoder();
  manualTurns_.beginJob(manualEncBaseline_, 0 /*no target*/, WindDir::CW);
  lastManualCmdMs_ = 0;
  lastManualTelMs_ = 0;
  setState(AppState::ManualMode);
}

void App::manualChangeDirection(WindDir newDir) {
  if (newDir == manualDir_) return;
  if (manualSetRpm_ > 0) return;  // safety: only change dir when stopped
  manualDir_ = newDir;
  motor_.setDirection(newDir);
  // Reset turn counter baseline on direction change.
  manualEncBaseline_ = motor_.encoder();
  manualTurns_.beginJob(manualEncBaseline_, 0, newDir);
}

void App::tickManualMode(uint32_t nowMs) {
  // Telemetry.
  if (nowMs - lastManualTelMs_ >= POSITION_POLL_MS) {
    lastManualTelMs_ = nowMs;
    motor_.pollTelemetry(nowMs);
    manualTurns_.update(motor_.encoder());
  }

  const uint16_t actualRpm = motor_.actualRpmAbs();

  // Handle stop completion.
  if (manualStopping_ && actualRpm <= MANUAL_STOPPED_RPM) {
    manualStopping_ = false;
    manualRunning_ = false;
    manualSetRpm_ = 0;
  }

  // Push speed command.
  if (!manualStopping_ && nowMs - lastManualCmdMs_ >= MANUAL_COMMAND_UPDATE_MS) {
    lastManualCmdMs_ = nowMs;
    const bool cw = (manualDir_ == WindDir::CW);
    if (manualSetRpm_ == 0) {
      if (actualRpm > MANUAL_STOPPED_RPM) {
        servo_.speedStop(SERVO_SOFT_STOP_ACC);
      }
    } else {
      servo_.speedRun(cw, manualSetRpm_, SERVO_INTERNAL_ACC);
      manualRunning_ = true;
    }
  }
}

void App::exitManualMode(bool immediate) {
  if (immediate || (motor_.actualRpmAbs() <= MANUAL_STOPPED_RPM && manualSetRpm_ == 0)) {
    motor_.idleSafe();
    setState(AppState::MainMenu);
  } else {
    // Ramp to 0 and then exit.
    manualSetRpm_ = 0;
    manualStopping_ = true;
    // Will exit on next tick when stopped — set a flag by resetting to go back.
    // Handled in tickManualMode transition: when stopped, check pending exit.
    // Simpler: just wait via the state machine (remain in ManualMode until stopped).
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// LCD rendering
// ─────────────────────────────────────────────────────────────────────────────

static void clampMenuWindow(uint8_t sel, uint8_t count, uint8_t& window) {
  if (count == 0) { window = 0; return; }
  if (sel < window) window = sel;
  if (sel >= window + 4) window = sel - 3;
  if (count <= 4) window = 0;
}

void App::render(uint32_t nowMs) {
  // Fast refresh for blinking digit editors; normal throttle elsewhere.
  const bool isEditScreen =
      (state_ == AppState::AutoEdit || state_ == AppState::PresetEdit);
  const uint32_t refreshMs = isEditScreen ? 100u : LCD_UPDATE_MS;
  if (nowMs - lastLcdMs_ < refreshMs && state_ != AppState::Countdown) return;
  lastLcdMs_ = nowMs;

  switch (state_) {

    // ── MAIN MENU ─────────────────────────────────────────────────
    case AppState::MainMenu: {
      const char* items[4] = {
          tr(lang_, StrId::Auto),
          tr(lang_, StrId::ManualMode),
          tr(lang_, StrId::Presets),
          tr(lang_, StrId::Settings)};
      clampMenuWindow(menuIndex_, 4, menuWindow_);
      ui_.drawMenu(lang_, items, 4, menuIndex_, menuWindow_);
      break;
    }

    // ── SETTINGS ──────────────────────────────────────────────────
    case AppState::Settings: {
      const char* items[3] = {
          tr(lang_, StrId::Language),
          tr(lang_, StrId::Diagnostics),
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
      ui_.drawDiagnostics(lang_, motor_.alarmOk(),
                          motor_.encoderOk() || servo_.failStreak() == 0,
                          motor_.actualRpmAbs(), motor_.encoder(), motor_.alarmStatus());
      break;
    }

    // ── AUTO EDIT (target-based setup) ────────────────────────────
    case AppState::AutoEdit:
    case AppState::PresetEdit: {
      const uint8_t fieldCount = 8;
      uint8_t top = (editField_ < 3) ? 0 : static_cast<uint8_t>(editField_ - 2);
      if (top > fieldCount - 4) top = fieldCount - 4;
      for (uint8_t row = 0; row < 4; row++) {
        const uint8_t f = static_cast<uint8_t>(top + row);
        char val[16]; val[0] = 0;
        const char* label = "";
        const bool sel = (editField_ == f);
        switch (f) {
          case 0: label = tr(lang_, StrId::Turns);    formatTurnsDigits(val, sizeof val, sel);  break;
          case 1: label = tr(lang_, StrId::Rpm);      formatRpmDigits(val, sizeof val, sel);    break;
          case 2: label = tr(lang_, StrId::RampUp);   formatRampType(lang_, draft_.rampUpType, val, sizeof val);  break;
          case 3: label = tr(lang_, StrId::UpTime);   formatRampTenthsDigits(val, sizeof val, draft_.rampUpMs, sel);   break;
          case 4: label = tr(lang_, StrId::RampDown); formatRampType(lang_, draft_.rampDownType, val, sizeof val); break;
          case 5: label = tr(lang_, StrId::DownTime); formatRampTenthsDigits(val, sizeof val, draft_.rampDownMs, sel);  break;
          case 6: label = tr(lang_, StrId::Direction);formatDir(lang_, draft_.direction, val, sizeof val); break;
          default:
            label = (state_ == AppState::PresetEdit)
                        ? tr(lang_, StrId::Save) : tr(lang_, StrId::Start);
            break;
        }
        char line[21];
        if (f == 7) snprintf(line, sizeof line, "%c%s", sel ? '>' : ' ', label);
        else        snprintf(line, sizeof line, "%c%s:%s", sel ? '>' : ' ', label, val);
        ui_.setLine(row, line);
      }
      break;
    }

    // ── MANUAL MODE ───────────────────────────────────────────────
    case AppState::ManualMode: {
      char dBuf[4]; formatDir(lang_, manualDir_, dBuf, sizeof dBuf);
      // Line 0: MANUAL          CW
      char l0[21]; snprintf(l0, sizeof l0, "%-12s%8s", tr(lang_, StrId::ManualMode), dBuf);
      ui_.setLine(0, l0);
      // Line 1: SET:  1200 RPM
      char l1[21]; snprintf(l1, sizeof l1, "%-6s%4u RPM", tr(lang_, StrId::ManualSet), manualSetRpm_);
      ui_.setLine(1, l1);
      // Line 2: ACT:  1197 RPM
      char l2[21]; snprintf(l2, sizeof l2, "%-6s%4u RPM", tr(lang_, StrId::ManualAct), motor_.actualRpmAbs());
      ui_.setLine(2, l2);
      // Line 3: context hint
      const bool stopped = (manualSetRpm_ == 0 && !manualStopping_);
      if (stopped) {
        ui_.setLine(3, tr(lang_, StrId::ManualClickDir));
      } else {
        ui_.setLine(3, tr(lang_, StrId::ManualClickStop));
      }
      break;
    }

    // ── PRESET LIST ───────────────────────────────────────────────
    case AppState::PresetList: {
      const uint8_t cnt = presets_.count();
      const uint8_t total = static_cast<uint8_t>(cnt + 2);
      char names[34][21];
      const char* items[34];
      snprintf(names[0], 21, "%s", tr(lang_, StrId::NewPreset));
      items[0] = names[0];
      for (uint8_t i = 0; i < cnt; i++) {
        PresetRecord p{};
        snprintf(names[i+1], 21, "%s", presets_.get(i, p) ? p.name : "?");
        items[i+1] = names[i+1];
      }
      snprintf(names[cnt+1], 21, "%s", tr(lang_, StrId::Back));
      items[cnt+1] = names[cnt+1];
      clampMenuWindow(menuIndex_, total, menuWindow_);
      ui_.drawMenu(lang_, items, total, menuIndex_, menuWindow_);
      break;
    }
    case AppState::PresetActions: {
      const char* items[5] = {
          tr(lang_, StrId::Start), tr(lang_, StrId::Edit),
          tr(lang_, StrId::Rename), tr(lang_, StrId::Delete),
          tr(lang_, StrId::Back)};
      clampMenuWindow(actionIndex_, 5, menuWindow_);
      ui_.drawMenu(lang_, items, 5, actionIndex_, menuWindow_);
      break;
    }
    case AppState::PresetName: {
      char line[21]; snprintf(line, sizeof line, "NAME:%s", nameBuf_);
      if (namePos_ >= 0 && namePos_ < PRESET_NAME_LEN) {
        const size_t pos = 5 + static_cast<size_t>(namePos_);
        if (pos < 20 && ((nowMs / 400) & 1)) line[pos] = '_';
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
      snprintf(line, sizeof line, "%c%s  %c%s",
               deleteYes_  ? '>' : ' ', tr(lang_, StrId::Yes),
               !deleteYes_ ? '>' : ' ', tr(lang_, StrId::No));
      ui_.setLine(1, line);
      ui_.setLine(2, "");
      ui_.setLine(3, tr(lang_, StrId::HoldBack));
      break;
    }

    // ── WINDING SCREENS ───────────────────────────────────────────
    case AppState::StartConfirm: ui_.drawStartConfirm(lang_, draft_); break;
    case AppState::Countdown:    ui_.drawCountdown(countdown_); break;
    case AppState::Winding:      ui_.drawRun(lang_, winding_.status()); break;
    case AppState::Paused:       ui_.drawPaused(lang_, winding_.status()); break;
    case AppState::Complete:     ui_.drawComplete(lang_, winding_.status()); break;
    case AppState::Aborted:      ui_.drawAborted(lang_, winding_.status()); break;

    // ── ERROR / BOOT ──────────────────────────────────────────────
    case AppState::BootError:
    case AppState::ErrorState:
      ui_.drawError(lang_, tr(lang_, StrId::MotorError),
                    errorLine_ ? errorLine_ : tr(lang_, StrId::NoRs485));
      break;
    default: break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Input handling
// ─────────────────────────────────────────────────────────────────────────────

void App::handleInput(uint32_t nowMs) {
  (void)nowMs;

  const EncDetent det = input_.takeDetent();
  const ButtonEvent btn = input_.takeButton();
  const int dir = det.dir;

  // ── ROTATION ──────────────────────────────────────────────────
  if (dir != 0) {
    switch (state_) {
      case AppState::MainMenu:
        if (dir > 0) menuIndex_ = static_cast<uint8_t>((menuIndex_ + 1) % 4);
        else         menuIndex_ = (menuIndex_ == 0) ? 3 : menuIndex_ - 1;
        break;
      case AppState::Settings:
        if (dir > 0) menuIndex_ = static_cast<uint8_t>((menuIndex_ + 1) % 3);
        else         menuIndex_ = (menuIndex_ == 0) ? 2 : menuIndex_ - 1;
        break;
      case AppState::Language:
        menuIndex_ = (menuIndex_ == 0) ? 1 : 0;
        break;
      case AppState::PresetList: {
        const uint8_t total = static_cast<uint8_t>(presets_.count() + 2);
        const int step = accelStepMenu(det.speed);
        if (dir > 0) menuIndex_ = static_cast<uint8_t>((menuIndex_ + step) % total);
        else         menuIndex_ = static_cast<uint8_t>((menuIndex_ + total - step % total) % total);
        break;
      }
      case AppState::PresetActions:
        if (dir > 0) actionIndex_ = static_cast<uint8_t>((actionIndex_ + 1) % 5);
        else         actionIndex_ = (actionIndex_ == 0) ? 4 : actionIndex_ - 1;
        break;
      case AppState::PresetDeleteConfirm:
        deleteYes_ = !deleteYes_;
        break;
      case AppState::AutoEdit:
      case AppState::PresetEdit:
        adjustActiveDigit(dir * accelStepDigit(det.speed));
        break;
      case AppState::PresetName: {
        const int step = accelStepName(det.speed);
        const char* set = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _";
        const int n = 38;
        char c = nameBuf_[namePos_];
        if (c == 0) c = 'A';
        int idx = 0;
        for (int i = 0; i < n; i++) { if (set[i] == c) { idx = i; break; } }
        idx = (idx + (dir > 0 ? step : n - step % n)) % n;
        nameBuf_[namePos_] = set[idx];
        if (namePos_ + 1 <= PRESET_NAME_LEN) nameBuf_[namePos_ + 1] = 0;
        break;
      }
      case AppState::ManualMode: {
        // Direction reversal guard: only change RPM, never reverse directly.
        const int step = accelStepManualRpm(det.speed);
        int32_t next = static_cast<int32_t>(manualSetRpm_) + dir * step;
        if (next < 0) next = 0;
        if (next > MAX_WINDER_RPM) next = MAX_WINDER_RPM;
        if (next > 0 && manualStopping_) next = 0;  // stay 0 while stopping
        manualSetRpm_ = static_cast<uint16_t>(next);
        break;
      }
      default: break;
    }
  }

  // ── LONG PRESS ────────────────────────────────────────────────
  if (btn == ButtonEvent::LongPress) {
    switch (state_) {
      case AppState::MainMenu: break;  // top-level, nowhere to go
      case AppState::Settings:
      case AppState::Language:
      case AppState::Diagnostics:
        menuIndex_ = 0; setState(AppState::MainMenu); break;
      case AppState::AutoEdit:
        setState(AppState::MainMenu); break;
      case AppState::PresetList:
        menuIndex_ = 0; setState(AppState::MainMenu); break;
      case AppState::PresetActions:
      case AppState::PresetEdit:
      case AppState::PresetDeleteConfirm:
        menuIndex_ = 0; setState(AppState::PresetList); break;
      case AppState::PresetName: {
        // Save preset name.
        nameBuf_[PRESET_NAME_LEN] = 0;
        PresetRecord rec{};
        strncpy(rec.name, nameBuf_, PRESET_NAME_LEN);
        rec.name[PRESET_NAME_LEN] = 0;
        rec.program = draft_;
        rec.program.targetRpm = clampRpm(rec.program.targetRpm);
        rec.valid = 1;
        if (saveAsNew_)     presets_.saveNew(rec);
        else if (editingPreset_) presets_.update(editingPresetIndex_, rec);
        saveAsNew_ = editingPreset_ = false;
        menuIndex_ = 0;
        setState(AppState::PresetList);
        break;
      }
      case AppState::StartConfirm:
      case AppState::Countdown:
        setState(editingPreset_ || saveAsNew_ ? AppState::PresetList : AppState::AutoEdit);
        break;
      case AppState::Winding:
        winding_.requestPause();
        break;
      case AppState::Paused:
        winding_.requestAbort();
        break;
      case AppState::Complete:
      case AppState::Aborted:
        setState(AppState::MainMenu); break;
      case AppState::ManualMode:
        // Stop motor then exit.
        exitManualMode(false);
        break;
      default: break;
    }
  }

  // ── SHORT CLICK ───────────────────────────────────────────────
  if (btn == ButtonEvent::Click) {
    switch (state_) {
      case AppState::BootError:
        bootStep_ = 0; setState(AppState::Boot); break;
      case AppState::ErrorState:
        setState(AppState::MainMenu); break;

      case AppState::MainMenu:
        menuWindow_ = 0;
        if      (menuIndex_ == 0) enterAutoEdit();
        else if (menuIndex_ == 1) enterManualMode();
        else if (menuIndex_ == 2) { menuIndex_ = 0; setState(AppState::PresetList); }
        else                      { menuIndex_ = 0; setState(AppState::Settings); }
        break;

      case AppState::Settings:
        if (menuIndex_ == 0) {
          menuIndex_ = (lang_ == Language::Polish) ? 0 : 1;
          setState(AppState::Language);
        } else if (menuIndex_ == 1) {
          motor_.detect(); setState(AppState::Diagnostics);
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
        setState(AppState::Settings); break;

      case AppState::AutoEdit:
      case AppState::PresetEdit:
        onEditClick(); break;

      case AppState::ManualMode: {
        const bool stopped = (manualSetRpm_ == 0 && !manualStopping_);
        if (stopped) {
          // Toggle direction when stopped.
          manualChangeDirection(manualDir_ == WindDir::CW ? WindDir::CCW : WindDir::CW);
        } else {
          // Stop motor.
          manualSetRpm_ = 0;
          manualStopping_ = true;
        }
        break;
      }

      case AppState::PresetList: {
        const uint8_t backIdx = static_cast<uint8_t>(presets_.count() + 1);
        if (menuIndex_ == 0) {
          enterPresetEditNew();
        } else if (menuIndex_ == backIdx) {
          menuIndex_ = 0; setState(AppState::MainMenu);
        } else {
          presetIndex_ = static_cast<uint8_t>(menuIndex_ - 1);
          actionIndex_ = 0; menuWindow_ = 0;
          setState(AppState::PresetActions);
        }
        break;
      }
      case AppState::PresetActions: {
        if (actionIndex_ == 4) { setState(AppState::PresetList); break; }
        PresetRecord p{};
        if (!presets_.get(presetIndex_, p)) { setState(AppState::PresetList); break; }
        if (actionIndex_ == 0) {
          draft_ = p.program; setState(AppState::StartConfirm);
        } else if (actionIndex_ == 1) {
          enterPresetEditExisting();
        } else if (actionIndex_ == 2) {
          strncpy(nameBuf_, p.name, PRESET_NAME_LEN); nameBuf_[PRESET_NAME_LEN] = 0;
          namePos_ = 0; editingPreset_ = true; editingPresetIndex_ = presetIndex_;
          saveAsNew_ = false; draft_ = p.program; setState(AppState::PresetName);
        } else {
          deleteYes_ = false; setState(AppState::PresetDeleteConfirm);
        }
        break;
      }
      case AppState::PresetName: {
        if (nameBuf_[namePos_] == 0) nameBuf_[namePos_] = 'A';
        namePos_++;
        if (namePos_ >= PRESET_NAME_LEN) namePos_ = PRESET_NAME_LEN - 1;
        nameBuf_[PRESET_NAME_LEN] = 0;
        break;
      }
      case AppState::PresetDeleteConfirm:
        if (deleteYes_) presets_.remove(presetIndex_);
        menuIndex_ = 0; setState(AppState::PresetList); break;

      case AppState::StartConfirm: startCountdown(); break;

      case AppState::Paused:
        winding_.requestResume(); setState(AppState::Winding); break;

      case AppState::Complete: startCountdown(); break;

      default: break;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Main loop
// ─────────────────────────────────────────────────────────────────────────────

void App::loop() {
  const uint32_t now = millis();

  // Input polling (high priority, every 1 ms).
  if (now - lastInputMs_ >= INPUT_POLL_MS) {
    lastInputMs_ = now;
    input_.update(now);
  }

  // Boot sequence.
  if (state_ == AppState::Boot) {
    handleBoot(now);
    return;
  }

  // True manual motor tick.
  if (state_ == AppState::ManualMode) {
    tickManualMode(now);
    // If stop requested and now halted, complete the exit if user pressed long-press.
    // (Long-press sets setRpm=0 via exitManualMode; here we just keep ticking
    //  until stopped, then we handle it in the next long-press or the user stays in manual.)
  }

  // Auto winding engine.
  if (state_ == AppState::Winding || state_ == AppState::Paused) {
    winding_.tick(now);
    if      (winding_.isPaused())   setState(AppState::Paused);
    else if (winding_.isComplete()) setState(AppState::Complete);
    else if (winding_.isAborted())  setState(AppState::Aborted);
    else if (winding_.isFault())    { errorLine_ = winding_.status().faultText; setState(AppState::ErrorState); }
    else if (state_ == AppState::Paused && !winding_.isPaused()) setState(AppState::Winding);
  }

  // Countdown timer.
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
