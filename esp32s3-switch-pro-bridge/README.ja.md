# ESP32-S3 Switch Pro Bridge

Nintendo Switch 2 Pro コントローラーを、macOS 上の有線 **Nintendo Switch Pro コントローラー**にします。

[English](README.md) · [中文](README.zh-CN.md) · [日本語](README.ja.md)

この ESP-IDF ファームウェアは ESP32-S3 上で動作します。実際の Switch 2 Pro コントローラー（Pro2）に Bluetooth Low Energy で接続し、それをネイティブの Nintendo Switch Pro コントローラー（USB VID `057E`、PID `2009`）として Mac に公開します。macOS は内蔵の Game Controller フレームワークで認識するため、ドライバーは不要です。
以前の予備テストでは Windows でも動作しました。このプロジェクトの主な対象は macOS です。

## 特徴

- macOS が「Nintendo Switch Pro Controller」としてネイティブ認識
- 全ボタン + アナログスティックのパススルー
- Nintendo HD 振動のパススルー（周波数 + 振幅）
- BLE 自動再接続
- USB レポートレートを調整可能（既定 66Hz、実機と同じ）

## 必要なハードウェア

- 16 MB フラッシュと 8 MB Octal PSRAM を搭載した ESP32-S3 ボード（公開ファームウェアの対象は N16R8）
- Nintendo Switch 2 Pro コントローラー
- Mac につなぐ USB ケーブル

## 仕組み

```text
Pro2  --BLE-->  ESP32-S3  --USB-->  macOS（Switch Pro コントローラー）
```

ファームウェアは BLE セントラルとして動作します。Pro2 をスキャンして接続し、入力通知を解析して状態を正規化し、標準の Switch Pro USB HID レポートとして再送します。ホストからの HD 振動はデコードされ、Pro2 の BLE 振動ストリームへ送り返されます。

## ビルド

検証済みツールチェーン: **ESP-IDF v5.3.3**。対応する環境を有効にしてから、このディレクトリで次のコマンドを実行してください（リポジトリには `esp32s3-switch-pro-bridge/` サブディレクトリがあります）。コンポーネント管理ツールは `dependencies.lock` に記録された依存バージョンを取得します。このファイルを保持してください。他の ESP-IDF バージョンは今回のリリースでは未検証です。

```bash
idf.py set-target esp32s3
idf.py build
```

## 書き込み

```bash
idf.py -p /dev/cu.usbmodemXXXX flash
```

macOS では `ls /dev/cu.*` でポートを確認します。書き込みとログには UART / 書き込み用ポートを使用してください。書き込み前に、そのポートを使用しているモニターを終了してください。`idf.py monitor` は **Ctrl + ]** で終了します。

公開ファームウェアは同じ N16R8 設定でビルドされています。アーカイブには `flash_args` と個別の bootloader、パーティションテーブル、アプリイメージが含まれます。付属の説明に従ってください。同じパーティション配置から更新する場合、NVS 設定は保持されます。他のボードでは `menuconfig` で設定を調整し、ソースからビルドしてください。

## 使い方

1. ファームウェアを書き込みます。
2. ESP32-S3 のネイティブ USB / USB-OTG データポートを Mac に接続します。UART / 書き込み用ポートだけではコントローラーは認識されません。
3. Pro2 の電源を入れ、ペアリングモードにします。
4. ファームウェアが自動接続し、macOS の「システム設定 → ゲームコントローラー」に「Nintendo Switch Pro Controller」と表示されます。

シリアルコンソールコマンド（115200 baud）:

| コマンド | 説明 |
| --- | --- |
| `status` | 接続・レポート状態を表示 |
| `debug on` / `debug off` | USB 診断を含む詳細ログの有効化 / 無効化 |
| `axis debug on 32` / `axis debug off` | スティックのサンプリングログの有効化 / 無効化 |
| `raw debug on 32` / `raw debug off` | BLE パケット・IMU サンプリングログの有効化 / 無効化 |
| `ble scan` | Pro2 をスキャン |
| `ble connect <addr>` | 指定アドレスに接続 |
| `ble forget` | 保存済み BLE ターゲットを消去 |
| `rate <hz>` | USB レポートレートを設定（20–1000） |
| `rumble` | 振動セルフテスト |
| `reboot` | 再起動 |

これらはファームウェアのシリアルコンソール用コマンドであり、シェルコマンドではありません。公開版では連続したスティック・USB 診断ログを既定で無効にしています。ネイティブ USB と書き込みポートが共通のボードでは、HID への切り替え時にシリアルポートが消えることがあります。モニターの再接続表示だけでファームウェアの異常とは判断できません。

5.9.14 のボタンとスティックはプロジェクト作者によって実機動作が確認されました。5.9.15 の変更と実機検証状況は[リリースノート](docs/release-v5.9.15.md)、検証範囲は[監査記録](docs/release-audit-v5.9.15.md)を参照してください。

## ライセンス

MIT。 [LICENSE](LICENSE) を参照。

## 謝辞

コミュニティの「XinHeLianSheng Pro2 Bridge」研究に基づいています。任天堂、ソニー、マイクロソフト、Espressif とは無関係です。
