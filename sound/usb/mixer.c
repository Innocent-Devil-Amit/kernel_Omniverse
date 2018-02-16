/*
 *   (Tentative) USB Audio Driver for ALSA
 *
 *   Mixer control part
 *
 *   Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 *
 *   Many codes borrowed from audio.c by
 *	    Alan Cox (alan@lxorguk.ukuu.org.uk)
 *	    Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
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
 * TODOs, for both the mixer and the streaming interfaces:
 *
 *  - support for UAC2 effect units
 *  - support for graphical equalizers
 *  - RANGE and MEM set commands (UAC2)
 *  - RANGE and MEM interrupt dispatchers (UAC2)
 *  - audio channel clustering (UAC2)
 *  - audio sample rate converter units (UAC2)
 *  - proper handling of clock multipliers (UAC2)
 *  - dispatch clock change notifications (UAC2)
 *  	- stop PCM streams which use a clock that became invalid
 *  	- stop PCM streams which use a clock selector that has changed
 *  	- parse available sample rates again when clock sources changed
 */

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/hwdep.h>
#include <sound/info.h>
#include <sound/tlv.h>

#include "usbaudio.h"
#include "mixer.h"
#include "helper.h"
#include "mixer_quirks.h"
#include "power.h"

#define MAX_ID_ELEMS	256

struct usb_audio_term {
	int id;
	int type;
	int channels;
	unsigned int chconfig;
	int name;
};

struct usbmix_name_map;

struct mixer_build {
	struct snd_usb_audio *chip;
	struct usb_mixer_interface *mixer;
	unsigned char *buffer;
	unsigned int buflen;
	DECLARE_BITMAP(unitbitmap, MAX_ID_ELEMS);
	struct usb_audio_term oterm;
	const struct usbmix_name_map *map;
	const struct usbmix_selector_map *selector_map;
};

/*E-mu 0202/0404/0204 eXtension Unit(XU) control*/
enum {
	USB_XU_CLOCK_RATE 		= 0xe301,
	USB_XU_CLOCK_SOURCE		= 0xe302,
	USB_XU_DIGITAL_IO_STATUS	= 0xe303,
	USB_XU_DEVICE_OPTIONS		= 0xe304,
	USB_XU_DIRECT_MONITORING	= 0xe305,
	USB_XU_METERING			= 0xe306
};
enum {
	USB_XU_CLOCK_SOURCE_SELECTOR = 0x02,	/* clock source*/
	USB_XU_CLOCK_RATE_SELECTOR = 0x03,	/* clock rate */
	USB_XU_DIGITAL_FORMAT_SELECTOR = 0x01,	/* the spdif format */
	USB_XU_SOFT_LIMIT_SELECTOR = 0x03	/* soft limiter */
};

/*
 * manual mapping of mixer names
 * if the mixer topology is too complicated and the parsed names are
 * ambiguous, add the entries in usbmixer_maps.c.
 */
#include "mixer_maps.c"

static const struct usbmix_name_map *
find_map(struct mixer_build *state, int unitid, int control)
{
	const struct usbmix_name_map *p = state->map;

	if (!p)
		return NULL;

	for (p = state->map; p->id; p++) {
		if (p->id == unitid &&
		    (!control || !p->control || control == p->control))
			return p;
	}
	return NULL;
}

/* get the mapped name if the unit matches */
static int
check_mapped_name(const struct usbmix_name_map *p, char *buf, int buflen)
{
	if (!p || !p->name)
		return 0;

	buflen--;
	return strlcpy(buf, p->name, buflen);
}

/* check whether the control should be ignored */
static inline int
check_ignored_ctl(const struct usbmix_name_map *p)
{
	if (!p || p->name || p->dB)
		return 0;
	return 1;
}

/* dB mapping */
static inline void check_mapped_dB(const struct usbmix_name_map *p,
				   struct usb_mixer_elem_info *cval)
{
	if (p && p->dB) {
		cval->dBCTO_name_map *s:
 *
 *  - support for UAC =*
 *  itiiELEMport for Uax =*
 *  itiaxLEMport fode <ids (Uconvoidnammatches */
static int availabl 0x03,t
checstruct usbmixame_map *p,
		4 eXtensichar  int control)
{
	const struct usbmix_nal)
{
	ial Pgnedic x!p->name)
		return 0;

	buflctor_map;
};

/*E-mu 0202/0404/020 state->ma== unit 0202/0404/0ing */
static if (p->id == unit4 eXtension U&
		    (!control || !p->control || conedic x <NULL;
untget the mappheck whether the cons[dic x]trol shouldname if the0id checixert un, a audio sine intd_name Free e giANTAd_namiude <luct usbix_na*t uni_name_sine in_d_na  int control)
{
	const stral)
{
	ialuflen;
	DECLARd_na	buflchewe je cen clocbut Weadr nametruct usbac_feaf te_d_na_descriplabl*hdrid +) {
		iangged((hdrid _interfat unidesc(== unit_BITMA, == unit_BIsho, hdrral)
{soft DT_CS_INOCKFACE)) !d +) {ontrol || hdrit_LengFre>= 4control ==hdrit_DescriplabSubonfie>= e a_INPUT_OCKMINALcontrol ==hdrit_DescriplabSubonfie<= e a2_SAMPLE/
	USBCONVEROCKcontrol ==hdrit_K_RAIDontrol |get the mapphdrldnamme if the unit matchixer
 *  a ude <lme Free e giANTAiude <luct usbetur_interfa
 * _ude <lidesc(==nt control)
{
	const stral)
{
	iaPgnedic x!p->name)
		returiax;

	bufletur;

id erfaude <l(== uniter;
 * ev,edic x!p)
		riax;

 (U1uldn)
	[;

]anuac inline vD_ELEmatchixer
 ling @lxorge e byte/word o_maps descriplablInc., 5zero-basmap, Megerde <luct usbetur
 ling _len;
	_rt ur  int co p->dB) {
		cval->dBCTO_n	returO_name_mse Fed
(ort fort _onfiontrocasmer c_MIXER_BOOLEAN:ng */
stat!!rt ;rocasmer c_MIXER_INV_BOOLEAN:ng */
stat!rt ;rocasmer c_MIXER_U8:ng rt  &nualffLEMpbreak;rocasmer c_MIXER_S8:ng rt  &nualffLEMp || rt  >nual80get trt  -nual100LEMpbreak;rocasmer c_MIXER_U16:ng rt  &nualffffLEMpbreak;rocasmer c_MIXER_S16:ng rt  &nualffffLEMp || rt  >nual8000get trt  -nual10000LEMpbreak;roame if thert ;rmatchixer
 ling @lxorge e zero-basmap, MlInc., 5byte/word f (paps descriplabde <luct usbetur
 ling _bytes_rt ur  int co p->dB) {
		cval->dBCTO_n	returO_name_mse Fed
(ort fort _onfiontrocasmer c_MIXER_BOOLEAN:ng */
stat!!rt ;rocasmer c_MIXER_INV_BOOLEAN:ng */
stat!rt ;rocasmer c_MIXER_S8:ngcasmer c_MIXER_U8:ng  if thert  &ualffLEMcasmer c_MIXER_S16:ngcasmer c_MIXER_U16:ng  if thert  &ualffffLEMame if the0i chenot reachctl(co}
luct usbeturs *_rel   Mi_rt ur  int co p->dB) {
		cval->dBCTO_n	returO_name_me->maort fores)EMport foresonvoidn || rt  < ort foiiEing */
static ielseck_m rt  >nuort foiaxing */
stat(ort foiax (Uort foiiE + ort foreso(U1u / ort foresc ielseng */
stat(rt  - ort foiiEi / ort foresc }
luct usbeturs *_abs_rt ur  int co p->dB) {
		cval->dBCTO_n	returO_name_m || rt  < 0ing */
statort foiiE;_me->maort fores)EMport foresonvoidnrt  *= ort foresc irt  +=tort foiiE;_me->mrt  >uort foiaxing */
statort foiax;me if thert ;rmattchixer if"miv  	-the part urde <liuct usbeturs *_
	i_rt ur_v1  int co p->dB) {
		cval->dBCTO_n	returrequestral)

	iaPgnee a cx	retur*rt ur_ ifame_ms_mixer_interface *mixer;
 =tort foii) {iter;
t buflen;
	DECLAR)
	[2]p;

strrt _;

id ort fort _onfie>= e c_MIXER_S16 ? 2 :voidn  MlIimeMERCnvo0;nsigned x =*0, err
		ierrid _interfaaulabesuar *rt foii) {iter;
);_me->merri< 0ing */
stat-EIO
		idown_ iad(&er;
 *shutdown_ wsem);_mangged(IimeMER-- >u0ontrol || er;
 *shutdownget tbreak;ro	d x =*_interfa
eckfer;f er;
) |t(ort fo_na<< 8)LEMp || _interfa
el_msg(er;
 * ev,eerfarcv
eckpipe(er;
 * ev,e0),rrequestral)


	iae c_RECIP_INOCKFACE |te c_TYPSBCLASS |te c_DIR_INral)


	iae a cx	redx!p)
		rrt _;

)e>= rt _;

)etrol	*rt ur_ ifid o ling _len;
	_rt ur TO_n	r_interfa
 mbusb_bytes()
		rrt _;

))LEMpierrid 0LEMp	goIncMERLEMp}EMamebmix_name_dbg(er;
ral)"cannot s */
elart ur:rreqid %#x!pwVt urid %#x!pwIic x d %#x!ponfied %d\n",ng */questrae a cx	redx!port fort _onfio;	ierrid -EINVA{
		cMER:ngup_ iad(&er;
 *shutdown_ wsem);_m_interfaaulasuspend *rt foii) {iter;
);_m*/
staterr
	}
luct usbeturs *_
	i_rt ur_v2  int co p->dB) {
		cval->dBCTO_n	returrequestral)

	iaPgnee a cx	retur*rt ur_ ifame_ms_mixer_interface *mixer;
 =tort foii) {iter;
t buflen;
	DECLAR)
	[2 + 3xers (Uof(__u16)]i cheenough spnsigf (poned
 CM sametuflen;
	DECLARErt ;roigned x =*0,  if,rs (U;ro__u8 bRequeststate->mrequestontre a_GET_CUR)etrolbRequest = e a2_CS_CURLEMps (U =*_ (Uof(__u16)LEMa elsectrolbRequest = e a2_CS_AC2)
LEMps (U =*_ (Uof()
	)ldnammememset()
		r0,*_ (Uof()
	));mme ifid _interfaaulabesuar *r;
) ?t-EIO :v0;ate->mre|get goIncerror
		idown_ iad(&er;
 *shutdown_ wsem);_m || er;
 *shutdowngctrol ifid -ENODEVLEMa elsectrold x =*_interfa
eckfer;f er;
) |t(ort fo_na<< 8)LEMp ifid _interfa
el_msg(er;
 * ev,eerfarcv
eckpipe(er;
 * ev,e0),rbRequestral)

	iaiae c_RECIP_INOCKFACE |te c_TYPSBCLASS |te c_DIR_INral)

	iaiae a cx	redx!p)
		r_ (Uuldnameup_ iad(&er;
 *shutdown_ wsem);_m_interfaaulasuspend *r;
);_ate->mre|i< 0ictrerror:ng bmix_name_err(er;
ral))"cannot s */
elart ur:rreqid %#x!pwVt urid %#x!pwIic x d %#x!ponfied %d\n",ng  */questrae a cx	redx!port fort _onfio;	im*/
stat*/
ldnammecheFIXME: howt
check_we *  - eck change f"mngets here?e <limse Fed
(*/questontrocasmer a_GET_CUR:ng rt  =p)
	LEMpbreak;rocasmer a_GET_MIN:ng rt  =p)
	 + _ (Uof(__u16)LEMpbreak;rocasmer a_GET_MAX:ng rt  =p)
	 + _ (Uof(__u16)xer2LEMpbreak;rocasmer a_GET_RES:ng rt  =p)
	 + _ (Uof(__u16)xer3LEMpbreak;rodefa ch:ng  if the-EINVA{
	namme*rt ur_ ifid o ling _len;
	_rt ur TO_n	r_interfa
 mbusb_bytes(O_n	r_ (Uof(__u16)));mme if the0id chuct usbeturs *_
	i_rt ur  int co p->dB) {
		cval->dBCTO_n	returrequestral)

Pgnee a cx	retur*rt ur_ ifame_me a cx +=tort fo cx_offLE
 */
stat(ort foii) {itproIncturn Nr a_VERSION_1u ?et g *_
	i_rt ur_v1 TO_n	r*/questrae a cx	rrt ur_ ifa :et g *_
	i_rt ur_v2 TO_n	r*/questrae a cx	rrt ur_ ifaid chuct usbeturs *_
ur_
	i_rt ur  int co p->dB) {
		cval->dBCTO_n	al)

	iaiPgnee a cx	retur*rt urame_m*/
stats *_
	i_rt ur TO_n	rr a_GET_CURrae a cx	rrt uruld be ignomple r=*0: ma con, 1r=*firsnt name;
l(const struct usbmixrs *_
ur_E-muraw  int co p->dB) {
		cval->dBCTO_n	al)
	  	int name;
	retur*rt urame_m*/
stats *_
	i_rt ur TO_n	rr a_GET_CURral)

	iai(ort fosine int<< 8) |t name;
	al)

	iairt uruld beuct usbeturs *_
