# LVGL v9 Benchmark

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Runs the LVGL v9 benchmark in a centered 320 × 320 safe viewport on the board's round CO5300 MIPI-DSI display, with CST9217 touch input available to LVGL.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Open `15_lvgl_demo_v9.ino`, select the board configuration in the repository README, upload it, and open the serial monitor at 115200 baud.

## Expected Result

The display shows the LVGL benchmark scenes and summary screen inside the centered square viewport.

## Troubleshooting

Enable PSRAM in the Arduino board options. This sketch uses the LVGL v9 library included under `examples/arduino/libraries/`.
