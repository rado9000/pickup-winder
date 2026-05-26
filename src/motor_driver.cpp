#include "motor_driver.h"
#include "config.h"

#include <FastAccelStepper.h>

static FastAccelStepperEngine fasEngine;
static FastAccelStepper *fasStepper = nullptr;

static bool directionCW_ = true;
static float stepAccumulator_ = 0.0f;
static int commandedRpm_ = 0;

static bool rampActive_ = false;
static uint32_t rampStartMs_ = 0;
static uint32_t rampDurationMs_ = 0;
static uint32_t rampStartHz_ = 0;
static uint32_t rampTargetHz_ = 0;

static bool stopRequested_ = false;
static bool stopForCompletion_ = false;
static bool pauseRequested_ = false;
static bool menuRequested_ = false;

static uint32_t rpmToStepHz(int rpm) {
  if (rpm <= 0) {
    return 0;
  }
  return (uint32_t)lroundf((rpm / 60.0f) * (float)STEPS_PER_REV);
}

static int hzToRpm(uint32_t hz) {
  if (hz == 0) {
    return 0;
  }
  return (int)lroundf((hz * 60.0f) / (float)STEPS_PER_REV);
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

// smootherstep — S-curve (zerowe przyspieszenie na początku i końcu)
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

static int32_t computeAccelerationStepsPerSec2(int fromRpm, int toRpm) {
  uint32_t fromHz = rpmToStepHz(fromRpm);
  uint32_t toHz = rpmToStepHz(toRpm);
  uint32_t deltaHz = (toHz > fromHz) ? (toHz - fromHz) : (fromHz - toHz);
  uint32_t rampMs = computeRampDurationMs(fromRpm, toRpm);
  float rampSec = (float)rampMs / 1000.0f;
  if (rampSec < 0.25f) {
    rampSec = 0.25f;
  }
  int32_t accel = (int32_t)lroundf((float)deltaHz / rampSec);
  if (accel < FAS_ACCEL_MIN) {
    accel = FAS_ACCEL_MIN;
  }
  if (accel > FAS_ACCEL_MAX) {
    accel = FAS_ACCEL_MAX;
  }
  return accel;
}

static void configureFasMotion(int fromRpm, int toRpm) {
  if (!fasStepper) {
    return;
  }
  int32_t accel = computeAccelerationStepsPerSec2(fromRpm, toRpm);
  fasStepper->setAcceleration(accel);
  fasStepper->setLinearAcceleration(FAS_LINEAR_ACCEL_STEPS);
}

static void updateCommandedRpmFromFas() {
  if (!fasStepper) {
    return;
  }
  int32_t mhz = fasStepper->getCurrentSpeedInMilliHz(false);
  if (mhz < 0) {
    mhz = -mhz;
  }
  commandedRpm_ = hzToRpm((uint32_t)((mhz + 500) / 1000));
}

static void beginRamp(int startRpm, int targetRpm) {
  rampActive_ = true;
  rampStartMs_ = millis();
  rampStartHz_ = rpmToStepHz(startRpm);
  rampTargetHz_ = rpmToStepHz(targetRpm);
  rampDurationMs_ = computeRampDurationMs(startRpm, targetRpm);
  configureFasMotion(startRpm, targetRpm);
}

static void updateRamp(uint32_t nowMs) {
  if (!rampActive_ || !fasStepper) {
    return;
  }

  uint32_t elapsed = nowMs - rampStartMs_;
  float t = (rampDurationMs_ == 0) ? 1.0f : ((float)elapsed / (float)rampDurationMs_);
  float eased = rampEase(t);
  float hzF = (float)rampStartHz_ + ((float)rampTargetHz_ - (float)rampStartHz_) * eased;
  uint32_t hz = (uint32_t)lroundf(hzF);
  if (hz < 1) {
    hz = 1;
  }

  fasStepper->setSpeedInHz(hz);
  fasStepper->applySpeedAcceleration();

  if (elapsed >= rampDurationMs_) {
    rampActive_ = false;
    fasStepper->setSpeedInHz(rampTargetHz_);
    fasStepper->applySpeedAcceleration();
  }

  updateCommandedRpmFromFas();
}

static void startContinuousMotion(int targetRpm, bool cw) {
  if (!fasStepper) {
    return;
  }
  motorSetDirection(cw);
  fasStepper->setSpeedInHz(rpmToStepHz(targetRpm));

  if (cw) {
    fasStepper->runForward();
  } else {
    fasStepper->runBackward();
  }
}

void motorDriverBegin() {
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(EN_PIN, HIGH);

  fasEngine.init();
  fasStepper = fasEngine.stepperConnectToPin(STEP_PIN);
  if (!fasStepper) {
    return;
  }

  fasStepper->setDirectionPin(DIR_PIN, DIR_CW_LEVEL == HIGH, FAS_DIR_CHANGE_DELAY_US);
  fasStepper->setEnablePin(EN_PIN, true);
  fasStepper->setAutoEnable(true);
  fasStepper->setDelayToEnable(FAS_ENABLE_DELAY_US);
  fasStepper->setDelayToDisable(FAS_DISABLE_DELAY_MS);
  fasStepper->setForwardPlanningTimeInMs(FAS_FORWARD_PLAN_MS);
  fasStepper->setLinearAcceleration(FAS_LINEAR_ACCEL_STEPS);
}

void motorSetDirection(bool cw) {
  directionCW_ = cw;
  if (!fasStepper) {
    digitalWrite(DIR_PIN, cw ? DIR_CW_LEVEL : !DIR_CW_LEVEL);
    return;
  }
  if (fasStepper->isRunning()) {
    if (cw) {
      fasStepper->runForward();
    } else {
      fasStepper->runBackward();
    }
  } else {
    digitalWrite(DIR_PIN, cw ? DIR_CW_LEVEL : !DIR_CW_LEVEL);
  }
}

void motorEnable(bool on) {
  if (fasStepper) {
    if (on) {
      fasStepper->enableOutputs();
    } else {
      fasStepper->disableOutputs();
    }
  } else {
    digitalWrite(EN_PIN, on ? LOW : HIGH);
  }
}

bool motorDirectionCW() {
  return directionCW_;
}

void motorStartWinding(int startRpm, int targetRpm, bool preserveSteps) {
  if (!fasStepper) {
    return;
  }
  if (targetRpm < MIN_RPM) {
    targetRpm = MIN_RPM;
  }

  int rampStartRpm = startRpm;
#if USE_SOFT_START
  rampStartRpm = effectiveRampStartRpm(targetRpm);
  if (startRpm > 0 && startRpm > rampStartRpm) {
    rampStartRpm = startRpm;
  }
#else
  rampStartRpm = targetRpm;
#endif

  if (!preserveSteps) {
    stepAccumulator_ = 0.0f;
    fasStepper->setCurrentPosition(0);
  } else {
    fasStepper->setCurrentPosition((int32_t)stepAccumulator_);
  }

  stopRequested_ = false;
  stopForCompletion_ = false;
  pauseRequested_ = false;
  menuRequested_ = false;

  directionCW_ = motorDirectionCW();
  motorEnable(true);

#if USE_SOFT_START
  configureFasMotion(rampStartRpm, targetRpm);
  fasStepper->setSpeedInHz(rpmToStepHz(rampStartRpm));
  if (directionCW_) {
    fasStepper->runForward();
  } else {
    fasStepper->runBackward();
  }
  beginRamp(rampStartRpm, targetRpm);
#else
  configureFasMotion(MIN_RPM, targetRpm);
  startContinuousMotion(targetRpm, directionCW_);
  rampActive_ = false;
#endif

  commandedRpm_ = rampStartRpm;
}

void motorRequestStop(bool forCompletion, bool pause, bool toMenu) {
#if USE_SOFT_STOP
  if (!stopRequested_ && fasStepper) {
    stopRequested_ = true;
    stopForCompletion_ = forCompletion;
    pauseRequested_ = pause;
    menuRequested_ = toMenu;
    fasStepper->stopMove();
    rampActive_ = false;
  }
#else
  (void)forCompletion;
  (void)pause;
  (void)toMenu;
  motorStopImmediate();
#endif
}

void motorUpdate(uint32_t nowMs) {
#if USE_SOFT_START
  updateRamp(nowMs);
#endif
  if (fasStepper && fasStepper->isRunning()) {
    stepAccumulator_ = (float)fasStepper->getCurrentPosition();
    if (!rampActive_) {
      updateCommandedRpmFromFas();
    }
  }
}

void motorStopImmediate() {
  rampActive_ = false;
  if (fasStepper) {
    if (fasStepper->isRunning()) {
      fasStepper->forceStop();
    }
    stepAccumulator_ = (float)fasStepper->getCurrentPosition();
  }
  stopRequested_ = false;
  stopForCompletion_ = false;
  pauseRequested_ = false;
  menuRequested_ = false;
  commandedRpm_ = 0;
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
  return (commandedRpm_ / 60.0f) * (float)STEPS_PER_REV;
}

void motorSingleStep(bool cw) {
  if (!fasStepper) {
    return;
  }
  motorSetDirection(cw);
  if (cw) {
    fasStepper->forwardStep(true);
  } else {
    fasStepper->backwardStep(true);
  }
  stepAccumulator_ = (float)fasStepper->getCurrentPosition();
}

void motorResetStepAccumulator() {
  stepAccumulator_ = 0.0f;
  if (fasStepper) {
    fasStepper->setCurrentPosition(0);
  }
}

long motorCurrentSteps() {
  if (fasStepper) {
    return fasStepper->getCurrentPosition();
  }
  return (long)stepAccumulator_;
}

float motorStepAccumulator() {
  return stepAccumulator_;
}

void motorSyncStepAccumulator(long steps) {
  stepAccumulator_ = (float)steps;
  if (fasStepper) {
    fasStepper->setCurrentPosition((int32_t)steps);
  }
}
