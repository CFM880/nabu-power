// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Ltd.
 * Author: Caleb Connolly <caleb.connolly@linaro.org>
 *
 * This driver is for the switch-mode battery charger and boost
 * hardware found in pmi8998 and related PMICs.
 */

#include <linux/bits.h>
#include <linux/devm-helpers.h>
#include <linux/extcon.h>
#include <linux/extcon-provider.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/usb/role.h>
#include <linux/types.h>
#include <linux/workqueue.h>

/* clang-format off */
#define BATTERY_CHARGER_STATUS_1			0x06
#define BVR_INITIAL_RAMP_BIT				BIT(7)
#define CC_SOFT_TERMINATE_BIT				BIT(6)
#define STEP_CHARGING_STATUS_SHIFT			3
#define STEP_CHARGING_STATUS_MASK			GENMASK(5, 3)
#define BATTERY_CHARGER_STATUS_MASK			GENMASK(2, 0)

#define BATTERY_CHARGER_STATUS_2			0x07
#define INPUT_CURRENT_LIMITED_BIT			BIT(7)
#define CHARGER_ERROR_STATUS_SFT_EXPIRE_BIT		BIT(6)
#define CHARGER_ERROR_STATUS_BAT_OV_BIT			BIT(5)
#define CHARGER_ERROR_STATUS_BAT_TERM_MISSING_BIT	BIT(4)
#define BAT_TEMP_STATUS_MASK				GENMASK(3, 0)
#define BAT_TEMP_STATUS_SOFT_LIMIT_MASK			GENMASK(3, 2)
#define BAT_TEMP_STATUS_HOT_SOFT_LIMIT_BIT		BIT(3)
#define BAT_TEMP_STATUS_COLD_SOFT_LIMIT_BIT		BIT(2)
#define BAT_TEMP_STATUS_TOO_HOT_BIT			BIT(1)
#define BAT_TEMP_STATUS_TOO_COLD_BIT			BIT(0)

#define CHARGE_CURRENT_POST_JEITA_MASK			GENMASK(7, 0)

#define BATTERY_CHARGER_STATUS_7			0x0D
#define ENABLE_TRICKLE_BIT				BIT(7)
#define ENABLE_PRE_CHARGING_BIT				BIT(6)
#define ENABLE_FAST_CHARGING_BIT			BIT(5)
#define ENABLE_FULLON_MODE_BIT				BIT(4)
#define TOO_COLD_ADC_BIT				BIT(3)
#define TOO_HOT_ADC_BIT					BIT(2)
#define HOT_SL_ADC_BIT					BIT(1)
#define COLD_SL_ADC_BIT					BIT(0)

#define CHARGING_ENABLE_CMD				0x42
#define CHARGING_ENABLE_CMD_BIT				BIT(0)

#define CHGR_CFG2					0x51
#define CHG_EN_SRC_BIT					BIT(7)
#define CHG_EN_POLARITY_BIT				BIT(6)
#define PRETOFAST_TRANSITION_CFG_BIT			BIT(5)
#define BAT_OV_ECC_BIT					BIT(4)
#define I_TERM_BIT					BIT(3)
#define AUTO_RECHG_BIT					BIT(2)
#define EN_ANALOG_DROP_IN_VBATT_BIT			BIT(1)
#define CHARGER_INHIBIT_BIT				BIT(0)

#define PRE_CHARGE_CURRENT_CFG				0x60
#define PRE_CHARGE_CURRENT_SETTING_MASK			GENMASK(5, 0)

#define FAST_CHARGE_CURRENT_CFG				0x61
#define FAST_CHARGE_CURRENT_SETTING_MASK		GENMASK(7, 0)

#define FLOAT_VOLTAGE_CFG				0x70
#define FLOAT_VOLTAGE_SETTING_MASK			GENMASK(7, 0)

#define CHGR_ADC_ITERM_UP_THD_MSB			0x67
#define CHGR_ADC_ITERM_UP_THD_LSB			0x68
#define CHGR_ADC_ITERM_LO_THD_MSB			0x69
#define CHGR_ADC_ITERM_LO_THD_LSB			0x6A
#define CHGR_ADC_TERM_CFG				0x6C
#define TERM_BASED_ON_SAMPLE_CNT			1
#define CHGR_ENG_CHARGING_CFG				0xC0
#define CHGR_ITERM_USE_ANALOG_BIT			BIT(3)
#define CHARGE_RCHG_SOC_THRESHOLD			0x7D

#define FG_UPDATE_CFG_2_SEL				0x7D
#define SOC_LT_OTG_THRESH_SEL_BIT			BIT(3)
#define SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT		BIT(2)
#define VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT		BIT(1)
#define IBT_LT_CHG_TERM_THRESH_SEL_BIT			BIT(0)

#define JEITA_EN_CFG					0x90
#define JEITA_EN_HARDLIMIT_BIT				BIT(4)
#define JEITA_EN_HOT_SL_FCV_BIT				BIT(3)
#define JEITA_EN_COLD_SL_FCV_BIT			BIT(2)
#define JEITA_EN_HOT_SL_CCC_BIT				BIT(1)
#define JEITA_EN_COLD_SL_CCC_BIT			BIT(0)

#define INT_RT_STS					0x310
#define TYPE_C_CHANGE_RT_STS_BIT			BIT(7)
#define USBIN_ICL_CHANGE_RT_STS_BIT			BIT(6)
#define USBIN_SOURCE_CHANGE_RT_STS_BIT			BIT(5)
#define USBIN_PLUGIN_RT_STS_BIT				BIT(4)
#define USBIN_OV_RT_STS_BIT				BIT(3)
#define USBIN_UV_RT_STS_BIT				BIT(2)
#define USBIN_LT_3P6V_RT_STS_BIT			BIT(1)
#define USBIN_COLLAPSE_RT_STS_BIT			BIT(0)

#define OTG_CFG						0x153
#define OTG_RESERVED_MASK				GENMASK(7, 6)
#define DIS_OTG_ON_TLIM_BIT				BIT(5)
#define QUICKSTART_OTG_FASTROLESWAP_BIT			BIT(4)
#define INCREASE_DFP_TIME_BIT				BIT(3)
#define ENABLE_OTG_IN_DEBUG_MODE_BIT			BIT(2)
#define OTG_EN_SRC_CFG_BIT				BIT(1)
#define CONCURRENT_MODE_CFG_BIT				BIT(0)

#define SMB5_DCDC_CMD_OTG	0x140
#define SMB5_DCDC_OTG_CFG	0x153
#define SMB5_DCDC_OTG_ILIM	0x152
#define SMB5_OTG_EN		BIT(0)
#define SMB5_OTG_EN_SRC_CMD	BIT(1)
/*
 * OTG current limit, 0.5A + 0.5A * value; the vendor driver programmes the DT
 * default (2A on nabu) at probe and the reset value is the lowest step, which
 * made the boost trip as soon as a device drew real current.
 */
#define SMB5_OTG_ILIM_2A	0x3

#define OTG_ENG_OTG_CFG					0x1C0
#define ENG_BUCKBOOST_HALT1_8_MODE_BIT			BIT(0)

#define APSD_STATUS					0x307
#define APSD_STATUS_7_BIT				BIT(7)
#define HVDCP_CHECK_TIMEOUT_BIT				BIT(6)
#define SLOW_PLUGIN_TIMEOUT_BIT				BIT(5)
#define ENUMERATION_DONE_BIT				BIT(4)
#define VADP_CHANGE_DONE_AFTER_AUTH_BIT			BIT(3)
#define QC_AUTH_DONE_STATUS_BIT				BIT(2)
#define QC_CHARGER_BIT					BIT(1)
#define APSD_DTC_STATUS_DONE_BIT			BIT(0)

#define APSD_RESULT_STATUS				0x308
#define ICL_OVERRIDE_LATCH_BIT				BIT(7)
#define APSD_RESULT_STATUS_MASK				GENMASK(6, 0)
#define QC_3P0_BIT					BIT(6)
#define QC_2P0_BIT					BIT(5)
#define FLOAT_CHARGER_BIT				BIT(4)
#define DCP_CHARGER_BIT					BIT(3)
#define CDP_CHARGER_BIT					BIT(2)
#define OCP_CHARGER_BIT					BIT(1)
#define SDP_CHARGER_BIT					BIT(0)

#define TYPE_C_STATUS_1					0x30B
#define UFP_TYPEC_MASK					GENMASK(7, 5)
#define UFP_TYPEC_RDSTD_BIT				BIT(7)
#define UFP_TYPEC_RD1P5_BIT				BIT(6)
#define UFP_TYPEC_RD3P0_BIT				BIT(5)
#define UFP_TYPEC_FMB_255K_BIT				BIT(4)
#define UFP_TYPEC_FMB_301K_BIT				BIT(3)
#define UFP_TYPEC_FMB_523K_BIT				BIT(2)
#define UFP_TYPEC_FMB_619K_BIT				BIT(1)
#define UFP_TYPEC_OPEN_OPEN_BIT				BIT(0)

#define TYPE_C_STATUS_2					0x30C
#define DFP_RA_OPEN_BIT					BIT(7)
#define TIMER_STAGE_BIT					BIT(6)
#define EXIT_UFP_MODE_BIT				BIT(5)
#define EXIT_DFP_MODE_BIT				BIT(4)
#define DFP_TYPEC_MASK					GENMASK(3, 0)
#define DFP_RD_OPEN_BIT					BIT(3)
#define DFP_RD_RA_VCONN_BIT				BIT(2)
#define DFP_RD_RD_BIT					BIT(1)
#define DFP_RA_RA_BIT					BIT(0)

#define TYPE_C_STATUS_3					0x30D
#define ENABLE_BANDGAP_BIT				BIT(7)
#define U_USB_GND_NOVBUS_BIT				BIT(6)
#define U_USB_FLOAT_NOVBUS_BIT				BIT(5)
#define U_USB_GND_BIT					BIT(4)
#define U_USB_FMB1_BIT					BIT(3)
#define U_USB_FLOAT1_BIT				BIT(2)
#define U_USB_FMB2_BIT					BIT(1)
#define U_USB_FLOAT2_BIT				BIT(0)

#define TYPE_C_STATUS_4					0x30E
#define UFP_DFP_MODE_STATUS_BIT				BIT(7)
#define TYPEC_VBUS_STATUS_BIT				BIT(6)
#define TYPEC_VBUS_ERROR_STATUS_BIT			BIT(5)
#define TYPEC_DEBOUNCE_DONE_STATUS_BIT			BIT(4)
#define TYPEC_UFP_AUDIO_ADAPT_STATUS_BIT		BIT(3)
#define TYPEC_VCONN_OVERCURR_STATUS_BIT			BIT(2)
#define CC_ORIENTATION_BIT				BIT(1)
#define CC_ATTACHED_BIT					BIT(0)

#define TYPE_C_STATUS_5					0x30F

/*
 * Nabu: the Type-C role is event driven - the attach/detach and CC interrupts
 * schedule the decision work immediately.  This interval is only a safety net
 * in case an interrupt is missed, so keep it long: a short poll would wake the
 * SoC out of s2idle for no reason.
 */
#define SMB2_TYPEC_POLL_MS				30000
/* while a decision is pending (votes, settle, hold) poll quickly instead */
#define SMB2_TYPEC_POLL_MS_BUSY				1000
#define TRY_SOURCE_FAILED_BIT				BIT(6)
#define TRY_SINK_FAILED_BIT				BIT(5)
#define TIMER_STAGE_2_BIT				BIT(4)
#define TYPEC_LEGACY_CABLE_STATUS_BIT			BIT(3)
#define TYPEC_NONCOMP_LEGACY_CABLE_STATUS_BIT		BIT(2)
#define TYPEC_TRYSOURCE_DETECT_STATUS_BIT		BIT(1)
#define TYPEC_TRYSINK_DETECT_STATUS_BIT			BIT(0)

#define CMD_APSD					0x341
#define APSD_RERUN_BIT					BIT(0)
/*
 * Software ICL override.  With the hardware in auto mode the input limit
 * follows the APSD/BC1.2 result, i.e. a misdetected adapter is clamped to the
 * 500mA USB default no matter what USBIN_CURRENT_LIMIT says.  The vendor
 * switches to "high current" mode whenever it programmes more than 500mA.
 */
#define CMD_ICL_OVERRIDE				0x342
#define ICL_OVERRIDE_CMD_BIT				BIT(0)

