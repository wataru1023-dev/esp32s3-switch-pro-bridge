# ESP32-S3 Switch Pro Bridge

Turn a Nintendo Switch 2 Pro controller into a wired **Nintendo Switch Pro Controller** for macOS.

[English](README.md) · [中文](README.zh-CN.md) · [日本語](README.ja.md)

This ESP-IDF firmware runs on an ESP32-S3. It connects to a real Switch 2 Pro controller ("Pro2") over Bluetooth Low Energy and exposes it to a Mac as a native Nintendo Switch Pro Controller (USB VID `057E`, PID `2009`). macOS recognizes it through the built-in Game Controller framework — no extra drivers required.
Due to near-flawless emulation of the Pro Controller, preliminary tests show it is also functional on Windows.

## Features

- Native macOS recognition as "Nintendo Switch Pro Controller"
- Full button and analog-stick passthrough
- Nintendo HD Rumble passthrough (frequency + amplitude)
- Automatic BLE reconnect
- Adjustable USB report rate (default 66 Hz, matching the real controller)

## Hardware

- An ESP32-S3 board (N16R8 recommended)
- A Nintendo Switch 2 Pro controller
- A USB cable to the Mac

## How it works

```text
Pro2  --BLE-->  ESP32-S3  --USB-->  macOS (Switch Pro Controller)
```

The firmware acts as a BLE central: it scans for and connects to the Pro2, parses its input notifications, normalizes the state, and re-emits it as a standard Switch Pro USB HID report. HD rumble coming from the host is decoded and sent back to the Pro2 over its BLE rumble stream.

## Build

Requirements: ESP-IDF v5.x. The `esp_tinyusb` component is fetched automatically.

```bash
idf.py set-target esp32s3
idf.py build
```

## Flash

```bash
idf.py -p /dev/cu.usbmodemXXXX flash
```

On macOS, find the port with `ls /dev/cu.*`.

## Usage

1. Flash the firmware.
2. Connect the ESP32-S3's native USB port to the Mac.
3. Turn on the Pro2 and put it into pairing mode.
4. The firmware auto-connects. macOS then shows the device as "Nintendo Switch Pro Controller" in System Settings → Game Controllers.

Serial console commands (115200 baud):

| Command | Description |
| --- | --- |
| `status` | Show connection and report status |
| `ble scan` | Scan for the Pro2 |
| `ble connect <addr>` | Connect to a specific address |
| `ble forget` | Clear the saved BLE target |
| `rate <hz>` | Set the USB report rate (20–1000) |
| `rumble` | Run a rumble self-test |
| `reboot` | Reboot the board |

## License

MIT. See [LICENSE](LICENSE).

## Acknowledgements

Built on community "XinHeLianSheng Pro2 Bridge" research. Not affiliated with Nintendo, Sony, Microsoft, or Espressif.
