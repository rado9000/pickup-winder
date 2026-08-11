#include "gauss_meter.h"

#include <Arduino.h>
#include <math.h>
#include <stdlib.h>

#if ENABLE_GAUSS_METER

void GaussMeter::begin() {
  pinMode(PIN_GAUSS_ADC, INPUT);
  analogReadResolution(12);
  // 11 dB ≈ full-scale near 3.3 V — matches AH49HZ3-G1 at 3.3 V VCC.
  analogSetPinAttenuation(PIN_GAUSS_ADC, ADC_11db);

  raw_ = analogRead(PIN_GAUSS_ADC);
  filteredRaw_ = static_cast<float>(raw_);
  filterSeeded_ = true;
  medBuf_[0] = medBuf_[1] = medBuf_[2] = raw_;
  medCount_ = 3;
  medIdx_ = 0;

  Serial.println(F("[GAUSS] sensor=AH49HZ3-G1"));
  Serial.println(F("[GAUSS] VCC=3.3V"));
  Serial.printf("[GAUSS] ADC GPIO=%d\n", PIN_GAUSS_ADC);
  Serial.printf("[GAUSS] scale=%.4f G/count TEMP\n",
                static_cast<double>(GAUSS_G_PER_ADC_COUNT));
}

void GaussMeter::sampleAdc() {
  raw_ = analogRead(PIN_GAUSS_ADC);
  pushMedian(raw_);
  const int med = median3();
  if (!filterSeeded_) {
    filteredRaw_ = static_cast<float>(med);
    filterSeeded_ = true;
  } else {
    filteredRaw_ = filteredRaw_ + GAUSS_FILTER_ALPHA *
                       (static_cast<float>(med) - filteredRaw_);
  }
}

void GaussMeter::pushMedian(int v) {
  medBuf_[medIdx_] = v;
  medIdx_ = static_cast<uint8_t>((medIdx_ + 1) % 3);
  if (medCount_ < 3) medCount_++;
}

int GaussMeter::median3() const {
  if (medCount_ < 3) return raw_;
  int a = medBuf_[0], b = medBuf_[1], c = medBuf_[2];
  if (a > b) { int t = a; a = b; b = t; }
  if (b > c) { int t = b; b = c; c = t; }
  if (a > b) { int t = a; a = b; b = t; }
  return b;
}

void GaussMeter::startZeroCalibration() {
  calPhase_ = CalPhase::Settling;
  calStartMs_ = millis();
  calSum_ = 0.0;
  calSamples_ = 0;
  calMin_ = 4095;
  calMax_ = 0;
  calibrationValid_ = false;
  overlayActive_ = false;
  triggerTiming_ = false;
  releaseTiming_ = false;
  Serial.println(F("[GAUSS] zero calibration start"));
}

float GaussMeter::zeroedCounts() const {
  float d = filteredRaw_ - zeroRaw_;
  if (fabsf(d) <= static_cast<float>(GAUSS_ZERO_DEADBAND_COUNTS)) {
    return 0.0f;
  }
  return d;
}

float GaussMeter::gauss() const {
  return zeroedCounts() * GAUSS_G_PER_ADC_COUNT;
}

void GaussMeter::updateOverlay(uint32_t nowMs, float absG) {
  if (!calibrationValid_) {
    overlayActive_ = false;
    return;
  }

  if (!overlayActive_) {
    if (absG >= GAUSS_TRIGGER_G) {
      if (!triggerTiming_) {
        triggerTiming_ = true;
        triggerHoldStartMs_ = nowMs;
      } else if (nowMs - triggerHoldStartMs_ >= GAUSS_TRIGGER_HOLD_MS) {
        overlayActive_ = true;
        triggerTiming_ = false;
        releaseTiming_ = false;
        Serial.printf("[GAUSS] overlay ON raw=%d zero=%.1f delta=%.1f value=%+.1f G\n",
                      raw_,
                      static_cast<double>(zeroRaw_),
                      static_cast<double>(zeroedCounts()),
                      static_cast<double>(gauss()));
      }
    } else {
      triggerTiming_ = false;
    }
  } else {
    if (absG < GAUSS_RELEASE_G) {
      if (!releaseTiming_) {
        releaseTiming_ = true;
        releaseHoldStartMs_ = nowMs;
      } else if (nowMs - releaseHoldStartMs_ >= GAUSS_RELEASE_HOLD_MS) {
        overlayActive_ = false;
        releaseTiming_ = false;
        triggerTiming_ = false;
        Serial.printf("[GAUSS] overlay OFF value=%+.1f G\n",
                      static_cast<double>(gauss()));
      }
    } else {
      releaseTiming_ = false;
    }
  }
}

