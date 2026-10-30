# Buttons

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Reports debounced press and release transitions for the BOOT and PWR buttons, and maintains an independent press count for each button. BOOT is active low, while PWR is active high after its on-board inverter.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Open `02_buttons.ino` with Arduino IDE, select the board configuration listed in the repository README, then upload it and open the serial monitor at 115200 baud.

## Expected Result

The serial monitor reports each button's initial state. Pressing and releasing either button prints its transition and updated press count after a 30 ms debounce interval.

## Troubleshooting

BOOT uses an internal pull-up and is active low. PWR is active high after its on-board inverter; if it remains reported as pressed, check that it is not held down during startup.
