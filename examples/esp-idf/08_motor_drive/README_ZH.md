# 08 电机驱动

[English](README.md) | [简体中文](README_ZH.md)

## 功能

此示例通过 LVGL 的按住式按钮驱动板载电机或振动器。它采用受限的正向脉冲，松开按钮、超时或出错时都会让两路控制信号回到空闲状态。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

在当前示例目录中，使用仓库 README 所列版本的 ESP-IDF 运行：

```bash
idf.py set-target esp32p4 build flash monitor
```

## 预期现象

屏幕显示 **VIBRATE** 按钮和当前电机状态。按住按钮会短暂振动；松开按钮或达到设定的保护超时时间后，电机会停止，界面回到 `IDLE`。

## 常见问题

最大通电时间由 `CONFIG_MOTOR08_MAX_ON_MS` 设置，源码将其限制为 500 ms。GPIO 或定时器控制失败时，界面会显示 `FAULT` 并尝试停止电机。
