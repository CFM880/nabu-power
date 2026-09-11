# nabu-power

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
- Qualcomm PM8150B 燃料计（`qcom,pm8150b-fg`），上报电量、电压、电流与温度
- LionSemi LN8000 快充 IC（`lionsemi,ln8000`）：9V+ 输入下的 2:1 电荷泵
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

充满由 SMB5 硬件 CC/CV 完成：充电到 4.47V 后恒压，电流衰减到约 400mA 终止；
电量掉到 99% 自动回充。终止时 `BATTERY_CHARGER_STATUS_1` 映射为
`POWER_SUPPLY_STATUS_FULL`，UPower 显示已充满。

QC 识别依赖 D+/D-（DPDM）：充电器在跑 APSD 前会把 USB HS PHY 通过
`dpdm-supply` 切到 UTMI non-driving（高阻），把 Dp/Dm 让给 SMB5 做握手；
否则 APSD 只会把适配器判成 SDP/OCP。上述 5V、QC 9V 和充满终止/回充均已实机
验证。

## 目录

```text
kernel-overlay/   按 Linux 源码路径组织的电源内核源码
config/           可合并到现有 .config 的电源 Kconfig fragment
scripts/          覆盖与构建辅助脚本
system/           可选的 modprobe 与 systemd 配置
LICENSES/         源码 SPDX 标识对应的许可证文本
```

电源覆盖层包含：

- `drivers/power/supply/qcom_pmi8998_charger.c`：在 mainline SMB2 驱动上扩展出
  PM8150B/SMB5 支持（50mA/10mV 步进、DCDC 状态寄存器偏移、跳过 Type-C/OTG 段、
  HVDCP、USBIN resume/MODE_CHG、清除 charge inhibit）；
- `drivers/power/supply/qcom_fg.c`：状态接充电器、`CURRENT_NOW` 符号标准化；
- `drivers/power/supply/ln8000_charger.c`：秒级状态日志降为 debug；
- 派生 DTS 与生产组合 DTS。


## 设备树追加模式

与 `nabu-iris`/`nabu-camera` 一样，电源设备树也使用派生板级文件：

```text
sm8150-xiaomi-nabu-power.dts
  ├─ include sm8150-xiaomi-nabu.dts
  └─ include sm8150-xiaomi-nabu-power.dtsi
```

另外提供生产用的组合 DTS `sm8150-xiaomi-nabu-iris-camera-accelerometer-power.dts`，
把 Iris、相机、SLPI/SSC 加速度计和电源四个片段汇总成一个启动镜像设备树：

```text
sm8150-xiaomi-nabu-iris-camera-accelerometer-power.dts
  ├─ include sm8150-xiaomi-nabu.dts
  ├─ include sm8150-xiaomi-nabu-iris.dtsi
  ├─ include sm8150-xiaomi-nabu-camera.dtsi
  ├─ include sm8150-xiaomi-nabu-accelerometer-slpi-boot-only.dtsi
  └─ include sm8150-xiaomi-nabu-power.dtsi
```

它必须使用最终的 SLPI/SSC 加速度计片段，不能再用已废止的 AP 侧 LSM6DSO
`sm8150-xiaomi-nabu-accelerometer.dtsi`（会让 `spi-geni-qcom` 访问 SLPI 拥有的
SSC MMIO，历史上会卡死内核）。

有一点与其它模块不同：电池、LN8000 充电器、`pm8150b_fg` 使能和 Type-C
`sink-pdos` 电压限制原本直接写在基线的
`sm8150-xiaomi-nabu.dts` 里。为让电源功能可独立拆装，本模块把这几处电源节点
**从基线 DTS 中移出**，放进 `sm8150-xiaomi-nabu-power.dtsi`；模块覆盖层同时
携带去除了这些节点的 `sm8150-xiaomi-nabu.dts`。因此：

- 应用本覆盖层后，基线 nabu DTS 不再含电池/充电节点；
- 启动时必须使用派生 DTB `qcom/sm8150-xiaomi-nabu-power.dtb`，原始 nabu
  DTB 不含这些节点；
- 共享的 `pm8150b.dtsi` 保持不动，其 `pm8150b_fg` 节点（默认 `disabled`）
  由派生 DTS 使能。

## 放入内核树

准备位于精确基线的 Linux 源码树：

```sh
git clone https://gitlab.postmarketos.org/soc/qualcomm-sm8150/linux.git linux
git -C linux checkout 5181e1358ddd6ea8028e841d928942373e6aebc8
./scripts/apply-overlay.sh ./linux
```

安装脚本允许目标树存在不重叠的修改，所以可以先应用 `nabu-iris`、
`nabu-camera`、`nabu-accelerometer`，最后再应用本覆盖层。生产组合 DTB
在构建时才需要那三个片段；如果某个电源覆盖目标（包括被抽取电源节点后的
`sm8150-xiaomi-nabu.dts`）已被其他工作修改，脚本会停止，不会静默覆盖。

## 构建

输出目录需要已有适用于 nabu 的 `.config`。构建脚本先用内核自带的
`merge_config.sh` 合并 `config/nabu-power.config`，不会替换主 defconfig：

```sh
./scripts/build.sh ./linux ./linux/out
```

也可以只合并配置：

```sh
./scripts/merge-config.sh ./linux ./linux/out
```

脚本构建 `ln8000_charger.ko` 模块以及派生 DTB。如果已安装 Iris、相机和加速度计
覆盖层，脚本自动改为构建生产组合 DTB：

```text
linux/out/drivers/power/supply/ln8000_charger.ko
linux/out/arch/arm64/boot/dts/qcom/sm8150-xiaomi-nabu-iris-camera-accelerometer-power.dtb
```

仅安装电源覆盖层时退回到：

```text
linux/out/arch/arm64/boot/dts/qcom/sm8150-xiaomi-nabu-power.dtb
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

## 来源和许可证

内核基线、原始提交和拆分说明见 [`SOURCE.md`](SOURCE.md)。各文件按自身 SPDX
标识授权；Linux 许可证说明见 `COPYING` 与 `LICENSES/`。