#define TYPE_C_CFG					0x358
#define APSD_START_ON_CC_BIT				BIT(7)
#define WAIT_FOR_APSD_BIT				BIT(6)
#define FACTORY_MODE_DETECTION_EN_BIT			BIT(5)
#define FACTORY_MODE_ICL_3A_4A_BIT			BIT(4)
#define FACTORY_MODE_DIS_CHGING_CFG_BIT			BIT(3)
#define SUSPEND_NON_COMPLIANT_CFG_BIT			BIT(2)
#define VCONN_OC_CFG_BIT				BIT(1)
#define TYPE_C_OR_U_USB_BIT				BIT(0)

#define TYPE_C_CFG_2					0x359
#define TYPE_C_DFP_CURRSRC_MODE_BIT			BIT(7)
#define DFP_CC_1P4V_OR_1P6V_BIT				BIT(6)
#define VCONN_SOFTSTART_CFG_MASK			GENMASK(5, 4)
#define EN_TRY_SOURCE_MODE_BIT				BIT(3)
#define USB_FACTORY_MODE_ENABLE_BIT			BIT(2)
#define TYPE_C_UFP_MODE_BIT				BIT(1)
#define EN_80UA_180UA_CUR_SOURCE_BIT			BIT(0)

#define TYPE_C_CFG_3					0x35A
#define TVBUS_DEBOUNCE_BIT				BIT(7)
#define TYPEC_LEGACY_CABLE_INT_EN_BIT			BIT(6)
#define TYPEC_NONCOMPLIANT_LEGACY_CABLE_INT_EN_B	BIT(5)
#define TYPEC_TRYSOURCE_DETECT_INT_EN_BIT		BIT(4)
#define TYPEC_TRYSINK_DETECT_INT_EN_BIT			BIT(3)
#define EN_TRYSINK_MODE_BIT				BIT(2)
#define EN_LEGACY_CABLE_DETECTION_BIT			BIT(1)
#define ALLOW_PD_DRING_UFP_TCCDB_BIT			BIT(0)

#define USBIN_OPTIONS_1_CFG				0x362
#define CABLE_R_SEL_BIT					BIT(7)
#define HVDCP_AUTH_ALG_EN_CFG_BIT			BIT(6)
#define HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT		BIT(5)
#define INPUT_PRIORITY_BIT				BIT(4)
#define AUTO_SRC_DETECT_BIT				BIT(3)
#define HVDCP_EN_BIT					BIT(2)
#define VADP_INCREMENT_VOLTAGE_LIMIT_BIT		BIT(1)
#define VADP_TAPER_TIMER_EN_BIT				BIT(0)

#define USBIN_OPTIONS_2_CFG				0x363
#define WIPWR_RST_EUD_CFG_BIT				BIT(7)
#define SWITCHER_START_CFG_BIT				BIT(6)
#define DCD_TIMEOUT_SEL_BIT				BIT(5)
#define OCD_CURRENT_SEL_BIT				BIT(4)
#define SLOW_PLUGIN_TIMER_EN_CFG_BIT			BIT(3)
#define FLOAT_OPTIONS_MASK				GENMASK(2, 0)
#define FLOAT_DIS_CHGING_CFG_BIT			BIT(2)
#define SUSPEND_FLOAT_CFG_BIT				BIT(1)
#define FORCE_FLOAT_SDP_CFG_BIT				BIT(0)

#define TAPER_TIMER_SEL_CFG				0x364
#define TYPEC_SPARE_CFG_BIT				BIT(7)
#define TYPEC_DRP_DFP_TIME_CFG_BIT			BIT(5)
#define TAPER_TIMER_SEL_MASK				GENMASK(1, 0)

#define USBIN_LOAD_CFG					0x365
#define USBIN_OV_CH_LOAD_OPTION_BIT			BIT(7)
#define ICL_OVERRIDE_AFTER_APSD_BIT			BIT(4)

#define USBIN_ICL_OPTIONS				0x366
#define CFG_USB3P0_SEL_BIT				BIT(2)
#define USB51_MODE_BIT					BIT(1)
#define USBIN_MODE_CHG_BIT				BIT(0)

#define USBIN_CMD_IL					0x340
#define USBIN_SUSPEND_BIT				BIT(0)

#define USBIN_ADAPTER_ALLOW_CFG				0x360
#define USBIN_ADAPTER_ALLOW_5V				0
#define USBIN_ADAPTER_ALLOW_5V_OR_9V			3
#define USBIN_ADAPTER_ALLOW_5V_TO_12V			12

#define CMD_HVDCP_2					0x343
#define HVDCP_FORCE_9V_BIT				BIT(4)
#define HVDCP_FORCE_5V_BIT				BIT(3)

/*
 * Nabu: the SMB5 Type-C block lives at its own base (0x1500), not in the
 * charger window.  The 0x3xx addresses that used to be used here belong to a
 * different PMIC generation: every Type-C write went to an unrelated register
 * and every read came back 0, which is why the port never left its power-on
 * role and never sourced VBUS.
 */
#define SMB5_TYPEC_BASE				0x1500
#define SMB5_TYPE_C_MODE_CFG			(SMB5_TYPEC_BASE + 0x44)
#define SMB5_TYPE_C_SNK_STATUS			(SMB5_TYPEC_BASE + 0x06)
#define SMB5_TYPE_C_SRC_STATUS			(SMB5_TYPEC_BASE + 0x08)
#define SMB5_TYPE_C_STATE_MACHINE_STATUS	(SMB5_TYPEC_BASE + 0x09)
#define SMB5_TYPE_C_MISC_STATUS			(SMB5_TYPEC_BASE + 0x0B)
#define SMB5_TYPE_C_CURRSRC_CFG			(SMB5_TYPEC_BASE + 0x52)

/* power role commands, per the vendor's smb5-reg.h */
#define TYPEC_POWER_ROLE_MASK			GENMASK(2, 0)
#define TYPEC_ROLE_SRC_ONLY			BIT(2)
#define TYPEC_ROLE_SNK_ONLY			BIT(1)
#define TYPEC_ROLE_DISABLE			BIT(0)
#define TYPEC_TRY_MODE_MASK			GENMASK(4, 3)
#define TYPEC_TRY_SNK				BIT(4)
#define TYPEC_ROLE_DRP_TRY_SNK			TYPEC_TRY_SNK
/* TYPE_C_MISC_STATUS (0x150B) */
#define TYPEC_CC_ATTACHED			BIT(0)
#define TYPEC_CC_ORIENTATION			BIT(1)
/*
 * Set once the CC debounce has completed, i.e. a partner is on the port.  In
 * source-only mode the block does NOT set CC_ATTACHED for a sink (a headset
 * enumerates and works with attached=0), so this bit is the only usable
 * "something is still plugged in" signal while we are the host.
 */
#define TYPEC_TCCDEBOUNCE_DONE			BIT(3)
#define TYPEC_SNK_SRC_MODE			BIT(6)
#define TYPEC_TRY_SRC				BIT(3)

#define TYPE_C_INTRPT_ENB_SOFTWARE_CTRL			0x368
#define EXIT_SNK_BASED_ON_CC_BIT			BIT(7)
#define VCONN_EN_ORIENTATION_BIT			BIT(6)
#define TYPEC_VCONN_OVERCURR_INT_EN_BIT			BIT(5)
#define VCONN_EN_SRC_BIT				BIT(4)
#define VCONN_EN_VALUE_BIT				BIT(3)
#define TYPEC_POWER_ROLE_CMD_MASK			GENMASK(2, 0)
#define UFP_EN_CMD_BIT					BIT(2)
#define DFP_EN_CMD_BIT					BIT(1)
#define TYPEC_DISABLE_CMD_BIT				BIT(0)

#define USBIN_CURRENT_LIMIT_CFG				0x370
#define USBIN_CURRENT_LIMIT_MASK			GENMASK(7, 0)

#define USBIN_AICL_OPTIONS_CFG				0x380
#define SUSPEND_ON_COLLAPSE_USBIN_BIT			BIT(7)
#define USBIN_AICL_HDC_EN_BIT				BIT(6)
#define USBIN_AICL_START_AT_MAX_BIT			BIT(5)
#define USBIN_AICL_RERUN_EN_BIT				BIT(4)
#define USBIN_AICL_ADC_EN_BIT				BIT(3)
#define USBIN_AICL_EN_BIT				BIT(2)
#define USBIN_HV_COLLAPSE_RESPONSE_BIT			BIT(1)
#define USBIN_LV_COLLAPSE_RESPONSE_BIT			BIT(0)

#define USBIN_5V_AICL_THRESHOLD_CFG			0x381
#define USBIN_5V_AICL_THRESHOLD_CFG_MASK		GENMASK(2, 0)

#define USBIN_CONT_AICL_THRESHOLD_CFG			0x384
#define USBIN_CONT_AICL_THRESHOLD_CFG_MASK		GENMASK(5, 0)

#define DC_ENG_SSUPPLY_CFG2				0x4C1
#define ENG_SSUPPLY_IVREF_OTG_SS_MASK			GENMASK(2, 0)
#define OTG_SS_SLOW					0x3

#define DCIN_AICL_REF_SEL_CFG				0x481
#define DCIN_CONT_AICL_THRESHOLD_CFG_MASK		GENMASK(5, 0)

#define WI_PWR_OPTIONS					0x495
#define CHG_OK_BIT					BIT(7)
#define WIPWR_UVLO_IRQ_OPT_BIT				BIT(6)
#define BUCK_HOLDOFF_ENABLE_BIT				BIT(5)
#define CHG_OK_HW_SW_SELECT_BIT				BIT(4)
#define WIPWR_RST_ENABLE_BIT				BIT(3)
#define DCIN_WIPWR_IRQ_SELECT_BIT			BIT(2)
#define AICL_SWITCH_ENABLE_BIT				BIT(1)
#define ZIN_ICL_ENABLE_BIT				BIT(0)

#define ICL_STATUS					0x607
#define INPUT_CURRENT_LIMIT_MASK			GENMASK(7, 0)

#define POWER_PATH_STATUS				0x60B
#define P_PATH_INPUT_SS_DONE_BIT			BIT(7)
#define P_PATH_USBIN_SUSPEND_STS_BIT			BIT(6)
#define P_PATH_DCIN_SUSPEND_STS_BIT			BIT(5)
#define P_PATH_USE_USBIN_BIT				BIT(4)
#define P_PATH_USE_DCIN_BIT				BIT(3)
#define P_PATH_POWER_PATH_MASK				GENMASK(2, 1)
#define P_PATH_VALID_INPUT_POWER_SOURCE_STS_BIT		BIT(0)

#define BARK_BITE_WDOG_PET				0x643
#define BARK_BITE_WDOG_PET_BIT				BIT(0)

#define WD_CFG						0x651
#define WATCHDOG_TRIGGER_AFP_EN_BIT			BIT(7)
#define BARK_WDOG_INT_EN_BIT				BIT(6)
#define BITE_WDOG_INT_EN_BIT				BIT(5)
#define SFT_AFTER_WDOG_IRQ_MASK				GENMASK(4, 3)
#define WDOG_IRQ_SFT_BIT				BIT(2)
#define WDOG_TIMER_EN_ON_PLUGIN_BIT			BIT(1)
#define WDOG_TIMER_EN_BIT				BIT(0)

#define SNARL_BARK_BITE_WD_CFG				0x653
#define BITE_WDOG_DISABLE_CHARGING_CFG_BIT		BIT(7)
#define SNARL_WDOG_TIMEOUT_MASK				GENMASK(6, 4)
#define BARK_WDOG_TIMEOUT_MASK				GENMASK(3, 2)
#define BITE_WDOG_TIMEOUT_MASK				GENMASK(1, 0)

#define AICL_RERUN_TIME_CFG				0x661
#define AICL_RERUN_TIME_MASK				GENMASK(1, 0)

#define STAT_CFG					0x690
#define STAT_SW_OVERRIDE_VALUE_BIT			BIT(7)
#define STAT_SW_OVERRIDE_CFG_BIT			BIT(6)
#define STAT_PARALLEL_OFF_DG_CFG_MASK			GENMASK(5, 4)
#define STAT_POLARITY_CFG_BIT				BIT(3)
#define STAT_PARALLEL_CFG_BIT				BIT(2)
#define STAT_FUNCTION_CFG_BIT				BIT(1)
#define STAT_IRQ_PULSING_EN_BIT				BIT(0)

#define SDP_CURRENT_UA					500000
#define CDP_CURRENT_UA					1500000
#define DCP_CURRENT_UA					1500000
#define CURRENT_MAX_UA					DCP_CURRENT_UA

/* pmi8998 registers represent current in increments of 1/40th of an amp */
#define CURRENT_SCALE_FACTOR				25000

