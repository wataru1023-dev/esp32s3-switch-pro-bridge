# Switch Pro USB 校准协议排查证据

## 本次日志能确定什么

2026-10-09 用户日志中的 `AXIS_DEBUG` 显示 BLE 输入及归一化输出持续变化：

```text
out=[4094,947,2048,2048]
out=[2048,2048,2665,3976]
out=[2048,2048,0,2048]
```

这支持“两个摇杆的数据已经到达固件”的结论。日志片段未包含 USB IN
报文或主机收到的数据，不能单凭此片段确认 USB 发送成功或主机完成校准。

## 已安装 macOS 驱动的可复核行为

只读取本机系统文件，未修改系统、连接设备或执行烧录：

```text
/System/Library/HIDPlugins/ServicePlugins/JoyConHIDServicePlugin.plugin/Contents/MacOS/JoyConHIDServicePlugin
SHA-256: cf1ea1458629f5e79e8757ac4bf7aaa547c234436132b736f6767791bd739a9d
架构: arm64e
```

复核命令：

```sh
strings /System/Library/HIDPlugins/ServicePlugins/JoyConHIDServicePlugin.plugin/Contents/MacOS/JoyConHIDServicePlugin
otool -arch arm64e -tvV /System/Library/HIDPlugins/ServicePlugins/JoyConHIDServicePlugin.plugin/Contents/MacOS/JoyConHIDServicePlugin
otool -arch arm64e -ov /System/Library/HIDPlugins/ServicePlugins/JoyConHIDServicePlugin.plugin/Contents/MacOS/JoyConHIDServicePlugin
```

以下为该二进制的虚拟地址；系统升级后地址可能变化。

| SPI 地址 | 长度 | 用途 | 驱动调用附近地址 |
| --- | ---: | --- | --- |
| `0x603d` | 18 | 左右工厂摇杆校准 | `0x7230` |
| `0x8010` | 22 | 用户摇杆校准，含左右 magic | `0x7668` |
| `0x6086` | 18 | 左摇杆参数 | `0x7a20` |
| `0x6098` | 18 | 右摇杆参数 | `0x7dac` |
| `0x6020` | 24 | 工厂 IMU 校准 | `0x68ec` |
| `0x8026` | 26 | 用户 IMU 校准，含 magic | `0x6950` |
| `0x6080` | 6 | IMU 水平偏移 | `0x69b4` |

### 为什么会出现按键正常、两个摇杆都没有输入

完整输入报告处理处：

```asm
0xa600  ldrb w8, [x8]                 ; _setupState
0xa608  tbz  w8, #0x2, 0xa8f0       ; 未完成校准，跳过摇杆处理
0xa60c  ldrh w8, [x21, #0x6]         ; 开始解包左摇杆
```

`0xa8f0` 分支仍处理运动与按键信息。这样，校准完成标志缺失时，主机可能
保持两个摇杆为零，而按键继续工作。校准完成函数 `0x6b74` 要求：

- 工厂左右摇杆读取标志都存在（`0x6ba8` 检查字段 `+0x18` 的两位）。
- 左右摇杆参数读取标志都存在（`0x6bb8` 检查 `+0x6c` 的两位）。
- 工厂 IMU 校准与水平偏移读取标志都存在（`0x6bc4` 检查 `+0x8e`）。
- 用户 IMU 读取已结束，即使结果表示不存在用户校准（`0x6bd4` 检查 `+0xae`）。

这证明了该主机驱动中存在与用户症状一致的机制；尚未抓取故障发生时的
主机 USB 报文，因此不能把它当作本次设备已经完成实机复现的证明。

### 校准数据的布局

`0x7300` 至 `0x73c4` 的解码显示：

- 左摇杆 9 字节：正向范围、中心、负向范围；各为一对打包的 12-bit 数值。
- 右摇杆 9 字节：中心、负向范围、正向范围。
- 用户摇杆校准仅在对应 magic 为小端 `b2 a1` 时覆盖工厂校准。

摇杆参数 18 字节包含 12 个打包值。`0x7af4` 起解包结果写入
`unknown1/unknown2/innerDeadZone/unknown4…unknown12`。
**内死区位于第二对的第一个值，即字节 3–5 的前 12 位。**
`noiseBuffer` 和 `outerDeadZone` 是主机额外初始化的字段，不能放到参数块首部。

