# Wi-Fi Scan

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Scans nearby Wi-Fi access points through the board's wireless coprocessor and prints up to ten SSID, RSSI, channel and authentication records.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Open `03_wifi.ino` in Arduino IDE and use the board settings in the repository README. Copy the shared `KnobWireless` library with the sketch; an ESP-IDF installation is not required.

## Expected Result

At 115200 baud, the serial monitor reports a successful scan and the access-point records. Hidden networks appear as `<hidden>`.

## Troubleshooting

Use the official Arduino-ESP32 3.3.12 core and keep the complete `KnobWireless` library, including its precompiled ESP32-P4 archive.
