#pragma once

#include <Arduino.h>

class GaussMeter {
 public:
  void begin(uint8_t adcPin);
  void update();

  float gauss() const { return gauss_; }
  bool active() const { return active_; }
  bool calibrated() const { return calibrated_; }
  bool calibrating() const { return !calibrated_; }

 private:
  uint8_t pin_ = 1;
  float gauss_ = 0;
  bool active_ = false;
  bool calibrated_ = false;

  uint32_t bootMs_ = 0;
  uint32_t quietSinceMs_ = 0;
  float quietSumMv_ = 0;
  uint16_t quietCount_ = 0;
  float offsetMv_ = 0;

  bool rezeroPending_ = false;

  float readMillivolts() const;
  float millivoltsToGauss(float mv) const;
  void resetQuietAccum();
  void accumulateQuiet(float mv);
  bool quietReady() const;
  void applyOffset(float avgMv);
  void startRezero();
};
