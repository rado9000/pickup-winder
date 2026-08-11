#include "motor_controller.h"
#include "config.h"

#include <Arduino.h>

void MotorController::begin(Servo42* servo) {
  servo_ = servo;
}

void MotorController::resetEncoderDiag(uint32_t nowMs) {
  encoderPollOk_ = 0;
  encoderPollFail_ = 0;
  maxEncoderGapMs_ = 0;
  jobStartMs_ = nowMs;
  // Preserve lastPosOkMs_ until a fresh sample succeeds.
}

uint32_t MotorController::lastPositionAgeMs(uint32_t nowMs) const {
  if (lastPosOkMs_ == 0) {
    return UINT32_MAX;
  }
  return nowMs - lastPosOkMs_;
}

void MotorController::notePositionOk(uint32_t nowMs, int64_t enc) {
  if (lastPosOkMs_ != 0 && jobStartMs_ != 0 && lastPosOkMs_ >= jobStartMs_) {
    const uint32_t gap = nowMs - lastPosOkMs_;
    if (gap > maxEncoderGapMs_) {
      maxEncoderGapMs_ = gap;
    }
  } else if (jobStartMs_ != 0 && lastPosOkMs_ != 0 && lastPosOkMs_ < jobStartMs_) {
    // First sample after job start: gap from job start.
    const uint32_t gap = nowMs - jobStartMs_;
    if (gap > maxEncoderGapMs_) {
      maxEncoderGapMs_ = gap;
    }
  }
  encoder_ = enc;
  encoderOk_ = true;
  lastPosOkMs_ = nowMs;
  encoderPollOk_++;
}

void MotorController::notePositionFail() {
  // Keep last valid encoder_ — never invent / zero / interpolate.
  encoderOk_ = false;
  encoderPollFail_++;
}

bool MotorController::detect() {
  if (!servo_) {
    return false;
  }

  // Several attempts — bus may need a moment after power-up (like the test sketch).
  bool gotStatus = false;
  bool gotEnc = false;
  for (int i = 0; i < 8; i++) {
    uint8_t st = 0;
    if (servo_->readAlarm(st)) {
      alarmStatus_ = st;
      gotStatus = true;
      Serial.printf("[MOTOR] status OK al=%u try=%d\n", st, i);
      break;
    }
    delay(100);
  }
  if (!gotStatus) {
    Serial.println(F("[MOTOR] status read failed"));
    return false;
  }

  for (int i = 0; i < 8; i++) {
    int64_t enc = 0;
    if (servo_->readEncoder(enc)) {
      notePositionOk(millis(), enc);
      gotEnc = true;
      Serial.printf("[MOTOR] enc OK=%lld try=%d\n", static_cast<long long>(enc), i);
      break;
    }
    delay(100);
  }
  if (!gotEnc) {
    Serial.println(F("[MOTOR] encoder read failed"));
    return false;
  }

  int16_t rpm = 0;
  if (servo_->readRpm(rpm)) {
    actualRpmSigned_ = rpm;
    actualRpmAbs_ = static_cast<uint16_t>(abs(rpm));
    rpmOk_ = true;
  }
  return true;
}

bool MotorController::prepareForWinding() {
  if (!servo_) {
    return false;
  }
  // Set mode without saving to flash (avoid wear every boot/start).
  if (!servo_->setWorkModeBusClosedFoc()) {
    Serial.println(F("[MOTOR] mode set failed"));
  }
  if (!servo_->setEnable(true)) {
    Serial.println(F("[MOTOR] enable failed"));
    return false;
  }
  enabled_ = true;
  if (SERVO_HEARTBEAT_MS > 0) {
    servo_->setHeartbeatMs(SERVO_HEARTBEAT_MS);
  }
  Serial.println(F("[MOTOR] ENABLED for winding"));
  return true;
}

void MotorController::idleSafe() {
  // Controlled stop only — driver remains enabled (e.g. Pause holding torque).
  if (!servo_) {
    return;
  }
  servo_->setHeartbeatMs(0);
  softStop();
}

bool MotorController::releaseMotor() {
  // Electrically release holding torque. Call ONLY after actual RPM ≈ 0.
  if (!servo_) {
    return false;
  }
  softStop();
  servo_->setHeartbeatMs(0);
  const bool ok = servo_->setEnable(false);
  enabled_ = false;
  setRpm_ = 0;
  Serial.println(F("[MOTOR] RELEASED (shaft free)"));
  return ok;
}

void MotorController::setDirection(WindDir dir) {
  dir_ = dir;
}

