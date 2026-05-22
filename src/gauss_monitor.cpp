#include "gauss_monitor.h"
#include "config.h"

static float gaussValue_ = 0.0f;
static int32_t gaussBaselineAdc_ = 0;
static int gaussReturnMode_ = 0;
static uint32_t gaussEnterSinceMs_ = 0;
static uint32_t gaussExitSinceMs_ = 0;
static bool gaussReady_ = false;
static uint32_t gaussBootDoneMs_ = 0;

static bool isMenuScreen(int mode) {
  return mode >= 0 && mode <= 5;
}

// G = (różnica ADC) przeliczona na mV, potem / mV/G
static float gaussFromAdcDelta(int32_t deltaAdc) {
  float deltaMv =
      ((float)deltaAdc * HALL_ADC_REF_V / (float)HALL_ADC_MAX) * 1000.0f;
  return deltaMv / HALL_MV_PER_GAUSS;
}

static int readAdcFiltered() {
  (void)analogRead(HALL_ADC_PIN);
  delayMicroseconds(200);
  int a = analogRead(HALL_ADC_PIN);
  delayMicroseconds(200);
  int b = analogRead(HALL_ADC_PIN);
  return (a + b) / 2;
}

static void refreshGaussValue() {
  int raw = readAdcFiltered();
  gaussValue_ = gaussFromAdcDelta((int32_t)raw - gaussBaselineAdc_);
}

void gaussBegin() {
#if USE_GAUSS_MONITOR
  pinMode(HALL_ADC_PIN, INPUT);
  analogReadResolution(12);
  gaussBaselineAdc_ = 0;
  gaussValue_ = 0.0f;
  gaussReady_ = false;
  gaussEnterSinceMs_ = 0;
  gaussExitSinceMs_ = 0;
  gaussBootDoneMs_ = 0;
#endif
}

void gaussCalibrateZero() {
#if USE_GAUSS_MONITOR
  for (int i = 0; i < 8; i++) {
    (void)analogRead(HALL_ADC_PIN);
    delay(5);
  }

  int64_t sumAdc = 0;
  for (int i = 0; i < GAUSS_CALIB_SAMPLES; i++) {
    sumAdc += readAdcFiltered();
    delay(10);
  }
  gaussBaselineAdc_ = (int32_t)(sumAdc / GAUSS_CALIB_SAMPLES);

  refreshGaussValue();
  if (fabsf(gaussValue_) > 2.0f) {
    float corrCounts = (gaussValue_ * HALL_MV_PER_GAUSS) /
                       (1000.0f * HALL_ADC_REF_V / (float)HALL_ADC_MAX);
    gaussBaselineAdc_ += (int32_t)lroundf(corrCounts);
    gaussValue_ = 0.0f;
  }

  gaussEnterSinceMs_ = 0;
  gaussExitSinceMs_ = 0;
#endif
}

bool gaussBootSetup() {
#if !USE_GAUSS_MONITOR
  return true;
#else
  gaussCalibrateZero();
  gaussReady_ = true;
  gaussBootDoneMs_ = millis() + 400;
  gaussValue_ = 0.0f;
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
  if (!gaussReady_) {
    return;
  }

  refreshGaussValue();
  float absGauss = fabsf(gaussValue_);

  if (nowMs < gaussBootDoneMs_) {
    gaussEnterSinceMs_ = 0;
    return;
  }

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