/*
 * Software JEITA window for nabu, derived from the battery profile
 * (fg-gen4-batterydata-nabu-sunwoda-8720mah): hard limits at -10C/59C,
 * reduced charge current below 10C and a reduced float voltage above 48C.
 * The PM8150B hardware soft-limit thresholds are raw thermistor ADC codes
 * that are never programmed here, so temperature protection is applied in
 * software from the fuel-gauge temperature instead.
 * Temperatures are in tenths of a degree Celsius.
 */
#define SMB2_JEITA_HARD_COLD					(-100)
#define SMB2_JEITA_TEMP_LOW					50
#define SMB2_JEITA_TEMP_MID					100
#define SMB2_JEITA_TEMP_WARM					480
#define SMB2_JEITA_HARD_HOT					590
#define SMB2_JEITA_LOW_FCC_UA					820000
#define SMB2_JEITA_MID_FCC_UA					2500000
#define SMB2_JEITA_WARM_FV_UV					4100000
#define SMB2_JEITA_POLL_MS					15000

/* Keep retrying APSD for a while so a slow adapter classification does not
 * leave the charger stuck until the next re-plug. */
#define SMB2_APSD_MAX_RETRIES					15
/* clang-format on */

enum charger_status {
	TRICKLE_CHARGE = 0,
	PRE_CHARGE,
	FAST_CHARGE,
	FULLON_CHARGE,
	TAPER_CHARGE,
	TERMINATE_CHARGE,
	INHIBIT_CHARGE,
	DISABLE_CHARGE,
};

/* PM8150B/SMB5 uses a different BATTERY_CHARGER_STATUS_1 encoding
 * (INHIBIT first, PAUSE instead of FAST). */
enum smb5_charger_status {
	SMB5_INHIBIT_CHARGE = 0,
	SMB5_TRICKLE_CHARGE,
	SMB5_PRE_CHARGE,
	SMB5_FULLON_CHARGE,
	SMB5_TAPER_CHARGE,
	SMB5_TERMINATE_CHARGE,
	SMB5_PAUSE_CHARGE,
	SMB5_DISABLE_CHARGE,
};

struct smb2_register {
	u16 addr;
	u8 mask;
	u8 val;
};

/**
 * struct smb2_chip - smb2 chip structure
 * @dev:		Device reference for power_supply
 * @name:		The platform device name
 * @base:		Base address for smb2 registers
 * @regmap:		Register map
 * @batt_info:		Battery data from DT
 * @status_change_work: Worker to handle plug/unplug events
 * @cable_irq:		USB plugin IRQ
 * @wakeup_enabled:	If the cable IRQ will cause a wakeup
 * @usb_in_v_chan:	USB_IN voltage measurement channel
 * @chg_psy:		Charger power supply instance
 */
struct smb2_chip {
	struct device *dev;
	const char *name;
	unsigned int base;
	struct regmap *regmap;
	struct power_supply_battery_info *batt_info;

	struct delayed_work status_change_work;
	struct delayed_work jeita_work;
	int cable_irq;
	bool wakeup_enabled;

	struct iio_channel *usb_in_v_chan;

	struct power_supply *chg_psy;
	/* Nabu: cable presence as an extcon, so the peripheral-only dwc3 port
	 * can runtime-suspend while nothing is plugged in and be woken by the
	 * always-on PMIC IRQ when a cable appears. */
	struct extcon_dev *usb_extcon;
	/* Nabu: Type-C role detection.  CC is sampled by the SMB5 (the PM8150B
	 * TYPEC block never sees it on this board), so the role is polled here
	 * and pushed to the dwc3 role switch. */
	struct usb_role_switch *role_sw;
	bool role_sw_reported;
	int typec_dbg_left;
	int typec_settle;	/* extra applies: dwc3 drops the first request */
	/* consecutive polls without an attach while we are the host */
	int typec_lost_votes;
	/* consecutive polls that wanted host, to filter dual-role transients */
	int typec_host_votes;
	int typec_mode;		/* last Type-C mode written, -1 = none yet */
	/* 0 = follow CC detection, otherwise force host/device */
	enum usb_role typec_force;
	struct delayed_work typec_work;
	struct regulator *otg_vbus;
	bool typec_attached;
	bool typec_dfp;
	int typec_orientation;
	enum usb_role typec_role;
	struct regulator *dpdm_reg;
	/* Nabu: SMB5 5V boost for the Type-C port in host mode. */
	struct regulator_dev *otg_rdev;

	/* Per-PMIC register scaling. SMB2 uses 25mA/7.5mV steps, SMB5 (PM8150B)
	 * uses 50mA/10mV steps and is configured by the Type-C driver instead. */
	unsigned int curr_scale;
	unsigned int curr_max_ua;
	unsigned int fcc_ua;
	unsigned int fv_base_uv;
	unsigned int fv_step_uv;
	unsigned int fv_max_uv;
	unsigned int fv_uv;
	unsigned int icl_status_reg;
	unsigned int power_path_reg;
	unsigned int bat_ov_bit;
	int apsd_retries;
	int online_retries;
	bool charge_full;
	/* last values written to the hardware, to avoid pointless rewrites */
	unsigned int last_fcc_ua;
	unsigned int last_fv_uv;
	bool is_smb5;
};

struct smb2_match_data {
	const char *name;
	unsigned int curr_scale;
	unsigned int curr_max_ua;
	unsigned int fcc_ua;
	unsigned int fv_base_uv;
	unsigned int fv_step_uv;
	unsigned int fv_max_uv;
	unsigned int icl_status_reg;
	unsigned int power_path_reg;
	unsigned int bat_ov_bit;
	bool is_smb5;
};

static enum power_supply_property smb2_properties[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
};

static int smb2_get_prop_usb_online(struct smb2_chip *chip, int *val)
{
	unsigned int stat;
	int rc;

	rc = regmap_read(chip->regmap, chip->base + chip->power_path_reg,
			 &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read power path status: %d\n", rc);
		return rc;
	}

	*val = (stat & P_PATH_USE_USBIN_BIT) &&
	       (stat & P_PATH_VALID_INPUT_POWER_SOURCE_STS_BIT);
	return 0;
}

/*
 * Qualcomm "automatic power source detection" aka APSD
 * tells us what type of charger we're connected to.
 */
/*
 * Nabu: bring the SMB5 Type-C block up in dual-role mode.
 *
 * The mainline driver never touches this register block, so the block stays in
 * its power-on state - disabled - and the CC comparators never sample anything.
 * That is why neither the PM8150B TCPM nor this driver ever saw a cable: the
 * attach is simply not being detected by hardware until detection is enabled.
 */
static void smb2_typec_init(struct smb2_chip *chip)
{
	unsigned int cfg = 0, cfg2 = 0, cfg3 = 0, ctrl = 0, s4 = 0;
	int rc;

	regmap_read(chip->regmap, chip->base + TYPE_C_CFG, &cfg);
	regmap_read(chip->regmap, chip->base + TYPE_C_CFG_2, &cfg2);
	regmap_read(chip->regmap, chip->base + TYPE_C_CFG_3, &cfg3);
	regmap_read(chip->regmap, SMB5_TYPE_C_MODE_CFG, &ctrl);
	regmap_read(chip->regmap, SMB5_TYPE_C_MISC_STATUS, &s4);
	dev_info(chip->dev,
		 "typec init: cfg=%#x cfg2=%#x cfg3=%#x mode_cfg=%#x misc=%#x snk=%#x src=%#x state=%#x\n",
		 cfg, cfg2, cfg3, ctrl, s4,
		 ({ unsigned int v = 0; regmap_read(chip->regmap, SMB5_TYPE_C_SNK_STATUS, &v); v; }),
		 ({ unsigned int v = 0; regmap_read(chip->regmap, SMB5_TYPE_C_SRC_STATUS, &v); v; }),
		 ({ unsigned int v = 0; regmap_read(chip->regmap, SMB5_TYPE_C_STATE_MACHINE_STATUS, &v); v; }));

	/* dual role: clear TYPEC_DISABLE_CMD, enable both UFP and DFP */
	rc = regmap_update_bits(chip->regmap,
				chip->base + TYPE_C_INTRPT_ENB_SOFTWARE_CTRL,
				TYPEC_POWER_ROLE_CMD_MASK,
				DFP_EN_CMD_BIT | UFP_EN_CMD_BIT);
	dev_info(chip->dev, "typec init: drp write rc=%d\n", rc);

	/*
	 * BC1.2 (APSD) has to start on VBUS alone, not only once the Type-C
	 * block reports a CC attach.  With BC1P2_START_ON_CC set an adapter
	 * that VBUS is present for but whose CC is not detected (this board's
	 * Type-C block frequently does not see it) never runs APSD at all, so
	 * it stays unclassified and the input limit stays at the 500mA USB
	 * default - which shows up as "plugged in but not charging".  The
	 * vendor clears this bit for exactly that reason.
	 */
	rc = regmap_update_bits(chip->regmap, chip->base + TYPE_C_CFG,
				APSD_START_ON_CC_BIT, 0);
	dev_info(chip->dev, "typec init: apsd-on-vbus rc=%d\n", rc);

	msleep(50);
	rc = regmap_read(chip->regmap, chip->base + TYPE_C_INTRPT_ENB_SOFTWARE_CTRL,
			 &ctrl);
	dev_info(chip->dev, "typec init: ctrl=%#x (rc=%d)\n", ctrl, rc);
}

/*
 * Nabu: apply a Type-C role (host needs the SMB5 5V boost).
 */
static void smb2_typec_set_mode(struct smb2_chip *chip, enum usb_role role)
{
	u8 val;
	int rc;

	switch (role) {
	case USB_ROLE_HOST:
		val = TYPEC_ROLE_SRC_ONLY;	/* we source VBUS */
		break;
	case USB_ROLE_DEVICE:
		val = TYPEC_ROLE_SNK_ONLY;
		break;
	default:
		/* automatic: dual role, try sink first so a charger is seen
		 * immediately and a device is found by the try-source phase */
		val = TYPEC_ROLE_DRP_TRY_SNK;
		break;
	}

	rc = regmap_update_bits(chip->regmap, SMB5_TYPE_C_MODE_CFG,
				TYPEC_POWER_ROLE_MASK | TYPEC_TRY_MODE_MASK,
				val);
	if (rc)
		dev_warn(chip->dev, "typec: mode cfg write failed: %d\n", rc);
}

static void smb2_typec_apply_role(struct smb2_chip *chip, enum usb_role role)
{
	/*
	 * Always apply, even when the cached role already matches: dwc3 can drop
	 * a request (see below), and an early return here made "write host again"
	 * a no-op, i.e. there was no way to retry a switch that did not take.
	 */
	if (role != chip->typec_role) {
		dev_info(chip->dev, "typec: role %d -> %d\n",
			 chip->typec_role, role);
		chip->typec_settle = 2;
	}

	/*
	 * NOTE: dwc3 drops the first role request after boot - the host
	 * controller never appears even though the switch reports success.  A
	 * second, spaced request works, so users switch with
	 *   echo auto > typec_role ; sleep 3 ; echo host > typec_role
	 * (see KNOWN-ISSUES 2.6.1).  Bouncing the role from the driver was
	 * tried and made things worse, so it is left to the caller for now.
	 */
	if (chip->role_sw) {
		/* dwc3 drops the first request after boot; the poll retries */
		usb_role_switch_set_role(chip->role_sw, role);
	}

	/*
	 * Move the 5V boost only when the role really changes.  Re-asserting it
	 * on every settle poll made the regulator core complain about
	 * unbalanced disables, and a disable it did not expect can cut VBUS
	 * under a device that is still attached.
	 */
	if (role != chip->typec_role && chip->otg_vbus) {
		if (role == USB_ROLE_HOST) {
			int rc = regulator_enable(chip->otg_vbus);

			if (rc)
				dev_warn(chip->dev, "OTG 5V on failed: %d\n", rc);
		} else {
			regulator_disable(chip->otg_vbus);
		}
	}

	chip->typec_role = role;
}

/*
 * The SMB5 Type-C block never reports a CC attach on this board (all status
 * registers stay 0 even with a device plugged in, see KNOWN-ISSUES), so the
 * role can only be chosen by hand.  "host" also turns the 5V boost on, which
 * is what makes a U disk / USB keyboard / USB-C headset work at all.
 */
static ssize_t typec_role_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct smb2_chip *chip = dev_get_drvdata(dev);

	if (chip->typec_force == USB_ROLE_HOST)
		return sysfs_emit(buf, "host\n");
	if (chip->typec_force == USB_ROLE_DEVICE)
		return sysfs_emit(buf, "device\n");
	return sysfs_emit(buf, "auto\n");
}

