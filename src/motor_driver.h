#pragma once

#include <Arduino.h>

void motorDriverBegin();
void motorSetDirection(bool cw);
void motorEnable(bool on);
bool motorDirectionCW();

void motorStartWinding(int startRpm, int targetRpm, bool preserveSteps);
void motorRequestStop(bool forCompletion, bool pause, bool toMenu);
void motorUpdate(uint32_t nowMs);
void motorStopImmediate();

bool motorRampActive();
bool motorStopPending();
bool motorStopForCompletion();
bool motorStopPause();
bool motorStopToMenu();

int motorCommandedRpm();
float motorCommandedStepHz();

void motorSingleStep(bool cw);
void motorResetStepAccumulator();
long motorCurrentSteps();
float motorStepAccumulator();

void motorSyncStepAccumulator(long steps);
