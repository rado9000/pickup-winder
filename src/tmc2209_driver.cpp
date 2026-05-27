#include "tmc2209_driver.h"
#include "config.h"

#if USE_TMC2209_UART

#include <HardwareSerial.h>
#include <TMCStepper.h>

// MKS TMC2209 V2.0: R_sense = 0.11 ohm (silkscreen 110)
static TMC2209Stepper *tmcDriver = nullptr;
static bool tmcReady_ = false;
static uint32_t lastServiceMs_ = 0;

static uint32_t rpmToStepHz(int rpm) {
  if (rpm <= 0) {
    return 0;
  }
  return (uint32_t)lroundf((rpm / 60.0f) * (float)STEPS_PER_REV);
}

static uint32_t tpwmThrsForRpm(int rpm) {
  if (rpm < 1) {
    rpm = 1;
  }
  uint32_t stepHz = rpmToStepHz(rpm);
  if (stepHz < 1) {
    stepHz = 1;
  }
  uint32_t tpwm = TMC_FCLK_HZ / stepHz;
  if (tpwm > 0xFFFFFU) {
    tpwm = 0xFFFFFU;
  }
  return tpwm;
}

static void applyHybridChopper(int spreadAboveRpm) {
  if (!tmcDriver) {
    return;
  }
  tmcDriver->en_spreadCycle(false);
  tmcDriver->TPWMTHRS(tpwmThrsForRpm(spreadAboveRpm));
  tmcDriver->pwm_autoscale(true);
  tmcDriver->pwm_autograd(true);
  tmcDriver->pwm_freq(1);
  tmcDriver->pwm_grad(4);
}

bool tmc2209Begin() {
  tmcReady_ = false;
  lastServiceMs_ = 0;

  Serial1.setRX(TMC_UART_RX_PIN);
  Serial1.setTX(TMC_UART_TX_PIN);
  Serial1.begin(TMC_UART_BAUD);
  delay(50);

  if (tmcDriver != nullptr) {
    delete tmcDriver;
    tmcDriver = nullptr;
  }

  tmcDriver = new TMC2209Stepper(&Serial1, TMC_R_SENSE, TMC_DRIVER_ADDRESS);
  if (tmcDriver == nullptr) {
    return false;
  }

  tmcDriver->begin();

  if (tmcDriver->version() != 0x21) {
    delete tmcDriver;
    tmcDriver = nullptr;
    return false;
  }

  tmcDriver->pdn_disable(true);
  tmcDriver->I_scale_analog(false);
  tmcDriver->internal_Rsense(false);

  tmcDriver->mstep_reg_select(true);
  tmcDriver->microsteps(TMC_MICROSTEPS);
  tmcDriver->intpol(true);

  tmcDriver->rms_current(TMC_RUN_CURRENT_MA, TMC_HOLD_MULTIPLIER);
  tmcDriver->iholddelay(10);
  tmcDriver->TPOWERDOWN(20);

  applyHybridChopper(TMC_STEALTH_TO_SPREAD_RPM);

  tmcDriver->toff(4);
  tmcDriver->hstrt(4);
  tmcDriver->hend(1);
  tmcDriver->tbl(2);

  tmcDriver->semin(0);

  tmcDriver->TCOOLTHRS(0xFFFFF);
  tmcDriver->SGTHRS(0);

  tmcReady_ = true;
  return true;
}

bool tmc2209Ready() {
  return tmcReady_;
}

void tmc2209ApplyWindingProfile(int targetRpm) {
  if (!tmcReady_ || !tmcDriver) {
    return;
  }

  uint16_t runMa = TMC_RUN_CURRENT_MA;
  if (targetRpm >= 800) {
    runMa = TMC_RUN_CURRENT_HIGH_MA;
  } else if (targetRpm >= 500) {
    runMa = TMC_RUN_CURRENT_MID_MA;
  }
  tmcDriver->rms_current(runMa, TMC_HOLD_MULTIPLIER);

  int spreadRpm = TMC_STEALTH_TO_SPREAD_RPM;
  if (targetRpm >= 900) {
    spreadRpm = 350;
  }
  applyHybridChopper(spreadRpm);
}

void tmc2209Service(uint32_t nowMs) {
  if (!tmcReady_ || !tmcDriver) {
    return;
  }
  if (nowMs - lastServiceMs_ < 500) {
    return;
  }
  lastServiceMs_ = nowMs;

  uint32_t drv = tmcDriver->DRV_STATUS();
  if (drv & 0x02) {
    tmcDriver->GSTAT(0x02);
  }
}

#else

bool tmc2209Begin() {
  return false;
}
bool tmc2209Ready() {
  return false;
}
void tmc2209ApplyWindingProfile(int targetRpm) {
  (void)targetRpm;
}
void tmc2209Service(uint32_t nowMs) {
  (void)nowMs;
}

#endif
