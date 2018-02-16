/*
 *  Copyright (c) by Jaroslav Kyselaic str <perex@perex.cz>opyriUniversal interface for Audioighdec '97opyopyriFor more details look to AC '97 component specification revision 2.2opyrioslIntelighrporation (http://developer.intel.com) and to datasheetsopyrifor specific codecs.opyopyopyri This program is free software; you can redistribute it and/or modifyopyri it under the terms of the GNU General Public License as publishediosopyri the Free SoftwareiFoundation; either version 2 of the License, oropyri (at your option) any later version.opyopyri This program is distributed in the hope that it will be useful,opyri but WITHOUT ANY WARRANTY; without even the implied warranty ofopyri MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See theopyri GNU General Public License for more details.opyopyri You should have received a copy of the GNU General Public Licenseopyri along with this program; if not, write to the Free Softwareopyri Foundation,lInc., 59 Temple Place, Suite 330, Boston,lMA  02111-1307 USAopyopy/

#include "ac97_local.h"
#include "ac97_patch.h"

/yopyriForward declarationsopy/

static struct snd_kcontrol *snd_ac97_find_mixer_ctl(struct snd_ac97 *ac97,
						ri  const char *name);
static int snd_ac97_add_vmaster(struct snd_ac97 *ac97, char *name,
				const unsigned int *tlv, const char **yseles);

/yopyriChip specific initializationopy/

static int patch_build_controls(struct snd_ac97 * ac97, const struct snd_kcontrol_new *controls, int count)
{
	int idx, err;

	for (idx = 0; idx < count; idx++)
		if ((err = snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&controls[idx], ac97))) < 0)
			return err;
	return 0;
}

/y replace with a new TLVpy/
static void reset_tlv(struct snd_ac97 *ac97, const char *name,
		  ri  const unsigned int *tlv)
{
	struct snd_ctl_elem_id sid;
	struct snd_kcontrol *kctl;
	memset(&sid, 0, sizeof(sid));
	strcpy(sid.name, name);
	sid.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	kctl = snd_ctl_find_id(ac97->bus->card, &sid);
	if (kctl && kctl->tlv.p)
		kctl->tlv.p = tlv;
}

/y set to the page, update bits and restore the pagepy/
static int ac97_update_bits_page(struct snd_ac97 *ac97, unsigned short reg, unsigned short mask, unsigned short value, unsigned short page)
{
	unsigned short page_sele;
	int ret;

	mutex_lock(&ac97->page_mutex);
	page_sele = snd_ac97_read(ac97, AC97_INT_PAGING) & AC97_PAGE_MASK;
	snd_ac97_update_bits(ac97, AC97_INT_PAGING, AC97_PAGE_MASK, page);
	ret = snd_ac97_update_bits(ac97, reg, mask, value);
	snd_ac97_update_bits(ac97, AC97_INT_PAGING, AC97_PAGE_MASK, page_sele);
	mutex_unlock(&ac97->page_mutex); /y unlock paging y/
	return ret;
}

/yopyrshared line-in/mic controls
py/
static int ac97_enum_text_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo,
			   ri  const char **texts, unsigned int nums)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = nums;
	if (uinfo->value.enumerated.item > nums - 1)
		uinfo->value.enumerated.item = nums - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int ac97_surround_jack_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char *texts[] = { "Shared", "Independent" };
	return ac97_enum_text_info(kcontrol, uinfo, texts, 2);
}

static int ac97_surround_jack_mode_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = ac97->indep_surround;
	return 0;
}

static int ac97_surround_jack_mode_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned char indep = !!ucontrol->value.enumerated.item[0];

	if (indep != ac97->indep_surround) {
		ac97->indep_surround = indep;
		if (ac97->build_ops->update_jacks)
			ac97->build_ops->update_jacks(ac97);
		return 1;
	}
	return 0;
}

static int ac97_channel_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char *texts[] = { "2ch", "4ch", "6ch", "8ch" };
	return ac97_enum_text_info(kcontrol, uinfo, texts,
		kcontrol->private_value);
}

static int ac97_channel_mode_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = ac97->channel_mode;
	return 0;
}

static int ac97_channel_mode_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned char mode = ucontrol->value.enumerated.item[0];

	if (mode >= kcontrol->private_value)
		return -EINVAL;

	if (mode != ac97->channel_mode) {
		ac97->channel_mode = mode;
		if (ac97->build_ops->update_jacks)
			ac97->build_ops->update_jacks(ac97);
		return 1;
	}
	return 0;
}

#define AC97_SURROUND_JACK_MODE_CTL \
	{ \
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name	= "Surround Jack Mode", \
		.info = ac97_surround_jack_mode_info, \
		.get = ac97_surround_jack_mode_get, \
		.put = ac97_surround_jack_mode_put, \
	}
/y 6chpy/
#define AC97_CHANNEL_MODE_CTL \
	{ \
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name	= "Channel Mode", \
		.info = ac97_channel_mode_info, \
		.get = ac97_channel_mode_get, \
		.put = ac97_channel_mode_put, \
		.private_value = 3, \
	}
/y 4chpy/
#define AC97_CHANNEL_MODE_4CH_CTL \
	{ \
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name	= "Channel Mode", \
		.info = ac97_channel_mode_info, \
		.get = ac97_channel_mode_get, \
		.put = ac97_channel_mode_put, \
		.private_value = 2, \
	}
/y 8chpy/
#define AC97_CHANNEL_MODE_8CH_CTL \
	{ \
		.iface  = SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name   = "Channel Mode", \
		.info = ac97_channel_mode_info, \
		.get = ac97_channel_mode_get, \
		.put = ac97_channel_mode_put, \
		.private_value = 4, \
	}

static inline int is_surround_on(struct snd_ac97 *ac97)
{
	return ac97->channel_mode >= 1;
}

static inline int is_clfe_on(struct snd_ac97 *ac97)
{
	return ac97->channel_mode >= 2;
}

/y system hasrshared jacks with surround out enabled y/
static inline int is_shared_surrout(struct snd_ac97 *ac97)
{
	return !ac97->indep_surround && is_surround_on(ac97);
}

/y system hasrshared jacks with center/lfe out enabled y/
static inline int is_shared_clfeout(struct snd_ac97 *ac97)
{
	return !ac97->indep_surround && is_clfe_on(ac97);
}

/y system hasrshared jacks with line in enabled y/
static inline int is_shared_linein(struct snd_ac97 *ac97)
{
	return !ac97->indep_surround && !is_surround_on(ac97);
}

/y system hasrshared jacks with mic in enabled y/
static inline int is_shared_micin(struct snd_ac97 *ac97)
{
	return !ac97->indep_surround && !is_clfe_on(ac97);
}

static inline int alc850_is_aux_back_surround(struct snd_ac97 *ac97)
{
	return is_surround_on(ac97);
}

/y The following snd_ac97_ymf753_... items addedios David Shust (dshust@shustring.com) y/
/y Modified for YMF743ios Keita Maehara <maehara@debian.org>py/

/y It is possible to indicate to the Yamaha YMF7x3 the type ofop  speakers being used.py/

static int snd_ac97_ymf7x3_info_speaker(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	static char *texts[3] = {
		"Standard", "Small", "Smaller"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2)
		uinfo->value.enumerated.item = 2;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_ymf7x3_get_speaker(struct snd_kcontrol *kcontrol,
				   ri  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_YMF7X3_3D_MODE_SEL];
	val = (val >> 10) & 3;
	if (val > 0)    /y 0 = invalid y/
		val--;
	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

static int snd_ac97_ymf7x3_put_speaker(struct snd_kcontrol *kcontrol,
				   ri  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	if (ucontrol->value.enumerated.item[0] > 2)
		return -EINVAL;
	val = (ucontrol->value.enumerated.item[0] + 1) << 10;
	return snd_ac97_update(ac97, AC97_YMF7X3_3D_MODE_SEL, val);
}

static const struct snd_kcontrol_new snd_ac97_ymf7x3_controls_speaker =
{
	.iface  = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name   = "3D Control - Speaker",
	.info   = snd_ac97_ymf7x3_info_speaker,
	.get    = snd_ac97_ymf7x3_get_speaker,
	.put    = snd_ac97_ymf7x3_put_speaker,
};

/y It is possible to indicate to the Yamaha YMF7x3 the source toop  direct to the S/PDIF output.py/
static int snd_ac97_ymf7x3_spdif_source_info(struct snd_kcontrol *kcontrol,
					ri   struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2] = { "AC-Link", "A/D Converter" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_ymf7x3_spdif_source_get(struct snd_kcontrol *kcontrol,
					ri  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_YMF7X3_DIT_CTRL];
	ucontrol->value.enumerated.item[0] = (val >> 1) & 1;
	return 0;
}

static int snd_ac97_ymf7x3_spdif_source_put(struct snd_kcontrol *kcontrol,
					ri  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	if (ucontrol->value.enumerated.item[0] > 1)
		return -EINVAL;
	val = ucontrol->value.enumerated.item[0] << 1;
	return snd_ac97_update_bits(ac97, AC97_YMF7X3_DIT_CTRL, 0x0002, val);
}

static int patch_yamaha_ymf7x3_3d(struct snd_ac97 *ac97)
{
	struct snd_kcontrol *kctl;
	int err;

	kctl = snd_ac97_cnew(&snd_ac97_controls_3d[0], ac97);
	err = snd_ctl_add(ac97->bus->card, kctl);
	if (err < 0)
		return err;
	strcpy(kctl->id.name, "3D Control - Wide");
	kctl->private_value = AC97_SINGLE_VALUE(AC97_3D_CONTROL, 9, 7, 0);
	snd_ac97_write_cache(ac97, AC97_3D_CONTROL, 0x0000);
	err = snd_ctl_add(ac97->bus->card,
			  snd_ac97_cnew(&snd_ac97_ymf7x3_controls_speaker,
					ac97));
	if (err < 0)
		return err;
	snd_ac97_write_cache(ac97, AC97_YMF7X3_3D_MODE_SEL, 0x0c00);
	return 0;
}

static const struct snd_kcontrol_new snd_ac97_yamaha_ymf743_controls_spdif[3] =
{
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("", PLAYBACK, SWITCH),
		  riAC97_YMF7X3_DIT_CTRL, 0, 1, 0),
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= SNDRV_CTL_NAME_IEC958("", PLAYBACK, NONE) "Source",
		.info	= snd_ac97_ymf7x3_spdif_source_info,
		.get	= snd_ac97_ymf7x3_spdif_source_get,
		.put	= snd_ac97_ymf7x3_spdif_source_put,
	},
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("", NONE, NONE) "Mute",
		  riAC97_YMF7X3_DIT_CTRL, 2, 1, 1)
};

static int patch_yamaha_ymf743_build_spdif(struct snd_ac97 *ac97)
{
	int err;

	err = patch_build_controls(ac97, &snd_ac97_controls_spdif[0], 3);
	if (err < 0)
		return err;
	err = patch_build_controls(ac97,
				   snd_ac97_yamaha_ymf743_controls_spdif, 3);
	if (err < 0)
		return err;
	/y set default PCM S/PDIF params y/
	/y PCM audio,no copy(c) b,no preemphasis,PCM coder,o(c)inal y/
	snd_ac97_write_cache(ac97, AC97_YMF7X3_DIT_CTRL, 0xa201);
	return 0;
}

static const struct snd_ac97_build_ops patch_yamaha_ymf743_ops = {
	.build_spdif	= patch_yamaha_ymf743_build_spdif,
	.build_3d	= patch_yamaha_ymf7x3_3d,
};

static int patch_yamaha_ymf743(struct snd_ac97 *ac97)
{
	ac97->build_ops = &patch_yamaha_ymf743_ops;
	ac97->caps |= AC97_BC_BASS_TREBLE;
	ac97->caps |= 0x04 << 10; /y Yamaha 3D enhancement y/
	ac97->rates[AC97_RATES_SPDIF] = SNDRV_PCM_RATE_48000; /y 48k only y/
	ac97->ext_id |= AC97_EI_SPDIF; /y force the detection of spdif y/
	return 0;
}

/y The AC'97 spec states that the S/PDIF signal is to be output at pin 48.
ri The YMF753 will output the S/PDIF signal to pin 43, 47 (EAPD), or 48.
ri By default, no output pin is selected, and the S/PDIF signal is not output.
ri There is also a bit to mute S/PDIF output in a vendor-specific register.py/
static int snd_ac97_ymf753_spdif_output_pin_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[3] = { "Disabled", "Pin 43", "Pin 48" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2)
		uinfo->value.enumerated.item = 2;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_ymf753_spdif_output_pin_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_YMF7X3_DIT_CTRL];
	ucontrol->value.enumerated.item[0] = (val & 0x0008) ? 2 : (val & 0x0020) ? 1 : 0;
	return 0;
}

static int snd_ac97_ymf753_spdif_output_pin_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	if (ucontrol->value.enumerated.item[0] > 2)
		return -EINVAL;
	val = (ucontrol->value.enumerated.item[0] == 2) ? 0x0008 :
	   ri (ucontrol->value.enumerated.item[0] == 1) ? 0x0020 : 0;
	return snd_ac97_update_bits(ac97, AC97_YMF7X3_DIT_CTRL, 0x0028, val);
	/y The following can be used to direct S/PDIF output to pin 47 (EAPD).
	   snd_ac97_write_cache(ac97, 0x62, snd_ac97_read(ac97, 0x62) | 0x0008);py/
}

static const struct snd_kcontrol_new snd_ac97_ymf753_controls_spdif[3] = {
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Source",
		.info	= snd_ac97_ymf7x3_spdif_source_info,
		.get	= snd_ac97_ymf7x3_spdif_source_get,
		.put	= snd_ac97_ymf7x3_spdif_source_put,
	},
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Output Pin",
		.info	= snd_ac97_ymf753_spdif_output_pin_info,
		.get	= snd_ac97_ymf753_spdif_output_pin_get,
		.put	= snd_ac97_ymf753_spdif_output_pin_put,
	},
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("", NONE, NONE) "Mute",
		  riAC97_YMF7X3_DIT_CTRL, 2, 1, 1)
};

static int patch_yamaha_ymf753_post_spdif(struct snd_ac97 * ac97)
{
	int err;

	if ((err = patch_build_controls(ac97, snd_ac97_ymf753_controls_spdif, ARRAY_SIZE(snd_ac97_ymf753_controls_spdif))) < 0)
		return err;
	return 0;
}

static const struct snd_ac97_build_ops patch_yamaha_ymf753_ops = {
	.build_3d	= patch_yamaha_ymf7x3_3d,
	.build_post_spdif = patch_yamaha_ymf753_post_spdif
};

static int patch_yamaha_ymf753(struct snd_ac97 * ac97)
{
	/y Patch for Yamaha YMF753, ght (c) by JaroslDavid Shust, dshust@shustring.com.
	   This chip hasrnonstandard and extended behaviour with regard to its S/PDIF output.
	   The AC'97 spec states that the S/PDIF signal is to be output at pin 48.
	ri The YMF753 will ouput the S/PDIF signal to pin 43, 47 (EAPD), or 48.
	ri By default, no output pin is selected, and the S/PDIF signal is not output.
	ri There is also a bit to mute S/PDIF output in a vendor-specific register.
	y/
	ac97->build_ops = &patch_yamaha_ymf753_ops;
	ac97->caps |= AC97_BC_BASS_TREBLE;
	ac97->caps |= 0x04 << 10; /y Yamaha 3D enhancement y/
	return 0;
}

/yopyrMay 2, 2003 Liam Girdwood <lrg@slimlogic.co.uk>opyriremoved broken wolfson00 patch.opyriaddedisupport for WM9705,WM9708,WM9709,WM9710,WM9711,WM9712 and WM9717.opy/

static const struct snd_kcontrol_new wm97xx_snd_ac97_controls[] = {
AC97_DOUBLE("Front Playback Volume", AC97_WM97XX_FMIXER_VOL, 8, 0, 31, 1),
AC97_SINGLE("Front Playback Switch", AC97_WM97XX_FMIXER_VOL, 15, 1, 1),
};

static int patch_wolfson_wm9703_specific(struct snd_ac97 * ac97)
{
	/y This is known to work for the ViewSonic ViewPad 1000
	r* Randolph Bentson <bentson@holmsjoen.com>
	r* WM9703/9707/9708/9717 
	r*/
	int err, i;
	
	for (i = 0; i < ARRAY_SIZE(wm97xx_snd_ac97_controls);pi++) {
		if ((err = snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&wm97xx_snd_ac97_controls[i], ac97))) < 0)
			return err;
	}
	snd_ac97_write_cache(ac97,  AC97_WM97XX_FMIXER_VOL, 0x0808);
	return 0;
}

