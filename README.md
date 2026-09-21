# nabu-power

**English** | [中文](README.zh.md)

Experimental battery and charging support for the Xiaomi Pad 5 (`nabu`, SM8150): kernel drivers,
device tree, and runtime configuration for the PM8150B SMB5 main charger, the PMIC fuel gauge
`qcom_fg`, and the LionSemi LN8000 fast-charge IC.

Like `nabu-iris` and `nabu-camera`, this repository stores a direct source overlay; it does not
include a complete Linux kernel tree, a prebuilt UKI, or a full module tree. Kernel files keep their
original relative paths, so they can be overlaid onto a chosen baseline for review and building.

> This is experimental code. Replacing the DTB, kernel, or modules may prevent the device from
> booting; always prepare a working recovery method.

## Current features

- Qualcomm PM8150B SMB5 charger (`qcom,pm8150b-charger`): USB detection, basic 5V charging, and
  QC2/QC3 HVDCP boost
- Qualcomm PM8150B fuel gauge (`qcom,pm8150b-fg`), reporting capacity, voltage, current, and
  temperature, with the vendor battery profile loaded at boot for full-range SOC accuracy
- LionSemi LN8000 fast-charge IC (`lionsemi,ln8000`): 2:1 charge pump for 9V+ input
- Software JEITA: charge current/float voltage limited from the fuel-gauge temperature, with
  charging disabled outside -10..59°C; the LN8000 charge pump is stopped outside 0..45°C
- Derived device tree appends battery, charger, and PMIC fuel gauge nodes

## Charging path

A-to-C (USB-A port) charging consists of three parts, all of which are required:

```text
adapter/USB  ──5V──▶  PM8150B SMB5 charger ──▶  battery
                │
                └─ QC2/QC3 boost to ~9V
                             │
                             ├─▶  SMB5 continues charging
                             └─▶  LN8000 2:1 charge pump in parallel fast charge (needs ≥~7.6V)
```

- With 5V only: the LN8000 is a 2:1 charge pump and cannot enter SWITCHING (it falls back to
  standby), so only the SMB5 can charge, at roughly 5V×2A; under high load with the screen on it
  tracks the load and the net current approaches 0.
- After enabling the SMB5's HVDCP (`HVDCP_EN | HVDCP_AUTH_ALG_EN | HVDCP_AUTONOMOUS_MODE_EN`), the
  PMIC raises the A port to QC high voltage (measured at about 8.3V), the SMB5 keeps charging, and
  the LN8000 enters SWITCHING for parallel fast charging; the measured battery-side current is about
  +3.4A.

`qcom_fg` is attached to `pm8150b_charger` (`power-supplies`); battery status is determined by the
SMB5 and no longer flickers as the instantaneous net current crosses zero; `CURRENT_NOW` now follows
the standard convention (positive = charging), so the desktop UPower/GNOME can display charging
correctly.

Full charge is handled by the SMB5 hardware CC/CV: it charges to 4.47V, then holds constant voltage
until the current decays; when capacity drops to 99% it automatically recharges. The PM8150B ADC
charge termination does not work on nabu: with the threshold, the ADC comparator and the sample mode
all programmed correctly (verified by register read-back) the comparator still never trips, even
with the threshold pushed to -2000mA. `POWER_SUPPLY_STATUS_FULL` is therefore latched in software
once the pack is at 100% and the charge current has tapered below 400mA (mirroring the vendor
`charge_full` flag); UPower then shows a full charge.

QC detection depends on D+/D- (DPDM): before running APSD, the charger switches the USB HS PHY
through `dpdm-supply` to UTMI non-driving (high impedance), handing Dp/Dm to the SMB5 for handshake;
otherwise APSD would only identify the adapter as SDP/OCP. The 5V, QC 9V, and full-charge
termination/recharge behaviors above have all been verified on real hardware.

## State of charge

The PM8150B fuel gauge has no usable battery model in OTP on nabu, so its SOC algorithm
returns a meaningless value (it sits near the top of its range) until a profile is loaded. The
overlay therefore carries the vendor profile blob (K82 sunwoda 8720mAh, 416 bytes) and
`qcom_fg` writes it into gauge SRAM during probe, together with the matching KI coefficients,
cutoff/termination currents and empty voltage, then restarts the algorithm. With the profile
loaded the reported percentage tracks the battery across the whole 0..100% range.

`capacity` reports 100% once the pack is full and 0% at the gauge's empty endpoint; the 1..99 range
in between is scaled from the gauge's monotonic SOC.

## Layout

```text
kernel-overlay/   power kernel source organized by Linux source paths
config/           power Kconfig fragment that can be merged into an existing .config
patches/          baseline DTS split patches
system/           optional modprobe and systemd configuration
LICENSES/         license texts for the source SPDX tags
```

The power overlay contains:

