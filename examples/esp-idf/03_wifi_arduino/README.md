# Wi-Fi Scan with Arduino Component

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Runs the existing ESP-IDF Wi-Fi scan through the board's ESP32 wireless controller while Arduino `setup()` and `loop()` provide the application entry point.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Build this ESP-IDF project with Arduino-ESP32 3.3.12 supplied as an IDF component, then open the serial monitor at 115200 baud.

## Expected Result

The log prints the controller MAC address and the strongest visible access points after the scan completes.

## Troubleshooting

This is an ESP-IDF project, not an Arduino IDE sketch; use the component manifest to resolve Arduino before building.
