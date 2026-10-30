# Vibration Motor

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Drives the DRV8837 vibration motor for one bounded 400 ms pulse through a serial command, with an immediate stop command.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Open `08_motor_drive.ino` in Arduino IDE, select the ESP32-P4 board with 32 MB Flash, PSRAM enabled, and USB CDC on boot, then upload it and open the serial monitor at 115200 baud. Send `v` to start one pulse or `s` to stop it.

## Expected Result

The motor vibrates after `v`, then stops after 400 ms. Sending another `v` during a pulse does not extend its timeout.

## Troubleshooting

This sketch intentionally drives only the board's vibration motor. It does not provide speed control or force feedback.
