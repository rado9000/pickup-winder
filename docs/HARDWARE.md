# Pickup winder – hardware

## Silnik

- **17HS4401** NEMA17, 200 kroków/obrót
- **TMC2209** MS1=MS2=LOW → **1/8** microstep (`MICROSTEP 8`)
- Zasilanie silnika: **24 V**, wspólna masa z Pico

## Pico → TMC2209

| Sygnał | GPIO |
| --- | --- |
| STEP | GP2 |
| DIR | GP3 |
| EN | GP10 |
| I2C LCD | GP0 SDA, GP1 SCL |

Sterowanie tylko **STEP/DIR/EN** (bez UART w firmware — stabilna jazda).

## Rampa

Jedna stała w `config.h`: `MOTOR_ACCEL_STEPS_S2` (domyślnie 4000 kroków/s²).  
Przy 1500 RPM (~40 kHz kroków) rampa trwa ok. **8 s** — płynny start pod nawijanie przetworników.

Maks. obroty w menu: `MAX_RPM` (1500).

## Kompilacja

```bash
pio run -e pico
```

Wgraj `.pio/build/pico/firmware.uf2`.
