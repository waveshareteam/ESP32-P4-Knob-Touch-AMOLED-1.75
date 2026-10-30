# 麦克风频谱

[English](README.md) | [简体中文](README_ZH.md)

## 功能

以 16 kHz 采集 ES7210 的 MIC1 和 MIC2 输入，并在 AMOLED 屏幕显示 512 点 FFT 频谱、RMS 和峰值电平。

## 硬件需求

- 带音频转接板的 ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

在 Arduino IDE 中打开 `12_mic_spectrum.ino`，选择仓库 README 中的开发板配置后烧录，并以 115200 波特率打开串口监视器。

## 预期现象

显示屏会显示 32 条随声音变化的频谱柱，并以额外 24 dB 显示增益增强轻微环境声；RMS 和峰值仍表示原始输入电平。扬声器功放保持关闭。

## 常见问题

若屏幕提示 ES7210 初始化失败，请检查音频转接板是否连接，并使用配置好的 32 MB Flash 和 PSRAM 开发板选项。