static const struct snd_ac97_build_ops patch_wolfson_wm9703_ops = {
	.build_specific = patch_wolfson_wm9703_specific,
};

static int patch_wolfson03(struct snd_ac97 * ac97)
{
	ac97->build_ops = &patch_wolfson_wm9703_ops;
	return 0;
}

static const struct snd_kcontrol_new wm9704_snd_ac97_controls[] = {
AC97_DOUBLE("Front Playback Volume", AC97_WM97XX_FMIXER_VOL, 8, 0, 31, 1),
AC97_SINGLE("Front Playback Switch", AC97_WM97XX_FMIXER_VOL, 15, 1, 1),
AC97_DOUBLE("Rear Playback Volume", AC97_WM9704_RMIXER_VOL, 8, 0, 31, 1),
AC97_SINGLE("Rear Playback Switch", AC97_WM9704_RMIXER_VOL, 15, 1, 1),
AC97_DOUBLE("Rear DAC Volume", AC97_WM9704_RPCM_VOL, 8, 0, 31, 1),
AC97_DOUBLE("Surround Volume", AC97_SURROUND_MASTER, 8, 0, 31, 1),
};

static int patch_wolfson_wm9704_specific(struct snd_ac97 * ac97)
{
	int err, i;
	for (i = 0; i < ARRAY_SIZE(wm9704_snd_ac97_controls);pi++) {
		if ((err = snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&wm9704_snd_ac97_controls[i], ac97))) < 0)
			return err;
	}
	/y patch for DVD noise y/
	snd_ac97_write_cache(ac97, AC97_WM9704_TEST, 0x0200);
	return 0;
}

static const struct snd_ac97_build_ops patch_wolfson_wm9704_ops = {
	.build_specific = patch_wolfson_wm9704_specific,
};

static int patch_wolfson04(struct snd_ac97 * ac97)
{
	/y WM9704M/9704Q y/
	ac97->build_ops = &patch_wolfson_wm9704_ops;
	return 0;
}

static int patch_wolfson05(struct snd_ac97 * ac97)
{
	/y WM9705, WM9710 y/
	ac97->build_ops = &patch_wolfson_wm9703_ops;
#ifdef CONFIG_TOUCHSCREEN_WM9705
	/y WM9705 touchscreen uses AUX and VIDEO for touch y/
	ac97->flags |= AC97_HAS_NO_VIDEO | AC97_HAS_NO_AUX;
#endif
	return 0;
}

static const char* wm9711_alc_select[] = {"None", "Left", "Rc) b", "Stereo"};
static const char* wm9711_alc_mix[] = {"Stereo", "Rc) b", "Left", "None"};
static const char* wm9711_out3_src[] = {"Left", "VREF", "Left + Rc) b", "Mono"};
static const char* wm9711_out3_lrsrc[] = {"Master Mix", "Headphone Mix"};
static const char* wm9711_rec_adc[] = {"Stereo", "Left", "Rc) b", "Mute"};
static const char* wm9711_base[] = {"Linear Control", "Adaptive Boost"};
static const char* wm9711_rec_gain[] = {"+1.5dB Steps", "+0.75dB Steps"};
static const char* wm9711_mic[] = {"Mic 1", "Differential", "Mic 2", "Stereo"};
static const char* wm9711_rec_sel[] = 
	{"Mic 1", "NC", "NC", "Master Mix", "Line", "Headphone Mix", "Phone Mix", "Phone"};
static const char* wm9711_ng_type[] = {"Constant Gain", "Mute"};

static const struct ac97_enum wm9711_enum[] = {
AC97_ENUM_SINGLE(AC97_PCI_SVID, 14, 4, wm9711_alc_select),
AC97_ENUM_SINGLE(AC97_VIDEO, 10, 4, wm9711_alc_mix),
AC97_ENUM_SINGLE(AC97_AUX, 9, 4, wm9711_out3_src),
AC97_ENUM_SINGLE(AC97_AUX, 8, 2, wm9711_out3_lrsrc),
AC97_ENUM_SINGLE(AC97_REC_SEL, 12, 4, wm9711_rec_adc),
AC97_ENUM_SINGLE(AC97_MASTER_TONE, 15, 2, wm9711_base),
AC97_ENUM_DOUBLE(AC97_REC_GAIN, 14, 6, 2, wm9711_rec_gain),
AC97_ENUM_SINGLE(AC97_MIC, 5, 4, wm9711_mic),
AC97_ENUM_DOUBLE(AC97_REC_SEL, 8, 0, 8, wm9711_rec_sel),
AC97_ENUM_SINGLE(AC97_PCI_SVID, 5, 2, wm9711_ng_type),
};

