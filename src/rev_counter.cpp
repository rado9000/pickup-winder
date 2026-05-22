#include "rev_counter.h"
#include "config.h"

RevCounter* RevCounter::instance_ = nullptr;

void RevCounter::begin(uint8_t pin, uint32_t debounceUs) {
  pin_ = pin;
  debounceUs_ = debounceUs;
  pulsesPerRev_ = A3144_PULSES_PER_REV;
  pinMode(pin_, INPUT_PULLUP);
  instance_ = this;
  attachInterrupt(digitalPinToInterrupt(pin_), isrWrapper, FALLING);
}

void RevCounter::isrWrapper() {
  if (instance_) {
    instance_->onEdge();
  }
}

void RevCounter::onEdge() {
  uint32_t now = micros();
  if (now - lastEdgeUs_ < debounceUs_) {
    return;
  }
  lastEdgeUs_ = now;
  if (motorDir_ == WindingDir::CW) {
    turnCount_++;
  } else {
    turnCount_--;
  }
}

void RevCounter::poll() {}

bool RevCounter::targetReached() const {
  if (targetTurns_ <= 0) {
    return false;
  }
  int32_t t = turns();
  if (motorDir_ == WindingDir::CCW) {
    t = -t;
  }
  return t >= targetTurns_;
}