ur_E-murt ur  int co p->dB) {
		cval->dBCTO_n	al)

	iaiPgne name;
	returdic x!petur*rt urame_meturerr
		i || ert fosachctl& (1t<<  name;
)gctrol*rt ur =tort fosachcurt [dic x];	im*/
stat0ldnameerrid s *_
ur_E-muraw TO_n	r name;
	rrt uruldme->merri< 0introl || !ort foii) {itap *p)_
	i_errorget tbmix_name_dbg(ert foii) {iter;
	al)
	"cannot s */
urregnee auigf (psine int%	DECt%	: errid %d\n",ng  

	iaiaort fosine in	r name;
	rerro;	im*/
staterr
	nameert fosachctl|nvot<<  name;
;meert fosachcurt [dic x]id *rt ur;me if the0id checixerM in	-the part urde <lietur_interfadB) {
s *_
	i_rt ur  int co p->dB) {
		cval->dBCTO_n	ng  
eturrequestriPgnee a cx	returrt ur_sifame_ms_mixer_interface *mixer;
 =tort foii) {iter;
t buflen;
	DECLAR)
	[2]p;

strd x =*0, rt _;

	rerr,lIimeMERCnvo0;n_me a cx +=tort fo cx_offLE
  || ert foii) {itproIncturn Nr a_VERSION_1u {ng rt _;

id ort fort _onfie>= e c_MIXER_S16 ? 2 :voidna elsect cher a_VERSION_2sametecheaudio slass v2psine in usbm always 25bytexer_ms (U ametert _;

id _ (Uof(__u16)LEetecheFIXME ametee->mrequesto! Nr a_SET_CUR)etrolebmix_name_dbg(er;
r "AC2)
 sift <lmnot y in RANGE ed\n")LEMpi if the-EINVA{
	nnamme	request = e a2_CS_CURLEM}n_me aur_sifid o ling _bytes_rt ur TO_n	rrt ur_sifaldn)
	[0]id e aur_sifi&ualffLEM)
	[1]id (e aur_sifi>> 8) &ualffLEMerrid _interfaaulabesuar *r;
);_me->merri< 0ing */
stat-EIO
	idown_ iad(&er;
 *shutdown_ wsem);_mangged(IimeMER-- >u0ontrol || er;
 *shutdownget tbreak;ro	d x =*_interfa
eckfer;f er;
) |t(ort fo_na<< 8)LEMp || _interfa
el_msg(er;
 * ev,ng  

	iaerfaund
eckpipe(er;
 * ev,e0),rrequestral)


	iae c_RECIP_INOCKFACE |te c_TYPSBCLASS |te c_DIR_OUTral)


	iae a cx	redx!p)
		rrt _;

)e>= 0ontrolierrid 0LEMp	goIncMERLEMp}EMamebmix_name_dbg(er;
r "cannot s */
elart ur:rreqid %#x!pwVt urid %#x!pwIic x d %#x!ponfied %d, data d %#x/%#x\n",ng 
	iaia*/questrae a cx	redx!port fort _onfi!p)
	[0]!p)
	[1]o;	ierrid -EINVA{
		cMER:ngup_ iad(&er;
 *shutdown_ wsem);_m_interfaaulasuspend *r;
);_m*/
staterr
	}
luct usbeturs *_
ur_
	i_rt ur  int co p->dB) {
		cval->dBCTO_n	al)

	iaiPgnee a cx	returrt urame_m*/
