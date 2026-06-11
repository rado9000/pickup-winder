#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#include "config.h"
#include "encoder.h"
#include "gauss_meter.h"
#include "motor.h"
#include "presets.h"
#include "rev_counter.h"
#include "servo42.h"

enum class AppMode {
  Manual,
  PresetsMenu,
  NewPresetTurns,
  NewPresetDir,
  NewPresetRpm,
  NewPresetName,
  PresetView,
  Countdown,
  Winding,
  WindingPaused,
  Complete,
  GaussMeasure,
};

static LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
static Servo42 servo42;
static RotaryEncoder encoder;
static WindingMotor motor;
static RevCounter revCounter;
static PresetStore presets;
static GaussMeter gaussMeter;

static AppMode mode = AppMode::Manual;
static uint32_t blinkPhaseMs = 0;

static uint32_t editTurns = 100;
static uint16_t editRpm = 300;
static WindingDir editDir = WindingDir::CW;

static int8_t digitField = 0;
static int8_t activeDigit = 0;

static uint8_t presetMenuIndex = 0;
static uint8_t selectedPreset = 0;
static char newPresetName[PRESET_NAME_LEN] = "Preset";
static int8_t nameCharIndex = 0;

static int countdown = 0;
static uint32_t countdownAtMs = 0;

static bool resumeAfterPause = false;
static int32_t turnsAtPause = 0;
static int32_t targetTurnsRun = 0;

static void lcdClearLine(uint8_t row) {
  lcd.setCursor(0, row);
  for (uint8_t c = 0; c < LCD_COLS; c++) {
    lcd.print(' ');
  }
}

static bool blinkOn() {
  return ((millis() - blinkPhaseMs) / BLINK_MS) % 2 == 0;
}

static void formatTurns(char* buf, size_t n, uint32_t v, int8_t dig, bool blink) {
  snprintf(buf, n, "%05lu", (unsigned long)v);
  if (blink && blinkOn() && dig >= 0 && dig < 5) {
    buf[4 - dig] = '_';
  }
}

static void formatRpm(char* buf, size_t n, uint16_t v, int8_t dig, bool blink) {
  snprintf(buf, n, "%04u", v);
  if (blink && blinkOn() && dig >= 0 && dig < 4) {
    buf[3 - dig] = '_';
  }
}

static void drawManual() {
  char t[8], r[8];
  bool blink = true;
  int8_t td = (digitField <= 4) ? activeDigit : -1;
  int8_t rd = (digitField >= 5 && digitField <= 8) ? (activeDigit - 5) : -1;
  formatTurns(t, sizeof t, editTurns, td, blink);
  formatRpm(r, sizeof r, editRpm, rd, blink);
  lcd.setCursor(0, 0);
  lcd.print("Turns:");
  lcd.print(t);
  lcd.setCursor(0, 1);
  lcd.print("RPM:");
  lcd.print(r);
  lcd.setCursor(0, 2);
  lcd.print("Dir:");
  lcd.print(editDir == WindingDir::CW ? "CW " : "CCW");
  if (digitField == 9 && blinkOn()) {
    lcd.print('*');
  } else {
    lcd.print(' ');
  }
  lcd.setCursor(0, 3);
  lcd.print("> Start winding   ");
}

static void drawPresetsMenu() {
  lcd.setCursor(0, 0);
  lcd.print("Presets           ");
  lcd.setCursor(0, 1);
  if (presetMenuIndex == 0) {
    lcd.print(blinkOn() ? "> New preset      " : "  New preset      ");
  } else {
    lcd.print("  New preset      ");
  }
  Preset p{};
  if (presetMenuIndex > 0 && presets.get(presetMenuIndex - 1, p)) {
    lcd.setCursor(0, 2);
    char line[21];
    snprintf(line, sizeof line, "%c %-16s", presetMenuIndex == 1 ? '>' : ' ', p.name);
    lcd.print(line);
  } else {
    lcdClearLine(2);
  }
  lcdClearLine(3);
}

static void drawPresetView(const Preset& p) {
  lcd.setCursor(0, 0);
  lcd.print(p.name);
  lcd.setCursor(0, 1);
  lcd.printf("T:%lu RPM:%u", (unsigned long)p.turns, p.rpm);
  lcd.setCursor(0, 2);
  lcd.print(p.dir == WindingDir::CW ? "Dir: CW" : "Dir:CCW");
  lcd.setCursor(0, 3);
  lcd.print("Click to start    ");
}

