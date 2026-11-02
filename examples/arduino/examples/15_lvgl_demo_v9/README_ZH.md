# LVGL v9 基准测试

[English](README.md) | [简体中文](README_ZH.md)

## 功能

在开发板的 466 × 466 圆形 CO5300 MIPI-DSI 显示屏上全屏运行 LVGL v9 基准测试，CST9217 触控输入同时提供给 LVGL。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板

## 怎么运行

打开 `15_lvgl_demo_v9.ino`，选择仓库 README 中的开发板配置后上传，并以 115200 波特率打开串口监视器。

## 预期现象

整个圆形显示屏会运行 LVGL 基准测试场景，并显示汇总页面。完成后，串口监视器会输出有效场景数量和平均 FPS。

## 常见问题

请在 Arduino 开发板选项中启用 PSRAM。此示例使用 `examples/arduino/libraries/` 中提供的 LVGL v9 库。
