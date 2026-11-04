# ESP-Brookesia 出厂应用

[English](README.md) | [简体中文](README_ZH.md)

## 功能

在 466 × 466 屏幕上启动 ESP-Brookesia 应用启动器，提供显示、媒体、无线、运动、设置、USB Screen 和小智等应用。媒体应用使用 FAT 格式 microSD 卡，目录和文件格式如下：

| 文件夹 | 应用 | 文件 |
| --- | --- | --- |
| `music` | 音乐播放器 | MP3、WAV 和可选同名 `.lrc` 歌词 |
| `videos` | 视频播放器 | MJPEG 视频的 MP4 或 AVI，音频为 AAC、MP3 或 PCM |
| `pictures` | 相册 | JPEG 和 PNG |
| `books` | 阅读器 | UTF-8、GBK 或 UTF-16 编码 TXT |
| `fonts` | 阅读器 | 可选 TTF 补充字体 |

小智接入 [xiaozhi.me](https://xiaozhi.me)：连接 Wi-Fi 后，在服务控制台添加首次显示的六位验证码。USB Screen 与 **设置 > Storage** 不能同时使用；只有打开对应应用时，USB 口才启用该功能。

## 硬件需求

- ESP32-P4-Knob-Touch-AMOLED-1.75 开发板
- 用于媒体应用的 FAT 格式 microSD 卡
- 使用小智时的 Wi-Fi 网络，以及使用 USB Screen 时的 USB 主机

## 怎么运行

使用[仓库首页](../../README_ZH.md)列出的出厂 ESP-IDF 版本，然后运行：

```bash
idf.py set-target esp32p4 build flash monitor
```

`spiffs/music/` 中的 MP3 和 `spiffs/xiaozhi/` 中的小智提示音会一起打包到 SPIFFS 镜像，本地与 CI 构建均包含这些素材。

## 预期现象

屏幕进入 Brookesia 启动器并显示状态栏。打开应用后，可使用对应的硬件功能或 microSD 内容。

## 常见问题

CMake 会自动生成板配置表；若已安装 `esp-bmgr-assist`，请设置 `ESP_BOARD_MANAGER_SKIP_GEN_CHECK=1` 后构建，以使用这些配置表。请保留本工程的 `spiffs` 目录，启动时会挂载它。
