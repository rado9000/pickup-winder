#include "motor_driver.h"
#include "config.h"

#include <hardware/timer.h>

static bool directionCW_ = true;
static float commandedStepHz_ = 0.0f;
static float stepAccumulator_ = 0.0f;
static uint32_t lastStepUpdateUs_ = 0;

static bool rampActive_ = false;
static uint32_t rampStartMs_ = 0;
static uint32_t lastRampUpdateMs_ = 0;
static uint16_t rampDurationMs_ = 0;
static float rampStartHz_ = 0.0f;
static float rampTargetHz_ = 0.0f;
static int commandedRpm_ = 0;

static bool stopRequested_ = false;
static bool stopForCompletion_ = false;
static bool pauseRequested_ = false;
static bool menuRequested_ = false;

static struct repeating_timer stepTimer_;
static volatile bool stepTimerActive_ = false;
static uint32_t stepTimerPeriodUs_ = 0;

static int64_t stepPulseLowAlarm(alarm_id_t id, void *user_data) {
  (void)id;
  (void)user_data;
  digitalWrite(STEP_PIN, LOW);
  return 0;
}

static bool stepTimerHandler(struct repeating_timer *rt) {
  (void)rt;
  digitalWrite(STEP_PIN, HIGH);
  add_alarm_in_us(STEP_PULSE_WIDTH_US, stepPulseLowAlarm, nullptr, true);
  return true;
}

static void stopStepTimer() {
  if (stepTimerActive_) {
    cancel_repeating_timer(&stepTimer_);
    stepTimerActive_ = false;
    stepTimerPeriodUs_ = 0;
  }
}

static void stopPwmSteps() {
  analogWrite(STEP_PIN, 0);
}

static void stopStepOutput() {
  stopStepTimer();
  stopPwmSteps();
  commandedStepHz_ = 0.0f;
}

static void startStepTimer(uint32_t periodUs) {
  if (periodUs < STEP_TIMER_MIN_PERIOD_US) {
    periodUs = STEP_TIMER_MIN_PERIOD_US;
  }
  if (stepTimerActive_ && periodUs == stepTimerPeriodUs_) {
    return;
  }
  stopStepTimer();
  stepTimerPeriodUs_ = periodUs;
  if (add_repeating_timer_us(-(int64_t)periodUs, stepTimerHandler, nullptr, &stepTimer_)) {
    stepTimerActive_ = true;
  }
}

static float rpmToStepHz(int rpm) {
  if (rpm <= 0) {
    return 0.0f;
  }
  return (rpm / 60.0f) * (float)STEPS_PER_REV;
}

static int hzToRpm(float hz) {
  if (hz <= 0.0f) {
    return 0;
  }
  return (int)lroundf((hz * 60.0f) / (float)STEPS_PER_REV);
}

static uint16_t computeRampDurationMs(int fromRpm, int toRpm) {
  int delta = fromRpm - toRpm;
  if (delta < 0) {
    delta = -delta;
  }
  if (delta < 1) {
    delta = 1;
  }
  long ms = (long)RAMP_BASE_MS + (long)delta * (long)RAMP_MS_PER_RPM;
  if (ms < RAMP_MIN_MS) {
    ms = RAMP_MIN_MS;
  }
  if (ms > RAMP_MAX_MS) {
    ms = RAMP_MAX_MS;
  }
  return (uint16_t)ms;
}

static float rampEase(float t) {
  if (t <= 0.0f) {
    return 0.0f;
  }
  if (t >= 1.0f) {
    return 1.0f;
  }
  return t * t * (3.0f - 2.0f * t);
}

static int effectiveRampStartRpm(int targetRpm) {
#if RAMP_FROM_MIN_RPM
  (void)targetRpm;
  return MIN_RPM;
#else
  int fromPct = (targetRpm * RAMP_START_PERCENT) / 100;
  if (fromPct < MIN_RPM) {
    fromPct = MIN_RPM;
  }
  if (fromPct > targetRpm) {
    fromPct = targetRpm;
  }
  return fromPct;
#endif
}

static void setStepFrequency(float stepHz) {
  commandedStepHz_ = stepHz;
  if (stepHz <= 0.0f) {
    stopStepOutput();
    return;
  }

  uint32_t freq = (uint32_t)lroundf(stepHz);
  if (freq < 1) {
    freq = 1;
  }

  uint32_t periodUs = (uint32_t)lroundf(1000000.0f / stepHz);
  if (periodUs < STEP_TIMER_MIN_PERIOD_US) {
    periodUs = STEP_TIMER_MIN_PERIOD_US;
  }

#if USE_TIMER_STEP_ABOVE_HZ > 0
  if (freq >= (uint32_t)USE_TIMER_STEP_ABOVE_HZ) {
    stopPwmSteps();
    startStepTimer(periodUs);
    return;
  }
#endif

  stopStepTimer();
  analogWriteFreq(freq);
  analogWrite(STEP_PIN, STEP_PWM_DUTY);
}

