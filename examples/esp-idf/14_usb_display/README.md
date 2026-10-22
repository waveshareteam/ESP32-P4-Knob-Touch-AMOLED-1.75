# 14 USB Display

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

This project is a reserved placeholder for a future USB display example. The current implementation only writes `RESERVED`, `NOT IMPLEMENTED`, and `UNTESTED` to the log, then waits.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Run this command from this example directory using the ESP-IDF version listed in the repository README:

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

The serial monitor reports that the example is reserved and not implemented. It does not initialize a USB role, a USB display protocol, or an external display device.

## Troubleshooting

This project has no USB-display test to troubleshoot because USB display support is not implemented.
