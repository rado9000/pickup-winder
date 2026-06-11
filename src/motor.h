#pragma once

#include <Arduino.h>
#include "servo42.h"

enum class WindingDir : int8_t { CW = 1, CCW = -1 };

class WindingMotor {
 public:
  void begin(Servo42* driver);

  void setDirection(WindingDir dir);
  WindingDir direction() const { return dir_; }

  void enable(bool on);
  bool enabled() const { return enabled_; }

  void setTargetRpm(uint16_t rpm, uint8_t acc = SERVO42_DEFAULT_ACC);
  uint16_t targetRpm() const { return targetRpm_; }
  uint16_t currentRpm() const { return currentRpm_; }

  void tick();
  bool isStopped() const;
  void waitUntilStopped();
  void quickStop();

  Servo42* driver() { return driver_; }

 private:
  Servo42* driver_ = nullptr;
  WindingDir dir_ = WindingDir::CW;
  bool enabled_ = false;
  uint16_t targetRpm_ = 0;
  uint16_t currentRpm_ = 0;
  uint32_t stoppedSinceMs_ = 0;
};
