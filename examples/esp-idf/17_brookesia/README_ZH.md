# ESP-Brookesia 手机式界面

[English](README.md) | [简体中文](README_ZH.md)

## 功能

在 466 x 466 AMOLED 屏幕上启动 ESP-Brookesia 手机式界面。应用会注册计算器、绘图、频谱分析、音乐播放器和设置，并刷新状态栏时钟和 Wi-Fi 状态。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

使用[仓库首页](../../../README_ZH.md)列出的 ESP-IDF 版本，然后运行：

```bash
idf.py set-target esp32p4 build flash monitor
```

## 预期现象

屏幕启动手机式界面，显示已安装的应用启动器和定期更新的状态栏。

## 常见问题

工程启动时会挂载随示例提供的 SPIFFS 镜像；请勿删除本示例的 `spiffs` 目录。
