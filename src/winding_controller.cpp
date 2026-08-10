#include "winding_controller.h"
#include "config.h"

#include <Arduino.h>
#include <math.h>

void WindingController::begin(MotorController* motor) {
  motor_ = motor;
  phase_ = WindPhase::Idle;
}

bool WindingController::isActive() const {
  switch (phase_) {
    case WindPhase::RampUp:
    case WindPhase::Cruise:
    case WindPhase::RampDown:
    case WindPhase::FinalApproach:
    case WindPhase::Pausing:
    case WindPhase::Paused:
      return true;
    default:
      return false;
  }
}

void WindingController::enterPhase(WindPhase p, uint32_t nowMs) {
  phase_ = p;
  switch (p) {
    case WindPhase::RampUp:
      Serial.println(F("[WIND] RAMP_UP"));
      rampStartRpm_ = 0;
      rampEndRpm_ = clampRpm(program_.targetRpm);
      rampStartMs_ = nowMs;
      rampDurationMs_ = program_.rampUpMs;
      activeTiming_ = true;
      lastActiveStampMs_ = nowMs;
      break;
    case WindPhase::Cruise:
      Serial.println(F("[WIND] CRUISE"));
      commandRpm_ = clampRpm(program_.targetRpm);
      motor_->commandRpm(commandRpm_);
      activeTiming_ = true;
      lastActiveStampMs_ = nowMs;
      break;
    case WindPhase::RampDown:
      Serial.println(F("[WIND] RAMP_DOWN"));
      rampStartRpm_ = commandRpm_ > 0 ? commandRpm_ : motor_->actualRpmAbs();
      if (rampStartRpm_ == 0) {
        rampStartRpm_ = clampRpm(program_.targetRpm);
      }
      rampEndRpm_ = 0;
      rampStartMs_ = nowMs;
      rampDurationMs_ = program_.rampDownMs;
      activeTiming_ = true;
      lastActiveStampMs_ = nowMs;
      break;
    case WindPhase::FinalApproach:
      Serial.println(F("[WIND] FINAL_APPROACH"));
      approachIssued_ = false;
      rampStartMs_ = nowMs;
      activeTiming_ = true;
      lastActiveStampMs_ = nowMs;
      break;
    case WindPhase::Pausing:
      Serial.println(F("[WIND] PAUSING"));
      rampStartRpm_ = commandRpm_ > 0 ? commandRpm_ : motor_->actualRpmAbs();
      rampEndRpm_ = 0;
      rampStartMs_ = nowMs;
      rampDurationMs_ = program_.rampDownMs;
      break;
    case WindPhase::Paused:
      Serial.println(F("[WIND] PAUSED"));
      commandRpm_ = 0;
      motor_->softStop();
      activeTiming_ = false;
      break;
    case WindPhase::Complete:
      Serial.println(F("[WIND] COMPLETE"));
      commandRpm_ = 0;
      motor_->idleSafe();
      activeTiming_ = false;
      break;
    case WindPhase::Aborted:
      Serial.println(F("[WIND] ABORTED"));
      commandRpm_ = 0;
      motor_->idleSafe();
      activeTiming_ = false;
      break;
    case WindPhase::Fault:
      Serial.printf("[WIND] FAULT %s\n", faultText_ ? faultText_ : "?");
      commandRpm_ = 0;
      motor_->emergencyStop();
      motor_->idleSafe();
      activeTiming_ = false;
      break;
    default:
      break;
  }
}

bool WindingController::start(const WindingProgram& program) {
  if (!motor_) {
    return false;
  }
  program_ = program;
  program_.targetRpm = clampRpm(program_.targetRpm);
  program_.targetTurns = clampTurns(program_.targetTurns);
  program_.rampUpMs = clampRampMs(program_.rampUpMs);
  program_.rampDownMs = clampRampMs(program_.rampDownMs);

  motor_->setDirection(program_.direction);
  if (!motor_->prepareForWinding()) {
    faultText_ = "MOTOR PREP FAIL";
    enterPhase(WindPhase::Fault, millis());
    return false;
  }

  int64_t enc = motor_->encoder();
  if (!motor_->encoderOk()) {
    if (!motor_->detect()) {
      faultText_ = "NO ENCODER";
      enterPhase(WindPhase::Fault, millis());
      return false;
    }
    enc = motor_->encoder();
  }

  turns_.beginJob(enc, program_.targetTurns, program_.direction);
  pauseRequested_ = false;
  resumeRequested_ = false;
  abortRequested_ = false;
  approachIssued_ = false;
  activeMs_ = 0;
  commandRpm_ = 0;
  faultText_ = nullptr;

  Serial.printf("[WIND] START target=%lu rpm=%u dir=%s\n",
                static_cast<unsigned long>(program_.targetTurns), program_.targetRpm,
                program_.direction == WindDir::CW ? "CW" : "CCW");

  enterPhase(WindPhase::RampUp, millis());
  return true;
}

