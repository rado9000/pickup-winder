# Pickup winder – sprzęt i kalibracja

## Scalony firmware (nowy UI + stary napęd)

Ten branch łączy:

- **Z nowego projektu:** menu, presety (32), Gauss z auto-zerowaniem przy starcie, prewind, A3144, odliczanie 3..0.
- **Ze starego (działającego) projektu:** `analogWrite` + `analogWriteFreq` na STEP, rampa RPM, **UART TMC** (prąd + mikrokrok), kierunek DIR jak w starym kodzie (`DIR_CW_LEVEL = HIGH`).

## Mapowanie pinów

| Moduł | GPIO |
| --- | --- |
| LCD I2C SDA/SCL | GP0 / GP1 |
| STEP / DIR / EN | GP2 / GP3 / GP10 |
| TMC UART TX/RX | GP4 / GP5 |
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

## Gauss – zerowanie przy starcie

Przy `setup()` wywoływane jest `gaussCalibrateZero()` (średnia z 64 próbek ADC bez magnesu). Nie trzymaj magnesu przy włączaniu.

## Konfiguracja w `src/config.h`

| Stała | Domyślnie | Opis |
| --- | --- | --- |
| `USE_TMC_UART` | 1 | Konfiguracja sterownika przez UART (zalecane) |
| `USE_TMC2209` | 0 | 1 jeśli masz TMC2209 jak w starym projekcie |
| `MICROSTEP` | 8 | Musi zgadzać się z MS1/MS2/MS3 (stary projekt: 1/8) |
| `MAX_RPM_A3144` | 2000 | Max RPM dla zliczania A3144 i UI |
| `DIR_CW_LEVEL` | HIGH | Jak w starym projekcie; odwróć jeśli kręci w złą stronę |

## Kompilacja

```bash
pio run -e pico
pio run -e pico -t upload
```
