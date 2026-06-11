#include "servo42.h"

#include <HardwareSerial.h>

static HardwareSerial rs485(1);

uint8_t Servo42::sumCrc(uint8_t* data, int len) {
  uint16_t sum = 0;
  for (int i = 0; i < len; i++) {
    sum += data[i];
  }
  return sum & 0xFF;
}

void Servo42::sendRaw(uint8_t* cmd, int len) {
  digitalWrite(RS485_RE_DE_PIN, HIGH);
  delayMicroseconds(100);
  rs485.write(cmd, len);
  rs485.flush();
  delayMicroseconds(100);
  digitalWrite(RS485_RE_DE_PIN, LOW);
}

void Servo42::clearRx() {
  while (rs485.available()) {
    rs485.read();
  }
}

bool Servo42::transact(uint8_t* cmd, int cmdLen, uint8_t* resp, int respLen, uint16_t timeoutMs) {
  clearRx();
  sendRaw(cmd, cmdLen);

  unsigned long start = millis();
  int idx = 0;
  while (millis() - start < timeoutMs) {
    while (rs485.available()) {
      uint8_t byte = rs485.read();
      if (idx < respLen) {
        resp[idx++] = byte;
      }
      if (idx >= respLen) {
        return true;
      }
    }
  }
  return false;
}

void Servo42::begin() {
  pinMode(RS485_RE_DE_PIN, OUTPUT);
  digitalWrite(RS485_RE_DE_PIN, LOW);
  rs485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
}

void Servo42::setModeBusClosedLoop() {
  uint8_t cmd[] = {0xFA, SERVO42_ADDR, 0x82, 0x05, 0x00};
  cmd[4] = sumCrc(cmd, 4);
  uint8_t resp[5];
  transact(cmd, 5, resp, 5);
}

void Servo42::saveSettings() {
  uint8_t cmd[] = {0xFA, SERVO42_ADDR, 0x60, 0x01, 0x00};
  cmd[4] = sumCrc(cmd, 4);
  uint8_t resp[5];
  transact(cmd, 5, resp, 5);
}

void Servo42::readStatus() {
  uint8_t cmd[] = {0xFA, SERVO42_ADDR, 0x37, 0x00};
  cmd[3] = sumCrc(cmd, 3);
  uint8_t resp[8];
  transact(cmd, 4, resp, 8);
}

bool Servo42::speedRun(bool reverse, uint16_t rpm, uint8_t acc) {
  if (rpm > SERVO42_MAX_RPM) {
    rpm = SERVO42_MAX_RPM;
  }

  uint8_t speedHigh = (rpm >> 8) & 0x0F;
  uint8_t speedLow = rpm & 0xFF;
  if (reverse) {
    speedHigh |= 0x80;
  }

  uint8_t cmd[] = {0xFA, SERVO42_ADDR, 0xF6, speedHigh, speedLow, acc, 0x00};
  cmd[6] = sumCrc(cmd, 6);
  uint8_t resp[5];
  return transact(cmd, 7, resp, 5);
}

int16_t Servo42::readRpm() {
  uint8_t cmd[] = {0xFA, SERVO42_ADDR, 0x32, 0x00};
  cmd[3] = sumCrc(cmd, 3);

  uint8_t resp[6];
  if (!transact(cmd, 4, resp, 6)) {
    return 9999;
  }
  if (resp[0] != 0xFB || resp[1] != SERVO42_ADDR || resp[2] != 0x32) {
    return 9999;
  }
  return (int16_t)((resp[3] << 8) | resp[4]);
}

int64_t Servo42::readEncoderAddition() {
  uint8_t cmd[] = {0xFA, SERVO42_ADDR, 0x31, 0x00};
  cmd[3] = sumCrc(cmd, 3);

  uint8_t resp[10];
  if (!transact(cmd, 4, resp, 10)) {
    return INT64_MIN;
  }
  if (resp[0] != 0xFB || resp[1] != SERVO42_ADDR || resp[2] != 0x31) {
    return INT64_MIN;
  }

  int64_t value = 0;
  for (int i = 0; i < 6; i++) {
    value = (value << 8) | resp[3 + i];
  }
  if (value & (int64_t(1) << 47)) {
    value |= ~((int64_t(1) << 48) - 1);
  }
  return value;
}

bool Servo42::waitUntilStopped(uint16_t timeoutMs) {
  unsigned long zeroStart = 0;
  unsigned long start = millis();

  while (millis() - start < timeoutMs) {
    int16_t rpm = readRpm();
    if (rpm == 9999) {
      delay(50);
      continue;
    }

    if (abs(rpm) < SERVO42_STOP_RPM_THRESHOLD) {
      if (zeroStart == 0) {
        zeroStart = millis();
      }
      if (millis() - zeroStart > SERVO42_STOP_HOLD_MS) {
        return true;
      }
    } else {
      zeroStart = 0;
    }
    delay(50);
  }
  return false;
}