void WindingController::requestPause() {
  if (phase_ == WindPhase::RampUp || phase_ == WindPhase::Cruise ||
      phase_ == WindPhase::RampDown || phase_ == WindPhase::FinalApproach) {
    pauseRequested_ = true;
  }
}

void WindingController::requestResume() {
  if (phase_ == WindPhase::Paused) {
    resumeRequested_ = true;
  }
}

void WindingController::requestAbort() {
  if (phase_ == WindPhase::Paused || phase_ == WindPhase::Pausing) {
    abortRequested_ = true;
  }
}

int32_t WindingController::remainingSigned() const {
  const int64_t rem = turns_.remainingCounts();
  if (rem > 2147483647LL) {
    return (program_.direction == WindDir::CW) ? 2147483647 : -2147483647;
  }
  // F4 relative: positive = CW direction in encoder space (manual CW +=)
  if (program_.direction == WindDir::CW) {
    return static_cast<int32_t>(rem);
  }
  return static_cast<int32_t>(-rem);
}

void WindingController::checkFaults(uint32_t nowMs) {
  if (phase_ == WindPhase::Idle || phase_ == WindPhase::Complete ||
      phase_ == WindPhase::Aborted || phase_ == WindPhase::Fault ||
      phase_ == WindPhase::Paused) {
    return;
  }
  if (motor_->alarmOk() == false) {
    faultText_ = "MOTOR ALARM";
    enterPhase(WindPhase::Fault, nowMs);
    return;
  }
  if (motor_->positionLost(nowMs)) {
    faultText_ = "RS485 POS LOSS";
    enterPhase(WindPhase::Fault, nowMs);
  }
}

void WindingController::updateSetpoint(uint32_t nowMs) {
  if (nowMs - lastSetpointMs_ < MOTOR_COMMAND_UPDATE_MS) {
    return;
  }
  lastSetpointMs_ = nowMs;

  if (phase_ == WindPhase::RampUp || phase_ == WindPhase::RampDown ||
      phase_ == WindPhase::Pausing) {
    const uint32_t elapsed = nowMs - rampStartMs_;
    float x = rampDurationMs_ == 0 ? 1.0f : static_cast<float>(elapsed) / static_cast<float>(rampDurationMs_);
    if (x > 1.0f) {
      x = 1.0f;
    }
    const RampType type =
        (phase_ == WindPhase::RampUp) ? program_.rampUpType : program_.rampDownType;
    const float rpmF =
        Ramp::interpolate(static_cast<float>(rampStartRpm_), static_cast<float>(rampEndRpm_), x,
                          type);
    commandRpm_ = clampRpm(static_cast<uint32_t>(rpmF + 0.5f));
    if (rampEndRpm_ == 0 && x >= 1.0f) {
      commandRpm_ = 0;
    }
    motor_->commandRpm(commandRpm_);
  } else if (phase_ == WindPhase::Cruise) {
    motor_->commandRpm(commandRpm_);
  }
}