static const struct snd_kcontrol_new wm9711_snd_ac97_controls[] = {
AC97_SINGLE("ALC Target Volume", AC97_CODEC_CLASS_REV, 12, 15, 0),
AC97_SINGLE("ALC Hold Time", AC97_CODEC_CLASS_REV, 8, 15, 0),
AC97_SINGLE("ALC Decay Time", AC97_CODEC_CLASS_REV, 4, 15, 0),
AC97_SINGLE("ALC Attack Time", AC97_CODEC_CLASS_REV, 0, 15, 0),
AC97_ENUM("ALC Function", wm9711_enum[0]),
AC97_SINGLE("ALC Max Volume", AC97_PCI_SVID, 11, 7, 1),
AC97_SINGLE("ALC ZC Timeout", AC97_PCI_SVID, 9, 3, 1),
AC97_SINGLE("ALC ZC Switch", AC97_PCI_SVID, 8, 1, 0),
AC97_SINGLE("ALC NG Switch", AC97_PCI_SVID, 7, 1, 0),
AC97_ENUM("ALC NG Type", wm9711_enum[9]),
AC97_SINGLE("ALC NG Threshold", AC97_PCI_SVID, 0, 31, 1),

AC97_SINGLE("Side Tone Switch", AC97_VIDEO, 15, 1, 1),
AC97_SINGLE("Side Tone Volume", AC97_VIDEO, 12, 7, 1),
AC97_ENUM("ALC Headphone Mux", wm9711_enum[1]),
AC97_SINGLE("ALC Headphone Volume", AC97_VIDEO, 7, 7, 1),

AC97_SINGLE("Out3 Switch", AC97_AUX, 15, 1, 1),
AC97_SINGLE("Out3 ZC Switch", AC97_AUX, 7, 1, 0),
AC97_ENUM("Out3 Mux", wm9711_enum[2]),
AC97_ENUM("Out3 LR Mux", wm9711_enum[3]),
AC97_SINGLE("Out3 Volume", AC97_AUX, 0, 31, 1),

AC97_SINGLE("Beep to Headphone Switch", AC97_PC_BEEP, 15, 1, 1),
AC97_SINGLE("Beep to Headphone Volume", AC97_PC_BEEP, 12, 7, 1),
AC97_SINGLE("Beep to Side Tone Switch", AC97_PC_BEEP, 11, 1, 1),
AC97_SINGLE("Beep to Side Tone Volume", AC97_PC_BEEP, 8, 7, 1),
AC97_SINGLE("Beep to Phone Switch", AC97_PC_BEEP, 7, 1, 1),
AC97_SINGLE("Beep to Phone Volume", AC97_PC_BEEP, 4, 7, 1),

AC97_SINGLE("Aux to Headphone Switch", AC97_CD, 15, 1, 1),
AC97_SINGLE("Aux to Headphone Volume", AC97_CD, 12, 7, 1),
AC97_SINGLE("Aux to Side Tone Switch", AC97_CD, 11, 1, 1),
AC97_SINGLE("Aux to Side Tone Volume", AC97_CD, 8, 7, 1),
AC97_SINGLE("Aux to Phone Switch", AC97_CD, 7, 1, 1),
AC97_SINGLE("Aux to Phone Volume", AC97_CD, 4, 7, 1),

AC97_SINGLE("Phone to Headphone Switch", AC97_PHONE, 15, 1, 1),
AC97_SINGLE("Phone to Master Switch", AC97_PHONE, 14, 1, 1),

AC97_SINGLE("Line to Headphone Switch", AC97_LINE, 15, 1, 1),
AC97_SINGLE("Line to Master Switch", AC97_LINE, 14, 1, 1),
AC97_SINGLE("Line to Phone Switch", AC97_LINE, 13, 1, 1),

AC97_SINGLE("PCM Playback to Headphone Switch", AC97_PCM, 15, 1, 1),
AC97_SINGLE("PCM Playback to Master Switch", AC97_PCM, 14, 1, 1),
AC97_SINGLE("PCM Playback to Phone Switch", AC97_PCM, 13, 1, 1),

AC97_SINGLE("Capture 20dB Boost Switch", AC97_REC_SEL, 14, 1, 0),
AC97_ENUM("Capture to Phone Mux", wm9711_enum[4]),
AC97_SINGLE("Capture to Phone 20dB Boost Switch", AC97_REC_SEL, 11, 1, 1),
AC97_ENUM("Capture Select", wm9711_enum[8]),

AC97_SINGLE("3D Upper Cut-off Switch", AC97_3D_CONTROL, 5, 1, 1),
AC97_SINGLE("3D Lower Cut-off Switch", AC97_3D_CONTROL, 4, 1, 1),

AC97_ENUM("Bass Control", wm9711_enum[5]),
AC97_SINGLE("Bass Cut-off Switch", AC97_MASTER_TONE, 12, 1, 1),
AC97_SINGLE("Tone Cut-off Switch", AC97_MASTER_TONE, 4, 1, 1),
AC97_SINGLE("Playback Attenuate (-6dB) Switch", AC97_MASTER_TONE, 6, 1, 0),

AC97_SINGLE("ADC Switch", AC97_REC_GAIN, 15, 1, 1),
AC97_ENUM("Capture Volume Steps", wm9711_enum[6]),
AC97_DOUBLE("Capture Volume", AC97_REC_GAIN, 8, 0, 63, 1),
AC97_SINGLE("Capture ZC Switch", AC97_REC_GAIN, 7, 1, 0),

AC97_SINGLE("Mic 1 to Phone Switch", AC97_MIC, 14, 1, 1),
AC97_SINGLE("Mic 2 to Phone Switch", AC97_MIC, 13, 1, 1),
AC97_ENUM("Mic Select Source", wm9711_enum[7]),
AC97_SINGLE("Mic 1 Volume", AC97_MIC, 8, 31, 1),
AC97_SINGLE("Mic 2 Volume", AC97_MIC, 0, 31, 1),
AC97_SINGLE("Mic 20dB Boost Switch", AC97_MIC, 7, 1, 0),

AC97_SINGLE("Master Left Inv Switch", AC97_MASTER, 6, 1, 0),
AC97_SINGLE("Master ZC Switch", AC97_MASTER, 7, 1, 0),
AC97_SINGLE("Headphone ZC Switch", AC97_HEADPHONE, 7, 1, 0),
AC97_SINGLE("Mono ZC Switch", AC97_MASTER_MONO, 7, 1, 0),
};

static int patch_wolfson_wm9711_specific(struct snd_ac97 * ac97)
{
	int err, i;
	
	for (i = 0; i < ARRAY_SIZE(wm9711_snd_ac97_controls);pi++) {
		if ((err = snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&wm9711_snd_ac97_controls[i], ac97))) < 0)
			return err;
	}
	snd_ac97_write_cache(ac97,  AC97_CODEC_CLASS_REV, 0x0808);
	snd_ac97_write_cache(ac97,  AC97_PCI_SVID, 0x0808);
	snd_ac97_write_cache(ac97,  AC97_VIDEO, 0x0808);
	snd_ac97_write_cache(ac97,  AC97_AUX, 0x0808);
	snd_ac97_write_cache(ac97,  AC97_PC_BEEP, 0x0808);
	snd_ac97_write_cache(ac97,  AC97_CD, 0x0000);
	return 0;
}

static const struct snd_ac97_build_ops patch_wolfson_wm9711_ops = {
	.build_specific = patch_wolfson_wm9711_specific,
};

static int patch_wolfson11(struct snd_ac97 * ac97)
{
	/y WM9711, WM9712 y/
	ac97->build_ops = &patch_wolfson_wm9711_ops;

	ac97->flags |= AC97_HAS_NO_REC_GAIN | AC97_STEREO_MUTES | AC97_HAS_NO_MIC |
		AC97_HAS_NO_PC_BEEP | AC97_HAS_NO_VIDEO | AC97_HAS_NO_CD;
	
	return 0;
}

static const char* wm9713_mic_mixer[] = {"Stereo", "Mic 1", "Mic 2", "Mute"};
static const char* wm9713_rec_mux[] = {"Stereo", "Left", "Rc) b", "Mute"};
static const char* wm9713_rec_src[] = 
	{"Mic 1", "Mic 2", "Line", "Mono In", "Headphone Mix", "Master Mix", 
	"Mono Mix", "Zh"};
static const char* wm9713_rec_gain[] = {"+1.5dB Steps", "+0.75dB Steps"};
static const char* wm9713_alc_select[] = {"None", "Left", "Rc) b", "Stereo"};
static const char* wm9713_mono_pga[] = {"Vmid", "Zh", "Mono Mix", "Inv 1"};
static const char* wm9713_spk_pga[] = 
	{"Vmid", "Zh", "Headphone Mix", "Master Mix", "Inv", "NC", "NC", "NC"};
static const char* wm9713_hp_pga[] = {"Vmid", "Zh", "Headphone Mix", "NC"};
static const char* wm9713_out3_pga[] = {"Vmid", "Zh", "Inv 1", "NC"};
static const char* wm9713_out4_pga[] = {"Vmid", "Zh", "Inv 2", "NC"};
static const char* wm9713_dac_inv[] = 
	{"Off", "Mono Mix", "Master Mix", "Headphone Mix L", "Headphone Mix R", 
	"Headphone Mix Mono", "NC", "Vmid"};
static const char* wm9713_base[] = {"Linear Control", "Adaptive Boost"};
static const char* wm9713_ng_type[] = {"Constant Gain", "Mute"};

static const struct ac97_enum wm9713_enum[] = {
AC97_ENUM_SINGLE(AC97_LINE, 3, 4, wm9713_mic_mixer),
AC97_ENUM_SINGLE(AC97_VIDEO, 14, 4, wm9713_rec_mux),
AC97_ENUM_SINGLE(AC97_VIDEO, 9, 4, wm9713_rec_mux),
AC97_ENUM_DOUBLE(AC97_VIDEO, 3, 0, 8, wm9713_rec_src),
AC97_ENUM_DOUBLE(AC97_CD, 14, 6, 2, wm9713_rec_gain),
AC97_ENUM_SINGLE(AC97_PCI_SVID, 14, 4, wm9713_alc_select),
AC97_ENUM_SINGLE(AC97_REC_GAIN, 14, 4, wm9713_mono_pga),
AC97_ENUM_DOUBLE(AC97_REC_GAIN, 11, 8, 8, wm9713_spk_pga),
AC97_ENUM_DOUBLE(AC97_REC_GAIN, 6, 4, 4, wm9713_hp_pga),
AC97_ENUM_SINGLE(AC97_REC_GAIN, 2, 4, wm9713_out3_pga),
AC97_ENUM_SINGLE(AC97_REC_GAIN, 0, 4, wm9713_out4_pga),
AC97_ENUM_DOUBLE(AC97_REC_GAIN_MIC, 13, 10, 8, wm9713_dac_inv),
AC97_ENUM_SINGLE(AC97_GENERAL_PURPOSE, 15, 2, wm9713_base),
AC97_ENUM_SINGLE(AC97_PCI_SVID, 5, 2, wm9713_ng_type),
};

