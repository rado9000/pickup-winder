# Pickup winder – hardware

## Wibracje ok. 400–700 RPM (np. 500 RPM)

Częsta przyczyna: **TMC2209 w StealthChop** — przy średnich obrotach silnik „buczy” i drży mimo poprawnych kroków.

**Co zrobić (wybierz jedno):**

1. **UART** (zalecane w firmware): `USE_TMC2209_UART 1` w `config.h`, przewód GP4/GP5, zworka R8. Przy **pierwszym** nawijaniu ustawi SpreadCycle (nie w menu — bez freeze).
2. **Bez UART:** na MKS TMC2209 włącz **SpreadCycle** jumperem / trybem standalone (wg instrukcji modułu), nie StealthChop.
3. **Microstep 1/16** na module (MS) + w `config.h` ustaw `MICROSTEP 16` — często mniej drgań niż 1/8 przy tym samym RPM.
4. **VREF / prąd:** za niski prąd = wibracje pod obciążeniem; typowo ~1–1,2 A na cewkę (VREF wg MKS).

Firmware: wolniejsza rampa i niższe przyspieszenie do 700 RPM (`MOTOR_ACCEL_LOW_RPM_STEPS_S2`), długi odcinek liniowy (`MOTOR_LINEAR_ACCEL_STEPS`).

## Okablowanie Pico → TMC2209

| Sygnał | GPIO |
| --- | --- |
| STEP | GP2 |
| DIR | GP3 |
| EN | GP10 |
| UART TX / RX | GP4 / GP5 (opcjonalnie) |

24 V na silnik, wspólna masa.

## Rampa (firmware)

| Stała | Domyślnie |
| --- | --- |
| `MOTOR_ACCEL_STEPS_S2` | 3500 |
| `MOTOR_ACCEL_LOW_RPM_STEPS_S2` | 1600 (RPM ≤ 700) |
| `MOTOR_LINEAR_ACCEL_STEPS` | 3000 |
| `MAX_RPM` | 1500 |

## Kompilacja

```bash
pio run -e pico
```

Wgraj `.pio/build/pico/firmware.uf2`.
