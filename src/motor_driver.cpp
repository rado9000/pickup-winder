#include "motor_driver.h"
#include "config.h"

#include <FastAccelStepper.h>

#if USE_TMC2209_UART
#include "tmc2209_driver.h"
#endif

static FastAccelStepperEngine fasEngine;
static FastAccelStepper *fasStepper = nullptr;
static bool directionCW_ = true;

static int windTargetRpm_ = 0;
static int windCurrentRpm_ = 0;
static bool windRamping_ = false;
static uint32_t windLastStepMs_ = 0;

static uint32_t rpmToMilliHz(int rpm) {
  if (rpm < MIN_RPM) {
    rpm = MIN_RPM;
  }
  return (uint32_t)lroundf((rpm / 60.0f) * (float)STEPS_PER_REV * 1000.0f);
}

static int rampStartRpmForTarget(int targetRpm) {
  int start = MOTOR_RPM_RAMP_START;
  if (targetRpm < start) {
    return targetRpm;
  }
  return start;
}

static void applyFasAccel() {
  fasStepper->setAcceleration(MOTOR_ACCEL_STEPS_S2);
  fasStepper->setLinearAcceleration(0);
}

static void setRunSpeedRpm(int rpm) {
  fasStepper->setSpeedInMilliHz(rpmToMilliHz(rpm));
  fasStepper->applySpeedAcceleration();
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

  fasStepper->setDirectionPin(DIR_PIN, DIR_CW_LEVEL == HIGH, MOTOR_DIR_SETUP_US);
  fasStepper->setEnablePin(EN_PIN, true);
  fasStepper->setAutoEnable(false);
  applyFasAccel();
}

void motorSetDirection(bool cw) {
  directionCW_ = cw;
}

void motorEnable(bool on) {
  if (!fasStepper) {
    digitalWrite(EN_PIN, on ? LOW : HIGH);
    return;
  }
  if (on) {
    fasStepper->enableOutputs();
  } else {
    fasStepper->disableOutputs();
  }
}

bool motorDirectionCW() {
  return directionCW_;
}

void motorStartWinding(int targetRpm) {
  if (!fasStepper) {
    return;
  }
  if (targetRpm < MIN_RPM) {
    targetRpm = MIN_RPM;
  } else if (targetRpm > MAX_RPM) {
    targetRpm = MAX_RPM;
  }

#if USE_TMC2209_UART
  (void)tmc2209ApplySpreadCycleOnce();
#endif

  if (fasStepper->isRunning()) {
    fasStepper->forceStop();
  }

  fasStepper->setCurrentPosition(0);
  applyFasAccel();

  windTargetRpm_ = targetRpm;
  windCurrentRpm_ = rampStartRpmForTarget(targetRpm);
  windRamping_ = true;
  windLastStepMs_ = millis();

  motorEnable(true);

  setRunSpeedRpm(windCurrentRpm_);
  if (directionCW_) {
    fasStepper->runForward();
  } else {
    fasStepper->runBackward();
  }
}

void motorUpdate() {
  if (!fasStepper || !windRamping_) {
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

  setRunSpeedRpm(windCurrentRpm_);
}

void motorStopImmediate() {
  windRamping_ = false;
  if (!fasStepper) {
    return;
  }
  if (fasStepper->isRunning()) {
    fasStepper->forceStop();
  }
}

void motorSingleStep(bool cw) {
  if (!fasStepper) {
    return;
  }
  directionCW_ = cw;
  if (cw) {
    fasStepper->forwardStep(true);
  } else {
    fasStepper->backwardStep(true);
  }
}

void motorResetStepAccumulator() {
  if (fasStepper) {
    fasStepper->setCurrentPosition(0);
  }
}

long motorCurrentSteps() {
  if (!fasStepper) {
    return 0;
  }
  return fasStepper->getCurrentPosition();
}
