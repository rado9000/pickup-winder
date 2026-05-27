# Pickup winder – 17HS4401 + MKS TMC2209 V2.0

## Zasilanie i mechanika

| | |
|---|---|
| **VM (silnik)** | **24 V** zalecane (12 V = słaby moment przy 500–1000 RPM) |
| **VDD (logika)** | 3,3 V (Pico) — moduł MKS ma regulator |
| **Microstep** | **1/8** przez UART (`TMC_MICROSTEPS 8`) — MS1/MS2 mogą być ignorowane gdy UART OK |
| **Prąd** | `TMC_RUN_CURRENT_MA` 1100 mA (17HS4401, 1,5 Ω); VREF nieużywany gdy UART steruje prądem |

## UART – okablowanie Pico → MKS TMC2209 V2.0

Tryb UART na module MKS (mostek/jumper **UART** według instrukcji MKS).

| TMC2209 | Pico | Uwagi |
| --- | --- | --- |
| **PDN_UART** | **GP5 (RX)** | bezpośrednio |
| **PDN_UART** | **GP4 (TX)** | przez rezystor **1 kΩ** |
| **GND** | **GND** | wspólna masa |
| STEP | GP2 | bez zmian |
| DIR | GP3 | bez zmian |
| EN | GP10 | bez zmian |

Adres drivera: `TMC_DRIVER_ADDRESS` (domyślnie **0** — zgodnie z MS1/MS2 na module).

Przy starcie na LCD: **„TMC2209 UART OK”** lub **„TMC UART: check”** (brak połączenia / zły adres).

## Co robi firmware (TMC2209)

| Funkcja | Opis |
| --- | --- |
| **StealthChop2** | Nisko: cicho, płynnie (do ~450 RPM) |
| **SpreadCycle** | Automatycznie powyżej progu (`TPWMTHRS`) — stabilniej przy wysokich RPM |
| **Interpolacja** | 1/8 → 256 microstepów (płynniejszy ruch) |
| **Prąd** | Wyższy przy 500+ / 800+ RPM (`TMC_RUN_CURRENT_*`) |
| **STEP** | FastAccelStepper (PIO), jedna rampa w bibliotece |

Próg Stealth→Spread: `TMC_STEALTH_TO_SPREAD_RPM` (domyślnie **450**).

## STEP / rampa

- **Nie** mieszaj własnej rampy w `loop()` z FAS — tylko `setSpeedInHz` + `setAcceleration` + `runForward()` przy starcie.
- LCD odświeżane co **250 ms** w nawijaniu.

## Dostrajanie (`src/config.h`)

| Stała | Domyślnie |
| --- | --- |
| `TMC_RUN_CURRENT_MA` | 1100 |
| `TMC_STEALTH_TO_SPREAD_RPM` | 450 |
| `FAS_ACCEL_DEFAULT` | 8000 |
| `FAS_ACCEL_HIGH_RPM` | 5000 |
| `USE_TMC2209_UART` | 1 (0 = tylko Step/Dir jak dawniej) |

## Kompilacja

```bash
pio run -e pico
```

Wgraj `.pio/build/pico/firmware.uf2`.
