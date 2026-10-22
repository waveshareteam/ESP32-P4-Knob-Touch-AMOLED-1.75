# Bluetooth A2DP Sink

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Initializes the external-connectivity transport and exposes a discoverable Bluetooth A2DP sink. Incoming audio is sent to the board codec, while the AMOLED UI shows the received title and lyrics text when provided.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board
- A Bluetooth audio source for pairing and playback

## How to Run

Use the ESP-IDF version listed in the [repository README](../../../README.md), then run:

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

After startup, the device is discoverable to a Bluetooth audio source. The display initially shows waiting messages and updates its text through the A2DP metadata callbacks.

## Troubleshooting

Audio remains muted until the A2DP audio stream enters its started state.
