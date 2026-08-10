#pragma once

#include <stdint.h>
#include "types.h"

// Encoder-based turn accounting. SERVO42ES cmd 0x31 is the sole source of truth.
class TurnCounter {
 public:
  void beginJob(int64_t encoderNow, uint32_t targetTurns, WindDir dir);
  void update(int64_t encoderNow);

  int64_t startEncoder() const { return startEnc_; }
  int64_t targetEncoder() const { return targetEnc_; }
  int64_t currentEncoder() const { return currentEnc_; }
  int64_t remainingCounts() const;
  int64_t progressCounts() const;

  double turnsExact() const;
  uint32_t turnsDisplay() const;
  uint32_t targetTurns() const { return targetTurns_; }
  WindDir direction() const { return dir_; }

  bool atTarget(int64_t toleranceCounts) const;

  // Signed delta: CW positive per manual (CW += 0x4000 / rev).
  static int64_t signedProgress(int64_t start, int64_t now, WindDir dir);
  static int64_t targetDeltaCounts(uint32_t turns, WindDir dir);
  static double countsToTurns(int64_t counts);

 private:
  int64_t startEnc_ = 0;
  int64_t targetEnc_ = 0;
  int64_t currentEnc_ = 0;
  uint32_t targetTurns_ = 0;
  WindDir dir_ = WindDir::CW;
};
