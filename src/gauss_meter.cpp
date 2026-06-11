#include "gauss_meter.h"
#include "config.h"

void GaussMeter::begin(uint8_t adcPin) {
  pin_ = adcPin;
  analogReadResolution(12);
  gauss_ = 0;
  active_ = false;
  calibrated_ = false;
  bootMs_ = millis();
  resetQuietAccum();
  rezeroPending_ = false;
}

float GaussMeter::readMillivolts() const {
  uint32_t sum = 0;
  for (int i = 0; i < GAUSS_ADC_SAMPLES; i++) {
    sum += analogRead(pin_);
  }
  float avg = (float)sum / GAUSS_ADC_SAMPLES;
  return avg * 3300.0f / 4095.0f;
}

float GaussMeter::millivoltsToGauss(float mv) const {
  return (mv - offsetMv_) / GAUSS_MV_PER_G;
}

void GaussMeter::resetQuietAccum() {
  quietSinceMs_ = 0;
  quietSumMv_ = 0;
  quietCount_ = 0;
}

void GaussMeter::accumulateQuiet(float mv) {
  if (quietSinceMs_ == 0) {
    quietSinceMs_ = millis();
  }
  quietSumMv_ += mv;
  quietCount_++;
}

bool GaussMeter::quietReady() const {
  if (quietSinceMs_ == 0 || quietCount_ < GAUSS_CALIB_SAMPLES) {
    return false;
  }
  return millis() - quietSinceMs_ >= GAUSS_CALIB_QUIET_MS;
}

void GaussMeter::applyOffset(float avgMv) {
  offsetMv_ = avgMv;
  calibrated_ = true;
  gauss_ = 0;
  resetQuietAccum();
  rezeroPending_ = false;
}

void GaussMeter::startRezero() {
  calibrated_ = false;
  active_ = false;
  gauss_ = 0;
  resetQuietAccum();
  rezeroPending_ = true;
}

void GaussMeter::update() {
  float mv = readMillivolts();

  if (!calibrated_) {
    gauss_ = 0;
    active_ = false;

    if (!rezeroPending_ && millis() - bootMs_ < GAUSS_WARMUP_MS) {
      return;
    }

    float roughG = (mv - offsetMv_) / GAUSS_MV_PER_G;
    if (offsetMv_ == 0 && !rezeroPending_) {
      roughG = (mv - 1650.0f) / GAUSS_MV_PER_G;
    }

    if (fabsf(roughG) < GAUSS_CALIB_MAX_G) {
      accumulateQuiet(mv);
      if (quietReady()) {
        applyOffset(quietSumMv_ / quietCount_);
      }
    } else {
      resetQuietAccum();
    }
    return;
  }

  gauss_ = millivoltsToGauss(mv);
  float absG = fabsf(gauss_);

  bool wasActive = active_;
  if (!active_ && absG >= GAUSS_ACTIVATE_G) {
    active_ = true;
  } else if (active_ && absG < GAUSS_ACTIVATE_G) {
    active_ = false;
  }

#if GAUSS_REZERO_ON_FIELD_REMOVE
  if (wasActive && !active_ && absG < GAUSS_CALIB_MAX_G) {
    startRezero();
  }
#endif
}
