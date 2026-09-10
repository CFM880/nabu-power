#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 /path/to/linux /path/to/output" >&2
	exit 2
fi

kernel_tree=$(CDPATH= cd -- "$1" && pwd)
mkdir -p "$2"
output_dir=$(CDPATH= cd -- "$2" && pwd)
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ ! -f "$kernel_tree/Makefile" ]; then
	echo "not a Linux source tree: $kernel_tree" >&2
	exit 1
fi

dts_dir=$kernel_tree/arch/arm64/boot/dts/qcom
power_dts=$dts_dir/sm8150-xiaomi-nabu-power.dts
if [ ! -f "$power_dts" ]; then
	echo "power overlay is not installed; run scripts/apply-overlay.sh first" >&2
	exit 1
fi

# Prefer the full production combination DTB when the Iris/camera/accelerometer
# overlays are installed. The power-only DTB remains for standalone testing.
dtb=sm8150-xiaomi-nabu-power.dtb
if [ -f "$dts_dir/sm8150-xiaomi-nabu-iris-camera-accelerometer-power.dts" ] &&
   [ -f "$dts_dir/sm8150-xiaomi-nabu-iris.dtsi" ] &&
   [ -f "$dts_dir/sm8150-xiaomi-nabu-camera.dtsi" ] &&
   [ -f "$dts_dir/sm8150-xiaomi-nabu-accelerometer-slpi-boot-only.dtsi" ]; then
	dtb=sm8150-xiaomi-nabu-iris-camera-accelerometer-power.dtb
fi

if [ ! -f "$output_dir/.config" ]; then
	echo "missing configured kernel output: $output_dir/.config" >&2
	exit 1
fi

: "${ARCH:=arm64}"
: "${CROSS_COMPILE:=aarch64-linux-gnu-}"
: "${JOBS:=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"
export ARCH CROSS_COMPILE

"$script_dir/merge-config.sh" "$kernel_tree" "$output_dir"
make -C "$kernel_tree" O="$output_dir" olddefconfig
make -C "$kernel_tree" O="$output_dir" -j"$JOBS" \
	modules "qcom/$dtb"

output_dtb=$output_dir/arch/arm64/boot/dts/qcom/$dtb
module=$output_dir/drivers/power/supply/ln8000_charger.ko
test -f "$output_dtb"
echo "built $output_dtb"
echo "built $module"
