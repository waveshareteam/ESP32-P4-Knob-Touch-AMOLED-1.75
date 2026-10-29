# 限位开关

[English](README.md) | [简体中文](README_ZH.md)

## 功能

监测两个独立的限位开关输入，并在 LVGL 页面上显示电平和触发计数。默认配置会在最后一次边沿后的 2 ms 确认 GPIO 电平；工程也提供轮询、仅输入和 PCNT 计数诊断模式。

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

默认模式只在输入保持低电平达到设定稳定时间后计入下降沿。轮询和 PCNT 模式用于诊断，其脉冲计数行为不同。
