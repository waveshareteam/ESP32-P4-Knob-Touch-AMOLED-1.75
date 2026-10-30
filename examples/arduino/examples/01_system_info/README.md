# System Information

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Prints the ESP32-P4 chip model and revision, CPU frequency, Flash, PSRAM, and available heap information through the serial monitor.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Open `01_system_info.ino` with Arduino IDE, select the board configuration listed in the repository README, then upload it and open the serial monitor at 115200 baud.

## Expected Result

The serial monitor prints a system-information report after boot and every five seconds, including nonzero Flash and PSRAM capacities when the board configuration enables PSRAM.

## Troubleshooting

If the serial monitor shows zero PSRAM, check that the selected Arduino board configuration enables the board's PSRAM.
