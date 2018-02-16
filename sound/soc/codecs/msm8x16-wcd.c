/* Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/spmi.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/wcd9xxx/core.h>
#include <linux/qdsp6v2/apr.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <sound/q6afe-v2.h>
#include <linux/switch.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <sound/q6core.h>
#include <soc/qcom/subsystem_notif.h>
#include "msm8x16-wcd.h"
#include "wcd-mbhc-v2.h"
#include "msm8916-wcd-irq.h"
#include "msm8x16_wcd_registers.h"

#define MSM8X16_WCD_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
			SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000)
#define MSM8X16_WCD_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
		SNDRV_PCM_FMTBIT_S24_LE |\
		SNDRV_PCM_FMTBIT_S24_3LE)

#define NUM_INTERPOLATORS	3
#define BITS_PER_REG		8
#define MSM8X16_WCD_TX_PORT_NUMBER	4

#define MSM8X16_WCD_I2S_MASTER_MODE_MASK	0x08
#define MSM8X16_DIGITAL_CODEC_BASE_ADDR		0x771C000
#define TOMBAK_CORE_0_SPMI_ADDR			0xf000
#define TOMBAK_CORE_1_SPMI_ADDR			0xf100
#define PMIC_SLAVE_ID_0		0
#define PMIC_SLAVE_ID_1		1

#define PMIC_MBG_OK		0x2C08
#define PMIC_LDO7_EN_CTL	0x4646
#define MASK_MSB_BIT		0x80

#define CODEC_DT_MAX_PROP_SIZE			40
#define MSM8X16_DIGITAL_CODEC_REG_SIZE		0x400
#define MAX_ON_DEMAND_SUPPLY_NAME_LENGTH	64

#define MCLK_RATE_9P6MHZ	9600000
#define MCLK_RATE_12P288MHZ	12288000

#define BUS_DOWN 1

/*
 *50 Milliseconds sufficient for DSP bring up in the modem
 * after Sub System Restart
 */
#define ADSP_STATE_READY_TIMEOUT_MS 50

#define HPHL_PA_DISABLE (0x01 << 1)
#define HPHR_PA_DISABLE (0x01 << 2)
#define EAR_PA_DISABLE (0x01 << 3)
#define SPKR_PA_DISABLE (0x01 << 4)

enum {
	BOOST_SWITCH = 0,
	BOOST_ALWAYS,
	BYPASS_ALWAYS,
	BOOST_ON_FOREVER,
};

#define EAR_PMD 0
#define EAR_PMU 1
#define SPK_PMD 2
#define SPK_PMU 3

#define MICBIAS_DEFAULT_VAL 2700000
#define MICBIAS_MIN_VAL 1600000
#define MICBIAS_STEP_SIZE 50000

#define DEFAULT_BOOST_VOLTAGE 5000
#define MIN_BOOST_VOLTAGE 4000
#define MAX_BOOST_VOLTAGE 5550
#define BOOST_VOLTAGE_STEP 50

#define MSM8X16_WCD_MBHC_BTN_COARSE_ADJ  100 /* in mV */
#define MSM8X16_WCD_MBHC_BTN_FINE_ADJ 12 /* in mV */

#define VOLTAGE_CONVERTER(value, min_value, step_size)\
	((value - min_value)/step_size)

enum {
	AIF1_PB = 0,
	AIF1_CAP,
	AIF2_VIFEED,
	NUM_CODEC_DAIS,
};

enum {
	RX_MIX1_INP_SEL_ZERO = 0,
	RX_MIX1_INP_SEL_IIR1,
	RX_MIX1_INP_SEL_IIR2,
	RX_MIX1_INP_SEL_RX1,
	RX_MIX1_INP_SEL_RX2,
	RX_MIX1_INP_SEL_RX3,
};

static const DECLARE_TLV_DB_SCALE(digital_gain, 0, 1, 0);
static const DECLARE_TLV_DB_SCALE(analog_gain, 0, 25, 1);
static struct snd_soc_dai_driver msm8x16_wcd_i2s_dai[];

static struct switch_dev accdet_data;
static int accdet_state;
static bool spkr_boost_en = true;

#define MSM8X16_WCD_ACQUIRE_LOCK(x) \
	mutex_lock_nested(&x, SINGLE_DEPTH_NESTING)

#define MSM8X16_WCD_RELEASE_LOCK(x) mutex_unlock(&x)


/* Codec supports 2 IIR filters */
enum {
	IIR1 = 0,
	IIR2,
	IIR_MAX,
};

/* Codec supports 5 bands */
enum {
	BAND1 = 0,
	BAND2,
	BAND3,
	BAND4,
	BAND5,
	BAND_MAX,
};

struct hpf_work {
	struct msm8x16_wcd_priv *msm8x16_wcd;
	u32 decimator;
	u8 tx_hpf_cut_of_freq;
	struct delayed_work dwork;
};

static struct hpf_work tx_hpf_work[NUM_DECIMATORS];

static char on_demand_supply_name[][MAX_ON_DEMAND_SUPPLY_NAME_LENGTH] = {
	"cdc-vdd-mic-bias",
};

static unsigned long rx_digital_gain_reg[] = {
	MSM8X16_WCD_A_CDC_RX1_VOL_CTL_B2_CTL,
	MSM8X16_WCD_A_CDC_RX2_VOL_CTL_B2_CTL,
	MSM8X16_WCD_A_CDC_RX3_VOL_CTL_B2_CTL,
};

static unsigned long tx_digital_gain_reg[] = {
	MSM8X16_WCD_A_CDC_TX1_VOL_CTL_GAIN,
	MSM8X16_WCD_A_CDC_TX2_VOL_CTL_GAIN,
};

enum {
	MSM8X16_WCD_SPMI_DIGITAL = 0,
	MSM8X16_WCD_SPMI_ANALOG,
	MAX_MSM8X16_WCD_DEVICE
};

static struct wcd_mbhc_register
	wcd_mbhc_registers[WCD_MBHC_REG_FUNC_MAX] = {

	WCD_MBHC_REGISTER("WCD_MBHC_L_DET_EN",
			  MSM8X16_WCD_A_ANALOG_MBHC_DET_CTL_1, 0x80, 7, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_GND_DET_EN",
			  MSM8X16_WCD_A_ANALOG_MBHC_DET_CTL_1, 0x40, 6, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_MECH_DETECTION_TYPE",
			  MSM8X16_WCD_A_ANALOG_MBHC_DET_CTL_1, 0x20, 5, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_MIC_CLAMP_CTL",
			  MSM8X16_WCD_A_ANALOG_MBHC_DET_CTL_1, 0x18, 3, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_ELECT_DETECTION_TYPE",
			  MSM8X16_WCD_A_ANALOG_MBHC_DET_CTL_1, 0x01, 0, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_HS_L_DET_PULL_UP_CTRL",
			  MSM8X16_WCD_A_ANALOG_MBHC_DET_CTL_2, 0xC0, 6, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_HS_L_DET_PULL_UP_COMP_CTRL",
			  MSM8X16_WCD_A_ANALOG_MBHC_DET_CTL_2, 0x20, 5, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_HPHL_PLUG_TYPE",
			  MSM8X16_WCD_A_ANALOG_MBHC_DET_CTL_2, 0x10, 4, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_GND_PLUG_TYPE",
			  MSM8X16_WCD_A_ANALOG_MBHC_DET_CTL_2, 0x08, 3, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_SW_HPH_LP_100K_TO_GND",
			  MSM8X16_WCD_A_ANALOG_MBHC_DET_CTL_2, 0x01, 0, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_ELECT_SCHMT_ISRC",
			  MSM8X16_WCD_A_ANALOG_MBHC_DET_CTL_2, 0x06, 1, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_FSM_EN",
			  MSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL, 0x80, 7, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_INSREM_DBNC",
			  MSM8X16_WCD_A_ANALOG_MBHC_DBNC_TIMER, 0xF0, 4, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_BTN_DBNC",
			  MSM8X16_WCD_A_ANALOG_MBHC_DBNC_TIMER, 0x0C, 2, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_HS_VREF",
			  MSM8X16_WCD_A_ANALOG_MBHC_BTN3_CTL, 0x03, 0, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_HS_COMP_RESULT",
			  MSM8X16_WCD_A_ANALOG_MBHC_ZDET_ELECT_RESULT, 0x01,
			  0, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_MIC_SCHMT_RESULT",
			  MSM8X16_WCD_A_ANALOG_MBHC_ZDET_ELECT_RESULT, 0x02,
			  1, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_HPHL_SCHMT_RESULT",
			  MSM8X16_WCD_A_ANALOG_MBHC_ZDET_ELECT_RESULT, 0x08,
			  3, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_HPHR_SCHMT_RESULT",
			  MSM8X16_WCD_A_ANALOG_MBHC_ZDET_ELECT_RESULT, 0x04,
			  2, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_OCP_FSM_EN",
			  MSM8X16_WCD_A_ANALOG_RX_COM_OCP_CTL, 0x10, 4, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_BTN_RESULT",
			  MSM8X16_WCD_A_ANALOG_MBHC_BTN_RESULT, 0xFF, 0, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_BTN_ISRC_CTL",
			  MSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL, 0x70, 4, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_ELECT_RESULT",
			  MSM8X16_WCD_A_ANALOG_MBHC_ZDET_ELECT_RESULT, 0xFF,
			  0, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_MICB_CTRL",
			  MSM8X16_WCD_A_ANALOG_MICB_2_EN, 0xC0, 6, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_HPH_CNP_WG_TIME",
			  MSM8X16_WCD_A_ANALOG_RX_HPH_CNP_WG_TIME, 0xFC, 2, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_HPHR_PA_EN",
			  MSM8X16_WCD_A_ANALOG_RX_HPH_CNP_EN, 0x10, 4, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_HPHL_PA_EN",
			  MSM8X16_WCD_A_ANALOG_RX_HPH_CNP_EN, 0x20, 5, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_HPH_PA_EN",
			  MSM8X16_WCD_A_ANALOG_RX_HPH_CNP_EN, 0x30, 4, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_SWCH_LEVEL_REMOVE",
			  MSM8X16_WCD_A_ANALOG_MBHC_ZDET_ELECT_RESULT,
			  0x10, 4, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_MOISTURE_VREF",
			  0, 0, 0, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_PULLDOWN_CTRL",
			  MSM8X16_WCD_A_ANALOG_MICB_2_EN, 0x20, 5, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_ANC_DET_EN",
			  0, 0, 0, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_FSM_STATUS",
			  0, 0, 0, 0),
	WCD_MBHC_REGISTER("WCD_MBHC_MUX_CTL",
			  0, 0, 0, 0),
};

struct msm8x16_wcd_spmi {
	struct spmi_device *spmi;
	int base;
};

/* Multiply gain_adj and offset by 1000 and 100 to avoid float arithmetic */
static const struct wcd_imped_i_ref imped_i_ref[] = {
	{I_h4_UA, 8, 800, 9000, 10000},
	{I_pt5_UA, 10, 100, 990, 4600},
	{I_14_UA, 17, 14, 1050, 700},
	{I_l4_UA, 10, 4, 1165, 110},
	{I_1_UA, 0, 1, 1200, 65},
};

static const struct wcd_mbhc_intr intr_ids = {
	.mbhc_sw_intr =  MSM8X16_WCD_IRQ_MBHC_HS_DET,
	.mbhc_btn_press_intr = MSM8X16_WCD_IRQ_MBHC_PRESS,
	.mbhc_btn_release_intr = MSM8X16_WCD_IRQ_MBHC_RELEASE,
	.mbhc_hs_ins_intr = MSM8X16_WCD_IRQ_MBHC_INSREM_DET1,
	.mbhc_hs_rem_intr = MSM8X16_WCD_IRQ_MBHC_INSREM_DET,
	.hph_left_ocp = MSM8X16_WCD_IRQ_HPHL_OCP,
	.hph_right_ocp = MSM8X16_WCD_IRQ_HPHR_OCP,
};

static int msm8x16_wcd_dt_parse_vreg_info(struct device *dev,
	struct msm8x16_wcd_regulator *vreg,
	const char *vreg_name, bool ondemand);
static struct msm8x16_wcd_pdata *msm8x16_wcd_populate_dt_pdata(
	struct device *dev);
static int msm8x16_wcd_enable_ext_mb_source(struct snd_soc_codec *codec,
					    bool turn_on);
static void msm8x16_trim_btn_reg(struct snd_soc_codec *codec);
static void msm8x16_wcd_set_micb_v(struct snd_soc_codec *codec);
static void msm8x16_wcd_set_boost_v(struct snd_soc_codec *codec);
static void msm8x16_wcd_set_auto_zeroing(struct snd_soc_codec *codec,
		bool enable);
static void msm8x16_wcd_configure_cap(struct snd_soc_codec *codec,
		bool micbias1, bool micbias2);
static bool msm8x16_wcd_use_mb(struct snd_soc_codec *codec);

struct msm8x16_wcd_spmi msm8x16_wcd_modules[MAX_MSM8X16_WCD_DEVICE];

static void *adsp_state_notifier;

static struct snd_soc_codec *registered_codec;

static int get_codec_version(struct msm8x16_wcd_priv *msm8x16_wcd)
{
	if (msm8x16_wcd->codec_version == DIANGU)
		return DIANGU;
	else if (msm8x16_wcd->codec_version == CAJON_2_0)
		return CAJON_2_0;
	else if (msm8x16_wcd->codec_version == CAJON)
		return CAJON;
	else if (msm8x16_wcd->codec_version == CONGA)
		return CONGA;
	else if (msm8x16_wcd->pmic_rev == TOMBAK_2_0)
		return TOMBAK_2_0;
	else if (msm8x16_wcd->pmic_rev == TOMBAK_1_0)
		return TOMBAK_1_0;

	pr_err("%s: unsupported codec version\n", __func__);
	return UNSUPPORTED;
}

static void wcd_mbhc_meas_imped(struct snd_soc_codec *codec,
				s16 *impedance_l, s16 *impedance_r)
{
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	if ((msm8x16_wcd->imped_det_pin == WCD_MBHC_DET_BOTH) ||
		(msm8x16_wcd->imped_det_pin == WCD_MBHC_DET_HPHL)) {
		/* Enable ZDET_L_MEAS_EN */
		snd_soc_update_bits(codec,
				MSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL,
				0x08, 0x08);
		/* Wait for 2ms for measurement to complete */
		usleep_range(2000, 2100);
		/* Read Left impedance value from Result1 */
		*impedance_l = snd_soc_read(codec,
				MSM8X16_WCD_A_ANALOG_MBHC_BTN_RESULT);
		/* Enable ZDET_R_MEAS_EN */
		snd_soc_update_bits(codec,
				MSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL,
				0x08, 0x00);
	}
	if ((msm8x16_wcd->imped_det_pin == WCD_MBHC_DET_BOTH) ||
		(msm8x16_wcd->imped_det_pin == WCD_MBHC_DET_HPHR)) {
			snd_soc_update_bits(codec,
				MSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL,
				0x04, 0x04);
		/* Wait for 2ms for measurement to complete */
		usleep_range(2000, 2100);
		/* Read Right impedance value from Result1 */
		*impedance_r = snd_soc_read(codec,
				MSM8X16_WCD_A_ANALOG_MBHC_BTN_RESULT);
		snd_soc_update_bits(codec,
				MSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL,
				0x04, 0x00);
	}
}

static void msm8x16_set_ref_current(struct snd_soc_codec *codec,
				enum wcd_curr_ref curr_ref)
{
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	pr_debug("%s: curr_ref: %d\n", __func__, curr_ref);

	if (get_codec_version(msm8x16_wcd) < CAJON)
		pr_debug("%s: Setting ref current not required\n", __func__);

	msm8x16_wcd->imped_i_ref = imped_i_ref[curr_ref];

	switch (curr_ref) {
	case I_h4_UA:
		snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MICB_2_EN,
			0x07, 0x01);
		break;
	case I_pt5_UA:
		snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MICB_2_EN,
			0x07, 0x04);
		break;
	case I_14_UA:
		snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MICB_2_EN,
			0x07, 0x03);
		break;
	case I_l4_UA:
		snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MICB_2_EN,
			0x07, 0x01);
		break;
	case I_1_UA:
		snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MICB_2_EN,
			0x07, 0x00);
		break;
	default:
		pr_debug("%s: No ref current set\n", __func__);
		break;
	}
}

static bool msm8x16_adj_ref_current(struct snd_soc_codec *codec,
					s16 *impedance_l, s16 *impedance_r)
{
	int i = 2;
	s16 compare_imp = 0;

	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	if (msm8x16_wcd->imped_det_pin == WCD_MBHC_DET_HPHR)
		compare_imp = *impedance_r;
	else
		compare_imp = *impedance_l;

	if (get_codec_version(msm8x16_wcd) < CAJON) {
		pr_debug("%s: Reference current adjustment not required\n",
			 __func__);
		return false;
	}

	while (compare_imp < imped_i_ref[i].min_val) {
		msm8x16_set_ref_current(codec,
					imped_i_ref[++i].curr_ref);
		wcd_mbhc_meas_imped(codec,
				impedance_l, impedance_r);
		compare_imp = (msm8x16_wcd->imped_det_pin == WCD_MBHC_DET_HPHR)
				? *impedance_r : *impedance_l;
		if (i >= I_1_UA)
			break;
	}

	return true;
}

void msm8x16_wcd_spk_ext_pa_cb(
		int (*codec_spk_ext_pa)(struct snd_soc_codec *codec,
			int enable), struct snd_soc_codec *codec)
{
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	pr_debug("%s: Enter\n", __func__);
	msm8x16_wcd->codec_spk_ext_pa_cb = codec_spk_ext_pa;
}

void msm8x16_wcd_hph_comp_cb(
	int (*codec_hph_comp_gpio)(bool enable), struct snd_soc_codec *codec)
{
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	pr_debug("%s: Enter\n", __func__);
	msm8x16_wcd->codec_hph_comp_gpio = codec_hph_comp_gpio;
}

static void msm8x16_wcd_compute_impedance(struct snd_soc_codec *codec, s16 l,
				s16 r, uint32_t *zl, uint32_t *zr, bool high)
{
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	uint32_t rl = 0, rr = 0;
	struct wcd_imped_i_ref R = msm8x16_wcd->imped_i_ref;
	int codec_ver = get_codec_version(msm8x16_wcd);

	switch (codec_ver) {
	case TOMBAK_1_0:
	case TOMBAK_2_0:
	case CONGA:
		if (high) {
			pr_debug("%s: This plug has high range impedance\n",
				 __func__);
			rl = (uint32_t)(((100 * (l * 400 - 200))/96) - 230);
			rr = (uint32_t)(((100 * (r * 400 - 200))/96) - 230);
		} else {
			pr_debug("%s: This plug has low range impedance\n",
				 __func__);
			rl = (uint32_t)(((1000 * (l * 2 - 1))/1165) - (13/10));
			rr = (uint32_t)(((1000 * (r * 2 - 1))/1165) - (13/10));
		}
		break;
	case CAJON:
	case CAJON_2_0:
	case DIANGU:
		if (msm8x16_wcd->imped_det_pin == WCD_MBHC_DET_HPHL) {
			rr = (uint32_t)(((DEFAULT_MULTIPLIER * (10 * r - 5)) -
			   (DEFAULT_OFFSET * DEFAULT_GAIN))/DEFAULT_GAIN);
			rl = (uint32_t)(((10000 * (R.multiplier * (10 * l - 5)))
			      - R.offset * R.gain_adj)/(R.gain_adj * 100));
		} else if (msm8x16_wcd->imped_det_pin == WCD_MBHC_DET_HPHR) {
			rr = (uint32_t)(((10000 * (R.multiplier * (10 * r - 5)))
			      - R.offset * R.gain_adj)/(R.gain_adj * 100));
			rl = (uint32_t)(((DEFAULT_MULTIPLIER * (10 * l - 5))-
			   (DEFAULT_OFFSET * DEFAULT_GAIN))/DEFAULT_GAIN);
		} else if (msm8x16_wcd->imped_det_pin == WCD_MBHC_DET_NONE) {
			rr = (uint32_t)(((DEFAULT_MULTIPLIER * (10 * r - 5)) -
			   (DEFAULT_OFFSET * DEFAULT_GAIN))/DEFAULT_GAIN);
			rl = (uint32_t)(((DEFAULT_MULTIPLIER * (10 * l - 5))-
			   (DEFAULT_OFFSET * DEFAULT_GAIN))/DEFAULT_GAIN);
		} else {
			rr = (uint32_t)(((10000 * (R.multiplier * (10 * r - 5)))
			      - R.offset * R.gain_adj)/(R.gain_adj * 100));
			rl = (uint32_t)(((10000 * (R.multiplier * (10 * l - 5)))
			      - R.offset * R.gain_adj)/(R.gain_adj * 100));
		}
		break;
	default:
		pr_debug("%s: No codec mentioned\n", __func__);
		break;
	}
	*zl = rl;
	*zr = rr;
}

static struct firmware_cal *msm8x16_wcd_get_hwdep_fw_cal(
		struct snd_soc_codec *codec,
		enum wcd_cal_type type)
{
	struct msm8x16_wcd_priv *msm8x16_wcd;
	struct firmware_cal *hwdep_cal;

	if (!codec) {
		pr_err("%s: NULL codec pointer\n", __func__);
		return NULL;
	}
	msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	hwdep_cal = wcdcal_get_fw_cal(msm8x16_wcd->fw_data, type);
	if (!hwdep_cal) {
		dev_err(codec->dev, "%s: cal not sent by %d\n",
				__func__, type);
		return NULL;
	}
	return hwdep_cal;
}

static void wcd9xxx_spmi_irq_control(struct snd_soc_codec *codec,
				     int irq, bool enable)
{
	if (enable)
		wcd9xxx_spmi_enable_irq(irq);
	else
		wcd9xxx_spmi_disable_irq(irq);
}

static void msm8x16_mbhc_clk_setup(struct snd_soc_codec *codec,
				   bool enable)
{
	if (enable)
		snd_soc_update_bits(codec,
				MSM8X16_WCD_A_DIGITAL_CDC_DIG_CLK_CTL,
				0x08, 0x08);
	else
		snd_soc_update_bits(codec,
				MSM8X16_WCD_A_DIGITAL_CDC_DIG_CLK_CTL,
				0x08, 0x00);
}

static int msm8x16_mbhc_map_btn_code_to_num(struct snd_soc_codec *codec)
{
	int btn_code;
	int btn;

	btn_code = snd_soc_read(codec, MSM8X16_WCD_A_ANALOG_MBHC_BTN_RESULT);

	switch (btn_code) {
	case 0:
		btn = 0;
		break;
	case 1:
		btn = 1;
		break;
	case 3:
		btn = 2;
		break;
	case 7:
		btn = 3;
		break;
	case 15:
		btn = 4;
		break;
	default:
		btn = -EINVAL;
		break;
	};

	return btn;
}

static bool msm8x16_spmi_lock_sleep(struct wcd_mbhc *mbhc, bool lock)
{
	if (lock)
		return wcd9xxx_spmi_lock_sleep();
	wcd9xxx_spmi_unlock_sleep();
	return 0;
}

static bool msm8x16_wcd_micb_en_status(struct wcd_mbhc *mbhc, int micb_num)
{
	if (micb_num == MIC_BIAS_1)
		return (snd_soc_read(mbhc->codec,
				     MSM8X16_WCD_A_ANALOG_MICB_1_EN) &
			0x80);
	if (micb_num == MIC_BIAS_2)
		return (snd_soc_read(mbhc->codec,
				     MSM8X16_WCD_A_ANALOG_MICB_2_EN) &
			0x80);
	return false;
}

static void msm8x16_wcd_enable_master_bias(struct snd_soc_codec *codec,
					   bool enable)
{
	if (enable)
		snd_soc_update_bits(codec, MSM8X16_WCD_A_ANALOG_MASTER_BIAS_CTL,
				    0x30, 0x30);
	else
		snd_soc_update_bits(codec, MSM8X16_WCD_A_ANALOG_MASTER_BIAS_CTL,
				    0x30, 0x00);
}

