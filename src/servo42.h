#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// Low-level MKS SERVO42ES RS485 protocol (manual V1.0.1).
// Frames: FA addr cmd ... CRC8_sum ; response FB addr cmd ... CRC
class Servo42 {
 public:
  bool begin();
  bool ping();  // read status 0x37

  bool setWorkModeBusClosedFoc();  // 0x82 / 0x05 — no save
  bool setEnable(bool on);         // 0xF3
  bool setHeartbeatMs(uint32_t ms);  // 0x89; 0 = off
  bool emergencyStop();            // 0xF7

  // Speed mode 0xF6. dirCw: true=CW (dir bit 0), false=CCW (dir bit 1).
  // Manual: dir 0/1 (CCW/CW) with examples using bit7 of speed-high byte.
  // We map: CW → bit7=0, CCW → bit7=1 (matches verified test code "reverse").
  bool speedRun(bool dirCw, uint16_t rpm, uint8_t acc);

  // Soft stop via F6 with rpm=0, acc≠0.
  bool speedStop(uint8_t acc);

  // Relative coordinate motion 0xF4 (encoder counts, int32). No zeroing required.
  bool moveRelative(uint16_t rpm, uint8_t acc, int32_t relCounts);

  bool readEncoder(int64_t& outCounts);   // 0x31 int48
  bool readRpm(int16_t& outRpm);          // 0x32
  bool readAlarm(uint8_t& outStatus);     // 0x37
  bool readBusStatus(uint8_t& outStatus); // 0xF1

  uint32_t lastOkMs() const { return lastOkMs_; }
  uint16_t failStreak() const { return failStreak_; }

 private:
  uint8_t crcSum(const uint8_t* data, int len) const;
  void setTx(bool enable);
  void clearRx();
  bool transact(const uint8_t* tx, int txLen, uint8_t* rx, int rxLen, uint16_t timeoutMs);
  bool expectEcho(uint8_t cmd, uint8_t* rx, int rxLen);

  uint32_t lastOkMs_ = 0;
  uint16_t failStreak_ = 0;
};