stat_interfadB) {
s *_
	i_rt ur TO_n	rr a_SET_CURrae a cx	rrt uruld beuct usbeturs *_
ur_E-murt ur  int co p->dB) {
		cval->dBCTO_n	iPgne name;
	al)

	iaiPgnedic x!peturrt urame_meturerr
	unitbitmap, MA iad_onlyid (nomple r== 0on?EMport foma con_ iadonlyi:EMport foch_ iadonlyi& (1t<< (nomple r(U1u);_ate->mread_onlyontrolbmix_name_dbg(ert foii) {iter;
	al)

	iaia"%s():t name;
l%	Datchine int%	DisA iad_only\n",ng  iaia__func__	r name;
	rort fosine ino;	im*/
stat0LEM}n_merrid _interfadB) {
s *_
	i_rt ur TO_n	al)
{s rr a_SET_CURra(ort fosine int<< 8) |t name;
	al)
{s rrt uruldme->merri< 0i	im*/
staterr
	nert fosachctl|nvot<<  name;
;meert fosachcurt [dic x]id rt ur;me if the0id checixerTLV callbackhave rhe paroluarpsine in de <letur_interfadB) {
rol_tlv(s_mixer_intksine int*ksine in	returop_flag,ng  initbitmap, MA_ (U,initbitmap, MA__usr na_tlvame_ms_mixer p->dB) {
		cval->dBCTO_nid ksine initprivate_data;EMS);
	strTLV_Dc_MINMAX(scale	r0,*0);_ate->ms (U < _ (Uof(scale)ing */
stat-ENOMEM;_m_cale[2]id ort for UAC;_m_cale[3]id ort for Uax;me || e * _to_usr (_tlv	r_cale	r_ (Uof(scale)iing */
stat-EFAULT;me if the0id checixern clor rMER uss begr_mhere...de <liuct usbeturn clox_name_d_na  int control)
{
	const struct usbmix_);_atchixer
ored k_mappeinput/MERputt name;
lrMER ugDisAenrcesd o_me e giANTAio_ter.de 
 * dhave rhe pad_namn clorde <luct usbetur
e_map *tr-muio_ter(uflen;
	DECLARE_termal)

	iaiaiPgnedch	returoch	returnum_MERsame_meturd x =*ged
*rnum_MERs +roch;me if the_ter[d x >> 3]i& (al80 >> (d x & 7))id checixer/
#i a alsachine int		cvegnixerM ared
 addincrcvegnmappeinc x untili a empty slot  it cludopy of tOR =progd, giAN up
 add and/c inline intinuctnce.de <lietur_interfadB) {
/
#_sine in  int co p->dB) {
er;
	unsigned inmal)

	iaias_mixer_intksine int*kstlame_ms_mixer p->dB) {
		cval->dBCTO_nid kstnitprivate_data;EMeturerr
		iangged(_int
	i_t uniid(ii) {iter;
fosard, &kstnitx_)ing kstnitx_.inc x++;me || (errid _int
	i_/
#(ii) {iter;
fosard, kstla)i< 0introlbmix_name_dbg(ii) {iter;
	 "cannot /
#isine int(errid %d)\n",ng  iaia rerro;	im*/
staterr
	nameert fo		cvalconv&kstnitx_;meert fonextalc
		cvonvii) {itac
		cvs[ort fo_n]p;
ii) {itac
		cvs[ort fo_n]id ort ;me if the0id checixerg in	-ap;
in_ni
checude <lde <liuct usbs_mixerxer melem_i
 mbo	unsigneonfig;
ECLAREuct mixrxer melem_s[]id uns{ual m00,*"OERput" },ns{ual m01,*"Speaker" },ns{ual m02,*"Hiadphone" },ns{ual m03,*"HMDontrol" },ns{ual m04,*"DeskctorSpeaker" },ns{ual m05r "AoomrSpeaker" },ns{ual m06r "ComrSpeaker" },ns{ual m07r "LFE" },ns{ual 600,*"Exer n_niIn" },ns{ual 601,*"An_nogiIn" },ns{ual 602,*"Digit_niIn" },ns{ual 603,*"Line" },ns{ual 604,*"LegacyiIn" },ns{ual 605r "IEC958iIn" },ns{ual 606r "1394 DA S has " },ns{ual 607r "1394 DV S has " },ns{ual 700,*"Embedded" },ns{ual 701,*"Noisemplx03," },ns{ual 702,*"Eands (which Noise" },ns{ual 703,*"CD" },ns{ual 704,*"DAT" },ns{ual 705r "DCC" },ns{ual 706r "MiniDisk" },ns{ual 707,*"An_nogiTape" },ns{ual 708,*"PhonoEM se" },ns{ual 709,*"VCRontrol" },ns{ual 70a,*"Video Diskontrol" },ns{ual 70br "DVDontrol" },ns{ual 70cr "TV Tun pantrol" },ns{ual 70d,*"Satell
 */Recantrol" },ns{ual 70e,*"Crces Tun pantrol" },ns{ual 70fr "DSS ntrol" },ns{ual 710,*"Radio Ric Licr" },ns{ual 711,*"Radio Transmittcr" },ns{ual 712,*"M cha-Traed Ricordcr" },ns{ual 713,*"Sync i_ (Ur" },ns{ua },nxer_but usbeturs *_er melem_  int control)
{
	const struruct usbmix_name_map **xer m,ng  initbitmapECLAREuct 	returiax;

	returer meonlyome_ms_mixerxer melem_i
 mbo	Euct s;_ate->mxer mn strlcpy(buf, p-_interfa
 * _ude <lidesc(== strucer mn strl	al)
{s	uct 	riax;

	;mmechevirtixeronfie-mnot aA ial-ap;
in_niamete->mxer mn onfie>> 16)xtrol || er meonlyom	im*/
stat0LEMmse Fed
(xer mn onfie>> 16)xtrolcasmer a_
/*
 * m_UNIT:EMpms_m wheuct 	r"S eXtens")LEMpi if the8;rolcasmer a1_PROCESSING_UNIT:EMpms_m wheuct 	r"ProcessCK_RA")LEMpi if the12;rolcasmer a1_EXTENSION_UNIT:EMpms_m wheuct 	r"ExeCK_RA")LEMpi if the8;rolcasmer a_MIXER_UNIT:EMpms_m wheuct 	r"c) 20")LEMpi if the5LEMpdefa ch:ng (buf, p-_prir;f uct 	r"K_RA %d"rucer mn x_);_Mp}EMam
mse Fed
(xer mn onfie&ualff00ontrocasmeOFT_00:ng s_m wheuct 	r"PCM"o;	im*/
stat3;rocasmeOFT200:ng s_m wheuct 	r"Mic"o;	im*/
stat3;rocasmeOFT400:ng s_m wheuct 	r"Hiads *"o;	im*/
stat7;rocasmeOFT500:ng s_m wheuct 	r"Phone"o;	im*/
stat5LEM}n_mf (p-s in u=rxer melem_s;es in n onfi;es in !control || s in n onfir== xer mn onfiontrolis_m wheuct 	rs in n strlcLEMpi if thehecken(s in n strlcLEMp}dnamme if the0id checixern cloT_SELE0x03,td_namrecursLicly untiliit reachc aren	-ap;
in_nixer (pa branchctld_na.de <luct usbetur
e_mapinput_map   int control)
{
	const struct ux_nal)
iaias_mixerbmix_name_map **map ame_meturerr
	uix_na*p1;mmememset(er m,r0,*_ (Uof(*map a);_mangged((p1r=*fiuni_name_sine in_d_na  i strucd)) !d +) {ontrolnitbitmapECLAREhdrid p1;m		er mn x_u=rxdLEMmse Fed
(hdr[2])xtrolcasmer a_INPUT_OCKMINAL:ng (e->ms= unitii) {itproIncturn Nr a_VERSION_1u {ng 	truct usbac_input_map in_n_descriplabl*did p1;m				er mn onfied le16_to_cpu(d->wTap in_nTnfio;	im		er mn  name;
}ed d->bNrCname;
};

m		er mn  name_maed le16_to_cpu(d->wCname;
Cme_mao;	im		er mn 
checd d->iTap in_n;	im	a elsect cher a_VERSION_2samete	truct usbac2_input_map in_n_descriplabl*did p1;m				er mn onfied le16_to_cpu(d->wTap in_nTnfio;	im		er mn  name;
}ed d->bNrCname;
};

m		er mn  name_maed le32_to_cpu(d->bmCname;
Cme_mao;	im		er mn 
checd d->iTap in_n;		im		 ignallmrecursLicly rens */
sta parse availabssamete	terrid 
e_mapinput_map   i strud->bCplx03,ID,-ap;
o;	im		e->merri< 0i	im	im*/
staterr
	nMp}dnim*/
stat0LEMmcasmer a_FEATURE_UNIT: {ng 	MIT_SELWeadr ns arhs chaigf (pv1
 addv2samete	ruct usbac_feaf te_d_na_descriplabl*did p1;m			x_u=rd->bplx03,ID;et tbreak; chesineinu, Incn cloTamete}rolcasmer a_MIXER_UNIT:ntrolis_mt usbac_ntrol)d_na_descriplabl*did p1;m			er mn onfied dit_DescriplabSubonfie<< 16; chevirtixeronfieamete	er mn  name;
}ed bac_ntrol)d_na_bNrCname;
}(_);_Mp	er mn  name_maed bac_ntrol)d_na_wCname;
Cme_ma(d, == unitii) {itproInctu);_Mp	er mn 
checd bac_ntrol)d_na_ic) 20(_);_Mp	*/
stat0LEMm}rolcasmer a_
/*
 * m_UNIT:EMpcasmer a2k source/*
 * m:ntrolis_mt usbac_4 eXtensid_na_descriplabl*did p1;m			 ignallmrecursLicly ren if"miv  
sta name;
ll->dBC/
e	terrid 
e_mapinput_map   i strud->baplx03,ID[0]!pap;
o;	im	e->merri< 0i	im	i*/
staterr
	nMper mn onfied dit_DescriplabSubonfie<< 16; chevirtixeronfieamete	er mn x_u=rxdLEMm	er mn 
checd bac_4 eXtensid_na_iS eXtens(_);_Mp	*/
stat0LEMm}rolcasmer a1_PROCESSING_UNIT:EMpcasmer a1_EXTENSION_UNIT:EMpcher a2_PROCESSING_UNIT_V2sametecher a2_EFFING	UNITsametecasmer a2kEXTENSION_UNIT_V2:ntrolis_mt usbac_process <lid_na_descriplabl*did p1;mng (e->ms= unitii) {itproIncturn Nr a_VERSION_2contro		hdr[2]rn Nr a2_EFFING	UNITu {ng 	tcher a2/r a1td_namIDs oicrlapmhereer_manal)


*td_* amt uces way. Ip *p)ee Sofd_namf (pnow.al)


*/	im	i*/
stat0
	nMp}dng (e->md->bNrInPinsu {ng 	tx_u=rd->baplx03,ID[0];	im		break; chesineinu, Incn cloTametep}dnimer mn onfied dit_DescriplabSubonfie<< 16; chevirtixeronfieamete	er mn  name;
}ed bac_process <lid_na_bNrCname;
}(_);_Mp	er mn  name_maed bac_process <lid_na_wCname;
Cme_ma(d, == unitii) {itproInctu);_Mp	er mn 
checd bac_process <lid_na_iProcess <l(d, == unitii) {itproInctu);_Mp	*/
stat0LEMm}rolcasmer a2k source*/
	U:ntrolis_mt usbac_ pars_E0x03,_descriplabl*did p1;m			er mn onfied dit_DescriplabSubonfie<< 16; chevirtixeronfieamete	er mn x_u=rxdLEMm	er mn 
checd d->iCparsplx03,;_Mp	*/
stat0LEMm}roldefa ch:ng (buf, p--ENODEVLEMp}EMamebuf, p--ENODEVLE checixerFeaf teCK_RAand the  feaf tefd_namline intin= 0x03ich  <lucmixerbmixfeaf te_sine in_l->dB{flctor_mECLAREuct miunitbitmap, MAonfi;	chesine intonfie(mustruroluar, etc.)names
 *uct usbs_mixerbmixfeaf te_sine in_l->dB_name_feaf te_l->d[]id uns{u"Must",	{soft MIXER_INV_BOOLEAN },ns{u"Voluar",	{soft MIXER_S16 },ns{u"TonedCine int- Bass",	r c_MIXER_S8 },ns{u"TonedCine int- Mid",	{r c_MIXER_S8 },ns{u"TonedCine int- Treces",	r c_MIXER_S8 },ns{u"GM set  Eands (UA",	{r c_MIXER_S8 }, cheFIXME: not TY oemeous,y inamet{u"AuIncGncluCine in",	{r c_MIXER_BOOLEAN },ns{u"DelayuCine in",	{r c_MIXER_U16 }, cheFIXME: U32er_maphicamet{u"Bass Boost",	{soft MIXER_BOOLEAN },ns{u"Loudness",	{soft MIXER_BOOLEAN },nscher a2 specams camet{u"InputcGncluCine in",	{r c_MIXER_S16 },ns{u"InputcGncluPaduCine in",	r c_MIXER_S16 },ns{u"PhasmeIling of Cine in",	r c_MIXER_BOOLEAN },ns
 * i private_ and/callbackh <luct usbix_na p->dB) {
		cva and(s_mixer_intksine int*kstlame_mk and(kstnitprivate_data);_Mkstnitprivate_dataid +) {
	 checixerer;
	unsigInc  Coesine intf (pfeaf te/rhe pad_nasand the  roluarpsine in MAX_IDh <luct usbix_naroluar_sine in_MAX_ID  int co p->dB) {
		cval->dBCTO_n	al)
	  s_mixer_intksine int*kstlame_ms_mixer_interface *mixer;
 =tort foii) {iter;
t bse Fed
(or;
foerfacd)ntrocasmer c_ID(al 763, 0x2030): cheM-ntrol Far_mTraed C400cametcasmer c_ID(al 763, 0x2031): cheM-ntrol Far_mTraed C600camet(e->ms=rcmp(kstnitx_.uct 	r"El equaDur03ich")r== 0ontroliort foiiE nual 000LEMpport fomax =*0lffffLEMpport foresonval 0e6;et tbreak;EMm}role->ms=rcmp(kstnitx_.uct 	r"El equaVoluar")r== 0 ||
)
iaias_mcmp(kstnitx_.uct 	r"El equaFeedbackhVoluar")r== 0ontroliort foiiE nual 0LEMpport fomax =*0lff;et tbreak;EMm}role->ms=rs=r(kstnitx_.uct 	r"El equaRuf, p") !d +) {ontroliort foiiE nualb706LEMpport fomax =*0lff7bLEMpport foresonval 073;et tbreak;EMm}role->mms=rs=r(kstnitx_.uct 	r"PlaybackhVoluar")r!d +) {on||
)
	ms=rs=r(kstnitx_.uct 	r"El equaSend")r!d +) {oontroliort foiiE nualb5fb; che-73k_manualb6ffeamete	ort fomax =*0lfcfeLEMpport foresonval 073;et }rolbreak;Eetcasmer c_ID(al 763, 0x2081): cheM-ntrol Far_mTraed Ultra 8Rcametcasmer c_ID(al 763, 0x2080): cheM-ntrol Far_mTraed Ultra amet(e->ms=rcmp(kstnitx_.uct 	r"El equaDur03ich")r== 0ontrolibmix_name_l->d(er;
ral))

	iaiai"s */MAX_Itf (pFTU El equaDur03ich\n")LEMpiort foiiE nual 000LEMpport fomax =*0l7f00LEMpport foresonval 100LEMptbreak;EMm}role->ms=rcmp(kstnitx_.uct 	r"El equaVoluar")r== 0 ||
)
iaias_mcmp(kstnitx_.uct 	r"El equaFeedbackhVoluar")r== 0ontrolibmix_name_l->d(er;
ral))

	iaiai"s */MAX_Istf (pFTU El equaFeedback/Voluar\n")LEMpiort foiiE nual 0LEMpport fomax =*0l7fLEMptbreak;EMm}rolbreak;Eetcasmer c_ID(al 471,val 101):etcasmer c_ID(al 471,val 104):etcasmer c_ID(al 471,val 105):etcasmer c_ID(al 672,ual1041):etcheMAX_Itf (pUDA1321/N101.al henotHOUT ANdeouc3ich between*firmhe Li2.1.1.7 (N101)al he add distr2.1.1.21ns anot ingy cleLARlxorgdatasheets.al heI WITHOUT ANand thnee auigs a-15360mf (pnew
 *
irmhe Li--jk
	 amet(e->m!s_mcmp(kstnitx_.uct 	r" thaPlaybackhVoluar")rontrol ==ort foiiE n=a-15616)xtrolibmix_name_l->d(er;
ral))

"s */roluarpMAX_Itf (pUDA1321/N101 er;
\n")LEMpiort foiax =*-256;EMm}rolbreak;Eetcasmer c_ID(al 46d,val 9a4):et(e->m!s_mcmp(kstnitx_.uct 	r"MusbCapf tefVoluar"))xtrolibmix_name_l->d(er;
ral))
"s */roluarpMAX_Itf (pQuickCam E3500\n")LEMpiort foiiE nu6080LEMpiort foiax =*8768LEMpport foresonv192;EMm}rolbreak;Eetcasmer c_ID(al 46d,val 807): cheLogiouch Webcam C500cametcasmer c_ID(al 46d,val 808):etcasmer c_ID(al 46d,val 809):etcasmer c_ID(al 46d,val 819): cheLogiouch Webcam C210cametcasmer c_ID(al 46d,val 81b): cheHD Webcam c310cametcasmer c_ID(al 46d,val 81d): cheHD Webcam c510cametcasmer c_ID(al 46d,val 825): cheHD Webcam c270cametcasmer c_ID(al 46d,val 826): cheHD Webcam c525cametcasmer c_ID(al 46d,val 8ca): cheLogiouch Quickcam Fusich  <ltcasmer c_ID(al 46d,val 991):etcheMosteaudio aps devis.h>lie abMERCroluarpresoluted inl heMosteLogiouch webcamsal Publisonv384inl heProboly rhereers soarplogiouch magusbbehiadd ts anumb
 *--fishor
	 amet(e->m!s_mcmp(kstnitx_.uct 	r"MusbCapf tefVoluar"))xtrolibmix_name_l->d(er;
ral))
"s */resoluted pMAX_I: ort foresonv384\n")LEMpiort foresonv384;EMm}rolbreak;Eetcasmer c_ID(al11
/*
al1620): cheLogiouch Speakers S150cametchee thaaudio devis.mple 2t name;
}
 addit exmbigitly */quiresor mol hehosteIncsend SET_CURnterrupt o_me e roluarpsine in ofrfaces:
 ol he name;
}. 7936 =*0l1F00cs arhs defa chee aui.
	 amet(e->mort fochame;
}ed= 2i	im	_interfadB) {
s *_
	i_rt ur TO_n	rr a_SET_CURr	im	im	(ort fosine int<< 8) |t2,u7936)LEMpbreak;rdnammatchixer if"miv  and thnimum
 addiaximum
rt urstf (p_SELECecamse#isine inde <luct usbeturs *_thn_iax_e Fr_MAX_ID  int co p->dB) {
		cval->dBCTO_n	al)
	  beturdefa ch_thn, s_mixer_intksine int*kstlame_me  f (pf a cafieametort foiiE nudefa ch_thn;
iort foiax =*ort foiiE + 1;
iort foresonvoidnort for UAC =*ort for Uax =*0LE
  || ert fort _onfie== e c_MIXER_BOOLEAN ||
)l ==ort fort _onfie== e c_MIXER_INV_BOOLEAN- support fode <ids (Uconvoidna elsectroldturiinchnid 0LEMpe->mort focmask)xtrolict uxLEMpif (p-iid 0L i < oterAR PNELSL i++i	im	ie->mort focmaski& (1t<< i))xtroli		iinchnid i + 1;
i	Mptbreak;EMmMm}rol}role->ms *_
	i_rt ur TO_n	rr a_GET_otera(ort fosine int<< 8) |tiinchn, &ort foiaxii< 0 ||
)
iaias *_
	i_rt ur TO_n	rr a_GET_oINra(ort fosine int<< 8) |tiinchn, &ort foiiEi < 0ontrolibmix_name_err(ert foii) {iter;
	al)
	
	iaia"%d:%	: cannot s */iiE/Uax rt urstf (phine int%	D(id %d)\n",ng  	 ==ort fox_na_interfa
eckfer;f ert foii) {iter;
)r	im	im	

	iaiaiort fosine in	r rt fox_)LEMpi if the-EINVA{
	nnamole->ms *_
	i_rt ur TO_n	rr a_GET_RES,ng  	 =(ort fosine int<< 8) |tiinchn,ng  	 =&ort fores) < 0ontroliort foresonvoidnna elsectrolletur;ar__e a c_resonvort foresc rollangged(ort foreso> 1u {ng 	t || _interfadB) {
s *_
	i_rt ur TO_n	rr a_SET_RES,ng  		im	(ort fosine int<< 8) |tiinchn,ng  		oliort foreso/ 2ii< 0i	im	imbreak;EMmMmort foreso/=r2LEMpp}dnime->ms *_
	i_rt ur TO_n	rr a_GET_RES,ng  		 =(ort fosine int<< 8) |tiinchn,=&ort fores) < 0oEMmMmort foreso=r;ar__e a c_res
	nnamole->mort foreso== 0oEMmMort foresonvoid
	tcheAddited al 
