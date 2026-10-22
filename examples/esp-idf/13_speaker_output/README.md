# 13 Speaker Output

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

This project is a reserved placeholder for a future speaker-output example. Its current implementation turns the power-amplifier control low, writes `RESERVED`, `NOT IMPLEMENTED`, and `UNTESTED` to the log, and then waits.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Run this command from this example directory using the ESP-IDF version listed in the repository README:

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

The serial monitor reports that the example is reserved and not implemented. It does not initialize the audio codec or generate speaker output.

## Troubleshooting

This project has no speaker test to troubleshoot because speaker output is not implemented.
