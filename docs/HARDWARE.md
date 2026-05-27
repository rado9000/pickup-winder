# Pickup winder

## Napęd

| Sygnał | Pico | Uwagi |
| --- | --- | --- |
| STEP | GP2 | **PWM** sprzętowy (~26–40 kHz przy 1000–1500 RPM, 1/8) |
| DIR | GP3 | GPIO |
| EN | GP10 | GPIO, aktywny LOW |
| GND | wspólna | |

**TMC2209 bez UART:** MS1/MS2 = 1/8, **SpreadCycle** (jumper), VREF potencjometrem (~1–1,2 A).

## Rampa (firmware)

- Start **60 RPM**
- Co **10 ms**: **+2 RPM** (tylko nowa częstotliwość PWM — bez przerw w STEP)
- `MAX_RPM` 1500

Dostrajanie w `config.h`: `MOTOR_RPM_RAMP_STEP`, `MOTOR_RPM_RAMP_INTERVAL_MS`, `MOTOR_RPM_RAMP_START`.

## Build

```bash
pio run -e pico
```

`.pio/build/pico/firmware.uf2`
