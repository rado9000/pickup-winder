#include "presets.h"
#include "config.h"
#include <Preferences.h>

struct PresetHeader {
  uint32_t magic;
  uint8_t count;
};

static constexpr size_t HEADER_SIZE = sizeof(PresetHeader);
static constexpr size_t PRESET_SIZE = sizeof(Preset);
static Preferences prefs;

void PresetStore::begin() {
  load();
}

void PresetStore::load() {
  if (!prefs.begin("pwinder", true)) {
    count_ = 0;
    return;
  }

  PresetHeader hdr{};
  hdr.magic = prefs.getUInt("magic", 0);
  hdr.count = prefs.getUChar("count", 0);

  if (hdr.magic != MAGIC || hdr.count > PRESET_MAX_COUNT) {
    count_ = 0;
    prefs.end();
    return;
  }
  count_ = hdr.count;
  prefs.end();
}

bool PresetStore::get(uint8_t index, Preset& out) const {
  if (index >= count_) {
    return false;
  }

  Preferences readPrefs;
  if (!readPrefs.begin("pwinder", true)) {
    return false;
  }

  char key[8];
  snprintf(key, sizeof key, "p%u", index);
  size_t len = readPrefs.getBytesLength(key);
  if (len != PRESET_SIZE) {
    readPrefs.end();
    return false;
  }
  readPrefs.getBytes(key, &out, PRESET_SIZE);
  readPrefs.end();
  return out.valid != 0;
}

bool PresetStore::save(const Preset& p) {
  if (count_ >= PRESET_MAX_COUNT) {
    return false;
  }

  if (!prefs.begin("pwinder", false)) {
    return false;
  }

  Preset copy = p;
  copy.valid = 1;
  char key[8];
  snprintf(key, sizeof key, "p%u", count_);
  prefs.putBytes(key, &copy, PRESET_SIZE);
  count_++;

  prefs.putUInt("magic", MAGIC);
  prefs.putUChar("count", count_);
  prefs.end();
  return true;
}

void PresetStore::clearAll() {
  prefs.begin("pwinder", false);
  prefs.clear();
  prefs.end();
  count_ = 0;
}
