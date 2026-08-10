#include "presets.h"

#include <Preferences.h>
#include <stdio.h>
#include <string.h>

static Preferences prefs;
static constexpr uint32_t kMagic = 0x50574E32;  // PWN2

void PresetStore::begin() {
  loadCount();
}

void PresetStore::loadCount() {
  if (!prefs.begin("pwinder2", true)) {
    count_ = 0;
    return;
  }
  const uint32_t magic = prefs.getUInt("magic", 0);
  const uint8_t c = prefs.getUChar("count", 0);
  prefs.end();
  if (magic != kMagic || c > PRESET_MAX_COUNT) {
    count_ = 0;
    return;
  }
  count_ = c;
}

bool PresetStore::validate(const PresetRecord& p) const {
  if (!p.valid) {
    return false;
  }
  if (p.program.targetTurns < MIN_TURNS || p.program.targetTurns > MAX_TURNS) {
    return false;
  }
  if (p.program.targetRpm < MIN_WINDER_RPM || p.program.targetRpm > MAX_WINDER_RPM) {
    return false;
  }
  if (p.program.rampUpMs < RAMP_TIME_MIN_MS || p.program.rampUpMs > RAMP_TIME_MAX_MS) {
    return false;
  }
  if (p.program.rampDownMs < RAMP_TIME_MIN_MS || p.program.rampDownMs > RAMP_TIME_MAX_MS) {
    return false;
  }
  return true;
}

bool PresetStore::readAt(uint8_t index, PresetRecord& out) const {
  Preferences r;
  if (!r.begin("pwinder2", true)) {
    return false;
  }
  char key[8];
  snprintf(key, sizeof key, "p%u", index);
  const size_t len = r.getBytesLength(key);
  if (len != sizeof(PresetRecord)) {
    r.end();
    return false;
  }
  r.getBytes(key, &out, sizeof(PresetRecord));
  r.end();
  out.program.targetRpm = clampRpm(out.program.targetRpm);
  out.program.targetTurns = clampTurns(out.program.targetTurns);
  out.program.rampUpMs = clampRampMs(out.program.rampUpMs);
  out.program.rampDownMs = clampRampMs(out.program.rampDownMs);
  return validate(out);
}

bool PresetStore::writeAt(uint8_t index, const PresetRecord& p) {
  if (!prefs.begin("pwinder2", false)) {
    return false;
  }
  char key[8];
  snprintf(key, sizeof key, "p%u", index);
  prefs.putBytes(key, &p, sizeof(PresetRecord));
  prefs.end();
  return true;
}

bool PresetStore::get(uint8_t index, PresetRecord& out) const {
  if (index >= count_) {
    return false;
  }
  return readAt(index, out);
}

bool PresetStore::saveNew(const PresetRecord& p) {
  if (count_ >= PRESET_MAX_COUNT || !validate(p)) {
    return false;
  }
  PresetRecord copy = p;
  copy.valid = 1;
  copy.name[PRESET_NAME_LEN] = 0;
  if (!writeAt(count_, copy)) {
    return false;
  }
  count_++;
  if (!prefs.begin("pwinder2", false)) {
    return false;
  }
  prefs.putUInt("magic", kMagic);
  prefs.putUChar("count", count_);
  prefs.end();
  return true;
}

bool PresetStore::update(uint8_t index, const PresetRecord& p) {
  if (index >= count_ || !validate(p)) {
    return false;
  }
  PresetRecord copy = p;
  copy.valid = 1;
  return writeAt(index, copy);
}

bool PresetStore::remove(uint8_t index) {
  if (index >= count_) {
    return false;
  }
  for (uint8_t i = index; i + 1 < count_; i++) {
    PresetRecord p{};
    if (!readAt(i + 1, p)) {
      return false;
    }
    if (!writeAt(i, p)) {
      return false;
    }
  }
  count_--;
  if (!prefs.begin("pwinder2", false)) {
    return false;
  }
  prefs.putUInt("magic", kMagic);
  prefs.putUChar("count", count_);
  prefs.end();
  return true;
}

Language PresetStore::loadLanguage() const {
  Preferences r;
  if (!r.begin("pwinder2", true)) {
    return Language::Polish;
  }
  const uint8_t v = r.getUChar("lang", 0);
  r.end();
  return (v == 1) ? Language::English : Language::Polish;
}

void PresetStore::saveLanguage(Language lang) {
  if (!prefs.begin("pwinder2", false)) {
    return;
  }
  prefs.putUChar("lang", static_cast<uint8_t>(lang));
  prefs.putUInt("magic", kMagic);
  prefs.end();
}
