/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   Creative Labs, Inc.
 *  Routines for effect processor FX8010
 *
 *  Copyright (c) by James Courtier-Dutton <James@superbug.co.uk>
 *  	Added EMU 1010 support.
 *
 *  BUGS:
 *    --
 *
 *  TODO:
 *    --
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/pci.h>
#include <linux/capability.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/moduleparam.h>

#include <sound/core.h>
#include <sound/tlv.h>
#include <sound/emu10k1.h>

#if 0		/* for testing purposes - digital out -> capture */
#define EMU10K1_CAPTURE_DIGITAL_OUT
#endif
#if 0		/* for testing purposes - set S/PDIF to AC3 output */
#define EMU10K1_SET_AC3_IEC958
#endif
#if 0		/* for testing purposes - feed the front signal to Center/LFE outputs */
#define EMU10K1_CENTER_LFE_FROM_FRONT
#endif

static bool high_res_gpr_volume;
module_param(high_res_gpr_volume, bool, 0444);
MODULE_PARM_DESC(high_res_gpr_volume, "GPR mixer controls use 31-bit range.");

/*
 *  Tables
 */ 

static char *fxbuses[16] = {
	/* 0x00 */ "PCM Left",
	/* 0x01 */ "PCM Right",
	/* 0x02 */ "PCM Surround Left",
	/* 0x03 */ "PCM Surround Right",
	/* 0x04 */ "MIDI Left",
	/* 0x05 */ "MIDI Right",
	/* 0x06 */ "Center",
	/* 0x07 */ "LFE",
	/* 0x08 */ NULL,
	/* 0x09 */ NULL,
	/* 0x0a */ NULL,
	/* 0x0b */ NULL,
	/* 0x0c */ "MIDI Reverb",
	/* 0x0d */ "MIDI Chorus",
	/* 0x0e */ NULL,
	/* 0x0f */ NULL
};

static char *creative_ins[16] = {
	/* 0x00 */ "AC97 Left",
	/* 0x01 */ "AC97 Right",
	/* 0x02 */ "TTL IEC958 Left",
	/* 0x03 */ "TTL IEC958 Right",
	/* 0x04 */ "Zoom Video Left",
	/* 0x05 */ "Zoom Video Right",
	/* 0x06 */ "Optical IEC958 Left",
	/* 0x07 */ "Optical IEC958 Right",
	/* 0x08 */ "Line/Mic 1 Left",
	/* 0x09 */ "Line/Mic 1 Right",
	/* 0x0a */ "Coaxial IEC958 Left",
	/* 0x0b */ "Coaxial IEC958 Right",
	/* 0x0c */ "Line/Mic 2 Left",
	/* 0x0d */ "Line/Mic 2 Right",
	/* 0x0e */ NULL,
	/* 0x0f */ NULL
};

static char *audigy_ins[16] = {
	/* 0x00 */ "AC97 Left",
	/* 0x01 */ "AC97 Right",
	/* 0x02 */ "Audigy CD Left",
	/* 0x03 */ "Audigy CD Right",
	/* 0x04 */ "Optical IEC958 Left",
	/* 0x05 */ "Optical IEC958 Right",
	/* 0x06 */ NULL,
	/* 0x07 */ NULL,
	/* 0x08 */ "Line/Mic 2 Left",
	/* 0x09 */ "Line/Mic 2 Right",
	/* 0x0a */ "SPDIF Left",
	/* 0x0b */ "SPDIF Right",
	/* 0x0c */ "Aux2 Left",
	/* 0x0d */ "Aux2 Right",
	/* 0x0e */ NULL,
	/* 0x0f */ NULL
};

static char *creative_outs[32] = {
	/* 0x00 */ "AC97 Left",
	/* 0x01 */ "AC97 Right",
	/* 0x02 */ "Optical IEC958 Left",
	/* 0x03 */ "Optical IEC958 Right",
	/* 0x04 */ "Center",
	/* 0x05 */ "LFE",
	/* 0x06 */ "Headphone Left",
	/* 0x07 */ "Headphone Right",
	/* 0x08 */ "Surround Left",
	/* 0x09 */ "Surround Right",
	/* 0x0a */ "PCM Capture Left",
	/* 0x0b */ "PCM Capture Right",
	/* 0x0c */ "MIC Capture",
	/* 0x0d */ "AC97 Surround Left",
	/* 0x0e */ "AC97 Surround Right",
	/* 0x0f */ NULL,
	/* 0x10 */ NULL,
	/* 0x11 */ "Analog Center",
	/* 0x12 */ "Analog LFE",
	/* 0x13 */ NULL,
	/* 0x14 */ NULL,
	/* 0x15 */ NULL,
	/* 0x16 */ NULL,
	/* 0x17 */ NULL,
	/* 0x18 */ NULL,
	/* 0x19 */ NULL,
	/* 0x1a */ NULL,
	/* 0x1b */ NULL,
	/* 0x1c */ NULL,
	/* 0x1d */ NULL,
	/* 0x1e */ NULL,
	/* 0x1f */ NULL,
};

static char *audigy_outs[32] = {
	/* 0x00 */ "Digital Front Left",
	/* 0x01 */ "Digital Front Right",
	/* 0x02 */ "Digital Center",
	/* 0x03 */ "Digital LEF",
	/* 0x04 */ "Headphone Left",
	/* 0x05 */ "Headphone Right",
	/* 0x06 */ "Digital Rear Left",
	/* 0x07 */ "Digital Rear Right",
	/* 0x08 */ "Front Left",
	/* 0x09 */ "Front Right",
	/* 0x0a */ "Center",
	/* 0x0b */ "LFE",
	/* 0x0c */ NULL,
	/* 0x0d */ NULL,
	/* 0x0e */ "Rear Left",
	/* 0x0f */ "Rear Right",
	/* 0x10 */ "AC97 Front Left",
	/* 0x11 */ "AC97 Front Right",
	/* 0x12 */ "ADC Caputre Left",
	/* 0x13 */ "ADC Capture Right",
	/* 0x14 */ NULL,
	/* 0x15 */ NULL,
	/* 0x16 */ NULL,
	/* 0x17 */ NULL,
	/* 0x18 */ NULL,
	/* 0x19 */ NULL,
	/* 0x1a */ NULL,
	/* 0x1b */ NULL,
	/* 0x1c */ NULL,
	/* 0x1d */ NULL,
	/* 0x1e */ NULL,
	/* 0x1f */ NULL,
};

static const u32 bass_table[41][5] = {
	{ 0x3e4f844f, 0x84ed4cc3, 0x3cc69927, 0x7b03553a, 0xc4da8486 },
	{ 0x3e69a17a, 0x84c280fb, 0x3cd77cd4, 0x7b2f2a6f, 0xc4b08d1d },
	{ 0x3e82ff42, 0x849991d5, 0x3ce7466b, 0x7b5917c6, 0xc48863ee },
	{ 0x3e9bab3c, 0x847267f0, 0x3cf5ffe8, 0x7b813560, 0xc461f22c },
	{ 0x3eb3b275, 0x844ced29, 0x3d03b295, 0x7ba79a1c, 0xc43d223b },
	{ 0x3ecb2174, 0x84290c8b, 0x3d106714, 0x7bcc5ba3, 0xc419dfa5 },
	{ 0x3ee2044b, 0x8406b244, 0x3d1c2561, 0x7bef8e77, 0xc3f8170f },
	{ 0x3ef86698, 0x83e5cb96, 0x3d26f4d8, 0x7c114600, 0xc3d7b625 },
	{ 0x3f0e5390, 0x83c646c9, 0x3d30dc39, 0x7c319498, 0xc3b8ab97 },
	{ 0x3f23d60b, 0x83a81321, 0x3d39e1af, 0x7c508b9c, 0xc39ae704 },
	{ 0x3f38f884, 0x838b20d2, 0x3d420ad2, 0x7c6e3b75, 0xc37e58f1 },
	{ 0x3f4dc52c, 0x836f60ef, 0x3d495cab, 0x7c8ab3a6, 0xc362f2be },
	{ 0x3f6245e8, 0x8354c565, 0x3d4fdbb8, 0x7ca602d6, 0xc348a69b },
	{ 0x3f76845f, 0x833b40ec, 0x3d558bf0, 0x7cc036df, 0xc32f677c },
	{ 0x3f8a8a03, 0x8322c6fb, 0x3d5a70c4, 0x7cd95cd7, 0xc317290b },
	{ 0x3f9e6014, 0x830b4bc3, 0x3d5e8d25, 0x7cf1811a, 0xc2ffdfa5 },
	{ 0x3fb20fae, 0x82f4c420, 0x3d61e37f, 0x7d08af56, 0xc2e9804a },
	{ 0x3fc5a1cc, 0x82df2592, 0x3d6475c3, 0x7d1ef294, 0xc2d40096 },
	{ 0x3fd91f55, 0x82ca6632, 0x3d664564, 0x7d345541, 0xc2bf56b9 },
	{ 0x3fec9120, 0x82b67cac, 0x3d675356, 0x7d48e138, 0xc2ab796e },
	{ 0x40000000, 0x82a36037, 0x3d67a012, 0x7d5c9fc9, 0xc2985fee },
	{ 0x401374c7, 0x8291088a, 0x3d672b93, 0x7d6f99c3, 0xc28601f2 },
	{ 0x4026f857, 0x827f6dd7, 0x3d65f559, 0x7d81d77c, 0xc27457a3 },
	{ 0x403a939f, 0x826e88c5, 0x3d63fc63, 0x7d9360d4, 0xc2635996 },
	{ 0x404e4faf, 0x825e5266, 0x3d613f32, 0x7da43d42, 0xc25300c6 },
	{ 0x406235ba, 0x824ec434, 0x3d5dbbc3, 0x7db473d7, 0xc243468e },
	{ 0x40764f1f, 0x823fd80c, 0x3d596f8f, 0x7dc40b44, 0xc23424a2 },
	{ 0x408aa576, 0x82318824, 0x3d545787, 0x7dd309e2, 0xc2259509 },
	{ 0x409f4296, 0x8223cf0b, 0x3d4e7012, 0x7de175b5, 0xc2179218 },
	{ 0x40b430a0, 0x8216a7a1, 0x3d47b505, 0x7def5475, 0xc20a1670 },
	{ 0x40c97a0a, 0x820a0d12, 0x3d4021a1, 0x7dfcab8d, 0xc1fd1cf5 },
	{ 0x40df29a6, 0x81fdfad6, 0x3d37b08d, 0x7e098028, 0xc1f0a0ca },
	{ 0x40f54ab1, 0x81f26ca9, 0x3d2e5bd1, 0x7e15d72b, 0xc1e49d52 },
	{ 0x410be8da, 0x81e75e89, 0x3d241cce, 0x7e21b544, 0xc1d90e24 },
	{ 0x41231051, 0x81dcccb3, 0x3d18ec37, 0x7e2d1ee6, 0xc1cdef10 },
	{ 0x413acdd0, 0x81d2b39e, 0x3d0cc20a, 0x7e38184e, 0xc1c33c13 },
	{ 0x41532ea7, 0x81c90ffb, 0x3cff9585, 0x7e42a58b, 0xc1b8f15a },
	{ 0x416c40cd, 0x81bfdeb2, 0x3cf15d21, 0x7e4cca7c, 0xc1af0b3f },
	{ 0x418612ea, 0x81b71cdc, 0x3ce20e85, 0x7e568ad3, 0xc1a58640 },
	{ 0x41a0b465, 0x81aec7c5, 0x3cd19e7c, 0x7e5fea1e, 0xc19c5f03 },
	{ 0x41bc3573, 0x81a6dcea, 0x3cc000e9, 0x7e68ebc2, 0xc1939250 }
};

