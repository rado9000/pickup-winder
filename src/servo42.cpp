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

static void dumpRx(const char* tag, const uint8_t* rx, int n) {
  Serial.printf("[RS485] %s:", tag);
  for (int i = 0; i < n; i++) {
    Serial.printf(" %02X", rx[i]);
  }
  Serial.println();
}

bool Servo42::transact(const uint8_t* tx, int txLen, uint8_t* rx, int rxLen, uint16_t timeoutMs) {
  clearRx();

  // Match verified test timing: assert DE before write, hold after flush.
  setTx(true);
  delayMicroseconds(100);
  Rs485Port.write(tx, txLen);
  Rs485Port.flush();
  // >= ~1 byte time @ 38400 (~260 µs) so last bits leave the wire before RX.
  delayMicroseconds(300);
  setTx(false);
  delayMicroseconds(50);

  if (rxLen <= 0) {
    lastOkMs_ = millis();
    failStreak_ = 0;
    return true;
  }

  const uint32_t start = millis();
  int idx = 0;
  bool synced = false;

  while (millis() - start < timeoutMs) {
    while (Rs485Port.available()) {
      const uint8_t b = static_cast<uint8_t>(Rs485Port.read());
      if (!synced) {
        if (b != 0xFB) {
          continue;  // discard noise / echo before uplink header
        }
        synced = true;
        rx[0] = b;
        idx = 1;
        continue;
      }
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
  Serial.printf("[RS485] timeout cmd=0x%02X got=%d/%d\n", txLen >= 3 ? tx[2] : 0, idx, rxLen);
  if (idx > 0) {
    dumpRx("partial", rx, idx);
  }
  return false;
}

bool Servo42::begin() {
  pinMode(PIN_RS485_RE_DE, OUTPUT);
  setTx(false);
  Rs485Port.setRxBufferSize(256);
  Rs485Port.begin(SERVO_BAUD, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
  // Verified test waited ~1.5 s before first frame; bus/transceiver need settle.
  delay(1000);
  clearRx();
  return true;
}

bool Servo42::ping() {
  uint8_t st = 0;
  for (int i = 0; i < SERVO_COMM_RETRIES; i++) {
    if (readAlarm(st)) {
      return true;
    }
    delay(30);
  }
  return false;
}

bool Servo42::setWorkModeBusClosedFoc() {
  uint8_t tx[5] = {0xFA, SERVO_ADDR, 0x82, SERVO_MODE_BUS_CLOSED_FOC, 0};
  tx[4] = crcSum(tx, 4);
  uint8_t rx[5];
  for (int i = 0; i < SERVO_COMM_RETRIES; i++) {
    if (transact(tx, 5, rx, 5, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB && rx[2] == 0x82) {
      return true;  // accept any status byte; some FW return non-1 while still OK
    }
    delay(20);
  }
  return false;
}

bool Servo42::setEnable(bool on) {
  uint8_t tx[5] = {0xFA, SERVO_ADDR, 0xF3, static_cast<uint8_t>(on ? 1 : 0), 0};
  tx[4] = crcSum(tx, 4);
  uint8_t rx[5];
  for (int i = 0; i < SERVO_COMM_RETRIES; i++) {
    if (transact(tx, 5, rx, 5, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB && rx[2] == 0xF3) {
      return true;
    }
    delay(20);
  }
  return false;
}

bool Servo42::setHeartbeatMs(uint32_t ms) {
  uint8_t tx[8] = {0xFA, SERVO_ADDR, 0x89, 0, 0, 0, 0, 0};
  tx[3] = static_cast<uint8_t>((ms >> 24) & 0xFF);
  tx[4] = static_cast<uint8_t>((ms >> 16) & 0xFF);
  tx[5] = static_cast<uint8_t>((ms >> 8) & 0xFF);
  tx[6] = static_cast<uint8_t>(ms & 0xFF);
  tx[7] = crcSum(tx, 7);
  uint8_t rx[5];
  for (int i = 0; i < SERVO_COMM_RETRIES; i++) {
    if (transact(tx, 8, rx, 5, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB && rx[2] == 0x89) {
      return true;
    }
    delay(20);
  }
  return false;
}

bool Servo42::emergencyStop() {
  uint8_t tx[4] = {0xFA, SERVO_ADDR, 0xF7, 0};
  tx[3] = crcSum(tx, 3);
  uint8_t rx[5];
  for (int i = 0; i < SERVO_COMM_RETRIES; i++) {
    if (transact(tx, 4, rx, 5, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB && rx[2] == 0xF7) {
      return true;
    }
    delay(10);
  }
  return false;
}

bool Servo42::speedRun(bool dirCw, uint16_t rpm, uint8_t acc) {
  if (rpm > SERVO_MAX_HARDWARE_RPM) {
    rpm = SERVO_MAX_HARDWARE_RPM;
  }
  uint8_t hi = static_cast<uint8_t>((rpm >> 8) & 0x0F);
  const uint8_t lo = static_cast<uint8_t>(rpm & 0xFF);
  if (!dirCw) {
    hi = static_cast<uint8_t>(hi | 0x80);
  }
  uint8_t tx[7] = {0xFA, SERVO_ADDR, 0xF6, hi, lo, acc, 0};
  tx[6] = crcSum(tx, 6);
  uint8_t rx[5];
  // Speed cmds: if UartRSP disabled, motor may still accept — don't hard-fail.
  if (transact(tx, 7, rx, 5, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB && rx[2] == 0xF6) {
    return true;
  }
  return failStreak_ < 5;
}

bool Servo42::speedStop(uint8_t acc) {
  uint8_t tx[7] = {0xFA, SERVO_ADDR, 0xF6, 0x00, 0x00, acc, 0};
  tx[6] = crcSum(tx, 6);
  uint8_t rx[5];
  if (transact(tx, 7, rx, 5, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB && rx[2] == 0xF6) {
    return true;
  }
  return failStreak_ < 5;
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
  for (int i = 0; i < SERVO_COMM_RETRIES; i++) {
    if (transact(tx, 11, rx, 5, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB && rx[2] == 0xF4) {
      return true;
    }
    delay(20);
  }
  return false;
}

bool Servo42::readEncoder(int64_t& outCounts) {
  uint8_t tx[4] = {0xFA, SERVO_ADDR, 0x31, 0};
  tx[3] = crcSum(tx, 3);
  uint8_t rx[10];
  for (int attempt = 0; attempt < SERVO_COMM_RETRIES; attempt++) {
    if (transact(tx, 4, rx, 10, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB &&
        rx[1] == SERVO_ADDR && rx[2] == 0x31) {
      int64_t v = 0;
      for (int i = 0; i < 6; i++) {
        v = (v << 8) | rx[3 + i];
      }
      if (v & (int64_t(1) << 47)) {
        v |= ~((int64_t(1) << 48) - 1);
      }
      outCounts = v;
      return true;
    }
    delay(20);
  }
  return false;
}

bool Servo42::readRpm(int16_t& outRpm) {
  uint8_t tx[4] = {0xFA, SERVO_ADDR, 0x32, 0};
  tx[3] = crcSum(tx, 3);
  uint8_t rx[6];
  for (int attempt = 0; attempt < SERVO_COMM_RETRIES; attempt++) {
    if (transact(tx, 4, rx, 6, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB &&
        rx[1] == SERVO_ADDR && rx[2] == 0x32) {
      outRpm = static_cast<int16_t>((rx[3] << 8) | rx[4]);
      return true;
    }
    delay(20);
  }
  return false;
}

bool Servo42::readAlarm(uint8_t& outStatus) {
  uint8_t tx[4] = {0xFA, SERVO_ADDR, 0x37, 0};
  tx[3] = crcSum(tx, 3);
  uint8_t rx[5];
  for (int attempt = 0; attempt < SERVO_COMM_RETRIES; attempt++) {
    if (transact(tx, 4, rx, 5, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB &&
        rx[1] == SERVO_ADDR && rx[2] == 0x37) {
      outStatus = rx[3];
      dumpRx("status", rx, 5);
      return true;
    }
    delay(30);
  }
  return false;
}

bool Servo42::readBusStatus(uint8_t& outStatus) {
  uint8_t tx[4] = {0xFA, SERVO_ADDR, 0xF1, 0};
  tx[3] = crcSum(tx, 3);
  uint8_t rx[5];
  for (int attempt = 0; attempt < SERVO_COMM_RETRIES; attempt++) {
    if (transact(tx, 4, rx, 5, SERVO_RESPONSE_TIMEOUT_MS) && rx[0] == 0xFB &&
        rx[1] == SERVO_ADDR && rx[2] == 0xF1) {
      outStatus = rx[3];
      return true;
    }
    delay(20);
  }
  return false;
}
