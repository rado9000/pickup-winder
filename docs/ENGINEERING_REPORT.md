# Engineering report — clean-sheet pickup winder

## 1. Architecture

Modular non-blocking firmware: `App` (UI FSM) + `WindingController` (motion FSM) +
`MotorController` / `Servo42` (RS485) + shared `WindingProgram` for Manual and Presets.

## 2–4. SERVO42ES strategy: **hybrid**

| Phase | Method | Cmd |
| --- | --- | --- |
| RAMP_UP, CRUISE, RAMP_DOWN | Speed mode + ESP32 LINEAR/S-curve | `0xF6` |
| FINAL_APPROACH | Relative encoder coordinates | `0xF4` |
| Pause / normal stop | Soft speed stop | `0xF6` rpm=0 |
| Fault | Emergency stop | `0xF7` |

**Why:** Speed mode gives smooth user-timed ramps. Relative `0xF4` hits exact remaining counts without requiring absolute zeroing (`0xF5` needs `0x91`/`0x92`, which moves the motor). Encoder `0x31` is the only turn source.

Work mode `0x82`/`0x05` (bus closed-loop FOC) is applied at wind start **without** `0x60` save.

Heartbeat `0x89` enabled only while winding.

## 5–7. Files

Created: `src/{config.h,types.h,servo42.*,motor_controller.*,ramp_generator.*,turn_counter.*,winding_controller.*,input.*,language.*,presets.*,ui.*,app.*,main.cpp}`, `test/test_math.cpp`, docs, README, platformio.ini.

Removed vs previous branch architecture: monolithic menu/motor/A3144/Gauss/live-mode prototype (this branch is clean from `main`).

## 8. Build

`pio run -e esp32-s3-n16r8` → **SUCCESS**

Host: `g++ … test/test_math.cpp` → **All math tests passed**

## 9. Features

Manual + Presets (NVS) + PL/EN + S-curve/Linear ramps + countdown + pause/resume/abort + diagnostics + alarm/RS485 fault handling. Gauss disabled. No A3144. GPIO13 free.

## 10. Limitations

- Final approach / stop compensation need on-machine tuning.
- Absolute coordinate mode unused (requires zeroing).
- No ETA.
- Brief `delay(400)` only on boot “SYSTEM READY”.

## 11. Tuning (`src/config.h`)

- `SERVO_INTERNAL_ACC` (250)
- `MOTOR_COMMAND_UPDATE_MS` (30)
- `FINAL_APPROACH_RPM` (40)
- `FINAL_POSITION_TOLERANCE_COUNTS` (64)
- `STOP_COMPENSATION_COUNTS` (512)

## 12. First tests

See `docs/HARDWARE.md` checklist (start ≤20 RPM; never begin at 2500).
