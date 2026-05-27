# Pickup winder

## Napęd (tylko firmware Pico)

- **STEP** — impulsy z timera co 50 µs; okres kroku z RPM (bez bibliotek stepperów).
- **Rampa** — start 20 RPM, co 600 ms +20 RPM (np. do 500 RPM ≈ 14 s).
- **TMC2209** — STEP/DIR/EN + MS1/MS2 na płytce (1/8). **SpreadCycle** ustaw jumperem MKS (StealthChop buczy ~500 RPM).
- **24 V**, wspólna masa, VREF ~1–1,2 A.

## `config.h`

| Stała | Domyślnie |
| --- | --- |
| `MOTOR_RPM_RAMP_STEP` | 20 |
| `MOTOR_RPM_RAMP_INTERVAL_MS` | 600 |
| `MAX_RPM` | 1200 |
| `MICROSTEP` | 8 (zgodnie z MS na TMC) |

Wolniej: większy `MOTOR_RPM_RAMP_INTERVAL_MS` (800–1000).

## Build

```bash
pio run -e pico
```

`.pio/build/pico/firmware.uf2`
