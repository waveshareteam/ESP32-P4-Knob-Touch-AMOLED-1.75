# ESP-Brookesia Factory Application

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Starts the 466 × 466 ESP-Brookesia launcher with display, media, wireless, motion, settings, USB Screen and Xiaozhi applications. Media uses a FAT-formatted microSD card as follows:

| Folder | App | Files |
| --- | --- | --- |
| `music` | Music player | MP3, WAV and optional matching `.lrc` lyrics |
| `videos` | Video player | MP4 or AVI with MJPEG video and AAC, MP3 or PCM audio |
| `pictures` | Gallery | JPEG and PNG |
| `books` | Reader | UTF-8, GBK or UTF-16 TXT |
| `fonts` | Reader | Optional TTF fallback font |

Xiaozhi connects to [xiaozhi.me](https://xiaozhi.me): connect Wi-Fi, then add the first-use six-digit code in the service console. USB Screen and **Settings > Storage** are mutually exclusive; each USB function is active only while its corresponding application is open.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board
- A FAT-formatted microSD card for media applications
- A Wi-Fi network for Xiaozhi and a USB host for USB Screen, when used

## How to Run

Use the factory ESP-IDF version listed in the [repository README](../../README.md), then run:

```bash
idf.py set-target esp32p4 build flash monitor
```

If the partition table changes, use `idf.py flash` so every partition is updated.

## Expected Result

The display opens the Brookesia launcher and its status bar. Open an application to use its matching board function or microSD content.

## Troubleshooting

If board artifacts are missing, run `idf.py bmgr -b esp32_p4_knob_touch_amoled_1_75` once and build again. Keep this project's `spiffs` directory because it is mounted at startup.
