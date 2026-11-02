# Limit Switches

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Monitors two independent limit-switch inputs and shows their levels and trigger counts on an LVGL page. The default configuration confirms GPIO levels 2 ms after the final edge; the project also provides polling, input-only, and PCNT diagnostic modes.

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

The default mode counts a falling edge only after the input remains low for its configured settle time. Polling and PCNT modes are diagnostic alternatives and have different pulse-counting behavior.
