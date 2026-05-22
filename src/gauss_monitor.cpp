#include "gauss_monitor.h"
#include "config.h"

static float gaussValue_ = 0.0f;
static float gaussZeroOffsetMv_ = 0.0f;
static bool gaussActive_ = false;
static int gaussReturnMode_ = 0;
static uint32_t gaussEnterSinceMs_ = 0;
static uint32_t gaussExitSinceMs_ = 0;

void gaussBegin() {
#if USE_GAUSS_MONITOR
  pinMode(HALL_ADC_PIN, INPUT);
  analogReadResolution(12);
  gaussCalibrateZero();
#else
  gaussZeroOffsetMv_ = 0.0f;
#endif
}

void gaussCalibrateZero() {
#if USE_GAUSS_MONITOR
  long sum = 0;
  for (int i = 0; i < GAUSS_CALIB_SAMPLES; i++) {
    sum += analogRead(HALL_ADC_PIN);
    delay(2);
  }
  float avgRaw = (float)sum / (float)GAUSS_CALIB_SAMPLES;
  float avgVoltage = (avgRaw / (float)HALL_ADC_MAX) * HALL_ADC_REF_V;
  gaussZeroOffsetMv_ = (avgVoltage - HALL_ZERO_V) * 1000.0f;
#endif
}

float gaussValue() {
  return gaussValue_;
}

bool gaussActive() {
  return gaussActive_;
}

static bool isMenuScreen(int mode) {
  return mode == 0 || mode == 1 || mode == 2 || mode == 3 || mode == 4 || mode == 5;
}

void gaussForceExit() {
  gaussActive_ = false;
  gaussEnterSinceMs_ = 0;
  gaussExitSinceMs_ = 0;
}

void gaussUpdate(uint32_t nowMs, int screenMode, int &ioScreenMode, bool menuVisible) {
#if !USE_GAUSS_MONITOR
  (void)nowMs;
  (void)screenMode;
  (void)ioScreenMode;
  (void)menuVisible;
  gaussActive_ = false;
  return;
#else
  (void)menuVisible;
  int raw = analogRead(HALL_ADC_PIN);
  float voltage = ((float)raw / (float)HALL_ADC_MAX) * HALL_ADC_REF_V;
  float deltaMv = (voltage - HALL_ZERO_V) * 1000.0f - gaussZeroOffsetMv_;
  gaussValue_ = deltaMv / HALL_MV_PER_GAUSS;
  float absGauss = fabsf(gaussValue_);

  if (!gaussActive_ && isMenuScreen(screenMode)) {
    if (absGauss >= GAUSS_ENTER_THRESHOLD) {
      if (gaussEnterSinceMs_ == 0) {
        gaussEnterSinceMs_ = nowMs;
      }
      if (nowMs - gaussEnterSinceMs_ >= GAUSS_ENTER_HOLD_MS) {
        gaussReturnMode_ = screenMode;
        gaussActive_ = true;
        gaussExitSinceMs_ = 0;
        ioScreenMode = 90;  // SCREEN_GAUSS
        return;
      }
    } else {
      gaussEnterSinceMs_ = 0;
    }
  }

  if (gaussActive_ && ioScreenMode == 90) {
    if (absGauss <= GAUSS_EXIT_THRESHOLD) {
      if (gaussExitSinceMs_ == 0) {
        gaussExitSinceMs_ = nowMs;
      }
      if (nowMs - gaussExitSinceMs_ >= GAUSS_EXIT_HOLD_MS) {
        gaussActive_ = false;
        gaussEnterSinceMs_ = 0;
        gaussExitSinceMs_ = 0;
        ioScreenMode = gaussReturnMode_;
      }
    } else {
      gaussExitSinceMs_ = 0;
    }
  }
#endif
}