static ssize_t typec_role_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct smb2_chip *chip = dev_get_drvdata(dev);
	enum usb_role role;

	if (sysfs_streq(buf, "host"))
		role = USB_ROLE_HOST;
	else if (sysfs_streq(buf, "device"))
		role = USB_ROLE_DEVICE;
	else if (sysfs_streq(buf, "auto"))
		role = USB_ROLE_NONE;
	else
		return -EINVAL;

	chip->typec_force = role;

	/*
	 * A forced role also has to program the Type-C block: without this
	 * "host" only moved the USB role and switched the boost on while the
	 * port stayed in dual role, so an attached device never saw Rp and the
	 * port had to be bounced through "auto" first.
	 */
	if (role != USB_ROLE_NONE && role != chip->typec_mode) {
		smb2_typec_set_mode(chip, role);
		chip->typec_mode = role;
	}
	smb2_typec_apply_role(chip, role);

	if (role == USB_ROLE_NONE)	/* "auto": start polling CC */
		schedule_delayed_work(&chip->typec_work, 0);
	else
		cancel_delayed_work_sync(&chip->typec_work);

	return count;
}
static DEVICE_ATTR_RW(typec_role);

/* Nabu: dump the SMB5 Type-C registers, to see how the state machine is set up. */
static ssize_t typec_regs_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	static const struct { const char *name; u16 reg; } r[] = {
		{ "MODE_CFG",       SMB5_TYPEC_BASE + 0x44 },
		{ "EXIT_STATE_CFG", SMB5_TYPEC_BASE + 0x50 },
		{ "CURRSRC_CFG",    SMB5_TYPEC_BASE + 0x52 },
		{ "INTERRUPT_EN_1", SMB5_TYPEC_BASE + 0x5E },
		{ "INTERRUPT_EN_2", SMB5_TYPEC_BASE + 0x60 },
		{ "DEBOUNCE",       SMB5_TYPEC_BASE + 0x62 },
		{ "U_USB_CFG",      SMB5_TYPEC_BASE + 0x70 },
		{ "MICRO_USB_MODE", SMB5_TYPEC_BASE + 0x73 },
		{ "SNK_STATUS",     SMB5_TYPEC_BASE + 0x06 },
		{ "SRC_STATUS",     SMB5_TYPEC_BASE + 0x08 },
		{ "STATE_MACHINE",  SMB5_TYPEC_BASE + 0x09 },
		{ "MISC_STATUS",    SMB5_TYPEC_BASE + 0x0B },
	};
	struct smb2_chip *chip = dev_get_drvdata(dev);
	int i, len = 0;
	unsigned int v;

	for (i = 0; i < ARRAY_SIZE(r); i++) {
		if (regmap_read(chip->regmap, r[i].reg, &v))
			continue;
		len += sysfs_emit_at(buf, len, "%-15s %#04x = %#04x\n",
				     r[i].name, r[i].reg, v);
	}

	/* charger/DCDC side: what the 5V boost is doing */
	if (!regmap_read(chip->regmap, chip->base + SMB5_DCDC_CMD_OTG, &v))
		len += sysfs_emit_at(buf, len, "%-15s %#04x = %#04x\n",
				     "OTG_CMD", chip->base + SMB5_DCDC_CMD_OTG, v);
	if (!regmap_read(chip->regmap, chip->base + SMB5_DCDC_OTG_ILIM, &v))
		len += sysfs_emit_at(buf, len, "%-15s %#04x = %#04x\n",
				     "OTG_ILIM", chip->base + SMB5_DCDC_OTG_ILIM, v);
	if (!regmap_read(chip->regmap, chip->base + SMB5_DCDC_OTG_CFG, &v))
		len += sysfs_emit_at(buf, len, "%-15s %#04x = %#04x\n",
				     "OTG_CFG", chip->base + SMB5_DCDC_OTG_CFG, v);

	/* adapter detection / charge path, for debugging "not charging" */
	{
		static const struct { const char *name; u16 reg; } c[] = {
			{ "APSD_STATUS",   APSD_STATUS },
			{ "APSD_RESULT",   APSD_RESULT_STATUS },
			{ "CMD_APSD",      CMD_APSD },
			{ "CHG_EN_CMD",    CHARGING_ENABLE_CMD },
			{ "USBIN_OPT_1",   USBIN_OPTIONS_1_CFG },
			{ "USBIN_OPT_2",   USBIN_OPTIONS_2_CFG },
			{ "USBIN_ILIM",    USBIN_CURRENT_LIMIT_CFG },
			{ "USBIN_LOAD",    USBIN_LOAD_CFG },
			{ "USBIN_ICL_OPT", USBIN_ICL_OPTIONS },
			{ "CMD_ICL_OVR",   CMD_ICL_OVERRIDE },
			{ "BATT_CHG_STS",  BATTERY_CHARGER_STATUS_1 },
			{ "BATT_CHG_STS2", BATTERY_CHARGER_STATUS_2 },
			{ "JEITA_EN",      JEITA_EN_CFG },
		};
		int j;

		for (j = 0; j < ARRAY_SIZE(c); j++) {
			if (regmap_read(chip->regmap, chip->base + c[j].reg, &v))
				continue;
			len += sysfs_emit_at(buf, len, "%-15s %#04x = %#04x\n",
					     c[j].name, chip->base + c[j].reg, v);
		}
	}

	return len;
}
static DEVICE_ATTR_RO(typec_regs);

/*
 * Nabu: read what the SMB5 thinks of the Type-C port.
 *
 * TYPE_C_STATUS_4 carries the whole result of the chip's own CC state machine:
 *   bit 0  CC_ATTACHED          something is on the other end
 *   bit 1  CC_ORIENTATION       flipped plug
 *   bit 7  UFP_DFP_MODE_STATUS  0 = we are the UFP (a source/charger is attached,
 *                               so this port is a device), 1 = we are the DFP
 *                               (a sink such as an OTG adapter, dock or keyboard
 *                               cover is attached, so this port is the host)
 * The PM8150B TYPEC block does not report CC at all on this board, which is why
 * the mainline TCPM never sees an attach.
 */
static void smb2_typec_poll(struct work_struct *work)
{
	struct smb2_chip *chip =
		container_of(work, struct smb2_chip, typec_work.work);
	unsigned int misc = 0;
	bool attached, present, we_are_source;
	enum usb_role role, mode;
	bool changed = false;
	int online = 0;

	if (!chip->role_sw) {
		chip->role_sw = usb_role_switch_get(chip->dev);
		if (IS_ERR(chip->role_sw)) {
			if (!chip->role_sw_reported) {
				dev_warn(chip->dev,
					 "no USB role switch yet: %ld\n",
					 PTR_ERR(chip->role_sw));
				chip->role_sw_reported = true;
			}
			chip->role_sw = NULL;
		} else {
			dev_info(chip->dev, "typec: role switch ready\n");
		}
	}

	if (regmap_read(chip->regmap, SMB5_TYPE_C_MISC_STATUS, &misc))
		goto out;

	attached = !!(misc & TYPEC_CC_ATTACHED);
	present = attached || !!(misc & TYPEC_TCCDEBOUNCE_DONE);
	we_are_source = !!(misc & TYPEC_SNK_SRC_MODE);

	changed = attached != chip->typec_attached ||
		  we_are_source != chip->typec_dfp;
	if (changed) {
		/*
		 * debug level on purpose: while the port rotates in dual role it
		 * flips we_are_source every couple of seconds with nothing
		 * attached, and that is not something anyone needs in dmesg.
		 */
		dev_dbg(chip->dev,
			"typec: attached=%d present=%d we_are_source=%d orientation=%d (misc=%#x)\n",
			attached, present, we_are_source,
			!!(misc & TYPEC_CC_ORIENTATION), misc);
		chip->typec_attached = attached;
		chip->typec_dfp = we_are_source;
	}

	/*
	 * Automatic role: a sink on the port means we are its host and have to
	 * feed it 5V; a charger or a PC - or nothing at all - keeps us the
	 * device.  The Type-C block runs in dual role, so it detects both.
	 */
	/*
	 * Three-way decision.  While nothing is attached the block has to stay in
	 * dual role: a device (sink) only pulls Dp up once we offer VBUS, so
	 * pinning the port to sink whenever it is idle would make devices
	 * invisible forever - which is exactly what happened before.
	 */
	smb2_get_prop_usb_online(chip, &online);

	if (chip->typec_force != USB_ROLE_NONE) {
		role = chip->typec_force;
		mode = role;
	} else if (online) {
		/*
		 * Somebody is feeding VBUS - a charger or a PC.  Never act as the
		 * host then: doing so would drive our own 5V into the charger (and
		 * the charging state would flip-flop).  This also filters the
		 * dual-role rotation, where the port can misread the same charger
		 * as a sink for a moment.
		 *
		 * Stay in dual role rather than pinning the port to sink: the
		 * Type-C block has to keep its own CC state machine running for
		 * APSD to classify the adapter, and pinning it to sink made a PD
		 * charger come up as a plain 500mA SDP source (measured:
		 * current_max 500000 with the battery net discharging).
		 */
		role = USB_ROLE_DEVICE;
		mode = USB_ROLE_NONE;
		chip->typec_host_votes = 0;
	} else if (chip->typec_role == USB_ROLE_HOST) {
		/*
		 * Sourcing VBUS for a device: stay there until it is really gone.
		 * The vendor's PD stack behaves the same way - the role is set
		 * once on attach and cleared on detach, never re-derived while the
		 * partner is attached - and re-deriving it here is what made the
		 * headset drop off (the CC status blips to "no attach" now and
		 * then even though the device is still there).
		 */
		if (present) {
			chip->typec_lost_votes = 0;
			goto out;		/* still there: keep hosting */
		}
		if (++chip->typec_lost_votes < 5)
			goto out;		/* a CC blip, not a detach */

		/* Really gone: drop the boost and go back to dual role, so a
		 * charger plugged in next is detected normally. */
		chip->typec_lost_votes = 0;
		role = USB_ROLE_DEVICE;
		mode = USB_ROLE_NONE;
	} else if (attached && we_are_source) {
		/*
		 * A sink is on the port (stick, headset, keyboard): we have to be
		 * its host and feed it 5V.  Ask for the same reading twice so a
		 * single sample taken mid dual-role rotation cannot turn the port
		 * around by mistake.
		 */
		if (++chip->typec_host_votes < 2) {
			role = chip->typec_role;
			mode = chip->typec_mode;
			goto apply;
		}
		role = USB_ROLE_HOST;
		mode = USB_ROLE_HOST;
		chip->typec_lost_votes = 0;
	} else if (attached) {
		/*
		 * Attached, but we are the UFP: a charger or a PC.  Dual role
		 * again, for the same reason as above.
		 */
		role = USB_ROLE_DEVICE;
		mode = USB_ROLE_NONE;
		chip->typec_host_votes = 0;
	} else {
		/* nothing attached: stay in dual role so devices are visible */
		role = USB_ROLE_DEVICE;
		mode = USB_ROLE_NONE;
		chip->typec_host_votes = 0;
		chip->typec_lost_votes = 0;
	}

apply:
	if ((int)mode != chip->typec_mode) {
		smb2_typec_set_mode(chip, mode);
		chip->typec_mode = (int)mode;
		chip->typec_settle = 2;
	}

	if (role != chip->typec_role || chip->typec_settle) {
		enum usb_role prev = chip->typec_role;

		smb2_typec_apply_role(chip, role);
		chip->typec_role = role;
		if (chip->typec_settle)
			chip->typec_settle--;

		/*
		 * Leaving host mode: while we were sourcing VBUS the charger saw a
		 * live port the whole time, so its plug interrupt never fires and
		 * APSD is not re-run - a charger attached right after unplugging a
		 * device went unnoticed until the cable was re-seated.  Kick the
		 * classification ourselves.
		 */
		if (prev == USB_ROLE_HOST && role != USB_ROLE_HOST) {
			chip->apsd_retries = 0;
			schedule_delayed_work(&chip->status_change_work,
					      msecs_to_jiffies(200));
		}
	}

out:
	if (chip->typec_force == USB_ROLE_NONE) {
		/*
		 * While the sink hold is running the port is pinned to sink, so it
		 * cannot see a device; poll quickly until it expires and the port
		 * goes back to dual role, then fall back to the long safety net.
		 */
		bool busy = chip->typec_host_votes > 0 ||
			    chip->typec_lost_votes > 0 ||
			    changed || chip->typec_settle;
		unsigned long delay = busy ?
				       msecs_to_jiffies(SMB2_TYPEC_POLL_MS_BUSY) :
				       msecs_to_jiffies(SMB2_TYPEC_POLL_MS);

		schedule_delayed_work(&chip->typec_work, delay);
	}
}

