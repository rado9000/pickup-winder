#include "motor_controller.h"
#include "config.h"

#include <Arduino.h>

void MotorController::begin(Servo42* servo) {
  servo_ = servo;
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
      encoder_ = enc;
      encoderOk_ = true;
      lastPosOkMs_ = millis();
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
  }
  if (SERVO_HEARTBEAT_MS > 0) {
    servo_->setHeartbeatMs(SERVO_HEARTBEAT_MS);
  }
  return true;
}

void MotorController::idleSafe() {
  if (!servo_) {
    return;
  }
  servo_->setHeartbeatMs(0);
  servo_->speedStop(SERVO_SOFT_STOP_ACC);
  setRpm_ = 0;
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

void MotorController::pollTelemetry(uint32_t nowMs) {
  if (!servo_) {
    return;
  }

  static uint32_t lastPos = 0;
  static uint32_t lastRpm = 0;
  static uint32_t lastSt = 0;

  if (nowMs - lastPos >= POSITION_POLL_MS) {
    lastPos = nowMs;
    int64_t enc = 0;
    if (servo_->readEncoder(enc)) {
      encoder_ = enc;
      encoderOk_ = true;
      lastPosOkMs_ = nowMs;
    } else {
      encoderOk_ = false;
    }
  }

  if (nowMs - lastRpm >= RPM_POLL_MS) {
    lastRpm = nowMs;
    int16_t rpm = 0;
    if (servo_->readRpm(rpm)) {
      actualRpmSigned_ = rpm;
      actualRpmAbs_ = static_cast<uint16_t>(abs(rpm));
      rpmOk_ = true;
    } else {
      rpmOk_ = false;
    }
  }

  if (nowMs - lastSt >= STATUS_POLL_MS) {
    lastSt = nowMs;
    uint8_t st = 0;
    if (servo_->readAlarm(st)) {
      alarmStatus_ = st;
      // 0=running, 1=stopped are OK; 2..7 are faults
      alarmFault_ = (st >= 2);
    }
  }
}

bool MotorController::positionLost(uint32_t nowMs) const {
  if (lastPosOkMs_ == 0) {
    return true;
  }
  return (nowMs - lastPosOkMs_) > SERVO_POS_LOSS_FAULT_MS;
}
