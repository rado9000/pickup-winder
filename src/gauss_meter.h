#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// AH49HZ3-G1 bipolar linear Hall → ESP32-S3 ADC on PIN_GAUSS_ADC.
// Zero field ≠ ADC 0; zero offset is measured at every boot (or Settings re-zero).
class GaussMeter {
 public:
  void begin();
  void update(uint32_t nowMs);

  // Non-blocking zero calibration (settle + multi-sample average).
  void startZeroCalibration();

  bool calibrating() const { return calPhase_ != CalPhase::Idle && calPhase_ != CalPhase::Done; }
  bool calibrationComplete() const { return calPhase_ == CalPhase::Done; }
  bool calibrationValid() const { return calibrationValid_; }

  int raw() const { return raw_; }
  float filteredRaw() const { return filteredRaw_; }
  float zeroRaw() const { return zeroRaw_; }
  float zeroedCounts() const;
  float gauss() const;

  // True while automatic magnet overlay should be shown (hysteresis applied).
  bool overlayRequested() const { return overlayActive_; }

 private:
  enum class CalPhase : uint8_t { Idle, Settling, Sampling, Done };

  void sampleAdc();
  void pushMedian(int v);
  int median3() const;
  void updateOverlay(uint32_t nowMs, float absG);

  int raw_ = 0;
  float filteredRaw_ = 0.0f;
  float zeroRaw_ = 0.0f;
  bool filterSeeded_ = false;
  bool calibrationValid_ = false;

  int medBuf_[3]{};
  uint8_t medCount_ = 0;
  uint8_t medIdx_ = 0;

  CalPhase calPhase_ = CalPhase::Idle;
  uint32_t calStartMs_ = 0;
  double calSum_ = 0.0;
  uint32_t calSamples_ = 0;
  int calMin_ = 0;
  int calMax_ = 0;

  bool overlayActive_ = false;
  uint32_t triggerHoldStartMs_ = 0;
  uint32_t releaseHoldStartMs_ = 0;
  bool triggerTiming_ = false;
  bool releaseTiming_ = false;

  uint32_t lastSampleMs_ = 0;
};
