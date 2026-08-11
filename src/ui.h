#pragma once

#include "language.h"
#include "types.h"
#include "winding_controller.h"

class Ui {
 public:
  void begin();
  void clear();
  void setLine(uint8_t row, const char* text);  // pads/truncates to 20
  void setLinef(uint8_t row, const char* fmt, ...);
  void drawBootProgress(uint8_t percent);
  void drawMenu(Language lang, const char* const* items, uint8_t count, uint8_t selected,
                uint8_t windowTop);
  void drawEditField(Language lang, const char* label, const char* value, bool selected);
  void drawStartConfirm(Language lang, const WindingProgram& p);
  void drawCountdown(int n);
  void drawRun(Language lang, const WindingStatus& s);
  void drawPaused(Language lang, const WindingStatus& s);
  void drawComplete(Language lang, const WindingStatus& s);
  void drawAborted(Language lang, const WindingStatus& s);
  void drawError(Language lang, const char* line1, const char* line2);
  void drawDiagnostics(Language lang, bool motorOk, bool rs485Ok, uint16_t rpm, int64_t enc,
                       uint8_t alarm, uint32_t encOk = 0, uint32_t encFail = 0,
                       uint32_t maxGapMs = 0, uint32_t ageMs = 0);

 private:
  char lines_[4][21]{};
  bool dirty_[4]{true, true, true, true};
  void flush();
  void mark(uint8_t row);
};
