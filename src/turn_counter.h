#pragma once

#include <stdint.h>
#include "types.h"

// Encoder-based turn accounting. SERVO42ES cmd 0x31 is the sole source of truth.
// Progress is ABSOLUTE physical travel from job start — independent of whether
// CW increases or decreases the electrical encoder sign.
class TurnCounter {
 public:
  void beginJob(int64_t encoderNow, uint32_t targetTurns, WindDir dir);
  void update(int64_t encoderNow);

  int64_t startEncoder() const { return startEnc_; }
  int64_t currentEncoder() const { return currentEnc_; }

  // Absolute physical progress / remaining (saturates at 0 when past target).
  uint64_t targetCounts() const;
  uint64_t progressCounts() const;
  uint64_t remainingCounts() const;

  double turnsExact() const;
  uint32_t turnsDisplay() const;
  uint32_t targetTurns() const { return targetTurns_; }
  WindDir direction() const { return dir_; }

  // True when physical progress is within tolerance of (or past) target.
  bool atTarget(int64_t toleranceCounts) const;
  bool targetReached() const;  // progress >= targetCounts

  static double countsToTurns(uint64_t counts);

 private:
  int64_t startEnc_ = 0;
  int64_t currentEnc_ = 0;
  uint32_t targetTurns_ = 0;
  WindDir dir_ = WindDir::CW;  // job direction (motor), not used for count sign
};
