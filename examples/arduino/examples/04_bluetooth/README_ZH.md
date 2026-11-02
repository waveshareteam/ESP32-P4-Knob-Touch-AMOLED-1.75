# 蓝牙 LE 扫描

[English](README.md) | [简体中文](README_ZH.md)

## 功能

扫描附近的蓝牙 LE 广播十秒，打印最多十个信号最强的设备，包括地址、RSSI 和可用的广播名称。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

用 Arduino IDE 或 Arduino CLI 打开 `04_bluetooth.ino`。采用仓库 README 中的开发板设置，复制示例时同时复制共享 `KnobWireless` 库；用户无需安装 ESP-IDF。

## 预期现象

以 115200 波特率打开串口监视器，十秒后会看到已接收的广播数量、唯一设备数量和信号最强的设备。没有广播名称的设备显示为 `<unnamed>`。

## 常见问题

使用官方 Arduino-ESP32 3.3.12，并保留完整的 `KnobWireless` 库，包括其中的 ESP32-P4 预编译库。本示例仅扫描 BLE 广播，不提供经典蓝牙串口或 GATT 功能。
