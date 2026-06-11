#pragma once

#include <Arduino.h>

class RotaryEncoder {
 public:
  void begin(uint8_t clkPin, uint8_t dtPin, uint8_t swPin);
  void update();

  int delta();
  bool clicked();
  bool longPressed();

  void setLongPressMs(uint32_t ms) { longPressMs_ = ms; }

 private:
  uint8_t clk_ = 0, dt_ = 0, sw_ = 0;
  int8_t lastClkState_ = 0;
  int pendingDelta_ = 0;

  bool swPrev_ = true;
  uint32_t swDownMs_ = 0;
  bool longFired_ = false;
  bool clickPending_ = false;
  uint32_t longPressMs_ = 800;
};
