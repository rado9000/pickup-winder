# Pickup winder – 17HS4401 + TMC2208

## Firmware (napęd)

- **FastAccelStepper** (PIO) — jedna rampa w bibliotece
- Start: `setAcceleration()` + `setSpeedInHz(cel)` + `runForward()` — **bez** własnej rampy w `loop()`
- **Nie** wywołuj `applySpeedAcceleration()` w pętli — to powodowało wibracje

Przy 1000 RPM i 1/8: `targetHz = 1000 × 1600 / 60 ≈ 26666`

## Hardware (ważniejsze niż kod)

| Temat | Zalecenie |
| --- | --- |
| **Zasilanie silnika** | **24 V** (przy 12 V moment spada przy wysokich RPM → buczenie, gubienie kroków) |
| **Microstep** | **1/8** (MS1=MS2=LOW), `MICROSTEP 8` — nie 1/32+ |
| **VREF** | ~0,9–1,2 V (17HS4401, 1,5 Ω); za nisko = gubi kroki, za wysoko = grzanie |
| **StealthChop** | Cicho do ~400–600 RPM |
| **SpreadCycle** | Stabilniej przy **wysokich** RPM (test powyżej 500–700 jeśli drży) |
| **Rezonans** | Unikaj długiej jazdy na „martwych” RPM (np. ~180, ~420) — przechodź rampą do celu |

## Dostrajanie przyspieszenia (`config.h`)

| Stała | Domyślnie | Gdy… |
| --- | --- | --- |
| `FAS_ACCEL_DEFAULT` | 8000 | gubi kroki → **6000**; buczy → **4000** |
| `FAS_ACCEL_HIGH_RPM` | 5000 | przy 700+ RPM łagodniej |
| `FAS_LINEAR_ACCEL_STEPS` | 800 | S-curve w bibliotece |

## LCD

Odświeżanie co **250 ms** w trakcie nawijania (`WINDING_UI_INTERVAL_MS`) — I2C nie blokuje STEP.

## Kompilacja

```bash
pio run -e pico
```

Wgraj `.pio/build/pico/firmware.uf2`.
