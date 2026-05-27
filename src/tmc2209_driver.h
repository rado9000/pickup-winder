#pragma once

#include <Arduino.h>

// Jednorazowo przy pierwszym nawijaniu: SpreadCycle, intpol off (~50 ms, timeout UART).
// false = brak UART / zostaw STEP+DIR.
bool tmc2209ApplySpreadCycleOnce();
