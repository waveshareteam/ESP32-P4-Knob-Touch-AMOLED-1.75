# AXP2101 Status

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Reads and prints AXP2101 power, battery, charging, and temperature status. It does not change PMU, charging, or regulator settings.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Open `05_axp2101.ino`, select the ESP32-P4 board configuration documented in the repository README, then upload and open the serial monitor at 115200 baud.

## Expected Result

The serial monitor updates once per second with the PMU ID and enabled ADC measurements.

## Troubleshooting

The sketch prints an I2C error and retries if the PMU cannot be read. A disabled PMU ADC channel is shown as `N/A(disabled)` and is never enabled by the sketch.