- `drivers/power/supply/qcom_pmi8998_charger.c`: extends the mainline SMB2 driver with
  PM8150B/SMB5 support (50mA/10mV steps, DCDC status register offsets, skipping the Type-C/OTG
  section, HVDCP, USBIN resume/MODE_CHG, clearing charge inhibit);
- `drivers/power/supply/qcom_fg.c`: status wired to the charger, `CURRENT_NOW` sign normalized,
  full/empty capacity endpoints, and a Gen4 (PM8150B) SRAM battery-profile loader;
- `drivers/power/supply/ln8000_charger.c`: per-second status logs demoted to debug;
- Derived DTS fragments and baseline DTS split patches.


## Device tree

Like `nabu-iris`/`nabu-camera`, the power device tree uses the append model and consists of two
parts:

- `arch/arm64/boot/dts/qcom/sm8150-xiaomi-nabu-power.dtsi`: appends the battery, LN8000 charger, and
  `pm8150b_fg` nodes;
- `patches/0001-base-dts-split-power.patch`: removes the previously inlined battery, LN8000,
  `pm8150b_fg`, and Type-C `sink-pdos` definitions from the baseline `sm8150-xiaomi-nabu.dts`.

The combined DTB is no longer written by hand; it is generated automatically by `nabu-main compose`
in product order:

```dts
#include "sm8150-xiaomi-nabu.dts"
#include "sm8150-xiaomi-nabu-iris.dtsi"
#include "sm8150-xiaomi-nabu-camera.dtsi"
#include "sm8150-xiaomi-nabu-accelerometer-slpi-boot-only.dtsi"
#include "sm8150-xiaomi-nabu-power.dtsi"
```

It must use the final SLPI/SSC accelerometer fragment and must not use the deprecated AP-side
LSM6DSO `sm8150-xiaomi-nabu-accelerometer.dtsi` (which would make `spi-geni-qcom` access the
SLPI-owned SSC MMIO and has historically hung the kernel).

## Unified build (nabu-main)

This repository no longer ships its own overlay, config merging, or module build scripts.
`nabu-main` reads the root `nabu-module.toml`, first `git apply`s the patch above, then copies the
overlay:

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

On kernel baseline `5181e1358ddd6ea8028e841d928942373e6aebc8`, run in `nabu-main`:

```sh
make apply      # reset linux, apply overlay/patch
make compose    # generate the combined DTS
make config     # merge fragments and pin the unified release
make build      # build Image, modules, and DTB
make collect    # collect artifacts into artifacts/<product>/
```

`qcom_fg` is compiled into the kernel image (`CONFIG_BATTERY_QCOM_FG=y`) and does not produce a
standalone module. Build artifacts must exactly match the running kernel's version, configuration,
and symbols.

## Runtime configuration

The LN8000 is an I2C device tree driver and the kernel can load it automatically via modalias. To
load it explicitly, install the included systemd unit:

```sh
sudo install -m 0644 system/ln8000-autoload.service \
    /etc/systemd/system/ln8000-autoload.service
sudo systemctl enable --now ln8000-autoload.service
```

You can check the battery level with `upower -i /org/freedesktop/UPower/devices/battery_BAT0` or
`cat /sys/class/power_supply/qcom-battery/capacity`.

### PMIC RTC clock synchronization

The device's UEFI firmware exposes the EFI RTC as `/dev/rtc0`, but its registers cannot be read on
Nabu (kernel log `rtc-efi: hctosys: unable to read the hardware clock`); the clock that actually
keeps time is the PM8150 PMIC RTC (`rtc-pm8xxx`), which probes later and enumerates as `/dev/rtc1`.
The kernel `hctosys` only recognizes `CONFIG_RTC_HCTOSYS_DEVICE="rtc0"`, so at boot the system clock
stops at the epoch and can only be corrected by network NTP.

`nabu-pmic-rtc-sync.service` reads the system clock back from `/dev/rtc1` early during boot and
writes the system clock back to `/dev/rtc1` on shutdown (the PMIC's correction is stored in SDAM and
only persists across reboots after the write-back). `make install` installs and enables it
automatically; to install manually:

```sh
sudo install -m 0755 system/nabu-pmic-rtc-sync /usr/local/libexec/nabu-pmic-rtc-sync
sudo install -m 0644 system/nabu-pmic-rtc-sync.service \
    /etc/systemd/system/nabu-pmic-rtc-sync.service
sudo systemctl daemon-reload
sudo systemctl enable --now nabu-pmic-rtc-sync.service
```

To verify: `systemctl status nabu-pmic-rtc-sync` should be active, and `timedatectl` should show the
correct time even after an offline reboot.

## Provenance and license

See [`SOURCE.md`](SOURCE.md) for the kernel baseline, original commits, and split notes. Each file
is licensed under its own SPDX tag; see `COPYING` and `LICENSES/` for the Linux license notices.
