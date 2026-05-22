#include "a3144_counter.h"
#include "config.h"

#if defined(ARDUINO_ARCH_RP2040)
#include "hardware/gpio.h"
#endif

static volatile long pulseCount_ = 0;
static volatile uint32_t lastPulseUs_ = 0;
static volatile bool targetCW_ = true;
static volatile bool motorCW_ = true;

static inline bool a3144PinActive() {
#if defined(ARDUINO_ARCH_RP2040)
  return gpio_get(A3144_PIN) == 0;
#else
  return digitalRead(A3144_PIN) == LOW;
#endif
}

static void handleA3144Isr() {
  uint32_t nowUs = micros();

#if A3144_COUNT_ON_FALLING
  if (!a3144PinActive()) {
    return;
  }
#else
  if (a3144PinActive()) {
    return;
  }
#endif

  if (lastPulseUs_ != 0) {
    uint32_t dt = nowUs - lastPulseUs_;
    if (dt < (uint32_t)A3144_DEBOUNCE_US) {
      return;
    }
    if (dt < (uint32_t)A3144_MIN_INTERVAL_US) {
      return;
    }
  }

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
