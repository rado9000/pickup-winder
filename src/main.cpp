#include <Arduino.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#include "a3144_counter.h"
#include "config.h"
#include "gauss_monitor.h"
#include "motor_driver.h"
#include "presets_store.h"
#include "tmc2209_driver.h"

LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLS, LCD_ROWS);

enum ScreenMode {
  SCREEN_MANUAL,
  SCREEN_PRESET_LIST,
  SCREEN_PRESET_NEW,
  SCREEN_PRESET_NAME,
  SCREEN_PRESET_VIEW,
  SCREEN_PRESET_FULL,
  SCREEN_PREWIND,
  SCREEN_GAUSS = 90,
  SCREEN_COUNTDOWN,
  SCREEN_WINDING,
  SCREEN_DONE
};

enum ButtonEvent : uint8_t { BTN_NONE, BTN_CLICK, BTN_LONG };

static ScreenMode screenMode = SCREEN_MANUAL;
static ScreenMode lastScreenMode = SCREEN_MANUAL;
static int manualField = 0;
static int menuIndex = 0;
static int presetIndex = 0;
static int presetListOffset = 0;
static int presetField = 0;
static int nameIndex = 0;
static int manualDigitIndex = 0;
static int presetDigitIndex = 0;
static char presetName[PRESET_NAME_LEN] = "PRESET";
static int countdownValue = 3;
static unsigned long countdownTickMs = 0;
static bool windingPaused = false;
static bool blinkOn = true;
static unsigned long blinkTickMs = 0;
static bool blinkDirty = false;
static unsigned long windingUpdateMs = 0;

static long targetTurns = 1000;
static int targetRpm = 300;
static bool targetDirectionCW = true;
static int turnsDigits[TURN_DIGITS];
static int rpmDigits[RPM_DIGITS];

static volatile int encoderDelta = 0;
static volatile uint8_t encoderState = 0;
static int encoderAccum = 0;
static unsigned long buttonDownMs = 0;
static bool buttonWasDown = false;
static long targetSteps = 0;
static float prewindStepCarry = 0.0f;
static long prewindStepsQueued = 0;
static uint32_t prewindLastStepUs = 0;

static ButtonEvent readButton() {
  bool pressed = digitalRead(ENC_BTN_PIN) == LOW;
  if (pressed && !buttonWasDown) {
    buttonDownMs = millis();
    buttonWasDown = true;
  }
  if (!pressed && buttonWasDown) {
    unsigned long held = millis() - buttonDownMs;
    buttonWasDown = false;
    if (held > LONG_PRESS_MS) {
      return BTN_LONG;
    }
    return BTN_CLICK;
  }
  return BTN_NONE;
}

static void handleEncoderInterrupt() {
  uint8_t state = (digitalRead(ENC_A_PIN) << 1) | digitalRead(ENC_B_PIN);
  uint8_t combined = (encoderState << 2) | state;
  static const int8_t table[16] = {0, -1, 1, 0, 1, 0, 0, -1,
                                   -1, 0, 0, 1, 0, 1, -1, 0};
  encoderDelta += table[combined];
  encoderState = state;
}

static int readEncoderDetent() {
  int delta = 0;
  noInterrupts();
  delta = encoderDelta;
  encoderDelta = 0;
  interrupts();
  if (delta == 0) {
    return 0;
  }
  encoderAccum += delta;
  if (encoderAccum >= 4) {
    encoderAccum = 0;
    return 1;
  }
  if (encoderAccum <= -4) {
    encoderAccum = 0;
    return -1;
  }
  return 0;
}

static void printPadded(const char *text) {
  lcd.print(text);
  int len = strlen(text);
  for (int i = len; i < 20; i++) {
    lcd.print(' ');
  }
}

static void valueToDigits(long value, int *digits, int count) {
  for (int i = count - 1; i >= 0; i--) {
    digits[i] = value % 10;
    value /= 10;
  }
}

static long digitsToValue(const int *digits, int count) {
  long value = 0;
  for (int i = 0; i < count; i++) {
    value = value * 10 + digits[i];
  }
  return value;
}

