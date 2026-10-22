# 蓝牙扫描

[English](README.md) | [简体中文](README_ZH.md)

## 功能

通过外部连接组件进行低功耗蓝牙 GAP 扫描。示例汇总附近广播设备，不连接、不配对，也不发现服务。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

在本示例目录中运行以下命令。ESP-IDF 版本请参见仓库 README。

```bash
idf.py set-target esp32p4 build flash monitor
```

## 预期现象

每个扫描窗口结束时，串口监视器会输出汇总信息以及最多十个按 RSSI 排序的广播设备。

## 常见问题

本示例仅用于 BLE GAP 扫描。扫描启动或传输错误会写入日志，并会安排下一次扫描。
