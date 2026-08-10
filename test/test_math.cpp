// Host unit tests for ramp + turn math (platform = native)
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "config.h"
#include "ramp_generator.h"
#include "turn_counter.h"

static int gFails = 0;

#define CHECK(cond)                                                                          \
  do {                                                                                       \
    if (!(cond)) {                                                                           \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
      gFails++;                                                                              \
    }                                                                                        \
  } while (0)

int main() {
  // S-curve endpoints
  CHECK(std::fabs(Ramp::quinticS(0.0f)) < 1e-6f);
  CHECK(std::fabs(Ramp::quinticS(1.0f) - 1.0f) < 1e-6f);

  // Monotonic
  float prev = -0.1f;
  for (int i = 0; i <= 100; i++) {
    const float x = i / 100.0f;
    const float y = Ramp::quinticS(x);
    CHECK(y >= prev - 1e-6f);
    prev = y;
  }

  // Linear profile
  CHECK(std::fabs(Ramp::profile(RampType::Linear, 0.5f) - 0.5f) < 1e-6f);

  // Encoder conversions
  CHECK(std::fabs(TurnCounter::countsToTurns(16384) - 1.0) < 1e-9);
  CHECK(std::fabs(TurnCounter::countsToTurns(8192) - 0.5) < 1e-9);
  CHECK(std::fabs(TurnCounter::countsToTurns(163840) - 10.0) < 1e-9);
  CHECK(std::fabs(TurnCounter::countsToTurns(-16384) - 1.0) < 1e-9);

  // Target delta direction
  CHECK(TurnCounter::targetDeltaCounts(10, WindDir::CW) == 10 * 16384LL);
  CHECK(TurnCounter::targetDeltaCounts(10, WindDir::CCW) == -10 * 16384LL);

  // Progress CW / CCW
  CHECK(TurnCounter::signedProgress(1000, 1000 + 16384, WindDir::CW) == 16384);
  CHECK(TurnCounter::signedProgress(1000, 1000 - 16384, WindDir::CCW) == 16384);

  // Job simulation
  TurnCounter tc;
  tc.beginJob(0, 10, WindDir::CW);
  tc.update(5 * 16384);
  CHECK(tc.turnsDisplay() == 5);
  CHECK(tc.remainingCounts() == 5 * 16384);
  tc.update(10 * 16384);
  CHECK(tc.atTarget(0));

  // RPM clamp
  CHECK(clampRpm(0) == 0);
  CHECK(clampRpm(1) == 1);
  CHECK(clampRpm(2500) == 2500);
  CHECK(clampRpm(3000) == 2500);

  // Stopping distance
  const float st = Ramp::stoppingTurns(1200.0f, 3.0f);
  // 1200/60 * 3 * 0.5 = 30
  CHECK(std::fabs(st - 30.0f) < 1e-3f);

  if (gFails) {
    std::printf("%d test(s) failed\n", gFails);
    return 1;
  }
  std::printf("All math tests passed\n");
  return 0;
}
