# 按键

[English](README.md) | [简体中文](README_ZH.md)

## 功能

将 BOOT 和 PWR 按键作为两个独立的低电平有效输入进行测试。示例只读取输入，并输出经过消抖的按下和释放事件。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

在本示例目录中运行以下命令。ESP-IDF 版本请参见仓库 README。

```bash
idf.py set-target esp32p4 build flash monitor
```

## 预期现象

分别按下和释放两个按键时，串口监视器会输出各自的 `PRESS`、`RELEASE` 消息及按下次数。

## 常见问题

输入使用内部上拉；如果未输出启动电平或变化信息，请分别检查对应按键。
