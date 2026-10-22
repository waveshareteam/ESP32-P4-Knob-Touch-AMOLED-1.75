# LVGL Benchmark

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Runs the LVGL benchmark demo on the board AMOLED display with the board BSP and touch/display configuration.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Use the ESP-IDF version listed in the [repository README](../../../README.md), then run:

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

The AMOLED display starts the LVGL benchmark interface after boot.

## Troubleshooting

None yet.