e_mastf (p_SELUAC2)
 resoluted 
)
i*
)
i* Soarpdevis.h>reNGE asmall)
 resoluted  arh a actixely
)
i* reacR ug.ope ey don'*/re
staterrabss thesTY oy clip
)
i* Inc., 5luct   a itmape aui.
		 amet(e->mort foiiE + ort foreso<uort foiaxictrolletur;ar__e a c_resonvort foresc olleturs Pud!papstra
e_mac olls *_
ur_E-muraw TO_n	riinchn,=&s Pud)LEMpif (p-;;u {ng 	ttest = s Pud;	im		e->mtest <uort foiaxing  	ttest +nvort foresc ollielseng  	ttest -nvort foresc ollie->mtest <uort foiiE ||papst >uort foiaxn||
)
	
iaias *_
ur_E-murt ur TO_n	riinchn,=0!papst)n||
)
	
iaias *_
ur_E-muraw TO_n	riinchn,=&
e_ma))xtroli		ort foreso=r;ar__e a c_res
	nnMptbreak;EMmMm}rollie->mtest =d 
e_mai	im	imbreak;EMmMmort foreso*=r2LEMpp}dnims *_
ur_E-murt ur TO_n	riinchn,=0!ps Pud)LEMp}dng ort fode <ids (UconvoidnaE
  || kstlam		roluar_sine in_MAX_ID TO_n	rkstla;mmecher codescripled  asinea   but dBr_caleer_m1/256 dBrd_naol heangged  CoeTLV cinea  ser_m1/100cdBrd_naol h/dnort for UAC =
m	(o ling _len;
	_rt ur TO_n	rort foiiEi * 100on/>mort fores)idnort for Uax =
m	(o ling _len;
	_rt ur TO_n	rort foiaxic* 100on/>mort fores)idne->mort for UAC >*ort for Uax)xtrolppingme tsugDisAwrong; assuardit's*   (at lxor/Inc0dBramet(e->mort for UAC < 0oEMmMort for Uax =*0LE	ielseck_m ort for UAC >*0oEMmMort for Uinid 0LEMpe->mort for UAC >*ort for Uax)xtrol	MIT_otxely crerm re
stataaterrabeamete	 if the-EINVA{
	nnamoamme if the0id chudio_ters *_thn_iax TO_n	rdio)	s *_thn_iax_e Fr_MAX_ID TO_n	rdio, +) {oatches */apfeaf te/rhe pad_nall->dBC/
uct usbeturdB) {

	i_teaf te_l->d(s_mixer_intksine int*ksine in	
)
	
ias_mixer_int
	i_e	cval->dBCul->dame_ms_mixer p->dB) {
		cval->dBCTO_nid ksine initprivate_data;E
  || ert fort _onfie== e c_MIXER_BOOLEAN ||
)l ==ort fort _onfie== e c_MIXER_INV_BOOLEAN-
)
ul->dn onfied SNDRV_CTL
	con_TYPSBBOOLEAN; ielseng ul->dn onfied SNDRV_CTL
	con_TYPSBINOCGERLEMul->dn ;
unt =tort fosname;
};

 || ert fort _onfie== e c_MIXER_BOOLEAN ||
)l ==ort fort _onfie== e c_MIXER_INV_BOOLEAN- suppul->dn e aui., Meger.Uinid 0LEMpul->dn e aui., Meger.Uax =*oidna elsectrold|| !ort fode <ids (Uc)xtrol	s *_thn_iax_e Fr_MAX_ID TO_n	r0, ksine ino;	im	e->mort fode <ids (Uco&& ort for UAC >=*ort for Uax)xtrol		ksine initvd[0].accessC&= 	im	im~(SNDRV_CTL
	con_ACCESSrTLV_READ |ng  		 =SNDRV_CTL
	con_ACCESSrTLV_CALLBACKo;	im		_int
	i_treamy ert foii) {iter;
fosard,ng  		 ======SNDRV_CTL
	VENT_otSK_INFO,ng  		 ======&ksine initx_)LEMpi}rol}rolul->dn e aui., Meger.Uinid 0LEMpul->dn e aui., Meger.Uax =
im	(ort foiax (Uort foiiE + ort foreso(U1u / ort foresc iame if the0id checns */
sta urregnee auigfxorgfeaf te/rhe pad_nalC/
uct usbeturdB) {

	i_teaf te_s *(s_mixer_intksine int*ksine in	
)
	
is_mixer_int
	i_e	cvae auig*uap *p = stats_mixer p->dB) {
		cval->dBCTO_nid ksine initprivate_data;EMetur
	rontrae a, err
		iusine initv aui., Meger.v aui[0]id ort foiiE;

 || ert focmask)xtrolcnt =t0LEMpf (p-cid 0L c < oterAR PNELSL c!controlld|| !mort focmaski& (1t<< c)iing 		sineinu,LEMpierrid s *_
ur_E-murt ur TO_n	ro + 1	rontra&O_no;	im	e->merri< 0i	im	i*/
statert foii) {itap *p)_
	i_error ? 0 :terr
	nMpO_nid s *_rel   Mi_rt ur TO_n	rO_no;	im	usine initv aui., Meger.v aui[ont]id rt LEMpiont++;mel}rol*/
stat0LEM} elsectrol/* ma cont name;
l(copierrid s *_
ur_E-murt ur TO_n	r0	r0	r&O_no;	ime->merri< 0i	im	*/
statert foii) {itap *p)_
	i_error ? 0 :terr
	nMO_nid s *_rel   Mi_rt ur TO_n	rO_no;	imusine initv aui., Meger.v aui[0]id rt LEMame if the0id checnputc
sta urregnee auigIncfeaf te/rhe pad_nalC/
uct usbeturdB) {

	i_teaf te_put(s_mixer_intksine int*ksine in	
)
	
is_mixer_int
	i_e	cvae auig*uap *p = stats_mixer p->dB) {
		cval->dBCTO_nid ksine initprivate_data;EMetur
	rontrae a, oe a, err
	
	int namgUconv0LE
  || ert focmask)xtrolcnt =t0LEMpf (p-cid 0L c < oterAR PNELSL c!controlld|| !mort focmaski& (1t<< c)iing 		sineinu,LEMpierrid s *_
ur_E-murt ur TO_n	ro + 1	rontra&oO_no;	im	e->merri< 0i	im	i*/
statert foii) {itap *p)_
	i_error ? 0 :terr
	nMpO_nid usine initv aui., Meger.v aui[ont]
	nMpO_nid s *_abs_rt ur TO_n	rO_no;	im	e->moO_nr!d O_no {ng 	tr *_
ur_E-murt ur TO_n	ro + 1	rontraO_no;	im		 namgUconv1LEMpp}dnimont++;mel}ro} elsectrol/* ma cont name;
l(copierrid s *_
ur_E-murt ur TO_n	r0	r0	r&oO_no;	ime->merri< 0i	im	*/
statert foii) {itap *p)_
	i_error ? 0 :terr
	nMO_nid usine initv aui., Meger.v aui[0]
	nMO_nid s *_abs_rt ur TO_n	rO_no;	ime->mrt  !d oO_no {ng 	r *_
ur_E-murt ur TO_n	r0	r0	rO_no;	im	 namgUconv1LEMp}EMamebuf, p- namgUcld beuct usbs_mixer_intksine in_newrbmixfeaf te_d_na_
elad uns.iunsigd SNDRV_CTL
	con_IFACE_MIXER	
).
checd "", cheARRANTY;fRRAedd distre mixely (cop.l->dB=rdB) {

	i_teaf te_l->d	
).gifid dB) {

	i_teaf te_s *	
).pufid dB) {

	i_teaf te_puf,ns
 * i 
sta iad-onlyivarianalC/
uct usbs_mixer_intksine in_newrbmixfeaf te_d_na_
el_road uns.iunsigd SNDRV_CTL
	con_IFACE_MIXER	
).
checd "", cheARRANTY;fRRAedd distre mixely (cop.l->dB=rdB) {

	i_teaf te_l->d	
).gifid dB) {

	i_teaf te_s *	
).pufid +) {,es
 * if the thasymbintis exmGE eder_mordcraren	llow and the paMAX_Istto
 hehook up
Inc., 5uctndard feaf tefd_namline intme namism
  <lucmixer_intksine in_newr*_interfateaf te_d_na_
elad &erfateaf te_d_na_
el
 * if th
{
	coapfeaf teisine inde <luct usb_ (U_*/appendt
	i_tem_  int co_intksine int*kstl, ctor_mECLARE iname_m*/
stat_eck at(kstnitx_.uct 	r_ec,*_ (Uof(kstnitx_.uct ))id checixerA lot ofLWeadr *s/hiadphonesal Puba*"Speaker" "mixer Make s teiweixer i
checkteInc"Hiadphone". Weivedap ineck_mngme tsugDisAa hiadphoneixerMimilararenhowtudevivedap inestf (rgfatens.de <luct usbix_name_mapno_speaker_on_Weadr *  int co_intksine int*kstl,ng  		s_mixer_int
ard *
ard	buflctor_mECLAREuct s_to_ce_ma[]id uns	"Hiads *",#incads *",#iHiadphone",#incadphone",#+) {};flctor_mECLARE*sc ibointf unconvfalse;_ate->ms_mcmp("Speaker", kstnitx_.uct ))rol*/
sta
		if (p-so=ruct s_to_ce_ma;RE ; s++i	ime->ms=rs=r(
ard *shGE uct 	r*s))xtrolif unconv_mieLEMptbreak;EMm}r_me->maf unc)rol*/
sta
		iheck whekstnitx_.uct 	r"Hiadphone",#_ (Uof(kstnitx_.uct ))id chuct usbix_na
{
	cxfeaf te_stl  int control)
{
	const struix_na*raw_desc,ng  iaia rstruct usbmix_el_maskname_map *p =mal)

	iaias_mixerbmix_name_map **xer m,uct usbmix_nal)
	iaiaiPgne iadonly_mask stats_mixer ac_feaf te_d_na_descriplabl*desconvraw_desc;iunitbitmap, MA;

id 0;nsigneap *p, charid 0;nsignecharx_u=r ac_feaf te_d_na_iFeaf te(desct struct us_intksine int*kstl;ats_mixer p->dB) {
		cval->dBCTO_n;flctor_mruct usbmix_selector_map *selenitbitmap, MA amgU;Eetcine in++;  ignompgigfxorgzero-basmapren1-basmape auig*/E
  || e
	return Nr a_FU_GRAPHIC_EQUALIZER)xtrolppiFIXME: not  RANGE ed,y inametl*/
sta
	nammema
 =tt unitid, i strusbmix_nasine ino;	i || erme_map *p)
{
	if4/0i)rol*/
sta
		iTO_nid kz	lloc(_ (Uof(*TO_n), GFP_KERNELo;	i || !ort )rol*/
sta
		ert foii) { = == unitii) {
		ert fox_u=r bmix_
		ert foe
	returnasine in
		ert foemaski=x_el_mask
		ert fort _onfie=B_name_feaf te_l->d[sine ini1].onfig;
	|| eel_maskr== 0ontrolort fochame;
}edv1Ll/* ma cont name;
l(copiort foia con_ iadonlyi=e iadonly_maskidna elsectroldturi	ro =t0LEMpf (p-iid 0L i < 16; i++i	im		|| eel_maskr& (1t<< i))	im		 ++;melort fochame;
}edvc;melort foch_ iadonlyi=e iadonly_maskidnammechal heIfn	llt name;
}
   but maskre Limarked, iad-only	riakd/c inline in

i* read-only.as *_
ur_E-murt ur )eARRAN
ored but maskreinclu addwon'*

i* issuigtion, terrupt dren iad-onlyi name;
}.ol h/dne->mort fochame;
}ed=  iadonly_mask sg kstn d _int
	i_new1(&erfateaf te_d_na_
el_ro	rort ); ielseng kstn d _int
	i_new1(&erfateaf te_d_na_
el	rort ); 	i || !kstlantrolbmix_name_err(== uniter;
	 "cannot m	lloc ksine in\n")LEMpk and(ort ); il*/
sta
	namMkstnitprivate_ and/=a p->dB) {
		cva and; 	i;

id oame_map *p, char term kstnitx_.uct 	r_ (Uof(kstnitx_.uct ))id	ap *p, charid ;

i!=v0;ate->m!;

i&&echarx_ sg ;

id _interfa
 * _ude <lidesc(== strucharx_,rol		kstnitx_.uct 	r_ (Uof(kstnitx_.uct ))id bse Fed
(oine inontrocasmer a_FU_MUTE:rocasmer a_FU_VOLUME:EMpch
)
i* vedap inecc inline intuct .  
sta uleers:
)
i* -ck_mat
check	DisAgiANTAin descriplab,
 *  na.d)
