/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *	NOTE TO LINUX KERNEL HACKERS:  DO NOT REFORMAT THIS CODE!
 *
 *	This is shared code between Digi's CVS archive and the
 *	Linux Kernel sources.
 *	Changing the source just for reformatting needlessly breaks
 *	our CVS diff history.
 *
 *	Send any bug fixes/changes to:  Eng.Linux at digi dot com.
 *	Thank you.
 */

/************************************************************************
 *
 * This file implements the tty driver functionality for the
 * Neo and ClassicBoard PCI based product lines.
 *
 ************************************************************************
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>	/* For jiffies, task states */
#include <linux/interrupt.h>	/* For tasklet and interrupt structs/defines */
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_reg.h>
#include <linux/slab.h>
#include <linux/delay.h>	/* For udelay */
#include <linux/uaccess.h>	/* For copy_from_user/copy_to_user */
#include <linux/pci.h>

#include "dgnc_driver.h"
#include "dgnc_tty.h"
#include "dgnc_types.h"
#include "dgnc_neo.h"
#include "dgnc_cls.h"
#include "dpacompat.h"
#include "dgnc_sysfs.h"
#include "dgnc_utils.h"

#define init_MUTEX(sem)	 sema_init(sem, 1)
#define DECLARE_MUTEX(name)     \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name, 1)

/*
 * internal variables
 */
static struct dgnc_board	*dgnc_BoardsByMajor[256];
static unsigned char		*dgnc_TmpWriteBuf;
static DECLARE_MUTEX(dgnc_TmpWriteSem);

/*
 * Default transparent print information.
 */
static struct digi_t dgnc_digi_init = {
	.digi_flags =	DIGI_COOK,	/* Flags			*/
	.digi_maxcps =	100,		/* Max CPS			*/
	.digi_maxchar =	50,		/* Max chars in print queue	*/
	.digi_bufsize =	100,		/* Printer buffer size		*/
	.digi_onlen =	4,		/* size of printer on string	*/
	.digi_offlen =	4,		/* size of printer off string	*/
	.digi_onstr =	"\033[5i",	/* ANSI printer on string ]	*/
	.digi_offstr =	"\033[4i",	/* ANSI printer off string ]	*/
	.digi_term =	"ansi"		/* default terminal type	*/
};


/*
 * Define a local default termios struct. All ports will be created
 * with this termios initially.
 *
 * This defines a raw port at 9600 baud, 8 data bits, no parity,
 * 1 stop bit.
 */
static struct ktermios DgncDefaultTermios = {
	.c_iflag =	(DEFAULT_IFLAGS),	/* iflags */
	.c_oflag =	(DEFAULT_OFLAGS),	/* oflags */
	.c_cflag =	(DEFAULT_CFLAGS),	/* cflags */
	.c_lflag =	(DEFAULT_LFLAGS),	/* lflags */
	.c_cc =		INIT_C_CC,
	.c_line =	0,
};


