#include <Arduino.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <TMCStepper.h>
#if defined(ARDUINO_ARCH_RP2040)
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#endif

enum ButtonEvent : uint8_t;
ButtonEvent readButton();

// LCD 2004A (HD44780) with I2C backpack
const int LCD_I2C_ADDRESS = 0x27;
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 20, 4);
const int I2C_SDA_PIN = 0;
const int I2C_SCL_PIN = 1;

// Encoder with button (RP2040 GPIO pins)
const int ENC_A = 6; // interrupt pin
const int ENC_B = 7;
const int ENC_BTN = 8;

// TMC2208 Step/Dir interface (RP2040 GPIO pins)
const int STEP_PIN = 2;
const int DIR_PIN = 3;
const int EN_PIN = 10; // active LOW for most drivers

// A3144 digital Hall switch for turn counting (one magnet = one pulse/rev)
const int A3144_PIN = 9; // active LOW, use INPUT_PULLUP
#if defined(ARDUINO_ARCH_RP2040)
static uint stepSlice = 0;
static uint stepChannel = 0;
static bool stepPwmInited = false;
static const uint16_t STEP_PWM_WRAP = 2000;
#endif

// Optional UART configuration using TMCStepper library (Serial1 on RP2040)
// Domyślny sterownik to TMC2208 w trybie Step/Dir, więc UART jest domyślnie wyłączony.
#define USE_TMC2208_UART 0
#if USE_TMC2208_UART
// Arduino Mbed RP2040 core uses Serial1 default pins: TX=GP4, RX=GP5.
const int TMC_UART_TX = 4;
const int TMC_UART_RX = 5;
const float TMC_R_SENSE = 0.11f;
HardwareSerial &tmcSerial = Serial1;
TMC2208Stepper tmcDriver(&tmcSerial, TMC_R_SENSE);
#endif

// Winding settings
const int MAX_PRESETS = 32;
const long MAX_TURNS = 99999;
const int MIN_RPM = 1;
const int MAX_RPM_USER = 1500;
const int TURN_DIGITS = 5;
const int RPM_DIGITS = 4;

struct Preset {
  char name[12];
  long turns;
  int rpm;
  bool directionCW;
  bool valid;
};

const uint32_t EEPROM_MAGIC = 0x57494E44UL;
const uint8_t EEPROM_VERSION = 1;

struct EepromHeader {
  uint32_t magic;
  uint8_t version;
  uint8_t reserved[3];
};

const int EEPROM_HEADER_ADDR = 0;
const int EEPROM_PRESET_BASE_ADDR = EEPROM_HEADER_ADDR + sizeof(EepromHeader);

bool isPrintableNameChar(char c) {
  return (c >= 32 && c <= 126);
}

