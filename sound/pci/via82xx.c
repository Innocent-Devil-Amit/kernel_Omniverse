/*
 *   ALSA driver for VIA VT82xx (South Bridge)
 *
 *   VT82C686A/B/C, VT8233A/C, VT8235
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@perex.cz>
 *	                   Tjeerd.Mulder <Tjeerd.Mulder@fujitsu-siemens.com>
 *                    2002 Takashi Iwai <tiwai@suse.de>
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

/*
 * Changes:
 *
 * Dec. 19, 2002	Takashi Iwai <tiwai@suse.de>
 *	- use the DSX channels for the first pcm playback.
 *	  (on VIA8233, 8233C and 8235 only)
 *	  this will allow you play simultaneously up to 4 streams.
 *	  multi-channel playback is assigned to the second device
 *	  on these chips.
 *	- support the secondary capture (on VIA8233/C,8235)
 *	- SPDIF support
 *	  the DSX3 channel can be used for SPDIF output.
 *	  on VIA8233A, this channel is assigned to the second pcm
 *	  playback.
 *	  the card config of alsa-lib will assign the correct
 *	  device for applications.
 *	- clean up the code, separate low-level initialization
 *	  routines for each chipset.
 *
 * Sep. 26, 2005	Karsten Wiese <annabellesgarden@yahoo.de>
 *	- Optimize position calculation for the 823x chips. 
 */

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/gameport.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/tlv.h>
#include <sound/ac97_codec.h>
#include <sound/mpu401.h>
#include <sound/initval.h>

#if 0
#define POINTER_DEBUG
#endif

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("VIA VT82xx audio");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{VIA,VT82C686A/B/C,pci},{VIA,VT8233A/C,8235}}");

#if defined(CONFIG_GAMEPORT) || (defined(MODULE) && defined(CONFIG_GAMEPORT_MODULE))
#define SUPPORT_JOYSTICK 1
#endif

