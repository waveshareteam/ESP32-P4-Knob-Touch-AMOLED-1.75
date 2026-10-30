# Arduino 组件 A2DP 接收端

[English](README.md) | [简体中文](README_ZH.md)

## 功能

通过 Arduino 的 `setup()` 和 `loop()` 作为入口，运行原有经典蓝牙 A2DP 接收、音频播放和显示界面。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板
- 扬声器转接板

## 怎么运行

使用将 Arduino-ESP32 3.3.12 作为 ESP-IDF 组件的方式构建本工程，然后将蓝牙音频源与屏幕显示的设备名称配对。

## 预期现象

开发板接受经典蓝牙音频连接，通过扬声器转接板播放 PCM，并更新显示的媒体信息。

## 常见问题

这是 ESP-IDF 工程，不是 Arduino IDE 草图；构建前请通过组件清单解析 Arduino 依赖。