void sanitizePresetName(char *nameBuf, size_t len) {
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

bool looksLikeValidPreset(const Preset &preset) {
  if (!preset.valid) {
    return false;
  }
  if (preset.turns < 1 || preset.turns > MAX_TURNS) {
    return false;
  }
  if (preset.rpm < MIN_RPM || preset.rpm > MAX_RPM_USER) {
    return false;
  }
  if (preset.name[0] == '\0') {
    return false;
  }
  if (!isPrintableNameChar(preset.name[0])) {
    return false;
  }
  return true;
}

void writeHeaderIfNeeded() {
  EepromHeader header;
  EEPROM.get(EEPROM_HEADER_ADDR, header);
  if (header.magic != EEPROM_MAGIC || header.version != EEPROM_VERSION) {
    header.magic = EEPROM_MAGIC;
    header.version = EEPROM_VERSION;
    header.reserved[0] = header.reserved[1] = header.reserved[2] = 0;
    EEPROM.put(EEPROM_HEADER_ADDR, header);

    Preset empty;
    memset(&empty, 0, sizeof(empty));
    empty.valid = false;
    int addr = EEPROM_PRESET_BASE_ADDR;
    for (int i = 0; i < MAX_PRESETS; i++) {
      EEPROM.put(addr, empty);
      addr += sizeof(Preset);
    }
  }
}

Preset presets[MAX_PRESETS];

enum ScreenMode {
  SCREEN_MANUAL,
  SCREEN_PRESET_LIST,
  SCREEN_PRESET_NEW,
  SCREEN_PRESET_NAME,
  SCREEN_PRESET_VIEW,
  SCREEN_PRESET_FULL,
  SCREEN_GAUSS,
  SCREEN_PREWIND,
  SCREEN_COUNTDOWN,
  SCREEN_WINDING,
  SCREEN_DONE
};

enum ButtonEvent : uint8_t {
  BTN_NONE,
  BTN_CLICK,
  BTN_LONG
};

ScreenMode screenMode = SCREEN_MANUAL;
ScreenMode lastScreenMode = SCREEN_MANUAL;
int manualField = 0;
int menuIndex = 0;
int presetIndex = 0;
int presetListOffset = 0;
int presetField = 0;
int nameIndex = 0;
int manualDigitIndex = 0;
int presetDigitIndex = 0;
char presetName[12] = "PRESET";
int countdownValue = 3;
unsigned long countdownTickMs = 0;
bool windingPaused = false;
bool blinkOn = true;
unsigned long blinkTickMs = 0;
bool blinkDirty = false;
unsigned long windingUpdateMs = 0;

long targetTurns = 1000;
int targetRpm = 1000;
bool targetDirectionCW = true;
int turnsDigits[TURN_DIGITS] = {0, 0, 0, 0, 0};
int rpmDigits[RPM_DIGITS] = {1, 0, 0, 0};

volatile int encoderDelta = 0;
volatile uint8_t encoderState = 0;
int encoderAccum = 0;

unsigned long buttonDownMs = 0;
bool buttonWasDown = false;

// Winding state
long targetSteps = 0;
volatile long currentSteps = 0;
int currentRpm = 0;
const unsigned long WINDING_UI_INTERVAL_MS = 200;
const int ENCODER_DETENTS_PER_REV = 20;
const uint16_t PREWIND_STEP_INTERVAL_US = 800;
const uint16_t PREWIND_STEP_PULSE_US = 6;
const int A3144_PULSES_PER_REV = 1;
const uint32_t A3144_DEBOUNCE_US = 3000;
const int HALL_PIN = 26; // RP2040 ADC0 (GP26)
const float HALL_ADC_REF_V = 3.3f;
const int HALL_ADC_MAX = 4095;
const float HALL_ZERO_V = HALL_ADC_REF_V / 2.0f;
const float HALL_MV_PER_GAUSS = 0.92f;
const float GAUSS_ENTER_THRESHOLD = 50.0f;
const float GAUSS_EXIT_THRESHOLD = 50.0f;
const uint16_t GAUSS_ENTER_HOLD_MS = 250;
const uint16_t GAUSS_EXIT_HOLD_MS = 250;
#define USE_GAUSS_MONITOR 1

// Soft stop flow
#define USE_SOFT_STOP 0
bool stopRequested = false;
bool stopForCompletion = false;
bool pauseRequested = false;
bool menuRequested = false;

// Soft-start/stop ramp
// Set to 0 to disable soft start.
#define USE_SOFT_START 1
const uint16_t RAMP_MIN_MS = 4000;
const uint16_t RAMP_MAX_MS = 8000;
const uint16_t RAMP_BASE_MS = 2000;
const uint16_t RAMP_MS_PER_RPM = 3;
bool rampActive = false;
uint32_t rampStartMs = 0;
uint16_t rampDurationMs = 0;
int rampStartRpm = 0;
int rampTargetRpm = 0;
int commandedRpm = 0;
float rampStartHz = 0.0f;
float rampTargetHz = 0.0f;
const uint16_t RAMP_UPDATE_MS = 5;
uint32_t lastRampUpdateMs = 0;

// 17HS4401: 1.8° step angle => 200 full steps/rev
const int MICROSTEP = 1; // MS1=LOW, MS2=LOW, MS3=LOW => full step on TMC2208
const int STEPS_PER_REV = 200 * MICROSTEP;
const int COUNT_STEPS_PER_REV = STEPS_PER_REV;

float commandedStepHz = 0.0f;
float stepAccumulator = 0.0f;
uint32_t lastStepUpdateUs = 0;
float turnsAccum = 0.0f;
uint32_t lastTurnUpdateMs = 0;
float prewindStepCarry = 0.0f;
long prewindStepsQueued = 0;
uint32_t prewindLastStepUs = 0;
volatile bool motorDirectionCW = true;
volatile long a3144PulseCount = 0;
volatile uint32_t a3144LastPulseUs = 0;
volatile int8_t a3144LastDirection = 0;
volatile bool a3144PulseSeen = false;
float gaussValue = 0.0f;
float gaussZeroOffsetMv = 0.0f;
bool gaussActive = false;
int gaussReturnMode = SCREEN_MANUAL;
uint32_t gaussEnterSinceMs = 0;
uint32_t gaussExitSinceMs = 0;

void loadPresets() {
  writeHeaderIfNeeded();
  int addr = EEPROM_PRESET_BASE_ADDR;
  for (int i = 0; i < MAX_PRESETS; i++) {
    Preset preset;
    EEPROM.get(addr, preset);
    sanitizePresetName(preset.name, sizeof(preset.name));
    if (!looksLikeValidPreset(preset)) {
      memset(&preset, 0, sizeof(preset));
      preset.valid = false;
    }
    presets[i] = preset;
    addr += sizeof(Preset);
  }
}

void savePreset(int index, const Preset &preset) {
  if (index < 0 || index >= MAX_PRESETS) {
    return;
  }
  Preset stored = preset;
  stored.valid = true;
  sanitizePresetName(stored.name, sizeof(stored.name));
  int addr = EEPROM_PRESET_BASE_ADDR + index * sizeof(Preset);
  EEPROM.put(addr, stored);
}

int findEmptyPresetSlot() {
  for (int i = 0; i < MAX_PRESETS; i++) {
    if (!presets[i].valid) {
      return i;
    }
  }
  return -1;
}

void deletePreset(int index) {
  if (index < 0 || index >= MAX_PRESETS) {
    return;
  }
  presets[index].valid = false;
  int addr = EEPROM_PRESET_BASE_ADDR + index * sizeof(Preset);
  EEPROM.put(addr, presets[index]);
}

void valueToDigits(long value, int *digits, int count) {
  for (int i = count - 1; i >= 0; i--) {
    digits[i] = value % 10;
    value /= 10;
  }
}

long digitsToValue(const int *digits, int count) {
  long value = 0;
  for (int i = 0; i < count; i++) {
    value = value * 10 + digits[i];
  }
  return value;
}

int wrapDigit(int digit, int delta) {
  int value = (digit + delta) % 10;
  if (value < 0) {
    value += 10;
  }
  return value;
}

void syncDigitsFromTargets() {
  valueToDigits(targetTurns, turnsDigits, TURN_DIGITS);
  valueToDigits(targetRpm, rpmDigits, RPM_DIGITS);
}

void clampTargets() {
  if (targetTurns < 1) {
    targetTurns = 1;
  } else if (targetTurns > MAX_TURNS) {
    targetTurns = MAX_TURNS;
  }
  if (targetRpm < MIN_RPM) {
    targetRpm = MIN_RPM;
  } else if (targetRpm > MAX_RPM_USER) {
    targetRpm = MAX_RPM_USER;
  }
  syncDigitsFromTargets();
}

void printPadded(const char *text) {
  lcd.print(text);
  int len = strlen(text);
  for (int i = len; i < 20; i++) {
    lcd.print(" ");
  }
}

void printDigitsLine(const char *label, const int *digits, int count, bool selected, int activeIndex) {
  char line[21];
  int offset = snprintf(line, sizeof(line), "%s%s", selected ? ">" : " ", label);
  for (int i = 0; i < count && offset + i < 20; i++) {
    if (selected && i == activeIndex && !blinkOn) {
      line[offset + i] = ' ';
    } else {
      line[offset + i] = (char)('0' + digits[i]);
    }
  }
  int total = offset + count;
  for (int i = total; i < 20; i++) {
    line[i] = ' ';
  }
  line[20] = '\0';
  printPadded(line);
}

void drawManualHeader() {
  lcd.setCursor(0, 0);
  printPadded("Manual mode");
}

void drawManualTurnsLine() {
  lcd.setCursor(0, 1);
  printDigitsLine("Turns:", turnsDigits, TURN_DIGITS, manualField == 0, manualField == 0 ? manualDigitIndex : -1);
}

void drawManualRpmLine() {
  lcd.setCursor(0, 2);
  printDigitsLine("RPM:", rpmDigits, RPM_DIGITS, manualField == 1, manualField == 1 ? manualDigitIndex : -1);
}

void drawManualActionLine() {
  lcd.setCursor(0, 3);
  if (manualField == 2) {
    char line[21];
    const char *dirText = targetDirectionCW ? "CW" : "CCW";
    snprintf(line, sizeof(line), "> Dir:%s", blinkOn ? dirText : "  ");
    printPadded(line);
  } else if (manualField == 3) {
    printPadded("> Start winding");
  } else {
    printPadded("Hold: presets");
  }
}

void drawManualScreen() {
  lcd.clear();
  drawManualHeader();
  drawManualTurnsLine();
  drawManualRpmLine();
  drawManualActionLine();
}

void drawPresetListScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  printPadded("Presets");

  int totalItems = 1 + MAX_PRESETS;
  if (menuIndex < presetListOffset) {
    presetListOffset = menuIndex;
  }
  if (menuIndex >= presetListOffset + 2) {
    presetListOffset = menuIndex - 1;
  }

  for (int row = 0; row < 2; row++) {
    int itemIndex = presetListOffset + row;
    lcd.setCursor(0, row + 1);
    if (itemIndex >= totalItems) {
      printPadded(" ");
      continue;
    }
    char line[21];
    snprintf(line, sizeof(line), "%s", menuIndex == itemIndex ? "> " : "  ");
    int offset = strlen(line);
    if (itemIndex == 0) {
      snprintf(line + offset, sizeof(line) - offset, "New preset");
    } else {
      int presetSlot = itemIndex - 1;
      if (presets[presetSlot].valid) {
        snprintf(line + offset, sizeof(line) - offset, "%s", presets[presetSlot].name);
      } else {
        snprintf(line + offset, sizeof(line) - offset, "(empty)");
      }
    }
    printPadded(line);
  }
  lcd.setCursor(0, 3);
  printPadded("Hold: back");
}

void drawPresetNewScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  printPadded("New preset");
  lcd.setCursor(0, 1);
  printDigitsLine("Turns:", turnsDigits, TURN_DIGITS, presetField == 0, presetField == 0 ? presetDigitIndex : -1);
  lcd.setCursor(0, 2);
  printDigitsLine("RPM:", rpmDigits, RPM_DIGITS, presetField == 1, presetField == 1 ? presetDigitIndex : -1);
  lcd.setCursor(0, 3);
  char line[21];
  const char *dirText = targetDirectionCW ? "CW" : "CCW";
  snprintf(line, sizeof(line), "%s Dir:%s", presetField == 2 ? ">" : " ", (presetField == 2 && !blinkOn) ? "  " : dirText);
  printPadded(line);
}

void drawPresetTurnsLine() {
  lcd.setCursor(0, 1);
  printDigitsLine("Turns:", turnsDigits, TURN_DIGITS, presetField == 0, presetField == 0 ? presetDigitIndex : -1);
}

void drawPresetRpmLine() {
  lcd.setCursor(0, 2);
  printDigitsLine("RPM:", rpmDigits, RPM_DIGITS, presetField == 1, presetField == 1 ? presetDigitIndex : -1);
}

void drawPresetDirLine() {
  lcd.setCursor(0, 3);
  char line[21];
  const char *dirText = targetDirectionCW ? "CW" : "CCW";
  snprintf(line, sizeof(line), "%s Dir:%s", presetField == 2 ? ">" : " ", (presetField == 2 && !blinkOn) ? "  " : dirText);
  printPadded(line);
}

void drawPresetNameScreen() {
  char displayName[12];
  strncpy(displayName, presetName, sizeof(displayName));
  if (!blinkOn && nameIndex >= 0 && nameIndex < (int)sizeof(displayName) - 1) {
    displayName[nameIndex] = ' ';
  }
  lcd.clear();
  lcd.setCursor(0, 0);
  printPadded("Preset name");
  lcd.setCursor(0, 1);
  printPadded(displayName);
  lcd.setCursor(0, 2);
  printPadded("Click: next");
  lcd.setCursor(0, 3);
  printPadded("Hold: save");
}

void drawPresetNameLine() {
  char displayName[12];
  strncpy(displayName, presetName, sizeof(displayName));
  if (!blinkOn && nameIndex >= 0 && nameIndex < (int)sizeof(displayName) - 1) {
    displayName[nameIndex] = ' ';
  }
  lcd.setCursor(0, 1);
  printPadded(displayName);
}

void drawPresetViewScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  printPadded(presets[presetIndex].name);
  lcd.setCursor(0, 1);
  char line[21];
  snprintf(line, sizeof(line), "Turns: %ld", presets[presetIndex].turns);
  printPadded(line);
  lcd.setCursor(0, 2);
  snprintf(line, sizeof(line), "RPM: %d", presets[presetIndex].rpm);
  printPadded(line);
  lcd.setCursor(0, 3);
  snprintf(line, sizeof(line), "Dir: %s", presets[presetIndex].directionCW ? "CW" : "CCW");
  printPadded(line);
}

void drawPresetFullScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  printPadded("Memory full");
  lcd.setCursor(0, 1);
  printPadded("Delete preset");
  lcd.setCursor(0, 2);
  printPadded("to save new");
  lcd.setCursor(0, 3);
  printPadded("Press to return");
}

void drawGaussScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  printPadded("Magnet gauss meter");
  lcd.setCursor(0, 2);
  printPadded("Auto-hide <50G");
}

void drawGaussValues() {
  lcd.setCursor(0, 1);
  char line[21];
  float absGauss = fabsf(gaussValue);
  const char *pole = "CENTER";
  if (gaussValue >= GAUSS_ENTER_THRESHOLD) {
    pole = "N";
  } else if (gaussValue <= -GAUSS_ENTER_THRESHOLD) {
    pole = "S";
  }
  snprintf(line, sizeof(line), "G:%7.1f  Pole:%s", absGauss, pole);
  printPadded(line);
}

void drawScreenForMode(int mode) {
  if (mode == SCREEN_MANUAL) {
    drawManualScreen();
  } else if (mode == SCREEN_PRESET_LIST) {
    drawPresetListScreen();
  } else if (mode == SCREEN_PRESET_NEW) {
    drawPresetNewScreen();
  } else if (mode == SCREEN_PRESET_NAME) {
    drawPresetNameScreen();
  } else if (mode == SCREEN_PRESET_VIEW) {
    drawPresetViewScreen();
  } else if (mode == SCREEN_PRESET_FULL) {
    drawPresetFullScreen();
  }
}

bool isMenuScreen(int mode) {
  return mode == SCREEN_MANUAL || mode == SCREEN_PRESET_LIST ||
         mode == SCREEN_PRESET_NEW || mode == SCREEN_PRESET_NAME ||
         mode == SCREEN_PRESET_VIEW || mode == SCREEN_PRESET_FULL;
}