i* -ck_mc inlinnec eder_putt ah be vedap ined!pae <luloT_SEL
ched)
i*   ofLap;
in_nionfi.d)
i* -ck_mc inlinnec edeMERputt ah be vedap ined!p *  na.d)
i* -co (atwis 	ranonymoustuct .
		 amet(e->m!;

)etrol	;

id s *_er melem_  i strucer mm kstnitx_.uct 	ng  		 ===_ (Uof(kstnitx_.uct ),U1uldnt(e->m!;

)ng  	;

id s *_er melem_  i stru&== unitoer m,ng   		 ===kstnitx_.uct 	ng  			 ===_ (Uof(kstnitx_.uct ),U1uldnt(e->m!;

)ng  	;

id snprir;f kstnitx_.uct 	ng  		 ======_ (Uof(kstnitx_.uct ),ng  		 ======"Feaf teC%d"rusbmix_);_Mp}dng e->m!ap *p, charoEMmMoe_mapno_speaker_on_Weadr * kstl, == unitii) {iter;
fosard)LEetech
)
i* vedap inecc ins has  diruc3ich:
)
i* k_mc inlinnec edeMERputtisAr cos has !pae <lit's*likcly a
)
i* capf tefs has .  o (atwis ckte
check_ignplaybackh(WITHfuely :)
		 amet(e->m!ap *p, chari&&e!(== unitoer m.onfie>> 16)ontrolld|| (== unitoer m.onfie&ualff00on=nval 100)ng  	;

id appendt
	i_tem_ kstl, "bCapf te")LEMpielseng  	;

id appendt
	i_tem_ kstl, "bPlayback"cLEMp}dn	appendt
	i_tem_ kstl, e
	return Nr a_FU_MUTEn?EMps	" Se Fed" : "bVoluar")LEMpbreak;rodefa ch:ng e->m!;

)ng  heck whekstnitx_.uct 	r_name_feaf te_l->d[sine ini1].uct 	ng  	_ (Uof(kstnitx_.uct ))id	pbreak;roammeches */iiE/Uax rt urstamets *_thn_iax_e Fr_MAX_ID TO_n	r0, kst ); 	i || e
	return Nr a_FU_VOLUMEontroloe_map *p,
				 term cO_no;	ime->mort for UAC < ort for Uax retuort fode <ids (Uc)xtrol	kstnittlv.c d _interfadB) {
rol_tlv;rol	kstnitvd[0].accessC|=
im		SNDRV_CTL
	con_ACCESSrTLV_READ |ng  	SNDRV_CTL
	con_ACCESSrTLV_CALLBACK
	nnamoamme mpgig=t(ort foiax (Uort foiiEu / ort foresc ichal heAp)ee ereedevis.h>e Freroluarprmpgigm*p)ee ah 255? Ip *  aAio_gm*p)

i* Incbe s te.v384DisAa resoluted pmagusbnumb
 *f uncod pLogiouch

i* vevis.h. IteARRANdio_titLicly c(conn	lltbuggyeLogiouch vevis.h.ol h/dne->mrmpgig>v384antrolbmix_name_warn(== uniter;
	
 		 ======"Warn <l! Unlikcly bigeroluarprmpgig(=%u), ort foresoisAprobably wrong.",ng  iaia rprmpgio;	imumix_name_warn(== uniter;
	
 		 ======"[%d] FU [%s]DECtd %d, O_nid %d/%d/%d",ng  iaia rport fox_nakstnitx_.uct 	rort fochame;
},ng  iaia rport fothn, ort foiax, ort fores)ldnammebmix_name_dbg(== uniter;
	 "[%d] FU [%s]DECtd %d, O_nid %d/%d/%d\n",ng 
	iaiaort fox_nakstnitx_.uct 	rort fochame;
},ng aia rport fothn, ort foiax, ort fores)ldn_interfadB) {
/
#_sine in  i unitii) {, kst );  checixern cloTa feaf tefd_napy of tmosteatchine in usbm dio_tedmhere.de <luct usbeturn clox_name_teaf te_d_na  int control)
{
	const struct usbmix_nal)
{
	iaix_na*_finame_mPgne name;
s,ri	rj;ats_mixer p->_name_map *cer melenitbitmap, MAia con_io_s,rfirsn_ch_io_s;_meturerr, os (U;roruct usbac_feaf te_d_na_descriplabl*hdrid _fin;ro__u8 E_teCine ins;_ate->ms_ unitii) {itproIncturn Nr a_VERSION_1u {ng cs (U =*hdrit_Cine inS (U;roi || !o_ (Uuetrolebmix_name_dbg(== uniter;
	
 			
	iaia"d_nam%u:p, v a c _Cine inS (Un=nva\n",ng  

	iaiasbmix_);_Mpi if the-EINVA{
	nnamolchame;
}edv hdrit_LengFre- 7u / os (U -v1LEMp_teCine ins =*hdrit_teCine ins;_ol || hdrit_LengFre< 7 + o_ (Uuetrolebmix_name_err(== uniter;
	
 			
	iaia"d_nam%u:p, v a c r a_FEATURE_UNIT descriplab\n",ng  

	iaiasbmix_);_Mpi if the-EINVA{
	nnamoa elsectrolruct usbac2_feaf te_d_na_descriplabl*finid _fin;ro cs (U =*4;EMmchame;
}edv hdrit_LengFre- 6u / 4 -v1LEMp_teCine ins =*finit_teCine ins;_ol || hdrit_LengFre< 6 + o_ (Uuetrolebmix_name_err(== uniter;
	
 			
	iaia"d_nam%u:p, v a c r a_FEATURE_UNIT descriplab\n",ng  

	iaiasbmix_);_Mpi if the-EINVA{
	nnamoammechen cloT_SELE0x03,td_namh/dne->m(errid n clox_name_d_na  i struhdrit_plx03,ID))i< 0i	im*/
staterr
	mechevedap inecc inr_puttE0x03,tonfie addcharih/dnerrid 
e_mapinput_map   i struhdrit_plx03,IDru&iap;
o;	ie->merri< 0i	im*/
staterr
	d	ap con_io_sid _interfa
 mbusb_bytes()teCine ins, os (U)c ich ma cont me_maur03ich MAX_IDh <lbse Fed
(== uniter;
foerfacd)ntrocasmer c_ID(al 8bb, 0x2702):ng bmix_name_l->d(s_ uniter;
	
 		 ======"bmix_ser: ma con/roluarpMAX_Itf (pPCM2702 er;
\n")LEMpchevisrces non-functed al roluarpsine in amet(ap con_io_si&= ~r a_CONTROL_BIT(r a_FU_VOLUMEoid	pbreak;rocasmer c_ID(al11
/*
alf211):ng bmix_name_l->d(s_ uniter;
	
 		 ======"bmix_ser: roluarpsine in MAX_Itf (pTenx TP6911 ntrol Hiads *\n")LEMpchevisrces non-functed al roluarpsine in amet(chame;
}edv0LEMpbreak;r	namM || erame;
}e>*0oEMmfirsn_ch_io_sid _interfa
 mbusb_bytes()teCine ins + o_ (U, os (U)c ielseng firsn_ch_io_sid 0;_ate->ms_ unitii) {itproIncturn Nr a_VERSION_1u {ng /er
ored 	llt ine intonfissametef (p-iid 0L i < 10; i++ietrolebtruct usbmix_h_io_sid 0;_	tef (p-jid 0L j <  name;
sL j++ietroleenitbitmap, MAia k;		im		maski=x_interfa
 mbusb_bytes()teCine ins +ng  				 ====cs (U *p-j+1), os (U)c iolld|| maskr& (1t<< i))	im			_h_io_si|= (1t<< j)LEMpi}rolecheaudio slass v1chine in usbm nev)
 read-onlyi*/E
 tech
)
	 the e*firsnt name;
lmusntbe set
)
	 th(f (peasmeatcprogram ing).
)
	 t/	im		|| eh_io_si& 1)	im		
{
	cxfeaf te_stl  i stru_fin,x_h_io_s,ri	ng  			 =&cer mm sbmix_na0uldnt(e->map con_io_si& (1t<< i))	im		
{
	cxfeaf te_stl  i stru_fin,x0,ri	r&ier m,ng   		 =sbmix_na0uldnt}dna elsect cher a_VERSION_2sametef (p-iid 0L i < ARRAY_SIZE(_name_feaf te_l->d); i++ietrolebtruct usbmix_h_io_sid 0;_	tebtruct usbmix_h_ iad_onlyid 0;		im	f (p-jid 0L j <  name;
sL j++ietroleenitbitmap, MAia k;		im		maski=x_interfa
 mbusb_bytes()teCine ins +ng  				 ====cs (U *p-j+1), os (U)c iolld|| bac2_sine in_ls_ iadrces(masknam))xtroli		oh_io_si|= (1t<< j)LEMpioi || !bac2_sine in_ls_tion,rces(masknam))ng  			_h_ iad_onlyi|= (1t<< j)LEMpio}EMpi}r
 tech
)
	 thNOTE:a
{
	cxfeaf te_stl )eARRANmark/c inline in

	
i* read-onlyck_mallt name;
}
e Limarked, iad-onlyp, 

	
i* rhs descriplabh. O (atwis 	rc inline intARRANTY

	
i* reNGE ed,as tion,rces,s therhs drLicrtARRANnot
)
	 thactixely issuigagtion, terrupttf (p iad-only
)
	 th name;
}.ol
	 t/	
 tech
)
	 the e*firsnt name;
lmusntbe set
)
	 th(f (peasmeatcprogram ing).
)
	 t/	im		|| eh_io_si& 1)	im		
{
	cxfeaf te_stl  i stru_fin,x_h_io_s,ri	ng  			 =&cer mm sbmix_na_h_ iad_onlyuldnt(e->mbac2_sine in_ls_ iadrces(mascon_io_s,ri))	im		
{
	cxfeaf te_stl  i stru_fin,x0,ri	r&ier m,usbmix_nal)
{		 =!bac2_sine in_ls_tion,rces(mascon_io_s,ri))
	nnamoamme if the0id checixerc) 20CK_RAand the f th
{
	coaprhe pad_nalsine inde 
i* rhs callback}
e Liiden usal>e Frefeaf tefd_na.ixererputt name;
lnumb
 *(zero basma)DisAgiANTAin sine intfieldtinuciad.de <luct usbix_na
{
	cxntrol)d_na_stl  int control)
{
	const strng  

s_mt usbac_ntrol)d_na_descriplabl*desc,ng  	iPgnedi_pin	returdi_ch	retursbmix_nal)
{
s_mixerbmix_name_map **xer m stats_mixer p->dB) {
		cval->dBCTO_nelenitbitmap, MAnum_MERs d bac_ntrol)d_na_bNrCname;
}(_esct stnitbitmap, MAi	rlen struct us_intksine int*kstl;atctor_mruct usbmix_selector_map *selmema
 =tt unitid, i strusbmix_na0o;	i || erme_map *p)
{
	if4/0i)rol*/
sta
		iTO_nid kz	lloc(_ (Uof(*TO_n), GFP_KERNELo;	i || !ort )rol*/
sta
			ert foii) { = == unitii) {
		ert fox_u=r bmix_
		ert foe
	returnadi_ch + 1; chebasmapch 1 h/dnort fort _onfie=Br c_MIXER_S16;
ef (p-iid 0L i < num_MERs; i++ietrol__u8 Ec d bac_ntrol)d_na_bmCine ins(_esc, == unitii) {itproInctu);_	ime->moe_map *tr-muio_ter(c,rdi_ch	re,rnum_MERsaontroliort foemaski|= (1t<< i)LEMpiort fo name;
}++;mel}ro}mmeches */iiE/Uax rt urstamets *_thn_iax TO_n	r0);_	ikstn d _int
	i_new1(&erfateaf te_d_na_
el	rort ); i || !kstlantrolbmix_name_err(== uniter;
	 "cannot m	lloc ksine in\n")LEMpk and(ort ); il*/
sta
	namMkstnitprivate_ and/=a p->dB) {
		cva and; 	i;

id oame_map *p, char term kstnitx_.uct 	r_ (Uof(kstnitx_.uct ))id	e->m!;

)ng ;

id s *_er melem_  i strucer mm kstnitx_.uct 	ng  	 ===_ (Uof(kstnitx_.uct ),U0)id	e->m!;

)ng ;