static void drawNewPreset(const char* label, const char* valueLine) {
  lcd.setCursor(0, 0);
  lcd.print(label);
  lcd.setCursor(0, 1);
  lcd.print(valueLine);
  lcdClearLine(2);
  lcdClearLine(3);
}

static void drawCountdown() {
  lcd.setCursor(0, 1);
  lcd.printf("      %d          ", countdown);
}

static void drawWinding(bool paused) {
  lcd.setCursor(0, 0);
  lcd.print(paused ? "PAUSED" : "Winding...");
  lcd.setCursor(0, 1);
  lcd.printf("Turn %ld/%ld", (long)revCounter.turns(), (long)targetTurnsRun);
  lcd.setCursor(0, 2);
  lcd.printf("RPM %u", motor.currentRpm());
  lcd.setCursor(0, 3);
  lcd.print(paused ? "Hold: menu" : "Click: pause");
}

static void drawComplete() {
  lcd.setCursor(0, 1);
  lcd.print(" Winding complete ");
  lcd.setCursor(0, 3);
  lcd.print("  Click to exit   ");
}

static void drawGauss() {
  lcd.setCursor(0, 0);
  lcd.print("Gauss meter       ");
  lcd.setCursor(0, 1);
  lcd.printf("%+.1f G           ", gaussMeter.gauss());
  lcd.setCursor(0, 3);
  lcd.print("Remove field=back ");
}

static void clampTurns() {
  if (editTurns < MIN_TURNS) {
    editTurns = MIN_TURNS;
  }
  if (editTurns > MAX_TURNS) {
    editTurns = MAX_TURNS;
  }
}

static void clampRpm() {
  if (editRpm < MIN_RPM) {
    editRpm = MIN_RPM;
  }
  if (editRpm > MAX_RPM_USER) {
    editRpm = MAX_RPM_USER;
  }
}

static void adjustDigit(uint32_t& value, int8_t dig, int delta, uint32_t maxVal) {
  uint32_t place = 1;
  for (int i = 0; i < dig; i++) {
    place *= 10;
  }
  int32_t digit = (value / place) % 10;
  digit += delta;
  if (digit > 9) {
    digit = 0;
  }
  if (digit < 0) {
    digit = 9;
  }
  value = (value / (place * 10)) * (place * 10) + (value % place) + (uint32_t)digit * place;
  if (value > maxVal) {
    value = maxVal;
  }
  if (value == 0 && maxVal >= 1) {
    value = 1;
  }
}

static void adjustDigitU16(uint16_t& value, int8_t dig, int delta, uint16_t maxVal) {
  uint32_t v = value;
  adjustDigit(v, dig, delta, maxVal);
  value = (uint16_t)v;
}

static void startCountdown(bool resume = false) {
  mode = AppMode::Countdown;
  resumeAfterPause = resume;
  countdown = COUNTDOWN_START;
  countdownAtMs = millis();
  lcd.clear();
}

static void beginWinding(uint32_t turns, uint16_t rpm, WindingDir dir) {
  targetTurnsRun = (int32_t)turns;
  editRpm = rpm;
  motor.setDirection(dir);
  revCounter.setMotorDirection(dir);
  revCounter.reset();
  revCounter.setTargetTurns(targetTurnsRun);
  motor.enable(true);
  motor.setTargetRpm(rpm);
  mode = AppMode::Winding;
}

static void stopMotorRamp() {
  motor.setTargetRpm(0);
}

static void advanceField() {
  if (digitField < 9) {
    digitField++;
    if (digitField <= 4) {
      activeDigit = digitField;
    } else if (digitField <= 8) {
      activeDigit = digitField;
    } else {
      activeDigit = 0;
    }
  } else {
    digitField = 0;
    activeDigit = 0;
  }
}