bool MotorController::commandRpm(uint16_t rpm) {
  if (!servo_) {
    return false;
  }
  rpm = clampRpm(rpm);
  setRpm_ = rpm;
  const uint32_t now = millis();
  if (now - lastCmdMs_ < MOTOR_COMMAND_UPDATE_MS && rpm != 0) {
    // Still allow immediate zero / first command via softStop path.
  }
  lastCmdMs_ = now;
  const bool cw = (dir_ == WindDir::CW);
  if (rpm == 0) {
    return servo_->speedStop(SERVO_INTERNAL_ACC);
  }
  return servo_->speedRun(cw, rpm, SERVO_INTERNAL_ACC);
}

bool MotorController::softStop() {
  setRpm_ = 0;
  return servo_ && servo_->speedStop(SERVO_SOFT_STOP_ACC);
}

bool MotorController::quickStop() {
  setRpm_ = 0;
  return servo_ && servo_->speedStop(SERVO_QUICK_STOP_ACC);
}

bool MotorController::emergencyStop() {
  setRpm_ = 0;
  return servo_ && servo_->emergencyStop();
}

bool MotorController::commandFinalApproach(WindDir dir) {
  if (!servo_) {
    return false;
  }
  dir_ = dir;
  setRpm_ = FINAL_APPROACH_RPM;
  const bool cw = (dir == WindDir::CW);
  return servo_->speedRun(cw, FINAL_APPROACH_RPM, SERVO_INTERNAL_ACC);
}

bool MotorController::refreshPositionNow(bool robust) {
  if (!servo_) {
    return false;
  }
  int64_t enc = 0;
  const bool ok = robust ? servo_->readEncoder(enc) : servo_->readEncoderActive(enc);
  if (ok) {
    notePositionOk(millis(), enc);
    return true;
  }
  notePositionFail();
  return false;
}

void MotorController::pollPosition(uint32_t nowMs, bool windingActive,
                                   uint64_t remainingCounts) {
  uint32_t interval = POSITION_POLL_IDLE_MS;
  if (windingActive) {
    interval = POSITION_POLL_ACTIVE_MS;
    if (remainingCounts <= static_cast<uint64_t>(POSITION_NEAR_TARGET_COUNTS)) {
      interval = POSITION_POLL_NEAR_TARGET_MS;
    }
  } else if (enabled_) {
    interval = POSITION_POLL_MS;
  }

  if (nowMs - lastPosPollMs_ < interval) {
    return;
  }
  lastPosPollMs_ = nowMs;

  int64_t enc = 0;
  const bool ok = windingActive ? servo_->readEncoderActive(enc)
                                : servo_->readEncoder(enc);
  if (ok) {
    notePositionOk(nowMs, enc);
  } else {
    notePositionFail();
  }
}

void MotorController::pollRpm(uint32_t nowMs) {
  if (nowMs - lastRpmPollMs_ < RPM_POLL_MS) {
    return;
  }
  lastRpmPollMs_ = nowMs;
  int16_t rpm = 0;
  if (servo_->readRpm(rpm)) {
    actualRpmSigned_ = rpm;
    actualRpmAbs_ = static_cast<uint16_t>(abs(rpm));
    rpmOk_ = true;
  } else {
    rpmOk_ = false;
  }
}

void MotorController::pollStatus(uint32_t nowMs) {
  if (nowMs - lastStatusPollMs_ < STATUS_POLL_MS) {
    return;
  }
  lastStatusPollMs_ = nowMs;
  uint8_t st = 0;
  if (servo_->readAlarm(st)) {
    alarmStatus_ = st;
    // 0=running, 1=stopped are OK; 2..7 are faults
    alarmFault_ = (st >= 2);
  }
}

void MotorController::pollTelemetry(uint32_t nowMs, bool windingActive,
                                    uint64_t remainingCounts) {
  if (!servo_) {
    return;
  }
  // Priority: POSITION → RPM → STATUS (never starve encoder for secondary telemetry).
  pollPosition(nowMs, windingActive, remainingCounts);
  pollRpm(nowMs);
  pollStatus(nowMs);
}

bool MotorController::positionLost(uint32_t nowMs, bool windingActive) const {
  if (lastPosOkMs_ == 0) {
    return true;
  }
  const uint32_t limit =
      windingActive ? SERVO_POS_LOSS_WINDING_MS : SERVO_POS_LOSS_FAULT_MS;
  return (nowMs - lastPosOkMs_) > limit;
}