Pro 控制器默认噪声半径 15、外半径 1236（`0x6c30` 至 `0x6c50`）。
正负范围差不超过 1 的工厂校准会触发兼容分支（`0x6d48` 起），使用工厂范围
作为外半径。中心 2048、负向范围 2048、正向范围 2047 对应固件发送的
`0…4095`，可与该兼容分支一致。

## 修复覆盖

`main/usb/switch_legacy_protocol.c` 提供完整校准区域，支持区域内偏移及跨区读取，
输出长度受目标容量限制。未写入的 SPI 区域和用户校准返回 `0xff`。
左、右参数使用小内死区 64。设备信息 ACK 为 `0x82`、SPI 读取 ACK 为 `0x90`，
普通确认 ACK 为 `0x80`。

传输层还必须保证校准应答在 USB IN 端点忙时等待并重试，不能直接丢弃。
数据内容修复与可靠传输共同覆盖上述初始化机制。

`usb_hid_device.c` 现在将应答排入 FIFO，优先于普通输入提交；提交成功后仍
保留应答，直到完成回调确认成功，异步失败可重试。会话重置、提交及完成使用
同一 mutex。应用保留 in-flight 报告副本，避免 TinyUSB 提前清除端点忙状态后
另一核覆盖完成回调缓冲区。实际上报统计在输入传输完成时记录。

排障固件 `5.9.14` 每秒自动打印 `USB_DIAG_V2`。发布版 `5.9.15` 默认关闭连续
诊断输出，通过固件串口命令 `debug on` 可开启 USB 诊断，通过
`axis debug on 32` 可开启摇杆采样。字段含义如下：

| 字段 | 判断 |
| --- | --- |
| `mounted=0` | 原生 USB 手柄接口尚未枚举；串口桥存在不代表 HID 已连接 |
| `axes` | 本轮 BLE 状态经桥接开关及过期检查后准备发送的四轴 |
| `tx` / `done` | 累计提交 / 已成功完成的普通输入报告；`done` 递增才能确认传输完成 |
| `usb_axes` | 最近一次传输完成的四轴；其变化确认四轴到达 USB 主机传输层 |
| `reply` | 应答排队、提交、完成、失败、未完成及队列满计数 |
| `enabled=0` / `blocked` | 主机初始化握手尚未开启普通输入；被门控不再计为发送成功 |

`ready` 是瞬时状态，正常提交后也会变为 0，不能仅用它判断持续故障。
传输完成仍不证明 macOS GameController 已解析摇杆。`usb_axes` 变化而游戏
仍无轴输入时，应结合主机的初始化应答及 GameController 结果判断。

2026-10-09 排障期间的一次只读检查主机 USB 枚举仅见 WCH `1a86:55d3`，名称
`USB Single Serial`，对应外部烧录串口。未见 Nintendo `057e:2009`；这是检查当时
的状态，不能直接反推此前每个故障时刻。日志与烧录走外部串口桥，使用手柄
接口还需要连接板子的原生 USB（USB/OTG）数据口。

烧录指令（由用户执行，需先退出占用串口的监视器）：

```sh
# 从下载的仓库根目录进入工程。
cd esp32s3-switch-pro-bridge
. ~/.espressif/v5.3.3/esp-idf/export.sh
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

用 `ls /dev/cu.*` 获取实际端口。启动日志应显示当前固件版本。烧录后重接原生
USB，使 macOS 重新执行初始化与校准读取。

主机回归测试：

```sh
clang -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -I main/usb main/usb/switch_legacy_protocol.c tests/test_switch_legacy_protocol.c \
  -o /private/tmp/test_switch_legacy_protocol
/private/tmp/test_switch_legacy_protocol
```

覆盖左右布局、实际轴范围、区域内读取、跨空隙读取、跨左右参数读取、
用户 magic 缺失、长度裁剪、地址边界、ACK 类型；ASan/UBSan 检查通过。

传输回归直接编译实际固件 USB 源码，仅替换平台服务：

```sh
sh tests/run_usb_hid_transport.sh
```

覆盖端点忙/同步失败/异步失败的应答保留、FIFO 与优先级、输入门控、完成
缓冲区覆盖、会话重置及挂起恢复。标准编译和 ASan/UBSan 均通过。
ESP-IDF v5.3.3 编译成功。随后项目作者自行烧录 `5.9.14`，于 2026-10-09
确认按键和摇杆均恢复正常。这是用户报告的实机结果；未进行长时间压力测试。
`5.9.15` 的审查修复和验证范围见 [发布说明](release-v5.9.15.md) 与
[审查记录](release-audit-v5.9.15.md)，该版本尚未烧录验证。
