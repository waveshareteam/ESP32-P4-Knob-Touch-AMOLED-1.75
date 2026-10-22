# Limit Switches

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Monitors two independent limit-switch inputs and shows their levels and trigger counts on an LVGL page. The default configuration polls the inputs; the project also provides diagnostic modes for input-only and PCNT counting.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Run the following command from this example directory. Use the ESP-IDF version listed in the repository README.

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

Operating either switch updates its independent HIGH/LOW state and count on the display and in the serial monitor.

## Troubleshooting

The default polling mode can miss pulses shorter than its sampling interval. Select a different diagnostic mode only when its behavior matches the connected hardware.
