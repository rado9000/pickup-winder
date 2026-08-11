#pragma once

#include "types.h"
#include "config.h"

struct PresetRecord {
  char name[PRESET_NAME_LEN + 1];
  WindingProgram program;
  uint8_t valid;
};

class PresetStore {
 public:
  void begin();
  uint8_t count() const { return count_; }
  bool get(uint8_t index, PresetRecord& out) const;
  bool saveNew(const PresetRecord& p);
  bool update(uint8_t index, const PresetRecord& p);
  bool remove(uint8_t index);
  Language loadLanguage() const;
  void saveLanguage(Language lang);

 private:
  uint8_t count_ = 0;
  void loadCount();
  void migrateIfNeeded();
  void clearAllPresets(Language keepLang);
  bool readAt(uint8_t index, PresetRecord& out) const;
  bool writeAt(uint8_t index, const PresetRecord& p);
  bool validate(const PresetRecord& p) const;
};