static const unsigned int smb2_usb_extcon_cables[] = {
	EXTCON_USB,
	EXTCON_NONE,
};

/*
 * Tell the USB controller whether a cable is attached.  The PMIC sees VBUS
 * before the controller does (the controller may even be runtime-suspended),
 * which is what makes the port's runtime PM workable on this device.
 */
static void smb2_sync_usb_extcon(struct smb2_chip *chip, int online)
{
	if (!chip->usb_extcon)
		return;

	extcon_set_state_sync(chip->usb_extcon, EXTCON_USB, !!online);
}

static int smb2_apsd_get_charger_type(struct smb2_chip *chip, int *val)
{
	unsigned int apsd_stat, stat;
	int usb_online = 0;
	int rc;

	rc = smb2_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		*val = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		return rc;
	}

	rc = regmap_read(chip->regmap, chip->base + APSD_STATUS, &apsd_stat);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read apsd status, rc = %d", rc);
		return rc;
	}
	if (!(apsd_stat & APSD_DTC_STATUS_DONE_BIT)) {
		dev_dbg(chip->dev, "Apsd not ready");
		return -EAGAIN;
	}

	rc = regmap_read(chip->regmap, chip->base + APSD_RESULT_STATUS, &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read apsd result, rc = %d", rc);
		return rc;
	}

	stat &= APSD_RESULT_STATUS_MASK;

	if (stat & CDP_CHARGER_BIT)
		*val = POWER_SUPPLY_USB_TYPE_CDP;
	else if (stat & (DCP_CHARGER_BIT | OCP_CHARGER_BIT | FLOAT_CHARGER_BIT))
		*val = POWER_SUPPLY_USB_TYPE_DCP;
	else /* SDP_CHARGER_BIT (or others) */
		*val = POWER_SUPPLY_USB_TYPE_SDP;

	return 0;
}

static int smb2_get_prop_status(struct smb2_chip *chip, int *val)
{
	unsigned char stat[2];
	int usb_online = 0;
	int rc;

	rc = smb2_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		*val = POWER_SUPPLY_STATUS_DISCHARGING;
		return rc;
	}

	/*
	 * The PM8150B ADC charge termination does not always fire on nabu,
	 * so additionally latch "full" from the fuel gauge: 100% and a
	 * tapered charge current. This mirrors the vendor charge_full flag
	 * and keeps the reported status (and UPower) at "full" instead of
	 * endlessly "charging".
	 */
	if (chip->is_smb5 && chip->charge_full) {
		*val = POWER_SUPPLY_STATUS_FULL;
		return 0;
	}

	rc = regmap_bulk_read(chip->regmap,
			      chip->base + BATTERY_CHARGER_STATUS_1, &stat, 2);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read charging status ret=%d\n",
			rc);
		return rc;
	}

	if (stat[1] & chip->bat_ov_bit) {
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return 0;
	}

	stat[0] = stat[0] & BATTERY_CHARGER_STATUS_MASK;

	if (chip->is_smb5) {
		switch (stat[0]) {
		case SMB5_TRICKLE_CHARGE:
		case SMB5_PRE_CHARGE:
		case SMB5_FULLON_CHARGE:
		case SMB5_TAPER_CHARGE:
			*val = POWER_SUPPLY_STATUS_CHARGING;
			return 0;
		case SMB5_TERMINATE_CHARGE:
		case SMB5_INHIBIT_CHARGE:
			*val = POWER_SUPPLY_STATUS_FULL;
			return 0;
		case SMB5_DISABLE_CHARGE:
		case SMB5_PAUSE_CHARGE:
		default:
			*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
			return 0;
		}
	}

	switch (stat[0]) {
	case TRICKLE_CHARGE:
	case PRE_CHARGE:
	case FAST_CHARGE:
	case FULLON_CHARGE:
	case TAPER_CHARGE:
		*val = POWER_SUPPLY_STATUS_CHARGING;
		return rc;
	case DISABLE_CHARGE:
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return rc;
	case TERMINATE_CHARGE:
	case INHIBIT_CHARGE:
		*val = POWER_SUPPLY_STATUS_FULL;
		return rc;
	default:
		*val = POWER_SUPPLY_STATUS_UNKNOWN;
		return rc;
	}
}

static inline int smb2_get_current_limit(struct smb2_chip *chip,
					 unsigned int *val)
{
	int rc = regmap_read(chip->regmap, chip->base + chip->icl_status_reg,
			     val);

	if (rc >= 0)
		*val *= chip->curr_scale;
	return rc;
}

static int smb2_set_current_limit(struct smb2_chip *chip, unsigned int val)
{
	unsigned char val_raw;
	int rc;

	if (val > chip->curr_max_ua) {
		dev_err(chip->dev,
			"Can't set current limit higher than %uuA",
			chip->curr_max_ua);
		return -EINVAL;
	}
	val_raw = val / chip->curr_scale;

	rc = regmap_write(chip->regmap, chip->base + USBIN_CURRENT_LIMIT_CFG,
			  val_raw);
	if (rc)
		return rc;

	if (!chip->is_smb5)
		return 0;

	/* Keep the USBIN on the charge path rather than the 5V/500mA one. */
	rc = regmap_update_bits(chip->regmap, chip->base + USBIN_ICL_OPTIONS,
				USB51_MODE_BIT | USBIN_MODE_CHG_BIT,
				USBIN_MODE_CHG_BIT);
	if (rc)
		return rc;

	if (val > 500000) {
		/*
		 * High-current mode: honour the value above instead of the
		 * APSD result.  Without this the adapter stays clamped to the
		 * USB default and the tablet only trickle-charges even though
		 * APSD already classified it as DCP.
		 */
		rc = regmap_update_bits(chip->regmap,
					chip->base + USBIN_LOAD_CFG,
					ICL_OVERRIDE_AFTER_APSD_BIT,
					ICL_OVERRIDE_AFTER_APSD_BIT);
		if (rc)
			return rc;
		rc = regmap_update_bits(chip->regmap,
					chip->base + CMD_ICL_OVERRIDE,
					ICL_OVERRIDE_CMD_BIT, 0);
	} else {
		/* Back to hardware auto for the low-current cases. */
		rc = regmap_update_bits(chip->regmap,
					chip->base + USBIN_LOAD_CFG,
					ICL_OVERRIDE_AFTER_APSD_BIT, 0);
	}

	return rc;
}

static void smb2_status_change_work(struct work_struct *work)
{
	unsigned int charger_type, current_ua;
	int usb_online = 0;
	int count, rc;
	struct smb2_chip *chip;

	chip = container_of(work, struct smb2_chip, status_change_work.work);

	smb2_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		/* The adapter may already be attached at boot before the power
		 * path settles; keep retrying instead of relying on a plug
		 * interrupt that never comes for a boot-time cable. */
		if (chip->is_smb5 && chip->online_retries < 30) {
			chip->online_retries++;
			schedule_delayed_work(&chip->status_change_work,
					      msecs_to_jiffies(1000));
		}
		return;
	}
	chip->online_retries = 0;

	/* The adapter is online: (re)start the software JEITA monitor. */
	mod_delayed_work(system_wq, &chip->jeita_work, 0);

	/* Release Dp/Dm from the USB PHY so the charger can run APSD/QC. */
	if (chip->dpdm_reg) {
		rc = regulator_enable(chip->dpdm_reg);
		if (rc < 0)
			dev_warn(chip->dev, "failed to enable DPDM: %d\n", rc);
	}

	/*
	 * APSD is asynchronous. Kick it once and then poll while Dp/Dm stay
	 * released. Writing APSD_RERUN on every iteration restarts the
	 * detection before it can finish, so the DTC status bit never
	 * completes and the adapter is never classified (input then stays at
	 * the low USB default and the battery slowly discharges).
	 */
	regmap_update_bits(chip->regmap, chip->base + CMD_APSD,
			   APSD_RERUN_BIT, APSD_RERUN_BIT);
	for (count = 0; count < 20; count++) {
		msleep(100);
		rc = smb2_apsd_get_charger_type(chip, &charger_type);
		if (rc != -EAGAIN)
			break;
		dev_dbg(chip->dev, "charger type not ready yet (%d)\n",
			count);
	}

	if (chip->dpdm_reg)
		regulator_disable(chip->dpdm_reg);

	if (rc == -EAGAIN) {
		unsigned int apsd = 0;

		regmap_read(chip->regmap, chip->base + APSD_STATUS, &apsd);
		if (chip->apsd_retries == 0)
			dev_info(chip->dev, "apsd not ready: status=%#x\n",
				 apsd);
		if (chip->is_smb5 &&
		    chip->apsd_retries < SMB2_APSD_MAX_RETRIES) {
			chip->apsd_retries++;
			schedule_delayed_work(&chip->status_change_work,
					      msecs_to_jiffies(2000));
		}
		return;
	}

	if (rc < 0) {
		dev_err(chip->dev, "get charger type failed: %d\n", rc);
		return;
	}

	switch (charger_type) {
	case POWER_SUPPLY_USB_TYPE_CDP:
		current_ua = CDP_CURRENT_UA;
		break;
	case POWER_SUPPLY_USB_TYPE_DCP:
		/* SMB5 boards pair this with charge pumps and expect the
		 * plain USB path to use the full DCP budget. */
		current_ua = chip->is_smb5 ? 3000000 : DCP_CURRENT_UA;
		break;
	case POWER_SUPPLY_USB_TYPE_SDP:
	default:
		/* Don't throttle a misclassified wall charger down to the USB
		 * spec 500mA on SMB5 boards. */
		current_ua = chip->is_smb5 ? 1500000 : SDP_CURRENT_UA;
		break;
	}

	smb2_set_current_limit(chip, current_ua);
	dev_info(chip->dev, "APSD: charger type %s, input limit %uuA\n",
		 charger_type == POWER_SUPPLY_USB_TYPE_DCP ? "DCP" :
		 charger_type == POWER_SUPPLY_USB_TYPE_CDP ? "CDP" :
		 charger_type == POWER_SUPPLY_USB_TYPE_SDP ? "SDP" : "unknown",
		 current_ua);

	/*
	 * Do NOT ask a QC adapter for 9V.  On nabu the high-voltage path is the
	 * LN8000 charge pump, which needs its own regulation loop; without it
	 * the port is left at 9.6V with the input limit collapsed to 100mA and
	 * the battery discharging (measured), while 5V charges fine.  Stay at
	 * 5V until the pump path is wired up.
	 */

	/* SMB5: an adapter that was already attached at boot can be
	 * mis-classified as SDP before D+/D- settle. Retry so the QC/HVDCP
	 * handshake is performed without requiring a re-plug. */
	if (chip->is_smb5 && charger_type == POWER_SUPPLY_USB_TYPE_SDP &&
	    chip->apsd_retries < SMB2_APSD_MAX_RETRIES) {
		chip->apsd_retries++;
		schedule_delayed_work(&chip->status_change_work,
				      msecs_to_jiffies(2000));
	} else {
		chip->apsd_retries = 0;
	}

	power_supply_changed(chip->chg_psy);
}

static int smb2_set_fast_charge_current(struct smb2_chip *chip, unsigned int ua)
{
	/*
	 * Rewriting the setpoint makes the SMB5 restart its regulation loop, so
	 * the charge current visibly dips for a moment.  The software JEITA
	 * monitor re-applies the same value every 15 s, which showed up as a
	 * "unstable charging power" oscillation between ~1.1 and ~1.5 A.  Only
	 * touch the hardware when the value really changes.
	 */
	if (ua == chip->last_fcc_ua)
		return 0;

	chip->last_fcc_ua = ua;
	return regmap_update_bits(chip->regmap,
				  chip->base + FAST_CHARGE_CURRENT_CFG,
				  FAST_CHARGE_CURRENT_SETTING_MASK,
				  ua / chip->curr_scale);
}

static int smb2_set_float_voltage(struct smb2_chip *chip, unsigned int uv)
{
	unsigned int raw;

	if (uv < chip->fv_base_uv)
		uv = chip->fv_base_uv;

	if (uv == chip->last_fv_uv)	/* same reason as the current setpoint */
		return 0;

	chip->last_fv_uv = uv;
	raw = (uv - chip->fv_base_uv) / chip->fv_step_uv;

	return regmap_update_bits(chip->regmap, chip->base + FLOAT_VOLTAGE_CFG,
				  FLOAT_VOLTAGE_SETTING_MASK, raw);
}

/*
 * Software JEITA. The PM8150B hardware soft-limit thresholds are raw
 * thermistor ADC codes that are not programmed on nabu, so enabling them
 * makes the charger latch a bogus over-temperature state and throttle
 * charging. Instead read the (accurate) fuel-gauge temperature and bound the
 * charge current and float voltage, disabling charging outside the safe
 * window. This mirrors the downstream step-chg-jeita policy for this battery.
 */
