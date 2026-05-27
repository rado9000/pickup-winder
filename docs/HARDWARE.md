# Pickup winder – hardware

## Wibracje ok. 400–700 RPM (np. 500 RPM)

Częsta przyczyna: **TMC2209 w StealthChop** — przy średnich obrotach silnik „buczy” i drży mimo poprawnych kroków.

**Co zrobić (wybierz jedno):**

1. **UART** (zalecane w firmware): `USE_TMC2209_UART 1` w `config.h`, przewód GP4/GP5, zworka R8. Przy **pierwszym** nawijaniu ustawi SpreadCycle (nie w menu — bez freeze).
2. **Bez UART:** na MKS TMC2209 włącz **SpreadCycle** jumperem / trybem standalone (wg instrukcji modułu), nie StealthChop.
3. **Microstep 1/16** na module (MS) + w `config.h` ustaw `MICROSTEP 16` — często mniej drgań niż 1/8 przy tym samym RPM.
4. **VREF / prąd:** za niski prąd = wibracje pod obciążeniem; typowo ~1–1,2 A na cewkę (VREF wg MKS).

## Rampa prędkości (firmware)

Nie ma ciągłych zmian co 1 RPM. Start od **10 RPM**, potem co **350 ms** +**10 RPM** aż do celu (`MOTOR_RPM_RAMP_STEP`, `MOTOR_RPM_RAMP_INTERVAL_MS` w `config.h`). Między progami FAS płynnie dojeżdża (`applySpeedAcceleration`).

## Okablowanie Pico → TMC2209

| Sygnał | GPIO |
| --- | --- |
| STEP | GP2 |
| DIR | GP3 |
| EN | GP10 |
| UART TX / RX | GP4 / GP5 (opcjonalnie) |

24 V na silnik, wspólna masa.

| Stała | Domyślnie |
| --- | --- |
| `MOTOR_RPM_RAMP_STEP` | 10 |
| `MOTOR_RPM_RAMP_INTERVAL_MS` | 350 |
| `MOTOR_ACCEL_STEPS_S2` | 2200 |
| `MAX_RPM` | 1500 |

## Kompilacja

```bash
pio run -e pico
```

Wgraj `.pio/build/pico/firmware.uf2`.
