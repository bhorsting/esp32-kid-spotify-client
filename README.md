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

- **Play** — now playing, huge ⏮ ▶ ⏭ + heart  
- **Lists** — parent-curated playlists  
- **♥** — favourites  
- **Find** — simplified paged keyboard (A–I / J–R / S–Z / 123), typeahead after 2 letters  
- **⚙** — volume (capped) + parent PIN (`2468` default) for Wi‑Fi / account  

## Repo status

UI + player **stub** (demo tracks) compile-ready. Next hardware steps:

1. Port Waveshare ST77916 QSPI + CST816 + TCA9554 into `src/board/`  
2. Real I2S sink in `board_audio_feed_pcm`  
3. Integrate [cspot](https://github.com/feelfreelinux/cspot) + Web API  
4. Captive-portal / device-login for parent Spotify + Wi‑Fi  

## Build

```bash
# PlatformIO CLI
pio run -e waveshare_s3_1_85
pio run -e waveshare_s3_1_85 -t upload
pio device monitor
```

Board not required to keep developing UI against the stub display/audio backends.

## Layout

```
include/board_pins.h     Waveshare pin map
include/ui/              UI API + screens + kid keyboard
src/ui/                  LVGL screens
src/spotify/player_stub  Mock player (swap for cspot)
src/board/*_stub         Display/audio placeholders
docs/ARCHITECTURE.md     Integration plan
```
