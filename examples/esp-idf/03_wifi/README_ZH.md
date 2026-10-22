# Wi-Fi 扫描

[English](README.md) | [简体中文](README_ZH.md)

## 功能

通过开发板的外部连接组件扫描附近的 Wi-Fi 接入点。无需配置凭据，也不会连接到网络。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

在本示例目录中运行以下命令。ESP-IDF 版本请参见仓库 README。

```bash
idf.py set-target esp32p4 build flash monitor
```

## 预期现象

串口监视器会输出一条扫描汇总信息，以及最多十个按信号强度排序的接入点。未发现接入点也是有效结果。

## 常见问题

示例会在外部连接组件初始化后才开始扫描；初始化或扫描错误会输出到串口监视器。
