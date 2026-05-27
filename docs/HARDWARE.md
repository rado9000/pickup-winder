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

1. **UART tylko poza nawijaniem** — `tmc2209ConfigureOnce()` przy pierwszym starcie nawijania (gdy `USE_TMC2209_UART=1`), **zero** UART w trakcie jazdy.
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

## Test UART (czy działa)

Najprościej: odczyt **`driver.version()`** po `begin()` na UART.

| Wartość | Znaczenie |
| --- | --- |
| **0x21** | UART OK (typowe dla TMC2209) |
| **0**, **255** | Zły pin, brak masy, zworka R8, UART źle ustawiony |

Na **RP2040** przed `Serial1.begin()` trzeba ustawić piny (w Arduino Uno często nie trzeba):

```cpp
Serial1.setTX(4);
Serial1.setRX(5);
Serial1.begin(115200);
```

W tym projekcie robi to `tmc2209ProbeVersion()` / `tmc2209ConfigureOnce()` w `src/tmc2209_driver.cpp`.

### Firmware

| `TMC_UART_BOOT_PROBE` | 0 domyślnie — **1 tylko gdy UART podłączony** (inaczej freeze menu) |
| `USE_TMC2209_UART` | 1 = pełna konfiguracja SpreadCycle przy **pierwszym** nawijaniu |
| `TMC_UART_USB_DEBUG` | 1 = dodatkowo `Serial.println(ver, HEX)` na USB |

Gdy na LCD widzisz **„TMC UART OK 0x21”**, możesz ustawić `USE_TMC2209_UART` na `1` i przebudować.

**Uwaga:** `TMC_UART_BOOT_PROBE=1` bez podłączonego UART (lub złej zworki R8) **zawiesza menu** — biblioteka TMC czeka na odpowiedź. Domyślnie wyłączone; test UART włącz dopiero po okablowaniu.

## LCD przy starcie (`TMC_UART_BOOT_PROBE`)

- **„TMC UART OK 0x21”** — komunikacja UART działa.
- **„TMC UART: 0/255?”** — brak sensownej odpowiedzi (zworka R8, przewód, masa).
- **„TMC UART 0xNN”** — inna wartość niż 0x21 (adres MS, uszkodzony moduł).

## `config.h`

| Stała | Domyślnie |
| --- | --- |
| `USE_TMC2209_UART` | 0 (menu od razu; włącz po teście 0x21) |
| `TMC_UART_BOOT_PROBE` | 0 |
| `TMC_EN_SPREADCYCLE` | 1 |
| `TMC_USE_INTERPOLATION` | 0 |
| `TMC_PWM_AUTOSCALE` | 0 |
| `MICROSTEP` | 8 |

## Kompilacja

```bash
pio run -e pico
```

Wgraj `.pio/build/pico/firmware.uf2`.
