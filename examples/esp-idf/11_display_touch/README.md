# 11 Display and Touch

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

This example checks the onboard display and touch path with red, green, and blue full-screen stages, followed by a touch drawing page. The drawing page adds dots at touch positions and provides a button to clear them.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Run this command from this example directory using the ESP-IDF version listed in the repository README:

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

The display passes through red, green, and blue screens, then opens a white touch page. Touching the page draws black dots; selecting **Clear** removes the dots and resets the count.

## Troubleshooting

Touching the color screen or pressing the BOOT button advances to the next color stage. The drawing page keeps a bounded list of dots and removes the oldest one when full.
