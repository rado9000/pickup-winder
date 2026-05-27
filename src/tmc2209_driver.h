#pragma once

#include <Arduino.h>

bool tmc2209Begin();
bool tmc2209Ready();
void tmc2209ApplyWindingProfile(int targetRpm);
void tmc2209Service(uint32_t nowMs);
