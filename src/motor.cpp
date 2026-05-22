#include "motor.h"
#include "config.h"

#if USE_TMC2208_UART
#include <TMCStepper.h>
static TMC2208Stepper driver(&Serial1, TMC_R_SENSE, 0);
#endif

void StepMotor::begin(uint8_t stepPin, uint8_t dirPin, uint8_t enPin) {
  step_ = stepPin;
  dirPin_ = dirPin;
  en_ = enPin;
  pinMode(step_, OUTPUT);
  pinMode(dirPin_, OUTPUT);
  pinMode(en_, OUTPUT);
  digitalWrite(step_, LOW);
  enable(false);
}

void StepMotor::setupTmc2208Uart() {
#if USE_TMC2208_UART
  Serial1.setTX(TMC_UART_TX_PIN);
  Serial1.setRX(TMC_UART_RX_PIN);
  Serial1.begin(115200);
  driver.begin();
  driver.toff(4);
  driver.rms_current(TMC_RUN_CURRENT_MA);
  driver.microsteps(MICROSTEP);
  driver.en_spreadCycle(false);
  driver.pwm_autoscale(true);
#endif
}

void StepMotor::setDirection(WindingDir dir) {
  dir_ = dir;
  digitalWrite(dirPin_, dir == WindingDir::CW ? HIGH : LOW);
}

void StepMotor::enable(bool on) {
  enabled_ = on;
  digitalWrite(en_, on ? LOW : HIGH);
}

void StepMotor::setTargetRpm(uint16_t rpm) {
  if (rpm > MAX_RPM_USER) {
    rpm = MAX_RPM_USER;
  }
  targetRpm_ = rpm;
#if USE_SOFT_START
  rampStartRpm_ = currentRpm_;
  ramping_ = true;
  rampStartMs_ = millis();
#else
  currentRpm_ = rpm;
  applyStepInterval();
#endif
}

void StepMotor::applyStepInterval() {
  if (currentRpm_ == 0 || !enabled_) {
    stepIntervalUs_ = 0;
    return;
  }
  uint32_t stepsPerSec = (uint32_t)STEPS_PER_REV * currentRpm_ / 60;
  if (stepsPerSec == 0) {
    stepIntervalUs_ = 0;
    return;
  }
  stepIntervalUs_ = 1000000UL / stepsPerSec;
}

void StepMotor::stepPulse() {
  digitalWrite(step_, HIGH);
  delayMicroseconds(2);
  digitalWrite(step_, LOW);
}

void StepMotor::tick() {
#if USE_SOFT_START
  if (ramping_) {
    uint32_t elapsed = millis() - rampStartMs_;
    if (elapsed >= SOFT_RAMP_MS) {
      currentRpm_ = targetRpm_;
      ramping_ = false;
    } else {
      int32_t delta = (int32_t)targetRpm_ - (int32_t)rampStartRpm_;
      currentRpm_ = (uint16_t)((int32_t)rampStartRpm_ + delta * (int32_t)elapsed / (int32_t)SOFT_RAMP_MS);
    }
  } else {
    currentRpm_ = targetRpm_;
  }
#else
  currentRpm_ = targetRpm_;
#endif

  applyStepInterval();
  if (!enabled_ || stepIntervalUs_ == 0) {
    return;
  }
  uint32_t now = micros();
  if (now - lastStepUs_ >= stepIntervalUs_) {
    lastStepUs_ = now;
    stepPulse();
  }
}
