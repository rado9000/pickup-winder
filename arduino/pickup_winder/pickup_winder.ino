#include <Arduino.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include <TMCStepper.h>

enum ButtonEvent : uint8_t;
ButtonEvent readButton();
void drawPresetFullScreen();

// LCD 2004A (HD44780) with I2C backpack
const int LCD_I2C_ADDRESS = 0x27;
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 20, 4);

// Encoder with button
const int ENC_A = 2; // interrupt pin
const int ENC_B = 3;
const int ENC_BTN = 4;

// TMC2209 Step/Dir interface
// Use OC1A (D9) for hardware-toggled STEP on Timer1.
const int STEP_PIN = 9;
const int DIR_PIN = 6;
const int EN_PIN = 7; // active LOW for most drivers

// Optional UART configuration using TMCStepper library
#define USE_TMC2209_UART 1
#if USE_TMC2209_UART
const int TMC_UART_RX = A0;
const int TMC_UART_TX = A1;
const int TMC_UART_ADDRESS = 0;
const float TMC_R_SENSE = 0.11f;
SoftwareSerial tmcSerial(TMC_UART_RX, TMC_UART_TX);
TMC2209Stepper tmcDriver(&tmcSerial, TMC_R_SENSE, TMC_UART_ADDRESS);
#endif

// Winding settings
const int MAX_PRESETS = 8;
const long MAX_TURNS = 99999;
const int MIN_RPM = 1;
// Lower this if you see missed steps at high RPM.
const int MAX_RPM_USER = 600;
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
int targetRpm = 200;
bool targetDirectionCW = true;
int turnsDigits[TURN_DIGITS] = {0, 0, 0, 0, 0};
int rpmDigits[RPM_DIGITS] = {0, 2, 0, 0};

volatile int encoderDelta = 0;
volatile uint8_t encoderState = 0;
int encoderAccum = 0;

unsigned long buttonDownMs = 0;
bool buttonWasDown = false;

// Winding state
long targetSteps = 0;
volatile long currentSteps = 0;
int currentRpm = 0;
volatile bool stepLevel = false;
volatile bool stepEnabled = false;
const unsigned long WINDING_UI_INTERVAL_MS = 500;
bool stopRequested = false;
bool pauseAfterStop = false;
bool stopForCompletion = false;

// Soft-start ramp (optional, keeps tension stable)
const uint16_t RAMP_MIN_MS = 2500;
const uint16_t RAMP_MAX_MS = 6000;
const uint16_t RAMP_BASE_MS = 1500;
bool rampActive = false;
uint32_t rampStartMs = 0;
uint16_t rampDurationMs = 0;
int rampStartRpm = 0;
int rampTargetRpm = 0;
int commandedRpm = 0;

// 17HS4401: 1.8° step angle => 200 full steps/rev
const int MICROSTEP = 8; // 1/8 microstep to allow higher RPM headroom
const int STEPS_PER_REV = 200 * MICROSTEP;
// Set to match your actual driver microstep setting if turns stop early/late.
const int COUNT_STEPS_PER_REV = STEPS_PER_REV;

// Practical limit for TIMER1 ISR frequency on ATmega328P (UNO/Nano).
const long MAX_ISR_HZ = 40000;
const int MAX_RPM = (int)((MAX_ISR_HZ / 2.0f) * 60.0f / (200.0f * (float)MICROSTEP));

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
  if (targetRpm > MAX_RPM) {
    targetRpm = MAX_RPM;
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
  printPadded("Paused");
  long turnsDone = currentSteps / COUNT_STEPS_PER_REV;
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
  printPadded("Hold: menu");
}

void drawDoneScreen() {
  lcd.clear();
  lcd.setCursor(0, 1);
  printPadded("Winding complete");
  lcd.setCursor(0, 2);
  printPadded("Press to return");
}

void setDirection(bool cw) {
  digitalWrite(DIR_PIN, cw ? HIGH : LOW);
}

void enableDriver(bool enable) {
  digitalWrite(EN_PIN, enable ? LOW : HIGH);
}

uint16_t computeRampDurationMs(int targetRpmValue) {
  long ms = (long)RAMP_BASE_MS + (long)targetRpmValue * 2L;
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
  rampActive = true;
  rampStartMs = millis();
  rampDurationMs = computeRampDurationMs(targetRpmValue);
  rampStartRpm = startRpm;
  rampTargetRpm = targetRpmValue;
}

float rpmToStepHz(int rpm) {
  if (rpm <= 0) {
    return 0.0f;
  }
  return (rpm / 60.0f) * (float)STEPS_PER_REV;
}

void setupTimer1ForStepHz(float stepHz) {
  float isrHz = stepHz * 2.0f;

  struct Presc {
    uint16_t div;
    uint8_t csBits;
  };
  const Presc prescList[] = {
      {1, (1 << CS10)},
      {8, (1 << CS11)},
      {64, (1 << CS11) | (1 << CS10)},
      {256, (1 << CS12)},
      {1024, (1 << CS12) | (1 << CS10)}};

  uint16_t ocr = 0;
  uint8_t cs = 0;

  for (const auto &p : prescList) {
    float ticks = (16000000.0f / (float)p.div) / isrHz;
    if (ticks >= 2.0f && ticks <= 65535.0f) {
      ocr = (uint16_t)(ticks - 1.0f + 0.5f);
      cs = p.csBits;
      break;
    }
  }

  if (cs == 0) {
    cs = (1 << CS11);
    ocr = 1000;
  }

  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  TCCR1A |= (1 << COM1A0); // Toggle OC1A on compare match
  TCCR1B |= (1 << WGM12);
  OCR1A = ocr;
  TIMSK1 |= (1 << OCIE1A);
  TCCR1B |= cs;
  sei();
}