static int wrapDigit(int digit, int delta) {
  int v = (digit + delta) % 10;
  if (v < 0) {
    v += 10;
  }
  return v;
}

static void syncDigitsFromTargets() {
  valueToDigits(targetTurns, turnsDigits, TURN_DIGITS);
  valueToDigits(targetRpm, rpmDigits, RPM_DIGITS);
}

static void clampTargets() {
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

static void printDigitsLine(const char *label, const int *digits, int count, bool selected, int activeIndex) {
  char line[21];
  int offset = snprintf(line, sizeof(line), "%s%s", selected ? ">" : " ", label);
  for (int i = 0; i < count && offset + i < 20; i++) {
    if (selected && i == activeIndex && !blinkOn) {
      line[offset + i] = ' ';
    } else {
      line[offset + i] = (char)('0' + digits[i]);
    }
  }
  for (int i = offset + count; i < 20; i++) {
    line[i] = ' ';
  }
  line[20] = '\0';
  printPadded(line);
}

static void drawManualHeader() {
  lcd.setCursor(0, 0);
  printPadded("Manual mode");
}

static void drawManualTurnsLine() {
  lcd.setCursor(0, 1);
  printDigitsLine("Turns:", turnsDigits, TURN_DIGITS, manualField == 0,
                  manualField == 0 ? manualDigitIndex : -1);
}

static void drawManualRpmLine() {
  lcd.setCursor(0, 2);
  printDigitsLine("RPM:", rpmDigits, RPM_DIGITS, manualField == 1,
                  manualField == 1 ? manualDigitIndex : -1);
}

static void drawManualActionLine() {
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

static void drawManualScreen() {
  lcd.clear();
  drawManualHeader();
  drawManualTurnsLine();
  drawManualRpmLine();
  drawManualActionLine();
}

static void drawPresetListScreen() {
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
      int slot = itemIndex - 1;
      snprintf(line + offset, sizeof(line) - offset, "%s",
               presets[slot].valid ? presets[slot].name : "(empty)");
    }
    printPadded(line);
  }
  lcd.setCursor(0, 3);
  printPadded("Hold: back");
}

static void drawPresetNewScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  printPadded("New preset");
  lcd.setCursor(0, 1);
  printDigitsLine("Turns:", turnsDigits, TURN_DIGITS, presetField == 0,
                  presetField == 0 ? presetDigitIndex : -1);
  lcd.setCursor(0, 2);
  printDigitsLine("RPM:", rpmDigits, RPM_DIGITS, presetField == 1,
                  presetField == 1 ? presetDigitIndex : -1);
  lcd.setCursor(0, 3);
  char line[21];
  const char *dirText = targetDirectionCW ? "CW" : "CCW";
  snprintf(line, sizeof(line), "%s Dir:%s", presetField == 2 ? ">" : " ",
           (presetField == 2 && !blinkOn) ? "  " : dirText);
  printPadded(line);
}

static void drawPresetTurnsLine() {
  lcd.setCursor(0, 1);
  printDigitsLine("Turns:", turnsDigits, TURN_DIGITS, presetField == 0,
                  presetField == 0 ? presetDigitIndex : -1);
}

static void drawPresetRpmLine() {
  lcd.setCursor(0, 2);
  printDigitsLine("RPM:", rpmDigits, RPM_DIGITS, presetField == 1,
                  presetField == 1 ? presetDigitIndex : -1);
}

static void drawPresetDirLine() {
  lcd.setCursor(0, 3);
  char line[21];
  const char *dirText = targetDirectionCW ? "CW" : "CCW";
  snprintf(line, sizeof(line), "%s Dir:%s", presetField == 2 ? ">" : " ",
           (presetField == 2 && !blinkOn) ? "  " : dirText);
  printPadded(line);
}

