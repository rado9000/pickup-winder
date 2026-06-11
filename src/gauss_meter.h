#pragma once

#include <Arduino.h>

class GaussMeter {
 public:
  void begin(uint8_t adcPin);
  void update();

  float gauss() const { return gauss_; }
  bool active() const { return active_; }

 private:
  uint8_t pin_ = 1;
  float gauss_ = 0;
  bool active_ = false;

  float readGauss();
};
