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
    case WindPhase::Stopping:
      return true;
    default:
      return false;
  }
}

void WindingController::commandStopOnly() {
  commandRpm_ = 0;
  if (motor_) {
    motor_->softStop();
  }
}

void WindingController::beginStopping(bool abortOutcome, uint32_t nowMs) {
  stoppingToAbort_ = abortOutcome;
  commandStopOnly();
  enterPhase(WindPhase::Stopping, nowMs);
}

bool WindingController::latchTargetReached(uint32_t nowMs) {
  if (targetReachedLatch_) {
    return true;
  }
  if (!turns_.targetReached()) {
    return false;
  }
  targetReachedLatch_ = true;
  commandStopOnly();
#if WIND_TARGET_DEBUG
  Serial.printf("[AUTO] TARGET REACHED progress=%.3f start=%lld cur=%lld abs=%llu tgt=%llu\n",
                turns_.turnsExact(),
                static_cast<long long>(turns_.startEncoder()),
                static_cast<long long>(turns_.currentEncoder()),
                static_cast<unsigned long long>(turns_.progressCounts()),
                static_cast<unsigned long long>(turns_.targetCounts()));
  Serial.println(F("[AUTO] STOPPING"));
#endif
  beginStopping(false, nowMs);
  return true;
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
      if (!targetReachedLatch_) {
        motor_->commandRpm(commandRpm_);
      }
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
      Serial.println(F("[AUTO] final forward approach"));
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
      // Keep driver ENABLED — holding torque preserves coil position.
      commandRpm_ = 0;
      motor_->softStop();
      activeTiming_ = false;
      break;
    case WindPhase::Stopping:
      Serial.println(F("[AUTO] STOPPING (wait RPM then release)"));
      commandRpm_ = 0;
      motor_->softStop();
      rampStartMs_ = nowMs;
      activeTiming_ = true;
      lastActiveStampMs_ = nowMs;
      break;
    case WindPhase::Complete:
      Serial.println(F("[WIND] COMPLETE"));
      commandRpm_ = 0;
      activeTiming_ = false;
      break;
    case WindPhase::Aborted:
      Serial.println(F("[WIND] ABORTED"));
      commandRpm_ = 0;
      activeTiming_ = false;
      break;
    case WindPhase::Fault:
      Serial.printf("[WIND] FAULT %s\n", faultText_ ? faultText_ : "?");
      commandRpm_ = 0;
      motor_->emergencyStop();
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

  // Job reference MUST be a fresh confirmed 0x31 — never a stale cache.
  motor_->resetEncoderDiag(millis());
  if (!motor_->refreshPositionNow(true)) {
    if (!motor_->detect() || !motor_->refreshPositionNow(true)) {
      faultText_ = "NO ENCODER";
      enterPhase(WindPhase::Fault, millis());
      return false;
    }
  }

  const int64_t enc = motor_->encoder();
  turns_.beginJob(enc, program_.targetTurns, program_.direction);
  pauseRequested_ = false;
  resumeRequested_ = false;
  abortRequested_ = false;
  approachIssued_ = false;
  targetReachedLatch_ = false;
  stoppingToAbort_ = false;
  activeMs_ = 0;
  commandRpm_ = 0;
  faultText_ = nullptr;

  Serial.printf("[WIND] START target=%lu rpm=%u dir=%s enc=%lld\n",
                static_cast<unsigned long>(program_.targetTurns), program_.targetRpm,
                program_.direction == WindDir::CW ? "CW" : "CCW",
                static_cast<long long>(enc));

  enterPhase(WindPhase::RampUp, millis());
  return true;
}

void WindingController::requestPause() {
  if (targetReachedLatch_) {
    return;
  }
  if (phase_ == WindPhase::RampUp || phase_ == WindPhase::Cruise ||
      phase_ == WindPhase::RampDown || phase_ == WindPhase::FinalApproach) {
    pauseRequested_ = true;
  }
}

void WindingController::requestResume() {
  if (targetReachedLatch_) {
    return;
  }
  if (phase_ == WindPhase::Paused) {
    resumeRequested_ = true;
  }
}

void WindingController::requestAbort() {
  if (phase_ == WindPhase::Paused || phase_ == WindPhase::Pausing) {
    abortRequested_ = true;
  }
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
  if (motor_->positionLost(nowMs, true)) {
    faultText_ = "RS485 POS LOSS";
    enterPhase(WindPhase::Fault, nowMs);
  }
}

