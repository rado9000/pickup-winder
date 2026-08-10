#include "ramp_generator.h"

#include <math.h>

namespace Ramp {

float quinticS(float x) {
  if (x <= 0.0f) {
    return 0.0f;
  }
  if (x >= 1.0f) {
    return 1.0f;
  }
  const float x2 = x * x;
  const float x3 = x2 * x;
  const float x4 = x2 * x2;
  const float x5 = x4 * x;
  return 10.0f * x3 - 15.0f * x4 + 6.0f * x5;
}

float profile(RampType type, float x) {
  if (type == RampType::Linear) {
    if (x <= 0.0f) {
      return 0.0f;
    }
    if (x >= 1.0f) {
      return 1.0f;
    }
    return x;
  }
  return quinticS(x);
}

float interpolate(float start, float end, float x, RampType type) {
  return start + (end - start) * profile(type, x);
}

float stoppingTurns(float actualRpm, float rampDownSeconds) {
  if (actualRpm <= 0.0f || rampDownSeconds <= 0.0f) {
    return 0.0f;
  }
  return (actualRpm / 60.0f) * rampDownSeconds * 0.5f;
}

}  // namespace Ramp
