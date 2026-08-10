# Professional Pickup Winder Firmware (clean-sheet)

ESP32-S3 + MKS SERVO42ES (RS485) + 20×4 LCD + single rotary encoder.

Copyright (c) 2026 Radosław Litke / AmpLab — all rights reserved.

## Motor control strategy (hybrid)

Documented decision based on MKS SERVO42ES/57ES RS485 User Manual V1.0.1:

| Phase | Mode | Command | Why |
| --- | --- | --- | --- |
| RAMP_UP / CRUISE / RAMP_DOWN | Speed | `0xF6` | ESP32 generates LINEAR / quintic S-curve setpoints in time; driver ACC kept high so internal ramp does not distort the profile |
| FINAL_APPROACH | Relative coordinate | `0xF4` | Exact remaining encoder counts (16384/rev from `0x31`); **no zeroing required** (unlike absolute `0xF5`) |
| Normal pause / stop | Speed stop | `0xF6` rpm=0, acc≠0 | Controlled deceleration |
| Fault / loss of control | Emergency stop | `0xF7` | Only for alarms / RS485 position loss |

- Work mode: **RS485 bus closed-loop FOC** (`0x82` / `0x05`), set at wind start **without** save (`0x60`) to avoid flash wear.
- Turns: **only** from cumulative encoder `0x31` (int48). Never from time or commanded RPM.
- Actual RPM: telemetry `0x32`. UI shows `SET / ACTUAL`.
- Heartbeat (`0x89`) enabled during winding as fail-safe; disabled when idle.

## Hardware / GPIO

| Function | GPIO |
| --- | --- |
| RS485 RX / TX / RE_DE | 17 / 18 / 4 |
| LCD SDA / SCL | 8 / 9 |
| Encoder CLK / DT / SW | 10 / 11 / 12 |
| Gauss ADC (disabled) | 1 |
| Free | 13 |

Manual: [docs/MKS-SERVO42ES-57ES_RS485_User_Manual_V1.0.1.pdf](docs/MKS-SERVO42ES-57ES_RS485_User_Manual_V1.0.1.pdf)

## Architecture

```
config.h              pins, limits, tuning
types.h               WindingProgram, enums
servo42.*             RS485 protocol
motor_controller.*    high-level motor + telemetry scheduling
ramp_generator.*      LINEAR + quintic S-curve
turn_counter.*        encoder → turns / target
winding_controller.*  RAMP_UP→CRUISE→RAMP_DOWN→FINAL_APPROACH + pause
input.*               rotary encoder events
language.*            PL/EN strings (no diacritics)
presets.*             NVS Preferences
ui.*                  20x4 LCD (no full clear every frame)
app.*                 UI state machine
main.cpp              setup/loop
```

## UI (one encoder)

- **Rotate** — menu / edit values  
- **Short click** — enter / accept / start / resume  
- **Long press (~900 ms)** — back / cancel / pause / abort (no click on release)

Languages: Polski / English (NVS). Max RPM: **2500**.

## Build / flash (Windows N16R8)

```bash
pio run -e esp32-s3-n16r8
pio run -e esp32-s3-n16r8 -t upload --upload-port COMx
pio device monitor -b 115200
```

See [docs/FLASH_WINDOWS.md](docs/FLASH_WINDOWS.md) and [docs/HARDWARE.md](docs/HARDWARE.md).

## Tuning (`src/config.h`)

| Parameter | Role |
| --- | --- |
| `SERVO_INTERNAL_ACC` | Driver ACC for F6/F4 (high = track software ramp) |
| `MOTOR_COMMAND_UPDATE_MS` | Min interval between speed setpoints |
| `FINAL_APPROACH_RPM` | Speed during F4 approach |
| `FINAL_POSITION_TOLERANCE_COUNTS` | Done when \|err\| ≤ this |
| `STOP_COMPENSATION_COUNTS` | Extra margin before starting ramp-down |

## First hardware tests

See checklist in `docs/HARDWARE.md`.
