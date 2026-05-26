# Pickup winder – sprzęt i kalibracja

## Scalony firmware (nowy UI + stary napęd)

Ten branch łączy:

- **Z nowego projektu:** menu, presety (32), Gauss z auto-zerowaniem przy starcie, prewind, A3144, odliczanie 3..0.
- **Napęd:** TMC2208 wyłącznie **Step/Dir** (bez UART), `analogWrite` + `analogWriteFreq` na STEP, rampa RPM, `DIR_CW_LEVEL = HIGH`.

## Mapowanie pinów

| Moduł | GPIO |
| --- | --- |
| LCD I2C SDA/SCL | GP0 / GP1 |
| STEP / DIR / EN | GP2 / GP3 / GP10 |
| Enkoder A/B/SW | GP6 / GP7 / GP8 |
| A3144 | GP9 (INPUT_PULLUP) |
| AH49HZ3 ADC | GP26 |

## A3144 – magnes diametryczny na osie (do 2000 RPM)

- **1 impuls / obrót** przy zboczu **FALLING** (LOW przy pull-up) — przejście S→N w polu diametrycznym na osi.
- Przy **2000 RPM** okres impulsu ≈ **30 ms** (33,3 Hz) — A3144 i ISR RP2040 to spokojnie obsługują.
- Filtry w `config.h` (liczone automatycznie):
  - `A3144_DEBOUNCE_US` = 800 µs (zbocze mechaniczne / drgania)
  - `A3144_MIN_INTERVAL_US` = 45% okresu przy 2000 RPM ≈ **13,5 ms** (nie odrzuca obrotów do 2000 RPM)
- Wyższe RPM w menu: ustaw `MAX_RPM_A3144` (domyślnie **2000**).
- Jeśli brak impulsów: obróć magnes o 180° lub `A3144_COUNT_ON_FALLING` → `0` (RISING).

## TMC2208 – microstep (MS1 / MS2)

Na Twoim module **MS3 nie jest obsługiwany** (zostaw LOW). Ustaw `MICROSTEP` w `config.h` tak jak poniżej:

| MS1 | MS2 | Microstep | `MICROSTEP` |
| --- | --- | --- | --- |
| LOW | LOW | 1/8 | **8** (domyślnie w firmware) |
| LOW | HIGH | 1/4 | 4 |
| HIGH | LOW | 1/2 | 2 |
| HIGH | HIGH | 1/16 | 16 |

Jeśli `MICROSTEP` nie zgadza się z pinami MS, RPM na LCD będzie mylące (np. przy 1/8 a `MICROSTEP=1` silnik jedzie ~8× wolniej).

## Soft start / stop

- Rampa od **MIN_RPM** do celu, ok. **15–60 s** (`RAMP_MS_PER_RPM` = 20).
- Krzywa **smootherstep** (płynne przyspieszenie bez szarpnięć), setpoint co pętlę `loop()`.
- Impulsy STEP: **alarmy** + stały czas impulsu (`STEP_PULSE_WIDTH_US`) — bez PWM (mniej wibracji).

## Gauss – zerowanie przy starcie

Przy `setup()` wywoływane jest `gaussCalibrateZero()` (średnia z 64 próbek ADC bez magnesu). Nie trzymaj magnesu przy włączaniu.

## Konfiguracja w `src/config.h`

| Stała | Domyślnie | Opis |
| --- | --- | --- |
| `MICROSTEP` | 8 | MS1=LOW, MS2=LOW → 1/8 kroku (patrz tabela wyżej) |
| `MAX_RPM_A3144` | 2000 | Max RPM dla zliczania A3144 i UI |
| `DIR_CW_LEVEL` | HIGH | Jak w starym projekcie; odwróć jeśli kręci w złą stronę |

## Kompilacja

```bash
pio run -e pico
pio run -e pico -t upload
```
