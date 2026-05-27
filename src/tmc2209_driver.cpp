#include "tmc2209_driver.h"
#include "config.h"

#if USE_TMC2209_UART || TMC_UART_BOOT_PROBE

#include <HardwareSerial.h>
#include <TMCStepper.h>

#if USE_TMC2209_UART
static TMC2209Stepper *tmcDriver = nullptr;
static bool tmcReady_ = false;
static bool tmcConfigured_ = false;
#endif

static void tmcUartBeginPins() {
  Serial1.setRX(TMC_UART_RX_PIN);
  Serial1.setTX(TMC_UART_TX_PIN);
  Serial1.begin(TMC_UART_BAUD);
  delay(50);
}

static bool tmcVersionLooksValid(uint8_t ver) {
  return ver != 0 && ver != 255;
}

int tmc2209ProbeVersion() {
  tmcUartBeginPins();

  TMC2209Stepper driver(&Serial1, TMC_R_SENSE, TMC_DRIVER_ADDRESS);
  driver.begin();

  uint8_t ver = driver.version();
#if TMC_UART_USB_DEBUG
  Serial.begin(115200);
  delay(100);
  Serial.print(F("TMC2209 version=0x"));
  Serial.println(ver, HEX);
#endif
  if (!tmcVersionLooksValid(ver)) {
    return -1;
  }
  return (int)ver;
}

#if USE_TMC2209_UART

static void applyHighRpmDriverProfile() {
  if (!tmcDriver) {
    return;
  }

  tmcDriver->pdn_disable(true);
  tmcDriver->I_scale_analog(false);
  tmcDriver->internal_Rsense(false);
  tmcDriver->mstep_reg_select(true);
  tmcDriver->microsteps(TMC_MICROSTEPS);

#if TMC_USE_INTERPOLATION
  tmcDriver->intpol(true);
#else
  tmcDriver->intpol(false);
#endif

  tmcDriver->rms_current(TMC_RUN_CURRENT_MA, TMC_HOLD_MULTIPLIER);
  tmcDriver->iholddelay(10);
  tmcDriver->TPOWERDOWN(20);

#if TMC_EN_SPREADCYCLE
  tmcDriver->en_spreadCycle(true);
#else
  tmcDriver->en_spreadCycle(false);
  tmcDriver->TPWMTHRS(0);
#endif

#if TMC_PWM_AUTOSCALE
  tmcDriver->pwm_autoscale(true);
  tmcDriver->pwm_autograd(true);
#else
  tmcDriver->pwm_autoscale(false);
  tmcDriver->pwm_autograd(false);
#endif

  tmcDriver->toff(4);
  tmcDriver->hstrt(4);
  tmcDriver->hend(1);
  tmcDriver->tbl(2);

  tmcDriver->semin(0);
  tmcDriver->TCOOLTHRS(0xFFFFF);
  tmcDriver->SGTHRS(0);
}

bool tmc2209ConfigureOnce() {
  if (tmcConfigured_) {
    return tmcReady_;
  }
  tmcConfigured_ = true;
  tmcReady_ = false;

  int ver = tmc2209ProbeVersion();
  if (ver != TMC_VERSION_OK) {
    return false;
  }

  if (tmcDriver != nullptr) {
    delete tmcDriver;
    tmcDriver = nullptr;
  }

  tmcDriver = new TMC2209Stepper(&Serial1, TMC_R_SENSE, TMC_DRIVER_ADDRESS);
  if (tmcDriver == nullptr) {
    return false;
  }

  tmcDriver->begin();
  applyHighRpmDriverProfile();
  tmcReady_ = true;
  return true;
}

bool tmc2209Ready() {
  return tmcReady_;
}

#else

bool tmc2209ConfigureOnce() {
  return false;
}

bool tmc2209Ready() {
  return false;
}

#endif

#else

int tmc2209ProbeVersion() {
  return -1;
}

bool tmc2209ConfigureOnce() {
  return false;
}

bool tmc2209Ready() {
  return false;
}

#endif
