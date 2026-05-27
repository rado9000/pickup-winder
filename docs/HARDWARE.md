# Pickup winder – 17HS4401 + MKS TMC2209 V2.0

## Zworka R8 / tryb UART (MKS V2.0)

Na module MKS **zworka przy R8 / UART** decyduje, czy PDN_UART jest połączony z linią sterującą:

| Tryb | Zworka R8 | UART w firmware |
| --- | --- | --- |
| **UART** (zalecane tu) | **Zworka ZAMKNIĘTA** (wg instrukcji MKS) | Tak — konfiguracja **raz** przy starcie |
| STEP/DIR only | Często **rozwarta** | `USE_TMC2209_UART 0` |

Zła zworka / długi przewód UART → losowe stopki, „cykanie”, gubienie kroków przy wysokich RPM.

**Przewód UART:** krótki, wspólna masa, TX przez **1 kΩ** do PDN_UART, RX bezpośrednio (GP4/GP5).

## Strategia firmware (stabilne 1000+ RPM)

1. **UART tylko w `setup()`** — `tmc2209ConfigureOnce()`, **zero** UART w `loop()` / podczas nawijania.
2. Potem tylko **STEP/DIR/EN** (FastAccelStepper na PIO).
3. Ustawienia TMC dla wysokich obrotów:
   - `en_spreadCycle(true)` — SpreadCycle (moment przy 500–1000 RPM)
   - `intpol(false)` — prawdziwe **1/8**, bez interpolacji do 256
   - `pwm_autoscale(false)`

## Okablowanie Pico

| TMC2209 | Pico |
| --- | --- |
| PDN_UART | GP5 RX + GP4 TX (1 kΩ) |
| STEP / DIR / EN | GP2 / GP3 / GP10 |
| VM | **24 V** silnika |
| GND | wspólna |

## Zasilanie

- **24 V** na silnik — przy 12 V przy 500–1000 RPM często buczenie i stop.
- Prąd z UART: `TMC_RUN_CURRENT_MA` (domyślnie 1200 mA) — bez kręcenia VREF.

## STEP / rampa

- FastAccelStepper: **jedna** rampa (`setAcceleration` + `setSpeedInHz` + `runForward()` przy starcie).
- LCD co **250 ms** — nie blokuje STEP.

## LCD przy starcie

- **„TMC cfg OK Spread”** — UART OK, SpreadCycle, bez intpol.
- **„TMC UART: R8/wire”** — brak komunikacji (zworka, adres, przewód).

## `config.h`

| Stała | Domyślnie |
| --- | --- |
| `USE_TMC2209_UART` | 1 |
| `TMC_EN_SPREADCYCLE` | 1 |
| `TMC_USE_INTERPOLATION` | 0 |
| `TMC_PWM_AUTOSCALE` | 0 |
| `MICROSTEP` | 8 |

## Kompilacja

```bash
pio run -e pico
```

Wgraj `.pio/build/pico/firmware.uf2`.
