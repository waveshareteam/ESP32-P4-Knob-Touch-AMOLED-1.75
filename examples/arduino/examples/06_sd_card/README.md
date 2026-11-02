# SD Card

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Mounts an inserted SD card over one SDMMC data line at 10 MHz, reports its capacity, verifies one new temporary file, removes that file, and unmounts the card.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board
- A compatible FAT-formatted microSD card

## How to Run

Open `06_sd_card.ino`, select **ESP32P4 Dev Module** with 32 MB Flash and PSRAM enabled, upload it, and open the serial monitor at 115200 baud.

## Expected Result

The serial monitor reports the 1-bit 10 MHz configuration, card and filesystem capacity, then reports a successful write/read/delete verification and unmount.

## Troubleshooting

This example does not format the card or change existing files. If writing fails, it stops without deleting the named temporary file; check the card before running it again.
