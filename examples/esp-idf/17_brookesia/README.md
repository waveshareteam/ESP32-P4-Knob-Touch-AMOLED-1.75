# ESP-Brookesia Phone UI

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Starts a 466 x 466 ESP-Brookesia phone-style UI on the AMOLED display. The application registers calculator, drawing, spectrum-analyzer, music-player, and settings apps, and refreshes the status bar clock and Wi-Fi state.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Use the ESP-IDF version listed in the [repository README](../../../README.md), then run:

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

The display starts the phone-style interface with its installed app launcher and a periodically updated status bar.

## Troubleshooting

The project mounts its bundled SPIFFS image at startup; do not remove the `spiffs` directory from this example.
