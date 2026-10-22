# 08 Motor Drive

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

This example drives the board motor or haptic output with a press-and-hold LVGL button. It uses a guarded forward pulse and returns both control lines to the idle state on release, timeout, or an error.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Run this command from this example directory using the ESP-IDF version listed in the repository README:

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

The display shows a **VIBRATE** button and the current motor state. Holding the button starts a short vibration; release or the configured guard timeout stops it and returns the UI to `IDLE`.

## Troubleshooting

The maximum on-time is set by `CONFIG_MOTOR08_MAX_ON_MS`; the source limits it to 500 ms. If GPIO or timer control fails, the UI reports `FAULT` and attempts to stop the motor.