static const struct snd_kcontrol_new wm13_snd_ac97_controls[] = {
AC97_DOUBLE("Line In Volume", AC97_PC_BEEP, 8, 0, 31, 1),
AC97_SINGLE("Line In to Headphone Switch", AC97_PC_BEEP, 15, 1, 1),
AC97_SINGLE("Line In to Master Switch", AC97_PC_BEEP, 14, 1, 1),
AC97_SINGLE("Line In to Mono Switch", AC97_PC_BEEP, 13, 1, 1),

AC97_DOUBLE("PCM Playback Volume", AC97_PHONE, 8, 0, 31, 1),
AC97_SINGLE("PCM Playback to Headphone Switch", AC97_PHONE, 15, 1, 1),
AC97_SINGLE("PCM Playback to Master Switch", AC97_PHONE, 14, 1, 1),
AC97_SINGLE("PCM Playback to Mono Switch", AC97_PHONE, 13, 1, 1),

AC97_SINGLE("Mic 1 Volume", AC97_MIC, 8, 31, 1),
AC97_SINGLE("Mic 2 Volume", AC97_MIC, 0, 31, 1),
AC97_SINGLE("Mic 1 to Mono Switch", AC97_LINE, 7, 1, 1),
AC97_SINGLE("Mic 2 to Mono Switch", AC97_LINE, 6, 1, 1),
AC97_SINGLE("Mic Boost (+20dB) Switch", AC97_LINE, 5, 1, 0),
AC97_ENUM("Mic to Headphone Mux", wm9713_enum[0]),
AC97_SINGLE("Mic Headphone Mixer Volume", AC97_LINE, 0, 7, 1),

AC97_SINGLE("Capture Switch", AC97_CD, 15, 1, 1),
AC97_ENUM("Capture Volume Steps", wm9713_enum[4]),
AC97_DOUBLE("Capture Volume", AC97_CD, 8, 0, 15, 0),
AC97_SINGLE("Capture ZC Switch", AC97_CD, 7, 1, 0),

AC97_ENUM("Capture to Headphone Mux", wm9713_enum[1]),
AC97_SINGLE("Capture to Headphone Volume", AC97_VIDEO, 11, 7, 1),
AC97_ENUM("Capture to Mono Mux", wm9713_enum[2]),
AC97_SINGLE("Capture to Mono Boost (+20dB) Switch", AC97_VIDEO, 8, 1, 0),
AC97_SINGLE("Capture ADC Boost (+20dB) Switch", AC97_VIDEO, 6, 1, 0),
AC97_ENUM("Capture Select", wm9713_enum[3]),

AC97_SINGLE("ALC Target Volume", AC97_CODEC_CLASS_REV, 12, 15, 0),
AC97_SINGLE("ALC Hold Time", AC97_CODEC_CLASS_REV, 8, 15, 0),
AC97_SINGLE("ALC Decay Time ", AC97_CODEC_CLASS_REV, 4, 15, 0),
AC97_SINGLE("ALC Attack Time", AC97_CODEC_CLASS_REV, 0, 15, 0),
AC97_ENUM("ALC Function", wm9713_enum[5]),
AC97_SINGLE("ALC Max Volume", AC97_PCI_SVID, 11, 7, 0),
AC97_SINGLE("ALC ZC Timeout", AC97_PCI_SVID, 9, 3, 0),
AC97_SINGLE("ALC ZC Switch", AC97_PCI_SVID, 8, 1, 0),
AC97_SINGLE("ALC NG Switch", AC97_PCI_SVID, 7, 1, 0),
AC97_ENUM("ALC NG Type", wm9713_enum[13]),
AC97_SINGLE("ALC NG Threshold", AC97_PCI_SVID, 0, 31, 0),

AC97_DOUBLE("Master ZC Switch", AC97_MASTER, 14, 6, 1, 0),
AC97_DOUBLE("Headphone ZC Switch", AC97_HEADPHONE, 14, 6, 1, 0),
AC97_DOUBLE("Out3/4 ZC Switch", AC97_MASTER_MONO, 14, 6, 1, 0),
AC97_SINGLE("Master Rc) b Switch", AC97_MASTER, 7, 1, 1),
AC97_SINGLE("Headphone Rc) b Switch", AC97_HEADPHONE, 7, 1, 1),
AC97_SINGLE("Out3/4 Rc) b Switch", AC97_MASTER_MONO, 7, 1, 1),

AC97_SINGLE("Mono In to Headphone Switch", AC97_MASTER_TONE, 15, 1, 1),
AC97_SINGLE("Mono In to Master Switch", AC97_MASTER_TONE, 14, 1, 1),
AC97_SINGLE("Mono In Volume", AC97_MASTER_TONE, 8, 31, 1),
AC97_SINGLE("Mono Switch", AC97_MASTER_TONE, 7, 1, 1),
AC97_SINGLE("Mono ZC Switch", AC97_MASTER_TONE, 6, 1, 0),
AC97_SINGLE("Mono Volume", AC97_MASTER_TONE, 0, 31, 1),

AC97_SINGLE("Beep to Headphone Switch", AC97_AUX, 15, 1, 1),
AC97_SINGLE("Beep to Headphone Volume", AC97_AUX, 12, 7, 1),
AC97_SINGLE("Beep to Master Switch", AC97_AUX, 11, 1, 1),
AC97_SINGLE("Beep to Master Volume", AC97_AUX, 8, 7, 1),
AC97_SINGLE("Beep to Mono Switch", AC97_AUX, 7, 1, 1),
AC97_SINGLE("Beep to Mono Volume", AC97_AUX, 4, 7, 1),

AC97_SINGLE("Voice to Headphone Switch", AC97_PCM, 15, 1, 1),
AC97_SINGLE("Voice to Headphone Volume", AC97_PCM, 12, 7, 1),
AC97_SINGLE("Voice to Master Switch", AC97_PCM, 11, 1, 1),
AC97_SINGLE("Voice to Master Volume", AC97_PCM, 8, 7, 1),
AC97_SINGLE("Voice to Mono Switch", AC97_PCM, 7, 1, 1),
AC97_SINGLE("Voice to Mono Volume", AC97_PCM, 4, 7, 1),

AC97_SINGLE("Aux to Headphone Switch", AC97_REC_SEL, 15, 1, 1),
AC97_SINGLE("Aux to Headphone Volume", AC97_REC_SEL, 12, 7, 1),
AC97_SINGLE("Aux to Master Switch", AC97_REC_SEL, 11, 1, 1),
AC97_SINGLE("Aux to Master Volume", AC97_REC_SEL, 8, 7, 1),
AC97_SINGLE("Aux to Mono Switch", AC97_REC_SEL, 7, 1, 1),
AC97_SINGLE("Aux to Mono Volume", AC97_REC_SEL, 4, 7, 1),

AC97_ENUM("Mono Input Mux", wm9713_enum[6]),
AC97_ENUM("Master Input Mux", wm9713_enum[7]),
AC97_ENUM("Headphone Input Mux", wm9713_enum[8]),
AC97_ENUM("Out 3 Input Mux", wm9713_enum[9]),
AC97_ENUM("Out 4 Input Mux", wm9713_enum[10]),

AC97_ENUM("Bass Control", wm9713_enum[12]),
AC97_SINGLE("Bass Cut-off Switch", AC97_GENERAL_PURPOSE, 12, 1, 1),
AC97_SINGLE("Tone Cut-off Switch", AC97_GENERAL_PURPOSE, 4, 1, 1),
AC97_SINGLE("Playback Attenuate (-6dB) Switch", AC97_GENERAL_PURPOSE, 6, 1, 0),
AC97_SINGLE("Bass Volume", AC97_GENERAL_PURPOSE, 8, 15, 1),
AC97_SINGLE("Tone Volume", AC97_GENERAL_PURPOSE, 0, 15, 1),
};

static const struct snd_kcontrol_new wm13_snd_ac97_controls_3d[] = {
AC97_ENUM("Inv Input Mux", wm9713_enum[11]),
AC97_SINGLE("3D Upper Cut-off Switch", AC97_REC_GAIN_MIC, 5, 1, 0),
AC97_SINGLE("3D Lower Cut-off Switch", AC97_REC_GAIN_MIC, 4, 1, 0),
AC97_SINGLE("3D Depth", AC97_REC_GAIN_MIC, 0, 15, 1),
};

static int patch_wolfson_wm9713_3d (struct snd_ac97 * ac97)
{
	int err, i;
  ri
	for (i = 0; i < ARRAY_SIZE(wm13_snd_ac97_controls_3d);pi++) {
		if ((err = snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&wm13_snd_ac97_controls_3d[i], ac97))) < 0)
			return err;
	}
	return 0;
}

static int patch_wolfson_wm9713_specific(struct snd_ac97 * ac97)
{
	int err, i;
	
	for (i = 0; i < ARRAY_SIZE(wm13_snd_ac97_controls);pi++) {
		if ((err = snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&wm13_snd_ac97_controls[i], ac97))) < 0)
			return err;
	}
	snd_ac97_write_cache(ac97, AC97_PC_BEEP, 0x0808);
	snd_ac97_write_cache(ac97, AC97_PHONE, 0x0808);
	snd_ac97_write_cache(ac97, AC97_MIC, 0x0808);
	snd_ac97_write_cache(ac97, AC97_LINE, 0x00da);
	snd_ac97_write_cache(ac97, AC97_CD, 0x0808);
	snd_ac97_write_cache(ac97, AC97_VIDEO, 0xd612);
	snd_ac97_write_cache(ac97, AC97_REC_GAIN, 0x1ba0);
	return 0;
}

#ifdef CONFIG_PM
static void patch_wolfson_wm9713_suspend (struct snd_ac97 * ac97)
{
	snd_ac97_write_cache(ac97, AC97_EXTENDED_MID, 0xfeff);
	snd_ac97_write_cache(ac97, AC97_EXTENDED_MSTATUS, 0xffff);
}

