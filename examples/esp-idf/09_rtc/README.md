# 09 RTC

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

This example reads the PCF85063 real-time clock once per second and presents its time and status on the LVGL display. It validates the received BCD time data and does not write a time value to the RTC.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Run this command from this example directory using the ESP-IDF version listed in the repository README:

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

When the RTC is available, the display updates the date and time every second and shows its status. Invalid data or a read failure clears the clock display and shows an error instead of stale time.

## Troubleshooting

This is a read-only example: it does not set the RTC time. The status view identifies invalid oscillator or BCD data and I2C read failures.