static void smb2_jeita_work(struct work_struct *work)
{
	struct smb2_chip *chip = container_of(work, struct smb2_chip,
					      jeita_work.work);
	union power_supply_propval val;
	struct power_supply *batt_psy;
	int usb_online = 0, temp, fcc_ua, fv_uv, rc;

	smb2_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		if (chip->charge_full) {
			chip->charge_full = false;
			power_supply_changed(chip->chg_psy);
		}
		return;
	}

	batt_psy = power_supply_get_by_name("qcom-battery");
	if (!batt_psy)
		goto resched;

	rc = power_supply_get_property(batt_psy, POWER_SUPPLY_PROP_TEMP, &val);
	if (rc) {
		power_supply_put(batt_psy);
		goto resched;
	}
	temp = val.intval;

	/*
	 * Software charge-complete latch, consumed by smb2_get_prop_status().
	 * The fuel gauge reports capacity and a current with positive =
	 * charging. Latch full at 100% with a tapered current, and clear it
	 * again once the pack drops or charging resumes hard. Notify on every
	 * change so the fuel gauge (and UPower) pick the new status up.
	 */
	rc = power_supply_get_property(batt_psy, POWER_SUPPLY_PROP_CAPACITY,
				       &val);
	if (!rc) {
		int cap = val.intval;
		int cur = 0;
		bool full = chip->charge_full;

		if (!power_supply_get_property(batt_psy,
					       POWER_SUPPLY_PROP_CURRENT_NOW,
					       &val))
			cur = val.intval;

		if (cap >= 100 && cur < 400000)
			full = true;
		else if (cap <= 98 || cur >= 500000)
			full = false;

		if (full != chip->charge_full) {
			chip->charge_full = full;
			power_supply_changed(chip->chg_psy);
		}
	}
	power_supply_put(batt_psy);

	if (temp < SMB2_JEITA_HARD_COLD || temp >= SMB2_JEITA_HARD_HOT) {
		regmap_update_bits(chip->regmap, chip->base + CHARGING_ENABLE_CMD,
				   CHARGING_ENABLE_CMD_BIT, 0);
		dev_info(chip->dev,
			 "JEITA: battery %d (0.1C) out of range, charging disabled\n",
			 temp);
		goto resched;
	}

	regmap_update_bits(chip->regmap, chip->base + CHARGING_ENABLE_CMD,
			   CHARGING_ENABLE_CMD_BIT, CHARGING_ENABLE_CMD_BIT);

	if (temp < SMB2_JEITA_TEMP_LOW)
		fcc_ua = SMB2_JEITA_LOW_FCC_UA;
	else if (temp < SMB2_JEITA_TEMP_MID)
		fcc_ua = SMB2_JEITA_MID_FCC_UA;
	else
		fcc_ua = chip->fcc_ua;

	fv_uv = (temp >= SMB2_JEITA_TEMP_WARM) ? SMB2_JEITA_WARM_FV_UV
						: chip->fv_uv;

	smb2_set_fast_charge_current(chip, fcc_ua);
	smb2_set_float_voltage(chip, fv_uv);

resched:
	schedule_delayed_work(&chip->jeita_work,
			      msecs_to_jiffies(SMB2_JEITA_POLL_MS));
}

static int smb2_get_iio_chan(struct smb2_chip *chip, struct iio_channel *chan,
			     int *val)
{
	if (!chan) {
		*val = 0;
		return 0;
	}

	if (IS_ERR(chan)) {
		dev_err(chip->dev, "Failed to chan, err = %li", PTR_ERR(chan));
		return PTR_ERR(chan);
	}

	return iio_read_channel_processed(chan, val);
}

static int smb2_get_prop_health(struct smb2_chip *chip, int *val)
{
	int rc;
	unsigned int stat;

	rc = regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_2,
			 &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read charger status rc=%d\n", rc);
		return rc;
	}

	switch (stat) {
	case CHARGER_ERROR_STATUS_BAT_OV_BIT:
		*val = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		return 0;
	case BAT_TEMP_STATUS_TOO_COLD_BIT:
		*val = POWER_SUPPLY_HEALTH_COLD;
		return 0;
	case BAT_TEMP_STATUS_TOO_HOT_BIT:
		*val = POWER_SUPPLY_HEALTH_OVERHEAT;
		return 0;
	case BAT_TEMP_STATUS_COLD_SOFT_LIMIT_BIT:
		*val = POWER_SUPPLY_HEALTH_COOL;
		return 0;
	case BAT_TEMP_STATUS_HOT_SOFT_LIMIT_BIT:
		*val = POWER_SUPPLY_HEALTH_WARM;
		return 0;
	default:
		*val = POWER_SUPPLY_HEALTH_GOOD;
		return 0;
	}
}

static int smb2_get_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     union power_supply_propval *val)
{
	struct smb2_chip *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Qualcomm";
		return 0;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = chip->name;
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return smb2_get_current_limit(chip, &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return smb2_get_iio_chan(chip, chip->usb_in_v_chan,
					 &val->intval);
	case POWER_SUPPLY_PROP_ONLINE:
		return smb2_get_prop_usb_online(chip, &val->intval);
	case POWER_SUPPLY_PROP_STATUS:
		return smb2_get_prop_status(chip, &val->intval);
	case POWER_SUPPLY_PROP_HEALTH:
		return smb2_get_prop_health(chip, &val->intval);
	case POWER_SUPPLY_PROP_USB_TYPE:
		return smb2_apsd_get_charger_type(chip, &val->intval);
	default:
		dev_err(chip->dev, "invalid property: %d\n", psp);
		return -EINVAL;
	}
}

static int smb2_set_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     const union power_supply_propval *val)
{
	struct smb2_chip *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return smb2_set_current_limit(chip, val->intval);
	default:
		dev_err(chip->dev, "No setter for property: %d\n", psp);
		return -EINVAL;
	}
}

static int smb2_property_is_writable(struct power_supply *psy,
				     enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return 1;
	default:
		return 0;
	}
}

static irqreturn_t smb2_handle_batt_overvoltage(int irq, void *data)
{
	struct smb2_chip *chip = data;
	unsigned int status;

	regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_2,
		    &status);

	if (status & CHARGER_ERROR_STATUS_BAT_OV_BIT) {
		/* The hardware stops charging automatically */
		dev_err(chip->dev, "battery overvoltage detected\n");
		power_supply_changed(chip->chg_psy);
	}

	return IRQ_HANDLED;
}

/*
 * Nabu: the Type-C block raises an interrupt on attach/detach and CC changes.
 * React to it immediately instead of waiting for the next 2 s poll - polling
 * alone left the port blind for several seconds after a plug.
 */
static irqreturn_t smb2_handle_typec_irq(int irq, void *data)
{
	struct smb2_chip *chip = data;

	schedule_delayed_work(&chip->typec_work, 0);

	return IRQ_HANDLED;
}

static irqreturn_t smb2_handle_usb_plugin(int irq, void *data)
{
	struct smb2_chip *chip = data;
	int online = 0;

	chip->apsd_retries = 0;
	if (!smb2_get_prop_usb_online(chip, &online))
		smb2_sync_usb_extcon(chip, online);
	power_supply_changed(chip->chg_psy);

	schedule_delayed_work(&chip->status_change_work,
			      msecs_to_jiffies(1500));

	return IRQ_HANDLED;
}