static void handleManualEncoder(int d, bool click, bool longPress) {
  if (longPress) {
    mode = AppMode::PresetsMenu;
    presetMenuIndex = 0;
    lcd.clear();
    return;
  }
  if (d != 0) {
    if (digitField <= 4) {
      adjustDigit(editTurns, activeDigit, d > 0 ? 1 : -1, MAX_TURNS);
      clampTurns();
    } else if (digitField <= 8) {
      adjustDigitU16(editRpm, activeDigit - 5, d > 0 ? 1 : -1, MAX_RPM_USER);
      clampRpm();
    } else {
      editDir = (d > 0) ? WindingDir::CW : WindingDir::CCW;
    }
  }
  if (click) {
    if (digitField == 9) {
      startCountdown(false);
    } else {
      advanceField();
    }
  }
}

static void handlePresets(int d, bool click, bool longPress) {
  if (longPress) {
    mode = AppMode::Manual;
    lcd.clear();
    return;
  }
  uint8_t items = presets.count() + 1;
  if (d != 0) {
    if (d > 0) {
      presetMenuIndex = (presetMenuIndex + 1) % items;
    } else {
      presetMenuIndex = (presetMenuIndex + items - 1) % items;
    }
  }
  if (click) {
    if (presetMenuIndex == 0) {
      mode = AppMode::NewPresetTurns;
      digitField = 0;
      activeDigit = 0;
      editTurns = 100;
      lcd.clear();
    } else {
      selectedPreset = presetMenuIndex - 1;
      Preset p{};
      if (presets.get(selectedPreset, p)) {
        editTurns = p.turns;
        editRpm = p.rpm;
        editDir = p.dir;
        mode = AppMode::PresetView;
        lcd.clear();
      }
    }
  }
}

static void handleNewPreset(int d, bool click, bool longPress) {
  if (longPress && mode == AppMode::NewPresetName) {
    Preset p{};
    strncpy(p.name, newPresetName, PRESET_NAME_LEN - 1);
    p.turns = editTurns;
    p.rpm = editRpm;
    p.dir = editDir;
    presets.save(p);
    mode = AppMode::PresetsMenu;
    lcd.clear();
    return;
  }
  if (longPress) {
    mode = AppMode::PresetsMenu;
    lcd.clear();
    return;
  }
  if (mode == AppMode::NewPresetTurns) {
    if (d != 0) {
      adjustDigit(editTurns, activeDigit, d > 0 ? 1 : -1, MAX_TURNS);
      clampTurns();
    }
    if (click) {
      mode = AppMode::NewPresetDir;
      digitField = 9;
    }
  } else if (mode == AppMode::NewPresetDir) {
    if (d != 0) {
      editDir = (d > 0) ? WindingDir::CW : WindingDir::CCW;
    }
    if (click) {
      mode = AppMode::NewPresetRpm;
      activeDigit = 0;
      digitField = 5;
    }
  } else if (mode == AppMode::NewPresetRpm) {
    if (d != 0) {
      adjustDigitU16(editRpm, activeDigit - 5, d > 0 ? 1 : -1, MAX_RPM_USER);
      clampRpm();
    }
    if (click) {
      mode = AppMode::NewPresetName;
      nameCharIndex = 0;
    }
  } else if (mode == AppMode::NewPresetName) {
    if (d != 0) {
      char c = newPresetName[nameCharIndex];
      if (c == 0) {
        c = 'A';
      }
      c += (d > 0) ? 1 : -1;
      if (c < ' ') {
        c = '~';
      }
      if (c > '~') {
        c = ' ';
      }
      newPresetName[nameCharIndex] = c;
    }
    if (click) {
      if (newPresetName[nameCharIndex] == 0) {
        newPresetName[nameCharIndex] = 'A';
      }
      nameCharIndex++;
      if (nameCharIndex >= PRESET_NAME_LEN - 1) {
        nameCharIndex = PRESET_NAME_LEN - 2;
      }
      newPresetName[nameCharIndex] = 0;
    }
  }
}

