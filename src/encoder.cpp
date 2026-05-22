#include "encoder.h"

void RotaryEncoder::begin(uint8_t clkPin, uint8_t dtPin, uint8_t swPin) {
  clk_ = clkPin;
  dt_ = dtPin;
  sw_ = swPin;
  pinMode(clk_, INPUT_PULLUP);
  pinMode(dt_, INPUT_PULLUP);
  pinMode(sw_, INPUT_PULLUP);
  lastClkState_ = digitalRead(clk_);
  swPrev_ = digitalRead(sw_);
}

void RotaryEncoder::update() {
  int8_t clk = digitalRead(clk_);
  if (clk != lastClkState_) {
    if (digitalRead(dt_) != clk) {
      pendingDelta_++;
    } else {
      pendingDelta_--;
    }
    lastClkState_ = clk;
  }

  bool sw = digitalRead(sw_);
  if (!sw && swPrev_) {
    swDownMs_ = millis();
    longFired_ = false;
  }
  if (!sw && !longFired_ && (millis() - swDownMs_ >= longPressMs_)) {
    longFired_ = true;
  }
  if (sw && !swPrev_) {
    if (!longFired_) {
      clickPending_ = true;
    }
  }
  swPrev_ = sw;
}

int RotaryEncoder::delta() {
  int d = pendingDelta_;
  pendingDelta_ = 0;
  return d;
}

bool RotaryEncoder::clicked() {
  if (clickPending_) {
    clickPending_ = false;
    return true;
  }
  return false;
}

bool RotaryEncoder::longPressed() {
  if (longFired_) {
    longFired_ = false;
    return true;
  }
  return false;
}
