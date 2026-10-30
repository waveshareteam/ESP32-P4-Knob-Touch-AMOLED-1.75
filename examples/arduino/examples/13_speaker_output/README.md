# Speaker Output

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Configures the ES8389 DAC and NS4150B amplifier, then alternates three-second 1 kHz stereo tone and digital-silence intervals.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board with its audio adapter board

## How to Run

Open `13_speaker_output.ino`, select the board configuration in the repository README, upload it, and open the serial monitor at 115200 baud.

## Expected Result

The serial monitor reports key ES8389 register values. The speaker alternates an audible 1 kHz tone and digital silence while the codec remains open.

## Troubleshooting

Confirm that the audio adapter board is connected. The example enables its amplifier only during tone playback.
