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

Implemented under `src/board/waveshare/` (ESP-IDF Waveshare port) and `src/board/display.c` (LVGL flush/indev + tick).

## Kid keyboard

`kid_keyboard` — 9 large letter keys + page cycle (ABC → JKL → STU → 123), space, backspace, GO. Search fires after ≥2 characters (`SEARCH_MIN_CHARS`). Fits inside ~320px safe diameter.

## Parent gate

Settings PIN (`PARENT_PIN_DEFAULT`) unlocks Wi‑Fi and Spotify account flows (to be added). Child volume capped at `VOLUME_MAX_CHILD`.

## Suggested integration order

1. Flash Marten — prove LCD + touch with real UI ✅  
2. I2S on PCM5101 pins ✅ (`components/board_audio`)  
3. cspot + Zeroconf Connect sink ✅  
4. Wire transport to cspot ✅  
5. Parent Wi‑Fi portal ✅ (`Marten-Setup`)  
6. Ring UI + cover art + curated playlist ✅  
7. Persist Spotify LoginBlob in NVS — next  
8. Volume gesture / hardware — later  
