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

## Połączenia (Arduino UNO / Nano)

### LCD 2004A (HD44780, I2C backpack)
| LCD | Arduino |
| --- | --- |
| SDA | A4 (UNO) / A4 (Nano) |
| SCL | A5 (UNO) / A5 (Nano) |
| VCC | 5V |
| GND | GND |

### Enkoder z przyciskiem
| Enkoder | Arduino |
| --- | --- |
| CLK | D2 |
| DT | D3 |
| SW | D4 |
| + | 5V |
| GND | GND |

### TMC2209 (Step/Dir)
| TMC2209 | Arduino |
| --- | --- |
| STEP | D9 (OC1A) |
| DIR | D6 |
| EN | D7 |
| VIO | 5V |
| GND | GND |
| UART RX | A0 (opcjonalnie) |
| UART TX | A1 (opcjonalnie) |

> **Uwaga:** ustaw mikrokrok zgodnie z konfiguracją sterownika, a `MICROSTEP`/`COUNT_STEPS_PER_REV` w kodzie dopasuj do konfiguracji. Adres I2C LCD (`0x27`) można zmienić w kodzie, jeśli moduł ma inny.

## Uwagi do implementacji

- Presety zapisujemy w EEPROM, limit 8 wpisów.
- Kod startuje w **Manual mode**.
- Menu jest na enkoderze: obrót = zmiana wartości/pozycji, klik = akceptacja.
- Soft start/stop: rampa prędkości przy starcie i hamowaniu w czasie pauzy/zakończenia.
- Zakresy: 1–99999 zwojów oraz 1–600 RPM (limit w kodzie `MAX_RPM_USER`).
- Silnik testowy: **17HS4401** (1.8° = 200 kroków/obrót). W kodzie `MICROSTEP` ustawia mikrokrok, a `STEPS_PER_REV = 200 * MICROSTEP`.
- Domyślnie mikrokrok **1/8** (`MICROSTEP = 8`) i `COUNT_STEPS_PER_REV` do kalibracji zliczania.
- Dla **maksymalnych obrotów** ustaw **pełny krok (1/1)**: MS1=LOW, MS2=LOW, MS3=LOW (jeśli moduł ma 3 piny). W TMC2209 zwykle są piny **CFG1/CFG2** – oba LOW oznaczają pełny krok; jeśli moduł ma CFG3/MS3, również ustaw LOW.
- Dla sterowania przez UART z biblioteką **TMCStepper** ustaw `USE_TMC2209_UART = 1` w kodzie i podłącz RX/TX do A0/A1 (można zmienić w kodzie). Dopasuj prąd silnika, mikrokrok i `R_SENSE` w `setupTmc2209Uart()`.
