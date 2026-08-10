#include "servo42.h"

#include <Arduino.h>
#include <HardwareSerial.h>

static HardwareSerial Rs485Port(1);

uint8_t Servo42::crcSum(const uint8_t* data, int len) const {
  uint16_t s = 0;
  for (int i = 0; i < len; i++) {
    s = static_cast<uint16_t>(s + data[i]);
  }
  return static_cast<uint8_t>(s & 0xFF);
}

void Servo42::setTx(bool enable) {
  digitalWrite(PIN_RS485_RE_DE, enable ? HIGH : LOW);
}

void Servo42::clearRx() {
  while (Rs485Port.available()) {
    Rs485Port.read();
  }
}

bool Servo42::transact(const uint8_t* tx, int txLen, uint8_t* rx, int rxLen, uint16_t timeoutMs) {
  clearRx();
  setTx(true);
  delayMicroseconds(50);
  Rs485Port.write(tx, txLen);
  Rs485Port.flush();
  delayMicroseconds(50);
  setTx(false);

  if (rxLen <= 0) {
    lastOkMs_ = millis();
    failStreak_ = 0;
    return true;
  }

  const uint32_t start = millis();
  int idx = 0;
  while (millis() - start < timeoutMs) {
    while (Rs485Port.available()) {
      const uint8_t b = static_cast<uint8_t>(Rs485Port.read());
      if (idx < rxLen) {
        rx[idx++] = b;
      }
      if (idx >= rxLen) {
        lastOkMs_ = millis();
        failStreak_ = 0;
        return true;
      }
    }
    delayMicroseconds(100);
  }
  failStreak_++;
  return false;
}

