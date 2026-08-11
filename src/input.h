#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// Velocity bands for encoder acceleration.
enum class EncSpeed : uint8_t { Slow = 0, Medium, Fast, VeryFast };

// One valid detent event (always +1 or -1).
struct EncDetent {
  int8_t dir;       // +1 CW, -1 CCW, 0 = no new detent this poll
  EncSpeed speed;   // velocity band of this detent (only valid when dir!=0)
  uint32_t dtMs;    // ms since previous detent (0 if first after idle)
};

enum class ButtonEvent : uint8_t { None, Click, LongPress };

// Low-level 20 PPR KY-040 encoder + switch driver.
// Returns exactly one logical step per mechanical detent.
// Contact bounce is suppressed by ENC_DEBOUNCE_US.
// Velocity is computed from inter-detent timing for use by higher-level code.
class Input {
 public:
  void begin();
  void update(uint32_t nowMs);

  // Returns at most one detent per call (drain with a loop if needed).
  EncDetent takeDetent();

  // Button events (long-press fires once on threshold, no extra click on release).
  ButtonEvent takeButton();

  // Raw velocity for display / debug.
  EncSpeed lastSpeed() const { return lastSpeed_; }

 private:
  // Encoder state
  int8_t lastClk_ = 1;
  uint32_t lastEdgeUs_ = 0;
  int8_t lastDir_ = 0;
  uint32_t lastDetentMs_ = 0;
  EncSpeed lastSpeed_ = EncSpeed::Slow;

  // Queued detent (at most 1 pending between update() calls)
  bool detentPending_ = false;
  EncDetent pendingDetent_{};

  // Switch state
  bool swPrev_ = true;
  uint32_t swDownMs_ = 0;
  bool longFired_ = false;
  ButtonEvent pendingButton_ = ButtonEvent::None;

  EncSpeed classify(uint32_t dtMs, int8_t newDir) const;
};
