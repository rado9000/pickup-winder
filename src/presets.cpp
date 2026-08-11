#include "presets.h"

#include <Preferences.h>
#include <stdio.h>
#include <string.h>
#include <Arduino.h>

static Preferences prefs;
static constexpr uint32_t kMagic = 0x50574E32;  // PWN2
static constexpr const char* kNs = "pwinder2";

void PresetStore::begin() {
  migrateIfNeeded();
  loadCount();
}

void PresetStore::migrateIfNeeded() {
  Language keepLang = Language::Polish;
  uint8_t storedSchema = 0;
  uint32_t magic = 0;

  if (prefs.begin(kNs, true)) {
    magic = prefs.getUInt("magic", 0);
    storedSchema = prefs.getUChar("schema", 0);
    const uint8_t v = prefs.getUChar("lang", 0);
    keepLang = (v == 1) ? Language::English : Language::Polish;
    prefs.end();
  }

  // Missing/unknown schema, wrong magic, or older firmware → one-time wipe.
  if (magic != kMagic || storedSchema != PRESET_STORAGE_VERSION) {
    Serial.printf("[NVS] preset schema %u -> %u; clearing legacy presets\n",
                  static_cast<unsigned>(storedSchema),
                  static_cast<unsigned>(PRESET_STORAGE_VERSION));
    clearAllPresets(keepLang);
  }
}

void PresetStore::clearAllPresets(Language keepLang) {
  if (!prefs.begin(kNs, false)) {
    count_ = 0;
    return;
  }
  // Physically remove old preset blobs (do not leave stale pN keys).
  for (uint8_t i = 0; i < PRESET_MAX_COUNT; i++) {
    char key[8];
    snprintf(key, sizeof key, "p%u", i);
    prefs.remove(key);
  }
  prefs.putUInt("magic", kMagic);
  prefs.putUChar("schema", PRESET_STORAGE_VERSION);
  prefs.putUChar("count", 0);
  prefs.putUChar("lang", static_cast<uint8_t>(keepLang));
  prefs.end();
  count_ = 0;
  Serial.println(F("[NVS] presets cleared; language preserved"));
}

void PresetStore::loadCount() {
  if (!prefs.begin(kNs, true)) {
    count_ = 0;
    return;
  }
  const uint32_t magic = prefs.getUInt("magic", 0);
  const uint8_t schema = prefs.getUChar("schema", 0);
  const uint8_t c = prefs.getUChar("count", 0);
  prefs.end();
  if (magic != kMagic || schema != PRESET_STORAGE_VERSION || c > PRESET_MAX_COUNT) {
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
  if (!r.begin(kNs, true)) {
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
  if (!prefs.begin(kNs, false)) {
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
  if (!prefs.begin(kNs, false)) {
    return false;
  }
  prefs.putUInt("magic", kMagic);
  prefs.putUChar("schema", PRESET_STORAGE_VERSION);
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
  // Remove trailing slot so stale blobs cannot resurrect.
  if (prefs.begin(kNs, false)) {
    char key[8];
    snprintf(key, sizeof key, "p%u", count_ - 1);
    prefs.remove(key);
    count_--;
    prefs.putUInt("magic", kMagic);
    prefs.putUChar("schema", PRESET_STORAGE_VERSION);
    prefs.putUChar("count", count_);
    prefs.end();
  } else {
    count_--;
  }
  return true;
}

Language PresetStore::loadLanguage() const {
  Preferences r;
  if (!r.begin(kNs, true)) {
    return Language::Polish;
  }
  const uint8_t v = r.getUChar("lang", 0);
  r.end();
  return (v == 1) ? Language::English : Language::Polish;
}

void PresetStore::saveLanguage(Language lang) {
  if (!prefs.begin(kNs, false)) {
    return;
  }
  prefs.putUChar("lang", static_cast<uint8_t>(lang));
  prefs.putUInt("magic", kMagic);
  prefs.putUChar("schema", PRESET_STORAGE_VERSION);
  prefs.end();
}
