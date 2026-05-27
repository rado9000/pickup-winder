# Pickup winder – hardware

## Napęd krokowy (bez FastAccelStepper)

STEP z **timera RP2040** (`repeating_timer`): stała częstotliwość impulsów dla danego RPM, bez biblioteki FAS.

Rampa: start **10 RPM**, co **400 ms** +**10 RPM** do celu. Zmiana RPM = nowy okres timera (skok co 10 obr/min, nie co 1).

| Stała | Domyślnie |
| --- | --- |
| `MOTOR_RPM_RAMP_STEP` | 10 |
| `MOTOR_RPM_RAMP_INTERVAL_MS` | 400 |
| `MAX_RPM` | 1500 |

## TMC2209 / wibracje

SpreadCycle na module (jumper) lub `USE_TMC2209_UART 1` (GP4/GP5, zworka R8). StealthChop często buczy ok. 500 RPM.

## Pico → TMC

| STEP / DIR / EN | GP2 / GP3 / GP10 |
| UART (opcja) | GP4 / GP5 |

24 V silnik, masa wspólna, MS1=MS2=LOW → 1/8 (`MICROSTEP 8`).

## Build

```bash
pio run -e pico
```

`.pio/build/pico/firmware.uf2`