static const u32 treble_table[41][5] = {
	{ 0x0125cba9, 0xfed5debd, 0x00599b6c, 0x0d2506da, 0xfa85b354 },
	{ 0x0142f67e, 0xfeb03163, 0x0066cd0f, 0x0d14c69d, 0xfa914473 },
	{ 0x016328bd, 0xfe860158, 0x0075b7f2, 0x0d03eb27, 0xfa9d32d2 },
	{ 0x0186b438, 0xfe56c982, 0x00869234, 0x0cf27048, 0xfaa97fca },
	{ 0x01adf358, 0xfe21f5fe, 0x00999842, 0x0ce051c2, 0xfab62ca5 },
	{ 0x01d949fa, 0xfde6e287, 0x00af0d8d, 0x0ccd8b4a, 0xfac33aa7 },
	{ 0x02092669, 0xfda4d8bf, 0x00c73d4c, 0x0cba1884, 0xfad0ab07 },
	{ 0x023e0268, 0xfd5b0e4a, 0x00e27b54, 0x0ca5f509, 0xfade7ef2 },
	{ 0x0278645c, 0xfd08a2b0, 0x01012509, 0x0c911c63, 0xfaecb788 },
	{ 0x02b8e091, 0xfcac9d1a, 0x0123a262, 0x0c7b8a14, 0xfafb55df },
	{ 0x03001a9a, 0xfc45e9ce, 0x014a6709, 0x0c65398f, 0xfb0a5aff },
	{ 0x034ec6d7, 0xfbd3576b, 0x0175f397, 0x0c4e2643, 0xfb19c7e4 },
	{ 0x03a5ac15, 0xfb5393ee, 0x01a6d6ed, 0x0c364b94, 0xfb299d7c },
	{ 0x0405a562, 0xfac52968, 0x01ddafae, 0x0c1da4e2, 0xfb39dca5 },
	{ 0x046fa3fe, 0xfa267a66, 0x021b2ddd, 0x0c042d8d, 0xfb4a8631 },
	{ 0x04e4b17f, 0xf975be0f, 0x0260149f, 0x0be9e0f2, 0xfb5b9ae0 },
	{ 0x0565f220, 0xf8b0fbe5, 0x02ad3c29, 0x0bceba73, 0xfb6d1b60 },
	{ 0x05f4a745, 0xf7d60722, 0x030393d4, 0x0bb2b578, 0xfb7f084d },
	{ 0x06923236, 0xf6e279bd, 0x03642465, 0x0b95cd75, 0xfb916233 },
	{ 0x07401713, 0xf5d3aef9, 0x03d01283, 0x0b77fded, 0xfba42984 },
	{ 0x08000000, 0xf4a6bd88, 0x0448a161, 0x0b594278, 0xfbb75e9f },
	{ 0x08d3c097, 0xf3587131, 0x04cf35a4, 0x0b3996c9, 0xfbcb01cb },
	{ 0x09bd59a2, 0xf1e543f9, 0x05655880, 0x0b18f6b2, 0xfbdf1333 },
	{ 0x0abefd0f, 0xf04956ca, 0x060cbb12, 0x0af75e2c, 0xfbf392e8 },
	{ 0x0bdb123e, 0xee806984, 0x06c739fe, 0x0ad4c962, 0xfc0880dd },
	{ 0x0d143a94, 0xec85d287, 0x0796e150, 0x0ab134b0, 0xfc1ddce5 },
	{ 0x0e6d5664, 0xea547598, 0x087df0a0, 0x0a8c9cb6, 0xfc33a6ad },
	{ 0x0fe98a2a, 0xe7e6ba35, 0x097edf83, 0x0a66fe5b, 0xfc49ddc2 },
	{ 0x118c4421, 0xe536813a, 0x0a9c6248, 0x0a4056d7, 0xfc608185 },
	{ 0x1359422e, 0xe23d19eb, 0x0bd96efb, 0x0a18a3bf, 0xfc77912c },
	{ 0x1554982b, 0xdef33645, 0x0d3942bd, 0x09efe312, 0xfc8f0bc1 },
	{ 0x1782b68a, 0xdb50deb1, 0x0ebf676d, 0x09c6133f, 0xfca6f019 },
	{ 0x19e8715d, 0xd74d64fd, 0x106fb999, 0x099b3337, 0xfcbf3cd6 },
	{ 0x1c8b07b8, 0xd2df56ab, 0x124e6ec8, 0x096f4274, 0xfcd7f060 },
	{ 0x1f702b6d, 0xcdfc6e92, 0x14601c10, 0x0942410b, 0xfcf108e5 },
	{ 0x229e0933, 0xc89985cd, 0x16a9bcfa, 0x09142fb5, 0xfd0a8451 },
	{ 0x261b5118, 0xc2aa8409, 0x1930bab6, 0x08e50fdc, 0xfd24604d },
	{ 0x29ef3f5d, 0xbc224f28, 0x1bfaf396, 0x08b4e3aa, 0xfd3e9a3b },
	{ 0x2e21a59b, 0xb4f2ba46, 0x1f0ec2d6, 0x0883ae15, 0xfd592f33 },
	{ 0x32baf44b, 0xad0c7429, 0x227308a3, 0x085172eb, 0xfd741bfd },
	{ 0x37c4448b, 0xa45ef51d, 0x262f3267, 0x081e36dc, 0xfd8f5d14 }
};

/* dB gain = (float) 20 * log10( float(db_table_value) / 0x8000000 ) */
static const u32 db_table[101] = {
	0x00000000, 0x01571f82, 0x01674b41, 0x01783a1b, 0x0189f540,
	0x019c8651, 0x01aff763, 0x01c45306, 0x01d9a446, 0x01eff6b8,
	0x0207567a, 0x021fd03d, 0x0239714c, 0x02544792, 0x027061a1,
	0x028dcebb, 0x02ac9edc, 0x02cce2bf, 0x02eeabe8, 0x03120cb0,
	0x0337184e, 0x035de2df, 0x03868173, 0x03b10a18, 0x03dd93e9,
	0x040c3713, 0x043d0cea, 0x04702ff3, 0x04a5bbf2, 0x04ddcdfb,
	0x0518847f, 0x0555ff62, 0x05966005, 0x05d9c95d, 0x06206005,
	0x066a4a52, 0x06b7b067, 0x0708bc4c, 0x075d9a01, 0x07b6779d,
	0x08138561, 0x0874f5d5, 0x08dafde1, 0x0945d4ed, 0x09b5b4fd,
	0x0a2adad1, 0x0aa58605, 0x0b25f936, 0x0bac7a24, 0x0c3951d8,
	0x0ccccccc, 0x0d673b17, 0x0e08f093, 0x0eb24510, 0x0f639481,
	0x101d3f2d, 0x10dfa9e6, 0x11ab3e3f, 0x12806ac3, 0x135fa333,
	0x144960c5, 0x153e2266, 0x163e6cfe, 0x174acbb7, 0x1863d04d,
	0x198a1357, 0x1abe349f, 0x1c00db77, 0x1d52b712, 0x1eb47ee6,
	0x2026f30f, 0x21aadcb6, 0x23410e7e, 0x24ea64f9, 0x26a7c71d,
	0x287a26c4, 0x2a62812c, 0x2c61df84, 0x2e795779, 0x30aa0bcf,
	0x32f52cfe, 0x355bf9d8, 0x37dfc033, 0x3a81dda4, 0x3d43c038,
	0x4026e73c, 0x432ce40f, 0x46575af8, 0x49a8040f, 0x4d20ac2a,
	0x50c335d3, 0x54919a57, 0x588dead1, 0x5cba514a, 0x611911ea,
	0x65ac8c2f, 0x6a773c39, 0x6f7bbc23, 0x74bcc56c, 0x7a3d3272,
	0x7fffffff,
};

/* EMU10k1/EMU10k2 DSP control db gain */
static const DECLARE_TLV_DB_SCALE(snd_emu10k1_db_scale1, -4000, 40, 1);
static const DECLARE_TLV_DB_LINEAR(snd_emu10k1_db_linear, TLV_DB_GAIN_MUTE, 0);

/* EMU10K1 bass/treble db gain */
static const DECLARE_TLV_DB_SCALE(snd_emu10k1_bass_treble_db_scale, -1200, 60, 0);

static const u32 onoff_table[2] = {
	0x00000000, 0x00000001
};

/*
 */
 
static inline mm_segment_t snd_enter_user(void)
{
	mm_segment_t fs = get_fs();
	set_fs(get_ds());
	return fs;
}

static inline void snd_leave_user(mm_segment_t fs)
{
	set_fs(fs);
}

/*
 *   controls
 */

static int snd_emu10k1_gpr_ctl_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct snd_emu10k1_fx8010_ctl *ctl =
		(struct snd_emu10k1_fx8010_ctl *) kcontrol->private_value;

	if (ctl->min == 0 && ctl->max == 1)
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = ctl->vcount;
	uinfo->value.integer.min = ctl->min;
	uinfo->value.integer.max = ctl->max;
	return 0;
}

