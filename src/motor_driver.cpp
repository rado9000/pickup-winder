#include "motor_driver.h"
#include "config.h"

#include <hardware/gpio.h>
#include "pico/stdlib.h"

static bool directionCW_ = true;
static float stepAccumulator_ = 0.0f;
static float outputStepHz_ = 0.0f;
static int commandedRpm_ = 0;
static uint32_t lastUpdateUs_ = 0;

static bool rampActive_ = false;
static int rampFinalRpm_ = 0;
static int rampSegIndex_ = 0;
static int rampSegFromRpm_ = 0;
static int rampSegToRpm_ = 0;
static bool rampSegHolding_ = false;
static uint32_t rampSegStartMs_ = 0;
static uint32_t rampSegDurationMs_ = 0;

static bool stopRequested_ = false;
static bool stopForCompletion_ = false;
static bool pauseRequested_ = false;
static bool menuRequested_ = false;

static const uint16_t RAMP_LADDER_RPM[] = RAMP_LADDER_TABLE;
static const size_t RAMP_LADDER_COUNT =
    sizeof(RAMP_LADDER_RPM) / sizeof(RAMP_LADDER_RPM[0]);

static uint32_t rpmToStepHz(int rpm) {
  if (rpm <= 0) {
    return 0;
  }
  return (uint32_t)lroundf((rpm / 60.0f) * (float)STEPS_PER_REV);
}

static int hzToRpm(float hz) {
  if (hz <= 0.0f) {
    return 0;
  }
  return (int)lroundf((hz * 60.0f) / (float)STEPS_PER_REV);
}

static uint32_t segmentRampMs(int fromRpm, int toRpm) {
  int delta = toRpm - fromRpm;
  if (delta < 1) {
    delta = 1;
  }
  long ms = (long)RAMP_SEG_BASE_MS + (long)delta * (long)RAMP_SEG_MS_PER_RPM;
  if (ms < RAMP_SEG_MIN_MS) {
    ms = RAMP_SEG_MIN_MS;
  }
  if (ms > RAMP_SEG_MAX_MS) {
    ms = RAMP_SEG_MAX_MS;
  }
  return (uint32_t)ms;
}

static void setStepFrequency(float hz) {
  outputStepHz_ = hz;
  if (hz <= 0.0f) {
    analogWrite(STEP_PIN, 0);
    commandedRpm_ = 0;
    return;
  }
  uint32_t freq = (uint32_t)lroundf(hz);
  if (freq < 1) {
    freq = 1;
  }
  analogWriteFreq(freq);
  analogWrite(STEP_PIN, STEP_PWM_DUTY);
  commandedRpm_ = hzToRpm(hz);
}

static void startRampSegment(int fromRpm, int toRpm, bool holdOnly) {
  rampSegFromRpm_ = fromRpm;
  rampSegToRpm_ = toRpm;
  rampSegHolding_ = holdOnly;
  rampSegStartMs_ = millis();
  rampSegDurationMs_ = holdOnly ? (uint32_t)RAMP_HOLD_MS : segmentRampMs(fromRpm, toRpm);
  if (!holdOnly) {
    setStepFrequency((float)rpmToStepHz(fromRpm));
  } else {
    setStepFrequency((float)rpmToStepHz(toRpm));
  }
}

static void beginLadderRamp(int targetRpm) {
  rampActive_ = true;
  rampFinalRpm_ = targetRpm;
  rampSegIndex_ = 0;
  rampSegFromRpm_ = MIN_RPM;
  rampSegToRpm_ = MIN_RPM;

  if (targetRpm <= (int)RAMP_LADDER_RPM[0]) {
    startRampSegment(MIN_RPM, targetRpm, false);
    return;
  }

  int first = (int)RAMP_LADDER_RPM[0];
  if (first > targetRpm) {
    first = targetRpm;
  }
  startRampSegment(MIN_RPM, first, false);
}

static void finishRamp(int targetRpm) {
  rampActive_ = false;
  setStepFrequency((float)rpmToStepHz(targetRpm));
}

