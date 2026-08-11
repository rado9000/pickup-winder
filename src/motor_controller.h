#pragma once

#include "servo42.h"
#include "types.h"

class MotorController {
 public:
  void begin(Servo42* servo);

  bool detect();
  bool prepareForWinding();  // mode + enable + heartbeat
  void idleSafe();           // heartbeat off, soft stop, keep enable optional

  void setDirection(WindDir dir);
  WindDir direction() const { return dir_; }

  bool commandRpm(uint16_t rpm);  // clamped, uses SERVO_INTERNAL_ACC
  bool softStop();
  bool quickStop();
  bool emergencyStop();

  // Same-direction low-speed F6 final approach — NEVER reverses for correction.
  bool commandFinalApproach(WindDir dir);

  void pollTelemetry(uint32_t nowMs);
  bool encoderOk() const { return encoderOk_; }
  bool rpmOk() const { return rpmOk_; }
  bool alarmOk() const { return !alarmFault_; }

  int64_t encoder() const { return encoder_; }
  uint16_t actualRpmAbs() const { return actualRpmAbs_; }
  int16_t actualRpmSigned() const { return actualRpmSigned_; }
  uint8_t alarmStatus() const { return alarmStatus_; }
  uint16_t setRpm() const { return setRpm_; }

  bool positionLost(uint32_t nowMs) const;

 private:
  Servo42* servo_ = nullptr;
  WindDir dir_ = WindDir::CW;
  uint16_t setRpm_ = 0;
  int64_t encoder_ = 0;
  int16_t actualRpmSigned_ = 0;
  uint16_t actualRpmAbs_ = 0;
  uint8_t alarmStatus_ = 1;
  bool encoderOk_ = false;
  bool rpmOk_ = false;
  bool alarmFault_ = false;
  uint32_t lastPosOkMs_ = 0;
  uint32_t lastCmdMs_ = 0;
};
