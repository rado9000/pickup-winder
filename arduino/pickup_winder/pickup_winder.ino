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
const int EN_PIN = 13; // active LOW for most drivers

// Winding settings
const int MAX_PRESETS = 8;
const long MAX_TURNS = 20000;
const int MIN_RPM = 10;
const int MAX_RPM = 1200;

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
int manualField = 0;
int menuIndex = 0;
int presetIndex = 0;
int presetListOffset = 0;
int presetField = 0;
int nameIndex = 0;
char presetName[12] = "PRESET";
int countdownValue = 3;
unsigned long countdownTickMs = 0;
bool windingPaused = false;

long targetTurns = 1000;
int targetRpm = 200;
bool targetDirectionCW = true;

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

const int STEPS_PER_REV = 200; // change if microstepping

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

void drawManualScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Manual mode");
  lcd.setCursor(0, 1);
  lcd.print(manualField == 0 ? ">" : " ");
  lcd.print("Turns:");
  lcd.print(targetTurns);
  lcd.setCursor(0, 2);
  lcd.print(manualField == 1 ? ">" : " ");
  lcd.print("RPM:");
  lcd.print(targetRpm);
  lcd.print(" ");
  lcd.print(manualField == 2 ? ">" : " ");
  lcd.print(targetDirectionCW ? "CW" : "CCW");
  lcd.setCursor(0, 3);
  if (manualField == 3) {
    lcd.print("> Start winding");
  } else {
    lcd.print("Hold: presets");
  }
}

void drawPresetListScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Presets");

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
      lcd.print(" ");
      continue;
    }
    lcd.print(menuIndex == itemIndex ? "> " : "  ");
    if (itemIndex == 0) {
      lcd.print("New preset");
    } else {
      int presetSlot = itemIndex - 1;
      if (presets[presetSlot].valid) {
        lcd.print(presets[presetSlot].name);
      } else {
        lcd.print("(empty)");
      }
    }
  }
  lcd.setCursor(0, 3);
  lcd.print("Hold: back");
}

void drawPresetNewScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("New preset");
  lcd.setCursor(0, 1);
  lcd.print(presetField == 0 ? "> Turns: " : "  Turns: ");
  lcd.print(targetTurns);
  lcd.setCursor(0, 2);
  lcd.print(presetField == 1 ? "> RPM:   " : "  RPM:   ");
  lcd.print(targetRpm);
  lcd.setCursor(0, 3);
  lcd.print(presetField == 2 ? "> Dir:  " : "  Dir:  ");
  lcd.print(targetDirectionCW ? "CW" : "CCW");
}

void drawPresetNameScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Preset name");
  lcd.setCursor(0, 1);
  lcd.print(presetName);
  lcd.setCursor(0, 2);
  lcd.print("Click: next");
  lcd.setCursor(0, 3);
  lcd.print("Hold: save");
}

void drawPresetViewScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(presets[presetIndex].name);
  lcd.setCursor(0, 1);
  lcd.print("Turns: ");
  lcd.print(presets[presetIndex].turns);
  lcd.setCursor(0, 2);
  lcd.print("RPM:   ");
  lcd.print(presets[presetIndex].rpm);
  lcd.setCursor(0, 3);
  lcd.print(presets[presetIndex].directionCW ? "Dir: CW" : "Dir: CCW");
}

void drawWindingScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Winding...");
  lcd.setCursor(0, 1);
  lcd.print("Turns: ");
  lcd.print(currentSteps / STEPS_PER_REV);
  lcd.print("/");
  lcd.print(targetTurns);
  lcd.setCursor(0, 2);
  lcd.print("RPM: ");
  lcd.print(currentRpm);
}

void drawCountdownScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Get ready");
  lcd.setCursor(0, 2);
  lcd.print("Starting in: ");
  lcd.print(countdownValue);
}

void drawPausedScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Paused");
  lcd.setCursor(0, 1);
  lcd.print("Turns: ");
  lcd.print(currentSteps / STEPS_PER_REV);
  lcd.print("/");
  lcd.print(targetTurns);
  lcd.setCursor(0, 2);
  lcd.print("RPM: ");
  lcd.print(currentRpm);
  lcd.setCursor(0, 3);
  lcd.print("Press to resume");
}

void drawDoneScreen() {
  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print("Winding complete");
  lcd.setCursor(0, 2);
  lcd.print("Press to return");
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
  drawManualScreen();
}

void loop() {
  handleEncoder();
  ButtonEvent buttonEvent = readButton();

  if (screenMode == SCREEN_MANUAL) {
    if (encoderDelta != 0) {
      if (manualField == 0) {
        targetTurns = constrain(targetTurns + encoderDelta * 10, 1, MAX_TURNS);
      } else if (manualField == 1) {
        targetRpm = constrain(targetRpm + encoderDelta * 10, MIN_RPM, MAX_RPM);
      } else if (manualField == 2) {
        targetDirectionCW = encoderDelta > 0 ? true : false;
      }
      encoderDelta = 0;
      drawManualScreen();
    }

    if (buttonEvent == BTN_CLICK) {
      if (manualField < 3) {
        manualField++;
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
        drawCountdownScreen();
      }
    } else if (buttonEvent == BTN_LONG) {
      screenMode = SCREEN_PRESET_LIST;
      menuIndex = 0;
      presetListOffset = 0;
      drawPresetListScreen();
    }
  } else if (screenMode == SCREEN_PRESET_LIST) {
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
      drawManualScreen();
    }
  } else if (screenMode == SCREEN_PRESET_NEW) {
    if (encoderDelta != 0) {
      if (presetField == 0) {
        targetTurns = constrain(targetTurns + encoderDelta * 10, 1, MAX_TURNS);
      } else if (presetField == 1) {
        targetRpm = constrain(targetRpm + encoderDelta * 10, MIN_RPM, MAX_RPM);
      } else if (presetField == 2) {
        targetDirectionCW = encoderDelta > 0 ? true : false;
      }
      encoderDelta = 0;
      drawPresetNewScreen();
    }

    if (buttonEvent == BTN_CLICK) {
      presetField++;
      if (presetField > 2) {
        screenMode = SCREEN_PRESET_NAME;
        memset(presetName, 0, sizeof(presetName));
        strncpy(presetName, "PRESET", sizeof(presetName) - 1);
        nameIndex = 0;
        drawPresetNameScreen();
      } else {
        drawPresetNewScreen();
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
    if (buttonEvent == BTN_CLICK) {
      targetTurns = presets[presetIndex].turns;
      targetRpm = presets[presetIndex].rpm;
      targetDirectionCW = presets[presetIndex].directionCW;

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
      drawCountdownScreen();
    } else if (buttonEvent == BTN_LONG) {
      screenMode = SCREEN_PRESET_LIST;
      drawPresetListScreen();
    }
  } else if (screenMode == SCREEN_COUNTDOWN) {
    if (buttonEvent == BTN_CLICK) {
      screenMode = SCREEN_WINDING;
      enableDriver(true);
      drawWindingScreen();
      return;
    }
    unsigned long nowMs = millis();
    if (nowMs - countdownTickMs >= 1000) {
      countdownTickMs = nowMs;
      countdownValue--;
      if (countdownValue < 0) {
        screenMode = SCREEN_WINDING;
        enableDriver(true);
        drawWindingScreen();
      } else {
        drawCountdownScreen();
      }
    }
  } else if (screenMode == SCREEN_WINDING) {
    if (buttonEvent == BTN_CLICK) {
      windingPaused = !windingPaused;
      if (windingPaused) {
        enableDriver(false);
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
    } else if (millis() % 250 == 0) {
      drawWindingScreen();
    }
  } else if (screenMode == SCREEN_DONE) {
    if (buttonEvent == BTN_CLICK) {
      screenMode = SCREEN_MANUAL;
      manualField = 0;
      drawManualScreen();
    }
  }
}