static void drawPresetNameScreen() {
  char displayName[PRESET_NAME_LEN];
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

static void drawPresetNameLine() {
  char displayName[PRESET_NAME_LEN];
  strncpy(displayName, presetName, sizeof(displayName));
  if (!blinkOn && nameIndex >= 0 && nameIndex < (int)sizeof(displayName) - 1) {
    displayName[nameIndex] = ' ';
  }
  lcd.setCursor(0, 1);
  printPadded(displayName);
}

static void drawPresetViewScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  printPadded(presets[presetIndex].name);
  char line[21];
  lcd.setCursor(0, 1);
  snprintf(line, sizeof(line), "Turns: %ld", presets[presetIndex].turns);
  printPadded(line);
  lcd.setCursor(0, 2);
  snprintf(line, sizeof(line), "RPM: %d", presets[presetIndex].rpm);
  printPadded(line);
  lcd.setCursor(0, 3);
  snprintf(line, sizeof(line), "Dir: %s", presets[presetIndex].directionCW ? "CW" : "CCW");
  printPadded(line);
}

static void drawGaussScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  printPadded("Magnet gauss meter");
  lcd.setCursor(0, 2);
  printPadded("Auto-hide <50G");
}

static void drawGaussValues() {
  lcd.setCursor(0, 1);
  char line[21];
  float g = gaussValue();
  float a = fabsf(g);
  const char *pole = "CENTER";
  if (g >= GAUSS_ENTER_THRESHOLD) {
    pole = "N";
  } else if (g <= -GAUSS_ENTER_THRESHOLD) {
    pole = "S";
  }
  snprintf(line, sizeof(line), "G:%7.1f  Pole:%s", a, pole);
  printPadded(line);
}

static void drawPrewindScreen() {
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

static void drawPrewindProgressLine() {
  lcd.setCursor(0, 1);
  char line[21];
  long t = (long)a3144Turns();
  if (t < 0) {
    t = 0;
  }
  long pct = targetTurns > 0 ? (t * 100L) / targetTurns : 0;
  snprintf(line, sizeof(line), "Turns:%5ld %3ld%%", t, pct);
  printPadded(line);
}

static void drawWindingScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  printPadded("Pickup winding");
  lcd.setCursor(0, 2);
  printPadded("Press to pause");
}

static void drawWindingProgressLine() {
  lcd.setCursor(0, 1);
  char line[21];
  long t = (long)a3144Turns();
  if (t < 0) {
    t = 0;
  }
  long pct = targetTurns > 0 ? (t * 100L) / targetTurns : 0;
  snprintf(line, sizeof(line), "Turns:%5ld %3ld%%", t, pct);
  printPadded(line);
}

static void drawCountdownScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  printPadded("Get ready");
  lcd.setCursor(0, 2);
  char line[21];
  snprintf(line, sizeof(line), "Starting in: %d", countdownValue);
  printPadded(line);
}

static void drawPausedScreen() {
  lcd.setCursor(0, 0);
  printPadded("Paused (hold menu)");
  long t = (long)a3144Turns();
  if (t < 0) {
    t = 0;
  }
  lcd.setCursor(0, 1);
  char line[21];
  snprintf(line, sizeof(line), "Turns: %ld/%ld", t, targetTurns);
  printPadded(line);
  lcd.setCursor(0, 3);
  printPadded("Press: resume");
}

static void drawDoneScreen() {
  lcd.clear();
  lcd.setCursor(0, 1);
  printPadded("Winding complete");
  lcd.setCursor(0, 2);
  printPadded("Press to return");
}

