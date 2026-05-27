# pickup-winder

Firmware for a guitar pickup coil winder on **Raspberry Pi Pico (RP2040)**.

## Stack

- **LiquidCrystal I2C** — jedyna zewnętrzna biblioteka
- Silnik — własny sterownik w `src/motor_driver.cpp` (timer + GPIO)
- UI, presety EEPROM, A3144

## Build

```bash
pio run -e pico
```

UF2: `.pio/build/pico/firmware.uf2`

## Docs

[docs/HARDWARE.md](docs/HARDWARE.md) — okablowanie, rampa, TMC.

Opcje: `src/config.h`