id sprir;f kstnitx_.uct 	r"c) 20mplx03, %d"ruci_ch + 1)id	appendt
	i_tem_ kstl, "bVoluar")LEmebmix_name_dbg(== uniter;
	 "[%d] MU [%s]DECtd %d, O_nid %d/%d\n",ng 
	iaort fox_nakstnitx_.uct 	rort fochame;
},port fothn, ort foiax)ldn_interfadB) {
/
#_sine in  i unitii) {, kst );  checixern cloTa rhe pad_nade <luct usbeturn clox_name_ntrol)d_na  int control)
{
	const struct usbmix_nal)
{
	ix_na*raw_desc stats_mixer ac_ntrol)d_na_descriplabl*desconvraw_desc;ius_mixer p->_name_map *cer meleeturdiput_pins,rnum_ins,rnum_MERs;leeturpin	rech	rerr
		i || descit_LengFre< 11 retu(diput_pins = descit_NrInPinsu ||
)l ==!(num_MERs d bac_ntrol)d_na_bNrCname;
}(_esct)antrolbmix_name_err(== uniter;
	
 		 =====", v a c MIXER UNIT descriplab %d\n",ng  
	iaiasbmix_);_Mp if the-EINVA{
	n}mecheno bmCine instfieldt(e.gr Maya44) -> ap *p)mh/dne->mdescit_LengFre<nvo0 + diput_pinsintrolbmix_name_dbg(== uniter;
	 "MU %dmple no bmCine instfield\n",ng  
	iaiasbmix_);_Mp if the0LEM}n_mnum_insid 0;nsiECtd 0;
ef (p-pinid 0Lrpine< diput_pinsLrpin++ietrolerrid n clox_name_d_na  i strudescit_aplx03,ID[pin]o;	ime->merri< 0i	im	sineinu,LEMperrid 
e_mapinput_map   i strudescit_aplx03,ID[pin]ru&iap;
o;	ime->merri< 0i	im	*/
staterr
	nMnum_insi+= xer m.sname;
};

if (p-;*ged
<rnum_ins;*ged!controlldturoch	re_h_ple_cine ins =*0;		im	f (p-oECtd 0;roch < num_MERs; oed!controlll__u8 Ec d bac_ntrol)d_na_bmCine ins(_esc,al)
{		== unitii) {itproInctu);_	imime->moe_map *tr-muio_ter(c,rdch	roch	rnum_MERsaontroli		e_h_ple_cine ins =*1;
i	Mptbreak;EMmMm}rolp}dnime->me_h_ple_cine ins)	im		
{
	cxntrol)d_na_stl  i strudesc,rpin	rech	al)
{		 =  =sbmix_na&iap;
o;	im}EMame if the0id chec
 heProcess <lCK_RA / Exernsich K_RAand the  s */callbackhave process <l/exernsich d_nalC/
uct usbeturdB) {

	i_procd_na_s *(s_mixer_intksine int*ksine in	
)
	
iis_mixer_int
	i_e	cvae auig*uap *p = stats_mixer p->dB) {
		cval->dBCTO_nid ksine initprivate_data;EMeturerr, v_n;		ierrid s *_
ur_
	i_rt ur TO_n	rert foe
	retur<< 8	r&O_no;	ie->merri< 0o&& ort foii) {itap *p)_
	i_errorintrolbsine initv aui., Meger.v aui[0]id ort foiiE;

p if the0LEM}nme->merri< 0i	im*/
staterr
	nO_nid s *_rel   Mi_rt ur TO_n	rO_no;	iusine initv aui., Meger.v aui[0]id rt LEM if the0id checnputccallbackhave process <l/exernsich d_nalC/
uct usbeturdB) {

	i_procd_na_put(s_mixer_intksine int*ksine in	
)
	
iis_mixer_int
	i_e	cvae auig*uap *p = stats_mixer p->dB) {
		cval->dBCTO_nid ksine initprivate_data;EMeture a, oe a, err
		ierrid s *_
ur_
	i_rt ur TO_n	rert foe
	retur<< 8	r&oO_no;	ie->merri< 0ontrol || ort foii) {itap *p)_
	i_errori	im	*/
stat0;	im*/
staterr
	n}
MO_nid usine initv aui., Meger.v aui[0]
	nO_nid s *_abs_rt ur TO_n	rO_no;	ie->mrt  !d oO_no {ng s *_
ur_
	i_rt ur TO_n	rert foe
	retur<< 8	rrt ); il*/
stavoidnaEM if the0id checnalsachine inter;
	unsigave process <l/exernsich d_nalC/
uct usbucmixer_intksine in_newrdB) {
procd_na_
elad uns.iunsigd SNDRV_CTL
	con_IFACE_MIXER	
).
checd "", cheARRANTY;fRRAedd distr(cop.l->dB=rdB) {

	i_teaf te_l->d	
).gifid dB) {

	i_procd_na_s *	
).pufid dB) {

	i_procd_na_put,es
 * if thpp)
io_tedmdataiave process <lad_nasand tucmixerprocd_na_rt ur_l->dB{flme_map *p =;;
ECLAREsuffix;EMeture a_onfig;
	turiin_rt ur;es
 *ucmixerprocd_na_l->dB{flme_monfig;
ECLAREuct mi	ucmixerprocd_na_rt ur_l->dB*rt urs;es
 *uct usbs_mixerprocd_na_rt ur_l->dBupdown_proc_l->d[]id uns{ur a_UD_ENABLE	r"Se Fed", e c_MIXER_BOOLEAN },ns{ur a_UD_MODEce/*
 *	 "Mode S eXte", e c_MIXER_U8	r1 },ns{ua }es
 uct usbs_mixerprocd_na_rt ur_l->dBprologic_proc_l->d[]id uns{ur a_DP_ENABLE	r"Se Fed", e c_MIXER_BOOLEAN },ns{ur a_DP_MODEce/*
 *	 "Mode S eXte", e c_MIXER_U8	r1 },ns{ua }es
 uct usbs_mixerprocd_na_rt ur_l->dBthandd_enh_proc_l->d[]id uns{ur a_3D_ENABLE	r"Se Fed", e c_MIXER_BOOLEAN },ns{ur a_3D_SPACE,*"Spaciousness", e c_MIXER_U8 },ns{ua }es
 uct usbs_mixerprocd_na_rt ur_l->dBrev)
b_proc_l->d[]id uns{ur a_REVERB_ENABLE	r"Se Fed", e c_MIXER_BOOLEAN },ns{ur a_REVERB_LEVEL,*"Level", e c_MIXER_U8 },ns{ur a_REVERB_TIME	r"Tiar", e c_MIXER_U16 },ns{ur a_REVERB_FEEDBACK,="Feedback", e c_MIXER_U8 },ns{ua }es
 uct usbs_mixerprocd_na_rt ur_l->dBchorus_proc_l->d[]id uns{ur a_CHORUS_ENABLE	r"Se Fed", e c_MIXER_BOOLEAN },ns{ur a_CHORUS_LEVEL,*"Level", e c_MIXER_U8 },ns{ur a_CHORUS_RATE,*"Ratr", e c_MIXER_U16 },ns{ur a_CHORUS_DEPTH,*"Depth", e c_MIXER_U16 },ns{ua }es
 uct usbs_mixerprocd_na_rt ur_l->dBdcr_proc_l->d[]id uns{ur a_DCR_ENABLE	r"Se Fed", e c_MIXER_BOOLEAN },ns{ur a_DCR_RATE,*"Ratio", e c_MIXER_U16 },ns{ur a_DCR_MAXAMPL,*"Max Amp", e c_MIXER_S16 },ns{ur a_DCR_THRESHOLD	r"Thanshold", e c_MIXER_S16 },ns{ur a_DCR_ATTACK_TIME	r"AttackhTiar", e c_MIXER_U16 },ns{ur a_DCR_R/*
ASE_TIME	r"R eXasmeTiar", e c_MIXER_U16 },ns{ua }es
  uct usbs_mixerprocd_na_l->dBprocd_nas[]id uns{ur a_PROCESS_UP_DOWNMIX	r"Kp Down"ruspdown_proc_l->d },ns{ur a_PROCESS_DOLBY_PROLOGIC,*"DolbyePrologic"ruprologic_proc_l->d },ns{ur a_PROCESS_STEREOkEXTENDER	 "3D Sisteo Exernder", thandd_enh_proc_l->d },ns{ur a_PROCESS_REVERB	r"R v)
b",Brev)
b_proc_l->d },ns{ur a_PROCESS_CHORUS,*"Chorus",Bchorus_proc_l->d },ns{ur a_PROCESS_DYN_RANGE_COMPr "DCR",Bdcr_proc_l->d },ns{ua },nxer if thpp)
io_tedmdataiave exernsich d_nas
lC/
uct usbucmixerprocd_na_rt ur_l->dBcpars_rate_xu_l->d[]id uns{ur c_XUk sourcRATEce/*
 * m	r"S eXtens", e c_MIXER_U8	r0 },ns{ua }es
 uct usbs_mixerprocd_na_rt ur_l->dBcpars_E0x03,_xu_l->d[]id uns{ur c_XUk source*/
	Uce/*
 * m	r"Exer n_n", e c_MIXER_BOOLEAN },ns{ua }es
 uct usbs_mixerprocd_na_rt ur_l->dBspdif_= 0x03_xu_l->d[]id uns{ur c_XUkDIGITAL_FORMATce/*
 * m	r"SPDIF/AC3", e c_MIXER_BOOLEAN },ns{ua }es
 uct usbs_mixerprocd_na_rt ur_l->dBsoft_limi3_xu_l->d[]id uns{ur c_XUkSOFT_LIMITce/*
 * m	r" ", e c_MIXER_BOOLEAN },ns{ua }es
 uct usbs_mixerprocd_na_l->dBexed_nas[]id uns{ur c_XUk sourcRATE,*"Cparseratr", cpars_rate_xu_l->d },ns{ur c_XUk source*/
	Ur "DigitalIn CLKtE0x03,", cpars_E0x03,_xu_l->d },ns{ur c_XUkDIGITAL_IO_STATUS,*"DigitalOuamf (x03:",#_pdif_= 0x03_xu_l->d },ns{ur c_XUkDEVI	UcOPTIONS,*"An_nogueIn Soft Limi3",#_oft_limi3_xu_l->d },ns{ua }es
  e f th
{
	coapprocess <l/exernsich d_nade <luct usbetur
{
	cx_name_procd_na  int control)
{
	const struct usbmix_nal)
{ix_na*raw_desc,bs_mixerprocd_na_l->dB*listnal)
{ECLAREuct  stats_mixer ac_process <lid_na_descriplabl*desconvraw_desc;iu, MAnum_ins = descit_NrInPins;ats_mixer p->dB) {
		cval->dBCTO_neleruct us_intksine int*kstl;at, MAi	rerr, charx_,monfi	rlen struct usprocd_na_l->dB*l->dmi	ucmixerprocd_na_rt ur_l->dB*rt l->dmi	ctor_mruct usbmix_selector_map *seleuct usbs_mixerprocd_na_rt ur_l->dBdefa ch_rt ur_l->d[]id uns	{val 1	r"Se Fed", e c_MIXER_BOOLEAN },nss{ua }e	}eleuct usbs_mixerprocd_na_l->dBdefa ch_l->dB=runs	0,#+) {,Bdefa ch_rt ur_l->de	}el	i || descit_LengFre< 13 retdescit_LengFre< 13 +Anum_ins ||
)l ==descit_LengFre< num_insi+ bac_process <lid_na_bCine inS (U(_esc, == unitii) {itproInctu)antrolbmix_name_err(== uniter;
	 ", v a c %s descriplab (id %d)\n", uct 	rsbmix_);_Mp if the-EINVA{
	n}m
ef (p-iid 0L i < num_ins;*g!controle->m(errid n clox_name_d_na  i strudescit_aplx03,ID[i]))i< 0i	imm*/
staterr
	n}

	onfied le16_to_cpu(descitwProcessTnfio;	if (p-i->dB=rlist;ll->dB&& l->dn onfi;ll->d++i	ime->ml->dn onfied=monfii	immbreak;EMe->m!l->dBretul->dn onfii	ime->dB=r&defa ch_l->d
		if (p-rt l->drnadi>dn e auis;*rt l->dfoe
	retu;*rt l->d++ietrol__u8 Ecine ins =*bac_process <lid_na_bmCine ins(_esc, == unitii) {itproInctu);_	ime->m! e
	retus[rt l->dfoe
	retu / 8]i& (1t<< ((rt l->dfoe
	retu % 8) (U1u)iing 	sineinu,LEMpma
 =tt unitid, i strusbmix_nart l->dfoe
	retuo;	ime->morme_map *p)
{
	if4/0i)rol	sineinu,LEMpTO_nid kz	lloc(_ (Uof(*TO_n), GFP_KERNELo;	ii || !ort )rolebuf, p--ENOMEMLEMpTO_nfoii) { = == unitii) {
			ert fox_u=r bmix_
			ert foe
	returnart l->dfoe
	retu;			ert fort _onfie=Brt l->dfoe a_onfig;
lort fochame;
}edv1LEeteches */iiE/Uax rt urstametie->mtnfie== e a_PROCESS_UP_DOWNMIXo&& ort foe
	return Nr a_UD_MODEce/*
 *ontroli__u8 Ecine in_spec =*bac_process <lid_na_specams (_esc, == unitii) {itproInctu);_	olppiFIXME: hard codedmamete	ort fominid 1LEMpiort foiax =*oine in_spec[0];	im	ort foresod 1LEMpiort fode <ids (Uconvoidnna elsectrolle->mtnfie== e c_XUk sourcRATEu {ng 	tch
)
	
i* E-MuAr co0404/0202/TraederPre/0204
)
	
i* saY oeratepsine in MAX_I
)
	
i*/	im	iort fominid 0;_	teiort foiax =*5;_	teiort foresod 1LEMpiiort fode <ids (Uconvoidnnna elseEMpiis *_thn_iax TO_n	rrt l->dfoiin_rt ur);_Mp}dng kstn d _int
	i_new1(&dB) {
procd_na_
elm cO_no;	ime->m!kstlantrolpk and(ort ); ilebuf, p--ENOMEMLEMp}
	Mkstnitprivate_ and/=a p->dB) {
		cva and; 	ime->moe_map * *p, char term kstnitx_.uct 	r_ (Uof(kstnitx_.uct )))xtrol	MITno tsugD*/ idnna elsece->ml->dn uct )ntrolis_mk whekstnitx_.uct 	rl->dn uct 	r_ (Uof(kstnitx_.uct ))id	na elsectrollcharx_u=r ac_process <lid_na_iProcess <l(desc, == unitii) {itproInctu);_	ol;

id 0;nsime->mcharx_ sg g ;

id _interfa
 * _ude <lidesc(== strucharx_,rol		g  
	iaia=kstnitx_.uct 	ng  				 ======_ (Uof(kstnitx_.uct )uldnt(e->m!;

)ng  	s_mk whekstnitx_.uct 	ruct 	r_ (Uof(kstnitx_.uct ))id	na
n	appendt
	i_tem_ kstl, " ")id	nappendt
	i_tem_ kstl, rt l->dfosuffix);_	imbmix_name_dbg(== uniter;
	
 		======"[%d] PU [%s]DECtd %d, O_nid %d/%d\n",ng  
	iaiaort fox_nakstnitx_.uct 	rort fochame;
},ng  aia rport fothn, ort foiax);_	imerrid _interfadB) {
/
#_sine in  i unitii) {, kst ); ime->merri< 0i	im	*/
staterr
	naEM if the0id chuct usbeturn clox_name_process <lid_na  int control)
{
	const struct usbmix_nal)
{
	ia 
	ix_na*raw_desc stat if the
{
	cx_name_procd_na  i strusbmix_naraw_desc,ng  {
	iaprocd_nas	r" rocess <lCK_RA")id chuct usbeturn clox_name_exernsichid_na  int control)
{
	const struct usbmix_nal)
{
	ia 
ix_na*raw_desc statchal heNotHOUT ANwern cloTexernsich d_nas>e Freprocess <lad_na descriplabh.
	 the at's*ok,as ., 5layouttisA_SELEct .
	i*/	i if the
{
	cx_name_procd_na  i strusbmix_naraw_desc,ng  {
	iaexed_nas	r"Exernsich K_RA");  checixerS eXtensCK_RAand the f thl->dBcallbackhave s eXtensCd_nade p *  aatenumerat (p_nfief (p out <lde <luct usbeturdB) {

	i_s eXtens_l->d(s_mixer_intksine int*ksine in	
)
	
iaas_mixer_int
	i_e	cval->dBCul->dame_ms_mixer p->dB) {
		cval->dBCTO_nid ksine initprivate_data;Elctor_mECLARE*itemlistg=t(otor_mECLARE*)ksine initprivate_rt ur;e
t || _intBUG_ONm!ltemlisti)rol*/
stae-EINVA{
	n*/
stat_int
	i_enum_in>d(ul->d, 1	rort foiaxrucermlistiid checns */callbackhave s eXtensCd_nae <luct usbeturdB) {

	i_s eXtens_s *(s_mixer_intksine int*ksine in	
)
	
iis_mixer_int
	i_e	cvae auig*uap *p = stats_mixer p->dB) {
		cval->dBCTO_nid ksine initprivate_data;EMeture a, err
		ierrid s *_
ur_
	i_rt ur TO_n	rert foe
	retur<< 8	r&O_no;	ie->merri< 0ontrol || ort foii) {itap *p)_
	i_errorietrolebsine initv aui.enumerated.cerm[0]id 0; ilebuf, p-0id	na
n	*/
staterr
	n}
MO_nid s *_rel   Mi_rt ur TO_n	rO_no;	iusine initv aui.enumerated.cerm[0]id rt LEM if the0id checnputccallbackhave s eXtensCd_nae <luct usbeturdB) {

	i_s eXtens_put(s_mixer_intksine int*ksine in	
)
	
iis_mixer_int
	i_e	cvae auig*uap *p = stats_mixer p->dB) {
		cval->dBCTO_nid ksine initprivate_data;EMeture a, oe a, err
		ierrid s *_
ur_
	i_rt ur TO_n	rert foe
	retur<< 8	r&oO_no;	ie->merri< 0ontrol || ort foii) {itap *p)_
	i_errori	im	*/
stat0;	im*/
staterr
	n}
MO_nid usine initv aui.enumerated.cerm[0]
	nO_nid s *_abs_rt ur TO_n	rO_no;	ie->mrt  !d oO_no {ng s *_
ur_
	i_rt ur TO_n	rert foe
	retur<< 8	rrt ); il*/
stavoidnaEM if the0id checnalsachine inter;
	unsigave s eXtensCd_nae <luct usbucmixer_intksine in_newrdB) {
s eXted_na_
elad uns.iunsigd SNDRV_CTL
	con_IFACE_MIXER	
).
checd "", cheARRANTY;fRRAedd distr(cop.l->dB=rdB) {

	i_s eXtens_l->d	
).gifid dB) {

	i_s eXtens_s *	
).pufid dB) {

	i_s eXtens_put,es
 * if thppivate  and/callback.ixer and/facesprivate_datae addprivate_rt urde <luct usbix_naerfadB) {
s eXtens_		cva and  int co_intksine int*kstlame_mPgnei, num_insid 0;n
  || kstlitprivate_data)ctrolruct usbp->dB) {
		cval->dBCTO_nid kstlitprivate_data;	nMnum_insi=rort foiax;	nMk and(ort ); ilkstlitprivate_data d +) {LEM}nme->mkstlitprivate_rt ur)ntroloeLARE*itemlistg=t(oCLARE*)kstlitprivate_rt ur;