static void redrawForBlink() {
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

static char nextNameChar(char current, int delta) {
  const char charset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  const int n = sizeof(charset) - 1;
  int index = 0;
  for (int i = 0; i < n; i++) {
    if (charset[i] == current) {
      index = i;
      break;
    }
  }
  index = (index + delta) % n;
  if (index < 0) {
    index += n;
  }
  return charset[index];
}

static void enterPrewind() {
  screenMode = SCREEN_PREWIND;
  windingPaused = false;
  a3144Reset();
  prewindStepsQueued = 0;
  prewindStepCarry = 0.0f;
  prewindLastStepUs = micros();
  targetSteps = targetTurns * COUNT_STEPS_PER_REV;
  motorEnable(true);
  motorSetDirection(targetDirectionCW);
  a3144SetTargetDirection(targetDirectionCW);
  motorStopImmediate();
  windingUpdateMs = millis();
  drawPrewindScreen();
}

static void startCountdownFromPrewind() {
  prewindStepsQueued = 0;
  prewindStepCarry = 0.0f;
  motorStopImmediate();
  screenMode = SCREEN_COUNTDOWN;
  countdownValue = 3;
  countdownTickMs = millis();
  drawCountdownScreen();
}

static void startWindingNow() {
  clampTargets();
  screenMode = SCREEN_WINDING;
  windingPaused = false;
  motorEnable(true);
  lcd.clear();
  motorSetDirection(targetDirectionCW);
  a3144SetTargetDirection(targetDirectionCW);
  a3144OnMotorDirection(targetDirectionCW);
  motorStartWinding(0, targetRpm, true);
  windingUpdateMs = millis();
  drawWindingScreen();
}

void setup() {
  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);
  pinMode(ENC_BTN_PIN, INPUT_PULLUP);

  gaussBegin();
  (void)gaussBootSetup();

  Wire.setSDA(I2C_SDA_PIN);
  Wire.setSCL(I2C_SCL_PIN);
  Wire.begin();
  Wire.setClock(100000);
  Wire.setTimeout(10);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  printPadded("Pickup winder");
  lcd.setCursor(0, 1);
  printPadded("Manual mode");

  presetsBegin();
  presetsLoad();
  a3144Begin();
  motorDriverBegin();
  motorEnable(false);

#if USE_TMC2209_UART
  lcd.setCursor(0, 3);
  if (tmc2209Ready()) {
    printPadded("TMC2209 UART OK");
  } else {
    printPadded("TMC UART: check");
  }
  delay(800);
#endif

  targetTurns = 1000;
  targetRpm = 300;
  syncDigitsFromTargets();
  blinkTickMs = millis();

  encoderState = (digitalRead(ENC_A_PIN) << 1) | digitalRead(ENC_B_PIN);
  attachInterrupt(digitalPinToInterrupt(ENC_A_PIN), handleEncoderInterrupt, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B_PIN), handleEncoderInterrupt, CHANGE);

  delay(200);
  drawManualScreen();
}

