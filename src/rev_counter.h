#pragma once

#include <Arduino.h>
#include "motor.h"

class RevCounter {
 public:
  void begin(uint8_t pin, uint32_t debounceUs);
  void setMotorDirection(WindingDir dir) { motorDir_ = dir; }
  void poll();

  int32_t turns() const { return turnCount_ / pulsesPerRev_; }
  void reset() { turnCount_ = 0; }
  void setTargetTurns(int32_t t) { targetTurns_ = t; }
  bool targetReached() const;

 private:
  uint8_t pin_ = 0;
  uint32_t debounceUs_ = 0;
  uint8_t pulsesPerRev_ = 1;
  volatile int32_t turnCount_ = 0;
  int32_t targetTurns_ = 0;
  WindingDir motorDir_ = WindingDir::CW;

  bool lastLevel_ = true;
  uint32_t lastEdgeUs_ = 0;

  static void isrWrapper();
  static RevCounter* instance_;
  void onEdge();
};