void WindingController::updateSetpoint(uint32_t nowMs) {
  if (targetReachedLatch_) {
    commandStopOnly();
    return;
  }
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
    const bool windingBusy =
        (phase_ == WindPhase::RampUp || phase_ == WindPhase::Cruise ||
         phase_ == WindPhase::RampDown || phase_ == WindPhase::FinalApproach ||
         phase_ == WindPhase::Pausing || phase_ == WindPhase::Stopping);
    motor_->pollTelemetry(nowMs, windingBusy, turns_.remainingCounts());
    turns_.update(motor_->encoder());
  }

  if (activeTiming_) {
    activeMs_ += nowMs - lastActiveStampMs_;
    lastActiveStampMs_ = nowMs;
  }

  checkFaults(nowMs);
  if (phase_ == WindPhase::Fault) {
    if (motor_->actualRpmAbs() <= MOTOR_RELEASE_RPM_THRESHOLD && motor_->isEnabled()) {
      motor_->releaseMotor();
    }
    return;
  }

  // Hard target guard — every active phase. Past target → STOP, never reverse.
  if (phase_ == WindPhase::RampUp || phase_ == WindPhase::Cruise ||
      phase_ == WindPhase::RampDown || phase_ == WindPhase::FinalApproach ||
      phase_ == WindPhase::Pausing) {
    if (latchTargetReached(nowMs)) {
      return;
    }
  }

  if (abortRequested_ && (phase_ == WindPhase::Paused || phase_ == WindPhase::Pausing)) {
    abortRequested_ = false;
    beginStopping(true, nowMs);
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
#if WIND_TARGET_DEBUG
      {
        static uint32_t lastDbg = 0;
        if (nowMs - lastDbg >= 500) {
          lastDbg = nowMs;
          Serial.printf("[AUTO] progress=%.2f rem=%.2f rpm=%u\n",
                        turns_.turnsExact(),
                        static_cast<double>(turns_.remainingCounts()) / SERVO_COUNTS_PER_REV,
                        motor_->actualRpmAbs());
        }
      }
#endif
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
        // Already at/past target handled by latch. Still short → same-dir approach.
        if (turns_.remainingCounts() <= FINAL_APPROACH_SKIP_COUNTS ||
            turns_.atTarget(FINAL_POSITION_TOLERANCE_COUNTS)) {
          targetReachedLatch_ = true;
          beginStopping(false, nowMs);
        } else {
          enterPhase(WindPhase::FinalApproach, nowMs);
        }
      }
      break;
    }
    case WindPhase::FinalApproach: {
      // Same-direction F6 only — NEVER F4 / never reverse for correction.
      if (!approachIssued_) {
        if (turns_.remainingCounts() <=
            static_cast<uint64_t>(FINAL_APPROACH_SKIP_COUNTS)) {
          targetReachedLatch_ = true;
          beginStopping(false, nowMs);
          break;
        }
        approachIssued_ = motor_->commandFinalApproach(program_.direction);
        commandRpm_ = FINAL_APPROACH_RPM;
        if (!approachIssued_) {
          faultText_ = "APPROACH FAIL";
          enterPhase(WindPhase::Fault, nowMs);
          break;
        }
      } else if (!targetReachedLatch_) {
        // Re-issue same-dir speed periodically (F6 open-loop speed mode).
        if (nowMs - lastSetpointMs_ >= MOTOR_COMMAND_UPDATE_MS) {
          lastSetpointMs_ = nowMs;
          // Stop slightly early to reduce overshoot — forward only.
          if (turns_.remainingCounts() <=
              static_cast<uint64_t>(FINAL_FORWARD_STOP_COMPENSATION_COUNTS)) {
            commandStopOnly();
            targetReachedLatch_ = true;
#if WIND_TARGET_DEBUG
            Serial.printf("[AUTO] TARGET REACHED (early stop) progress=%.3f\n",
                          turns_.turnsExact());
            Serial.println(F("[AUTO] STOPPING"));
#endif
            beginStopping(false, nowMs);
            break;
          }
          motor_->commandFinalApproach(program_.direction);
        }
      }

      if (turns_.targetReached() || turns_.remainingCounts() == 0) {
        commandStopOnly();
        targetReachedLatch_ = true;
        beginStopping(false, nowMs);
        break;
      }

      if (nowMs - rampStartMs_ > 30000UL) {
        commandStopOnly();
        targetReachedLatch_ = true;
        Serial.println(F("[AUTO] approach timeout — stopping"));
        beginStopping(false, nowMs);
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
    case WindPhase::Stopping: {
      // Keep commanding stop until actually still, then final 0x31, then release.
      if (nowMs - lastSetpointMs_ >= MOTOR_COMMAND_UPDATE_MS) {
        lastSetpointMs_ = nowMs;
        motor_->softStop();
      }
      if (motor_->actualRpmAbs() <= MOTOR_RELEASE_RPM_THRESHOLD) {
        // Fresh encoder sample before Complete — authoritative final count.
        if (motor_->refreshPositionNow(true)) {
          turns_.update(motor_->encoder());
        }
        Serial.printf("[WIND] FINAL enc=%lld progress=%.4f maxGapMs=%lu ok=%lu fail=%lu\n",
                      static_cast<long long>(turns_.currentEncoder()),
                      turns_.turnsExact(),
                      static_cast<unsigned long>(motor_->maxEncoderGapMs()),
                      static_cast<unsigned long>(motor_->encoderPollOk()),
                      static_cast<unsigned long>(motor_->encoderPollFail()));
        motor_->releaseMotor();
#if WIND_TARGET_DEBUG
        Serial.println(F("[AUTO] MOTOR RELEASED"));
#endif
        if (stoppingToAbort_) {
          enterPhase(WindPhase::Aborted, nowMs);
        } else {
          enterPhase(WindPhase::Complete, nowMs);
        }
      } else if (nowMs - rampStartMs_ > 15000UL) {
        // Safety timeout: still try one final sample, then release.
        if (motor_->refreshPositionNow(true)) {
          turns_.update(motor_->encoder());
        }
        motor_->releaseMotor();
        if (stoppingToAbort_) {
          enterPhase(WindPhase::Aborted, nowMs);
        } else {
          enterPhase(WindPhase::Complete, nowMs);
        }
      }
      break;
    }
    case WindPhase::Fault: {
      // Handled above after checkFaults.
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
