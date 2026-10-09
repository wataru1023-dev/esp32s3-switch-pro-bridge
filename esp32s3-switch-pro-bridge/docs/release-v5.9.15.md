# v5.9.15 — USB 摇杆校准与稳定性更新

本次更新修复 macOS 上偶发“按键正常、两个摇杆没有输入”的 USB 初始化问题，
并补充输入边界、配置保存及重连会话保护。GitHub `main` 的更新基线是
`dd7312505aace3ef12051aa33d0d70060a52921f`（源码工程版本 `5.9.8`）。

## 修复

- 补齐 Switch Pro 左右工厂摇杆、参数及 IMU 校准区域，正确返回用户校准不存在的值；
  支持 SPI 区域内及跨区读取，修正设备信息与 SPI 读取 ACK。
- USB 初始化与校准应答按 FIFO 排队，端点忙、提交失败、异步失败时保留并重试；
  普通输入在应答之后发送，传输完成才计入成功统计。
- 检查 HID 报文长度，对超过报告容量的 SPI 读取请求返回失败；检查 USB 控制传输方向，
  避免错误方向的请求写入只读数据；USB 会话重置清除旧 vendor 应答。
- BLE 订阅与读轮询记录连接代次，忽略重连后的旧回调；订阅调度转到 NimBLE host 事件，
  减少断开和重连时的调用竞争。诊断读取不再训练摇杆中心，校准与诊断设置同步访问。
- 串口超长命令整行丢弃，禁止执行截断后的前缀；严格解析数字参数，正确转义 JSON，
  将操作和 NVS 保存失败返回给调用者。
- 配置标量使用原子读写，BLE 目标通过锁保护的副本读取；保存串行化，成功提交后才更新
  运行态；超过 39 字节的 BLE 目标直接拒绝，不静默截断。
- 增加状态接口的空指针与枚举检查；固件版本取自 ESP-IDF 应用描述，避免版本号分叉。
- 修复标准 neutral 振动包被误判为有振幅、子命令包振动未转发的问题；停止包发送失败时
  保留重试，旧写入完成不再消费新停止请求。

## 发布默认值

连续摇杆 / USB 诊断默认关闭，`debug on`、`axis debug on 32`、
`raw debug on 32` 仍可开启对应日志。默认控制台使用 UART，关闭与原生 USB HID
共享接口的第二串口输出；TinyUSB 内部调试为 0，应用 DEBUG 日志保留编译支持。
PSRAM、Flash 和 66 Hz 默认上报率沿用已使用的配置，未更换依赖版本。

## 验证状态

- 项目作者在 2026-10-09 自行烧录 `5.9.14`，确认按键与摇杆功能正常。
- `5.9.15` 的六组主机回归及 AddressSanitizer / UndefinedBehaviorSanitizer 检查通过，
  配置并发测试也通过 ThreadSanitizer。当前工程和干净源码均在 ESP-IDF v5.3.3 下编译成功，
  发布固件来自干净构建；完整验证记录见
  [审查记录](release-audit-v5.9.15.md)。
- `5.9.15` 尚未烧录实机验证，未验证长时间运行、频繁断连、睡眠唤醒或其他主机平台。

## 下载包与升级

- `esp32s3-switch-pro-bridge-v5.9.15-source.zip`：可提交 GitHub 的工程源码，保留仓库的 `esp32s3-switch-pro-bridge/` 目录层级。
  包含 `dependencies.lock`、测试与说明，排除本机构建目录、自动下载依赖与个人配置。
- `esp32s3-switch-pro-bridge-v5.9.15-n16r8-firmware.zip`：仅适用于 16 MB Flash、8 MB Octal PSRAM 的 ESP32-S3。
  包含分开的 bootloader、分区表、应用镜像和烧录参数，按包内说明烧录。
- `SHA256SUMS`：发布附件校验值；`RELEASE_AUDIT.md`：独立审查记录。

Manager 控制请求的长度与分包约束见源码中的 `docs/manager_control_protocol.md`。
实时 USB 输出目前仅为 `0x30` 模式，HD 振动保留现有简化振幅映射。

升级使用独立镜像按偏移写入，保持相同分区布局下的 NVS 设置。不要执行全片擦除来进行
普通升级。烧录后重新连接开发板的原生 USB / USB-OTG 数据口，让主机重新读取校准。
UART / 烧录口用于编程和日志，原生 USB 数据口用于手柄连接。

## English release summary

Fix intermittent macOS loss of both analog sticks while buttons remain active: provide complete
Switch Pro calibration data and ACKs, queue initialization replies, and retry busy / failed USB
transfers. Harden malformed USB and console input, isolate reconnect sessions, synchronize
calibration and configuration access, and keep diagnostic logging opt-in.

Built for the existing ESP32-S3 N16R8 configuration with ESP-IDF 5.3.3 and the committed dependency
lock. The owner confirmed working buttons and sticks on 5.9.14. Version 5.9.15 passed all six host
regression suites, ASan/UBSan, configuration TSan and both existing and clean-source builds.
The firmware is from the clean build and has not been flashed to hardware. See the audit notes
for the verification limits. Live USB output supports report mode 0x30; rumble keeps the existing
simplified amplitude mapping.
