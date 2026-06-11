# Pickup winder – ESP32-S3 + MKS SERVO42ES

## Platforma

| Element | Model |
| --- | --- |
| MCU | ESP32-S3 DevKitC-1 |
| Silnik | Makerbase MKS SERVO42ES NEMA17 (RS485) |
| Wyświetlacz | LCD 2004A (HD44780, I2C backpack) |
| Enkoder UI | Rotary encoder z przyciskiem |
| Zliczanie obrotów | Enkoder magnetyczny wbudowany w SERVO42 (RS485) |
| Czujnik Gauss | AH49HZ3 (pomiar magnesów pickupu) |

Manual silnika: [MKS SERVO42&57ES RS485 User Manual V1.0.1](MKS-SERVO42ES-57ES_RS485_User_Manual_V1.0.1.pdf)  
Źródło: [makerbase-motor/MKS-SERVO42ES-57ES](https://github.com/makerbase-motor/MKS-SERVO42ES-57ES/blob/master/User%20Manual/MKS%20SERVO42%2657ES_RS485%20User%20Manual%20V1.0.1.pdf)

## Funkcje

- **Tryb Manual** – edycja zwojów (5 cyfr), RPM (4 cyfry), kierunku CW/CCW
- **Presets** – zapis do NVS (max 32), long-press = powrót
- **Nawijanie** – odliczanie 3..0, pauza/wznowienie, soft stop przez RS485
- **Gauss meter** – auto-kalibracja zera po starcie (gdy brak magnesu), auto-aktywacja przy polu ≥ 50 G

## Połączenia

### RS485 → MKS SERVO42ES

| ESP32-S3 | SERVO42ES |
| --- | --- |
| GPIO 17 (RX) | TX (RS485 A/B przez konwerter) |
| GPIO 18 (TX) | RX |
| GPIO 4 (RE/DE) | DE + RE konwertera MAX485 |
| GND | GND |
| 5V/3.3V | VCC modułu RS485 |

> Adres slave domyślnie `0x01`, baud `38400`. Ustaw w `src/config.h`.

### LCD 2004A (I2C)

| LCD | ESP32-S3 |
| --- | --- |
| SDA | GPIO 8 |
| SCL | GPIO 9 |
| VCC | 5V |
| GND | GND |

### Enkoder (UI)

| Enkoder | ESP32-S3 |
| --- | --- |
| CLK | GPIO 10 |
| DT | GPIO 11 |
| SW | GPIO 12 |
| + | 3.3V |
| GND | GND |

### Czujnik Gauss AH49HZ3

| AH49HZ3 | ESP32-S3 |
| --- | --- |
| VCC | 3.3V |
| GND | GND |
| VOUT | GPIO 1 (ADC1) |

Kalibracja zera: po ~1.5 s od startu, gdy pole < 25 G przez 0.8 s, czujnik sam ustawia offset
(nie trzeba trzymać stałego 1.65 V w kodzie). Po zdjęciu magnesu offset jest ponownie
aktualizowany. Stałe w `src/config.h` (`GAUSS_WARMUP_MS`, `GAUSS_CALIB_MAX_G`, itd.).

## Zliczanie obrotów (z sterownika silnika)

Obroty liczone są wyłącznie z wbudowanego enkodera SERVO42 przez RS485:

| Komenda | Opis |
| --- | --- |
| `FA addr 31 CRC` | Odczyt wartości enkodera (addition, int48) |
| — | Jedna rewolucja = `0x4000` (16384) impulsów |
| — | CW: wartość rośnie o `0x4000`/obrót |
| — | CCW: wartość maleje o `0x4000`/obrót |

Implementacja: `servo42.cpp` → `readEncoderAddition()`, `rev_counter.cpp`.

## Kompilacja

```bash
pio run -e esp32-s3-devkitc-1
pio run -e esp32-s3-devkitc-1 -t upload
```

Konfiguracja pinów: `src/config.h`.

## Komendy RS485 (używane w firmware)

| Komenda | Funkcja |
| --- | --- |
| `0x82` | Ustawienie trybu SR_CLOSE (closed-loop) |
| `0x60` | Zapis ustawień |
| `0xF6` | Sterowanie prędkością (RPM + acc) |
| `0x32` | Odczyt bieżącego RPM |
| `0x31` | Odczyt pozycji enkodera (zliczanie obrotów) |
| `0x37` | Odczyt statusu |
