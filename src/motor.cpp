#include "motor.h"
#include "config.h"

void WindingMotor::begin(Servo42* driver) {
  driver_ = driver;
  enabled_ = false;
  targetRpm_ = 0;
  currentRpm_ = 0;
}

void WindingMotor::setDirection(WindingDir dir) {
  dir_ = dir;
}

void WindingMotor::enable(bool on) {
  enabled_ = on;
  if (!on) {
    targetRpm_ = 0;
    currentRpm_ = 0;
    if (driver_) {
      driver_->speedRun(false, 0, SERVO42_DEFAULT_ACC);
    }
  }
}

void WindingMotor::setTargetRpm(uint16_t rpm) {
  if (rpm > MAX_RPM_USER) {
    rpm = MAX_RPM_USER;
  }
  targetRpm_ = rpm;
  if (!enabled_ || !driver_) {
    return;
  }

  bool reverse = (dir_ == WindingDir::CCW);
  driver_->speedRun(reverse, rpm, SERVO42_DEFAULT_ACC);
}

void WindingMotor::tick() {
  if (!driver_) {
    return;
  }

  int16_t rpm = driver_->readRpm();
  if (rpm == 9999) {
    return;
  }
  currentRpm_ = (uint16_t)abs(rpm);

  if (abs(rpm) < SERVO42_STOP_RPM_THRESHOLD) {
    if (stoppedSinceMs_ == 0) {
      stoppedSinceMs_ = millis();
    }
  } else {
    stoppedSinceMs_ = 0;
  }
}

bool WindingMotor::isStopped() const {
  if (!enabled_ && targetRpm_ == 0) {
    return true;
  }
  if (stoppedSinceMs_ == 0) {
    return false;
  }
  return millis() - stoppedSinceMs_ > SERVO42_STOP_HOLD_MS;
}

void WindingMotor::waitUntilStopped() {
  if (driver_) {
    driver_->waitUntilStopped();
  }
  currentRpm_ = 0;
  stoppedSinceMs_ = millis();
}
