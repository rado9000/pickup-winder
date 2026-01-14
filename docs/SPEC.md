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
- **Soft start**:
  - rampa prędkości, aby chronić drut AWG43.
- **Stop/Resume**:
  - kliknięcie w trakcie nawijania zatrzymuje silnik,
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
| STEP | D5 |
| DIR | D6 |
| EN | D13 |
| VIO | 5V |
| GND | GND |

> **Uwaga:** ustaw mikrokrok zgodnie z wymaganiami (mostki MS1/MS2), a `STEPS_PER_REV` w kodzie dopasuj do konfiguracji. Adres I2C LCD (`0x27`) można zmienić w kodzie, jeśli moduł ma inny.

## Uwagi do implementacji

- Presety zapisujemy w EEPROM, limit 8 wpisów.
- Kod startuje w **Manual mode**.
- Menu jest na enkoderze: obrót = zmiana wartości/pozycji, klik = akceptacja.
- Soft start: zwiększanie RPM przy starcie w krokach co kilka ms.
- Zakresy: 1–99999 zwojów oraz 1–2000 RPM.
- Silnik testowy: **42STH60-2004Q** (1.8° = 200 kroków/obrót). W kodzie `MICROSTEP` ustawia mikrokrok, a `STEPS_PER_REV = 200 * MICROSTEP`.
- Dla **gładkiej pracy** ustaw mikrokrok np. **1/8** (w kodzie `MICROSTEP = 8`) i dopasuj MS/CFG do 1/8.
- Dla **maksymalnych obrotów** ustaw **pełny krok (1/1)**: MS1=LOW, MS2=LOW, MS3=LOW (jeśli moduł ma 3 piny). W TMC2209 zwykle są piny **CFG1/CFG2** – oba LOW oznaczają pełny krok; jeśli moduł ma CFG3/MS3, również ustaw LOW.
