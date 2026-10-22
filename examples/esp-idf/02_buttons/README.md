# Buttons

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Tests the BOOT and PWR buttons as separate active-low inputs. The example only reads the inputs and reports debounced press and release events.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Run the following command from this example directory. Use the ESP-IDF version listed in the repository README.

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

Pressing and releasing either button produces independent `PRESS` and `RELEASE` messages in the serial monitor, with a press count for each button.

## Troubleshooting

The inputs use internal pull-ups; verify each button separately if its startup level or transitions are not reported.
