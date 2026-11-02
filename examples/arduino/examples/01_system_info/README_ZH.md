# 系统信息

[English](README.md) | [简体中文](README_ZH.md)

## 功能

通过串口输出 ESP32-P4 的芯片型号、版本、CPU 频率、Flash、PSRAM 和可用堆内存信息。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

用 Arduino IDE 打开 `01_system_info.ino`，选择仓库 README 所列的开发板配置，上传后以 115200 波特率打开串口监视器。

## 预期现象

启动后和每隔五秒，串口监视器都会输出一份系统信息；所选配置启用 PSRAM 时，Flash 和 PSRAM 容量均为非零值。

## 常见问题

如果串口显示 PSRAM 容量为零，请检查所选 Arduino 开发板配置是否启用了本板的 PSRAM。
