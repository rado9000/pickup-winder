#include "gauss_monitor.h"
#include "config.h"

static float gaussValue_ = 0.0f;
static float gaussZeroVoltage_ = HALL_ZERO_V;
static int gaussReturnMode_ = 0;
static uint32_t gaussEnterSinceMs_ = 0;
static uint32_t gaussExitSinceMs_ = 0;
static uint32_t gaussMenuArmMs_ = 0;
static bool gaussAutoEnterEnabled_ = false;

static bool isMenuScreen(int mode) {
  return mode >= 0 && mode <= 5;
}

static float readVoltage() {
  int raw = analogRead(HALL_ADC_PIN);
  return ((float)raw / (float)HALL_ADC_MAX) * HALL_ADC_REF_V;
}

static void refreshGaussValue() {
  float voltage = readVoltage();
  float deltaMv = (voltage - gaussZeroVoltage_) * 1000.0f;
  gaussValue_ = deltaMv / HALL_MV_PER_GAUSS;
}

void gaussBegin() {
#if USE_GAUSS_MONITOR
  pinMode(HALL_ADC_PIN, INPUT);
  analogReadResolution(12);
  gaussAutoEnterEnabled_ = false;
  gaussMenuArmMs_ = 0;
  gaussEnterSinceMs_ = 0;
  gaussExitSinceMs_ = 0;
#endif
}

void gaussCalibrateZero() {
#if USE_GAUSS_MONITOR
  long sum = 0;
  for (int i = 0; i < GAUSS_CALIB_SAMPLES; i++) {
    sum += analogRead(HALL_ADC_PIN);
    delayMicroseconds(800);
  }
  float avgRaw = (float)sum / (float)GAUSS_CALIB_SAMPLES;
  gaussZeroVoltage_ = (avgRaw / (float)HALL_ADC_MAX) * HALL_ADC_REF_V;
  gaussValue_ = 0.0f;
  gaussEnterSinceMs_ = 0;
  gaussExitSinceMs_ = 0;
#endif
}

bool gaussBootSetup() {
#if !USE_GAUSS_MONITOR
  return true;
#else
  gaussAutoEnterEnabled_ = false;
  gaussMenuArmMs_ = millis() + GAUSS_MENU_ARM_MS;

  for (int attempt = 0; attempt < 12; attempt++) {
    gaussCalibrateZero();
    delay(80);
    refreshGaussValue();
    if (fabsf(gaussValue_) <= GAUSS_BOOT_OK_G) {
      gaussValue_ = 0.0f;
      gaussAutoEnterEnabled_ = true;
      return true;
    }
  }

  gaussCalibrateZero();
  gaussValue_ = 0.0f;
  gaussAutoEnterEnabled_ = true;
  return fabsf(gaussValue_) <= GAUSS_BOOT_OK_G * 3.0f;
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

  if (!gaussAutoEnterEnabled_ || nowMs < gaussMenuArmMs_) {
    gaussEnterSinceMs_ = 0;
    return;
  }

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
