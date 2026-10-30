# PCF85063 RTC

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Reads the PCF85063 time and status once per second, validates its BCD date, and reports the interrupt level. The sketch never writes the RTC time.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Open `09_rtc.ino`, select the ESP32-P4 board configuration documented in the repository README, then upload and open the serial monitor at 115200 baud.

## Expected Result

The serial monitor prints the current RTC date, time, oscillator-stop state, and interrupt level.

## Troubleshooting

Invalid BCD data and I2C read failures are reported before the next read attempt.