static irqreturn_t smb2_handle_usb_icl_change(int irq, void *data)
{
	struct smb2_chip *chip = data;

	power_supply_changed(chip->chg_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smb2_handle_wdog_bark(int irq, void *data)
{
	struct smb2_chip *chip = data;
	int rc;

	power_supply_changed(chip->chg_psy);

	rc = regmap_write(chip->regmap, BARK_BITE_WDOG_PET,
			  BARK_BITE_WDOG_PET_BIT);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't pet the dog rc=%d\n", rc);

	return IRQ_HANDLED;
}

static const struct power_supply_desc smb2_psy_desc = {
	.name = "pmi8998_charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_SDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_CDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_DCP) |
		     BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN),
	.properties = smb2_properties,
	.num_properties = ARRAY_SIZE(smb2_properties),
	.get_property = smb2_get_property,
	.set_property = smb2_set_property,
	.property_is_writeable = smb2_property_is_writable,
};

/* Init sequence derived from vendor downstream driver */
static const struct smb2_register smb2_init_seq[] = {
	{ .addr = AICL_RERUN_TIME_CFG, .mask = AICL_RERUN_TIME_MASK, .val = 0 },
	/*
	 * By default configure us as an upstream facing port
	 * FIXME: This will be handled by the type-c driver
	 */
	{ .addr = TYPE_C_INTRPT_ENB_SOFTWARE_CTRL,
	  .mask = TYPEC_POWER_ROLE_CMD_MASK | VCONN_EN_SRC_BIT |
		  VCONN_EN_VALUE_BIT,
	  .val = VCONN_EN_SRC_BIT },
	/*
	 * Disable Type-C factory mode and stay in Attached.SRC state when VCONN
	 * over-current happens
	 */
	{ .addr = TYPE_C_CFG,
	  .mask = FACTORY_MODE_DETECTION_EN_BIT | VCONN_OC_CFG_BIT,
	  .val = 0 },
	/* Configure VBUS for software control */
	{ .addr = OTG_CFG, .mask = OTG_EN_SRC_CFG_BIT, .val = 0 },
	/*
	 * Use VBAT to determine the recharge threshold when battery is full
	 * rather than the state of charge.
	 */
	{ .addr = FG_UPDATE_CFG_2_SEL,
	  .mask = SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT |
		  VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT,
	  .val = VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT },
	/* Enable charging */
	{ .addr = USBIN_OPTIONS_1_CFG, .mask = HVDCP_EN_BIT, .val = 0 },
	{ .addr = CHARGING_ENABLE_CMD,
	  .mask = CHARGING_ENABLE_CMD_BIT,
	  .val = CHARGING_ENABLE_CMD_BIT },
	/*
	 * Match downstream defaults
	 * CHG_EN_SRC_BIT - charger enable is controlled by software
	 * CHG_EN_POLARITY_BIT - polarity of charge enable pin when in HW control
	 *                       pulled low on OnePlus 6 and SHIFT6mq
	 * PRETOFAST_TRANSITION_CFG_BIT -
	 * BAT_OV_ECC_BIT -
	 * I_TERM_BIT - Current termination ?? 0 = enabled
	 * AUTO_RECHG_BIT - Enable automatic recharge when battery is full
	 *                  0 = enabled
	 * EN_ANALOG_DROP_IN_VBATT_BIT
	 * CHARGER_INHIBIT_BIT - Inhibit charging based on battery voltage
	 *                       instead of ??
	 */
	{ .addr = CHGR_CFG2,
	  .mask = CHG_EN_SRC_BIT | CHG_EN_POLARITY_BIT |
		  PRETOFAST_TRANSITION_CFG_BIT | BAT_OV_ECC_BIT | I_TERM_BIT |
		  AUTO_RECHG_BIT | EN_ANALOG_DROP_IN_VBATT_BIT |
		  CHARGER_INHIBIT_BIT,
	  .val = CHARGER_INHIBIT_BIT },
	/* STAT pin software override, match downstream. Parallel charging? */
	{ .addr = STAT_CFG,
	  .mask = STAT_SW_OVERRIDE_CFG_BIT,
	  .val = STAT_SW_OVERRIDE_CFG_BIT },
	/* Set the default SDP charger type to a 500ma USB 2.0 port */
	{ .addr = USBIN_ICL_OPTIONS,
	  .mask = USB51_MODE_BIT | USBIN_MODE_CHG_BIT,
	  .val = USB51_MODE_BIT },
	/* Disable watchdog */
	{ .addr = SNARL_BARK_BITE_WD_CFG, .mask = 0xff, .val = 0 },
	{ .addr = WD_CFG,
	  .mask = WATCHDOG_TRIGGER_AFP_EN_BIT | WDOG_TIMER_EN_ON_PLUGIN_BIT |
		  BARK_WDOG_INT_EN_BIT,
	  .val = 0 },
	/* These bits aren't documented anywhere */
	{ .addr = USBIN_5V_AICL_THRESHOLD_CFG,
	  .mask = USBIN_5V_AICL_THRESHOLD_CFG_MASK,
	  .val = 0x3 },
	{ .addr = USBIN_CONT_AICL_THRESHOLD_CFG,
	  .mask = USBIN_CONT_AICL_THRESHOLD_CFG_MASK,
	  .val = 0x3 },
	/*
	 * Enable Automatic Input Current Limit, this will slowly ramp up the current
	 * When connected to a wall charger, and automatically stop when it detects
	 * the charger current limit (voltage drop?) or it reaches the programmed limit.
	 */
	{ .addr = USBIN_AICL_OPTIONS_CFG,
	  .mask = USBIN_AICL_START_AT_MAX_BIT | USBIN_AICL_ADC_EN_BIT |
		  USBIN_AICL_EN_BIT | SUSPEND_ON_COLLAPSE_USBIN_BIT |
		  USBIN_HV_COLLAPSE_RESPONSE_BIT |
		  USBIN_LV_COLLAPSE_RESPONSE_BIT,
	  .val = USBIN_HV_COLLAPSE_RESPONSE_BIT |
		 USBIN_LV_COLLAPSE_RESPONSE_BIT | USBIN_AICL_EN_BIT },
	/*
	 * Set pre charge current to default, the OnePlus 6 bootloader
	 * sets this very conservatively.
	 */
	{ .addr = PRE_CHARGE_CURRENT_CFG,
	  .mask = PRE_CHARGE_CURRENT_SETTING_MASK,
	  .val = 500000 / CURRENT_SCALE_FACTOR },
	/*
	 * This overrides all of the current limit options exposed to userspace
	 * and prevents the device from pulling more than ~1A. This is done
	 * to minimise potential fire hazard risks.
	 */
	{ .addr = FAST_CHARGE_CURRENT_CFG,
	  .mask = FAST_CHARGE_CURRENT_SETTING_MASK,
	  .val = 1000000 / CURRENT_SCALE_FACTOR },
};

static int smb2_init_hw(struct smb2_chip *chip)
{
	u8 lobuf[2];
	int rc, i, iterm, raw, term_ma;
	s32 term_s32;

	for (i = 0; i < ARRAY_SIZE(smb2_init_seq); i++) {
		/* On SMB5 the Type-C/OTG blocks and the MISC status-pin config
		 * are owned by other drivers or have a different layout. */
		if (chip->is_smb5 &&
		    (smb2_init_seq[i].addr == TYPE_C_INTRPT_ENB_SOFTWARE_CTRL ||
		     smb2_init_seq[i].addr == TYPE_C_CFG ||
		     smb2_init_seq[i].addr == OTG_CFG ||
		     smb2_init_seq[i].addr == STAT_CFG ||
		     /* SMB5 0x7D is the recharge-SOC threshold, written later. */
		     smb2_init_seq[i].addr == FG_UPDATE_CFG_2_SEL))
			continue;

		dev_dbg(chip->dev, "%d: Writing 0x%02x to 0x%02x\n", i,
			smb2_init_seq[i].val, smb2_init_seq[i].addr);
		rc = regmap_update_bits(chip->regmap,
					chip->base + smb2_init_seq[i].addr,
					smb2_init_seq[i].mask,
					smb2_init_seq[i].val);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "%s: init command %d failed\n",
					     __func__, i);
	}

	/* Pre-charge and fast-charge current depend on the PMIC step size,
	 * so program them from the per-PMIC scale. */
	rc = regmap_update_bits(chip->regmap,
				chip->base + PRE_CHARGE_CURRENT_CFG,
				PRE_CHARGE_CURRENT_SETTING_MASK,
				500000 / chip->curr_scale);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc,
				     "failed to set pre-charge current\n");

	rc = regmap_update_bits(chip->regmap,
				chip->base + FAST_CHARGE_CURRENT_CFG,
				FAST_CHARGE_CURRENT_SETTING_MASK,
				chip->fcc_ua / chip->curr_scale);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc,
				     "failed to set fast-charge current\n");

	if (chip->is_smb5) {
		/* SMB5-specific: resume USBIN and select the USBIN charge path.
		 * Without these the charger reports "charging" but the input
		 * stays suspended and no current flows into the battery. */
		rc = regmap_update_bits(chip->regmap, chip->base + USBIN_CMD_IL,
					USBIN_SUSPEND_BIT, 0);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "failed to resume USBIN\n");

		rc = regmap_update_bits(chip->regmap,
					chip->base + USBIN_ICL_OPTIONS,
					USBIN_MODE_CHG_BIT, USBIN_MODE_CHG_BIT);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "failed to select USBIN charge path\n");

		/* SMB5 defaults to voltage-based charge inhibit; disable it so
		 * the battery actually accepts current. */
		rc = regmap_update_bits(chip->regmap, chip->base + CHGR_CFG2,
					CHARGER_INHIBIT_BIT, 0);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "failed to clear charge inhibit\n");

		/*
		 * Enable BC1.2 source detection (AUTO_SRC_DETECT, a.k.a.
		 * BC1P2_SRC_DETECT) and HVDCP (QC2/QC3). Without the source
		 * detection bit APSD never runs, so the DTC status never
		 * completes and the charger type stays unclassified; the PMIC
		 * then keeps the input at the low default and the adapter is
		 * never asked for high voltage. The downstream driver enables
		 * this in smblib_apsd_enable().
		 */
		rc = regmap_update_bits(chip->regmap,
					chip->base + USBIN_OPTIONS_1_CFG,
					AUTO_SRC_DETECT_BIT |
					HVDCP_AUTH_ALG_EN_CFG_BIT |
					HVDCP_EN_BIT |
					HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT,
					AUTO_SRC_DETECT_BIT);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "failed to enable BC1.2\n");

		/*
		 * Stay at 5V.  High voltage belongs to the LN8000 charge pump
		 * and to its own regulation loop; with only the SMB5 running,
		 * a QC adapter that has been asked for 9V leaves the port at
		 * ~10V with the input limit collapsed to 100mA and the battery
		 * discharging, while 5V charges normally.
		 */
		rc = regmap_write(chip->regmap,
				  chip->base + USBIN_ADAPTER_ALLOW_CFG,
				  USBIN_ADAPTER_ALLOW_5V);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "failed to set adapter allowance\n");

		/* And drop an adapter that latched a higher voltage earlier. */
		rc = regmap_write(chip->regmap, chip->base + CMD_HVDCP_2,
				  HVDCP_FORCE_5V_BIT);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "failed to force 5V\n");

		/*
		 * Nabu: while idle the Type-C block parks in the sink phase, so a
		 * plugged sink (stick, headset) is only noticed when it next tries
		 * the source phase.  Ask for the shorter dual-role DFP window so
		 * that probe comes around sooner.
		 */
		rc = regmap_update_bits(chip->regmap,
					chip->base + TAPER_TIMER_SEL_CFG,
					TYPEC_DRP_DFP_TIME_CFG_BIT,
					TYPEC_DRP_DFP_TIME_CFG_BIT);
		if (rc)
			dev_warn(chip->dev, "failed to set DRP DFP time: %d\n",
				 rc);

		/* Re-run APSD so an adapter that was already attached before
		 * HVDCP was enabled is re-classified and the QC handshake is
		 * performed at boot. */
		rc = regmap_update_bits(chip->regmap, chip->base + CMD_APSD,
					APSD_RERUN_BIT, APSD_RERUN_BIT);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "failed to rerun APSD\n");

		/*
		 * ADC-based charge termination.  The threshold has to sit clearly
		 * above the current the pack keeps drawing once it is full -
		 * with a hardcoded 400 mA the pack floated at 400-410 mA, i.e.
		 * just above the limit, so termination never fired and the
		 * charger trickled for ever.  Use the DT value
		 * (qcom,fg-sys-term-current, -500 here).
		 */
		term_ma = 500;
		if (!of_property_read_u32(chip->dev->of_node,
					  "qcom,fg-sys-term-current", &term_s32))
			term_ma = abs((int)term_s32);
		if (term_ma < 100 || term_ma > 2000)
			term_ma = 500;
		iterm = (-term_ma * 32767) / 10000;
		rc = regmap_write(chip->regmap,
				  chip->base + CHGR_ADC_ITERM_UP_THD_MSB,
				  (iterm >> 8) & 0xff);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "failed to set ITERM MSB\n");
		rc = regmap_write(chip->regmap,
				  chip->base + CHGR_ADC_ITERM_UP_THD_LSB,
				  iterm & 0xff);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "failed to set ITERM LSB\n");
		rc = regmap_update_bits(chip->regmap,
					chip->base + CHGR_ADC_TERM_CFG,
					BIT(0), TERM_BASED_ON_SAMPLE_CNT);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "failed to enable ADC termination\n");

		/*
		 * The PM8150B has both an analog and an ADC comparator for
		 * charge termination. The analog path is not wired up on nabu,
		 * so select the ADC comparator explicitly (the vendor driver
		 * requires CHGR_ITERM_USE_ANALOG_BIT == 0) and program its low
		 * threshold to 50mA like the vendor does. Without this the
		 * charger never terminates and trickles at the CV voltage.
		 */
		raw = (50 * 32767) / 10000;
		lobuf[0] = (raw >> 8) & 0xff;
		lobuf[1] = raw & 0xff;
		rc = regmap_bulk_write(chip->regmap,
				       chip->base + CHGR_ADC_ITERM_LO_THD_MSB,
				       lobuf, 2);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "failed to set ITERM LO\n");

		rc = regmap_update_bits(chip->regmap,
					chip->base + CHGR_ENG_CHARGING_CFG,
					CHGR_ITERM_USE_ANALOG_BIT, 0);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "failed to select ADC termination\n");
		/* Recharge automatically once the battery drops to 99%. */
		rc = regmap_write(chip->regmap,
				  chip->base + CHARGE_RCHG_SOC_THRESHOLD, 99);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "failed to set recharge threshold\n");

		/*
		 * Keep only the hardware hard limit. The hardware soft-limit
		 * thresholds are raw thermistor ADC codes that are not
		 * programmed on nabu (the battery profile only carries the
		 * downstream software JEITA policy), so leaving them enabled
		 * latches a bogus over-temperature state and throttles
		 * charging. Temperature protection is applied in software from
		 * the fuel gauge in smb2_jeita_work().
		 */
		rc = regmap_update_bits(chip->regmap, chip->base + JEITA_EN_CFG,
					JEITA_EN_HARDLIMIT_BIT |
					JEITA_EN_HOT_SL_FCV_BIT |
					JEITA_EN_COLD_SL_FCV_BIT |
					JEITA_EN_HOT_SL_CCC_BIT |
					JEITA_EN_COLD_SL_CCC_BIT,
					JEITA_EN_HARDLIMIT_BIT);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "failed to configure JEITA\n");

		/* Conservative 1.5A default until APSD classifies the
		 * charger; AICL will back off if the source is weaker. */
		rc = regmap_write(chip->regmap,
				  chip->base + USBIN_CURRENT_LIMIT_CFG,
				  1500000 / chip->curr_scale);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "failed to set default ICL\n");
	}

	return 0;
}

static int smb2_init_irq(struct smb2_chip *chip, int *irq, const char *name,
			 irqreturn_t (*handler)(int irq, void *data))
{
	int irqnum;
	int rc;

	irqnum = platform_get_irq_byname(to_platform_device(chip->dev), name);
	if (irqnum < 0)
		return irqnum;

	rc = devm_request_threaded_irq(chip->dev, irqnum, NULL, handler,
				       IRQF_ONESHOT, name, chip);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't request irq %s\n",
				     name);

	if (irq)
		*irq = irqnum;

	return 0;
}

/*
 * Nabu: SMB5 OTG boost, i.e. 5V out on the Type-C port so it can act as a USB
 * host (sticks, headsets, keyboards).  Only the DCDC bits are touched:
 *
 *   0x153 OTG_EN_SRC_CFG = BIT(1)  - OTG enable follows the command bit
 *   0x140 OTG_EN         = BIT(0)  - boost on/off
 *
 * The current limit keeps its reset value.  Offsets are relative to CHGR_BASE,
 * so 0x1140 / 0x1153 on PM8150B.
 */


static int smb2_otg_enable(struct regulator_dev *rdev)
{
	struct smb2_chip *chip = rdev_get_drvdata(rdev);
	int rc;

	/*
	 * OTG_EN_SRC = 0: the boost follows the DCDC_CMD bit below, i.e. it is
	 * under our control.  Leaving it at 1 hands the boost to the Type-C
	 * block, whose CC state machine drops back to "no attach" now and then
	 * even with a device plugged in - VBUS then disappears under the
	 * device and it disconnects a second after enumerating.  The vendor
	 * driver also configures VBUS "for software control" (writes 0 here).
	 */
	rc = regmap_update_bits(chip->regmap, chip->base + SMB5_DCDC_OTG_CFG,
				SMB5_OTG_EN_SRC_CMD, 0);
	if (rc)
		return rc;

	return regmap_update_bits(chip->regmap, chip->base + SMB5_DCDC_CMD_OTG,
				  SMB5_OTG_EN, SMB5_OTG_EN);
}

