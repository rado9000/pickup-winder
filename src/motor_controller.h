#pragma once

#include "servo42.h"
#include "types.h"

class MotorController {
 public:
  void begin(Servo42* servo);

  bool detect();
  bool prepareForWinding();  // mode + enable + heartbeat (before any motion)
  void idleSafe();           // soft stop + heartbeat off; motor STAYS enabled
  bool releaseMotor();       // disable driver (shaft free) — call only when stopped
  bool isEnabled() const { return enabled_; }

  void setDirection(WindDir dir);
  WindDir direction() const { return dir_; }

  bool commandRpm(uint16_t rpm);  // clamped, uses SERVO_INTERNAL_ACC
  bool softStop();
  bool quickStop();
  bool emergencyStop();

  // Same-direction low-speed F6 final approach — NEVER reverses for correction.
  bool commandFinalApproach(WindDir dir);

  // Telemetry: POSITION first, then RPM, then STATUS.
  // windingActive selects active/near-target/idle poll intervals + active 0x31 path.
  void pollTelemetry(uint32_t nowMs, bool windingActive = false,
                     uint64_t remainingCounts = UINT64_C(0xFFFFFFFFFFFFFFFF));

  // Force a fresh 0x31 sample (robust retries). Used at job start / final sample.
  bool refreshPositionNow(bool robust = true);

  bool encoderOk() const { return encoderOk_; }
  bool rpmOk() const { return rpmOk_; }
  bool alarmOk() const { return !alarmFault_; }

  int64_t encoder() const { return encoder_; }
  uint16_t actualRpmAbs() const { return actualRpmAbs_; }
  int16_t actualRpmSigned() const { return actualRpmSigned_; }
  uint8_t alarmStatus() const { return alarmStatus_; }
  uint16_t setRpm() const { return setRpm_; }

  bool positionLost(uint32_t nowMs, bool windingActive = false) const;

  // Lightweight encoder diagnostics (reset at each winding job start).
  void resetEncoderDiag(uint32_t nowMs);
  uint32_t encoderPollOk() const { return encoderPollOk_; }
  uint32_t encoderPollFail() const { return encoderPollFail_; }
  uint32_t maxEncoderGapMs() const { return maxEncoderGapMs_; }
  uint32_t lastPositionAgeMs(uint32_t nowMs) const;

 private:
  void pollPosition(uint32_t nowMs, bool windingActive, uint64_t remainingCounts);
  void pollRpm(uint32_t nowMs);
  void pollStatus(uint32_t nowMs);
  void notePositionOk(uint32_t nowMs, int64_t enc);
  void notePositionFail();

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
  bool enabled_ = false;
  uint32_t lastPosOkMs_ = 0;
  uint32_t lastCmdMs_ = 0;

  uint32_t lastPosPollMs_ = 0;
  uint32_t lastRpmPollMs_ = 0;
  uint32_t lastStatusPollMs_ = 0;

  uint32_t encoderPollOk_ = 0;
  uint32_t encoderPollFail_ = 0;
  uint32_t maxEncoderGapMs_ = 0;
  uint32_t jobStartMs_ = 0;
};
