# Bluetooth Scan

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Performs Bluetooth Low Energy GAP scanning through the external connectivity component. It summarizes nearby advertisers and does not connect, pair, or discover services.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Run the following command from this example directory. Use the ESP-IDF version listed in the repository README.

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

At the end of each scan window, the serial monitor prints an aggregate summary and up to ten advertisers sorted by RSSI.

## Troubleshooting

This is a BLE GAP scanner only. Scan startup or transport errors are logged and the example schedules another scan attempt.
