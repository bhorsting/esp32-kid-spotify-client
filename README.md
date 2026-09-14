# Marten

Kid-friendly **Spotify player** for the [Waveshare ESP32-S3-Touch-LCD-1.85](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.85) (360×360 round touch LCD, onboard speaker via PCM5101). Local audio only — not a remote for a phone.

## Stack

| Layer | Choice |
|--------|--------|
| MCU / board | ESP32-S3R8, 16MB flash, 8MB OPI PSRAM |
| UI | LVGL 8.3 — large targets, round-safe layout |
| Audio | **cspot** → I2S PCM5101 (DIN 47, LRCK 38, BCK 48) |
| Auth | Spotify **Premium**; credentials in NVS after parent setup |
| Search / library | Spotify Web API (likes, search) + playback on this device |

cspot is unofficial and outside Spotify’s ToS; streams can break when Spotify changes protocols. Requires Premium.

## Child UX

- **Ring** — center = now playing cover; 5 slices = next tracks in curated playlist  
- Tap a slice to jump; triple-tap center → Settings (Wi‑Fi / playlist portal)  
- Parents share one playlist via Marten-Setup portal  

## Display / touch

LCD bring-up is a port of the official Waveshare ESP-IDF drivers under `src/board/waveshare/`: **ST77916** (QSPI), **CST816** touch, and **TCA9554** EXIO (reset / expanders), wired through `src/board/display.c` into LVGL 8.3. Cover art via `esp_jpeg` + PSRAM.

## Repo status

Ring UI + cspot Connect + Web API curated playlist. Next:

1. Persist Spotify LoginBlob in NVS  
2. Volume hardware / gesture  
3. Faster art cache  

## Build

```bash
# PlatformIO CLI
pio run -e waveshare_s3_1_85
pio run -e waveshare_s3_1_85 -t upload
pio device monitor
```

## Layout

```
include/board_pins.h           Waveshare pin map
include/ui/                    UI API + cover art + screens
src/ui/screens/ring.c          Main ring UI
src/ui/cover_art.c             JPEG download/decode
src/spotify/player_stub.cpp    Curated queue + Connect
components/spotify_connect/    cspot + Web API
docs/ARCHITECTURE.md           Integration plan
```
