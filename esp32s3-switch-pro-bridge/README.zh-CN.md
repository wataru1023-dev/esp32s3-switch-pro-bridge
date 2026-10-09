# ESP32-S3 Switch Pro 桥接

把任天堂 Switch 2 Pro 手柄变成 macOS 上的有线 **Nintendo Switch Pro 手柄**。

[English](README.md) · [中文](README.zh-CN.md) · [日本語](README.ja.md)

本固件运行在 ESP32-S3 上：通过蓝牙低功耗连接真实的 Switch 2 Pro 手柄（Pro2），再以原生 Nintendo Switch Pro Controller 的身份（USB VID `057E`、PID `2009`）呈现给 Mac。macOS 通过自带的 Game Controller 框架直接识别，无需额外驱动。
此前初步测试在 Windows 上也可用；项目主要用于 macOS。

## 特性

- macOS 原生识别为「Nintendo Switch Pro Controller」
- 全部按键 + 摇杆透传
- 任天堂 HD 震动透传（频率 + 振幅）
- BLE 自动重连
- 可调 USB 上报率（默认 66Hz，与真机一致）

## 硬件

- 一块具有 16 MB Flash、8 MB 八线 PSRAM 的 ESP32-S3 开发板（N16R8，发布固件的目标配置）
- 一个 Nintendo Switch 2 Pro 手柄
- 一条连接 Mac 的 USB 线

## 工作原理

```text
Pro2  --BLE-->  ESP32-S3  --USB-->  macOS（Switch Pro 手柄）
```

固件作为 BLE 主机运行：扫描并连接 Pro2，解析其输入通知，归一化状态，再重新封装成标准 Switch Pro USB HID 报告发出。主机发来的 HD 震动会被解码后通过 Pro2 的 BLE 震动流回传。

## 编译

验证工具链：**ESP-IDF v5.3.3**。先加载对应版本的环境，再在本目录执行以下命令（GitHub 仓库内有一层 `esp32s3-switch-pro-bridge/` 子目录）。组件管理器会按 `dependencies.lock` 下载依赖，保留该文件以固定依赖版本。本次发布未验证其他 ESP-IDF 版本。

```bash
idf.py set-target esp32s3
idf.py build
```

## 烧录

```bash
idf.py -p /dev/cu.usbmodemXXXX flash
```

macOS 上可用 `ls /dev/cu.*` 查看端口。烧录和日志使用开发板的 UART / 烧录口；烧录前退出占用该口的监视器。在 `idf.py monitor` 中按 **Ctrl + ]** 返回 shell。

Release 固件采用相同的 N16R8 配置，压缩包内包含 `flash_args` 与独立的 bootloader、分区表、应用镜像，按包内说明烧录。使用相同分区布局升级时，这种方式保留 NVS 内的已有设置。其他开发板应通过 `menuconfig` 调整配置并从源码编译。

## 使用

1. 烧录固件。
2. 把 ESP32-S3 的原生 USB / USB-OTG 数据口连到 Mac；只连接 UART / 烧录口不会出现手柄接口。
3. 打开 Pro2 并进入配对模式。
4. 固件会自动连接；macOS 在「系统设置 → 游戏控制器」中显示为「Nintendo Switch Pro Controller」。

串口命令（115200 波特率）：

| 命令 | 说明 |
| --- | --- |
| `status` | 查看连接与上报状态 |
| `debug on` / `debug off` | 开启 / 关闭详细日志，包含 USB 诊断 |
| `axis debug on 32` / `axis debug off` | 开启摇杆采样日志 / 关闭 |
| `raw debug on 32` / `raw debug off` | 开启 BLE 报文及 IMU 采样日志 / 关闭 |
| `ble scan` | 扫描 Pro2 |
| `ble connect <addr>` | 连接指定地址 |
| `ble forget` | 清除已保存的 BLE 目标 |
| `rate <hz>` | 设置 USB 上报率（20–1000） |
| `rumble` | 震动自检 |
| `reboot` | 重启 |

这些命令输入固件的串口控制台，不能在 shell 中执行。发布版默认关闭连续摇杆与 USB 诊断输出。原生 USB 兼做烧录口的开发板，在切换为 HID 后串口可能消失；监视器的重连提示本身不能说明固件故障。

项目作者已实机确认 5.9.14 的按键和摇杆工作正常。5.9.15 增加本次审查修复，变更与实机状态见[发布说明](docs/release-v5.9.15.md)，验证范围见[审查记录](docs/release-audit-v5.9.15.md)。

## 许可证

MIT。见 [LICENSE](LICENSE)。

## 致谢

基于社区「新和联胜 Pro2 Bridge」研究构建。与任天堂、索尼、微软、乐鑫无关。
