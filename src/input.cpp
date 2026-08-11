#include "input.h"

#include <Arduino.h>

// Full Gray-code transition table for a quadrature encoder.
// State = (prevCLK<<1)|prevDT. Each row = [state0, state1, state2, state3].
// Value = step contribution:  0 = no step, +1 = CW, -1 = CCW, 99 = invalid/bounce.
static const int8_t kGrayTable[4][4] = {
//   to:  00   01   10   11
/*from 00*/ { 0,  +1,  -1,  99},
/*from 01*/ {-1,   0,  99,  +1},
/*from 10*/ {+1,  99,   0,  -1},
/*from 11*/ {99,  -1,  +1,   0},
};

// A complete mechanical detent on a 20 PPR KY-040 = 4 Gray-code transitions.
static constexpr int8_t kStepsPerDetent = 4;

void Input::begin() {
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);

  const uint8_t clk = digitalRead(PIN_ENC_CLK);
  const uint8_t dt  = digitalRead(PIN_ENC_DT);
  qState_      = static_cast<uint8_t>((clk << 1) | dt);
  subCount_    = 0;
  streakCount_ = 0;
  lastDir_     = 0;
  lastEdgeUs_  = micros();
  lastDetentMs_ = millis();
  swPrev_ = digitalRead(PIN_ENC_SW);
}

EncSpeed Input::classifySpeed(uint32_t dtMs, int8_t newDir, uint8_t streak) const {
  // Direction change or idle reset → always SLOW.
  if (newDir != 0 && lastDir_ != 0 && newDir != lastDir_) {
    return EncSpeed::Slow;
  }
  if (dtMs >= static_cast<uint32_t>(ENC_SLOW_THRESHOLD_MS)) {
    return EncSpeed::Slow;
  }
  // Determine raw band by timing.
  EncSpeed raw;
  if (dtMs >= static_cast<uint32_t>(ENC_MEDIUM_THRESHOLD_MS)) {
    raw = EncSpeed::Medium;
  } else if (dtMs >= static_cast<uint32_t>(ENC_FAST_THRESHOLD_MS)) {
    raw = EncSpeed::Fast;
  } else {
    raw = EncSpeed::VeryFast;
  }
  // Acceleration only activates after ENC_ACCEL_STREAK_REQUIRED consecutive
  // same-direction fast detents. Isolated fast bumps remain at SLOW.
  if (streak < ENC_ACCEL_STREAK_REQUIRED) {
    return EncSpeed::Slow;
  }
  return raw;
}

void Input::processStep(int8_t step, uint32_t nowMs) {
  if (step == 0) return;

  subCount_ += step;

  // Only emit a logical detent after a full cycle of kStepsPerDetent.
  if (subCount_ >= kStepsPerDetent) {
    subCount_ -= kStepsPerDetent;
  } else if (subCount_ <= -kStepsPerDetent) {
    subCount_ += kStepsPerDetent;
  } else {
    return;  // incomplete detent
  }

  // Raw Gray-code sense, then optional global logical inversion.
  // Applied once here — before streak, lastDir_, and pending detent —
  // so every UI context sees the same natural CW/CCW mapping.
  const int8_t rawDir = (step > 0) ? static_cast<int8_t>(+1)
                                   : static_cast<int8_t>(-1);
  const int8_t dir = ENCODER_INVERT_DIRECTION
                         ? static_cast<int8_t>(-rawDir)
                         : rawDir;

  const uint32_t dtMs = (nowMs > lastDetentMs_) ? (nowMs - lastDetentMs_) : 0;

  // Reset streak on idle or direction change.
  const bool isIdle = (dtMs >= static_cast<uint32_t>(ENC_ACCEL_RESET_MS));
  if (isIdle || (lastDir_ != 0 && dir != lastDir_)) {
    streakCount_ = 0;
  } else {
    if (streakCount_ < 255) streakCount_++;
  }

  const uint32_t effectiveDt = isIdle ? (ENC_SLOW_THRESHOLD_MS + 1) : dtMs;
  const EncSpeed spd = classifySpeed(effectiveDt, dir, streakCount_);

#if ENC_DEBUG
  const char* spdStr = (spd == EncSpeed::Slow)     ? "SLOW"
                       : (spd == EncSpeed::Medium)  ? "MED "
                       : (spd == EncSpeed::Fast)    ? "FAST"
                                                    : "XFST";
  Serial.printf("[ENC] dir=%+d dt=%4lums spd=%s streak=%u\n",
                static_cast<int>(dir),
                static_cast<unsigned long>(dtMs),
                spdStr,
                static_cast<unsigned>(streakCount_));
#endif

  lastDir_      = dir;
  lastDetentMs_ = nowMs;
  lastSpeed_    = spd;

  if (!detentPending_) {
    detentPending_  = true;
    pendingDetent_  = {dir, spd, dtMs};
  }
  // If a detent was already pending it means loop() is slower than encoder —
  // shouldn't happen at 1 ms polling, so we silently drop.
}

void Input::update(uint32_t nowMs) {
  // ── Encoder quadrature decode ────────────────────────────────────
  const uint8_t clk = static_cast<uint8_t>(digitalRead(PIN_ENC_CLK));
  const uint8_t dt  = static_cast<uint8_t>(digitalRead(PIN_ENC_DT));
  const uint8_t newState = static_cast<uint8_t>((clk << 1) | dt);

  if (newState != qState_) {
    const uint32_t nowUs = micros();
    const uint32_t elapsedUs = nowUs - lastEdgeUs_;

    if (elapsedUs >= static_cast<uint32_t>(ENC_DEBOUNCE_US)) {
      const int8_t step = kGrayTable[qState_][newState];
      if (step != 99) {         // valid Gray-code transition
        lastEdgeUs_ = nowUs;
        processStep(step, nowMs);
      }
      // Invalid/noise transitions are silently ignored (debounce will mask them).
    }
    qState_ = newState;
  }

  // ── Switch ───────────────────────────────────────────────────────
  const bool sw = digitalRead(PIN_ENC_SW);
  if (!sw && swPrev_) {
    swDownMs_  = millis();
    longFired_ = false;
  }
  if (!sw && !longFired_ && (millis() - swDownMs_ >= BUTTON_LONG_PRESS_MS)) {
    longFired_     = true;
    pendingButton_ = ButtonEvent::LongPress;
  }
  if (sw && !swPrev_) {
    if (!longFired_ && pendingButton_ == ButtonEvent::None) {
      pendingButton_ = ButtonEvent::Click;
    }
  }
  swPrev_ = sw;
}

EncDetent Input::takeDetent() {
  if (detentPending_) {
    detentPending_ = false;
    return pendingDetent_;
  }
  return {0, EncSpeed::Slow, 0};
}

ButtonEvent Input::takeButton() {
  const ButtonEvent ev = pendingButton_;
  pendingButton_ = ButtonEvent::None;
  return ev;
}
