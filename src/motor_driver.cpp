#include "motor_driver.h"
#include "config.h"

static bool directionCW_ = true;
static float commandedStepHz_ = 0.0f;
static float stepAccumulator_ = 0.0f;
static uint32_t lastStepUpdateUs_ = 0;

static bool rampActive_ = false;
static uint32_t rampStartMs_ = 0;
static uint16_t rampDurationMs_ = 0;
static int rampStartRpm_ = 0;
static int rampTargetRpm_ = 0;
static int commandedRpm_ = 0;

static bool stopRequested_ = false;
static bool stopForCompletion_ = false;
static bool pauseRequested_ = false;
static bool menuRequested_ = false;

static float rpmToStepHz(int rpm) {
  if (rpm <= 0) {
    return 0.0f;
  }
  return (rpm / 60.0f) * (float)STEPS_PER_REV;
}

static uint16_t computeRampDurationMs(int targetRpmValue) {
  long ms = (long)RAMP_BASE_MS + (long)targetRpmValue * (long)RAMP_MS_PER_RPM;
  if (ms < RAMP_MIN_MS) {
    ms = RAMP_MIN_MS;
  }
  if (ms > RAMP_MAX_MS) {
    ms = RAMP_MAX_MS;
  }
  return (uint16_t)ms;
}

static float smoothstep(float t) {
  if (t <= 0.0f) {
    return 0.0f;
  }
  if (t >= 1.0f) {
    return 1.0f;
  }
  return t * t * (3.0f - 2.0f * t);
}

static void setStepFrequency(float stepHz) {
  commandedStepHz_ = stepHz;
  if (stepHz <= 0.0f) {
    analogWrite(STEP_PIN, 0);
    return;
  }
  uint32_t freq = (uint32_t)lroundf(stepHz);
  if (freq < 1) {
    freq = 1;
  }
  analogWriteFreq(freq);
  analogWrite(STEP_PIN, 128);
}

static void beginRampToTarget(int targetRpmValue, int startRpm) {
  if (!USE_SOFT_START) {
    rampActive_ = false;
    commandedRpm_ = targetRpmValue;
    setStepFrequency(rpmToStepHz(targetRpmValue));
    return;
  }
  rampActive_ = true;
  rampStartMs_ = millis();
  rampDurationMs_ = computeRampDurationMs(targetRpmValue);
  rampStartRpm_ = startRpm;
  rampTargetRpm_ = targetRpmValue;
}

static void updateRamp(uint32_t nowMs) {
  if (!rampActive_) {
    return;
  }
  uint32_t elapsed = nowMs - rampStartMs_;
  float t = (rampDurationMs_ == 0) ? 1.0f : (elapsed / (float)rampDurationMs_);
  float eased = smoothstep(t);
  float rpmF = rampStartRpm_ + (rampTargetRpm_ - rampStartRpm_) * eased;
  int rpm = (int)lroundf(rpmF);
  if (rampTargetRpm_ == 0) {
    if (rpm < 0) {
      rpm = 0;
    }
  } else {
    if (rpm < MIN_RPM) {
      rpm = MIN_RPM;
    }
    if (rpm > rampTargetRpm_) {
      rpm = rampTargetRpm_;
    }
  }
  if (rpm != commandedRpm_) {
    commandedRpm_ = rpm;
    setStepFrequency(rpmToStepHz(commandedRpm_));
  }
  if (elapsed >= rampDurationMs_) {
    rampActive_ = false;
    commandedRpm_ = rampTargetRpm_;
    setStepFrequency(rpmToStepHz(commandedRpm_));
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
  analogWrite(STEP_PIN, 0);
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
  if (!preserveSteps) {
    stepAccumulator_ = 0.0f;
  }
  lastStepUpdateUs_ = micros();
  commandedRpm_ = startRpm;
  setStepFrequency(rpmToStepHz(startRpm));
  beginRampToTarget(targetRpm, startRpm);
  stopRequested_ = false;
  stopForCompletion_ = false;
  pauseRequested_ = false;
  menuRequested_ = false;
}

void motorRequestStop(bool forCompletion, bool pause, bool toMenu) {
#if USE_SOFT_STOP
  if (!stopRequested_) {
    stopRequested_ = true;
    stopForCompletion_ = forCompletion;
    pauseRequested_ = pause;
    menuRequested_ = toMenu;
    beginRampToTarget(0, commandedRpm_);
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
  analogWrite(STEP_PIN, 0);
  commandedStepHz_ = 0.0f;
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
  delayMicroseconds(PREWIND_STEP_PULSE_US);
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