static int snd_emu10k1_gpr_ctl_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_fx8010_ctl *ctl =
		(struct snd_emu10k1_fx8010_ctl *) kcontrol->private_value;
	unsigned long flags;
	unsigned int i;
	
	spin_lock_irqss()Ouse(&0k1->regk_irq,gs;
	ustrutest(ind_0; i <->vcount;
	ui i++uinforol->prive.integer.max e.int[i]tl->vcoun.int[i];in_lockun_irqss()restore(&0k1->regk_irq,gs;
	ustrurn 0;
}

static int snd_emu10k1_gpr_ctl_get(puruct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_fx8010_ctl *ctl =
		(struct snd_emu10k1_fx8010_ctl *) kcontrol->private_value;
	unsigned long flags;
	unsigned int snd_ne;
, e;
nsigned int i;
	
, jnsii;
	ch.");nd_0;spin_lock_irqss()Ouse(&0k1->regk_irq,gs;
	ustrutest(ind_0; i <->vcount;
	ui i++u0x00	ne;
nd_orol->prive.integer.max e.int[i]tru(ctl-ne;
n<->min;
	uuinf	ne;
nd_>min;
	uinf(ctl-ne;
n>->max;
	ruinf	ne;
nd_>min;
	retu(ctl-ne;
n!l->vcoun.int[i]uinf	ch.");nd_1etu(e;
nd_>min;e.int[i]tl-ne;
nsi	switchl->min transla) any x00	casU10K1_CENTGPR_TRANSLATION_NONE:inf	emu10k1_fx80ptr_e to (0k1, 0k1->ctl_basU1+_>min;ctl[i]000, e;
stru		break;00	casU10K1_CENTGPR_TRANSLATION_TABLE100:inf	emu10k1_fx80ptr_e to (0k1, 0k1->ctl_basU1+_>min;ctl[i]000, able[101]e;
]stru		break;00	casU10K1_CENTGPR_TRANSLATION_BASS:inf	ctl-->min t = ct% 5)n!l-0 || ->min t = ct/ 5)n!l->vcount;
	uy x00	f	ch.");nd_-EIOtru			goto __errortru		}ru		test(jnd_0; jn<-5; j++uinff	emu10k1_fx80ptr_e to (0k1, 0k1->ctl_basU1+_>min;ctl[j co>vcount;
	u1+_i]000, _table[41][e;
][j]stru		break;00	casU10K1_CENTGPR_TRANSLATION_TREBLE:inf	ctl-->min t = ct% 5)n!l-0 || ->min t = ct/ 5)n!l->vcount;
	uy x00	f	ch.");nd_-EIOtru			goto __errortru		}ru		test(jnd_0; jn<-5; j++uinff	emu10k1_fx80ptr_e to (0k1, 0k1->ctl_basU1+_>min;ctl[j co>vcount;
	u1+_i]000, le_table[41][e;
][j]stru		break;00	casU10K1_CENTGPR_TRANSLATION_ONOFF:inf	emu10k1_fx80ptr_e to (0k1, 0k1->ctl_basU1+_>min;ctl[i]000, f_table[2] =e;
]stru		break;00	}ru}
  Crea__error:in_lockun_irqss()restore(&0k1->regk_irq,gs;
	ustrurn 0;
}ch.");
/*
 *   contI_userupt h."dler

static int  snd_leav0k1_fx8010_ctl i_useruptuct snd_emu10k1_fx8u = s	struct snd_emu10k1_fx8010_ctl s() *s(), *ns()if (c() = 0k1->10_ctl.c()_h."dlerunsiwhib g(c()u0x00	nc() = c()->next;0x1fc() ptr redibceivmoa cofrom lis
#deff	ctl-emu10k1_fx80ptr_read(0k1, 0k1->ctl_basU1+_c()->ctl_running;

s &d8f55551
};y x00	fctl-c()->h."dleruinff	c()->h."dler(0k1, c()->ate_valudatastru		emu10k1_fx80ptr_e to (0k1, 0k1->ctl_basU1+_c()->ctl_running;

;
stat		}ru	c() = ns()if	}r*
 snd_emu10k1_gpr_10_ctl regis
serc()_h."dleruct snd_emu10k1_fx8u = s,inff		Creaemu110_ctl s()_h."dlers)
*h."dler,inff		Creagned int  *audctl_running;inff		Crea snd_*ate_valudata,inff		Creaet snd_emu10k1_fx8010_ctl s() **erc()	struct snd_emu10k1_fx8010_ctl s() *s()nsigned long flags;
	unsi (c() = koc.h>
(sizeof(*s()), GFP_ATOMICtat	ctl-c()1)
	,
};uinfrn 0;
}-ENOMEM;
	c()->h."dler = h."dler;
	c()->ctl_runninget_ftl_running;
	c()->ate_valudataet_ate_valudata;
	c()->next 
	,
};;in_lock_irqss()Ouse(&0k1->10_ctl.c()__irq,gs;
	ustructl-0k1->10_ctl.c()_h."dleru1)
	,
};u x00	0k1->10_ctl.c()_h."dleru1) s()nsi	0k1->dsp i_useruptnd_kcon0k1_fx8010_ctl i_useruptnsi	scon0k1_fx80i_urer_[2] (0k1, GER;_FXDSPENABLEstru} 
		u x00	c()->next 
	0k1->10_ctl.c()_h."dlerunsi	0k1->10_ctl.c()_h."dleru1) s()nsi}in_lockun_irqss()restore(&0k1->10_ctl.c()__irq,gs;
	ustructl-erc()	s		*erc()1) s()nsirn 0;
}

statsnd_emu10k1_gpr_10_ctl unregis
serc()_h."dleruct snd_emu10k1_fx8u = s,inff		Creaeaet snd_emu10k1_fx8010_ctl s() *c()	struct snd_emu10k1_fx8010_ctl s() *tmpnsigned long flags;
	unsi (_lock_irqss()Ouse(&0k1->10_ctl.c()__irq,gs;
	ustructl-(tmp 
	0k1->10_ctl.c()_h."dleru)1)
	c()u0x00	0k1->10_ctl.c()_h."dleru1) tmp->next;ff	ctl-0k1->10_ctl.c()_h."dleru1)
	,
};u x00		scon0k1_fx80i_uredis[2] (0k1, GER;_FXDSPENABLEstrui	0k1->dsp i_useruptnd_,
};;in	}ru} 
		u x00	whib g(tmp && tmp->nextn!l-c()	s			tmp 
	tmp->next;ff	ctl-tmp	s			tmp->next 
	tmp->next->next;ff}in_lockun_irqss()restore(&0k1->10_ctl.c()__irq,gs;
	ustruk sof(c()unsirn 0;
}

stat/*************************************************************************  co0K1 bassct procmanager

ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssstatic int  snd_leav0k1_fx80e to _opuct snd_emu10k1_fx8010_ctl *odconi*odc,inff	 gned int i;
	*ptr,inff	 gnoffp, gnofr, gnofa, gnofx, gnofy	struu i_u32s)
**odctructl-emu1BUG_ON(*ptr >= 512)uinfrn 0;
tru*odco= (u i_u32s)
__tesccon)i*odcn t dU1+_(*ptr) co2et_fs(gbit(*ptr, c*odcn t dUue;
id)tru*odc[0]tl-((x &d8f3ff) << 1;y | -y &d8f3ff)tru*odc[1]tl-((op &d8f0f) << 2;y | -(r &d8f3ff) << 1;y | -a &d8f3ff)tru(*ptr)++
statine EMU1OP(i*odc, ptr, fp, r, a, x, y) \
	leav0k1_fx80e to _opui*odc, ptr, fp, r, a, x, y)atic int  snd_leav0k1_fx80gy_outse to _opuct snd_emu10k1_fx8010_ctl *odconi*odc,inff		gned int i;
	*ptr,inff		gnoffp, gnofr, gnofa, gnofx, gnofy	struu i_u32s)
**odctructl-emu1BUG_ON(*ptr >= 1024)uinfrn 0;
tru*odco= (u i_u32s)
__tesccon)i*odcn t dU1+_(*ptr) co2et_fs(gbit(*ptr, c*odcn t dUue;
id)tru*odc[0]tl-((x &d8f7ff) << 12y | -y &d8f7ff)tru*odc[1]tl-((op &d8f0f) << 24y | -(r &d8f7ff) << 12y | -a &d8f7ff)tru(*ptr)++
statine EMU1A_OP(i*odc, ptr, fp, r, a, x, y) \
	leav0k1_fx80gy_outse to _opui*odc, ptr, fp, r, a, x, y)atic int  snd_leav0k1_fx80efx_e to (ct snd_emu10k1_fx8u = s, gned int i;
	pc, gned int i;
	datasstrupc +
	0k1->gy_out ?1A_MICROCODEBASE : MICROCODEBASE;
	emu10k1_fx80ptr_e to (0k1, pc, 0, aatastrtatgned int i;
	leav0k1_fx80efx_read(ct snd_emu10k1_fx8u = s, gned int i;
	pcsstrupc +
	0k1->gy_out ?1A_MICROCODEBASE : MICROCODEBASE;
	rn 0;
}emu10k1_fx80ptr_read(0k1, pc, 0)
static int snd_emu10k1_gpr_ctl_pok (ct snd_emu10k1_fx8u = s,inff	et snd_emu10k1_fx8010_ctl *odconi*odcsstrusnd_ctlnsignofe;
nsrutest(ctlnd_0; ctln<l-0k1->gy_out ?126f30 : dfa90)
 ctl++u0x00	ctl-!ing gbit(ctl, c*odcn volum;
id)uinf	contin	unsi	ctl-ds()(void);
, &c*odcn volumap[vol])uinf	rn 0;
}-EFAULTnsi	scon0k1_fx80ptr_e to (0k1, 0k1->ctl_basU1+_ctl, 0, e;
stru}rurn 0;
}

static int snd_emu10k1_gpr_ctl_peek(ct snd_emu10k1_fx8u = s,inff	et snd_emu10k1_fx8010_ctl *odconi*odcsstrusnd_ctlnsignofe;
nsrutest(ctlnd_0; ctln<l-0k1->gy_out ?126f30 : dfa90)
 ctl++u0x00	fs(gbit(ctl, c*odcn volum;
id)etu(e;
nd_emu10k1_fx80ptr_read(0k1, 0k1->ctl_basU1+_ctl, 0)nsi	ctl-pu()(void);
, &c*odcn volumap[vol])uinf	rn 0;
}-EFAULTnsi}rurn 0;
}

static int snd_emu10k1_gpr_tram_pok (ct snd_emu10k1_fx8u = s,inff	aet snd_emu10k1_fx8010_ctl *odconi*odcsstrusnd_tramnsignofaddr,fe;
nsrutest(tramnd_0; tramn<l-0k1->gy_out ?126130 : dfa0)
 tram++u0x00	ctl-!ing gbit(tram, c*odcn tram_m;
id)uinf	contin	unsi	ctl-ds()(void);
, &c*odcn tram_aataumap[tram]y ||
		Creads()(voidaddr,f&c*odcn tram_addrumap[tram]yuinf	rn 0;
}-EFAULTnsi	scon0k1_fx80ptr_e to (0k1, TANKMEMDATAREGBASE + tram, 0, e;
stru	ctl-!0k1->gy_outu x00		scon0k1_fx80ptr_e to (0k1, TANKMEMADDRREGBASE + tram, 0, addrtat		} 
		u x00		scon0k1_fx80ptr_e to (0k1, TANKMEMADDRREGBASE + tram, 0, addr << 12ytru		emu10k1_fx80ptr_e to (0k1, A_TANKMEMCTLREGBASE + tram, 0, addr >> 2;y;00	}ru}
urn 0;
}

static int snd_emu10k1_gpr_tram_peek(ct snd_emu10k1_fx8u = s,inff	aet snd_emu10k1_fx8010_ctl *odconi*odcsstrusnd_tramnsignof);
, addrnsrumemfs((c*odcn tram_m;
id, 0, sizeof(c*odcn tram_m;
id)u;rutest(tramnd_0; tramn<l-0k1->gy_out ?126130 : dfa0)
 tram++u0x00	fs(gbit(tram, c*odcn tram_m;
id)etu(e;
nd_emu10k1_fx80ptr_read(0k1, TANKMEMDATAREGBASE + tram, 0stru	ctl-!0k1->gy_outu x00		addr d_emu10k1_fx80ptr_read(0k1, TANKMEMADDRREGBASE + tram, 0tat		} 
		u x00		addr d_emu10k1_fx80ptr_read(0k1, TANKMEMADDRREGBASE + tram, 0t >> 12;00		addr |d_emu10k1_fx80ptr_read(0k1, A_TANKMEMCTLREGBASE + tram, 0) << 2;at		}ru	ctl-pu()(void);
, &c*odcn tram_aataumap[tram]y ||
		Creapu()(voidaddr,f&c*odcn tram_addrumap[tram]yuinf	rn 0;
}-EFAULTnsi}
urn 0;
}

static int snd_emu10k1_gpr_t dUupok (ct snd_emu10k1_fx8u = s,inff	aet snd_emu10k1_fx8010_ctl *odconi*odcsstrugnofpc, lo, hinsrutest(pc d_0; pc <l-0k1->gy_out ?12*1024 : 2*512); pc +
	2u0x00	ctl-!ing gbit(pc / 2, c*odcn t dUue;
id)uinf	contin	unsi	ctl-ds()(voidlo, &c*odcn t dU[pc + 0]y ||
		Creads()(voidhi, &c*odcn t dU[pc + 1]yuinf	rn 0;
}-EFAULTnsi	scon0k1_fx80efx_e to (0k1, pc + 0, lo)nsi	scon0k1_fx80efx_e to (0k1, pc + 1, histru}rurn 0;
}

static int snd_emu10k1_gpr_t dUupeek(ct snd_emu10k1_fx8u = s,inff	aet snd_emu10k1_fx8010_ctl *odconi*odcsstrugnofpcnsrumemfs((c*odcn t dUue;
id, 0, sizeof(c*odcn t dUue;
id)u;rutest(pc d_0; pc <l-0k1->gy_out ?12*1024 : 2*512); pc +
	2u0x00	fs(gbit(pc / 2, c*odcn t dUue;
id)nsi	ctl-pu()(voidleav0k1_fx80efx_read(0k1, pc + 0), &c*odcn t dU[pc + 0]yuinf	rn 0;
}-EFAULTnsi	ctl-pu()(voidleav0k1_fx80efx_read(0k1, pc + 1), &c*odcn t dU[pc + 1]yuinf	rn 0;
}-EFAULTnsi}rurn 0;
}

static int ct snd_emu10k1_fx8010_ctl *) kc
emu10k1_fx80look_tes *) (ct snd_emu10k1_fx8u = s, ct snd_ctl_elem_info donid	struct snd_emu10k1_fx8010_ctl *ctl =
	;ruct snd_emu1trol *kcontrol, stnsrulis
_tes eacher_ury->mi, &0k1->10_ctl.ctl_get, lis
u0x00	trol *kcol->vcoutrol, stnsi	ctl-trol->privid.ifacco)
	cdivifacco&&
		Crea!ct cmp-trol->privid.na"GPRcdivna"G)o&&
		Creatrol->privid.indexo)
	cdivindexuinf	rn 0;
}=
	;ru}rurn 0;
},
};;itatine EMU1MAX_DB_SSIZE	256atic int gned int i;
	* of _tlv(t u32 oned int i;
	_)(voi *_tlvsstrugned int i;
	data[2];rugned int i;
	*tlvif (ctl-!_tlvss	urn 0;
},
};;i(ctl->of _from)(voiddata, _tlv, sizeof(aatas)ss	urn 0;
},
};;i(ctl-data[1] >= MAX_DB_SSIZEss	urn 0;
},
};;i(tlv = koc.h>
(data[1] + sizeof(aatas, GFP_KERNELstructl-!tlvss	urn 0;
},
};;i(memcpy(tlv, data, sizeof(aatas);i(ctl->of _from)(voidtlv + 2, _tlv + 2, data[1])u0x00	t sof(tlvs;s	urn 0;
},
};;i(}rurn 0;
}tlviftatic int snd_>of _g*) (ct snd_emu10k1_fx8u = s,
		Creaaet snd_emu10k1_fx8010_ctl *ol_chipctln*g*) ,
		Creaaet snd_emu10k1_fx8010_ctl *ol_chipctln_)(voi *_g*) ,
		Creaai;
	
dx	struct snd_emu10k1_fx8010_ctl *ol_chipoldpctln_)(voi *o=
	;rructl-0k1->ort.
 *_tlvss	urn 0;
}>of _from)(voidg>mi, &_g*) [
dx], sizeof(*g*) s);i(o=
		( uct snd_emu10k1_fx8010_ctl *ol_chipoldpctln_)(voi *)_g*) ;i(ctl->of _from)(voidg>mi, &o*) [
dx], sizeof(*o*) s)uinfrn 0;
}-EFAULTnsig>min tlv = ,
};;i(rn 0;
}

static int snd_>of _g*) _to)(voidlt snd_emu10k1_fx8u = s,
		Creaaet snd_emu10k1_fx8010_ctl *ol_chipctln_)(voi *_g*) ,
		Creaaet snd_emu10k1_fx8010_ctl *ol_chipctln*g*) ,
		Creaai;
	
dx	struct snd_emu10k1_fx8010_ctl *ol_chipoldpctln_)(voi *o=
	;rructl-0k1->ort.
 *_tlvss	urn 0;
}>of _to)(void&_g*) [
dx], g>mi, sizeof(*g*) s);i(i(o=
		( uct snd_emu10k1_fx8010_ctl *ol_chipoldpctln_)(voi *)_g*) ;i(rn 0;
}>of _to)(void&o*) [
dx], g>mi, sizeof(*o*) s);itatic int snd_emu10k1_gpr_verify *ol_chis(ct snd_emu10k1_fx8u = s,inff	aCreaeaet snd_emu10k1_fx8010_ctl *odconi*odcsstrugned int i;
	
	spct snd_ctl_elem_info do_)(voi *_id	spct snd_ctl_elem_info doid	spct snd_ctl_0k1_fx8010_ctl *ol_chipctln*g*) ;rusnd_err;i(i(test(ind_0, _ do= c*odcn voludel *ol_chis;i(Creaai <lc*odcn voludel *ol_chi_t;
	ui i++, _ d++u0x00Creaa(ctl->of _from)(void&id, _id, sizeof(cds)uinCreaa(	rn 0;
}-EFAULTnsi	ctl-emu10k1_fx80look_tes *) (= s, &id)1)
	,
};uinffrn 0;
}-ENOENTnsi}rug=
		( koc.h>
(sizeof(*g*) s, GFP_KERNELstructl-! g>miuinfrn 0;
}-ENOMEM;
	errnd_0;sptest(ind_0; i <-c*odcn voluadd *ol_chi_t;
	ui i++u0x00	ctl->of _g*) (= s, g>mi, c*odcn voluadd *ol_chis, c)u0x00		errnd_-EFAULTnsi		goto __errortru	}si	ctl-emu10k1_fx80look_tes *) (= s, &g>min id)uinf	contin	unsi	down_read(&0k1->cardn t l_chis_rwsem)nsi	ctl-emu1elem EMd_id(0k1->card, &g>min id)n!l-,
};u x00		up_read(&0k1->cardn t l_chis_rwsem)nsi		errnd_-EEXISTnsi		goto __errortru	}si	up_read(&0k1->cardn t l_chis_rwsem)nsi	ctl-d>min id.ifacco!DRV_CTL_ELEM_TYPEIFACE_MIXERo&&
		Cread>min id.ifacco!DRV_CTL_ELEM_TYPEIFACE_PCMu0x00		errnd_-EINVALnsi		goto __errortru	}si}sptest(ind_0; i <-c*odcn volulis
_*ol_chi_t;
	ui i++u0x00Creaa(/* FIXME: we nthe fo check fronWRITE acor F#deff	ctl->of _g*) (= s, g>mi, c*odcn volulis
_*ol_chis, c)u0x00		errnd_-EFAULTnsi		goto __errortru	}si}
a__error:int sof(g>miu;i(rn 0;
}err;i}atic int  snd_leav0k1_fx80get(pte_valu sof(ct snd_emu1trol *kcontrt
	struct snd_emu10k1 *em010_ctl *ctl =
	;ru
	=
		( uct snd_emu10k1_fx8010_ctl *) kcontrtrivate_value;
	unsitrtrivate_value;
	und_0;splis
_del(&rtrivlis
utruk sof(>miu;i(ctl-trmin tlv.p	s		k sof(trmin tlv.p	;itatic int snd_emu10k1_gpr_add *ol_chis(ct snd_emu10k1_fx8u = s,inff	aCreet snd_emu10k1_fx8010_ctl *odconi*odcsstrugned int i;
	
, jnsict snd_ctl_0k1_fx8010_ctl *ol_chipctln*g*) ;ruct snd_emu10k1 *em010_ctl *ctl =
	, *n=
	;ruct snd_emu1trol *kc_new knew;ruct snd_emu1trol *kcontr
	;ruct snd_emu1elem_value *ucone;
nsisnd_errnd_0;s
(e;
nd_koc.h>
(sizeof(*e;
s, GFP_KERNELstrug=
		( koc.h>
(sizeof(*g*) s, GFP_KERNELstrun=
		( koc.h>
(sizeof(*n*) s, GFP_KERNELstructl-!e;
n|| !g=
		|| !n*) s0x00	0rrnd_-ENOMEM;
		goto __errortru}
sptest(ind_0; i <-c*odcn voluadd *ol_chi_t;
	ui i++u0x00	ctl->of _g*) (= s, g>mi, c*odcn voluadd *ol_chis, c)u0x00		errnd_-EFAULTnsi		goto __errortru	}si	ctl-d>min id.ifacco!DRV_CTL_ELEM_TYPEIFACE_MIXERo&&
		Cread>min id.ifacco!DRV_CTL_ELEM_TYPEIFACE_PCMu0x00		errnd_-EINVALnsi		goto __errortru	}siuctl-! g>miivid.na"G[0]y x00		errnd_-EINVALnsi		goto __errortru	}siu=
		( emu10k1_fx80look_tes *) (= s, &g>min id)tru	memfs((&knew, 0, sizeof(knews);i(	knew.ifacco)ad>min id.ifacc;i(	knew.na"Go)ad>min id.na"G;i(	knew.indexo)ad>min id.index;i(	knew.devicco)ad>min id.devicc;i(	knew.subdevicco)ad>min id.subdevicc;i(	knew.info	( emu10k1_fx80ctl_info(str;i(	knew.tlv.pol->of _tlv(g>min tlv)nsi	ctl-knew.tlv.puinf	knew.acor F#DRV_CTL_ELEM_TYPEACCESS_READWRITE |inff	V_CTL_ELEM_TYPEACCESS_DB_SREAD;i(	knew.getnd_kcon0k1_fx80ctl_get(str;i(	knew.*/
