# AXP2101 状态

[English](README.md) | [简体中文](README_ZH.md)

## 功能

在 LVGL 页面上显示只读的 AXP2101 电源管理状态。示例读取可用的供电和电池信息，不会修改电源、充电或稳压器设置。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

在本示例目录中运行以下命令。ESP-IDF 版本请参见仓库 README。

```bash
idf.py set-target esp32p4 build flash monitor
```

## 预期现象

显示屏会更新 AXP2101 在线状态及可用的供电、电池、温度和充电信息；不可用的读数显示为 `N/A`。

## 常见问题

I2C 或 PMU 读取错误会显示在屏幕和串口监视器中，随后示例会重试连接。
