# 13 Speaker Output

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

This example initializes the on-board ES8389 playback path and plays a short, low-volume 440 Hz PCM tone every two seconds.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Run this command from this example directory using the ESP-IDF version listed in the repository README:

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

The serial monitor reports each playback cycle. The connected speaker outputs a brief tone followed by a quiet interval.

## Troubleshooting

If there is no tone, confirm that the board is powered from USB and keep the speaker volume at the example's low default before changing it in source.
