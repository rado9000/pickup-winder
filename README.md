# pickup-winder

Nawijarka do pickupów gitarowych.

## Wersje firmware

| Branch / platforma | MCU | Silnik |
| --- | --- | --- |
| `cursor/esp32-s3-servo42es-f0d9` (aktualna) | ESP32-S3 DevKit | MKS SERVO42ES RS485 |
| `cursor/pickup-winder-firmware-9725` | RP2040 Pico | TMC2208 step/dir |

## ESP32-S3 + SERVO42ES

- Menu jak w wersji RP2040 (manual, presety, pauza, Gauss meter)
- Sterowanie silnikiem przez RS485 (closed-loop)
- Dokumentacja hardware: [docs/HARDWARE.md](docs/HARDWARE.md)
- Manual silnika: [docs/MKS-SERVO42ES-57ES_RS485_User_Manual_V1.0.1.pdf](docs/MKS-SERVO42ES-57ES_RS485_User_Manual_V1.0.1.pdf)

Środowisko PlatformIO: **`esp32-s3-n16r8`** (16 MB flash, 8 MB PSRAM).

**Windows — instrukcja krok po kroku:** [docs/FLASH_WINDOWS.md](docs/FLASH_WINDOWS.md)

```bash
pio run -e esp32-s3-n16r8 -t upload
pio device monitor -b 115200
```

Copyright (c) 2026 Radosław Litke / AmpLab

All rights reserved.
No permission is granted to use, copy, modify, or distribute this software
without explicit written permission from the author.