static int smb2_otg_disable(struct regulator_dev *rdev)
{
	struct smb2_chip *chip = rdev_get_drvdata(rdev);

	return regmap_update_bits(chip->regmap, chip->base + SMB5_DCDC_CMD_OTG,
				  SMB5_OTG_EN, 0);
}

static int smb2_otg_is_enabled(struct regulator_dev *rdev)
{
	struct smb2_chip *chip = rdev_get_drvdata(rdev);
	unsigned int val;
	int rc;

	rc = regmap_read(chip->regmap, chip->base + SMB5_DCDC_CMD_OTG, &val);
	if (rc)
		return rc;

	return !!(val & SMB5_OTG_EN);
}

static const struct regulator_ops smb2_otg_ops = {
	.enable     = smb2_otg_enable,
	.disable    = smb2_otg_disable,
	.is_enabled = smb2_otg_is_enabled,
};

/*
 * Without a valid_ops_mask the regulator core refuses regulator_enable() with
 * -EPERM: this supply has no device tree constraints of its own, so "may change
 * status" has to be granted here or the OTG 5V boost can never be turned on.
 */
static const struct regulator_init_data smb2_otg_init_data = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

static const struct regulator_desc smb2_otg_desc = {
	.name		= "usb-otg-vbus",
	.type		= REGULATOR_VOLTAGE,
	.owner		= THIS_MODULE,
	.ops		= &smb2_otg_ops,
	.fixed_uV	= 5000000,
	.n_voltages	= 1,
};

static int smb2_probe(struct platform_device *pdev)
{
	struct power_supply_config supply_config = {};
	struct power_supply_desc *desc;
	const struct smb2_match_data *data;
	struct smb2_chip *chip;
	int rc, irq, fv_uv;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	data = device_get_match_data(&pdev->dev);
	if (!data)
		return dev_err_probe(&pdev->dev, -EINVAL, "missing match data\n");

	chip->dev = &pdev->dev;
	chip->name = data->name;
	chip->curr_scale = data->curr_scale;
	chip->curr_max_ua = data->curr_max_ua;
	chip->fcc_ua = data->fcc_ua;
	chip->fv_base_uv = data->fv_base_uv;
	chip->fv_step_uv = data->fv_step_uv;
	chip->fv_max_uv = data->fv_max_uv;
	chip->icl_status_reg = data->icl_status_reg;
	chip->power_path_reg = data->power_path_reg;
	chip->bat_ov_bit = data->bat_ov_bit;
	chip->is_smb5 = data->is_smb5;

	chip->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->regmap)
		return dev_err_probe(chip->dev, -ENODEV,
				     "failed to locate the regmap\n");

	rc = device_property_read_u32(chip->dev, "reg", &chip->base);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc,
				     "Couldn't read base address\n");

	chip->usb_in_v_chan = devm_iio_channel_get(chip->dev, "usbin_v");
	if (IS_ERR(chip->usb_in_v_chan)) {
		if (PTR_ERR(chip->usb_in_v_chan) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		chip->usb_in_v_chan = NULL;
	}

	chip->dpdm_reg = devm_regulator_get_optional(chip->dev, "dpdm");
	if (IS_ERR(chip->dpdm_reg)) {
		if (PTR_ERR(chip->dpdm_reg) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		chip->dpdm_reg = NULL;
	}

	rc = smb2_init_hw(chip);
	if (rc < 0)
		return rc;

	supply_config.drv_data = chip;
	supply_config.of_node = pdev->dev.of_node;

	desc = devm_kzalloc(chip->dev, sizeof(smb2_psy_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	memcpy(desc, &smb2_psy_desc, sizeof(smb2_psy_desc));
	desc->name =
		devm_kasprintf(chip->dev, GFP_KERNEL, "%s-charger", chip->name);
	if (!desc->name)
		return -ENOMEM;

	chip->chg_psy =
		devm_power_supply_register(chip->dev, desc, &supply_config);
	if (IS_ERR(chip->chg_psy))
		return dev_err_probe(chip->dev, PTR_ERR(chip->chg_psy),
				     "failed to register power supply\n");

	chip->usb_extcon = devm_extcon_dev_allocate(chip->dev,
						    smb2_usb_extcon_cables);
	if (IS_ERR(chip->usb_extcon))
		return dev_err_probe(chip->dev, PTR_ERR(chip->usb_extcon),
				     "failed to allocate USB extcon\n");
	rc = devm_extcon_dev_register(chip->dev, chip->usb_extcon);
	if (rc)
		return dev_err_probe(chip->dev, rc,
				     "failed to register USB extcon\n");

	rc = power_supply_get_battery_info(chip->chg_psy, &chip->batt_info);
	if (rc)
		return dev_err_probe(chip->dev, rc,
				     "Failed to get battery info\n");

	rc = devm_delayed_work_autocancel(chip->dev, &chip->status_change_work,
					  smb2_status_change_work);
	if (rc)
		return dev_err_probe(chip->dev, rc,
				     "Failed to init status change work\n");

	rc = devm_delayed_work_autocancel(chip->dev, &chip->jeita_work,
					  smb2_jeita_work);
	if (rc)
		return dev_err_probe(chip->dev, rc,
				     "Failed to init JEITA work\n");

	fv_uv = chip->batt_info->voltage_max_design_uv;
	if (chip->fv_max_uv && fv_uv > chip->fv_max_uv)
		fv_uv = chip->fv_max_uv;
	chip->fv_uv = fv_uv;
	rc = (fv_uv - chip->fv_base_uv) / chip->fv_step_uv;
	rc = regmap_update_bits(chip->regmap, chip->base + FLOAT_VOLTAGE_CFG,
				FLOAT_VOLTAGE_SETTING_MASK, rc);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't set vbat max\n");

	rc = smb2_init_irq(chip, &irq, "bat-ov", smb2_handle_batt_overvoltage);
	if (rc < 0)
		return rc;

	rc = smb2_init_irq(chip, &chip->cable_irq, "usb-plugin",
			   smb2_handle_usb_plugin);
	if (rc < 0)
		return rc;

	rc = smb2_init_irq(chip, &irq, "usbin-icl-change",
			   smb2_handle_usb_icl_change);
	if (rc < 0)
		return rc;
	rc = smb2_init_irq(chip, &irq, "wdog-bark", smb2_handle_wdog_bark);
	if (rc < 0)
		return rc;

	if (data->is_smb5) {
		struct regulator_config cfg = {
			.dev			= chip->dev,
			.driver_data		= chip,
			.of_node		= chip->dev->of_node,
			.init_data		= &smb2_otg_init_data,
		};

		rc = regmap_write(chip->regmap,
				  chip->base + SMB5_DCDC_OTG_ILIM,
				  SMB5_OTG_ILIM_2A);
		if (rc)
			dev_warn(chip->dev,
				 "failed to set OTG current limit: %d\n", rc);

		chip->otg_rdev = devm_regulator_register(chip->dev,
							 &smb2_otg_desc, &cfg);
		if (IS_ERR(chip->otg_rdev))
			return dev_err_probe(chip->dev,
					     PTR_ERR(chip->otg_rdev),
					     "failed to register OTG regulator\n");

		/*
		 * "nabu,otg-force-on" keeps the 5V boost up from probe, which is
		 * the only way to test the host path while the TCPM on this port
		 * cannot report a CC attach (see nabu-power's dtsi).
		 */
		if (of_property_read_bool(chip->dev->of_node, "nabu,otg-force-on")) {
			struct regulator *vbus;

			vbus = devm_regulator_get(chip->dev, "usb-otg-vbus");
			if (IS_ERR(vbus)) {
				dev_warn(chip->dev, "no usb-otg-vbus handle: %ld\n",
					 PTR_ERR(vbus));
			} else {
				rc = regulator_enable(vbus);
				if (rc)
					dev_warn(chip->dev,
						 "OTG force enable failed: %d\n", rc);
				else
					dev_info(chip->dev, "OTG boost forced on\n");
			}
		}
	}

	rc = dev_pm_set_wake_irq(chip->dev, chip->cable_irq);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't set wake irq\n");

	platform_set_drvdata(pdev, chip);

	if (chip->is_smb5) {
		/* obtained on the first typec poll: dwc3 registers its role
		 * switch after this driver probes */
		chip->role_sw = NULL;

		chip->otg_vbus = devm_regulator_get_optional(chip->dev,
							    "usb-otg-vbus");
		if (IS_ERR(chip->otg_vbus))
			chip->otg_vbus = NULL;

		chip->typec_role = USB_ROLE_NONE;
		chip->typec_force = USB_ROLE_NONE;
		chip->typec_mode = -1;	/* force the first poll to program DRP */
		chip->typec_dbg_left = 20;
		if (device_create_file(chip->dev, &dev_attr_typec_regs))
			dev_warn(chip->dev, "no typec_regs attribute\n");
		if (device_create_file(chip->dev, &dev_attr_typec_role))
			dev_warn(chip->dev, "no typec_role attribute\n");
		smb2_typec_init(chip);
		INIT_DELAYED_WORK(&chip->typec_work, smb2_typec_poll);
		/* Automatic role detection is a 2 s poll.  The old "no polling"
		 * default existed only because the Type-C registers were read at
		 * the wrong address; with that fixed the poll is what makes the
		 * port configure itself. */
		{
			unsigned int misc = 0;

			regmap_read(chip->regmap, SMB5_TYPE_C_MISC_STATUS,
				    &misc);
			dev_info(chip->dev,
				 "typec init: misc=%#x, automatic role polling on\n",
				 misc);
		}
		/* start the automatic role poll */
		schedule_delayed_work(&chip->typec_work, 0);

		/* and react to attach/CC interrupts without waiting for it */
		for (unsigned int tci = 0; tci < 2; tci++) {
			static const char * const names[] = {
				"typec-attach", "typec-cc"
			};
			int irq = platform_get_irq_byname_optional(pdev,
								   names[tci]);

			if (irq > 0)
				devm_request_threaded_irq(chip->dev, irq, NULL,
					smb2_handle_typec_irq,
					IRQF_ONESHOT, names[tci], chip);
		}
	}

	/* Initialise charger state */
	{
		int online = 0;

		if (!smb2_get_prop_usb_online(chip, &online))
			smb2_sync_usb_extcon(chip, online);
	}
	schedule_delayed_work(&chip->status_change_work, 0);

	return 0;
}

static const struct smb2_match_data smb2_pmi8998_data = {
	.name = "pmi8998",
	.curr_scale = 25000,
	.curr_max_ua = 4800000,
	.fcc_ua = 1000000,
	.fv_base_uv = 3480000,
	.fv_step_uv = 7500,
	.icl_status_reg = ICL_STATUS,
	.power_path_reg = POWER_PATH_STATUS,
	.bat_ov_bit = 0x20,
};

static const struct smb2_match_data smb2_pm660_data = {
	.name = "pm660",
	.curr_scale = 25000,
	.curr_max_ua = 4800000,
	.fcc_ua = 1000000,
	.fv_base_uv = 3480000,
	.fv_step_uv = 7500,
	.icl_status_reg = ICL_STATUS,
	.power_path_reg = POWER_PATH_STATUS,
	.bat_ov_bit = 0x20,
};

static const struct smb2_match_data smb2_pm8150b_data = {
	.name = "pm8150b",
	.curr_scale = 50000,
	.curr_max_ua = 5000000,
	.fcc_ua = 3000000,
	.fv_base_uv = 3600000,
	.fv_step_uv = 10000,
	.fv_max_uv = 4470000,
	/* SMB5 puts the DCDC status registers at DCDC_BASE (0x1100). */
	.icl_status_reg = 0x107,
	.power_path_reg = 0x10B,
	.bat_ov_bit = 0x02,
	.is_smb5 = true,
};

static const struct of_device_id smb2_match_id_table[] = {
	{ .compatible = "qcom,pmi8998-charger", .data = &smb2_pmi8998_data },
	{ .compatible = "qcom,pm660-charger", .data = &smb2_pm660_data },
	{ .compatible = "qcom,pm8150b-charger", .data = &smb2_pm8150b_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, smb2_match_id_table);

static struct platform_driver qcom_spmi_smb2 = {
	.probe = smb2_probe,
	.driver = {
		.name = "qcom-pmi8998/pm660-charger",
		.of_match_table = smb2_match_id_table,
		},
};

module_platform_driver(qcom_spmi_smb2);

MODULE_AUTHOR("Caleb Connolly <caleb.connolly@linaro.org>");
MODULE_DESCRIPTION("Qualcomm SMB2 Charger Driver");
MODULE_LICENSE("GPL");
