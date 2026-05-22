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

## A3144 – magnes diametryczny na osie

- Jeden impuls na obrót przy zboczu **FALLING** (aktywny LOW).
- Magnes: połówka **S** / połówka **N** wzdłuż średnicy; czujnik musi widzieć wyraźne przejście S→N (nie „zwykły” magnes z półki).
- Filtr: `A3144_DEBOUNCE_US`, minimalny odstęp `A3144_MIN_INTERVAL_US` (ochrona przed podwójnym zliczeniem).
- Jeśli brak impulsów: obróć magnes o 180° lub zmień `A3144_COUNT_ON_FALLING` na `0` (zbocze RISING) w `src/config.h`.

## Gauss – zerowanie przy starcie

Przy `setup()` wywoływane jest `gaussCalibrateZero()` (średnia z 64 próbek ADC bez magnesu). Nie trzymaj magnesu przy włączaniu.

## Konfiguracja w `src/config.h`

| Stała | Domyślnie | Opis |
| --- | --- | --- |
| `USE_TMC_UART` | 1 | Konfiguracja sterownika przez UART (zalecane) |
| `USE_TMC2209` | 0 | 1 jeśli masz TMC2209 jak w starym projekcie |
| `MICROSTEP` | 8 | Musi zgadzać się z MS1/MS2/MS3 (stary projekt: 1/8) |
| `MAX_RPM_USER` | 1500 | Limit RPM w UI |
| `DIR_CW_LEVEL` | HIGH | Jak w starym projekcie; odwróć jeśli kręci w złą stronę |

## Kompilacja

```bash
pio run -e pico
pio run -e pico -t upload
```
