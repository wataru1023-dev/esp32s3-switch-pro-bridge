# ESP32-S3 Switch Pro Bridge

通过 ESP32-S3，将 Nintendo Switch 2 Pro 手柄桥接为 macOS 原生识别的有线 Nintendo Switch Pro 手柄。

## 项目功能

- 按键与左右摇杆透传
- HD 震动转发
- BLE 自动重连
- 可调 USB 上报率，默认 66 Hz

## 使用说明

- [中文说明](esp32s3-switch-pro-bridge/README.zh-CN.md)
- [English](esp32s3-switch-pro-bridge/README.md)
- [日本語](esp32s3-switch-pro-bridge/README.ja.md)

## 下载

[下载最新固件与源码](https://github.com/wataru1023-dev/esp32s3-switch-pro-bridge/releases/latest)

预编译固件仅适用于 ESP32-S3 N16R8：
16 MB Flash、8 MB Octal PSRAM。

## 源码与编译

工程位于 `esp32s3-switch-pro-bridge/` 子目录。
使用 ESP-IDF v5.3.3，在工程目录中编译。

## 许可证

[MIT](esp32s3-switch-pro-bridge/LICENSE)
