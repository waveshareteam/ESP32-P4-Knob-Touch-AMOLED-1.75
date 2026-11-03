# Firmware

[English](README.md) | [简体中文](README_ZH.md)

Ready-to-flash firmware for the ESP32-P4-Knob-Touch-AMOLED-1.75. Each file is a merged image written at `0x0`; the date in its name is the version.

| File | Purpose |
| --- | --- |
| `ESP32-P4-Knob-Touch-AMOLED-1.75_FactoryOnly_20261103.bin` | Brookesia demonstration firmware with knob page turning on the home screen, round-display UI fixes, fixes for Xiaozhi connections with several apps open and for leaving Xiaozhi while it connects, and built-in music. |

Install esptool and replace `PORT` with the board's serial port:

```bash
python -m pip install "esptool>=5"
python -m esptool --chip esp32p4 --port PORT erase-flash
python -m esptool --chip esp32p4 --port PORT --baud 921600 write-flash --flash-mode dio --flash-freq 80m --flash-size 32MB 0x0 ESP32-P4-Knob-Touch-AMOLED-1.75_FactoryOnly_20261103.bin
```

Full flashing resets saved device settings. The firmware source is in [`examples/factory`](../examples/factory/).
