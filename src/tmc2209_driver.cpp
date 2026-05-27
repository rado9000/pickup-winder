#include "tmc2209_driver.h"
#include "config.h"

#if USE_TMC2209_UART

#include <HardwareSerial.h>
#include <TMCStepper.h>

static bool tmcApplied_ = false;

bool tmc2209ApplySpreadCycleOnce() {
  if (tmcApplied_) {
    return true;
  }
  tmcApplied_ = true;

  Serial1.setRX(TMC_UART_RX_PIN);
  Serial1.setTX(TMC_UART_TX_PIN);
  Serial1.begin(TMC_UART_BAUD);
  Serial1.setTimeout(TMC_UART_READ_TIMEOUT_MS);
  delay(10);

  TMC2209Stepper driver(&Serial1, TMC_R_SENSE, TMC_DRIVER_ADDRESS);
  uint8_t ver = driver.version();
  if (ver != TMC_VERSION_OK) {
    Serial1.end();
    return false;
  }

  driver.pdn_disable(true);
  driver.I_scale_analog(false);
  driver.mstep_reg_select(false);
  driver.intpol(false);
  driver.en_spreadCycle(true);
  driver.pwm_autoscale(false);

  Serial1.end();
  return true;
}

#else

bool tmc2209ApplySpreadCycleOnce() {
  return false;
}

#endif