static void handleWinding(int d, bool click, bool longPress) {
  (void)d;
  if (mode == AppMode::WindingPaused && longPress) {
    stopMotorRamp();
    motor.enable(false);
    mode = AppMode::Manual;
    lcd.clear();
    return;
  }
  if (click && mode == AppMode::Winding) {
    stopMotorRamp();
    motor.waitUntilStopped();
    motor.enable(false);
    turnsAtPause = revCounter.turns();
    mode = AppMode::WindingPaused;
    lcd.clear();
    return;
  }
  if (click && mode == AppMode::WindingPaused) {
    startCountdown(true);
    return;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("Pickup winder");
  lcd.setCursor(0, 1);
  lcd.print("ESP32-S3 / SERVO42");

  servo42.begin();
#if SERVO42_INIT_ON_BOOT
  servo42.readStatus();
  delay(300);
  servo42.setModeBusClosedLoop();
  delay(500);
  servo42.saveSettings();
  delay(500);
#endif

  motor.begin(&servo42);
  encoder.begin(ENCODER_CLK_PIN, ENCODER_DT_PIN, ENCODER_SW_PIN);
  encoder.setLongPressMs(LONG_PRESS_MS);
  revCounter.begin(&servo42);
  presets.begin();
  gaussMeter.begin(GAUSS_ADC_PIN);

  digitField = 0;
  activeDigit = 0;
  delay(800);
  lcd.clear();
}

void loop() {
  encoder.update();
  int d = encoder.delta();
  bool click = encoder.clicked();
  bool longPress = encoder.longPressed();

  gaussMeter.update();
  if (gaussMeter.active() && mode != AppMode::GaussMeasure && mode != AppMode::Winding &&
      mode != AppMode::Countdown) {
    mode = AppMode::GaussMeasure;
    lcd.clear();
  }
  if (mode == AppMode::GaussMeasure && !gaussMeter.active()) {
    mode = AppMode::Manual;
    lcd.clear();
  }

  motor.tick();
  revCounter.setMotorDirection(motor.direction());
  revCounter.poll();

  switch (mode) {
    case AppMode::Manual:
      handleManualEncoder(d, click, longPress);
      drawManual();
      break;
    case AppMode::PresetsMenu:
      handlePresets(d, click, longPress);
      drawPresetsMenu();
      break;
    case AppMode::NewPresetTurns:
    case AppMode::NewPresetDir:
    case AppMode::NewPresetRpm:
    case AppMode::NewPresetName:
      handleNewPreset(d, click, longPress);
      if (mode == AppMode::NewPresetTurns) {
        char t[8];
        formatTurns(t, sizeof t, editTurns, activeDigit, true);
        drawNewPreset("New: turns", t);
      } else if (mode == AppMode::NewPresetDir) {
        drawNewPreset("New: direction", editDir == WindingDir::CW ? "CW" : "CCW");
      } else if (mode == AppMode::NewPresetRpm) {
        char r[8];
        formatRpm(r, sizeof r, editRpm, activeDigit - 5, true);
        drawNewPreset("New: RPM", r);
      } else {
        char line[21];
        snprintf(line, sizeof line, "Name: %s", newPresetName);
        if (blinkOn()) {
          size_t pos = 6 + (size_t)nameCharIndex;
          if (pos < sizeof line - 1) {
            line[pos] = '_';
          }
        }
        drawNewPreset("New: name", line);
      }
      break;
    case AppMode::PresetView: {
      Preset p{};
      if (presets.get(selectedPreset, p)) {
        drawPresetView(p);
        if (click) {
          editTurns = p.turns;
          editRpm = p.rpm;
          editDir = p.dir;
          startCountdown(false);
        }
      }
      break;
    }
    case AppMode::Countdown:
      if (millis() - countdownAtMs >= 1000) {
        countdownAtMs = millis();
        countdown--;
        if (countdown < 0) {
          if (resumeAfterPause) {
            motor.setDirection(editDir);
            revCounter.setMotorDirection(editDir);
            motor.enable(true);
            motor.setTargetRpm(editRpm);
            mode = AppMode::Winding;
            resumeAfterPause = false;
          } else {
            beginWinding(editTurns, editRpm, editDir);
          }
          lcd.clear();
        }
      }
      drawCountdown();
      break;
    case AppMode::Winding:
    case AppMode::WindingPaused:
      handleWinding(d, click, longPress);
      if (mode == AppMode::Winding && revCounter.targetReached()) {
        stopMotorRamp();
        motor.waitUntilStopped();
        motor.enable(false);
        mode = AppMode::Complete;
        lcd.clear();
      }
      drawWinding(mode == AppMode::WindingPaused);
      break;
    case AppMode::Complete:
      drawComplete();
      if (click) {
        mode = AppMode::Manual;
        lcd.clear();
      }
      break;
    case AppMode::GaussMeasure:
      drawGauss();
      break;
  }
}
