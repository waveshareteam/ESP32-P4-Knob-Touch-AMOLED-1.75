# Knob Direction Switches

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Reports debounced active-low clockwise and counter-clockwise switch pulses from the knob, with a separate count for each direction.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Open `07_limit_switches.ino` in Arduino IDE, select the ESP32-P4 board with 32 MB Flash, PSRAM enabled, and USB CDC on boot, then upload it and open the serial monitor at 115200 baud.

## Expected Result

Rotating the knob produces CW or CCW trigger messages after a 2 ms debounce interval and increments the matching direction count.

## Troubleshooting

The two inputs are independent active-low direction switches, so do not configure them as a quadrature encoder.
