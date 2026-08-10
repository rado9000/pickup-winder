#include "turn_counter.h"
#include "config.h"

#include <math.h>
#include <stdlib.h>

int64_t TurnCounter::targetDeltaCounts(uint32_t turns, WindDir dir) {
  const int64_t mag = static_cast<int64_t>(turns) * SERVO_COUNTS_PER_REV;
  // Manual 0x31: CW increases encoder, CCW decreases.
  return (dir == WindDir::CW) ? mag : -mag;
}

double TurnCounter::countsToTurns(int64_t counts) {
  return static_cast<double>(llabs(counts)) / static_cast<double>(SERVO_COUNTS_PER_REV);
}

int64_t TurnCounter::signedProgress(int64_t start, int64_t now, WindDir dir) {
  const int64_t delta = now - start;
  // Progress along commanded direction as non-negative magnitude in signed space.
  if (dir == WindDir::CW) {
    return delta;  // want positive
  }
  return -delta;  // CCW: encoder decreases, progress is -delta
}

void TurnCounter::beginJob(int64_t encoderNow, uint32_t targetTurns, WindDir dir) {
  startEnc_ = encoderNow;
  currentEnc_ = encoderNow;
  targetTurns_ = targetTurns;
  dir_ = dir;
  targetEnc_ = startEnc_ + targetDeltaCounts(targetTurns, dir);
}

void TurnCounter::update(int64_t encoderNow) {
  currentEnc_ = encoderNow;
}

int64_t TurnCounter::progressCounts() const {
  return signedProgress(startEnc_, currentEnc_, dir_);
}

int64_t TurnCounter::remainingCounts() const {
  const int64_t targetMag =
      static_cast<int64_t>(targetTurns_) * SERVO_COUNTS_PER_REV;
  const int64_t done = progressCounts();
  const int64_t rem = targetMag - done;
  return rem > 0 ? rem : 0;
}

double TurnCounter::turnsExact() const {
  return countsToTurns(progressCounts());
}

uint32_t TurnCounter::turnsDisplay() const {
  const double t = turnsExact();
  if (t <= 0.0) {
    return 0;
  }
  if (t >= static_cast<double>(MAX_TURNS)) {
    return MAX_TURNS;
  }
  return static_cast<uint32_t>(t);
}

bool TurnCounter::atTarget(int64_t toleranceCounts) const {
  const int64_t err = llabs(currentEnc_ - targetEnc_);
  return err <= toleranceCounts;
}
