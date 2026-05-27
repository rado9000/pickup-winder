#pragma once

#include <Arduino.h>

// UART tylko raz w setup() — podczas nawijania zero komunikacji (stabilny STEP).
bool tmc2209ConfigureOnce();

bool tmc2209Ready();
