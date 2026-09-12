# Architecture

## Goal

Standalone Spotify **player** on ESP32-S3-Touch-LCD-1.85 for a ~6-year-old: sound from the onboard speaker, touch UI on the 360×360 round display, account configured by a parent.

## Audio path

```
Spotify servers ──cspot──► PCM 44.1 kHz 16-bit stereo
                              │
                              ▼
                     board_audio_feed_pcm()
                              │
                              ▼
                     I2S → PCM5101 → amp → speaker
```

Control path (play/pause/next/search/like) goes through `spotify/player.h`. Today that is `player_stub.cpp`. Production impl:

1. **cspot** session — login, stream, volume, track metadata callbacks  
2. **Web API** — search, saved tracks, playlist contents; start playback targeting this Connect device once registered  
3. NVS — refresh credentials, Wi‑Fi, parent PIN, volume cap, curated playlist IDs  

## Display path

```
LVGL ──flush──► ST77916 (QSPI)
CST816 ──indev──► LVGL pointer
TCA9554 ──LCD_RST / TP_RST / etc.
```

Stub flush/indev in `display_stub.c` until Waveshare drivers are ported (their Arduino `LVGL_Arduino` demo is the reference).

## Kid keyboard

`kid_keyboard` — 9 large letter keys + page cycle (ABC → JKL → STU → 123), space, backspace, GO. Search fires after ≥2 characters (`SEARCH_MIN_CHARS`). Fits inside ~320px safe diameter.

## Parent gate

Settings PIN (`PARENT_PIN_DEFAULT`) unlocks Wi‑Fi and Spotify account flows (to be added). Child volume capped at `VOLUME_MAX_CHILD`.

## Suggested integration order

1. Flash Waveshare LVGL demo — prove LCD + touch  
2. Replace stub display with that bring-up; keep Marten UI  
3. I2S tone test on PCM5101 pins  
4. cspot as library + `PCM5102AudioSink`-style sink with our pins  
5. Wire `player_*` to cspot + Web API  
6. Parent provisioning UI  
