#include "rev_counter.h"
#include "config.h"

void RevCounter::begin(Servo42* driver) {
  driver_ = driver;
  reset();
}

void RevCounter::reset() {
  turns_ = 0;
  if (!driver_) {
    return;
  }
  encoderBaseline_ = driver_->readEncoderAddition();
  if (encoderBaseline_ == INT64_MIN) {
    encoderBaseline_ = 0;
  }
}

void RevCounter::poll() {
  if (!driver_) {
    return;
  }

  int64_t value = driver_->readEncoderAddition();
  if (value == INT64_MIN) {
    return;
  }

  // Manual 0x31: CW += 0x4000/rev, CCW -= 0x4000/rev
  if (motorDir_ == WindingDir::CW) {
    turns_ = (int32_t)((value - encoderBaseline_) / SERVO42_PULSES_PER_REV);
  } else {
    turns_ = (int32_t)((encoderBaseline_ - value) / SERVO42_PULSES_PER_REV);
  }
}

bool RevCounter::targetReached() const {
  if (targetTurns_ <= 0) {
    return false;
  }
  return turns_ >= targetTurns_;
}
