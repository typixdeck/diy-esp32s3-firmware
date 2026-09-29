# 0.3.1 启动诊断候选

最新：2026-09-30 已更新并验证 0.3.2 的 Wi-Fi 开启/扫描，见文末；以下保留启动恢复的完整过程。

2026-09-29：用户报告 Copilot 写入 DIY 后共享屏幕反复显示开机画面。旧 CM4 地址 SSH 超时；本次局域网可达 SSH 主机均未匹配此前已知 CM4 主机密钥。尚未读取这次 Copilot 事务，不能确认到底完成了哪次写入、读回或重启，也不能推断树莓派是否掉电。

0.3.0 配置没有持久化 core dump；ESP-IDF panic 输出在 UART0，应用 UAC+CDC 并不是 UART 控制台。它输出音频/触摸/键盘相关数据，禁止直接采集完整 CDC 流当作启动日志。新诊断不能追溯重建 0.3.0 的旧 panic。

## 本地已修正的问题

音频驱动此前在 I2S 初始化错误、codec 分配/打开失败时通过 assert/ESP_ERROR_CHECK 终止整个程序。0.3.1 改为可选服务错误返回，逐层回收资源，完整成功后原子发布句柄；音频不可用时录音回调已有静音降级，继续尝试 USB 维护接口。内部 SDK 故障或 USB 初始化失败本身仍可能使维护口不可达，不能承诺所有错误都能通过 CDC 恢复。

此前 Wi-Fi 默认关闭仍会初始化整个驱动；现在按需初始化，失败不在同一次启动中反复分配。CDC 统计 snprintf 的返回值也按实际缓冲容量限长，避免截断时读取越界。

不更改 AW9523 的 CONFIG→OUTPUT 初始化顺序、CM_PMIC_EN 高阻、显示 MUX、电源/复位或 Flash 分区布局。不自动因连续重启清空 NVS、切换硬件电源、刷写或恢复备份。

## BOOT_STATUS

仅接受完全匹配的 ASCII 命令 `BOOT_STATUS` 加换行，回复固定 `TD_DIAG v=1` 单行；连接时也请求输出一次，有 TX 空间才发送。主机采集器只能保留完整校验过的这一行字段，忽略其他 CDC 数据。串口访问仍需按 Copilot 板配置绑定物理端口、取得维护互斥锁，并确认无刷写进行。

| 字段 | 含义 |
| --- | --- |
| `fw` | 编译固件版本 |
| `reset` | SDK 提供的上次复位类别，如 poweron、panic、task-wdt、brownout；不是确定的源码根因 |
| `retained` | 是否有校验通过的上一轮 RTC 记录 |
| `boots` | 有效暖复位链中的启动次数，最大 65535；并非终生使用统计 |
| `prev_main` / `prev_audio` | 上一轮最后记录的两条任务进度；并非异常发生任务的证明 |
| `main` / `audio` | 本轮进度 |
| `audio_err` / `usb_err` | 当前可选服务错误码；阶段尚未执行时 0 不表示已就绪 |
| `heap` / `largest` | 当前内部 8-bit 可分配内存/最大连续块，字节 |
| `up` | 本次启动后的秒数 |

主任务阶段：0 尚未开始；1 入口；2 I2C/AW；3 LCD SPI/触摸；4 RGB；5 UI；6 服务启动；7 显示主循环。音频任务阶段：0 未开始；1 入口；2 codec；3 USB；4 USB/CDC 任务已创建（不表示主机已经成功枚举）。

RTC 仅存固定阶段和计数及反码校验。只有软件重启、panic、看门狗等暖复位且记录有效时才沿用；断电、棕断或损坏时清除。无 MAC、设备序列号、账号、SSID、键盘、音频或 Flash 内容。没有启用 Flash coredump。

