# AXP2101 状态

[English](README.md) | [简体中文](README_ZH.md)

## 功能

读取并打印 AXP2101 的电源、电池、充电和温度状态，不会修改 PMU、充电或稳压器设置。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

打开 `05_axp2101.ino`，按仓库 README 选择 ESP32-P4 板卡配置，烧录后以 115200 波特率打开串口监视器。

## 预期现象

串口每秒更新一次 PMU ID 和已启用 ADC 通道的测量信息。

## 常见问题

无法读取 PMU 时，程序会打印 I2C 错误并重试。未启用的 PMU ADC 通道显示为 `N/A(disabled)`，程序不会启用这些通道。
