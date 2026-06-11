# Wgrywanie firmware — Windows, ESP32-S3 N16R8

## Wymagania

1. [Cursor](https://cursor.com) lub VS Code
2. Rozszerzenie **PlatformIO IDE**
3. Kabel USB **danych** (nie tylko ładowania)
4. Płytka **ESP32-S3 N16R8** (16 MB flash, 8 MB PSRAM)

## Krok po kroku

1. Sklonuj / otwórz folder `pickup-winder` w Cursorze.
2. Podłącz płytkę do PC kablem USB do gniazda **USB** na DevKicie (natywny USB S3).
3. W **Menedżerze urządzeń** sprawdź port COM, np.:
   - `USB JTAG/serial debug unit (COM7)`
   - czasem `Silicon Labs CP210x` lub `CH343` — zależy od wersji płytki
4. Na dolnym pasku PlatformIO wybierz środowisko **`esp32-s3-n16r8`**.
5. Kliknij **Upload** (strzałka →) — odpowiednik „Wgraj” z Arduino IDE.
6. Po wgraniu: **Monitor** (115200 baud) — opcjonalnie, do debugu.

## Jeśli nie wgrywa

1. **BOOT + RESET**: przytrzymaj **BOOT**, kliknij **RESET**, puść **BOOT**, od razu Upload.
2. **Inny port USB** na płytce — niektóre mają USB-UART i USB-native; do wgrywania użyj **USB-native (S3)**.
3. **Ręczny port** w terminalu:
   ```bash
   pio run -e esp32-s3-n16r8 -t upload --upload-port COM7
   ```
   (`COM7` zamień na swój z Menedżera urządzeń)
4. **Sterownik**: jeśli płytki nie widać — zainstaluj sterownik z [Espressif](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/windows-setup.html) lub od producenta (CH343/CP2102).

## Terminal zamiast IDE

```bash
pip install platformio
cd pickup-winder
pio run -e esp32-s3-n16r8 -t upload
pio device monitor -b 115200
```

## Po wgraniu

Na LCD powinno być `Pickup winder` / `ESP32-S3 / SERVO42`. Jeśli ekran pusty — sprawdź I2C LCD (SDA/SCL) i zasilanie.
