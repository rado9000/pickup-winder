#include "motor_driver.h"
#include "config.h"

#include <FastAccelStepper.h>

static FastAccelStepperEngine fasEngine;
static FastAccelStepper *fasStepper = nullptr;
static bool directionCW_ = true;

static uint32_t rpmToStepHz(int rpm) {
  if (rpm < MIN_RPM) {
    rpm = MIN_RPM;
  }
  return (uint32_t)lroundf((rpm / 60.0f) * (float)STEPS_PER_REV);
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
  fasStepper->setAcceleration(MOTOR_ACCEL_STEPS_S2);
}

void motorSetDirection(bool cw) {
  directionCW_ = cw;
  digitalWrite(DIR_PIN, cw ? DIR_CW_LEVEL : !DIR_CW_LEVEL);
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

  if (fasStepper->isRunning()) {
    fasStepper->forceStop();
  }

  fasStepper->setCurrentPosition(0);
  fasStepper->setAcceleration(MOTOR_ACCEL_STEPS_S2);

  motorEnable(true);

  uint32_t hz = rpmToStepHz(targetRpm);
  fasStepper->setSpeedInHz(hz);
  if (directionCW_) {
    fasStepper->runForward();
  } else {
    fasStepper->runBackward();
  }
}

void motorUpdate() {
  (void)fasStepper;
}

void motorStopImmediate() {
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
  motorSetDirection(cw);
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
