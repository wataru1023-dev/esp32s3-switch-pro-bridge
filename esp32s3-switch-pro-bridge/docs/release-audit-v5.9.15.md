# v5.9.15 发布审查记录

日期：2026-10-09 至 2026-10-10。审查对象：当前工程及与 GitHub `main`
`dd7312505aace3ef12051aa33d0d70060a52921f` 的差异。未操作设备，未推送或创建 GitHub Release。

## 范围与结果

| 范围 | 发现与处理 |
| --- | --- |
| macOS 摇杆初始化 | 校准参数缺失、ACK 类型错误及端点忙丢应答；补齐协议数据并排队重试 |
| USB 传输 | 审查提交、完成、失败及会话重置；保留 in-flight 副本并按完成统计 |
| USB 输入边界 | 检查报文长度、SPI 请求容量及控制传输方向，重置清理旧 vendor 应答 |
| BLE 回调与校准 | 增加连接代次与 host 事件调度，旧回调不再作用于新会话；诊断读取不训练中心 |
| 控制命令 | 超长行拒绝、严格参数解析、JSON 转义与真实操作错误返回 |
| 振动流 | 正确识别标准 neutral，转发子命令包中的振动；停止包写入成功后才消费，旧写入不消费新停止请求 |
| 配置并发与持久化 | 原子标量、目标副本、NVS 保存串行化、提交后更新、长目标拒绝 |
| 发布工程 | 单一版本来源、固定测试工具链、依赖锁保留、默认日志降噪、公开文档移除个人路径 |

## 可复现环境

- ESP-IDF `v5.3.3`，目标 `esp32s3`。
- `dependencies.lock` 保留：`espressif/esp_tinyusb 1.7.6~2`、`espressif/tinyusb 0.19.0~3`。
  未修改依赖版本；源码中的宽版本约束与锁文件共同使用。
- `sdkconfig.defaults`：16 MB Flash / 8 MB Octal PSRAM、80 MHz PSRAM、1000 Hz FreeRTOS、
  双 HID 与一个 vendor 接口、默认 INFO / 最大 DEBUG、UART 控制台。
- 构建产物由工具链和配置生成，不能据此声称不同主机生成的二进制逐字节一致。

## 已执行的验证

配置测试实际编译 `main/config/device_config.c`，NVS 与平台服务使用主机替身，
锁使用 pthread mutex。以下命令均通过：

```sh
sh tests/run_device_config.sh
TEST_CFLAGS='-fsanitize=address,undefined -g' sh tests/run_device_config.sh
TEST_CFLAGS='-fsanitize=thread -g' sh tests/run_device_config.sh
```

覆盖：NVS open / set / commit 失败时运行态不变、上报率夹限、39 字节目标合法而
40 字节目标拒绝、目标复制边界、两个写线程与一个读线程的完整副本，以及写入结束后
NVS / 内存一致。NVS 替身的失败语义不等同于实际 Flash 掉电，未进行掉电测试。
ThreadSanitizer 在本次配置测试路径未报告竞争；未覆盖其余固件并发路径。

最终验证均通过：

```sh
sh tests/run_all.sh
TEST_CFLAGS='-fsanitize=address,undefined -g' sh tests/run_all.sh
TEST_CFLAGS='-fsanitize=thread -g' sh tests/run_device_config.sh
idf.py build
```

完整回归入口运行六组测试：Switch 校准 / ACK、USB HID 传输、配置、BLE 输入、
USB vendor 防护、控制命令。各组编译实际生产源码，平台和协议栈服务使用主机替身。
覆盖端点忙、提交与异步失败重试、会话清理、SPI 长度与 GET 缓冲区边界、错误方向的
控制请求、命令截断和数字溢出、JSON 转义、标准 neutral，以及停止包失败重试和新请求保护。
BLE 测试覆盖解析、校准隔离及连接句柄重用的代次检查 helper；未模拟完整 NimBLE 栈时序。

当前工程与独立干净源码目录均使用 ESP-IDF v5.3.3 构建成功，无编译警告。
干净目录从发布源码和 `sdkconfig.defaults` 生成配置，不带原工程的 `sdkconfig` 或 `build`；
复用了锁文件对应的组件缓存，未升级依赖。发布固件来自这次干净构建。
应用大小 `0xa19f0`（662000 字节），1 MiB 应用分区剩余 37%。

打包脚本核对应用描述中的版本 / IDF 版本、目标、Flash / PSRAM 参数与烧录偏移，
检查源码比应用镜像更新的情况。发布包保留源码层级、烧录说明、审查记录和 SHA-256。

## 实机证据与局限

项目作者已确认 `5.9.14` 的按键与摇杆可用；`5.9.15` 尚未烧录。
静态审查和主机替身测试不能证明 USB 主机枚举、NimBLE 时序或完整硬件行为。
本次未声称代码不存在全部风险，未做长时间耐久、频繁重连、主机休眠唤醒及 Windows 回归。
实时 USB 输出仅实现 `0x30` 模式；`0x3f` / `0x31` 模式及完整非线性振动映射不在本次实现范围。

发布固件限制为 N16R8。源码包排除 `build/`、`managed_components/`、本机 `sdkconfig`
及输出附件，保留锁文件。固件以独立偏移镜像发布，避免合并镜像填充空隙覆盖 NVS。
