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
static uint32_t rampTotalMs_ = 0;
static uint32_t rampPhase2Ms_ = 0;
static int rampStartRpm_ = MIN_RPM;
static int rampTargetRpm_ = MIN_RPM;
static uint32_t lastSetpointMs_ = 0;
static uint32_t lastSetpointHz_ = 0;

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

static uint32_t computeHighSegmentRampMs(int fromRpm, int toRpm) {
  int delta = toRpm - fromRpm;
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

static float rampEaseHigh(float t) {
  if (t <= 0.0f) {
    return 0.0f;
  }
  if (t >= 1.0f) {
    return 1.0f;
  }
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float computeRpmSetpoint(uint32_t elapsedMs, int startRpm, int targetRpm) {
  if (targetRpm <= startRpm) {
    return (float)targetRpm;
  }

  if (targetRpm <= RAMP_PIVOT_RPM) {
    float t = (rampTotalMs_ == 0) ? 1.0f : ((float)elapsedMs / (float)rampTotalMs_);
    return (float)startRpm + ((float)targetRpm - (float)startRpm) * rampEaseHigh(t);
  }

  if (elapsedMs < RAMP_PHASE1_MS) {
    float t = (float)elapsedMs / (float)RAMP_PHASE1_MS;
    return (float)startRpm + ((float)RAMP_PIVOT_RPM - (float)startRpm) * t;
  }

  uint32_t e2 = elapsedMs - RAMP_PHASE1_MS;
  float t2 = (rampPhase2Ms_ == 0) ? 1.0f : ((float)e2 / (float)rampPhase2Ms_);
  float eased = rampEaseHigh(t2);
  return (float)RAMP_PIVOT_RPM + ((float)targetRpm - (float)RAMP_PIVOT_RPM) * eased;
}

static int32_t computeAccelerationStepsPerSec2(int fromRpm, int toRpm) {
  uint32_t fromHz = rpmToStepHz(fromRpm);
  uint32_t toHz = rpmToStepHz(toRpm);
  uint32_t deltaHz = (toHz > fromHz) ? (toHz - fromHz) : (fromHz - toHz);

  uint32_t rampMs = RAMP_PHASE1_MS;
  if (toRpm > RAMP_PIVOT_RPM && fromRpm < toRpm) {
    int segFrom = (fromRpm > RAMP_PIVOT_RPM) ? fromRpm : RAMP_PIVOT_RPM;
    rampMs += computeHighSegmentRampMs(segFrom, toRpm);
  } else {
    rampMs = computeHighSegmentRampMs(fromRpm, toRpm);
  }

  float rampSec = (float)rampMs / 1000.0f;
  if (rampSec < 1.0f) {
    rampSec = 1.0f;
  }

  int32_t accel = (int32_t)lroundf((float)deltaHz / rampSec);
  if (accel < FAS_ACCEL_MIN) {
    accel = FAS_ACCEL_MIN;
  }
  if (accel > FAS_ACCEL_MAX) {
    accel = FAS_ACCEL_MAX;
  }
  if (toRpm >= 500) {
    int32_t cap = FAS_ACCEL_CAP_500RPM;
    if (accel > cap) {
      accel = cap;
    }
  }
  if (toRpm >= 800) {
    int32_t cap = FAS_ACCEL_CAP_800RPM;
    if (accel > cap) {
      accel = cap;
    }
  }
  return accel;
}

static bool applySpeedHz(uint32_t hz) {
  if (!fasStepper || hz < 1) {
    return false;
  }
  if (hz == lastSetpointHz_) {
    return true;
  }
  if (fasStepper->setSpeedInHz(hz) != 0) {
    return false;
  }
  lastSetpointHz_ = hz;
  fasStepper->applySpeedAcceleration();
  commandedRpm_ = hzToRpm(hz);
  return true;
}

static void configureFasMotion(int fromRpm, int toRpm) {
  if (!fasStepper) {
    return;
  }
  fasStepper->setAcceleration(computeAccelerationStepsPerSec2(fromRpm, toRpm));
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
  lastSetpointMs_ = 0;
  lastSetpointHz_ = 0;
  rampStartRpm_ = startRpm;
  rampTargetRpm_ = targetRpm;

  if (targetRpm > RAMP_PIVOT_RPM) {
    rampPhase2Ms_ = computeHighSegmentRampMs(RAMP_PIVOT_RPM, targetRpm);
    rampTotalMs_ = RAMP_PHASE1_MS + rampPhase2Ms_;
  } else {
    rampPhase2Ms_ = computeHighSegmentRampMs(startRpm, targetRpm);
    rampTotalMs_ = rampPhase2Ms_;
  }

  configureFasMotion(startRpm, targetRpm);
}

static void finishRamp() {
  if (!fasStepper) {
    return;
  }
  rampActive_ = false;
  uint32_t hz = rpmToStepHz(rampTargetRpm_);
  lastSetpointHz_ = 0;
  applySpeedHz(hz);
  commandedRpm_ = rampTargetRpm_;
}

static void updateRamp(uint32_t nowMs) {
  if (!rampActive_ || !fasStepper) {
    return;
  }

  uint32_t elapsed = nowMs - rampStartMs_;
  if (elapsed >= rampTotalMs_) {
    finishRamp();
    return;
  }

  if (nowMs - lastSetpointMs_ < (uint32_t)RAMP_SETPOINT_MS) {
    return;
  }
  lastSetpointMs_ = nowMs;

  float rpmF = computeRpmSetpoint(elapsed, rampStartRpm_, rampTargetRpm_);
  uint32_t hz = rpmToStepHz((int)lroundf(rpmF));
  applySpeedHz(hz);
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
#if FAS_AUTO_ENABLE
  fasStepper->setAutoEnable(true);
  fasStepper->setDelayToEnable(FAS_ENABLE_DELAY_US);
  fasStepper->setDelayToDisable(FAS_DISABLE_DELAY_MS);
#else
  fasStepper->setAutoEnable(false);
#endif
  fasStepper->setForwardPlanningTimeInMs(FAS_FORWARD_PLAN_MS);
  fasStepper->setLinearAcceleration(FAS_LINEAR_ACCEL_STEPS);
}

void motorSetDirection(bool cw) {
  directionCW_ = cw;
  if (!fasStepper) {
    digitalWrite(DIR_PIN, cw ? DIR_CW_LEVEL : !DIR_CW_LEVEL);
    return;
  }
  digitalWrite(DIR_PIN, cw ? DIR_CW_LEVEL : !DIR_CW_LEVEL);
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

  int rampStartRpm = MIN_RPM;
#if USE_SOFT_START
  rampStartRpm = MIN_RPM;
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

  motorEnable(true);
  configureFasMotion(rampStartRpm, targetRpm);

  uint32_t startHz = rpmToStepHz(rampStartRpm);
  lastSetpointHz_ = 0;
  fasStepper->setSpeedInHz(startHz);
  if (directionCW_) {
    fasStepper->runForward();
  } else {
    fasStepper->runBackward();
  }

#if USE_SOFT_START
  commandedRpm_ = rampStartRpm;
  beginRamp(rampStartRpm, targetRpm);
  applySpeedHz(startHz);
#else
  commandedRpm_ = targetRpm;
  applySpeedHz(rpmToStepHz(targetRpm));
  rampActive_ = false;
#endif
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
  lastSetpointHz_ = 0;
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