void updateGaussMonitor(uint32_t nowMs) {
  if (!USE_GAUSS_MONITOR) {
    gaussActive = false;
    gaussEnterSinceMs = 0;
    gaussExitSinceMs = 0;
    return;
  }
  int raw = analogRead(HALL_PIN);
  float voltage = ((float)raw / (float)HALL_ADC_MAX) * HALL_ADC_REF_V;
  float deltaMv = (voltage - HALL_ZERO_V) * 1000.0f - gaussZeroOffsetMv;
  gaussValue = deltaMv / HALL_MV_PER_GAUSS;
  float absGauss = fabsf(gaussValue);

  if (!gaussActive && isMenuScreen(screenMode)) {
    if (absGauss >= GAUSS_ENTER_THRESHOLD) {
      if (gaussEnterSinceMs == 0) {
        gaussEnterSinceMs = nowMs;
      }
      if (nowMs - gaussEnterSinceMs >= GAUSS_ENTER_HOLD_MS) {
        gaussReturnMode = screenMode;
        gaussActive = true;
        gaussExitSinceMs = 0;
        screenMode = SCREEN_GAUSS;
        drawGaussScreen();
        drawGaussValues();
        windingUpdateMs = nowMs;
        return;
      }
    } else {
      gaussEnterSinceMs = 0;
    }
  }

  if (gaussActive) {
    if (screenMode != SCREEN_GAUSS) {
      gaussActive = false;
      return;
    }
    if (absGauss <= GAUSS_EXIT_THRESHOLD) {
      if (gaussExitSinceMs == 0) {
        gaussExitSinceMs = nowMs;
      }
      if (nowMs - gaussExitSinceMs >= GAUSS_EXIT_HOLD_MS) {
        gaussActive = false;
        gaussEnterSinceMs = 0;
        gaussExitSinceMs = 0;
        screenMode = (ScreenMode)gaussReturnMode;
        drawScreenForMode(screenMode);
        return;
      }
    } else {
      gaussExitSinceMs = 0;
    }
    if (nowMs - windingUpdateMs >= WINDING_UI_INTERVAL_MS) {
      windingUpdateMs = nowMs;
      drawGaussValues();
    }
  }
}

void calibrateGaussZero() {
  if (!USE_GAUSS_MONITOR) {
    gaussZeroOffsetMv = 0.0f;
    return;
  }
  const int samples = 64;
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(HALL_PIN);
    delay(2);
  }
  float avgRaw = (float)sum / (float)samples;
  float avgVoltage = (avgRaw / (float)HALL_ADC_MAX) * HALL_ADC_REF_V;
  gaussZeroOffsetMv = (avgVoltage - HALL_ZERO_V) * 1000.0f;
}

void drawWindingScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  printPadded("Pickup winding");
  lcd.setCursor(0, 1);
  printPadded("in progress...");
  lcd.setCursor(0, 2);
  printPadded("Press to pause");
  lcd.setCursor(0, 3);
  printPadded(" ");
}

void drawPrewindScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  printPadded("Prewind mode");
  lcd.setCursor(0, 1);
  printPadded("Rotate encoder");
  lcd.setCursor(0, 2);
  printPadded("Click: start");
  lcd.setCursor(0, 3);
  printPadded("Hold: cancel");
}

void drawPrewindProgressLine() {
  lcd.setCursor(0, 1);
  char line[21];
  long turnsDone = (long)turnsAccum;
  if (turnsDone < 0) {
    turnsDone = 0;
  }
  long percent = targetTurns > 0 ? (turnsDone * 100L) / targetTurns : 0;
  snprintf(line, sizeof(line), "Turns:%5ld %3ld%%", turnsDone, percent);
  printPadded(line);
}

void drawWindingProgressLine() {
  lcd.setCursor(0, 1);
  char line[21];
  long turnsDone = (long)turnsAccum;
  if (turnsDone < 0) {
    turnsDone = 0;
  }
  long percent = targetTurns > 0 ? (turnsDone * 100L) / targetTurns : 0;
  snprintf(line, sizeof(line), "Turns:%5ld %3ld%%", turnsDone, percent);
  printPadded(line);
}

void drawCountdownScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  printPadded("Get ready");
  lcd.setCursor(0, 2);
  char line[21];
  snprintf(line, sizeof(line), "Starting in: %d", countdownValue);
  printPadded(line);
}

void drawPausedScreen() {
  lcd.setCursor(0, 0);
  printPadded("Paused (hold menu)");
  long turnsDone = (long)turnsAccum;
  if (turnsDone < 0) {
    turnsDone = 0;
  }
  long percent = targetTurns > 0 ? (turnsDone * 100L) / targetTurns : 0;
  lcd.setCursor(0, 1);
  char line[21];
  snprintf(line, sizeof(line), "Turns: %ld/%ld", turnsDone, targetTurns);
  printPadded(line);
  lcd.setCursor(0, 2);
  snprintf(line, sizeof(line), "Done: %ld%%", percent);
  printPadded(line);
  lcd.setCursor(0, 3);
  printPadded("Press: resume");
}

void drawDoneScreen() {
  lcd.clear();
  lcd.setCursor(0, 1);
  printPadded("Winding complete");
  lcd.setCursor(0, 2);
  printPadded("Press to return");
}

void setDirection(bool cw) {
  motorDirectionCW = cw;
  digitalWrite(DIR_PIN, cw ? LOW : HIGH);
}

void enableDriver(bool enable) {
  digitalWrite(EN_PIN, enable ? LOW : HIGH);
}

void disableStepOutput() {
#if defined(ARDUINO_ARCH_RP2040)
  if (stepPwmInited) {
    pwm_set_enabled(stepSlice, false);
  }
  pinMode(STEP_PIN, OUTPUT);
  digitalWrite(STEP_PIN, LOW);
#else
  analogWrite(STEP_PIN, 0);
#endif
}

void stepMotorSingle(bool cw) {
  setDirection(cw);
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(PREWIND_STEP_PULSE_US);
  digitalWrite(STEP_PIN, LOW);
}

void resetA3144Turns() {
  uint32_t nowUs = micros();
  noInterrupts();
  a3144PulseCount = 0;
  a3144LastPulseUs = nowUs;
  a3144LastDirection = 0;
  a3144PulseSeen = false;
  interrupts();
}

void handleA3144Interrupt() {
  uint32_t nowUs = micros();
  if (nowUs - a3144LastPulseUs < A3144_DEBOUNCE_US) {
    return;
  }
  a3144LastPulseUs = nowUs;

  int8_t countDirection = (motorDirectionCW == targetDirectionCW) ? 1 : -1;
  a3144PulseCount += countDirection;
  a3144LastDirection = countDirection;
  a3144PulseSeen = true;
}