bool Servo42::begin() {
  pinMode(PIN_RS485_RE_DE, OUTPUT);
  setTx(false);
  Rs485Port.begin(SERVO_BAUD, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
  delay(20);
  return true;
}

bool Servo42::ping() {
  uint8_t st = 0;
  return readAlarm(st);
}

bool Servo42::setWorkModeBusClosedFoc() {
  uint8_t tx[5] = {0xFA, SERVO_ADDR, 0x82, SERVO_MODE_BUS_CLOSED_FOC, 0};
  tx[4] = crcSum(tx, 4);
  uint8_t rx[5];
  for (int i = 0; i < SERVO_COMM_RETRIES; i++) {
    if (transact(tx, 5, rx, 5, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB && rx[2] == 0x82) {
      return rx[3] == 1;
    }
  }
  return false;
}

bool Servo42::setEnable(bool on) {
  uint8_t tx[5] = {0xFA, SERVO_ADDR, 0xF3, static_cast<uint8_t>(on ? 1 : 0), 0};
  tx[4] = crcSum(tx, 4);
  uint8_t rx[5];
  return transact(tx, 5, rx, 5, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB && rx[2] == 0xF3 &&
         rx[3] == 1;
}

bool Servo42::setHeartbeatMs(uint32_t ms) {
  uint8_t tx[8] = {0xFA, SERVO_ADDR, 0x89, 0, 0, 0, 0, 0};
  tx[3] = static_cast<uint8_t>((ms >> 24) & 0xFF);
  tx[4] = static_cast<uint8_t>((ms >> 16) & 0xFF);
  tx[5] = static_cast<uint8_t>((ms >> 8) & 0xFF);
  tx[6] = static_cast<uint8_t>(ms & 0xFF);
  tx[7] = crcSum(tx, 7);
  uint8_t rx[5];
  return transact(tx, 8, rx, 5, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB && rx[2] == 0x89 &&
         rx[3] == 1;
}

bool Servo42::emergencyStop() {
  uint8_t tx[4] = {0xFA, SERVO_ADDR, 0xF7, 0};
  tx[3] = crcSum(tx, 3);
  uint8_t rx[5];
  return transact(tx, 4, rx, 5, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB && rx[2] == 0xF7;
}

bool Servo42::speedRun(bool dirCw, uint16_t rpm, uint8_t acc) {
  if (rpm > SERVO_MAX_HARDWARE_RPM) {
    rpm = SERVO_MAX_HARDWARE_RPM;
  }
  uint8_t hi = static_cast<uint8_t>((rpm >> 8) & 0x0F);
  const uint8_t lo = static_cast<uint8_t>(rpm & 0xFF);
  if (!dirCw) {
    hi = static_cast<uint8_t>(hi | 0x80);  // CCW / reverse
  }
  uint8_t tx[7] = {0xFA, SERVO_ADDR, 0xF6, hi, lo, acc, 0};
  tx[6] = crcSum(tx, 6);
  uint8_t rx[5];
  return transact(tx, 7, rx, 5, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB && rx[2] == 0xF6;
}

bool Servo42::speedStop(uint8_t acc) {
  uint8_t tx[7] = {0xFA, SERVO_ADDR, 0xF6, 0x00, 0x00, acc, 0};
  tx[6] = crcSum(tx, 6);
  uint8_t rx[5];
  return transact(tx, 7, rx, 5, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB && rx[2] == 0xF6;
}

bool Servo42::moveRelative(uint16_t rpm, uint8_t acc, int32_t relCounts) {
  if (rpm > SERVO_MAX_HARDWARE_RPM) {
    rpm = SERVO_MAX_HARDWARE_RPM;
  }
  uint8_t tx[11] = {0xFA, SERVO_ADDR, 0xF4, 0, 0, acc, 0, 0, 0, 0, 0};
  tx[3] = static_cast<uint8_t>((rpm >> 8) & 0xFF);
  tx[4] = static_cast<uint8_t>(rpm & 0xFF);
  tx[6] = static_cast<uint8_t>((relCounts >> 24) & 0xFF);
  tx[7] = static_cast<uint8_t>((relCounts >> 16) & 0xFF);
  tx[8] = static_cast<uint8_t>((relCounts >> 8) & 0xFF);
  tx[9] = static_cast<uint8_t>(relCounts & 0xFF);
  tx[10] = crcSum(tx, 10);
  uint8_t rx[5];
  return transact(tx, 11, rx, 5, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB && rx[2] == 0xF4;
}

bool Servo42::readEncoder(int64_t& outCounts) {
  uint8_t tx[4] = {0xFA, SERVO_ADDR, 0x31, 0};
  tx[3] = crcSum(tx, 3);
  uint8_t rx[10];
  if (!transact(tx, 4, rx, 10, SERVO_RESPONSE_TIMEOUT_MS)) {
    return false;
  }
  if (rx[0] != 0xFB || rx[1] != SERVO_ADDR || rx[2] != 0x31) {
    failStreak_++;
    return false;
  }
  int64_t v = 0;
  for (int i = 0; i < 6; i++) {
    v = (v << 8) | rx[3 + i];
  }
  if (v & (int64_t(1) << 47)) {
    v |= ~((int64_t(1) << 48) - 1);  // sign-extend int48
  }
  outCounts = v;
  return true;
}

bool Servo42::readRpm(int16_t& outRpm) {
  uint8_t tx[4] = {0xFA, SERVO_ADDR, 0x32, 0};
  tx[3] = crcSum(tx, 3);
  uint8_t rx[6];
  if (!transact(tx, 4, rx, 6, SERVO_RESPONSE_TIMEOUT_MS)) {
    return false;
  }
  if (rx[0] != 0xFB || rx[1] != SERVO_ADDR || rx[2] != 0x32) {
    failStreak_++;
    return false;
  }
  outRpm = static_cast<int16_t>((rx[3] << 8) | rx[4]);
  return true;
}

bool Servo42::readAlarm(uint8_t& outStatus) {
  uint8_t tx[4] = {0xFA, SERVO_ADDR, 0x37, 0};
  tx[3] = crcSum(tx, 3);
  uint8_t rx[5];
  if (!transact(tx, 4, rx, 5, SERVO_RESPONSE_TIMEOUT_MS)) {
    return false;
  }
  if (rx[0] != 0xFB || rx[1] != SERVO_ADDR || rx[2] != 0x37) {
    failStreak_++;
    return false;
  }
  outStatus = rx[3];
  return true;
}

bool Servo42::readBusStatus(uint8_t& outStatus) {
  uint8_t tx[4] = {0xFA, SERVO_ADDR, 0xF1, 0};
  tx[3] = crcSum(tx, 3);
  uint8_t rx[5];
  if (!transact(tx, 4, rx, 5, SERVO_RESPONSE_TIMEOUT_MS)) {
    return false;
  }
  if (rx[0] != 0xFB || rx[1] != SERVO_ADDR || rx[2] != 0xF1) {
    failStreak_++;
    return false;
  }
  outStatus = rx[3];
  return true;
}
