# Pickup winder – sprzęt i kalibracja

## Napęd STEP (jak w starym, działającym softie)

- **analogWriteFreq** na GP2 + niski duty (`STEP_PWM_DUTY`) — ten sam zestaw co przy 1000 RPM wcześniej
- **Bez FastAccelStepper** — biblioteka powodowała rezonans i zatrzymania przy 400–500 RPM
- **Rampa schodkowa:** 50 → 100 → … → cel RPM, na **każdym progu postój 2,5 s** (bez gwałtu na „końcu rampy”)

## Silnik 17HS4401 + TMC2208

| | |
|---|---|
| 200 kroków/obrót, MS 1/8 | `MICROSTEP 8` |
| StealthChop | cicho do ~400 RPM |
| Powyżej 500 RPM | jeśli gubi kroki: **VREF** ↑ lub test **SpreadCycle** |
| VREF | ~1,0–1,2 A RMS/faza (cewka 1,5 Ω) |

## Dlaczego drgało przy 400 RPM „na końcu”?

Koniec segmentu rampy = nagła zmiana przyspieszenia (FAS + S-curve). Teraz: **stała częstotliwość + hold** na 400, 450, 500… przed dalszym wzrostem.

## Dostrajanie (`config.h`)

- `RAMP_HOLD_MS` — dłuższy postój = spokojniej
- `RAMP_SEG_MS_PER_RPM` — wolniejszy dojazd między progami
- `RAMP_LADDER_TABLE` — progi RPM

## Kompilacja

```bash
pio run -e pico
```

Wgraj `.pio/build/pico/firmware.uf2` (BOOTSEL).