long readA3144PulseCount() {
  noInterrupts();
  long pulseCount = a3144PulseCount;
  interrupts();
  return pulseCount;
}

uint16_t computeRampDurationMs(int targetRpmValue) {
  long ms = (long)RAMP_BASE_MS + (long)targetRpmValue * (long)RAMP_MS_PER_RPM;
  if (ms < RAMP_MIN_MS) {
    ms = RAMP_MIN_MS;
  }
  if (ms > RAMP_MAX_MS) {
    ms = RAMP_MAX_MS;
  }
  return (uint16_t)ms;
}

float smoothstep(float t) {
  if (t <= 0.0f) {
    return 0.0f;
  }
  if (t >= 1.0f) {
    return 1.0f;
  }
  return t * t * (3.0f - 2.0f * t);
}

void beginRampToTarget(int targetRpmValue, int startRpm) {
  float startHz = rpmToStepHz(startRpm);
  float targetHz = rpmToStepHz(targetRpmValue);

  if (!USE_SOFT_START) {
    rampActive = false;
    rampStartMs = 0;
    rampDurationMs = 0;
    rampStartRpm = targetRpmValue;
    rampTargetRpm = targetRpmValue;
    commandedRpm = targetRpmValue;
    currentRpm = targetRpmValue;
    setStepFrequency(targetHz);
    return;
  }
  rampActive = true;
  rampStartMs = millis();
  lastRampUpdateMs = rampStartMs;
  rampDurationMs = computeRampDurationMs(targetRpmValue);
  rampStartRpm = startRpm;
  rampTargetRpm = targetRpmValue;
  rampStartHz = startHz;
  rampTargetHz = targetHz;
}

float rpmToStepHz(int rpm) {
  if (rpm <= 0) {
    return 0.0f;
  }
  return (rpm / 60.0f) * (float)STEPS_PER_REV;
}

#if defined(ARDUINO_ARCH_RP2040)
void initStepPwm() {
  if (stepPwmInited) {
    return;
  }
  gpio_set_function(STEP_PIN, GPIO_FUNC_PWM);
  stepSlice = pwm_gpio_to_slice_num(STEP_PIN);
  stepChannel = pwm_gpio_to_channel(STEP_PIN);
  pwm_set_wrap(stepSlice, STEP_PWM_WRAP);
  pwm_set_chan_level(stepSlice, stepChannel, (STEP_PWM_WRAP + 1) / 2);
  pwm_set_enabled(stepSlice, false);
  stepPwmInited = true;
}

void setStepFrequencyPwm(float stepHz) {
  initStepPwm();
  if (stepHz <= 0.0f) {
    pwm_set_enabled(stepSlice, false);
    pinMode(STEP_PIN, OUTPUT);
    digitalWrite(STEP_PIN, LOW);
    return;
  }

  const float clockHz = (float)clock_get_hz(clk_sys);
  float clkdiv = clockHz / (stepHz * (float)(STEP_PWM_WRAP + 1));
  if (clkdiv < 1.0f) {
    clkdiv = 1.0f;
  }
  if (clkdiv > 255.996f) {
    clkdiv = 255.996f;
  }

  gpio_set_function(STEP_PIN, GPIO_FUNC_PWM);
  pwm_set_enabled(stepSlice, false);
  pwm_set_clkdiv(stepSlice, clkdiv);
  pwm_set_chan_level(stepSlice, stepChannel, (STEP_PWM_WRAP + 1) / 2);
  pwm_set_enabled(stepSlice, true);
}
#endif

void setStepFrequency(float stepHz) {
  commandedStepHz = stepHz;
#if defined(ARDUINO_ARCH_RP2040)
  setStepFrequencyPwm(stepHz);
#else
  if (stepHz <= 0.0f) {
    analogWrite(STEP_PIN, 0);
    return;
  }
  uint32_t freq = (uint32_t)lroundf(stepHz);
  if (freq < 1) {
    freq = 1;
  }
  analogWriteFreq(freq);
  analogWrite(STEP_PIN, 128);
#endif
}

void startStepTimer(int rpm, bool preserveTurns) {
  float stepHz = rpmToStepHz(rpm);
  commandedStepHz = stepHz;
  stepAccumulator = currentSteps;
  lastStepUpdateUs = micros();
  if (!preserveTurns) {
    turnsAccum = 0.0f;
  }
  lastTurnUpdateMs = millis();
  setStepFrequency(stepHz);
}

void stopStepTimer() {
  analogWrite(STEP_PIN, 0);
  commandedStepHz = 0.0f;
  rampActive = false;
  stopRequested = false;
  stopForCompletion = false;
  pauseRequested = false;
  menuRequested = false;
}

void updateRamp(uint32_t nowMs) {
  if (!USE_SOFT_START || !rampActive) {
    return;
  }
  if (nowMs - lastRampUpdateMs < RAMP_UPDATE_MS) {
    return;
  }
  lastRampUpdateMs = nowMs;
  uint32_t elapsed = nowMs - rampStartMs;
  float t = (rampDurationMs == 0) ? 1.0f : (elapsed / (float)rampDurationMs);
  float eased = smoothstep(t);
  float hz = rampStartHz + (rampTargetHz - rampStartHz) * eased;
  if (hz < 0.0f) {
    hz = 0.0f;
  }
  setStepFrequency(hz);
  commandedStepHz = hz;
  float rpmF = (hz * 60.0f) / (float)STEPS_PER_REV;
  int rpm = (int)lroundf(rpmF);
  if (rampTargetRpm == 0) {
    if (rpm < 0) {
      rpm = 0;
    }
  } else {
    if (rpm < MIN_RPM) {
      rpm = MIN_RPM;
    }
    if (rpm > rampTargetRpm) {
      rpm = rampTargetRpm;
    }
  }
  commandedRpm = rpm;
  currentRpm = rpm;
  if (elapsed >= rampDurationMs) {
    rampActive = false;
    setStepFrequency(rampTargetHz);
    commandedStepHz = rampTargetHz;
    commandedRpm = rampTargetRpm;
    currentRpm = rampTargetRpm;
  }
}

void updateStepCounting() {
  if (commandedStepHz <= 0.0f) {
    lastStepUpdateUs = micros();
    return;
  }
  uint32_t nowUs = micros();
  uint32_t deltaUs = nowUs - lastStepUpdateUs;
  lastStepUpdateUs = nowUs;
  stepAccumulator += commandedStepHz * (deltaUs / 1000000.0f);
  currentSteps = (long)stepAccumulator;
}

