# SD Card

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Mounts an SD card, writes a temporary file, reads it back for verification, deletes the file, and unmounts the card. The status is shown on an LVGL page and in the serial monitor.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board
- A compatible SD card

## How to Run

Insert an SD card, then run the following command from this example directory. Use the ESP-IDF version listed in the repository README.

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

The example reports the card information and a successful write/read/delete round trip, then shows a passed status after unmounting the card.

## Troubleshooting

This example modifies a temporary file on the inserted card. Mount or verification failures are reported in the serial monitor and on the display.
