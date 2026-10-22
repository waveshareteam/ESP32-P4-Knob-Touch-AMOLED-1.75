# System Information

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Reads the ESP32-P4 flash, PSRAM, and internal-heap capacity information. It does not initialize the display or external peripherals.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Run the following command from this example directory. Use the ESP-IDF version listed in the repository README.

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

The serial monitor reports flash, PSRAM, and internal heap values, then prints `PASS` when all required non-zero values are available.

## Troubleshooting

If PSRAM is enabled but fails to initialize, the example reports `FAIL` instead of treating a zero capacity as valid.
