# Pickup winder – funkcje i schemat połączeń

## Założenia funkcjonalne

- **Tryb Manual** (start po uruchomieniu):
  - wybór liczby zwojów (5 cyfr) i prędkości (4 cyfry) oraz kierunku,
  - obrót zmienia aktywną cyfrę,
  - kliknięcie przechodzi do kolejnej cyfry, a po niej do następnego pola,
  - aktywne pole miga, aby wskazać edycję,
  - pole **Start winding** uruchamia nawijanie,
  - dłuższe przytrzymanie otwiera **Presets**.
- **Menu Presets**:
  - pierwsza opcja: **New preset**,
  - poniżej zapisane presety,
  - dłuższe przytrzymanie wraca do Manual.
- **New preset**:
  - pytania o liczbę zwojów, kierunek i prędkość (edycja cyfr),
  - aktywne pole miga (cyfry lub znak nazwy),
  - następnie edycja nazwy (obrót = zmiana znaku, klik = kolejny znak),
  - dłuższe przytrzymanie zapisuje do EEPROM.
- **Uruchomienie presetów**:
  - po wybraniu presetu pokazuje parametry,
  - kliknięcie = start nawijania.
- **Zakończenie**:
  - komunikat **Winding complete**,
  - powrót do menu dopiero po kliknięciu.
- **Soft start/stop**:
  - rampa prędkości przy starcie i łagodne hamowanie przy zatrzymaniu.
- **Stop/Resume**:
  - kliknięcie w trakcie nawijania uruchamia hamowanie do zera i zatrzymuje silnik,
  - kolejne kliknięcie uruchamia odliczanie 3..0 i wznawia nawijanie.
- **Wyjście do menu po pauzie**:
  - dłuższe przytrzymanie podczas pauzy wraca do Manual.
- **Odliczanie startu**:
  - każde uruchomienie nawijania (manual i preset) wyświetla 3..0.

## Połączenia (RP2040 / Raspberry Pi Pico)

### LCD 2004A (HD44780, I2C backpack)
| LCD | RP2040 |
| --- | --- |
| SDA | GP0 |
| SCL | GP1 |
| VCC | 5V |
| GND | GND |

### Enkoder z przyciskiem
| Enkoder | RP2040 |
| --- | --- |
| CLK | GP6 |
| DT | GP7 |
| SW | GP8 |
| + | 3.3V lub 5V (zgodnie z modułem) |
| GND | GND |

### Czujnik obrotów A3144 (cyfrowy Hall)
| A3144 | RP2040 |
| --- | --- |
| VCC | 3.3V |
| GND | GND |
| OUT | GP9 |

> **Uwaga:** A3144 działa jako cyfrowy przełącznik Halla. Zamontuj jeden magnes na obracającym się elemencie: jedno zbocze opadające na `OUT` = jeden obrót. Wejście GP9 używa `INPUT_PULLUP`, więc wyjście czujnika jest traktowane jako aktywne w stanie LOW.

### TMC2208 (Step/Dir)
| TMC2208 | RP2040 |
| --- | --- |
| STEP | GP2 |
| DIR | GP3 |
| EN | GP10 |
| VIO | 3.3V |
| GND | GND |
| UART RX | GP5 (opcjonalnie) |
| UART TX | GP4 (opcjonalnie) |

### Czujnik Halla AH49HZ3 (pomiar Gauss)
| AH49HZ3 | RP2040 |
| --- | --- |
| VCC | 3.3V |
| GND | GND |
| VOUT | GP26 / ADC0 |

> **Uwaga:** pomiar aktywuje się automatycznie po wykryciu pola >= 50 G i wraca do menu po spadku poniżej 50 G.

> **Uwaga:** RP2040 używa logiki 3.3V. Jeśli sterownik lub enkoder wymaga 5V, użyj konwertera poziomów.

## Uwagi do implementacji

- Presety zapisujemy w EEPROM, limit 32 wpisów.
- Kod startuje w **Manual mode**.
- Menu jest na enkoderze: obrót = zmiana wartości/pozycji, klik = akceptacja.
- Soft start/stop: rampa prędkości przy starcie i hamowaniu w czasie pauzy/zakończenia (można wyłączyć `USE_SOFT_START`).
- Zakresy: 1–99999 zwojów oraz 1–1500 RPM (limit w kodzie `MAX_RPM_USER`, ustawione pod docelową stabilną prędkość).
- Silnik testowy: **17HS4401** (1.8° = 200 kroków/obrót). W kodzie `MICROSTEP` ustawia mikrokrok, a `STEPS_PER_REV = 200 * MICROSTEP`.
- Domyślnie kod jest ustawiony na **pełny krok (1/1)** (`MICROSTEP = 1`), zgodnie z przełącznikami **MS1=LOW, MS2=LOW, MS3=LOW**.
- Dla docelowych 1500 RPM zostaw pełny krok: sterownik dostaje wtedy ok. 5000 impulsów STEP/s (`200 kroków/obrót * 1500 / 60`). Włączenie mikrokroku zwiększa wymaganą częstotliwość STEP i może pogorszyć maksymalną prędkość; jeśli zmienisz MS1/MS2/MS3, koniecznie ustaw taką samą wartość `MICROSTEP` w kodzie.
- Domyślny sterownik w projekcie to **TMC2208** w trybie **Step/Dir** (bez UART).
- Dla sterowania przez UART z biblioteką **TMCStepper** ustaw `USE_TMC2208_UART = 1`. Dopasuj prąd silnika, mikrokrok i `R_SENSE` w `setupTmc2208Uart()`.
- Wariant RP2040 używa A3144 do zliczania obrotów: `A3144_PIN = GP9`, `A3144_PULSES_PER_REV = 1` i `A3144_DEBOUNCE_US = 3000`. Kierunek zliczania jest wnioskowany z aktualnie zadanej strony pracy silnika (`DIR`), bo pojedynczy A3144 nie wykrywa kierunku samodzielnie.