static void advanceLadderIfNeeded() {
  if (rampSegToRpm_ >= rampFinalRpm_) {
    finishRamp(rampFinalRpm_);
    return;
  }

  while (rampSegIndex_ + 1 < RAMP_LADDER_COUNT &&
         (int)RAMP_LADDER_RPM[rampSegIndex_ + 1] <= rampFinalRpm_) {
    rampSegIndex_++;
    int next = (int)RAMP_LADDER_RPM[rampSegIndex_];
    startRampSegment(rampSegToRpm_, next, false);
    return;
  }

  if (rampSegToRpm_ < rampFinalRpm_) {
    startRampSegment(rampSegToRpm_, rampFinalRpm_, false);
  } else {
    finishRamp(rampFinalRpm_);
  }
}

static void updateLadderRamp(uint32_t nowMs) {
  if (!rampActive_) {
    return;
  }

  uint32_t elapsed = nowMs - rampSegStartMs_;

  if (!rampSegHolding_) {
    float t = (rampSegDurationMs_ == 0)
                  ? 1.0f
                  : ((float)elapsed / (float)rampSegDurationMs_);
    if (t > 1.0f) {
      t = 1.0f;
    }
    float fromHz = (float)rpmToStepHz(rampSegFromRpm_);
    float toHz = (float)rpmToStepHz(rampSegToRpm_);
    setStepFrequency(fromHz + (toHz - fromHz) * t);

    if (elapsed >= rampSegDurationMs_) {
      setStepFrequency(toHz);
      rampSegHolding_ = true;
      rampSegStartMs_ = nowMs;
      rampSegDurationMs_ = (uint32_t)RAMP_HOLD_MS;
    }
    return;
  }

  if (elapsed >= rampSegDurationMs_) {
    advanceLadderIfNeeded();
  }
}

static void updateStepAccumulator() {
  uint32_t nowUs = micros();
  if (lastUpdateUs_ == 0) {
    lastUpdateUs_ = nowUs;
    return;
  }
  uint32_t dtUs = nowUs - lastUpdateUs_;
  lastUpdateUs_ = nowUs;
  if (outputStepHz_ > 0.0f) {
    stepAccumulator_ += outputStepHz_ * (dtUs / 1000000.0f);
  }
}

void motorDriverBegin() {
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);
  analogWrite(STEP_PIN, 0);
  digitalWrite(EN_PIN, HIGH);
  lastUpdateUs_ = micros();
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
  (void)startRpm;
  if (targetRpm < MIN_RPM) {
    targetRpm = MIN_RPM;
  }

  if (!preserveSteps) {
    stepAccumulator_ = 0.0f;
  }
  lastUpdateUs_ = micros();

  stopRequested_ = false;
  stopForCompletion_ = false;
  pauseRequested_ = false;
  menuRequested_ = false;

  motorEnable(true);

#if USE_SOFT_START
  beginLadderRamp(targetRpm);
#else
  rampActive_ = false;
  setStepFrequency((float)rpmToStepHz(targetRpm));
#endif
}

void motorRequestStop(bool forCompletion, bool pause, bool toMenu) {
#if USE_SOFT_STOP
  (void)forCompletion;
  (void)pause;
  (void)toMenu;
  motorStopImmediate();
#else
  (void)forCompletion;
  (void)pause;
  (void)toMenu;
  motorStopImmediate();
#endif
}

void motorUpdate(uint32_t nowMs) {
#if USE_SOFT_START
  updateLadderRamp(nowMs);
#endif
  updateStepAccumulator();
}

void motorStopImmediate() {
  rampActive_ = false;
  setStepFrequency(0.0f);
  lastUpdateUs_ = micros();
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
  return outputStepHz_;
}

void motorSingleStep(bool cw) {
  motorSetDirection(cw);
  gpio_put(STEP_PIN, 1);
  busy_wait_us(PREWIND_PULSE_US);
  gpio_put(STEP_PIN, 0);
  stepAccumulator_ += 1.0f;
}

void motorResetStepAccumulator() {
  stepAccumulator_ = 0.0f;
  lastUpdateUs_ = micros();
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
