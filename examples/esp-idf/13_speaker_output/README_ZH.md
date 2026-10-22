# 13 扬声器输出

[English](README.md) | [简体中文](README_ZH.md)

## 功能

此工程是未来扬声器输出示例的预留占位符。当前实现会将功率放大器控制信号置低，在日志中输出 `RESERVED`、`NOT IMPLEMENTED` 和 `UNTESTED`，随后持续等待。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

在当前示例目录中，使用仓库 README 所列版本的 ESP-IDF 运行：

```bash
idf.py set-target esp32p4 build flash monitor
```

## 预期现象

串口监视器会报告该示例处于预留状态且尚未实现。它不会初始化音频编解码器，也不会产生扬声器输出。

## 常见问题

扬声器输出尚未实现，因此当前没有可排查的扬声器测试问题。
