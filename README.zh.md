# nabu-power

[English](README.md) | **中文**

Xiaomi Pad 5（`nabu`、SM8150）的实验性电池与充电支持：PM8150B SMB5 主充电器、
PMIC 燃料计 `qcom_fg` 与 LionSemi LN8000 快充 IC 的内核驱动、设备树与运行配置。

本仓库与 `nabu-iris`、`nabu-camera` 一样保存直接源码覆盖层，不包含完整
Linux 内核树、预编译 UKI 或完整模块树。内核文件保留原始相对路径，可以覆盖到
指定基线后审查和构建。

> 这是实验性代码。替换 DTB、内核或模块可能导致设备无法启动，请准备可用的恢复
> 方式。

## 当前功能

- Qualcomm PM8150B SMB5 充电器（`qcom,pm8150b-charger`）：USB 检测、5V 基础
  充电，以及 QC2/QC3 HVDCP 升压
- Qualcomm PM8150B 燃料计（`qcom,pm8150b-fg`），上报电量、电压、电流与温度，
  并在启动时加载厂商电池 profile，保证全量程 SOC 精度
- LionSemi LN8000 快充 IC（`lionsemi,ln8000`）：9V+ 输入下的 2:1 电荷泵
- 软件 JEITA：按燃料计温度限制充电电流与浮充电压，超出 -10..59°C 时停充；
  LN8000 电荷泵在 0..45°C 之外停止
- 派生设备树追加电池、充电器与 PMIC 燃料计节点

## 充电链路

A-to-C（USB-A 口）充电由三段组成，缺一不可：

```text
适配器/USB  ──5V──▶  PM8150B SMB5 充电器 ──▶  电池
                │
                └─ QC2/QC3 升压到 ~9V
                             │
                             ├─▶  SMB5 继续充电
                             └─▶  LN8000 2:1 电荷泵并联快充（需 ≥~7.6V）
```

- 只给 5V 时：LN8000 是 2:1 电荷泵，进不了 SWITCHING（回 standby），只有 SMB5
  能充，功率约 5V×2A；亮屏高负载时会贴着负载、净电流接近 0。
- 打开 SMB5 的 HVDCP（`HVDCP_EN | HVDCP_AUTH_ALG_EN |
  HVDCP_AUTONOMOUS_MODE_EN`）后，PMIC 把 A 口抬到 QC 高压（实测约 8.3V），
  SMB5 继续充，同时 LN8000 进 SWITCHING 并联快充；电池端实测约 +3.4A。

`qcom_fg` 接在 `pm8150b_charger` 上（`power-supplies`），电池状态由 SMB5 决定，
不再随瞬时净电流过零抖动；`CURRENT_NOW` 已改成标准约定（正=充电），桌面
UPower/GNOME 可正确显示充电。

充满由 SMB5 硬件 CC/CV 完成：充电到 4.47V 后恒压，电流衰减；电量掉到 99%
自动回充。PM8150B 的 ADC 充电终止在 nabu 上不工作：门限、ADC 比较器与采样
模式都已正确配置（已用寄存器回读确认），但比较器始终不翻转，即使把门限推到
-2000mA。因此当电量到 100% 且充电电流衰减到 400mA 以下时，由软件锁存
`POWER_SUPPLY_STATUS_FULL`（对应 vendor 的 `charge_full` 标志），UPower
显示已充满。

QC 识别依赖 D+/D-（DPDM）：充电器在跑 APSD 前会把 USB HS PHY 通过
`dpdm-supply` 切到 UTMI non-driving（高阻），把 Dp/Dm 让给 SMB5 做握手；
否则 APSD 只会把适配器判成 SDP/OCP。上述 5V、QC 9V 和充满终止/回充均已实机
验证。

## 电量状态

Nabu 上 PM8150B 燃料计的 OTP 中没有可用的电池模型，因此在加载 profile 之前，
SOC 算法输出的值没有意义（长期停在量程顶端附近）。因此本模块内置了厂商电池
profile（K82 sunwoda 8720mAh，416 字节），`qcom_fg` 在 probe 时把它连同匹配的
KI 系数、截止/终止电流和空电电压写入燃料计 SRAM，并重启算法。加载后上报的
百分比会在 0..100% 全量程跟随电池实际状态。

SMB5 结束涓流充满（`POWER_SUPPLY_STATUS_FULL`）时 `capacity` 上报 100%，
燃料计空电端点上报 0%，中间 1..99 由单调 SOC 缩放得到。

## 目录

```text
kernel-overlay/   按 Linux 源码路径组织的电源内核源码
config/           可合并到现有 .config 的电源 Kconfig fragment
patches/          基线 DTS 拆分补丁
system/           可选的 modprobe 与 systemd 配置
LICENSES/         源码 SPDX 标识对应的许可证文本
```

电源覆盖层包含：

- `drivers/power/supply/qcom_pmi8998_charger.c`：在 mainline SMB2 驱动上扩展出
  PM8150B/SMB5 支持（50mA/10mV 步进、DCDC 状态寄存器偏移、跳过 Type-C/OTG 段、
  HVDCP、USBIN resume/MODE_CHG、清除 charge inhibit）；
