#pragma once

#include <Arduino.h>

enum class WindingDir : int8_t { CW = 1, CCW = -1 };

class StepMotor {
 public:
  void begin(uint8_t stepPin, uint8_t dirPin, uint8_t enPin);
  void setupTmc2208Uart();

  void setDirection(WindingDir dir);
  WindingDir direction() const { return dir_; }

  void enable(bool on);
  bool enabled() const { return enabled_; }

  void setTargetRpm(uint16_t rpm);
  uint16_t targetRpm() const { return targetRpm_; }
  uint16_t currentRpm() const { return currentRpm_; }

  void tick();

  bool isStopped() const { return currentRpm_ == 0; }

 private:
  uint8_t step_ = 0, dirPin_ = 0, en_ = 0;
  WindingDir dir_ = WindingDir::CW;
  bool enabled_ = false;

  uint16_t targetRpm_ = 0;
  uint16_t currentRpm_ = 0;
  uint32_t stepIntervalUs_ = 0;
  uint32_t lastStepUs_ = 0;
  uint32_t rampStartMs_ = 0;
  uint16_t rampStartRpm_ = 0;
  bool ramping_ = false;

  void applyStepInterval();
  void stepPulse();
};