void loop() {
  ButtonEvent buttonEvent = readButton();
  unsigned long nowMs = millis();
  int delta = readEncoderDetent();

  bool allowBlink = (screenMode == SCREEN_MANUAL && manualField <= 2) ||
                    screenMode == SCREEN_PRESET_NEW || screenMode == SCREEN_PRESET_NAME;
  if (allowBlink && nowMs - blinkTickMs >= BLINK_MS) {
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

#if USE_GAUSS_MONITOR
  ScreenMode beforeGauss = screenMode;
  int sm = (int)screenMode;
  gaussUpdate(nowMs, sm, sm);
  screenMode = (ScreenMode)sm;
  if (beforeGauss == SCREEN_GAUSS && screenMode != SCREEN_GAUSS) {
    if (screenMode == SCREEN_MANUAL) {
      drawManualScreen();
    } else if (screenMode == SCREEN_PRESET_LIST) {
      drawPresetListScreen();
    } else {
      drawManualScreen();
      screenMode = SCREEN_MANUAL;
    }
  }
  if (screenMode == SCREEN_GAUSS) {
    if (lastScreenMode != SCREEN_GAUSS) {
      drawGaussScreen();
      drawGaussValues();
    } else if (nowMs - windingUpdateMs >= WINDING_UI_INTERVAL_MS) {
      windingUpdateMs = nowMs;
      drawGaussValues();
    }
    if (buttonEvent == BTN_LONG) {
      gaussCalibrateZero();
      screenMode = (ScreenMode)gaussReturnScreenMode();
      if (screenMode == SCREEN_MANUAL) {
        drawManualScreen();
      } else if (screenMode == SCREEN_PRESET_LIST) {
        drawPresetListScreen();
      } else {
        drawManualScreen();
        screenMode = SCREEN_MANUAL;
      }
    }
    return;
  }
#endif

  switch (screenMode) {
    case SCREEN_MANUAL:
      if (delta != 0) {
        if (manualField == 0) {
          turnsDigits[manualDigitIndex] = wrapDigit(turnsDigits[manualDigitIndex], delta);
          targetTurns = digitsToValue(turnsDigits, TURN_DIGITS);
          drawManualTurnsLine();
        } else if (manualField == 1) {
          rpmDigits[manualDigitIndex] = wrapDigit(rpmDigits[manualDigitIndex], delta);
          targetRpm = digitsToValue(rpmDigits, RPM_DIGITS);
          drawManualRpmLine();
        } else if (manualField == 2) {
          targetDirectionCW = delta > 0;
          drawManualActionLine();
        }
      }
      if (buttonEvent == BTN_CLICK) {
        if (manualField == 0) {
          manualDigitIndex++;
          if (manualDigitIndex >= TURN_DIGITS) {
            manualDigitIndex = 0;
            manualField = 1;
          }
          clampTargets();
          drawManualScreen();
        } else if (manualField == 1) {
          manualDigitIndex++;
          if (manualDigitIndex >= RPM_DIGITS) {
            manualDigitIndex = 0;
            manualField = 2;
          }
          clampTargets();
          drawManualScreen();
        } else if (manualField == 2) {
          manualField = 3;
          drawManualScreen();
        } else {
          enterPrewind();
        }
      } else if (buttonEvent == BTN_LONG) {
        screenMode = SCREEN_PRESET_LIST;
        menuIndex = 0;
        presetListOffset = 0;
        drawPresetListScreen();
      }
      break;

    case SCREEN_PRESET_LIST:
      if (delta != 0) {
        menuIndex = constrain(menuIndex + delta, 0, MAX_PRESETS);
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
      break;

    case SCREEN_PRESET_NEW:
      if (delta != 0) {
        if (presetField == 0) {
          turnsDigits[presetDigitIndex] = wrapDigit(turnsDigits[presetDigitIndex], delta);
          targetTurns = digitsToValue(turnsDigits, TURN_DIGITS);
          drawPresetTurnsLine();
        } else if (presetField == 1) {
          rpmDigits[presetDigitIndex] = wrapDigit(rpmDigits[presetDigitIndex], delta);
          targetRpm = digitsToValue(rpmDigits, RPM_DIGITS);
          drawPresetRpmLine();
        } else {
          targetDirectionCW = delta > 0;
          drawPresetDirLine();
        }
      }
      if (buttonEvent == BTN_CLICK) {
        if (presetField == 0) {
          presetDigitIndex++;
          if (presetDigitIndex >= TURN_DIGITS) {
            presetDigitIndex = 0;
            presetField = 1;
          }
          clampTargets();
          drawPresetNewScreen();
        } else if (presetField == 1) {
          presetDigitIndex++;
          if (presetDigitIndex >= RPM_DIGITS) {
            presetDigitIndex = 0;
            presetField = 2;
          }
          clampTargets();
          drawPresetNewScreen();
        } else {
          screenMode = SCREEN_PRESET_NAME;
          strncpy(presetName, "PRESET", sizeof(presetName) - 1);
          nameIndex = 0;
          drawPresetNameScreen();
        }
      } else if (buttonEvent == BTN_LONG) {
        screenMode = SCREEN_PRESET_LIST;
        drawPresetListScreen();
      }
      break;

    case SCREEN_PRESET_NAME:
      if (delta != 0) {
        presetName[nameIndex] = nextNameChar(presetName[nameIndex], delta);
        drawPresetNameLine();
      }
      if (buttonEvent == BTN_CLICK) {
        nameIndex++;
        if (nameIndex >= PRESET_NAME_LEN - 1) {
          nameIndex = 0;
        }
        drawPresetNameLine();
      } else if (buttonEvent == BTN_LONG) {
        int slot = presetsFindEmptySlot();
        if (slot < 0) {
          screenMode = SCREEN_PRESET_FULL;
          lcd.clear();
          lcd.setCursor(0, 0);
          printPadded("Memory full");
        } else {
          Preset p{};
          strncpy(p.name, presetName, sizeof(p.name) - 1);
          p.turns = targetTurns;
          p.rpm = targetRpm > MAX_RPM_USER ? MAX_RPM_USER : targetRpm;
          p.directionCW = targetDirectionCW;
          p.valid = true;
          presetsSave(slot, p);
          screenMode = SCREEN_PRESET_LIST;
          drawPresetListScreen();
        }
      }
      break;

    case SCREEN_PRESET_VIEW:
      if (buttonEvent == BTN_CLICK) {
        targetTurns = presets[presetIndex].turns;
        targetRpm = presets[presetIndex].rpm > MAX_RPM_USER ? MAX_RPM_USER : presets[presetIndex].rpm;
        targetDirectionCW = presets[presetIndex].directionCW;
        syncDigitsFromTargets();
        enterPrewind();
      } else if (buttonEvent == BTN_LONG) {
        presetsDelete(presetIndex);
        screenMode = SCREEN_PRESET_LIST;
        drawPresetListScreen();
      }
      break;

    case SCREEN_PREWIND:
      if (buttonEvent == BTN_CLICK) {
        startCountdownFromPrewind();
        break;
      }
      if (buttonEvent == BTN_LONG) {
        motorStopImmediate();
        motorEnable(false);
        screenMode = SCREEN_MANUAL;
        drawManualScreen();
        break;
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
          motorSingleStep(stepCw);
          a3144OnMotorDirection(stepCw);
          prewindStepsQueued += (prewindStepsQueued > 0) ? -1 : 1;
        }
      }
      if (nowMs - windingUpdateMs >= WINDING_UI_INTERVAL_MS) {
        windingUpdateMs = nowMs;
        drawPrewindProgressLine();
      }
      break;

    case SCREEN_COUNTDOWN:
      if (buttonEvent == BTN_CLICK) {
        startWindingNow();
        break;
      }
      if (nowMs - countdownTickMs >= 1000) {
        countdownTickMs = nowMs;
        countdownValue--;
        if (countdownValue < 0) {
          startWindingNow();
        } else {
          drawCountdownScreen();
        }
      }
      break;

    case SCREEN_WINDING:
      motorUpdate(nowMs);
      a3144OnMotorDirection(motorDirectionCW());

      if (windingPaused) {
        if (buttonEvent == BTN_CLICK) {
          windingPaused = false;
          screenMode = SCREEN_COUNTDOWN;
          countdownValue = 3;
          countdownTickMs = millis();
          drawCountdownScreen();
        } else if (buttonEvent == BTN_LONG) {
          windingPaused = false;
          motorStopImmediate();
          motorEnable(false);
          screenMode = SCREEN_MANUAL;
          manualField = 0;
          manualDigitIndex = 0;
          syncDigitsFromTargets();
          drawManualScreen();
        }
        break;
      }

      if (buttonEvent == BTN_CLICK) {
        motorStopImmediate();
        motorEnable(false);
        windingPaused = true;
        lcd.clear();
        drawPausedScreen();
        break;
      }
      if (buttonEvent == BTN_LONG) {
        motorStopImmediate();
        motorEnable(false);
        screenMode = SCREEN_MANUAL;
        manualField = 0;
        manualDigitIndex = 0;
        syncDigitsFromTargets();
        drawManualScreen();
        break;
      }

      if (a3144Turns() >= (float)targetTurns) {
        motorStopImmediate();
        motorEnable(false);
        screenMode = SCREEN_DONE;
        drawDoneScreen();
        break;
      }
      if (nowMs - windingUpdateMs >= WINDING_UI_INTERVAL_MS) {
        windingUpdateMs = nowMs;
        drawWindingProgressLine();
      }
      break;

    case SCREEN_DONE:
      if (buttonEvent == BTN_CLICK) {
        screenMode = SCREEN_MANUAL;
        manualField = 0;
        manualDigitIndex = 0;
        syncDigitsFromTargets();
        drawManualScreen();
      }
      break;

    default:
      break;
  }
}
