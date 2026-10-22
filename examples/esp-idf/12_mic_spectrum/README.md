# 12 Microphone Spectrum

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

This example captures audio from the ES7210 microphone path and shows a real-time FFT spectrum, RMS level, and peak level on the LVGL display. It keeps the output codec disabled and processes microphone input in a separate task.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board

## How to Run

Run this command from this example directory using the ESP-IDF version listed in the repository README:

```bash
idf.py set-target esp32p4 build flash monitor
```

## Expected Result

The display shows moving spectrum bars that respond to nearby sound, together with RMS, peak, overrun, and read-error values. If audio initialization or sampling fails, the status line identifies the failed stage.

## Troubleshooting

This example captures microphone input only and does not produce speaker audio. Its queue retains the newest frame when it overflows, so the overrun counter can reveal an overloaded display path.
