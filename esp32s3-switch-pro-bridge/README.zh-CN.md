# ESP32-S3 Switch Pro 桥接

把任天堂 Switch 2 Pro 手柄变成 macOS 上的有线 **Nintendo Switch Pro 手柄**。

[English](README.md) · [中文](README.zh-CN.md) · [日本語](README.ja.md)

本固件运行在 ESP32-S3 上：通过蓝牙低功耗连接真实的 Switch 2 Pro 手柄（Pro2），再以原生 Nintendo Switch Pro Controller 的身份（USB VID `057E`、PID `2009`）呈现给 Mac。macOS 通过自带的 Game Controller 框架直接识别，无需额外驱动。
由于较为完美的模拟了pro手柄，经初步测试在windows端也可用。

## 特性

- macOS 原生识别为「Nintendo Switch Pro Controller」
- 全部按键 + 摇杆透传
- 任天堂 HD 震动透传（频率 + 振幅）
- BLE 自动重连
- 可调 USB 上报率（默认 66Hz，与真机一致）

## 硬件

- 一块 ESP32-S3 开发板（推荐 N16R8）
- 一个 Nintendo Switch 2 Pro 手柄
- 一条连接 Mac 的 USB 线

## 工作原理

```text
Pro2  --BLE-->  ESP32-S3  --USB-->  macOS（Switch Pro 手柄）
```

固件作为 BLE 主机运行：扫描并连接 Pro2，解析其输入通知，归一化状态，再重新封装成标准 Switch Pro USB HID 报告发出。主机发来的 HD 震动会被解码后通过 Pro2 的 BLE 震动流回传。

## 编译

依赖：ESP-IDF v5.x（`esp_tinyusb` 组件会自动拉取）。

```bash
idf.py set-target esp32s3
idf.py build
```

## 烧录

```bash
idf.py -p /dev/cu.usbmodemXXXX flash
```

macOS 上可用 `ls /dev/cu.*` 查看端口。

## 使用

1. 烧录固件。
2. 把 ESP32-S3 的原生 USB 口连到 Mac。
3. 打开 Pro2 并进入配对模式。
4. 固件会自动连接；macOS 在「系统设置 → 游戏控制器」中显示为「Nintendo Switch Pro Controller」。

串口命令（115200 波特率）：

| 命令 | 说明 |
| --- | --- |
| `status` | 查看连接与上报状态 |
| `ble scan` | 扫描 Pro2 |
| `ble connect <addr>` | 连接指定地址 |
| `ble forget` | 清除已保存的 BLE 目标 |
| `rate <hz>` | 设置 USB 上报率（20–1000） |
| `rumble` | 震动自检 |
| `reboot` | 重启 |

## 许可证

MIT。见 [LICENSE](LICENSE)。

## 致谢

基于社区「新和联胜 Pro2 Bridge」研究构建。与任天堂、索尼、微软、乐鑫无关。
