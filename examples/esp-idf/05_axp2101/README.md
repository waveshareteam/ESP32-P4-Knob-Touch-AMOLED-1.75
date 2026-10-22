# AXP2101 Status

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Displays read-only AXP2101 power-management status on an LVGL page. It reads available power and battery information without changing power, charging, or regulator settings.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Run the following command from this example directory. Use the ESP-IDF version listed in the repository README.

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

The display updates the AXP2101 online state and available power, battery, temperature, and charging information; unavailable readings are shown as `N/A`.

## Troubleshooting

I2C or PMU read errors appear on the display and in the serial monitor, after which the example retries the connection.
