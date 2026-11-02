# Wi-Fi 扫描

[English](README.md) | [简体中文](README_ZH.md)

## 功能

通过板载无线协处理器扫描附近热点，打印最多十条 SSID、RSSI、信道和认证模式记录。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

用 Arduino IDE 打开 `03_wifi.ino`，采用仓库 README 中的开发板设置。复制示例时同时复制共享 `KnobWireless` 库，用户无需安装 ESP-IDF。

## 预期现象

以 115200 波特率打开串口监视器，会看到扫描成功及热点记录。隐藏网络显示为 `<hidden>`。

## 常见问题

使用官方 Arduino-ESP32 3.3.12，并保留完整的 `KnobWireless` 库，包括其中的 ESP32-P4 预编译库。
