# 蓝牙 A2DP 接收端

[English](README.md) | [简体中文](README_ZH.md)

## 功能

初始化外部无线连接，并提供可被发现的蓝牙 A2DP 接收端。接收到的音频输出到板载编解码器，AMOLED 界面会在收到元数据时显示标题和歌词文本。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板
- 用于配对和播放的蓝牙音频设备

## 怎么运行

使用[仓库首页](../../../README_ZH.md)列出的 ESP-IDF 版本，然后运行：

```bash
idf.py set-target esp32p4 build flash monitor
```

## 预期现象

启动后，设备可被蓝牙音频设备发现。屏幕先显示等待提示，并在 A2DP 元数据回调提供内容时更新文本。

## 常见问题

只有 A2DP 音频流进入开始状态后，音频输出才会解除静音。