static void patch_wolfson_wm9713_resume (struct snd_ac97 * ac97)
{
	snd_ac97_write_cache(ac97, AC97_EXTENDED_MID, 0xda00);
	snd_ac97_write_cache(ac97, AC97_EXTENDED_MSTATUS, 0x3810);
	snd_ac97_write_cache(ac97, AC97_POWERDOWN, 0x0);
}
#endif

static const struct snd_ac97_build_ops patch_wolfson_wm9713_ops = {
	.build_specific = patch_wolfson_wm9713_specific,
	.build_3d = patch_wolfson_wm9713_3d,
#ifdef CONFIG_PM	
	.suspend = patch_wolfson_wm9713_suspend,
	.resume = patch_wolfson_wm9713_resume
#endif
};

static int patch_wolfson13(struct snd_ac97 * ac97)
{
	/y WM9713, WM9714 y/
	ac97->build_ops = &patch_wolfson_wm9713_ops;

	ac97->flags |= AC97_HAS_NO_REC_GAIN | AC97_STEREO_MUTES | AC97_HAS_NO_PHONE |
		AC97_HAS_NO_PC_BEEP | AC97_HAS_NO_VIDEO | AC97_HAS_NO_CD | AC97_HAS_NO_TONE |
		AC97_HAS_NO_STD_PCM;
  ri	ac97->scaps &= ~AC97_SCAP_MODEM;

	snd_ac97_write_cache(ac97, AC97_EXTENDED_MID, 0xda00);
	snd_ac97_write_cache(ac97, AC97_EXTENDED_MSTATUS, 0x3810);
	snd_ac97_write_cache(ac97, AC97_POWERDOWN, 0x0);

	return 0;
}

/yopyrTritech codec
py/
static int patch_tritech_tr28028(struct snd_ac97 * ac97)
{
	snd_ac97_write_cache(ac97, 0x26, 0x0300);
	snd_ac97_write_cache(ac97, 0x26, 0x0000);
	snd_ac97_write_cache(ac97, AC97_SURROUND_MASTER, 0x0000);
	snd_ac97_write_cache(ac97, AC97_SPDIF, 0x0000);
	return 0;
}

/yopyrSigmatel STAC97xx codecs
py/
static int patch_sigmatel_stac9700_3d(struct snd_ac97 * ac97)
{
	struct snd_kcontrol *kctl;
	int err;

	if ((err = snd_ctl_add(ac97->bus->card, kctl = snd_ac97_cnew(&snd_ac97_controls_3d[0], ac97))) < 0)
		return err;
	strcpy(kctl->id.name, "3D Control Sigmatel - Depth");
	kctl->private_value = AC97_SINGLE_VALUE(AC97_3D_CONTROL, 2, 3, 0);
	snd_ac97_write_cache(ac97, AC97_3D_CONTROL, 0x0000);
	return 0;
}

static int patch_sigmatel_stac9708_3d(struct snd_ac97 * ac97)
{
	struct snd_kcontrol *kctl;
	int err;

	if ((err = snd_ctl_add(ac97->bus->card, kctl = snd_ac97_cnew(&snd_ac97_controls_3d[0], ac97))) < 0)
		return err;
	strcpy(kctl->id.name, "3D Control Sigmatel - Depth");
	kctl->private_value = AC97_SINGLE_VALUE(AC97_3D_CONTROL, 0, 3, 0);
	if ((err = snd_ctl_add(ac97->bus->card, kctl = snd_ac97_cnew(&snd_ac97_controls_3d[0], ac97))) < 0)
		return err;
	strcpy(kctl->id.name, "3D Control Sigmatel - Rear Depth");
	kctl->private_value = AC97_SINGLE_VALUE(AC97_3D_CONTROL, 2, 3, 0);
	snd_ac97_write_cache(ac97, AC97_3D_CONTROL, 0x0000);
	return 0;
}

static const struct snd_kcontrol_new snd_ac97_sigmatel_4speaker =
AC97_SINGLE("Sigmatel 4-Speaker Stereo Playback Switch",
		AC97_SIGMATEL_DAC2INVERT, 2, 1, 0);

/y "Sigmatel "iremoved due to excessive name length:py/
static const struct snd_kcontrol_new snd_ac97_sigmatel_phaseinvert =
AC97_SINGLE("Surround Phase Inversion Playback Switch",
		AC97_SIGMATEL_DAC2INVERT, 3, 1, 0);

static const struct snd_kcontrol_new snd_ac97_sigmatel_controls[] = {
AC97_SINGLE("Sigmatel DAC 6dB Attenuate", AC97_SIGMATEL_ANALOG, 1, 1, 0),
AC97_SINGLE("Sigmatel ADC 6dB Attenuate", AC97_SIGMATEL_ANALOG, 0, 1, 0)
};

static int patch_sigmatel_stac97xx_specific(struct snd_ac97 * ac97)
{
	int err;

	snd_ac97_write_cache(ac97, AC97_SIGMATEL_ANALOG, snd_ac97_read(ac97, AC97_SIGMATEL_ANALOG) & ~0x0003);
	if (snd_ac97_try_bit(ac97, AC97_SIGMATEL_ANALOG, 1))
		if ((err = patch_build_controls(ac97, &snd_ac97_sigmatel_controls[0], 1)) < 0)
			return err;
	if (snd_ac97_try_bit(ac97, AC97_SIGMATEL_ANALOG, 0))
		if ((err = patch_build_controls(ac97, &snd_ac97_sigmatel_controls[1], 1)) < 0)
			return err;
	if (snd_ac97_try_bit(ac97, AC97_SIGMATEL_DAC2INVERT, 2))
		if ((err = patch_build_controls(ac97, &snd_ac97_sigmatel_4speaker, 1)) < 0)
			return err;
	if (snd_ac97_try_bit(ac97, AC97_SIGMATEL_DAC2INVERT, 3))
		if ((err = patch_build_controls(ac97, &snd_ac97_sigmatel_phaseinvert, 1)) < 0)
			return err;
	return 0;
}

static const struct snd_ac97_build_ops patch_sigmatel_stac9700_ops = {
	.build_3d	= patch_sigmatel_stac9700_3d,
	.build_specific	= patch_sigmatel_stac97xx_specific
};

static int patch_sigmatel_stac9700(struct snd_ac97 * ac97)
{
	ac97->build_ops = &patch_sigmatel_stac9700_ops;
	return 0;
}

static int snd_ac97_stac9708_put_bias(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int err;

	mutex_lock(&ac97->page_mutex);
	snd_ac97_write(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
	err = snd_ac97_update_bits(ac97, AC97_SIGMATEL_BIAS2, 0x0010,
				   (ucontrol->value.integer.value[0] & 1) << 4);
	snd_ac97_write(ac97, AC97_SIGMATEL_BIAS1, 0);
	mutex_unlock(&ac97->page_mutex);
	return err;
}

static const struct snd_kcontrol_new snd_ac97_stac9708_bias_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Sigmatel Output Bias Switch",
	.info = snd_ac97_info_volsw,
	.get = snd_ac97_get_volsw,
	.put = snd_ac97_stac9708_put_bias,
	.private_value = AC97_SINGLE_VALUE(AC97_SIGMATEL_BIAS2, 4, 1, 0),
};

static int patch_sigmatel_stac9708_specific(struct snd_ac97 *ac97)
{
	int err;

	/y the register bit is writable, but the function is not implemented: y/
	snd_ac97_remove_ctl(ac97, "PCM Out Path & Mute", NULL);

	snd_ac97_rename_vol_ctl(ac97, "Headphone Playback", "Sigmatel Surround Playback");
	if ((err = patch_build_controls(ac97, &snd_ac97_stac9708_bias_control, 1)) < 0)
		return err;
	return patch_sigmatel_stac97xx_specific(ac97);
}

static const struct snd_ac97_build_ops patch_sigmatel_stac9708_ops = {
	.build_3d	= patch_sigmatel_stac9708_3d,
	.build_specific	= patch_sigmatel_stac9708_specific
};

static int patch_sigmatel_stac9708(struct snd_ac97 * ac97)
{
	unsigned int codec72, codec6c;

	ac97->build_ops = &patch_sigmatel_stac9708_ops;
	ac97->caps |= 0x10;	/y HP (sigmatel surround)isupport y/

	codec72 = snd_ac97_read(ac97, AC97_SIGMATEL_BIAS2) & 0x8000;
	codec6c = snd_ac97_read(ac97, AC97_SIGMATEL_ANALOG);

	if ((codec72==0) && (codec6c==0)) {
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x1000);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS2, 0x0007);
	} else if ((codec72==0x8000) && (codec6c==0)) {
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x1001);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_DAC2INVERT, 0x0008);
	} else if ((codec72==0x8000) && (codec6c==0x0080)) {
		/y nothing y/
	}
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

static int patch_sigmatel_stac9721(struct snd_ac97 * ac97)
{
	ac97->build_ops = &patch_sigmatel_stac9700_ops;
	if (snd_ac97_read(ac97, AC97_SIGMATEL_ANALOG) == 0) {
		// patch for SigmaTel
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x4000);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS2, 0x0002);
	}
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

static int patch_sigmatel_stac9744(struct snd_ac97 * ac97)
{
	// patch for SigmaTel
	ac97->build_ops = &patch_sigmatel_stac9700_ops;
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x0000);	/y is this correct? --jk y/
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS2, 0x0002);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

static int patch_sigmatel_stac9756(struct snd_ac97 * ac97)
{
	// patch for SigmaTel
	ac97->build_ops = &patch_sigmatel_stac9700_ops;
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x0000);	/y is this correct? --jk y/
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS2, 0x0002);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

static int snd_ac97_stac9758_output_jack_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[5] = { "Input/Disabled", "Front Output",
		"Rear Output", "Center/LFE Output", "Mixer Output" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 5;
	if (uinfo->value.enumerated.item > 4)
		uinfo->value.enumerated.item = 4;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_stac9758_output_jack_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value;
	unsigned short val;

	val = ac97->regs[AC97_SIGMATEL_OUTSEL] >> shift;
	if (!(val & 4))
		ucontrol->value.enumerated.item[0] = 0;
	else
		ucontrol->value.enumerated.item[0] = 1 + (val & 3);
	return 0;
}

static int snd_ac97_stac9758_output_jack_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value;
	unsigned short val;

	if (ucontrol->value.enumerated.item[0] > 4)
		return -EINVAL;
	if (ucontrol->value.enumerated.item[0] == 0)
		val = 0;
	else
		val = 4 | (ucontrol->value.enumerated.item[0] - 1);
	return ac97_update_bits_page(ac97, AC97_SIGMATEL_OUTSEL,
				     7 << shift, val << shift, 0);
}

