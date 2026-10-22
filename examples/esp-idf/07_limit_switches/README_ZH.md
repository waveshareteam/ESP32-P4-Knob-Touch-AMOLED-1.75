# 限位开关

[English](README.md) | [简体中文](README_ZH.md)

## 功能

监测两个独立的限位开关输入，并在 LVGL 页面上显示电平和触发计数。默认配置使用轮询；工程也提供仅输入和 PCNT 计数诊断模式。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

在本示例目录中运行以下命令。ESP-IDF 版本请参见仓库 README。

```bash
idf.py set-target esp32p4 build flash monitor
```

## 预期现象

操作任一开关时，显示屏和串口监视器会更新其独立的高低电平状态和计数。

## 常见问题

默认轮询模式可能漏掉短于采样间隔的脉冲。仅在其行为符合已连接硬件时才选择其他诊断模式。
