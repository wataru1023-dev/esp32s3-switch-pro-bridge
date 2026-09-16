# ESP32-S3 Switch Pro Bridge

Nintendo Switch 2 Pro コントローラーを、macOS 上の有線 **Nintendo Switch Pro コントローラー**にします。

[English](README.md) · [中文](README.zh-CN.md) · [日本語](README.ja.md)

この ESP-IDF ファームウェアは ESP32-S3 上で動作します。実際の Switch 2 Pro コントローラー（Pro2）に Bluetooth Low Energy で接続し、それをネイティブの Nintendo Switch Pro コントローラー（USB VID `057E`、PID `2009`）として Mac に公開します。macOS は内蔵の Game Controller フレームワークで認識するため、ドライバーは不要です。
Proコントローラーを極めて高精度にエミュレートしているため、予備テストの段階ではWindows環境でも使用可能です。

## 特徴

- macOS が「Nintendo Switch Pro Controller」としてネイティブ認識
- 全ボタン + アナログスティックのパススルー
- Nintendo HD 振動のパススルー（周波数 + 振幅）
- BLE 自動再接続
- USB レポートレートを調整可能（既定 66Hz、実機と同じ）

## 必要なハードウェア

- ESP32-S3 ボード（N16R8 推奨）
- Nintendo Switch 2 Pro コントローラー
- Mac につなぐ USB ケーブル

## 仕組み

```text
Pro2  --BLE-->  ESP32-S3  --USB-->  macOS（Switch Pro コントローラー）
```

ファームウェアは BLE セントラルとして動作します。Pro2 をスキャンして接続し、入力通知を解析して状態を正規化し、標準の Switch Pro USB HID レポートとして再送します。ホストからの HD 振動はデコードされ、Pro2 の BLE 振動ストリームへ送り返されます。

## ビルド

依存: ESP-IDF v5.x（`esp_tinyusb` コンポーネントは自動取得）。

```bash
idf.py set-target esp32s3
idf.py build
```

## 書き込み

```bash
idf.py -p /dev/cu.usbmodemXXXX flash
```

macOS では `ls /dev/cu.*` でポートを確認します。

## 使い方

1. ファームウェアを書き込みます。
2. ESP32-S3 のネイティブ USB ポートを Mac に接続します。
3. Pro2 の電源を入れ、ペアリングモードにします。
4. ファームウェアが自動接続し、macOS の「システム設定 → ゲームコントローラー」に「Nintendo Switch Pro Controller」と表示されます。

シリアルコンソールコマンド（115200 baud）:

| コマンド | 説明 |
| --- | --- |
| `status` | 接続・レポート状態を表示 |
| `ble scan` | Pro2 をスキャン |
| `ble connect <addr>` | 指定アドレスに接続 |
| `ble forget` | 保存済み BLE ターゲットを消去 |
| `rate <hz>` | USB レポートレートを設定（20–1000） |
| `rumble` | 振動セルフテスト |
| `reboot` | 再起動 |

## ライセンス

MIT。 [LICENSE](LICENSE) を参照。

## 謝辞

コミュニティの「XinHeLianSheng Pro2 Bridge」研究に基づいています。任天堂、ソニー、マイクロソフト、Espressif とは無関係です。
