#include "ui.h"
#include "config.h"

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);

static void pad20(char* dst, const char* src) {
  size_t i = 0;
  for (; i < 20 && src[i]; i++) {
    dst[i] = src[i];
  }
  for (; i < 20; i++) {
    dst[i] = ' ';
  }
  dst[20] = 0;
}

void Ui::begin() {
  Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
  lcd.init();
  lcd.backlight();
  clear();
}

void Ui::clear() {
  lcd.clear();
  for (int r = 0; r < 4; r++) {
    memset(lines_[r], ' ', 20);
    lines_[r][20] = 0;
    dirty_[r] = false;
  }
}

void Ui::mark(uint8_t row) {
  if (row < 4) {
    dirty_[row] = true;
  }
}

void Ui::flush() {
  for (uint8_t r = 0; r < 4; r++) {
    if (!dirty_[r]) {
      continue;
    }
    lcd.setCursor(0, r);
    lcd.print(lines_[r]);
    dirty_[r] = false;
  }
}

void Ui::setLine(uint8_t row, const char* text) {
  if (row >= 4) {
    return;
  }
  char tmp[21];
  pad20(tmp, text ? text : "");
  if (memcmp(lines_[row], tmp, 20) != 0) {
    memcpy(lines_[row], tmp, 21);
    mark(row);
  }
  flush();
}

void Ui::setLinef(uint8_t row, const char* fmt, ...) {
  char buf[32];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  setLine(row, buf);
}

void Ui::drawBootProgress(uint8_t percent) {
  setLine(0, tr(Language::English, StrId::AppTitle));
  setLine(1, tr(Language::English, StrId::Initializing));
  char bar[21];
  const int filled = percent / 6;  // 0..16-ish in 20 cols with brackets
  bar[0] = '[';
  for (int i = 0; i < 16; i++) {
    bar[1 + i] = (i < filled) ? '#' : '-';
  }
  bar[17] = ']';
  bar[18] = 0;
  setLine(2, bar);
  setLine(3, "");
}

void Ui::drawMenu(Language lang, const char* const* items, uint8_t count, uint8_t selected,
                  uint8_t windowTop) {
  (void)lang;
  for (uint8_t row = 0; row < 4; row++) {
    const uint8_t idx = static_cast<uint8_t>(windowTop + row);
    if (idx >= count) {
      setLine(row, "");
      continue;
    }
    char line[21];
    snprintf(line, sizeof line, "%c %-18s", (idx == selected) ? '>' : ' ', items[idx]);
    setLine(row, line);
  }
}

void Ui::drawEditField(Language lang, const char* label, const char* value, bool selected) {
  (void)lang;
  char line[21];
  snprintf(line, sizeof line, "%c%s:%s", selected ? '>' : ' ', label, value);
  // caller places on chosen row
  (void)line;
}

void Ui::drawStartConfirm(Language lang, const WindingProgram& p) {
  char d[8], ru[12], rd[12];
  formatDir(lang, p.direction, d, sizeof d);
  formatRampType(lang, p.rampUpType, ru, sizeof ru);
  formatRampType(lang, p.rampDownType, rd, sizeof rd);
  setLinef(0, "%05lu %s", static_cast<unsigned long>(p.targetTurns), tr(lang, StrId::Turns));
  setLinef(1, "%u RPM  %s", p.targetRpm, d);
  setLinef(2, "UP %s %0.1fs", ru, p.rampUpMs / 1000.0f);
  setLinef(3, "%s", tr(lang, StrId::ClickStart));
}

void Ui::drawCountdown(int n) {
  if (n > 0) {
    setLine(0, "");
    setLinef(1, "        %d", n);
    setLine(2, "");
    setLine(3, "");
  } else {
    setLine(0, "");
    setLine(1, "      START");
    setLine(2, "");
    setLine(3, "");
  }
}

static const char* phaseLabel(Language lang, WindPhase p) {
  switch (p) {
    case WindPhase::RampUp:
      return "RAMP UP";
    case WindPhase::Cruise:
      return tr(lang, StrId::Run);
    case WindPhase::RampDown:
      return "RAMP DN";
    case WindPhase::FinalApproach:
      return "APPROACH";
    case WindPhase::Pausing:
      return "STOPPING";
    default:
      return tr(lang, StrId::Run);
  }
}

void Ui::drawRun(Language lang, const WindingStatus& s) {
  char d[8];
  formatDir(lang, s.direction, d, sizeof d);
  setLinef(0, "%-7s %5lu/%lu", phaseLabel(lang, s.phase),
           static_cast<unsigned long>(s.turnsDone),
           static_cast<unsigned long>(s.turnsTarget));
  setLinef(1, "RPM %4u / %4u", s.setRpm, s.actualRpm);
  char ru[12];
  formatRampType(lang, s.rampUpType, ru, sizeof ru);
  setLinef(2, "%-3s %s", d, ru);
  setLine(3, tr(lang, StrId::HoldPause));
}

void Ui::drawPaused(Language lang, const WindingStatus& s) {
  setLinef(0, "      %s", tr(lang, StrId::Paused));
  setLinef(1, "   %lu / %lu", static_cast<unsigned long>(s.turnsDone),
           static_cast<unsigned long>(s.turnsTarget));
  setLine(2, "");
  setLine(3, tr(lang, StrId::ClickGoHoldStop));
}

void Ui::drawComplete(Language lang, const WindingStatus& s) {
  const uint32_t sec = s.activeMs / 1000UL;
  setLinef(0, "     %s", tr(lang, StrId::Complete));
  setLinef(1, "   %lu / %lu", static_cast<unsigned long>(s.turnsDone),
           static_cast<unsigned long>(s.turnsTarget));
  setLinef(2, "   TIME %02u:%02u", static_cast<unsigned>(sec / 60),
           static_cast<unsigned>(sec % 60));
  setLine(3, tr(lang, StrId::ClickAgain));
}

void Ui::drawAborted(Language lang, const WindingStatus& s) {
  const uint32_t sec = s.activeMs / 1000UL;
  setLinef(0, "      %s", tr(lang, StrId::Stopped));
  setLinef(1, "   %lu / %lu", static_cast<unsigned long>(s.turnsDone),
           static_cast<unsigned long>(s.turnsTarget));
  setLinef(2, "   TIME %02u:%02u", static_cast<unsigned>(sec / 60),
           static_cast<unsigned>(sec % 60));
  setLine(3, tr(lang, StrId::HoldBack));
}

void Ui::drawError(Language lang, const char* line1, const char* line2) {
  setLine(0, line1 ? line1 : tr(lang, StrId::MotorError));
  setLine(1, line2 ? line2 : "");
  setLine(2, "");
  setLine(3, tr(lang, StrId::ClickRetry));
}

void Ui::drawDiagnostics(Language lang, bool motorOk, bool rs485Ok, uint16_t rpm, int64_t enc,
                         uint8_t alarm, uint32_t encOk, uint32_t encFail, uint32_t maxGapMs,
                         uint32_t ageMs) {
  setLine(0, tr(lang, motorOk ? StrId::MotorOk : StrId::MotorError));
  setLinef(1, "%s gap:%lu", tr(lang, rs485Ok ? StrId::Rs485Ok : StrId::NoRs485),
           static_cast<unsigned long>(maxGapMs));
  setLinef(2, "RPM:%u AL:%u age:%lu", rpm, alarm, static_cast<unsigned long>(ageMs));
  setLinef(3, "E:%lld %lu/%lu", static_cast<long long>(enc),
           static_cast<unsigned long>(encOk), static_cast<unsigned long>(encFail));
}
