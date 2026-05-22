#include "gauss_monitor.h"
#include "config.h"

static float gaussValue_ = 0.0f;
static float gaussBaselineG_ = 0.0f;
static int gaussReturnMode_ = 0;
static uint32_t gaussEnterSinceMs_ = 0;
static uint32_t gaussExitSinceMs_ = 0;

static bool isMenuScreen(int mode) {
  return mode >= 0 && mode <= 5;
}

static float readGaussRaw() {
  int raw = analogRead(HALL_ADC_PIN);
  float voltage = ((float)raw / (float)HALL_ADC_MAX) * HALL_ADC_REF_V;
  float deltaMv = (voltage - HALL_ZERO_V) * 1000.0f;
  return deltaMv / HALL_MV_PER_GAUSS;
}

static void refreshGaussValue() {
  gaussValue_ = readGaussRaw() - gaussBaselineG_;
}

void gaussBegin() {
#if USE_GAUSS_MONITOR
  pinMode(HALL_ADC_PIN, INPUT);
  analogReadResolution(12);
  gaussBaselineG_ = 0.0f;
  gaussValue_ = 0.0f;
  gaussEnterSinceMs_ = 0;
  gaussExitSinceMs_ = 0;
#endif
}

void gaussCalibrateZero() {
#if USE_GAUSS_MONITOR
  float sum = 0.0f;
  for (int i = 0; i < GAUSS_CALIB_SAMPLES; i++) {
    sum += readGaussRaw();
    delay(5);
  }
  gaussBaselineG_ = sum / (float)GAUSS_CALIB_SAMPLES;
  gaussValue_ = 0.0f;
  gaussEnterSinceMs_ = 0;
  gaussExitSinceMs_ = 0;
#endif
}

bool gaussBootSetup() {
#if !USE_GAUSS_MONITOR
  return true;
#else
  gaussCalibrateZero();
  return true;
#endif
}

float gaussValue() {
  return gaussValue_;
}

int gaussReturnScreenMode() {
  return gaussReturnMode_;
}

void gaussUpdate(uint32_t nowMs, int screenMode, int &ioScreenMode) {
#if !USE_GAUSS_MONITOR
  (void)nowMs;
  (void)screenMode;
  (void)ioScreenMode;
  return;
#else
  refreshGaussValue();
  float absGauss = fabsf(gaussValue_);

  if (ioScreenMode != 90 && isMenuScreen(screenMode)) {
    if (absGauss >= GAUSS_ENTER_THRESHOLD) {
      if (gaussEnterSinceMs_ == 0) {
        gaussEnterSinceMs_ = nowMs;
      }
      if (nowMs - gaussEnterSinceMs_ >= GAUSS_ENTER_HOLD_MS) {
        gaussReturnMode_ = screenMode;
        gaussExitSinceMs_ = 0;
        ioScreenMode = 90;
        return;
      }
    } else {
      gaussEnterSinceMs_ = 0;
    }
  }

  if (ioScreenMode == 90) {
    if (absGauss <= GAUSS_EXIT_THRESHOLD) {
      if (gaussExitSinceMs_ == 0) {
        gaussExitSinceMs_ = nowMs;
      }
      if (nowMs - gaussExitSinceMs_ >= GAUSS_EXIT_HOLD_MS) {
        gaussEnterSinceMs_ = 0;
        gaussExitSinceMs_ = 0;
        gaussCalibrateZero();
        ioScreenMode = gaussReturnMode_;
      }
    } else {
      gaussExitSinceMs_ = 0;
    }
  }
#endif
}
