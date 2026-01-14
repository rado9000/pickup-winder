# Pickup winder – funkcje i schemat połączeń

## Założenia funkcjonalne

- **Tryb Manual** (start po uruchomieniu):
  - wybór liczby zwojów, prędkości (RPM) i kierunku,
  - kliknięcie przechodzi między polami,
  - pole **Start winding** uruchamia nawijanie,
  - dłuższe przytrzymanie otwiera **Presets**.
- **Menu Presets**:
  - pierwsza opcja: **New preset**,
  - poniżej zapisane presety,
  - dłuższe przytrzymanie wraca do Manual.
- **New preset**:
  - pytania o liczbę zwojów, kierunek i prędkość,
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