void startStepTimer(int rpm) {
  float stepHz = rpmToStepHz(rpm);
  if (stepHz <= 0.0f) {
    return;
  }
  noInterrupts();
  stepLevel = false;
  stepEnabled = true;
  interrupts();
  digitalWrite(STEP_PIN, LOW);
  setupTimer1ForStepHz(stepHz);
}

void stopStepTimer() {
  cli();
  stepEnabled = false;
  TIMSK1 &= ~(1 << OCIE1A);
  TCCR1B = 0;
  sei();
  digitalWrite(STEP_PIN, LOW);
  rampActive = false;
  stopRequested = false;
  pauseAfterStop = false;
  stopForCompletion = false;
}

void updateRamp(uint32_t nowMs) {
  if (!rampActive || !stepEnabled) {
    return;
  }
  uint32_t elapsed = nowMs - rampStartMs;
  float t = (rampDurationMs == 0) ? 1.0f : (elapsed / (float)rampDurationMs);
  float eased = smoothstep(t);
  float rpmF = rampStartRpm + (rampTargetRpm - rampStartRpm) * eased;
  int rpm = (int)lroundf(rpmF);
  if (rampTargetRpm == 0) {
    if (rpm < 0) {
      rpm = 0;
    }
  } else {
    if (rpm < MIN_RPM) {
      rpm = MIN_RPM;
    }
    if (rpm > targetRpm) {
      rpm = targetRpm;
    }
  }
  if (rpm != commandedRpm) {
    commandedRpm = rpm;
    currentRpm = rpm;
    if (commandedRpm > 0) {
      setupTimer1ForStepHz(rpmToStepHz(commandedRpm));
    }
  }
  if (elapsed >= rampDurationMs) {
    rampActive = false;
  }
}

void setupTmc2209Uart() {
#if USE_TMC2209_UART
  tmcSerial.begin(115200);
  tmcDriver.begin();
  tmcDriver.toff(4);
  tmcDriver.rms_current(1200);
  tmcDriver.microsteps(MICROSTEP);
  tmcDriver.intpol(true);
  tmcDriver.pwm_autoscale(true);
#endif
}

ISR(TIMER1_COMPA_vect) {
  if (!stepEnabled) {
    return;
  }
  stepLevel = !stepLevel;
  if (stepLevel) {
    currentSteps += 1;
  }
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
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  enableDriver(false);
  setupTmc2209Uart();
  loadPresets();
  syncDigitsFromTargets();
  blinkTickMs = millis();
  drawManualScreen();

  encoderState = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  attachInterrupt(digitalPinToInterrupt(ENC_A), handleEncoderInterrupt, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), handleEncoderInterrupt, CHANGE);
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
        screenMode = SCREEN_COUNTDOWN;
        currentSteps = 0;
        currentRpm = 0;
        windingPaused = false;
        targetSteps = targetTurns * COUNT_STEPS_PER_REV;
        enableDriver(false);
        setDirection(targetDirectionCW);
        countdownValue = 3;
        countdownTickMs = millis();
        drawCountdownScreen();
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

      screenMode = SCREEN_COUNTDOWN;
      currentSteps = 0;
      currentRpm = 0;
      windingPaused = false;
      targetSteps = targetTurns * COUNT_STEPS_PER_REV;
      enableDriver(false);
      setDirection(targetDirectionCW);
      countdownValue = 3;
      countdownTickMs = millis();
      drawCountdownScreen();
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
  } else if (screenMode == SCREEN_COUNTDOWN) {
    blinkDirty = false;
    if (buttonEvent == BTN_CLICK) {
      clampTargets();
      screenMode = SCREEN_WINDING;
      enableDriver(true);
      lcd.clear();
      commandedRpm = MIN_RPM;
      currentRpm = commandedRpm;
      startStepTimer(commandedRpm);
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
        startStepTimer(commandedRpm);
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
        screenMode = SCREEN_MANUAL;
        manualField = 0;
        manualDigitIndex = 0;
        syncDigitsFromTargets();
        drawManualScreen();
      }
      return;
    }
    if (buttonEvent == BTN_CLICK) {
      if (!stopRequested) {
        stopRequested = true;
        pauseAfterStop = true;
        stopForCompletion = false;
        beginRampToTarget(0, currentRpm);
      }
      return;
    }
    updateRamp(nowMs);
    long stepsSnapshot = 0;
    noInterrupts();
    stepsSnapshot = currentSteps;
    interrupts();
    if (stopRequested && commandedRpm == 0 && !stepLevel) {
      stopStepTimer();
      enableDriver(false);
      stopRequested = false;
      if (stopForCompletion) {
        screenMode = SCREEN_DONE;
        drawDoneScreen();
      } else if (pauseAfterStop) {
        windingPaused = true;
        blinkOn = true;
        lcd.clear();
        drawPausedScreen();
      }
      return;
    }
    if (stepsSnapshot >= targetSteps) {
      if (!stopRequested) {
        stopRequested = true;
        pauseAfterStop = false;
        stopForCompletion = true;
        beginRampToTarget(0, currentRpm);
      }
    } else if (nowMs - windingUpdateMs >= WINDING_UI_INTERVAL_MS) {
      windingUpdateMs = nowMs;
      // Keep LCD frozen during winding for maximum stability.
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
