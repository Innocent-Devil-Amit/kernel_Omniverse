/* Copyright (c) 2012-2017, The Linux Foundation. All rights reserved.
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


#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

static struct snd_soc_dai_ops msm_fe_dai_ops = {};

/* Conventional and unconventional sample rate supported */
static unsigned int supported_sample_rates[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000,
	88200, 96000, 176400, 192000, 352800, 384000
};

static struct snd_pcm_hw_constraint_list constraints_sample_rates = {
	.count = ARRAY_SIZE(supported_sample_rates),
	.list = supported_sample_rates,
	.mask = 0,
};

static int multimedia_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	snd_pcm_hw_constraint_list(substream->runtime, 0,
		SNDRV_PCM_HW_PARAM_RATE,
		&constraints_sample_rates);
	return 0;
}

static int fe_dai_probe(struct snd_soc_dai *dai)
{
	struct snd_soc_dapm_route intercon;
	struct snd_soc_dapm_context *dapm;

	if (!dai || !dai->driver) {
		pr_err("%s invalid params\n", __func__);
		return -EINVAL;
	}
	dapm = snd_soc_component_get_dapm(dai->component);
	memset(&intercon, 0 , sizeof(intercon));
	if (dai->driver->playback.stream_name &&
		dai->driver->playback.aif_name) {
		dev_dbg(dai->dev, "%s add route for widget %s",
			   __func__, dai->driver->playback.stream_name);
		intercon.source = dai->driver->playback.stream_name;
		intercon.sink = dai->driver->playback.aif_name;
		dev_dbg(dai->dev, "%s src %s sink %s\n",
			   __func__, intercon.source, intercon.sink);
		snd_soc_dapm_add_routes(dapm, &intercon, 1);
	}
	if (dai->driver->capture.stream_name &&
	   dai->driver->capture.aif_name) {
		dev_dbg(dai->dev, "%s add route for widget %s",
			   __func__, dai->driver->capture.stream_name);
		intercon.sink = dai->driver->capture.stream_name;
		intercon.source = dai->driver->capture.aif_name;
		dev_dbg(dai->dev, "%s src %s sink %s\n",
			   __func__, intercon.source, intercon.sink);
		snd_soc_dapm_add_routes(dapm, &intercon, 1);
	}
	return 0;
}

static struct snd_soc_dai_ops msm_fe_Multimedia_dai_ops = {
	.startup	= multimedia_startup,
};

static const struct snd_soc_component_driver msm_fe_dai_component = {
	.name		= "msm-dai-fe",
};