void GaussMeter::update(uint32_t nowMs) {
  // Calibration phases may sample faster than GAUSS_SAMPLE_MS for denser averages.
  const bool calBusy = (calPhase_ == CalPhase::Settling ||
                        calPhase_ == CalPhase::Sampling);

  if (!calBusy) {
    if (nowMs - lastSampleMs_ < GAUSS_SAMPLE_MS) {
      // Still refresh overlay timing with last value.
      if (calibrationValid_) {
        updateOverlay(nowMs, fabsf(gauss()));
      }
      return;
    }
    lastSampleMs_ = nowMs;
  } else if (nowMs - lastSampleMs_ < 2) {
    return;  // ~500 Hz during calibration settle/sample windows
  } else {
    lastSampleMs_ = nowMs;
  }

  sampleAdc();

  switch (calPhase_) {
    case CalPhase::Settling:
      if (nowMs - calStartMs_ >= GAUSS_ZERO_SETTLE_MS) {
        calPhase_ = CalPhase::Sampling;
        calStartMs_ = nowMs;
        calSum_ = 0.0;
        calSamples_ = 0;
        calMin_ = 4095;
        calMax_ = 0;
      }
      break;

    case CalPhase::Sampling: {
      const int v = raw_;
      calSum_ += static_cast<double>(v);
      calSamples_++;
      if (v < calMin_) calMin_ = v;
      if (v > calMax_) calMax_ = v;

      if (nowMs - calStartMs_ >= GAUSS_ZERO_CALIBRATION_MS && calSamples_ >= 16) {
        // Trimmed mean: drop one min and one max contribution when possible.
        double sum = calSum_;
        uint32_t n = calSamples_;
        if (n >= 8) {
          sum -= static_cast<double>(calMin_);
          sum -= static_cast<double>(calMax_);
          n -= 2;
        }
        zeroRaw_ = static_cast<float>(sum / static_cast<double>(n));
        filteredRaw_ = zeroRaw_;
        calibrationValid_ = true;
        calPhase_ = CalPhase::Done;
        Serial.printf("[GAUSS] zero raw=%.1f\n", static_cast<double>(zeroRaw_));
        Serial.printf("[GAUSS] zero noise range=%d\n", calMax_ - calMin_);
        Serial.println(F("[GAUSS] calibration complete"));
      }
      break;
    }

    case CalPhase::Done:
    case CalPhase::Idle:
      break;
  }

  if (calibrationValid_ && calPhase_ == CalPhase::Done) {
    updateOverlay(nowMs, fabsf(gauss()));
#if GAUSS_DEBUG
    static uint32_t lastDbg = 0;
    if (nowMs - lastDbg >= 500) {
      lastDbg = nowMs;
      Serial.printf("[GAUSS] raw=%d filt=%.1f zero=%.1f delta=%.1f G=%.1f\n",
                    raw_, static_cast<double>(filteredRaw_),
                    static_cast<double>(zeroRaw_),
                    static_cast<double>(zeroedCounts()),
                    static_cast<double>(gauss()));
    }
#endif
  }
}

#else  // !ENABLE_GAUSS_METER

void GaussMeter::begin() {}
void GaussMeter::update(uint32_t) {}
void GaussMeter::startZeroCalibration() {}
float GaussMeter::zeroedCounts() const { return 0.0f; }
float GaussMeter::gauss() const { return 0.0f; }
void GaussMeter::sampleAdc() {}
void GaussMeter::pushMedian(int) {}
int GaussMeter::median3() const { return 0; }
void GaussMeter::updateOverlay(uint32_t, float) {}

#endif
