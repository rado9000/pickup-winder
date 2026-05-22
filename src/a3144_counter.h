#pragma once

#include <Arduino.h>

void a3144Begin();
void a3144Reset();
void a3144SetTargetDirection(bool cw);
void a3144OnMotorDirection(bool cw);

float a3144Turns();
long a3144PulseCount();
void a3144Update();