tef (p-iid 0L i < num_ins;*g!corolpk and(itemlist[i]);	nMk and(cermlistiidilkstlitprivate_e auig=e0LEM}n checixern cloTa s eXtensCd_nade <luct usbeturn clox_name_s eXtens_d_na  int control)
{
	const struct usbmix_nal)
{
	ia ix_na*raw_desc stats_mixer ac_s eXtens_d_na_descriplabl*desconvraw_desc;iunitbitmap, MAi, charx_,mlen steturerr;ats_mixer p->dB) {
		cval->dBCTO_neleruct us_intksine int*kstl;atctor_mruct usbmix_selector_map *seleoCLARE*lectlist; 	i || !descit_NrInPins retdescit_LengFre< 5 + descit_NrInPinsu trolbmix_name_err(== uniter;
	al)
", v a c e/*
 * m UNIT descriplab %d\n",rsbmix_);_Mp if the-EINVA{
	n}m
ef (p-iid 0L i < descit_NrInPins;*g!controle->m(errid n clox_name_d_na  i strudescit_aplx03,ID[i]))i< 0i	imm*/
staterr
	n}

	 || descit_NrInPins ==U1u /* onlypone ? nonsrnse!nametl*/
sta 0;n
 ma
 =tt unitid, i strusbmix_na0o;	i || erme_map *p)
{
	if4/0i)rol*/
sta 0;n
 TO_nid kz	lloc(_ (Uof(*TO_n), GFP_KERNELo;	i || !ort )rol*/
sta--ENOMEMLEMert foii) { = == unitii) {
		ert fox_u=r bmix_
		ert fovt _onfie=Br c_MIXER_U8
		ert fochame;
}edv1LE	ort fominid 1LEMort foiax =*descit_NrInPins;atort foresod 1LEMort fode <ids (Uconvoidate->ms_ unitii) {itproIncturn Nr a_VERSION_1u			ert foe
	returna0LEMelseccher a_VERSION_2sameteert foe
	returna descit_DescriplabSubtnfie== e a2k source/*
 * m)n?EMpse a2k Xk source/*
 * m : e a2kSUce/*
 * midatlectlistid km	lloc(_ (Uof(oCLARE)i* vescit_NrInPins, GFP_KERNELo;	i || !lectlistontrolk and(ort ); il*/
sta--ENOMEMLEM}hudio_teroterITon_NAME_LEN	64
ef (p-iid 0L i < descit_NrInPins;*g!controls_mixer p->_name_map *cer melel;

id 0;nsilectlist[i]id km	lloc(oterITon_NAME_LEN, GFP_KERNELo;	ii || !lectlist[i]ietroleangged(i--)ng  	k and(lectlist[i]i;
  	k and(lectlisti;
  	k and(ort ); ilebuf, p--ENOMEMLEMp}
	M;

id oame_map *p, s eXtens_lem_  i strusbmix_nai, charlist[i]	al)
{		 oterITon_NAME_LENo;	ii || ! ;

i&&e
e_mapinput_map   i strudescit_aplx03,ID[i]ru&iap;
o >=*0i	imm;

id s *_er melem_  i stru&cer mm charlist[i]	 oterITon_NAME_LENna0uldnt || ! ;

)ng  hprir;f charlist[i]	 "Irputt%u"ruc)ldnammekstn d _int
	i_new1(&dB) {
s eXted_na_
el	rort ); i || ! kstlantrolbmix_name_err(== uniter;
	 "cannot m	lloc ksine in\n")LEMpk and(lectlisti;
  k and(ort ); il*/
sta--ENOMEMLEM}hlkstlitprivate_e auig=e(nitbitmaplong)lectlist; Mkstnitprivate_ and/=a p->dB) {
s eXtens_		cva andidatlectx_u=r ac_s eXtens_d_na_iS eXtens(_esct st;

id oame_map *p, char term kstnitx_.uct 	r_ (Uof(kstnitx_.uct ))id	e->m;

)ng LEMelsece->mcharx_ sg _interfa
 * _ude <lidesc(== strucharx_, kstnitx_.uct 	ng  		 _ (Uof(kstnitx_.uct ))id	elsectrol;

id s *_er melem_  i stru&== unitoer m,ng   aia=kstnitx_.uct 	=_ (Uof(kstnitx_.uct ),U0)id	 e->m!;

)ng  heck whekstnitx_.uct 	r"r c",#_ (Uof(kstnitx_.uct ))idd	 e->mdescit_DescriplabSubtnfie== e a2k source/*
 * m)ng  appendt
	i_tem_ kstl, "bCparseplx03,")LEMpelsece->m(== unitoer m.onfie&ualff00on=nval 100)ng  appendt
	i_tem_ kstl, "bCapf teeplx03,")LEMpelseng  appendt
	i_tem_ kstl, "bPlaybackeplx03,")LEMammebmix_name_dbg(== uniter;
	 "[%d] SU [%s]Dcermsid %d\n",ng 
	iaort fox_nakstnitx_.uct 	rdescit_NrInPinsu;dne->m(errid _interfadB) {
/
#_sine in  i unitii) {, kst ))i< 0i	im*/
staterr
	me if the0id chec
 hen cloTaneaudio d_naere
ursLiclyand thuct usbeturn clox_name_d_na  int control)
{
	const struct usbmix_ statnitbitmapoCLAREpoidate->mtest_ ad
s *_bna sbmix_na i unitsbmiio_teri)rol*/
sta 0;  i 
stad_naeal iady visitedmame
	p1 =tt uni_name_sine in_d_na  i strusbmix_); i || !p1antrolbmix_name_err(== uniter;
	 "d_nam%d not f unc!\n",rsbmix_);_Mp if the-EINVA{
	n}m
ese Fed
(p1[2]ontrocasmer a_INPUT_TERMINAL:rocasmer a2k source*/
	U:rol*/
sta 0;  i NOP h/dnoasmer a_MIXER_UNIT:rol*/
sta n clox_name_ntrol)d_na  i strusbmix_nap1)id	oasmer a_e/*
 * m_UNIT:rocasmer a2k source/*
 * m:rol*/
