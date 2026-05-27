#include "presets_store.h"
#include "config.h"
#include <EEPROM.h>
#include <cstring>

Preset presets[MAX_PRESETS];

static const uint32_t EEPROM_MAGIC = 0x57494E44UL;
static const uint8_t EEPROM_VERSION = 1;

struct EepromHeader {
  uint32_t magic;
  uint8_t version;
  uint8_t reserved[3];
};

static const int EEPROM_HEADER_ADDR = 0;
static const int EEPROM_PRESET_BASE_ADDR = EEPROM_HEADER_ADDR + (int)sizeof(EepromHeader);

static bool isPrintableNameChar(char c) {
  return (c >= 32 && c <= 126);
}

static void sanitizePresetName(char *nameBuf, size_t len) {
  if (len == 0) {
    return;
  }
  nameBuf[len - 1] = '\0';
  for (size_t i = 0; i < len - 1; i++) {
    char c = nameBuf[i];
    if (c == '\0') {
      break;
    }
    if (!isPrintableNameChar(c)) {
      nameBuf[i] = ' ';
    }
  }
}

static bool looksLikeValidPreset(const Preset &preset) {
  if (!preset.valid) {
    return false;
  }
  if (preset.turns < 1 || preset.turns > MAX_TURNS) {
    return false;
  }
  if (preset.rpm < MIN_RPM || preset.rpm > MAX_RPM) {
    return false;
  }
  if (preset.name[0] == '\0' || !isPrintableNameChar(preset.name[0])) {
    return false;
  }
  return true;
}

static void writeHeaderIfNeeded() {
  EepromHeader header;
  EEPROM.get(EEPROM_HEADER_ADDR, header);
  if (header.magic != EEPROM_MAGIC || header.version != EEPROM_VERSION) {
    header.magic = EEPROM_MAGIC;
    header.version = EEPROM_VERSION;
    header.reserved[0] = header.reserved[1] = header.reserved[2] = 0;
    EEPROM.put(EEPROM_HEADER_ADDR, header);
    Preset empty{};
    int addr = EEPROM_PRESET_BASE_ADDR;
    for (int i = 0; i < MAX_PRESETS; i++) {
      EEPROM.put(addr, empty);
      addr += (int)sizeof(Preset);
    }
  }
}

void presetsBegin() {
  EEPROM.begin(EEPROM_SIZE);
  writeHeaderIfNeeded();
}

void presetsLoad() {
  writeHeaderIfNeeded();
  int addr = EEPROM_PRESET_BASE_ADDR;
  for (int i = 0; i < MAX_PRESETS; i++) {
    Preset p{};
    EEPROM.get(addr, p);
    sanitizePresetName(p.name, sizeof(p.name));
    if (!looksLikeValidPreset(p)) {
      memset(&p, 0, sizeof(p));
      p.valid = false;
    }
    presets[i] = p;
    addr += (int)sizeof(Preset);
  }
}

bool presetsGet(int index, Preset &out) {
  if (index < 0 || index >= MAX_PRESETS) {
    return false;
  }
  out = presets[index];
  return out.valid;
}

void presetsSave(int index, const Preset &p) {
  if (index < 0 || index >= MAX_PRESETS) {
    return;
  }
  Preset stored = p;
  stored.valid = true;
  sanitizePresetName(stored.name, sizeof(stored.name));
  presets[index] = stored;
  int addr = EEPROM_PRESET_BASE_ADDR + index * (int)sizeof(Preset);
  EEPROM.put(addr, stored);
}

void presetsDelete(int index) {
  if (index < 0 || index >= MAX_PRESETS) {
    return;
  }
  presets[index].valid = false;
  int addr = EEPROM_PRESET_BASE_ADDR + index * (int)sizeof(Preset);
  EEPROM.put(addr, presets[index]);
}

int presetsFindEmptySlot() {
  for (int i = 0; i < MAX_PRESETS; i++) {
    if (!presets[i].valid) {
      return i;
    }
  }
  return -1;
}
