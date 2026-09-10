#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

expected_base=5181e1358ddd6ea8028e841d928942373e6aebc8

if [ "$#" -ne 1 ]; then
	echo "usage: $0 /path/to/linux" >&2
	exit 2
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
overlay_dir=$(dirname -- "$script_dir")/kernel-overlay
kernel_tree=$1

if [ ! -f "$kernel_tree/Makefile" ] ||
   ! git -C "$kernel_tree" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	echo "not a Linux Git worktree: $kernel_tree" >&2
	exit 1
fi

current=$(git -C "$kernel_tree" rev-parse HEAD)
if [ "$current" != "$expected_base" ]; then
	echo "expected base $expected_base, found $current" >&2
	exit 1
fi

# Permit unrelated overlays (notably nabu-iris/nabu-camera), but never
# overwrite a target path carrying unknown local changes. This also makes
# repeated application idempotent.
find "$overlay_dir" -type f -print | sort | while IFS= read -r source; do
	relative=${source#"$overlay_dir"/}
	target=$kernel_tree/$relative

	if [ -f "$target" ] && cmp -s "$source" "$target"; then
		continue
	fi

	if git -C "$kernel_tree" ls-files --error-unmatch "$relative" >/dev/null 2>&1; then
		if ! git -C "$kernel_tree" diff --quiet HEAD -- "$relative"; then
			echo "refusing to overwrite locally changed path: $relative" >&2
			exit 1
		fi
	elif [ -e "$target" ]; then
		echo "refusing to overwrite untracked path: $relative" >&2
		exit 1
	fi
done

# Give installed sources fresh timestamps so an incremental O= build cannot
# reuse objects produced from an older revision of the overlay.
cp -R "$overlay_dir/." "$kernel_tree/"
echo "installed nabu-power source overlay into $kernel_tree"
echo "power DTB target: qcom/sm8150-xiaomi-nabu-power.dtb"
if [ -f "$kernel_tree/arch/arm64/boot/dts/qcom/sm8150-xiaomi-nabu-iris.dtsi" ] &&
   [ -f "$kernel_tree/arch/arm64/boot/dts/qcom/sm8150-xiaomi-nabu-camera.dtsi" ] &&
   [ -f "$kernel_tree/arch/arm64/boot/dts/qcom/sm8150-xiaomi-nabu-accelerometer-slpi-boot-only.dtsi" ]; then
	echo "production DTB target: qcom/sm8150-xiaomi-nabu-iris-camera-accelerometer-power.dtb"
else
	echo "production DTB needs the nabu-iris, nabu-camera and nabu-accelerometer overlays"
fi
echo "config fragment: config/nabu-power.config"
git -C "$kernel_tree" status --short
