# Display and Touch

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Initializes the 466 × 466 CO5300 MIPI-DSI display, shows red, green, and blue screens, then opens a CST9217 touch drawing page with a Clear button.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Open `11_display_touch.ino`, select the board configuration in the repository README, upload it, and open the serial monitor at 115200 baud.

## Expected Result

The display changes through three primary colors and then shows a white touch page. Press and drag to draw continuous lines across the full touch surface, including its edges; Clear erases the canvas.

## Troubleshooting

Enable PSRAM in the Arduino board options. The MIPI display requires the shared board display library included in this repository.
