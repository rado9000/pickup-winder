#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

enum class InputEvent : uint8_t { None, RotateCW, RotateCCW, Click, LongPress };

class Input {
 public:
  void begin();
  void update(uint32_t nowMs);
  InputEvent takeEvent();
  int takeDelta();  // accumulated rotation steps since last take

 private:
  int8_t lastClk_ = 0;
  int pendingDelta_ = 0;
  bool swPrev_ = true;
  uint32_t swDownMs_ = 0;
  bool longFired_ = false;
  bool clickPending_ = false;
  bool longPending_ = false;
};