void WindingController::tick(uint32_t nowMs) {
  if (!motor_) {
    return;
  }

  if (isActive() || phase_ == WindPhase::Complete || phase_ == WindPhase::Aborted ||
      phase_ == WindPhase::Fault) {
    motor_->pollTelemetry(nowMs);
    turns_.update(motor_->encoder());
  }

  if (activeTiming_) {
    activeMs_ += nowMs - lastActiveStampMs_;
    lastActiveStampMs_ = nowMs;
  }

  checkFaults(nowMs);
  if (phase_ == WindPhase::Fault) {
    return;
  }

  if (abortRequested_ && (phase_ == WindPhase::Paused || phase_ == WindPhase::Pausing)) {
    abortRequested_ = false;
    enterPhase(WindPhase::Aborted, nowMs);
    return;
  }

  if (pauseRequested_ &&
      (phase_ == WindPhase::RampUp || phase_ == WindPhase::Cruise || phase_ == WindPhase::RampDown ||
       phase_ == WindPhase::FinalApproach)) {
    pauseRequested_ = false;
    enterPhase(WindPhase::Pausing, nowMs);
  }

  if (resumeRequested_ && phase_ == WindPhase::Paused) {
    resumeRequested_ = false;
    enterPhase(WindPhase::RampUp, nowMs);
  }

  switch (phase_) {
    case WindPhase::RampUp: {
      updateSetpoint(nowMs);
      if (nowMs - rampStartMs_ >= rampDurationMs_) {
        enterPhase(WindPhase::Cruise, nowMs);
      }
      // Early transition to ramp-down if we already need to stop
      {
        const float stopTurns = Ramp::stoppingTurns(
            static_cast<float>(motor_->actualRpmAbs() > 0 ? motor_->actualRpmAbs() : commandRpm_),
            program_.rampDownMs / 1000.0f);
        const double remTurns =
            static_cast<double>(turns_.remainingCounts()) / SERVO_COUNTS_PER_REV;
        const double compTurns =
            static_cast<double>(STOP_COMPENSATION_COUNTS) / SERVO_COUNTS_PER_REV;
        if (remTurns <= stopTurns + compTurns) {
          enterPhase(WindPhase::RampDown, nowMs);
        }
      }
      break;
    }
    case WindPhase::Cruise: {
      updateSetpoint(nowMs);
      {
        const float stopTurns = Ramp::stoppingTurns(
            static_cast<float>(motor_->actualRpmAbs() > 0 ? motor_->actualRpmAbs() : commandRpm_),
            program_.rampDownMs / 1000.0f);
        const double remTurns =
            static_cast<double>(turns_.remainingCounts()) / SERVO_COUNTS_PER_REV;
        const double compTurns =
            static_cast<double>(STOP_COMPENSATION_COUNTS) / SERVO_COUNTS_PER_REV;
        if (remTurns <= stopTurns + compTurns) {
          enterPhase(WindPhase::RampDown, nowMs);
        }
      }
      break;
    }
    case WindPhase::RampDown: {
      updateSetpoint(nowMs);
      const bool timeDone = (nowMs - rampStartMs_ >= rampDurationMs_);
      const bool slow = motor_->actualRpmAbs() < 15 && commandRpm_ == 0;
      if (timeDone || slow) {
        commandRpm_ = 0;
        motor_->softStop();
        if (turns_.remainingCounts() <= FINAL_APPROACH_SKIP_COUNTS ||
            turns_.atTarget(FINAL_POSITION_TOLERANCE_COUNTS)) {
          enterPhase(WindPhase::Complete, nowMs);
        } else {
          enterPhase(WindPhase::FinalApproach, nowMs);
        }
      }
      break;
    }
    case WindPhase::FinalApproach: {
      if (!approachIssued_) {
        const int32_t rel = remainingSigned();
        if (llabs(rel) <= FINAL_APPROACH_SKIP_COUNTS) {
          enterPhase(WindPhase::Complete, nowMs);
          break;
        }
        approachIssued_ = motor_->startFinalApproach(rel);
        if (!approachIssued_) {
          faultText_ = "APPROACH FAIL";
          enterPhase(WindPhase::Fault, nowMs);
          break;
        }
      }
      if (turns_.atTarget(FINAL_POSITION_TOLERANCE_COUNTS) ||
          turns_.remainingCounts() == 0 || motor_->actualRpmAbs() == 0) {
        // Wait briefly for settle — non-blocking via remaining check
        if (turns_.atTarget(FINAL_POSITION_TOLERANCE_COUNTS) ||
            (motor_->actualRpmAbs() == 0 &&
             turns_.remainingCounts() <= FINAL_POSITION_TOLERANCE_COUNTS * 2)) {
          enterPhase(WindPhase::Complete, nowMs);
        }
      }
      // Timeout safety: if approach stalls too long
      if (nowMs - rampStartMs_ > 30000UL) {
        if (turns_.atTarget(FINAL_POSITION_TOLERANCE_COUNTS * 4)) {
          enterPhase(WindPhase::Complete, nowMs);
        } else {
          faultText_ = "APPROACH TIMEOUT";
          enterPhase(WindPhase::Fault, nowMs);
        }
      }
      break;
    }
    case WindPhase::Pausing: {
      updateSetpoint(nowMs);
      if ((nowMs - rampStartMs_ >= rampDurationMs_) ||
          (commandRpm_ == 0 && motor_->actualRpmAbs() < 10)) {
        enterPhase(WindPhase::Paused, nowMs);
      }
      break;
    }
    default:
      break;
  }
}

WindingStatus WindingController::status() const {
  WindingStatus s;
  s.phase = phase_;
  s.setRpm = commandRpm_;
  s.actualRpm = motor_ ? motor_->actualRpmAbs() : 0;
  s.turnsDone = turns_.turnsDisplay();
  s.turnsTarget = program_.targetTurns;
  s.turnsExact = turns_.turnsExact();
  s.activeMs = activeMs_;
  s.direction = program_.direction;
  s.rampUpType = program_.rampUpType;
  s.rampDownType = program_.rampDownType;
  s.faultText = faultText_;
  return s;
}
