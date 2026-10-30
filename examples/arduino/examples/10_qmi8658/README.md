# QMI8658 Accelerometer

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Initializes the QMI8658 accelerometer at ±8 g and 250 Hz, then prints X, Y, and Z acceleration and both interrupt levels.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Open `10_qmi8658.ino`, select the ESP32-P4 board configuration documented in the repository README, then upload and open the serial monitor at 115200 baud.

## Expected Result

The serial monitor reports the detected IMU address and updates acceleration values as the board moves.

## Troubleshooting

The sketch tries addresses `0x6B` and `0x6A`, expects device ID `0x05`, and retries after an I2C error.
