# USB Display

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Receives 466 × 466 JPEG frames from Espressif's Windows USB display driver over a high-speed vendor interface and returns touch events as a HID touch screen.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board
- The adapter board's high-speed USB Type-C connection and a Windows 10 or Windows 11 computer

## How to Run

Open `14_usb_display.ino`, select the repository's ESP32-P4 board configuration, upload it, then connect the adapter's high-speed USB port and install the compatible Espressif USB display driver on Windows.

## Expected Result

Windows recognizes a 466 × 466 JPEG display device and receives touch reports from the board. The device accepts full-screen and partial JPEG updates in the same protocol as the ESP-IDF example.

## Troubleshooting

Use the adapter's high-speed USB port, not the USB-to-UART connection. The Windows driver must be compatible with the USB vendor string exposed by this example.
