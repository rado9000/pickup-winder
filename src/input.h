#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// Velocity bands (for use by UI context — never applied inside input module).
enum class EncSpeed : uint8_t { Slow = 0, Medium, Fast, VeryFast };

// One valid mechanical detent event.
struct EncDetent {
  int8_t   dir;    // +1 or -1; 0 = no pending event
  EncSpeed speed;  // velocity band (based on inter-detent timing + streak)
  uint32_t dtMs;   // ms since previous valid detent
};

enum class ButtonEvent : uint8_t { None, Click, LongPress };

// Low-level KY-040 encoder + switch driver.
//
// Decoder: full-quadrature Gray-code state machine.
// Tracks (A,B) = (CLK,DT) transitions. A complete detent corresponds to the
// full 4-step Gray-code cycle: 00→01→11→10→00 (CW) or reverse (CCW).
// Invalid transitions (noise/bounce) are ignored.
//
// Acceleration: based on inter-detent timing *and* a streak counter.
// A streak counts consecutive detents in the same direction within the
// same velocity band. Acceleration kicks in only after ENC_ACCEL_STREAK_REQUIRED
// consecutive genuinely-fast detents — single fast bumps stay at SLOW.
class Input {
 public:
  void begin();
  void update(uint32_t nowMs);

  // Returns one pending logical detent per call (zero-copy drain in a loop).
  EncDetent takeDetent();

  // Button events. Long-press fires once; release does NOT generate an extra click.
  ButtonEvent takeButton();

  EncSpeed lastSpeed() const { return lastSpeed_; }

 private:
  // Quadrature Gray-code state machine.
  // State encodes (prevA, prevB) as 2-bit value: bit1=CLK, bit0=DT.
  uint8_t qState_ = 0;   // 0-3
  int8_t  subCount_ = 0; // accumulated half-steps toward a full detent

  // Velocity / streak tracking
  uint32_t lastDetentMs_  = 0;
  uint32_t lastEdgeUs_    = 0;
  int8_t   lastDir_       = 0;
  uint8_t  streakCount_   = 0;
  EncSpeed lastSpeed_     = EncSpeed::Slow;

  // Pending detent queue (depth 1 — always drained before next update at 1 ms poll)
  bool     detentPending_ = false;
  EncDetent pendingDetent_{};

  // Switch state
  bool     swPrev_       = true;
  uint32_t swDownMs_     = 0;
  bool     longFired_    = false;
  ButtonEvent pendingButton_ = ButtonEvent::None;

  void processStep(int8_t step, uint32_t nowMs);
  EncSpeed classifySpeed(uint32_t dtMs, int8_t newDir, uint8_t streak) const;
};
