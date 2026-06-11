#pragma once

#include <Arduino.h>
#include "config.h"

class Servo42 {
 public:
  void begin();

  void setModeBusClosedLoop();
  void saveSettings();
  void readStatus();

  bool speedRun(bool reverse, uint16_t rpm, uint8_t acc);
  int16_t readRpm();
  int64_t readEncoderAddition();

  bool waitUntilStopped(uint16_t timeoutMs = 60000);

 private:
  uint8_t sumCrc(uint8_t* data, int len);
  void sendRaw(uint8_t* cmd, int len);
  void clearRx();
  bool transact(uint8_t* cmd, int cmdLen, uint8_t* resp, int respLen, uint16_t timeoutMs = 150);
};
