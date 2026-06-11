#include "gauss_meter.h"
#include "config.h"

void GaussMeter::begin(uint8_t adcPin) {
  pin_ = adcPin;
  analogReadResolution(12);
  gauss_ = 0;
  active_ = false;
}

float GaussMeter::readGauss() {
  uint32_t sum = 0;
  for (int i = 0; i < GAUSS_ADC_SAMPLES; i++) {
    sum += analogRead(pin_);
  }
  float avg = (float)sum / GAUSS_ADC_SAMPLES;
  float volts = avg * 3.3f / 4095.0f;
  const float mvPerG = 2.5f;
  float millivolts = volts * 1000.0f;
  float center = 1650.0f;
  return (millivolts - center) / mvPerG;
}

void GaussMeter::update() {
  gauss_ = readGauss();
  float absG = fabsf(gauss_);
  if (!active_ && absG >= GAUSS_ACTIVATE_G) {
    active_ = true;
  } else if (active_ && absG < GAUSS_ACTIVATE_G) {
    active_ = false;
  }
}