static void msm8x16_wcd_mbhc_common_micb_ctrl(struct snd_soc_codec *codec,
					      int event, bool enable)
{
	u16 reg;
	u8 mask;
	u8 val;

	switch (event) {
	case MBHC_COMMON_MICB_PRECHARGE:
		reg = MSM8X16_WCD_A_ANALOG_MICB_1_CTL;
		mask = 0x60;
		val = (enable ? 0x60 : 0x00);
		break;
	case MBHC_COMMON_MICB_SET_VAL:
		reg = MSM8X16_WCD_A_ANALOG_MICB_1_VAL;
		mask = 0xFF;
		val = (enable ? 0xC0 : 0x00);
		break;
	case MBHC_COMMON_MICB_TAIL_CURR:
		reg = MSM8X16_WCD_A_ANALOG_MICB_1_EN;
		mask = 0x04;
		val = (enable ? 0x04 : 0x00);
		break;
	default:
		pr_err("%s: Invalid event received\n", __func__);
		return;
	};
	snd_soc_update_bits(codec, reg, mask, val);
}

static void msm8x16_wcd_mbhc_internal_micbias_ctrl(struct snd_soc_codec *codec,
						   int micbias_num, bool enable)
{
	if (micbias_num == 1) {
		if (enable)
			snd_soc_update_bits(codec,
				MSM8X16_WCD_A_ANALOG_MICB_1_INT_RBIAS,
				0x18, 0x18);
		else
			snd_soc_update_bits(codec,
				MSM8X16_WCD_A_ANALOG_MICB_1_INT_RBIAS,
				0x10, 0x00);
	}
}

static bool msm8x16_wcd_mbhc_hph_pa_on_status(struct snd_soc_codec *codec)
{
	return (snd_soc_read(codec, MSM8X16_WCD_A_ANALOG_RX_HPH_CNP_EN) &
		0x30) ? true : false;
}

static void msm8x16_wcd_mbhc_program_btn_thr(struct snd_soc_codec *codec,
					     s16 *btn_low, s16 *btn_high,
					     int num_btn, bool is_micbias)
{
	int i;
	u32 course, fine, reg_val;
	u16 reg_addr = MSM8X16_WCD_A_ANALOG_MBHC_BTN0_ZDETL_CTL;
	s16 *btn_voltage;

	btn_voltage = ((is_micbias) ? btn_high : btn_low);

	for (i = 0; i <  num_btn; i++) {
		course = (btn_voltage[i] / MSM8X16_WCD_MBHC_BTN_COARSE_ADJ);
		fine = ((btn_voltage[i] % MSM8X16_WCD_MBHC_BTN_COARSE_ADJ) /
				MSM8X16_WCD_MBHC_BTN_FINE_ADJ);

		reg_val = (course << 5) | (fine << 2);
		snd_soc_update_bits(codec, reg_addr, 0xFC, reg_val);
		pr_debug("%s: course: %d fine: %d reg_addr: %x reg_val: %x\n",
			  __func__, course, fine, reg_addr, reg_val);
		reg_addr++;
	}
}

static void msm8x16_wcd_mbhc_calc_impedance(struct wcd_mbhc *mbhc, uint32_t *zl,
					    uint32_t *zr)
{
	struct snd_soc_codec *codec = mbhc->codec;
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	s16 impedance_l, impedance_r;
	s16 impedance_l_fixed;
	s16 reg0, reg1, reg2, reg3, reg4;
	bool high = false;
	bool min_range_used =  false;

	WCD_MBHC_RSC_ASSERT_LOCKED(mbhc);
	reg0 = snd_soc_read(codec, MSM8X16_WCD_A_ANALOG_MBHC_DBNC_TIMER);
	reg1 = snd_soc_read(codec, MSM8X16_WCD_A_ANALOG_MBHC_BTN2_ZDETH_CTL);
	reg2 = snd_soc_read(codec, MSM8X16_WCD_A_ANALOG_MBHC_DET_CTL_2);
	reg3 = snd_soc_read(codec, MSM8X16_WCD_A_ANALOG_MICB_2_EN);
	reg4 = snd_soc_read(codec, MSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL);

	msm8x16_wcd->imped_det_pin = WCD_MBHC_DET_BOTH;
	mbhc->hph_type = WCD_MBHC_HPH_NONE;

	/* disable FSM and micbias and enable pullup*/
	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL,
			0x80, 0x00);
	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MICB_2_EN,
			0xA5, 0x25);
	/*
	 * Enable legacy electrical detection current sources
	 * and disable fast ramp and enable manual switching
	 * of extra capacitance
	 */
	pr_debug("%s: Setup for impedance det\n", __func__);

	msm8x16_set_ref_current(codec, I_h4_UA);

	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBHC_DET_CTL_2,
			0x06, 0x02);
	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBHC_DBNC_TIMER,
			0x02, 0x02);
	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBHC_BTN2_ZDETH_CTL,
			0x02, 0x00);

	pr_debug("%s: Start performing impedance detection\n",
		 __func__);

	H) ||
		(msm8x16_wcd->impeL &mpedance_l, i&mpedance_r);
		if (mmpedance_l => 2 ||impedance_r;=> 2 {
		cigh = frue;
}	if (e!bhc->hbhc_cafg>hbono_ter_o_etection\ {
			r* detuZDET_RCHGto c0 to cisahar e =amp a/
		*snd_soc_update_bits(codec,
				MMSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL,
				00x02, 0x00);

		r* dwit f40s for mhe misahar e =amp ao complete */
		uusleep_range(2000
, 4,000);
		/snd_soc_update_bits(codec,
				MSM8X16_WCD_A_ANALOG_MBHC_FTN0_ZDETL_CTL;
				0x083 0x00);

		rsm8x16_wcd->imped_det_pin = Wmmpedance_l => 2 &&						   i  inpedance_r;=> 2 {?						   i  iCD_MBHC_DET_NONE) :						   i  i(mmpedance_l => 2 {?						   i  iCD_MBHC_DET_NPHR) :						   i  iCD_MBHC_DET_HPHL) 

		rf (msm8x16_wcd->imped_det_pin == WCD_MBHC_DET_NONE) 					goo cexit
		} else {
			rf (get_codec_version(msm8x16_wcd) <>=CAJON) {
		p	if (mmpedance_l == W63 &&inpedance_r;== W63 {
		p	ipr_debug("%s: SPHL)and ePHR) re Float ng n",
				_	 __func__);
			r	rsm8x16_wcd->imped_det_pin = 				_	 	CD_MBHC_DET_NONE)
			r	rshc->hph_type = WCD_MBHC_HPH_NONE;

	r	r else if (mmpedance_l == W63					    &&inpedance_r;=<W63 {
		p	ipr_debug("%s: SMono HSwithoSPHL)aloat ng n",
				_	 __func__);
			r	rsm8x16_wcd->imped_det_pin = 				_	 	CD_MBHC_DET_NPHR)
			r	rshc->hph_type = WCD_MBHC_HPH_NMONO

	r	r else if (mmpedance_lr== W63 &&					    mpedance_l =<W63 {
		p	ipr_debug("%s: SMono HSwithoSPHLRaloat ng n",
				_	 __func__);
			r	rsm8x16_wcd->imped_det_pin = 				_	 	CD_MBHC_DET_NPHRL
			r	rshc->hph_type = WCD_MBHC_HPH_NMONO

	r	r else if (mmpedance_l => 3 &&inpedance_r;=> 3 &&			r	rmmpedance_l == Wmpedance_r); {
		p	ipnd_soc_update_bits(codec,
				MMSM8X16_WCD_A_ANALOG_MBHC_FET_CTL_2,
			0	0x06, 0x026;
			r	r) ||
		(msm8x16_wcd->impeL &mpedance_l, 				_	 	i  i&mpedance_r);
			p	if (mmpedance_lr== Wmpedance_rl)				_	 r_debug("%s: SMono Had(et\n", 				_	 	i _func__);
			r	rrsm8x16_wcd->imped_det_pin = 				_	 	CD_MBHC_DET_NONE)
			r	rrshc->hph_type = 				_	 	CD_MBHC_DPH_NMONO

	r	r else i
		p	ipr_debug("%s: STER_EO had(et\is frund/n",
				_	 __func__);
			r	rsm8x16_wcd->imped_det_pin = 				_	 	CD_MBHC_DET_NOTH;
	m	r	rshc->hph_type = WCD_MBHC_HPH_NTER_EO

	r	r 
r	r 
r	}	}

	rsm8x16_set_ref_current(codec, I_ht5_UA:;
	msm8x16_wet_ref_current(codec, I_h1_UA);

	s* Enable ZRMP_CL ,ZRMP_CR &ZDET_RCHG/
	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBHC_FTN0_ZDETL_CTL;
				x083 0x003;
	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBHC_BSM_CTL,
			0x82, 0x02);
	s* dwit for m50s c =or mhe mHWto avply_=amp an cPHL)and ePHR) /
	ssleep_range(2500
, 45000);
		* Enable ZDET_RDISCHGCAP,CTL, to caddextra capacitance
 /
	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL,
			0x801 0x01);
		* dwit for m5s c =or mhe moltage =o cet_ stble Z/
	ssleep_range(2500
 4500);
	
	H) ||
		(msm8x16_wcd->impeL &mpedance_l, i&mpedance_r);
		iin_range_used =  sm8x16_adj_ref_current(sodec,
						 &mpedance_l, i&mpedance_r);
	if (e!bhc->hbhc_cafg>hbono_ter_o_etection\ {
			* detuZDET_RCHGto c0 to cisahar e =amp a/
		*nd_soc_update_bits(codec,
				MSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL,
				0x04, 0x00);

		* dwit for m40s c =or mhe mapacitar mh cisahar e =/
		usleep_range(2000
, 4,000);
		/nd_soc_update_bits(codec,
				MSM8X16_WCD_A_ANALOG_MBHC_FTN0_ZDETL_CTL;
				0x083 0x00);

		goo cexit
		}
	s* Ewe re Fstting ref current no che moinimunrange ir mhe measuremd	 * oalue flr e  mheanche moinimum alue, stomin_range_used =s frue;.	 * oI the Ghad(et\is fbonoGhad(et\iithoSetho  mPHL)aorSPHLRaloat ng 	 * oho nEwe have rlead(y donethe modnoGteredodetection cnd dionot 	 * oned =o comn nge frurho  .	 */
		if (e!bn_range_used =|
		i  ism8x16_wcd->imped_det_pin == WCD_MBHC_DET_HPHL)=|
		i  ism8x16_wcd->imped_det_pin == WCD_MBHC_DET_HPHL)
			goo cexit
	
	s* EDsable fetuZDET_RCONN_RMP_CL nd enable mDET_RCONN_FIXEDCL /
	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBHC_FTN0_ZDETL_CTL;
				x082 0x00);
	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MIHC_FTN01ZDETL_CTL,
			0x82, 0x02);
	s* detuZDET_RCHGto c0 t/
	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL,
			0x802 0x00);
	s* dwit for m40s c =or mhe mapacitar mh cisahar e =/
		sleep_range(2000
, 4,000);
		s* detuZDET_RCONN_RMP_CRto c0 t/
	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBHC_FTN0_ZDETL_CTL;
				x081 0x00);
	s* dnable ZDET_L_MEAS_EN */
		nd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL,
			0x80, 0x08);
	e* dwit for m2s c =or mhe mHWto aompute_ lft imnedance value f/
		sleep_range(2000, 2100);
		* Read Left impedance value from Result1 */
		mpedance_l_fixed;= snd_soc_read(codec,
				SM8X16_WCD_A_ANALOG_MBHC_BTN_RESULT);
		* EDsable fDET_L_MEAS_EN */
		nd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL,
			0x80, 0x080;
	/*
	 * EAsumer mpedance_l =s fL1,impedance_l_fixed;=s fL2.	 * oI the Gfollowng rondsiion cs fbet,Ewe an rtakethei
	 * ahad(et\ia fbonoGonetithoSmpedance voffL2.	 * oOho  wie, ftaketi\ia fteredodithoSmpedance voffL1.	 * oCndsiion :	 * anbs[(L2-0.5L1)/(L2+0.5L1)]=<Wnbs [(L2-L1)/(L2+L1)]	 */
	pf ((mnbs(mpedance_l_fixed;=-impedance_l_/2) 
	 	(mpedance_l_fixed;=+Wmpedance_rl) <>=	 	(nbs(mpedance_l_fixed;=-impedance_l_) 
	 	(mpedance_l_fixed;=+Wmpedance_rl/2)) {
		pr_debug("%s: RTER_EO lug hype =etectid\n",
			 __func__);
		rshc->hph_type = WCD_MBHC_HPH_NTER_EO

	 else i
		pr_debug("%s: SMONO lug hype =etectid\n",
			 _func__);
		rshc->hph_type = WCD_MBHC_HPH_NMONO

	rmpedance_l = smpedance_l_fixed;
	s}	s* dnable ZDET_LCHGtt/
	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL,
			0x802 0x00);
	s* dwit for m10s c =or mhe mapacitar mh char e =/
		sleep_range(21000, 10010);
	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MIHC_FTN0_ZDETL_CTL;
				x082 0x00);
	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBHC_BTN21ZDETL_CTL,
			0x82, 0x020;
	s* detuZDET_RCHGto c0 th cisahar e =PHL)=/
	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL,
			0x802 0x00);
	s* dwit for m40s c =or mhe mapacitar mh cisahar e =/
		sleep_range(2000
, 4,000);
	snd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBHC_FTN0_ZDETL_CTL;
				x082 0x00);
	
exit:	snd_soc_uwritecodec, MSM8X16_WCD_A_ANALOG_MBHC_FSM_CTL) reg4;;
	snd_soc_uwritecodec, MSM8X16_WCD_A_ANALOG_MBCB_2_EN, 0eg3 ;
	snd_soc_uwritecodec, MSM8X16_WCD_A_ANALOG_MBHC_BTN2_ZDETH_CTL,
reg1,;
	snd_soc_uwritecodec, MSM8X16_WCD_A_ANALOG_MBHC_BBNC_TIMER, 0eg0 ;
	snd_soc_uwritecodec, MSM8X16_WCD_A_ANALOG_MBHC_BBT_CTL_2, 0eg2 ;
	msm8x16_wcd-compute_impedance(sodec, Mmpedance_l, impedance_r;
				     M l, ur, bigh) 

	pr_debug("%s: SRL%d rohm, RR%d rohmn", __func__, czl, uzr)
;	pr_debug("%s: SIpedance detection\complete \n", __func__);
	

