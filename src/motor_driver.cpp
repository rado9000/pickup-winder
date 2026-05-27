/*
 * Napęd krokowy: STEP/DIR/EN + jeden timer Pico (50 µs).
 * RPM -> okres kroku; rampa +20 RPM / 600 ms bez restartu timera.
 * Brak FastAccelStepper, brak UART TMC.
 */
#include "motor_driver.h"
#include "config.h"

#include <hardware/gpio.h>
#include <pico/stdlib.h>

static struct repeating_timer tickTimer_;
static bool tickTimerOn_ = false;

static volatile bool run_ = false;
static volatile uint32_t stepPeriodUs_ = 1000000UL;
static volatile uint32_t stepAccumUs_ = 0;

static bool dirCw_ = true;
static int targetRpm_ = 0;
static int currentRpm_ = 0;
static bool ramping_ = false;
static uint32_t lastRampMs_ = 0;
static volatile long stepCount_ = 0;

static uint32_t usPerStep(int rpm) {
  if (rpm < MIN_RPM) {
    rpm = MIN_RPM;
  }
  uint32_t us = (uint32_t)(60000000UL / ((uint32_t)rpm * (uint32_t)STEPS_PER_REV));
  if (us < (uint32_t)MOTOR_MIN_STEP_US) {
    us = (uint32_t)MOTOR_MIN_STEP_US;
  }
  return us;
}

static void pulseStep() {
  gpio_put(STEP_PIN, 1);
  gpio_put(STEP_PIN, 0);
  stepCount_++;
}

static bool tickHandler(struct repeating_timer *rt) {
  (void)rt;
  if (!run_) {
    return true;
  }

  uint32_t period = stepPeriodUs_;
  stepAccumUs_ += (uint32_t)MOTOR_TICK_US;
  if (stepAccumUs_ < period) {
    return true;
  }
  stepAccumUs_ -= period;

  pulseStep();
  return true;
}

static void timerStart() {
  if (tickTimerOn_) {
    return;
  }
  if (add_repeating_timer_us(-(int64_t)MOTOR_TICK_US, tickHandler, nullptr,
                             &tickTimer_)) {
    tickTimerOn_ = true;
  }
}

static void timerStop() {
  if (tickTimerOn_) {
    cancel_repeating_timer(&tickTimer_);
    tickTimerOn_ = false;
  }
}

static void setRpmNow(int rpm) {
  currentRpm_ = rpm;
  stepPeriodUs_ = usPerStep(rpm);
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
  stepAccumUs_ = 0;

  motorEnable(true);
  setRpmNow(start);
  run_ = true;
  timerStart();
}

void motorUpdate() {
  if (!run_ || !ramping_) {
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
  setRpmNow(next);
}

void motorStopImmediate() {
  run_ = false;
  ramping_ = false;
  timerStop();
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
