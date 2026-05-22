#pragma once

#include <Arduino.h>
#include "config.h"
#include "motor.h"

struct Preset {
  char name[PRESET_NAME_LEN];
  uint32_t turns;
  uint16_t rpm;
  WindingDir dir;
  uint8_t valid;
};

class PresetStore {
 public:
  void begin();
  uint8_t count() const { return count_; }
  bool get(uint8_t index, Preset& out) const;
  bool save(const Preset& p);
  void clearAll();

 private:
  uint8_t count_ = 0;
  static constexpr uint32_t MAGIC = 0x5057494E;  // PWIN

  void load();
  void persist();
};
