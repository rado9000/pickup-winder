#include "input.h"

#include <Arduino.h>

void Input::begin() {
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  lastClk_ = digitalRead(PIN_ENC_CLK);
  swPrev_ = digitalRead(PIN_ENC_SW);
}

void Input::update(uint32_t nowMs) {
  (void)nowMs;
  const int8_t clk = digitalRead(PIN_ENC_CLK);
  if (clk != lastClk_) {
    if (digitalRead(PIN_ENC_DT) != clk) {
      pendingDelta_++;
    } else {
      pendingDelta_--;
    }
    lastClk_ = clk;
  }

  const bool sw = digitalRead(PIN_ENC_SW);
  if (!sw && swPrev_) {
    swDownMs_ = millis();
    longFired_ = false;
  }
  if (!sw && !longFired_ && (millis() - swDownMs_ >= BUTTON_LONG_PRESS_MS)) {
    longFired_ = true;
    longPending_ = true;
  }
  if (sw && !swPrev_) {
    if (!longFired_) {
      clickPending_ = true;
    }
  }
  swPrev_ = sw;
}

int Input::takeDelta() {
  const int d = pendingDelta_;
  pendingDelta_ = 0;
  return d;
}

InputEvent Input::takeEvent() {
  if (longPending_) {
    longPending_ = false;
    return InputEvent::LongPress;
  }
  if (clickPending_) {
    clickPending_ = false;
    return InputEvent::Click;
  }
  if (pendingDelta_ > 0) {
    pendingDelta_--;
    return InputEvent::RotateCW;
  }
  if (pendingDelta_ < 0) {
    pendingDelta_++;
    return InputEvent::RotateCCW;
  }
  return InputEvent::None;
}
