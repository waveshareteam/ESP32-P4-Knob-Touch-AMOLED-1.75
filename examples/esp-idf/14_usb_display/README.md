# 14 USB Display

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

This example exposes the board as a 466 × 466 JPEG USB display for the Espressif Windows USB-display driver. It also sends the board touch input back to the host as a HID touch screen.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board
- A USB-C cable connected to the adapter-board USB 2.0 port and a Windows 10 or Windows 11 PC

## How to Run

Build and flash with the ESP-IDF version listed in the repository README, then connect the board to the PC and install the compatible Espressif USB-display driver.

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

The log reports that the board is ready at 466 × 466 and reports USB enumeration after the PC configures it. The PC driver can send JPEG frames for the full screen or changed rectangles and receives touch reports from the board.

## Troubleshooting

This example requires the high-speed USB 2.0 port on the adapter board; it cannot use the USB-to-serial connection. If the PC does not enumerate the display, reconnect the cable with the plug flipped.