static void beginRampToTargetHz(float targetHz, float startHz) {
  rampActive_ = true;
  rampStartMs_ = millis();
  lastRampUpdateMs_ = rampStartMs_;
  rampStartHz_ = startHz;
  rampTargetHz_ = targetHz;
  rampDurationMs_ = computeRampDurationMs(hzToRpm(startHz), hzToRpm(targetHz));
}

static void updateRamp(uint32_t nowMs) {
  if (!rampActive_) {
    return;
  }
  if (nowMs - lastRampUpdateMs_ < RAMP_UPDATE_MS) {
    return;
  }
  lastRampUpdateMs_ = nowMs;

  uint32_t elapsed = nowMs - rampStartMs_;
  float t = (rampDurationMs_ == 0) ? 1.0f : ((float)elapsed / (float)rampDurationMs_);
  float eased = rampEase(t);
  float hz = rampStartHz_ + (rampTargetHz_ - rampStartHz_) * eased;

  setStepFrequency(hz);
  commandedRpm_ = hzToRpm(commandedStepHz_);

  if (elapsed >= rampDurationMs_) {
    rampActive_ = false;
    setStepFrequency(rampTargetHz_);
    commandedRpm_ = hzToRpm(commandedStepHz_);
  }
}

static void updateStepCounting() {
  if (commandedStepHz_ <= 0.0f) {
    lastStepUpdateUs_ = micros();
    return;
  }
  uint32_t nowUs = micros();
  uint32_t deltaUs = nowUs - lastStepUpdateUs_;
  lastStepUpdateUs_ = nowUs;
  stepAccumulator_ += commandedStepHz_ * (deltaUs / 1000000.0f);
}

void motorDriverBegin() {
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(STEP_PIN, LOW);
  stopStepOutput();
  motorEnable(false);
}

void motorSetDirection(bool cw) {
  directionCW_ = cw;
  digitalWrite(DIR_PIN, cw ? DIR_CW_LEVEL : !DIR_CW_LEVEL);
}

void motorEnable(bool on) {
  digitalWrite(EN_PIN, on ? LOW : HIGH);
}

bool motorDirectionCW() {
  return directionCW_;
}

void motorStartWinding(int startRpm, int targetRpm, bool preserveSteps) {
  if (targetRpm < MIN_RPM) {
    targetRpm = MIN_RPM;
  }
#if USE_SOFT_START
  int rampStartRpm = effectiveRampStartRpm(targetRpm);
  if (startRpm <= 0) {
    startRpm = rampStartRpm;
  } else {
    if (startRpm < MIN_RPM) {
      startRpm = MIN_RPM;
    }
    if (startRpm < rampStartRpm) {
      startRpm = rampStartRpm;
    }
  }
#else
  if (startRpm < MIN_RPM) {
    startRpm = targetRpm;
  }
#endif
  if (!preserveSteps) {
    stepAccumulator_ = 0.0f;
  }
  lastStepUpdateUs_ = micros();
  stopRequested_ = false;
  stopForCompletion_ = false;
  pauseRequested_ = false;
  menuRequested_ = false;
#if USE_SOFT_START
  commandedRpm_ = startRpm;
  float startHz = rpmToStepHz(startRpm);
  setStepFrequency(startHz);
  beginRampToTargetHz(rpmToStepHz(targetRpm), startHz);
#else
  commandedRpm_ = targetRpm;
  setStepFrequency(rpmToStepHz(targetRpm));
  rampActive_ = false;
#endif
}

void motorRequestStop(bool forCompletion, bool pause, bool toMenu) {
#if USE_SOFT_STOP
  if (!stopRequested_) {
    stopRequested_ = true;
    stopForCompletion_ = forCompletion;
    pauseRequested_ = pause;
    menuRequested_ = toMenu;
    beginRampToTargetHz(0.0f, commandedStepHz_);
  }
#else
  (void)forCompletion;
  (void)pause;
  (void)toMenu;
  motorStopImmediate();
#endif
}

void motorUpdate(uint32_t nowMs) {
  updateRamp(nowMs);
  updateStepCounting();
}

void motorStopImmediate() {
  stopStepOutput();
  rampActive_ = false;
  stopRequested_ = false;
  stopForCompletion_ = false;
  pauseRequested_ = false;
  menuRequested_ = false;
}

bool motorRampActive() {
  return rampActive_;
}
bool motorStopPending() {
  return stopRequested_;
}
bool motorStopForCompletion() {
  return stopForCompletion_;
}
bool motorStopPause() {
  return pauseRequested_;
}
bool motorStopToMenu() {
  return menuRequested_;
}
int motorCommandedRpm() {
  return commandedRpm_;
}
float motorCommandedStepHz() {
  return commandedStepHz_;
}

void motorSingleStep(bool cw) {
  motorSetDirection(cw);
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(STEP_PULSE_WIDTH_US);
  digitalWrite(STEP_PIN, LOW);
}

void motorResetStepAccumulator() {
  stepAccumulator_ = 0.0f;
  lastStepUpdateUs_ = micros();
}

long motorCurrentSteps() {
  return (long)stepAccumulator_;
}

float motorStepAccumulator() {
  return stepAccumulator_;
}

void motorSyncStepAccumulator(long steps) {
  stepAccumulator_ = (float)steps;
}
