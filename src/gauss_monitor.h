#pragma once

#include <Arduino.h>

void gaussBegin();
void gaussCalibrateZero();
void gaussUpdate(uint32_t nowMs, int screenMode, int &ioScreenMode, bool menuVisible);

float gaussValue();
bool gaussActive();
void gaussForceExit();
