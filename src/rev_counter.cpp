#include "rev_counter.h"
#include "config.h"

void RevCounter::begin(Servo42* driver, uint8_t hallPin) {
  driver_ = driver;
  hallPin_ = hallPin;
#if USE_MOTOR_ENCODER
  useMotorEncoder_ = true;
#else
  useMotorEncoder_ = false;
  pinMode(hallPin_, INPUT_PULLUP);
  lastHallLevel_ = digitalRead(hallPin_);
#endif
  reset();
}

void RevCounter::reset() {
  turns_ = 0;
  hallCount_ = 0;
  if (useMotorEncoder_ && driver_) {
    encoderBaseline_ = driver_->readEncoderAddition();
    if (encoderBaseline_ == INT64_MIN) {
      encoderBaseline_ = 0;
    }
  }
}

void RevCounter::poll() {
  if (useMotorEncoder_) {
    if (!driver_) {
      return;
    }
    int64_t value = driver_->readEncoderAddition();
    if (value == INT64_MIN) {
      return;
    }
    if (motorDir_ == WindingDir::CW) {
      turns_ = (int32_t)((encoderBaseline_ - value) / SERVO42_PULSES_PER_REV);
    } else {
      turns_ = (int32_t)((value - encoderBaseline_) / SERVO42_PULSES_PER_REV);
    }
    return;
  }

  bool level = digitalRead(hallPin_);
  if (!level && lastHallLevel_) {
    uint32_t now = micros();
    if (now - lastHallEdgeUs_ >= A3144_DEBOUNCE_US) {
      lastHallEdgeUs_ = now;
      if (motorDir_ == WindingDir::CW) {
        hallCount_++;
      } else {
        hallCount_--;
      }
    }
  }
  lastHallLevel_ = level;
  turns_ = hallCount_ / A3144_PULSES_PER_REV;
}

bool RevCounter::targetReached() const {
  if (targetTurns_ <= 0) {
    return false;
  }
  return turns_ >= targetTurns_;
}
