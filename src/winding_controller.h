#pragma once

#include "motor_controller.h"
#include "ramp_generator.h"
#include "turn_counter.h"
#include "types.h"

struct WindingStatus {
  WindPhase phase = WindPhase::Idle;
  uint16_t setRpm = 0;
  uint16_t actualRpm = 0;
  uint32_t turnsDone = 0;
  uint32_t turnsTarget = 0;
  double turnsExact = 0;
  uint32_t activeMs = 0;
  WindDir direction = WindDir::CW;
  RampType rampUpType = RampType::SCurve;
  RampType rampDownType = RampType::SCurve;
  const char* faultText = nullptr;
};

class WindingController {
 public:
  void begin(MotorController* motor);

  bool start(const WindingProgram& program);
  void requestPause();
  void requestResume();
  void requestAbort();

  void tick(uint32_t nowMs);

  bool isActive() const;
  bool isPaused() const { return phase_ == WindPhase::Paused; }
  bool isComplete() const { return phase_ == WindPhase::Complete; }
  bool isAborted() const { return phase_ == WindPhase::Aborted; }
  bool isFault() const { return phase_ == WindPhase::Fault; }

  WindingStatus status() const;
  const WindingProgram& program() const { return program_; }
  const TurnCounter& turns() const { return turns_; }

 private:
  void enterPhase(WindPhase p, uint32_t nowMs);
  void updateSetpoint(uint32_t nowMs);
  void checkFaults(uint32_t nowMs);
  bool latchTargetReached(uint32_t nowMs);  // stop + Complete if past target
  void commandStopOnly();

  MotorController* motor_ = nullptr;
  TurnCounter turns_;
  WindingProgram program_{};

  WindPhase phase_ = WindPhase::Idle;
  uint16_t commandRpm_ = 0;
  uint16_t rampStartRpm_ = 0;
  uint16_t rampEndRpm_ = 0;
  uint32_t rampStartMs_ = 0;
  uint32_t rampDurationMs_ = 0;
  uint32_t lastSetpointMs_ = 0;

  uint32_t activeMs_ = 0;
  uint32_t lastActiveStampMs_ = 0;
  bool activeTiming_ = false;

  bool pauseRequested_ = false;
  bool resumeRequested_ = false;
  bool abortRequested_ = false;
  bool approachIssued_ = false;

  // Once true for this job: never issue nonzero speed again — only STOP.
  bool targetReachedLatch_ = false;

  const char* faultText_ = nullptr;
};
