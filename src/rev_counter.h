#pragma once

#include <Arduino.h>
#include "motor.h"

// Turn counting via SERVO42 built-in encoder (RS485 cmd 0x31).
class RevCounter {
 public:
  void begin(Servo42* driver);
  void setMotorDirection(WindingDir dir) { motorDir_ = dir; }
  void poll();

  int32_t turns() const { return turns_; }
  void reset();
  void setTargetTurns(int32_t t) { targetTurns_ = t; }
  bool targetReached() const;

 private:
  Servo42* driver_ = nullptr;

  int32_t turns_ = 0;
  int32_t targetTurns_ = 0;
  WindingDir motorDir_ = WindingDir::CW;

  int64_t encoderBaseline_ = 0;
};
