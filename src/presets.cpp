#include "presets.h"
#include "config.h"
#include <EEPROM.h>

struct PresetHeader {
  uint32_t magic;
  uint8_t count;
};

static constexpr size_t HEADER_SIZE = sizeof(PresetHeader);
static constexpr size_t PRESET_SIZE = sizeof(Preset);
static constexpr size_t EEPROM_SIZE = HEADER_SIZE + PRESET_MAX_COUNT * PRESET_SIZE;

void PresetStore::begin() {
  EEPROM.begin((int)EEPROM_SIZE);
  load();
}

void PresetStore::load() {
  PresetHeader hdr{};
  EEPROM.get(0, hdr);
  if (hdr.magic != MAGIC || hdr.count > PRESET_MAX_COUNT) {
    count_ = 0;
    return;
  }
  count_ = hdr.count;
}

void PresetStore::persist() {
  PresetHeader hdr{MAGIC, count_};
  EEPROM.put(0, hdr);
  for (uint8_t i = 0; i < count_; i++) {
    Preset p{};
    EEPROM.get((int)(HEADER_SIZE + i * PRESET_SIZE), p);
    (void)p;
  }
  EEPROM.commit();
}

bool PresetStore::get(uint8_t index, Preset& out) const {
  if (index >= count_) {
    return false;
  }
  EEPROM.get((int)(HEADER_SIZE + index * PRESET_SIZE), out);
  return out.valid != 0;
}

bool PresetStore::save(const Preset& p) {
  if (count_ >= PRESET_MAX_COUNT) {
    return false;
  }
  Preset copy = p;
  copy.valid = 1;
  EEPROM.put((int)(HEADER_SIZE + count_ * PRESET_SIZE), copy);
  count_++;
  PresetHeader hdr{MAGIC, count_};
  EEPROM.put(0, hdr);
  EEPROM.commit();
  return true;
}

void PresetStore::clearAll() {
  count_ = 0;
  PresetHeader hdr{MAGIC, 0};
  EEPROM.put(0, hdr);
  EEPROM.commit();
}
