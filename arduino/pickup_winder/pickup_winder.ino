#include <Arduino.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>

// LCD 2004A (HD44780) with I2C backpack
const int LCD_I2C_ADDRESS = 0x27;
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 20, 4);

// Encoder with button
const int ENC_A = 2; // interrupt pin
const int ENC_B = 3;
const int ENC_BTN = 4;

// TMC2209 Step/Dir interface
const int STEP_PIN = 5;
const int DIR_PIN = 6;
const int EN_PIN = 7; // active LOW for most drivers

// Winding settings
const int MAX_PRESETS = 8;
const long MAX_TURNS = 99999;
const int MIN_RPM = 1;
const int MAX_RPM = 2000;
const int TURN_DIGITS = 5;
const int RPM_DIGITS = 4;

struct Preset {
  char name[12];
  long turns;
  int rpm;
  bool directionCW;
  bool valid;
};

Preset presets[MAX_PRESETS];

enum ScreenMode {
  SCREEN_MANUAL,
  SCREEN_PRESET_LIST,
  SCREEN_PRESET_NEW,
  SCREEN_PRESET_NAME,
  SCREEN_PRESET_VIEW,
  SCREEN_COUNTDOWN,
  SCREEN_WINDING,
  SCREEN_DONE
};

enum ButtonEvent {
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
int lastEncA = LOW;

unsigned long buttonDownMs = 0;
bool buttonWasDown = false;

// Winding state
long targetSteps = 0;
long currentSteps = 0;
int currentRpm = 0;
unsigned long lastStepMicros = 0;
unsigned long stepIntervalMicros = 0;

// 42STH60-2004Q: 1.8° step angle => 200 full steps/rev
const int MICROSTEP = 8; // 1,2,4,8,16... match driver microstep setting
const int STEPS_PER_REV = 200 * MICROSTEP;

void loadPresets() {
  int addr = 0;
  for (int i = 0; i < MAX_PRESETS; i++) {
    EEPROM.get(addr, presets[i]);
    if (presets[i].valid != true) {
      presets[i].valid = false;
    }
    addr += sizeof(Preset);
  }
}

void savePreset(int index, const Preset &preset) {
  int addr = index * sizeof(Preset);
  EEPROM.put(addr, preset);
}

int findEmptyPresetSlot() {
  for (int i = 0; i < MAX_PRESETS; i++) {
    if (!presets[i].valid) {
      return i;
    }
  }
  return -1;
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
  } else if (targetRpm > MAX_RPM) {
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

void drawWindingScreen() {
  lcd.setCursor(0, 0);
  printPadded("Winding...");
  lcd.setCursor(0, 1);
  char line[21];
  snprintf(line, sizeof(line), "Turns: %ld/%ld", currentSteps / STEPS_PER_REV, targetTurns);
  printPadded(line);
  lcd.setCursor(0, 2);
  snprintf(line, sizeof(line), "RPM: %d", currentRpm);
  printPadded(line);
  lcd.setCursor(0, 3);
  printPadded("Press: pause");
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
  lcd.setCursor(0, 1);
  char line[21];
  snprintf(line, sizeof(line), "Turns: %ld/%ld", currentSteps / STEPS_PER_REV, targetTurns);
  printPadded(line);
  lcd.setCursor(0, 2);
  snprintf(line, sizeof(line), "RPM: %d", currentRpm);
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

void updateStepInterval() {
  if (currentRpm <= 0) {
    stepIntervalMicros = 0;
    return;
  }
  float stepsPerMinute = (float)currentRpm * STEPS_PER_REV;
  float stepsPerSecond = stepsPerMinute / 60.0f;
  stepIntervalMicros = (unsigned long)(1000000.0f / stepsPerSecond);
}

void rampSpeed() {
  if (currentRpm < targetRpm) {
    currentRpm += 5;
    if (currentRpm > targetRpm) {
      currentRpm = targetRpm;
    }
    updateStepInterval();
  }
}

void stepMotor() {
  if (stepIntervalMicros == 0) {
    return;
  }
  unsigned long now = micros();
  if (now - lastStepMicros >= stepIntervalMicros) {
    lastStepMicros = now;
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(2);
    digitalWrite(STEP_PIN, LOW);
    currentSteps += 1;
  }
}

void handleEncoder() {
  int encA = digitalRead(ENC_A);
  int encB = digitalRead(ENC_B);
  if (encA != lastEncA && encA == HIGH) {
    encoderDelta += (encB == LOW) ? 1 : -1;
  }
  lastEncA = encA;
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
  loadPresets();
  syncDigitsFromTargets();
  blinkTickMs = millis();
  drawManualScreen();
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
  handleEncoder();
  ButtonEvent buttonEvent = readButton();
  unsigned long nowMs = millis();
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
    if (encoderDelta != 0) {
      if (manualField == 0) {
        turnsDigits[manualDigitIndex] = wrapDigit(turnsDigits[manualDigitIndex], encoderDelta);
        targetTurns = digitsToValue(turnsDigits, TURN_DIGITS);
      } else if (manualField == 1) {
        rpmDigits[manualDigitIndex] = wrapDigit(rpmDigits[manualDigitIndex], encoderDelta);
        targetRpm = digitsToValue(rpmDigits, RPM_DIGITS);
      } else if (manualField == 2) {
        targetDirectionCW = encoderDelta > 0 ? true : false;
      }
      encoderDelta = 0;
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
        targetSteps = targetTurns * STEPS_PER_REV;
        enableDriver(false);
        setDirection(targetDirectionCW);
        updateStepInterval();
        countdownValue = 3;
        countdownTickMs = millis();
        windingUpdateMs = millis();
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
    if (encoderDelta != 0) {
      menuIndex = constrain(menuIndex + encoderDelta, 0, maxIndex);
      encoderDelta = 0;
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
    if (encoderDelta != 0) {
      if (presetField == 0) {
        turnsDigits[presetDigitIndex] = wrapDigit(turnsDigits[presetDigitIndex], encoderDelta);
        targetTurns = digitsToValue(turnsDigits, TURN_DIGITS);
      } else if (presetField == 1) {
        rpmDigits[presetDigitIndex] = wrapDigit(rpmDigits[presetDigitIndex], encoderDelta);
        targetRpm = digitsToValue(rpmDigits, RPM_DIGITS);
      } else if (presetField == 2) {
        targetDirectionCW = encoderDelta > 0 ? true : false;
      }
      encoderDelta = 0;
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
    if (encoderDelta != 0) {
      presetName[nameIndex] = nextNameChar(presetName[nameIndex], encoderDelta);
      encoderDelta = 0;
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
        slot = 0;
      }
      Preset preset;
      memset(&preset, 0, sizeof(preset));
      strncpy(preset.name, presetName, sizeof(preset.name) - 1);
      preset.turns = targetTurns;
      preset.rpm = targetRpm;
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
      targetRpm = presets[presetIndex].rpm;
      targetDirectionCW = presets[presetIndex].directionCW;
      syncDigitsFromTargets();

      screenMode = SCREEN_COUNTDOWN;
      currentSteps = 0;
      currentRpm = 0;
      windingPaused = false;
      targetSteps = targetTurns * STEPS_PER_REV;
      enableDriver(false);
      setDirection(targetDirectionCW);
      updateStepInterval();
      countdownValue = 3;
      countdownTickMs = millis();
      windingUpdateMs = millis();
      drawCountdownScreen();
    } else if (buttonEvent == BTN_LONG) {
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
        windingUpdateMs = nowMs;
        drawWindingScreen();
      } else {
        drawCountdownScreen();
      }
    }
  } else if (screenMode == SCREEN_WINDING) {
    blinkDirty = false;
    if (buttonEvent == BTN_LONG && windingPaused) {
      enableDriver(false);
      screenMode = SCREEN_MANUAL;
      manualField = 0;
      manualDigitIndex = 0;
      syncDigitsFromTargets();
      drawManualScreen();
      return;
    }
    if (buttonEvent == BTN_CLICK) {
      windingPaused = !windingPaused;
      if (windingPaused) {
        enableDriver(false);
        blinkOn = true;
        lcd.clear();
        drawPausedScreen();
      } else {
        screenMode = SCREEN_COUNTDOWN;
        countdownValue = 3;
        countdownTickMs = millis();
        drawCountdownScreen();
      }
      return;
    }
    if (windingPaused) {
      return;
    }
    rampSpeed();
    stepMotor();
    if (currentSteps >= targetSteps) {
      enableDriver(false);
      screenMode = SCREEN_DONE;
      drawDoneScreen();
    } else if (nowMs - windingUpdateMs >= 500) {
      windingUpdateMs = nowMs;
      drawWindingScreen();
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
