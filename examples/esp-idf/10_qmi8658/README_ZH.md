# 10 QMI8658

[English](README.md) | [简体中文](README_ZH.md)

## 功能

此示例采集 QMI8658 加速度计数据，并在 LVGL 屏幕上绘制 X、Y、Z 三轴加速度曲线。它会探测支持的器件地址、校验器件 ID，并显示当前地址和中断电平。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

在当前示例目录中，使用仓库 README 所列版本的 ESP-IDF 运行：

```bash
idf.py set-target esp32p4 build flash monitor
```

## 预期现象

检测到 IMU 后，移动开发板时曲线和 X、Y、Z 数值会更新。探测、配置或读取加速度计失败时，屏幕会显示错误。

## 常见问题

示例支持 QMI8658 的 `0x6B` 和 `0x6A` 地址，并要求 `WHO_AM_I` 为 `0x05`。读取出错后，工作任务会重新打开并配置器件。
