# A2DP Sink with Arduino Component

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Runs the existing Bluetooth Classic A2DP sink, audio playback, and display interface while Arduino `setup()` and `loop()` provide the application entry point.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board
- Speaker adapter board

## How to Run

Build this ESP-IDF project with Arduino-ESP32 3.3.12 supplied as an IDF component, then pair a Bluetooth audio source with the displayed device name.

## Expected Result

The board accepts a Classic Bluetooth audio connection, plays PCM through the speaker adapter, and updates the display metadata.

## Troubleshooting

This is an ESP-IDF project, not an Arduino IDE sketch; use the component manifest to resolve Arduino before building.
