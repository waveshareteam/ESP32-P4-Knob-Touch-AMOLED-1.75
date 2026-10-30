# Bluetooth A2DP Sink

[English](README.md) | [简体中文](README_ZH.md)

## What It Does

Starts a Bluetooth A2DP sink named `Knob_BT_INO`. Received 16-bit mono or stereo PCM is queued before playback through the ES8389 DAC and speaker amplifier.

## Hardware Required

- ESP32-P4-Knob-Touch-AMOLED-1.75 development board with its audio adapter board

## How to Run

Open `16_a2dp_sink.ino`, select the board configuration in the repository README, upload it, then pair an audio source with `Knob_BT_INO` and begin playback.

## Expected Result

The full round display shows the device name, Bluetooth/audio status, received sample format, and current volume. The DAC and amplifier are enabled when PCM playback begins. Turn the knob to adjust volume.

## Troubleshooting

Enable PSRAM in the Arduino board options and confirm the audio adapter board is connected. Use the official Arduino-ESP32 3.3.12 core and install the complete shared libraries, including the precompiled `KnobWireless` backend.
