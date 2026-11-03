# 固件

[English](README.md) | [简体中文](README_ZH.md)

适用于 ESP32-P4-Knob-Touch-AMOLED-1.75 的可直接烧录固件。每个文件都是从 `0x0` 写入的合并镜像，文件名中的日期即版本。

| 文件 | 用途 |
| --- | --- |
| `ESP32-P4-Knob-Touch-AMOLED-1.75_FactoryOnly_20261103.bin` | Brookesia 演示固件，新增主屏旋钮翻页，修复圆屏界面显示问题和多个应用同时运行时小智联网失败，包含内置音乐。 |

安装 esptool，并将 `PORT` 替换为开发板的实际串口：

```bash
python -m pip install "esptool>=5"
python -m esptool --chip esp32p4 --port PORT erase-flash
python -m esptool --chip esp32p4 --port PORT --baud 921600 write-flash --flash-mode dio --flash-freq 80m --flash-size 32MB 0x0 ESP32-P4-Knob-Touch-AMOLED-1.75_FactoryOnly_20261103.bin
```

完整烧录会重置已保存的设备配置。固件源码位于 [`examples/factory`](../examples/factory/)。