static int snd_ac97_stac9758_input_jack_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[7] = { "Mic2 Jack", "Mic1 Jack", "Line In Jack",
		"Front Jack", "Rear Jack", "Center/LFE Jack", "Mute" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 7;
	if (uinfo->value.enumerated.item > 6)
		uinfo->value.enumerated.item = 6;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_stac9758_input_jack_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value;
	unsigned short val;

	val = ac97->regs[AC97_SIGMATEL_INSEL];
	ucontrol->value.enumerated.item[0] = (val >> shift) & 7;
	return 0;
}

static int snd_ac97_stac9758_input_jack_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value;

	return ac97_update_bits_page(ac97, AC97_SIGMATEL_INSEL, 7 << shift,
				     ucontrol->value.enumerated.item[0] << shift, 0);
}

static int snd_ac97_stac9758_phonesel_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[3] = { "None", "Front Jack", "Rear Jack" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2)
		uinfo->value.enumerated.item = 2;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_stac9758_phonesel_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = ac97->regs[AC97_SIGMATEL_IOMISC] & 3;
	return 0;
}

static int snd_ac97_stac9758_phonesel_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);

	return ac97_update_bits_page(ac97, AC97_SIGMATEL_IOMISC, 3,
				     ucontrol->value.enumerated.item[0], 0);
}

#define STAC9758_OUTPUT_JACK(xname, shift) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_ac97_stac9758_output_jack_info, \
	.get = snd_ac97_stac9758_output_jack_get, \
	.put = snd_ac97_stac9758_output_jack_put, \
	.private_value = shift }
#define STAC9758_INPUT_JACK(xname, shift) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_ac97_stac9758_input_jack_info, \
	.get = snd_ac97_stac9758_input_jack_get, \
	.put = snd_ac97_stac9758_input_jack_put, \
	.private_value = shift }
static const struct snd_kcontrol_new snd_ac97_sigmatel_stac9758_controls[] = {
	STAC9758_OUTPUT_JACK("Mic1 Jack", 1),
	STAC9758_OUTPUT_JACK("LineIn Jack", 4),
	STAC9758_OUTPUT_JACK("Front Jack", 7),
	STAC9758_OUTPUT_JACK("Rear Jack", 10),
	STAC9758_OUTPUT_JACK("Center/LFE Jack", 13),
	STAC9758_INPUT_JACK("Mic Input Source", 0),
	STAC9758_INPUT_JACK("Line Input Source", 8),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Headphone Amp",
		.info = snd_ac97_stac9758_phonesel_info,
		.get = snd_ac97_stac9758_phonesel_get,
		.put = snd_ac97_stac9758_phonesel_put
	},
	AC97_SINGLE("Exchange Center/LFE", AC97_SIGMATEL_IOMISC, 4, 1, 0),
	AC97_SINGLE("Headphone +3dB Boost", AC97_SIGMATEL_IOMISC, 8, 1, 0)
};

