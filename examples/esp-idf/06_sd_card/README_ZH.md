# SD 卡

[English](README.md) | [简体中文](README_ZH.md)

## 功能

挂载 SD 卡、写入临时文件、读回校验、删除该文件，再卸载 SD 卡。状态会显示在 LVGL 页面和串口监视器中。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板
- 兼容的 SD 卡

## 怎么运行

插入 SD 卡后，在本示例目录中运行以下命令。ESP-IDF 版本请参见仓库 README。

```bash
idf.py set-target esp32p4 build flash monitor
```

## 预期现象

示例会输出卡信息和成功的写入、读取、删除校验结果；卸载 SD 卡后显示通过状态。

## 常见问题

本示例会修改插入卡上的临时文件。挂载或校验失败会显示在串口监视器和屏幕上。
