#pragma once

#include <Arduino.h>

// Odczyt driver.version() (0x21 = OK; 0 / 255 = zly pin, masa, zworka R8).
// Na RP2040: Serial1.setTX/setRX przed begin() — wewnatrz implementacji.
// Zwraca bajt wersji (0–255) albo -1 gdy brak sensownej odpowiedzi.
int tmc2209ProbeVersion();

// Pelna konfiguracja TMC (SpreadCycle itd.) — tylko gdy USE_TMC2209_UART=1.
bool tmc2209ConfigureOnce();

bool tmc2209Ready();
