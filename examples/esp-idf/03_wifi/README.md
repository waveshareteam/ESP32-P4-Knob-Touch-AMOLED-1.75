# Wi-Fi Scan

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Scans nearby Wi-Fi access points through the board's external connectivity component. It does not require credentials and does not connect to a network.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Run the following command from this example directory. Use the ESP-IDF version listed in the repository README.

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

The serial monitor prints one scan summary and up to ten access points ordered by signal strength. A result with no visible access points is valid.

## Troubleshooting

The example waits for the external connectivity component to initialize before starting the scan; initialization or scan errors are reported in the serial monitor.
