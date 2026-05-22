# pickup-winder

Firmware for a guitar pickup coil winder on **Raspberry Pi Pico (RP2040)**.

## Features

- Manual mode at boot: turns (5 digits), RPM (4 digits), direction, soft start/stop
- Up to 32 presets in emulated EEPROM
- Hall rotation counting (A3144) and optional Gauss screen (AH49HZ3)
- TMC2208 step/dir only (no UART)

## Documentation

- [Hardware and wiring (PL)](docs/HARDWARE.md)
- Pin map and build-time options: `src/config.h`

## Merge (nowy + stary projekt)

- UI, Gauss (auto-zero przy starcie), A3144, prewind — z nowszego firmware.
- Napęd silnika — TMC2208 Step/Dir + `analogWrite` STEP + rampa (bez UART).

Szczegóły kalibracji: [docs/HARDWARE.md](docs/HARDWARE.md). Wszystkie przełączniki: `src/config.h`.

## Build

Requires [PlatformIO](https://platformio.org/):

```bash
pio run -e pico
pio run -e pico -t upload
```

---

Copyright (c) 2026 Radosław Litke / AmpLab

All rights reserved.
No permission is granted to use, copy, modify, or distribute this software
without explicit written permission from the author.