#d_kcon0k1_fx80ctl_get(*/
tru	memfs((n>mi, 0, sizeof(*n*) s)nsi	n>vcount;
	u1)ad>min nt;
	uinf	n>vcout;
	u1)ad>min t;
	uinf	test(jnd_0; jn<-32; j++u x00		n>min;ctl[j]1)ad>min ctl[j];00		n>min;e.int[j]1)a~g>min;e.int[j];0x1fcnvert0x09we wand_tote to tnew e *ucoin ctl_get(puru)#deff	(e;
ive.integer.max e.int[j]1)ad>min e.int[j];ru	}siun>min;
	u1)ad>min 
	uinf(n>min;
	r1)ad>min 
	retu(n>min transla) an1)ad>min transla) annsi	ctl-=
		(l-,
};u x00		=
		( koc.h>
(sizeof(**) s, GFP_KERNELstrui	ctl-=
		(l-,
};u x00			0rrnd_-ENOMEM;
				k sof(tnew.tlv.pu;
				goto __errortru		}ru		knew.*te_value;
	und_(gned long fla)r
	;ru		 =
		( *n=
	;ruf	ctl--0rrnd_emu1elemadd(0k1->card, k=
		( emu1elemnew1(&knew, 0k1s)un<-;y x00	fuk sof(>miu;i(			k sof(tnew.tlv.pu;
				goto __errortru		}ru		krtrivate_valu soft=_leav0k1_fx80get(pte_valu sof;00		=
	outrol, st	( k=
	;ruf	lis
_add s.
 (&rtrivlis
, &0k1->10_ctl.ctl_gettat		} 
		u x00		x1fovere to tdeff	(nrtrivlis
ol->vcoulis
;00		n>min;trol *kcol->vcoutrol, stnsi		 =
		( *n=
	;ruf	emu1elemnotify(0k1->card, V_CTL_ELEM_VENT_MASK_VALUE |inff               CreaaaaaaaaV_CTL_ELEM_VENT_MASK_INFO, &>vcoutrol, stn id)tru	}si	scon0k1_fx80ctl_get(puru>vcoutrol, st, e;
stru}r  Crea__error:ink sof(ngettat	t sof(g>miu;i(t sof(e;
strurn 0;
}err;i}atic int snd_emu10k1_gpr_del *ol_chis(ct snd_emu10k1_fx8u = s,inff	aCreet snd_emu10k1_fx8010_ctl *odconi*odcsstrugned int i;
	
	spct snd_ctl_elem_info doid	spct snd_ctl_elem_info do_)(voi *_id	spct snd_ctl_0k1_fx8010_ctl *ctl =
	;ruct snd_emu1cardl =ardl
	0k1->=ard;i(i(test(ind_0, _ do= c*odcn voludel *ol_chis;i(Creaai <lc*odcn voludel *ol_chi_t;
	ui i++, _ d++u0x00Creaa(ctl->of _from)(void&id, _id, sizeof(cds)uin(	rn 0;
}-EFAULTnsi	down_e to (&cardn t l_chis_rwsem)nsi	=
		( emu10k1_fx80look_tes *) (= s, &id)nsi	ctl->miuinf	emu1elemivmoa (card, >vcoutrol, st)nsi	up_e to (&cardn t l_chis_rwsem)nsi}rurn 0;
}

static int snd_emu10k1_gpr_lis
_*ol_chis(ct snd_emu10k1_fx8u = s,inff	aCreaet snd_emu10k1_fx8010_ctl *odconi*odcsstrugned int i;
	
nd_0, jnsigned int i;
	tot;
nd_0nsict snd_ctl_0k1_fx8010_ctl *ol_chipctln*g*) ;ruct snd_emu10k1 *em010_ctl *ctl =
		spct snd_ctl_elem_info do*id	srug=
		( koc.h>
(sizeof(*g*) s, GFP_KERNELstructl-! g>miuinfrn 0;
}-ENOMEM;
rulis
_tes eacher_ury->mi, &0k1->10_ctl.ctl_get, lis
u0x00	tot;
++
s	fctl-c*odcn volulis
_*ol_chiso&&
		Creai <-c*odcn volulis
_*ol_chi_t;
	uy x00	fmemfs((g>mi, 0, sizeof(*g*) s);i(	fcdo= &>vcoutrol, stn idnsi		g>min id.ifacco
	cdivifacc;ruf	etrlcpy(d>min id.na"GPRcdivna"G, sizeof(d>min id.na"Gs);i(	fd>min id.index 
	cdivindex;i(	fd>min id.devicco)acdivdevicc;i(		d>min id.subdevicco)acdivsubdevicc;i(		d>min nt;
	utl->vcount;
	uinf		d>min t;
	utl->vcout;
	uinf		test(jnd_0; jn<-32; j++u x00			d>min ctl[j]tl->vcouctl[j];00			d>min e.int[j]nd_>min;e.int[j];00		}nf		d>min = ctl->min;
	uinf		d>min = ctl->max;
	retu		d>min transla) an1)a>min transla) annsi		ctl->of _g*) _to)(void= s, c*odcn volulis
_*ol_chis,
nff		Creaeag>mi, c)y x00	fuk sof(g>miu;i(n(	rn 0;
}-EFAULTnsi		}nf		i++
s	f}si}spc*odcn volulis
_*ol_chi_tot;
nd_tot;
at	t sof(g>miu;i(rn 0;
}

static int snd_emu10k1_gpr_it dUupok (ct snd_emu10k1_fx8u = s,inff	aaet snd_emu10k1_fx8010_ctl *odconi*odcsstrusnd_errnd_0;s
(x.h>
__irq(&0k1->10_ctl._irqstructl-(0rrnd_emu10k1_gpr_verify *ol_chis(= s, c*odc)un<-;y
		goto __errortruetrlcpy(0k1->10_ctl.na"GPRc*odcn na"G, sizeof(0k1->10_ctl.na"Gs);i(x1fstop FXcessor FX80-s progmayibced.");rous, WITHit'sibctversto miss00Creso"Gosa Plast itn cal Puo twrflagon feed[jk]tdeffctl-0k1->gy_outu
		emu10k1_fx80ptr_e to (0k1, A_DBG, 0, 0k1->10_ctl.dbg | A_DBG_SINGLE_STEP);i(
		uinfemu10k1_fx80ptr_e to (0k1, DBG, 0, 0k1->10_ctl.dbg | 0K1_CENTDBG_SINGLE_STEP);i(x1fok, de Freem*/
sjobtdeffctl-(0rrnd_emu10k1_gpr_del *ol_chis(= s, c*odc)un<-; ||
	eaea(0rrnd_emu10k1_gpr_ctl_pok (= s, c*odc)un<-; ||
	eaea(0rrnd_emu10k1_gpr_tram_pok (= s, c*odc)un<-; ||
	eaea(0rrnd_emu10k1_gpr_t dUupok (= s, c*odc)un<-; ||
	eaea(0rrnd_emu10k1_gpr_add *ol_chis(= s, c*odc)un<-;y
		goto __errortrux1fstart FXcessor FX80whhe implcontrodcorogupdain tdeffctl-0k1->gy_outu
		emu10k1_fx80ptr_e to (0k1, A_DBG, 0, 0k1->10_ctl.dbg);i(
		uinfemu10k1_fx80ptr_e to (0k1, DBG, 0, 0k1->10_ctl.dbg);i  Crea__error:inx.h>
_un_irq(&0k1->10_ctl._irqstrurn 0;
}err;i}atic int snd_emu10k1_gpr_it dUupeek(ct snd_emu10k1_fx8u = s,inff	aaet snd_emu10k1_fx8010_ctl *odconi*odcsstrusnd_err;s
(x.h>
__irq(&0k1->10_ctl._irqstruetrlcpy(c*odcn na"G, 0k1->10_ctl.na"GPRsizeof(c*odcn na"Gs);i(x1fok, de Freem*/
sjobtdeff0rrnd_emu10k1_gpr_ctl_peek(= s, c*odc)tructl-0rrn>=-;y
		0rrnd_emu10k1_gpr_tram_peek(= s, c*odc)tructl-0rrn>=-;y
		0rrnd_emu10k1_gpr_t dUupeek(= s, c*odc)tructl-0rrn>=-;y
		0rrnd_emu10k1_gpr_lis
_*ol_chis(= s, c*odc)trux.h>
_un_irq(&0k1->10_ctl._irqstrurn 0;
}err;i}atic int snd_emu10k1_gpr_ipcm_pok (ct snd_emu10k1_fx8u = s,inff	aet snd_emu10k1_fx8010_ctl pcm_reconipcmsstrugned int i;
	
	spsnd_errnd_0;suct snd_emu10k1 *em010_ctl pcm *pcm;rructl-ipcmivsubct eamn>=-0K1_CENT10
 *
_PCM_COUNTuinfrn 0;
}-EINVALnsictl-ipcmivch."n
		 >-32uinfrn 0;
}-EINVALnsipcm = &0k1->10_ctl.pcm[ipcmivsubct eam];
(x.h>
__irq(&0k1->10_ctl._irqstruelock_irqss()(&0k1->regk_irq)tructl-pcmivopal ds0x00	0rrnd_-EBUSY;
		goto __errortru}
ictl-ipcmivch."n
		  && s0x(x1fivmoa tdeff	pcmive;
idnd_0;su} 
		u x00	/* FIXME: we nthe fo add uniion.;
n*odcohe FreeCapttransfersrnes fo#deff	ctl-ipcmivch."n
		 !
	2u0x00		errnd_-EINVALnsi		goto __errortru	}siupcmive;
idnd_1etu(pcmivopal dnd_0;su	pcmivch."n
		   ipcmivch."n
		;su	pcmivtram_start   ipcmivtram_start;su	pcmivbuffer_size   ipcmivbuffer_size;su	pcmivctl_size   ipcmivctl_size;su	pcmivctl_t;
	utl-ipcmivctl_t;
	u;su	pcmivctl_tmpt;
	utl-ipcmivctl_tmpt;
	u;su	pcmivctl_ptr l-ipcmivctl_ptr;su	pcmivctl_trigger l-ipcmivctl_trigger;su	pcmivctl_runninget_ipcmivctl_runninginf	test(ind_0; i <-pcmivch."n
		; i++uinf	pcmivetram[i]tl-ipcmivetram[i]tru}r  Crea__error:in_lockun_irqss()(&0k1->regk_irq)trux.h>
_un_irq(&0k1->10_ctl._irqstrurn 0;
}err;i}atic int snd_emu10k1_gpr_ipcm_peek(ct snd_emu10k1_fx8u = s,inff	aet snd_emu10k1_fx8010_ctl pcm_reconipcmsstrugned int i;
	
	spsnd_errnd_0;suct snd_emu10k1 *em010_ctl pcm *pcm;rructl-ipcmivsubct eamn>=-0K1_CENT10
 *
_PCM_COUNTuinfrn 0;
}-EINVALnsipcm = &0k1->10_ctl.pcm[ipcmivsubct eam];
(x.h>
__irq(&0k1->10_ctl._irqstruelock_irqss()(&0k1->regk_irq)trucpcmivch."n
		   pcmivch."n
		;suipcmivtram_start   pcmivtram_start;suipcmivbuffer_size   pcmivbuffer_size;suipcmivctl_size   pcmivctl_size;suipcmivctl_ptr l-pcmivctl_ptr;suipcmivctl_t;
	utl-pcmivctl_t;
	u;suipcmivctl_tmpt;
	utl-pcmivctl_tmpt;
	u;suipcmivctl_triggertl-pcmivctl_trigger;suipcmivctl_runningtl-pcmivctl_runninginftest(ind_0; i <-pcmivch."n
		; i++uinfipcmivetram[i]tl-pcmivetram[i]truipcmivres1tl-ipcmivres2nd_0;suipcmivpadnd_0;su_lockun_irqss()(&0k1->regk_irq)trux.h>
_un_irq(&0k1->10_ctl._irqstrurn 0;
}err;i}atine EMU1SND_0K1_CENTGPR_CONTROLS	44tine EMU1SND_0K1_CENTINPUTS		12tine EMU1SND_0K1_CENTPLAYBACK_CHANNELS	8tine EMU1SND_0K1_CENTURE_DIGICHANNELS	4atic int  snd
emu10k1_fx80.h>
_mono_*ol_chiuct snd_emu10k1_fx8010_ctl *ol_chipctln*c) ,
			Creaeat u32  *audina"GPRcnd_ctlPRcnd_ne e;
sstru>min id.ifacco
	V_CTL_ELEM_TYPEIFACE_MIXER;suct cpy(>min id.na"GPRna"Gs;ru>min vt;
	utl->vcout;
	und_1etu>vcouctl[0]tl-ctln+_0; >min;e.int[0]tl-ne e;
tructl-_res_gpr_volume, "Gy x00	cmin = ctl-0;00	cmin = ctl-ffffff,
};;00	cmin tlv = emu10k1_gpr_dnear, TL;00	cmin transla) an1)a0K1_CENTGPR_TRANSLATION_NONE;su} 
		u x00	cmin = ctl-0;00	cmin = ctl-100;00	cmin tlv = emu10k1_gpr_dnee1, -4;00	cmin transla) an1)a0K1_CENTGPR_TRANSLATION_TABLE100;f	}r*
 ic int  snd
emu10k1_fx80.h>
_s
seeo_*ol_chiuct snd_emu10k1_fx8010_ctl *ol_chipctln*c) ,
				t u32  *audina"GPRcnd_ctlPRcnd_ne e;
sstru>min id.ifacco
	V_CTL_ELEM_TYPEIFACE_MIXER;suct cpy(>min id.na"GPRna"Gs;ru>min vt;
	utl->vcout;
	und_2etu>vcouctl[0]tl-ctln+_0; >min;e.int[0]tl-ne e;
tru>vcouctl[1]tl-ctln+_1; >min;e.int[1]tl-ne e;
tructl-_res_gpr_volume, "Gy x00	cmin = ctl-0;00	cmin = ctl-ffffff,
};;00	cmin tlv = emu10k1_gpr_dnear, TL;00	cmin transla) an1)a0K1_CENTGPR_TRANSLATION_NONE;su} 
		u x00	cmin = ctl-0;00	cmin = ctl-100;00	cmin tlv = emu10k1_gpr_dnee1, -4;00	cmin transla) an1)a0K1_CENTGPR_TRANSLATION_TABLE100;f	}r*
 ic int  snd
emu10k1_fx80.h>
_mono_f_tabl*ol_chiuct snd_emu10k1_fx8010_ctl *ol_chipctln*c) ,
				eaeat u32  *audina"GPRcnd_ctlPRcnd_ne e;
sstru>min id.ifacco
	V_CTL_ELEM_TYPEIFACE_MIXER;suct cpy(>min id.na"GPRna"Gs;ru>min vt;
	utl->vcout;
	und_1etu>vcouctl[0]tl-ctln+_0; >min;e.int[0]tl-ne e;
trucmin = ctl-0;00cmin = ctl-1;00cmin transla) an1)a0K1_CENTGPR_TRANSLATION_ONOFF;i}atic int  snd
emu10k1_fx80.h>
_s
seeo_f_tabl*ol_chiuct snd_emu10k1_fx8010_ctl *ol_chipctln*c) ,
				eaeaeat u32  *audina"GPRcnd_ctlPRcnd_ne e;
sstru>min id.ifacco
	V_CTL_ELEM_TYPEIFACE_MIXER;suct cpy(>min id.na"GPRna"Gs;ru>min vt;
	utl->vcout;
	und_2etu>vcouctl[0]tl-ctln+_0; >min;e.int[0]tl-ne e;
tru>vcouctl[1]tl-ctln+_1; >min;e.int[1]tl-ne e;
trucmin = ctl-0;00cmin = ctl-1;00cmin transla) an1)a0K1_CENTGPR_TRANSLATION_ONOFF;i}at*   coUs cofest0k1_f10 -at uion. an1from 32-bi2  re Righinputs1from HANA  coto 2 x 16-bi2 regis
sesoin gy_out -s peir e *ucs arceivadnvia DMA.  coC uion. an1is perfesm coby Ay_out continct snd ans of 10
 *
.
static conssnd_emu10k1_gpr_ay_outsdsp t uiont_32s)o_2x16(
				ct snd_emu10k1_fx8010_ctl *odconi*odc,inff	gnof*ptr, c;
	tmp, c;
	b>
_shif
se16,inff	c;
	regkin, c;
	regknessstruA_OP(i*odc, ptr, iACC3, A_GPR(tmp + 1), regkin, A_C_0000, 0x00A_C_0000, 0xs;ruA_OP(i*odc, ptr, iANDXOR, A_GPR(tmp), A_GPR(tmp + 1), A_GPR(b>
_shif
se16 -s1), A_C_0000, 0xs;ruA_OP(i*odc, ptr, iTSTNEG, A_GPR(tmp + 2), A_GPR(tmp), A_C_8000, 0x00A_GPR(b>
_shif
se16 -s2)s;ruA_OP(i*odc, ptr, iANDXOR, A_GPR(tmp + 2), A_GPR(tmp + 2), A_C_8000, 0x00A_C_0000, 0xs;ruA_OP(i*odc, ptr, iANDXOR, A_GPR(tmp), A_GPR(tmp), A_GPR(b>
_shif
se16 -s3), A_C_0000, 0xs;ruA_OP(i*odc, ptr, iMACINT0, A_GPR(tmp), A_C_0000, 0x00A_GPR(tmp), A_C_0001, 0xs;ruA_OP(i*odc, ptr, iANDXOR, regknes00A_GPR(tmp), A_C_ffff,
};
 A_GPR(tmp + 2)s;ruA_OP(i*odc, ptr, iACC3, regknes + 1, A_GPR(tmp + 1), A_C_0000, 0x00A_C_0000, 0xs;rurn 0;
}1;i}at*   co.h>
ial controlfigura) an1festAy_out

static int snd__emu10k1_gpr_ay_outs.h>
_efxuct snd_emu10k1_fx8u = s	strusnd_err,	
, z,_ctlPRn=
	;ruc;
	b>
_shif
se16;00c u32 i;
	playbacktl-10;00c u32 i;
	 re Righ=	playbackt+ (SND_0K1_CENTPLAYBACK_CHANNELS co2); /* we gprerve-10  snceF#deffc u32 i;
	s
seeo_mictl->re Righ+_2etu> u32 i;
	tmp 
	0x88;rugnofptr;suct snd_emu10k1 *em010_ctl *odconi*odc 
	,
};;in_t snd_emu10k1_fx8010_ctl *ol_chipctln*col_chiso
	,
};,l =
		spgnof*volumap;_segment_t fs =ent;rru0rrnd_-ENOMEM;
	i*odc 
	kzc.h>
(sizeof(*s*odc), GFP_KERNELstructl-!i*odcss	urn 0;
}err;ispc*odcn volumap = (u i_u32s)
__(voi *) kcc.h>
(512h+_256h+_256h+_2 co1024,
nff			Creaeaeizeof(u i_u32s)), GFP_KERNELstructl-!i*odcn volumapy
		goto __err_ctlnsicol_chiso
	kcc.h>
(SND_0K1_CENTGPR_CONTROLS,
			Cresizeof(**ol_chis), GFP_KERNELstructl-!*ol_chis)
		goto __err_ctr		;s
	volumap = (unof__tesccon)i*odcn volumap;_spc*odcn tram_aataumapo= c*odcn volumapo+ 512;spc*odcn tram_addrumapo= c*odcn tram_aataumapo+_256;spc*odcn *odc 
	c*odcn tram_addrumapo+_256;si(x1fcl TL  softGPRF#defftest(ind_0; i <-512; i++uinffs(gbit(i, c*odcn volum;
id)etu(i(x1fcl TL TRAM data & addrr F#ar, F#defftest(ind_0; i <-256; i++uinffs(gbit(i, c*odcn tram_m;
id)etsuct cpy(c*odcn na"G, "Ay_out cont*odc festALSA")etuptr l-0;00n=
		( 0;00ctlnd_s
seeo_mict+-10;00volumap[vol++] 
	0x, 0x7
};;00volumap[vol++] 
	0x, 0x8000;00volumap[vol++] 
	0x, 0x,
};;00b>
_shif
se16 =_ctlnsi(x1fstop FXcessor FX80deffemu10k1_fx80ptr_e to (0k1, A_DBG, 0, (0k1->10_ctl.dbg && s0| A_DBG_SINGLE_STEP);i
#ctl1i(x1fCaptfro;
	PlaybacktVe, "Gt(independe;
	from s
seeo micuinC*	playbacktl-0t+ (-ctln* FXBUS_PCM_LEFT_FRONT >> 31uinC*	whsee-ctln*ol_ainc attenua) an1from corrr pondingtmicertrols
 *inC*	(emu10k1_fx80.h>
_s
seeo_*ol_chiuinC*/ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback), A_C_0000, 0x00A_GPR(ctl), A_FXBUS(FXBUS_PCM_LEFT_FRONT)s;ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+1), A_C_0000, 0x00A_GPR(ctl+1), A_FXBUS(FXBUS_PCM_RIGHT_FRONT)s;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], "CaptFro;
	PlaybacktVe, "G",_ctlPR10xs;ructln+d_2eti(x1fCaptSurr;
	d	Playbackt(independe;
	from s
seeo micuC*/ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+2), A_C_0000, 0x00A_GPR(ctl), A_FXBUS(FXBUS_PCM_LEFT_REAR)s;ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+3), A_C_0000, 0x00A_GPR(ctl+1), A_FXBUS(FXBUS_PCM_RIGHT_REAR)s;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], "CaptSurr;
	d	PlaybacktVe, "G",_ctlPR10xs;ructln+d_2et	i(x1fCaptSidc Playbackt(independe;
	from s
seeo micuC*/ructl-0k1->card_>reabil>
ies->spk71y x00	A_OP(i*odc, &ptr, iMACx00A_GPR(playback+6), A_C_0000, 0x00A_GPR(ctl), A_FXBUS(FXBUS_PCM_LEFT_SIDEs);i(	A_OP(i*odc, &ptr, iMACx00A_GPR(playback+7), A_C_0000, 0x00A_GPR(ctl+1), A_FXBUS(FXBUS_PCM_RIGHT_SIDEs);i(	emu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], "CaptSidc PlaybacktVe, "G",_ctlPR10xs;ruuctln+d_2et	}ti(x1fCaptCr_use	Playbackt(independe;
	from s
seeo micuC*/ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+4), A_C_0000, 0x00A_GPR(ctl), A_FXBUS(FXBUS_PCM_CENTER)s;ruemu10k1_fx80.h>
_mono_*ol_chiu&*ol_chis[n=
	++], "CaptCr_use	PlaybacktVe, "G",_ctlPR10xs;ructl++
si(x1fCaptLFE	Playbackt(independe;
	from s
seeo micuC*/ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+5), A_C_0000, 0x00A_GPR(ctl), A_FXBUS(FXBUS_PCM_LFE)s;ruemu10k1_fx80.h>
_mono_*ol_chiu&*ol_chis[n=
	++], "CaptLFE	PlaybacktVe, "G",_ctlPR10xs;ructl++
s	i(x1inC*	S
seeo MixinC*/rux1fWave-(PCMu0PlaybacktVe, "Gt(willibceivna"Gd la
seuC*/ruA_OP(i*odc, &ptr, iMACx00A_GPR(s
seeo_mic), A_C_0000, 0x00A_GPR(ctl), A_FXBUS(FXBUS_PCM_LEFT)s;ruA_OP(i*odc, &ptr, iMACx00A_GPR(s
seeo_mic+1), A_C_0000, 0x00A_GPR(ctl+1), A_FXBUS(FXBUS_PCM_RIGHT)s;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], "Wave-PlaybacktVe, "G",_ctlPR10xs;ructln+d_2eti(x1fSynth-Playbackt*/ruA_OP(i*odc, &ptr, iMACx00A_GPR(s
seeo_mic+0)00A_GPR(s
seeo_mic+0)00A_GPR(ctl), A_FXBUS(FXBUS_MIDI_LEFT)s;ruA_OP(i*odc, &ptr, iMACx00A_GPR(s
seeo_mic+1), A_GPR(s
seeo_mic+1), A_GPR(ctl+1), A_FXBUS(FXBUS_MIDI_RIGHT)s;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], "Synth-PlaybacktVe, "G",_ctlPR10xs;ructln+d_2eti(x1fWave-(PCMu0Cre Righ*/ruA_OP(i*odc, &ptr, iMACx00A_GPR(>re Rig+0)00A_C_0000, 0x00A_GPR(ctl), A_FXBUS(FXBUS_PCM_LEFT)s;ruA_OP(i*odc, &ptr, iMACx00A_GPR(>re Rig+1), A_C_0000, 0x00A_GPR(ctl+1), A_FXBUS(FXBUS_PCM_RIGHT)s;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], "CaptCre RighVe, "G",_ctlPRxs;ructln+d_2eti(x1fSynth-Cre Righ*/ruA_OP(i*odc, &ptr, iMACx00A_GPR(>re Rig+0)00A_GPR(>re Rig+0)00A_GPR(ctl), A_FXBUS(FXBUS_MIDI_LEFT)s;ruA_OP(i*odc, &ptr, iMACx00A_GPR(>re Rig+1), A_GPR(>re Rig+1), A_GPR(ctl+1), A_FXBUS(FXBUS_MIDI_RIGHT)s;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], "Synth-Cre RighVe, "G",_ctlPRxs;ructln+d_2etCreaeai(x1inC*	inputsinC*/rine EMU1A_ADD_VOLUMTEGE(var,vol,input) \
A_OP(i*odc, &ptr, iMACx00A_GPR(var)00A_GPR(var)00A_GPR(vol)00A_EXTGE(input))ti(x1f0k1_21P cont0 a	d	cont1-Cre Righ*/ructl-0k1->card_>reabil>
ies->0k1_modelu0x00	ctl-0k1->card_>reabil>
ies->cactl8p(kco) x00		x1fNote:JCD:Nog flase	bi2 shif
g fwse	16bi2soto uppse	16bi2soof 32bi2 e.intetdeff	(A_OP(i*odc, &ptr, iMACINT0, A_GPR(tmp), A_C_0000, 0x00A3_0K132GE(0x,), A_C_0000, 0stat		uA_OP(i*odc, &ptr, iMACx00A_GPR(>re Rig+0)00A_GPR(>re Rig+0)00A_GPR(ctl), A_GPR(tmp)tat		uA_OP(i*odc, &ptr, iMACINT0, A_GPR(tmp), A_C_0000, 0x00A3_0K132GE(0x1), A_C_0000, 0stat		uA_OP(i*odc, &ptr, iMACx00A_GPR(>re Rig+1), A_GPR(>re Rig+1), A_GPR(ctl), A_GPR(tmp)tat		} 
		u x00		A_OP(i*odc, &ptr, iMACx00A_GPR(>re Rig+0)00A_GPR(>re Rig+0)00A_GPR(ctl), A_P16VGE(0x,)tat		uA_OP(i*odc, &ptr, iMACx00A_GPR(>re Rig+1), A_GPR(>re Rig+1), A_GPR(ctl+1), A_P16VGE(0x1))tru	}si	scon0k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], "0K1-Cre RighVe, "G",_ctlPRxs;ruuctln+d_2et	}t	x1fAC'970PlaybacktVe, "Gt- us coonly festmic (ivna"Gd la
seuC*/ruA_ADD_VOLUMTEGE(s
seeo_mic,_ctlPRA_EXTGE_AC97_Ls;ruA_ADD_VOLUMTEGE(s
seeo_mic+1,_ctl+1PRA_EXTGE_AC97_Rs;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], "AMic PlaybacktVe, "G",_ctlPRxs;ructln+d_2et	x1fAC'970Cre RighVe, "Gt- us coonly festmic */ruA_ADD_VOLUMTEGE(>re Rig,_ctlPRA_EXTGE_AC97_Ls;ruA_ADD_VOLUMTEGE(>re Rig+1,_ctl+1PRA_EXTGE_AC97_Rs;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], "Mic Cre RighVe, "G",_ctlPRxs;ructln+d_2eti(x1fmic >re Righbuffer */	ruA_OP(i*odc, &ptr, iGER;RPPRA_EXTOUT(A_EXTOUT_MICTURE)00A_EXTGE(A_EXTGE_AC97_Ls0000cd00A_EXTGE(A_EXTGE_AC97_R)s;rt	x1fAy_out CD0PlaybacktVe, "Gt*/ruA_ADD_VOLUMTEGE(s
seeo_mic,_ctlPRA_EXTGE_SPDIF_CD_Ls;ruA_ADD_VOLUMTEGE(s
seeo_mic+1,_ctl+1PRA_EXTGE_SPDIF_CD_Rs;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++],
nff		0k1->card_>reabil>
ies->ac97_(kco ? "Ay_out CD0PlaybacktVe, "G" : "CD0PlaybacktVe, "G",
nff		ctlPRxs;ructln+d_2et	x1fAy_out CD0Cre RighVe, "Gt*/ruA_ADD_VOLUMTEGE(>re Rig,_ctlPRA_EXTGE_SPDIF_CD_Ls;ruA_ADD_VOLUMTEGE(>re Rig+1,_ctl+1PRA_EXTGE_SPDIF_CD_Rs;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++],
nff		0k1->card_>reabil>
ies->ac97_(kco ? "Ay_out CD0Cre RighVe, "G" : "CD0Cre RighVe, "G",
nff		ctlPRxs;ructln+d_2et
a(/* Optical SPDIF0PlaybacktVe, "Gt*/ruA_ADD_VOLUMTEGE(s
seeo_mic,_ctlPRA_EXTGE_OPT_SPDIF_Ls;ruA_ADD_VOLUMTEGE(s
seeo_mic+1,_ctl+1PRA_EXTGE_OPT_SPDIF_Rs;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], V_CTL_ELEMNAMTEGEC958("Optical ",PLAYBACK,VOLUMT),_ctlPRxs;ructln+d_2et	x1fOptical SPDIF0Cre RighVe, "Gt*/ruA_ADD_VOLUMTEGE(>re Rig,_ctlPRA_EXTGE_OPT_SPDIF_Ls;ruA_ADD_VOLUMTEGE(>re Rig+1,_ctl+1PRA_EXTGE_OPT_SPDIF_Rs;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], V_CTL_ELEMNAMTEGEC958("Optical ",URE_DIG,VOLUMT),_ctlPRxs;ructln+d_2ett	x1fLEMU20PlaybacktVe, "Gt*/ruA_ADD_VOLUMTEGE(s
seeo_mic,_ctlPRA_EXTGE_AR(s2_Ls;ruA_ADD_VOLUMTEGE(s
seeo_mic+1,_ctl+1PRA_EXTGE_AR(s2_Rs;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++],
nff		0k1->card_>reabil>
ies->ac97_(kco ? "LEMU20PlaybacktVe, "G" : "LEMU0PlaybacktVe, "G",
nff		ctlPRxs;ructln+d_2et	x1fLEMU20Cre RighVe, "Gt*/ruA_ADD_VOLUMTEGE(>re Rig,_ctlPRA_EXTGE_AR(s2_Ls;ruA_ADD_VOLUMTEGE(>re Rig+1,_ctl+1PRA_EXTGE_AR(s2_Rs;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++],
nff		0k1->card_>reabil>
ies->ac97_(kco ? "LEMU20Cre RighVe, "G" : "LEMU0Cre RighVe, "G",
nff		ctlPRxs;ructln+d_2etaaaaaaaai(x1fChil>ps ADC0PlaybacktVe, "Gt*/ruA_ADD_VOLUMTEGE(s
seeo_mic,_ctlPRA_EXTGE_ADC_Ls;ruA_ADD_VOLUMTEGE(s
seeo_mic+1,_ctl+1PRA_EXTGE_ADC_Rs;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], "Analog Mix PlaybacktVe, "G",_ctlPRxs;ructln+d_2et	x1fChil>ps ADC0Cre RighVe, "Gt*/ruA_ADD_VOLUMTEGE(>re Rig,_ctlPRA_EXTGE_ADC_Ls;ruA_ADD_VOLUMTEGE(>re Rig+1,_ctl+1PRA_EXTGE_ADC_Rs;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], "Analog Mix Cre RighVe, "G",_ctlPRxs;ructln+d_2eti(x1fAux20PlaybacktVe, "Gt*/ruA_ADD_VOLUMTEGE(s
seeo_mic,_ctlPRA_EXTGE_AUX2_Ls;ruA_ADD_VOLUMTEGE(s
seeo_mic+1,_ctl+1PRA_EXTGE_AUX2_Rs;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++],
nff		0k1->card_>reabil>
ies->ac97_(kco ? "Ayx20PlaybacktVe, "G" : "Ayx0PlaybacktVe, "G",
nff		ctlPRxs;ructln+d_2et	x1fAyx20Cre RighVe, "Gt*/ruA_ADD_VOLUMTEGE(>re Rig,_ctlPRA_EXTGE_AUX2_Ls;ruA_ADD_VOLUMTEGE(>re Rig+1,_ctl+1PRA_EXTGE_AUX2_Rs;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++],
nff		0k1->card_>reabil>
ies->ac97_(kco ? "Ayx20Cre RighVe, "G" : "Ayx0Cre RighVe, "G",
nff		ctlPRxs;ructln+d_2et	i(x1fS
seeo MixtFro;
	PlaybacktVe, "GC*/ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback), A_GPR(playback), A_GPR(ctl), A_GPR(s
seeo_mic)s;ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+1), A_GPR(playback+1), A_GPR(ctl+1), A_GPR(s
seeo_mic+1)s;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], "Fro;
	PlaybacktVe, "G",_ctlPR10xs;ructln+d_2et	i(x1fS
seeo MixtSurr;
	d	Playbackt*/ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+2), A_GPR(playback+2), A_GPR(ctl), A_GPR(s
seeo_mic)s;ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+3), A_GPR(playback+3), A_GPR(ctl+1), A_GPR(s
seeo_mic+1)s;ruemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], "Surr;
	d	PlaybacktVe, "G",_ctlPRxs;ructln+d_2eti(x1fS
seeo MixtCr_use	Playbackt*/rux1fCr_use	= sub	= Left/2h+_Right/2h*/ruA_OP(i*odc, &ptr, iGER;RPPRA_GPR(tmp), A_GPR(s
seeo_mic), 00cd00A_GPR(s
seeo_mic+1)s;ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+4), A_GPR(playback+4), A_GPR(ctl), A_GPR(tmp)tat	emu10k1_fx80.h>
_mono_*ol_chiu&*ol_chis[n=
	++], "Cr_use	PlaybacktVe, "G",_ctlPRxs;ructl++
si(x1fS
seeo MixtLFE	Playbackt*/ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+5), A_GPR(playback+5), A_GPR(ctl), A_GPR(tmp)tat	emu10k1_fx80.h>
_mono_*ol_chiu&*ol_chis[n=
	++], "LFE	PlaybacktVe, "G",_ctlPRxs;ructl++
s	i(ctl-0k1->card_>reabil>
ies->spk71y x00	x1fS
seeo MixtSidc Playbacktdeff	A_OP(i*odc, &ptr, iMACx00A_GPR(playback+6), A_GPR(playback+6), A_GPR(ctl), A_GPR(s
seeo_mic)s;ru	A_OP(i*odc, &ptr, iMACx00A_GPR(playback+7), A_GPR(playback+7), A_GPR(ctl+1), A_GPR(s
seeo_mic+1)s;ruuemu10k1_fx80.h>
_s
seeo_*ol_chiu&*ol_chis[n=
	++], "Sidc PlaybacktVe, "G",_ctlPRxs;ruuctln+d_2et	}ti(x1inC*	outputsinC*/rine EMU1A_PUT_OUTPUT(nes0src) A_OP(i*odc, &ptr, iACC3, A_EXTOUT(ness, A_C_0000, 0x00A_C_0000, 0x00A_GPR(src))rine EMU1A_PUT_SR;REO_OUTPUT(nes1,nes20src) \
	{A_PUT_OUTPUT(nes10src);1A_PUT_OUTPUT(nes20src+1);}atine EMU1_A_SWITCH(i*odc, ptr, ds
, src, sw) \
	A_OP((s*odc), ptr, iMACINT0, ds
, A_C_0000, 0x00src, sw);rine EMU1A_SWITCH(i*odc, ptr, ds
, src, sw) \
		_A_SWITCH(i*odc, ptr, A_GPR(ds
)00A_GPR(src)00A_GPR(sw))rine EMU1_A_SWITCH_NEG(i*odc, ptr, ds
, src) \
	A_OP((s*odc), ptr, iANDXOR, ds
, src, A_C_0000, 0s, A_C_0000, 0statine EMU1A_SWITCH_NEG(i*odc, ptr, ds
, src) \
		_A_SWITCH_NEG(i*odc, ptr, A_GPR(ds
)00A_GPR(src))
ti(x1inC*	 Pssor F toMU1rols
 *inC*/ruA_OP(i*odc, &ptr, iACC3, A_GPR(playbackt+ SND_0K1_CENTPLAYBACK_CHANNELS + 0), A_GPR(playbackt+ 0)00A_C_0000, 0x00A_C_0000, 0xs; /* leftC*/ruA_OP(i*odc, &ptr, iACC3, A_GPR(playbackt+ SND_0K1_CENTPLAYBACK_CHANNELS + 1), A_GPR(playback + 1), A_C_0000, 0x00A_C_0000, 0xs; /* rightC*/ruA_OP(i*odc, &ptr, iACC3, A_GPR(playbackt+ SND_0K1_CENTPLAYBACK_CHANNELS + 2), A_GPR(playback + 2), A_C_0000, 0x00A_C_0000, 0xs; /* r TL leftC*/ruA_OP(i*odc, &ptr, iACC3, A_GPR(playbackt+ SND_0K1_CENTPLAYBACK_CHANNELS + 3), A_GPR(playback + 3), A_C_0000, 0x00A_C_0000, 0xs; /* r TL rightC*/ruA_OP(i*odc, &ptr, iACC3, A_GPR(playbackt+ SND_0K1_CENTPLAYBACK_CHANNELS + 4), A_GPR(playback + 4), A_C_0000, 0x00A_C_0000, 0xs; /* cr_use	*/ruA_OP(i*odc, &ptr, iACC3, A_GPR(playbackt+ SND_0K1_CENTPLAYBACK_CHANNELS + 5), A_GPR(playback + 5), A_C_0000, 0x00A_C_0000, 0xs; /* LFE	*/ructl-0k1->card_>reabil>
ies->spk71y x00	A_OP(i*odc, &ptr, iACC3, A_GPR(playbackt+ SND_0K1_CENTPLAYBACK_CHANNELS + 6), A_GPR(playback + 6), A_C_0000, 0x00A_C_0000, 0xs; /* sidc leftC*/ru	A_OP(i*odc, &ptr, iACC3, A_GPR(playbackt+ SND_0K1_CENTPLAYBACK_CHANNELS + 7), A_GPR(playback + 7), A_C_0000, 0x00A_C_0000, 0xs; /* sidc rightC*/ru}s	i
	=
		( &*ol_chis[n=
	 + 0];ru>min id.ifacco
	V_CTL_ELEM_TYPEIFACE_MIXER;suct cpy(>min id.na"GPR"ToMU1Col *kco- Bass"s;ru>min vt;
	utl-2etu>vcout;
	und_10;00cmin = ctl-0;00cmin = ctl-40;00cmin e.int[0]tl->min;e.int[1]tl-20;00cmin transla) an1)a0K1_CENTGPR_TRANSLATION_BASS;
	=
		( &*ol_chis[n=
	 + 1];ru>min id.ifacco
	V_CTL_ELEM_TYPEIFACE_MIXER;suct cpy(>min id.na"GPR"ToMU1Col *kco- Treble"s;ru>min vt;
	utl-2etu>vcout;
	und_10;00cmin = ctl-0;00cmin = ctl-40;00cmin e.int[0]tl->min;e.int[1]tl-20;00cmin transla) an1)a0K1_CENTGPR_TRANSLATION_TREBLE;atine EMU1BASSTGPR	0x8ctine EMU1TREBLETGPR	0x96
sptest(znd_0; z <-5; z++u0x00	c	unjnsi	test(jnd_0; jn<-2; j++u x00		*ol_chis[n=
	 + 0].ctl[z co2 + j]nd_BASSTGPR + z co2 + j;00		*ol_chis[n=
	 + 1].ctl[z co2 + j]nd_TREBLETGPR + z co2 + j;00	}si}sptest(znd_0; z <-4; z++u0x0	x1ffro;
/r TL/cr_use-lfe/sidc deff	c	unj, k, l, dnsi	test(jnd_0; jn<-2; j++u x	/* left/rightC*/ru		ktl-0xb0t+ (z co8)t+ (j co4tat		u		( 0xe0t+ (z co8)t+ (j co4tat		udh=	playbackt+ SND_0K1_CENTPLAYBACK_CHANNELS + z co2 + j;0t		uA_OP(i*odc, &ptr, iMACx00A_C_0000, 0x00A_C_0000, 0x00A_GPR(d), A_GPR(BASSTGPR + 0t+ j)tat		uA_OP(i*odc, &ptr, iMACMV, A_GPR(k+1), A_GPR(k), A_GPR(k+1), A_GPR(BASSTGPR + 4t+ j)tat		uA_OP(i*odc, &ptr, iMACMV, A_GPR(k)00A_GPR(d), A_GPR(k), A_GPR(BASSTGPR + 2t+ j)tat		uA_OP(i*odc, &ptr, iMACMV, A_GPR(k+3), A_GPR(k+2), A_GPR(k+3), A_GPR(BASSTGPR + 8t+ j)tat		uA_OP(i*odc, &ptr, iMAC0, A_GPR(k+2), A_GPREACCU, A_GPR(k+2), A_GPR(BASSTGPR + 6t+ j)tat		uA_OP(i*odc, &ptr, iACC3, A_GPR(k+2), A_GPR(k+2), A_GPR(k+2), A_C_0000, 0xs;rt		uA_OP(i*odc, &ptr, iMACx00A_C_0000, 0x00A_C_0000, 0x00A_GPR(k+2), A_GPR(TREBLETGPR + 0t+ j)tat		uA_OP(i*odc, &ptr, iMACMV, A_GPR(l+1), A_GPR(l), A_GPR(l+1), A_GPR(TREBLETGPR + 4t+ j)tat		uA_OP(i*odc, &ptr, iMACMV, A_GPR(l), A_GPR(k+2), A_GPR(l), A_GPR(TREBLETGPR + 2t+ j)tat		uA_OP(i*odc, &ptr, iMACMV, A_GPR(l+3), A_GPR(l+2), A_GPR(l+3), A_GPR(TREBLETGPR + 8t+ j)tat		uA_OP(i*odc, &ptr, iMAC0, A_GPR(l+2), A_GPREACCU, A_GPR(l+2), A_GPR(TREBLETGPR + 6t+ j)tat		uA_OP(i*odc, &ptr, iMACINT0, A_GPR(l+2), A_C_0000, 0x00A_GPR(l+2), A_C_0000, 1xs;rt		uA_OP(i*odc, &ptr, iACC3, A_GPR(d), A_GPR(l+2), A_C_0000, 0x00A_C_0000, 0xs;r
i		ctl-z  &&2)	/* cr_use	*/ruuuubr Tk;00	}si}spn=
	 +d_2eti#unne _BASSTGPRi#unne _TREBLETGPR
sptest(znd_0; z <-8; z++u0x00	A_SWITCH(i*odc, &ptr, tmp + x00playbackt+ SND_0K1_CENTPLAYBACK_CHANNELS + z,-ctln+_0s;ru	A_SWITCH_NEG(i*odc, &ptr, tmp + 1,-ctln+_0s;ru	A_SWITCH(i*odc, &ptr, tmp + 1,-playbackt+ z, tmp + 1s;ru	A_OP(i*odc, &ptr, iACC3, A_GPR(playbackt+ SND_0K1_CENTPLAYBACK_CHANNELS + z), A_GPR(tmp + 0), A_GPR(tmp + 1), A_C_0000, 0x)et	}t	emu10k1_fx80.h>
_s
seeo_f_tabl*ol_chiucol_chiso+ n=
	++PR"ToMU1Col *kco- Switch",_ctlPRxs;ructln+d_2eti(x1fMas
se ve, "Gt(willibceivna"Gd la
seuC*/ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+0+SND_0K1_CENTPLAYBACK_CHANNELS)00A_C_0000, 0x00A_GPR(ctl), A_GPR(playback+0+SND_0K1_CENTPLAYBACK_CHANNELS)s;ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+1+SND_0K1_CENTPLAYBACK_CHANNELS)00A_C_0000, 0x00A_GPR(ctl), A_GPR(playback+1+SND_0K1_CENTPLAYBACK_CHANNELS)s;ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+2+SND_0K1_CENTPLAYBACK_CHANNELS)00A_C_0000, 0x00A_GPR(ctl), A_GPR(playback+2+SND_0K1_CENTPLAYBACK_CHANNELS)s;ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+3+SND_0K1_CENTPLAYBACK_CHANNELS)00A_C_0000, 0x00A_GPR(ctl), A_GPR(playback+3+SND_0K1_CENTPLAYBACK_CHANNELS)s;ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+4+SND_0K1_CENTPLAYBACK_CHANNELS)00A_C_0000, 0x00A_GPR(ctl), A_GPR(playback+4+SND_0K1_CENTPLAYBACK_CHANNELS)s;ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+5+SND_0K1_CENTPLAYBACK_CHANNELS)00A_C_0000, 0x00A_GPR(ctl), A_GPR(playback+5+SND_0K1_CENTPLAYBACK_CHANNELS)s;ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+6+SND_0K1_CENTPLAYBACK_CHANNELS)00A_C_0000, 0x00A_GPR(ctl), A_GPR(playback+6+SND_0K1_CENTPLAYBACK_CHANNELS)s;ruA_OP(i*odc, &ptr, iMACx00A_GPR(playback+7+SND_0K1_CENTPLAYBACK_CHANNELS)00A_C_0000, 0x00A_GPR(ctl), A_GPR(playback+7+SND_0K1_CENTPLAYBACK_CHANNELS)tat	emu10k1_fx80.h>
_mono_*ol_chiu&*ol_chis[n=
	++], "Wave-Mas
se PlaybacktVe, "G",_ctlPRxs;ructln+d_2eti(x1fanalog sp Tkseso*/ruA_PUT_SR;REO_OUTPUT(A_EXTOUT_AFRONT_L, A_EXTOUT_AFRONT_R00playbackt+ SND_0K1_CENTPLAYBACK_CHANNELSs;ruA_PUT_SR;REO_OUTPUT(A_EXTOUT_AREAR_L, A_EXTOUT_AREAR_R00playback+2t+ SND_0K1_CENTPLAYBACK_CHANNELSs;ruA_PUT_OUTPUT(A_EXTOUT_ACENTER00playback+4t+ SND_0K1_CENTPLAYBACK_CHANNELSs;ruA_PUT_OUTPUT(A_EXTOUT_ALFE00playback+5t+ SND_0K1_CENTPLAYBACK_CHANNELSs;ructl-0k1->card_>reabil>
ies->spk71yru	A_PUT_SR;REO_OUTPUT(A_EXTOUT_ASIDE_L, A_EXTOUT_ASIDE_R00playback+6t+ SND_0K1_CENTPLAYBACK_CHANNELSs;ri(x1fheadphoneo*/ruA_PUT_SR;REO_OUTPUT(A_EXTOUT_HEADPHONE_L, A_EXTOUT_HEADPHONE_R00playbackt+ SND_0K1_CENTPLAYBACK_CHANNELSs;ri(x1fdigit;
noutputst*/rux1fA_PUT_SR;REO_OUTPUT(A_EXTOUT_FRONT_L, A_EXTOUT_FRONT_R00playbackt+ SND_0K1_CENTPLAYBACK_CHANNELSs;h*/ructl-0k1->card_>reabil>
ies->0k1_modelu0x00	x1f0K1_C10 Outputstfrom CaptFro;
, RealPRCr_use, LFE00Sidc deff	devo(str-0k1->cardivdev, "0K1-outputston\n"s;ruptest(znd_0; z <-8; z++u0x00		ctl-0k1->card_>reabil>
ies->cactl8p(kco) x00			A_OP(i*odc, &ptr, iACC3, A3_0K132OUT(z), A_GPR(playbackt+ SND_0K1_CENTPLAYBACK_CHANNELS + z), A_C_0000, 0x00A_C_0000, 0xs;ru		} 
		u x00			A_OP(i*odc, &ptr, iACC3, A_0K132OUTL(z), A_GPR(playbackt+ SND_0K1_CENTPLAYBACK_CHANNELS + z), A_C_0000, 0x00A_C_0000, 0xs;ru		}00	}si}srux1fGEC958fOptical Raw PlaybacktSwitch de 00volumap[vol++] 
	0;00volumap[vol++] 
	0x1008;00volumap[vol++] 
	0x,
};, 0x;sptest(znd_0; z <-2; z++u0x00	A_OP(i*odc, &ptr, iMACx00A_GPR(tmp + 2), A_FXBUS(FXBUS_PT_LEFT + z), A_C_0000, 0x00A_C_0000, 0xs;ru	A_OP(i*odc, &ptr, iSKIPPRA_GPR_CONDPRA_GPR_CONDPRA_GPR(ctl -s2)00A_C_0000, 01s;ru	A_OP(i*odc, &ptr, iACC3, A_GPR(tmp + 2), A_C_0000, 0x00A_C_0001, 0x00A_GPR(tmp + 2)s;ru	A_OP(i*odc, &ptr, iANDXOR, A_GPR(tmp + 2), A_GPR(tmp + 2), A_GPR(ctl -s1), A_C_0000, 0x)et		A_SWITCH(i*odc, &ptr, tmp + x00tmp + 2,-ctln+_zs;ru	A_SWITCH_NEG(i*odc, &ptr, tmp + 1,-ctln+_zs;ru	A_SWITCH(i*odc, &ptr, tmp + 1,-playbackt+ SND_0K1_CENTPLAYBACK_CHANNELS + z,-tmp + 1s;ru	ctl-(z==1) &&l-0k1->card_>reabil>
ies->spdif_bug)) x00		x1fDucohe a SPDIF0output bugtoneso"GoAy_out cards,s prog*odc delays FreeRight ch."n
	oby 1osa Pla	*/ruuudevo(str-0k1->cardivdev,
				e"Inst;
lingtspdif_bug patch: %s\n",
				e0k1->card_>reabil>
ies->na"Gs;ru		A_OP(i*odc, &ptr, iACC3, A_0XTOUT(A_EXTOUT_FRONT_L + z), A_GPR(ctl -s3), A_C_0000, 0x00A_C_0000, 0xs;t		uA_OP(i*odc, &ptr, iACC3, A_GPR(ctl -s3), A_GPR(tmp + 0), A_GPR(tmp + 1), A_C_0000, 0x)et		} 
		u x00		A_OP(i*odc, &ptr, iACC3, A_0XTOUT(A_EXTOUT_FRONT_L + z), A_GPR(tmp + 0), A_GPR(tmp + 1), A_C_0000, 0x)et		}t	}t	emu10k1_fx80.h>
_s
seeo_f_tabl*ol_chiucol_chiso+ n=
	++PRV_CTL_ELEMNAMTEGEC958("Optical Raw ",PLAYBACK,SWITCH),_ctlPRxs;ructln+d_2et	ruA_PUT_SR;REO_OUTPUT(A_EXTOUT_REAR_L, A_EXTOUT_REAR_R00playback+2t+ SND_0K1_CENTPLAYBACK_CHANNELSs;ruA_PUT_OUTPUT(A_EXTOUT_CENTER00playback+4t+ SND_0K1_CENTPLAYBACK_CHANNELSs;ruA_PUT_OUTPUT(A_EXTOUT_LFE00playback+5t+ SND_0K1_CENTPLAYBACK_CHANNELSs;rrux1fADChbuffer */
#ctne _0K1_CENTURE_DIGIDIGITAL_OUTruA_PUT_SR;REO_OUTPUT(A_EXTOUT_ADCTURE_L, A_EXTOUT_ADCTURE_R00playbackt+ SND_0K1_CENTPLAYBACK_CHANNELSs;r#
		uinA_PUT_OUTPUT(A_EXTOUT_ADCTURE_L, >re Rigs;ruA_PUT_OUTPUT(A_EXTOUT_ADCTURE_R00>re Rig+1);r#
ndif
ructl-0k1->card_>reabil>
ies->0k1_modelu0x00	ctl-0k1->card_>reabil>
ies->cactl8p(kco) x00		devo(str-0k1->cardivdev, "0K12hinputs1on\n"s;rupptest(znd_0; z <-0x10; z++u0x00			emu10k1_fx80ay_outsdsp t uiont_32s)o_2x16( i*odc, &ptr, tmp, 
nff			uuub>
_shif
se16,inff						A3_0K132GE(z),inff						A_FXBUS2(z*2) s;ru		}00	} 
		u x00		devo(str-0k1->cardivdev, "0K1-inputs1on\n"s;ruppx1fCre Righ16 (originallyo8)tch."n
		 of S32sLEeso
	d	statuppx100		devodbg-0k1->cardivdev, "0k1fx.c:_ctl=0x%x, tmp=0x%x\n",
			aaaaaaactlPRtmp);rupp*/ruuu/* FestFree0K1_C10: Howohe getn32bi2 e.intstfrom implcon. High	16bi2soinhe L, lowo16bi2soinhe Retdeff	(x1fA_P16VGE(0)1is delay coby oneosa Pla,
			a*eso all oimprfA_P16VGEtch."n
		 willinthe fo also be delay c
			a*eff	(x1fLeftfADChin. 1oof 2a*eff	(emu10k1_fx80ay_outsdsp t uiont_32s)o_2x16( i*odc, &ptr, tmp, b>
_shif
se16, A_P16VGE(0x,), A_FXBUS2(0)1s;ruppx1fRight ADChin 1oof 2a*eff	(volumap[vol++] 
	0x0000, 0x;ruppx1fDelayingtby oneosa Pla:tinctvadnof >of ingtimplinput
			a*ee;
	unA_P16VGEtfo output A_FXBUS2 asoin implfirst ch."n
	,
			a*ewe us  an gyxil>ary regis
se, delayingtimple;
	unby one
			a*esa Pla
			a*eff	(emu10k1_fx80ay_outsdsp t uiont_32s)o_2x16( i*odc, &ptr, tmp, b>
_shif
se16, A_GPR(ctl -s1), A_FXBUS2(2) s;ru		A_OP(i*odc, &ptr, iACC3, A_GPR(ctl -s1), A_P16VGE(0x1), A_C_0000, 0x00A_C_0000, 0xs;t		uvolumap[vol++] 
	0x0000, 0x;ruppemu10k1_fx80ay_outsdsp t uiont_32s)o_2x16( i*odc, &ptr, tmp, b>
_shif
se16, A_GPR(ctl -s1), A_FXBUS2(4) s;ru		A_OP(i*odc, &ptr, iACC3, A_GPR(ctl -s1), A_P16VGE(0x2), A_C_0000, 0x00A_C_0000, 0xs;r		uvolumap[vol++] 
	0x0000, 0x;ruppemu10k1_fx80ay_outsdsp t uiont_32s)o_2x16( i*odc, &ptr, tmp, b>
_shif
se16, A_GPR(ctl -s1), A_FXBUS2(6) s;ru		A_OP(i*odc, &ptr, iACC3, A_GPR(ctl -s1), A_P16VGE(0x3), A_C_0000, 0x00A_C_0000, 0xs;t		u/* Fest96kHz modc deff	(x1fLeftfADChin. 2oof 2a*eff	(volumap[vol++] 
	0x0000, 0x;ruppemu10k1_fx80ay_outsdsp t uiont_32s)o_2x16( i*odc, &ptr, tmp, b>
_shif
se16, A_GPR(ctl -s1), A_FXBUS2(0x8) s;ru		A_OP(i*odc, &ptr, iACC3, A_GPR(ctl -s1), A_P16VGE(0x4), A_C_0000, 0x00A_C_0000, 0xs;ruppx1fRight ADChin 2oof 2a*eff	(volumap[vol++] 
	0x0000, 0x;ruppemu10k1_fx80ay_outsdsp t uiont_32s)o_2x16( i*odc, &ptr, tmp, b>
_shif
se16, A_GPR(ctl -s1), A_FXBUS2(0xa) s;ru		A_OP(i*odc, &ptr, iACC3, A_GPR(ctl -s1), A_P16VGE(0x5), A_C_0000, 0x00A_C_0000, 0xs;ff	(volumap[vol++] 
	0x0000, 0x;ruppemu10k1_fx80ay_outsdsp t uiont_32s)o_2x16( i*odc, &ptr, tmp, b>
_shif
se16, A_GPR(ctl -s1), A_FXBUS2(0xc) s;ru		A_OP(i*odc, &ptr, iACC3, A_GPR(ctl -s1), A_P16VGE(0x6), A_C_0000, 0x00A_C_0000, 0xs;ff	(volumap[vol++] 
	0x0000, 0x;ruppemu10k1_fx80ay_outsdsp t uiont_32s)o_2x16( i*odc, &ptr, tmp, b>
_shif
se16, A_GPR(ctl -s1), A_FXBUS2(0xe) s;ru		A_OP(i*odc, &ptr, iACC3, A_GPR(ctl -s1), A_P16VGE(0x7), A_C_0000, 0x00A_C_0000, 0xs;ruppx1fPav
	oHofman -ewe stillihave- snceF, A_FXBUS2F, anc
			a* A_P16VGEs availabla	-
			a* let'siadd 8 moigh>re Righch."n
		 -	tot;
nof 16
			a*eff	(volumap[vol++] 
	0x0000, 0x;ruppemu10k1_fx80ay_outsdsp t uiont_32s)o_2x16(i*odc, &ptr, tmp,inff					 	b>
_shif
se16,inff					 	A_GPR(ctl -s1),inff					 	A_FXBUS2(0x1,)tat		uA_OP(i*odc, &ptr, iACC3, A_GPR(ctl -s1), A_P16VGE(0x8),
			aaaaaA_C_0000, 0x00A_C_0000, 0xs;ff	(volumap[vol++] 
	0x0000, 0x;ruppemu10k1_fx80ay_outsdsp t uiont_32s)o_2x16(i*odc, &ptr, tmp,inff					 	b>
_shif
se16,inff					 	A_GPR(ctl -s1),inff					 	A_FXBUS2(0x12)tat		uA_OP(i*odc, &ptr, iACC3, A_GPR(ctl -s1), A_P16VGE(0x9),
			aaaaaA_C_0000, 0x00A_C_0000, 0xs;ff	(volumap[vol++] 
	0x0000, 0x;ruppemu10k1_fx80ay_outsdsp t uiont_32s)o_2x16(i*odc, &ptr, tmp,inff					 	b>
_shif
se16,inff					 	A_GPR(ctl -s1),inff					 	A_FXBUS2(0x14)tat		uA_OP(i*odc, &ptr, iACC3, A_GPR(ctl -s1), A_P16VGE(0xa),
			aaaaaA_C_0000, 0x00A_C_0000, 0xs;ff	(volumap[vol++] 
	0x0000, 0x;ruppemu10k1_fx80ay_outsdsp t uiont_32s)o_2x16(i*odc, &ptr, tmp,inff					 	b>
_shif
se16,inff					 	A_GPR(ctl -s1),inff					 	A_FXBUS2(0x16)tat		uA_OP(i*odc, &ptr, iACC3, A_GPR(ctl -s1), A_P16VGE(0xb),
			aaaaaA_C_0000, 0x00A_C_0000, 0xs;ff	(volumap[vol++] 
	0x0000, 0x;ruppemu10k1_fx80ay_outsdsp t uiont_32s)o_2x16(i*odc, &ptr, tmp,inff					 	b>
_shif
se16,inff					 	A_GPR(ctl -s1),inff					 	A_FXBUS2(0x18)tat		uA_OP(i*odc, &ptr, iACC3, A_GPR(ctl -s1), A_P16VGE(0xc),
			aaaaaA_C_0000, 0x00A_C_0000, 0xs;ff	(volumap[vol++] 
	0x0000, 0x;ruppemu10k1_fx80ay_outsdsp t uiont_32s)o_2x16(i*odc, &ptr, tmp,inff					 	b>
_shif
se16,inff					 	A_GPR(ctl -s1),inff					 	A_FXBUS2(0x1a)tat		uA_OP(i*odc, &ptr, iACC3, A_GPR(ctl -s1), A_P16VGE(0xd),
			aaaaaA_C_0000, 0x00A_C_0000, 0xs;ff	(volumap[vol++] 
	0x0000, 0x;ruppemu10k1_fx80ay_outsdsp t uiont_32s)o_2x16(i*odc, &ptr, tmp,inff					 	b>
_shif
se16,inff					 	A_GPR(ctl -s1),inff					 	A_FXBUS2(0x1c)tat		uA_OP(i*odc, &ptr, iACC3, A_GPR(ctl -s1), A_P16VGE(0xe),
			aaaaaA_C_0000, 0x00A_C_0000, 0xs;ff	(volumap[vol++] 
	0x0000, 0x;ruppemu10k1_fx80ay_outsdsp t uiont_32s)o_2x16(i*odc, &ptr, tmp,inff					 	b>
_shif
se16,inff					 	A_GPR(ctl -s1),inff					 	A_FXBUS2(0x1e)tat		uA_OP(i*odc, &ptr, iACC3, A_GPR(ctl -s1), A_P16VGE(0xf),
			aaaaaA_C_0000, 0x00A_C_0000, 0xs;ff	}i
#ctl0
pptest(znd_4; z <-8; z++u0x00		A_OP(i*odc, &ptr, iACC3, A_FXBUS2(zs, A_C_0000, 0x00A_C_0000, 0x00A_C_0000, 0x)et		}t	ptest(znd_0xc; z <-0x10; z++u0x00		A_OP(i*odc, &ptr, iACC3, A_FXBUS2(zs, A_C_0000, 0x00A_C_0000, 0x00A_C_0000, 0x)et		}t#
ndif
u} 
		u x00	/* EFXh>re Righ-h>re Righimpl16 EXTGEst*/rupx1fCre Righ16 ch."n
		 of S16sLEeso
	d	stapptest(znd_0; z <-16; z++u0x00		A_OP(i*odc, &ptr, iACC3, A_FXBUS2(zs, A_C_0000, 0x00A_C_0000, 0x00A_EXTGE(z))tru	}si}s	i#
ndif /* JCD testt*/rux1inC*	ok, set up done..inC*/rructl-ctl >Rtmp)0x00	emu1BUG()tru	errnd_-EIOtru	goto __err;t	}t	x1fcl TL rem*/
ingtinct snd an memoiyt*/ruwhila	(ptr <-0x400yru	A_OP(i*odc, &ptr, 0x0f, 00c0, 00c0, 00cf, 00c0s;rruseg = emu10_use_(voi()truc*odcn voluadd *ol_chi_t;
	utl-n=
	;ruc*odcn voluadd *ol_chiso
	uct snd_emu10k1_fx8010_ctl *ol_chipctln__(voi *)*ol_chis;ru0k1->suppont_tlv = 1; /* suppont TLVtdeff0rrnd_emu10k1_gpr_c*odcupok (= s, c*odc);ru0k1->suppont_tlv = 0; /* cl TL ag*/
sdeffemu1l Tve_(voi(segs;rr__err:
	k sofucol_chiss;r__err_ctr		:
	k sofu( sndf__tesccon)i*odcn volumaps;r__err_vol:
	k sofuc*odc);rurn 0;
}err;i}att*   co.h>
ial controlfigura) an1festEk1_gpr

stat/*0whhe ve, "Gt= max, thhe >of oonly fo a sndfve, "Gtmodifica) an1sta/*0with iMACx (nega) vple;
	us)static cons sndf_ve, "Guct snd_emu10k1_fx8010_ctl *odconi*odc, gnof*ptr, gnofds
, gnofsrc, gnofvol)struOP(i*odc, ptr, iMAC0, ds
, C_0000, 0x00src, vol);ruOP(i*odc, ptr, iANDXOR, C_0000, 0x00vol, C_ffff,
};
 C_ffff,
};);ruOP(i*odc, ptr, iSKIPPRGPR_CONDPRGPR_CONDPRCC_REG_NONZERO, C_0000, 01);ruOP(i*odc, ptr, iACC3, ds
, src, C_0000, 0x00C_0000, 0x)et}tic cons sndf_ve, "Guadduct snd_emu10k1_fx8010_ctl *odconi*odc, gnof*ptr, gnofds
, gnofsrc, gnofvol)struOP(i*odc, ptr, iANDXOR, C_0000, 0x00vol, C_ffff,
};
 C_ffff,
};);ruOP(i*odc, ptr, iSKIPPRGPR_CONDPRGPR_CONDPRCC_REG_NONZERO, C_0000, 02);ruOP(i*odc, ptr, iMACINT0, ds
, ds
, src, C_0000, 01);ruOP(i*odc, ptr, iSKIPPRC_0000, 0x00C_7fff,
};
 C_ffff,
};, C_0000, 01);ruOP(i*odc, ptr, iMAC0, ds
, ds
, src, vol);r}tic cons sndf_ve, "Guoutuct snd_emu10k1_fx8010_ctl *odconi*odc, gnof*ptr, gnofds
, gnofsrc, gnofvol)struOP(i*odc, ptr, iANDXOR, C_0000, 0x00vol, C_ffff,
};
 C_ffff,
};);ruOP(i*odc, ptr, iSKIPPRGPR_CONDPRGPR_CONDPRCC_REG_NONZERO, C_0000, 02);ruOP(i*odc, ptr, iACC3, ds
, src, C_0000, 0x00C_0000, 0x)etuOP(i*odc, ptr, iSKIPPRC_0000, 0x00C_7fff,
};
 C_ffff,
};, C_0000, 01);ruOP(i*odc, ptr, iMAC0, ds
, C_0000, 0x00src, vol);r}atine EMU1VOLUMT(i*odc, ptr, ds
, src, vol) \
		_ve, "Gui*odc, ptr, GPR(ds
)00GPR(src)00GPR(vol))tine EMU1VOLUMTEGE(i*odc, ptr, ds
, src, vol) \
		_ve, "Gui*odc, ptr, GPR(ds
)00EXTGE(src)00GPR(vol))tine EMU1VOLUMTEADD(i*odc, ptr, ds
, src, vol) \
		_ve, "Guaddui*odc, ptr, GPR(ds
)00GPR(src)00GPR(vol))tine EMU1VOLUMTEADDGE(i*odc, ptr, ds
, src, vol) \
		_ve, "Guaddui*odc, ptr, GPR(ds
)00EXTGE(src)00GPR(vol))tine EMU1VOLUMTEOUT(i*odc, ptr, ds
, src, vol) \
		_ve, "Guoutui*odc, ptr, 0XTOUT(ds
)00GPR(src)00GPR(vol))tine EMU1_SWITCH(i*odc, ptr, ds
, src, sw) \
	OP((s*odc), ptr, iMACINT0, ds
, C_0000, 0x00src, sw);rine EMU1SWITCH(i*odc, ptr, ds
, src, sw) \
		_SWITCH(i*odc, ptr, GPR(ds
)00GPR(src)00GPR(sw))rine EMU1SWITCH_GE(i*odc, ptr, ds
, src, sw) \
		_SWITCH(i*odc, ptr, GPR(ds
)00EXTGE(src)00GPR(sw))rine EMU1_SWITCH_NEG(i*odc, ptr, ds
, src) \
	OP((s*odc), ptr, iANDXOR, ds
, src, C_0000, 0s, C_0000, 0statine EMU1SWITCH_NEG(i*odc, ptr, ds
, src) \
		_SWITCH_NEG(i*odc, ptr, GPR(ds
)00GPR(src))
tiic int snd__emu10k1_gpr_.h>
_efxuct snd_emu10k1_fx8u = s	strusnd_err,	
, z,_ctlPRtmp, playback00>re Rig;rugnofptr;suct snd_emu10k1 *em010_ctl *odconi*odc;suct snd_emu10k1 *em010_ctl pcm_reconipcm 
	,
};;in_t snd_emu10k1_fx8010_ctl *ol_chipctln*col_chiso
	,
};,l =
		spgnof*volumap;_segment_t fs =ent;rru0rrnd_-ENOMEM;
	i*odc 
	kzc.h>
(sizeof(*s*odc), GFP_KERNELstructl-!i*o