<div align="center">
  <h1>ESP32-P4-Knob-Touch-AMOLED-1.75</h1>
  <p><strong>ESP32-P4 1.75-inch 466 × 466 MIPI-DSI AMOLED touch knob development board</strong></p>
  <p><strong>English</strong> | <a href="README_ZH.md">简体中文</a></p>
  <p>
    <a href="https://github.com/waveshareteam/ESP32-P4-Knob-Touch-AMOLED-1.75/actions/workflows/build.yml"><img alt="Build" src="https://github.com/waveshareteam/ESP32-P4-Knob-Touch-AMOLED-1.75/actions/workflows/build.yml/badge.svg"></a>
    <a href="https://github.com/waveshareteam/ESP32-P4-Knob-Touch-AMOLED-1.75/releases"><img alt="Latest Release" src="https://img.shields.io/github/v/release/waveshareteam/ESP32-P4-Knob-Touch-AMOLED-1.75"></a>
    <a href="LICENSE"><img alt="License" src="https://img.shields.io/github/license/waveshareteam/ESP32-P4-Knob-Touch-AMOLED-1.75"></a>
  </p>
  <p>
    <a href="https://www.waveshare.com/wiki/ESP32-P4-Knob-Touch-AMOLED-1.75">🌐 Product</a> &middot;
    <a href="#quick-start">📦 Firmware</a> &middot;
    <a href="examples/esp-idf/">🧩 ESP-IDF</a> &middot;
    <a href="hardware/schematic/">📚 Documentation</a>
  </p>
</div>

---

## ✨ Overview

This board combines an ESP32-P4 with a round AMOLED touch display, rotary direction inputs and vibration feedback. This repository provides ESP-IDF peripheral examples, display and audio applications, and schematics for the main board and adapter.

| Feature | Device / interface |
| --- | --- |
| MCU | ESP32-P4NRW32X |
| Memory | 32 MB Flash, 32 MB PSRAM |
| Display | 1.75-inch, 466 × 466 AMOLED, MIPI-DSI, CO5300 |
| Touch | CST9217, I2C |
| Wireless | ESP32 coprocessor over SDIO |
| Power / time / motion | AXP2101 PMU, PCF85063 RTC, QMI8658 IMU |
| Audio | ES8389 codec, ES7210 microphone ADC; NS4150B amplifier on the adapter |
| Controls | Rotary direction switches, DRV8837 vibration motor driver |
| Storage | microSD interface |
| Board support | Local BSP components included with the display and audio examples |

<a id="quick-start"></a>

## 📦 Quick Start

The repository currently provides source examples. Prebuilt example packages and a factory image are not included.

1. Install and activate ESP-IDF using the version in the development environment table below.
2. Download or clone this repository, then open a terminal in the chosen example directory.
3. Connect the board over USB and run `idf.py set-target esp32p4 build flash monitor`.
4. Follow the example README for any required peripherals and the expected result; start with [system information](examples/esp-idf/01_system_info/).

## 🧪 Examples

### ESP-IDF

| Example | Main feature |
| --- | --- |
| [01_system_info](examples/esp-idf/01_system_info/) | Chip, Flash, PSRAM and internal memory information |
| [02_buttons](examples/esp-idf/02_buttons/) | BOOT and PWR button state logging |
| [03_wifi](examples/esp-idf/03_wifi/) | Wi-Fi access point scanning |
| [04_bluetooth](examples/esp-idf/04_bluetooth/) | BLE GAP scanning and summaries |
| [05_axp2101](examples/esp-idf/05_axp2101/) | Read-only PMU status display |
| [06_sd_card](examples/esp-idf/06_sd_card/) | microSD mount and temporary-file verification |
| [07_limit_switches](examples/esp-idf/07_limit_switches/) | Two direction-switch indicators |
| [08_motor_drive](examples/esp-idf/08_motor_drive/) | Touch-controlled vibration with automatic stop |
| [09_rtc](examples/esp-idf/09_rtc/) | RTC time display |
| [10_qmi8658](examples/esp-idf/10_qmi8658/) | Accelerometer charts |
| [11_display_touch](examples/esp-idf/11_display_touch/) | RGB display stages and touch drawing |
| [12_mic_spectrum](examples/esp-idf/12_mic_spectrum/) | Microphone FFT spectrum |
| [13_speaker_output](examples/esp-idf/13_speaker_output/) | Reserved skeleton; no audio playback |
| [14_usb_display](examples/esp-idf/14_usb_display/) | Reserved skeleton; no USB display implementation |
| [15_lvgl_demo_v9](examples/esp-idf/15_lvgl_demo_v9/) | LVGL benchmark |
| [16_a2dp_sink](examples/esp-idf/16_a2dp_sink/) | Bluetooth audio receiver and display |
| [17_brookesia](examples/esp-idf/17_brookesia/) | Brookesia application launcher |

## 🛠️ Development Environment

| Framework | Configured version | Examples |
| --- | --- | ---: |
| ESP-IDF | `v6.1` | 17 |

The target is `esp32p4`; the framework configuration is maintained in [product.json](.github/product.json).

## 📚 Resources

- [Main board schematic](hardware/schematic/ESP32-P4-Knob-Touch-AMOLED-1.75.pdf)
- [Adapter schematic](hardware/schematic/ESP32-P4-Knob-Adapter.pdf)
- [Official product Wiki](https://www.waveshare.com/wiki/ESP32-P4-Knob-Touch-AMOLED-1.75)

## 🤝 Support

- Example or repository problem: [open an issue](https://github.com/waveshareteam/ESP32-P4-Knob-Touch-AMOLED-1.75/issues/new/choose).
- Product usage, hardware or order questions: [Waveshare support](https://service.waveshare.com/).

## 📄 License

First-party content is licensed under [Apache License 2.0](LICENSE), except where a file states otherwise; bundled third-party components retain their own licenses.
