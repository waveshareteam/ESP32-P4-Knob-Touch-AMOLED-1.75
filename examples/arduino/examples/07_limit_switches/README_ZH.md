# 旋钮方向开关

[English](README.md) | [简体中文](README_ZH.md)

## 功能

输出旋钮顺时针和逆时针低电平脉冲的防抖结果，并分别统计两个方向的触发次数。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

用 Arduino IDE 打开 `07_limit_switches.ino`，选择 ESP32-P4 开发板并设置 32 MB Flash、启用 PSRAM 和启动 USB CDC，上传后以 115200 波特率打开串口监视器。

## 预期现象

旋转旋钮后，串口会在 2 ms 防抖后输出 CW 或 CCW 触发消息，并递增对应方向的计数。

## 常见问题

两个输入是独立的低电平有效方向开关，不能按正交编码器方式配置。
