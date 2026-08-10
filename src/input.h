#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

enum class InputEvent : uint8_t { None, RotateCW, RotateCCW, Click, LongPress };

// KY-040 style 20 PPR rotary encoder with switch.
// Emits one step per mechanical detent (falling CLK + debounce).
class Input {
 public:
  void begin();
  void update(uint32_t nowMs);
  InputEvent takeEvent();
  // Detent steps since last take; magnitude is typically 0/1 after filtering.
  int takeDelta();

 private:
  int8_t lastClk_ = 1;
  int pendingDelta_ = 0;
  uint32_t lastEdgeUs_ = 0;

  bool swPrev_ = true;
  uint32_t swDownMs_ = 0;
  bool longFired_ = false;
  bool clickPending_ = false;
  bool longPending_ = false;
};
