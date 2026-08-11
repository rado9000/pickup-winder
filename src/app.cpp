#include "app.h"
#include "config.h"
#include "ramp_generator.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// ─────────────────────────────────────────────────────────────────────────────
// UI acceleration policy helpers — NEVER called inside the input module
// ─────────────────────────────────────────────────────────────────────────────

int App::manualRpmStep(const EncDetent& det) {
  // Dedicated Manual policy based on det.dtMs + short same-direction streak.
  // Does NOT use the global EncSpeed classifier (menus/names stay precise).
  const bool idle = (det.dtMs >= static_cast<uint32_t>(ENC_ACCEL_RESET_MS));
  const bool reversed = (manualAccelDir_ != 0 && det.dir != 0 &&
                         det.dir != manualAccelDir_);

  if (idle || reversed || det.dir == 0) {
    manualAccelStreak_ = 1;
  } else {
    if (manualAccelStreak_ < 255) manualAccelStreak_++;
  }
  if (det.dir != 0) manualAccelDir_ = det.dir;

  // Slow deliberate rotation — always 1 RPM, regardless of streak.
  if (idle || det.dtMs >= static_cast<uint32_t>(MANUAL_ACCEL_SLOW_MS)) {
    return MANUAL_RPM_STEP_SLOW;
  }

  // Need a short continuous same-direction burst before accelerating.
  // First detent after idle/reversal stays at 1; ~2 fast detents unlock accel.
  if (manualAccelStreak_ < MANUAL_ACCEL_STREAK_REQUIRED) {
    return MANUAL_RPM_STEP_SLOW;
  }

  if (det.dtMs >= static_cast<uint32_t>(MANUAL_ACCEL_NORMAL_MS)) {
    return MANUAL_RPM_STEP_NORMAL;
  }
  if (det.dtMs >= static_cast<uint32_t>(MANUAL_ACCEL_FAST_MS)) {
    return MANUAL_RPM_STEP_FAST;
  }
  return MANUAL_RPM_STEP_VERY_FAST;
}

