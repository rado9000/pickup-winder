#include "turn_counter.h"
#include "config.h"

#include <math.h>
#include <stdlib.h>

double TurnCounter::countsToTurns(uint64_t counts) {
  return static_cast<double>(counts) / static_cast<double>(SERVO_COUNTS_PER_REV);
}

void TurnCounter::beginJob(int64_t encoderNow, uint32_t targetTurns, WindDir dir) {
  startEnc_ = encoderNow;
  currentEnc_ = encoderNow;
  targetTurns_ = targetTurns;
  dir_ = dir;
}

void TurnCounter::update(int64_t encoderNow) {
  currentEnc_ = encoderNow;
}

uint64_t TurnCounter::targetCounts() const {
  return static_cast<uint64_t>(targetTurns_) *
         static_cast<uint64_t>(SERVO_COUNTS_PER_REV);
}

uint64_t TurnCounter::progressCounts() const {
  const int64_t delta = currentEnc_ - startEnc_;
  const int64_t absDelta = (delta < 0) ? -delta : delta;
  return static_cast<uint64_t>(absDelta);
}

uint64_t TurnCounter::remainingCounts() const {
  const uint64_t target = targetCounts();
  const uint64_t done = progressCounts();
  return (done >= target) ? 0ULL : (target - done);
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

bool TurnCounter::targetReached() const {
  return progressCounts() >= targetCounts();
}

bool TurnCounter::atTarget(int64_t toleranceCounts) const {
  const uint64_t target = targetCounts();
  const uint64_t done = progressCounts();
  const uint64_t tol =
      (toleranceCounts < 0) ? 0ULL : static_cast<uint64_t>(toleranceCounts);
  if (done >= target) {
    return true;  // at or past target — never "need reverse"
  }
  return (target - done) <= tol;
}
