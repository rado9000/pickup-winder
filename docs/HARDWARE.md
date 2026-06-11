# Pickup winder – ESP32-S3 + MKS SERVO42ES

## Platforma

| Element | Model |
| --- | --- |
| MCU | ESP32-S3 DevKitC-1 |
| Silnik | Makerbase MKS SERVO42ES NEMA17 (RS485) |
| Wyświetlacz | LCD 2004A (HD44780, I2C backpack) |
| Enkoder | Rotary encoder z przyciskiem |
| Czujnik obrotów | Enkoder wbudowany w SERVO42 (domyślnie) lub A3144 |
| Czujnik Gauss | AH49HZ3 (analogowy Hall) |

Manual silnika: [MKS SERVO42&57ES RS485 User Manual V1.0.1](MKS-SERVO42ES-57ES_RS485_User_Manual_V1.0.1.pdf)  
Źródło: [makerbase-motor/MKS-SERVO42ES-57ES](https://github.com/makerbase-motor/MKS-SERVO42ES-57ES/blob/master/User%20Manual/MKS%20SERVO42%2657ES_RS485%20User%20Manual%20V1.0.1.pdf)

## Funkcje (jak w wersji RP2040)

- **Tryb Manual** – edycja zwojów (5 cyfr), RPM (4 cyfry), kierunku CW/CCW
- **Presets** – zapis do NVS (max 32), long-press = powrót
- **Nawijanie** – odliczanie 3..0, pauza/wznowienie, soft stop przez RS485
- **Gauss meter** – auto-aktywacja przy polu ≥ 50 G

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

### Enkoder

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

### A3144 (opcjonalnie, gdy `USE_MOTOR_ENCODER = 0`)

| A3144 | ESP32-S3 |
| --- | --- |
| VCC | 3.3V |
| GND | GND |
| OUT | GPIO 13 |

## Zliczanie obrotów

Domyślnie (`USE_MOTOR_ENCODER = 1`) obroty liczone są z enkodera silnika przez RS485 (komenda `0x31`, 0x4000 impulsów/obrót). Alternatywnie ustaw `USE_MOTOR_ENCODER = 0` i podłącz A3144 na GPIO 13.

## Kompilacja

```bash
pio run -e esp32-s3-devkitc-1
pio run -e esp32-s3-devkitc-1 -t upload
```

Konfiguracja pinów: `src/config.h`.

## Test RS485 (bazowy kod)

Kod testowy silnika (sprawdzony na hardware) jest wbudowany w `src/servo42.cpp`. Sterowanie prędkością: komenda `0xF6`, odczyt RPM: `0x32`, tryb closed-loop: `0x82`.
