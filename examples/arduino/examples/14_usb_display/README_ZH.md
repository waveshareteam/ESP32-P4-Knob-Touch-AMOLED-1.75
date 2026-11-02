# USB 显示

[English](README.md) | [简体中文](README_ZH.md)

## 功能

通过高速 USB Vendor 接口接收 Espressif Windows USB 显示驱动发送的 466 × 466 JPEG 画面，并以 HID 触摸屏回传触摸事件。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板
- 转接板高速 USB Type-C 接口，以及 Windows 10 或 Windows 11 电脑

## 怎么运行

在 Arduino IDE 打开 `14_usb_display.ino`，选择仓库 README 中的 ESP32-P4 板级配置并烧录，然后将转接板高速 USB 接口连接到电脑，安装兼容的 Espressif USB 显示驱动。

## 预期现象

Windows 会识别到 466 × 466 JPEG 显示设备，并接收开发板的触摸回传。该设备支持与 ESP-IDF 示例相同协议的全屏和局部 JPEG 更新。

## 常见问题

请使用转接板高速 USB 接口，不能使用 USB 转串口接口。Windows 驱动必须与本示例暴露的 USB Vendor 字符串兼容。
