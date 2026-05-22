#include "a3144_counter.h"
#include "config.h"

static volatile long pulseCount_ = 0;
static volatile uint32_t lastPulseUs_ = 0;
static volatile bool targetCW_ = true;
static volatile bool motorCW_ = true;

static void handleA3144Isr() {
  uint32_t nowUs = micros();
  if (lastPulseUs_ != 0) {
    uint32_t dt = nowUs - lastPulseUs_;
    if (dt < (uint32_t)A3144_DEBOUNCE_US) {
      return;
    }
    if (dt < (uint32_t)A3144_MIN_INTERVAL_US) {
      return;
    }
  }
#if A3144_COUNT_ON_FALLING
  if (digitalRead(A3144_PIN) != LOW) {
    return;
  }
#else
  if (digitalRead(A3144_PIN) != HIGH) {
    return;
  }
#endif
  lastPulseUs_ = nowUs;
  int8_t dir = (motorCW_ == targetCW_) ? 1 : -1;
  pulseCount_ += dir;
}

void a3144Begin() {
  pinMode(A3144_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(A3144_PIN), handleA3144Isr,
#if A3144_COUNT_ON_FALLING
                   FALLING);
#else
                   RISING);
#endif
  a3144Reset();
}

void a3144Reset() {
  noInterrupts();
  pulseCount_ = 0;
  lastPulseUs_ = 0;
  interrupts();
}

void a3144SetTargetDirection(bool cw) {
  targetCW_ = cw;
}

void a3144OnMotorDirection(bool cw) {
  motorCW_ = cw;
}

float a3144Turns() {
  noInterrupts();
  long p = pulseCount_;
  interrupts();
  long absP = p < 0 ? -p : p;
  return (float)absP / (float)A3144_PULSES_PER_REV;
}

long a3144PulseCount() {
  noInterrupts();
  long p = pulseCount_;
  interrupts();
  return p;
}

void a3144Update() {}