/* Our function prototypes */
static int dgnc_tty_open(struct tty_struct *tty, struct file *file);
static void dgnc_tty_close(struct tty_struct *tty, struct file *file);
static int dgnc_block_til_ready(struct tty_struct *tty, struct file *file, struct chan***ap hig *chtatic int dgnc_blocstruioctluct tty_struct *tty, strugned chardgnccmdrugned char withargtatic int dgnc_blocstru_tergls.uct tty_struct *tty, struct file_t dgnc_r */
#iretrmattatic int dgnc_blocstru_tersls.uct tty_struct *tty, struct file_t dgnc_r */
#inew_rmattatic int dgnc_blocstrue to _roomuct tty_struct *tty, sttatic int dgnc_blocstruput_s inuct tty_struct *tty, strugned char =	50ctatic int dgnc_blocstrus in _rm_er sizuct tty_struct *tty, sttatic int  dgnc_tty_closic rtuct tty_struct *tty, sttatic int  dgnc_tty_closicopuct tty_struct *tty, sttatic int  dgnc_tty_closthtyptlruct tty_struct *tty, sttatic int  dgnc_tty_closunthtyptlruct tty_struct *tty, sttatic int  dgnc_tty_closflushus in uct tty_struct *tty, sttatic int  dgnc_tty_closflushuer sizuct tty_struct *tty, sttatic int  dgnc_tty_closes tupuct tty_struct *tty, sttatic int dgnc_blocsls_le.em_rmatuct tty_struct *tty, strugned chardgnccommintrugned chardgnc_r */
#ivalustatic int dgnc_blocgls_le.em_rmatuct tty_***ap hig *chrugned chardgnc_r */
#ivalustatic int dgnc_blocclostiocmglsuct tty_struct *tty, sttatic int dgnc_blocstrutiocmslsuct tty_struct *tty, strugned chardgncslsrugned chardgnccleartatic int dgnc_blocstrusany_ks
 *uct tty_struct *tty, strudgncmslctatic int  dgnc_tty_closwaUTEunteadsansuct tty_struct *tty, strudgnctimeouttatic int dgnc_blocstrue to uct tty_struct *tty, strucr =	ugned char =	50*er ,rdgnccounttatic int  dgnc_tty_closils_ios Dgnuct tty_struct *tty, struct filemios Dgnc*old_ios Dgntatic int  dgnc_tty_closilny_xs inuct tty_struct *tty, stru =	50chtat
tic int cr =	uct tty_stru(strn.
 *sc_tty_open(s{
	.c_if(str
	._tty_open(strc_lin(str
	._tty_openn(strc_lie to t=c_blocstrue to c_lie to _roomt=c_blocstrue to _roomc_liflushuer sizt=c_blocstruflushuer sizc_lin in _rm_er siz
	._tty_openn in _rm_er sizc_liflushus in p=c_blocstruflushus in c_liioctlp=c_blocstruioctlc_liils_ios Dgnp=c_blocstruils_ios Dgnc_liibit.=c_tty_closicopc_liib rt.=c_tty_closic rtc_lithtyptlr.=c_tty_closthtyptlrc_liunthtyptlr.=c_tty_closunthtyptlrc_lies tup.=c_tty_closes tupc_liput_s in.=c_tty_closput_s inc_litiocmgls.=c_tty_clostiocmglsc_litiocmsls.=c_tty_clostiocmslsc_liks
 *_ctlp=c_blocstrusany_ks
 *c_lieaUTEunteadsanst=c_blocstrueaUTEunteadsansc_liilny_xs inp=c_blocstrusany_xs in

/* *********************************************************************
 *
 * This TY Ially.
izn.
 */Cleanup.Fion pros ************************************************************************
 *
/
 * Defi_tty_clospre(sem,) * ThisIally.
ize bug globype	ty rer veda bits,matte we downloay bug fsByMa/
statdgnc_blocstrupre(sem, dgn)
c_i* D	hispor detlocaer siz
the
dothe sour of t_use  */
#entc theD	hiskl sourcntc trin_bloce to u).  W prnly  */prneaer siz
intD	hiscr trol ss.h>	the ill bhisaaphore nam.  If we Founpa the, weD	hisFounaly(strthe hroublurceprneaer siz
won't hurt.much bugwa*
 	hiD	hisWe Founokaythe sleepthe hopemaor d,publ termroutrmeD	hisermrnly defleda u ]	*/le.h>
 loay, (noformerrupt strucr text),D	hisFndl bhisari_tils held
 	hic_c_TmpWriteBuf;
stp=ckmaor d(WRITEBUFLEN, GFP_EL HACtat
	if (!_TmpWriteBuf;
st)
		return -ENOMEMat
	return 0;
}

 * Defi_tty_closreersupt,) * ThisIall tty drivsubsysupm the
 *ermfsByM/
statdgnc_blocstrureersupt,ct dgnc_board	*dg0*ern)
c_idgncr	IN 0;

	ern->Sl_regDr.h"
#ma t	IN  TY_DRIVER_MAGIC;

	snter of(ern->Sl_regN 1)

MAX TYNAMELEN, "stru_boar%d_", ern->d	*dgnu
/*
 	ern->Sl_regDr.h"
# = __SEern->Sl_regN 1); 	ern->Sl_regDr.h"
# = __d prIN 0;
	ern->Sl_regDr.h"
#majorIN 0;
	ern->Sl_regDr.h"
#minor_ib rt.=c0;
	ern->Sl_regDr.h"
#nu
_SEern->maxs wil;
	ern->Sl_regDr.h"
#s */IN  TY_DRIVER_TYPE_SERIAL;
	ern->Sl_regDr.h"
#subs */IN SERIAL_TYPE_NT THL;
	ern->Sl_regDr.h"
#_MUTEios Dgnp=cDefaultTermios = {;
	ern->Sl_regDr.h"
#er.h"
_ = __SEDRVSTR;
	ern->Sl_regDr.h"
#s =	DIG ( TY_DRIVER_REAL_RAW |  TY_DRIVER_DYNAMIC_DEV |  TY_DRIVER_HARDWMUTEBREAK/*
 	* D	hisTty kl sourwathe entc thep biounporruptstheD	hisstruct *tt's the
 os = {'s
 	hic_cern->Sl_regDr.h"
#styDIG kcaor d(ern->maxs wil,e of of(*ern->Sl_regDr.h"
#styD), GFP_EL HACtat	if (!ern->Sl_regDr.h"
#styD)
		return -ENOMEMat
	kreft(sem,&ern->Sl_regDr.h"
#kref);
	ern->Sl_regDr.h"
#sos Dgnp=ckcaor d(ern->maxs wil,e of of(*ern->Sl_regDr.h"
#sos Dgnt, GFP_EL HACtat	if (!ern->Sl_regDr.h"
#sos Dgnt
		return -ENOMEMat
	* D	hisE trynporrus
the
dr.h"
#  Cefledaby tty kl sour_useD	hisstruio.c the
n.h"
#c
 	hic_cstruils_(strn.
 *s,&ern->Sl_regDr.h"
, &_tty_open(s{tat
	if (!ern->_tty_r[256_Sl_reg_Reersupted).c_ilflaReersupt driveevic/
stat		r	IN strureersuptver.h"
,&ern->Sl_regDr.h"
tat		if (r	I< 0).c_il	APR(("Can't reersupt driveevic/ (%d)\n", rc)tat			return rcat		}t		ern->_tty_r[256_Sl_reg_ReersuptedIN  RUEat	}t
	* D	hisIf we'etaiothe sparent print inf, wee recehepdonallthe GNU aboveD	hisFgain,e ent etlly cepwe don't gls.GNU LDucr fuprodaboull it wmajorD	hiswe Founwhtr
we gls.infe Free_tty_open(stru)mroutrme
 	hic_cern->ter bDr.h"
#ma t	IN  TY_DRIVER_MAGIC;
	snter of(ern->ter bN 1)

MAX TYNAMELEN, "pru_boar%d_", ern->d	*dgnu
/*
 	ern->ter bDr.h"
# = __SEern->ter bN 1); 	ern->ter bDr.h"
# = __d prIN 0;
	ern->ter bDr.h"
#majorIN ern->Sl_regDr.h"
#major;
	ern->ter bDr.h"
#minor_ib rt.=c0x80;
	ern->ter bDr.h"
#nu
_SEern->maxs wil;
	ern->ter bDr.h"
#s */IN  TY_DRIVER_TYPE_SERIAL;
	ern->ter bDr.h"
#subs */IN SERIAL_TYPE_NT THL;
	ern->ter bDr.h"
#_MUTEios Dgnp=cDefaultTermios = {;
	ern->ter bDr.h"
#er.h"
_ = __SEDRVSTR;
	ern->ter bDr.h"
#s =	DIG ( TY_DRIVER_REAL_RAW |  TY_DRIVER_DYNAMIC_DEV |  TY_DRIVER_HARDWMUTEBREAK/*
 	* D	hisTty kl sourwathe entc thep biounporruptstheD	hisstruct *tt's the
 os = {'s
  Mfor bee ent etldr_useD	hissty Sl_reg Dr.h"
 cepwe don't gls.cr fupro 	hic_cern->ter bDr.h"
#styDIG kcaor d(ern->maxs wil,e of of(*ern->ter bDr.h"
#styDt, GFP_EL HACtat	if (!ern->ter bDr.h"
#styDt
		return -ENOMEMat	kreft(sem,&ern->ter bDr.h"
#kref);
	ern->ter bDr.h"
#sos Dgnp=ckcaor d(ern->maxs wil,e of of(*ern->ter bDr.h"
#sos Dgnt, GFP_EL HACtat	if (!ern->ter bDr.h"
#sos Dgnt
		return -ENOMEMat
	* D	hisE trynporrus
the
dr.h"
#  Cefledaby tty kl sour_useD	hisstruio.c the
n.h"
#c
 	hic_cstruils_(strn.
 *s,&ern->ter bDr.h"
, &_tty_open(s{tat
	if (!ern->_tty_r[256_Tparent priter b_Reersupted).c_ilflaReersupt Tparent printer bveevic/
stat		r	IN strureersuptver.h"
,&ern->ter bDr.h"
tat		if (r	I< 0).c_il	APR(("Can't reersupt Tparent printer bveevic/ (%d)\n", rc)tat			return rcat		}t		ern->_tty_r[256_Tparent priter b_ReersuptedIN  RUEat	}t
	_BoardsByMajor[256]ern->Sl_regDr.h"
#major]_SEern;
	ern->_BoarSl_reg_MajorIN ern->Sl_regDr.h"
#major;
	ern->_TmpWrparent priter b_MajorIN ern->ter bDr.h"
#majorat
	return rcat}

 * Defi_tty_clos(sem,) * ThisIall tty drivsubsysupm#  Cefledaonc/ puffe	*dg0aftuffe	*dg0hublbeenDefi_ownloayrodanterrit'eM/
statdgnc_blocstru(sem, t dgnc_board	*dg0*ern)
c_idgnci;
	 dgnc_uiompm *vaddr;
	ct tty_***ap hig *chat
	if (!ernt
		return -ENXIOat
	* D	hisIally.
ize d	*dg0ct ttyuounents the
 	hic_
	 addrIN ern->re_map_mpmd pr*
 	ern->nasync_SEern->maxs wil;
_i* D	hispor detlo***ap h mpmoryt it wm 2003nofo recebeen aor detltD	hiswhtr
Free_r.h"
 wale iror loayro
 	hic_cthe
(iIN 0; iI< ern->nasync; i++).c_ilif (!ern->***ap hs[i]).c_t			* D			hisOkaythe maor dl bhisGFP_EL HAC,swe FounnofoatD			hisrrupt strucr text, the
 *	re Founnoi_tils held
 			hic_c		ern->***ap hs[i]p=ckzaor d( of of(*ern->***ap hs[i]), GFP_EL HACtat		}t	}t
	ch_SEern->***ap hs[0];
	 addrIN ern->re_map_mpmd pr*
 	flaSls.upo***ap h ables
 */hic_cthe
(iIN 0; iI< ern->nasync; i++ru =_SEern->***ap hs[i]).c_t		if (!ern->***ap hs[i])_c		cr tinur*
 		spin__til_(sem,&ch->**__tiltat
		flaSbiounallthur ma t	Inu
bptstic_c	ch->ma t	IN DGNC_TABIHAC_MAGIC;
		ch->**_tun.ma t	IN DGNC_UC_CCMAGIC;
		ch->**_tun.un_ =_SEchat		ch->**_tun.un_s */IN DGNC_SERIAL;
		ch->**_tun.un_eevIN iat
		ch->**_pun.ma t	IN DGNC_UC_CCMAGIC;
		ch->**_pun.un_ =_SEchat		ch->**_pun.un_s */IN DGNC_PRINTat		ch->**_pun.un_eevIN i + 128;_t		if (ern->dd_u rtstr =ls.==c0x200)_c		ch->**_neo_u rtIN  addrI+ (ern->dd_u rtstr =ls.isrtat		 hse_c		ch->**_cls_u rtIN  addrI+ (ern->dd_u rtstr =ls.isrtatt		ch->**_bd_SEern;
		ch->**_p winu
_SEi;
		ch->**_ dot =c_digi_init = {at
		fla.25e ecr d y */
#inc		ch->**_close_y */
#= 250;_t		iMUTEeaUTe	*/
_head,&ch->**_s =	DEeaUTtat		iMUTEeaUTe	*/
_head,&ch->**_tun.un_s =	DEeaUTtat		iMUTEeaUTe	*/
_head,&ch->**_pun.un_s =	DEeaUTtat		iMUTEeaUTe	*/
_head,&ch->**_sniffEeaUTtatt		c_il	 t dgnc_evic/ *cicBopatt			cicBopIN strureersuptveevic/,&ern->Sl_regDr.h"
, i,t				&(ch->**_bd->peev->_ev)tat			ch->**_tun.un_s.h"
_SEcicBopat			_cls.hed
 *cstrus.h"
,&ch->**_tun,EcicBop)att			cicBopIN strureersuptveevic/,&ern->ter bDr.h"
, i,t				&(ch->**_bd->peev->_ev)tat			ch->**_pun.un_s.h"
_SEcicBopat			_cls.hed
 *cstrus.h"
,&ch->**_pun,EcicBop)at		}t
	}t
	return 0;
}

 * Defi_tty_closposTEun(sem,) * ThisUnIally.
ize bug globype	ty rer veda bit/
stat dgnc_tty_closposTEun(sem, dgn)
c_ik sof(_TmpWriteBuf;
st);_c_TmpWriteBuf;
stp=cNULL;
}

 * Defi_tty_closun(sem,) * ThisUnially.
ize Free TY p wiprothe GNefinr.h"
#   SoftalltmpmorytintDhisreces.
 *	Chaat dgnc_tty_closun(sem, t dgnc_board	*dg0*ern)
c_idgnciIN 0;

	if (ern->_tty_r[256_Sl_reg_Reersupted).c_il_BoardsByMajor[256]ern->Sl_regDr.h"
#major]_SENULL;
		ern->_BoarSl_reg_MajorIN 0;
	cthe
(iIN 0; iI< ern->nasync; i++).c_ill_Boarrpmov*cstrus.h"
,ern->***ap hs[i]->**_tun.un_s.h"
tat			closunreersuptveevic/,&ern->Sl_regDr.h"
, itat		}t		closunreersuptver.h"
,&ern->Sl_regDr.h"
tat		ern->_tty_r[256_Sl_reg_ReersuptedIN FALSEat	}t
	if (ern->_tty_r[256_Tparent priter b_Reersupted).c_il_BoardsByMajor[256]ern->ter bDr.h"
#major]_SENULL;
		ern->_Boarrparent priter b_MajorIN 0;
	cthe
(iIN 0; iI< ern->nasync; i++).c_ill_Boarrpmov*cstrus.h"
,ern->***ap hs[i]->**_pun.un_s.h"
tat			closunreersuptveevic/,&ern->ter bDr.h"
, itat		}t		closunreersuptver.h"
,&ern->ter bDr.h"
tat		ern->_tty_r[256_Tparent priter b_ReersuptedIN FALSEat	}t
	k sof(ern->Sl_regDr.h"
#styD);
	ern->Sl_regDr.h"
#styDIG NULL;
	k sof(ern->ter bDr.h"
#styDt;
	ern->ter bDr.h"
#styDIG NULL;
}

 ine DECLTMPBUFLEN (1024)
 * Defi_tty_sniff - Dumpa bitsoullfe Free"sniff"aer siz
if
 * Neo pr dlsniff  imples (strro
.	Chaat dgnc_tty_sniffEnoeaUTEno_tiluct tty_***ap hig *chrugned char =	50*text, gned char =	50*er ,rdgnclen)
c_ict tty_simevype	v;_idgncn;_idgncr;_idgncnbstatidgnci;
	dgnctmpbstlen;
	ch	50*tmpbst;
	ch	50*p;
	dgnctoo_much_ bit;

	tmpbstp=ckzaor d(TMPBUFLEN, GFP_ATOMICtat	if (!tmpbstt
		returnat	pIN smpbst;
 	flaLereceif sniff nofo(str
inc	if (!(ch->**_sniffEs =	DI& SNIFF_OPEN)t
		gofe ex {at
	docglstimeofday(&tv/*
 	*  Ced
 *thur headiz
the
dbitsdumpainc	p += ster of(p, "<%ld %ld><%s><",e	v.tv_ ec,e	v.tv_u ec,e	ext);
	tmpbst=	4,	 p - smpbst;
 	do.c_iltoo_much_ bitIN 0;

	cthe
(iIN 0; iI< =	4,&&ctmpbstlenI< (TMPBUFLEN - 4); i++).c_illp += ster of(p, "%02x ",e*er tat			er ++at			cmpbst=	4,	 p - smpbst;
		}t
		if (tmpbstlenI< (TMPBUFLEN - 4)).c_illif (i > 0)_c		lp += ster of(p - 1, "%s\n", ">"tat			 hse_c		lp += ster of(p, "%s\n", ">"tat		}  hse.c_illtoo_much_ bitIN 1at			lenI-SEi;
		}t
		nbstp=cct len(tmpbsttat		pIN smpbst;
 		* D		eo  Loopswhimpl bitIrpmaine
 		hic_c	whimpl(nbstp> 0,&&cch->**_sniffEbstt.c_ill* D			his Deinal te Freeamountthe availablurcntc tleftthe hopD			his er siz.  If  *	re's none,swander tiurcemeeappears
 			hic_c		4,	 (ch->**_sniffEoull-cch->**_sniffEhe - 1)I& SNIFF_MASK;_t			* D			hisIf  *	reles norcntc tlefttfe e to the he hur sniff er sizc_l		hiswe  recenorchoic/ bullfe dropsFree_bit/
l		hisW/ *c*apot* sleept*	relwandthe the
cntc , beca */pGNef
l		histion protwalepr bably defledaby tty rrupt str/timermroutrmes! 			hic_c		if (n.==c0)_c		lgofe ex {at
			* D			hisrighpublmuch  bitIall be cf */
			hic__c		if (n.>cnbst)_c		l4,	 nbst;
 			rIN SNIFF_MAXl-cch->**_sniffEhe;__c		if (r <	 nt.c_ill	mpmcpy(ch->**_sniffEbstI+ ch->**_sniffEhe, p, r)att				nI-SErat				ch->**_sniffEhe N 0;
	c	lp += rat				nbstp-SErat			}t
			mpmcpy(ch->**_sniffEbstI+ ch->**_sniffEhe, p, n)att			ch->**_sniffEhe += n;_illp += n;_illnbstp-SEnat
			* D			his Wakeup bug thy(stlwandthe the
 bit 			hic_c		if (ch->**_sniffEs =	DI& SNIFF_WAIT_DATAt.c_ill	ch->**_sniffEs =	DI&= ~SNIFF_WAIT_DATAat				wake_up_rrupt striblu,&ch->**_sniffEeaUTtat			}t		}
 		* D		eo If  *	  */
#eprinustheolmuch  bitIfe push.infe hur smpbst,
		hiswe lessIfe keeptloopiitharatiostrialltFree_bit/
l	hic_c	if (too_much_ bit).c_illp = smpbst;
			cmpbst=	4,	 0at		}t
	}swhimpl(too_much_ bit)at
ex {:
	k sof(tmpbsttat}

 * ======================================================================= *	Send_blocemov*l-ceBuf;  bitIfe sparemieue	*/

 *	Send	ch	- PorruptIfe ***ap h ct ttyuou	Than	er 	- PoruptIfe ***racuptsthe beemov*d	Than	n	- Nu
bptthe ***racuptsthe mov*
 *	Sen=======================================================================tatic int  dgnc_tty_emov*uct tty_***ap hig *chru =	50*er ,rudgncn)
c_idgn	rpmainat	udgn	headat
	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		returnat
	head_SEch->**_w_headI& WQUEUEMASK;_t	* D	hisIf Freee to twraps (vhe termsopshe GNU circular er sizc_lhismov*lGNU p wiprotup fe Freewrapnporru, the
recls.GNU_lhisporruptsthe Freebott *	T	hic_crpmain_SEWQUEUESIZEl-cheadat
	if (n.>=Irpmain).c_ilnI-SErpmainat		mpmcpy(ch->**_we	*/
I+ head, er ,rrpmain)at		head_SE0at		bstI+SErpmainat	}t
	if (n.> 0).c_il* D		eo Moeceivstthe _bit/
l	hic_c	rpmain_SEnat		mpmcpy(ch->**_we	*/
I+ head, er ,rrpmain)at		head_+SErpmainat	}t
	headI&= WQUEUEMASK;_	ch->**_w_headI=cheadat}

 
 * ======================================================================= *	Sen \
	sc_tty_inpull-cPr dh>	tived a co bit/
stThan \
	scch      - PorruptIfe ***ap h ct ttyuou	ThaSen=======================================================================tat dgnc_tty_inpuluct tty_***ap hig *ch)
c_ict tty__board	*dg0*ed;
	ct tty_struct *tty, p;
	ct tty_struldisc *ldat	udgn	rmaskat	ush wi	headat	ush wi	s.
 ;
	dgn	 bit_len;
	gned char withs =	D;
	dgnc.h>
_len;
	dgnclen_SE0at	dgncn_SE0at	dgncs_SE0at	dgnciIN 0;

	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		returnat
	tp_SEch->**_tun.un_sty*
 	ed_SEch->**_ed;
	if (!ed || bd->ma t	I!N DGNC_BOARD_MAGICt
		returnat
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	* D	hissssssFiguounFreenu
bptthe ***racuptsthe hopeer siz.D	hissssssEx { immedietlly ot, wme
 	hic_crmask_SERQUEUEMASK;_	head_SEch->**_r_headI& rmaskat	s.
 _SEch->**_r_s.
 _& rmaskat	 bit_len,	 (headI- s.
 )_& rmaskat
	if ( bit_len,	= 0).c_ilspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
		returnat	}_t	* D	hisIf Free_evic/ es nofo(str (at CREADles (ffc_lhisflush inpull bitIahe
return immedietlly
 	hic_cif (!tp || (tp->ma t	I!N  TY_MAGICt || !(ch->**_tun.un_s =	D_& UN_ISOPEN) ||
n \
	!(tp->sos Dgnflag =	(D& CREADt || (ch->**_tun.un_s =	D_& UN_CLOSING)).c_t		ch->**_r_headI= s.
 ;

		flaFojuste	*/
Iflowscr trol he beerele pro, ot, essharic_c	_cls.hheil_e	*/
_flow_cr trol(chtat
ilspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
		returnat	}_t	* D	hisIf we Founthtyptlrd,e omply don't y(stlbug _bit/
lhic_cif (ch->**_s =	DD& CH_FORCED_STOPI).c_ilspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
		returnat	}_t	.h>
_lenIN  TY_FLIPBUF_SIZE*
 	*  Chops_own Freelength, ot, essharic_clenIN l t( bit_len,c.h>
_len/*
	lenIN l t(len,c(N_ TY_BUF_SIZE - 1)tat
ildIN struldisc_ref(tptat
#ifdef  TY_DONT_FLIPt	* D	hisIf FreeDONT_FLIPhs =	sermrn, don't flush hur er sizcIahe
acuD	hislikunFreeldIdoesn't have bug entc theppullFree_bit r 2003now/
lhic_cif (tvst_bit( TY_DONT_FLIP, &tp->s =	D/t
		len_SE0at#endif_t	* D	hisIf we w	relunablurhepgls.tIrpsizenc the Freeldc_lhisdon't flush hur er sizcIahe
acuslikunFreeldIdoesn't_lhishave bug entc theppullFree_bit r 2003now/
lhic_cif (!ld).c_illen_SE0at	}  hse.c_il* D		eo If ldIdoesn't have bsporruptthepatived a EbstItion pro,
		hisflush Free_bit, Fren
acuslikunFreeldIdoesn'tshave bug
		hisentc theppullFree_bit r 2003now/
l	hic_c	if (!ld->ops->reed a Ebst).c_illch->**_r_headI= ch->**_r_s.
 at			lenI	 0at		}t	}t
	if (lenI<= 0).c_ilspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
		if (lnt
			struldisc_deref(lnt*
		returnat	}_t	* D	hisTty drivlayiz
in tty kl sourhublges to the 2.6.16+
 	hiD	hisTty .h>
 er sizsthe hopedrivst ttyuounFounnoi_t tor expopro,D	hisFndlpr bably  be creagothe awayn thetu.
 *
 	hiD	hisIf we Founat.hletlly raw,pwe don't lessIfe gonthtyughocal t_lhishe GNU drivlayizst it wex st.D	hisIa GNefic pr,pwe takunFreesh wivsttFndlfasivsttroute weD	hisredife r */
#Free_bit he Free */

 	hiD	hisOe hopeo vershintruifswe Founnoforaw,pwe lessIfe gonthtyughD	hissty new 2.6.16+ drivlayiz,swhich hublits APIe detawellines a o
 	hic_clenI	 struer siz_ree	*st_roomutp->p wi, len/*
	nI	 len;
t	* D	hisn3nowscr taine hopemosttFmountthe _bit we redi of ,D	hisbatiossIer versby howsmuch GNU L at ddrivlayiz
redihintlrc_lhishr Freeamountthe _bit hNU c*dg0actu.
 * hublpending
.	C	hic_cwhimpl(n).c_ils,	 ((headI>= s.
 )_?chead :ERQUEUESIZE)I- s.
 ;_ils,	 l t(s, n)att		if (sI<= 0)t			es
 *;
 		* D		eo If cr di.
 *scFounsuch GNt wldIlesssthe softallD		eo UART errors,pwe  be c recehepwalk each ***racuptD		eo Fndlerrorsbytd the
sany hNUmthe Freebr siz
rneaatD		eo F_sime/
l	hic_c	if (I_PARMRK(tpt || I_BRKINT(tpt || I_INPCK(tpt).c_illthe
(iIN 0; iI< s; i++).c_ill	if (*(ch->**_ee	*/
I+ s.
 _+ i)_& UART_LSR_BI)_c		l	stru(s*/
tp.h>
_s inutp->p wi, *(ch->**_re	*/
I+ s.
 _+ i),  TY_BREAK/*
				 hse if (*(ch->**_ee	*/
I+ s.
 _+ i)_& UART_LSR_PE)_c		l	stru(s*/
tp.h>
_s inutp->p wi, *(ch->**_re	*/
I+ s.
 _+ i),  TY_PARITY/*
				 hse if (*(ch->**_ee	*/
I+ s.
 _+ i)_& UART_LSR_FE)_c		l	stru(s*/
tp.h>
_s inutp->p wi, *(ch->**_re	*/
I+ s.
 _+ i),  TY_FRAME/*
				 hse_c		l	stru(s*/
tp.h>
_s inutp->p wi, *(ch->**_re	*/
I+ s.
 _+ i),  TY_NT THLtat			}t		}  hse.c_illttru(s*/
tp.h>
_ng ]	*utp->p wi, ch->**_re	*/
I+ s.
 ,e )at		}t
		_tty_sniffEnoeaUTEno_tiluchru"USER READ", ch->**_re	*/
I+ s.
 ,e )at
		s.
 _+= D;
	lnI-SED;
	llags	>
 e	*/
Iot, essharic_c	s.
 _&= rmaskat	}t
	ch->**_r_s.
 _= s.
 _& rmaskat	ch->**_e_s.
 _= s.
 _& rmaskat	_cls.hheil_e	*/
_flow_cr trol(chtatlspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
 	*  TelltFreedrivlayiz
its okaythe "eat"#Free_bit nowsic_cstru.h>
_er siz_pushutp->p witat
	if (lnt
		struldisc_deref(lnt*
}

 *                                                                        Defineinal tesswhtr
CARRIER ges to: es */ the
 akuseapproprietlalongc.
 */
sttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttat dgnc_tty_c*dript,ct dgnc***ap hig *ch)
c_ict tty__board	*dg0*ed;
t	dgncvi
tpc*dript_SE0at	dgncphyspc*dript_SE0at
	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		returnat
	ed_SEch->**_ed;

	if (!ed || bd->ma t	I!N DGNC_BOARD_MAGICt
		returnat
	if (ch->**_mies *_& UART_MSR_DCDt
		physpc*dript_SE1at
	if (ch->**_.com)_flags =	DI& _COOKFORCEDCDt
		vi
tpc*dript_SE1at
	if (ch->**_lag =	(D& CLOCHLt
		vi
tpc*dript_SE1at
	* D	hisTeor refoa VIRTUAL c*dript_sparei.
 *the HIGH/
lhic_cif (((ch->**_s =	DD& CH_FCAR),	= 0).&& (vi
tpc*dript_S= 1)t.c_t		* D		eo Whtr
c*dript_ristaskwake bug thy(stslwandthe
		hisfopy_*dript_he hope(str
routrme
 		hic__c	if (eaUTe	*/
_gc.
ec,&(ch->**_s =	DEeaUTt/t
			wake_up_rrupt striblu,&ch->**_s =	DEeaUTtat	}t
	* D	hisTeor refoa PHYSICAL c*dript_sparei.
 *the HIGH/
lhic_cif (((ch->**_s =	DD& CH_CDt,	= 0).&& (physpc*dript_S= 1)t.c_t		* D		eo Whtr
c*dript_ristaskwake bug thy(stslwandthe
		hisfopy_*dript_he hope(str
routrme
 		hic__c	if (eaUTe	*/
_gc.
ec,&(ch->**_s =	DEeaUTt/t
			wake_up_rrupt striblu,&ch->**_s =	DEeaUTtat	}t
	* D	hissTeor refoa PHYSICAL sparei.
 *the low, cep withasswe Foun't_lhis cur prily ogno ]	*/physidefasparei.
 *s (ehich isl it w"vi
tu.
_lhis c*dript"_hedidetls)
 	hiD	hissTty dparei.
 *the GNU vi
tu.
 c*dript_se low y(slly doesn't_lhis ing /

.. illy(slly rnly meare "ogno e c*dript_es */" part_lhis "make pretany hNt w_*dript_hs  *	re"/
lhic_cif ((vi
tpc*dript_S= 0).&& ((ch->**_s =	DD& CH_CDt,!= 0).&&
n \
	(physpc*dript_S= 0)t.c_t		* D		eo   Whtr
c*dript_drops:D		eoD		eo   Dropsc*dript_triallt(str
unite
 		hiD		eo   Flush e	*/
askwak]	*/up bug task_wandthe he hopD		eo    =	0,disciplrme
 		hiD		eo    any b es tup.he Freecr trol hnal typ
 		hiD		eo   Enabluralltselegnc*alls/
l	hic_c	if (eaUTe	*/
_gc.
ec,&(ch->**_s =	DEeaUTt/t
			wake_up_rrupt striblu,&ch->**_s =	DEeaUTtat
		if (ch->**_tun.un_(str_count > 0)_c		closes tupuch->**_tun.un_stytat
		if (ch->**_pun.un_(str_count > 0)_c		closes tupuch->**_pun.un_stytat	}t
	* D	hissMake suounFrt whur ca.h>	 valus	tivflegncFreecur prily(slity
 	hic_cif (vi
tpc*dript_S= 1)t		ch->**_s =	DD|= CH_FCARat	 hse_c	ch->**_s =	DD&= ~CH_FCARat_cif (physpc*dript_S= 1)t		ch->**_s =	DD|= CH_CDat	 hse_c	ch->**_s =	DD&= ~CH_CDat}
 * Defi Ased ccFreecustomd, 8   etl.he Freec**ap h ct ttyuou
static stru dgnc_tty_setpcustom_spessuct tty_***ap hig *chrugdgncnew etl)
c_idgncivstdiv;_idgncivst etl_high;_idgncivst etl_low;_idgncdeltahigh;_idgncdeltalow;_
	if (new etlI<= 0).c_ilch->**_lustom_spessI	 0at		returnat	}_t	* D	his Sinc three_ivisot_hs storc the a 16-bis.infegiz,swpemake suouD	his we don't aor w bug r */
#smaorhe teae a 16-bis.infegiz w haveaor w.D	hissAiostf crurpr,pr */
#abovethree_ividany won't fly
 	hic_cif (new etlI&& new etlI< ((ch->**_bn->dd__ividany / 0xFFFF) + 1/t
		new etlI= ((ch->**_bn->dd__ividany / 0xFFFF) + 1/;_
	if (new etlI&& new etlI> ch->**_bn->dd__ividanyt
		new etlI= ch->**_bn->dd__ividany;_
	if (new etlI> 0).c_ilivstdivI= ch->**_bn->dd__ividany / new etl;
 		* D		eo  If we trynhe figuounoull it w etl.hhe d	*dg0w haveuspD		eo   this tecivste_ivisot,will be usefer versee	.
 ot_highptD		eo  teae  tecree	*stedab 8   etl.  If we Fren
deinal te FreD		eo   etl. bhisaa_ivisot_rneahighpt,pwe  be cgls.GNU nexr lowptD		eo  supp wie   etl.ber w  tecree	*sted/
l	hic_c	ivst etl_highI= ch->**_bn->dd__ividany / ivstdiv;_i	ivst etl_low I= ch->**_bn->dd__ividany / (ivstdivI+ 1/;_
		* D		eo  If  tecretl.the
 * cree	*steda_ivisot_hs cor pct,t forD		eo   */pnd/or usefdone/
l	hic_c	if (ivst etl_highI!= new etl).c_ill* D			his O verwipr,ppick  tecretl. it wis closee
(i.l. ehichevhe retlD			his hubla#smaorhe delta)
 			hic_c		deltahigh_= svst etl_highI- new etl;
c		deltalow = new etlI- svst etl_low;_
ll	if (deltahigh_<cdeltalow)_c		l4ew etlI= ivst etl_high;_i		 hse_c		l4ew etlI= ivst etl_low;_i	}t	}t
	ch->**_lustom_spessI	 new etl;
}

  dgnc_tty_cheil_e	*/
_flow_cr trol(ct dgnc***ap hig *ch)
c_iqueuelefttSE0at
	flaSbiounhowsmuch entc twe  receleftthe hop e	*/
Iic_celefttSEch->**_r_s.
 _-Ech->**_r_headI- 1at	if (eleftt< 0)_c	eleftt+SERQUEUEMASKI+ 1at
	* D	hisCheilthe softifswe ld haveematic
Iflowscr trol oe hur e	*/
Ibeca */D	hissty ave(delasee)wisn't y(stthe  bitsoulltf hur e	*/
Ifasieemuf
 	hiD	his TO :  is shardrneae prodrotwit wFreecur prilflowscr trol of.GNU_lhispor wis cls.ati
 	hiD	his1) HWFLOW (RTS)I- Turn striFreeUART's Reed a  rrupt str
 	his is s be cca */pGNeeUART's FIFO he bailtupcIahe
atic
 	hisGNeeRTS ed cal he beedropp o
 	hi 2) SWFLOW (IXOFF) - Keepttrythe so
sany ap bit.***racupttheD	hi	hopeo verssida,rdge thaswill be u bit.sendinge_bit he ue
 	hi 3)s TNE - No vingewe redido.  W p be u omply dropsbug exrra
 bit 	hi	hoat glss#eprininfe usswhtr
Freee	*/
Ifbe stup
 	hic_cif (eleftt< 256).c_ilflaHWFLOW ic_c	if (ch->**_.com)_flags =	DI& CTSPACE || ch->**_lag =	(D& CRTSCTS)Ic_illif (!(ch->**_s =	DD& CH_RECEIVER_OFF)t.c_ill	ch->**_bn->dd_ops->disablu_reed a r(chtatl			ch->**_s =	DD|= (CH_RECEIVER_OFF)at			}t		}
		flaSWFLOW ic_c	 hse if (ch->**_lai =	(D& IXOFF) c_illif (ch->**_sbitsdsanst<= MAX_STOPS_SENTt.c_ill	ch->**_bn->dd_ops->sany_sbit_***racupt(chtatl			ch->**_sbitsdsans++at			}t		}
		flaNo FLOW ic_c	 hse c_ill*  Empty
.. Can't donany vingeaboulltty rmpending (vheflow
.. ic_c	}t	}_t	* D	hisCheilthe softifswe ld haveunematic
Iflowscr trol beca */D	hisave(delasee)wf typly readIemuf  bitsoulltf hur e	*/

 	hiD	his TO :  is shardrneae prodrotwit wFreecur prilflowscr trol of.GNU_lhispor wis cls.ati
 	hiD	his1) HWFLOW (RTS)I- Turn bailtoniFreeUART's Reed a  rrupt str
 	his is s be cca */pGNeeUART's FIFO he raipreRTS bailtupc 	hisehich  be caor w hopeo verssidathep b rtIsendinge_bit Fgain
 	hi 2) SWFLOW (IXOFF) - Sany ap b rtI***racupttheD	hi	hopeo verssida,rsowill be u b rtIsendinge_bit fe ussFgain
 	hi 3)s TNE - Dopartving. Sinc twe didn't donany vingehe Furn striFreD	hi	o verssida,rwe don't lessIfe donany vingenow/
lhic_cif (eleftt> (RQUEUESIZEl/ 2)).c_ilflaHWFLOW ic_c	if (ch->**_.com)_flags =	DI& RTSPACE || ch->**_lag =	(D& CRTSCTS)Ic_illif (ch->**_s =	DD& CH_RECEIVER_OFF).c_ill	ch->**_bn->dd_ops->enablu_reed a r(chtatl			ch->**_s =	DD&= ~(CH_RECEIVER_OFF)at			}t		}
		flaSWFLOW ic_c	 hse if (ch->**_lai =	(D& IXOFF,&&cch->**_sbitsdsans).c_illch->**_sbitsdsanstN 0;
	c	ch->**_bn->dd_ops->sany_sb rts***racupt(chtatl	}
		flaNo FLOW ic_c	 hse c_ill*  No vinge essha. ic_c	}t	}_}

  dgnc_tty_wakeupce to s(ct dgnc***ap hig *ch)
c_iqueuelenI	 0at	gned char withs =	D;

	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		returnat
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	* D	hisIfec**ap h nowshublcntc , wake up bugrneawandthe oniFreecr di.
 */
lhic_celenI	 ch->**_w_headI- ch->**_w_s.
 ;
	df (elent< 0)_c	elee += WQUEUESIZE;

	if (elee >= (WQUEUESIZEl-c256)).c_ilspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
		returnat	}_t	if (ch->**_tun.un_s =	D_& UN_ISOPEN) {_c	if (uch->**_tun.un_sty->s =	D_& (1 <<  TY_DO_WRITE_WAKEUP)).&&
n		ch->**_tun.un_sty->ldisc->ops->e to _wakeup).c_illspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
			uch->**_tun.un_sty->ldisc->ops->e to _wakeup)uch->**_tun.un_stytat			spin__til_(rqsrec,&ch->**__til,hs =	D/*
		}t
		wake_up_rrupt striblu,&ch->**_tun.un_sty->e to _waUTtat
		* D		eo If unitwis cls.hepwander tiurempty, cheilthe make suouD		hissty e	*/
IAND FIFO Founbo vrempty/
l	hic_c	if (ch->**_tun.un_s =	D_& UN_EMPTY)Ic_illif ((elee S= 0).&& (ch->**_bn->dd_ops->gls_u rtsbytds_left(cht_S= 0)t.c_	n		ch->**_tun.un_s =	DD&= ~(UN_EMPTY)att				* D				eo If RTS Togglpemodesermrn, whtrevheD				eo sty e	*/
Iahe
UART ermempty, keeptRTS r w.D				hic_c			if (ch->**_.com)_flags =	DI& _COOKRTS_TOGGLEt.c_	n			ch->**_most *_&= ~(UART_MCRKRTStatl				ch->**_bn->dd_ops->as*/
tple.em_ed cals(chtatl			}tt				* D				eo If DTR Togglpemodesermrn, whtrevheD				eo sty e	*/
Iahe
UART ermempty, keeptDTR r w.D				hic_c			if (ch->**_.com)_flags =	DI& _COOKDTR_TOGGLEt.c_	n			ch->**_most *_&= ~(UART_MCRKDTRtatl				ch->**_bn->dd_ops->as*/
tple.em_ed cals(chtatl			}t			}t		}
 		wake_up_rrupt striblu,&ch->**_tun.un_s =	DEeaUTtat	}t
	if (ch->**_pun.un_s =	D_& UN_ISOPEN) {_c	if (uch->**_pun.un_sty->s =	D_& (1 <<  TY_DO_WRITE_WAKEUP)).&&
n		ch->**_pun.un_sty->ldisc->ops->e to _wakeup).c_illspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
			uch->**_pun.un_sty->ldisc->ops->e to _wakeup)uch->**_pun.un_stytat			spin__til_(rqsrec,&ch->**__til,hs =	D/*
		}t
		wake_up_rrupt striblu,&ch->**_pun.un_sty->e to _waUTtat
		* D		eo If unitwis cls.hepwander tiurempty, cheilthe make suouD		hissty e	*/
IAND FIFO Founbo vrempty/
l	hic_c	if (ch->**_pun.un_s =	D_& UN_EMPTY)Ic_illif ((elee S= 0).&& (ch->**_bn->dd_ops->gls_u rtsbytds_left(cht_S= 0)t_	n		ch->**_pun.un_s =	DD&= ~(UN_EMPTY)at		}t
		wake_up_rrupt striblu,&ch->**_pun.un_s =	DEeaUTtat	}t
	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
}


 *                                                                        DefThis TY E trynporrus
ahe
helpunctionalits ************************************************************************
 *
/
 * Defi_tty_clos(stru) */

#inic int dgnc_tty_open(struct tty_struct *tty, struct file *file);
st
c_ict tty__board	*dg	*ern;
	ct tty_***ap hig *chat	ct tty_un_s	*unat	udgn		majorIN 0;
	udgn		minor_SE0at	dgn		r	IN 0at	gned char withs =	D;

	r	IN 0;

	majorIN MAJOR(stru_evnum(str)tat	minor_SEMINOR(stru_evnum(str)tat
	if (majorI>c255t
		return -ENXIOat
	*  Gls.d	*dg0porruptt_use hur nty  the majorstwe  receaor detlthic_cern =c_digidsByMajor[256]major];
	if (!ernt
		return -ENXIOat
	* D	hisIf.d	*dg0es nofoyls.upohepates */ he READY, gonteD	hissleeptwandthe the
is.hephappenshr Frey denc l hope(str
 	hic_cr	IN eaUTE thet_rrupt striblu,ern->es */EeaUT,
		,ern->es */_& BOARD_READY)tat
	if (rct
		return rcat
	spin__til_(rqsrec,&ern->dd__til,hs =	D/*
 	*  If.(strroe_evic/ es ged
 *e teae hur nu
bptthe s wil,eb.
 .hic_cif (PORT_NUM(minor)I>cern->nasync).c_ilspin_un_til_(rqivstorc,&ern->dd__til,hs =	D/*
		return -ENXIOat	}t
	ch_SEern->***ap hs[PORT_NUM(minor)];
	if (!cht_c_ilspin_un_til_(rqivstorc,&ern->dd__til,hs =	D/*
		return -ENXIOat	}t
	*  Dropsd	*dg0_tilhic_cspin_un_til_(rqivstorc,&ern->dd__til,hs =	D/*

	*  Grabec**ap h _tilhic_cspin__til_(rqsrec,&ch->**__til,hs =	D/*
 	* sFiguounoulltur s */Iic_cif (!IS_PRINT(minor)t_c_ilunI	 &ern->***ap hs[PORT_NUM(minor)]->**_tun;_ilun->un_s */IN DGNC_SERIAL;
	}  hse.if (IS_PRINT(minor)t_c_ilunI	 &ern->***ap hs[PORT_NUM(minor)]->**_pun;_ilun->un_s */IN DGNC_PRINTat	}  hse.c_ilspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
		return -ENXIOat	}t
	* D	hisIf Freepor wis ctbe uhe a previouso(str (anterrpates */D	hiswhtte we  omply c*apot saflly keeptgothe,swander tiurGNU_lhises */_clears
 	hic_cspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
 	r	IN eaUTE thet_rrupt striblu,ch->**_s =	DEeaUT, ((ch->**_s =	DD& CH_OPENING)_S= 0)t*
 	*  If.ret0es non-zero,  */
#ctrl-c'eveusIic_cif (rct
		return -EINTRat
	* D	hisIf.er versunitwis he hopemiddlethe GNU frag*filp rtItf c(strc_lhiswe  for c*apot touch GNU c**ap h caflly
 	hi Gothe sleep, knowthe soatswhtr
Freec**ap h redib/D	hissouchevecaflly,
Freec(str
routrmep be u o cal hNU_lhis**_s =	DEeaUT.hepwake us bailtup
 	hic_cr	IN eaUTE thet_rrupt striblu,ch->**_s =	DEeaUT,
		,(uch->**_tun.un_s =	DD| ch->**_pun.un_s =	D)_& UN_CLOSING)_S= 0)t*
 	*  If.ret0es non-zero,  */
#ctrl-c'eveusIic_cif (rct
		return -EINTRat
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 
	flaSbiountur unitwinfe dr.h"
__bit, cepwe always  recend/ovailablu.sic_cstr->dr.h"
__bitIN un;_t
	* D	hisIally.
ize Fty's
lhic_cif (!(un->un_s =	D_& UN_ISOPEN)).c_ilflaSbiounompor agncvbles
 */. ic_c	un->un_stysssss	 str;

		flaMaysefdorceme vingehtte he Free TY ct tty_asswell? ic_c}_t
	* D	hispor detlo***ap h er sizstthe
read/e to /error
 	hi Sls.s =	, cepwe don't gls.Graticrodro
 	hic_cch->**_s =	DD|= (CH_OPENING);t
	*  Drops_tils,publmaor dl bhisGFP_EL HAC redisleeptic_cspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
 	if (!ch->**_re	*/
)t		ch->**_re	*/
I=ckzaor d(RQUEUESIZE, GFP_EL HACtat	if (!ch->**_ee	*/
)t		ch->**_ee	*/
I=ckzaor d(EQUEUESIZE, GFP_EL HACtat	if (!ch->**_we	*/
)t		ch->**_we	*/
I=ckzaor d(WQUEUESIZE, GFP_EL HACtat_cspin__til_(rqsrec,&ch->**__til,hs =	D/*
 	ch->**_s =	DD&= ~(CH_OPENING);t	wake_up_rrupt striblu,&ch->**_s =	DEeaUTtat
	* D	hisIally.
ize ot, er versinal typehe
ter off es (str/
lhic_cif (!(uch->**_tun.un_s =	DD| ch->**_pun.un_s =	D)_& UN_ISOPEN)).c_
		* D		eo Flush inpulle	*/
a/
l	hic_c	ch->**_r_headI= 0;
	cch->**_r_s.
 _= 0;
	cch->**_e_headI= 0;
	cch->**_e_s.
 _= 0;
	cch->**_w_headI= 0;
	cch->**_w_s.
 _= 0;

		ern->dd_ops->flushuu rtse to uchtatl	ern->dd_ops->flushuu rtsread(chtat
ilch->**_s =	DD= 0;
	cch->**_ca.h>	_lsrD= 0;
	cch->**_sbit_sending_ks
 *D= 0;
	cch->**_sbitsdsanstN 0;

	cch->**_cag =	(Dss	 str->sos Dgnflag =	(;
	cch->**_cai =	(Dss	 str->sos Dgnflai =	(;
	cch->**_cao =	(Dss	 str->sos Dgnflao =	(;
	cch->**_cal =	(Dss	 str->sos Dgnflal =	(;
	cch->**_sb rtcs	 str->sos Dgnflagc[VSTART];
	cch->**_sbitcss	 str->sos Dgnflagc[VSTOP]at
		* D		eo Br]	*/up RTS anteDTR
.	C		hisposoihintlr RTS he
DTR togglpeif set/
l	hic_c	if (!(ch->**_.com)_flags =	DI& _COOKRTS_TOGGLEt)_c		ch->**_most *_|= (UART_MCRKRTStatl	if (!(ch->**_.com)_flags =	DI& _COOKDTR_TOGGLEt)_c		ch->**_most *_|= (UART_MCRKDTRtat
		flaTelltUART he heitwitselfhic_c	ern->dd_ops->u rts(sem,chtatl}t
	* D	hisRunInt em he c prewe res to tany ving 	hic_cern->dd_ops->nt em(stytat
	_tty_c*dript,chtat
i* D	hisfoor w otypecoltthe
(str]	*/por  	hic_
	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
 	r	IN _block_til_ready(stru stru, struchtat
i* aNo gothe bailtnow, incrts thntur unitwantec**ap h rountptstic_cspin__til_(rqsrec,&ch->**__til,hs =	D/*
	ch->**_(str_count++at	un->un_(str_count++at	un->un_s =	DD|= (UN_ISOPEN)atlspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
 	return rcat}

 * Defi_tty_k_til_ready(stru) * ThisWaUT.the
DCD, ot, essha.

#inic int dgnc_tty_k_til_ready(struct tty_struct *tty, struct file *file, struct chan***ap hig *cht
c_idgncretvype= 0;
	ct tty_un_s *unI	 NULL;
	gned char withs =	D;
	udgn	old_s =	DD= 0;
	dgn	sleep_on_un_s =	DD= 0;
_cif (!stys|| str->ma t	I!N  TY_MAGIC || ! *fil|| !ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		return -ENXIOat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		return -ENXIOat
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	ch->**_w(str++at 	flaLoopsattevhe ic_cwhimpl(1).c_
		sleep_on_un_s =	DD= 0;
_c	* D		eo If e	*dg0hublf.
 evecemehows u ]	*/hur sleep, b.
 l bhiserror
 		hic_c	if (ch->**_bn->es */_S= BOARD_FAILED)Ic_illretvype= -ENXIOat			es
 *;
		}
 		* sIf Ftyswalehu	*/up, er
 *Doulltf loop the
satserror
hic_c	if (iloseu	*_up_p();
st)Ic_illretvype= -EAGAINat			es
 *;
		}
 		* D		eo If er versunitwis he hopemiddlethe GNU frag*filp rtItf c(strc_llhiswe  for c*apot touch GNU c**ap h caflly
 		hi Gotbailthe sleep, knowthe soatswhtr
Freec**ap h redib/D		hissouchevecaflly,
Freec(str
routrmep be u o cal hNU_llhis**_eaUTEs =	DDhepwake us bailtup
 		hic_c	if (!(uch->**_tun.un_s =	DD| ch->**_pun.un_s =	D)_& UN_CLOSING)).c_t			* D			hisOur cr di.
 *scse lerececleanly ahe
happily:D			his1)s TNBLOCKING oniFreeFtysis cls/
l		his2) CLOCHLsis cls/
l		his3)
DCD (fake he
real)wis gc.
ec/
			hic__c		if ();
s->fgs =	DI& O_ TNBLOCKt_	n		es
 *;
 			if (ilo->s =	D_& (1 <<  TY_IO_ERROR)t.c_	n		retvype= -EIOat				es
 *;
			}tt			if (ch->**_s =	DD& CH_CDt
				es
 *;
 			if (ch->**_s =	DD& CH_FCAR)t				es
 *;
		}  hse.c_illsleep_on_un_s =	DD= 1;
		}
 		* D		eo If  *	reles au o cal pending,  *	  */
#pr bably
		hisrrupt streve(ctrl-c) ue
 		hisLereceloop  bhiserror set/
l	hic_c	if ( o cal_pending(cur prit)Ic_illretvype= -ERESTARTSYSat			es
 *;
		}
 		* D		eo SbiounGNU f =	DD,matte we latsgothe ***ap h _til
l	hic_c	if ( leep_on_un_s =	D)t			old_s =	DD= ch->**_tun.un_s =	DD| ch->**_pun.un_s =	D;
		 hse_c		old_s =	DD= ch->**_s =	D;

		* D		eo Latsgothe ***ap h _tilD,matte *allthe schevule/
l	hisOur poorhe  be cgls.bug FEPn thets
ahe
wake us upswhtr
DCD
l	his thetu.
 *sgoes gc.
ec/
		hic__c	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
 		* D		eo WaUT.the
ceme vingehe hopes =	DDhepres tot_use Freecur prilvalus.
l	hic_c	if ( leep_on_un_s =	D)t			retvype= eaUTE thet_rrupt striblu,un->un_s =	DEeaUT,
				(old_s =	DD!= (ch->**_tun.un_s =	D_| ch->**_pun.un_s =	D))tat		 hse_c		retvype= eaUTE thet_rrupt striblu,ch->**_s =	DEeaUT,
				(old_s =	DD!= ch->**_s =	D)/*
 		* D		eo Weagotswokeotup the
ceme
reasro
 		eo Bmatte loopiitharatio, grabehur c**ap h _til.
l	hic_c	spin__til_(rqsrec,&ch->**__til,hs =	D/*
	}
 	ch->**_w(str--at
	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
 	if (retvypt
		return retvyp*
 	return 0;
}

 * Defi_tty_closes tupu) * ThisHs tup.hreepor .  Likuna c(strc bulldon't waUT.the
outpullfe drain
 static stru dgnc_tty_closes tupuct tty_struct *tty, stt
c_ict tty_un_s	*unat_cif (!stys|| str->ma t	I!N  TY_MAGICt
		returnat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		returnat 	flaflush Freesparemieue	*/
shic_c_TmpWstru.hushuer siz(stytat
}

 * Defi_tty_closc(stru) */

#inic int  dgnc_tty_closc(struct tty_struct *tty, struct file *file);
st
c_ict tty_ksos Dgnp*il;
	ct tty__board	*dg0*ed;
	ct tty_***ap hig *chat	ct tty_un_s *unat	uned char withs =	D;
	dgncr	IN 0;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returnat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		returnat 	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		returnat
	ed_SEch->**_ed;
	if (!ed || bd->ma t	I!N DGNC_BOARD_MAGICt
		returnat
	tDD= &str->sos Dgnat
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	* D	hisDeinal te ie GNefihs  *	 lasiec(str
he
pot -(anterfswe FgSoftaboulD	hiswhich s */Itf c(str itwis  this tecL te Disciplrme
lhic_cif ((str->count S= 1).&& (un->un_(str_countI!N 1)).c_ilfl 		eo Uh, oh.  str->count is 1,swhich meare tit wFreestr 		eo st ttyuoun be usef sofd.  un_(str_countIld havealways 		eo sefrneahe hopseecr di.
 *s.  If it's ged
 *e teae 		eo one,swe'veagotsreal#pr bleml,e onc titwmeare tiU_llhissl_reg por wwon't bee hut_own.
l	hic_c	APR(("str->count is 1,suns(str
count is %d\n", un->un_(str_count)tat		un->un_(str_countI= 1;
	}t
	if (un->un_(str_count)t		un->un_(str_count--at	 hse_c	APR(("badssl_reg por w(str
count tf %d\n", un->un_(str_count)tat
	ch->**_(str_count--at
	if (ch->**_(str_countI&& un->un_(str_count).c_ilspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
		returnat	}_t	*  OK,
its  *	 lasiec(str
he hopeunitwic_cun->un_s =	DD|= UN_CLOSINGat
	ttr->c(stiithSE1at
 	* D	hisOnly offici.
 *sc(str
c**ap h if
count is 0tintD	hisDCOOKPRINTER bit0es nofoset/
lhic_cif ((ch->**_(str_countIS= 0).&& !(ch->**_.com)_flags =	DI& _COOKPRINTER)).c_t		ch->**_s =	DD&= ~(CH_STOPI_| CH_FORCED_STOPI)*
 		* D		eo Furn striper bveevic/ whtr
c(stiithper bveevic/.
l	hic_c	if ((un->un_s */INN DGNC_PRINT).&& (ch->**_s =	DD& CH_PRON)).c_ild_blocemov*(chru =->**_.com)_flagtr =tr,
				(int). =->**_.com)_flagtr len/*
	c	ch->**_s =	DD&= ~CH_PRON;
		}
 		spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
		*  waUT.the
outpullfe drainhic_c	flaTis s be caosoireturn ifpwe takunansrrupt struic__c	r	IN bn->dd_ops->drainu stru0)*
 		_TmpWstru.hushuer siz(stytat		struldisc_.hush(stytat
		spin__til_(rqsrec,&ch->**__til,hs =	D/*
 		ttr->c(stiithSE0;
_c	* D		eo If we  receHUPCLoset, lowpt
DTR anteRTS 		hic_c	if (ch->**_lag =	(D& HUPCL).c_t			*  DropsRTS/DTR ic_c		ch->**_most *_&= ~(UART_MCRKDTR_| UART_MCRKRTStatl		bn->dd_ops->as*/
tple.em_ed cals(chtatt			* D			hisGothe sleepthe ensuounRTS/DTRD			his recebeen dropp o.the
le.emsthe soft */
			hic_			if (ch->**_close_y */
t.c_	n		spin_un_til_(rqivstorc,&ch->**__til,_	n			n \
	schs =	D/*
				_TmpWms_sleep(ch->**_close_y */
t;_	n		spin__til_(rqsrec,&ch->**__til,hs =	D/*
			}t		}
 		ch->**_(ld_b 8  SE0;
_c	*  Turn striUART erupt strs.the
 *s spor wic_c	ch->**_bn->dd_ops->u rtsstr,chtatl}  hse.c_il* D		eo Furn striper bveevic/ whtr
c(stiithper bveevic/.
l	hic_c	if ((un->un_s */INN DGNC_PRINT).&& (ch->**_s =	DD& CH_PRON)).c_ild_blocemov*(chru =->**_.com)_flagtr =tr,
				(int). =->**_.com)_flagtr len/*
	c	ch->**_s =	DD&= ~CH_PRON;
		}
	}
 	un->un_stys	 NULL;
	gn->un_s =	D_&= ~(UN_ISOPEN_| UN_CLOSING);
_cwake_up_rrupt striblu,&ch->**_s =	DEeaUTtat	wake_up_rrupt striblu,&un->un_s =	DEeaUT)at
	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
}

 * Defi_tty_closchars_rruer siz() * ThisReturn nu
bptthe ***racuptsttit w recenotebeen sparemiereveyet/
efThis *s sroutrmeps susedaby tty  =	0,disciplrmelfe deinal te ie GNeou
stshardbit wandthe he beesparemierev/drainev/.hushrodre
pot.

#inic int dgnc_tty_closchars_rruer siz(ct tty_struct *tty, stt
c_ict tty_***ap hig *chs	 NULL;
	ct tty_un_s *unI	 NULL;
	gsh wi GNeadat	ush wi_st.
 ;
	udgnctmaskat	udgnccharsI	 0at	gned char withs =	D;

	if (stys		 NULLt
		return 0at
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		return 0at 	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		return 0at 	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	tmask_SEWQUEUEMASK;_	thead_SEch->**_w_headI& tmaskat	ts.
 _SEch->**_w_s.
 _& tmaskat
	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
 	if (ts.
 _S= GNead).c_ilcharsI	 0at	}  hse.c_ilif (theadI>= ss.
 )
	c	charsI	 theadI- ss.
 ;_il hse_c		charsI	 theadI- ss.
  + WQUEUESIZE;
	}t
	return chars*
}

 * Defi_tty_maxcps_room * ThisReduces bytds_availablurhe Freemax nu
bptthe ***racupts
hisstt w_*n bee prilcur prily g.h"n Freemaxcpslvalus,tintDhisreturnsssty new bytds_availablu. s *s srnly a sicts
ter offDhisoutpul.

#inic int dgnc_tty_maxcps_roomuct tty_struct *tty, strudgncbytds_availablut
c_ict tty_***ap hig *chs	 NULL;
	ct tty_un_s *unI	 NULL;

	if (!styt
		return bytds_availabluat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		return bytds_availabluat
	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		return bytds_availabluat
	* D	hisIf.its pot tree parent prihper bveevic/,sreturnD	hissty fullinbit Fmount/
lhic_cif (un->un_s */I!N DGNC_PRINT)
		return bytds_availabluat
	if (ch->**_.com)_flagmaxcpsl> 0,&&cch->**_.com)_flager size > 0).c_ildgnccps_limieu= 0;
	cgned char withcur pri_simeu= jiffies;
	cgned char wither siz_simeu= cur pri_simeu+_c		(HZhis**->**_.com)_flager size) /s**->**_.com)_flagmaxcpsat
		if (ch->**_cpssimeu< cur pri_sime).c_ill* aer siz
irmempty ic_c		ch->**_cpssimeu= cur pri_sime;n \
	/*
recls.**_cpssimeuic_c		cps_limieu= **->**_.com)_flager size;
		}  hse.if (ch->**_cpssimeu< er siz_sime).c_ill* actbe uroomthe hopeer sizuic_c		cps_limieu= ((er siz_simeu- ch->**_cpssime)his**->**_.com)_flagmaxcps) /sHZ;
		}  hse.c_ill* anouroomthe hopeer sizuic_c		cps_limieu= 0at		}t
		bytds_availablur	 l t(cps_limie,cbytds_availablut;
	}t
	return bytds_availabluat}

 * Defi_tty_close to _roomu) * ThisReturn entc tavailablurhe Txeer siz

#inic int dgnc_tty_close to _roomuct tty_struct *tty, stt
c_ict tty_***ap hig *chs	 NULL;
	ct tty_un_s *unI	 NULL;
	gsh wi Neadat	ush wi_s.
 ;
	ush wi_smaskat	dgncretI	 0at	gned char withs =	D;

	if (stys		 NULL || _TmpWriteBuf;
stp=	 NULLt
		return 0at
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		return 0at 	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		return 0at 	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	tmask_SEWQUEUEMASK;_	head_SE(ch->**_w_head)I& tmaskat	t.
 _SE(ch->**_w_s.
 )_& tmaskat
	retI	 s.
 _-EheadI- 1at	if (retI< 0)_c	retI+= WQUEUESIZE;

	flaLimieuter off he maxcpslic_crptIN _blocmaxcps_roomu strurpt/*
 	* D	hisIfewe Founter off eevic/,slereceentc tthe_lhispossibly bo vrhope(n antestring ]	*s/
lhic_cif (un->un_s */INN DGNC_PRINT).c_ilif (!(ch->**_s =	DD& CH_PRON))_c		retI-SE =->**_.com)_flagtnlen;
		retI-SE =->**_.com)_flagtftlen;
	}  hse.c_ilif (ch->**_s =	DD& CH_PRON)_c		retI-SE =->**_.com)_flagtftlen;
	}
t	if (retI< 0)_c	retI= 0at 	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
 	return retat}

 * Defi_tty_clospul_s inu) * ThisPus.tI***racupttinfe ch->**_buf *	Sen \
	sc-susedaby tty  =	0,disciplrmelthe
OPOST pr destiit

#inic int dgnc_tty_clospul_s inuct tty_struct *tty, strugned char =	50ct
c_i* D	hisSomply c*lltFlose to /
lhic_c_tty_close to u stru&c, 1/;_	return 1at}

 * Defi_tty_close to u) * ThisTakunnbit _use Free */
#he
kl sourthe
sany itsoullfe FreeFEP.

#sIa *	relex stsialltFree parent prihter b ma t	Iasswell.

#inic int dgnc_tty_close to uct tty_struct *tty, str_ilconst gned char =	50*er ,rdgnccount)tc_ict tty_***ap hig *chs	 NULL;
	ct tty_un_s *unI	 NULL;
	dgncbufcountI= 0,cn_SE0at	dgncorig_countI= 0at	gned char withs =	D;
	gsh wi Neadat	ush wi_s.
 ;
	ush wi_smaskat	udgncremainat	dgnc.use_ */
#N 0;

	if (stys		 NULL || _TmpWriteBuf;
stp=	 NULLt
		return 0at
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		return 0at 	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		return 0at 	if (!count)t		return 0at 	* D	hisSbiountrig typeamountthe ***racuptstpas*/terr.D	hisTtirmhelpsnhe figuounoullifswe ld haveask_FreeFEPD	hisso
sany ussFns thet whtr
it0hubl detaentc tavailablu/
lhic_corig_countI= countat
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	*  Gls.hur sntc tavailablurthe
 * cc**ap h _use Freed	*dg0*/ 	tmask_SEWQUEUEMASK;_	head_SE(ch->**_w_head)I& tmaskat	t.
 _SE(ch->**_w_s.
 )_& tmaskat
	bufcountI= s.
 _-EheadI- 1at	if (bufcountI< 0)_c	bufcountI+= WQUEUESIZE;

	flD	hisLimieuter off outpullfe maxcpsl(vheall,  thisburstsiallowetD	hisupoheper size ***racupts/
lhic_cbufcountI= _blocmaxcps_roomu strubufcount/*
 	* D	hisTakunl timumthe wit wFree */
#waets
so
sany (anteGNU_lhisentc tavailablurhe FreeFEPeer siz.D	hic_ccountI= l t(countrubufcount/*
 	* D	hisB.
 _ot, orcntc tleft/
lhic_cif (countI<= 0).c_ilspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
		return 0at	}
 	* D	hisOutpullfhunter off ONing ]	*ruifswe Founhe Fnal typele.ec bulD	hisnessIfe bunhe ter off le.e/
lhic_cif ((un->un_s */INN DGNC_PRINT).&& !(ch->**_s =	DD& CH_PRON)).c_il_blocemov*(chru =->**_.com)_flagtn=tr,
		 \
	(int). =->**_.com)_flagtnlen/*
	chead_SE(ch->**_w_head)I& tmaskat		ch->**_s =	DD|= CH_PRON;
	}
 	* D	hisOe hopeo vershintruoutpullfhunter off OFFing ]	*ruifswe Fou_lhis*ur prily oe ter off le.ec bullnessIfe outpullfe  tecival typ
 	hic_cif ((un->un_s */I!N DGNC_PRINT).&& (ch->**_s =	DD& CH_PRON)).c_il_blocemov*(chru =->**_.com)_flagtr =tr,
			(int). =->**_.com)_flagtr len/*
	chead_SE(ch->**_w_head)I& tmaskat		ch->**_s =	DD&= ~CH_PRON;
	}t
	* D	hisIf Frer/ es nofvingelefttfe cof ,#he
Iw_*n'tshantlr bug  deta_bit, lerec/
lhic_cif (countI<= 0).c_ilspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
		return 0at	}
 	if ()use_ */
).c_t		countI= l t(countruWRITEBUFLEN)at_ilspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
_c	* D		eo If nbit hs comthe tuse  */
#cntc , cof  itwinfe acivmporarr 		eo er sizucepwe don't gls.swapp o.oull i*fildothe soe cof  teD		hissty d	*dg.
l	hic_c	*  we'reiallowetIfe b_tilDif it's .use_ */
#ic_c	if (_own_rrupt striblu,&_TmpWriteBuf;Sem))_c		return -EINTRat
		* D		eo cof _.use_ */
()sreturnsssty nu
bpt 		eo of bytdssstt w_ have*NOTo sefcofied/
l	hic_c	countI-SE of _.use_ */
(_TmpWriteBuf;
st, (const gned char =	50__ */
#i) er ,rcount/*
 		if (!count).c_illup,&_TmpWriteBuf;Sem);_c		return -EFAULT;
		}
 		spin__til_(rqsrec,&ch->**__til,hs =	D/*
 		bstI= _TmpWriteBuf;
st*
 	}
 	nI= countat
	* D	hisIf Freee to twraps (vhe termsopshe GNU circular er sizc_lhismov*lGNU p wiprotup fe Freewrapnporru, the
recls.GNU_lhisporruptsthe Freebott *	T	hic_crpmain_SEWQUEUESIZEl-cheadat
	if (n.>=Irpmain).c_ilnI-SErpmainat		mpmcpy(ch->**_we	*/
I+ head, er ,rrpmain)at		_tty_sniffEnoeaUTEno_tiluchru"USER WRITE", ch->**_we	*/
I+ head, rpmain)at		head_= 0at		bstI+SErpmainat	}t
	if (n.> 0).c_il* D		eo Mov*livstthe _bit/
l	hic_c	rpmain_SEnat		mpmcpy(ch->**_we	*/
I+ head, er ,rrpmain)at		_tty_sniffEnoeaUTEno_tiluchru"USER WRITE", ch->**_we	*/
I+ head, rpmain)at		head_+SErpmainat	}t
	if (count).c_ilheadI&= tmaskat		ch->**_w_headI= Neadat	}_t	*  Upd */_ter off er sizuempty sime/hic_cif ((un->un_s */INN DGNC_PRINT).&& (ch->**_.com)_flagmaxcpsl> 0)
n \
	&& (ch->**_.com)_flager size > 0)).c_ilch->**_lpssimeu+SE(HZhis*ount)./s**->**_.com)_flagmaxcpsat	}
 	if ()use_ */
).c_ilspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
		up,&_TmpWriteBuf;Sem);_c}  hse.c_ilspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
	}t
	if (count).c_il* D		eo C**ap h _tilDes geabbo tand Fren
rele pro
		hisrrsidath*s sroutrme/
l	hic_c	ch->**_bn->dd_ops-> of _ bit_)use_e	*/
_touu rt,chtatl}t
	return countat}

 * DefiReturn le.emu o calscse la.

#innic int dgnc_tty_closiprcmglsuct tty_struct *tty, stt
c_ict tty_***ap hig *ch;
	ct tty_un_s *unat	dgncresulte= -EIOat	gned char =	50mst *_	 0at	gned char withs =	D;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returncresultat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		return resultat
	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		return resultat
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	mst *_	 (ch->**_most *_|s**->**_mies *)at
	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*

	resulte= 0at 	if (mes *_& UART_MCRKDTRt
		resulte|= TIOCMKDTR; 	if (mes *_& UART_MCRKRTSt
		resulte|= TIOCMKRTS; 	if (mes *_& UART_MSR_CTSt
		resulte|= TIOCMKCTS; 	if (mes *_& UART_MSR_DSRt
		resulte|= TIOCMKDSR; 	if (mes *_& UART_MSR_RIt
		resulte|= TIOCMKRI; 	if (mes *_& UART_MSR_DCDt
		resulte|= TIOCMKCD*
 	return resultat}

 * Defi_tty_closiprcmclsu) * ThisSls.le.emu o cals, c*lledaby la.

#innic int dgnc_tty_closiprcmslsuct tty_struct *tty, st,
	cgned chardgncset, gned chardgncclear)
c_ict tty__board	*dg0*ed;
ict tty_***ap hig *ch;
	ct tty_un_s *unat	dgncrete= -EIOat	gned char withs =	D;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returncretat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		return retat
	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		return retat
	ed_SEch->**_ed;
	if (!ed || bd->ma t	I!N DGNC_BOARD_MAGICt
		return retat
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	if ( e*_& TIOCMKRTS)t		ch->**_most *_|= UART_MCRKRTS*
 	if ( e*_& TIOCMKDTRt
		ch->**_most *_|= UART_MCRKDTRat_cif (clear_& TIOCMKRTS)t		ch->**_most *_&= ~(UART_MCRKRTStat_cif (clear_& TIOCMKDTRt
		ch->**_most *_&= ~(UART_MCRKDTRtat
	ch->**_bn->dd_ops->as*/
tple.em_ed cals(chtatt	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
 	return 0;
}

 * Defi_tty_clossany_es
 *u) * ThisSlny apBs
 *, c*lledaby la.

#inic int dgnc_tty_clossany_es
 *uct tty_struct *tty, strudgncmslc)
c_ict tty__board	*dg0*ed;
ict tty_***ap hig *ch;
	ct tty_un_s *unat	dgncrete= -EIOat	gned char withs =	D;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returncretat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		return retat
	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		return retat
	ed_SEch->**_ed;
	if (!ed || bd->ma t	I!N DGNC_BOARD_MAGICt
		return retat
	switch (mslc).c_ic pre-1:D		mslce= 0xFFFFat		bs
 *;
	c pre0:D		mslce= 0at		bs
 *;
	default:t		bs
 *;
	}t
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	ch->**_bn->dd_ops->sany_es
 *uchrumslc)att	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
 	return 0;
t}

 * Defi_tty_closeaUTEr tiudsansu) * Thiswander tiurnbit hublbeen sparemierev, c*lledaby la.

#inic int  dgnc_tty_closeaUTEr tiudsansuct tty_struct *tty, strudgncsimeout)
c_ict tty__board	*dg0*ed;
ict tty_***ap hig *ch;
	ct tty_un_s *unat	dgncrc;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returnat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		returnat 	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		returnat
	ed_SEch->**_ed;
	if (!ed || bd->ma t	I!N DGNC_BOARD_MAGICt
		returnat
	r	IN bn->dd_ops->drainu stru0)*
}

 * Defi_tty_sany_xs inu) * Thissany aphigh_teroBufy ***racupt, c*lledaby la.

#inic int  dgnc_tty_clossany_xs inuct tty_struct *tty, stru =	50ct
c_ict tty__board	*dg0*ed;
	ct tty_***ap hig *chat	ct tty_un_s *unat	uned char withs =	D;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returnat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		returnat 	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		returnat
	ed_SEch->**_ed;
	if (!ed || bd->ma t	I!N DGNC_BOARD_MAGICt
		returnat
	dev_dbg(str->dev, "_tty_clossany_xs inu b rt\n"tat_cspin__til_(rqsrec,&ch->**__til,hs =	D/*
	bn->dd_ops->sany_immedietl_s inuchru )atlspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
 	dev_dbg(str->dev, "_tty_clossany_xs inufinish\n"tat}


  * DefiReturn le.emu o calscse la.

#inic int dglrmeldgnc_tty_gls_mes *(ct dgnc***ap hig *ch)
c_igned char =	50mst *at	dgncresulte= -EIOat	gned char withs =	D;

	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		return -ENXIOat
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	mst *_	 (ch->**_most *_|s**->**_mies *)at
	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*

	resulte= 0at 	if (mes *_& UART_MCRKDTRt
		resulte|= TIOCMKDTR; 	if (mes *_& UART_MCRKRTSt
		resulte|= TIOCMKRTS; 	if (mes *_& UART_MSR_CTSt
		resulte|= TIOCMKCTS; 	if (mes *_& UART_MSR_DSRt
		resulte|= TIOCMKDSR; 	if (mes *_& UART_MSR_RIt
		resulte|= TIOCMKRI; 	if (mes *_& UART_MSR_DCDt
		resulte|= TIOCMKCD*
 	return resultat}

  * DefiReturn le.emu o calscse la.

#inic int dgnc_tty_gls_me.em_infouct tty_***ap hig *chrugned chardgnc0__ */
#ivalust
c_idgncresultat
	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		return -ENXIOat
	resulte= _tty_gls_mes *(chtatt	if (resulte< 0)_c	return -ENXIOat
	return pul_ */
(result,lvalus)*
}

 * Defi_tty_sas_me.em_infou) * ThisSls.le.emu o cals, c*lledaby la.

#inic int dgnc_tty_sls_me.em_infouct tty_struct *tty, strugned chardgnccommintrugned chardgnc__ */
#ivalust
c_ict tty__board	*dg0*ed;
ict tty_***ap hig *ch;
	ct tty_un_s *unat	dgncrete= -ENXIOat	gned chardgncarg_	 0at	gned char withs =	D;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returncretat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		return retat
	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		return retat
	ed_SEch->**_ed;
	if (!ed || bd->ma t	I!N DGNC_BOARD_MAGICt
		return retat
	retI= 0at 	retI= gls_u*/
(arg,lvalus)*
	if (rett
		return retat
	switch (commint).c_ic preTIOCMBIS: 		if (arg_& TIOCMKRTS)t			ch->**_most *_|= UART_MCRKRTS*
 		if (arg_& TIOCMKDTRt
			ch->**_most *_|= UART_MCRKDTRat_c	es
 *;
 	c preTIOCMBIC: 		if (arg_& TIOCMKRTS)t			ch->**_most *_&= ~(UART_MCRKRTStat_c	if (arg_& TIOCMKDTRt
			ch->**_most *_&= ~(UART_MCRKDTRtat
		es
 *;
 	c preTIOCMSET:t_c	if (arg_& TIOCMKRTS)t			ch->**_most *_|= UART_MCRKRTS*
il hse_c		ch->**_most *_&= ~(UART_MCRKRTStat_c	if (arg_& TIOCMKDTRt
			ch->**_most *_|= UART_MCRKDTRatil hse_c		ch->**_most *_&= ~(UART_MCRKDTRtat
		es
 *;
 	default:t		return -EINVAL;
	}t
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	ch->**_bn->dd_ops->as*/
tple.em_ed cals(chtatt	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
 	return 0;
}

 * Defi_tty_clos_flaglsau) * ThisIoctl he gls.GNU informaiprotthe
diery/
efThi */

#inic int dgnc_tty_open_flaglsauct tty_struct *tty, struct file_flagnc__ */
#iretinfot
c_ict tty_***ap hig *ch;
	ct tty_un_s *unat	ct file_flagnctmpat	gned char withs =	D;

	if (!retinfot
		return -EFAULT;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returnc-EFAULT;

	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		return -EFAULT;

	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		return -EFAULT;

	memslsu&tmp, 0,csizeof(tmp)tat_cspin__til_(rqsrec,&ch->**__til,hs =	D/*
	mpmcpy(&tmp, &ch->**__fla,csizeof(tmp)tat	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
 	if ( of _touu*/
(retinfo, &tmp, sizeof(iretinfot)t
		return -EFAULT;

	return 0;
}

 * Defi_tty_clos_flaslsau) * ThisIoctl he sls.GNU informaiprotthe
diery/
efThi */

#inic int dgnc_tty_open_flaslsauct tty_struct *tty, struct file_flagnc__ */
#inew_infot
c_ict tty__board	*dg0*ed;
ict tty_***ap hig *ch;
	ct tty_un_s *unat	ct file_flagncnew__flaat	gned char withs =	D;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returnc-EFAULT;

	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		return -EFAULT;

	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		return -EFAULT;

	ed_SEch->**_ed;
	if (!ed || bd->ma t	I!N DGNC_BOARD_MAGICt
		return -EFAULT;

	if ( of _.use_ */
(&new__fla, new_info, sizeof(new__flat)t
		return -EFAULT;

	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	* D	hisHantlr spareies
 *scse ahe
ause RTS Togglp/
lhic_cif (!(**->**_.com)_flags =	DI& _COOKRTS_TOGGLEt	&& (new__fla)_flags =	DI& _COOKRTS_TOGGLEt)_c	ch->**_most *_&= ~(UART_MCRKRTStatcif ((ch->**_.com)_flags =	DI& _COOKRTS_TOGGLEt	&& !(new__fla)_flags =	DI& _COOKRTS_TOGGLEt)_c	ch->**_most *_|= (UART_MCRKRTStat 	* D	hisHantlr spareies
 *scse ahe
ause DTR Togglp/
lhic_cif (!(**->**_.com)_flags =	DI& _COOKDTR_TOGGLEt.&& (new__fla)_flags =	DI& _COOKDTR_TOGGLEt)_c	ch->**_most *_&= ~(UART_MCRKDTRtatcif ((ch->**_.com)_flags =	DI& _COOKDTR_TOGGLEt.&& !(new__fla)_flags =	DI& _COOKDTR_TOGGLEt)_c	ch->**_most *_|= (UART_MCRKDTRtat
	mpmcpy(&ch->**__fla,c&new__fla, sizeof(new__flat)at
	if (ch->**_.com)_flagmaxcpsl< 1)_c	ch->**_.com)_flagmaxcpslSE1at
	if (ch->**_.com)_flagmaxcpsl> 10000)_c	ch->**_.com)_flagmaxcpslSE10000at
	if (ch->**_.com)_flager size < 10)_c	ch->**_.com)_flager size SE10at
	if (ch->**_.com)_flagmaxc=	50< 1)_c	ch->**_.com)_flagmaxc=	50SE1at
	if (ch->**_.com)_flagmaxc=	50>s**->**_.com)_flager size)_c	ch->**_.com)_flagmaxc=	50SE**->**_.com)_flager size;

	if (ch->**_.com)_flagtnlen0>s_COOKPLEN)_c	ch->**_.com)_flagtnlen0=s_COOKPLEN;

	if (ch->**_.com)_flagtr len0>s_COOKPLEN)_c	ch->**_.com)_flagtr len0=s_COOKPLEN;

	ch->**_bn->dd_ops->nt em(stytat
	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
 	return 0;
}

 * Defi_tty_sls_sos Dgnu) * inic int  dgnc_tty_clossas_sos Dgnuct tty_struct *tty, struct fileksos Dgnp*(ld_sos Dgnt
c_ict tty__board	*dg0*ed;
	ct tty_***ap hig *chat	ct tty_un_s *unat	uned char withs =	D;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returnat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		returnat 	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		returnat
	ed_SEch->**_ed;
	if (!ed || bd->ma t	I!N DGNC_BOARD_MAGICt
		returnat
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	ch->**_cag =	(Dss	 str->sos Dgnflag =	(;
	ch->**_cai =	(Dss	 str->sos Dgnflai =	(;
	ch->**_cao =	(Dss	 str->sos Dgnflao =	(;
	ch->**_cal =	(Dss	 str->sos Dgnflal =	(;
	ch->**_sb rtcs	 str->sos Dgnflagc[VSTART];
	ch->**_sbitcss	 str->sos Dgnflagc[VSTOP]at
	ch->**_bn->dd_ops->nt em(stytat	_tty_c*dript,chtat
ispin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
}

 ic int  dgnc_tty_closthrottl uct tty_struct *tty, stt
c_ict tty_***ap hig *ch;
	ct tty_un_s *unat	uned char withs =	D;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returnat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		returnat 	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		returnat
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	ch->**_s =	DD|= (CH_FORCED_STOPI)*
 	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
}

 ic int  dgnc_tty_closunthrottl uct tty_struct *tty, stt
c_ict tty_***ap hig *ch;
	ct tty_un_s *unat	uned char withs =	D;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returnat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		returnat 	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		returnat
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	ch->**_s =	DD&= ~(CH_FORCED_STOPI)*
 	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
}

 ic int  dgnc_tty_clossb rtuct tty_struct *tty, stt
c_ict tty__board	*dg0*ed;
	ct tty_***ap hig *chat	ct tty_un_s *unat	uned char withs =	D;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returnat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		returnat 	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		returnat
	ed_SEch->**_ed;
	if (!ed || bd->ma t	I!N DGNC_BOARD_MAGICt
		returnat
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	ch->**_s =	DD&= ~(CH_FORCED_STOP)*
 	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
}

 ic int  dgnc_tty_clossbopuct tty_struct *tty, stt
c_ict tty__board	*dg0*ed;
	ct tty_***ap hig *chat	ct tty_un_s *unat	uned char withs =	D;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returnat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		returnat 	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		returnat
	ed_SEch->**_ed;
	if (!ed || bd->ma t	I!N DGNC_BOARD_MAGICt
		returnat
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	ch->**_s =	DD|= (CH_FORCED_STOP)at
	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
}

 * Defi_tty_clos.hushucharsu) * ThisFlush Freecookeer siz

#ThisNote he sllf, the
bug o verspohe
ceuls who theture *	re:

#Thisflush inth*s sc preDOES NOTwmear,dispstr
he GNU _bit/
hisrrstead, itwmeare "sbopeer siziithahe
sany itsif you
his recn't aly(str."  Jfor guess howsI figuoudsstt wout...DssSRW 2-Jun-98 * ThisIt0es aosoialways c*lledainsrrupt strucorupxt -(JAR 8-Sept-99 * inic int  dgnc_tty_clos.hushucharsuct tty_struct *tty, stt
c_ict tty__board	*dg0*ed;
	ct tty_***ap hig *chat	ct tty_un_s *unat	uned char withs =	D;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returnat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		returnat 	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		returnat
	ed_SEch->**_ed;
	if (!ed || bd->ma t	I!N DGNC_BOARD_MAGICt
		returnat
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	*  Dorceme vingemaysefrer/ ic_
	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
}

  * Defi_TmpWstru.hushuer siz() * ThisFlush Txeer siz (makunhe == out)
* inic int  dgnc_tty_clos.hushuer siz(ct tty_struct *tty, stt
c_ict tty_***ap hig *chat	ct tty_un_s *unat	uned char withs =	D;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returnat
	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		returnat 	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		returnat
	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	ch->**_s =	DD&= ~CH_STOP*
 	*  Flush hur e to te	*/
Iic_cch->**_w_headI= ch->**_w_s.
 ;
 	*  Flush UARTsesparemieuFIFOIic_cch->**_bn->dd_ops->flushuu rtse to uchtat
	if (ch->**_tun.un_s =	D_& (UN_LOW|UN_EMPTY)).c_ilch->**_tun.un_s =	D_&= ~(UN_LOW|UN_EMPTY)atilwake_up_rrupt striblu,&ch->**_tun.un_s =	DEeaUTtat	}
	if (ch->**_pun.un_s =	D_& (UN_LOW|UN_EMPTY)).c_ilch->**_pun.un_s =	D_&= ~(UN_LOW|UN_EMPTY)atilwake_up_rrupt striblu,&ch->**_pun.un_s =	DEeaUTtat	}

	spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
}

  *                                                                             
efThis *e IOCTL funciprotthe
bll
he its helppts
hi
hiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii/  * Defi_TmpWstruioctlu) * ThisTree *uypeassorts thnte ioctl's

#inic int dgnc_tty_openioctluct tty_struct *tty, strugned chardgnccmd,
	cgned char witharg)
c_ict tty__board	*dg0*ed;
ict tty_***ap hig *ch;
	ct tty_un_s *unat	dgncrc;
	gned char withs =	D;
	 dgnc__ */
#iuarg_	 ( dgnc__ */
#i)harg;

	if (!stys|| str->ma t	I!N  TY_MAGICt
		returnc-ENODEV;

	uns	 str->dr.h"
__bitat	if (!uns|| un->ma t	I!N DGNC_UC_CCMAGICt
		return -ENODEV;

	ch_SEun->un_chat	if (!ch || ch->ma t	I!N DGNC_TABIHAC_MAGICt
		return -ENODEV;

	ed_SEch->**_ed;
	if (!ed || bd->ma t	I!N DGNC_BOARD_MAGICt
		return -ENODEV;

	spin__til_(rqsrec,&ch->**__til,hs =	D/*
 	if (un->un_(str_countI<= 0).c_ilspin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
		return -EIOat	}

	switch (cmd).c_t	*  Her/ areiall GNU ic nd*dg0ioctl's soatswe MUSTnomplts thnic_
	c preTCSBRK:t		* D		eo TCSBRK0es SVID thrspro: non-zerocarg_-->, ores
 *
		hisstiblbe reihur irmexploitedaby tcdrainu)/
l	hi
		hisAccordthe he POSIX.1 spec (7.2.2.1.2)res
 *s ld haveb/D		hisbetween 0.25tthe
0.5
sacr dsucepwe'e cask.the
ceme ving
		hisrr hopemiddle:
0.375
sacr ds/
l	hic_c	rcs	 str_cheil_res to(stytat		spin_un_til_(rqivstorc,&ch->**__til,hs =	D/*
		if (rc)_c		return rc;

		rcs	 ch->**_bn->dd_ops->drainu stru0)*
 		if (rc)_c		return -EINTRat
		spin__til_(rqsrec,&ch->**__til,hs =	D/*
 		if (((cmd == TCSBRKt.&& (!arg