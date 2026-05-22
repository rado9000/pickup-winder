#pragma once

#include <Arduino.h>

void gaussBegin();
bool gaussBootSetup();
void gaussCalibrateZero();
void gaussUpdate(uint32_t nowMs, int screenMode, int &ioScreenMode);

float gaussValue();
int gaussReturnScreenMode();