void updateTurnCounting(uint32_t nowMs) {
  (void)nowMs;
  long pulseCount = readA3144PulseCount();
  if (pulseCount < 0) {
    pulseCount = 0;
  }
  turnsAccum = (float)pulseCount / (float)A3144_PULSES_PER_REV;
}

void setupTmc2208Uart() {
#if USE_TMC2208_UART
  tmcSerial.begin(115200);
  tmcDriver.begin();
  tmcDriver.toff(4);
  tmcDriver.rms_current(1200);
  tmcDriver.microsteps(MICROSTEP);
  tmcDriver.intpol(true);
  tmcDriver.pwm_autoscale(true);
#endif
}

void handleEncoderInterrupt() {
  uint8_t state = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  uint8_t combined = (encoderState << 2) | state;
  static const int8_t table[16] = {0, -1, 1, 0, 1, 0, 0, -1,
                                   -1, 0, 0, 1, 0, 1, -1, 0};
  encoderDelta += table[combined];
  encoderState = state;
}

ButtonEvent readButton() {
  bool pressed = digitalRead(ENC_BTN) == LOW;
  if (pressed && !buttonWasDown) {
    buttonDownMs = millis();
    buttonWasDown = true;
  }
  if (!pressed && buttonWasDown) {
    unsigned long held = millis() - buttonDownMs;
    buttonWasDown = false;
    if (held > 800) {
      return BTN_LONG;
    }
    return BTN_CLICK;
  }
  return BTN_NONE;
}

char nextNameChar(char current, int delta) {
  const char charset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  const int charsetSize = sizeof(charset) - 1;
  int index = 0;
  for (int i = 0; i < charsetSize; i++) {
    if (charset[i] == current) {
      index = i;
      break;
    }
  }
  index = (index + delta) % charsetSize;
  if (index < 0) {
    index += charsetSize;
  }
  return charset[index];
}

void setup() {
  Wire.setSDA(I2C_SDA_PIN);
  Wire.setSCL(I2C_SCL_PIN);
  Wire.begin();
  Wire.setClock(100000);
  Wire.setTimeout(10);
  EEPROM.begin(1024);

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);
  pinMode(A3144_PIN, INPUT_PULLUP);
  if (USE_GAUSS_MONITOR) {
    pinMode(HALL_PIN, INPUT_PULLDOWN);
    analogReadResolution(12);
    calibrateGaussZero();
  }

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);
#if defined(ARDUINO_ARCH_RP2040)
  initStepPwm();
#endif

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  printPadded("RP2040 winder");
  lcd.setCursor(0, 1);
  printPadded("Starting...");
  delay(500);

  enableDriver(false);
  setupTmc2208Uart();
  loadPresets();
  syncDigitsFromTargets();
  blinkTickMs = millis();
  drawManualScreen();

  encoderState = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  attachInterrupt(digitalPinToInterrupt(ENC_A), handleEncoderInterrupt, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), handleEncoderInterrupt, CHANGE);
  attachInterrupt(digitalPinToInterrupt(A3144_PIN), handleA3144Interrupt, FALLING);
}

void redrawForBlink() {
  if (screenMode == SCREEN_MANUAL) {
    if (manualField == 0) {
      drawManualTurnsLine();
    } else if (manualField == 1) {
      drawManualRpmLine();
    } else if (manualField == 2) {
      drawManualActionLine();
    }
  } else if (screenMode == SCREEN_PRESET_NEW) {
    if (presetField == 0) {
      drawPresetTurnsLine();
    } else if (presetField == 1) {
      drawPresetRpmLine();
    } else if (presetField == 2) {
      drawPresetDirLine();
    }
  } else if (screenMode == SCREEN_PRESET_NAME) {
    drawPresetNameLine();
  }
}

