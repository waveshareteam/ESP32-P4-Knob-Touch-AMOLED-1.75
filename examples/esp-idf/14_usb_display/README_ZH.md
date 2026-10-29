# 14 USB 显示

[English](README.md) | [简体中文](README_ZH.md)

## 功能

本示例将开发板作为 466 × 466 JPEG USB 扩展屏，供 Espressif Windows USB 显示驱动使用。开发板触摸会通过 HID 触摸屏接口回传到主机。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板
- 将转接板 USB 2.0 Type-C 口连接到 Windows 10 或 Windows 11 电脑的 USB-C 线

## 怎么运行

使用仓库 README 所列 ESP-IDF 版本编译、烧录，然后将开发板连接到电脑并安装兼容的 Espressif USB 显示驱动。

```bash
idf.py set-target esp32p4 build flash monitor
```

## 预期现象

日志会显示开发板已准备好接收 466 × 466 画面；电脑配置 USB 设备后会记录枚举信息。电脑驱动可以发送全屏 JPEG 帧或局部更新帧，并接收开发板的触摸回传。

## 常见问题

本示例必须使用转接板上的高速 USB 2.0 口，不能使用 USB 转串口连接。电脑没有识别到扩展屏时，请翻转 Type-C 插头后重新连接。