static struct snd_soc_dai_driver msm_fe_dais[] = {
	{
		.playback = {
			.stream_name = "MultiMedia1 Playback",
			.aif_name = "MM_DL1",
			.rates = (SNDRV_PCM_RATE_8000_384000|
					SNDRV_PCM_RATE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S24_3LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     384000,
		},
		.capture = {
			.stream_name = "MultiMedia1 Capture",
			.aif_name = "MM_UL1",
			.rates = (SNDRV_PCM_RATE_8000_192000|
					SNDRV_PCM_RATE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S24_3LE),
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_fe_Multimedia_dai_ops,
		.name = "MultiMedia1",
		.probe = fe_dai_probe,
	},
	{
		.playback = {
			.stream_name = "MultiMedia2 Playback",
			.aif_name = "MM_DL2",
			.rates = (SNDRV_PCM_RATE_8000_384000 |
					SNDRV_PCM_RATE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S24_3LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     384000,
		},
		.capture = {
			.stream_name = "MultiMedia2 Capture",
			.aif_name = "MM_UL2",
			.rates = (SNDRV_PCM_RATE_8000_192000|
					SNDRV_PCM_RATE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S24_3LE),
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_fe_Multimedia_dai_ops,
		.name = "MultiMedia2",
		.probe = fe_dai_probe,
	},
	{
		.playback = {
			.stream_name = "CS-VOICE Playback",
			.aif_name = "CS-VOICE_DL1",
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.capture = {
			.stream_name = "CS-VOICE Capture",
			.aif_name = "CS-VOICE_UL1",
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_fe_dai_ops,
		.name = "CS-VOICE",
		.probe = fe_dai_probe,
	},
	{
		.playback = {
			.stream_name = "VoIP Playback",
			.aif_name = "VOIP_DL",
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIAL,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.capture = {
			.stream_name = "VoIP Capture",
			.aif_name = "VOIP_UL",
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIAL,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_fe_dai_ops,
		.name = "VoIP",
		.probe = fe_dai_probe,
	},
	{
		.playback = {
			.stream_name = "MultiMedia3 Playback",
			.aif_name = "MM_DL3",
			.rates = (SNDRV_PCM_RATE_8000_384000 |
					SNDRV_PCM_RATE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S24_3LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
			.channels_min = 1,
			.channels_max = 6,
			.rate_min =     8000,
			.rate_max =     384000,
		},
		.capture = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8000__8000_192000|
					SNDRV_PCM_RATE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t status copyis invalid, max allowed = %d\n",
				__f			.rate_min =     8000,
			.rate_max =     384000,
		},
		.capture = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8000__8000_192000|
					SNDRV_PCM_RATE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se   80 (SNDRV_PCM_RATE_8000__8000_192000|
					SNDRV_PCM_RATE_KNOT),
			.formats = (E2o4MTBIT_S24_LE |
				    SNDRV_PCM				__f			.rate_min =     8000,
			.rMTBIT_S32_LE),
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     384000,
		},
		.capture = {
			.stream_name = "MultiMedia2 Ca0
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8000__8000_192000|
					Stercressrtup(SNDRV_PSNDRV_PCM_RATE_KNOT4,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t status cop5is invalid, max allowed = %d\n",
5			__f			.rate_min =     8000,
			.rate_max =     384000,
		},
		.capture = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8000__8000_192000|
					SNDRV_PCM_RATE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se   80 (SNDRV_PCM_RATE_8000__8000_192000|
					SNDRV_PCM_RATE_KNOT),
			.formats = (E2o5MTBIT_S24_LE |
				    SNDRV_PCM5			__f			.rate_min =     8000,
			.r		.ra =     384000,
		},
		.capture = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8000__800,
		},
		.capture = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8000__8000_192000|
					SNDRV_PCM_RATE_KNOT5,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t status cop6is invalid, max allowed = %d\n",
6			__f			.rate_min =     8000,
			.rate_max =     384000,
		},
		.capture = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8000__8000_192000|
					SNDRV_PCM_RATE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se   80 (SNDRV_PCM_RATE_8000__8000_192000|
					SNDRV_PCM_RATE_KNOT),
			.formats = (E2o6MTBIT_S24_LE |
				    SNDRV_PCM6			__f			.rate_min =     8000,
			.r		.ra =     384000,
		},
		.capture = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8000__800,
		},
		.capture = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8000__8000_192000|
					SNDRV_PCM_RATE_KNOT6,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t status cop7is invalid, max allowed = %d\n",
7			__f			.rate_min =     8000,
			.rate_max =     384000,
		},
		.capture = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8000__8000_192000|
					SNDRV_PCM_RATE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se   80 (SNDRV_PCM_RATE_8000__8000_192000|
				RV_PCM_RATE_8000__8000_192000|
					Stercressrtup(SNDRV_PSNDRV_PCM_RATE_KNOT7,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t status cop8is invalid, max allowed = %d\n",
8			__f			.rate_min =     8000,
			.rate_max =     384000,
		},
		.capture = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8000__8000_192000|
					SNDRV_PCM_RATE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se   80 (SNDRV_PCM_RATE_8000__8000_192000|
					SNDRV_PCM_RATE_KNOT),
			.formats = (E2o8MTBIT_S24_LE |
				    SNDRV_PCM8			__f			.rate_min =     8000,
			.r		.ra =     384000,
		},
		.capture = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8000__800,
		},
		.capture = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8000__8000_192000|
					SNDRV_PCM_RATE_KNOT8,
			.formats = (SNDRV_PCM_FMTBIT/* FE DAIs cOT)linusrc hostless operation purposeux/pT_S16_LE |
				    SNDRV_PCM_FMTBIT_t SLIMBUS0_HOSTLand
s invalid, max allowed = %dSLIM0",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND_soc_dare = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
00,
		},
		.capture = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
	_soc_dare |
					SNDRV_PCM_RATE_KNOT),
			.formSLIMBUS0_HOSTLand
TBIT_S24_LE |
				    SNDRSLIM0"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDatic sre = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se   80 (SNDRV_PCM_RATE_8000__8_soc_dare |
				ame = "VOIP_UL",
			.rates = SNDRVSLIMBUS0_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t SLIMBUS1_HOSTLand
s invalid, max allowed = %dSLIM1",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND_soc_dare = {
			.stream_name = "MultiMedia3 Capture"hannels_max = 2,
			L3",
00,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {
			.stream_name = "VoIP Capt_soc_dare |
					SNDRV_PCM_RATE_KNOT),
			.formSLIMBUS1_HOSTLand
TBIT_S24_LE |
				    SNDRSLIM1"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
00,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {
			.stream_name = "VoIP Capture",
			.aif_name = "VOIP_UL",
			.rates = SNDRVSLIMBUS1_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t SLIMBUS3_HOSTLand
s invalid, max allowed = %dSLIM3",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND_soc_dare = {
			.stream_name = "MultiMedia3 Capture"hannels_max = 2,
			L3",
00,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {
			.stream_name = "VoIP Capt_soc_dare |
					SNDRV_PCM_RATE_KNOT),
			.formSLIMBUS3_HOSTLand
TBIT_S24_LE |
				    SNDRSLIM3"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
00,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {
			.stream_name = "VoIP Capture",
			.aif_name = "VOIP_UL",
			.rates = SNDrmSLIMBUS3_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t SLIMBUS4_HOSTLand
s invalid, max allowed = %dSLIM4",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND_soc_dare = {
			.stream_name = "MultiMedia3 Capture"hannels_max = 2,
			L3",
00,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {
			.stream_name = "VoIP Capt_soc_dare |
					SNDRV_PCM_RATE_KNOT),
			.formSLIMBUS4_HOSTLand
TBIT_S24_LE |
				    SNDRSLIM4"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
00,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {
			.stream_name = "VoIP Capture",
			.aif_name = "VOIP_UL",
			.rates = SNDrmSLIMBUS4_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t SLIMBUS6_HOSTLand
s invalid, max allowed = %dSLIM6",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND_soc_dare = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
00,
		},
		.capture = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
	_soc_dare |
				ame = "VOIP_UL",
			.rates = SNDrmSLIMBUS6_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t INTe =_HOSTLand
s invalid, max allowed = %dINTFM",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDMTBIT_S16_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.capture = {
			.stream_name = "C	SNDRV_PCM_RATE_KNOT),
			.formINTe =_HOSTLand
TBIT_S24_LE |
				    SNDRINTFM"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALnnels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.capture = {
			.stream_name = "Came = "VOIP_UL",
			.rates = SNDrmINTe =_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t INTeHFP_BT Hostless s invalid, max allowed = %dINTHFP_,
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SN |BIT_S16_LE |
			1tic sre = {
			.strennels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.capture = {
			.s1tic sre e = "C	SNDRV_PCM_RATE_KNOT),
			.formINTeHFP_BT Hostless TBIT_S24_LE |
				    SNDRINTHFP_U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SN |BIT_S16_LE |
			1tic sre = {
			.strennels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.capture = {
			.s1tic sre e = "Came = "VOIP_UL",
			.rates = SNDrmINTeHFP_BT_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t AFannels_min = 1,
			.channels__LE |X			__f			.rate_min =     8000,
			.apture"IT_S16_LE |
			1tic apture"IT_S16_LE |
			treamture = {
			.strennels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.capture = {
			.stream_name = "C	SNDRV_PCM_RATE_KNOT),
			.formAFanTBIT_S24_LE |
				    SNDRmin TX			__f			.rate_min =     8000,
			.apture"IT_S16_LE |
			1tic apture"IT_S16_LE |
			treamture = {
			.strennels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.capture = {
			.stream_name = "Came = "VOIP_UL",
			.rates = SNDrmAFa-PROXY,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t HDMI_HOSTLand
s invalid, max allowed = %dHDMI_,
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDMTBIT_S16_LE,
			.chaam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
00,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {		},
		.capture = {
	tream_name = "Came = "VOIP_UL",
			.rates = SNDrmHDMI_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t AUXnameHOSTLand
s invalid, max allowed = %dAUXname,
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SN |BIT_S16_LE |
			1tic sre = {
			.strennels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.1ate_max =     48000,
		},
		.capture = {
			.s1tic sre e = "C	SNDRV_PCM_RATE_KNOT),
			.formAUXnameHOSTLand
TBIT_S24_LE |
				    SNDRAUXnameU
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SN |BIT_S16_LE |
			1tic sre = {
			.strennels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.1ate_max =     48000,
		},
		.capture = {
			.1tic sre e = "Came = "VOIP_UL",
			.rates = SNDrmAUXnameHOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t SEC_AUXnameHOSTLand
s invalid, max allowed = %dSEC_AUXname,
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SN |BIT_S16_LE |
			1tic sre = {
			.strennels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.1ate_max =     48000,
		},
		.capture = {
			.s1tic sre e = "Came = "VOIP_UL",
			.rates = SNDrmSEC_AUXnameRXeHOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formSEC_AUXnameHOSTLand
TBIT_S24_LE |
				    SNDRSEC_AUXnameU
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SN |BIT_S16_LE |
			1tic sre = {
			.strennels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.1ate_max =     48000,
		},
		.capture = {
			.1tic sre e = "Came = "VOIP_UL",
			.rates = SNDrmSEC_AUXnameTXeHOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16__48000,
			.formats = SNDRV_PCM_F_RATESTUBIT_S16_LE,
			.channels_min = 1ATESTUB
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.0,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {		},
		.capture = {
	tream_name = "Came = "VOIP_DL",
			.rates = SND_RATESTUBICM_RATE_8000_48000,
			.formaATESTUB
 = SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIAL,
			.channels_min = 1,0,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {		},
		.capture = {
	tream_name = "Came = "VOIP_UL",
			.rates = SNDrmrmaATESTUB_UL1",
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMLTannels_min = 1,
			.channels_FMLTa
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.0,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {00,
		},
		.capture = {
			.stream_name = "C	SNDRV_PCM_RATE_KNOT),
			.formFMLTanCM_RATE_8000_48000,
			.forMLTa
 = SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIAL,
			.channels_min = 1,0,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {00,
		},
		.capture = {
			.stream_name = "Came = "VOIP_UL",
			.rates = SNDrmrMLTa,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t sI2SeRXeHOSTLandis invalid, max allowed = %d\I2Se,
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDMTBIT_S16_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     480		},
		.capture = {
	tream_name = "Came = "VOIP_DL",
			.rates = SN\I2SeTXeHOSTLandMTBIT_S24_LE |
				    SNDRVI2SeU
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
00,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {		},
		.capture = {
	tream_name = "Came = "VOIP_UL",
			.rates = SNDrm\I2SeTXeHOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t SEC_I2SeRXeHOSTLandis invalid, max allowed = %dSEC_I2Se,
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDMTBIT_S16_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.capture = {
			.ure",
			.aif_name = "VOIP_UL",
			.rates = SNDrmSEC_I2SeRXeHOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formPrimary \I2SeTX Hostless TBIT_S24_LE |
				    SNDRPRI_VI2SeU
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
00,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {		},
		.capture = {
	tream_name = "Came = "VOIP_UL",
			.rates = SNDrmPRI_VI2SeTXeHOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Primary \I2SeRX Hostless s invalid, max allowed = %dPRI_VI2Se,
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND_soc_dare = {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
00,
		},
		.capture = {
			.stream_name = "Mrate_max =     48000,
		},
		.capture = {
			._soc_dare |
				ame = "VOIP_UL",
			.rates = SNDrmPRI_VI2SeRXeHOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formSecondary \I2SeTX Hostless TBIT_S24_LE |
				    SNDRSEC_VI2SeU
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
00,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {		},
		.capture = {
	tream_name = "Came = "VOIP_UL",
			.rates = SNDrmSEC_VI2SeTXeHOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Secondary \I2SeRX Hostless s invalid, max allowed = %dSEC_VI2Se,
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND_soc_dare = {
			.strem_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
0,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {		},
		.capture = {
	_soc_dare |
				ame = "VOIP_UL",
			.rates = SNDrmSEC_VI2SeRXeHOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formTertiary \I2SeTX Hostless TBIT_S24_LE |
				    SNDRTERT_VI2SeU
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
00,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {		},
		.capture = {
	tream_name = "Came = "VOIP_UL",
			.rates = SNDrmTERT_VI2SeTXeHOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Tertiary \I2SeRX Hostless s invalid, max allowed = %dTERT_VI2Se,
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND_soc_dare = {
			.strem_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
0,
		},
		.capture = {
			.stream_name = "M	},
		.capture = 			},
		.capture = {
			._soc_dare |
				ame = "VOIP_UL",
			.rates = SNDrmTERT_VI2SeRXeHOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formQuturrnary \I2SeTX Hostless TBIT_S24_LE |
				    SNDRQUAT_VI2SeU
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
00,
		},
		.capture = {
			.stream_name = "M	},
		.capture = {		},
		.capture = {
	tream_name = "Came = "VOIP_UL",
			.rates = SNDrmQUAT_VI2SeTXeHOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Quturrnary \I2SeRX Hostless s invalid, max allowed = %dQUAT_VI2Se,
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND_soc_dare = {
			.strem_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
0,
		},
		.capture = {
			.stream_name = "MultiMedia3 Capture		},
		.capture = {
	_soc_dare |
				ame = "VOIP_UL",
			.rates = SNDrmQUAT_VI2SeRXeHOSTLand,
			.formats = (SNDRV_PCM_FMTBIT/* TDM Hostless x/pT_S16_	SNDRV_PCM_RATE_KNOT),
			.formPrimary TDM0 Hostless TBIT_S24_LE |
				    SNDRPRI_TDMeTXe0"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmPRI_TDMeTXe0"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Primary TDM0 Hostless s invalid, max allowed = %dPRI_TDMeRXe0",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmPRI_TDMeRXe0"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formPrimary TDM1 Hostless TBIT_S24_LE |
				    SNDRPRI_TDMeTXe1"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmPRI_TDMeTXe1"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Primary TDM1 Hostless s invalid, max allowed = %dPRI_TDMeRXe1",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmPRI_TDMeRXe1"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formPrimary TDM2 Hostless TBIT_S24_LE |
				    SNDRPRI_TDMeTXe2"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmPRI_TDMeTXe2"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Primary TDM2 Hostless s invalid, max allowed = %dPRI_TDMeRXe2",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmPRI_TDMeRXe2"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formPrimary TDM3 Hostless TBIT_S24_LE |
				    SNDRPRI_TDMeTXe3"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmPRI_TDMeTXe3"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Primary TDM3 Hostless s invalid, max allowed = %dPRI_TDMeRXe3",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmPRI_TDMeRXe3"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formPrimary TDM4 Hostless TBIT_S24_LE |
				    SNDRPRI_TDMeTXe4"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmPRI_TDMeTXe4"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Primary TDM4 Hostless s invalid, max allowed = %dPRI_TDMeRXe4",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmPRI_TDMeRXe4"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formPrimary TDM5 Hostless TBIT_S24_LE |
				    SNDRPRI_TDMeTXe5"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmPRI_TDMeTXe5"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Primary TDM5 Hostless s invalid, max allowed = %dPRI_TDMeRXe5",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmPRI_TDMeRXe5"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formPrimary TDM6 Hostless TBIT_S24_LE |
				    SNDRPRI_TDMeTXe6"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmPRI_TDMeTXe6_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Primary TDM6 Hostless s invalid, max allowed = %dPRI_TDMeRXe6",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmPRI_TDMeRXe6_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formPrimary TDM7 Hostless TBIT_S24_LE |
				    SNDRPRI_TDMeTXe7"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmPRI_TDMeTXe7_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Primary TDM7 Hostless s invalid, max allowed = %dPRI_TDMeRXe7",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmPRI_TDMeRXe7_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formSecondary TDM0 Hostless TBIT_S24_LE |
				    SNDRSEC_TDMeTXe0"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmSEC_TDMeTXe0"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Secondary TDM0 Hostless s invalid, max allowed = %dSEC_TDMeRXe0",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmSEC_TDMeRXe0"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formSecondary TDM1 Hostless TBIT_S24_LE |
				    SNDRSEC_TDMeTXe1"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmSEC_TDMeTXe1_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Secondary TDM1 Hostless s invalid, max allowed = %dSEC_TDMeRXe1",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmSEC_TDMeRXe1"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formSecondary TDM2 Hostless TBIT_S24_LE |
				    SNDRSEC_TDMeTXe2"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmSEC_TDMeTXe2"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Secondary TDM2 Hostless s invalid, max allowed = %dSEC_TDMeRXe2",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmSEC_TDMeRXe2"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formSecondary TDM3 Hostless TBIT_S24_LE |
				    SNDRSEC_TDMeTXe3"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmSEC_TDMeTXe3"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Secondary TDM3 Hostless s invalid, max allowed = %dSEC_TDMeRXe3",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmSEC_TDMeRXe3"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formSecondary TDM4 Hostless TBIT_S24_LE |
				    SNDRSEC_TDMeTXe4"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmSEC_TDMeTXe4_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Secondary TDM4 Hostless s invalid, max allowed = %dSEC_TDMeRXe4",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmSEC_TDMeRXe4"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formSecondary TDM5 Hostless TBIT_S24_LE |
				    SNDRSEC_TDMeTXe5"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmSEC_TDMeTXe5"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Secondary TDM5 Hostless s invalid, max allowed = %dSEC_TDMeRXe5",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmSEC_TDMeRXe5"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formSecondary TDM6 Hostless TBIT_S24_LE |
				    SNDRSEC_TDMeTXe6"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmSEC_TDMeTXe6_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Secondary TDM6 Hostless s invalid, max allowed = %dSEC_TDMeRXe6",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmSEC_TDMeRXe6_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formSecondary TDM7 Hostless TBIT_S24_LE |
				    SNDRSEC_TDMeTXe7"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmSEC_TDMeTXe7_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Secondary TDM7 Hostless s invalid, max allowed = %dSEC_TDMeRXe7",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmSEC_TDMeRXe7_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formTertiary TDM0 Hostless TBIT_S24_LE |
				    SNDRTERT_TDMeTXe0"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmTERT_TDMeTXe0"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Tertiary TDM0 Hostless s invalid, max allowed = %dTERT_TDMeRXe0",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmTERT_TDMeRXe0"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formTertiary TDM1 Hostless TBIT_S24_LE |
				    SNDRTERT_TDMeTXe1"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmTERT_TDMeTXe1_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Tertiary TDM1 Hostless s invalid, max allowed = %dTERT_TDMeRXe1",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmTERT_TDMeRXe1"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formTertiary TDM2 Hostless TBIT_S24_LE |
				    SNDRTERT_TDMeTXe2"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmTERT_TDMeTXe2"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Tertiary TDM2 Hostless s invalid, max allowed = %dTERT_TDMeRXe2",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmTERT_TDMeRXe2"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formTertiary TDM3 Hostless TBIT_S24_LE |
				    SNDRTERT_TDMeTXe3"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmTERT_TDMeTXe3"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Tertiary TDM3 Hostless s invalid, max allowed = %dTERT_TDMeRXe3",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmTERT_TDMeRXe3"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formTertiary TDM4 Hostless TBIT_S24_LE |
				    SNDRTERT_TDMeTXe4"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmTERT_TDMeTXe4_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Tertiary TDM4 Hostless s invalid, max allowed = %dTERT_TDMeRXe4",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmTERT_TDMeRXe4"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formTertiary TDM5 Hostless TBIT_S24_LE |
				    SNDRTERT_TDMeTXe5"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmTERT_TDMeTXe5"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Tertiary TDM5 Hostless s invalid, max allowed = %dTERT_TDMeRXe5",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmTERT_TDMeRXe5"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formTertiary TDM6 Hostless TBIT_S24_LE |
				    SNDRTERT_TDMeTXe6"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmTERT_TDMeTXe6_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Tertiary TDM6 Hostless s invalid, max allowed = %dTERT_TDMeRXe6",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmTERT_TDMeRXe6_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formTertiary TDM7 Hostless TBIT_S24_LE |
				    SNDRTERT_TDMeTXe7"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmTERT_TDMeTXe7_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Tertiary TDM7 Hostless s invalid, max allowed = %dTERT_TDMeRXe7",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmTERT_TDMeRXe7_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formQuturrnary TDM0 Hostless TBIT_S24_LE |
				    SNDRQUAT_TDMeTXe0"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmQUAT_TDMeTXe0"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Quturrnary TDM0 Hostless s invalid, max allowed = %dQUAT_TDMeRXe0",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmQUAT_TDMeRXe0"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formQuturrnary TDM1 Hostless TBIT_S24_LE |
				    SNDRQUAT_TDMeTXe1"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmQUAT_TDMeTXe1_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Quturrnary TDM1 Hostless s invalid, max allowed = %dQUAT_TDMeRXe1",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmQUAT_TDMeRXe1"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formQuturrnary TDM2 Hostless TBIT_S24_LE |
				    SNDRQUAT_TDMeTXe2"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmQUAT_TDMeTXe2"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Quturrnary TDM2 Hostless s invalid, max allowed = %dQUAT_TDMeRXe2",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmQUAT_TDMeRXe2"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formQuturrnary TDM3 Hostless TBIT_S24_LE |
				    SNDRQUAT_TDMeTXe3"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmQUAT_TDMeTXe3"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Quturrnary TDM3 Hostless s invalid, max allowed = %dQUAT_TDMeRXe3",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmQUAT_TDMeRXe3"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formQuturrnary TDM4 Hostless TBIT_S24_LE |
				    SNDRQUAT_TDMeTXe4"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmQUAT_TDMeTXe4_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Quturrnary TDM4 Hostless s invalid, max allowed = %dQUAT_TDMeRXe4",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmQUAT_TDMeRXe4"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formQuturrnary TDM5 Hostless TBIT_S24_LE |
				    SNDRQUAT_TDMeTXe5"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmQUAT_TDMeTXe5"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Quturrnary TDM5 Hostless s invalid, max allowed = %dQUAT_TDMeRXe5",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmQUAT_TDMeRXe5"HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formQuturrnary TDM6 Hostless TBIT_S24_LE |
				    SNDRQUAT_TDMeTXe6"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmQUAT_TDMeTXe6_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Quturrnary TDM6 Hostless s invalid, max allowed = %dQUAT_TDMeRXe6",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmQUAT_TDMeRXe6_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formQuturrnary TDM7 Hostless TBIT_S24_LE |
				    SNDRQUAT_TDMeTXe7"U
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmQUAT_TDMeTXe7_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Quturrnary TDM7 Hostless s invalid, max allowed = %dQUAT_TDMeRXe7",
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SND000_192000= {
			.stream_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80",
			.rates = SNDrmQUAT_TDMeRXe7_HOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Voice2 s invalid, max allowed = %dVOICE2",
 SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE rate_max =     48000,
		},
		.capture = {
			.	tream_name = "C	SNDRV_PCM_RATE_KNOT),
			.formVoice2 TBIT_S24_LE |
				    SNDRVOICE2"U
 SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE rate_max =     48000,
		},
		.capture = {
			.	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmVoice2,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t Pseudo s invalid, max allowed = %dMM",
9 SNDRV_PCM_FMTB(IT_S16_LE |
					SNDRV_PC			.rat	IT_S16_LE |
			KNOT)M_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__s			},
		.capture = {
	tream_name = "C	SNDRV_PCM_RATE_KNOT),
			.formPseudo TBIT_S24_LE |
				    SNDRMM"U
9 SNDRV_PCM_FMTB(IT_S16_LE |
					SNDRV_PC		.rat	IT_S16_LE |
			KNOT)M_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__s000,
		},
		.capture = {
	tream_name = "CRV_PCM_RATE_80Multimedia0",
			.rates = SNDrmPseudo,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t DTMFeRXeHOSTLand s invalid, max allowed = %dDTMFe,
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE rate_max =     48			},
		.capture = {
	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmDTMFeRXeHOSTLand,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formCPE Listen Audio cBIT_S24_LE |
				    SNDRCPE_LSMeU
_H= SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
00,
		},
		.capture = {
			.stream_name = "M1
				  "se000__se		},
		.capture = {
	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmCPE_LSMeNOHOST"_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t VOL			STUB s invalid, max allowed = %dVOL			STUB",
 SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE rate_max =     480		},
		.capture = {
	tream_name = "C	SNDRV_PCM_RATE_KNOT),
			.formVOL			STUB TBIT_S24_LE |
				    SNDRVOL			STUB"U
 SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE rate_max =     480		},
		.capture = {
	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmVOL			STUB,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t VOICE2"STUB s invalid, max allowed = %dVOICE2"STUB",
 SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE rate_max =     480		},
		.capture = {
	tream_name = "C	SNDRV_PCM_RATE_KNOT),
			.formVOICE2"STUB TBIT_S24_LE |
				    SNDRVOICE2"STUB"U
 SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE rate_max =     480		},
		.capture = {
	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmVOICE2"STUB,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t MultiMedia9 s invalid, max allowed = %dMM",
9 SNDRV_PCM_FMTB(IT_S16_LE |
					SND000_19pture",
IT_S16_LE |
			KNOT)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8L3"3,
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__se00,
		},
		.capture = {
	000_192000|
					SNDRV_PCM_RATE_KNOT),
			.formMultiMedia9 TBIT_S24_LE |
				    SNDRMM"U
9 SNDRV_PCM_FMTB(IT_S16_LE |
					SNDRV_PC		.rat,
IT_S16_LE |
			KNOT)M_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				  "se000__s000,
		},
		.capture = {
	tream_name = "CRV_PCM_RATE_80Multimedia0",
			.rates = SNDrmMultiMedia9,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t QCHAT s invalid, max allowed = %dQCHAT",
 SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE rate_max =     480		},
		.capture = {
	tream_name = "C	SNDRV_PCM_RATE_KNOT),
			.formQCHAT TBIT_S24_LE |
				    SNDRQCHAT"U
 SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE rate_max =     480		},
		.capture = {
	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmQCHAT,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formListen 1 Audio Service TBIT_S24_LE |
				    SNDRLSM1"U
_H= SNDRV_PCM_FMTB(IT_S16_LE |
			16_PC			.rat,
IT_S16_LE |
			tream)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
OT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE 4ate_max =     48016_PC
		.capture = {
	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmLSM1,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formListen 2 Audio Service TBIT_S24_LE |
				    SNDRLSM2"U
_H= SNDRV_PCM_FMTB(IT_S16_LE |
			16_PC			.rat,
IT_S16_LE |
			tream)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
OT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE 4ate_max =     48016_PC
		.capture = {
	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmLSM2,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formListen 3 Audio Service TBIT_S24_LE |
				    SNDRLSM3"U
_H= SNDRV_PCM_FMTB(IT_S16_LE |
			16_PC			.rat,
IT_S16_LE |
			tream)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
OT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE 4ate_max =     48016_PC
		.capture = {
	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmLSM3,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formListen 4 Audio Service TBIT_S24_LE |
				    SNDRLSM4"U
_H= SNDRV_PCM_FMTB(IT_S16_LE |
			16_PC			.rat,
IT_S16_LE |
			tream)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
OT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE 4ate_max =     48016_PC
		.capture = {
	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmLSM4,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formListen 5 Audio Service TBIT_S24_LE |
				    SNDRLSM5"U
_H= SNDRV_PCM_FMTB(IT_S16_LE |
			16_PC			.rat,
IT_S16_LE |
			tream)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
OT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE 4ate_max =     48016_PC
		.capture = {
	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmLSM5,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formListen 6 Audio Service TBIT_S24_LE |
				    SNDRLSM6"U
_H= SNDRV_PCM_FMTB(IT_S16_LE |
			16_PC			.rat,
IT_S16_LE |
			tream)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
OT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE 4ate_max =     48016_PC
		.capture = {
	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmLSM6,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formListen 7 Audio Service TBIT_S24_LE |
				    SNDRLSM7"U
_H= SNDRV_PCM_FMTB(IT_S16_LE |
			16_PC			.rat,
IT_S16_LE |
			tream)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
OT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE 4ate_max =     48016_PC
		.capture = {
	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmLSM7,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formListen 8 Audio Service TBIT_S24_LE |
				    SNDRLSM8"U
_H= SNDRV_PCM_FMTB(IT_S16_LE |
			16_PC			.rat,
IT_S16_LE |
			tream)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
OT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE 4ate_max =     48016_PC
		.capture = {
	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmLSM8,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t VoWLAN s invalid, max allowed = %dVoWLAN",
 SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE rate_max =     480		},
		.capture = {
	tream_name = "C	SNDRV_PCM_RATE_KNOT),
			.formVoWLAN TBIT_S24_LE |
				    SNDRVoWLAN"U
 SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE rate_max =     480		},
		.capture = {
	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmVoWLAN,
			.formats = (SNDRV_PCM_FMTBIT/* FE DAIs cOT)ted IT_ multiple instanc_FMofMoffload LE |
				*/IT_S16_LE |
				    SNDRV_PCM_FMTBIT_t MultiMedia10 s invalid, max allowed = %dMM",
10 SNDRV_PCM_FMTB(IT_S16_LE |
					SND000_19pture",
IT_S16_LE |
			KNOT)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8L3"3,
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |ate_max =     48000,
		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80Multimedia0",
			.ratescompress0",
(SNDRV_Ps = SNDrmMultiMedia10 SNDR.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t MultiMedia11 s invalid, max allowed = %dMM",
11 SNDRV_PCM_FMTB(IT_S16_LE |
					SND000_19pture",
IT_S16_LE |
			KNOT)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8L3"3,
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |ate_max =     48000,
		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80Multimedia0",
			.ratescompress0",
(SNDRV_Ps = SNDrmMultiMedia11,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t MultiMedia12 s invalid, max allowed = %dMM",
12 SNDRV_PCM_FMTB(IT_S16_LE |
					SND000_19pture",
IT_S16_LE |
			KNOT)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8L3"3,
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |ate_max =     48000,
		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80Multimedia0",
			.ratescompress0",
(SNDRV_Ps = SNDrmMultiMedia12,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t MultiMedia13 s invalid, max allowed = %dMM",
13 SNDRV_PCM_FMTB(IT_S16_LE |
					SND000_19pture",
IT_S16_LE |
			KNOT)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8L3"3,
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |ate_max =     48000,
		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80Multimedia0",
			.ratescompress0",
(SNDRV_Ps = SNDrmMultiMedia13,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t MultiMedia14 s invalid, max allowed = %dMM",
14 SNDRV_PCM_FMTB(IT_S16_LE |
					SND000_19pture",
IT_S16_LE |
			KNOT)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8L3"3,
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |ate_max =     48000,
		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80Multimedia0",
			.ratescompress0",
(SNDRV_Ps = SNDrmMultiMedia14,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t MultiMedia15 s invalid, max allowed = %dMM",
15 SNDRV_PCM_FMTB(IT_S16_LE |
					SND000_19pture",
IT_S16_LE |
			KNOT)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8L3"3,
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |ate_max =     48000,
		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80Multimedia0",
			.ratescompress0",
(SNDRV_Ps = SNDrmMultiMedia15,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t MultiMedia16 s invalid, max allowed = %dMM",
16 SNDRV_PCM_FMTB(IT_S16_LE |
					SND000_19pture",
IT_S16_LE |
			KNOT)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8L3"3,
			.rates = (SNDRV_PCM_RATE_8TE_KNOT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |ate_max =     48000,
		},
		.capture = {
	000_192000|
				RV_PCM_RATE_80Multimedia0",
			.ratescompress0",
(SNDRV_Ps = SNDrmMultiMedia16,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t VoiceMMode1 s invalid, max allowed = %dVOICEMMODE1",
 SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE rate_max =     48000,
		},
		.capture = {
			.	tream_na e = "C	SNDRV_PCM_RATE_KNOT),
			.formVoiceMMode1 TBIT_S24_LE |
				    SNDRVOICEMMODE1"U
 SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE rate_max =     48000,
		},
		.capture = {
			.	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmVoiceMMode1,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_t VoiceMMode2 s invalid, max allowed = %dVOICEMMODE2",
 SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE rate_max =     48000,
		},
		.capture = {
			.	tream_name = "C	SNDRV_PCM_RATE_KNOT),
			.formVoiceMMode2 TBIT_S24_LE |
				    SNDRVOICEMMODE2"U
 SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_SPECIALm_name = "MultiMedia3 CT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE rate_max =     48000,
		},
		.capture = {
			.	tream_name = "CRV_PCM_RATE_80",
			.rates = SNDrmVoiceMMode2,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formMultiMedia17 TBIT_S24_LE |
				    SNDRMM"U
17 SNDRV_PCM_FMTB(IT_S16_LE |
					SND192_PC		.rat	IT_S16_LE |
			KNOT)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8L3"3,
OT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |ate_max =     48000,
		},
		.capture = {
000,
192_PC2000|
				RV_PCM_RATE_80Multimedia0",
			.ratescompress0",
(SNDRV_Ps = SNDrmMultiMedia17,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formMultiMedia18 TBIT_S24_LE |
				    SNDRMM"U
18 SNDRV_PCM_FMTB(IT_S16_LE |
					SND192_PC		.rat	IT_S16_LE |
			KNOT)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8L3"3,
OT),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |ate_max =     48000,
		},
		.capture = {
			.	tream_name = "CRV_PCM_RATE_80Multimedia0",
			.ratescompress0",
(SNDRV_Ps = SNDrmMultiMedia18,
			.formats = (SNDRV_PCM_FMTBIT_S16_	SNDRV_PCM_RATE_KNOT),
			.formMultiMedia19 TBIT_S24_LE |
				    SNDRMM"U
19 SNDRV_PCM_FMTB(IT_S16_LE |
					SND192_PC		.rat	IT_S16_LE |
			KNOT)M_FMTBIT_SPECIALam_name = "MultiMedia3 Capture",
			.aif_name = "MM_UL3",
			.rates = (SNDRV_PCM_RATE_8L3"3,
OT),
			.formats = (