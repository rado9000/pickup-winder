#pragma once

#include <Arduino.h>

void motorDriverBegin();
void motorSetDirection(bool cw);
void motorEnable(bool on);
bool motorDirectionCW();

void motorStartWinding(int targetRpm);
void motorUpdate();
void motorStopImmediate();

void motorSingleStep(bool cw);
void motorResetStepAccumulator();
long motorCurrentSteps();