static int msm8x16_megisterenotifier;struct snd_soc_codec *codec,
				     itruct sotifier;_bock_ *nbock_
				     iool enable)
{
	itruct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	if (mnable)
			eturn btock_ng notifier;_harn_reg[stere(&sm8x16_wcd->iotifier; 				_	 	nbock_;
	return ftock_ng notifier;_harn_runeg[stere(				&sm8x16_wcd->iotifier; 	nbock_;
	

static int msm8x16_mcd_regquestirq(itruct snd_soc_codec *codec,
				    nt irq, brq_ceandlr;_t eandlr;
				    onst char *vame, boid *aata(
{
	return (cd9xxx_spmi_uegquestirq(irq, beandlr;
 ame, bata(

	

static int msm8x16_mcd_rree irq(itruct snd_soc_codec *codec,
				 nt irq, boid *aata(
{
	return (cd9xxx_spmi_uree irq(irq, bata(

	

static ionst struct wcd_mbhc_icb bhc_icb  {
	.mnable_ma_source(= msm8x16_wcd-enable_ext_mb_source(
	.hrim_btn_reg(= msm8x16_wrim_btn_reg(
	.hompute_impedance(= msm8x16_wcd-ebhc_calc_impedance(
	.het_micb_as_nalue f msm8x16_wcd-eet_micb_v(
	.het_muto_zeroing(f msm8x16_wcd-eet_muto_zeroing(
	.het_hwdep_fw_cal(f msm8x16_wcd-eet_hwdep_fw_cal(
	.het_mapamodu f msm8x16_wcd-eonfigure_cap(
	.hegisterenotifier;f msm8x16_wegisterenotifier;
	.hegquestirq(f msm8x16_wcd-eegquestirq(
	.hrq_control(= wcdcxxx_spmi_irq_control(
	.hree irq(f msm8x16_wcd-eree irq(
	.hok_setup(f msm8x16_wbhc_clk_setup(
	.hap_btn_code_to_num(f msm8x16_wbhc_cap_btn_code_to_num(
	.hock_sleep(f msm8x16_wpmi_lock_sleep(
	.hacb_as_nnable_etatus(= msm8x16_wcd-ebcb_en_status(,	.mbhc_btas a msm8x16_wcd-enable_easter_bias(,	.mbhc_bommon_micb_ctrl(= msm8x16_wcd-ebhc_cammon_micb_ctrl(
	.hacb_internal_= msm8x16_wcd-ebhc_cnternal_micbias_ctrl(,	.hph_ra_on_status(= msm8x16_wcd-ebhc_cph_ra_on_status(
	.het_mtn_thr(= msm8x16_wcd-ebhc_crogram_btn_thr(
	.hxt_nuse_mb(= msm8x16_wcd-ese_mb(
};

static const sint32_t *cd_imped_ialu] = {
4 8, 81, 013 016 				_	0, 524 82, 3,,
			0	036 4,0 4,4 848;

soid msm8x16_wotifier;_hal(struct snd_soc_codec *codec,
				   onst snum wcd_cotifiy_vent rvent) {
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	pr_debug("%s: Eotifier;fhal(event rd\n", __func__, cvent) 
	boock_ng notifier;_hal(_harn_(&sm8x16_wcd->iotifier; event, 				     i&sm8x16_wcd->ibhc);
	}
static int get_cpmi_lsm8x16_wcd_dtvice info(s16 r*eg(
	.	struct msm8x16_wcd_ppmi m*msm8x16_wcd)
{
	ift ren = 0;
		ft ralue f m((*eg( &0x00f0); >> 8) &0x0000f

	p*eg(= m*eg(=- value -*0x10, ;
	snitch (ealue)/{
	case 0:
		ase 1:
		bmsm8x16_wcd = s&sm8x16_wcd-ebdules[Malue)]
		break;
	default:
		prn = -EINVAL;
		break;
	};	return (rn;
}

static bnt msm8x16_mcd_rahbuwritedtvice struct msm8x16_wcd_*msm8x16_wcd 
			0	016 reg_, u8 *alue, s32 cbyte)
{
	i32 ctep = (m(32 )(*alue,)) &0x0000000F;
		16 rffset * (meg(=- x0820); &0x003F;
		ool eq6state_= false;
	
	q6state_= f6core._is_dsp_sead(y(;
	if (eq6state_=! frue; {
		pr_debug("%s: Rq6not reqd(y d\n", __func__, cq6state_;
		return N0
		}
	sr_debug("%s: RSP bisreqd(y d\n", __func__, cq6state_;
		iowrite32(tep ,ism8x16_wcd->idig_ase; +rffset ;
	return 0;
}

static bnt msm8x16_mcd_rahbueqd(dtvice struct msm8x16_wcd_*msm8x16_wcd 
			0	016 reg_, u2 cbyte), u8 *alue,
{
	i32 ctep 
		16 rffset * (meg(=- x0820); &0x003F;
		ool eq6state_= false;
	
	q6state_= f6core._is_dsp_sead(y(;
	if (eq6state_=! frue; {
		pr_debug("%s: Rq6not reqd(y d\n", __func__, cq6state_;
		return N0
		}
sr_debug("%s: RSP bisreqd(y d\n", __func__, cq6state_;
	
	tep = (ioeqd(32(sm8x16_wcd->idig_ase; +rffset ;
	r*alue f m(u8)tep 
		eturn 0;
}

static bnt msm8x16_mcd_rpmi_lwritedtvice s16 reg_, u8 *alue, s32 cbyte)
{
		ift reet
	struct msm8x16_wcd_ppmi m*cd = sULL;
			etu= get_cpmi_lsm8x16_wcd_dtvice info(s&eg_, &cd);

if (eetu {
		pr_err("%s: Nnvalid eegisterecaddess_n", __func__);
		return Neet
	s}
	if (mcd =  sULL; {
		pr_err("%s: NFaild =o cet_ evice *nfo(n", __func__);
		return N-ENODEV
	};	retu= snmi_enxtwegisterenwritel(cd->inmi_->trl(, cd->inmi_->si 
			0	0	cd->iase; +reg_, alue, sbyte)


if (eetu 		pr_err(_atelimit.ed("Uable Zo cwriteto caddr=%x 0egt(%d)n",
				_eg_, rt ;
	r/* Try aain_if (he mwritetfils.*/
	pf ((etu=! f0 {
		psleep_range(210 110;
		retu= snmi_enxtwegisterenwritel(cd->inmi_->trl(, cd->inmi_->si 
			0	0	cd->iase; +reg_, alue, s0;
		rf ((etu=! f0 {
		ppr_err(_atelimit.ed("faild =o cwritetoe mivice n",;
			rlturn Neet
	s	}	}

sr_debug("%writetsucss_eegisterec=%x ral = (x\n",
reg_, *alue,

		eturn 0;
}

s
nt msm8x16_mcd_rpmi_leqd(dtvice s16 reg_, u2 cbyte), u8 *dest
{
	ift retu= s0
	struct msm8x16_wcd_ppmi m*cd = sULL;
			etu= get_cpmi_lsm8x16_wcd_dtvice info(s&eg_, &cd);

if (eetu {
		pr_err("%s: Nnvalid eegisterecaddess_n", __func__);
		return Neet
	s}
	if (mcd =  sULL; {
		pr_err("%s: NFaild =o cet_ evice *nfo(n", __func__);
		return N-ENODEV
	};		retu= snmi_enxtwegistereneqd(l(cd->inmi_->trl(, cd->inmi_->si 
			0	0	cd->iase; +reg_, dest sbyte)


if (eetu=! f0 {
		pr_err("%faild =o ceqd(toe mivice n",;
			eturn Neet
	s}
sr_debug("%s: Reg(=0xx r==0xx n", __func__, ceg_, *dest

		eturn 0;
}

snt msm8x16_mcd_rpmi_leqd((nsigned lshortceg_, nt btyte), oid *aaest
{
	ieturn 0sm8x16_mcd_rpmi_leqd(dtvice seg_, tyte), dest

	

snt msm8x16_mcd_rpmi_lwritecnsigned lshortceg_, nt btyte), oid *asrc
{
	ieturn 0sm8x16_mcd_rpmi_lwritedtvice seg_, src sbyte)




static bnt m_lsm8x16_wcd_deg_aeqd((truct snd_soc_codec *codec,
				 nsigned lshortceg_
{
	ift retu= sEINVAL;
		8 txep = 0;

struct msm8x16_wcd_*msm8x16_wcd = sodec->dontrol(data;
sstruct msm8x96_adoc_cmah_deta *mdata * sULL;
			r_debug("%s: eg(= mx n", __func__, ceg_;
	mstex_lock_(&sm8x16_wcd->iio_ock_;
	rdata * snd_soc_coardget_drvdata(codec)>donmponnt,.oard


if (eSM8X16_WCD_IRS_OMBAK_2REGseg_))		retu= ssm8x16_mcd_rpmi_leqd((eg_, 1, &xep ;
	else
 f (eSM8X16_WCD_IRS_IGITAL_CREGseg_)){
		mstex_lock_(&data >dod_cmlk_sstex_;
		rf ((atoic_revd((&data >dmlk_snable_d)=  salse;){
			rf (gdata >dafeclk_ser = = AFECLK_CVERSON_TV1 {
		p	idata >digital_god_clk_hok_sal = 				_	 	data >dmlk_sreq;
	s		retu= safecet_migital_godec_gore._cock_(				_	 AFECORTEID_1PRIMARY_MI2S_RX
						 &data >digital_god_clk_;
			r else i
		p	idata >digital_god_clre._cokmnable_= 1;
		b	retu= safecet_mlpass_cock__v2(				_	 AFECORTEID_1PRIMARY_MI2S_RX
						 &data >digital_god_clre._cok;
			r 			rf (getu=<f0 {
		pppr_err("%faild =o cnable_=oe mCLK_n",;
			r	goo cer;
}		r 			rr_debug("%nable_d igital_codec plre. cokn",;
			rltu= ssm8x16_mcd_rahbueqd(dtvice s			r	rsm8x16_wcd- ceg_, 1, &xep ;
	e		atoic_ret_(&data >dmlk_snable_d,frue; 
	e		scheules_elayed_work (&data >digable_imlk_sork  450 
	er;:				utex_unlock(&xdata >dod_cmlk_sstex_;
		r	utex_unlock(&xsm8x16_wcd->iio_ock_;
	r		eturn Ntep 
		r 			ltu= ssm8x16_mcd_rahbueqd(dtvice ssm8x16_wcd- ceg_, 1, &xep ;
	e	utex_unlock(&xdata >dod_cmlk_sstex_;
		}
	utex_unlock(&xsm8x16_wcd->iio_ock_;
	
rf (getu=<f0 {
		pev_err(_atelimit.ed(sm8x16_wcd->idv,
	ssss%s: couec peqd(tfaild =or meg(=0xx n",
				__func__, teg_;
	m	eturn Neet
	s}
sev_edbg(sm8x16_wcd->idv,
 "ead L0xx02xfrom R0xx n", _tep ,ieg_;
	
	eturn Ntep 
	

static bnt m_lsm8x16_wcd_deg_awritectruct snd_soc_codec *codec,
				nsigned lshortceg_, 8 val;
{
	ift retu= sEINVAL;
		truct msm8x16_wcd_*msm8x16_wcd = sodec->dontrol(data;
sstruct msm8x96_adoc_cmah_deta *mdata * sULL;
			stex_lock_(&sm8x16_wcd->iio_ock_;
	rdata * snd_soc_coardget_drvdata(codec)>donmponnt,.oard


if (eSM8X16_WCD_IRS_OMBAK_2REGseg_))		retu= ssm8x16_mcd_rpmi_lwriteceg_, 1, &al);
		lse
 f (eSM8X16_WCD_IRS_IGITAL_CREGseg_)){
		mstex_lock_(&data >dod_cmlk_sstex_;
		rf ((atoic_revd((&data >dmlk_snable_d)=  salse;){
			rr_debug("%nable_mCLK_=or mAHBcwriten",;
			rf (gdata >dafeclk_ser = = AFECLK_CVERSON_TV1 {
		p	idata >digital_god_clk_hok_sal = 				_	 	data >dmlk_sreq;
	s		retu= safecet_migital_godec_gore._cock_(				_	 AFECORTEID_1PRIMARY_MI2S_RX
						 &data >digital_god_clk_;
			r else i
		p	idata >digital_god_clre._cokmnable_= 1;
		b	retu= safecet_mlpass_cock__v2(				_	 AFECORTEID_1PRIMARY_MI2S_RX
						 &data >digital_god_clre._cok;
			r 			rf (getu=<f0 {
		pppr_err("%faild =o cnable_=oe mCLK_n",;
			r	etu= s0
	s	r	goo cer;
}		r 			rr_debug("%nable_d igital_codec plre. cokn",;
			rltu= ssm8x16_mcd_rahbuwritedtvice s						 sm8x16_wcd- ceg_, &al) s0;
		r	atoic_ret_(&data >dmlk_snable_d,frue; 
	e		scheules_elayed_work (&data >digable_imlk_sork  450 
	er;:				utex_unlock(&xdata >dod_cmlk_sstex_;
		r	utex_unlock(&xsm8x16_wcd->iio_ock_;
	r		eturn Neet
	s	}	}rltu= ssm8x16_mcd_rahbuwritedtvice ssm8x16_wcd- ceg_, &al) s0;
		rutex_unlock(&xdata >dod_cmlk_sstex_;
		}
	utex_unlock(&xsm8x16_wcd->iio_ock_;
	
return Neet
	

static bnt msm8x16_mcd_rvolticl(struct snd_soc_codec *codec, snsigned lft ret_
{
	iev_edbg(odec->dev, "%s: ceg(=0xx n",
__func__, ceg_;
		ieturn 0sm8x16_mcd_reg_aeqd(only[eg_]
	

static int msm8x16_mcd_regadble_struct snd_soc_codec *css, snsigned lft ret_
{
	ieturn 0sm8x16_mcd_reg_aeqd(ble_[eg_]
	

static int msm8x16_mcd_rwritectruct snd_soc_codec *codec,
snsigned lft ret_
			  _  uisigned lft ralue,
{
	ift reet
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	pev_edbg(odec->dev, "%s: cWritetfom Reg(=0xx ral =0xx n",
				_	_func__, ceg_, alue,

		f (getg= = SND_SOC_NOPM
			eturn b;

	sBUG_ONgetg=>MSM8X16_WCD_MBAXREGISTER(;
	if (e!bm8x16_mcd_rvolticl(sodec, reg_)){
		metu= snd_soc_coacheuwritecodec, Meg_, alue,

		if (eetu=! f0 				ev_err(_atelimit.ed(odec->dev, "%Cachecwriteto cx rfaild :%d\n",
				_eg_, rt ;
	r
	if ((nloikely(testibit(BUS_IOWN,i&sm8x16_wcd->itatus(eastk)) {
		pr_drr(_atelimit.ed("writet0xx02xfhile (offlinen",
				_eg_;
		return N-ENODEV
	};	return N_lsm8x16_wcd_deg_awritecodec, Meg_, (u8)alue,

	

static iisigned lft rsm8x16_mcd_regad(truct snd_soc_codec *codec,
				 nsigned lft ret_
{
	iisigned lft ralu;	ift reet
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	pf (getg= = SND_SOC_NOPM
			eturn b;

	sBUG_ONgetg=>MSM8X16_WCD_MBAXREGISTER(;
		if (e!bm8x16_mcd_rvolticl(sodec, reg_) &&		i  ism8x16_wcd-regadble_sodec, reg_) &&		reg =<sodec->ddiv er->eg_aoacheusize){
		metu= snd_soc_coacheuead(codec, Meg_, &al)

		if (eetu=> f0 				eturn balu;	i	ev_err(_atelimit.ed(odec->dev, "%Cacheceqd(tfom Rx rfaild :%d\n",
				_eg_, rt ;
	r
	if ((nloikely(testibit(BUS_IOWN,i&sm8x16_wcd->itatus(eastk)) {
		pr_drr(_atelimit.ed("writet0xx02xfhile (offlinen",
				_eg_;
		return N-ENODEV
	};	ral = (_lsm8x16_wcd_deg_aeqd((odec, reg_);	/*
	 * EIfeegisterecis0x106aorSx106 regurn Need(talue fas0, rs chea 	 * oSW an risable fonly=oe mequired\ nternrupts. Which will	 * oenurem oho  mnternrupts re Fot reffctid\.	 */
	pf ((metg= = SM8X16_WCD_A_DIGITAL_CNT_RENCLKR ||
		(	metg= = SM8X16_WCD_A_DNALOG_MNT_RENCLKR  			al = (0
	sev_edbg(odec->dev, "%s: cRqd(tfom Reg(=0xx ral =0xx n",
				_	_func__, ceg_, alu

		eturn 0vl;
}

static void wsm8x16_wcd_doost_vn(struct mnd_soc_codec *codec)
{
	int beet
	s8 vdest
	struct msm8x16_wcd_ppmi m*cd = s&sm8x16_wcd-ebdules[M0]
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	petu= snmi_enxtwegistereneqd(l(cd->inmi_->trl(, PIC_BSLAVEID_11
				_	PIC_BLDO7RENCLL,
r&dest s1


if (eetu=! f0 {
		pr_err("%s: cfaild =o ceqd(toe mivice :d\n", __func__, crt ;
	rreturn;
	};
sr_debug("%s: RLDO tate_:=0xx n",
__func__, cdest

		pf ((mdest & MASK_MSB_BIT)=  s0 {
		pr_err("%LDO7Fot reable_d eturn;!n",;
			eturn 
	};	retu= snmi_enxtwegistereneqd(l(cd->inmi_->trl(, PIC_BSLAVEID_10
						 PIC_BMBG_OK
r&dest s1


if (eetu=! f0 {
		pr_err("%s: cfaild =o ceqd(toe mivice :d\n", __func__, crt ;
	rreturn;
	};
sr_debug("%s: RPIC_ BG tate_:=0xx n",
__func__, cdest

		pf ((mdest & MASK_MSB_BIT)=  s0 {
		pr_err("%PIC_ MBGFot rON,inable_=odec *hwsna MB bi\iaain_n",;
			nd_soc_uwritecodec, 
	MSM8X16_WCD_A_ANALOG_MBSTER_BIAS_CTL,
0x30);
	er/* Allow 1s for mPIC_ MBGFtate_=o cbe pdate_d=/
		usleep_range(2CODE_BBTLAY_1_MS, CODE_BBTLAY_1_1_MS;
		retu= snmi_enxtwegistereneqd(l(cd->inmi_->trl(, PIC_BSLAVEID_10
						 PIC_BMBG_OK
r&dest s1


irf ((etu=! f0 {
		ppr_err("%s: cfaild =o ceqd(toe mivice :d\n", 						 _func__, crt ;
	rrreturn;
	}	}	}rf ((mdest & MASK_MSB_BIT)=  s0 {
		ppr_err("%PIC_ MBGFsicllFot rON aferecetury eturn;!n",;
			return;
	}	}	}}	snd_soc_update_bits(codec,
			SM8X16_WCD_A_DIGITAL_CPERPHRESUT_CTL_3
			0x0F 0x00F;
	snd_soc_uwritecodec, 
	MSM8X16_WCD_A_ANALOG_MSE_BACCSS,
	.0xA5,;
	snd_soc_uwritecodec, 
	MSM8X16_WCD_A_ANALOG_MPERPHRESUT_CTL_3
			0x0F;
	snd_soc_uwritecodec, 
	MSM8X16_WCD_A_ANALOG_MASTER_BIAS_CTL,
			x30);
	ef (get_codec_version(msm8x16_wcd) < CAJON)2_0)
{
		pnd_soc_uwritecodec, 
	MMSM8X16_WCD_A_ANALOG_MURR:ET_RLIMIT
			0x80 ;
	m else i
		pnd_soc_uwritecodec, 
	MMSM8X16_WCD_A_ANALOG_MURR:ET_RLIMIT
			0x8A2;
	r
	ind_soc_update_bits(codec,
			SM8X16_WCD_A_DNALOG_MSPKR_DRVCTL,
			x369 0x069;
	snd_soc_update_bits(codec,
			SM8X16_WCD_A_DNALOG_MSPKR_DRVCDBG
			0x01 0x01);
		nd_soc_update_bits(codec,
			SM8X16_WCD_A_DNALOG_MSLOPECOMMP_IP_ZERO
			0x8, 0x088;
	snd_soc_update_bits(codec,
			SM8X16_WCD_A_DNALOG_MSPKR_DACCTL,
			x383 0x003;
	snd_soc_update_bits(codec,
			SM8X16_WCD_A_DNALOG_MSPKR_OCPCTL,
			x3E1 0x0E1;
	ef (get_codec_version(msm8x16_wcd) < CAJON)2_0)
{
		pnd_soc_update_bits(codec,
			MSM8X16_WCD_A_AIGITAL_CDC_DIG_CLK_CTL,
				0x2, 0x02);
	ersleep_range(2CODE_BBTLAY_1_MS, CODE_BBTLAY_1_1_MS;
		rnd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBOOSTRENCLL,
				0xDF 0x0DF;
	ersleep_range(2CODE_BBTLAY_1_MS, CODE_BBTLAY_1_1_MS;
		 else i
		pnd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBOOSTRENCLL,
				0x4, 0x00);
	spnd_soc_update_bits(codec,
			MSM8X16_WCD_A_AIGITAL_CDC_DIG_CLK_CTL,
				0x2, 0x02);
	ernd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBOOSTRENCLL,
				0x0, 0x08);
	ersleep_range(250
 4500;
	ernd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBOOSTRENCLL,
				0x4, 0x04);
	ersleep_range(250
 4500;
	e
}

static void msm8x16_wcd_moost_vnffstruct mnd_soc_codec *codec)
{
	ind_soc_update_bits(codec,
			SM8X16_WCD_A_DNALOG_MBOOSTRENCLL,
			0xDF 0x05F;
	snd_soc_update_bits(codec,
			SM8X16_WCD_A_DIGITAL_CDC_DIG_CLK_CTL,
			0x2, 0x00);
}

static void msm8x16_wcd_mbypass_n(struct mnd_soc_codec *codec)
{
	itruct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	pf (get_codec_version(msm8x16_wcd) < CAJON)2_0)
{
		pnd_soc_uwritecodec, 
	MMSM8X16_WCD_A_ANALOG_MSE_BACCSS,
	.00xA5,;
	spnd_soc_uwritecodec, 
	MMSM8X16_WCD_A_ANALOG_MPERPHRESUT_CTL_3
			0x07,;
	ernd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBYPASS_MODE
				x082 0x00);
	srnd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBYPASS_MODE
				x081 0x00);
	spnd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBYPASS_MODE
				x04, 0x04);
	ernd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBYPASS_MODE
				x00, 0x08);
	ernd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBOOSTRENCLL,
				0xDF 0x0DF;
	e else i
		pnd_soc_update_bits(codec,
			MSM8X16_WCD_A_AIGITAL_CDC_DIG_CLK_CTL,
				0x2, 0x02);
	ernd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBYPASS_MODE
				x02, 0x02);
	e
}

static void msm8x16_wcd_moypass_nffstruct mnd_soc_codec *codec)
{
	inruct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	pf (get_codec_version(msm8x16_wcd) < CAJON)2_0)
{
		pnd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBOOSTRENCLL,
				0x0, 0x00);
	spnd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBYPASS_MODE
				x00, 0x00);
	spnd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBYPASS_MODE
				x04, 0x00);

		nd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBYPASS_MODE
				x04, 0x00);
	}
else i
		pnd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBYPASS_MODE
				x02, 0x00);
	spnd_soc_update_bits(codec,
			MSM8X16_WCD_A_AIGITAL_CDC_DIG_CLK_CTL,
				0x2, 0x00);
	}
}

static void msm8x16_scd_moost_vodu _sgquece(struct snd_soc_codec *codec, 						nt bflag
{
	inruct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	pf (gflag=  sEAR_PMU
{
		pnitch (esm8x16_wcd->ioost_vnpion\ {
			ase 1BOOSTRSWITCH:				f (msm8x16_wcd->iearra_ooost_vstu {
		p	 sm8x16_wcd-moost_vnffsodec);

	p	 sm8x16_wcd-moypass_n(sodec);

	p	 			rreak;
	}	ase 1BOOSTRALWAYS:				um8x16_wcd_doost_vn(sodec);

	p	reak;
	}	ase 1BYPASS_ALWAYS:				um8x16_wcd_doypass_n(sodec);

	p	reak;
	}	ase 1BOOSTRN)2FOREVER:				um8x16_wcd_doost_vn(sodec);

	p	reak;
	}	efault:
		ppr_err("%s: civalid eoost_ npion\: d\n", __func__, 
					 sm8x16_wcd->ioost_vnpion\ 

	p	reak;
	}	}
r else if (mflag=  sEAR_PMD
{
		pnitch (esm8x16_wcd->ioost_vnpion\ {
			ase 1BOOSTRSWITCH:				f (msm8x16_wcd->iearra_ooost_vstu 
	p	 sm8x16_wcd-moypass_nffsodec);

	p	reak;
	}	ase 1BOOSTRALWAYS:				um8x16_wcd_doost_vnffsodec);

	p	/* 80s for mEAReoost_ toFsttile down*/
		uumleep()8);
	er	reak;
	}	ase 1BYPASS_ALWAYS:				/* noting
mh ciofas0oypassan calways*/
		uureak;
	}	ase 1BOOSTRN)2FOREVER:				/* noting
mh ciofas0oost_ nnfor eer =/
		uureak;
	}	efault:
		ppr_err("%s: civalid eoost_ npion\: d\n", __func__, 
					 sm8x16_wcd->ioost_vnpion\ 

	p	reak;
	}	}
r else if (mflag=  sSPK_PMU
{
		pnitch (esm8x16_wcd->ioost_vnpion\ {
			ase 1BOOSTRSWITCH:				f (msm8x16_wcd->ipk_eoost_vstu {
		p	 sm8x16_wcd-moypass_nffsodec);

	p		um8x16_wcd_doost_vn(sodec);

	p	 			rreak;
	}	ase 1BOOSTRALWAYS:				um8x16_wcd_doost_vn(sodec);

	p	reak;
	}	ase 1BYPASS_ALWAYS:				um8x16_wcd_doypass_n(sodec);

	p	reak;
	}	ase 1BOOSTRN)2FOREVER:				um8x16_wcd_doost_vn(sodec);

	p	reak;
	}	efault:
		ppr_err("%s: civalid eoost_ npion\: d\n", __func__, 
					 sm8x16_wcd->ioost_vnpion\ 

	p	reak;
	}	}
r else if (mflag=  sSPK_PMD
{
		pnitch (esm8x16_wcd->ioost_vnpion\ {
			ase 1BOOSTRSWITCH:				f (msm8x16_wcd->ipk_eoost_vstu {
		p	 sm8x16_wcd-moost_vnffsodec);

	p	 *
	    * EAdd 40msm leep(for mhe mpk_	    * Eoost_ toFsttile down	    * /		p	 smeep()4);
	er	 			rreak;
	}	ase 1BOOSTRALWAYS:				um8x16_wcd_doost_vnffsodec);

	p	/*
   * EAdd 40msm leep(for mhe mpk_	   * Eoost_ toFsttile down	   */
		uumleep()4);
	er	reak;
	}	ase 1BYPASS_ALWAYS:				/* noting
mh ciofas0oypassan calways*/
		uureak;
	}	ase 1BOOSTRN)2FOREVER:				/* noting
mh ciofas0oost_ nnfor eer =/
		uureak;
	}	efault:
		ppr_err("%s: civalid eoost_ npion\: d\n", __func__, 
					 sm8x16_wcd->ioost_vnpion\ 

	p	reak;
	}	}
r 	

static int msm8x16_mcd_rdtra_rse_veg_anfo(snruct mevice **dv,
	snruct msm8x16_wcd_peg_ulaar m*veg_, onst char *vveg_aame, 		ool eondeman)
{
	ift rlen,retu= s0
	sonst c__be32 *pro 
		har *pro aame,[CODE_BBTMBAXRPROP_SIZE]
	struct mtvice indu f*eg(ndu f mULL;
	}u2 cpro aal;

	swnrivntf(pro aame,, CODE_BBTMBAXRPROP_SIZE "%s:-suply_, 			veg_aame,

		et(ndu f mofra_rse_peandlr(tvi->ofrndu , pro aame,, 0;
		if (e!et(ndu  {
		dev_err(cev, "%Loo_ng  up %s pro ertyint ndu f%s faild n", 				pro aame,, tvi->ofrndu ->fullaame,

		return N-ENODEV
	};		rev_edbg(ev, "%Loo_ng  up %s pro ertyint ndu f%sn", 			pro aame,, tvi->ofrndu ->fullaame,

	
	veg_->ame,f mveg_aame,;
	veg_->ondeman)f mondeman)

	swnrivntf(pro aame,, CODE_BBTMBAXRPROP_SIZE 			"qcom,s:-oltage , _veg_aame,

		pro f mofret_dpro erty(tvi->ofrndu , pro aame,, &len;
		if (e!pro f|| (len=! f(2 * sizeof(__be32))) {
		pev_err(cev, "%%s %s pro ertyn", 				pro  ? "ivalid eormiat" : "no", pro aame,

		return N-ENVAL;
		}
	veg_->bn_ruvf mbe32to_ncpup(&pro [0]);
	veg_->maxruvf mbe32to_ncpup(&pro [1])

	swnrivntf(pro aame,, CODE_BBTMBAXRPROP_SIZE 			"qcom,s:-urrent , _veg_aame,

		retu= sofraro ertyueqd(du2 (tvi->ofrndu , pro aame,, &pro aal;


if (eetu {
		dev_err(cev, "%Loo_ng  up %s pro ertyint ndu f%s faild , 				pro aame,, tvi->ofrndu ->fullaame,

		return N-EAULT_
		}
	veg_->npiomum_u * spro aal;

	sev_edbg(ev, "%s: colt=[%d d\]uV,curre=[%d]uA,mond d\n"n", _veg_->ame, 			 veg_->bn_ruv _veg_->maxruv _veg_->npiomum_u  _veg_->nndeman)
;		eturn 0;
}

static boid msm8x16_scd_mdtra_rse_oost_vnfo(snruct mnd_soc_codec *codec)
{
	inruct msm8x16_wcd_priv *msm8x16_wcd priv *=	spnd_soc_uodec_get_drvdata(codec);

	onst char *vpro aame,* s"qcom,cdc-oost_-oltage ,;	int btost_voltage ,beet
		retu= sofraro ertyueqd(du2 (odec->dev,->ofrndu , pro aame,,				&tost_voltage 


if (eetu {
		dev_edbg(odec->dev, "%Loo_ng  up %s pro ertyint ndu f%s faild n", 				pro aame,, odec->dev,->ofrndu ->fullaame,

		rtost_voltage = sDEAULT_MBOOSTRVOLTAGE;	r
	if ((tost_voltage =< MINMBOOSTRVOLTAGE||
		(	tost_voltage => BAXRBOOSTRVOLTAGE {
		dev_err(codec->dev, 
				"Incorenc btost_moltage . Reer tng
mh cifault:n",;
			tost_voltage = sDEAULT_MBOOSTRVOLTAGE;	r
		msm8x16_wcd-criv >ioost_voltage = 			VOLTAGERCONVERTER(tost_voltage ,bMINMBOOSTRVOLTAGE 
				BOOSTRVOLTAGENTERP;
		ev_edbg(odec->dev, "%Bost_moltage talue fis:%d\n",
				tost_voltage 




static boid msm8x16_scd_mdtra_rse_icbias_cnfo(snruct mevice **dv,
	s	struct mwdcxxx_sicbias_cstting r*icbias)
{
	ionst char *vpro aame,* s"qcom,cdc-icbias)-cfilt-mv,;	int beet
		retu= sofraro ertyueqd(du2 (ev,->ofrndu , pro aame,,				&icbias)->cfilt1_mv


if (eetu {
		dev_edbg(ev, "%Loo_ng  up %s pro ertyint ndu f%s faild , 				pro aame,, tvi->ofrndu ->fullaame,

		ricbias)->cfilt1_mv  MIC_IAS_CDEAULT_MAL;
		}


static bnruct msm8x16_wcd_preta *msm8x16_wcd_propulaaemdtraata(c
					 nruct mevice **dv,
{
	inruct msm8x16_wcd_preta *mdata ;	int beet, tatic _ct, bnnd_ct, bidx,i;
	uonst char *vame,f mULL;
	}onst char *vtatic _pro aame,* s"qcom,cdc-tatic -suplyies"
	}onst char *vnnd_pro aame,* s"qcom,cdc-on-deman)-suplyies"
	}onst char *vadde_pro aame,* s"qcom,dig-cdc-ose -adde"
			rata * sdv,m_kzalloc(ev, "sizeof(mdata ), GFP_KERNEL;
	if (e!data )		return NULL;
			tatic _ct,= sofraro ertyucoun_vstivngs(ev,->ofrndu , tatic _pro aame,;
	if (eIS_ERRMAL;UE(tatic _ct,) {
		pev_err(cev, "%%s NFaild =o cet_ tatic bnuplyies d\n", __func__, 
			tatic _ct,)
		retu= -EINVAL;
		bgoo cer;
}	}
	s* EOn-deman)bnuplyy lis\is fan npion\al pro ertyi/
		nnd_ct,= sofraro ertyucoun_vstivngs(ev,->ofrndu , nnd_pro aame,;
	if (eIS_ERRMAL;UE(nnd_ct,  			nnd_ct,= s;

	sBUG_ONgtatic _ct,=< s;f|| nnd_ct,=< );
	ef (ggtatic _ct,=+ nnd_ct,)=> ARRAY_SIZEgdata >deg_ulaar ) {
		pev_err(cev, "%%s NNumvoffnuplyies du=> maxfnuplore_d=%zdn",
				__func__, tgtatic _ct,=+ nnd_ct,) 
					ARRAY_SIZEgdata >deg_ulaar ) 
		retu= -EINVAL;
		bgoo cer;
}	}
	sor (i d r==0;bidx=< tatic _ct,;bidx++){
		metu= sofraro ertyueqd(dstivngcnfdex(ev,->ofrndu ,
					    itrtic _pro aame, bidx,
					    i&ame,

		rf (eetu {
		dpev_err(cev, "%%s Nofceqd(tstivngf%s idx=%d rr(r (%dn",
				__func__, ttrtic _pro aame, bidx,crt ;
	rrrgoo cer;
}		}
	ssev_edbg(ev, "%s: cFund/ tatic bcdcbnuplyy %sn", __func__, 
			ame,

		retu= ssm8x16_mcd_rdtra_rse_veg_anfo(sev, 
					 &data >deg_ulaar [idx] 
					 ame, balse;)
		rf (eetu {
		dpev_err(cev, "%%s rr( a_rsvngfveg_for m%s idx=%dn",
				__func__, tame, bidx;
	rrrgoo cer;
}		}
	}
	sor (i   0; i <  nnd_ct, i++),bidx++){
		metu= sofraro ertyueqd(dstivngcnfdex(ev,->ofrndu , nnd_pro aame,,
					    ii,i&ame,

		rf (eetu {
		dpev_err(cev, "%%s Nrr( a_rsvngfon_deman)bor m%s idx=%dn",
				__func__, tnnd_pro aame,, i;
	rrrgoo cer;
}		}
	ssev_edbg(ev, "%s: cFund/ on-deman)bcdcbnuplyy %sn", __func__, 
			ame,

		retu= ssm8x16_mcd_rdtra_rse_veg_anfo(sev, 
					 &data >deg_ulaar [idx] 
					 ame, brue; 
	e	f (eetu {
		dpev_err(cev, "%%s Nrr( a_rsvngfveg_fon_deman)bor m%s idx=%dn",
				__func__, tame, bidx;
	rrrgoo cer;
}		}
	}
	sm8x16_scd_mdtra_rse_icbias_cnfo(sev, "&data >dmcbias)
;	retu= sofraro ertyueqd(du2 (ev,->ofrndu , adde_pro aame,,				&data >digigod_cadde


if (eetu {
		dev_edbg(ev, "%s: courldFot rfind ds enury nt dtn",
				__func__, tadde_pro aame,;
	rrdata >digigod_cadde = SM8X16_WIGITAL_CDODE_BBAE_ADJDR;	};		return Ndata ;	er;:		dv,m_kree (ev, "pata(

	pev_err(cev, "%%s NFaild =o cropulaae DTbata(retu= s%dn",
			_func__, crt ;
	return NULL;
	

static int msm8x16_mcd_rodec_gnable_eon_deman)_nuplyyc
		nruct mnd_soc_cdapm_widet_ *w,
		nruct mnd_skontrol(=*kontrol(, nt bvent) {
	sft retu= s0
	struct mnd_soc_codec *codec = mw>codec;
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	s1ruct mon_deman)_nuplyy*vtuplyy
		if (ew>cshifu=> fONCDEMAND_SUPPLIESMBAX {
		dev_err(codec->dev, "%%s Nrr(r imfdex=> BAX Deman)bnuplyies"
				_func__);
		retu= -EINVAL;
		bgoo cout
	s}
sev_edbg(odec->dev, "%%s Ntuplyy: ds eent) %d regf:s%dn",
			_func__, con_deman)_nuplyyaame,[w>cshifu] event, 			atoic_revd((&sm8x16_wcd->ion_deman)_lis\[w>cshifu].egf))

	swuplyy*= &sm8x16_wcd->ion_deman)_lis\[w>cshifu]
	sWARN_ONCE(!wuplyy>csuplyy "%%s isn' mevfind n", 			 con_deman)_nuplyyaame,[w>cshifu];
	if (e!wuplyy>csuplyy {
		dev_err(codec->dev, "%%s Nrr(bnuplyy ot rpresnt ron)bor m%d"
				_func__), w>cshifu)
		bgoo cout
	s}
snitch (event) {
	case 0SND_SOC_DAPM_PRE_PMU:
	rf ((atoic_rin_revurn (&wuplyy>cegf)=  s1 				etu*= eg_ulaar gnable_(wuplyy>csuplyy 
	e	f (eetu 		dpev_err(codec->dev, "%%s NFaild =o cnable_=%sn", 					_func__),					on_deman)_nuplyyaame,[w>cshifu];
	i	reak;
	}ase 0SND_SOC_DAPM_POSTRPMD:
	rf ((atoic_revd((&wuplyy>cegf)=  s0 {
		dpev_edbg(odec->dev, "%%s N%s nuplyy has0oee risable d.n", 					 _func__, con_deman)_nuplyyaame,[w>cshifu];
	rrrgoo cout
	s	}	}rf ((atoic_rec_gevurn (&wuplyy>cegf)=  s0 				etu*= eg_ulaar gisable (wuplyy>csuplyy 
	e		f (eetu 		dppev_err(codec->dev, "%%s NFaild =o cisable f%sn", 						_func__),						on_deman)_nuplyyaame,[w>cshifu];
	i	reak;
	}efault:
		preak;
	};	out:
return Neet
	

static bnt msm8x16_mcd_rodec_gnable_ecock__oock_struct snd_soc_codec *codec, 						sft rnable)
{
	itruct msm8x96_adoc_cmah_deta *mdata * sULL;
			rata * snd_soc_coardget_drvdata(codec)>donmponnt,.oard


if (enable)
i
		pnd_soc_update_bits(codec,
			MSM8X16_WCD_A_ADC_DLK_CMLK_CTL,
 0x01 0x01);
		pnd_soc_update_bits(codec,
			MSM8X16_WCD_A_ADC_DLK_CPDMCTL,
 0x03 0x003;
	ssnd_soc_update_bits(codec,
			MSM8X16_WCD_A_ANALOG_MBSTER_BIAS_CTL,
0x30)
0x30);
	ernd_soc_update_bits(codec,
			MSM8X16_WCD_A_AIGITAL_CDC_DRSTCTL,
0x30, 0x08);
	ernd_soc_update_bits(codec,
			MSM8X16_WCD_A_AIGITAL_CDC_DTOP_LK_CTL,
 0x0C
 0x0C 
	e	f (edata >dmlk_sreq;= = SLK_CRATE_12P288MHZ 		dpnd_soc_update_bits(codec,
				MSM8X16_WCD_A_ADC_DTOP_LL,
 0x01 0x01);
	erlse if (mdata >dmlk_sreq;= = SLK_CRATE_9P6MHZ 		dpnd_soc_update_bits(codec,
				MSM8X16_WCD_A_ADC_DTOP_LL,
 0x01 0x011;
	e else i
		pnd_soc_update_bits(codec,
			MSM8X16_WCD_A_AIGITAL_CDC_DTOP_LK_CTL,
 0x0C
 0x0);
		/nd_soc_update_bits(codec,
				MSM8X16_WCD_A_ADC_DLK_CPDMCTL,
 0x03 0x000;
		i}		eturn 0;
}

static bnt msm8x16_mcd_rodec_gnable_ecar e _pumpstruct snd_soc_cdapm_widet_ *w,
		nruct mnd_skontrol(=*kontrol(, nt bvent) {
	struct mnd_soc_codec *codec = mw>codec;
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	
sev_edbg(odec->dev, "%%s Nvent r=rd\n", __func__, cvent) 
	bnitch (event) {
	case 0SND_SOC_DAPM_PRE_PMU:
	rsm8x16_mcd_rodec_gnable_ecock__oock_sodec, r1


irf ((!strucmpsw->ame,  "EAReCP")) {
		ppnd_soc_update_bits(codec,
				MMSM8X16_WCD_A_AIGITAL_CDC_DIG_CLK_CTL,
						x00, 0x08);
	er	um8x16_wcd_doost_vodu _sgquece(sodec, rEAR_PMU

	s	}else if (met_codec_version(msm8x16_wcd) < = DIANGU {
		ppnd_soc_update_bits(codec,
				MMSM8X16_WCD_A_AIGITAL_CDC_DIG_CLK_CTL,
						x00, 0x08);
	er else i
		p	nd_soc_update_bits(codec,
				MMSM8X16_WCD_A_AIGITAL_CDC_DIG_CLK_CTL,
						x0C, 0x0C);
	er 	i	reak;
	}ase 0SND_SOC_DAPM_POSTRPMU:
	rsleep_range(2CODE_BBTLAY_1_MS, CODE_BBTLAY_1_1_MS;
		rreak;
	}ase 0SND_SOC_DAPM_POSTRPMD:
	rsleep_range(2CODE_BBTLAY_1_MS, CODE_BBTLAY_1_1_MS;
		rf ((!strucmpsw->ame,  "EAReCP")) {
		ppnd_soc_update_bits(codec,
				MMSM8X16_WCD_A_AIGITAL_CDC_DIG_CLK_CTL,
						x00, 0x00);
		/	f (msm8x16_wcd->ioost_vnpion\=! fBOOSTRALWAYS {
		p	 ev_edbg(odec->dev, 						%%s Noost_vnpion\:%d,frear down*earn",
				_	_func__, csm8x16_wcd->ioost_vnpion\ 

	p		um8x16_wcd_doost_vodu _sgquece(sodec, rEAR_PMD;
	er	 			r/*
   * EReet *pa selnc btt foom Rear o chph aferecear pa
   * Eisrisable dand ePHR DACcisable fo ceqducecear
   * Eurn 0offcropand eaoid mPHR ropai\comnurrentcy
   * /		dpnd_soc_update_bits(codec,
				MSM8X16_WCD_A_ANALOG_MRX_EAR_TL,
0x30, 0x00);
	er else i
		p	f (get_codec_version(msm8x16_wcd) < CDIANGU 				Mnd_soc_update_bits(codec,
				MMSM8X16_WCD_A_AIGITAL_CDC_DIG_CLK_CTL,
						x04, 0x00);
		/	f (msm8x16_wcd->irx_ias_ctoun_=  s0 				pnd_soc_update_bits(codec,
				MMSM8X16_WCD_A_AIGITAL_CDC_DIG_CLK_CTL,
						x00, 0x00);
		/	ev_edbg(odec->dev, "%s: cex_ias_ctoun_= =%dn",
				_	_func__, csm8x16_wcd->iex_ias_ctoun_;
	er 	i	reak;
	}}		eturn 0;
}

static bnt msm8x16_mcd_rearra_ooost_vet_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	
suontrol(->alue .nterger.alue [0]= 			msm8x16_wcd->iearra_ooost_vstu ? 1 : );
	eev_edbg(odec->dev, "%s: csm8x16_wcd->iearra_ooost_vstu  =%dn",
				_func__, csm8x16_wcd->iearra_ooost_vstu ;		eturn 0;
}

static bnt msm8x16_mcd_rearra_ooost_vst_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	
sev_edbg(odec->dev, "%s: cuontrol(->alue .nterger.alue [0]=  %ldn",
			_func__, cuontrol(->alue .nterger.alue [0])
	ssm8x16_wcd->iearra_ooost_vstu  			muontrol(->alue .nterger.alue [0]=?brue; :balse;)
		eturn 0;
}

static bnt msm8x16_mcd_ra_oain_vet_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	su8 earra_oain_
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	
	earra_oain_  snd_soc_read(codec,
 SM8X16_WCD_A_DNALOG_MRX_EAR_TL,)
	
	earra_oain_  s(earra_oain_ >> 5; &0x01

	if (mnarra_oain_  =0x00);{
		psontrol(->alue .nterger.alue [0]=  0;
r else if (mnarra_oain_  =0x001;{
		psontrol(->alue .nterger.alue [0]=  1;
r else i{
		dev_err(codec->dev, "%%s NERROR: Unnuplore_d=Ear Gin_  s0xx n",
				_func__, cvarra_oain_

		return N-ENVAL;
		}

suontrol(->alue .nterger.alue [0]=  earra_oain_
	sev_edbg(odec->dev, "%%s Nvarra_oain_  s0xx n",
__func__, cvarra_oain_

		eturn 0;
}

static bnt msm8x16_mcd_rloopbak__odu _et_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	struct msm8x96_adoc_cmah_deta *mdata * sULL;
			rata * snd_soc_coardget_drvdata(codec)>donmponnt,.oard


iev_edbg(odec->dev, "%s: cuontrol(->alue .nterger.alue [0]=  %ldn",
			_func__, cuontrol(->alue .nterger.alue [0])
		return Ndata ->lb_odu 
}

static bnt msm8x16_mcd_rloopbak__odu _pu_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	struct msm8x96_adoc_cmah_deta *mdata * sULL;
			rata * snd_soc_coardget_drvdata(codec)>donmponnt,.oard


iev_edbg(odec->dev, "%s: cuontrol(->alue .nterger.alue [0]=  %ldn",
			_func__, cuontrol(->alue .nterger.alue [0])
		rnitch (euontrol(->alue .nterger.alue [0]){
	case 0:
			data ->lb_odu = false;
		rreak;
	}ase 01
			data ->lb_odu = frue;
	i	reak;
	}efault:
		peturn N-ENVAL;
		}

seturn 0;
}

static bnt msm8x16_mcd_ra_oain_vpu_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	su8 earra_oain_
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	
	ev_edbg(odec->dev, "%s: cuontrol(->alue .nterger.alue [0]=  %ldn",
			_func__, cuontrol(->alue .nterger.alue [0])
		rnitch (euontrol(->alue .nterger.alue [0]){
	case 0:
			varra_oain_  s0x00
		rreak;
	}ase 01
			varra_oain_  s0x20
	i	reak;
	}efault:
		peturn N-ENVAL;
		}

snd_soc_update_bits(codec,
 SM8X16_WCD_A_DNALOG_MRX_EAR_TL,,				   i0x2, 0varra_oain_

		eturn 0;
}

static bnt msm8x16_mcd_rph_rodu _et_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	pf (gsm8x16_wcd->iph_rodu =  sUORML_CMODE;{
		psontrol(->alue .nterger.alue [0]=  0;
r else if (msm8x16_wcd->iph_rodu =  sHD2CMODE;{
		psontrol(->alue .nterger.alue [0]=  1;
r else i{
		dev_err(codec->dev, "%%s NERROR: Dfault:mPHR Mdec =%dn",
				_func__, csm8x16_wcd->iph_rodu );	};		rev_edbg(odec->dev, "%s: csm8x16_wcd->iph_rodu =  d\n", __func__, 
			sm8x16_wcd->iph_rodu );	}eturn 0;
}

static bnt msm8x16_mcd_rph_rodu _st_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	
sev_edbg(odec->dev, "%s: cuontrol(->alue .nterger.alue [0]=  %ldn",
			_func__, cuontrol(->alue .nterger.alue [0])
		rnitch (euontrol(->alue .nterger.alue [0]){
	case 0:
			sm8x16_wcd->iph_rodu =  UORML_CMODE
		rreak;
	}ase 01
			f (get_codec_version(msm8x16_wcd) <>=CDIANGU 				sm8x16_wcd->iph_rodu =  HD2CMODE
	i	reak;
	}efault:
		psm8x16_wcd->iph_rodu =  UORML_CMODE
		rreak;
	}}
sev_edbg(odec->dev, "%%s Nsm8x16_wcd->iph_rodu vstu  =%dn",
			_func__, csm8x16_wcd->iph_rodu );	}eturn 0;
}

static bnt msm8x16_mcd_roost_vnpion\_et_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	pf (gsm8x16_wcd->ioost_vnpion\===1BOOSTRSWITCH;{
		psontrol(->alue .nterger.alue [0]=  0;
r else if (msm8x16_wcd->ioost_vnpion\===1BOOSTRALWAYS {
		psontrol(->alue .nterger.alue [0]=  1;
r else if (msm8x16_wcd->ioost_vnpion\===1BYPASS_ALWAYS {
		psontrol(->alue .nterger.alue [0]=  2;
r else if (msm8x16_wcd->ioost_vnpion\===1BOOSTRN)2FOREVER {
		psontrol(->alue .nterger.alue [0]=  3;
r else i{
		dev_err(codec->dev, "%%s NERROR: Unnuplore_d=Bost_ npion\ =%dn",
				_func__, csm8x16_wcd->ioost_vnpion\ 

	peturn N-ENVAL;
		}

sev_edbg(odec->dev, "%%s Nsm8x16_wcd->ioost_vnpion\== d\n", __func__, 
			sm8x16_wcd->ioost_vnpion\ 

	eturn 0;
}

static bnt msm8x16_mcd_roost_vnpion\_st_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	
sev_edbg(odec->dev, "%s: cuontrol(->alue .nterger.alue [0]=  %ldn",
			_func__, cuontrol(->alue .nterger.alue [0])
		rnitch (euontrol(->alue .nterger.alue [0]){
	case 0:
			sm8x16_wcd->ioost_vnpion\== BOOSTRSWITCH
		rreak;
	}ase 01
			sm8x16_wcd->ioost_vnpion\== BOOSTRALWAYS
		rreak;
	}ase 02
			sm8x16_wcd->ioost_vnpion\== BYPASS_ALWAYS;
		um8x16_wcd_doypass_n(sodec);

	preak;
	}ase 03
			sm8x16_wcd->ioost_vnpion\== BOOSTRN)2FOREVER;
		um8x16_wcd_doost_vn(sodec);

	preak;
	}efault:
		pr_err("%s: civalid eoost_ npion\: d\n", __func__, 
					sm8x16_wcd->ioost_vnpion\ 

	peturn N-ENVAL;
		}
sev_edbg(odec->dev, "%%s Nsm8x16_wcd->ioost_vnpion\vstu  =%dn",
			_func__, csm8x16_wcd->ioost_vnpion\ 

	eturn 0;
}

static bnt msm8x16_mcd_rpk_eoost_vet_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	pf (gsm8x16_wcd->ipk_eoost_vstu=  salse;){
			sontrol(->alue .nterger.alue [0]=  0;
r else if (msm8x16_wcd->ipk_eoost_vstu=  srue; {
		psontrol(->alue .nterger.alue [0]=  1;
r else i{
		dev_err(codec->dev, "%%s NERROR: Unnuplore_d=Spak;er=Bost_ ==%dn",
				__func__, tsm8x16_wcd->ipk_eoost_vstu 

	peturn N-ENVAL;
		}

sev_edbg(odec->dev, "%%s Nsm8x16_wcd->ipk_eoost_vstu=  d\n", __func__, 
			sm8x16_wcd->ipk_eoost_vstu 

	eturn 0;
}

static bnt msm8x16_mcd_rpk_eoost_vst_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	
sev_edbg(odec->dev, "%s: cuontrol(->alue .nterger.alue [0]=  %ldn",
				_func__, cuontrol(->alue .nterger.alue [0])
		rnitch (euontrol(->alue .nterger.alue [0]){
	case 0:
			sm8x16_wcd->ipk_eoost_vstu=  alse;
		rreak;
	}ase 01
			sm8x16_wcd->ipk_eoost_vstu=  rue;
	i	reak;
	}efault:
		peturn N-ENVAL;
		}
sev_edbg(odec->dev, "%%s Nsm8x16_wcd->ipk_eoost_vstu=  d\n", 			_func__, csm8x16_wcd->ipk_eoost_vstu 

	eturn 0;
}

static bnt msm8x16_mcd_rnxtwpk_eoost_vet_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	pf (gsm8x16_wcd->inxtwpk_eoost_vstu=  salse;)			sontrol(->alue .nterger.alue [0]=  0;
rlse 		psontrol(->alue .nterger.alue [0]=  1;
	eev_edbg(odec->dev, "%s: csm8x16_wcd->iextwpk_eoost_vstu= =%dn",
				__func__, tsm8x16_wcd->iextwpk_eoost_vstu 

	eturn 0;
}

static bnt msm8x16_mcd_rnxtwpk_eoost_vst_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	
sev_edbg(odec->dev, "%s: cuontrol(->alue .nterger.alue [0]=  %ldn",
			_func__, cuontrol(->alue .nterger.alue [0])
		rnitch (euontrol(->alue .nterger.alue [0]){
	case 0:
			sm8x16_wcd->iextwpk_eoost_vstu= =alse;
		rreak;
	}ase 01
			sm8x16_wcd->iextwpk_eoost_vstu= =rue;
	i	reak;
	}efault:
		peturn N-ENVAL;
		}
sev_edbg(odec->dev, "%%s Nsm8x16_wcd->ipk_eoost_vstu=  d\n", 			_func__, csm8x16_wcd->ipk_eoost_vstu 

	eturn 0;
}

tatic bnt msm8x16_mcd_ret_dii gnable__audio_mixerc
					nruct mnd_skontrol(=*kontrol(,				ppnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	snt mii g d r==((nruct mnc_cmlt:i_mixer_ontrol(=* 				p	kontrol(->riv te_balue,
>deg_
	snt mban)_ d r==((nruct mnc_cmlt:i_mixer_ontrol(=* 				p	kontrol(->riv te_balue,
>dshifu
	
suontrol(->alue .nterger.alue [0]= 			mnd_soc_read(codec,
				   i(SM8X16_WCD_A_ADC_DIIR1_TL, + 64* Eii g d )) &		r(1 <<mban)_ d )) ! s;

	sev_edbg(odec->dev, "%%s NIIR #%dmban) #%dmnable_=%\n", __func__, 
		ii g d ,mban)_ d ,			munte32to)uontrol(->alue .nterger.alue [0])
	seturn 0;
}

static bnt msm8x16_mcd_rau_dii gnable__audio_mixerc
					nruct mnd_skontrol(=*kontrol(,				ppnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	snt mii g d r==((nruct mnc_cmlt:i_mixer_ontrol(=* 				p	kontrol(->riv te_balue,
>deg_
	snt mban)_ d r==((nruct mnc_cmlt:i_mixer_ontrol(=* 				p	kontrol(->riv te_balue,
>dshifu
		ft ralue,r==uontrol(->alue .nterger.alue [0];
	s* EMask firt_ 5btt s, 6-8 re Fresnrv_d=/
		nd_soc_update_bits(codec,
			(SM8X16_WCD_A_ADC_DIIR1_TL, + 64* Eii g d )
				   i(1 <<mban)_ d ), (alue,r<<mban)_ d ))

	sev_edbg(odec->dev, "%%s NIIR #%dmban) #%dmnable_=%\n", __func__, 
	  ii g d ,mban)_ d ,			mmnd_soc_read(codec,
			(SM8X16_WCD_A_ADC_DIIR1_TL, + 64* Eii g d )) &		 i(1 <<mban)_ d )) ! s;))
		return N;
}

tatic bunte32to et_dii gban)_coeffstruct mnd_soc_codec *codec) 					   nt mii g d , nt bban)_ d ,					   nt mcoeff_ d ){
	sunte32to alue,r==0;
	s* EAddress does ot rautomtic ally pdate_if (ead(ng r*
		nd_soc_uwritecodec, 
	M(SM8X16_WCD_A_ADC_DIIR1_TOEF_B1_TL, + 64* Eii g d )
			((ban)_ d r* BAND_BAX +mcoeff_ d ){		* sizeof(unte32to); &0x07F)
		ralue,r| snd_soc_read(codec,

	M(SM8X16_WCD_A_ADC_DIIR1_TOEF_B2_TL, + 64* Eii g d ));
		nd_soc_uwritecodec, 
	M(SM8X16_WCD_A_ADC_DIIR1_TOEF_B1_TL, + 64* Eii g d )
			((ban)_ d r* BAND_BAX +mcoeff_ d ){		* sizeof(unte32to) +m1; &0x07F)
		ralue,r| smnd_soc_read(codec,
			(SM8X16_WCD_A_ADC_DIIR1_TOEF_B2_TL, + 64* Eii g d )) <<m8);
		nd_soc_uwritecodec, 
	M(SM8X16_WCD_A_ADC_DIIR1_TOEF_B1_TL, + 64* Eii g d )
			((ban)_ d r* BAND_BAX +mcoeff_ d ){		* sizeof(unte32to) +m2; &0x07F)
		ralue,r| smnd_soc_read(codec,
			(SM8X16_WCD_A_ADC_DIIR1_TOEF_B2_TL, + 64* Eii g d )) <<m16);
		nd_soc_uwritecodec, 
	M(SM8X16_WCD_A_ADC_DIIR1_TOEF_B1_TL, + 64* Eii g d )
			((ban)_ d r* BAND_BAX +mcoeff_ d ){		* sizeof(unte32to) +m3; &0x07F)
		r* EMask its( top 2 its( sinc_=oe y re Fresnrv_d=/
		alue,r| smmnd_soc_read(codec,
i(SM8X16_WCD_A_ADC_DIIR1_TOEF_B2_TL,{		+ 64* Eii g d )) &0x30f) <<m24)
		return Nalue,
		

static bnt msm8x16_mcd_ret_dii gban)_audio_mixerc
					nruct mnd_skontrol(=*kontrol(,				ppnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	snt mii g d r==((nruct mnc_cmlt:i_mixer_ontrol(=* 				p	kontrol(->riv te_balue,
>deg_
	snt mban)_ d r==((nruct mnc_cmlt:i_mixer_ontrol(=* 				p	kontrol(->riv te_balue,
>dshifu
	
suontrol(->alue .nterger.alue [0]= 			et_dii gban)_coeffsodec,
iii g d ,mban)_ d , );
	euontrol(->alue .nterger.alue [1]= 			et_dii gban)_coeffsodec,
iii g d ,mban)_ d , 1;
	euontrol(->alue .nterger.alue [2]= 			et_dii gban)_coeffsodec,
iii g d ,mban)_ d , 2;
	euontrol(->alue .nterger.alue [3]= 			et_dii gban)_coeffsodec,
iii g d ,mban)_ d , 3;
	euontrol(->alue .nterger.alue [4]= 			et_dii gban)_coeffsodec,
iii g d ,mban)_ d , 4)

	sev_edbg(odec->dev, "%%s NIIR #%dmban) #%dmb0  s0xx n",
		%%s NIIR #%dmban) #%dmb1  s0xx n",
		%%s NIIR #%dmban) #%dmb2  s0xx n",
		%%s NIIR #%dmban) #%dma1  s0xx n",
		%%s NIIR #%dmban) #%dma2  s0xx n", 			_func__, cii g d ,mban)_ d ,			munte32to)uontrol(->alue .nterger.alue [0] 			_func__, cii g d ,mban)_ d ,			munte32to)uontrol(->alue .nterger.alue [1] 			_func__, cii g d ,mban)_ d ,			munte32to)uontrol(->alue .nterger.alue [2] 			_func__, cii g d ,mban)_ d ,			munte32to)uontrol(->alue .nterger.alue [3] 			_func__, cii g d ,mban)_ d ,			munte32to)uontrol(->alue .nterger.alue [4])
	seturn 0;
}

static boid mst_dii gban)_coeffstruct mnd_soc_codec *codec) 					nt mii g d , nt bban)_ d ,					unte32to alue,
{
	ind_soc_uwritecodec, 
	M(SM8X16_WCD_A_ADC_DIIR1_TOEF_B2_TL, + 64* Eii g d ) 
	M(alue f&0x3FF));
		nd_soc_uwritecodec, 
	M(SM8X16_WCD_A_ADC_DIIR1_TOEF_B2_TL, + 64* Eii g d ) 
	M(alue f>>m8)f&0x3FF);
		nd_soc_uwritecodec, 
	M(SM8X16_WCD_A_ADC_DIIR1_TOEF_B2_TL, + 64* Eii g d ) 
	M(alue f>>m16)f&0x3FF);
		* EMask top 2 its(, 7-8 re Fresnrv_d=/
		nd_soc_uwritecodec, 
	M(SM8X16_WCD_A_ADC_DIIR1_TOEF_B2_TL, + 64* Eii g d ) 
	M(alue f>>m24) &0x30F);
	

static bnt msm8x16_mcd_rau_dii gban)_audio_mixerc
					nruct mnd_skontrol(=*kontrol(,				ppnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	snt mii g d r==((nruct mnc_cmlt:i_mixer_ontrol(=* 				p	kontrol(->riv te_balue,
>deg_
	snt mban)_ d r==((nruct mnc_cmlt:i_mixer_ontrol(=* 				p	kontrol(->riv te_balue,
>dshifu
	
s* EMask top tt fi\is fresnrv_d=/
		* EUdate_stadderautomtic ally or meach B2 writet*
		nd_soc_uwritecodec, 
	M(SM8X16_WCD_A_ADC_DIIR1_TOEF_B1_TL, + 64* Eii g d )
			(ban)_ d r* BAND_BAX * sizeof(unte32to); &0x07F)
				nt_dii gban)_coeffsodec,
iii g d ,mban)_ d ,				   uontrol(->alue .nterger.alue [0])
	snt_dii gban)_coeffsodec,
iii g d ,mban)_ d ,				   uontrol(->alue .nterger.alue [1])
	snt_dii gban)_coeffsodec,
iii g d ,mban)_ d ,				   uontrol(->alue .nterger.alue [2])
	snt_dii gban)_coeffsodec,
iii g d ,mban)_ d ,				   uontrol(->alue .nterger.alue [3])
	snt_dii gban)_coeffsodec,
iii g d ,mban)_ d ,				   uontrol(->alue .nterger.alue [4])

	sev_edbg(odec->dev, "%%s NIIR #%dmban) #%dmb0  s0xx n",
		%%s NIIR #%dmban) #%dmb1  s0xx n",
		%%s NIIR #%dmban) #%dmb2  s0xx n",
		%%s NIIR #%dmban) #%dma1  s0xx n",
		%%s NIIR #%dmban) #%dma2  s0xx n", 			_func__, cii g d ,mban)_ d ,			et_dii gban)_coeffsodec,
iii g d ,mban)_ d , ); 			_func__, cii g d ,mban)_ d ,			et_dii gban)_coeffsodec,
iii g d ,mban)_ d , 1; 			_func__, cii g d ,mban)_ d ,			et_dii gban)_coeffsodec,
iii g d ,mban)_ d , 2; 			_func__, cii g d ,mban)_ d ,			et_dii gban)_coeffsodec,
iii g d ,mban)_ d , 3; 			_func__, cii g d ,mban)_ d ,			et_dii gban)_coeffsodec,
iii g d ,mban)_ d , 4))
	seturn 0;
}

static bnt msm8x16_mcd_ronmpan)er_et_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);

	nt mcompg d r==((nruct mnc_cmlt:i_mixer_ontrol(=* 				p	kontrol(->riv te_balue,
>deg_
	snt mrx_ d r==((nruct mnc_cmlt:i_mixer_ontrol(=* 				p	kontrol(->riv te_balue,
>dshifu
	
sev_edbg(odec->dev, "%%s Nsm8x16_wcd->icomp[%d]gnable_d[%d]  =%dn",
				_func__, ccompg d ,mrx_ d  
			sm8x16_wcd->icompgnable_d[rx_ d ];
	
suontrol(->alue .nterger.alue [0]= Nsm8x16_wcd->icompgnable_d[rx_ d ]
	
sev_edbg(odec->dev, "%s: cuontrol(->alue .nterger.alue [0]=  %ldn",
			_func__, cuontrol(->alue .nterger.alue [0])
		return 0;
}

static bnt msm8x16_mcd_ronmpan)er_st_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
		nt mcompg d r==((nruct mnc_cmlt:i_mixer_ontrol(=* 				p	kontrol(->riv te_balue,
>deg_
	snt mrx_ d r==((nruct mnc_cmlt:i_mixer_ontrol(=* 				p	kontrol(->riv te_balue,
>dshifu
		ft ralue,r==uontrol(->alue .nterger.alue [0];
	sev_edbg(odec->dev, "%s: cuontrol(->alue .nterger.alue [0]=  %ldn",
			_func__, cuontrol(->alue .nterger.alue [0])
		rf (get_codec_version(msm8x16_wcd) <>=CDIANGU {
		df ((!alue,
{			sm8x16_wcd->icompgnable_d[rx_ d ]=  0;
rrlse 		p	sm8x16_wcd->icompgnable_d[rx_ d ]=  compg d 
		}

sev_edbg(odec->dev, "%%s Nsm8x16_wcd->icomp[%d]gnable_d[%d]  =%dn",
			_func__, ccompg d ,mrx_ d  
		sm8x16_wcd->icompgnable_d[rx_ d ];
	
seturn 0;
}

static bcnst char *vbcnst csm8x16_mcd_rloopbak__odu _ctrl_text[]  =
		d"DISABLE"  "ENABLE"};static bcnst cnruct mnc_cenumcsm8x16_mcd_rloopbak__odu _ctlcenum[]  =
		dSOC_ENUM_SINGLE_EXT(2,csm8x16_mcd_rloopbak__odu _ctrl_text; 	};sstatic bcnst char *vbcnst csm8x16_mcd_rearra_ooost_vctrl_text[]  =
		d"DISABLE"  "ENABLE"};static bcnst cnruct mnc_cenumcsm8x16_mcd_rearra_ooost_vctlcenum[]  =
		dSOC_ENUM_SINGLE_EXT(2,csm8x16_mcd_rearra_ooost_vctrl_text; 	};sstatic bcnst char *vbcnst csm8x16_mcd_rearra_oain_vtext[]  =
		d"POS_1P5_DB"  "POS_6_DB"};static bcnst cnruct mnc_cenumcsm8x16_mcd_rearra_oain_venum[]  =
		dSOC_ENUM_SINGLE_EXT(2,csm8x16_mcd_rearra_oain_vtext; 	};sstatic bcnst char *vbcnst csm8x16_mcd_roost_vnpion\vctrl_text[]  =
		d"BOOSTRSWITCH"  "BOOSTRALWAYS"  "BYPASS_ALWAYS,
			"BOOSTRN)2FOREVER"};static bcnst cnruct mnc_cenumcsm8x16_mcd_roost_vnpion\vctlcenum[]  =
		dSOC_ENUM_SINGLE_EXT(4,csm8x16_mcd_roost_vnpion\vctrl_text; 	};static bcnst char *vbcnst csm8x16_mcd_rpk_eoost_vctrl_text[]  =
		d"DISABLE"  "ENABLE"};static bcnst cnruct mnc_cenumcsm8x16_mcd_rpk_eoost_vctlcenum[]  =
		dSOC_ENUM_SINGLE_EXT(2,csm8x16_mcd_rpk_eoost_vctrl_text; 	};sstatic bcnst char *vbcnst csm8x16_mcd_rextwpk_eoost_vctrl_text[]  =
		d"DISABLE"  "ENABLE"};static bcnst cnruct mnc_cenumcsm8x16_mcd_rextwpk_eoost_vctlcenum[]  =
		dSOC_ENUM_SINGLE_EXT(2,csm8x16_mcd_rextwpk_eoost_vctrl_text; 	};sstatic bcnst char *vbcnst csm8x16_mcd_rph_rodu vctrl_text[]  =
		d"UORML_"  "HD2"};static bcnst cnruct mnc_cenumcsm8x16_mcd_rph_rodu vctlcenum[]  =
		dSOC_ENUM_SINGLE_EXT(ARRAY_SIZEgsm8x16_mcd_rph_rodu vctrl_text) 
			sm8x16_wcd-rph_rodu vctrl_text) 
};ss/*cutNofcreq;uecey or mhigh passafilter*/static bcnst char *vbcnst ccf_text[]  =
		"MINM3DB_4Hz"  "MINM3DB_75Hz"  "MINM3DB_150Hz"	};sstatic bcnst cnruct mnc_cenumccf_ec-1cenumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DTX1_MUX_TL,
04,c3,ccf_text);sstatic bcnst cnruct mnc_cenumccf_ec-2cenumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DTX2_MUX_TL,
04,c3,ccf_text);sstatic bcnst cnruct mnc_cenumccf_rxmix1cenumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DRX1_B4_TL,
0x,c3,ccf_text);sstatic bcnst cnruct mnc_cenumccf_rxmix2cenumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DRX2_B4_TL,
0x,c3,ccf_text);sstatic bcnst cnruct mnc_cenumccf_rxmix3cenumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DRX3_B4_TL,
0x,c3,ccf_text);sstatic bcnst cnruct mnd_skontrol(_newcsm8x16_mcd_rpn)_cotrol(s[]  =
	
dSOC_ENUM_EXT("RXmPHR Mdec",csm8x16_mcd_rph_rodu vctlcenum[0] 			sm8x16_mcd_rph_rodu _et_,csm8x16_mcd_rph_rodu vstu ,	
dSOC_ENUM_EXT("Bost_ Opion\",csm8x16_mcd_roost_vnpion\vctlcenum[0] 			sm8x16_mcd_roost_vnpion\_et_,csm8x16_mcd_roost_vnpion\vstu ,	
dSOC_ENUM_EXT("EARePA=Bost_",csm8x16_mcd_rearra_ooost_vctlcenum[0] 			sm8x16_mcd_rearra_ooost_vet_,csm8x16_mcd_rearra_ooost_vstu ,	
dSOC_ENUM_EXT("EARePA=Gin_",csm8x16_mcd_rearra_oain_venum[0] 			sm8x16_mcd_ra_oain_vet_,csm8x16_mcd_ra_oain_vpu_ ,	
dSOC_ENUM_EXT("Spak;er=Bost_",csm8x16_mcd_rpk_eoost_vctlcenum[0] 			sm8x16_mcd_rpk_eoost_vet_,csm8x16_mcd_rpk_eoost_vstu ,	
dSOC_ENUM_EXT("Ext Spk=Bost_",csm8x16_mcd_rextwpk_eoost_vctlcenum[0] 			sm8x16_mcd_rextwpk_eoost_vet_,csm8x16_mcd_rextwpk_eoost_vstu ,	
dSOC_ENUM_EXT("LOOPBACK Mdec",csm8x16_mcd_rloopbak__odu _ctlcenum[0] 			sm8x16_mcd_rloopbak__odu _et_,csm8x16_mcd_rloopbak__odu _pu_ ,	
dSOC_SINGLE_TLV("ADC1 Volumc",cSM8X16_WCD_A_DNALOG_MTX_1_EN,c3,				p	8
0x,canalogoain_
,
dSOC_SINGLE_TLV("ADC2 Volumc",cSM8X16_WCD_A_DNALOG_MTX_2_EN,c3,				p	8
0x,canalogoain_
,
dSOC_SINGLE_TLV("ADC3 Volumc",cSM8X16_WCD_A_DNALOG_MTX_3_EN,c3,				p	8
0x,canalogoain_
,

dSOC_SINGLE_SX_TLV("RX1 Digital Volumc",				  SM8X16_WCD_A_ADC_DRX1_VOL_TL,_B2_TL,,				x,c -84,c4, 0digitaloain_
,
dSOC_SINGLE_SX_TLV("RX2 Digital Volumc",				  SM8X16_WCD_A_ADC_DRX2_VOL_TL,_B2_TL,,				x,c -84,c4, 0digitaloain_
,
dSOC_SINGLE_SX_TLV("RX3 Digital Volumc",				  SM8X16_WCD_A_ADC_DRX3_VOL_TL,_B2_TL,,				x,c -84,c4, 0digitaloain_
,

dSOC_SINGLE_SX_TLV("DEC1 Volumc",				  SM8X16_WCD_A_ADC_DTX1_VOL_TL,_GAIN,				x,c -84,c4, 0digitaloain_
,
dSOC_SINGLE_SX_TLV("DEC2 Volumc",				  SM8X16_WCD_A_ADC_DTX2_VOL_TL,_GAIN,				x,c -84,c4, 0digitaloain_
,

dSOC_SINGLE_SX_TLV("IIR1 INP1 Volumc",				  SM8X16_WCD_A_ADC_DIIR1_GAIN_B1_TL,,				x,c -84,c4, 0digitaloain_
,
dSOC_SINGLE_SX_TLV("IIR1 INP2 Volumc",				  SM8X16_WCD_A_ADC_DIIR1_GAIN_B2_TL,,				x,c -84,c4, 0digitaloain_
,
dSOC_SINGLE_SX_TLV("IIR1 INP3 Volumc",				  SM8X16_WCD_A_ADC_DIIR1_GAIN_B3_TL,,				x,c -84,c4, 0digitaloain_
,
dSOC_SINGLE_SX_TLV("IIR1 INP4 Volumc",				  SM8X16_WCD_A_ADC_DIIR1_GAIN_B4_TL,,				x,c -84,	4, 0digitaloain_
,
dSOC_SINGLE_SX_TLV("IIR2 INP1 Volumc",				  SM8X16_WCD_A_ADC_DIIR2_GAIN_B1_TL,,				x,c -84,c4, 0digitaloain_
,

dSOC_ENUM("TX1mPHF cutNoff",ccf_ec-1cenum
,
dSOC_ENUM("TX2mPHF cutNoff",ccf_ec-2cenum
,

dSOC_SINGLE("TX1mPHF Sitch ",			SM8X16_WCD_A_ADC_DTX1_MUX_TL,
03,c1, ); 		SOC_SINGLE("TX2mPHF Sitch ",			SM8X16_WCD_A_ADC_DTX2_MUX_TL,
03,c1, ); 			SOC_SINGLE("RX1mPHF Sitch ",			SM8X16_WCD_A_ADC_DRX1_B5_TL,
02,c1, ); 		SOC_SINGLE("RX2mPHF Sitch ",			SM8X16_WCD_A_ADC_DRX2_B5_TL,
02,c1, ); 		SOC_SINGLE("RX3mPHF Sitch ",			SM8X16_WCD_A_ADC_DRX3_B5_TL,
02,c1, ); 	
dSOC_ENUM("RX1mPHF cutNoff",ccf_rxmix1cenum
,
dSOC_ENUM("RX2mPHF cutNoff",ccf_rxmix2cenum
,
dSOC_ENUM("RX3mPHF cutNoff",ccf_rxmix3cenum
,

dSOC_SINGLE_EXT("IIR1 Eable_=Band1",cIIR1, BAND1,c1, ),
	sm8x16_scd_met_dii gnable__audio_mixer,
	sm8x16_scd_mau_dii gnable__audio_mixer
,
dSOC_SINGLE_EXT("IIR1 Eable_=Band2",cIIR1, BAND2,c1, ),
	sm8x16_scd_met_dii gnable__audio_mixer,
	sm8x16_scd_mau_dii gnable__audio_mixer
,
dSOC_SINGLE_EXT("IIR1 Eable_=Band3",cIIR1, BAND3,c1, ),
	sm8x16_scd_met_dii gnable__audio_mixer,
	sm8x16_scd_mau_dii gnable__audio_mixer
,
dSOC_SINGLE_EXT("IIR1 Eable_=Band4",cIIR1, BAND4,c1, ),
	sm8x16_scd_met_dii gnable__audio_mixer,
	sm8x16_scd_mau_dii gnable__audio_mixer
,
dSOC_SINGLE_EXT("IIR1 Eable_=Band5",cIIR1, BAND5,c1, ),
	sm8x16_scd_met_dii gnable__audio_mixer,
	sm8x16_scd_mau_dii gnable__audio_mixer
,
dSOC_SINGLE_EXT("IIR2 Eable_=Band1",cIIR2, BAND1,c1, ),
	sm8x16_scd_met_dii gnable__audio_mixer,
	sm8x16_scd_mau_dii gnable__audio_mixer
,
dSOC_SINGLE_EXT("IIR2 Eable_=Band2",cIIR2, BAND2,c1, ),
	sm8x16_scd_met_dii gnable__audio_mixer,
	sm8x16_scd_mau_dii gnable__audio_mixer
,
dSOC_SINGLE_EXT("IIR2 Eable_=Band3",cIIR2, BAND3,c1, ),
	sm8x16_scd_met_dii gnable__audio_mixer,
	sm8x16_scd_mau_dii gnable__audio_mixer
,
dSOC_SINGLE_EXT("IIR2 Eable_=Band4",cIIR2, BAND4,c1, ),
	sm8x16_scd_met_dii gnable__audio_mixer,
	sm8x16_scd_mau_dii gnable__audio_mixer
,
dSOC_SINGLE_EXT("IIR2 Eable_=Band5",cIIR2, BAND5,c1, ),
	sm8x16_scd_met_dii gnable__audio_mixer,
	sm8x16_scd_mau_dii gnable__audio_mixer
,

dSOC_SINGLE_MULTI_EXT("IIR1 Band1",cIIR1, BAND1,c255
0x,c5,
	sm8x16_scd_met_dii gban)_audio_mixer,
	sm8x16_scd_mau_dii gban)_audio_mixer
,
dSOC_SINGLE_MULTI_EXT("IIR1 Band2",cIIR1, BAND2,c255
0x,c5,
	sm8x16_scd_met_dii gban)_audio_mixer,
	sm8x16_scd_mau_dii gban)_audio_mixer
,
dSOC_SINGLE_MULTI_EXT("IIR1 Band3",cIIR1, BAND3,c255
0x,c5,
	sm8x16_scd_met_dii gban)_audio_mixer,
	sm8x16_scd_mau_dii gban)_audio_mixer
,
dSOC_SINGLE_MULTI_EXT("IIR1 Band4",cIIR1, BAND4,c255
0x,c5,
	sm8x16_scd_met_dii gban)_audio_mixer,
	sm8x16_scd_mau_dii gban)_audio_mixer
,
dSOC_SINGLE_MULTI_EXT("IIR1 Band5",cIIR1, BAND5,c255
0x,c5,
	sm8x16_scd_met_dii gban)_audio_mixer,
	sm8x16_scd_mau_dii gban)_audio_mixer
,
dSOC_SINGLE_MULTI_EXT("IIR2=Band1",cIIR2, BAND1,c255
0x,c5,
	sm8x16_scd_met_dii gban)_audio_mixer,
	sm8x16_scd_mau_dii gban)_audio_mixer
,
dSOC_SINGLE_MULTI_EXT("IIR2=Band2",cIIR2, BAND2,c255
0x,c5,
	sm8x16_scd_met_dii gban)_audio_mixer,
	sm8x16_scd_mau_dii gban)_audio_mixer
,
dSOC_SINGLE_MULTI_EXT("IIR2=Band3",cIIR2, BAND3,c255
0x,c5,
	sm8x16_scd_met_dii gban)_audio_mixer,
	sm8x16_scd_mau_dii gban)_audio_mixer
,
dSOC_SINGLE_MULTI_EXT("IIR2=Band4",cIIR2, BAND4,c255
0x,c5,
	sm8x16_scd_met_dii gban)_audio_mixer,
	sm8x16_scd_mau_dii gban)_audio_mixer
,
dSOC_SINGLE_MULTI_EXT("IIR2=Band5",cIIR2, BAND5,c255
0x,c5,
	sm8x16_scd_met_dii gban)_audio_mixer,
	sm8x16_scd_mau_dii gban)_audio_mixer
,

dSOC_SINGLE_EXT("COMP0 RX1", COMPANDER_1,cSM8X16_WCD_ARX1,c1, ),
	sm8x16_scd_monmpan)er_et_,csm8x16_mcd_ronmpan)er_st_
,

dSOC_SINGLE_EXT("COMP0 RX2", COMPANDER_1,cSM8X16_WCD_ARX2,c1, ),
	sm8x16_scd_monmpan)er_et_,csm8x16_mcd_ronmpan)er_st_
,
};sstatic bft rtombakrph_rimpedanc _et_(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	sft retu;	sunte32to zl, z;
}	bol(=ph_r
	struct mnc_cmlt:i_mixer_ontrol(=*m;
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	struct msm8x16_wcd_priv *mriv * snd_soc_codec_get_drvdata(codec);
	
sm = m(nruct mnc_cmlt:i_mixer_ontrol(=* (kontrol(->riv te_balue,

	
sph_r= Nsc>dshifu
		etu*= cd_pmbh_get_dimpedanc (&riv ->mbh_, &zl, &ze


if (eetu 
	pr_edebug(%%s NFaild =o cet_ mbh_ imped, __func__,


ir_edebug(%%s Nzl %u, z; %un",
__func__, czl, z;;
	euontrol(->alue .nterger.alue [0]=  ph_r=? z;  Nzl
	
seturn 0;
}

static bcnst cnruct mnd_skontrol(_newcimpedanc _detect_cotrol(s[]  =
	dSOC_SINGLE_EXT("HPHL Impedanc "
0x,cx,cUINT_BAX, ),
			tombakrph_rimpedanc _et_,sULL;
,
dSOC_SINGLE_EXT("HPHR Impedanc "
0x,c1,cUINT_BAX, ),
			tombakrph_rimpedanc _et_,sULL;
,
};sstatic bft rtombakret_dph_rtype(nruct mnd_skontrol(=*kontrol(,				pnruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_codec *codec = mnd_soc_ckontrol(codec (kontrol()
	struct msm8x16_wcd_priv *mriv * snd_soc_codec_get_drvdata(codec);
	struct mcd_pmbh_=*mbh_
		rf (g!riv  {
		dr_edebug(%%s Nsm8x16_-cd =riv te_bata(rissULL;n",
				__func__,


ipeturn N-ENVAL;
		}

smbh_== &riv ->mbh_

if (e!mbh_ {
		dr_edebug(%%s Nsbh_=ot rinitialize\n", __func__,

		return N-ENVAL;
		}

suontrol(->alue .nterger.alue [0]=  (u32)Nsbh_>iph_rtype

ir_edebug(%%s Nph_rtype  =%un",
__func__, csbh_>iph_rtype;
	
seturn 0;
}

static bcnst cnruct mnd_skontrol(_newcph_rtype_detect_cotrol(s[]  =
	dSOC_SINGLE_EXT("HPH Type"
0x,cx,cUINT_BAX, ),
	tombakret_dph_rtype,sULL;
,
};sstatic bcnst char *vbcnst crx_mix1ctext[]  =
		"ZERO"  "IIR1"  "IIR2", "RX1", "RX2", "RX3"	};sstatic bcnst char *vbcnst crx_mix2ctext[]  =
		"ZERO"  "IIR1"  "IIR2"	};sstatic bcnst char *vbcnst cec_gmuxctext[]  =
		"ZERO"  "ADC1"  "ADC2"  "ADC3",c"DMIC1"  "DMIC2"	};sstatic bcnst char *vbcnst cec_3gmuxctext[]  =
		"ZERO"  "DMIC3"	};sstatic bcnst char *vbcnst cec_4gmuxctext[]  =
		"ZERO"  "DMIC4"	};sstatic bcnst char *vbcnst cadc2gmuxctext[]  =
		"ZERO"  "INP2"  "INP3"	};sstatic bcnst char *vbcnst cextwpk_etext[]  =
		"Off",c"On"	};sstatic bcnst char *vbcnst cwsawpk_etext[]  =
		"ZERO"  "WSA"	};sstatic bcnst char *vbcnst crdac2gmuxctext[]  =
		"ZERO"  "RX2", "RX1"	};sstatic bcnst char *vbcnst cii g np1ctext[]  =
		"ZERO"  "DEC1"  "DEC2", "RX1", "RX2", "RX3"	};sstatic bcnst cnruct mnc_cenumcadc2genumc=
dSOC_ENUM_SINGLE(SND_SOC_NOPM, ),
		ARRAY_SIZEgadc2gmuxctext),cadc2gmuxctext);sstatic bcnst cnruct mnc_cenumcextwpk_eenumc=
dSOC_ENUM_SINGLE(SND_SOC_NOPM, ),
		ARRAY_SIZEgextwpk_etext),cextwpk_etext);sstatic bcnst cnruct mnc_cenumcwsawpk_eenumc=
dSOC_ENUM_SINGLE(SND_SOC_NOPM, ),
		ARRAY_SIZEgwsawpk_etext),cwsawpk_etext);ss/* RX1 MIX1 */static bcnst cnruct mnc_cenumcrx_mix1c np1charn_venumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DCONNDRX1_B1_TL,,			x,c6,crx_mix1ctext);sstatic bcnst cnruct mnc_cenumcrx_mix1c np2charn_venumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DCONNDRX1_B1_TL,,			3,c6,crx_mix1ctext);sstatic bcnst cnruct mnc_cenumcrx_mix1c np3charn_venumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DCONNDRX1_B2_TL,,			x,c6,crx_mix1ctext);ss/* RX1 MIX2 */static bcnst cnruct mnc_cenumcrx_mix2c np1charn_venumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DCONNDRX1_B3_TL,,			x,c3,crx_mix2ctext);ss/* RX2 MIX1 */static bcnst cnruct mnc_cenumcrx2_mix1c np1charn_venumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DCONNDRX2_B1_TL,,			x,c6,crx_mix1ctext);sstatic bcnst cnruct mnc_cenumcrx2_mix1c np2charn_venumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DCONNDRX2_B1_TL,,			3,c6,crx_mix1ctext);sstatic bcnst cnruct mnc_cenumcrx2_mix1c np3charn_venumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DCONNDRX2_B1_TL,,			x,c6,crx_mix1ctext);ss/* RX2 MIX2 */static bcnst cnruct mnc_cenumcrx2_mix2c np1charn_venumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DCONNDRX2_B3_TL,,			x,c3,crx_mix2ctext);ss/* RX3 MIX1 */static bcnst cnruct mnc_cenumcrx3_mix1c np1charn_venumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DCONNDRX3_B1_TL,,			x,c6,crx_mix1ctext);sstatic bcnst cnruct mnc_cenumcrx3_mix1c np2charn_venumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DCONNDRX3_B1_TL,,			3,c6,crx_mix1ctext);sstatic bcnst cnruct mnc_cenumcrx3_mix1c np3charn_venumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DCONNDRX3_B1_TL,,			x,c6,crx_mix1ctext);ss/* DEC */static bcnst cnruct mnc_cenumcec-1cmuxcenumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DCONNDTX_B1_TL,,			x,c6,cec_gmuxctext);sstatic bcnst cnruct mnc_cenumcec-2cmuxcenumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DCONNDTX_B1_TL,,			3,c6,cec_gmuxctext);sstatic bcnst cnruct mnc_cenumcec-3cmuxcenumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DTX3_MUX_TL,
0),
				ARRAY_SIZEgec_3gmuxctext),cec_3gmuxctext);sstatic bcnst cnruct mnc_cenumcec-4cmuxcenumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DTX4_MUX_TL,
0),
				ARRAY_SIZEgec_4gmuxctext),cec_4gmuxctext);sstatic bcnst cnruct mnc_cenumcrdac2gmuxcenumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_AIGITAL_CDC_DCONNDHPHR_DAC_TL,,			x,c3,crdac2gmuxctext);sstatic bcnst cnruct mnc_cenumcii 1c np1cmuxcenumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DCONNDEQ1_B1_TL,,			x,c6,cii g np1ctext);sstatic bcnst cnruct mnc_cenumcii 2c np1cmuxcenumc=
dSOC_ENUM_SINGLE(SM8X16_WCD_A_ADC_DCONNDEQ2_B1_TL,,			x,c6,cii g np1ctext);sstatic bcnst cnruct mnd_skontrol(_newcextwpk_emuxc=
dSOC_DAPM_ENUM("Ext Spk=Sitch  Mux",cextwpk_eenum);sstatic bcnst cnruct mnd_skontrol(_newcrx_mix1c np1cmuxc=
dSOC_DAPM_ENUM("RX1 MIX1 INP1 Mux",crx_mix1c np1charn_venum);sstatic bcnst cnruct mnd_skontrol(_newcrx_mix1c np2cmuxc=
dSOC_DAPM_ENUM("RX1 MIX1 INP2 Mux",crx_mix1c np2charn_venum);sstatic bcnst cnruct mnd_skontrol(_newcrx_mix1c np3cmuxc=
dSOC_DAPM_ENUM("RX1 MIX1 INP3 Mux",crx_mix1c np3charn_venum);sstatic bcnst cnruct mnd_skontrol(_newcrx2_mix1c np1cmuxc=
dSOC_DAPM_ENUM("RX2 MIX1 INP1 Mux",crx2_mix1c np1charn_venum);sstatic bcnst cnruct mnd_skontrol(_newcrx2_mix1c np2cmuxc=
dSOC_DAPM_ENUM("RX2 MIX1 INP2 Mux",crx2_mix1c np2charn_venum);sstatic bcnst cnruct mnd_skontrol(_newcrx2_mix1c np3cmuxc=
dSOC_DAPM_ENUM("RX2 MIX1 INP3 Mux",crx2_mix1c np3charn_venum);sstatic bcnst cnruct mnd_skontrol(_newcrx3_mix1c np1cmuxc=
dSOC_DAPM_ENUM("RX3 MIX1 INP1 Mux",crx3_mix1c np1charn_venum);sstatic bcnst cnruct mnd_skontrol(_newcrx3_mix1c np2cmuxc=
dSOC_DAPM_ENUM("RX3 MIX1 INP2 Mux",crx3_mix1c np2charn_venum);sstatic bcnst cnruct mnd_skontrol(_newcrx3_mix1c np3cmuxc=
dSOC_DAPM_ENUM("RX3 MIX1 INP3 Mux",crx3_mix1c np3charn_venum);sstatic bcnst cnruct mnd_skontrol(_newcrx1_mix2c np1cmuxc=
dSOC_DAPM_ENUM("RX1 MIX2 INP1 Mux",crx_mix2c np1charn_venum);sstatic bcnst cnruct mnd_skontrol(_newcrx2_mix2c np1cmuxc=
dSOC_DAPM_ENUM("RX2 MIX2 INP1 Mux",crx2_mix2c np1charn_venum);sstatic bcnst cnruct mnd_skontrol(_newctx_adc2gmuxc=
dSOC_DAPM_ENUM("ADC2 MUX Mux",cadc2genum);sstatic bnt msm8x16_mcd_rau_dec_genum(nruct mnd_skontrol(=*kontrol(,				      nruct mnd_sctl_elnmnalue f*uontrol( {
	struct mnd_soc_cdapm_widet_dlit c*wlit c 				dapm_kontrol(_et_dwlit (kontrol()
	struct mnd_soc_cdapm_widet_c*w*= clit ->widet_s[0];
struct mnd_soc_codec *codec = mw>icoec 
	struct mnc_cenumc*e= m(nruct mnc_cenumc*)kontrol(->riv te_balue,
	eunsignedbnt mec_gmux,cec_imator
	shar *vec_gnam =  ULL;
	shar *vwidet_dnam =  ULL;
	shar *vtemp
	eu16ctx_muxcctl_eg_
	su8cadc_dmic_sel  s0x0;	sft retu=  0;
rhar *vec_gnum
		rf (guontrol(->alue .enumerte_d.item[0]=> e->items){
		dev_err(codec->dev, "%%s NIvalid eenumcalue :=%dn",
				_func__, cuontrol(->alue .enumerte_d.item[0] 

	peturn N-ENVAL;
		}
sevcgmuxc=cuontrol(->alue .enumerte_d.item[0]
		rwidet_dnam =  knrundup(w>inam , 15, GFP_KERNEL


if (e!widet_dnam ){
		dev_err(codec->dev, "%%s Nfaild =o ccopy nruingn",
				_func__, 

	peturn N-ENOMEM
		}
stemp= mwidet_dnam ;
	sev_gnam =  nrusep(&widet_dnam  "% " 

	widet_dnam =  temp
	ef (e!ev_gnam ){
		dev_err(codec->dev, "%%s NIvalid eec_imator  =%sn",
				_func__, cw>inam  

	petu  =N-ENVAL;
			goo cout
		}

sev_gnum=  nrupbrk(ev_gnam  "%12"


if (eev_gnum=   ULL;){
		dev_err(codec->dev, "%%s NIvalid eDEC selecte\n", __func__,

		retu=  -ENVAL;
			goo cout
		}

setu=  knrutounteeev_gnum, 1x,c&ec_imator


if (eetu < 0){
		dev_err(codec->dev, "%%s NIvalid eec_imator  =%sn",
				_func__, cev_gnam )

	petu  =N-ENVAL;
			goo cout
		}

sev_edbg(w>idapm>dev, "%%s():mwidet_  =%seec_imator  =%u evcgmuxc=c%un",			
__func__, cw>inam , ec_imator,cec_gmux)
		rnitch (eec_imator
{
	case 01
		ase 02
			f (e(evcgmuxc== 4) || (evcgmuxc== 5)
{			adc_dmic_sel  s0x1;
rrlse 		p	adc_dmic_sel  s0x0;	s	reak;
	}efault:
		pev_err(codec->dev, "%%s NIvalid eDc_imator  =%un",
				_func__, cev_imator


iretu=  -ENVAL;
			goo cout
		}

stx_muxcctl_eg_c 			SM8X16_WCD_A_ADC_DTX1_MUX_TL, +m32 *(eec_imator - 1;
	
snd_soc_update_bits(codec,
 tx_muxcctl_eg_,s0x1,cadc_dmic_sel;
	
setu* snd_soc_cdapm_au_denum_double(kontrol( cuontrol(;
	
out
		kfree(widet_dnam )
	seturn 0etu;	}

#efainecSM8X16_WCD_ADEC_ENUM(xnam , xenum) \
{	.ifac =  SNDRV_TL,_ELEM_IFACE_MIXER, .nam =  xnam , \
	.info* snd_soc_cinfodenum_double, \
	.gtu* snd_soc_cdapm_et_denum_double, \
	.au_= Nsm8x16_wcd-rau_dec_genum, \
	.aiv te_balue,=  (unsignedblong)&xenum 

static bcnst cnruct mnd_skontrol(_newcec-1cmuxc 		SM8X16_WCD_ADEC_ENUM("DEC1 MUX Mux",cec-1cmuxcenum);sstatic bcnst cnruct mnd_skontrol(_newcec-2cmuxc 		SM8X16_WCD_ADEC_ENUM("DEC2 MUX Mux",cec-2cmuxcenum);sstatic bcnst cnruct mnd_skontrol(_newcec-3cmuxc=
dSOC_DAPM_ENUM("DEC3 MUX Mux",cec-3cmuxcenum);sstatic bcnst cnruct mnd_skontrol(_newcec-4cmuxc=
dSOC_DAPM_ENUM("DEC4 MUX Mux",cec-4cmuxcenum);sstatic bcnst cnruct mnd_skontrol(_newcrdac2gmuxc=
dSOC_DAPM_ENUM("RDAC2 MUX Mux",crdac2gmuxcenum);sstatic bcnst cnruct mnd_skontrol(_newcii 1c np1cmuxc=
dSOC_DAPM_ENUM("IIR1 INP1 Mux",cii 1c np1cmuxcenum);sstatic bcnst char *vbcnst cearctext[]  =
		"ZERO"  "Sitch ",	};sstatic bcnst cnruct mnc_cenumcearcenumc=
dSOC_ENUM_SINGLE(SND_SOC_NOPM, ), ARRAY_SIZEgearctext),cearctext);sstatic bcnst cnruct mnd_skontrol(_newcearra_omux[]  =
	dSOC_DAPM_ENUM("EAR_S"  earcenum)	};sstatic bcnst cnruct mnd_skontrol(_newcwsawpk_emux[]  =
	dSOC_DAPM_ENUM("WSA Spk=Sitch ",cwsawpk_eenum)	};sstatic bcnst cnruct mnd_skontrol(_newcii 2c np1cmuxc=
dSOC_DAPM_ENUM("IIR2 INP1 Mux",cii 2c np1cmuxcenum);sstatic bcnst char *vbcnst cph_rtext[]  =
		"ZERO"  "Sitch ",	};sstatic bcnst cnruct mnc_cenumcph_renumc=
dSOC_ENUM_SINGLE(SND_SOC_NOPM, ), ARRAY_SIZEgph_rtext),cph_rtext);sstatic bcnst cnruct mnd_skontrol(_newcph_lemux[]  =
	dSOC_DAPM_ENUM("HPHL",cph_renum)	};sstatic bcnst cnruct mnd_skontrol(_newcph_remux[]  =
	dSOC_DAPM_ENUM("HPHR",cph_renum)	};sstatic bcnst cnruct mnd_skontrol(_newcpk_remux[]  =
	dSOC_DAPM_ENUM("SPK",cph_renum)	};sstatic bcnst char *vbcnst clortext[]  =
		"ZERO"  "Sitch ",	};sstatic bcnst cnruct mnc_cenumclorenumc=
dSOC_ENUM_SINGLE(SND_SOC_NOPM, ), ARRAY_SIZEgph_rtext),cph_rtext);sstatic bcnst cnruct mnd_skontrol(_newclormux[]  =
	dSOC_DAPM_ENUM("LINE_OUT"  lorenum)	};sstatic boid msm8x16_mcd_ronec_genble__adc_blockstruct mnd_soc_codec *codec) 						bnt menble_ {
	struct msm8x16_wcd_priv *mcd_x16_= snd_soc_codec_get_drvdata(codec);
	
sev_edbg(odec->dev, "%s:=%\n", __func__, menble_ 
		rf (genble_ {
		dcd_x16_->adc_count++
			nd_soc_update_bits(codec,
					    SM8X16_WCD_A_AIGITAL_CDC_DAN_ADLK_TL,,					    0x2x,cxx2x)
			nd_soc_update_bits(codec,
					    SM8X16_WCD_A_AIGITAL_CDC_DIGIADLK_TL,,					    0x1x,cxx1x)
		 else i
		dcd_x16_->adc_count--;		df ((!cd_x16_->adc_count {
		d	nd_soc_update_bits(codec,
					    SM8X16_WCD_A_AIGITAL_CDC_DIGIADLK_TL,,					    0x1x,cxx0x)
				nd_soc_update_bits(codec,
					    SM8X16_WCD_A_AIGITAL_CDC_DAN_ADLK_TL,,						    0x2x,cxxx)
			}		}


static bnt msm8x16_mcd_ronec_genble__adcstruct mnd_soc_cdapm_widet_c*w,
	nruct mnd_skontrol(=*kontrol(,bnt mevent {
	struct mnd_soc_codec *codec = mw>icoec 
	su16cadc_eg_
	su8cinitbits_shifu
	
sev_edbg(odec->dev, "%%s=%\n", __func__, mevent 
	
sadc_eg_= mSM8X16_WCD_A_DNALOG_MTX_1_2_TEST_TL,_2
		rf (gw>deg_c== SM8X16_WCD_A_DNALOG_MTX_1_EN
{		initbits_shifu= m5;
rlse if (mgw>deg_c== SM8X16_WCD_A_DNALOG_MTX_2_EN) ||{		(gw>deg_c== SM8X16_WCD_A_DNALOG_MTX_3_EN)
{		initbits_shifu= m4;
rlse i
		dev_err(codec->dev, "%%s NError,civalid eadc0etgistern",
				_func__, 

	peturn N-ENVAL;
		}

snitch (eevent {
	case 0SND_SOC_DAPM_PRE_PMU
			sm8x16_wcd-ronec_genble__adc_blocksodec,
 1;
	erf (gw>deg_c== SM8X16_WCD_A_DNALOG_MTX_2_EN
{			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MMICB_1_TL,
0)x02
0)x02;
	er/*{		( EAddceclayNofc10msm=o ceive sufficiet rtim =or mtheboiltage{		( Eo cshoot up an) settle somthatmthebtxfecinit does ot {		( Ehappen when theb npu_=oiltagerissharngng rtoo much.{		( /{		usleep_rrnge(10000, 1x01x)
			nd_soc_update_bits(codec,
cadc_eg_, 1 <<minitbits_shifu,					1 <<minitbits_shifu;
	erf (gw>deg_c== SM8X16_WCD_A_DNALOG_MTX_1_EN
{			nd_soc_update_bits(codec,
					SM8X16_WCD_A_AIGITAL_CDC_DCONNDTX1_TL,,					)x03,cxx0x)
			lse if (mgw>deg_c== SM8X16_WCD_A_DNALOG_MTX_2_EN) ||{			gw>deg_c== SM8X16_WCD_A_DNALOG_MTX_3_EN)
{			nd_soc_update_bits(codec,
					SM8X16_WCD_A_AIGITAL_CDC_DCONNDTX2_TL,,					)x03,cxx0x)
			usleep_rrnge(CODEC_DELAY_1_MS, CODEC_DELAY_1_1_MS);	s	reak;
	}ase 0SND_SOC_DAPM_POSTRPMU
			/*{		( EAddceclayNofc12msm=beor ececassertng rthecinit{		( Eo creduc_=oe btx pop{		( /{	usleep_rrnge(12000, 1201x)
			nd_soc_update_bits(codec,
cadc_eg_, 1 <<minitbits_shifu,cxx0x)
			usleep_rrnge(CODEC_DELAY_1_MS, CODEC_DELAY_1_1_MS);	s	reak;
	}ase 0SND_SOC_DAPM_POSTRPMD
			sm8x16_wcd-ronec_genble__adc_blocksodec,
 0;
	erf (gw>deg_c== SM8X16_WCD_A_DNALOG_MTX_2_EN
{			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MMICB_1_TL,
0)x02
0)x00;
	erf (gw>deg_c== SM8X16_WCD_A_DNALOG_MTX_1_EN
{			nd_soc_update_bits(codec,
					SM8X16_WCD_A_AIGITAL_CDC_DCONNDTX1_TL,,					)x03,cxx02)
			lse if (mgw>deg_c== SM8X16_WCD_A_DNALOG_MTX_2_EN) ||{			gw>deg_c== SM8X16_WCD_A_DNALOG_MTX_3_EN)
{			nd_soc_update_bits(codec,
					SM8X16_WCD_A_AIGITAL_CDC_DCONNDTX2_TL,,					)x03,cxx02 
	
s	reak;
	}}	return 0;
}

static bnt msm8x16_mcd_ronec_genble__pk_epastruct mnd_soc_cdapm_widet_c*w,
				     nruct mnd_skontrol(=*kontrol(,bnt mevent {
	struct mnd_soc_codec *codec = mw>icoec 
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	
sev_edbg(w>iodec->dev, "%%s=%\=%sn",
__func__, mevent cw>inam  

	nitch (eevent {
	case 0SND_SOC_DAPM_PRE_PMU
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DIGITAL_CDC_DAN_ADLK_TL,, 0x1x,cxx1x)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MSPKR_PWRSTG_TL,
0)x01
0)x01)
			nitch (esm8x16_wcd->ioost_vnpion\ {
		dase 0BOOSTRSWITCH
			if (e!mm8x16_wcd->ipk_eoost_vstu 
				nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MSPKR_DAC_TL,,						)x1x,cxx1x)
				reak;
	}dase 0BOOSTRALWAYS:	}dase 0BOOSTRN)2FOREVER:				reak;
	}dase 0BYPASS_ALWAYS:{			nd_soc_update_bits(codec,
					SM8X16_WCD_A_ANALOG_MSPKR_DAC_TL,,					)x1x,cxx1x)
				reak;
	}default:
		pir_err(c%%s Nivalid eoost_ npion\:=%\n", __func__, 
				p	sm8x16_wcd->ioost_vnpion\ 
				reak;
	}d}			usleep_rrnge(CODEC_DELAY_1_MS, CODEC_DELAY_1_1_MS);	s	nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MSPKR_PWRSTG_TL,
0)xE0
0)xE0;
	erf (get_codec_version(msm8x16_wcd) <!= TOMBAK_1_0
{			nd_soc_update_bits(codec,
					SM8X16_WCD_A_ANALOG_MRX_EAR_TL,
0)x01
0)x01)
			reak;
	}ase 0SND_SOC_DAPM_POSTRPMU
			usleep_rrnge(CODEC_DELAY_1_MS, CODEC_DELAY_1_1_MS);	s	nitch (esm8x16_wcd->ioost_vnpion\ {
		dase 0BOOSTRSWITCH
			if (emm8x16_wcd->ipk_eoost_vstu 
				nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MSPKR_DRV_TL,,						)xEF
0)xEF 
				lse 		p		nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MSPKR_DAC_TL,,						)x1x,cxx0x)
				reak;
	}dase 0BOOSTRALWAYS:	}dase 0BOOSTRN)2FOREVER:				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ANALOG_MSPKR_DRV_TL,,					)xEF
0)xEF 
				reak;
	}dase 0BYPASS_ALWAYS:{			nd_soc_update_bits(codec,
					SM8X16_WCD_A_ANALOG_MSPKR_DAC_TL,, 0x1x,cxx0x)
				reak;
	}default:
		pir_err(c%%s Nivalid eoost_ npion\:=%\n", __func__, 
				p	sm8x16_wcd->ioost_vnpion\ 
				reak;
	}d}			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DDC_DRX3_B6_TL,
0)x01
0)x0x)
			nd_soc_update_bits(codec,
cw>deg_
0)x80
0)x80)
			reak;
	}ase 0SND_SOC_DAPM_PRE_PMD
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DDC_DRX3_B6_TL,
0)x01
0)x01)
			sm8x16_wcd->imue_bmask |= SPKR_P_DIGSABLE
	er/*{		( EAddc1msm=sleep=or mthebmue_Eo ctake effect{		( /{		usleep_rrnge(CODEC_DELAY_1_MS, CODEC_DELAY_1_1_MS);	s	nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MSPKR_DAC_TL,, 0x1x,cxx10;
	erf (get_codec_version(msm8x16_wcd) << CAJN)22_0
{			sm8x16_mcd_roost_vodu vst;ueceecodec,  SPK_PMD)
			nd_soc_update_bits(codec,
cw>deg_
0)x80
0)x0x)
			nitch (esm8x16_wcd->ioost_vnpion\ {
		dase 0BOOSTRSWITCH
			if (emm8x16_wcd->ipk_eoost_vstu 
				nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MSPKR_DRV_TL,,						)xEF
0)x69)
				reak;
	}dase 0BOOSTRALWAYS:	}dase 0BOOSTRN)2FOREVER:				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ANALOG_MSPKR_DRV_TL,,					)xEF
0)x69)
				reak;
	}dase 0BYPASS_ALWAYS:{			reak;
	}default:
		pir_err(c%%s Nivalid eoost_ npion\:=%\n", __func__, 
				p	sm8x16_wcd->ioost_vnpion\ 
				reak;
	}d}			reak;
	}ase 0SND_SOC_DAPM_POSTRPMD
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MSPKR_PWRSTG_TL,
0)xE0
0)x0x)
			usleep_rrnge(CODEC_DELAY_1_MS, CODEC_DELAY_1_1_MS);	s	nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MSPKR_PWRSTG_TL,
0)x01
0)x0x)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MSPKR_DAC_TL,, 0x1x,cxx0x)
			f (get_codec_version(msm8x16_wcd) <!= TOMBAK_1_0
{			nd_soc_update_bits(codec,
					SM8X16_WCD_A_ANALOG_MRX_EAR_TL,
0)x01
0)x0x)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DIGITAL_CDC_DAN_ADLK_TL,, 0x1x,cxx0x)
			f (get_codec_version(msm8x16_wcd) <>= CAJN)22_0
{			sm8x16_mcd_roost_vodu vst;ueceecodec,  SPK_PMD)
			reak;
	}}	return 0;
}

static bnt msm8x16_mcd_ronec_genble__dig_clkstruct mnd_soc_cdapm_widet_c*w,
				     nruct mnd_skontrol(=*kontrol(,bnt mevent {
	struct mnd_soc_codec *codec = mw>icoec 
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	struct msm8x96_wanc_cmach_ata(r*pata(r  ULL;
	
	pata(r  nd_soc_coardget_drvdata(codec)>icomponent.oard;
	
sev_edbg(w>iodec->dev, "%%s=event=%\=w>inam =%sn",
__func__, 				lvent cw>inam  

	nitch (eevent {
	case 0SND_SOC_DAPM_PRE_PMU
			sm8x16_mcd_ronec_genble__clock_blocksodec,
 1;
	ernd_soc_update_bits(codec,
cw>deg_
0)x80
0)x80)
			sm8x16_mcd_roost_vodu vst;ueceecodec,  SPK_PMU);	s	reak;
	}ase 0SND_SOC_DAPM_POSTRPMD
			f (emm8x16_wcd->irxbitas_countc== 0
{			nd_soc_update_bits(codec,
						SM8X16_WCD_A_DIGITAL_CDC_DIGIADLK_TL,,						)x80
0)x0x)
		}	return 0;
}

static bnt msm8x16_mcd_ronec_genble__dmicstruct mnd_soc_cdapm_widet_c*w,
	nruct mnd_skontrol(=*kontrol(,bnt mevent {
	struct mnd_soc_codec *codec = mw>icoec 
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	su8  dmic_clkgen
	su16cdmic_clkgeg_
	ss32 *dmic_clkgcnu;	sunsignedbnt memic;	sft retu;	shar *vec_gnum=  nrupbrk(w>inam , %12"



if (eev_gnum=   ULL;){
		dev_err(codec->dev, "%%s NIvalid eDMICn", __func__,

		return N-ENVAL;
		}

setu=  knrutounteeev_gnum, 1x,c&emic


if (eetu < 0){
		dev_err(codec->dev, 				%%s NIvalid eDMIC linecon thebodec-n", __func__,

		return N-ENVAL;
		}

snitch (eemic
{
	case 01
		ase 02
			dmic_clkgen  s0x01;
rrdmic_clkgcnu== &emm8x16_wcd->idmic_1_2_clkgcnu);
rrdmic_clkgeg_= mSM8X16_WCD_A_DDC_DCLK_DMIC_B1_TL,;		dev_edbg(odec->dev, 				%%s()=event=%\=DMIC%\=dmic_1_2_clkgcnu=%dn",
				_func__, clvent c=dmic, *dmic_clkgcnu);	s	reak;
	}efault:
		pev_err(codec->dev, "%%s NIvalid eDMIC Selecton(n", __func__,

		return N-ENVAL;
		}

snitch (eevent {
	case 0SND_SOC_DAPM_PRE_PMU
			(*dmic_clkgcnu)++
			f (e*dmic_clkgcnu=== 1;{
		d	nd_soc_update_bits(codec,
cdmic_clkgeg_,						)x0E,cxx02)
				nd_soc_update_bits(codec,
cdmic_clkgeg_,						dmic_clkgen, dmic_clkgen)
			}		if (eemic=== 1;{			nd_soc_update_bits(codec,
				SM8X16_WCD_A_ADC_DTX1_DMIC_TL,
0)x07
0)x01)
			f (eemic=== 2;{			nd_soc_update_bits(codec,
				SM8X16_WCD_A_ADC_DTX2_DMIC_TL,
0)x07
0)x01)
			reak;
	}ase 0SND_SOC_DAPM_POSTRPMD
			(*dmic_clkgcnu)--;		df ((*dmic_clkgcnu=c== 0
{			nd_soc_update_bits(codec,
cdmic_clkgeg_,						dmic_clkgen, 0)
			reak;
	}}	return 0;
}

static bbol(=sm8x16_wcd_pus_bmbstruct mnd_soc_codec *codec) {
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	
sf (get_codec_version(msm8x16_wcd) << CAJN))		return Nruce;
rlse 		return Nfase 
}

static boid msm8x16_mcd_rnt_dauto_zeroingstruct mnd_soc_codec *codec) 						bol(=enble_ {
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	
sf (get_codec_version(msm8x16_wcd) << CONGA {
		df ((enble_ {	er/*{			( EStu autozeroing=or mspecial headnt_ detecton( an){			( Ebuttonm=o cwork.{			( /{			nd_soc_update_bits(codec,
					SM8X16_WCD_A_ANALOG_MMICB_2_EN,					)x18,cxx10;
	erlse 		p	nd_soc_update_bits(codec,
					SM8X16_WCD_A_ANALOG_MMICB_2_EN,					)x18,cxx0x)
			 else i
		dr_edebug(%%s NAuto Zeroing=is ot  eq;uiredbfrom CONGAn",
					_func__, 

	}}

static boid msm8x16_mtrim_btngeg_struct mnd_soc_codec *codec) {
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	
sf (get_codec_version(msm8x16_wcd) <== TOMBAK_1_0
i
		dr_edebug(%%s NThiseecvice needm=o cbe trimme\n", __func__,

		r/*{		( ECalculte_boe btrimralue,ror meach ecvice us_d{		( Etillrisshomesbnt producton( by ar dwar bteam{		( /{		nd_soc_update_bits(codec,
					SM8X16_WCD_A_ANALOG_MSE_DACCESS,					)xA5
0xxA5)
			nd_soc_update_bits(codec,
					SM8X16_WCD_A_DNALOG_MTRIM_TLRL2,					)xFF
0)x3x)
		 else i
		dr_edebug(%%s NThiseecvice isetrimme\ at ATEn", __func__,

		}}

tatic bnt msm8x16_mcd_renble__extwmbsocurcestruct mnd_soc_codec *codec) 						bbbbbol(=urn _on {
	sft retu=  0;
rtatic bnt mcount
	
sev_edbg(odec->dev, "%%s=urn _on:=%\mcount:=%\n", __func__, =urn _on 				count 

if (eurn _on {
		df ((!count {
		d	etu* snd_soc_cdapm_or cerenble__pin(&odec->deapm 					"MICBIAS_REGULATOR")
				nd_soc_udapm_sync(&odec->deapm)
			}		icount++
		 else i
		df ((countc> 0
{			count--;		df ((!count {
		d	etu* snd_soc_cdapm_disble__pin(&odec->deapm 					"MICBIAS_REGULATOR")
				nd_soc_udapm_sync(&odec->deapm)
			}		}

if (eetu 
	pev_err(codec->dev, "%%s NFaild =o c%s=external micitas ocurcen",
				_func__, curn _on ? "enble_" : "disble_d")
		lse 		pev_edbg(odec->dev, "%%s N%s=external micitas ocurcen",
				__func__, =urn _on ? "Eable_d" : "Disble_d")
		seturn 0etu;	}

tatic bnt msm8x16_mcd_ronec_genble__micitasstruct mnd_soc_cdapm_widet_c*w,
	nruct mnd_skontrol(=*kontrol(,bnt mevent {
	struct mnd_soc_codec *codec = mw>icoec 
	struct msm8x16_wcd_priv *msm8x16_wcd = 
				nd_soc_uodec_get_drvdata(codec);
	su16cmici_nt geg_
	shar *vnterrnal1ctext= m"Iterrnal1"
	shar *vnterrnal2ctext= m"Iterrnal2"
	shar *vnterrnal3ctext= m"Iterrnal3"
	shar *vexternal2ctext= m"Exerrnal2"
	shar *vexternalctext= m"Exerrnal"
	sbol(=sicitas2
	
sev_edbg(odec->dev, "%%s=%\n", __func__, mevent 
	snitch (ew>deg_ {
	case 0SM8X16_WCD_A_DNALOG_MMICB_1_EN:	case 0SM8X16_WCD_A_DNALOG_MMICB_2_EN:	c	mici_nt geg_= mSM8X16_WCD_A_DNALOG_MMICB_1_INT_RBIAS;	s	reak;
	}efault:
		pev_err(codec->dev, 				%%s NError,civalid emicitas etgister0)x%xn",
				_func__, cw>ieg_ 
		return N-ENVAL;
		}

ssicitas2= m(nd_soc_ueakdcodec,
cSM8X16_WCD_A_DNALOG_MMICB_2_EN) &0)x80)
		nitch (eevent {
	case 0SND_SOC_DAPM_PRE_PMU
			f (enrunnru(w>inam , nterrnal1ctext, nrulen(w>inam )) {
		d	f (get_codec_version(msm8x16_wcd) <>= CAJN) 
				nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MTX_1_2_ATEST_TL,_2,						)x02,cxx02)
				nd_soc_update_bits(codec,
cmici_nt geg_
0)x80
0)x80)
			 else if (enrunnru(w>inam , nterrnal2ctext, nrulen(w>inam )) {
		d	nd_soc_update_bits(codec,
cmici_nt geg_
0)x1x,cxx1x)
				nd_soc_update_bits(codec,
cw>deg_
0)x6x,cxx0x)
			 else if (enrunnru(w>inam , nterrnal3ctext, nrulen(w>inam )) {
		d	nd_soc_update_bits(codec,
cmici_nt geg_
0)x2,cxx2;
	er/*{		( Epdate_mSM8X16_WCD_A_DNALOG_MTX_1_2_ATEST_TL,_2{		( Eor mexternal itas only, ot  or mexternal2.{		 /{		 else if (e!nrunnru(w>inam , external2ctext, nrulen(w>inam )) &&						nrunnru(w>inam , externalctext,							nrulen(w>inam )) {
		d	nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MTX_1_2_ATEST_TL,_2,						)x02,cxx02)
			}		if (e!nrunnru(w>inam , externalctext, nrulen(w>inam )) 		p	nd_soc_update_bits(codec,
					SM8X16_WCD_A_ANALOG_MMICB_1_EN,cxx05
0xx04;
	erf (gw>deg_c== SM8X16_WCD_A_DNALOG_MMICB_1_EN
{			sm8x16_mcd_rontfigure_capcodec,
 tuce,=sicitas2 
	
s	reak;
	}ase 0SND_SOC_DAPM_POSTRPMU
			f (get_codec_version(msm8x16_wcd) <<= TOMBAK_2_0
{			usleep_rrnge(20000, 201xx)
			f (gnrunnru(w>inam , nterrnal1ctext, nrulen(w>inam )) {
		d	nd_soc_update_bits(codec,
cmici_nt geg_
0)x40
0)x40)
			 else if (enrunnru(w>inam , nterrnal2ctext,  nrulen(w>inam )) {
		d	nd_soc_update_bits(codec,
cmici_nt geg_
0)x08,cxx08)
				sm8x16_wot ifier_oallcodec,
						CD_AEVENT_POSTRMICBIAS_2_N) 
			 else if (enrunnru(w>inam , nterrnal3ctext, 3x) {
		d	nd_soc_update_bits(codec,
cmici_nt geg_
0)x01
0)x01)
			 else if (enrunnru(w>inam , external2ctext, nrulen(w>inam )) {
		d	sm8x16_wot ifier_oallcodec,
						CD_AEVENT_POSTRMICBIAS_2_N) 
			 			reak;
	}ase 0SND_SOC_DAPM_POSTRPMD
			f (gnrunnru(w>inam , nterrnal1ctext, nrulen(w>inam )) {
		d	nd_soc_update_bits(codec,
cmici_nt geg_
0)xC0
0)x40)
			 else if (enrunnru(w>inam , nterrnal2ctext, nrulen(w>inam )) {
		d	sm8x16_wot ifier_oallcodec,
						CD_AEVENT_POSTRMICBIAS_2_NFF 
			 else if (enrunnru(w>inam , nterrnal3ctext, 3x) {
		d	nd_soc_update_bits(codec,
cmici_nt geg_
0)x2,cxxx)
			}else if (enrunnru(w>inam , external2ctext, nrulen(w>inam )) {
		d	/*{			( Esen emicitas urn Noff=event=o csbh_=div er0an) then{			( Ebeak;, as ot needEo cst_cMICB_1_EN etgister.{			( /{			sm8x16_wot ifier_oallcodec,
						CD_AEVENT_POSTRMICBIAS_2_NFF 
				reak;
	}d}			f (gw>deg_c== SM8X16_WCD_A_DNALOG_MMICB_1_EN
{			sm8x16_mcd_rontfigure_capcodec,
 fase ,=sicitas2 
			reak;
	}}	return 0;
}

static boid mtx_hpfronrner_freq_oallbak_struct mwork_truct m*work {
	struct meclayed_work *hpfreclayed_work
	struct mhpfrwork *hpfrwork
	struct msm8x16_wcd_priv *msm8x16_wcd ;	struct mnd_soc_codec *codec ;	eu16ctx_muxcctl_eg_
	su8chpfrout_of_freq
	
sphfreclayed_work=  toreclayed_work(work ;
sphfrwork=  ontrainer_of(phfreclayed_work, truct mhpfrwork, dwork ;
ssm8x16_wcd = shpfrwork->sm8x16_wcd ;	sodec = mhpfrwork->sm8x16_wcd >icoec 
	shpfrout_of_freq= mhpfrwork->tx_hpfrout_of_freq
	
stx_muxcctl_eg_c  SM8X16_WCD_A_ADC_DTX1_MUX_TL, +{			(hpfrwork->dc_imator - 1;( E32
	
sev_edbg(odec->dev, "%%s():mdc_imator %uchpfrout_of_freq0)x%xn",
			__func__, mhpfrwork->dc_imator, (unsignedbnt )hpfrout_of_freq)
		nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MTX_1_2_TXFEDCLKDIV,cxxFF
0)x51;
	
snd_soc_update_bits(codec,
 tx_muxcctl_eg_,s0x30,chpfrout_of_freq0<< 4;
	

s
#efainec TX_MUX_TL,_CUT_NFF_FREQ_MASK	0x30
#efainec CFMMIN_3DB_4HZ			)x0
#efainec CFMMIN_3DB_75HZ		xx1
#efainec CFMMIN_3DB_150HZ		xx2

tatic bnt msm8x16_mcd_ronec_gst_dii gain_struct mnd_soc_cdapm_widet_c*w,
		nruct mnd_skontrol(=*kontrol(,bnt mevent {
	struct mnd_soc_codec *codec = mw>icoec 
	snt malue,=  0,ceg_
			nitch (eevent {
	case 0SND_SOC_DAPM_POSTRPMU
			f (gw>dshifuc== 0
{			eg_c  SM8X16_WCD_A_ADC_DIIR1_GAIN_B1_TL,
	erlse  f (gw>dshifuc== 1
{			eg_c  SM8X16_WCD_A_ADC_DIIR2_GAIN_B1_TL,
	eralue,=  nd_soc_ueakdcodec,
ceg_ 
		rnd_soc_uwritecodec,
ceg_, alue,

	s	reak;
	}efault:
		pr_err(c%%s Nevent===%\mot  expecte\n", __func__, mevent 
	s}	return 0;
}

static bnt msm8x16_mcd_ronec_genble__decstruct mnd_soc_cdapm_widet_c*w,
	nruct mnd_skontrol(=*kontrol(,bnt mevent {
	struct mnd_soc_codec *codec = mw>icoec 
	struct msm8x96_wanc_cmach_ata(r*pata(r  ULL;
	eunsignedbnt mec_imator
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	shar *vec_gnam =  ULL;
	shar *vwidet_dnam =  ULL;
	shar *vtemp
	eft retu=  0,bn;	eu16cec_grest_deg_, tx_vl(cotl_eg_,stx_muxcctl_eg_
	su8cec_ghpfrout_of_freq
	eft roffstu;	shar *vec_gnum
	
	pata(r  nd_soc_coardget_drvdata(codec)>icomponent.oard;
	sev_edbg(odec->dev, "%%s=%\n", __func__, mevent 
	
swidet_dnam =  knrundup(w>inam , 15, GFP_KERNEL


if (e!widet_dnam )
	peturn N-ENOMEM
		temp= mwidet_dnam ;
	sev_gnam =  nrusep(&widet_dnam  "% " 

	widet_dnam =  temp
	ef (e!ev_gnam ){
		dev_err(codec->dev, 				%%s NIvalid eec_imator  =%sn",
__func__, cw>inam  
		retu=  -ENVAL;
			goo cout
		}

sec_gnum=  nrupbrk(ev_gnam , %1234"


if (eev_gnum=   ULL;){
		dev_err(codec->dev, "%%s NIvalid eDc_imatorn", __func__,

		retu=  -ENVAL;
			goo cout
		}

setu=  knrutounteeev_gnum, 1x,c&ec_imator


if (eetu < 0){
		dev_err(codec->dev, 				%%s NIvalid eec_imator  =%sn",
__func__, cev_gnam )
		retu=  -ENVAL;
			goo cout
		}

sec_edbg(odec->dev, 			%%s():mwidet_  =%seec_dnam =  %seec_imator  =%un",
__func__, 			w>inam , ec__nam , ec_imator)
		rf (gw>deg_c== SM8X16_WCD_A_DDC_DCLK_TX_CLK_EN_B1_TL,){
		dev_grest_deg_= mSM8X16_WCD_A_DDC_DCLK_TX_RESET_B1_TL,
	eroffstu=  0;
r else i
		dev_err(codec->dev, "%%s NError,civonrret mec-n", __func__,

		retu=  -ENVAL;
			goo cout
		}

stx_vl(cotl_eg_c  SM8X16_WCD_A_ADC_DTX1_VOL_TL,_CFG +{			m32 *(eec_imator - 1;
	stx_muxcctl_eg_c  SM8X16_WCD_A_ADC_DTX1_MUX_TL, +{			 m32 *(eec_imator - 1;
	
snitch (eevent {
	case 0SND_SOC_DAPM_PRE_PMU
			f (eec_imator  = 3 || ec_imator  = 4 {
		d	nd_soc_update_bits(codec,
					SM8X16_WCD_A_DDC_DCLK_WSA_VI_B1_TL,,					)xFF
0)x5)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_DDC_DTX1_DMIC_TL, +{					(dc_imator - 1;( E0x2x,cxx7,cxx2;
	er	nd_soc_update_bits(codec,
					SM8X16_WCD_A_DDC_DTX1_DMIC_TL, +{					(dc_imator - 1;( E0x2x,cxx7,cxx2;
	er}	er/* Eable_ble_=TX digital mue_E /{		nd_soc_update_bits(codec,
 tx_vl(cotl_eg_,s)x01
0)x01)
			or m(i=  0; i < NUM_DECIMATORS; i++ {
		d	f (gec_imator  = i + 1
{				sm8x16_wcd->iev_gactive[i]  =ruce;
r	}

s	ec_ghpfrout_of_freq=  nd_soc_ueakdcodec,
ctx_muxcctl_eg_ 
	
s	ec_ghpfrout_of_freq=  (ec_ghpfrout_of_freq=&0)x3x) >>m4;

s	tx_hpfrwork[dc_imator - 1].tx_hpfrout_of_freqc 				dc_ghpfrout_of_freq
				f (eec__hpfrout_of_freqc!= CFMMIN_3DB_150HZ {
			d	/*cst_coutNofcfreqco cCFMMIN_3DB_150HZ (xx1);( /{			nd_soc_update_bits(codec,
 tx_muxcctl_eg_,s0x30,
					bbbbCFMMIN_3DB_150HZ << 4;
	}d}			nd_soc_update_bits(codec,
					SM8X16_WCD_A_DNALOG_MTX_1_2_TXFEDCLKDIV,					)xFF
0)x42 
	
s	reak;
	}ase 0SND_SOC_DAPM_POSTRPMU
			/*cenble_ HPFE /{		nd_soc_update_bits(codec,
 tx_muxcctl_eg_c
0)x08,cxx0x)
			if (eux_hpfrwork[dc_imator - 1].tx_hpfrout_of_freqc! 
				CFMMIN_3DB_150HZ {
			d	schedue__delayed_work(&ux_hpfrwork[dc_imator - 1].dwork,
					msecs_to_jiffies(30x);
	er}	er/* applyboe bdigital ain_ after0oe bdc_imator iscenble_d /{		f (mgw>dshifu; < ARRAY_SIZEgux_digitalgain__eg_  		p	nd_soc_uwritecodec,

				bbux_digitalgain__eg_[w>dshifuc+roffstu]

				bbnd_soc_ueakdcodec,

				bbux_digitalgain__eg_[w>dshifuc+roffstu])
				bb)
			f (gpata(->lbvodu  {
		d	r_edebug(%%s Nloopbak_ odu  unmue_Eoe bDECn",
								_func__, 

	p	nd_soc_update_bits(codec,
 tx_vl(cotl_eg_,s)x01
0)x0x)
			}			reak;
	}ase 0SND_SOC_DAPM_PRE_PMD
			nd_soc_update_bits(codec,
 tx_vl(cotl_eg_,s)x01
0)x01)
			msleep(2x)
			nd_soc_update_bits(codec,
 tx_muxcctl_eg_,s0x08,cxx08)
			canc l_delayed_work_sync(&ux_hpfrwork[dc_imator - 1].dwork)
			reak;
	}ase 0SND_SOC_DAPM_POSTRPMD
			nd_soc_update_bits(codec,
cdc_grest_deg_, 1 <<mw>dshifu
				1 <<mw>dshifu)
			nd_soc_update_bits(codec,
 dc_grest_deg_, 1 <<mw>dshifu
cxxx)
			nd_soc_update_bits(codec,
 tx_muxcctl_eg_,s0x08,cxx08)
			nd_soc_update_bits(codec,
 tx_muxcctl_eg_,s0x30,
			eux_hpfrwork[dc_imator - 1].tx_hpfrout_of_freq) << 4;
	}dnd_soc_update_bits(codec,
 tx_vl(cotl_eg_,s)x01
0)x0x)
			or m(i=  0; i < NUM_DECIMATORS; i++ {
		d	f (gec_imator  = i + 1
{				sm8x16_wcd->iev_gactive[i]  =fase 
}		}		if (eec_imator  = 3 || ec_imator  = 4 {
		d	nd_soc_update_bits(codec,
					SM8X16_WCD_A_DDC_DCLK_WSA_VI_B1_TL,,					)xFF
0)xx)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_DDC_DTX1_DMIC_TL, +{					(dc_imator - 1;( E0x2x,cxx7,cxxx)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_DDC_DTX1_DMIC_TL, +{					(dc_imator - 1;( E0x2x,cxx7,cxxx)
			}			reak;
	}}
out
		kfree(widet_dnam )
	seturn 0etu;	}

tatic bnt msm8x9xxmcd_ronec_genble__vdd_pk_rstruct mnd_soc_cdapm_widet_c*w,
				       nruct mnd_skontrol(=*kontrol(,bnt mevent {
	struct mnd_soc_codec *codec = mw>icoec 
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	sft retu=  0;

if (e!mm8x16_wcd->iextwpk_eoost_vstu i
		dev_edbg(odec->dev, "%%s Nextwoost_ ot  supported/disble_dn",
									_func__, 

	peturn N0;		}
sev_edbg(odec->dev, "%%s N%s=%\n", __func__, mw>inam , event 
	snitch (eevent {
	case 0SND_SOC_DAPM_PRE_PMU
			f (emm8x16_wcd->ipk_rvd_eg_ {
	c	retu=  eg_ulteorgenble_emm8x16_wcd->ipk_rvd_eg_ ;		d	f (grtu 
				ev_err(codec->dev, 						%%sNFaild =o cenble_ pk_rvd eg_=%sn",
						_func__, cSM8X9XX_VDDMSPKDRV_NAME 
			 			reak;
	}ase 0SND_SOC_DAPM_POSTRPMD
			f (gmm8x16_wcd->ipk_rvd_eg_ {
	c	retu=  eg_ulteorgdisble_emm8x16_wcd->ipk_rvd_eg_ ;		d	f (grtu 
				ev_err(codec->dev, 						%%s NFaild =o cdisble_ pk_rvd_eg_=%sn",
						_func__, cSM8X9XX_VDDMSPKDRV_NAME 
			 			reak;
	}}	return 0;
}

static bnt msm8x16_mcd_ronec_gontfig_companderstruct mnd_soc_codec *codec) 						nt mnterrp_n,bnt mevent {
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	
sev_edbg(odec->dev, "%%s Nevent=%\=shifuc%d menble_d=%dn",
			_func__, clvent cnterrp_n,
		sm8x16_wcd->icompgenble_d[nterrp_n];
	
s/vbcnmpander=is ot  enble_d= /{	f (e!mm8x16_wcd->icompgenble_d[nterrp_n];i
		df ((nterrp_n < SM8X16_WCD_ARX3)		d	f (get_codec_version(msm8x16_wcd) <>= DIANGU 
				nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MRX_COM_BIAS_DAC,						)x08
0)x0x)
			eturn N0;		}

	nitch (esm8x16_wcd->icompgenble_d[nterrp_n];i
		ase 0COMPANDER_1
			f (gSND_SOC_DAPM_EVENT_ONeevent  {
		d	f (get_codec_version(msm8x16_wcd) <>= DIANGU 
				nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MRX_COM_BIAS_DAC,						)x08
0)x08)
				/* Eable_ Cnmpander=Clock( /{			nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DCOMP0_B2_TL,,0)x0F
0)xx9)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_DDC_DCLK_RX_B2_TL,,0)x01
0)x01)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DCOMP0_B1_TL,,					1 <<minerrp_n,b1 <<minerrp_n)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DCOMP0_B3_TL,,0)xFF
0)xx1)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DCOMP0_B2_TL,,0)xFx,cxx50)
				/* add=sleep=or mcnmpander=o cst_tle  /{			usleep_rrnge(1000,b110x)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DCOMP0_B3_TL,,0)xFF
0)x28)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DCOMP0_B2_TL,,0)xFx,cxxBx)
			i	/* Eable_ Cnmpander=GPIO  /{			f (gmm8x16_wcd->iodec_vph_rcompggpio
{				sm8x16_wcd->iodec_vph_rcompggpio(1)
			 else if (eSND_SOC_DAPM_EVENT_OFFeevent  {
		d	/* Disble_ Cnmpander=GPIO  /{			f (gmm8x16_wcd->iodec_vph_rcompggpio
{				sm8x16_wcd->iodec_vph_rcompggpio(x)
			i	nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DCOMP0_B2_TL,,0)x0F
0)xx5)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_DDC_DCOMP0_B1_TL,,					1 <<minerrp_n,bx)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DCLK_RX_B2_TL,,0)x01
0)x0x)
			}			reak;
	}efault:
		pev_edbg(odec->dev, "%%s NIvalid ecnmpander=%\n", __func__, 
				sm8x16_wcd->icompgenble_d[nterrp_n];
			reak;
	}}
		seturn 0;
}

static bnt msm8x16_mcd_ronec_genble__nterrpolteorstruct mnd_soc_cdapm_widet_c*w,
						 nruct mnd_skontrol(=*kontrol(,							bnt mevent {
	struct mnd_soc_codec *codec = mw>icoec 
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	
sev_edbg(odec->dev, "%%s=%\=%sn",
__func__, mevent cw>inam  

		nitch (eevent {
	case 0SND_SOC_DAPM_POSTRPMU
			sm8x16_mcd_ronec_gontfig_compandersodec,
cw>dshifu
cevent 
	sr/* applyboe bdigital ain_ after0oe bnterrpolteor iscenble_d /{		f (mgw>dshifu; < ARRAY_SIZEgrx_digitalgain__eg_  		p	nd_soc_uwritecodec,

				bbrx_digitalgain__eg_[w>dshifu]

				bbnd_soc_ueakdcodec,

				bbrx_digitalgain__eg_[w>dshifu])
				bb)
			reak;
	}ase 0SND_SOC_DAPM_POSTRPMD
			sm8x16_wcd-ronec_gontfig_compandersodec,
cw>dshifu
cevent 
	srnd_soc_update_bits(codec,
				SM8X16_WCD_A_ADC_DCLK_RX_RESET_TL,,				1 <<mw>dshifu
c1 <<mw>dshifu)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_ADC_DCLK_RX_RESET_TL,,				1 <<mw>dshifu
cxxx)
			/*{		( Edisble_ thebmue_Eenble_d=durng rthecPMDNofcthiseecvice
		( /{		f (mgw>dshifuc== 0
 &&				gmm8x16_wcd->imue_bmask & HPHL_P_DIGSABLE) {
		d	r_edebug(%disbleng rHPHLbmue_n",)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX1_B6_TL,
0)x01
0)x0x)
				f (get_codec_version(msm8x16_wcd) <>= CAJN) 
				nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MRX_HPH_BIAS_CNP,						)xFx,cxx2x)
				sm8x16_wcd->imue_bmask &= ~(HPHL_P_DIGSABLE)
			 else if (egw>dshifuc== 1
 &&					gmm8x16_wcd->imue_bmask & HPHR_P_DIGSABLE) {
		d	r_edebug(%disbleng rHPHRbmue_n",)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX2_B6_TL,
0)x01
0)x0x)
				f (get_codec_version(msm8x16_wcd) <>= CAJN) 
				nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MRX_HPH_BIAS_CNP,						)xFx,cxx2x)
				sm8x16_wcd->imue_bmask &= ~(HPHR_P_DIGSABLE)
			 else if (egw>dshifuc== 2
 &&					gmm8x16_wcd->imue_bmask & SPKR_P_DIGSABLE) {
		d	r_edebug(%disbleng rSPKRbmue_n",)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX3_B6_TL,
0)x01
0)x0x)
				sm8x16_wcd->imue_bmask &= ~(SPKR_P_DIGSABLE)
			 else if (egw>dshifuc== 0
 &&					gmm8x16_wcd->imue_bmask & EAR_P_DIGSABLE) {
		d	r_edebug(%disbleng rEARbmue_n",)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX1_B6_TL,
0)x01
0)x0x)
				sm8x16_wcd->imue_bmask &= ~(EAR_P_DIGSABLE)
			}		}	return 0;
}

s
/* Thecetgister0address isethecsam =as otherbodec- somit can us_cetsmgr( /{tatic bnt msm8x16_mcd_ronec_genble__rxbitasstruct mnd_soc_cdapm_widet_c*w,
	nruct mnd_skontrol(=*kontrol(,bnt mevent {
	struct mnd_soc_codec *codec = mw>icoec 
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	
sev_edbg(odec->dev, "%%s=%\n", __func__, mevent 
	
snitch (eevent {
	case 0SND_SOC_DAPM_PRE_PMU
			sm8x16_mcd_>irxbitas_count++
			f (emm8x16_wcd->irxbitas_countc== 1;{
		d	nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MRX_COM_BIAS_DAC,						)x80
0)x80)
				nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MRX_COM_BIAS_DAC,						)x01
0)x01)
			 			reak;
	}ase 0SND_SOC_DAPM_POSTRPMD
			sm8x16_wcd->irxbitas_count--;		df ((mm8x16_wcd->irxbitas_countc== 0
{
		d	nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MRX_COM_BIAS_DAC,						)x01
0)x0x)
				nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MRX_COM_BIAS_DAC,						)x80
0)x0x)
			}			reak;
	}}
sev_edbg(odec->dev, "%%s rxbitas_countc==%dn",
				_func__, cmm8x16_wcd->irxbitas_count)
	seturn 0;
}

static bunte32_t cd_pet_dimpedanc balue,(unte32_t imped {
	sft ri
	
sor m(i=  0; i < ARRAY_SIZEgcd_pimpedbalu) - 1; i++ {
		df ((nmped<>= cd_pimpedbalu[i] &&				nmped<< cd_pimpedbalu[i + 1])
			reak;
	}}

	r_edebug(%%s Nselecte\ impedanc malue,=  %dn",
			__func__, mwd_pimpedbalu[i])
	seturn 0wd_pimpedbalu[i]
}

soid mwd_pimpedbontfigstruct mnd_soc_codec *codec) 				unte32_t imped,bbol(=st_dain_ {
	sunte32_t alue,
	ent mcoec_version(
	struct msm8x16_wcd_priv *msm8x16_wcd = 
				nd_soc_uodec_get_drvdata(codec);
	
salue,=  cd_pet_dimpedanc balue,(imped ;

if (ealue,=< cd_pimpedbalu[0]
i
		dr_edebug(%%s, detecte\ impedanc miseless than 4 Ohmn",
				__func__,)
			eturn 
	}}

	coec_version(=  et_codec_version(msm8x16_wcd) ;

if (est_dain_ i
		dnitch (eodec_version( {
		dase 0TOMBAK_1_0:		dase 0TOMBAK_2_0:		dase 0CONGA:		d	/*				_* Fr m32Ohm load0an) higherbloads, Stu 0x19E				_* its 5=o c1 (POS_0_DBDIG). Fr mloadsmlower				_* than 32Ohm (suh (as 16Ohm load), Stu 0x19E				_* its 5=o c0 (POS_M4P5_DBDIG)				_*/{			f (galue,=>= 32 
				nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MRX_EAR_TL,
						)x2x,cxx2x)
				lse 		p		nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MRX_EAR_TL,
						)x2x,cxx0x)
				reak;
	}dase 0CAJN):	}dase 0CAJN)_2_0:		dase 0DIANGU:{			f (galue,=>= 13
{
		d		nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MRX_EAR_TL,
						)x2x,cxx2x)
					nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MNCP_VTLRL,						)x07,cxxx7)
				 else i
		d		nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MRX_EAR_TL,
						)x2x,cxx0x)
					nd_soc_update_bits(codec,
						SM8X16_WCD_A_DNALOG_MNCP_VTLRL,						)x07,cxxx4)
				 				reak;
	}d}
r else i
		dnd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_EAR_TL,
				)x2x,cxx0x)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MNCP_VTLRL,				)x07,cxxx4)
		}

	r_edebug(%%s NExitn", __func__,

	

static bnt msm8x16_mcd_rph_leda_geventstruct mnd_soc_cdapm_widet_c*w,
	nruct mnd_skontrol(=*kontrol(,bnt mevent {
	sunte32_t imped(,bnmpedr
	struct mnd_soc_codec *codec = mw>icoec 
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	sft retu
	
sev_edbg(odec->dev, "%%s=%s=%\n", __func__, mw>inam , event 
	setu=  cd_psbh_pet_dimpedanc (&sm8x16_wcd->imbh_,				&imped(,b&impedr;
	
snitch (eevent {
	case 0SND_SOC_DAPM_PRE_PMU
			f (eet_codec_version(msm8x16_wcd) <> CAJN) 
			nd_soc_update_bits(codec,
					SM8X16_WCD_A_DNALOG_MRX_HPH_CNP_EN,					)x08
0)x08)
			f (get_codec_version(msm8x16_wcd) <== CAJN) ||{			et_codec_version(msm8x16_wcd) <== CAJN)_2_0
{
		d	nd_soc_update_bits(codec,
					SM8X16_WCD_A_DNALOG_MRX_HPH_L_TEST,					)x80
0)x80)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_DNALOG_MRX_HPH_R_TEST,					)x80
0)x80)
			}			f (eet_codec_version(msm8x16_wcd) <> CAJN) 
			nd_soc_update_bits(codec,
					SM8X16_WCD_A_DNALOG_MRX_HPH_CNP_EN,					)x08
0)x0x)
			f (gHD2_MODE<== sm8x16_wcd->iph_rodu  {
		d	nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX1_B3_TL,,0)x1C,0)x14)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX1_B4_TL,,0)x18,cxx10;
	er	nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX1_B3_TL,,0)x80
0)x80)
			}			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_HPH_L_P_DIAC_TL,, 0x02,cxx02)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DIGITAL_CDC_DIGIADLK_TL,,0)x01
0)x01)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DIGITAL_CDC_DAN_ADLK_TL,, 0x02,cxx02)
			f (e!rtu 
			wd_pimpedbontfigsodec,
cimped(,bruce)
			lse 		p	ev_edbg(odec->dev, "%Faild =o cet_csbh_=impedanc m%dn",
				setu)
			reak;
	}ase 0SND_SOC_DAPM_POSTRPMU
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_HPH_L_P_DIAC_TL,, 0x02,cxx00)
			reak;
	}ase 0SND_SOC_DAPM_POSTRPMD
			wd_pimpedbontfigsodec,
cimped(,bfase )
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DIGITAL_CDC_DAN_ADLK_TL,, 0x02,cxx0x)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DIGITAL_CDC_DIGIADLK_TL,,0)x01
0)x0x)
			f (gHD2_MODE<== sm8x16_wcd->iph_rodu  {
		d	nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX1_B3_TL,,0)x1C,0)x0x)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX1_B4_TL,,0)x18,cxxFF 
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX1_B3_TL,,0)x80
0)x0x)
			}			reak;
	}}
seturn 0;
}

static bnt msm8x16_mcd_rloeda_geventstruct mnd_soc_cdapm_widet_c*w,
	nruct mnd_skontrol(=*kontrol(,bnt mevent {
	struct mnd_soc_codec *codec = mw>icoec 
	
sev_edbg(odec->dev, "%%s=%s=%\n", __func__, mw>inam , event 
	
	nitch (eevent {
	case 0SND_SOC_DAPM_PRE_PMU
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DIGITAL_CDC_DAN_ADLK_TL,, 0x1x,cxx1x)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_LO_EN_TL,, 0x2x,cxx2x)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_LO_EN_TL,, 0x80
0)x80)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_LO_IAC_TL,, 0x08,cxx08)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_LO_IAC_TL,, 0x40
0)x40)
			reak;
	}ase 0SND_SOC_DAPM_POSTRPMU
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_LO_IAC_TL,, 0x80
0)x80)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_LO_IAC_TL,, 0x08,cxx0x)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_LO_EN_TL,, 0x40
0)x40)
			reak;
	}ase 0SND_SOC_DAPM_POSTRPMD
			usleep_rrnge(20000, 201xx)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_LO_IAC_TL,, 0x80
0)xxx)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_LO_IAC_TL,, 0x40
0)xxx)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_LO_IAC_TL,, 0x08,cxx0x)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_LO_EN_TL,, 0x80
0)xxx)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_LO_EN_TL,, 0x40
0)xxx)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_LO_EN_TL,, 0x2x,cxx0x)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DIGITAL_CDC_DAN_ADLK_TL,, 0x1x,cxx0x)
			reak;
	}}
seturn 0;
}

static bnt msm8x16_mcd_rph_reda_geventstruct mnd_soc_cdapm_widet_c*w,
	nruct mnd_skontrol(=*kontrol(,bnt mevent {
	struct mnd_soc_codec *codec = mw>icoec 
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	
sev_edbg(odec->dev, "%%s=%s=%\n", __func__, mw>inam , event 
	
	nitch (eevent {
	case 0SND_SOC_DAPM_PRE_PMU
			f (gHD2_MODE<== sm8x16_wcd->iph_rodu  {
		d	nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX2_B3_TL,,0)x1C,0)x14)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX2_B4_TL,,0)x18,cxx10;
	er	nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX2_B3_TL,,0)x80
0)x80)
			}			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_HPH_R_P_DIAC_TL,, 0x02,cxx02)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DIGITAL_CDC_DIGIADLK_TL,,0)x02,cxx02)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DIGITAL_CDC_DAN_ADLK_TL,, 0x01
0)x01)
			reak;
	}ase 0SND_SOC_DAPM_POSTRPMU
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DNALOG_MRX_HPH_R_P_DIAC_TL,, 0x02,cxx00)
			reak;
	}ase 0SND_SOC_DAPM_POSTRPMD
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DIGITAL_CDC_DAN_ADLK_TL,, 0x01
0)x0x)
			nd_soc_update_bits(codec,
				SM8X16_WCD_A_DIGITAL_CDC_DIGIADLK_TL,,0)x02
0)x00;
	erf (gHD2_MODE<== sm8x16_wcd->iph_rodu  {
		d	nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX2_B3_TL,,0)x1C,0)x0x)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX2_B4_TL,,0)x18,cxxFF 
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX2_B3_TL,,0)x80
0)x0x)
			}			reak;
	}}
seturn 0;
}

soid msm8x16_mcd_rodec_gst_dheadnt__tatie(u32 tatie {
	stitch _nt__tatie(struct mnitch _ev, *)&accdt_drta(, tatie 
	}accdt_dtatie=  nrtie
}

snt msm8x16_mcd_ronec_ggt_dheadnt__tatie(oid  {
	sr_edebug(%%s accdt_dtatie=  %\n", __func__, maccdt_dtatie)
	seturn 0accdt_dtatie;}

tatic bnt msm8x16_mcd_rph_rpageventstruct mnd_soc_cdapm_widet_c*w,
			      nruct mnd_skontrol(=*kontrol(,bnt mevent {
	struct mnd_soc_codec *codec = mw>icoec 
	struct msm8x16_wcd_priv *msm8x16_wcd = snd_soc_codec_get_drvdata(codec);
	
sev_edbg(odec->dev, "%%s N%s=event===%\n", __func__, mw>inam , event 
	
	nitch (eevent {
	case 0SND_SOC_DAPM_PRE_PMU
			f (gw>dshifuc== 5
{			sm8x16_mot ifier_oallcodec,
						CD_AEVENT_PRE_HPHL_P_DN) 
			lse  f (gw>dshifuc== 4
{			sm8x16_mot ifier_oallcodec,
						CD_AEVENT_PRE_HPHR_P_DN) 
			nd_soc_update_bits(codec,
					SM8X16_WCD_A_DNALOG_MNCP_FBTLRL, 0x2x,cxx2x)
			reak;
		}ase 0SND_SOC_DAPM_POSTRPMU
			usleep_rrnge(7000, 71xx)
			f (gw>dshifuc== 5
{
		d	nd_soc_update_bits(codec,
					SM8X16_WCD_A_DNALOG_MRX_HPH_L_TEST,cxxx4,cxxx4)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX1_B6_TL,
0)x01
0)x0x)
			 else if (ew>dshifuc== 4
{
		d	nd_soc_update_bits(codec,
					SM8X16_WCD_A_DNALOG_MRX_HPH_R_TEST,cxxx4,cxxx4)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_ADC_DRX2_B6_TL,
0)x01
0)x0x)
			}			reak;
		}ase 0SND_SOC_DAPM_PRE_PMD
			f (gw>dshifuc== 5
{
		d	nd_soc_update_bits(codec,
					SM8X16_WCD_A_DDC_DRX1_B6_TL,
0)x01
0)x01)
				smleep(2x)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_DNALOG_MRX_HPH_L_TEST,cxxx4,cxxxx)
				sm8x16_wcd->imue_bmask |= HPHL_P_DIGSABLE;{			sm8x16_mot ifier_oallcodec,
						CD_AEVENT_PRE_HPHL_P_DNFF 
			 else if (ew>dshifuc== 4
{
		d	nd_soc_update_bits(codec,
					SM8X16_WCD_A_DDC_DRX2_B6_TL,
0)x01
0)x01)
				smleep(2x)
				nd_soc_update_bits(codec,
					SM8X16_WCD_A_DNALOG_MRX_HPH_R_TEST,cxxx4,cxxxx)
				sm8x16_wcd->imue_bmask |= HPHR_P_DIGSABLE
	er	sm8x16_mot ifier_oallcodec,
						CD_AEVENT_PRE_HPHR_P_DNFF 
			 
		f (get_codec_version(msm8x16_wcd) <>= CAJN) {
		d	nd_soc_update_bits(codec,
					SM8X16_WCD_A_DNALOG_MRX_HPH_BIAS_CNP,					)xFx,cxx3x)
			}			reak;
	}ase 0SND_SOC_DAPM_POSTRPMD
			f (gw>dshifuc== 5
{
		d	clearbits(CD_AMBHC_HPHL_P_DNFF_ACK,					&sm8x16_wcd->imbh_.ph_rpagda_gtatie)
	s		sm8x16_wot ifier_oallcodec,
						CD_AEVENT_POSTRHPHL_P_DNFF 
			 else if (ew>dshifuc== 4
{
		d	clearbits(CD_AMBHC_HPHR_P_DNFF_ACK,					&sm8x16_wcd->imbh_.ph_rpagda_gtatie)
	s		sm8x16_wot ifier_oallcodec,
						CD_AEVENT_POSTRHPHR_P_DNFF 
			 
		usleep_rrnge(4000, 41xx)
			usleep_rrnge(CODEC_DELAY_1_MS, CODEC_DELAY_1_1_MS);	
	dev_edbg(odec->dev, 				%%s:=sleep=10msm=after0%s=PAEdisble_.n", __func__, 
			w>inam  
		rusleep_rrnge(10000,b101xx)
			reak;
	}}
seturn 0;
}

static bontst truct mnd_soc_cdapm_roue_Eaudiobmap[]  =
		{"RX_I2SADLK, _ULL; "%DC_DCONN"},		{"I2S RX1, _ULL; "%RX_I2SADLK,},		{"I2S RX2, _ULL; "%RX_I2SADLK,},		{"I2S RX3, _ULL; "%RX_I2SADLK,},			{"I2S TX1, _ULL; "%TX_I2SADLK,},		{"I2S TX2, _ULL; "%TX_I2SADLK,},		{"AIF2 VI, _ULL; "%TX_I2SADLK,},			{"I2S TX1, _ULL; "%DEC1 MUX,},		{"I2S TX2, _ULL; "%DEC2 MUX,},		{"AIF2 VI, _ULL; "%D