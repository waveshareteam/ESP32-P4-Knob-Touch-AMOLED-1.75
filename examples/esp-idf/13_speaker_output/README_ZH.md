# 13 扬声器输出

[English](README.md) | [简体中文](README_ZH.md)

## 功能

本示例初始化板载 ES8389 播放链路，并每两秒播放一次短时、低音量的 440 Hz PCM 提示音。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

在当前示例目录中，使用仓库 README 所列版本的 ESP-IDF 运行：

```bash
idf.py set-target esp32p4 build flash monitor
```

## 预期现象

串口监视器会报告每次播放。扬声器会发出短提示音，随后保持安静直到下一次播放。

## 常见问题

没有声音时，确认开发板通过 USB 供电；需要改音量前，请先保持示例中的低音量默认值。
