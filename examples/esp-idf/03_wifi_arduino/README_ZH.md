# Arduino 组件 Wi-Fi 扫描

[English](README.md) | [简体中文](README_ZH.md)

## 功能

通过 Arduino 的 `setup()` 和 `loop()` 作为入口，运行原有 ESP-IDF Wi-Fi 扫描并使用板载 ESP32 无线控制器。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

使用将 Arduino-ESP32 3.3.12 作为 ESP-IDF 组件的方式构建本工程，然后以 115200 波特率打开串口监视器。

## 预期现象

扫描完成后，日志会打印无线控制器 MAC 地址和信号最强的可见热点。

## 常见问题

这是 ESP-IDF 工程，不是 Arduino IDE 草图；构建前请通过组件清单解析 Arduino 依赖。
