# Hardware & first tests — clean-sheet winder

## Platform

| Item | Model |
| --- | --- |
| MCU | ESP32-S3 N16R8 |
| Motor | MKS SERVO42ES RS485 NEMA17 |
| LCD | 2004A HD44780 I2C |
| Input | Rotary encoder + switch |
| Feedback | SERVO42ES magnetic encoder only |

Manual: [MKS-SERVO42ES-57ES_RS485_User_Manual_V1.0.1.pdf](MKS-SERVO42ES-57ES_RS485_User_Manual_V1.0.1.pdf)

## Wiring

### RS485

| ESP32-S3 | MAX485 / SERVO |
| --- | --- |
| GPIO17 | RO / RX |
| GPIO18 | DI / TX |
| GPIO4 | DE+RE |
| GND | GND |

Baud **38400**, slave addr **0x01**.

### LCD I2C

| LCD | ESP32-S3 |
| --- | --- |
| SDA | GPIO8 |
| SCL | GPIO9 |
| VCC | 5V |
| GND | GND |

Address `0x27` (`config.h`).

### Encoder

| Enc | ESP32-S3 |
| --- | --- |
| CLK | GPIO10 |
| DT | GPIO11 |
| SW | GPIO12 |
| + | 3.3V |
| GND | GND |

GPIO13 free. Gauss on GPIO1 is **disabled** (`ENABLE_GAUSS_METER=0`).

## Control strategy (summary)

Hybrid: software LINEAR/S-curve via speed `0xF6`, then relative coordinate `0xF4` for final counts. Emergency `0xF7` only on fault. See README.

## First hardware test checklist

1. Power on — motor must **not** move.
2. LCD shows INITIALIZING → SYSTEM READY → main menu.
3. Encoder rotates menu; click enters; long-press backs.
4. Diagnostics: MOTOR/RS485 OK, ENC updates.
5. Idle encoder reading stable.
6. Manually rotate shaft one turn → ENC changes ≈ ±16384.
7. Confirm CW user setting increases encoder (manual: CW += 0x4000).
8. Manual program: 10 turns, 20 RPM, LINEAR 1.0 s up/down.
9. Confirm ACTUAL RPM ≈ SET during cruise.
10. Confirm completed turns ≈ 10.
11. Repeat with S-CURVE.
12. Long-press PAUSE mid-run; click RESUME; long-press STOP from pause.
13. 100 turns @ 100 RPM — measure overshoot; tune `STOP_COMPENSATION_COUNTS` / `FINAL_APPROACH_*`.
14. Step up: 500 → 1000 → 1500 → 2000 → 2500 RPM (never start at 2500).

## Flash (Windows)

[FLASH_WINDOWS.md](FLASH_WINDOWS.md)

```bash
pio run -e esp32-s3-n16r8 -t upload
```