API 依据：[ESP-IDF 5.5.1 reset reason](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/system/misc_system_api.html#reset-reason)、[内存类型](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/memory-types.html)。RTC 实机保持效果仍待验收。

## 设备恢复可达后的验收

1. 先验证为原 CM4；读取 uptime、最近 Copilot 固定字段事务、绑定端口 USB 重枚举计数。核实备份、write_started、写入及读回验证结果。
2. 先保存设备已有固定字段证据，保持私有 Flash/NVS 备份留在真机。不直接复用绑定旧任务的恢复脚本。
3. 通过现有受审计写入路径部署候选，不能跳过安全供电/端口/完整备份/读回步骤；维护模式不可达时停止，不猜 GPIO。
4. 确认返回运行态及实际 `fw=0.3.1`，观察持续运行、Pi/ESP 显示切换、音频和开启/关闭 Wi-Fi。记录结构化复位类别和阶段，不以“写入成功”代替启动验收。
5. 通过真实运行验证后才更新在线 firmware 目录和状态说明；保留 0.3.0 镜像及对应 ELF 用于核对。

## 2026-09-30 外部 USB 实测

用户纠正 USB 接线后，Mac 稳定识别板载 Hub、键盘和 ROM 下载接口。会话设备访问权限恢复后，真实 ROM 检查确认 ESP32-S3、8 MB Flash，未启用安全启动、加密或安全下载模式。用户明确要求跳过不可用旧固件的备份；此次没有创建旧 Flash/NVS 备份。

- 使用 esptool 5.3.1、固定 SHA256 的 ESP32-S3 v1.2.2 RAM stub；没有通用 DTR/RTS 复位、自动写入重试或重新绑定其他串口。
- 写入 0x0 的完整镜像为 3,997,320 字节，SHA256 `e88e1595238c8fe8948b1a3e575cb176c80101fdb38938cb1bc081979a5efbb2`。独立读取同样长度后，逐字节和 SHA256 均匹配。
- 写入、读回及返回运行态共约 58.5 秒。通过既有受审计看门狗复位路径返回 UAC+CDC，本轮未要求额外手动按键复位。
- 连续观察 150.3 秒，收到 15 条有效 `BOOT_STATUS`：`fw=0.3.1`，uptime 从 26 增至 167 秒，`boots=1` 不变，USB 断连 0 次。
- 所有记录均为 `main=7`、`audio=4`、`audio_err=0`、`usb_err=0`。之后读取完整 1024×768 framebuffer，传感器界面正常绘制；截图后的诊断 uptime=214 秒，启动计数仍为 1。
- 用户确认“界面正常，未反复重启”。因此本次外部 USB 写入、启动与基本界面验收通过。

这是一轮短时启动验证；未验证持续数小时运行、冷启动重复性、Wi-Fi、实际音频、Pi/ESP 切屏或 Pi 内部 Copilot 刷写。截图里电池百分比为 0%、电压为 4.20 V，该读数差异另待调查，不能把界面正常等同所有传感器数据正确。未取得旧版 panic，仍不能确认旧固件重启的具体原因。在线 firmware 目录尚未更新。

## 0.3.2 Wi-Fi 启动修正与实测

用户在 0.3.1 复位后仍报告 Wi-Fi 失败。当时诊断 uptime=83 秒、内部 heap=13,319 bytes、最大连续块=8,192 bytes，主显示、音频及 USB 仍运行。旧诊断未输出 Wi-Fi SDK 错误码，所以当时的确切返回码不可追溯；内存配置是基于可测内存压力和源码的修正方向，不把推断写成已取得的旧错误码。

配置依据为 [ESP-IDF 5.5.1 RAM 优化](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/performance/ram-usage.html) 和本机同版本 Kconfig。将 malloc 内部优先阈值由 16 KB 改为 2 KB、启用 Wi-Fi/lwIP 优先 PSRAM、关闭 Wi-Fi IRAM/RX IRAM 优化、静态 RX 缓冲 10→6、动态收发缓冲 32→16。静态 RX 数量仍不小于 AMPDU RX BA window=6。保留 LCD ISR、DMA、全部任务栈与内部保留池配置。

Wi-Fi 服务新增保留初始化步骤和 SDK 错误码，`WIFI_STATUS` 返回 `TD_NET v=1 state=… enabled=… connected=… aps=… error=… stage=… sdk_err=…`，仅固定数值，不包含网络身份或凭据。步骤 1=netif、2=event loop、3=STA netif、4=Wi-Fi driver、5/6=事件处理器、7=RAM storage、8=STA mode、9=节能配置、10=start、11=ready。关闭 Wi-Fi 后 stage=11 可以保留，当前状态仍须结合 enabled/state 判断。新增本地维护 `WIFI_ON/OFF/SCAN`，只排队到既有服务，不提供远端参数或密码设置。

本轮构建与 ASan/UBSan 本地检查通过，包括故障返回码及诊断长度/敏感字段排除。构建中的 Flash HPM 提示和既有 i18n 缺失初始化警告已记录；与 0.3.1 的 sdkconfig 对比仅上述内存/Wi-Fi项及其兼容别名变化。

设备升级前核实运行 fw=0.3.1 和 AW CONFIG_P0 安全状态；保持 CDC 打开完成一次维护模式转换。ROM 中验证既有 bootloader/分区/字体与上一轮已校验镜像一致，再只写 0x10000 应用区，保留 NVS 与字体；按用户要求不备份旧固件。0.3.2 app 共 1,544,768 字节，SHA256 `aaa203c462a0f914bb78f7c71a0f3fc4e0e1966a5e24f17b911195d19dd6dc3c`，读回完全匹配，43.2 秒返回运行态。

真机测试从 uptime=100 秒开始：关闭 Wi-Fi 时内部 heap=87,519 bytes；通过同一服务开启后 stage=11、sdk_err=0，扫描返回 5 个 AP。连续观察 90.4 秒，heap 稳定约 54,791 bytes、最大块 31,744 bytes，uptime 增至 189 秒且 boots=1，audio_err/usb_err=0。本轮已经验证开启和扫描，尚未取得用户网络的连接与 NTP 验收；扫描到网络不等于已联网。没有保存 SSID、密码或完整 CDC 流。
