#pragma once

#include <stdint.h>
#include "types.h"

// Quintic smoothstep / linear ramp profiles (time-based, x in [0,1]).
namespace Ramp {

float quinticS(float x);
float profile(RampType type, float x);
float interpolate(float start, float end, float x, RampType type);

// Predicted stopping distance in turns for ramp from rpm to 0.
// Average normalized velocity ≈ 0.5 for both LINEAR and symmetric quintic.
float stoppingTurns(float actualRpm, float rampDownSeconds);

}  // namespace Ramp