sta n clox_name_s eXtens_d_na  i strusbmix_nap1)id	oasmer a_FEATURE_UNIT:rol*/
sta n clox_name_teaf te_d_na  i strusbmix_nap1)id	oasmer a1_PROCESSING_UNIT:ro/*   r a2kEFF
 *_UNIT hasA_SELEct pe auig*/E	te->ms_ unitii) {itproIncturn Nr a_VERSION_1u			l*/
sta n clox_name_process <lid_na  i strusbmix_nap1)id	pelseng  */
sta 0;  i FIXME - effect d_nas>not TY oemen ed,y inametoasmer a1_EXTENSION_UNIT:ro/*   r a2kPROCESSING_UNIT_V2 hasA_SELEct pe auig*/E	te->ms_ unitii) {itproIncturn Nr a_VERSION_1u			l*/
sta n clox_name_exernsichid_na  i strusbmix_nap1)id	pelseccher a_VERSION_2sametel*/
sta n clox_name_process <lid_na  i strusbmix_nap1)id	casmer a2kEXTENSION_UNIT_V2:rol*/
sta n clox_name_exernsichid_na  i strusbmix_nap1)id	defa ch:ng bmix_name_err(== uniter;
	al)
"d_nam%u:usbexpec edeonfie0x%02x\n",rsbmix_nap1[2]o;_Mp if the-EINVA{
	n}m chuct usbix_na_interfadB) {
 and  int coerfadB) {
er;
	unsig*dB) { statk and(ii) {itad_		cvsu;dne->mii) {iturbontrolk and(ii) {iturb->transfol)
{ffero;	imumix and_urbmii) {iturboLEM}hlumix and_urbmii) {itrc_urboLEMk and(ii) {itrc_s tup_paedetoLEMk and(ii) {)id chuct usbetur_interfadB) {
deva and  int co_intvevis.l*devis. stats_mixer p->dB) {
er;
	unsig*dB) { =*devis.->devis._data;	n_interfadB) {
 and ii) {)ide if the0id chec
 hec iatd the pae
	retusde 
i* walk/c rougnn	lltr a_OUTPUT_TERMINAL descriplabh Incseared
f (pthe psde <luct usbetur_interfadB) {
e
	retus  int coerfadB) {
er;
	unsig*dB) { stat int control)
{
	co== un steturerr;atctor_mruct usbmix_secel_mamap *seleix_na*selmemcvset(& i stru0,#_ (Uof( i st))id	 i st.er;
id dB) {iter;
id	 i st.dB) { =*ii) {
		 i st.
{fferid dB) {ithostif->exera
		 i st.
{f;

id dB) {ithostif->exeralen s
 /er
ored but mapp <latrces ametf (p-ma
 =tbmix_secel_mams;*mamitad;*mam!controle->mmamitadrn N i st.er;
foerfacd)ntro		 i st.da
 =tmamit *sele		 i st.s eXtens_da
 =tmamits eXtens_da
ele		ii) {itap *p)_
	i_error =tmamitap *p)_
	i_errorele		break;EMm}rn}m
ep d +) {LEMangged((pid _interfat unics <a_desc(dB) {ithostif->exera,ng  		 ===dB) {ithostif->exeralen,ng  		 ===
	 r a_OUTPUT_TERMINAL))i!d +) {ontrole->mmi) {itproIncturn Nr a_VERSION_1u {ng ts_mixer ac1_MERput_ap;
in_n_descriplabl*desconvp;_	imi || descit_LengFre< _ (Uof(*_esct)ng  	sineinu,Lcche, v a c descriplab?sametelch mark/cp;
in_niID,as visitedmame	g s *_bna descit_Tp;
in_nIDru i st.sbmiio_teriele		 i st.oer m.x_u=rdescit_Tp;
in_nIDele		 i st.oer m.onfied le16_to_cpu(descitwTp;
in_nTnfio;	i		 i st.oer m.
checd descitiTp;
in_n;	i		errid n clox_name_d_na & i strudescit_plx03,ID);	imi || erri< 0o&& erri!d -EINVA{i	im	i*/
staterr
	nMa elsect cher a_VERSION_2sametelruct usbac2_MERput_ap;
in_n_descriplabl*desconvp;_	imi || descit_LengFre< _ (Uof(*_esct)ng  	sineinu,Lcche, v a c descriplab?sametelch mark/cp;
in_niID,as visitedmame	g s *_bna descit_Tp;
in_nIDru i st.sbmiio_teriele		 i st.oer m.x_u=rdescit_Tp;
in_nIDele		 i st.oer m.onfied le16_to_cpu(descitwTp;
in_nTnfio;	i		 i st.oer m.
checd descitiTp;
in_n;	i		errid n clox_name_d_na & i strudescit_plx03,ID);	imi || erri< 0o&& erri!d -EINVA{i	im	i*/
staterr
	etelch

	
i* FnsCK a2,luloT_SELEct papproaed
ren	lsen	ddT_SE

	
i* cpars s eXtenss

	
i*/	im	errid n clox_name_d_na & i strudescit_Cplx03,ID);	imi || erri< 0o&& erri!d -EINVA{i	im	i*/
staterr
	nMamoamme if the0id chix_na_interfadB) {
notifyacd  int coerfadB) {
er;
	unsig*dB) {ruct usbmix_ stats_mixer p->dB) {
		cval->dBCl->d
		if (p-l->dB=rdB) {itad_		cvs[sbmix_];ll->d;ll->dBnadi>dn bext_ad_		cv sg _int
	i_totify(dB) {iter;
fosard, SNDRV_CTL
	VENT_MASK_VA{UE	
 		 ======di>dn 		cvald)id chuct usbix_na_interfadB) {
dump_ort   int co_intdi>d)
{ffer *
{ffer,ng   aia=ct usbmix_nal)
{
	ias_mixer p->dB) {
		cval->dBCTO_n stats_t usboCLAREvt _onfis[]id u"BOOLEAN",#iINV_BOOLEAN",al)
{
	ia"S8",#iU8",#iS16",#iU16"};	n_intiprir;f 
{ffer, " CK_RA: %i\n",rsbmix_);_M || ort fo		cvald)sg _intiprir;f 
{ffer, " C  Cine in:uchar=\"%s\"rucidex=%i\n",ng  	srt fo		cvaldn uct 	rsrt fo		cvaldn cidex);	n_intiprir;f 
{ffer, " C  I->d: id=%i, e
	retu=%i, emask=0x%x, "
 		 ==="chame;
}=%i, onfi=\"%s\"\n",rort fox_n
 		 ===ert foe
	retu	rert foemasknaort fochame;
},ng  aia vt _onfis[ert fovt _onfi]);	n_intiprir;f 
{ffer, " C  Voluar:/iiE=%i, max=%i, r UAC=%i, r Uax=%i\n",ng  a rport fothn, ort foiax, ort for UAC, ort for Uax);_ chuct usbix_na_interfadB) {
proc_ iad  int co_intdi>d)en ry *en rynal)
{
	ias_mixer_intdi>d)
{ffer *
{ffer stats_mixer_interfaaudio *er;
id en ryitprivate_data;	n int coerfadB) {
er;
	unsig*dB) {;ats_mixer p->dB) {
		cval->dBCTO_nelect usbmix_; 	i;ist_= 0_eaed)en ry(dB) {ru&er;
fodB) {
listn listontrol_intiprir;f 
{ffer,al)
"r coM_ser: erfacd=0x%08x, otrlif=%i, etlerr=%i\n",ng  	sr;
foerfacd, _interfa
trl_ir;f sr;
),ng  	ii) {itap *p)_
	i_errori;sg _intiprir;f 
{ffer, "Card: %s\n",ror;
fosard->longuct );
tef (p-sbmix_id 0L sbmix_i< oterID
	conSL sbmix_!controllf (p-TO_nid dB) {itad_		cvs[sbmix_];lTO_nele	g  	srt i=rort fobext_ad_		cv sg 		_interfadB) {
dump_ort  
{ffer, sbmix_naort ); il}	n}m chuct usbix_na_interfadB) {
er;
	rupt_v2  int coerfadB) {
er;
	unsig*dB) {ral)
{
	iaia=ct uattribustruct ue aui	returdidex)stats_mixer p->dB) {
		cval->dBCl->d
	i__u8 sbmix_id (didexe>> 8)e&ualff
	i__u8 e
	returna e auig>> 8)e&ualff
	i__u8 ename;
l=pe auig&ualff
		i || erame;
l>= oterCHANNELSintrolbmix_name_dbg(dB) {iter;
,al)
"%s(): bogust name;
lnumb
 *%d\n",ng  __func__,t name;
); il*/
sta
	nam	if (p-l->dB=rdB) {itad_		cvs[sbmix_];ll->d;ll->dBnadi>dn bext_ad_		cv ntrole->ml->dfoe
	retu !=*oine in)rol	sineinu,LEsg _e Fed
(attribust)ntroloasmer a2k S_CUm:rol	che, v a catepsaed 	=_nc., 5e auigisp iadgfxorgrhs devis.l*/	im		|| ehame;
)al)
{l->dfoeaed di&= ~(1t<<  name;
); ilpelsecchema cont name;
l(copi
{l->dfoeaed di=*0;		im	_int
	i_totify(dB) {iter;
fosard, SNDRV_CTL
	VENT_MASK_VA{UE	
 		
{l->dfo		cvald)id	Mpbreak;r	nloasmer a2k S_RANGE:rol	cheTODOl(copi
break;r	nloasmer a2k S_MEM:rol	cheTODOl(copi
break;r	nldefa ch:ng lbmix_name_dbg(dB) {iter;
,al)

"d_knownuattribustm%d in er;
	rupt\n",ng  	attribust)ele		break;EMm}cche_e Fed
(cop}m chuct usbix_na_interfadB) {
er;
	rupt  int coerb *urbostats_mixer p->dB) {
er;
	unsig*dB) { =*urb->sineextelect u;

id urb->actixe_lengFrelect usuct usid urb->uct us
		i || suct usi!=*0i	imgoren iqueur;e
t || mi) {itproIncturn Nr a_VERSION_1u {ng s_mixer ac1_uct us_worconst sus
		iif (p-sct usid urb->transfol)
{ffer;ng 
	iau;

i>= _ (Uof(*sct us);ng 
	iau;

i-= _ (Uof(*sct us)ru i sus!controlldevadbg(&urb->dev->dev, "sct usier;
	rupt: %02x %02x\n",le	g  	sct usit_pct usTnfi,le	g  	sct usit_Origin_tori;srol	che,p *p)many totific03ichs>not fxorgrhs hine inter;
	unsig*/	im		|| (sct usit_pct usTnfig&ur a1_STATUS_TYPE_ORIG_MASK)i!dle	g r a1_STATUS_TYPE_ORIG_AUDIO_CONTROL_IF)ng  	sineinu,L_	imi || sct usit_pct usTnfig&ur a1_STATUS_TYPE_MEMrCHANGED sg 		_interfadB) {
rc_memory_nompgi(dB) {rusct usit_Origin_tori;s	pielseng  	_interfadB) {
notifyacd dB) {rusct usit_Origin_tori;s	p}dna elsect cher a_VERSION_2sameteruct usbac2_er;
	rupt_data_msgg*dsg
		iif (p-msggd urb->transfol)
{ffer;ng 
	iau;

i>= _ (Uof(*msg);ng 
	iau;

i-= _ (Uof(*msg), msg!controllchevrop vendve specams e addendpo, MA iquestsg*/	im		|| (msgit_I->dB&er a2kINTERRUPT_DATA_MSG_VEND m)n||ng  a rp(msgit_I->dB&er a2kINTERRUPT_DATA_MSG_EPt)ng  	sineinu,L		im	_interfadB) {
er;
	rupt_v2 dB) {rumsgit_Attribustrle	g  	iau;
16_to_cpu(msgitwVt ur)rle	g  	iau;
16_to_cpu(msgitwIidex))
	nnamoamm iqueur:	i || suct usi!=*-ENOENT &&
)l ==suct usi!=*-ECONNRESET &&
)l ==suct usi!=*-ESHUTDOWNintrolbrb->devid dB) {iter;
->dev;	imumixsubmi3_urbmurb, GFP_ATOMIC);EM}n checec iatd rhs ompdl
 *f r rhs opted al sct usier;
	ruptdendpo, MA <luct usbetur_interfadB) {
uct us_c iatd  int coerfadB) {
er;
	unsig*dB) { stat int coerfaendpo, M_descriplabl*eseleix_na*transfol)
{ffer;ngetur
{ffer_lengFrelenitbitmap, MAepnum s
 /erwm nemapcheier;
	ruptderputtendpo, MA <li || s *_iunsi_desc(dB) {ithostif)it_NumEndpo, Mse< 1)rol*/
sta 0;n	epid s *_endpo, M(dB) {ithostif,U0)id	e->m!e