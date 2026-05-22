#pragma once

#include <Arduino.h>
#include "config.h"

struct Preset {
  char name[PRESET_NAME_LEN];
  long turns;
  int rpm;
  bool directionCW;
  bool valid;
};

void presetsBegin();
void presetsLoad();
bool presetsGet(int index, Preset &out);
void presetsSave(int index, const Preset &p);
void presetsDelete(int index);
int presetsFindEmptySlot();

extern Preset presets[MAX_PRESETS];
