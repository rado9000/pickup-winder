# Wgrywanie — Windows, ESP32-S3 N16R8

1. Cursor/VS Code + rozszerzenie **PlatformIO IDE**
2. Otwórz folder projektu (z `platformio.ini`)
3. USB → natywny port USB płytki S3
4. Środowisko: **`esp32-s3-n16r8`**
5. **Upload** (strzałka)

```bash
pio run -e esp32-s3-n16r8 -t upload --upload-port COMx
pio device monitor -b 115200
```

Jeśli nie wgrywa: BOOT+RESET, sprawdź COM w Menedżerze urządzeń.
