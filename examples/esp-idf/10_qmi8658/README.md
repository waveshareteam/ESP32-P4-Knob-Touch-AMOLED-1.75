# 10 QMI8658

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

This example samples the QMI8658 accelerometer and plots X, Y, and Z acceleration on the LVGL display. It probes the supported device addresses, verifies the device ID, and displays the active address and interrupt levels.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Run this command from this example directory using the ESP-IDF version listed in the repository README:

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

When the IMU is detected, the chart and numeric X, Y, and Z readings update as the board is moved. The display reports an error if probing, configuration, or accelerometer reads fail.

## Troubleshooting

The example accepts QMI8658 addresses `0x6B` and `0x6A`; it expects `WHO_AM_I` to be `0x05`. Read errors cause the worker to reopen and configure the device again.
