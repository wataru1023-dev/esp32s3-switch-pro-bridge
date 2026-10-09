# ESP32-S3 Switch Pro Bridge

Turn a Nintendo Switch 2 Pro controller into a wired **Nintendo Switch Pro Controller** for macOS.

[English](README.md) · [中文](README.zh-CN.md) · [日本語](README.ja.md)

This ESP-IDF firmware runs on an ESP32-S3. It connects to a real Switch 2 Pro controller ("Pro2") over Bluetooth Low Energy and exposes it to a Mac as a native Nintendo Switch Pro Controller (USB VID `057E`, PID `2009`). macOS recognizes it through the built-in Game Controller framework — no extra drivers required.
Preliminary tests also found it functional on Windows; the project is primarily used on macOS.

## Features

- Native macOS recognition as "Nintendo Switch Pro Controller"
- Full button and analog-stick passthrough
- Nintendo HD Rumble passthrough (frequency + amplitude)
- Automatic BLE reconnect
- Adjustable USB report rate (default 66 Hz, matching the real controller)

## Hardware

- An ESP32-S3 board with 16 MB flash and 8 MB octal PSRAM (N16R8, the published firmware target)
- A Nintendo Switch 2 Pro controller
- A USB cable to the Mac

## How it works

```text
Pro2  --BLE-->  ESP32-S3  --USB-->  macOS (Switch Pro Controller)
```

The firmware acts as a BLE central: it scans for and connects to the Pro2, parses its input notifications, normalizes the state, and re-emits it as a standard Switch Pro USB HID report. HD rumble coming from the host is decoded and sent back to the Pro2 over its BLE rumble stream.

## Build

Tested toolchain: **ESP-IDF v5.3.3**. Activate its environment before running these commands from this directory (the repository contains an `esp32s3-switch-pro-bridge/` subdirectory). The component manager downloads the versions recorded in `dependencies.lock`; keep that file for repeatable builds. Other ESP-IDF versions have not been verified for this release.

```bash
idf.py set-target esp32s3
idf.py build
```

## Flash

```bash
idf.py -p /dev/cu.usbmodemXXXX flash
```

On macOS, find the port with `ls /dev/cu.*`. Use the board's UART / programming port for flashing and logging. Stop any monitor occupying that port before flashing. In `idf.py monitor`, **Ctrl + ]** exits to the shell.

Release firmware uses the same N16R8 configuration. Its archive contains `flash_args` and separate bootloader, partition-table, and application images; follow the included instructions. This layout preserves existing NVS settings when upgrading from the same partition layout. Other boards require an appropriate `menuconfig` configuration and a source build.

## Usage

1. Flash the firmware.
2. Connect the ESP32-S3's native USB / USB-OTG data port to the Mac. The UART / programming port alone does not expose the controller.
3. Turn on the Pro2 and put it into pairing mode.
4. The firmware auto-connects. macOS then shows the device as "Nintendo Switch Pro Controller" in System Settings → Game Controllers.

Serial console commands (115200 baud):

| Command | Description |
| --- | --- |
| `status` | Show connection and report status |
| `debug on` / `debug off` | Enable / disable detailed logs, including USB diagnostics |
| `axis debug on 32` / `axis debug off` | Enable sampled stick logs / disable them |
| `raw debug on 32` / `raw debug off` | Enable sampled BLE packet / IMU logs / disable them |
| `ble scan` | Scan for the Pro2 |
| `ble connect <addr>` | Connect to a specific address |
| `ble forget` | Clear the saved BLE target |
| `rate <hz>` | Set the USB report rate (20–1000) |
| `rumble` | Run a rumble self-test |
| `reboot` | Reboot the board |

These are firmware console commands, not shell commands. The default release logs avoid continuous axis and USB dumps. A board whose native USB port is also its programming port can disappear from the serial monitor when USB switches to HID; a monitor reconnect message does not by itself indicate a firmware fault.

Version 5.9.14's button and stick operation was confirmed by the project owner on hardware. Version 5.9.15 adds the review fixes described in [release notes](docs/release-v5.9.15.md); its hardware status and build / regression evidence are recorded there and in the [audit notes](docs/release-audit-v5.9.15.md).

## License

MIT. See [LICENSE](LICENSE).

## Acknowledgements

Built on community "XinHeLianSheng Pro2 Bridge" research. Not affiliated with Nintendo, Sony, Microsoft, or Espressif.