int App::accelStepName(EncSpeed spd) const {
  // Very conservative: only 2 chars max, and only after a sustained streak.
  return (spd == EncSpeed::VeryFast) ? NAME_ACCEL_MAX : 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// State helpers
// ─────────────────────────────────────────────────────────────────────────────

void App::setState(AppState s) {
  state_    = s;
  lastLcdMs_= 0;  // force immediate redraw
}

void App::startCountdown() {
  countdown_    = COUNTDOWN_SECONDS;
  countdownAtMs_= millis();
  setState(AppState::Countdown);
}

// ─────────────────────────────────────────────────────────────────────────────
// Preset name buffer helpers
// ─────────────────────────────────────────────────────────────────────────────

// Init nameBuf_ from src: pad unused positions with spaces, always terminate.
// IMPORTANT: never read past the source C-string terminator (was causing
// "PRESET RENAM" by reading adjacent flash/RODATA such as "RENAME").
void App::initNameBuf(const char* src) {
  bool endReached = (src == nullptr);
  for (int i = 0; i < PRESET_NAME_LEN; ++i) {
    if (!endReached && src[i] != '\0') {
      nameBuf_[i] = src[i];
    } else {
      endReached = true;
      nameBuf_[i] = ' ';
    }
  }
  nameBuf_[PRESET_NAME_LEN] = '\0';  // permanent terminator
#if WIND_TARGET_DEBUG
  if (src && src[0] == 'P') {
    Serial.printf("[PRESET] name buffer='%.*s'\n", PRESET_NAME_LEN, nameBuf_);
  }
#endif
}

void App::savePresetName() {
  // Trim trailing spaces before saving.
  char trimmed[PRESET_NAME_LEN + 1];
  memcpy(trimmed, nameBuf_, PRESET_NAME_LEN);
  trimmed[PRESET_NAME_LEN] = '\0';
  int last = PRESET_NAME_LEN - 1;
  while (last >= 0 && trimmed[last] == ' ') {
    trimmed[last--] = '\0';
  }
  if (last < 0) {  // all spaces → use default
    strncpy(trimmed, "PRESET", PRESET_NAME_LEN);
    trimmed[PRESET_NAME_LEN] = '\0';
  }

  PresetRecord rec{};
  strncpy(rec.name, trimmed, PRESET_NAME_LEN);
  rec.name[PRESET_NAME_LEN] = '\0';
  rec.program   = draft_;
  rec.program.targetRpm = clampRpm(rec.program.targetRpm);
  rec.valid = 1;

  if (saveAsNew_)         presets_.saveNew(rec);
  else if (editingPreset_) presets_.update(editingPresetIndex_, rec);

  saveAsNew_    = false;
  editingPreset_= false;
  menuIndex_    = 0;
  setState(AppState::PresetList);
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
  lang_      = presets_.loadLanguage();
  servo_.begin();
  motor_.begin(&servo_);
  winding_.begin(&motor_);
#if ENABLE_GAUSS_METER
  gauss_.begin();
#endif

  draft_    = WindingProgram{};
  bootStep_ = 0;
  bootStepMs_= millis();
  setState(AppState::Boot);
}

void App::enterGaussZeroCal(bool returnToSettings) {
  gaussCalReturnToSettings_ = returnToSettings;
  gaussCalDoneMs_ = 0;
#if ENABLE_GAUSS_METER
  gauss_.startZeroCalibration();
  setState(AppState::GaussZeroCal);
#else
  finishGaussZeroCal();
#endif
}

void App::finishGaussZeroCal() {
  if (motor_.isEnabled()) {
    motor_.releaseMotor();
  }
  if (gaussCalReturnToSettings_) {
    gaussCalReturnToSettings_ = false;
    menuIndex_ = 0;
    setState(AppState::Settings);
  } else {
    menuIndex_  = 0;
    menuWindow_ = 0;
    setState(AppState::MainMenu);
  }
}

bool App::isSafetyCriticalState() const {
  return state_ == AppState::BootError || state_ == AppState::ErrorState;
}

bool App::motorActivityBlocksGaussOverlay() const {
#if !GAUSS_OVERLAY_DURING_MOTOR_RUN
  if (state_ == AppState::Winding || state_ == AppState::Countdown) return true;
  if (state_ == AppState::ManualMode &&
      (manualPhase_ == ManualPhase::Running ||
       manualPhase_ == ManualPhase::Braking ||
       manualPhase_ == ManualPhase::Reversing ||
       manualPhase_ == ManualPhase::TargetBraking ||
       manualPhase_ == ManualPhase::TargetApproach ||
       manualPhase_ == ManualPhase::TargetStopping ||
       manualTargetSigned_ != 0 ||
       motor_.actualRpmAbs() > MANUAL_STOPPED_RPM)) {
    return true;
  }
#endif
  return false;
}

bool App::shouldShowGaussOverlay() const {
#if !ENABLE_GAUSS_METER
  return false;
#else
  if (!gauss_.calibrationComplete() || !gauss_.calibrationValid()) return false;
  if (!gauss_.overlayRequested()) return false;
  if (isSafetyCriticalState()) return false;
  if (state_ == AppState::Boot || state_ == AppState::GaussZeroCal) return false;
  if (state_ == AppState::Countdown) return false;
  if (motorActivityBlocksGaussOverlay()) return false;
  return true;
#endif
}

void App::drawGaussCalibration() {
  ui_.setLine(0, tr(lang_, StrId::GaussCalibration));
  ui_.setLine(1, "");
  ui_.setLine(2, tr(lang_, StrId::RemoveMagnet));
  // Simple activity dots while calibrating.
  const uint8_t phase = static_cast<uint8_t>((millis() / 300) % 4);
  char dots[21] = "       ";
  for (uint8_t i = 0; i < 3; i++) {
    dots[7 + i] = (i <= phase) ? '.' : ' ';
  }
  dots[10] = '\0';
  if (gauss_.calibrationComplete()) {
    ui_.setLine(3, tr(lang_, StrId::GaussZeroOk));
  } else {
    ui_.setLine(3, dots);
  }
}

void App::drawGaussOverlay() {
  // Magnitude + separate pole — no signed ± on the user screen.
  ui_.setLine(0, tr(lang_, StrId::MagnetMeasurement));
  ui_.setLine(1, tr(lang_, StrId::MagnetStrength));

  const float g = gauss_.gauss();
  const int mag = static_cast<int>(lroundf(fabsf(g)));
  char lineG[21];
  snprintf(lineG, sizeof lineG, "     %d GAUSS", mag);
  ui_.setLine(2, lineG);

  const MagneticPole pole = gauss_.pole();
  const char* poleCh = (pole == MagneticPole::North) ? "N"
                       : (pole == MagneticPole::South) ? "S" : "-";
  char lineP[21];
  // Polish: "     BIEGUN: N" / English: "      POLE: N"
  if (lang_ == Language::Polish) {
    snprintf(lineP, sizeof lineP, "     %s: %s", tr(lang_, StrId::MagnetPole), poleCh);
  } else {
    snprintf(lineP, sizeof lineP, "      %s: %s", tr(lang_, StrId::MagnetPole), poleCh);
  }
  ui_.setLine(3, lineP);
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
    // Ensure idle shaft is free after probe — do not leave holding torque on.
    motor_.releaseMotor();
  }
  if (bootStep_ >= 7) {
    ui_.setLine(0, tr(lang_, StrId::AppTitle));
    ui_.setLine(1, tr(lang_, StrId::SystemReady));
    ui_.setLine(2, "");
    ui_.setLine(3, "");
    delay(200);
#if ENABLE_GAUSS_METER
    enterGaussZeroCal(false);
#else
    menuIndex_  = 0;
    menuWindow_ = 0;
    setState(AppState::MainMenu);
#endif
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// AUTO mode (target-based winding)
// ─────────────────────────────────────────────────────────────────────────────

void App::enterAutoEdit() {
  draft_     = WindingProgram{};
  editField_ = 0;
  digitPos_  = 0;
  editingPreset_ = false;
  saveAsNew_     = false;
  setState(AppState::AutoEdit);
}

void App::enterPresetEditNew() {
  draft_     = WindingProgram{};
  editField_ = 0;
  digitPos_  = 0;
  editingPreset_ = false;
  saveAsNew_     = true;
  // Clear any stale rename/edit buffer immediately so NEW always starts as PRESET.
  namePos_ = 0;
  initNameBuf("PRESET");
  setState(AppState::PresetEdit);
}

void App::enterPresetEditExisting() {
  PresetRecord p{};
  if (!presets_.get(presetIndex_, p)) { setState(AppState::PresetList); return; }
  draft_ = p.program;
  initNameBuf(p.name);
  editField_          = 0;
  digitPos_           = 0;
  editingPreset_      = true;
  editingPresetIndex_ = presetIndex_;
  saveAsNew_          = false;
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

void App::formatManualTurnsDigits(char* out, unsigned n, bool blink) const {
  snprintf(out, n, "%05lu", static_cast<unsigned long>(manualTargetTurns_));
  if (blink && ((millis() / 400) & 1)) {
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
  // "02.0s" layout: index 0='0', 1='2', 2='.', 3='0', 4='s'
  if (blink && ((millis() / 400) & 1)) {
    int idx = -1;
    if      (digitPos_ == 0) idx = 3;
    else if (digitPos_ == 1) idx = 1;
    else if (digitPos_ == 2) idx = 0;
    if (idx >= 0 && static_cast<unsigned>(idx) + 1 < n) out[idx] = '_';
  }
}

void App::adjustActiveDigit(int dir) {
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

  if      (editField_ == 0) { uint32_t v = draft_.targetTurns; bumpDigit(v, digitPos_, MIN_TURNS, MAX_TURNS); draft_.targetTurns = clampTurns(v); }
  else if (editField_ == 1) { uint32_t v = draft_.targetRpm;   bumpDigit(v, digitPos_, MIN_WINDER_RPM, MAX_WINDER_RPM); draft_.targetRpm = clampRpm(v); }
  else if (editField_ == 3 || editField_ == 5) {
    uint16_t& ms = (editField_ == 3) ? draft_.rampUpMs : draft_.rampDownMs;
    uint32_t tenths = ms / 100;
    bumpDigit(tenths, digitPos_, RAMP_TIME_MIN_MS/100, RAMP_TIME_MAX_MS/100);
    ms = clampRampMs(tenths * 100);
  }
  else if (editField_ == 2) draft_.rampUpType   = (draft_.rampUpType   == RampType::SCurve) ? RampType::Linear : RampType::SCurve;
  else if (editField_ == 4) draft_.rampDownType = (draft_.rampDownType == RampType::SCurve) ? RampType::Linear : RampType::SCurve;
  else if (editField_ == 6) draft_.direction    = (draft_.direction    == WindDir::CW) ? WindDir::CCW : WindDir::CW;
}

void App::adjustManualTurnsDigit(int dir) {
  // Same digit-by-digit philosophy as Auto, but 00000 is allowed (unlimited).
  uint32_t value = manualTargetTurns_;
  uint32_t placeVal = 1;
  for (uint8_t i = 0; i < digitPos_; i++) placeVal *= 10;
  int32_t digit = static_cast<int32_t>((value / placeVal) % 10) + dir;
  if (digit > 9) digit = 0;
  if (digit < 0) digit = 9;
  value = (value / (placeVal * 10)) * (placeVal * 10) + (value % placeVal)
          + static_cast<uint32_t>(digit) * placeVal;
  if (value > MAX_TURNS) value = MAX_TURNS;
  manualTargetTurns_ = value;
}

void App::onEditClick() {
  const uint8_t digs = digitsForField(editField_);
  if (digs > 0) {
    if (++digitPos_ >= digs) { digitPos_ = 0; editField_++; }
    return;
  }
  if (editField_ < 7) { editField_++; digitPos_ = 0; return; }
  // Field 7 = Start / Save
  if (state_ == AppState::PresetEdit) {
    namePos_ = 0;
    if (saveAsNew_) {
      // NEW preset: always force the editable default, never reuse stale buffer
      // and never copy the LCD title ("NAZWA PRESETU" / "PRESET NAME").
      initNameBuf("PRESET");
    } else if (!editingPreset_) {
      initNameBuf("PRESET");
    }
    // editing existing: nameBuf_ already loaded in enterPresetEditExisting()
    setState(AppState::PresetName);
  } else {
    setState(AppState::StartConfirm);
  }
}

void App::onManualTurnsSetupClick() {
  if (++digitPos_ >= TURNS_DIGITS) {
    digitPos_ = 0;
    enterManualMode();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// TRUE MANUAL MODE
// ─────────────────────────────────────────────────────────────────────────────

void App::enterManualTurnsSetup() {
  manualTargetTurns_ = DEFAULT_MANUAL_TURNS;
  digitPos_          = 0;
  setState(AppState::ManualTurnsSetup);
}

void App::enterManualMode() {
  manualTargetSigned_   = 0;
  manualPhase_          = ManualPhase::Idle;
  manualMotorDir_       = WindDir::CW;
  manualExitPending_    = false;
  manualTravelCounts_   = 0;
  manualAccelDir_       = 0;
  manualAccelStreak_    = 0;
  manualApproachIssued_ = false;
  manualTargetReached_  = false;
  manualTargetFinishDir_ = WindDir::CW;

  // 0 = unlimited free-running Manual; >0 enables encoder turn-limit stop.
  manualTargetEnabled_ = (manualTargetTurns_ > 0);
  manualTargetCounts_  = static_cast<uint64_t>(manualTargetTurns_) *
                         static_cast<uint64_t>(SERVO_COUNTS_PER_REV);

  motor_.prepareForWinding();

  manualEncStart_ = motor_.encoder();
  manualEncPrev_  = manualEncStart_;

  lastManualCmdMs_ = 0;
  lastManualTelMs_ = 0;
  setState(AppState::ManualMode);
}

uint64_t App::manualRemainingCounts() const {
  if (!manualTargetEnabled_) return UINT64_C(0xFFFFFFFFFFFFFFFF);
  if (manualTravelCounts_ >= manualTargetCounts_) return 0;
  return manualTargetCounts_ - manualTravelCounts_;
}

bool App::manualShouldStartTargetBrake(uint64_t remainingCounts, uint16_t actualRpm) const {
  if (!manualTargetEnabled_) return false;
  // Reuse Auto stopping-distance prediction (Ramp::stoppingTurns + compensation).
  const float stopTurns = Ramp::stoppingTurns(
      static_cast<float>(actualRpm),
      MANUAL_TARGET_RAMP_DOWN_MS / 1000.0f);
  const double needTurns =
      static_cast<double>(stopTurns) +
      (static_cast<double>(STOP_COMPENSATION_COUNTS) /
       static_cast<double>(SERVO_COUNTS_PER_REV));
  const uint64_t needCounts =
      static_cast<uint64_t>(needTurns * static_cast<double>(SERVO_COUNTS_PER_REV) + 0.5);
  return remainingCounts <= needCounts;
}

void App::finishManualTarget() {
  // Do not Complete while still spinning — enter TargetStopping first.
  manualTargetReached_  = true;
  manualTargetSigned_   = 0;
  manualApproachIssued_ = false;
  manualPhase_          = ManualPhase::TargetStopping;
  motor_.softStop();
#if WIND_TARGET_DEBUG
  Serial.println(F("[MANUAL] TARGET REACHED"));
  Serial.printf("[MANUAL] travel=%llu tgt=%llu\n",
                static_cast<unsigned long long>(manualTravelCounts_),
                static_cast<unsigned long long>(manualTargetCounts_));
  Serial.println(F("[MANUAL] STOPPING"));
#endif
}

// Returns |manualTargetSigned_| clamped to MAX_WINDER_RPM
static uint16_t absClamped(int16_t v) {
  int32_t a = v < 0 ? -static_cast<int32_t>(v) : static_cast<int32_t>(v);
  if (a > MAX_WINDER_RPM) a = MAX_WINDER_RPM;
  return static_cast<uint16_t>(a);
}

void App::tickManualMode(uint32_t nowMs) {
  // ── Telemetry ────────────────────────────────────────────────────
  if (nowMs - lastManualTelMs_ >= POSITION_POLL_MS) {
    lastManualTelMs_ = nowMs;
    motor_.pollTelemetry(nowMs);

    // Accumulate absolute physical travel (CW + CCW both count toward limit).
    const int64_t encNow   = motor_.encoder();
    const int64_t delta    = encNow - manualEncPrev_;
    const int64_t absDelta = (delta < 0) ? -delta : delta;
    manualTravelCounts_ += static_cast<uint64_t>(absDelta);
    manualEncPrev_ = encNow;
  }

  const uint16_t actualRpm  = motor_.actualRpmAbs();
  const bool     motorStop  = (actualRpm <= MANUAL_STOPPED_RPM);
  const uint64_t remCounts  = manualRemainingCounts();

  // ── Exit pending: stop first, release driver, then leave ─────────
  if (manualExitPending_) {
    if (nowMs - lastManualCmdMs_ >= MANUAL_COMMAND_UPDATE_MS) {
      lastManualCmdMs_ = nowMs;
      motor_.softStop();
    }
    if (motorStop) {
      motor_.releaseMotor();
      manualExitPending_ = false;
      setState(AppState::MainMenu);
      return;
    }
    return;
  }

  // ── Turn-limit: reached or passed? ───────────────────────────────
  if (manualTargetEnabled_ && !manualTargetReached_ && remCounts == 0) {
    finishManualTarget();
    // fall through into TargetStopping this tick
  }

  // ── Turn-limit: begin controlled stop before overshoot ───────────
  if (manualTargetEnabled_ && !manualTargetReached_ &&
      manualPhase_ != ManualPhase::TargetBraking &&
      manualPhase_ != ManualPhase::TargetApproach &&
      manualPhase_ != ManualPhase::TargetStopping &&
      manualShouldStartTargetBrake(remCounts, actualRpm)) {
    // Lock physical direction for the rest of target completion.
    manualTargetFinishDir_ = manualMotorDir_;
    manualTargetSigned_    = 0;
    manualApproachIssued_  = false;
    manualPhase_           = ManualPhase::TargetBraking;
#if WIND_TARGET_DEBUG
    Serial.printf("[MANUAL] target brake start rem=%.2f dir=%s\n",
                  static_cast<double>(remCounts) / SERVO_COUNTS_PER_REV,
                  manualTargetFinishDir_ == WindDir::CW ? "CW" : "CCW");
#endif
  }

  const int16_t  tgt        = manualTargetSigned_;
  const bool     tgtCw      = (tgt > 0);
  const bool     tgtStop    = (tgt == 0);

  // ── Manual FSM ───────────────────────────────────────────────────
  switch (manualPhase_) {

    case ManualPhase::Idle:
      if (!manualTargetReached_ && !tgtStop) {
        manualMotorDir_ = tgtCw ? WindDir::CW : WindDir::CCW;
        motor_.setDirection(manualMotorDir_);
        manualPhase_ = ManualPhase::Running;
      }
      break;

    case ManualPhase::Running: {
      if (manualTargetReached_) break;
      const bool runningCw = (manualMotorDir_ == WindDir::CW);
      if (!tgtStop && (tgtCw != runningCw)) {
        manualPhase_ = ManualPhase::Braking;
      } else if (tgtStop) {
        manualPhase_ = ManualPhase::Braking;
      } else {
        if (nowMs - lastManualCmdMs_ >= MANUAL_COMMAND_UPDATE_MS) {
          lastManualCmdMs_ = nowMs;
          servo_.speedRun(runningCw, absClamped(tgt), SERVO_INTERNAL_ACC);
        }
      }
      break;
    }

    case ManualPhase::Braking:
      if (nowMs - lastManualCmdMs_ >= MANUAL_COMMAND_UPDATE_MS) {
        lastManualCmdMs_ = nowMs;
        motor_.softStop();
      }
      if (motorStop) {
        manualPhase_ = ManualPhase::Reversing;
      }
      break;

    case ManualPhase::Reversing:
      if (tgtStop) {
        manualPhase_ = ManualPhase::Idle;
      } else {
        manualMotorDir_ = tgtCw ? WindDir::CW : WindDir::CCW;
        motor_.setDirection(manualMotorDir_);
        manualPhase_ = ManualPhase::Running;
        lastManualCmdMs_ = 0;
      }
      break;

    case ManualPhase::TargetBraking:
      // Controlled deceleration — direction locked, no reverse correction.
      if (nowMs - lastManualCmdMs_ >= MANUAL_COMMAND_UPDATE_MS) {
        lastManualCmdMs_ = nowMs;
        motor_.softStop();
      }
      if (motorStop) {
        if (remCounts == 0 ||
            remCounts <= static_cast<uint64_t>(FINAL_APPROACH_SKIP_COUNTS)) {
          finishManualTarget();
          break;
        }
        // Still short of target → same-direction low-speed F6 approach.
        manualApproachIssued_ = false;
        manualPhase_ = ManualPhase::TargetApproach;
#if WIND_TARGET_DEBUG
        Serial.printf("[MANUAL] target approach dir=%s rem=%.2f\n",
                      manualTargetFinishDir_ == WindDir::CW ? "CW" : "CCW",
                      static_cast<double>(remCounts) / SERVO_COUNTS_PER_REV);
#endif
      }
      break;

    case ManualPhase::TargetApproach: {
      // Low-speed F6 in locked winding direction ONLY. Never F4 / never reverse.
      if (manualTargetReached_ || remCounts == 0) {
        finishManualTarget();
        break;
      }
      // Early forward stop compensation — reduces overshoot, never reverses.
      if (remCounts <= static_cast<uint64_t>(FINAL_FORWARD_STOP_COMPENSATION_COUNTS)) {
        finishManualTarget();
        break;
      }
      if (nowMs - lastManualCmdMs_ >= MANUAL_COMMAND_UPDATE_MS) {
        lastManualCmdMs_ = nowMs;
        motor_.setDirection(manualTargetFinishDir_);
        motor_.commandFinalApproach(manualTargetFinishDir_);
        manualApproachIssued_ = true;
      }
      break;
    }

    case ManualPhase::TargetStopping:
      // Target latched: only STOP, then release driver, then Complete UI.
      if (nowMs - lastManualCmdMs_ >= MANUAL_COMMAND_UPDATE_MS) {
        lastManualCmdMs_ = nowMs;
        motor_.softStop();
      }
      if (motorStop) {
        motor_.releaseMotor();
#if WIND_TARGET_DEBUG
        Serial.println(F("[MANUAL] MOTOR RELEASED"));
#endif
        manualPhase_ = ManualPhase::Idle;
        setState(AppState::ManualComplete);
        return;
      }
      break;
  }

  // If somehow stopped with no target, ensure idle (user stop path only).
  if (!manualTargetReached_ && manualPhase_ == ManualPhase::Running && tgtStop) {
    manualPhase_ = ManualPhase::Braking;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// LCD rendering
// ─────────────────────────────────────────────────────────────────────────────

static void clampMenuWindow(uint8_t sel, uint8_t count, uint8_t& window) {
  if (count == 0) { window = 0; return; }
  if (sel < window)        window = sel;
  if (sel >= window + 4)   window = sel - 3;
  if (count <= 4)          window = 0;
}

// Safe: builds a 20-char display line without touching nameBuf_.
// Shows cursor position via a separate indicator line.
static void buildNameDisplayLine(const char* buf, int pos, char* out) {
  // out must be at least 22 bytes (20 chars + '[', ']', '\0')
  out[0] = '[';
  for (int i = 0; i < PRESET_NAME_LEN; i++) {
    out[1 + i] = buf[i];  // buf is space-padded, always printable
  }
  out[1 + PRESET_NAME_LEN] = ']';
  out[2 + PRESET_NAME_LEN] = '\0';
  // Truncate to 20 chars for LCD.
  out[20] = '\0';
  (void)pos;
}

// Builds a 20-char cursor-indicator line: spaces + '^' at position pos+1 (offset by '[').
static void buildNameCursorLine(int pos, char* out) {
  for (int i = 0; i < 20; i++) out[i] = ' ';
  out[20] = '\0';
  const int col = 1 + pos;  // +1 for the leading '['
  if (col >= 0 && col < 20) out[col] = '^';
}

void App::render(uint32_t nowMs) {
  const bool isEdit = (state_ == AppState::AutoEdit ||
                       state_ == AppState::PresetEdit ||
                       state_ == AppState::ManualTurnsSetup);
  const uint32_t refreshMs = isEdit ? 100u : LCD_UPDATE_MS;
  if (nowMs - lastLcdMs_ < refreshMs && state_ != AppState::Countdown &&
      state_ != AppState::GaussZeroCal) {
    return;
  }
  lastLcdMs_ = nowMs;

  // Priority: ERROR > Gauss calibration > Countdown > Gauss overlay > normal UI
  if (isSafetyCriticalState()) {
    ui_.drawError(lang_, tr(lang_, StrId::MotorError),
                  errorLine_ ? errorLine_ : tr(lang_, StrId::NoRs485));
    return;
  }
  if (state_ == AppState::GaussZeroCal) {
    drawGaussCalibration();
    return;
  }
  if (state_ == AppState::Countdown) {
    ui_.drawCountdown(countdown_);
    return;
  }
  if (shouldShowGaussOverlay()) {
    drawGaussOverlay();
    return;
  }

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
      const char* items[4] = {tr(lang_, StrId::Language),
                              tr(lang_, StrId::Diagnostics),
                              tr(lang_, StrId::ZeroGauss),
                              tr(lang_, StrId::Back)};
      clampMenuWindow(menuIndex_, 4, menuWindow_);
      ui_.drawMenu(lang_, items, 4, menuIndex_, menuWindow_);
      break;
    }
    case AppState::Language: {
      const char* items[2] = {tr(lang_, StrId::Polski), tr(lang_, StrId::English)};
      ui_.drawMenu(lang_, items, 2, menuIndex_, 0);
      break;
    }
    case AppState::Diagnostics:
      motor_.pollTelemetry(nowMs);
      ui_.drawDiagnostics(lang_, motor_.alarmOk(),
                          motor_.encoderOk() || servo_.failStreak() == 0,
                          motor_.actualRpmAbs(), motor_.encoder(), motor_.alarmStatus());
      break;

    // ── AUTO EDIT ─────────────────────────────────────────────────
    case AppState::AutoEdit:
    case AppState::PresetEdit: {
      const uint8_t FC = 8;
      uint8_t top = (editField_ < 3) ? 0 : static_cast<uint8_t>(editField_ - 2);
      if (top > FC - 4) top = FC - 4;
      for (uint8_t row = 0; row < 4; row++) {
        const uint8_t f = static_cast<uint8_t>(top + row);
        char val[16]; val[0] = 0;
        const char* label = "";
        const bool sel = (editField_ == f);
        switch (f) {
          case 0: label = tr(lang_, StrId::Turns);    formatTurnsDigits(val, sizeof val, sel);   break;
          case 1: label = tr(lang_, StrId::Rpm);      formatRpmDigits(val, sizeof val, sel);     break;
          case 2: label = tr(lang_, StrId::RampUp);   formatRampType(lang_, draft_.rampUpType, val, sizeof val); break;
          case 3: label = tr(lang_, StrId::UpTime);   formatRampTenthsDigits(val, sizeof val, draft_.rampUpMs, sel); break;
          case 4: label = tr(lang_, StrId::RampDown); formatRampType(lang_, draft_.rampDownType, val, sizeof val); break;
          case 5: label = tr(lang_, StrId::DownTime); formatRampTenthsDigits(val, sizeof val, draft_.rampDownMs, sel); break;
          case 6: label = tr(lang_, StrId::Direction);formatDir(lang_, draft_.direction, val, sizeof val); break;
          default:
            label = (state_ == AppState::PresetEdit) ? tr(lang_, StrId::Save) : tr(lang_, StrId::Start);
            break;
        }
        char line[21];
        if (f == 7) snprintf(line, sizeof line, "%c%s", sel ? '>' : ' ', label);
        else        snprintf(line, sizeof line, "%c%s:%s", sel ? '>' : ' ', label, val);
        ui_.setLine(row, line);
      }
      break;
    }

    // ── MANUAL TURN LIMIT SETUP ───────────────────────────────────
    case AppState::ManualTurnsSetup: {
      ui_.setLine(0, tr(lang_, StrId::ManualTitle));
      ui_.setLine(1, tr(lang_, StrId::ManualTurnLimit));
      char digits[8];
      formatManualTurnsDigits(digits, sizeof digits, true);
      ui_.setLine(2, digits);
      ui_.setLine(3, (manualTargetTurns_ == 0)
                         ? tr(lang_, StrId::Unlimited)
                         : tr(lang_, StrId::HoldBack));
      break;
    }

    // ── MANUAL MODE ───────────────────────────────────────────────
    case AppState::ManualMode: {
      // Derive display direction from signed target.
      const char* dirStr;
      if (manualTargetSigned_ > 0)      dirStr = "CW";
      else if (manualTargetSigned_ < 0)  dirStr = "CCW";
      else                               dirStr = tr(lang_, StrId::Stop);

      // Line 0: "TRYB RECZNY       CW" (20 chars)
      char l0[21];
      snprintf(l0, sizeof l0, "%-14s%6s", tr(lang_, StrId::ManualTitle), dirStr);
      ui_.setLine(0, l0);

      // Line 1: "UST:       850 RPM  " (SET)
      char l1[21];
      snprintf(l1, sizeof l1, "%-5s%9u RPM", tr(lang_, StrId::ManualSet), absClamped(manualTargetSigned_));
      ui_.setLine(1, l1);

      // Line 2: "AKT:       847 RPM  " (ACTUAL)
      char l2[21];
      snprintf(l2, sizeof l2, "%-5s%9u RPM", tr(lang_, StrId::ManualAct), motor_.actualRpmAbs());
      ui_.setLine(2, l2);

      // Line 3: "ZWOJE: 1234/8000" or "ZWOJE: 1234/----"
      const uint32_t travelTurns = static_cast<uint32_t>(
          manualTravelCounts_ / static_cast<uint64_t>(SERVO_COUNTS_PER_REV));
      char l3[21];
      if (!manualTargetEnabled_) {
        snprintf(l3, sizeof l3, "%-6s%5lu/----",
                 tr(lang_, StrId::ManualTurns),
                 static_cast<unsigned long>(travelTurns));
      } else {
        snprintf(l3, sizeof l3, "%-6s%5lu/%-5lu",
                 tr(lang_, StrId::ManualTurns),
                 static_cast<unsigned long>(travelTurns),
                 static_cast<unsigned long>(manualTargetTurns_));
      }
      ui_.setLine(3, l3);
      break;
    }

    case AppState::ManualComplete: {
      //     GOTOWE
      //   8000 / 8000
      // TRYB RECZNY
      // KLIK: PONOW
      char title[21];
      snprintf(title, sizeof title, "    %s", tr(lang_, StrId::Complete));
      ui_.setLine(0, title);
      char counts[21];
      snprintf(counts, sizeof counts, "  %lu / %lu",
               static_cast<unsigned long>(manualTargetTurns_),
               static_cast<unsigned long>(manualTargetTurns_));
      ui_.setLine(1, counts);
      ui_.setLine(2, tr(lang_, StrId::ManualTitle));
      ui_.setLine(3, tr(lang_, StrId::ClickAgain));
      break;
    }

    // ── PRESET LIST ───────────────────────────────────────────────
    case AppState::PresetList: {
      const uint8_t cnt   = presets_.count();
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

    // ── PRESET NAME EDITOR ────────────────────────────────────────
    case AppState::PresetName: {
      // Line 0: header
      ui_.setLine(0, tr(lang_, StrId::PresetName));

      // Line 1: "[STRAT NECK  ]"  — built from fixed-width buffer, NO terminator hack
      char dispLine[22];   // 1 + PRESET_NAME_LEN + 1 + '\0' = 15 max; LCD pads to 20
      buildNameDisplayLine(nameBuf_, namePos_, dispLine);
      ui_.setLine(1, dispLine);

      // Line 2: cursor indicator
      char cursorLine[21];
      buildNameCursorLine(namePos_, cursorLine);
      ui_.setLine(2, cursorLine);

      // Line 3: instructions
      ui_.setLine(3, tr(lang_, StrId::HoldSave));
      break;
    }

    case AppState::PresetDeleteConfirm: {
      ui_.setLine(0, tr(lang_, StrId::ConfirmDelete));
      char line[21];
      snprintf(line, sizeof line, "%c%s  %c%s",
               deleteYes_  ? '>' : ' ', tr(lang_, StrId::Yes),
               !deleteYes_ ? '>' : ' ', tr(lang_, StrId::No));
      line[20] = '\0';
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
    case AppState::GaussZeroCal:
      drawGaussCalibration();
      break;
    default: break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Input handling
// ─────────────────────────────────────────────────────────────────────────────

void App::handleInput(uint32_t nowMs) {
  (void)nowMs;
  const EncDetent  det = input_.takeDetent();
  const ButtonEvent btn = input_.takeButton();
  const int         dir = det.dir;

  // Character set for name editor.
  // SPACE sits immediately before A so spaces are one detent from letters.
  // Order: SPACE, A-Z, 0-9, '-', '_'
  static const char kCharSet[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
  static const int  kCharN     = 39;

  const bool gaussOverlay = shouldShowGaussOverlay();

  // ── ROTATION ─────────────────────────────────────────────────────
  if (dir != 0) {
    // While Gauss overlay covers menus/editors, do not move selections underneath.
    // Manual RPM control remains available (motor safety / workshop use).
    const bool blockMenuEnc = gaussOverlay &&
        state_ != AppState::ManualMode &&
        state_ != AppState::Winding &&
        state_ != AppState::Paused;

    if (!blockMenuEnc) {
    switch (state_) {
      // Menus with NO acceleration: always 1 step.
      case AppState::MainMenu:
        if (dir > 0) menuIndex_ = static_cast<uint8_t>((menuIndex_ + 1) % 4);
        else         menuIndex_ = (menuIndex_ == 0) ? 3 : menuIndex_ - 1;
        break;
      case AppState::Settings:
        if (dir > 0) menuIndex_ = static_cast<uint8_t>((menuIndex_ + 1) % 4);
        else         menuIndex_ = (menuIndex_ == 0) ? 3 : menuIndex_ - 1;
        break;
      case AppState::Language:
        menuIndex_ = (menuIndex_ == 0) ? 1 : 0;
        break;
      case AppState::PresetActions:
        if (dir > 0) actionIndex_ = static_cast<uint8_t>((actionIndex_ + 1) % 5);
        else         actionIndex_ = (actionIndex_ == 0) ? 4 : actionIndex_ - 1;
        break;
      case AppState::PresetDeleteConfirm:
        deleteYes_ = !deleteYes_;
        break;

      // Preset list: mild acceleration for long lists.
      case AppState::PresetList: {
        const uint8_t total = static_cast<uint8_t>(presets_.count() + 2);
        // Mild accel: x2 max, only at VeryFast.
        const int step = (det.speed == EncSpeed::VeryFast) ? PRESET_LIST_ACCEL_MAX : 1;
        if (dir > 0) menuIndex_ = static_cast<uint8_t>((menuIndex_ + step) % total);
        else {
          const int back = (total - step % total) % total;
          menuIndex_ = static_cast<uint8_t>((menuIndex_ + back) % total);
        }
        break;
      }

      // Digit editor: always exactly 1 (no accel).
      case AppState::AutoEdit:
      case AppState::PresetEdit:
        adjustActiveDigit(dir);  // step=1, wraps 0-9
        break;

      case AppState::ManualTurnsSetup:
        adjustManualTurnsDigit(dir);  // 1 detent = 1 digit, 00000 allowed
        break;

      // Preset name: conservative acceleration.
      case AppState::PresetName: {
        const int step = accelStepName(det.speed);
        char c = nameBuf_[namePos_];
        int idx = 0;
        for (int i = 0; i < kCharN; i++) { if (kCharSet[i] == c) { idx = i; break; } }
        if (dir > 0) idx = (idx + step)               % kCharN;
        else         idx = (idx - step % kCharN + kCharN) % kCharN;
        nameBuf_[namePos_] = kCharSet[idx];
        // nameBuf_[PRESET_NAME_LEN] stays '\0' permanently — never touched here.
        break;
      }

      // Manual: signed RPM with dedicated human-speed accel + zero clamp.
      // During target braking/approach, encoder must not re-accelerate.
      case AppState::ManualMode: {
        if (manualExitPending_) break;
        if (manualPhase_ == ManualPhase::TargetBraking ||
            manualPhase_ == ManualPhase::TargetApproach ||
            manualPhase_ == ManualPhase::TargetStopping) {
          break;  // turn-limit safety owns the motor
        }
        const int step = manualRpmStep(det);
        int32_t next = static_cast<int32_t>(manualTargetSigned_) + dir * step;

        // Clamp to valid range.
        if (next > static_cast<int32_t>(MAX_WINDER_RPM))   next =  static_cast<int32_t>(MAX_WINDER_RPM);
        if (next < -static_cast<int32_t>(MAX_WINDER_RPM))  next = -static_cast<int32_t>(MAX_WINDER_RPM);

        // Zero-crossing: if step would have crossed zero, stop at zero first.
        const bool prevPos  = (manualTargetSigned_ > 0);
        const bool prevNeg  = (manualTargetSigned_ < 0);
        const bool nextPos  = (next > 0);
        const bool nextNeg  = (next < 0);
        if ((prevPos && nextNeg) || (prevNeg && nextPos)) {
          next = 0;  // clamp at zero; user must apply another detent to cross
        }

        manualTargetSigned_ = static_cast<int16_t>(next);
        break;
      }
      default: break;
    }
    }  // !blockMenuEnc
  }

  // ── LONG PRESS ───────────────────────────────────────────────────
  // Safety controls remain active even while Gauss overlay is visible.
  if (btn == ButtonEvent::LongPress) {
    const bool blockMenuLong = gaussOverlay &&
        state_ != AppState::ManualMode &&
        state_ != AppState::Winding &&
        state_ != AppState::Paused;
    if (blockMenuLong) {
      // Ignore menu long-press under overlay (do not save/exit menus).
    } else
    switch (state_) {
      case AppState::MainMenu:   break;  // top-level, no parent
      case AppState::Settings:
      case AppState::Language:
      case AppState::Diagnostics:
        menuIndex_ = 0; setState(AppState::MainMenu); break;
      case AppState::GaussZeroCal:
        break;  // wait for calibration to finish
      case AppState::AutoEdit:
        setState(AppState::MainMenu); break;
      case AppState::ManualTurnsSetup:
        setState(AppState::MainMenu); break;
      case AppState::PresetList:
        menuIndex_ = 0; setState(AppState::MainMenu); break;
      case AppState::PresetActions:
      case AppState::PresetEdit:
      case AppState::PresetDeleteConfirm:
        menuIndex_ = 0; setState(AppState::PresetList); break;
      case AppState::PresetName:
        savePresetName(); break;
      case AppState::StartConfirm:
      case AppState::Countdown:
        setState(editingPreset_ || saveAsNew_ ? AppState::PresetList : AppState::AutoEdit);
        break;
      case AppState::Winding:
        winding_.requestPause(); break;
      case AppState::Paused:
        winding_.requestAbort(); break;
      case AppState::Complete:
      case AppState::Aborted:
        if (motor_.isEnabled()) motor_.releaseMotor();
        setState(AppState::MainMenu); break;
      case AppState::ManualComplete:
        if (motor_.isEnabled()) motor_.releaseMotor();
        setState(AppState::MainMenu); break;
      case AppState::ManualMode:
        // Single long-press: stop + exit when safe.
        manualTargetSigned_ = 0;
        manualExitPending_  = true;
        break;
      default: break;
    }
  }

  // ── SHORT CLICK ──────────────────────────────────────────────────
  // Safety clicks still work under Gauss overlay; menu clicks do not.
  if (btn == ButtonEvent::Click) {
    const bool blockMenuClick = gaussOverlay &&
        state_ != AppState::ManualMode &&
        state_ != AppState::Winding &&
        state_ != AppState::Paused &&
        state_ != AppState::BootError &&
        state_ != AppState::ErrorState;

    if (blockMenuClick) {
      // Swallow click so underlying menu/editor does not advance.
    } else
    switch (state_) {
      case AppState::BootError:
        bootStep_ = 0; setState(AppState::Boot); break;
      case AppState::ErrorState:
        setState(AppState::MainMenu); break;

      case AppState::MainMenu:
        menuWindow_ = 0;
        if      (menuIndex_ == 0) enterAutoEdit();
        else if (menuIndex_ == 1) enterManualTurnsSetup();
        else if (menuIndex_ == 2) { menuIndex_ = 0; setState(AppState::PresetList); }
        else                       { menuIndex_ = 0; setState(AppState::Settings); }
        break;

      case AppState::Settings:
        if      (menuIndex_ == 0) { menuIndex_ = (lang_ == Language::Polish) ? 0 : 1; setState(AppState::Language); }
        else if (menuIndex_ == 1) { motor_.detect(); setState(AppState::Diagnostics); }
        else if (menuIndex_ == 2) { enterGaussZeroCal(true); }
        else                       setState(AppState::MainMenu);
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

      case AppState::ManualTurnsSetup:
        onManualTurnsSetupClick(); break;

      case AppState::ManualMode:
        // Click = controlled stop (target → 0). Do NOT exit / reset turns.
        // Also allowed during target braking to request earlier stop (already 0).
        if (!manualExitPending_) {
          manualTargetSigned_ = 0;
        }
        break;

      case AppState::ManualComplete:
        // Same turn limit, fresh session at 0 RPM (counter resets in enterManualMode).
        enterManualMode();
        break;

      case AppState::PresetList: {
        const uint8_t backIdx = static_cast<uint8_t>(presets_.count() + 1);
        if      (menuIndex_ == 0)       enterPresetEditNew();
        else if (menuIndex_ == backIdx) { menuIndex_ = 0; setState(AppState::MainMenu); }
        else {
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
        if      (actionIndex_ == 0) { draft_ = p.program; setState(AppState::StartConfirm); }
        else if (actionIndex_ == 1) { enterPresetEditExisting(); }
        else if (actionIndex_ == 2) {
          initNameBuf(p.name);
          namePos_ = 0; editingPreset_ = true; editingPresetIndex_ = presetIndex_;
          saveAsNew_ = false; draft_ = p.program; setState(AppState::PresetName);
        } else {
          deleteYes_ = false; setState(AppState::PresetDeleteConfirm);
        }
        break;
      }
      case AppState::PresetName:
        // Click: confirm character and advance to next position.
        if (namePos_ < PRESET_NAME_LEN - 1) namePos_++;
        // nameBuf_[PRESET_NAME_LEN] stays '\0'; do NOT touch it.
        break;

      case AppState::PresetDeleteConfirm:
        if (deleteYes_) presets_.remove(presetIndex_);
        menuIndex_ = 0; setState(AppState::PresetList); break;

      case AppState::StartConfirm:
        startCountdown(); break;
      case AppState::Paused:
        winding_.requestResume(); setState(AppState::Winding); break;
      case AppState::Complete:
        startCountdown(); break;
      default: break;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Main loop
// ─────────────────────────────────────────────────────────────────────────────

void App::loop() {
  const uint32_t now = millis();

  if (now - lastInputMs_ >= INPUT_POLL_MS) {
    lastInputMs_ = now;
    input_.update(now);
  }

#if ENABLE_GAUSS_METER
  // Lower priority than motor control — sample after input, before heavy UI.
  gauss_.update(now);
#endif

  if (state_ == AppState::Boot) {
    handleBoot(now);
    return;
  }

  if (state_ == AppState::GaussZeroCal) {
#if ENABLE_GAUSS_METER
    if (gauss_.calibrationComplete()) {
      if (gaussCalDoneMs_ == 0) gaussCalDoneMs_ = now;
      if (now - gaussCalDoneMs_ >= 400) {
        gaussCalDoneMs_ = 0;
        finishGaussZeroCal();
      }
    }
#else
    finishGaussZeroCal();
#endif
    handleInput(now);
    render(now);
    return;
  }

  if (state_ == AppState::ManualMode) {
    tickManualMode(now);
  }

  if (state_ == AppState::Winding || state_ == AppState::Paused) {
    winding_.tick(now);
    if      (winding_.isPaused())   setState(AppState::Paused);
    else if (winding_.isComplete()) setState(AppState::Complete);
    else if (winding_.isAborted())  setState(AppState::Aborted);
    else if (winding_.isFault())    { errorLine_ = winding_.status().faultText; setState(AppState::ErrorState); }
    else if (state_ == AppState::Paused && !winding_.isPaused()) setState(AppState::Winding);
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
