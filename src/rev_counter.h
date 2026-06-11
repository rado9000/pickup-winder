#pragma once

#include <Arduino.h>
#include "motor.h"

class RevCounter {
 public:
  void begin(Servo42* driver, uint8_t hallPin = 0);
  void setMotorDirection(WindingDir dir) { motorDir_ = dir; }
  void poll();

  int32_t turns() const { return turns_; }
  void reset();
  void setTargetTurns(int32_t t) { targetTurns_ = t; }
  bool targetReached() const;

 private:
  Servo42* driver_ = nullptr;
  uint8_t hallPin_ = 0;
  bool useMotorEncoder_ = true;

  int32_t turns_ = 0;
  int32_t targetTurns_ = 0;
  WindingDir motorDir_ = WindingDir::CW;

  int64_t encoderBaseline_ = 0;
  int32_t hallCount_ = 0;

  bool lastHallLevel_ = true;
  uint32_t lastHallEdgeUs_ = 0;
};
