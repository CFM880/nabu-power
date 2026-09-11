# Source and provenance

The kernel overlay targets commit
`5181e1358ddd6ea8028e841d928942373e6aebc8` from the postmarketOS
Qualcomm SM8150 Linux tree:

```text
https://gitlab.postmarketos.org/soc/qualcomm-sm8150/linux.git
```

The power driver sources are drawn from the following nabu commits in that
tree:

```text
dd1d67fc67eb power: supply: Add driver for Qualcomm PMIC fuel gauge
fbb1b7d5e5c1 power: qcom_fg: Add initial pm8150b support
0ea11228c784 qcom_fg: Fix compilation in 6.11
70a40e4111e8 NABU: Add ln8000 fast charge IC for testing
5181e1358ddd NABU: dts: enable ln8000 charger, reduce charge voltage to 9V
25f75983ba51 NABU: Add pmic fg and battery nodes
26267e6570a5 arm64: dts: qcom: pm8150b: Add fuel gauge
```

The overlay carries:

- `drivers/power/supply/qcom_fg.c`, `drivers/power/supply/ln8000_charger.c`
  and `drivers/power/supply/qcom_pmi8998_charger.c` together with the
  `drivers/power/supply/Kconfig`/`Makefile` integration;
- the derived board DTS `sm8150-xiaomi-nabu-power.dts`/`.dtsi` that appends
  the battery, PM8150B SMB5 charger, LN8000 charger, `pm8150b_fg` enable and
  Type-C `sink-pdos` nodes;
- the production combination DTS
  `sm8150-xiaomi-nabu-iris-camera-accelerometer-power.dts`, which includes the
  Iris, camera and final SLPI/SSC accelerometer fragments alongside the power
  fragment. It intentionally does not use the retired AP-side
  `sm8150-xiaomi-nabu-accelerometer.dtsi`;
- a copy of `sm8150-xiaomi-nabu.dts` with those power nodes removed so the
  power overlay is the authoritative source.

Unlike `nabu-iris` and `nabu-camera`, the power additions were historically
committed directly into `sm8150-xiaomi-nabu.dts` rather than into a derived
DTS. Extracting them therefore requires carrying a power-stripped copy of the
board DTS inside this overlay. The shared `pm8150b.dtsi` is left untouched;
its `qcom,pm8150b-fg` node (disabled by default) is enabled by the derived DTS.

`arch/arm64/configs/sm8150.config` is not replaced. The power options live in
`config/nabu-power.config` and are merged into an existing kernel `.config`.

Mainline ships no PM8150B SMB5 charger driver. The overlay reuses and extends
the mainline SMB2 driver, `qcom_pmi8998_charger.c`, with a `qcom,pm8150b-charger`
compatible. SMB5 shares the CHGR/USBIN register offsets with SMB2 but differs in

- current/voltage step sizes (50mA / 10mV instead of 25mA / 7.5mV);
- DCDC status register placement (`ICL_STATUS` 0x1107, `POWER_PATH_STATUS`
  0x110B instead of 0x1607 / 0x160B);
- the Type-C/OTG/MISC status-pin blocks, which stay owned by the
  `qcom,pm8150b-typec` driver and are skipped;
- the extra bring-up needed for charging: resume USBIN (`USBIN_CMD_IL`),
  select the USBIN charge path (`USBIN_ICL_OPTIONS.USBIN_MODE_CHG`), clear
  `CHGR_CFG2.CHARGER_INHIBIT`, and enable HVDCP autonomous mode for QC2/QC3
  with a 5-12V adapter allowance and an APSD re-run so an adapter that was
  already attached at boot is re-classified.

Driver changes maintained directly in this repository:

- `qcom_pmi8998_charger.c`: per-PMIC scaling/register data and the PM8150B
  bring-up above; HVDCP with a 5-12V adapter allowance and an APSD re-run so an
  already-attached adapter is classified at boot; per-PMIC fast-charge current
  (3A on PM8150B), a per-PMIC battery-overvoltage status bit (SMB5 uses BIT(1),
  SMB2 uses BIT(5)), ADC-based charge termination at 400mA, auto-recharge at
  99%, a 4.47V float-voltage cap, per-PMIC input current limits, a 1.5A SDP
  floor, disabled SMB5 hardware JEITA (nabu does not wire the PMIC thermistor),
  and an optional input-voltage IIO channel that defers the probe when the
  PM8150B VADC is not ready yet (the raw USB_IN sense voltage is intentionally
  not exposed as `CURRENT_NOW`);
- `drivers/phy/qualcomm/phy-qcom-snps-femto-v2.c`: a `dpdm` regulator that puts
  the USB HS PHY into UTMI non-driving mode, releasing Dp/Dm for the charger's
  APSD/QC detection (`dpdm-supply` is wired to `usb_1_hsphy` in the DTS);
- `qcom_fg.c`: avoid reading an uninitialized `propval.intval` in the charger
  notifier, return `-EPROBE_DEFER` when a DT-described charger supply is not
  registered yet, take the battery status from the charger
  (`power-supplies`) so it does not oscillate around zero net current, and
  report `CURRENT_NOW` with the standard sign (positive = charging);
- `ln8000_charger.c`: demote the per-second `psy_chg_get_ti_alarm_status`
  register dump from info to debug.

When importing a new upstream snapshot, update the Linux source worktree first,
resynchronize all changed files as one coherent overlay, and then rebase the
nabu-specific commits; do not hand-maintain a second patch series.
