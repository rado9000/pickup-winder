#include "motor_driver.h"
#include "config.h"

#include <hardware/gpio.h>
#include <hardware/timer.h>

static bool directionCW_ = true;
static float commandedStepHz_ = 0.0f;
static float stepAccumulator_ = 0.0f;
static uint32_t lastStepUpdateUs_ = 0;

static bool rampActive_ = false;
static uint32_t rampStartMs_ = 0;
static uint32_t lastRampUpdateMs_ = 0;
static uint32_t rampDurationMs_ = 0;
static float rampStartHz_ = 0.0f;
static float rampTargetHz_ = 0.0f;
static float rampSetpointHz_ = 0.0f;
static int commandedRpm_ = 0;

static bool stopRequested_ = false;
static bool stopForCompletion_ = false;
static bool pauseRequested_ = false;
static bool menuRequested_ = false;

static volatile bool steppingActive_ = false;
static volatile float velocityHz_ = 0.0f;
static alarm_id_t nextStepAlarmId_ = 0;

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

static int64_t stepPulseLowAlarm(alarm_id_t id, void *user_data) {
  (void)id;
  (void)user_data;
  gpio_put(STEP_PIN, 0);
  return 0;
}

static void scheduleNextStepAlarm();

static int64_t stepPulseAlarm(alarm_id_t id, void *user_data) {
  (void)id;
  (void)user_data;
  nextStepAlarmId_ = 0;

  if (!steppingActive_) {
    return 0;
  }

  float hz = velocityHz_;
  if (hz < 1.0f) {
    hz = 1.0f;
  }

  gpio_put(STEP_PIN, 1);
  add_alarm_in_us(STEP_PULSE_WIDTH_US, stepPulseLowAlarm, nullptr, true);

  stepAccumulator_ += 1.0f;
  commandedStepHz_ = hz;
  commandedRpm_ = hzToRpm(hz);

  scheduleNextStepAlarm();
  return 0;
}

static void scheduleNextStepAlarm() {
  if (!steppingActive_) {
    return;
  }

  float hz = velocityHz_;
  if (hz < 1.0f) {
    return;
  }

  uint32_t intervalUs = (uint32_t)lroundf(1000000.0f / hz);
  if (intervalUs < STEP_MIN_INTERVAL_US) {
    intervalUs = STEP_MIN_INTERVAL_US;
  }

  nextStepAlarmId_ = add_alarm_in_us(intervalUs, stepPulseAlarm, nullptr, true);
}

static void stopStepOutput() {
  steppingActive_ = false;
  if (nextStepAlarmId_ > 0) {
    cancel_alarm(nextStepAlarmId_);
    nextStepAlarmId_ = 0;
  }
  gpio_put(STEP_PIN, 0);
  commandedStepHz_ = 0.0f;
  velocityHz_ = 0.0f;
  rampSetpointHz_ = 0.0f;
}

static uint32_t computeRampDurationMs(int fromRpm, int toRpm) {
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
  return (uint32_t)ms;
}

// smootherstep — zerowe przyspieszenie na początku i końcu rampy
static float rampEase(float t) {
  if (t <= 0.0f) {
    return 0.0f;
  }
  if (t >= 1.0f) {
    return 1.0f;
  }
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
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

static void updateRampSetpoint(uint32_t nowMs) {
  if (!rampActive_) {
    return;
  }

  uint32_t elapsed = nowMs - rampStartMs_;
  float t = (rampDurationMs_ == 0) ? 1.0f : ((float)elapsed / (float)rampDurationMs_);
  float eased = rampEase(t);
  rampSetpointHz_ = rampStartHz_ + (rampTargetHz_ - rampStartHz_) * eased;
  velocityHz_ = rampSetpointHz_;
  commandedStepHz_ = velocityHz_;
  commandedRpm_ = hzToRpm(velocityHz_);

  if (elapsed >= rampDurationMs_) {
    rampActive_ = false;
    rampSetpointHz_ = rampTargetHz_;
    velocityHz_ = rampTargetHz_;
    commandedStepHz_ = rampTargetHz_;
    commandedRpm_ = hzToRpm(rampTargetHz_);
  }
}

static void applyStepOutput(float stepHz) {
  commandedStepHz_ = stepHz;
  rampSetpointHz_ = stepHz;

  if (stepHz <= 0.0f) {
    stopStepOutput();
    return;
  }

  if (!steppingActive_) {
    steppingActive_ = true;
    velocityHz_ = stepHz;
    scheduleNextStepAlarm();
  }
}

static void beginRampToTargetHz(float targetHz, float startHz) {
  rampActive_ = true;
  rampStartMs_ = millis();
  rampStartHz_ = startHz;
  rampTargetHz_ = targetHz;
  rampSetpointHz_ = startHz;
  rampDurationMs_ = computeRampDurationMs(hzToRpm(startHz), hzToRpm(targetHz));
}

static void updateRamp(uint32_t nowMs) {
  if (!rampActive_) {
    return;
  }
  updateRampSetpoint(nowMs);
}

static void updateStepCounting() {
  lastStepUpdateUs_ = micros();
}

void motorDriverBegin() {
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);
  gpio_put(STEP_PIN, 0);
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
  float targetHz = rpmToStepHz(targetRpm);
#if USE_SOFT_START
  float startHz = rpmToStepHz(startRpm);
  commandedRpm_ = startRpm;
  velocityHz_ = startHz;
  rampSetpointHz_ = startHz;
  steppingActive_ = true;
  scheduleNextStepAlarm();
  beginRampToTargetHz(targetHz, startHz);
#else
  commandedRpm_ = targetRpm;
  applyStepOutput(targetHz);
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
    beginRampToTargetHz(0.0f, velocityHz_);
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
  gpio_put(STEP_PIN, 1);
  delayMicroseconds(STEP_PULSE_WIDTH_US);
  gpio_put(STEP_PIN, 0);
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
