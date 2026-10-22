# 09 RTC

[English](README.md) | [简体中文](README_ZH.md)

## 功能

此示例每秒读取一次 PCF85063 实时时钟，并在 LVGL 屏幕上显示时间和状态。它会校验收到的 BCD 时间数据，且不会向 RTC 写入时间。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

在当前示例目录中，使用仓库 README 所列版本的 ESP-IDF 运行：

```bash
idf.py set-target esp32p4 build flash monitor
```

## 预期现象

RTC 可用时，屏幕每秒更新日期和时间，并显示其状态。数据无效或读取失败时，时钟显示会清空并显示错误，不会保留过期时间。

## 常见问题

这是只读示例，不能用来设置 RTC 时间。状态页面会标明振荡器或 BCD 数据无效，以及 I2C 读取错误。
