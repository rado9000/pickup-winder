#include "motor_driver.h"
#include "config.h"
#include "tmc2209_driver.h"

#include <FastAccelStepper.h>

static FastAccelStepperEngine fasEngine;
static FastAccelStepper *fasStepper = nullptr;

static bool directionCW_ = true;
static float stepAccumulator_ = 0.0f;
static int commandedRpm_ = 0;
static int cruiseTargetRpm_ = 0;

static bool rampActive_ = false;
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

static int32_t fasAccelerationForRpm(int targetRpm) {
  int32_t accel = FAS_ACCEL_DEFAULT;
  if (targetRpm >= 700) {
    accel = FAS_ACCEL_HIGH_RPM;
  } else if (targetRpm >= 400) {
    accel = FAS_ACCEL_MID_RPM;
  }
  if (accel < FAS_ACCEL_MIN) {
    accel = FAS_ACCEL_MIN;
  }
  if (accel > FAS_ACCEL_MAX) {
    accel = FAS_ACCEL_MAX;
  }
  return accel;
}

static void configureFasForTarget(int targetRpm) {
  if (!fasStepper) {
    return;
  }
  fasStepper->setAcceleration(fasAccelerationForRpm(targetRpm));
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

static void startFasMotion(uint32_t targetHz, bool cw) {
  if (!fasStepper) {
    return;
  }
  fasStepper->setSpeedInHz(targetHz);
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
  fasStepper->setAutoEnable(false);
  fasStepper->setForwardPlanningTimeInMs(FAS_FORWARD_PLAN_MS);
}

void motorSetDirection(bool cw) {
  directionCW_ = cw;
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
  (void)startRpm;
  if (!fasStepper) {
    return;
  }
  if (targetRpm < MIN_RPM) {
    targetRpm = MIN_RPM;
  }

  cruiseTargetRpm_ = targetRpm;

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

#if USE_TMC2209_UART
  static bool tmcUartTried = false;
  if (!tmcUartTried) {
    tmcUartTried = true;
    (void)tmc2209ConfigureOnce();
  }
#endif

  configureFasForTarget(targetRpm);

  uint32_t targetHz = rpmToStepHz(targetRpm);

  // Jedna rampa: FastAccelStepper — bez wlasnej rampy w petli (applySpeedAcceleration tylko raz)
  startFasMotion(targetHz, directionCW_);
  rampActive_ = true;
  commandedRpm_ = 0;
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
  (void)nowMs;
  if (!fasStepper) {
    return;
  }

  if (fasStepper->isRunning()) {
    stepAccumulator_ = (float)fasStepper->getCurrentPosition();
    updateCommandedRpmFromFas();

    if (rampActive_) {
      uint32_t targetHz = rpmToStepHz(cruiseTargetRpm_);
      int32_t mhz = fasStepper->getCurrentSpeedInMilliHz(false);
      if (mhz < 0) {
        mhz = -mhz;
      }
      uint32_t curHz = (uint32_t)((mhz + 500) / 1000);
      if (targetHz > 0 && curHz >= (targetHz * 92U) / 100U) {
        rampActive_ = false;
      }
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
