#include "input.h"

#include <Arduino.h>

void Input::begin() {
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  lastClk_ = digitalRead(PIN_ENC_CLK);
  swPrev_ = digitalRead(PIN_ENC_SW);
  lastEdgeUs_ = micros();
}

void Input::update(uint32_t nowMs) {
  (void)nowMs;

  const int8_t clk = digitalRead(PIN_ENC_CLK);
  if (clk != lastClk_) {
    const uint32_t nowUs = micros();
    // Debounce contact bounce on 20 PPR mechanical encoder.
    if (nowUs - lastEdgeUs_ >= ENC_DEBOUNCE_US) {
      // One detent ≈ one falling edge on CLK for typical KY-040 modules.
      if (lastClk_ == HIGH && clk == LOW) {
        if (digitalRead(PIN_ENC_DT) == HIGH) {
          pendingDelta_++;
        } else {
          pendingDelta_--;
        }
        lastEdgeUs_ = nowUs;
      }
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
  // Coalesce burst bounce into at most one detent step per poll.
  int d = 0;
  if (pendingDelta_ > 0) {
    d = 1;
    pendingDelta_--;
  } else if (pendingDelta_ < 0) {
    d = -1;
    pendingDelta_++;
  }
  // Drop residual noise from the same burst.
  if ((d > 0 && pendingDelta_ > 0) || (d < 0 && pendingDelta_ < 0)) {
    pendingDelta_ = 0;
  }
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
  return InputEvent::None;
}
