# 蓝牙 A2DP 接收器

[English](README.md) | [简体中文](README_ZH.md)

## 功能说明

启动名为 `Knob_BT_INO` 的蓝牙 A2DP 接收器。接收到的 16 位单声道或立体声 PCM 会先进入队列，再通过 ES8389 DAC 和扬声器功放播放。

## 所需硬件

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板及其音频转接板

## 运行方法

打开 `16_a2dp_sink.ino`，选择仓库 README 中的开发板配置并上传。随后将音频源与 `Knob_BT_INO` 配对并开始播放。

## 预期结果

完整圆形屏幕会显示设备名称、蓝牙/音频状态、收到的采样格式和当前音量。收到 PCM 并开始播放时会启用 DAC 和功放。旋转旋钮可调节音量。

## 故障排除

请在 Arduino 开发板选项中启用 PSRAM，并确认已连接音频转接板。请使用官方 Arduino-ESP32 3.3.12，并安装完整共享库，包括预编译的 `KnobWireless` 后端。
