#include "input.h"

#include <Arduino.h>

void Input::begin() {
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  lastClk_ = digitalRead(PIN_ENC_CLK);
  swPrev_ = digitalRead(PIN_ENC_SW);
  lastEdgeUs_ = micros();
  lastDetentMs_ = millis();
}

EncSpeed Input::classify(uint32_t dtMs, int8_t newDir) const {
  // Direction reversal → reset to SLOW (first step after reversal is precise).
  if (newDir != 0 && lastDir_ != 0 && newDir != lastDir_) {
    return EncSpeed::Slow;
  }
  if (dtMs >= static_cast<uint32_t>(ENC_SLOW_THRESHOLD_MS)) {
    return EncSpeed::Slow;
  }
  if (dtMs >= static_cast<uint32_t>(ENC_FAST_THRESHOLD_MS)) {
    if (dtMs >= static_cast<uint32_t>(ENC_MEDIUM_THRESHOLD_MS)) {
      return EncSpeed::Medium;
    }
    return EncSpeed::Fast;
  }
  return EncSpeed::VeryFast;
}

void Input::update(uint32_t nowMs) {
  // --- Encoder ---
  const int8_t clk = digitalRead(PIN_ENC_CLK);
  if (clk != lastClk_) {
    const uint32_t nowUs = micros();
    const uint32_t elapsedUs = nowUs - lastEdgeUs_;

    if (elapsedUs >= static_cast<uint32_t>(ENC_DEBOUNCE_US)) {
      // Only count falling CLK edge (KY-040 standard).
      if (lastClk_ == HIGH && clk == LOW) {
        const int8_t dir = (digitalRead(PIN_ENC_DT) == HIGH) ? +1 : -1;
        const uint32_t dtMs =
            (nowMs > lastDetentMs_) ? (nowMs - lastDetentMs_) : 0;
        // If idle for too long, treat as fresh start (SLOW).
        const uint32_t effectiveDt =
            (dtMs > static_cast<uint32_t>(ENC_ACCEL_RESET_MS)) ? ENC_SLOW_THRESHOLD_MS + 1 : dtMs;
        const EncSpeed spd = classify(effectiveDt, dir);

#if ENC_DEBUG
        const char* spdStr = (spd == EncSpeed::Slow)     ? "SLOW"
                             : (spd == EncSpeed::Medium)  ? "MED "
                             : (spd == EncSpeed::Fast)    ? "FAST"
                                                          : "XFST";
        Serial.printf("[ENC] dir=%+d dt=%4lums spd=%s\n",
                      static_cast<int>(dir), static_cast<unsigned long>(dtMs), spdStr);
#endif

        lastDir_ = dir;
        lastDetentMs_ = nowMs;
        lastSpeed_ = spd;
        lastEdgeUs_ = nowUs;

        // Queue at most one detent per poll; extras are discarded (shouldn't happen at 1 ms poll).
        if (!detentPending_) {
          detentPending_ = true;
          pendingDetent_ = {dir, spd, dtMs};
        }
      }
    }
    lastClk_ = clk;
  }

  // --- Switch ---
  const bool sw = digitalRead(PIN_ENC_SW);
  if (!sw && swPrev_) {
    swDownMs_ = millis();
    longFired_ = false;
  }
  if (!sw && !longFired_ && (millis() - swDownMs_ >= BUTTON_LONG_PRESS_MS)) {
    longFired_ = true;
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