void loop() {
  ButtonEvent buttonEvent = readButton();
  unsigned long nowMs = millis();
  int delta = 0;
  noInterrupts();
  delta = encoderDelta;
  encoderDelta = 0;
  interrupts();
  if (delta != 0) {
    encoderAccum += delta;
    if (encoderAccum >= 4) {
      delta = 1;
      encoderAccum = 0;
    } else if (encoderAccum <= -4) {
      delta = -1;
      encoderAccum = 0;
    } else {
      delta = 0;
    }
  }
  bool allowBlink = (screenMode == SCREEN_MANUAL && manualField <= 2) ||
                    (screenMode == SCREEN_PRESET_NEW) ||
                    (screenMode == SCREEN_PRESET_NAME);
  if (allowBlink && nowMs - blinkTickMs >= 500) {
    blinkTickMs = nowMs;
    blinkOn = !blinkOn;
    blinkDirty = true;
  }

  if (screenMode != lastScreenMode) {
    lastScreenMode = screenMode;
    blinkDirty = false;
  }

  if (blinkDirty) {
    redrawForBlink();
    blinkDirty = false;
  }

  if (USE_GAUSS_MONITOR) {
    updateGaussMonitor(nowMs);

    if (screenMode == SCREEN_GAUSS) {
      if (buttonEvent == BTN_LONG) {
        gaussActive = false;
        gaussEnterSinceMs = 0;
        gaussExitSinceMs = 0;
        screenMode = (ScreenMode)gaussReturnMode;
        drawScreenForMode(screenMode);
      }
      return;
    }
  }

  if (screenMode == SCREEN_MANUAL) {
    if (delta != 0) {
      if (manualField == 0) {
        turnsDigits[manualDigitIndex] = wrapDigit(turnsDigits[manualDigitIndex], delta);
        targetTurns = digitsToValue(turnsDigits, TURN_DIGITS);
      } else if (manualField == 1) {
        rpmDigits[manualDigitIndex] = wrapDigit(rpmDigits[manualDigitIndex], delta);
        targetRpm = digitsToValue(rpmDigits, RPM_DIGITS);
      } else if (manualField == 2) {
        targetDirectionCW = delta > 0 ? true : false;
      }
      drawManualScreen();
    }

    if (buttonEvent == BTN_CLICK) {
      if (manualField == 0) {
        manualDigitIndex++;
        if (manualDigitIndex >= TURN_DIGITS) {
          manualDigitIndex = 0;
          manualField = 1;
        }
        targetTurns = digitsToValue(turnsDigits, TURN_DIGITS);
        clampTargets();
        drawManualScreen();
      } else if (manualField == 1) {
        manualDigitIndex++;
        if (manualDigitIndex >= RPM_DIGITS) {
          manualDigitIndex = 0;
          manualField = 2;
        }
        targetRpm = digitsToValue(rpmDigits, RPM_DIGITS);
        clampTargets();
        drawManualScreen();
      } else if (manualField == 2) {
        manualField = 3;
        drawManualScreen();
      } else {
        screenMode = SCREEN_PREWIND;
        currentSteps = 0;
        stepAccumulator = 0.0f;
        turnsAccum = 0.0f;
        resetA3144Turns();
        prewindStepCarry = 0.0f;
        prewindStepsQueued = 0;
        prewindLastStepUs = micros();
        lastTurnUpdateMs = millis();
        currentRpm = 0;
        windingPaused = false;
        targetSteps = targetTurns * COUNT_STEPS_PER_REV;
        enableDriver(true);
        setDirection(targetDirectionCW);
        disableStepOutput();
        windingUpdateMs = millis();
        drawPrewindScreen();
      }
    } else if (buttonEvent == BTN_LONG) {
      screenMode = SCREEN_PRESET_LIST;
      menuIndex = 0;
      presetListOffset = 0;
      drawPresetListScreen();
    }
  } else if (screenMode == SCREEN_PRESET_LIST) {
    blinkDirty = false;
    int maxIndex = MAX_PRESETS;
    if (delta != 0) {
      menuIndex = constrain(menuIndex + delta, 0, maxIndex);
      drawPresetListScreen();
    }
    if (buttonEvent == BTN_CLICK) {
      if (menuIndex == 0) {
        screenMode = SCREEN_PRESET_NEW;
        presetField = 0;
        presetDigitIndex = 0;
        drawPresetNewScreen();
      } else {
        presetIndex = menuIndex - 1;
        if (presets[presetIndex].valid) {
          screenMode = SCREEN_PRESET_VIEW;
          drawPresetViewScreen();
        }
      }
    } else if (buttonEvent == BTN_LONG) {
      screenMode = SCREEN_MANUAL;
      manualField = 0;
      manualDigitIndex = 0;
      syncDigitsFromTargets();
      drawManualScreen();
    }
  } else if (screenMode == SCREEN_PRESET_NEW) {
    if (delta != 0) {
      if (presetField == 0) {
        turnsDigits[presetDigitIndex] = wrapDigit(turnsDigits[presetDigitIndex], delta);
        targetTurns = digitsToValue(turnsDigits, TURN_DIGITS);
      } else if (presetField == 1) {
        rpmDigits[presetDigitIndex] = wrapDigit(rpmDigits[presetDigitIndex], delta);
        targetRpm = digitsToValue(rpmDigits, RPM_DIGITS);
      } else if (presetField == 2) {
        targetDirectionCW = delta > 0 ? true : false;
      }
      drawPresetNewScreen();
    }

    if (buttonEvent == BTN_CLICK) {
      if (presetField == 0) {
        presetDigitIndex++;
        if (presetDigitIndex >= TURN_DIGITS) {
          presetDigitIndex = 0;
          presetField = 1;
        }
        targetTurns = digitsToValue(turnsDigits, TURN_DIGITS);
        clampTargets();
        drawPresetNewScreen();
      } else if (presetField == 1) {
        presetDigitIndex++;
        if (presetDigitIndex >= RPM_DIGITS) {
          presetDigitIndex = 0;
          presetField = 2;
        }
        targetRpm = digitsToValue(rpmDigits, RPM_DIGITS);
        clampTargets();
        drawPresetNewScreen();
      } else if (presetField == 2) {
        screenMode = SCREEN_PRESET_NAME;
        memset(presetName, 0, sizeof(presetName));
        strncpy(presetName, "PRESET", sizeof(presetName) - 1);
        nameIndex = 0;
        drawPresetNameScreen();
      }
    } else if (buttonEvent == BTN_LONG) {
      screenMode = SCREEN_PRESET_LIST;
      drawPresetListScreen();
    }
  } else if (screenMode == SCREEN_PRESET_NAME) {
    if (delta != 0) {
      presetName[nameIndex] = nextNameChar(presetName[nameIndex], delta);
      drawPresetNameScreen();
    }

    if (buttonEvent == BTN_CLICK) {
      nameIndex++;
      if (nameIndex >= (int)sizeof(presetName) - 1) {
        nameIndex = 0;
      }
      drawPresetNameScreen();
    } else if (buttonEvent == BTN_LONG) {
      int slot = findEmptyPresetSlot();
      if (slot < 0) {
        screenMode = SCREEN_PRESET_FULL;
        drawPresetFullScreen();
        return;
      }
      Preset preset;
      memset(&preset, 0, sizeof(preset));
      strncpy(preset.name, presetName, sizeof(preset.name) - 1);
      sanitizePresetName(preset.name, sizeof(preset.name));
      preset.turns = targetTurns;
      preset.rpm = min(targetRpm, MAX_RPM_USER);
      preset.directionCW = targetDirectionCW;
      preset.valid = true;
      savePreset(slot, preset);
      presets[slot] = preset;

      screenMode = SCREEN_PRESET_LIST;
      drawPresetListScreen();
    }
  } else if (screenMode == SCREEN_PRESET_VIEW) {
    blinkDirty = false;
    if (buttonEvent == BTN_CLICK) {
      targetTurns = presets[presetIndex].turns;
      targetRpm = min(presets[presetIndex].rpm, MAX_RPM_USER);
      targetDirectionCW = presets[presetIndex].directionCW;
      syncDigitsFromTargets();

      screenMode = SCREEN_PREWIND;
      currentSteps = 0;
      stepAccumulator = 0.0f;
      turnsAccum = 0.0f;
      resetA3144Turns();
      prewindStepCarry = 0.0f;
      prewindStepsQueued = 0;
      prewindLastStepUs = micros();
      lastTurnUpdateMs = millis();
      currentRpm = 0;
      windingPaused = false;
      targetSteps = targetTurns * COUNT_STEPS_PER_REV;
      enableDriver(true);
      setDirection(targetDirectionCW);
      disableStepOutput();
      windingUpdateMs = millis();
      drawPrewindScreen();
    } else if (buttonEvent == BTN_LONG) {
      deletePreset(presetIndex);
      screenMode = SCREEN_PRESET_LIST;
      drawPresetListScreen();
    }
  } else if (screenMode == SCREEN_PRESET_FULL) {
    blinkDirty = false;
    if (buttonEvent == BTN_CLICK || buttonEvent == BTN_LONG) {
      screenMode = SCREEN_PRESET_LIST;
      drawPresetListScreen();
    }
  } else if (screenMode == SCREEN_PREWIND) {
    blinkDirty = false;
    if (buttonEvent == BTN_CLICK) {
      prewindStepsQueued = 0;
      prewindStepCarry = 0.0f;
      disableStepOutput();
      screenMode = SCREEN_COUNTDOWN;
      countdownValue = 3;
      countdownTickMs = millis();
      drawCountdownScreen();
      return;
    }
    if (buttonEvent == BTN_LONG) {
      prewindStepsQueued = 0;
      prewindStepCarry = 0.0f;
      screenMode = SCREEN_MANUAL;
      manualField = 0;
      manualDigitIndex = 0;
      syncDigitsFromTargets();
      enableDriver(false);
      disableStepOutput();
      drawManualScreen();
      return;
    }
    if (delta != 0) {
      float stepsPerDetent = (float)STEPS_PER_REV / (float)ENCODER_DETENTS_PER_REV;
      prewindStepCarry += stepsPerDetent * (float)delta;
      long stepsToMove = (long)prewindStepCarry;
      if (stepsToMove != 0) {
        prewindStepCarry -= (float)stepsToMove;
        prewindStepsQueued += stepsToMove;
      }
    }
    if (prewindStepsQueued != 0) {
      uint32_t nowUs = micros();
      if (nowUs - prewindLastStepUs >= PREWIND_STEP_INTERVAL_US) {
        prewindLastStepUs = nowUs;
        bool stepCw = prewindStepsQueued > 0 ? targetDirectionCW : !targetDirectionCW;
        stepMotorSingle(stepCw);
        prewindStepsQueued += (prewindStepsQueued > 0) ? -1 : 1;
        stepAccumulator += (stepCw == targetDirectionCW) ? 1.0f : -1.0f;
        if (stepAccumulator < 0.0f) {
          stepAccumulator = 0.0f;
          prewindStepsQueued = 0;
          prewindStepCarry = 0.0f;
        }
        currentSteps = (long)stepAccumulator;
        updateTurnCounting(nowMs);
      }
    }
    updateTurnCounting(nowMs);
    if (nowMs - windingUpdateMs >= WINDING_UI_INTERVAL_MS) {
      windingUpdateMs = nowMs;
      drawPrewindProgressLine();
    }
  } else if (screenMode == SCREEN_COUNTDOWN) {
    blinkDirty = false;
    if (buttonEvent == BTN_CLICK) {
      clampTargets();
      screenMode = SCREEN_WINDING;
      enableDriver(true);
      lcd.clear();
      commandedRpm = MIN_RPM;
      currentRpm = commandedRpm;
      startStepTimer(commandedRpm, true);
      beginRampToTarget(targetRpm, commandedRpm);
      windingUpdateMs = nowMs;
      drawWindingScreen();
      return;
    }
    if (nowMs - countdownTickMs >= 1000) {
      countdownTickMs = nowMs;
      countdownValue--;
      if (countdownValue < 0) {
        clampTargets();
        screenMode = SCREEN_WINDING;
        enableDriver(true);
        lcd.clear();
        commandedRpm = MIN_RPM;
        currentRpm = commandedRpm;
        startStepTimer(commandedRpm, true);
        beginRampToTarget(targetRpm, commandedRpm);
        windingUpdateMs = nowMs;
        drawWindingScreen();
      } else {
        drawCountdownScreen();
      }
    }
  } else if (screenMode == SCREEN_WINDING) {
    blinkDirty = false;
    if (windingPaused) {
      if (buttonEvent == BTN_CLICK) {
        windingPaused = false;
        screenMode = SCREEN_COUNTDOWN;
        countdownValue = 3;
        countdownTickMs = millis();
        drawCountdownScreen();
      } else if (buttonEvent == BTN_LONG) {
        windingPaused = false;
        screenMode = SCREEN_MANUAL;
        manualField = 0;
        manualDigitIndex = 0;
        syncDigitsFromTargets();
        drawManualScreen();
      }
      return;
    }
    if (buttonEvent == BTN_CLICK) {
#if USE_SOFT_STOP
      if (!stopRequested) {
        stopRequested = true;
        stopForCompletion = false;
        pauseRequested = true;
        menuRequested = false;
        beginRampToTarget(0, currentRpm);
      }
#else
      stopStepTimer();
      enableDriver(false);
      windingPaused = true;
      blinkOn = true;
      lcd.clear();
      drawPausedScreen();
#endif
      return;
    }
    if (buttonEvent == BTN_LONG) {
#if USE_SOFT_STOP
      if (!stopRequested) {
        stopRequested = true;
        stopForCompletion = false;
        pauseRequested = false;
        menuRequested = true;
        beginRampToTarget(0, currentRpm);
      }
#else
      stopStepTimer();
      enableDriver(false);
      screenMode = SCREEN_MANUAL;
      manualField = 0;
      manualDigitIndex = 0;
      syncDigitsFromTargets();
      drawManualScreen();
#endif
      return;
    }
    updateRamp(nowMs);
    updateStepCounting();
    updateTurnCounting(nowMs);
#if USE_SOFT_STOP
    if (stopRequested && !rampActive) {
      stopStepTimer();
      enableDriver(false);
      stopRequested = false;
      if (stopForCompletion) {
        screenMode = SCREEN_DONE;
        drawDoneScreen();
      } else if (pauseRequested) {
        pauseRequested = false;
        windingPaused = true;
        blinkOn = true;
        lcd.clear();
        drawPausedScreen();
      } else if (menuRequested) {
        menuRequested = false;
        screenMode = SCREEN_MANUAL;
        manualField = 0;
        manualDigitIndex = 0;
        syncDigitsFromTargets();
        drawManualScreen();
      }
      return;
    }
#endif
    if (turnsAccum >= (float)targetTurns) {
#if USE_SOFT_STOP
      if (!stopRequested) {
        stopRequested = true;
        stopForCompletion = true;
        pauseRequested = false;
        menuRequested = false;
        beginRampToTarget(0, currentRpm);
      }
#else
      stopStepTimer();
      enableDriver(false);
      screenMode = SCREEN_DONE;
      drawDoneScreen();
#endif
    } else if (nowMs - windingUpdateMs >= WINDING_UI_INTERVAL_MS) {
      windingUpdateMs = nowMs;
      drawWindingProgressLine();
    }
  } else if (screenMode == SCREEN_DONE) {
    blinkDirty = false;
    if (buttonEvent == BTN_CLICK) {
      screenMode = SCREEN_MANUAL;
      manualField = 0;
      manualDigitIndex = 0;
      syncDigitsFromTargets();
      drawManualScreen();
    }
  }
}