- `drivers/power/supply/qcom_fg.c`：状态接充电器、`CURRENT_NOW` 符号标准化、
  满/空电量端点，以及 Gen4（PM8150B）SRAM 电池 profile 加载；
- `drivers/power/supply/ln8000_charger.c`：秒级状态日志降为 debug；
- 派生 DTS 片段与基线 DTS 拆分补丁。


## 设备树

与 `nabu-iris`/`nabu-camera` 一样，电源设备树也使用追加模式，由两部分组成：

- `arch/arm64/boot/dts/qcom/sm8150-xiaomi-nabu-power.dtsi`：追加电池、LN8000
  充电器与 `pm8150b_fg` 节点；
- `patches/0001-base-dts-split-power.patch`：从基线 `sm8150-xiaomi-nabu.dts`
  中移除原本内联的电池、LN8000、`pm8150b_fg` 和 Type-C `sink-pdos` 定义。

组合 DTB 不再手写，由 `nabu-main compose` 按产品顺序自动生成：

```dts
#include "sm8150-xiaomi-nabu.dts"
#include "sm8150-xiaomi-nabu-iris.dtsi"
#include "sm8150-xiaomi-nabu-camera.dtsi"
#include "sm8150-xiaomi-nabu-accelerometer-slpi-boot-only.dtsi"
#include "sm8150-xiaomi-nabu-power.dtsi"
```

它必须使用最终的 SLPI/SSC 加速度计片段，不能再用已废止的 AP 侧 LSM6DSO
`sm8150-xiaomi-nabu-accelerometer.dtsi`（会让 `spi-geni-qcom` 访问 SLPI 拥有的
SSC MMIO，历史上会卡死内核）。

## 统一构建（nabu-main）

本仓库不再自带覆盖、配置合并或模块构建脚本。`nabu-main` 读取根目录的
`nabu-module.toml`，先 `git apply` 上述补丁，再复制 overlay：

```toml
[provides]
overlay = "kernel-overlay"
patches = ["patches/0001-base-dts-split-power.patch"]
dtsi    = ["arch/arm64/boot/dts/qcom/sm8150-xiaomi-nabu-power.dtsi"]
config  = ["config/nabu-power.config"]
systemd = ["system/ln8000-autoload.service"]

[build]
kernel_targets = ["drivers/power/supply/ln8000_charger.ko"]
```

在内核基线 `5181e1358ddd6ea8028e841d928942373e6aebc8` 上，于 `nabu-main` 运行：

```sh
make apply      # reset linux，应用 overlay/patch
make compose    # 生成组合 DTS
make config     # 合并 fragment 并固定统一 release
make build      # 构建 Image、模块与 DTB
make collect    # 收集产物到 artifacts/<product>/
```

`qcom_fg` 编译进内核镜像（`CONFIG_BATTERY_QCOM_FG=y`），不产生独立模块。
构建产物必须与正在运行的内核版本、配置和符号完全匹配。

## 运行配置

LN8000 是 I2C 设备树驱动，内核可通过 modalias 自动加载。若需显式加载，
可安装随附的 systemd 单元：

```sh
sudo install -m 0644 system/ln8000-autoload.service \
    /etc/systemd/system/ln8000-autoload.service
sudo systemctl enable --now ln8000-autoload.service
```

可用 `upower -i /org/freedesktop/UPower/devices/battery_BAT0` 或
`cat /sys/class/power_supply/qcom-battery/capacity` 检查电量。

### PMIC RTC 时钟同步

本机 UEFI 固件把 EFI RTC 暴露为 `/dev/rtc0`，但它的寄存器在 Nabu 上读不出来
（内核日志 `rtc-efi: hctosys: unable to read the hardware clock`），真正走时的
是 PM8150 PMIC RTC（`rtc-pm8xxx`），它 probe 较晚，枚举为 `/dev/rtc1`。内核
`hctosys` 只认 `CONFIG_RTC_HCTOSYS_DEVICE="rtc0"`，所以开机时系统时钟停在
epoch，只有联网 NTP 才能纠正。

`nabu-pmic-rtc-sync.service` 在启动早期从 `/dev/rtc1` 读回到系统时钟，并在关机
时把系统时钟写回 `/dev/rtc1`（PMIC 的修正量存于 SDAM，写回后才能跨重启保留）。
`make install` 会自动安装并 enable；手工安装：

```sh
sudo install -m 0755 system/nabu-pmic-rtc-sync /usr/local/libexec/nabu-pmic-rtc-sync
sudo install -m 0644 system/nabu-pmic-rtc-sync.service \
    /etc/systemd/system/nabu-pmic-rtc-sync.service
sudo systemctl daemon-reload
sudo systemctl enable --now nabu-pmic-rtc-sync.service
```

验证：`systemctl status nabu-pmic-rtc-sync` 应为 active，`timedatectl` 在断网
重启后也应显示正确时间。

## 来源和许可证

内核基线、原始提交和拆分说明见 [`SOURCE.md`](SOURCE.md)。各文件按自身 SPDX
标识授权；Linux 许可证说明见 `COPYING` 与 `LICENSES/`。
