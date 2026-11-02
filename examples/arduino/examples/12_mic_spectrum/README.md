# Microphone Spectrum

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Captures ES7210 MIC1 and MIC2 input at 16 kHz, then shows a 512-point FFT spectrum, RMS, and peak level on the AMOLED display.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board with its audio adapter board

## How to Run

Open `12_mic_spectrum.ino`, select the board configuration in the repository README, upload it, and open the serial monitor at 115200 baud.

## Expected Result

The 32 spectrum bars use an extra 24 dB of display gain to reveal quiet ambient sounds. RMS and peak still describe the original input level; the speaker amplifier remains off.

## Troubleshooting

If the display reports ES7210 initialization failure, check that the audio adapter board is connected and use the configured 32 MB Flash and PSRAM board options.
