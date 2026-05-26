# Pickup winder – sprzęt i kalibracja

## Scalony firmware

- Menu, presety, prewind, A3144, odliczanie
- Napęd: **TMC2208 Step/Dir** + **FastAccelStepper** (PIO RP2040, bez `delayMicroseconds` na STEP)
- Rampa **S-curve** (smootherstep) + przyspieszenie w bibliotece

## Silnik: 17HS4401 (NEMA 17)

| Parametr | Wartość | W firmware |
| --- | --- | --- |
| Krok | 1,8° | 200 kroków/obrót silnika |
| Microstep (MS1=MS2=LOW) | 1/8 | `MICROSTEP 8` → 1600 impulsów/obrót wału |
| Rezystancja cewki | 1,5 Ω | Ustaw **VREF** na TMC2208 (typ. ~1,0–1,2 A RMS na fazę, z chłodzeniem) |
| Moment | ~4,28 kg·cm | Przy zbyt niskim prądzie gubi kroki przy wyższym RPM |

Rezystancja 1,5 Ω jest niska — **nie** kręć potencjometrem prądu „na max” bez pomiaru; silnik i sterownik mogą się przegrzewać.

## Mapowanie pinów

| Moduł | GPIO |
| --- | --- |
| LCD I2C SDA/SCL | GP0 / GP1 |
| STEP / DIR / EN | GP2 / GP3 / GP10 |
| Enkoder A/B/SW | GP6 / GP7 / GP8 |
| A3144 | GP9 |
| AH49HZ3 ADC | GP26 |

## TMC2208 – StealthChop (najważniejsze na module)

Firmware **nie używa UART** do TMC — tryb cichy ustawiasz na **module**:

| Tryb | Charakterystyka | Do winder'a |
| --- | --- | --- |
| **SpreadCycle** | większy moment, bardziej szarpany dźwięk | niezalecane |
| **StealthChop** | cicho, płynnie, małe wibracje | **zalecane** |

**Co zrobić:**

1. Na płytce TMC2208 włącz **StealthChop** (złącze/jumper z dokumentacji modułu — często etykieta `SPREAD` / `STEALTH` albo mostek MS/CFG zależnie od wersji).
2. Zostaw **MS1=LOW, MS2=LOW** (u Ciebie 1/8 kroku) i `MICROSTEP 8` w `config.h`.
3. Ustaw sensowny prąd (**VREF** / potencjometr) — za niski = gubienie kroków przy 1000 RPM, za wysoki = grzanie.

Bez StealthChop na sterowniku żadna rampa w kodzie nie usunie piszczenia i rezonansu w całości.

## TMC2208 – microstep (MS1 / MS2)

MS3 na tym module **nieużywany** (LOW).

| MS1 | MS2 | Microstep | `MICROSTEP` |
| --- | --- | --- | --- |
| LOW | LOW | 1/8 | **8** |
| LOW | HIGH | 1/4 | 4 |
| HIGH | LOW | 1/2 | 2 |
| HIGH | HIGH | 1/16 | 16 |

## Soft start – rampa RPM

- Start od **1 RPM**, koniec = RPM z menu.
- Czas rampy ok. **20–90 s** (zależnie od ΔRPM): `RAMP_MS_PER_RPM`, `RAMP_MAX_MS`.
- Krzywa **smootherstep** (S-curve) — płynne 0 → cel bez skoku.
- Impulsy STEP: **FastAccelStepper** (hardware PIO), nie PWM i nie `delayMicroseconds()`.

Stałe w `config.h`: `FAS_LINEAR_ACCEL_STEPS`, `RAMP_MIN_MS`, `RAMP_MAX_MS`.

## Kompilacja

```bash
pio run -e pico
```

Wgraj: `.pio/build/pico/firmware.uf2` (BOOTSEL).
