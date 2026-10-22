# 系统信息

[English](README.md) | [简体中文](README_ZH.md)

## 功能

读取 ESP32-P4 的 Flash、PSRAM 和内部堆内存容量信息。不初始化显示屏或外部外设。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

在本示例目录中运行以下命令。ESP-IDF 版本请参见仓库 README。

```bash
idf.py set-target esp32p4 build flash monitor
```

## 预期现象

串口监视器会输出 Flash、PSRAM 和内部堆内存数值；所有必需数值均非零时输出 `PASS`。

## 常见问题

若已启用 PSRAM 但初始化失败，示例会输出 `FAIL`，不会将零容量视为有效结果。
