#include "motor_driver.h"
#include "config.h"

#include <hardware/gpio.h>
#include <pico/stdlib.h>

#if USE_TMC2209_UART
#include "tmc2209_driver.h"
#endif

static bool directionCW_ = true;
static bool motorRunning_ = false;
static struct repeating_timer stepTimer_;
static bool stepTimerLive_ = false;

static int windTargetRpm_ = 0;
static int windCurrentRpm_ = 0;
static bool windRamping_ = false;
static uint32_t windLastStepMs_ = 0;
static volatile long stepCount_ = 0;

static int rampStartRpmForTarget(int targetRpm) {
  int start = MOTOR_RPM_RAMP_START;
  if (targetRpm < start) {
    return targetRpm;
  }
  return start;
}

static uint32_t stepIntervalUsForRpm(int rpm) {
  if (rpm < MIN_RPM) {
    rpm = MIN_RPM;
  }
  uint32_t us = (uint32_t)(60000000UL / ((uint32_t)rpm * (uint32_t)STEPS_PER_REV));
  if (us < MOTOR_MIN_STEP_INTERVAL_US) {
    us = MOTOR_MIN_STEP_INTERVAL_US;
  }
  return us;
}

static bool stepTimerHandler(struct repeating_timer *rt) {
  (void)rt;
  gpio_put(STEP_PIN, 1);
  busy_wait_us(MOTOR_STEP_PULSE_US);
  gpio_put(STEP_PIN, 0);
  stepCount_++;
  return true;
}

static void stopStepTimer() {
  if (stepTimerLive_) {
    cancel_repeating_timer(&stepTimer_);
    stepTimerLive_ = false;
  }
}

static void applyStepRateRpm(int rpm) {
  stopStepTimer();
  if (!motorRunning_) {
    return;
  }
  uint32_t intervalUs = stepIntervalUsForRpm(rpm);
  if (add_repeating_timer_us(-(int64_t)intervalUs, stepTimerHandler, nullptr,
                             &stepTimer_)) {
    stepTimerLive_ = true;
  }
}

void motorDriverBegin() {
  gpio_init(STEP_PIN);
  gpio_init(DIR_PIN);
  gpio_init(EN_PIN);
  gpio_set_dir(STEP_PIN, GPIO_OUT);
  gpio_set_dir(DIR_PIN, GPIO_OUT);
  gpio_set_dir(EN_PIN, GPIO_OUT);
  gpio_put(STEP_PIN, 0);
  gpio_put(EN_PIN, 1);
  motorSetDirection(true);
}

void motorSetDirection(bool cw) {
  directionCW_ = cw;
  gpio_put(DIR_PIN, (cw == DIR_CW_LEVEL) ? 1 : 0);
}

void motorEnable(bool on) {
  gpio_put(EN_PIN, on ? 0 : 1);
}

bool motorDirectionCW() {
  return directionCW_;
}

void motorStartWinding(int targetRpm) {
  if (targetRpm < MIN_RPM) {
    targetRpm = MIN_RPM;
  } else if (targetRpm > MAX_RPM) {
    targetRpm = MAX_RPM;
  }

#if USE_TMC2209_UART
  (void)tmc2209ApplySpreadCycleOnce();
#endif

  motorStopImmediate();

  windTargetRpm_ = targetRpm;
  windCurrentRpm_ = rampStartRpmForTarget(targetRpm);
  windRamping_ = true;
  windLastStepMs_ = millis();
  stepCount_ = 0;

  motorEnable(true);
  motorRunning_ = true;
  applyStepRateRpm(windCurrentRpm_);
}

void motorUpdate() {
  if (!motorRunning_ || !windRamping_) {
    return;
  }
  if (windCurrentRpm_ >= windTargetRpm_) {
    return;
  }

  uint32_t nowMs = millis();
  if (nowMs - windLastStepMs_ < (uint32_t)MOTOR_RPM_RAMP_INTERVAL_MS) {
    return;
  }
  windLastStepMs_ = nowMs;

  windCurrentRpm_ += MOTOR_RPM_RAMP_STEP;
  if (windCurrentRpm_ > windTargetRpm_) {
    windCurrentRpm_ = windTargetRpm_;
  }

  applyStepRateRpm(windCurrentRpm_);
}

void motorStopImmediate() {
  windRamping_ = false;
  motorRunning_ = false;
  stopStepTimer();
  gpio_put(STEP_PIN, 0);
}

void motorSingleStep(bool cw) {
  motorSetDirection(cw);
  gpio_put(STEP_PIN, 1);
  busy_wait_us(MOTOR_STEP_PULSE_US);
  gpio_put(STEP_PIN, 0);
  stepCount_++;
}

void motorResetStepAccumulator() {
  stepCount_ = 0;
}

long motorCurrentSteps() {
  return stepCount_;
}
