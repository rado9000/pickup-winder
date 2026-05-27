/*
 * STEP = PWM sprzetowy RP2040 (GP2). DIR/EN = GPIO.
 * Rampa: tylko zmiana czestotliwosci PWM (+2 RPM / 10 ms), bez kasowania timera.
 * TMC2209: MS + VREF + SpreadCycle na module (bez UART).
 */
#include "motor_driver.h"
#include "config.h"

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <pico/stdlib.h>

static uint stepSlice_ = 0;
static uint stepChannel_ = 0;
static bool pwmReady_ = false;

static bool motorRunning_ = false;
static bool dirCw_ = true;
static int targetRpm_ = 0;
static int currentRpm_ = 0;
static bool ramping_ = false;
static uint32_t lastRampMs_ = 0;
static long stepCount_ = 0;

static void stepPinAsGpioOut() {
  pwm_set_enabled(stepSlice_, false);
  gpio_set_function(STEP_PIN, GPIO_FUNC_SIO);
  gpio_set_dir(STEP_PIN, GPIO_OUT);
  gpio_put(STEP_PIN, 0);
}

static void stepPwmBegin() {
  gpio_set_function(STEP_PIN, GPIO_FUNC_PWM);
  stepSlice_ = pwm_gpio_to_slice_num(STEP_PIN);
  stepChannel_ = pwm_gpio_to_channel(STEP_PIN);
  pwm_set_enabled(stepSlice_, false);
  pwmReady_ = true;
}

static void setStepFrequencyHz(uint32_t hz) {
  if (!pwmReady_) {
    return;
  }
  if (hz < 1) {
    stepPinAsGpioOut();
    return;
  }

  gpio_set_function(STEP_PIN, GPIO_FUNC_PWM);

  const uint32_t sysHz = clock_get_hz(clk_sys);
  const uint16_t top = 999;
  float div = (float)sysHz / ((float)hz * (float)(top + 1));
  if (div < 1.0f) {
    div = 1.0f;
  }
  if (div > 255.0f) {
    div = 255.0f;
  }

  pwm_config cfg = pwm_get_default_config();
  pwm_config_set_clkdiv(&cfg, div);
  pwm_config_set_wrap(&cfg, top);
  pwm_init(stepSlice_, &cfg, true);
  pwm_set_chan_level(stepSlice_, stepChannel_, top / 2);
  pwm_set_enabled(stepSlice_, true);
}

static uint32_t rpmToStepHz(int rpm) {
  if (rpm < 1) {
    return 0;
  }
  return ((uint32_t)rpm * (uint32_t)STEPS_PER_REV) / 60U;
}

static void applyStepRateRpm(int rpm) {
  currentRpm_ = rpm;
  if (!motorRunning_) {
    setStepFrequencyHz(0);
    return;
  }
  setStepFrequencyHz(rpmToStepHz(rpm));
}

void motorDriverBegin() {
  gpio_init(STEP_PIN);
  gpio_init(DIR_PIN);
  gpio_init(EN_PIN);
  gpio_set_dir(DIR_PIN, GPIO_OUT);
  gpio_set_dir(EN_PIN, GPIO_OUT);
  gpio_put(EN_PIN, 1);

  stepPwmBegin();
  pwm_set_chan_level(stepSlice_, stepChannel_, 0);
  pwm_set_enabled(stepSlice_, false);
  motorSetDirection(true);
}

void motorSetDirection(bool cw) {
  dirCw_ = cw;
  gpio_put(DIR_PIN, (cw == (DIR_CW_LEVEL == HIGH)) ? 1 : 0);
}

void motorEnable(bool on) {
  gpio_put(EN_PIN, on ? 0 : 1);
}

bool motorDirectionCW() {
  return dirCw_;
}

void motorStartWinding(int targetRpm) {
  if (targetRpm < MIN_RPM) {
    targetRpm = MIN_RPM;
  }
  if (targetRpm > MAX_RPM) {
    targetRpm = MAX_RPM;
  }

  motorStopImmediate();

  targetRpm_ = targetRpm;
  int start = MOTOR_RPM_RAMP_START;
  if (targetRpm < start) {
    start = targetRpm;
  }

  ramping_ = true;
  lastRampMs_ = millis();
  stepCount_ = 0;

  motorEnable(true);
  motorRunning_ = true;
  applyStepRateRpm(start);
}

void motorUpdate() {
  if (!motorRunning_ || !ramping_) {
    return;
  }
  if (currentRpm_ >= targetRpm_) {
    ramping_ = false;
    return;
  }

  uint32_t now = millis();
  if (now - lastRampMs_ < (uint32_t)MOTOR_RPM_RAMP_INTERVAL_MS) {
    return;
  }
  lastRampMs_ = now;

  int next = currentRpm_ + MOTOR_RPM_RAMP_STEP;
  if (next > targetRpm_) {
    next = targetRpm_;
  }
  applyStepRateRpm(next);
}

void motorStopImmediate() {
  motorRunning_ = false;
  ramping_ = false;
  setStepFrequencyHz(0);
}

void motorSingleStep(bool cw) {
  bool wasRunning = motorRunning_;
  int saveRpm = currentRpm_;

  if (wasRunning) {
    setStepFrequencyHz(0);
  }

  motorSetDirection(cw);
  gpio_put(STEP_PIN, 1);
  busy_wait_us(MOTOR_STEP_PULSE_US);
  gpio_put(STEP_PIN, 0);
  stepCount_++;

  if (wasRunning) {
    applyStepRateRpm(saveRpm);
  }
}

void motorResetStepAccumulator() {
  stepCount_ = 0;
}

long motorCurrentSteps() {
  return stepCount_;
}