static int index = SNDRV_DEFAULT_IDX1;	/* Index 0-MAX */
static char *id = SNDRV_DEFAULT_STR1;	/* ID for this card */
static long mpu_port;
#ifdef SUPPORT_JOYSTICK
static bool joystick;
#endif
static int ac97_clock = 48000;
static char *ac97_quirk;
static int dxs_support;
static int dxs_init_volume = 31;
static int nodelay;

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for VIA 82xx bridge.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for VIA 82xx bridge.");
module_param(mpu_port, long, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 port. (VT82C686x only)");
#ifdef SUPPORT_JOYSTICK
module_param(joystick, bool, 0444);
MODULE_PARM_DESC(joystick, "Enable joystick. (VT82C686x only)");
#endif
module_param(ac97_clock, int, 0444);
MODULE_PARM_DESC(ac97_clock, "AC'97 codec clock (default 48000Hz).");
module_param(ac97_quirk, charp, 0444);
MODULE_PARM_DESC(ac97_quirk, "AC'97 workaround for strange hardware.");
module_param(dxs_support, int, 0444);
MODULE_PARM_DESC(dxs_support, "Support for DXS channels (0 = auto, 1 = enable, 2 = disable, 3 = 48k only, 4 = no VRA, 5 = enable any sample rate)");
module_param(dxs_init_volume, int, 0644);
MODULE_PARM_DESC(dxs_init_volume, "initial DXS volume (0-31)");
module_param(nodelay, int, 0444);
MODULE_PARM_DESC(nodelay, "Disable 500ms init delay");

/* just for backward compatibility */
static bool enable;
module_param(enable, bool, 0444);


/* revision numbers for via686 */
#define VIA_REV_686_A		0x10
#define VIA_REV_686_B		0x11
#define VIA_REV_686_C		0x12
#define VIA_REV_686_D		0x13
#define VIA_REV_686_E		0x14
#define VIA_REV_686_H		0x20

/* revision numbers for via8233 */
#define VIA_REV_PRE_8233	0x10	/* not in market */
#define VIA_REV_8233C		0x20	/* 2 rec, 4 pb, 1 multi-pb */
#define VIA_REV_8233		0x30	/* 2 rec, 4 pb, 1 multi-pb, spdif */
#define VIA_REV_8233A		0x40	/* 1 rec, 1 multi-pb, spdf */
#define VIA_REV_8235		0x50	/* 2 rec, 4 pb, 1 multi-pb, spdif */
#define VIA_REV_8237		0x60
#define VIA_REV_8251		0x70

/*
 *  Direct registers
 */

#define VIAREG(via, x) ((via)->port + VIA_REG_##x)
#define VIADEV_REG(viadev, x) ((viadev)->port + VIA_REG_##x)

/* common offsets */
#define VIA_REG_OFFSET_STATUS		0x00	/* byte - channel status */
#define   VIA_REG_STAT_ACTIVE		0x80	/* RO */
#define   VIA8233_SHADOW_STAT_ACTIVE	0x08	/* RO */
#define   VIA_REG_STAT_PAUSED		0x40	/* RO */
#define   VIA_REG_STAT_TRIGGER_QUEUED	0x08	/* RO */
#define   VIA_REG_STAT_STOPPED		0x04	/* RWC */
#define   VIA_REG_STAT_EOL		0x02	/* RWC */
#define   VIA_REG_STAT_FLAG		0x01	/* RWC */
#define VIA_REG_OFFSET_CONTROL		0x01	/* byte - channel control */
#define   VIA_REG_CTRL_START		0x80	/* WO */
#define   VIA_REG_CTRL_TERMINATE	0x40	/* WO */
#define   VIA_REG_CTRL_AUTOSTART	0x20
#define   VIA_REG_CTRL_PAUSE		0x08	/* RW */
#define   VIA_REG_CTRL_INT_STOP		0x04		
#define   VIA_REG_CTRL_INT_EOL		0x02
#define   VIA_REG_CTRL_INT_FLAG		0x01
#define   VIA_REG_CTRL_RESET		0x01	/* RW - probably reset? undocumented */
#define   VIA_REG_CTRL_INT (VIA_REG_CTRL_INT_FLAG | VIA_REG_CTRL_INT_EOL | VIA_REG_CTRL_AUTOSTART)
#define VIA_REG_OFFSET_TYPE		0x02	/* byte - channel type (686 only) */
#define   VIA_REG_TYPE_AUTOSTART	0x80	/* RW - autostart at EOL */
#define   VIA_REG_TYPE_16BIT		0x20	/* RW */
#define   VIA_REG_TYPE_STEREO		0x10	/* RW */
#define   VIA_REG_TYPE_INT_LLINE	0x00
#define   VIA_REG_TYPE_INT_LSAMPLE	0x04
#define   VIA_REG_TYPE_INT_LESSONE	0x08
#define   VIA_REG_TYPE_INT_MASK		0x0c
#define   VIA_REG_TYPE_INT_EOL		0x02
#define   VIA_REG_TYPE_INT_FLAG		0x01
#define VIA_REG_OFFSET_TABLE_PTR	0x04	/* dword - channel table pointer */
#define VIA_REG_OFFSET_CURR_PTR		0x04	/* dword - channel current pointer */
#define VIA_REG_OFFSET_STOP_IDX		0x08	/* dword - stop index, channel type, sample rate */
#define   VIA8233_REG_TYPE_16BIT	0x00200000	/* RW */
#define   VIA8233_REG_TYPE_STEREO	0x00100000	/* RW */
#define VIA_REG_OFFSET_CURR_COUNT	0x0c	/* dword - channel current count (24 bit) */
#define VIA_REG_OFFSET_CURR_INDEX	0x0f	/* byte - channel current index (for via8233 only) */

#define DEFINE_VIA_REGSET(name,val) \
enum {\
	VIA_REG_##name##_STATUS		= (val),\
	VIA_REG_##name##_CONTROL	= (val) + 0x01,\
	VIA_REG_##name##_TYPE			= (val) + 0xlong, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-4_TYPE_;
MODULE_PARM2DESC(mpu_port, "MPU-4__OFFSET_C;
MODULE_PARM4YPE			= (val) + 0xlongEG_OFFSE;
MODULE_PARM4YPE			= (val) + 0xlon/
#define;
MODULE_PARM8YPE			= (val) + 0xlongEG_OIA_REG;
MODULE_PARMcYPE}e - c	- supporb
MODU##_ (val),\
	VIA_REG_PLAYBACK,PARM00
# (val),\
	VIA_REG_CAPTURE,PAR100
# (val),\
	VIA_REG_FM,PAR20ule_part, intnt index (for via82AC9#dex20	/* RWCURR_INT_LSAMPLE	0x04
#defiAC9#OIADECefiG_TYPE(3<<30)_LSAMPLE	0x04
#defiAC9#OIADECefiGSHIFT	3INT_LESSONE	0x08
#deAC9#OIADECefiGPRIMARYYPE_INT_LESSONE	0x08
#deAC9#OIADECefiGSEg, DARY ndocumented */
#define AC9#OSEg, DARY_VALID	(1<<27)_LSAMPLE	0x04
#defiAC9#OPRIMARY_VALID	(1<<25)_LSAMPLE	0x04
#defiAC9#OBUSY		(1<<24)_LSAMPLE	0x04
#defiAC9#OREAD		(1<<23)_LSAMPLE	0x04
#defiAC9#OIMiGSHIFT	16_LSAMPLE	0x04
#defiAC9#OIMiG_TYPE_IN7e_LSAMPLE	0x04
#defiAC9#ODATAGSHIFT	INT_LESSONE	0x08
#deAC9#ODATAG_TYPE0xffff
 index (for via82SGD0x40	/*ex20	4* RWCURR_INT_ RW
#define VIA_REV_6 for via82SGD0xx08	/Bnel ta(1<<0)_LSAMPLE	0x04
#defiSGD0xx08	CPnel ta(1<<1)_LSAMPLE	0x04
#defiSGD0xx08	FMnel ta(1<<2)_LSAMPLE	0x04
#defiSGD0xx08	/BnFFSE(1<<4)_LSAMPLE	0x04
#defiSGD0xx08	CPnFFSE(1<<5)_LSAMPLE	0x04
#defiSGD0xx08	FMnFFSE(1<<6)_LSAMPLE	0x04
#defiSGD0xx08	/BnEG_CT(1<<8)_LSAMPLE	0x04
#defiSGD0xx08	CPnEG_CT(1<<9)_LSAMPLE	0x04
#defiSGD0xx08	FMnEG_CT(1<<10)_LSAMPLE	0x04
#defiSGD0xx08	/Bn
#defin(1<<12)_LSAMPLE	0x04
#defiSGD0xx08	CPn
#defin(1<<13)_LSAMPLE	0x04
#defiSGD0xx08	FMn
#defin(1<<14)_ RW
#d VIA_REV_8233C		SET_CURR_COUNTSGD0xx08	Fl ta(1<<0)_LSAMPLE	0x04
URR_COUNTSGD0xx08	FFSE(1<<1)_LSAMPLE	0x04
URR_COUNTSGD0xx08	EG_CT(1<<2)_LSAMPLE	0x04
URR_COUNTSGD0xx08	
#defin(1<<3nly) */
#defiURR_ChanRG_TYP(EGSEA_RET_CURR_COUNTSGD0xx08	Fl t|04
URR_COUNTSGD0xx08	FFS) <<_REEGSEA_* 4))_LSAMPLE	0x04
URR_COUNTSGD0NU G_Sine0_LSAMPLE	0x04
URR_COUNTSGD0NU G_MULTI	4_LSAMPLE	0x04
URR_COUNTSGD0NU G_REC	6_LSAMPLE	0x04
URR_COUNTSGD0NU G_REC1	7
 index (for via82GPIACTIVE		0x888 index (for via82GPIAhanR	0x88ce_parhese chips.
 * 4 snel can 
#define 
#define VIA_REV (val),\
	VIA_REG_MULTPLAY,PAR400
# (val),\
	VIA_REG_CAPTURE_eouslyfineule_parine VIA-specific 
#define 
nt index (for via8233S_PLAYBACK_VOLUME_L_AUTOSTART	0x8nt index (for via8233S_PLAYBACK_VOLUME__CURR3START	0x8nt index (for via8233S_MULTPLAY_FORMAT_AUTOSTART	0x80	#demat* 4 snVRA, 5 =e VIA_REV_6 for via82MULTPLAY_FMT_8000	/* RVIA_REV_6 for via82MULTPLAY_FMT_00000	/*8RVIA_REV_6 for via82MULTPLAY_FMT_CHG_TYPE0x7/* RW#snVRA, 5 =<<_4
MODUort;
1,2,4,6rent index (for via8233S_CAPTURE_FIFO_AUTOSTART	0x80	cur 6t;
fifo 44);
MODRL_INT_EOL | VIA_REG_CAPTURE_FIFO_EN_OFFA_REG
 index (for vDXS_MAX_VOLUME		3INT (max.ESC(nodelattTROahe hopof 
#dyfi32/IA_REV index (for via82CAPTURE_NU GNESET_T63START	0x80	input sel##x)RL_INT_EOL | VIA_REG_CAPTURE_NU GNES_MICA_RE_INT_EOL | VIA_REG_CAPTURE_NU GNES_EG_TYP_INT_EOL | VIA_REG_CAPTURE_SELECTOIADECCURR3STARrecRR_ODULsource, 0444) enabprimarame##_STATUS		=VIA_TBL_000	Fl ta_REGGGGGGGSTATUS		=VIA_TBL_000	FFSET_T8GGGGGGGS - c	ci spacx8nt index (for vACEG_KACTIV*/
#de_INT_EOL |or vACEG_KAC11OREADYEG_CTRL_INT_STOor vACEG_KAC10OREADYEG_1TRL_INT_STOor vACEG_KAC01OREADYEG_04 - c DSX3 chann0444)ready)RL_INT_EOL |or vACEG_KALOWPOWE_CURR2 - cp. 2power VIA8e)RL_INT_EOL |or vACEG_KAC00OREADYEG_01 - cprimarann0444)ready)RL_INT_EOL or vACEG_KACTRSET_T41_INT_EOL |or vACEG_KACl tyEN_OFFA_R80 - c0:;
module_p1:44);
MODRL_INT_EOL |or vACEG_KACl ty_REG_C
#de - c0:;asse 3 =1:4de-asse 3DRL_INT_EOL |or vACEG_KACl tySYNCEG_CT - c0:;rel#ase SYNC =1:4#dece SYNC hiDRL_INT_EOL |or vACEG_KACl tySDOEG_1T - c0:;rel#ase SDO =1:4#dece SDO hiDRL_INT_EOL |or vACEG_KACl tyVRA   VI - c0:;
module, int,1:44);
MOD inDRL_INT_EOL |or vACEG_KACl tyPCMEG_04 - c0:;
module,PCMt,1:44);
MODPCMDRL_INT_EOL |or vACEG_KACl tyFMCURR2 - c
#defin##naDRL_INT_EOL |or vACEG_KACl tySBEG_01 - c
#defin##naDRL_INT_EOL |or vACEG_KACl tyIN00	(or vACEG_KACl tyEN_OFF|PE				|or vACEG_KACl ty_REG_|PE				|or vACEG_KACl tyPCM|PE				|or vACEG_KACl ty innly) */
#definFUNCyEN_OFFAC
#d2_INT_EOL |or vFUNCyMIDI_PNPA_R80 - cFIXME: it'sPAR40Y; withodatasheet!DRL_INT_EOL |or vFUNCyMIDI_IRQ_TYPE0x40 - cFIXME: lti- VIA_REG_C!DRL_INT_EOL |or vFUNCyRX2C_WRIVIA_RCTRL_INT_STOor vFUNCySB_FIFO_EMPTYEG_1TRL_INT_STOor vFUNCyEN_OFFindexE_INT_EOL		0x02or vFUNCyEN_OFFiFMCURR4_EOL		0x02or vFUNCyEN_OFFiMIDIT_TABLE_PTR	0x0or vFUNCyEN_OFFiSBEG_01ly) */
#definPNPL_START		0x84ision numbers FMnNMIACTRSET_T48 index (for URR_CVOLCHnnel tT_T48 index (for URR_Cecondnel tT_T49LE_PTR	0x0or URR_CecondnDX3E_INT_EOL		0x02or URR_CecondnSLOTG_TYPE0x03_EOL		0x02or URR_CecondnSLOTG1011	/* RVIA_REV_6 or URR_CecondnSLOTG34 undocumented */or URR_CecondnSLOTG78ET_TABLE_PTR	0x0or URR_CecondnSLOTG69	E0x03_ + VIA/
 index (for vDXS__16B	0 index (for vDXS_EN_OFFA1 index (for vDXS_DIS_OFFA2 index (for vDXS_48K	3 index (for vDXS_NOyVRA 4 index (for vDXS_SRC	5
t + VIA_ll avice
 VIA/
 vicu#x)snd_
#d_sg_ chann{
	unlsa-libdex,status;
	unlsa-libdex,size;
} ;_STATUS		=VIA_T_OFFiSIZFA255STATUS		=VIA_MAX_BUFSIZFA(1<<24)_ vicu#x)G_OFFSn{
	unlsa-libdex,
#d_status;
	unlsa-libif
ste_para	, intG_##xy la	- c	- suppor= 0,snel can ;
1IA/
 free sovicu#x)snd_ludesubvice
  *subvice
 ra	, inrunnODU;
	unlsa-libdex,tbl_REGries;  RW#sdescriptoe 
nt 	vicu#x)snd_dma_buff   t via68	vicu#x)snd_
#d_sg_ chann*idx_t via68	 RW#defrecRveranfromwithounexpecG_CTindex, chan	unlsa-libdex,lastpos;
	unlsa-libdex,fragsize;
	unlsa-libdex,bufsize;
	unlsa-libdex,bufsize2ra	, inhwptr_done;		- c	rocesto thram.h>
#includ; withobuff   han	 = SND_ude <linura	, inshadow_shifura}
#deTROL	= 	0x0cCARD,\
	efin;
1, 	0x0cCARD,\
	 VIA_}
#TROL	= 	0x0c\
	efi, 	0x0c\
	eously	0x0c\
	eousA_}
#STATUS		=VIA_MAX_DEVS	7	- c4c	- suppo_8251		0xte)"nel can A/
 vicu#x)
#d_ine _
MODU{
	spin
MOD_x,lo;
st	, inrne st	, in to ra}
#dvicu#x)
#dESC(m{n	 = SNrq
#d	unlsa-libif
ste_para	vicu#x)resource,*ICK
resst	, ine <l_/* R;
	unlsa-libume =not in m
#d	unlsa-libume =old_legacy;
	unlsa-libume =old_legacy_cfgULE_PARM_c int iPMnSLEEP
	unlsa-libume =legacy_savo ra	unlsa-libume =legacy_cfg_savo ra	unlsa-libume =*
 * _ctrl_savo ra	unlsa-libume =nel can_src_savo [2];
	unlsa-libdex,ICK
modu_savo rastatic c	unlsa-libume =	- suppo 0444);[4][2];  RW#defVed for S/237	;if

ARM_D= 0chan	unlsa-libume =	- suppo 0444);_c[2];  RW#defVed for S/237	;if

ARM_D= 0chan
	unlsa-libdex,dexr_mask;  RWSGD0x40	/* maskb wicheportde <linuschan
	vicu#x)	ci_FFSn*	ci68	vicu#x)snd_ joyst joy;n
	unlsa-libdex,ROL_FFSs;
	unlsa-libdex,	- suppo FFSno,51		0x FFSno,5nel can_FFSno68	vicu#x)G_OFFSnFFSs[VIA_MAX_DEVS]68	vicu#x)G_O_ine _
MODUine s[2];  RW	- suppor 4 snel can han	unlsa-libdex,ay, fixed: YSTICK= no VRA, 5 accenusc##naD48kHz han	unlsa-libdex,no_vra: YST1 mult nelib witusD inDlud= no VRA, 5 =han	unlsa-libdex,ay, src: YSTICK, 82full SRCsnela
/* riespof = nohan	unlsa-libdex,*
 * _on: YSTICK##naD*
 *  ine st, 04b wiexe <nPARMACschan
	vicu#x)snd_ludn*	cms[2];
	vicu#x)snd_rawmidiDRrmidi;
	vicu#x)snd_kVIA_REG_Cay, VIA_REGs[4];n
	vicu#x)snd_am(dxb233_am(dxb23;
	vicu#x)snd_am(d3_am(d;
	unlsa-libdex,(ac97_quir;
	unlsa-libdex,(ac97 DSX3 chaSTICK DSX3 chancharp, 0444)nc., esal) han
	vpin
MOD_x,
#d_quir;
	vicu#x)snd_lude_REGraDR	roc_REGraNFIG_GdRM_DESC(joystick, "E	vicu#x)clude <s *clude <srastatic }
#dvit_voluonst vicu#x)	ci_FFSice_id)snd_
#dESC(_ids[]D= {8	 RWine 06lyfi3058ohan	{DPCI_Vfined(CVIA,DPCI_fined(efiGVIA_LE_PAR_5), 	0x0cCARD,\
	efi, },	 RWRT) ohan	 RWine 06lyfi3059ohan	{DPCI_Vfined(CVIA,DPCI_fined(efiGVIA_LER_C5), 	0x0cCARD,\
	eously},	 RWULE) &ohan	{D0,s} }
#dDULE_SUPPned(eT_OFF(	ci,)snd_
#dESC(_idsule_pa.de>
 *	- us placA8e) 4 s5	Karsten8233C descriptoeobuff  sVIA_leriodsolumket *pof leriodsVIA_fragsizenabperiod,sized; wT	0x VIADEDESC(index,build_
#d_t via(vicu#x)G_OFFSn*ine Vvicu#x)snd_ludesubvice
  *subvice
 ,E			 sovicu#x)	ci_FFSn*	ci,E			 sounlsa-libdex,	eriods, unlsa-libdex,fragsize)
{
	unlsa-libdex,i,)idx,pofs,  est68	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule_	*  (FFS->t via.ace
 == NULL) {8		 RWll ase   Vof @yaholisuscmm(enbe) lsa-lib wi8wT	0x ,E		IA_   Mll ak <n
 *	ag st optmuahobigger Vvo wC don'tl joeE		IAan		*  (snd_dma_ plac_	ag s(#ifdef MAT	0x0cPPn,)snd_dma_	ci_Fata(e <l->	ci),E					PAGE_ALIGN(VIA_T_OFFiSIZF efin* 8),E					&FFS->t via) < 0)E			re can -ENOMEM68	}_	*  (! FFS->idx_t via) {8		FFS->idx_t vianabkm plac(sizeof(*FFS->idx_t via) *=VIA_T_OFFiSIZF, GFP_KERNELule		*  (! FFS->idx_t via)E			re can -ENOMEM68	}_8	 RW#p thll aREGries han	 d_DEF068	ofsDEF068	#def(iDEF06 i < 	eriods6 i++) {8		 estt;
fragsize;
		 RW#p thdescriptoe 
ializ 	eriod.E		IA_z 	eriodassignedsp* rb wituvo thedescriptoe 
*  it'sE		IA_Rver*	ag  b07 USry.E		IAan		do {8			unlsa-libdex,
;8			unlsa-libdex,flag;8			unlsa-libdex,addrle_			*  ( d_D>==VIA_T_OFFiSIZF) {8				FFS_ <l(&	ci->ine V"tootmuahot viansize!\n"ule				re can -EINVALle			}e			addrnabsnd_ludesgbuf_get_addr(subvice
 ,pofsule			((u3in*)FFS->t via.ace
)[ d_D<< 1]D= cCK
to_le32(addrule			rnabsnd_ludesgbuf_get_chunk_size(subvice
 ,pofs,  estule			restt-=,
;8			*  (!  estu {8				*  (  == leriodso- 1)E					flag ==VIA_TBL_000	FFS;  RWbuff   b07 USryIAan				 5 eE					flag ==VIA_TBL_000	Fl t;  RW	eriodab07 USryIAan			}  5 eE				flag ==0;  RW	eriodaVIA_inues  will anextIAan			*	-			FFS_dbg(&	ci->ine E				"tbl %d: at %d ,sized%d (restt%d)\n" E				idx,pofs,  ,  estule			Aan			((u3in*)FFS->t via.ace
)[( d_<<1) + 1]D= cCK
to_le32(r |,flagule			FFS->idx_t via[ d_].statusD= ofsle			FFS->idx_t via[ d_].sizenab
;8			ofsD+=,
;8			*dx++;8		} whiian(restt> eule	}e	FFS->tbl_REGriesnab*dx;e	FFS->bufsize = leriodso*
fragsize;
	FFS->bufsize)");
FS->bufsize / 2;
	FFS->fragsizenabfragsize;
	re can 068}_8EDESC(index,s for_
#d_t via(vicu#x)G_OFFSn*ine Vvicu#x)snd_ludesubvice
  *subvice
 ,E			 sovicu#x)	ci_FFSn*	ci)
{
	*  (FFS->t via.ace
u {8		snd_dma_r th_	ag s(&FFS->t via);8		FFS->t via.ace
 = NULLle	}e	kr th(FFS->idx_t via);
	FFS->idx_t vianabNULLle	re can 068}_8+ VIA_RBas(inI/OVIA/
 viSC(indelS		=unlsa-libdex,*nd_
#dESC(_ 0444_xread(vicu#x)G_OESC(m*e <l)
{
	re can del(VIA*/
#e <l, AC9#))68}_  viSC(indelS		=void)snd_
#dESC(_ 0444_x, Sui(vicu#x)G_OESC(m*e <l, unlsa-libdex,ODUL
{
	outlMODU, VIA*/
#e <l, AC9#))68}_  viSC(index,*nd_
#dESC(_ 0444_ready(vicu#x)G_OESC(m*e <l, dex,*DSX3 cha)
{
	unlsa-libdex,timeFOR =  chaSTICK1m =han	unlsa-libdex,ODUle	
	whiian(timeFOR--t> eu {8		ue;
mo(1ule		*  (!(MODUnabsnd_
#dESC(_ 0444_xread(e <l)) &x04
#defiAC9#OBUSY))E			re can ODUn& 0xffffle	}e	FFS_ <l(e <l-> joy->ine V" 0444_ready:, 0444)%i)nc.lti-ready)[0x%x]\n" E		 sovDSX3 cha,bsnd_
#dESC(_ 0444_xread(e <l))le	re can -EIO68}_  viSC(index,*nd_
#dESC(_ 0444_ODUor(vicu#x)G_OESC(m*e <l, dex,*DSX3 cha)
{
	unlsa-libdex,timeFOR =  chaSTICK1m =han	unlsa-libdex,ODU,,ODU1;
	unlsa-libdex,siSC = ! DSX3 chan?x04
#defiAC9#OPRIMARY_VALID :E					
#define AC9#OSEg, DARY_VALIDle	
	whiian(timeFOR--t> eu {8		ODUnabsnd_
#dESC(_ 0444_xread(e <l);8		ODU samODUn& (04
#defiAC9#OBUSY |,siSCule		*  (ODU sa=,siSCuE			re can ODUn& 0xffffle		ue;
mo(1ule	}e	re can -EIO68}_  viSC(invoid)snd_
#dESC(_ 0444_wait(vicu#x)snd_am(d3_am(d)
{
	vicu#x)G_OESC(m*e <lnabam(d->	rivne _Fatast	, in <lst	 <lnabsnd_
#dESC(_ 0444_ready(e <l, am(d->mke)68	 RWheoptwe nelib wiwait fairly
ialiif
sttime.. han	   (!tibilituE		msleep(5M00
#}
 viSC(invoid)snd_
#dESC(_ 0444_w Sui(vicu#x)snd_am(d3_am(d E				  sounlsa-libsh <s reg E				  sounlsa-libsh <s ODUL
{
	vicu#x)G_OESC(m*e <lnabam(d->	rivne _Fatast	unlsa-libdex,xODUle
	xODUnab!am(d->mken?x04
#defiAC9#OIADECefiGPRIMARY :	0x08
#deAC9#OIADECefiGSEg, DARY;
	xODUn<<=x04
#defiAC9#OIADECefiGSHIFT;
	xODUn|= 
#dy<< 04
#defiAC9#OIMiGSHIFT;
	xODUn|= ODUn<<x04
#defiAC9#ODATAGSHIFT68	vnd_
#dESC(_ 0444_x, Sui(e <l, xODU)68	vnd_
#dESC(_ 0444_ready(e <l, am(d->mke)68}
 viSC(inunlsa-libsh <s vnd_
#dESC(_ 0444_read(vicu#x)snd_am(d3_am(d ounlsa-libsh <s regL
{
	vicu#x)G_OESC(m*e <lnabam(d->	rivne _Fatast	unlsa-libdex,xODU,,ODU ==0xffffle	dex,againDEF068
	xODUnabam(d->mken<< 04
#defiAC9#OIADECefiGSHIFT;
	xODUn|= am(d->mken?x04
#defiAC9#OSEg, DARY_VALID :	0x08
#deAC9#OPRIMARY_VALID;
	xODUn|= 0x08
#deAC9#OREAD;
	xODUn|= (
#dy& 0x7f)y<< 04
#defiAC9#OIMiGSHIFT;
ree so	whiian(1u {8ree so		   (again++t> 3u {8			FFS_ <l(e <l-> joy->ine E				" 0444_read:, 0444)%i)nc.lti-ODUort[0x%x]\n" E				  sam(d->mke,bsnd_
#dESC(_ 0444_xread(e <l))le	com>
 *	re can 0xffffle		}8		snd_
#dESC(_ 0444_x, Sui(e <l, xODU)68		ue;
mo (20ule		*  (snd_
#dESC(_ 0444_ODUor(e <l, am(d->mke)D>==eu {8			ue;
mo(25ule			ODUnabsnd_
#dESC(_ 0444_xread(e <l);8			breakle		}8	}e	re can ODUn& 0xffffle}
 viSC(invoid)snd_
#dESC(_ VRA, 5_NT_FL(vicu#x)G_OESC(m*e <l, vicu#x)G_OFFSn*G_OFFSE
{
	outbSTART)
#defineine  yte - channel tyfine   VIyte - channel ty_REG_ E	m>
 *Vfsets */
#define VEG_CTRL_START	))le	inbSTARets */
#define VEG_CTRL_START	))le	ue;
mo(50)68	 RW
module,tde <linuschan	outbS/* R,*Vfsets */
#define VEG_CTRL_START	))le	- ch for,tde <linuschan	outbS/* 3,*Vfsets */
#define VEG_CTRLCTIVE	))le	outbS/* R,*Vfsets */
#define VEG_CTRL	0x0))l  RW#def
#define V	// outlMR,*Vfsets */
#define VEG_CTRL_EG_OFFS))le	define->lastposDEF068	define->hwptr_doneDEF068}
t + VIA_ Ide <linu VRAdles distUto the sefin 4 stre3AVIADEDESC(indrqre can_s vnd_
#dPAR_tde <linu( = SNrq,nvoid)*ine_idL
{
	vicu#x)G_OESC(m*e <lnabine_id;
	unlsa-libdex,siSCus;
	unlsa-libdex,i;n
	viA8233= del(VIA*/
#e <l, SGD0x40	/*))le	i  (! (viA8233& e <l->dexr_mask)u {8		i  (e <l->rmidi)n			*	ichepor
#endi,tde <linuIAan			re can vnd_
#endi_uart_tde <linu( rq,ne <l->rmidi->	rivne _Fata);8		re can IRQ_NONE68	}_8	 RWcheporviA8233rden@yahovice
  */
	vpin_quir(&e <l->r#d_quir)68	#def(iDEF06 i < e <l->ROL_FFSs; i++) {8		vicu#x)G_OFFSn*G_OFFSDEF&e <l->FFSs[i]68		unlsa-libume =n_viA8233= debSTARets */
#define VEG_CTRLCTIVE	))le		i  (! (n_viA8233& (04
#defixx08	FFS|04
#defixx08	Fl t|04
#defixx08	/
#defi)))E			VIA_inuele		*  (Oefine->subvice
  && Oefine->runnODUu {8			*	-			disUpdA8e)hwptr_doneDbato ton '	erioda;
mpto '-			distde <linus. We'llK, 82iace,hPARTICUe <lnre cans 0 -			dis#def*/
#define VIA_RE.-			di/8			*  (n_viA8233& 04
#defixx08	FFS)E				define->hwptr_doneDEF068			 5 eE				define->hwptr_doneD+= Oefine->fragsize;
			define->ND_ude <linuD= c_siSCus;
			vpin_unquir(&e <l->r#d_quir)68			snd_lude eriod_;
mpto (Oefine->subvice
 );
			vpin_quir(&e <l->r#d_quir)68			define->ND_ude <linuD= 0le		}8		outbSc_siSCus,*Vfsets */
#define VEG_CTRLCTIVE	))l  RWaODU##_	}8	vpin_unquir(&e <l->r#d_quir)68	re can IRQ_HANDLED68}_8+ VIA_RIde <linu VRAdles diDEDESC(indrqre can_s vnd_
#dLER_Ctde <linu( = SNrq,nvoid)*ine_idL
{
	vicu#x)G_OESC(m*e <lnabine_id;
	unlsa-libdex,siSCus;
	unlsa-libdex,i;n	 = SNrqre can EF068
	 RWcheporviA8233rden@yahovice
  */
	vpin_quir(&e <l->r#d_quir)68	viA8233= del(VIA*/
#e <l, SGD0x40	/*))le8	#def(iDEF06 i < e <l->ROL_FFSs; i++) {8		vicu#x)G_OFFSn*G_OFFSDEF&e <l->FFSs[i]68		vicu#x)snd_ludesubvice
  *subvice
 ra		unlsa-libume =n_viA823,nshadow_siSCus;
8		vhadow_siSCusDEF(viA8233>> Oefine->shadow_shifu) &n			(VUSED		0x40	/* RO */
#defi|04
#defixx08	FFS|
			
#define xx08	Fl t)68		n_viA8233= vhadow_siSCusD& (04
#defixx08	FFS|04
#defixx08	Fl t)le		i  (!n_viA823)E			VIA_inuele8		vubvice
  = Oefine->subvice
 ra		*  (subvice
  && Oefine->runnODUu {8			*	-			disUpdA8e)hwptr_doneDbato ton '	erioda;
mpto '-			distde <linus. We'llK, 82iace,hPARTICUe <lnre cans 0 -			dis#def*/
#define VIA_RE.-			di/8			*  (n_viA8233& 04
#defixx08	FFS)E				define->hwptr_doneDEF068			 5 eE				define->hwptr_doneD+= Oefine->fragsize;
			define->ND_ude <linuD= c_siSCus;
			*  (shadow_siSCusD& VUSED		0x40	/* RO */
#defi)E				define->ND_ude <linuD|= 0x08
#deRO */
#defi;
			vpin_unquir(&e <l->r#d_quir)688			snd_lude eriod_;
mpto (subvice
 ule_			vpin_quir(&e <l->r#d_quir)68			define->ND_ude <linuD= 0le		}8		outbSc_siSCus,*Vfsets */
#define VEG_CTRLCTIVE	))l  RWaODU##_		Nrqre can EF1;_	}8	vpin_unquir(&e <l->r#d_quir)68	re can IRQ_RETVAL(Nrqre can)68}_8+ VIA_RPCMDcalluppos.de>
 *	- ustriggerDcalluppoVIADEDESC(index,snd_
#dESC(_ludetrigger(vicu#x)snd_ludesubvice
  *subvice
 ,ndex,smdL
{
	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule	vicu#x)G_OFFSn*G_OFFSDEFsubvice
 ->runtime->	rivne _Fatast	unlsa-libume =ODUle
	i  (e <l->e <l_/* R != 	0x0c\
	efi)8		ODUnab - channel tyINT;
	 5 eE		ODU ==0le	vwitaho(smdL {8	case SifdefPCM4	/* RWC IT		0:8	case SifdefPCM4	/* RWC _REUME:E		ODU |ab - channel tyIT		0;8		Oefine->runnODU EF1;_		breakle	case SifdefPCM4	/* RWC ITOP:8	case SifdefPCM4	/* RWC SUSPEND:8		ODUnab - channel tyfine   VI;8		Oefine->runnODU EF0;_		breakle	case SifdefPCM4	/* RWC ine  _PUSH:E		ODU |ab - channel tyine  ;8		Oefine->runnODU EF0;_		breakle	case SifdefPCM4	/* RWC ine  _RELEASE:E		Oefine->runnODU EF1;_		breakle	f

ARM_:
		re can -EINVALle	}e	outbSODU, VIAets */
#define VEG_CTRL_START	))le	ifo(smdsa=,SifdefPCM4	/* RWC ITOP)8		snd_
#dESC(_ VRA, 5_NT_FL(e <l, G_OFFSE;
	re can 068}_8E+ VIA_lndex, ccalluppos.de>
 *	- use <linux8233C lS		e =	
#includa Mll agi PARsg-buff   \
enum 4 sll arestt- chaVIA/
 index (fchepo_udODUor_	
##define 	
#_CON	((	
#_C< Oefine->lastposD&& ((	
#_C>= Oefine->bufsize)"||PE				|||||Oefine->lastposD< Oefine->bufsize)))
 viSC(indelS		=unlsa-libdex,e <l_lS		e _	
##vicu#x)G_OESC(m*e <l,E					
  vicu#x)G_OFFSn*G_OFFS,E					
  unlsa-libdex,idx,E					
  unlsa-libdex,- cha)
{
	unlsa-libdex,size,Dbato,  es;n
	vizenabdefine->Ndx_t via[ d_].size;
	batonabdefine->Ndx_t via[ d_].status;
	resnabbaton+,sized-,- chale	ifo(resn>= Oefine->bufsize)
		rest-=,Oefine->bufsize68
	 RWcheporll aODUorit if not, e <linux8d=	
#includhan	   (sized<,- cha) {8		FFS_dbg(e <l-> joy->ine E			"udODUor 
#dESC(_ u _	tr (sized= %dge. chan=t%d)\n" E			stridex)size,Didex)- cha);
		rest=|Oefine->lastposle	}  5 e {8		i  (!,- cha) {8			 RWSome mobosDrde <s . chan=t0,8235)
 DMAab07 USry,-			dist.e. . chan=tsized; deed.-			di Let'sPchepor,hP vers boolstepg of bov8233C expecG_CTsize.-			di/8			*nble;
t
 = rest-|Oefine->lastposle			*  (FF
t
 < 0)E				FF
t
 +=,Oefine->bufsize68			*  ((unlsa-libdex)FF
t
 > Oefine->fragsize)E				resnabbatole		}8		i  (e epo_udODUor_	
##define  res)u {8G_GdRM_.cz>");
MODULE			FFS_dbg(e <l-> joy->ine E				"fail:  d_DEF%i/%i, lastposDEF0x%x, bufsize)");0x%x, statized= 0x%x, tized= 0x%x, . chan=t0x%x\n" E				idx,pOefine->tbl_REGries E			str||||Oefine->lastpos, Oefine->bufsize) E			str||||Oefine->Ndx_t via[ d_].status E			str||||Oefine->Ndx_t via[ d_].size,D- cha);
static 			*	ic chan
#definenre cans full tized,hPARtatif nbuff   \sDrdyah_CTRL_			resnabbaton+,size68			*  (e epo_udODUor_	
##define  res)u {8				FFS_dbg(e <l-> joy->ine E					"udODUor 
#dESC(_ u _	tr (2),K, ODU last-ODUortlndex, \n"ule				rest=|Oefine->lastposle			}8		}8	}e	re can  es;n}_8+ VIA_ge Mll a - stop index, c823
#dPARVIADEDESC(insnd_ludeuhram.s_s vnd_
#dPAR_lude ndex, (vicu#x)snd_ludesubvice
  *subvice
 L
{
	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule	vicu#x)G_OFFSn*G_OFFSDEFsubvice
 ->runtime->	rivne _Fatast	unlsa-libdex,idx, 	tr,D- cha,  es;n
	*  (snd_DUL_ON(!Oefine->tbl_REGries))
		re can 0le	i  (!(debSTARets */
#define VEG_CTRLCTIVE	))3& 04
#defixx08	
#defi))
		re can 0le
	vpin_quir(&e <l->r#d_quir)68	. chan=tdel(VIAets */
#define VEG_CTRL_EG_OIA_RE))3& 0xffffffle	*	iTl aO#dPARa doec.lti- withll a - stop \
enum
#define E	m*Vvo wC nelib wie <linux8233C \
enumfromw_EG_OFFS.
	di/8		tr =tdel(VIAets */
#define VEG_CTRL_EG_OFFS))le	i  (	tr <= (unlsa-libdex)Oefine->t via.addru
		 d_DEF068	 5 e *	i_EG_OFFS holds233C addressn+,8U##_		Nd_DEF((	tr - (unlsa-libdex)Oefine->t via.addru /,8U- 1) %pOefine->tbl_REGries;
	resnabe <l_lS		e _	
##e <l, G_OFFS,)idx,p- cha);
	define->lastposDEFres;  RWremeet *p33C last-	
#includhan	vpin_unquir(&e <l->r#d_quir)688	re can T	0x 
to_hram.s(subvice
 ->runtime  res);n}_8+ VIA_ge Mll a - stop index, c823
#dncluVIADEDESC(insnd_ludeuhram.s_s vnd_
#dED		0lude ndex, (vicu#x)snd_ludesubvice
  *subvice
 L
{
	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule	vicu#x)G_OFFSn*G_OFFSDEFsubvice
 ->runtime->	rivne _Fatast	unlsa-libdex,idx, - cha,  es;n	dex,siSCus;
	
	*  (snd_DUL_ON(!Oefine->tbl_REGries))
		re can 0le
	vpin_quir(&e <l->r#d_quir)68	. chan=tdel(VIAets */
#define VEG_CTRL_EG_OIA_RE))68	viA8233= define->ND_ude <linule	i  (!viA823)E		viA8233= debSTARets */
#define VEG_CTRLCTIVE	))lee	*	iAnizatastop bugd; witho(via \sD, 04prog4);
MOby statiDU a E	m*Vhannel tyIT		0. han	   (e <l->r#t in ma=ab - cha ((viaD&& (viA8233& 04
#defixx08	FFS))8		snd_
#dESC(_ludetrigger(vubvice
 ,nSifdefPCM4	/* RWC IT		0ule_	*  (!(viA8233& 04
#defixx08	
#defi)) {8		 es EF0;_		go wiunquir;8	}_	*  (. chan& 0xffffffu {8		id_DEF. chan>> 24ra		*  ( d_D>==Oefine->tbl_REGries) {8G_GdRM_.cz>");
MODULE			FFS_dbg(e <l-> joy->ine E				"fail:  dODUor  d_DEF%i/%i\n" ,idx,E			str||||Oefine->tbl_REGries);
static 			rest=|Oefine->lastposle		}  5 e {8			. chan&= 0xffffffle			resnabe <l_lS		e _	
##e <l, G_OFFS,)idx,p- cha);
		}8	}  5 e {8		rest=|Oefine->hwptr_done;8		i  (!define->ND_ude <linuu {8			*  (siA8233& 04
#defixx08	FFS) {8				 es EF0;_			}  5 eE				*  (siA8233& 04
#defixx08	Fl t) {8					 es += Oefine->fragsize;
				}8		}8	}			str|
unquir:
	define->lastposDEFres;n	vpin_unquir(&e <l->r#d_quir)688	re can T	0x 
to_hram.s(subvice
 ->runtime  res);n}_88+ VIA_hwe <sounccalluppo:- us placA8e)ithobuff    4 sbuild each chbuff   descriptn mat viaVIADEDESC(index,snd_
#dESC(_hwe <soun(vicu#x)snd_ludesubvice
  *subvice
 ,E				|vicu#x)snd_ludehwe <sounc*hwe <sounL
{
	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule	vicu#x)G_OFFSn*G_OFFSDEFsubvice
 ->runtime->	rivne _Fatast	, in <lstt	 <lnabsnd_ludelib_m plac_	ag s(vubvice
 ,n <soun_buff  _T	0x (hwe <sounL)le	i  ( <ln< 0)E		re can  <lst	 <lnabbuild_
#d_t via(G_OFFS,)vubvice
 ,ne <l->	ci,E			str||| <soun_leriods(hwe <sounL,E			str||| <soun_leriod_T	0x (hwe <sounL)le	i  ( <ln< 0)E		re can  <lste	re can 068}_8+ VIA_hwer thecalluppo:- uss for each chbuff   descriptn mat via  4 srel#ase h chbuff  VIADEDESC(index,snd_
#dESC(_hwer th(vicu#x)snd_ludesubvice
  *subvice
 L
{
	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule	vicu#x)G_OFFSn*G_OFFSDEFsubvice
 ->runtime->	rivne _Fatast8	. for_
#d_t via(G_OFFS,)vubvice
 ,ne <l->	ci)68	vnd_ludelib_r th_	ag s(subvice
 ule	re can 068}_8E+ VIA_tusDeach ch channel currVIADEDESC(invoid)snd_
#dESC(_tus_t via_	tr(vicu#x)G_OESC(m*e <l, vicu#x)G_OFFSn*G_OFFSE
{
	vnd_
#dESC(_ 0444_ready(e <l, 0)le	outl((u3i)Oefine->t via.addr,*Vfsets */
#define VEG_CTRL	_OFFSET_))le	ue;
mo(20ule	vnd_
#dESC(_ 0444_ready(e <l, 0)le}_8+ VIA_pr *
 hecalluppos#def	- suppor 4 snel can 823
#dPARVIADEDESC(invoid)
#dPAR_tusup_#demat(vicu#x)G_OESC(m*e <l, vicu#x)G_OFFSn*G_OFFS,E				vicu#x)snd_luderuntime *runtimeE
{
	vnd_
#dESC(_ VRA, 5_NT_FL(e <l, G_OFFSE;
	 RWlliscmm(enbe)tusDafx, ccVRA, 5_NT_FLdhan	vnd_
#dESC(_tus_t via_	tr(e <l, G_OFFSE;
	outbSTART)
#d_TYPE_16BIT		0 |E	m>
 *(runtime->#demat*a=,SifdefPCM4FORMAT_S16_LEn?x04
#defi0x00100000 :=eu |E	m>
 *(runtime-> VRA, 5 => 1n?x04
#defi0x001	/* dw :=eu |E	m>
 *((Oefine->
#d_statusn& 0x1eu ==t0,?x04
#defi0x001  VIA_REG_T :=eu |E	m>
 *04
#defi0x001  VI* bytE	m>
 *04
#defi0x001  VIFl t,*Vfsets */
#define VEG_CTRL	0x0))le}
 viSC(index,snd_
#dPAR_l- suppo pr *
 h(vicu#x)snd_ludesubvice
  *subvice
 L
{
	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule	vicu#x)G_OFFSn*G_OFFSDEFsubvice
 ->runtime->	rivne _Fatast	vicu#x)snd_luderuntime *runtimeDEFsubvice
 ->runtimele
	vnd_am(dxtus_ine (e <l->am(d oAC9#OPCM4FRO VIDAC_RATE, runtime->SC(dx;
	vnd_am(dxtus_ine (e <l->am(d oAC9#Oecond, runtime->SC(dx;
	
#dPAR_tusup_#demat(e <l, G_OFFS,)runtimeEle	re can 068}_8viSC(index,snd_
#dPAR_nel can_pr *
 h(vicu#x)snd_ludesubvice
  *subvice
 L
{
	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule	vicu#x)G_OFFSn*G_OFFSDEFsubvice
 ->runtime->	rivne _Fatast	vicu#x)snd_luderuntime *runtimeDEFsubvice
 ->runtimele
	vnd_am(dxtus_ine (e <l->am(d oAC9#OPCM4LR_ADC_RATE, runtime->SC(dx;
	
#dPAR_tusup_#demat(e <l, G_OFFS,)runtimeEle	re can 068}_8+ VIA_
MODUll a - stop SC(dVIADEDESC(index,
#d_
MOD_ine (vicu#x)G_O_ine _
MODU*e VIA, inrne )
{
	* ine RM_Dd EF068
	vpin_quir_irq(&e V->luir)68	ifo(rec->SC(d != rne ) {8		i  (rec->SC(d && rec-> to t> 1)  RWalready)_FLdhan			. RM_Dd EF-EINVALle		 5 e {8			rec->SC(d =nrne st			. RM_Dd EF1;
		}8	}n	vpin_unquir_irq(&e V->luir)68	re can . RM_Ddle}_8+ VIA_pr *
 hecalluppos#defand 	- suppor823
#dncluVIADEDESC(index,snd_
#dES		0l- suppo pr *
 h(vicu#x)snd_ludesubvice
  *subvice
 L
{
	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule	vicu#x)G_OFFSn*G_OFFSDEFsubvice
 ->runtime->	rivne _Fatast	vicu#x)snd_luderuntime *runtimeDEFsubvice
 ->runtimele	dex,(ac97SC(d =ne <l->Fy, src,?xESC(a :=runtime->SC(d;t	, inrne _. RM_Ddle	u3inrbits;n
	*  ((rne _. RM_Ddt=|Oef_
MOD_ine (&e <l->rne s[0],,(ac97SC(d))n< 0)E		re can rne _. RM_Ddle	i  (rne _. RM_Dd)8		snd_am(dxtus_ine (e <l->am(d oAC9#OPCM4FRO VIDAC_RATE,E				||e <l->Ro_vra,?xESC(a :=runtime->SC(d)le	ifo(s <l->*
 * _on && Oefine->r#d_statusn==t0x30)8		snd_am(dxtus_ine (e <l->am(d oAC9#Oecond, runtime->SC(dx;
e	i  (runtime->SC(dn==tESC(a)E		rbits ==0xfffff;
	 5 eE		rbits ==(0x1eeeee /tESC(a)IA_runtime->SC(dn+n			((0x1eeeee %tESC(a)IA_runtime->SC(d) /tESC(a;
	vnd_DUL_ON(rbits & ~0xfffffule	vnd_
#dESC(_ VRA, 5_NT_FL(e <l, G_OFFSE;
	vnd_
#dESC(_tus_t via_	tr(e <l, G_OFFSE;
	outbSe <l->	- suppo 0444);[Oefine->r#d_statusn/ 0x1e][0],E	m>
 *Vfsets */
#define VEGS_PLAYBACK_VOLUME_L)E;
	outbSe <l->	- suppo 0444);[Oefine->r#d_statusn/ 0x1e][1],E	m>
 *Vfsets */
#define VEGS_PLAYBACK_VOLUME_R))le	outl((runtime->#demat*a=,SifdefPCM4FORMAT_S16_LEn?x04
E_STEREO	0x00100000 :=eu |  RW#demat*han	m>
 *(runtime-> VRA, 5 => 1n?x04
URR_COUNT	0x0c	/* dw :=eu |  RWfineeo*han	m>
 *rbits |  RWine   VI	m>
 *0xffGGGGGG,>
 * RWSTOP \
enumnc.luvo Drdyah_CTRL_	m>
 *Vfsets */
#define VEG_CTRL/
#defin))le	ue;
mo(20ule	vnd_
#dESC(_ 0444_ready(e <l, 0)le	re can 068}_8+ VIA_pr *
 hecalluppos#defhese chips.
 *	- suppor823
#dncluVIADEDESC(index,snd_
#dES		01		0x pr *
 h(vicu#x)snd_ludesubvice
  *subvice
 L
{
	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule	vicu#x)G_OFFSn*G_OFFSDEFsubvice
 ->runtime->	rivne _Fatast	vicu#x)snd_luderuntime *runtimeDEFsubvice
 ->runtimele	unlsa-libdex,slots;n	dex,fmt;
e	i  (Oef_
MOD_ine (&e <l->rne s[0],,runtime->SC(d) < 0)E		re can -EINVALle	vnd_am(dxtus_ine (e <l->am(d oAC9#OPCM4FRO VIDAC_RATE, runtime->SC(dx;
	vnd_am(dxtus_ine (e <l->am(d oAC9#OPCM4SEG_ODAC_RATE, runtime->SC(dx;
	vnd_am(dxtus_ine (e <l->am(d oAC9#OPCM4LFEIDAC_RATE, runtime->SC(dx;
	vnd_am(dxtus_ine (e <l->am(d oAC9#Oecond, runtime->SC(dx;
	vnd_
#dESC(_ VRA, 5_NT_FL(e <l, G_OFFSE;
	vnd_
#dESC(_tus_t via_	tr(e <l, G_OFFSE;

	fmt ==(runtime->#demat*a=,SifdefPCM4FORMAT_S16_LE) ?E		04
#defiMULTPLAY_FMT_00000 :	0x08
#deMULTPLAY_FMT_8000;
	fmt |= 
untime-> VRA, 5 =<<_4;
	outbSfmt,*Vfsets */
#define VEGS_MULTPLAY_FORMAT))lea <per	   (e <l->r#t in ma=ab - cha ((v33A)8		slotsDEF068	 5 e
static 	{8		/A_tusD*/
#defmket *p witlot 3, 4, d o8, 6, 9m {\
	Ved for SPDIF odhan		*	ic  stspoatiDU  wiFL, FR, RL, RR, C, LFE ??dhan		vwitaho(
untime-> VRA, 5 ) {8		case 1: slotsDEF(1<<0) | (1<<4); breakle		case 2: slotsDEF(1<<0) | (2<<4); breakle		case 3: slotsDEF(1<<0) | (2<<4) | (5<<8); breakle		case 4: slotsDEF(1<<0) | (2<<4) | (3<<8) | (4<<12); breakle		case 5: slotsDEF(1<<0) | (2<<4) | (3<<8) | (4<<12) | (5<<16); breakle		case 6: slotsDEF(1<<0) | (2<<4) | (3<<8) | (4<<12) | (5<<16) | (6<<20ul breakle		f

ARM_: slotsDEF06 breakle		}8	}e	 RWSTOP \
enumnc.luvo Drdyah_CTRL_	outlMRxffGGGGGG |,slots,*Vfsets */
#define VEG_CTRLCT#defin))le	ue;
mo(20ule	vnd_
#dESC(_ 0444_ready(e <l, 0)le	re can 068}_8+ VIA_pr *
 hecalluppos#defnel can 823
#dncluVIADEDESC(index,snd_
#dES		0nel can_pr *
 h(vicu#x)snd_ludesubvice
  *subvice
 L
{
	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule	vicu#x)G_OFFSn*G_OFFSDEFsubvice
 ->runtime->	rivne _Fatast	vicu#x)snd_luderuntime *runtimeDEFsubvice
 ->runtimele
	i  (Oef_
MOD_ine (&e <l->rne s[1],,runtime->SC(d) < 0)E		re can -EINVALle	vnd_am(dxtus_ine (e <l->am(d oAC9#OPCM4LR_ADC_RATE, runtime->SC(dx;
	vnd_
#dESC(_ VRA, 5_NT_FL(e <l, G_OFFSE;
	vnd_
#dESC(_tus_t via_	tr(e <l, G_OFFSE;
	outbS - channeAPTURE_FIFO_EN_OFF,*Vfsets */
#define VEGS_eAPTURE_FIFO))le	outl((runtime->#demat*a=,SifdefPCM4FORMAT_S16_LEn?x04
E_STEREO	0x00100000 :=eu |n	m>
 *(runtime-> VRA, 5 => 1n?x04
URR_COUNT	0x0c	/* dw :=eu |I	m>
 *0xffGGGGGG,>
 * RWSTOP \
enumnc.luvo Drdyah_CTRL_	m>
 *Vfsets */
#define VEG_CTRL/
#defin))le	ue;
mo(20ule	vnd_
#dESC(_ 0444_ready(e <l, 0)le	re can 068}_8 + VIA_ll aSC(dxs_s ndex inclu,)identicalnable,othf	- suppor 4 snel canVIADEDESC(insicu#x)snd_ludehC(dxs_s snd_
#dESC(_hw =
{
	.lude =			(SifdefPCM4INFO_MMAP |,SifdefPCM4INFO_z>");LEAVED |E				|SifdefPCM4INFO_BLOCK_TRANSFER |E				|SifdefPCM4INFO_MMAP_VALID |E				| RWSifdefPCM4INFO__REUME |,Aan				WSifdefPCM4INFO_ine  L,E	.#demats =		SifdefPCM4FMT000	U8 |,SifdefPCM4FMT000	S16_LE,E	.ine st=		SifdefPCM4RATE_ESC(a,E	.ine _minDE		ESC(a,E	.ine _maxDE		ESC(a,E	. VRA, 5 _minDE		1,E	. VRA, 5 _maxDE		) E	.buff  _T	0x _maxDE	VIA_MAX_BUFSIZF E	.leriod_T	0x _minDE	32 E	.leriod_T	0x _maxDE	VIA_MAX_BUFSIZF / 2 E	.leriod _minDE		2 E	.leriod _maxDE		VIA_T_OFFiSIZF / 2 E	.fifo_tized=		a,E};_8 + VIA_openecallupposskeletonVIADEDESC(index,snd_
#dESC(_ludeopen(vicu#x)G_OESC(m*e <l, vicu#x)G_OFFSn*G_OFFS,E				vicu#x)snd_ludesubvice
  *subvice
 L
{
	vicu#x)snd_luderuntime *runtimeDEFsubvice
 ->runtimele	dex, <lst	vicu#x)G_O_ine _
MODU*ene pst	ers fusn_srcnabfalsele
	runtime->hw = snd_
#dESC(_hw;
	
	/A_tusDll ahw ine  SX3 includhan	ene pDEF&e <l->rne s[Oefine->tG_##xy l];
	vpin_quir_irq(&ene p->luir)68	rne p-> to ++;8	ifo(s <l->*
 * _on && Oefine->r#d_statusn==t0x30) {8		 RWDXS#3r 4 s*
 *  isc##,Aan		runtime->hw.ine st= e <l->am(d->rne s[AC9#ORATESOecond]68		vnd_ludelimit_hwerne s(runtimeule	}  5 e ifo(s <l->ay, fixed && Oefine->r#d_statusn<PAR400 {8		 RWfixed = no	- supporine   VI		runtime->hw.ine st= SifdefPCM4RATE_ESC(a;I		runtime->hw.ine _minDE runtime->hw.ine _maxDEtESC(a;
	}  5 e ifo(s <l->ay, srcn&& Oefine->r#d_statusn<PAR400 {8		 RW, 82full SRCsnela
/* riespof = nohan		runtime->hw.ine st= (SifdefPCM4RATE__STAINUOUS |E				|||||SifdefPCM4RATE_SC(a_ESC(a);I		runtime->hw.ine _minDE SC(a;I		runtime->hw.ine _maxDEtESC(a;
		usn_srcnabicue;
	}  5 e ifo(! rne p->rne ) {8		iex,idxt=|Oefine->tG_##xy ln?xAC9#ORATESOADC :=AC9#ORATESOFRO VIDAC;I		runtime->hw.ine st= e <l->am(d->rne s[ d_]68		vnd_ludelimit_hwerne s(runtimeule	}  5 e {8		 RWaWfixed ine   VI		runtime->hw.ine st= SifdefPCM4RATE_KNOT;I		runtime->hw.ine _maxDEtruntime->hw.ine _minDE rne p->rne ;8	}n	vpin_unquir_irq(&ene p->luir)68
	/A_we ma_INTmov82followiDU uonstaiex,,hPARwe mo * yh channREGries_	m>
in,tde <linuIAan	*  (( <lnabsnd_ludehweuonstraiex_ude ger(runtime  SifdefPCM4HW_inRAM_PERIODS))n< 0)E		re can  <lste	*  (usn_src) {8		 <lnabsnd_ludehweruia_noNT_/
#de(runtime  ESC(a);I		i  ( <ln< 0)E			re can  <lst	}e
	runtime->	rivne _Fatat=|Oefine;
	define->vubvice
  = subvice
 rae	re can 068}_8 + VIA_openecalluppos#def	- suppor823
#dPARVIADEDESC(index,snd_
#dPAR_l- suppo open(vicu#x)snd_ludesubvice
  *subvice
 L
{
	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule	vicu#x)G_OFFSn*G_OFFSDEF&e <l->FFSs[e <l->	- suppo dFSnon+,subvice
 ->mket *]st	, in <lstt	*  (( <lnabsnd_
#dESC(_ludeopen(e <l, G_OFFS,)subvice
 u)n< 0)E		re can  <lst	re can 068}_8+ VIA_openecalluppos#def	- suppor823
#dnclu = nVIADEDESC(index,snd_
#dES		0l- suppo open(vicu#x)snd_ludesubvice
  *subvice
 L
{
	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule	vicu#x)G_OFFSn*G_OFFSle	unlsa-libdex,sice
 ra	, in <lstt	G_OFFSDEF&e <l->FFSs[e <l->	- suppo dFSnon+,subvice
 ->mket *]st	,  (( <lnabsnd_
#dESC(_ludeopen(e <l, G_OFFS,)subvice
 u)n< 0)E		re can  <lst	vice
  = Oefine->r#d_statusn/ 0x1e;8	ifo(s <l->ay, VIA_REGs[vice
 ]) {8		c <l->	- suppo 0444);[vice
 ][0] =
				or vDXS_MAX_VOLUME - (ay, x in 0444); & 31ule		c <l->	- suppo 0444);[vice
 ][1] =
				or vDXS_MAX_VOLUME - (ay, x in 0444); & 31ule		c <l->ay, VIA_REGs[vice
 ]->vd[0].access &=
			~SifdefCTL_ELEMn
#CESS4IN
#defi;
		snd_ctl_not* y(e <l-> joy  SifdefCTL_EVENTG_TYP_VALUE |E			str||||SifdefCTL_EVENTG_TYP_INFO,E			str||||&c <l->ay, VIA_REGs[vice
 ]->idule	}e	re can 068}_8+ VIA_openecalluppos#def	- suppor823
#dnclu hese chips.
 VIADEDESC(index,snd_
#dES		01		0x open(vicu#x)snd_ludesubvice
  *subvice
 L
{
	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule	vicu#x)G_OFFSn*G_OFFSDEF&e <l->FFSs[e <l->1		0x FFSno]st	, in <lst	 RWchRA, 5 =uonstraiexW#defVed forAE	m*V3r 4 s5WchRA, 5 =s_s lti-supe <sed
	di/8	viSC(inunlsa-lib* ine RM, 5 []D= {8		1, 2  4, 6e	};8	viSC(invicu#x)snd_ludehweuonstraiex_lisu hweuonstraiexs_chRA, 5 == {8		.. chan=tARRAYiSIZF( VRA, 5 ),E		.lisu =ne RM, 5 ,E		.maskb= 0,e	};8t	,  (( <lnabsnd_
#dESC(_ludeopen(e <l, G_OFFS,)subvice
 u)n< 0)E		re can  <lst	vubvice
 ->runtime->hw. VRA, 5 _maxDE 6;r	   (e <l->r#t in ma=ab - cha ((v33A)8		snd_ludehweuonstraiex_lisu(subvice
 ->runtime  0,E					
  SifdefPCM4HW_inRAM_NU GNESS,E					
  &hweuonstraiexs_chRA, 5 )le	re can 068}_8+ VIA_openecalluppos#defnel can 823
#dPARr 4 s
#dncluVIADEDESC(index,snd_
#dESC(_ el can_open(vicu#x)snd_ludesubvice
  *subvice
 L
{
	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule	vicu#x)G_OFFSn*G_OFFSDEF&e <l->FFSs[e <l->nel can_FFSnon+,subvice
 ->lud->FFSice]rae	re can snd_
#dESC(_ludeopen(e <l, G_OFFS,)subvice
 u68}_8+ VIA_closeDcalluppoVIADEDESC(index,snd_
#dESC(_ludeclose(vicu#x)snd_ludesubvice
  *subvice
 L
{
	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule	vicu#x)G_OFFSn*G_OFFSDEFsubvice
 ->runtime->	rivne _Fatast	vicu#x)G_O_ine _
MODU*ene pst
STARrel#ase h chine  
MODU*an	ene pDEF&e <l->rne s[Oefine->tG_##xy l];
	vpin_quir_irq(&ene p->luir)68	rne p-> to --;e	i  (! rne p-> to )E		rne p->rne  ==0le	vpin_unquir_irq(&ene p->luir)68	ifo(! rne p->rne ) {8		ifo(! Oefine->tG_##xy l) {8			vnd_am(dxupdA8e_power(e <l->am(d E					
     AC9#OPCM4FRO VIDAC_RATE, 0)68			snd_am(dxupdA8e_power(e <l->am(d E					
     AC9#OPCM4SEG_ODAC_RATE, 0)68			snd_am(dxupdA8e_power(e <l->am(d E					
     AC9#OPCM4LFEIDAC_RATE, 0)68		}  5 eE			snd_am(dxupdA8e_power(e <l->am(d E					
     AC9#OPCM4LR_ADC_RATE, eule	}e	define->vubvice
  = NULLle	re can 068}_8DESC(index,snd_
#dES		0l- suppo close(vicu#x)snd_ludesubvice
  *subvice
 L
{
	vicu#x)G_OESC(m*e <lnabsnd_ludesubvice
 _e <l(subvice
 ule	vicu#x)G_OFFSn*G_OFFSDEFsubvice
 ->runtime->	rivne _Fatast	unlsa-libdex,sice
 rat	vice
  = Oefine->r#d_statusn/ 0x1e;8	ifo(s <l->ay, VIA_REGs[vice
 ]) {8		c <l->ay, VIA_REGs[vice
 ]->vd[0].access |=
			SifdefCTL_ELEMn
#CESS4IN
#defi;
		snd_ctl_not* y(e <l-> joy  SifdefCTL_EVENTG_TYP_INFO,E			str||||&c <l->ay, VIA_REGs[vice
 ]->idule	}e	re can snd_
#dESC(_ludeclose(vubvice
 u68}_8
- c
#defin	- supporcallupposIADEDESC(insicu#x)snd_ludeops,snd_
#dPAR_l- suppo op == {8	.opene=		snd_
#dPAR_l- suppo open,E	. loseD=	snd_
#dESC(_lude lose,E	.ioctlD=	snd_ludelib_ioctl,E	.hwe <sounc=	snd_
#dESC(_hwe <soun,E	.hwer the=	snd_
#dESC(_hwer th E	.lr *
 he=	snd_
#dPAR_l- suppo lr *
 h E	.triggerD=	snd_
#dESC(_ludetrigger E	.lndex, c=	snd_
#dPAR_lude ndex,  E	.lag  =		snd_ludesgbuf_op _	ag ,E};_8- c
#definnel can callupposIADEDESC(insicu#x)snd_ludeops,snd_
#dPAR_ el can_op == {8	.opene=		snd_
#dESC(_ el can_open,E	. loseD=	snd_
#dESC(_lude lose,E	.ioctlD=	snd_ludelib_ioctl,E	.hwe <sounc=	snd_
#dESC(_hwe <soun,E	.hwer the=	snd_
#dESC(_hwer th E	.lr *
 he=	snd_
#dPAR_nel can_pr *
 h E	.triggerD=	snd_
#dESC(_ludetrigger E	.lndex, c=	snd_
#dPAR_lude ndex,  E	.lag  =		snd_ludesgbuf_op _	ag ,E};_8- c
#dnclu =nd 	- supporcallupposIADEDESC(insicu#x)snd_ludeops,snd_
#dES		0l- suppo op == {8	.opene=		snd_
#dES		0l- suppo open,E	. loseD=	snd_
#dES		0l- suppo close,E	.ioctlD=	snd_ludelib_ioctl,E	.hwe <sounc=	snd_
#dESC(_hwe <soun,E	.hwer the=	snd_
#dESC(_hwer th E	.lr *
 he=	snd_
#dES		0l- suppo pr *
 h E	.triggerD=	snd_
#dESC(_ludetrigger E	.lndex, c=	snd_
#dED		0lude ndex,  E	.lag  =		snd_ludesgbuf_op _	ag ,E};_8- c
#dnclu hese chips.
 *	- supporcallupposIADEDESC(insicu#x)snd_ludeops,snd_
#dES		01		0x op == {8	.opene=		snd_
#dES		01		0x open,E	. loseD=	snd_
#dESC(_lude lose,E	.ioctlD=	snd_ludelib_ioctl,E	.hwe <sounc=	snd_
#dESC(_hwe <soun,E	.hwer the=	snd_
#dESC(_hwer th E	.lr *
 he=	snd_
#dES		01		0x pr *
 h E	.triggerD=	snd_
#dESC(_ludetrigger E	.lndex, c=	snd_
#dED		0lude ndex,  E	.lag  =		snd_ludesgbuf_op _	ag ,E};_8- c
#dnclu nel can callupposIADEDESC(insicu#x)snd_ludeops,snd_
#dES		0nel can_op == {8	.opene=		snd_
#dESC(_ el can_open,E	. loseD=	snd_
#dESC(_lude lose,E	.ioctlD=	snd_ludelib_ioctl,E	.hwe <sounc=	snd_
#dESC(_hwe <soun,E	.hwer the=	snd_
#dESC(_hwer th E	.lr *
 he=	snd_
#dES		0nel can_pr *
 h E	.triggerD=	snd_
#dESC(_ludetrigger E	.lndex, c=	snd_
#dED		0lude ndex,  E	.lag  =		snd_ludesgbuf_op _	ag ,E};_8EDESC(invoid)x in 0efine(vicu#x)G_OESC(m*e <l, dex,idx,punlsa-libdex,
#d_status,E			i inshadow_pos, i intG_##xy lL
{
	e <l->FFSs[id_].r#d_statusn=,
#d_status;
	e <l->FFSs[id_].shadow_shifu =nshadow_posIA_4;
	e <l->FFSs[id_].tG_##xy ln=ntG_##xy la
	e <l->FFSs[id_].e <s = e <l->e <s +,
#d_status;
}_8+ VIA_cce
e  ll ainstancesW#defVed for,stre3Cn 4 stre5 (lti-(v33A)8IADEDESC(index,snd_
#dES		0ludenew(vicu#x)G_OESC(m*e <l)
{
	sicu#x)snd_ludn*	cmst	vicu#x)snd_ludechmapm*e mapst	, ini,n <lstt	e <l->	- suppo dFSnon==0lSTARx 4U*an	e <l->1		0x FFSnoDEtEST1 mux
1IA/
	e <l->nel can_FFSnon= 5lSTARx 2IA/
	e <l->ROL_FFSsn= 7a
	e <l->dexr_mask ==0x33033333;  RWFl t|* by#defrec0-1, mc, sdx0-3 han
	 RWPCMD#0:  4U=nd 	- supposn 4 s1snel can han	 <lnabsnd_ludenew(e <l-> joy  e <l-> joy->sh <sname  0, 4, 1, &lud)le	i  ( <ln< 0)E		re can  <lst	snd_ludeset_op (lud  SifdefPCM4STREAM_PLAYBACK, &snd_
#dES		0l- suppo op )st	snd_ludeset_op (lud  SifdefPCM4STREAM_eAPTURE, &snd_
#dES		0nel can_op )st	lud->	rivne _Fatat=|e <lst	viccpy(lud->name  e <l-> joy->sh <sname)a
	e <l->	cms[0] = 	cmst	/A_tusDeac	- supposnhan	#def(iDEF06 i < 4; i++)8		iein 0efine(e <l, d, 0x1edist, d, 0)le	- chel can han	iein 0efine(e <l, e <l->nel can_FFSno,  - channeAPTURE_ED		0xTIVE	, 6, 1)le
	vnd_ludelib_pr  placA8e_	ag s_#de_ pl(lud  Sifdef MAT	0x0cPPn_SG E					
     vnd_dma_	ci_Fata(e <l->	ci),E					
     64*1024, VIA_MAX_BUFSIZF)stt	 <lnabsnd_ludeaddechmap_ctl (lud  SifdefPCM4STREAM_PLAYBACK,E				|||||snd_ludestdechmaps, 2  0,E				|||||&chmap)le	i  ( <ln< 0)E		re can  <lste	 RWPCMD#1:  hese chips.
 *	- suppor 4 s24 snel can han	 <lnabsnd_ludenew(e <l-> joy  e <l-> joy->sh <sname  1  1  1  &lud)le	i  ( <ln< 0)E		re can  <lst	snd_ludeset_op (lud  SifdefPCM4STREAM_PLAYBACK, &snd_
#dES		01		0x op )st	snd_ludeset_op (lud  SifdefPCM4STREAM_eAPTURE, &snd_
#dES		0nel can_op )st	lud->	rivne _Fatat=|e <lst	viccpy(lud->name  e <l-> joy->sh <sname)a
	e <l->	cms[1] = 	cmst	/A_tusDeac	- suppo han	iein 0efine(e <l, e <l->1		0x FFSno,50x08
#deMULTPLAY_xTIVE	, 4, 0)le	- ctusDeachel can han	iein 0efine(e <l, e <l->nel can_FFSno + 1,  - channeAPTURE_ED		0xTIVE	 + 0x1e, d o1)le
	vnd_ludelib_pr  placA8e_	ag s_#de_ pl(lud  Sifdef MAT	0x0cPPn_SG E					
     vnd_dma_	ci_Fata(e <l->	ci),E					
     64*1024, VIA_MAX_BUFSIZF)stt	 <lnabsnd_ludeaddechmap_ctl (lud  SifdefPCM4STREAM_PLAYBACK,E				|||||snd_ludealtechmaps, 6  0,E				|||||&chmap)le	i  ( <ln< 0)E		re can  <lst	e <l->am(d->chmaps[SifdefPCM4STREAM_PLAYBACK]D= c mapste	re can 068}_8+ VIA_cce
e  ll ainstancesW#defVed forA8IADEDESC(index,snd_
#dES		a0ludenew(vicu#x)G_OESC(m*e <l)
{
	sicu#x)snd_ludn*	cmst	vicu#x)snd_ludechmapm*e mapst	, in <lstt	e <l->1		0x FFSnoDEt0;t	e <l->	- suppo dFSnon==1;
	e <l->nel can_FFSnon= 2;
	e <l->ROL_FFSsn= 3a
	e <l->dexr_mask ==0x03033C(a;  RWFl t|* by#defrec0, mc, sdx3 han
	 RWPCMD#0:  hese chips.
 *	- suppor 4 snel can han	 <lnabsnd_ludenew(e <l-> joy  e <l-> joy->sh <sname  0, 1  1  &lud)le	i  ( <ln< 0)E		re can  <lst	snd_ludeset_op (lud  SifdefPCM4STREAM_PLAYBACK, &snd_
#dES		01		0x op )st	snd_ludeset_op (lud  SifdefPCM4STREAM_eAPTURE, &snd_
#dES		0nel can_op )st	lud->	rivne _Fatat=|e <lst	viccpy(lud->name  e <l-> joy->sh <sname)a
	e <l->	cms[0] = 	cmst	/A_tusDeac	- suppo han	iein 0efine(e <l, e <l->1		0x FFSno,50x08
#deMULTPLAY_xTIVE	, 4, 0)le	- chel can han	iein 0efine(e <l, e <l->nel can_FFSno,  - channeAPTURE_ED		0xTIVE	, 6, 1)le
	vnd_ludelib_pr  placA8e_	ag s_#de_ pl(lud  Sifdef MAT	0x0cPPn_SG E					
     vnd_dma_	ci_Fata(e <l->	ci),E					
     64*1024, VIA_MAX_BUFSIZF)stt	 <lnabsnd_ludeaddechmap_ctl (lud  SifdefPCM4STREAM_PLAYBACK,E				|||||snd_ludealtechmaps, 6  0,E				|||||&chmap)le	i  ( <ln< 0)E		re can  <lst	e <l->am(d->chmaps[SifdefPCM4STREAM_PLAYBACK]D= c mapste	 RWScond-supe <sed? han	   (!,(ac97can_*
 * (e <l->am(d))
		re can 0le
	 RWPCMD#1:  DXS3c	- suppo  {\
	*
 * ) han	 <lnabsnd_ludenew(e <l-> joy  e <l-> joy->sh <sname  1  1  0, &lud)le	i  ( <ln< 0)E		re can  <lst	snd_ludeset_op (lud  SifdefPCM4STREAM_PLAYBACK, &snd_
#dES		0l- suppo op )st	lud->	rivne _Fatat=|e <lst	viccpy(lud->name  e <l-> joy->sh <sname)a
	e <l->	cms[1] = 	cmst	/A_tusDeac	- suppo han	iein 0efine(e <l, e <l->	- suppo dFSno,t0x30, 3, 0)le
	vnd_ludelib_pr  placA8e_	ag s_#de_ pl(lud  Sifdef MAT	0x0cPPn_SG E					
     vnd_dma_	ci_Fata(e <l->	ci),E					
     64*1024, VIA_MAX_BUFSIZF)st	re can 068}_8+ VIA_cce
e  a ll ainstanceW#def
#defia/bVIADEDESC(index,snd_
#dPAR_ludenew(vicu#x)G_OESC(m*e <l)
{
	sicu#x)snd_ludn*	cmst	, in <lstt	e <l->	- suppo dFSnon==0l
	e <l->nel can_FFSnon= 1;
	e <l->ROL_FFSsn= 2a
	e <l->dexr_mask ==0x77;  RWFl t | * by#defPB, CP, FM han
	 <lnabsnd_ludenew(e <l-> joy  e <l-> joy->sh <sname  0, 1  1  &lud)le	i  ( <ln< 0)E		re can  <lst	snd_ludeset_op (lud  SifdefPCM4STREAM_PLAYBACK, &snd_
#dPAR_l- suppo op )st	snd_ludeset_op (lud  SifdefPCM4STREAM_eAPTURE, &snd_
#dPAR_ el can_op )st	lud->	rivne _Fatat=|e <lst	viccpy(lud->name  e <l-> joy->sh <sname)a
	e <l->	cms[0] = 	cmst	iein 0efine(e <l, 0,  - channPLAYBACK_xTIVE	, 0, 0)le	iein 0efine(e <l, 1,  - channeAPTURE_xTIVE	, 0, 1)le
	vnd_ludelib_pr  placA8e_	ag s_#de_ pl(lud  Sifdef MAT	0x0cPPn_SG E					
     vnd_dma_	ci_Fata(e <l->	ci),E					
     64*1024, VIA_MAX_BUFSIZF)st	re can 068}_8 + VIA_ Mixer| <stVIA/
 viSC(index,snd_
#dES		0nel can_source_lude(vicu#x)snd_kVIA_REG *kVIA_REG,E					
  vicu#x)snd_ctl_elem_lude *ulude)
{
	 RW#demerly
they weopt"Line"r 4 s"Mic", but it looks like h a Mll yE	m*V withnothiDU  wido with233C actua *	hysicalnVIAn##xy ls...
	di/8	viSC(inume =*texts[2]D= {8		"Input1", "Input2"e	};8	ulude->/* R = SifdefCTL_ELEMn	0x0cENUMERATED;8	ulude->. chan=t1;8	ulude->value.eROLerne d.itemsn= 2a
	*  (ulude->value.eROLerne d.itemD>==2)E		ulude->value.eROLerne d.itemD=t1;8	viccpy(ulude->value.eROLerne d.name  texts[ulude->value.eROLerne d.item]Ele	re can 068}_8viSC(index,snd_
#dES		0nel can_source_get(vicu#x)snd_kVIA_REG *kVIA_REG,E					
 vicu#x)snd_ctl_elem_value *uVIA_REGL
{
	vicu#x)G_OESC(m*e <lnabsnd_kVIA_REG_e <l(kVIA_REG)st	unlsa-libif
ste <s = e <l->e <s +,(kVIA_REG->id.\
enum? S - channeAPTURE_NU GNES + 0x1e) :	0x08
#deeAPTURE_NU GNES)st	uVIA_REG->value.eROLerne d.item[0] = debSe <s)3& 04
#defieAPTURE_NU GNES_MICm? 1 :=ele	re can 068}_8viSC(index,snd_
#dES		0nel can_source_put(vicu#x)snd_kVIA_REG *kVIA_REG,E					
 vicu#x)snd_ctl_elem_value *uVIA_REGL
{
	vicu#x)G_OESC(m*e <lnabsnd_kVIA_REG_e <l(kVIA_REG)st	unlsa-libif
ste <s = e <l->e <s +,(kVIA_REG->id.\
enum? S - channeAPTURE_NU GNES + 0x1e) :	0x08
#deeAPTURE_NU GNES)st	u8 ODU, oODUle
	vpin_quir_irq(&e <l->r#d_quir)68	oODUnabdebSe <s)68	ODUnaboODUn& ~04
#defieAPTURE_NU GNES_MICa
	*  (uVIA_REG->value.eROLerne d.item[0])8		ODUn|ab - channeAPTURE_NU GNES_MICa
	*  (ODUn!aboODU)8		outbSODU, e <s)68	vpin_unquir_irq(&e <l->r#d_quir)68	re can ODUn!aboODU68}_8viSC(invicu#x)snd_kVIA_REGenew,snd_
#dES		0nel can_source== {8	.name== "Input Source=Select",E	.ifacR = SifdefCTL_ELEMnIFACE_MIXER,
	.lude =,snd_
#dES		0nel can_source_lude,
	.gusn=,snd_
#dES		0nel can_source_get E	.lusn=,snd_
#dES		0nel can_source_lus,E};_8index (fsnd_
#dES		0dxs3_*
 * _lude	snd_ctl_ers for_mono_lude_8viSC(index,snd_
#dES		0dxs3_*
 * _get(vicu#x)snd_kVIA_REG *kVIA_REG,E				
     vicu#x)snd_ctl_elem_value *uVIA_REGL
{
	vicu#x)G_OESC(m*e <lnabsnd_kVIA_REG_e <l(kVIA_REG)st	u8=ODUle
		ci_read VIAfig_T	0x(e <l->	ci, VUSED		0xcondnel t, &ODU)68	uVIA_REG->value.ude ger.value[0] = (ODUn& VUSED		0xcondnDX3)m? 1 :=ele	re can 068}_8viSC(index,snd_
#dES		0dxs3_*
 * _put(vicu#x)snd_kVIA_REG *kVIA_REG,E				
     vicu#x)snd_ctl_elem_value *uVIA_REGL
{
	vicu#x)G_OESC(m*e <lnabsnd_kVIA_REG_e <l(kVIA_REG)st	u8=ODU, oODUle
		ci_read VIAfig_T	0x(e <l->	ci, VUSED		0xcondnel t, &oODU)68	ODUnaboODUn& ~04
ED		0xcondnDX3a
	*  (uVIA_REG->value.ude ger.value[0])8		ODUn|ab - ED		0xcondnDX3a
	/A_twithll a*
 *  flagy#defr
e  filteriDU A/
	e <l->*
 * _on = uVIA_REG->value.ude ger.value[0] ? 1 :=ele	*  (ODUn!aboODU) {8			ci_, Sui VIAfig_T	0x(e <l->	ci, VUSED		0xcondnel t, ODU)68		re can 1;e	}e	re can 068}_8viSC(invicu#x)snd_kVIA_REGenew,snd_
#dES		0dxs3_*
 * _VIA_REG = {8	.name== SifdefCTL_NAME_IEC958("Outlusn",NONE,SWITCH),E	.ifacR = SifdefCTL_ELEMnIFACE_MIXER,
	.lude =,snd_
#dES		0dxs3_*
 * _lude,
	.gusn=,snd_
#dES		0dxs3_*
 * _get E	.lusn=,snd_
#dES		0dxs3_*
 * _put,E};_8viSC(index,snd_
#dES		0dxs 0444);_lude(vicu#x)snd_kVIA_REG *kVIA_REG,E				 
     vicu#x)snd_ctl_elem_lude *ulude)
{
	ulude->/* R = SifdefCTL_ELEMn	0x0cz>")GER;8	ulude->. chan=t2;8	ulude->value.ude ger.minDE 0;8	ulude->value.ude ger.maxDE or vDXS_MAX_VOLUMEle	re can 068}_8viSC(index,snd_
#dES		0dxs 0444);_get(vicu#x)snd_kVIA_REG *kVIA_REG,E				
     vicu#x)snd_ctl_elem_value *uVIA_REGL
{
	vicu#x)G_OESC(m*e <lnabsnd_kVIA_REG_e <l(kVIA_REG)st	unlsa-libdex,idxnabkVIA_REG->id.subFFSicele
	uVIA_REG->value.ude ger.value[0] = or vDXS_MAX_VOLUME - c <l->	- suppo 0444);[id_][0]68	uVIA_REG->value.ude ger.value[1] = or vDXS_MAX_VOLUME - c <l->	- suppo 0444);[id_][1]le	re can 068}_8DESC(index,snd_
#dES		0lcmdxs 0444);_get(vicu#x)snd_kVIA_REG *kVIA_REG,E					 vicu#x)snd_ctl_elem_value *uVIA_REGL
{
	vicu#x)G_OESC(m*e <lnabsnd_kVIA_REG_e <l(kVIA_REG)st	uVIA_REG->value.ude ger.value[0] = or vDXS_MAX_VOLUME - c <l->	- suppo 0444);_c[0]68	uVIA_REG->value.ude ger.value[1] = or vDXS_MAX_VOLUME - c <l->	- suppo 0444);_c[1]le	re can 068}_8DESC(index,snd_
#dES		0dxs 0444);_put(vicu#x)snd_kVIA_REG *kVIA_REG,E				
     vicu#x)snd_ctl_elem_value *uVIA_REGL
{
	vicu#x)G_OESC(m*e <lnabsnd_kVIA_REG_e <l(kVIA_REG)st	unlsa-libdex,idxnabkVIA_REG->id.subFFSicele	unlsa-libif
ste <s = e <l->e <s +,0x1edistdxle	unlsa-libume =ODUle	, ini,n. RM_D EF068
	#def(iDEF06 i < 2; i++) {8		ODUnabuVIA_REG->value.ude ger.value[i]68		*  (ODUn> or vDXS_MAX_VOLUME)E			ODUnab - cDXS_MAX_VOLUMEle		ODUnab - cDXS_MAX_VOLUMEt-|ODUle		. RM_Dn|abODUn!abc <l->	- suppo 0444);[id_][i]68		*  (. RM_D) {8			c <l->	- suppo 0444);[id_][i] =|ODUle			outbSODU, e <s +, - channEGS_PLAYBACK_VOLUME_L +,i)68		}e	}e	re can . RM_D68}_8DESC(index,snd_
#dES		0lcmdxs 0444);_put(vicu#x)snd_kVIA_REG *kVIA_REG,E					
vicu#x)snd_ctl_elem_value *uVIA_REGL
{
	vicu#x)G_OESC(m*e <lnabsnd_kVIA_REG_e <l(kVIA_REG)st	unlsa-libdex,idxle	unlsa-libume =ODUle	, ini,n. RM_D EF068
	#def(iDEF06 i < 2; i++) {8		ODUnabuVIA_REG->value.ude ger.value[i]68		*  (ODUn> or vDXS_MAX_VOLUME)E			ODUnab - cDXS_MAX_VOLUMEle		ODUnab - cDXS_MAX_VOLUMEt-|ODUle		*  (ODUn!abc <l->	- suppo 0444);_c[i]) {8			c RM_D EF1;
			c <l->	- suppo 0444);_c[i] =|ODUle			#def(id_DEF06,idxn< 4; idx++) {8				unlsa-libif
ste <s = e <l->e <s +,0x1edistdxle				c <l->	- suppo 0444);[id_][i] =|ODUle				outbSODU, e <s +, - channEGS_PLAYBACK_VOLUME_L +,i)68			}8		}8	}e	re can . RM_D68}_8DESC(inuonst DECLARE_TLV_DB_SCALE(db_scale0dxs, -4650, 150, 1)le
viSC(invicu#x)snd_kVIA_REGenew,snd_
#dES		0lcmdxs 0444);_VIA_REG = {8	.name== "PCMDP- suppo V444);",E	.ifacR = SifdefCTL_ELEMnIFACE_MIXER,
	.access = (SifdefCTL_ELEMn
#CESS4READWRITE |E		|||SifdefCTL_ELEMn
#CESS4TLV_READ),
	.lude =,snd_
#dES		0dxs 0444);_lude,
	.gusn=,snd_
#dES		0lcmdxs 0444);_get E	.lusn=,snd_
#dES		0lcmdxs 0444);_put E	.tlSDEF{ .lnabdb_scale0dxs }E};_8viSC(invicu#x)snd_kVIA_REGenew,snd_
#dES		0dxs 0444);_VIA_REG = {8	.ifacR = SifdefCTL_ELEMnIFACE_PCM E	.FFSiceb= 0,e	/A_.subFFSice_tusDnux8r A/
	.name== "PCMDP- suppo V444);",E	.access = SifdefCTL_ELEMn
#CESS4READWRITE |E		||SifdefCTL_ELEMn
#CESS4TLV_READ |E		||SifdefCTL_ELEMn
#CESS4IN
#defi,
	.lude =,snd_
#dES		0dxs 0444);_lude,
	.gusn=,snd_
#dES		0dxs 0444);_get E	.lusn=,snd_
#dES		0dxs 0444);_put E	.tlSDEF{ .lnabdb_scale0dxs }E};_8+ VIA/8EDESC(invoid)snd_
#dESC(_mixer_r th_(ac97bun(vicu#x)snd_(ac97bun *bunL
{
	vicu#x)G_OESC(m*e <lnabbun->	rivne _Fatast	e <l->am(d7bun = NULLle}8EDESC(invoid)snd_
#dESC(_mixer_r th_(ac9(vicu#x)snd_(ac9 *am(d)
{
	vicu#x)G_OESC(m*e <lnabam(d->	rivne _Fatast	e <l->am(d = NULLle}8EDESC(invicu#x)am(d7quirk)am(d7quirk []D= {8	{8		.subvendor ==0x1106,8		.subFFSiceb= 0x4161,8		.. 444_id EF0x56494161,  RWVT1612A  VI		.name== "Soltek SL-75fde5" E		./* R = AC9#OTUNE_NONE8	} E	{	/A_FIXME: which . 444?dhan		.subvendor ==0x1106,8		.subFFSiceb= 0x4161,8		.name== "ASRMODUK7VT2" E		./* R = AC9#OTUNE_HP_ONLY8	} E	{n		.subvendor ==0x110a,8		.subFFSiceb= 0x0079,8		.name== "Fujitsu Siemens D1289" E		./* R = AC9#OTUNE_HP_ONLY8	} E	{n		.subvendor ==0x1019,8		.subFFSiceb= 0x0a81,8		.name== "ECSUK7VTA3" E		./* R = AC9#OTUNE_HP_ONLY8	} E	{n		.subvendor ==0x1019,8		.subFFSiceb= 0x0a85,8		.name== "ECSUL7VMM2" E		./* R = AC9#OTUNE_HP_ONLY8	} E	{n		.subvendor ==0x1019,8		.subFFSiceb= 0x1841,8		.name== "ECSUK7VTA3" E		./* R = AC9#OTUNE_HP_ONLY8	} E	{n		.subvendor ==0x1849,8		.subFFSiceb= 0x3059,8		.name== "ASRMODUK7VM2" E		./* R = AC9#OTUNE_HP_ONLY	 RWVT1616dhan	} E	{n		.subvendor ==0x14cd,8		.subFFSiceb= 0x7002,8		.name== "Unknown" E		./* R = AC9#OTUNE_ALC_JACK8	} E	{n		.subvendor ==0x1071,8		.subFFSiceb= 0x8590,8		.name== "Mitac Mobo" E		./* R = AC9#OTUNE_ALC_JACK8	} E	{n		.subvendor ==0x161f,8		.subFFSiceb= 0x202b,8		.name== "Arima Notebook" E		./* R = AC9#OTUNE_HP_ONLY,8	} E	{n		.subvendor ==0x161f,8		.subFFSiceb= 0x2032,8		.name== "Targa Travell8r 811" E		./* R = AC9#OTUNE_HP_ONLY,8	} E	{n		.subvendor ==0x161f,8		.subFFSiceb= 0x2032,8		.name== "m680x" E		./* R = AC9#OTUNE_HP_ONLY,  RWhttp://launchpad.net/bugs/38546dhan	} E	{n		.subvendor ==0x12(d E		.subFFSiceb= 0xa232,8		.name== "Shuttle AK32VN" E		./* R = AC9#OTUNE_HP_ONLY8	} E	{ }  RWx8rminator A/
};_8viSC(index,snd_
#dESC(_mixer_new(vicu#x)G_OESC(m*e <l,nuonst ume =*quirk_override)
{
	sicu#x)snd_am(d7tempnux82am(dst	, in <lst	viSC(invicu#x)snd_am(d7bun_op =op == {8		., Suin=,snd_
#dESC(_ 0444_, Sui E		.readn=,snd_
#dESC(_ 0444_read E		.waitn=,snd_
#dESC(_ 0444_,ait,e	};8t	,  (( <lnabsnd_(ac97bun(e <l-> joy  0, &ops, e <l,n&e <l->am(d7bunu)n< 0)E		re can  <lst	e <l->am(d7bun->	rivne _r the=)snd_
#dESC(_mixer_r th_(ac97bunst	e <l->am(d7bun->c
MODU= e <l->am(d_cquir;8
	mem_FL(&am(d o0, tizeof(am(d))st	am(d.	rivne _Fatat=|e <lst	am(d.	rivne _r the=)snd_
#dESC(_mixer_r th_(ac9st	am(d.	ci = e <l->ecist	am(d.scap == AC9#OeCAP_SKIP_MODEM | AC9#OeCAP_POWWC IAfi;
	,  (( <lnabsnd_(ac97mixer(e <l->am(d7bun,n&am(d o&e <l->am(d))n< 0)E		re can  <lste	snd_am(d7tuneehC(dxs_s(e <l->am(d oam(d7quirk , quirk_override);8t	,  (e <l->  <l_/* R !ab	0x0c - 6860 {8		 RW, 82tlot 10/11dhan		vnd_am(dxupdA8e_bits(e <l->am(d oAC9#OEXTENDED_xTIVE	, 0x03=<<_4, 0x03=<<_4ule	}ee	re can 068}_8G_GdRM_SUPPORT_JOYSTICK8index (fJOYSTICK_ADDR	0x2008viSC(index,snd_
#dPAR_nce
e _gamee <s(vicu#x)G_OESC(m*e <l,nunlsa-libume =*legacy)
{
	sicu#x)gamee <s *glst	vicu#x)resource=*lstt	*  (!joysC(ik)E		re can -ENODEVste	r =,
#quest_
#deon(JOYSTICK_ADDR o8, " - 686)gamee <s")68	ifo(!r0 {8		FFS_,arn(e <l-> joy->ine  "canlti-NT_Frve joysC(ik e <s %#x\n" E		 
     JOYSTICK_ADDR)68		re can -EBUSYle	}ee	e <l->gamee <s = glnabgamee <s_ placA8e_	 <s()68	ifo(!gp0 {8		FFS_err(e <l-> joy->ine E			"canlti- placA8e)memoryy#defgamee <s\n")68		rel#ase_and_r th_resource(r)68		re can -ENOMEMle	}ee	gamee <s_set_name(gp, " - 686)Gamee <s")68	gamee <s_set_	hys(gp, "eci%s/gamee <s0", 	ci_name(e <l->	ci))68	gamee <s_set_FFS_tastop(gp, &e <l->	ci->ine)68	gl->de =,JOYSTICK_ADDR68	gamee <s_set_	 <s_Fata(gp, r)68
	/A_Enchannlegacy joysC(ik e <s han	*legacyn|ab - cFUNC_EN_OFF_GAMEle		ci_, Sui VIAfig_T	0x(e <l->	ci, VUScFUNC_EN_OFF,=*legacy);ee	gamee <s_
#define_	 <s(e <l->gamee <s)ste	re can 068}_8DESC(invoid)snd_
#dPAR_r th_gamee <s(vicu#x)G_OESC(m*e <l)
{
	*  (e <l->gamee <s) {8		vicu#x)resource=*lnabgamee <s_get_	 <s_Fata(e <l->gamee <s)ste		gamee <s_un
#define_	 <s(e <l->gamee <s)st		e <l->gamee <s = NULLle		rel#ase_and_r th_resource(r)68	}E}
# 5 eEviSC(indelS		ndex,snd_
#dPAR_nce
e _gamee <s(vicu#x)G_OESC(m*e <l,nunlsa-libume =*legacy)
{
	re can -ENOSYS68}_viSC(indelS		nvoid)snd_
#dPAR_r th_gamee <s(vicu#x)G_OESC(m*e <l) { }
static _8+ VIAVIA/
 viSC(index,snd_
#dES		0iein misc(vicu#x)G_OESC(m*e <l)
{
	* ini,n <l,chelsle	unlsa-libume =ODUlee	eap == e <l->  <l_/* R =ab	0x0c -  forA ? 1 :=2;8	#def(iDEF06 i < helsl i++) {8		snd_
#dES		0nel can_source.\
enum= ist		 <lnabsnd_ctl_add(e <l-> joy  snd_ctl_new1(&snd_
#dES		0nel can_source, e <l));I		i  ( <ln< 0)E			re can  <lst	}e	i  ((ac97can_*
 * (e <l->am(d)) {8		 <lnabsnd_ctl_add(e <l-> joy  snd_ctl_new1(&snd_
#dES		0dxs3_*
 * _VIA_REG, e <l));I		i  ( <ln< 0)E			re can  <lst	}e	i  (e <l->  <l_/* R !ab	0x0c - (v33A) {8		 RW,hPARno h/wWPCMD0444); VIA_REG isW#duny  , 82= no0444); VIA_REGE		 * ashll aPCMD044 VIA_REGE		 */
		vicu#x)snd_ctl_elem_ld)sidle		mem_FL(&sid o0, tizeof(sid));I		viccpy(sid.name  "PCMDP- suppo V444);");I		vid.\facR = SifdefCTL_ELEMnIFACE_MIXER;I		i  (!)snd_ctl_ex d_ld(e <l-> joy  &sid)) {8			FFS_lude(e <l-> joy->ine E				 "UsiDU = noashPCMDP- suppo\n")68			 <lnabsnd_ctl_add(e <l-> joy  snd_ctl_new1(&snd_
#dES		0lcmdxs 0444);_VIA_REG, e <l));I			i  ( <ln< 0)E				re can  <lst		}8		 5 e  RWUsiDU = no,hPARPCMDemunuxy lnisWenchandnisWr  ply weirCTRL_		{n			#def(iDEF06 i < 4; ++i) {8				vicu#x)snd_kVIA_REG *kVtUlee				kctlD= snd_ctl_new1(E					&snd_
#dES		0dxs 0444);_VIA_REG, e <l)le				i  (!kctl)E					re can -ENOMEMle				kctl->id.subFFSicem= ist				 <lnabsnd_ctl_add(e <l-> joy  kctl)le				i  ( <ln< 0)E					re can  <lst				c <l->ay, VIA_REGs[i] =|kVtUle			}8		}8	}e	/A_tulecta*
 *  Fatattlot 10/11dhan		ci_read VIAfig_T	0x(e <l->	ci, VUSED		0xcondnel t, &ODU)68	ODUnab(ODUn& ~04
ED		0xcondnSLOTG_TYP) | 04
ED		0xcondnSLOTG101168	ODUn&= ~04
ED		0xcondnDX3a  RWScond-offoashf

ARM_dhan		ci_, Sui VIAfig_T	0x(e <l->	ci, VUSED		0xcondnel t, ODU)68e	re can 068}_8viSC(index,snd_
#dPAR_iein misc(vicu#x)G_OESC(m*e <l)
{
	unlsa-libume =legacy,=legacy_cfgst	, inrFS_h EF068
	legacyn= e <l->old_legacy;
	legacy_cfgn= e <l->old_legacy_cfgst	legacyn|ab - cFUNC_MIDI_IRQ_TYP;	/A_FIXME: c  stct? (dischannMIDI) han	legacyn&= ~04
cFUNC_EN_OFF_GAMEl	/A_dischannjoysC(ik han	   (e <l->r#t in ma>ab - cha (PAR_H) {8		rFS_h EF1;I		i  (mpu_	 <sa>ab0x200) {	 RW#decnnMIDI,Aan			mpu_	 <sa&==0xfffcle				ci_, Sui VIAfig_dword(e <l->	ci, 0x18, mpu_	 <sa| 0x01)lea <dRM_CONFIG_PMnSLEEP
			c <l->mpu_	 <s_twitdn=,mpu_	 <s;
static 		}  5 e {8			mpu_	 <sa= 	ci_resource_viS<s(e <l->	ci, 2)68		}e	}  5 e {8		vwitaho(mpu_	 <s) {	 RW#decnnMIDI,Aan		case 0x300:n		case 0x310:n		case 0x320:n		case 0x330:n			legacy_cfgn&= ~(3=<<_2)68			legacy_cfgn|ab(mpu_	 <sa& 0x0030) >>=2;8			breakle		f

ARM_:			 RWno,t, 82BIOS_tustiDUs,Aan			i  (legacyn&b - cFUNC_EN_OFF_MIDI)t				mpu_	 <sa= 0x300 +,((legacy_cfgn& 0x000c)=<<_2)68			breakle		}8	}e	i  (mpu_	 <sa>ab0x200 &&_	m>
 (c <l->mpu_r st= 
#quest_
#deon(mpu_	 <s, 2  "VUSEDC(mMPU401"))
	m>
 != NULL) {8		i  (reS_h)8			legacyn|ab - cFUNC_MIDI_PNPl	/A_enchanRPCI I/O 2IA/
		legacyn|ab - cFUNC_EN_OFF_MIDI;e	}  5 e {8		i  (reS_h)8			legacyn&= ~04
cFUNC_MIDI_PNPl	/A_dischannPCI I/O 2IA/
		legacyn&= ~04
cFUNC_EN_OFF_MIDI;e		mpu_	 <sa= 0le	}ee		ci_, Sui VIAfig_T	0x(e <l->	ci, VUScFUNC_EN_OFF,=legacy);e		ci_, Sui VIAfig_T	0x(e <l->	ci, VUScPNP__STAROL,=legacy_cfg)68	ifo(c <l->mpu_r s) {8		i  (snd_mpu401_uS<senew(e <l-> joy  0, MPU4014HW_ - 686A,E					mpu_	 <s, MPU4014INFO_z>")GRATED |E					MPU4014INFO_zRQ_HOOK, -1,E					&e <l->rmidi)n< 0) {8			FFS_,arn(e <l-> joy->ine E				 "unchanRto)x inializedMPU-401 atb0x%lx  skippiDU\n" E				 mpu_	 <s);8			legacyn&= ~04
cFUNC_EN_OFF_MIDI;e		}  5 e {8			legacyn&= ~04
cFUNC_MIDI_IRQ_TYP;	/A_enchanRMIDI,tde <linuIAan		}8			ci_, Sui VIAfig_T	0x(e <l->	ci, VUScFUNC_EN_OFF,=legacy);e	}
e	vnd_
#dPAR_nce
e _gamee <s(e <l,n&legacy);eea <dRM_CONFIG_PMnSLEEP
	e <l->legacy_twitdn=,legacy;
	e <l->legacy_cfg_twitdn=,legacy_cfgststatic _	re can 068}_8 + VIA_lroc,tde <facRVIADEDESC(invoid)snd_
#dESC(_lroc_read(vicu#x)snd_lude_REGry *REGry,E				
 vicu#x)snd_lude_buff   *buff  )
{
	vicu#x)G_OESC(m*e <lnabREGry->	rivne _Fatast	* ini;
	
	snd_l	rintf(buff    "%s\n\n"  e <l-> joy->if
sname)a
	#def(iDEF06 i < 0xa06 i += 4) {8		snd_l	rintf(buff    "%02x: %08x\n" st, dnl(e <l->	 <s +,i))68	}e}8EDESC(invoid)snd_
#dESC(_lroc_x in(vicu#x)G_OESC(m*e <l)
{
	sicu#x)snd_lude_REGry *REGrystt	*  (!bsnd_cjoy_lroc_new(e <l-> joy  "G_OESC(" s&REGry))8		snd_lude_tus_text_op (REGry, e <l,nsnd_
#dESC(_lroc_readu68}_8+ VIAVIA/
 viSC(index,snd_
#dESC(_ V<l_x in(vicu#x)G_OESC(m*e <l)
{
	unlsa-libdex,ODUle	unlsa-libif
sttat_timele	unlsa-libume =pODUleea <pe  RWbrokPAR maK7M? han	   (e <l->  <l_/* R =ab	0x0c - 68608		 RWdischann plnlegacy 	 <ss,Aan			ci_, Sui VIAfig_T	0x(e <l->	ci, VUScFUNC_EN_OFF,=0)lestatic 		ci_read VIAfig_T	0x(e <l->	ci, VUS_ACLINK_xTIV  &lODU)68	*  (!b(pODUn& VUS_ACLINK_C00_READY)) { *	ic dec lti-NTady?dhan		*	ideasse<s ACLink-NT_Fs, #decnnSYNC,Aan			ci_, Sui VIAfig_T	0x(e <l->	ci, VUScACLINK_Cl t,
				
     VUScACLINK_Cl t_EN_OFF |E				||||| VUScACLINK_Cl t_RESET |E				||||| VUScACLINK_Cl t_SYNC);8		ue;
mo(1(a);Ia <p1 /A_FIXME: shouldRwe do2full NT_Fs heopt#def plne <lnm dels?,Aan			ci_, Sui VIAfig_T	0x(e <l->	ci, VUScACLINK_Cl t, 0x00);8		ue;
mo(1(a);Ia 5 eE		*	ideasse<s ACLink-NT_Fs, #decnnSYNC,(,arm AC'97-NT_Fs),Aan			ci_, Sui VIAfig_T	0x(e <l->	ci, VUScACLINK_Cl t,
				
     VUScACLINK_Cl t_RESET|VUScACLINK_Cl t_SYNC);8		ue;
mo(2)lestatic 		*	iACLink-lu,)deasse<s ACLink-NT_Fs, VSR, SGD Fatatoutdhan		*	iltie - FM Fatatoutdhashlrouhannwith2n maVRAic decs !!,Aan			ci_, Sui VIAfig_T	0x(e <l->	ci, VUScACLINK_Cl t, VUScACLINK_Cl t_INIT);8		ue;
mo(1(a);I	}8	e	/A_Make scan VRAiisWenchand, dn case we didn't do2aE	m*Vco
#dee  SXdec NT_Fs, abov82han		ci_read VIAfig_T	0x(e <l->	ci, VUScACLINK_Cl t, &lODU)68	*  ((pODUn& VUS_ACLINK_Cl t_INIT) != VUS_ACLINK_Cl t_INIT) { 		*	iACLink-lu,)deasse<s ACLink-NT_Fs, VSR, SGD Fatatoutdhan		*	iltie - FM Fatatoutdhashlrouhannwith2n maVRAic decs !!,Aan			ci_, Sui VIAfig_T	0x(e <l->	ci, VUScACLINK_Cl t, VUScACLINK_Cl t_INIT);8		ue;
mo(1(a);I	}8
	/A_waitnuntil SXdec NTady han	 at_timeDEFjiffiesp+ msecs_to_jiffies(75a);I	do2{n			ci_read VIAfig_T	0x(e <l->	ci, VUS_ACLINK_xTIV  &lODU)68		*  (pODUn& VUS_ACLINK_C00_READY) /A_primary SXdec NTady han			breakle		sah_Cuia_timeout_untde <linuihan(1ule	} whiann(time_be#dee(