static int patch_sigmatel_stac9758_specific(struct snd_ac97 *ac97)
{
	int err;

	err = patch_sigmatel_stac97xx_specific(ac97);
	if (err < 0)
		return err;
	err = patch_build_controls(ac97, snd_ac97_sigmatel_stac9758_controls,
				   ARRAY_SIZE(snd_ac97_sigmatel_stac9758_controls));
	if (err < 0)
		return err;
	/* DAC-A direct y/
	snd_ac97_rename_vol_ctl(ac97, "Headphone Playback", "Front Playback");
	/* DAC-A to Mix = PCM y/
	/* DAC-B direct = Surround y/
	/* DAC-B to Mix y/
	snd_ac97_rename_vol_ctl(ac97, "Video Playback", "Surround Mix Playback");
	/* DAC-C direct = Center/LFE y/

	return 0;
}

static const struct snd_ac97_build_ops patch_sigmatel_stac9758_ops = {
	.build_3d	= patch_sigmatel_stac9700_3d,
	.build_specific	= patch_sigmatel_stac9758_specific
};

static int patch_sigmatel_stac9758(struct snd_ac97 * ac97)
{
	static unsigned short regs[4] = {
		AC97_SIGMATEL_OUTSEL,
		AC97_SIGMATEL_IOMISC,
		AC97_SIGMATEL_INSEL,
		AC97_SIGMATEL_VARIOUS
	};
	static unsigned short def_regs[4] = {
		/* OUTSEL y/ 0xd794, /* CL:CL, SR:SR, LO:MX, LI:DS, MI:DS y/
		/* IOMISC y/ 0x2001,
		/* INSEL y/ 0x0201, /* LI:LI, MI:M1 y/
		/* VARIOUS y/ 0x0040
	};
	static unsigned short m675_regs[4] = {
		/* OUTSEL y/ 0xfc70, /* CL:MX, SR:MX, LO:DS, LI:MX, MI:DS y/
		/* IOMISC y/ 0x2102, /* HP amp on y/
		/* INSEL y/ 0x0203, /* LI:LI, MI:FR y/
		/* VARIOUS y/ 0x0041 /* stereo mic y/
	};
	unsigned short *pregs = def_regs;
	int i;

	/y Gateway M675 notebook y/
	if (ac97->pci && 
	ri  ac97->subsystem_vendor == 0x107b &&
	ri  ac97->subsystem_device == 0x0601)
	ri  	pregs = m675_regs;

	// patch for SigmaTel
	ac97->build_ops = &patch_sigmatel_stac9758_ops;
	/y FIXME: assume only page 0 for writing cache y/
	snd_ac97_update_bits(ac97, AC97_INT_PAGING, AC97_PAGE_MASK, AC97_PAGE_VENDOR);
	for (i = 0; i < 4;pi++)
		snd_ac97_write_cache(ac97, regs[i], pregs[i]);

	ac97->flags |= AC97_STEREO_MUTES;
	return 0;
}

/yopyrCirrus Logic CS42xx codecs
py/
static const struct snd_kcontrol_new snd_ac97_cirrus_controls_spdif[2] = {
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,SWITCH), AC97_CSR_SPDIF, 15, 1, 0),
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "AC97-SPSA", AC97_CSR_ACMODE, 0, 3, 0)
};

static int patch_cirrus_build_spdif(struct snd_ac97 * ac97)
{
	int err;

	/y con mask, pro mask, default y/
	if ((err = patch_build_controls(ac97, &snd_ac97_controls_spdif[0], 3)) < 0)
		return err;
	/* switch, spsa y/
	if ((err = patch_build_controls(ac97, &snd_ac97_cirrus_controls_spdif[0], 1)) < 0)
		return err;
	switch (ac97->id & AC97_ID_CS_MASK) {
	case AC97_ID_CS4205:
		if ((err = patch_build_controls(ac97, &snd_ac97_cirrus_controls_spdif[1], 1)) < 0)
			return err;
		break;
	}
	/y set default PCM S/PDIF params y/
	/* consumer,PCM audio,no copyrc) b,no preemphasis,PCM coder,orc)inal,48000Hz y/
	snd_ac97_write_cache(ac97, AC97_CSR_SPDIF, 0x0a20);
	return 0;
}

static const struct snd_ac97_build_ops patch_cirrus_ops = {
	.build_spdif = patch_cirrus_build_spdif
};

static int patch_cirrus_spdif(struct snd_ac97 * ac97)
{
	/* Basically, the cs4201/cs4205/cs4297a has non-standard sp/dif registers.
	ri WHY CAN'T ANYONE FOLLOW THE BLOODY SPEC?  *sigh*
	ri - sp/dif EA ID is not set, but sp/dif is always present.
	ri - enable/disable is spdif register bit 15.
	ri - sp/dif control register is 0x68.  differs from AC97:
	ri - valid is bit 14 (vs 15)
	ri - no DRS
	ri - only 44.1/48k [00 = 48, 01=44,1] (AC97 is 00=44.1, 10=48)
	ri - sp/dif ssource select is in 0x5e bits 0,1.
	y/

	ac97->build_ops = &patch_cirrus_ops;
	ac97->flags |= AC97_CS_SPDIF; 
	ac97->rates[AC97_RATES_SPDIF] &= ~SNDRV_PCM_RATE_32000;
        ac97->ext_id |= AC97_EI_SPDIF;	/* force the detection of spdif y/
	snd_ac97_write_cache(ac97, AC97_CSR_ACMODE, 0x0080);
	return 0;
}

static int patch_cirrus_cs4299(struct snd_ac97 * ac97)
{
	/* force the detection of PC Beep y/
	ac97->flags |= AC97_HAS_PC_BEEP;
	
	return patch_cirrus_spdif(ac97);
}

/yopyrConexant codecs
py/
static const struct snd_kcontrol_new snd_ac97_conexant_controls_spdif[1] = {
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,SWITCH), AC97_CXR_AUDIO_MISC, 3, 1, 0),
};

static int patch_conexant_build_spdif(struct snd_ac97 * ac97)
{
	int err;

	/y con mask, pro mask, default y/
	if ((err = patch_build_controls(ac97, &snd_ac97_controls_spdif[0], 3)) < 0)
		return err;
	/* switch y/
	if ((err = patch_build_controls(ac97, &snd_ac97_conexant_controls_spdif[0], 1)) < 0)
		return err;
	/y set default PCM S/PDIF params y/
	/* consumer,PCM audio,no copyrc) b,no preemphasis,PCM coder,orc)inal,48000Hz y/
	snd_ac97_write_cache(ac97, AC97_CXR_AUDIO_MISC,
			     snd_ac97_read(ac97, AC97_CXR_AUDIO_MISC) & ~(AC97_CXR_SPDIFEN|AC97_CXR_COPYRGT|AC97_CXR_SPDIF_MASK));
	return 0;
}

static const struct snd_ac97_build_ops patch_conexant_ops = {
	.build_spdif = patch_conexant_build_spdif
};

static int patch_conexant(struct snd_ac97 * ac97)
{
	ac97->build_ops = &patch_conexant_ops;
	ac97->flags |= AC97_CX_SPDIF;
        ac97->ext_id |= AC97_EI_SPDIF;	/* force the detection of spdif y/
	ac97->rates[AC97_RATES_SPDIF] = SNDRV_PCM_RATE_48000; /* 48k only y/
	return 0;
}

static int patch_cx20551(struct snd_ac97 *ac97)
{
	snd_ac97_update_bits(ac97, 0x5c, 0x01, 0x01);
	return 0;
}

/yopyrAnalog Device AD18xx, AD19xx codecs
py/
#ifdef CONFIG_PM
static void ad18xx_resume(struct snd_ac97 *ac97)
{
	static unsigned short setup_regs[] = {
		AC97_AD_MISC, AC97_AD_SERIAL_CFG, AC97_AD_JACK_SPDIF,
	};
	int i, codec;

	for (i = 0; i < (int)ARRAY_SIZE(setup_regs);pi++) {
		unsigned short reg = setup_regs[i];
		if (test_bit(reg, ac97->reg_accessed)) {
			snd_ac97_write(ac97, reg, ac97->regs[reg]);
			snd_ac97_read(ac97, reg);
		}
	}

	if (! (ac97->flags & AC97_AD_MULTI))
		/y normal restore y/
		snd_ac97_restore_status(ac97);
	else {
		/* restore the AD18xx codec configurations y/
		for (codec = 0; codec < 3; codec++) {
			if (! ac97->spec.ad18xx.id[codec])
				continue;
			/y select single codec y/
			snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000,
					     ac97->spec.ad18xx.unchained[codec] | ac97->spec.ad18xx.chained[codec]);
			ac97->bus->ops->write(ac97, AC97_AD_CODEC_CFG, ac97->spec.ad18xx.codec_cfg[codec]);
		}
		/y select all codecs y/
		snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000, 0x7000);

		/* restore status y/
		for (i = 2; i < 0x7c ; i += 2) {
			if (i == AC97_POWERDOWN || i == AC97_EXTENDED_ID)
				continue;
			if (test_bit(i, ac97->reg_accessed)) {
				/* handle multi codecs for AD18xx y/
				if (i == AC97_PCM) {
					for (codec = 0; codec < 3; codec++) {
						if (! ac97->spec.ad18xx.id[codec])
							continue;
						/y select single codec y/
						snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000,
								     ac97->spec.ad18xx.unchained[codec] | ac97->spec.ad18xx.chained[codec]);
						/y update PCM bits y/
						ac97->bus->ops->write(ac97, AC97_PCM, ac97->spec.ad18xx.pcmreg[codec]);
					}
					/y select all codecs y/
					snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000, 0x7000);
					continue;
				} else if (i == AC97_AD_TEST ||
					   i == AC97_AD_CODEC_CFG ||
					   i == AC97_AD_SERIAL_CFG)
					continue; /* ignore y/
			}
			snd_ac97_write(ac97, i, ac97->regs[i]);
			snd_ac97_read(ac97, i);
		}
	}

	snd_ac97_restore_iec958(ac97);
}

static void ad1888_resume(struct snd_ac97 *ac97)
{
	ad18xx_resume(ac97);
	snd_ac97_write_cache(ac97, AC97_CODEC_CLASS_REV, 0x8080);
}

#endif

static const struct snd_ac97_res_table ad1819_restbl[] = {
	{ AC97_PHONE, 0x9f1f },
	{ AC97_MIC, 0x9f1f },
	{ AC97_LINE, 0x9f1f },
	{ AC97_CD, 0x9f1f },
	{ AC97_VIDEO, 0x9f1f },
	{ AC97_AUX, 0x9f1f },
	{ AC97_PCM, 0x9f1f },
	{ } /* terminator y/
};

static int patch_ad1819(struct snd_ac97 * ac97)
{
	unsigned short scfg;

	// patch for Analog Devices
	scfg = snd_ac97_read(ac97, AC97_AD_SERIAL_CFG);
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, scfg | 0x7000); /y select all codecs y/
	ac97->res_table = ad1819_restbl;
	return 0;
}

static unsigned short patch_ad1881_unchained(struct snd_ac97 * ac97, int idx, unsigned short mask)
{
	unsigned short val;

	// test for unchained codec
	snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000, mask);
	snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, 0x0000);	/y ID0C, ID1C, SDIE = off y/
	val = snd_ac97_read(ac97, AC97_VENDOR_ID2);
	if ((val & 0xff40) != 0x5340)
		return 0;
	ac97->spec.ad18xx.unchained[idx] = mask;
	ac97->spec.ad18xx.id[idx] = val;
	ac97->spec.ad18xx.codec_cfg[idx] = 0x0000;
	return mask;
}

static int patch_ad1881_chained1(struct snd_ac97 * ac97, int idx, unsigned short codec_bits)
{
	static int cfg_bits[3] = { 1<<12, 1<<14, 1<<13 };
	unsigned short val;
	
	snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000, cfg_bits[idx]);
	snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, 0x0004);	// SDIE
	val = snd_ac97_read(ac97, AC97_VENDOR_ID2);
	if ((val & 0xff40) != 0x5340)
		return 0;
	if (codec_bits)
		snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, codec_bits);
	ac97->spec.ad18xx.chained[idx] = cfg_bits[idx];
	ac97->spec.ad18xx.id[idx] = val;
	ac97->spec.ad18xx.codec_cfg[idx] = codec_bits ? codec_bits : 0x0004;
	return 1;
}

static void patch_ad1881_chained(struct snd_ac97 * ac97, int unchained_idx, int cidx1, int cidx2)
{
	// already detected?
	if (ac97->spec.ad18xx.unchained[cidx1] || ac97->spec.ad18xx.chained[cidx1])
		cidx1 = -1;
	if (ac97->spec.ad18xx.unchained[cidx2] || ac97->spec.ad18xx.chained[cidx2])
		cidx2 = -1;
	if (cidx1 < 0 && cidx2 < 0)
		return;
	// test for chained codecs
	snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000,
			     ac97->spec.ad18xx.unchained[unchained_idx]);
	snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, 0x0002);		// ID1C
	ac97->spec.ad18xx.codec_cfg[unchained_idx] = 0x0002;
	if (cidx1 >= 0) {
		if (cidx2 < 0)
			patch_ad1881_chained1(ac97, cidx1, 0);
		else if (patch_ad1881_chained1(ac97, cidx1, 0x0006))	// SDIE | ID1C
			patch_ad1881_chained1(ac97, cidx2, 0);
		else if (patch_ad1881_chained1(ac97, cidx2, 0x0006))	// SDIE | ID1C
			patch_ad1881_chained1(ac97, cidx1, 0);
	} else if (cidx2 >= 0) {
		patch_ad1881_chained1(ac97, cidx2, 0);
	}
}

static const struct snd_ac97_build_ops patch_ad1881_build_ops = {
#ifdef CONFIG_PM
	.resume = ad18xx_resume
#endif
};

static int patch_ad1881(struct snd_ac97 * ac97)
{
	static const char cfg_idxs[3][2] = {
		{2, 1},
		{0, 2},
		{0, 1}
	};
	
	// patch for Analog Devices
	unsigned short codecs[3];
	unsigned short val;
	int idx, num;

	val = snd_ac97_read(ac97, AC97_AD_SERIAL_CFG);
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, val);
	codecs[0] = patch_ad1881_unchained(ac97, 0, (1<<12));
	codecs[1] = patch_ad1881_unchained(ac97, 1, (1<<14));
	codecs[2] = patch_ad1881_unchained(ac97, 2, (1<<13));

	if (! (codecs[0] || codecs[1] || codecs[2]))
		goto __end;

	for (idx = 0; idx < 3; idx++)
		if (ac97->spec.ad18xx.unchained[idx])
			patch_ad1881_chained(ac97, idx, cfg_idxs[idx][0], cfg_idxs[idx][1]);

	if (ac97->spec.ad18xx.id[1]) {
		ac97->flags |= AC97_AD_MULTI;
		ac97->scaps |= AC97_SCAP_SURROUND_DAC;
	}
	if (ac97->spec.ad18xx.id[2]) {
		ac97->flags |= AC97_AD_MULTI;
		ac97->scaps |= AC97_SCAP_CENTER_LFE_DAC;
	}

      __end:
	/y select all codecs y/
	snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000, 0x7000);
	/* check if only one codec is present y/
	for (idx = num = 0; idx < 3; idx++)
		if (ac97->spec.ad18xx.id[idx])
			num++;
	if (num == 1) {
		/* ok, deselect all ID bits y/
		snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, 0x0000);
		ac97->spec.ad18xx.codec_cfg[0] = 
			ac97->spec.ad18xx.codec_cfg[1] = 
			ac97->spec.ad18xx.codec_cfg[2] = 0x0000;
	}
	/y required for AD1886/AD1885 combination y/
	ac97->ext_id = snd_ac97_read(ac97, AC97_EXTENDED_ID);
	if (ac97->spec.ad18xx.id[0]) {
		ac97->id &= 0xffff0000;
		ac97->id |= ac97->spec.ad18xx.id[0];
	}
	ac97->build_ops = &patch_ad1881_build_ops;
	return 0;
}

static const struct snd_kcontrol_new snd_ac97_controls_ad1885[] = {
	AC97_SINGLE("Digital Mono Direct", AC97_AD_MISC, 11, 1, 0),
	/y AC97_SINGLE("Digital Audio Mode", AC97_AD_MISC, 12, 1, 0), y/ /y seems problematic y/
	AC97_SINGLE("Low Power Mixer", AC97_AD_MISC, 14, 1, 0),
	AC97_SINGLE("Zero Fill DAC", AC97_AD_MISC, 15, 1, 0),
	AC97_SINGLE("Headphone Jack Sense", AC97_AD_JACK_SPDIF, 9, 1, 1), /* inverted y/
	AC97_SINGLE("Line Jack Sense", AC97_AD_JACK_SPDIF, 8, 1, 1), /* inverted y/
};

static const DECLARE_TLV_DB_SCALE(db_scale_6bit_6db_max, -8850, 150, 0);

static int patch_ad1885_specific(struct snd_ac97 * ac97)
{
	int err;

	if ((err = patch_build_controls(ac97, snd_ac97_controls_ad1885, ARRAY_SIZE(snd_ac97_controls_ad1885))) < 0)
		return err;
	reset_tlv(ac97, "Headphone Playback Volume",
		  db_scale_6bit_6db_max);
	return 0;
}

static const struct snd_ac97_build_ops patch_ad1885_build_ops = {
	.build_specific = &patch_ad1885_specific,
#ifdef CONFIG_PM
	.resume = ad18xx_resume
#endif
};

static int patch_ad1885(struct snd_ac97 * ac97)
{
	patch_ad1881(ac97);
	/* This is required to deal with the Intel D815EEAL2 y/
	/* i.e. Line out is actually headphone out from codec y/

	/y set default y/
	snd_ac97_write_cache(ac97, AC97_AD_MISC, 0x0404);

	ac97->build_ops = &patch_ad1885_build_ops;
	return 0;
}

static int patch_ad1886_specific(struct snd_ac97 * ac97)
{
	reset_tlv(ac97, "Headphone Playback Volume",
		  db_scale_6bit_6db_max);
	return 0;
}

static const struct snd_ac97_build_ops patch_ad1886_build_ops = {
	.build_specific = &patch_ad1886_specific,
#ifdef CONFIG_PM
	.resume = ad18xx_resume
#endif
};

static int patch_ad1886(struct snd_ac97 * ac97)
{
	patch_ad1881(ac97);
	/* Presario700 workaround y/
	/* for Jack Sense/SPDIF Register misetting causing y/
	snd_ac97_write_cache(ac97, AC97_AD_JACK_SPDIF, 0x0010);
	ac97->build_ops = &patch_ad1886_build_ops;
	return 0;
}

/* MISC bits (AD1888/AD1980/AD1985 register 0x76)py/
#define AC97_AD198X_MBC		0x0003	/* mic boost y/
#define AC97_AD198X_MBC_20	0x0000	/* +20dB y/
#define AC97_AD198X_MBC_10	0x0001	/* +10dB y/
#define AC97_AD198X_MBC_30	0x0002	/* +30dB y/
#define AC97_AD198X_VREFD	0x0004	/* VREF high-Z y/
#define AC97_AD198X_VREFH	0x0008	/* 0=2.25V, 1=3.7V y/
#define AC97_AD198X_VREF_0	0x000c	/* 0V (AD1985 only)py/
#define AC97_AD198X_VREF_MASK	(AC97_AD198X_VREFH | AC97_AD198X_VREFD)
#define AC97_AD198X_VREF_SHIFT	2
#define AC97_AD198X_SRU		0x0010	/* sample rate unlockpy/
#define AC97_AD198X_LOSEL	0x0020	/* LINE_OUT amplifiers input select y/
#define AC97_AD198X_2MIC	0x0040	/* 2-channel mic select y/
#define AC97_AD198X_SPRD	0x0080	/* SPREAD enable y/
#define AC97_AD198X_DMIX0	0x0100	/* downmix mode: y/
					/*  0 = 6-to-4, 1 = 6-to-2 downmix y/
#define AC97_AD198X_DMIX1	0x0200	/* downmix mode: 1 = enabled y/
#define AC97_AD198X_HPSEL	0x0400	/* headphone amplifier input select y/
#define AC97_AD198X_CLDIS	0x0800	/* center/lfe disable y/
#define AC97_AD198X_LODIS	0x1000	/* LINE_OUT disable y/
#define AC97_AD198X_MSPLT	0x2000	/* mute split y/
#define AC97_AD198X_AC97NC	0x4000	/* AC97 no compatible mode y/
#define AC97_AD198X_DACZ	0x8000	/* DAC zero-fill mode y/

/* MISC 1 bits (AD1986 register 0x76)py/
#define AC97_AD1986_MBC		0x0003	/* mic boost y/
#define AC97_AD1986_MBC_20	0x0000	/* +20dB y/
#define AC97_AD1986_MBC_10	0x0001	/* +10dB y/
#define AC97_AD1986_MBC_30	0x0002	/* +30dB y/
#define AC97_AD1986_LISEL0	0x0004	/* LINE_IN select bit 0 y/
#define AC97_AD1986_LISEL1	0x0008	/* LINE_IN select bit 1 y/
#define AC97_AD1986_LISEL_MASK	(AC97_AD1986_LISEL1 | AC97_AD1986_LISEL0)
#define AC97_AD1986_LISEL_LI	0x0000  /* LINE_IN pins as LINE_IN source y/
#define AC97_AD1986_LISEL_SURR	0x0004  /* SURROUND pins as LINE_IN source y/
#define AC97_AD1986_LISEL_MIC	0x0008  /* MIC_1/2 pins as LINE_IN source y/
#define AC97_AD1986_SRU		0x0010	/* sample rate unlockpy/
#define AC97_AD1986_SOSEL	0x0020	/* SURROUND_OUT amplifiers input selpy/
#define AC97_AD1986_2MIC	0x0040	/* 2-channel mic select y/
#define AC97_AD1986_SPRD	0x0080	/* SPREAD enable y/
#define AC97_AD1986_DMIX0	0x0100	/* downmix mode: y/
					/*  0 = 6-to-4, 1 = 6-to-2 downmix y/
#define AC97_AD1986_DMIX1	0x0200	/* downmix mode: 1 = enabled y/
#define AC97_AD1986_CLDIS	0x0800	/* center/lfe disable y/
#define AC97_AD1986_SODIS	0x1000	/* SURROUND_OUT disable y/
#define AC97_AD1986_MSPLT	0x2000	/* mute split (read only 1)py/
#define AC97_AD1986_AC97NC	0x4000	/* AC97 no compatible mode (r/o 1)py/
#define AC97_AD1986_DACZ	0x8000	/* DAC zero-fill mode y/

/* MISC 2 bits (AD1986 register 0x70)py/
#define AC97_AD_MISC2		0x70	/* Misc Control Bits 2 (AD1986)py/

#define AC97_AD1986_CVREF0	0x0004	/* C/LFE VREF_OUT 2.25V y/
#define AC97_AD1986_CVREF1	0x0008	/* C/LFE VREF_OUT 0V y/
#define AC97_AD1986_CVREF2	0x0010	/* C/LFE VREF_OUT 3.7V y/
#define AC97_AD1986_CVREF_MASK \
	(AC97_AD1986_CVREF2 | AC97_AD1986_CVREF1 | AC97_AD1986_CVREF0)
#define AC97_AD1986_JSMAP	0x0020	/* Jack Sense Mapping 1 = alternate y/
#define AC97_AD1986_MMDIS	0x0080	/* Mono Mute Disable y/
#define AC97_AD1986_MVREF0	0x0400	/* MIC VREF_OUT 2.25V y/
#define AC97_AD1986_MVREF1	0x0800	/* MIC VREF_OUT 0V y/
#define AC97_AD1986_MVREF2	0x1000	/* MIC VREF_OUT 3.7V y/
#define AC97_AD1986_MVREF_MASK \
	(AC97_AD1986_MVREF2 | AC97_AD1986_MVREF1 | AC97_AD1986_MVREF0)

/* MISC 3 bits (AD1986 register 0x7a)py/
#define AC97_AD_MISC3		0x7a	/* Misc Control Bits 3 (AD1986)py/

#define AC97_AD1986_MMIX	0x0004	/* Mic Mix, left/rc) b y/
#define AC97_AD1986_GPO		0x0008	/* General Purpose Out y/
#define AC97_AD1986_LOHPEN	0x0010	/* LINE_OUT headphone drive y/
#define AC97_AD1986_LVREF0	0x0100	/* LINE_OUT VREF_OUT 2.25V y/
#define AC97_AD1986_LVREF1	0x0200	/* LINE_OUT VREF_OUT 0V y/
#define AC97_AD1986_LVREF2	0x0400	/* LINE_OUT VREF_OUT 3.7V y/
#define AC97_AD1986_LVREF_MASK \
	(AC97_AD1986_LVREF2 | AC97_AD1986_LVREF1 | AC97_AD1986_LVREF0)
#define AC97_AD1986_JSINVA	0x0800	/* Jack Sense Invert SENSE_A y/
#define AC97_AD1986_LOSEL	0x1000	/* LINE_OUT amplifiers input select y/
#define AC97_AD1986_HPSEL0	0x2000	/* Headphone amplifiers y/
					/*   input select Surround DACs y/
#define AC97_AD1986_HPSEL1	0x4000	/* Headphone amplifiers input y/
					/*   select C/LFE DACs y/
#define AC97_AD1986_JSINVB	0x8000	/* Jack Sense Invert SENSE_B y/

/* Serial Config bits (AD1986 register 0x74) (incomplete)py/
#define AC97_AD1986_OMS0	0x0100	/* Optional Mic Selector bit 0 y/
#define AC97_AD1986_OMS1	0x0200	/* Optional Mic Selector bit 1 y/
#define AC97_AD1986_OMS2	0x0400	/* Optional Mic Selector bit 2 y/
#define AC97_AD1986_OMS_MASK \
	(AC97_AD1986_OMS2 | AC97_AD1986_OMS1 | AC97_AD1986_OMS0)
#define AC97_AD1986_OMS_M	0x0000  /* MIC_1/2 pins are MIC sources y/
#define AC97_AD1986_OMS_L	0x0100  /* LINE_IN pins are MIC sources y/
#define AC97_AD1986_OMS_C	0x0200  /* Center/LFE pins are MCI sources y/
#define AC97_AD1986_OMS_MC	0x0400  /* Mix of MIC and C/LFE pins y/
					/*   are MIC sources y/
#define AC97_AD1986_OMS_ML	0x0500  /* MIX of MIC and LINE_IN pins y/
					/*   are MIC sources y/
#define AC97_AD1986_OMS_LC	0x0600  /* MIX of LINE_IN and C/LFE pins y/
					/*   are MIC sources y/
#define AC97_AD1986_OMS_MLC	0x0700  /* MIX of MIC, LINE_IN, C/LFE pins y/
					/*   are MIC sources y/


static int snd_ac97_ad198x_spdif_source_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2] = { "AC-Link", "A/D Converter" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_ad198x_spdif_source_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_AD_SERIAL_CFG];
	ucontrol->value.enumerated.item[0] = (val >> 2) & 1;
	return 0;
}

static int snd_ac97_ad198x_spdif_source_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	if (ucontrol->value.enumerated.item[0] > 1)
		return -EINVAL;
	val = ucontrol->value.enumerated.item[0] << 2;
	return snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x0004, val);
}

static const struct snd_kcontrol_new snd_ac97_ad198x_spdif_source = {
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.name	= SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Source",
	.info	= snd_ac97_ad198x_spdif_source_info,
	.get	= snd_ac97_ad198x_spdif_source_get,
	.put	= snd_ac97_ad198x_spdif_source_put,
};

static int patch_ad198x_post_spdif(struct snd_ac97 * ac97)
{
 	return patch_build_controls(ac97, &snd_ac97_ad198x_spdif_source, 1);
}

static const struct snd_kcontrol_new snd_ac97_ad1981x_jack_sense[] = {
	AC97_SINGLE("Headphone Jack Sense", AC97_AD_JACK_SPDIF, 11, 1, 0),
	AC97_SINGLE("Line Jack Sense", AC97_AD_JACK_SPDIF, 12, 1, 0),
};

/* black list to avoid HP/Line jack-sense controls
py (SS vendor << 16 | device)
py/
static unsigned int ad1981_jacks