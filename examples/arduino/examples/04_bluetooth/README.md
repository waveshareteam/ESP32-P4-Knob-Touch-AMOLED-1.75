# Bluetooth LE Scan

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Scans nearby Bluetooth LE advertisements for ten seconds and prints up to ten strongest devices, including address, RSSI, and advertising name when available.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Open `04_bluetooth.ino` in Arduino IDE or Arduino CLI. Use the board settings in the repository README and copy the shared `KnobWireless` library with the sketch; no ESP-IDF installation is needed.

## Expected Result

At 115200 baud, the serial monitor reports the received advertisement and unique-device counts after ten seconds, followed by the strongest devices. Devices without an advertising name display as `<unnamed>`.

## Troubleshooting

Use the official Arduino-ESP32 3.3.12 core and keep the complete `KnobWireless` library, including its precompiled ESP32-P4 archive. This example scans BLE advertisements only; it does not provide Classic Bluetooth serial or GATT functions.
