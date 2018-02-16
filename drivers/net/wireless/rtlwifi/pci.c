/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "wifi.h"
#include "core.h"
#include "pci.h"
#include "base.h"
#include "ps.h"
#include "efuse.h"
#include <linux/interrupt.h>
#include <linux/export.h>
#include <linux/kmemleak.h>
#include <linux/module.h>

MODULE_AUTHOR("lizhaoming	<chaoming_li@realsil.com.cn>");
MODULE_AUTHOR("Realtek WlanFAE	<wlanfae@realtek.com>");
MODULE_AUTHOR("Larry Finger	<Larry.FInger@lwfinger.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PCI basic driver for rtlwifi");

static const u16 pcibridge_vendors[PCI_BRIDGE_VENDOR_MAX] = {
	INTEL_VENDOR_ID,
	ATI_VENDOR_ID,
	AMD_VENDOR_ID,
	SIS_VENDOR_ID
};

static const u8 ac_to_hwq[] = {
	VO_QUEUE,
	VI_QUEUE,
	BE_QUEUE,
	BK_QUEUE
};

static u8 _rtl_mac_to_hwqueue(struct ieee80211_hw *hw,
		       struct sk_buff *skb)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	__le16 fc = rtl_get_fc(skb);
	u8 queue_index = skb_get_queue_mapping(skb);

	if (unlikely(ieee80211_is_beacon(fc)))
		return BEACON_QUEUE;
	if (ieee80211_is_mgmt(fc) || ieee80211_is_ctl(fc))
		return MGNT_QUEUE;
	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192SE)
		if (ieee80211_is_nullfunc(fc))
			return HIGH_QUEUE;

	return ac_to_hwq[queue_index];
}

/* Update PCI dependent default settings*/
static void _rtl_pci_update_default_setting(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 pcibridge_vendor = pcipriv->ndis_adapter.pcibridge_vendor;
	u8 init_aspm;

	ppsc->reg_rfps_level = 0;
	ppsc->support_aspm = false;

	/*Update PCI ASPM setting */
	ppsc->const_amdpci_aspm = rtlpci->const_amdpci_aspm;
	switch (rtlpci->const_pci_aspm) {
	case 0:
		/*No ASPM */
		break;

	case 1:
		/*ASPM dynamically enabled/disable. */
		ppsc->reg_rfps_level |= RT_RF_LPS_LEVEL_ASPM;
		break;

	case 2:
		/*ASPM with Clock Req dynamically enabled/disable. */
		ppsc->reg_rfps_level |= (RT_RF_LPS_LEVEL_ASPM |
					 RT_RF_OFF_LEVL_CLK_REQ);
		break;

	case 3:
		/*
		 * Always enable ASPM and Clock Req
		 * from initialization to halt.
		 * */
		ppsc->reg_rfps_level &= ~(RT_RF_LPS_LEVEL_ASPM);
		ppsc->reg_rfps_level |= (RT_RF_PS_LEVEL_ALWAYS_ASPM |
					 RT_RF_OFF_LEVL_CLK_REQ);
		break;

	case 4:
		/*
		 * Always enable ASPM without Clock Req
		 * from initialization to halt.
		 * */
		ppsc->reg_rfps_level &= ~(RT_RF_LPS_LEVEL_ASPM |
					  RT_RF_OFF_LEVL_CLK_REQ);
		ppsc->reg_rfps_level |= RT_RF_PS_LEVEL_ALWAYS_ASPM;
		break;
	}

	ppsc->reg_rfps_level |= RT_RF_OFF_LEVL_HALT_NIC;

	/*Update Radio OFF setting */
	switch (rtlpci->const_hwsw_rfoff_d3) {
	case 1:
		if (ppsc->reg_rfps_level & RT_RF_LPS_LEVEL_ASPM)
			ppsc->reg_rfps_level |= RT_RF_OFF_LEVL_ASPM;
		break;

	case 2:
		if (ppsc->reg_rfps_level & RT_RF_LPS_LEVEL_ASPM)
			ppsc->reg_rfps_level |= RT_RF_OFF_LEVL_ASPM;
		ppsc->reg_rfps_level |= RT_RF_OFF_LEVL_HALT_NIC;
		break;

	case 3:
		ppsc->reg_rfps_level |= RT_RF_OFF_LEVL_PCI_D3;
		break;
	}

	/*Set HW definition to determine if it supports ASPM. */
	switch (rtlpci->const_support_pciaspm) {
	case 0:{
			/*Not support ASPM. */
			bool support_aspm = false;
			ppsc->support_aspm = support_aspm;
			break;
		}
	case 1:{
			/*Support ASPM. */
			bool support_aspm = true;
			bool support_backdoor = true;
			ppsc->support_aspm = support_aspm;

			/*if (priv->oem_id == RT_CID_TOSHIBA &&
			   !priv->ndis_adapter.amd_l1_patch)
			   support_backdoor = false; */

			ppsc->support_backdoor = support_backdoor;

			break;
		}
	case 2:
		/*ASPM Iitno;
			} rsfg_rfps_level |= RT_RF_OFF_LEVL_PCI_D3;
		ipCI_ suppor   !pcibridge_vendor;
= ID,
	ATI_VENDOR_IDDOR_I		boort_backdoor = true;
			ppsc->suak;
		}
	case 1:{
			/*Support ASPM. }ermine if i{
	stru RT_er.TRACE(riv *pc, COMP_ERR, DBG_EMERG,psc->"	case 0vel |n= fprocessed\n")termine if it suppitcshiba 1:{
	issue,itcshiba TY; wCI_D1:{
	selfly
LPS_so weeneral Pn= fCI_D1:{
	inpcibridgst_amk;
ne d_ot fig_byte:{
			/*Npr =, 0x8er@&psc->supp)TL8192SE)
pport_E)
		i.(ieee80211_is_nullfunc(fc))
			reth)
			psc->supp211_0x43)
suak;
		}
	case 1:{
			pm;
			}ee80211_h_backee80211_hplatm>
 _	case 
{
viceeak;

	ca(psc-sk_buff *skb)
{
	struct rtl;
	pLEVL_hw);
	struct rtl_
	u8 pcibridge_vendor = pcipriv->ndis_adapt(hw));
	__le16 fc = rtl_get_fc(skb);
	u8 queue_iL8192SE)
		if (ieee802!1_is_nullfunc(fc))
			return LEVL_P|1_0x40ort_ak;
c.,
 _ot fig_byte:{
			/*Npr =, 0x8er@LEVL_hUpdate PCI pm;
			}ee/*When weenI_D0x01itchnitialiclk redefs_ sSI_D0x0itch RF_LPSiclk red.tting(struct ieee80211_h	case 
clk
neq sk_buff *skb)
{
	struct  
	pLEVL_hw);
	struct rtl_
	u8 pcibridge_vendor = pcipriv->ndis_adapt(hw));
	__le16 fc = rtl_get_fc(skb);
	u8 queue_iL8ak;
c.,
 _ot fig_byte:{
			/*Npr =, 0x81r@LEVL_hUpda192SE)
		if (ieee80211_is_nullfunc(fc))
			return udelay(100)		}ee/*DRF_LPSi))
			reth_OFF_& DRF_LPSiPbriBridgeh_OFFtting(struct iee80211_h RF_LPS

	ca(iv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 pcibridge_vendor = pcipriv->ndis_adapter.pcibridge_vendor;
	u8 init_aspm;

	ppsc->reg_rfps_level = 0;
	pnum4bytes;
	u8 init_aspm;

	ppsc->num4bytes;suppRetri}

 originrtlot figuu Scieefault_se suppo
	plinkctrl
neg;
	u8 init_aspm;

	ppsc->linkctrl
neg 0;
X] = {
	INTELlinkctrlneg;
	u8 init_aspm;

	ppsc->reg_r= {
	INTELlinkctrlneg 0;
X] 
	ca	/*Update PC
	ptmp_u1bdate Pda192S!ak;
		}
	case 1:{
pe == HARD Pda192Spcibridge_vendor;
= ID,
	ATI_VENDOR_IDUNKNOWN		boorer.TRACE(riv *pc, COMP_POWER, DBG_TRACE,psc->"ID,(Bridge) UNKNOWN\n")tee == HARD Pit suL_ASPM)
			ppsc->reg_rfps_level | |= RT_RF_PS_LEVE	boorer.CLEAR->reg_rfpSPM)
,s_level |= RT_RF_PS_LEVEL_ALee80211_h	case 
clk
neq ct  0x0)f it suppe_vepromisd3) {
vice TY; winpL0 ng(se afsc-o h I/O.gst_amk;
ne d_ot fig_byte:{
			/*Npr =, 0x8er@&tmp_u1b); supportscorrespospm3) LEVL_.gst_a
	ca	/*Upd|1_BIT(0) |_BIT(1)f ilinkctrl
neg;OFF_
	ca	/*Up;
r= {
	INTELlinkctrlneg_OFF_LBIT(0) |_BIT(1)); suee80211_hplatm>
 _	case 
{
viceeak;

	ca(ct  linkctrl
negdaptedelay(50); supp4 DRF_LPSiPbriBridgeh_OFFgst_amk;
c.,
 _ot fig_byte:{
			/*Npr =, (num4bytes;<< 2),psc->suuuu= {
	INTELlinkctrlneg); suedelay(50); }ee/*
 *En_LPSi))
			reth_OFF_& En_LPSiPbriBridgeh_OFFge recepowc-osavm3) Weeneral Pfollow1 Frasedefarrytchnitialece))
			rethfirst1 FrnhnitialiPbriBridgeh_OFFeceoal Pubsystem TY; wChow1bVL_screeger.nting(struct iee80211_hnitial

	ca(iv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 pcibridge_vendor = pcipriv->ndis_adapter.pcibridge_vendor;
	u8 init_aspm;

	ppsc->reg_rfps_level = 0;
	pnum4bytes;
	u8 init_aspm;

	ppsc->num4bytes;su
X] 
	ca	/*Up 0;
	pu_reg_rfps_l
	cafault_s 0;
	pu_{
vicee
	cafault_s 0da192S!ak;
		}
	case 1:{
pe == HARD Pda192Spcibridge_vendor;
= ID,
	ATI_VENDOR_IDUNKNOWN		boorer.TRACE(riv *pc, COMP_POWER, DBG_TRACE,psc->"ID,(Bridge) UNKNOWN\n")te == HARD Pit supp4 En_LPSiPbriBridgeh_OFFgackdou_reg_rfps_l
	cafault_s =
	uuuu= {init_aspm;

	ppsc->reg_rfps_llinkctrlneg_|
	uuuu	if (ppsc->reg_ostak;

	ca_fault_s 0da192Spcibridge_vendor;
= ID,
	ATI_VENDOR_IDDOR_I	rn u_reg_rfps_l
	cafault_s OFF_BIT(0)_iL8ak;
c.,
 _ot fig_byte:{
			/*Npr =, (num4bytes;<< 2),psc->suuuuu_reg_rfps_l
	cafault_s)_iL8er.TRACE(riv *pc, COMP_INIT, DBG_LOUD rtl_"Platm>
 En_LPS_OFF(): W.,
 *neg[%xK_QU%x\n" rtl_(= {init_aspm;

	ppsc->reg_rfps_lregehdr_offnI_D+ 0x10),pscuu_reg_rfps_l
	cafault_s)_iL8edelay(50); suppGe		bool 	/*Upd(n to/n to halt.
		 * *)gst_a
	ca	/*Updpci->const_pci_a{
viceak;

	ca_fault_s 0	u_{
vicee
	cafault_s;
	u8 init_aspm;

	ppsc->linkctrl
neg 0suppee80211_hplatm>
 _	case 
{
viceeak;

	ca(r =,st_a/*(init_aspm;

	ppsc->linkctrl
neg |_boolL/*Upadagackdou_{
vicee
	cafault_s;|1_
	ca	/*Up 0suee80211_hplatm>
 _	case 
{
viceeak;

	ca(ct  u_{
vicee
	cafault_shUpda192SPM)
			ppsc->reg_rfps_level | |= RT_RF_PS_LEVE	booree80211_h	case 
clk
neq ct  SPM)
			ppsc->reg_rfps_reg_rfps ps_level |= RT_RF_PS_LEVE ? 1 : 0)f i8er.SET->reg_rfpSPM)
,s_level |= RT_RF_PS_LEVEL_A}
 udelay(100)		}ee80211_h_backe80211_hee80lse; */

			(iv *rtlpriv = rtl_priv(hw);
	struct rtl_
	u8 pcibridge_vendor = pcipriv->ndis_adap
t_backd021us_aspm;
			b
	poffnI__ee PC
nsignedpoffnI__e4_iL8ak;
c.,
 _ot fig_byte:{
			/*Npr =, 0xe0, 0xa0)_iL8ak;
ne d_ot fig_byte:{
			/*Npr =, 0xe0, &offnI__eehUpda192SoffnI__ee211_0xA0E	boorak;
ne d_ot fig_dword:{
			/*Npr =, 0xe4, &offnI__e4)f i8192SoffnI__e4s_lBIT(23wq[qued021us_as	ppsc->}pdate PCI d021us		}ee80211_h_backe80211_hchecrtl_ddy_pcidesk_buff *skb)
{
	struct rtl;	al *rtlhal = rtl_pci_p*l_ddy_pcidhw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_pri_backfindtl_ddy_pcid_aspm;
			b	struct rtl_pci_ptpcid_asNULLpriv(hw);
	struct rtl_pst_ctl *ppscNULLprda192S!lii_aempty(&E)
pport_glb_vart_glb_ppor_lii_)E	boorlii_am>
_eae 
entry(t *pc, &E)
pport_glb_vart_glb_ppor_lii_ rtl;	al *lii_)	boort192St *pc)	boort	t_ctl *ppsc(v(hw);
	struct rtl_ps)tpport_pporc->su8er.TRACE(riv *pc, COMP_INIT, DBG_LOUD rtlsc->"u8 init_aspm;

	ppsc->ac_tnumbc-o%x\n" rtlg_r= {init_aspm;

	ppsc->ac_tnumbc-)c->su8er.TRACE(riv *pc, COMP_INIT, DBG_LOUD rtlsc->"tu8 init_aspm;

	ppsc->ac_tnumbc-o%x\n" rtlg_rtu8 init_aspm;

	ppsc->ac_tnumbc-)tee =rt192S(= {init_aspm;

	ppsc->busnumbc-o==rtl;	al *rtu8 init_aspm;

	ppsc->busnumbc-)h)
			 	al *(= {init_aspm;

	ppsc->r =numbc-o==rtl;	al *tu8 init_aspm;

	ppsc->r =numbc-)h)
			 	al *(= {init_aspm;

	ppsc->ac_tnumbc-o!=rtl;	al *tu8 init_aspm;

	ppsc->ac_tnumbc-))	boort		findtl_ddy_pcid_as	ppsc->su} rsfg_rfps. }erm }erm}->}pdaer.TRACE(riv *pc, COMP_INIT, DBG_LOUD rtl_"findtl_ddy_pcid_%d\n",kfindtl_ddy_pcidhUpda192Sfindtl_ddy_pcidhrtl*l_ddy_pcid_as	pporc-date PCI pindtl_ddy_pcid		}ee80211_hct iee80211_hee80linkcontrol_field(iv *rtlpriv = rtl_priv(hw);
	struct rtl_
	 rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	str
	u8 pcibridge_vendor = tl_psc(dapter.capabilityoffnI_D
	u8 init_aspm;

	ppsc->reg_rfps_lregehdr_offnI_;po
	plinkctrl
neg 0;
	pnum4bbytes;s
	num4bbytespsc(capabilityoffnI_D+ 0x10) / 4 0suppRe d  Link@realrol * gii_idgst_amk;
ne d_ot fig_byte:{
			/*Npr =, (num4bbytesp<< 2), &linkctrl
negdap
r= {init_aspm;

	ppsc->= {
	INTELlinkctrlneg;
	linkctrl
neg 0}ee80211_hct iee80211_hpars _ot figuu Scie(iv *rtl11_h e_ps_r =,
c-sk_buff *skb)
{
	structhw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_prPC
	ptmp;su
X] linkctrl
neg 0suppLink@realrol * gii_idgst_amk; _oapability
ne d_word:pr =, ID,
EXP_LNKCTL, &linkctrl
negdapr= {init_aspm;

	ppsc->linkctrl
neg;
	(u8)linkctrl
neg 0suer.TRACE(riv *pc, COMP_INIT, DBG_TRACE, "Link@realrol * gii_idg=%x\n" rtl_= {init_aspm;

	ppsc->linkctrl
neg)_iL8ak;
ne d_ot fig_byte:pr =, 0x98r@&tmpdaprtmpd|1_BIT(4dapr= {
c.,
 _ot fig_byte:pr =, 0x98r@tmpdapprtmpd1_0x17apr= {
c.,
 _ot fig_byte:pr =, 0x70fr@tmpdap}ee80211_hct iee80211_hpsc->supp(iv *rtlpriv = rtl_priv(hw);
	struct rtl_uct rtl_pci *rtlpci = rtl_pcidev(rtl_suee80211_hw *hw)
{
	struct rtl_prtl_prPC192SPM)
			ppsc->reg_rfps_level |>reg_rfps_level |= R)	boor/* from initializatio&lt.
		 * * suppore80211_hnitial

	ca(pcipri8er.SET->reg_rfpSPM)
,s_level>reg_rfps_level |= R)c->}pd}ee80211_hct ieee80211_hio_handlerhpsc-(iv *rtl{
vice *r =,
c-;	al *rtlhal = *skb)
{
	structhw);
	struct rtl_pci_priv *pcipriv = rtl_pcipr
	E)
pport_io>r =iprr =pr
	E)
pport_io>c.,
 8

	yncD
	u8 _c.,
 8

	ync;
	E)
pport_io>c.,
 16

	yncD
	u8 _c.,
 16

	ync;
	E)
pport_io>c.,
 32

	yncD
	u8 _c.,
 32

	yncpr
	E)
pport_io>ne d8_	yncD
	u8 _ne d8_	ync;
	E)
pport_io>ne d16
	yncD
	u8 _ne d16
	ync;
	E)
pport_io>ne d32
	yncD
	u8 _ne d32
	ync;
	}ee80211_h_backee802w *hw)
earlymodult foesk_buff *skb)
{
	struct rtltlhal = rtl_hal(rtl,rtlhal = rtltcb
{
ci *tcb
{
ci  
	ptidhw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
 *skb)
{
	txlt fo *t fo = IEEEb)
{
	SKB_CBpping(sk(hw));
	__le16 fc = rtl_get_fc(skb);
	u8 queue_intlhal = rtl_hal(nexuctkb 0;
	padd/
	swlenl_gFCreg_N 0supp here openlis 4, wep/tkiplis 8r@aespis 12uppoHIGH_ fost_pclrol.(iekeyhrtladd/
	swlenl+= _ fost_pclrol.(iekeyt_icr_len 0supp cludmos = rbpnumpis 6suppotcb
{
ci->empkt_numpate PCspin_.
		_bh(&E)
pport_.
		s.waitq_.
		e_intkb0211_iswalk(&E)
pport_macb)
{
.tkb0waitq[tid], nexuctkb)	boorv(hw);
 *skb)
{
	txlt fo *nexuct fotee =nexuct fo = IEEEb)
{
	SKB_CBpnexuctkb)f i8192Snexuct fo->flagso&lIEEEb)
{
	TX_CTL_AMPDU)	boorttcb
{
ci->empkt_len[tcb
{
ci->empkt_num] =rtl;	nexuctkbt_.enl+padd/
	swlen;oorttcb
{
ci->empkt_num++PM. } e;
		boort_sfg_rfps_l i8192Stkb0211_ism;
last(&E)
pport_macb)
{
.tkb0waitq[tid],
c-;	al *r nexuctkb))oort_sfg_rf i8192Stcb
{
ci->empkt_nump>_get_		if max
earlymodulnum)oort_sfg_rfm}->spin_un.
		_bh(&E)
pport_.
		s.waitq_.
		e_idate PCI 	ppsc-gs*/
sjus =e_veearly2 of |n=w.nting(struct ieee80211_htxlchk0waitq(iv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	strmac *mac =
	strmac rtl_pcidev(rtl_pcipriv(hw));
	u8 pcibridge_vendor = pcipriv->ndis_adapttlhal = rtl_hal(rtl_asNULLpriv(hw);
 *skb)
{
	txlt fo *t fo = NULLpriv(hw);
	stre16 fc = rtl_get_fc(skb);
	u8 queue_inintptidprda192S!E)
pport_c = rt.earlymodulnitialpe == HARD Pda192SE)
pport_dm.}
	c_phymodul	case 0)
		al *(E)
pport_e
	y_ot currentct r.	case 
in_process ||		al *(E)
pport_l_ddy_pcid_)
		al *E)
pport_l_ddy_pcidt_e
	y_ot currentct r.	case 
in_process_is_mgmt(fc);supp weejusthout em=e_veBE/BK/VI/VOsuppoe_ve(t_ada 7;ptidp>_g0;ptid--)	boor
	p(ie211_ida dependent  rtltidpendac tidh];oorv(hw);
 rt			r	txlrd3) {rt_s;
	&{
			/*Ntxlrd3)[(ie211_i];oorwhtion(!mac->acuctcannt_s O
			_hal *rE)
pport_M)
.c->wr_ng(se == ERFON		boor	tlhal = rtltcb
{
ci tcb
{
ci;oortmeafau(&tcb
{
ci  0,rtizeof(tlhal = rtltcb
{
ci))tee =rspin_.
		_bh(&E)
pport_.
		s.waitq_.
		e_ini8192S!tkb0211_isempty(&mac->tkb0waitq[tid])h)
			   s*(Et_st_entri}s - tkb0211_islen(&Et_st_211_i) >psc->suuuet_		if max
earlymodulnum))	boort	rtl_astkb0detruct &mac->tkb0waitq[tid])_ini8} e;
		boort>spin_un.
		_bh(&E)
pport_.
		s.waitq_.
		e_isu} rsfg_rfps.}
rt>spin_un.
		_bh(&E)
pport_.
		s.waitq_.
		e_i&&
			 Somudmacaddrunde'tl{oeearly2 of .  BEApsc->* mtruicast/broadcast/no_qos *hwasupportt fo = IEEEb)
{
	SKB_CBpping(sk	oHIGH_ fostflagso&lIEEEb)
{
	TX_CTL_AMPDU)isu} ee802w *hw)
earlymodult foect  rtl,->su} c->su&tcb
{
ci  tidh_i&&
	E)
pport_intf_ops->a	ppsc-	txect  NULL  rtl,u&tcb
{
ci);erm}->}pgs*ing(struct ieee80211_htxlisr sk_buff *skb)
{
	struct  intpppoohw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	structu8 pcibridge_vendor = pcipriv->ndis_adap
tv(hw);
 rt			r	txlrd3) {rt_s;
	&{
			/*Ntxlrd3)[ppoo]ap
twhtion(tkb0211_islen(&Et_st_211_i))	boorv(hw);
 rtl_hal(rtl;oorv(hw);
 *skb)
{
	txlt fo *t foL_ALeex = skb;oor
	ptidpror
	p*entryrf i8192SE)
pport_out_new_trx_flow)isu}entry;
	(u8 *)(&Et_st_l_hac-	{
ci[Et_st_idx])_inie;
	isu}entry;
	(u8 *)(&Et_st_{
ci[Et_st_idx])_i i8192SE)
pport_cfst_ops->ee80lvailtial
{
ci O
			_halE)
pport_cfst_ops->ee80lvailtial
{
ciect  ppooh <= 1)	boorter.TRACE(riv *pc, (COMP_INTR | COMP_SEND), DBG_DMESG,
c-;	a"no lvailtial {
ci!\n")te =gmt(fc);sus_l i8192S!E)
pport_cfst_ops->m;
txl{
ci_closedect  ppoo, Et_st_idxwq[queue_indte ==t_st_idx;
	(=t_st_idx;+ 1)	% Et_st_entri}s_i i8rtl_as__tkb0detruct &Et_st_211_i);oorak;
unmap_sd3)le:{
			/*Npr =,
c-;	aE)
pport_cfst_ops->reg_rfps psee80{
cie(u8 *)entry, 	pps,->su} chal *rHWi");
	TXBUFF_ADDR),
c-;	atkbt_.en, ID,
DMA_TODEVICE)_i i8		 removeeearly2 of |he didgst_a8192SE)
pport_E)
		i.earlymodulnitialpe =ntkb0pull(rtl,uEM_HDReg_N)_i i8er.TRACE(riv *pc, (COMP_INTR | COMP_SEND), DBG_TRACE,psc->"new Et_st_idx:%d,nd/or: tkb0211_islen:%d,nd/or: teq:%x\n" rtlg Et_st_idx rtlg tkb0211_islen(&Et_st_211_i) rtlg *(
X] *)(tkbt_*hwas+ 22))_i i8192Sppoo == TXCMD}

/* )	boortr =_kd/or_tkbpping(sk	ogo 51 x_d021us_o_rf i8}i i8		 e_vesw LPS,sjus =afsc-oNULL= rbpsend ou Incon:
n
c->* sure AP kn=wsnconet, sleeacon, weeneral Pn= flet
c->* rf sleea
c->*t_a8kb_get_queue_mapping(sk HIGH_QUEUE;

	return ac_to_hwq	boort192S *skb)
{
	sas_pmo_hwq	boort	E)
pport_macb)
{
.offchan_delay_as	ppsc->su}E)
pport_M)
.ng(selt ap_as	ppsc->su} e;
		boort>E)
pport_M)
.ng(selt ap_aspm;
			bre}erm}-> HIGH_QUEUE;

	retacui80211_i	boor	tlhal =_QUEUE;

	n MG *acui80_framud=rtl;	 sk_buff *skb)
{
	n MG *)tkbt_*hwa(sk	oHIGHacui80_framut_o.acui80.u.huctmps.acui80o==rtl;l *rWLAN_HT_ACatic_SMPSq	boort	r =_kd/or_tkbpping(sk	oogo 51 x_d021us_o_rfbre}erm}- i8		 w *hw)ptidptx pktpnump*t_a8t_ada t_queue_t_apping(sk HIGHt_ad<= 7)&&
	E)
pport_linklt fo.t_atxlt pepood[tid]++PM
rtt fo = IEEEb)
{
	SKB_CBpping(sk	 *skb)
{
	txlt fo_clear_d021usH_ fo)PM
rtt fostflagso|=lIEEEb)
{
	TX_STAT_ACK; i8		t fostd021us.rhw)s[0].coun_D
	1_backdoo *skb)
{
	txld021us_irqsafeect  rtl)_i i8192S(Et_st_entri}s - tkb0211_islen(&Et_st_211_i)h <= 4q	booorter.TRACE(riv *pc, COMP_ERR, DBG_DMESG,
c-;	a"ved a cci lef Incake tkb0211_i@%d,n=t_st_idx;
	%d,ntkb0211_islend1_0x%x\n" rtlg_ ppoo, Et_st_idx,
c-;	atkb0211_islen(&Et_st_211_i)h_i&&
	 *skb)
{
	cake0211_i(ct rtl;		y(ieee80211_is_beaconrtl;		pping);erm}- x_d021us_o_: i8rtl_asNULLprit suL_AS((E)
pport_linklt fo.num_rxlt pepood +

	E)
pport_linklt fo.num_txlt pepood) > 8) ||			(E)
pport_linklt fo.num_rxlt pepood > 2_i	boorE)
pport_eclud_ps_aspm;
			b	scheing__work(&E)
pport_works.l_ucthang__work)c->}p}ee80211_hintpee80211_hisc->one_rx{
ciesk_buff *skb)
{
	struct rtl;	al *tlhal = rtl_hal(new_rtl,u
	p*entry rtl;	al *intprxEt_s_idx,*intp{
ci_idxww);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	structu8 pcibridge_vendor = pcipriv->ndis_adap	u32 l_hac-address;PC
	ptmp_oneD
	1_pttlhal = rtl_hal(rtl Pda192S BEACONnew_rtl))	boorvtl_asnew_rtl		b	go 51remaprfm}->stl_asr =_al.
	_tkbp{
			/*Nrxl_hac-tize)TL8192S!rtl_pueue_indee Pdremap:supp jus =nI_Dtkbt_cbytch_beacon addrue_vepk;
unmap_sd3)lehout *t_a*((dma_addr_G *)tkbt_cb)d=rtlpk;
map_sd3)le:{
			/*Npr =,atkb0opy _poncludpping,psc->suuuu {
			/*Nrxl_hac-tize, ID,
DMA_FROMDEVICE)_i	l_hac-address_as*((dma_addr_G *)tkbt_cb);da192Spci_dma__beacon_error:{
			/*Npr =,al_hac-address)_pueue_indee P	{
			/*Nrxlrd3)[rxEt_s_idx].rxll_h[{
ci_idx]_astkb;
8192SE)
pport_out_new_trx_flow)	boorE)
pport_cfst_ops->se80{
ciect  Su8 *)entry, pm;
	 rtlsc-> *rHWi");
	RX_PREPARE rtlsc-> *rSu8 *)&l_hac-address)rfm} e;
		boorE)
pport_cfst_ops->se80{
ciect  Su8 *)entry, pm;
	 rtlsc-> *rHWi");
	RXBUFF_ADDR rtlsc-> *rSu8 *)&l_hac-address)rfmrE)
pport_cfst_ops->se80{
ciect  Su8 *)entry, pm;
	 rtlsc-> *rHWi");
	RXPKTeg_N rtlsc-> *rSu8 *)&{
			/*Nrxl_hac-tize)TL8rE)
pport_cfst_ops->se80{
ciect  Su8 *)entry, pm;
	 rtlsc-> *rHWi");
	RXOWN rtlsc-> *rSu8 *)&tmp_oneEL_A}
 ue_inde1c-gs*/
sinoreral oc Licens 8K AMSDU weeublicnI_Dtkbl oet>
9100bytespinreg_rprx Et_sen theifSoftwarackI_Diationn= fa AMSDU,SoftwalargearackI_DTY; withsci_u oet>
TCP/IP di Litly,Softwacaout bigarackI_Dacon fpy et>
 BEA: "acon -s 65507",_so here we TY; wer	<.
	Dtkbet>
"
#id o or
 *	ppsrtizewithrackI_, Macb)
{
et>
Probtiay TY; wdonst_betluden thedoespn= fyeter.net>
Somudplatm>
  TY; wfpy  wFrnh	<.
	Dtkb_sometimeof thed LICENScospmSciencwe TY; wsend r
 *ol Ptkbl oet>
macb)
{
 di Litly,SoftwaTY; wn= fcaout any or
 receiissuesen theonlySoftwarackI_DTY; withlos =by
TCP/IPr.nting(struct ieee80211_hrx_to_macb)
{
esk_buff *skb)
{
	struct rtl;	al *tlhal = rtl_hal(rtl,->su}l *rtlhal = *skb)
{
	rx_d021us rx_d021usww);
return BEACON!E)
tacui80_procect  rtl, pm;
	)))	boorr =_kd/or_tkb_anypping(sk} e;
		boortlhal = rtl_hal(urtl_asNULLprir
	p*p*hwa(srir
stl_asr =_al.
	_tkbptkbt_.enl+p128g(sk HIGH BEACONurtl))	boortmeacpy(IEEEb)
{
	SKB_RXCBpurtl), &Ex_d021us,psc->suuuu tizeof(rx_d021uswg(sk	op*hwa;
	(u8 *)tkb0putpurtl,atkbt_.en);oortmeacpy(p*hwa,atkbt_*hwa,atkbt_.en);oortr =_kd/or_tkb_anypping(sk
	 *skb)
{
	rxltrqsafeect  uping(sk
} e;
		boort *skb)
{
	rxltrqsafeect  ping(sk
}->}p}ee/*hsisr nclude <l handlernting(struct ieee80211_hhs_iclude <l(iv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	structu8 pcibridge_vendor = pcipriv->ndis_adap
tpcipc.,
 _byte:{
		*pc, E)
pport_cfst__bes[MAC_HSISR] rtl_hal *rpcipne d_byte:{
		*pc, E)
pport_cfst__bes[MAC_HSISR])->reg>suuuu {
			/*Nsys_irq_maskdap}ee80211_hct ieee80211_hrx_iclude <l(iv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	structu8 pcibridge_vendor = pcipriv->ndis_adap	intprxEt_s_idx pteron to RX_MPDUTYPE_RTL8tlhal = *skb)
{
	rx_d021us rx_d021us_QUE 0 };PC
nsignedpintpcoun_D
	{
			/*NrxEt_scoun_;PC
	pown;PC
	ptmp_one;ri_backunicast_aspm;
			b
	p(ie211_ida 0;PC
nsignedpintprxlremained_cn_;PCv(hw);
	strd021s d021s QUEUE	.signrtl_g0 rtl.rhw)l_g0 rt} 0suppRX NORMAL PKT *t_awhtion(coun_--)	booriv *rtlpriv = rtl_dru*_drL_ALeex = skb;oor
X] len;oor/*rx l_hac-p{
ciriptodgst_a8v(hw);
	strrxll_hac-	{
ci *l_hac-	{
ci asNULLprir		   out new trx flow,nst_meanwaTYfi t fo *t_a8v(hw);
	strrxl{
ci *p{
ci asNULLprir		rx pktp*t_a8v(hw);
 rtl_hal(rtl_as{
			/*Nrxlrd3)[rxEt_s_idx].rxll_h[->su}l *r s{
			/*Nrxlrd3)[rxEt_s_idx].idx];_a8v(hw);
 rtl_hal(new_rtl		 i8192SE)
pport_out_new_trx_flow)	boortrxlremained_cn_d=rtl;	E)
pport_cfst_ops->rxl{
citl_halremained_cn_(ct rtl;		su}l *r s(ie211_ig(sk	oHIGHrxlremained_cn_d=_g0)isu} ue_indte =	l_hac-	{
ci as&{
			/*Nrxlrd3)[rxEt_s_idx].l_hac-	{
ci[rtl;	E)
p	/*Nrxlrd3)[rxEt_s_idx].idx];_a8	p{
ci as(v(hw);
	strrxl{
ci *)tkbt_*hwa(sk	} e;
		b8		 rxp{
ciriptodgst_a8	p{
ci as&{
			/*Nrxlrd3)[rxEt_s_idx].{
ci[rtl;	E)
p	/*Nrxlrd3)[rxEt_s_idx].idx];_rtl;own;
	(u8)E)
pport_cfst_ops->ee80{
cie(u8 *)p{
ci,->su} c->suuuupm;
	 rtlsc- chal *rHWi");
	OWN	(sk	oHIGHown) pp wast_*hwa; ocithfi * wlby
hardeet, st_a8	gmt(fc);sus_l i8/Road chcon oftwaroncl_meanw:_*hwa; andi * wlalne dy
c->* AAAAAAtlunui80o!!!
c->* Won:
n NOT access 'tkb'citfed a'pk;
unmap_sd3)le'
c->*t_a8ak;
unmap_sd3)le:{
			/*Npr =,s*((dma_addr_G *)tkbt_cb),
c-;	aE)
p	/*Nrxl_hac-tize, ID,
DMA_FROMDEVICE)_i i8/RogI_D1 new rtl_-eifSfpy ,*ol PoneDTY; withreu#id *t_a8new_rtl_asr =_al.
	_tkbp{
			/*Nrxl_hac-tize)TL8
return BEACON!new_rtl))sk	ogo 51no_newTL8
meafau(&rx_d021us_, 0 , tizeof(rx_d021uswg(sk	E)
pport_cfst_ops->211ryrrxl{
ciect  &d021s,->su} ch&Ex_d021us, (u8 *)p{
ci, rtl)_i i8192SE)
pport_out_new_trx_flow)isu}E)
pport_cfst_ops->rxlchecrtdma_ok(ct rtl;		su *rSu8 *)l_hac-	{
ci rtl;		su *r(ie211_ig(srtllend1_E)
pport_cfst_ops->ee80{
cie(u8 *)p{
ci,upm;
	 rtlsc- *rHWi");
	RXPKTeg_N)_i i8192Stkbt_end -atkbt_opy  > leni	boor	tkb0putprtl,a.en);oort192SE)
pport_out_new_trx_flow)isu}	tkb0reserve(rtl,rtl21s.rxldrvt fo_tizew+rtlsc-> *rtl21s.rxll_hshif_D+ 24)f i8ie;
	isu}	tkb0reserve(rtl,rtl21s.rxldrvt fo_tizew+rtlsc-> *rtl21s.rxll_hshif_g(sk
} e;
		boorter.TRACE(riv *pc, COMP_ERR, DBG_WARNING,
c-;	a"tkbt_end -atkbt_opy  
	%d,nlenlis %d\n",
c-;	atkbt_end -atkbt_opy ,a.en);oortr =_kd/or_tkb_anypping(sk
	go 51new_trx_end(sk
}->upp handlepcomm halrackI_Dhere st_a8192SE)
pport_cfst_ops->rxlcomm ha_rackI_DO
			_halE)
pport_cfst_ops->rxlcomm ha_rackI_ect  p021s, rtl))	boorttr =_kd/or_tkb_anypping(sk
		go 51new_trx_end(sk
}-
k Req
		 *NOTICEthat i:
n n= fithoutue_vemacb)
{
,q
		 *oftwaENSEoneDd Lmacb)
{
 code,q
		 *192EoneDhere sec DHCP TY; wfpy q
		 *tkb0orim(rtl,atkbt_.en -a4)f i8backdoo_drua t_queue__drpping(sk
kb_get_queue_mapping(s
i8192S!tl21s.cri O
 !tl21s.hwerror)	boortmeacpy(IEEEb)
{
	SKB_RXCBprtl), &Ex_d021us,psc->suuuu tizeof(rx_d021uswg(soort192Sf (iroadcast_er
 r_addr(_dr->a	dr1))	boortt;/*TODOst_a8	} e;
		192Sf (mtruicast_er
 r_addr(_dr->a	dr1))	boortt;/*TODOst_a8	} e;
		boorttunicast_as	ppsc->su}E)
pport_tl21s.rxbytesunicast_+=atkbt_.enrfps.}
rt>t_quf (special_*hwaect  rtl, pm;
	)(soort192SfQUEUE;

	ret*hwae_hwq	boort	E)
pport_cfst_ops->led_ot lrolect  LED_CTL_RXg(sk
		returnicast)isu}		E)
pport_linklt fo.num_rxlt pepood++PM. .}
rt>pp 80211_h_cnge_venoamd3) {
	ct>t_quieee80_d021is11_ect  ping(sk
re80212plt foect  (ct ie*)tkbt_*hwa,atkbt_.en);oort		 e_vesw lps {
	ct>t_quswl_ucieee802ct  (ct ie*)tkbt_*hwa,atkbt_.en);oortpcipnecognize_peer2ct  (ct ie*)tkbt_*hwa,atkbt_.en);oort192S(E)
pport_macb)
{
.op of |=_gNLUE;

	IFnc(fcAP)h)
			   s*(E)
pport_E)
		i.currentcb haee80211psc->suuuBAND_ON_2_4G)h)
			   s*(_QUEUE;
	if (ieee80211_ ||			->suuu_QUEUE;
	if (probe0resp211_is	boorttr =_kd/or_tkb_anypping(sk
	} e;
		boorttee80211_hrx_to_macb)
{
ect  rtl, rx_d021uswrfbre}erm} e;
		boortr =_kd/or_tkb_anypping(sk
}
new_trx_endLEVEL_ASE)
pport_out_new_trx_flow)	boortr)
p	/*Nrxlrd3)[(ie211_i].nexucrxlrp_+=a1;oortr)
p	/*Nrxlrd3)[(ie211_i].nexucrxlrp_%=rtl;		eron to MAX RX_COUNT_i&&
	Exlremained_cn_--;oortpcipc.,
 _word:{
		*pc, 0x3B4,
c-;	al *r s{
			/*Nrxlrd3)[(ie211_i].nexucrxlrpg(sk
}->uL_AS((E)
pport_linklt fo.num_rxlt pepood +

	hal *rE)
pport_linklt fo.num_txlt pepood) > 8) ||			hal *r(E)
pport_linklt fo.num_rxlt pepood > 2_i	boorrE)
pport_eclud_ps_aspm;
			b		scheing__work(&E)
pport_works.l_ucthang__work)c->
}->uvtl_asnew_rtl		no_newLEVEL_ASE)
pport_out_new_trx_flow)	boortee80211_hisc->one_rx{
ciect  rtl, Su8 *)l_hac-	{
ci rtl;		sprxEt_s_idx,rtl;		spr)
p	/*Nrxlrd3)[rxEt_s_idx].idxg(sk
} e;
		boortee80211_hisc->one_rx{
ciect  rtl, Su8 *)p{
ci rtl;		sprxEt_s_idx,rtl;		spr)
p	/*Nrxlrd3)[rxEt_s_idx].idxg(sk
EL_ASE)
p	/*Nrxlrd3)[rxEt_s_idx].idx211psc->suu{
			/*NrxEt_scoun_ -a1)oort	E)
pport_cfst_ops->se80{
ciect  Su8 *)p{
ci,->su} c->suupm;
	 rtlsc- chal HWi");
	RXERO rtlsc- chal Su8 *)&tmp_oneEL_A
}->uE)
p	/*Nrxlrd3)[rxEt_s_idx].idx21rtlscSE)
p	/*Nrxlrd3)[rxEt_s_idx].idx2+ 1)	%rtl;	E)
p	/*NrxEt_scoun_;PC}p}ee80211_hirqmt(fc)_tpee80211_hislude <l(ncl_irq,hct ie*r =_idhw);
	structpriv = rtl_priv(_asr =_idpro	struct rtl_
	u8 pcibridge_vendor = pcipriv->ndis_adapt(hw));
	__l_pci_priv *pcipriv = rtl_pcipriv(hw);
	stre16 fc = rtl_get_fc(skb);
	u8 queue_in
nsignedpto thflagsap	u32 nclada 0;PC
32 nclbdate P	irqmt(fc)_tprI_D
	IRQ_HANDLED Pda192SE)
p	/*Nirq_l |= (Rd=_g0)isuue_inderI_;pPCspin_.
		_trqsave(&E)
pport_.
		s.irq_th_.
		 , flags);
	E)
pport_cfst_ops-> RF_LPS
islude <l(pcipr
	/*ne d ISR: 4/8bytesp{
	cE)
pport_cfst_ops->islude <lpnecognizedect  &ncla  &nclb); suppohar(RdIRQ _ve ASPRF_ppar(RduppoHIGH!ncladEUE;clada1_0xffff)isugo 51done;rsupp<1> ieee80erIlat(RduppoHIGH;clad& E)
pport_cfst__bes[eronIMR_TBDOK]		boorer.TRACE(riv *pc, COMP_INTR, DBG_TRACE,psc->"ieee80eok nclude <l!\n")te t suL_ASrn BEACON_clad& E)
pport_cfst__bes[eronIMR_TBDER])		boorer.TRACE(riv *pc, COMP_INTR, DBG_TRACE,psc->"ieee80eude nclude <l!\n")te t suL_AS_clad& E)
pport_cfst__bes[eronIMR_BDOK]		boorer.TRACE(riv *pc, COMP_INTR, DBG_TRACE,>"ieee80enclude <l!\n")te t suL_AS_clad& E)
pport_cfst__bes[eronIMR_BCNINT]		boorer.TRACE(riv *pc, COMP_INTR, DBG_TRACE,psc->"prepar( ieee80ee_venclude <l!\n")te 	taskleuctcheing_(&E)
pport_works.irq_prepar(__cn_taskleu)te t supp<2> TxerIlat(RduppoHIGHrn BEACON_clbd& E)
pport_cfst__bes[eronIMR_TXFOVW]))sk	er.TRACE(riv *pc, COMP_ERR, DBG_WARNING, "IMR_TXFOVW!\n")tee L_AS_clad& E)
pport_cfst__bes[eronIMR_ARE_DOK]		boorer.TRACE(riv *pc, COMP_INTR, DBG_TRACE,psc->"Manageeok nclude <l!\n")te 	ee80211_htxlisr ct  ARE_TYPE_R)te t suL_AS_clad& E)
pport_cfst__bes[eronIMR_x];
DOK]		boorer.TRACE(riv *pc, COMP_INTR, DBG_TRACE,psc->"x];
}

/* eok nclude <l!\n")te 	ee80211_htxlisr ct  x];
}

/* )te t suL_AS_clad& E)
pport_cfst__bes[eronIMR_BKDOK]		boorE)
pport_linklt fo.num_txlt pepood++PM
rter.TRACE(riv *pc, COMP_INTR, DBG_TRACE,psc->"BK TxeOK nclude <l!\n")te 	ee80211_htxlisr ct  e(struct)te t suL_AS_clad& E)
pport_cfst__bes[eronIMR_BEDOK]		boorE)
pport_linklt fo.num_txlt pepood++PM
rter.TRACE(riv *pc, COMP_INTR, DBG_TRACE,psc->"BE TXeOK nclude <l!\n")te 	ee80211_htxlisr ct  eEstruct)te t suL_AS_clad& E)
pport_cfst__bes[eronIMR_VIDOK]		boorE)
pport_linklt fo.num_txlt pepood++PM
rter.TRACE(riv *pc, COMP_INTR, DBG_TRACE,psc->"VI TXeOK nclude <l!\n")te 	ee80211_htxlisr ct   u8 _rtl)te t suL_AS_clad& E)
pport_cfst__bes[eronIMR_VODOK]		boorE)
pport_linklt fo.num_txlt pepood++PM
rter.TRACE(riv *pc, COMP_INTR, DBG_TRACE,psc->"Vo TXeOK nclude <l!\n")te 	ee80211_htxlisr ct   O8 _rtl)te t suL_ASr)
		if (ieee80211_is_nullfunc(fc))
			retu	boorL_AS_clad& E)
pport_cfst__bes[eronIMR_COMDOK]		boorrE)
pport_linklt fo.num_txlt pepood++PM
rtter.TRACE(riv *pc, COMP_INTR, DBG_TRACE,psc-->"CMD TXeOK nclude <l!\n")te 		ee80211_htxlisr ct  TXCMD}

/* )(sk
}->}psupp<3> RxerIlat(RduppoHIGH_clad& E)
pport_cfst__bes[eronIMR_ROK]		boorer.TRACE(riv *pc, COMP_INTR, DBG_TRACE,>"Rxeok nclude <l!\n")te 	ee80211_hrx_iclude <l(pciprit suL_ASrn BEACON_clad& E)
pport_cfst__bes[eronIMR_RDU])		boorer.TRACE(riv *pc, COMP_ERR, DBG_WARNING,
c-; "rxp{
ciriptodgunlvailtial!\n")te 	ee80211_hrx_iclude <l(pciprit suL_ASrn BEACON_clbd& E)
pport_cfst__bes[eronIMR_RXFOVW]))	boorer.TRACE(riv *pc, COMP_ERR, DBG_WARNING, "rxpoverflow !\n")te 	ee80211_hrx_iclude <l(pciprit supp<4> fwerIlat(RuppoHIGHr)
		if (ieee80211_is_nullfunc(fc))
	723Atu	boorL_AS_clad& E)
pport_cfst__bes[eronIMR_C2HCMD]		boorrer.TRACE(riv *pc, COMP_INTR, DBG_TRACE,psc-->"firmeet, nclude <l!\n")te 		211_isdelayed_work(E)
pport_works.pcipcq rtlsc-> *&E)
pport_works.fwevtpcq  0)f i8}->}psupp<5> hsisr rIlat(Ruppo/* OnlyS8188EEd& 	723BE rue;
		ed.
LPS_If Or
 r ICs Comudin, System TY; wcorr <l,
LPS_becaout _bes[eronIMR_xSISR_IND]d& _bes[MAC_HSISR]
LPS_et, n= feg_rfps_led
	 uppoHIGHr)
		if (ieee80211_is_nullfunc(fc))
	188EEd||
	uuuu	if		if (ieee80211_is_nullfunc(fc))
	723Btu	boorL_ASrn BEACON_clad& E)
pport_cfst__bes[eronIMR_xSISR_IND])		boorrer.TRACE(riv *pc, COMP_INTR, DBG_TRACE,psc-->"hsisr nclude <l!\n")te 		ee80211_hhs_iclude <l(pcipri8}e t suL_ASr)
pport_E)
		i.earlymodulnitialpe =taskleuctcheing_(&E)
pport_works.irq_taskleu)te
done:	cE)
pport_cfst_ops->nitial
iclude <l(pciprispin_un.
		_irqmtstode(&E)
pport_.
		s.irq_th_.
		, flags);
	Ee_inderI_;p}ee80211_hct ieee80211_hirq_taskleu(iv *rtlpriv = rtl_priv(hw);
ee80211_htxlchk0waitq(pcipr}ee80211_hct ieee80211_hprepar(__cn_taskleu(iv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	structu8 pcibridge_vendor = pcipriv->ndis_adap	v(hw);
	strmac *mac =
	strmac rtl_pcidev(rtl_pcipriv(hw)			r	txlrd3) {rt_s;
	NULLpriv(hw);
 *skb)
{
	_dru*_dr_asNULLpriv(hw);
 *skb)
{
	txlt fo *t fo = NULLpriv(hw);
 rtl_hal(prtl_asNULLpriv(hw);
rtl_txl{
ci *p{
ci asNULLpritlhal = rtltcb
{
ci tcb
{
ci;oo/*TftwaENSe_venew trx flow) {
	(hw);
rtl_txll_hac-	{
ci *pl_hac-	{
ci asNULLpri
	ptemp_oneD
	1_pt
	p*entryrf imeafau(&tcb
{
ci  0,rtizeof(tlhal = rtltcb
{
ci))te	rt_s;
	&{
			/*Ntxlrd3)[|| ieee80211];_aprtl_as__tkb0detruct &Et_st_211_i);oo192SE)
pport_out_new_trx_flow)isuentry;
	(u8 *)(&Et_st_l_hac-	{
ci[Et_st_idx])_ine;
	isuentry;
	(u8 *)(&Et_st_{
ci[Et_st_idx])_ia192Spskb)	boorak;
unmap_sd3)le:{
			/*Npr =,
c-;	aE)
pport_cfst_ops->ee80{
cie
c-;	a(u8 *)entry, 	pps,rHWi");
	TXBUFF_ADDR),
c-;	aptkbt_.en, ID,
DMA_TODEVICE)_i		kd/or_tkbppping(sk}psuppNB: r
 *ieee80e*hwa;l_hac-pmus =be 32-b verligned.gst_amrtl_as *skb)
{
	ieee80_gI_ect  mac->vif)_ia192Spskb|=_gNULLs_mgmt(fc);su_drua t_queue__drppping(skt fo = IEEEb)
{
	SKB_CBppping(skp{
ci as&{t_st_{
ci[0];oo192SE)
pport_out_new_trx_flow)isupl_hac-	{
ci as&{t_st_l_hac-	{
ci[0]pr
	E)
pport_cfst_ops->di *_txl{
ciect  _dr  Su8 *)p{
ci,->su} Su8 *)pl_hac-	{
ci  t fo  NULL  prtl,->su} || ieee80211,u&tcb
{
ci);eindetkb0211_isopy  &Et_st_211_i  prtl); suL_ASr)
pport_out_new_trx_flow)	boortemp_oneD
	4;
t	E)
pport_cfst_ops->se80{
ciect  Su8 *)pl_hac-	{
ci  	pps,->su} l *rHWi");
	OWN, Su8 *)&temp_oneEL_A} e;
		boorE)
pport_cfst_ops->se80{
ciect  Su8 *)p{
ci  	pps,rHWi");
	OWN,->su} l *r&temp_oneEL_A}
gmt(fc);s}ee80211_hct ieee80211_hisc->trx_var sk_buff *skb)
{
	structhw);
	struct rtl_
	u8 pcibridge_vendor = pcipriv->ndis_adapt(hw));
	__l_pci_priv *pcipriv = rtl_pcipriv(hw);
	stre16 fc = rtl_get_fc(skb); rtl)_pt
	pi;su
X] {
citnumUpda192SE)
		if (ieee80211_is_nullfunc(fc))
			rEturn {
citnum = TXi");
	NUM_	rE_ine;
	isu{
citnum = er.TX");
	NUMUpdae_ve(idate  i < eron to MAX TXi80211_COUNT_ i++)->uE)
p	/*NtxEt_scoun_[i]_asr citnumUpdaReq
 *weejusth	<.
	D2a cci e_veieee80e211_i q
 *becaout weejusthne(Rdfirst1 cci in strieee80.
	 uppoE)
p	/*NtxEt_scoun_[|| ieee80211]_as2UpdaReBE 211_idne(Rdved a cciriptodge_veperf>
 *nce
	 u_pcior u ScieeGNU Nodved atxp{
ci TY; whappen,
	 u halmay caout _bcb)
{
 mea _AUTage.
	 uppoHIGH!iv = rtl_pcit_out_new_trx_flow)isuE)
p	/*NtxEt_scoun_[||e80211]_aser.TX");
	NUM_||e80211pr
	E)
p	/*Nrxl_hac-tize_as9100;aRe2048/1024; uppoE)
p	/*NrxEt_scoun_ = eron to MAX RX_COUNT_aRe64; upp}ee80211_hct ieee80211_hisc->v(hw);esk_buff *skb)
{
	struct rtltlhal =11_h e_ps_r =hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	strmac *mac =
	strmac rtl_pcidev(rtl_pcipriv(hw));
	u8 pcibridge_vendor = pcipriv->ndis_adapttlhal =	stre16 fc = rtl_get_fc(skb);_pcidev(rtl_suE)
p	/*Nup_first_time_as	ppsc->E)
p	/*Nbet_s_isc->s	ppsc- ppsc->constE)
		if (i pphwc->E)
p	/*Npr =iprpr =pr
	/*Tx/RxerIlat(Rdvar uppoee80211_hisc->trx_var pcipr
	/*IBSS*/ mac->ieee80_icludvrtl_g100;r
	/*AMPDUuppomac->min_space_cfsdate P	mac->max_mssh eciotydate P	/*nI_DtaneDAMPDUupdate_dsp{
	cmac->currentcampduh eciotydat7;	cmac->currentcampduhfactor;
	3;r
	/*QOSuppoE)
p	/*Nacm_methoada | iMWAY2_SW;r
	/*tasksuppotaskleucpsc-(&E)
pport_works.irq_taskleu rtl_hal (ct ie(*)(
nsignedpto t))ee80211_hirq_taskleu rtl_hal (
nsignedpto t)pcipritaskleucpsc-(&E)
pport_works.irq_prepar(__cn_taskleu rtl_hal (ct ie(*)(
nsignedpto t))ee80211_hprepar(__cn_taskleu rtl_hal (
nsignedpto t)pcipriINIT_WORK(&E)
pport_works.l_ucthang__work rtl_he802l_ucthang__work_s_leM Iiipr}ee80211_hintpee80211_hisc->txlrd3)esk_buff *skb)
{
	struct rtl;	a
nsignedpintpppoo, 
nsignedpintpentri}shw);
	struct rtl_
	u8 pcibridge_vendor = pcipriv->ndis_adapt(hw));
	__l_pci_priv *pcipriv = rtl_pcipriv(hw);
	strtxll_hac-	{
ci *l_hac-	{
cipriv(hw);
rtl_txl{
ci *{
cipridma_addr_G l_hac-	{
citdma, {
citdma;PC
32 nexu{
ciaddress;PCncl_i 0supp 	<.
	Dtx l_hac-p{
ciSe_venew trx flow) {
L_ASr)
pport_out_new_trx_flow)	boorl_hac-	{
ci artl_ha11_hzal.
	__pcioslunu:{
			/*Npr =,
c-;		rtizeof(*l_hac-	{
ci) *pentri}s,
c-;		r&l_hac-	{
citdmag(s
i8192S!l_hac-	{
ci || (
nsignedpto t)l_hac-	{
ci & 0xFF		boorrer.TRACE(riv *pc, COMP_ERR, DBG_EMERG,psc-->"Cann= fal.
	hw)lTX Et_s2Sppoo = %d)\n" rtlg_ ppoo);oortpe_inde-ENOMEMpri8}eisuE)
p	/*Ntxlrd3)[ppoo].l_hac-	{
ci asl_hac-	{
cipriuE)
p	/*Ntxlrd3)[ppoo].l_hac-	{
citdma asl_hac-	{
citdma;PriuE)
p	/*Ntxlrd3)[ppoo].cur>txlrpdate P	uE)
p	/*Ntxlrd3)[ppoo].cur>txlwpdate P	uE)
p	/*Ntxlrd3)[ppoo].avl	{
ci asentri}s_iit suppi	<.
	Ddma foal Pis Et_s2) {
{
ci as11_hzal.
	__pcioslunu:{
			/*Npr =,
c-;	uuuu tizeof(*{
ci) *pentri}s, &{
citdmag(s
i192S!{
ci || (
nsignedpto t){
ci & 0xFF		boorer.TRACE(riv *pc, COMP_ERR, DBG_EMERG,psc->"Cann= fal.
	hw)lTX Et_s2Sppoo = %d)\n"  ppoo);oorpe_inde-ENOMEMpri}

uE)
p	/*Ntxlrd3)[ppoo].{
ci as{
cipriE)
p	/*Ntxlrd3)[ppoo].{ma as{
citdma;PriE)
p	/*Ntxlrd3)[ppoo].idx pt0priE)
p	/*Ntxlrd3)[ppoo].entri}s asentri}s_iitkb0211_ishe dcpsc-(&E)
p	/*Ntxlrd3)[ppoo].211_ig(srter.TRACE(riv *pc, COMP_INIT, DBG_LOUD  "211_i:%d,n=t_s_addr:%p\n" rtl_=poo, {
ci);ein/
sinitpevery1 cci in  Pis Et_s2) {
192S!E)
pport_out_new_trx_flow)	boore_ve(idate  i < entri}s_ i++)	boorrnexu{
ciaddress;
	(u32){
citdma +rtlsc-> ((id+	1)	% entri}sh *rtlsc-> tizeof(*{
ci)_i&&
	E)
pport_cfst_ops->se80{
ciect  Su8 *)&{
ci[i],->su} chal 	pps,->su} chal HWi");
	TX_NEXT");
	ADDR rtlsc-chal Su8 *)&nexu{
ciaddressipri8}e t eue_indee P}ee80211_hintpee80211_hisc->rxlrd3)esk_buff *skb)
{
	struct *intprxEt_s_idxhw);
	struct rtl_
	u8 pcibridge_vendor = pcipriv->ndis_adapt(hw));
	__l_pci_priv *pcipriv = rtl_pciprincl_i 0suL_ASr)
pport_out_new_trx_flow)	boorv(hw);
	strrxll_hac-	{
ci *entry;
	NULLprir		i	<.
	Ddma foal Pis Et_s2) {
uE)
p	/*Nrxlrd3)[rxEt_s_idx].l_hac-	{
ci artl_haa11_hzal.
	__pcioslunu:{
			/*Npr =,
c-;		r tizeof(*E)
p	/*Nrxlrd3)[rxEt_s_idx].rtlsc-chl_hac-	{
ci) *rtlsc-chE)
p	/*NrxEt_scoun_,
c-;		r &{
			/*Nrxlrd3)[rxEt_s_idx].{mag(si8192S!E)
p	/*Nrxlrd3)[rxEt_s_idx].l_hac-	{
ci ||			hal (uto t)E)
p	/*Nrxlrd3)[rxEt_s_idx].l_hac-	{
ci & 0xFF		boorrer.TRACE(riv *pc, COMP_ERR, DBG_EMERG,psc-->"Cann= fal.
	hw)lRX Et_s\n")te =gmt(fc)e-ENOMEMpri8}eisu/
sinitpevery1 cci in  Pis Et_s2) {
uE)
p	/*Nrxlrd3)[rxEt_s_idx].idx21te P	ue_ve(idate  i < E)
p	/*NrxEt_scoun__ i++)	boorrentry;
	&{
			/*Nrxlrd3)[rxEt_s_idx].l_hac-	{
ci[i];_a8	192S!ee80211_hisc->one_rx{
ciect  NULL  (u8 *)entry,rtlsc-chal  prxEt_s_idx,*i))isu} ue_inde-ENOMEMpri8}ek} e;
		boortlhal =rstrrxl{
ci *entry;
	NULLprir
	ptmp_oneD
	1_ptr		i	<.
	Ddma foal Pis Et_s2) {
uE)
p	/*Nrxlrd3)[rxEt_s_idx].{
ci artl_haa11_hzal.
	__pcioslunu:{
			/*Npr =,
c-;		r tizeof(*E)
p	/*Nrxlrd3)[rxEt_s_idx].rtlsc-  {
ci) *pE)
p	/*NrxEt_scoun_,
c-;		r &{
			/*Nrxlrd3)[rxEt_s_idx].{mag(si8192S!E)
p	/*Nrxlrd3)[rxEt_s_idx].{
ci ||			hal (unsignedpto t)E)
p	/*Nrxlrd3)[rxEt_s_idx].{
ci & 0xFF		boorrer.TRACE(riv *pc, COMP_ERR, DBG_EMERG,psc-->"Cann= fal.
	hw)lRX Et_s\n")te =gmt(fc)e-ENOMEMpri8}eisu/
sinitpevery1 cci in  Pis Et_s2) {
uE)
p	/*Nrxlrd3)[rxEt_s_idx].idx21te PP	ue_ve(idate  i < E)
p	/*NrxEt_scoun__ i++)	boorrentry;
	&{
			/*Nrxlrd3)[rxEt_s_idx].{
ci[i];_a8	192S!ee80211_hisc->one_rx{
ciect  NULL  (u8 *)entry,rtlsc-chal  prxEt_s_idx,*i))isu} ue_inde-ENOMEMpri8}eoorE)
pport_cfst_ops->se80{
ciect  Su8 *)entry, pm;
	 rtlsc-> *rHWi");
	RXERO  &tmp_oneEL_A}
 ue_inde0;s}ee80211_hct ieee80211_hd/or_txlrd3)esk_buff *skb)
{
	struct rtl
nsignedpintpppoohw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	structu8 pcibridge_vendor = pcipriv->ndis_adap	v(hw);
	st			r	txlrd3) {rt_s;
	&{
			/*Ntxlrd3)[ppoo]ap
t		 e/orpevery1 cci in  Pis Et_s2) {
whtion(tkb0211_islen(&Et_st_211_i))	boor
	p*entryrfa8v(hw);
 rtl_hal(rtl_as__tkb0detruct &Et_st_211_i);o
i8192SE)
pport_out_new_trx_flow)isu}entry;
	(u8 *)(&Et_st_l_hac-	{
ci[Et_st_idx])_inie;
	isu}entry;
	(u8 *)(&Et_st_{
ci[Et_st_idx])_i i8ak;
unmap_sd3)le:{
			/*Npr =,
c-;	aE)
pport_cfst_rtlsc-> *r ops->ee80{
cie(u8 *)entry, 	pps,->su} chalHWi");
	TXBUFF_ADDR),
c-;	atkbt_.en, ID,
DMA_TODEVICE)_i		kd/or_tkbpping(sk
=t_st_idx;
	(=t_st_idx;+ 1)	% Et_st_entri}s_iit suppie/orpdma of  Pis Et_s2) {
11_hd/or__pcioslunu:{
			/*Npr =,
c-;uuu tizeof(*Et_st_{
ci) *pEt_st_entri}s,
c-;uuu Et_st_{
ci, Et_st_{mag(siEt_st_{
ci;
	NULLpriL_ASr)
pport_out_new_trx_flow)	boor11_hd/or__pcioslunu:{
			/*Npr =,
c-;;uuu tizeof(*Et_st_l_hac-	{
ci) *pEt_st_entri}s,
c-;;uuu Et_st_l_hac-	{
ci  Et_st_l_hac-	{
ci_{mag(si8Et_st_l_hac-	{
ci_asNULLprit }ee80211_hct ieee80211_hd/or_rxlrd3)esk_buff *skb)
{
	struct *intprxEt_s_idxhw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	structu8 pcibridge_vendor = pcipriv->ndis_adap	ncl_i 0supp e/orpevery1 cci in  Pis Et_s2) {
e_ve(idate  i < E)
p	/*NrxEt_scoun__ i++)	boorv(hw);
 rtl_hal(rtl_as{
			/*Nrxlrd3)[rxEt_s_idx].rxll_h[i](s
i8192S!ttl_pue	ot linpsc->sak;
unmap_sd3)le:{
			/*Npr =,s*((dma_addr_G *)tkbt_cb),
c-;	aE)
p	/*Nrxl_hac-tize, ID,
DMA_FROMDEVICE)_i		kd/or_tkbpping(skt suppie/orpdma of  Pis Et_s2) {
L_ASr)
pport_out_new_trx_flow)	boor11_hd/or__pcioslunu:{
			/*Npr =,
c-;;uuu tizeof(*E)
p	/*Nrxlrd3)[rxEt_s_idx].rtlscuuu l_hac-	{
ci) *pE)
p	/*NrxEt_scoun_,
c-;	*r s{
			/*Nrxlrd3)[rxEt_s_idx].l_hac-	{
ci rtl;	*r s{
			/*Nrxlrd3)[rxEt_s_idx].{mag(si8E)
p	/*Nrxlrd3)[rxEt_s_idx].l_hac-	{
ci asNULLprit e;
		boor11_hd/or__pcioslunu:{
			/*Npr =,
c-;;uuu tizeof(*E)
p	/*Nrxlrd3)[rxEt_s_idx].{
ci) *rtlsc>suu{
			/*NrxEt_scoun_ rtl;	*r s{
			/*Nrxlrd3)[rxEt_s_idx].{
ci rtl;	*r s{
			/*Nrxlrd3)[rxEt_s_idx].{mag(si8E)
p	/*Nrxlrd3)[rxEt_s_idx].{
ci_asNULLprit }ee80211_hintpee80211_hisc->trxlrd3)esk_buff *skb)
{
	structhw);
	struct rtl_
	u8 pcibridge_vendor = pcipriv->ndis_adaptintprI_;poncl_i,prxEt_s_idx 0supp rxEt_s_idx 0:RX_MPDUTYPE_R
LPS_rxEt_s_idx 1:RX_CMD}

/* 
	 uppoe_ve(rxEt_s_idx pte  rxEt_s_idx < eron to MAX RX_80211p rxEt_s_idx++)	boorrI_D
	ee80211_hisc->rxlrd3)ect *rxEt_s_idxh(si8192SrI_q[queue_indprI_;po}pdae_ve(idate  i < eron to MAX TXi80211_COUNT_ i++)	boorrI_D
	ee80211_hisc->txlrd3)ect *i,
c-;	aE)
p	/*NtxEt_scoun_[i]h(si8192SrI_q[quego 51errhd/or_rt_ss_iit suue_indee Pderrhd/or_rt_ss:poe_ve(rxEt_s_idx pte  rxEt_s_idx < eron to MAX RX_80211p rxEt_s_idx++)
		ee80211_hd/or_rxlrd3)ect *rxEt_s_idxh(sdae_ve(idate  i < eron to MAX TXi80211_COUNT_ i++)->u192SE)
p	/*Ntxlrd3)[i].{
ci ||			hal E)
p	/*Ntxlrd3)[i].l_hac-	{
ci)[queee80211_hd/or_txlrd3)ect *i)_idate PCI 1 P}ee80211_hintpee80211_hdeisc->trxlrd3)esk_buff *skb)
{
	structhw);

32 n,prxEt_s_idx 0suppe/orprx Et_ss uppoe_ve(rxEt_s_idx pte  rxEt_s_idx < eron to MAX RX_80211p rxEt_s_idx++)
		ee80211_hd/or_rxlrd3)ect *rxEt_s_idxh(sdappe/orptx Et_ss uppoe_ve(idate  i < eron to MAX TXi80211_COUNT_ i++)->uee80211_hd/or_txlrd3)ect *i)_idate PCI 0;s}eeintpr80211_hrese80trxlrd3)esk_buff *skb)
{
	structhw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	structu8 pcibridge_vendor = pcipriv->ndis_adap	ncl_i,prxEt_s_idx 0n
nsignedpto thflagsap	u	ptmp_oneD
	1_ptu32 l_hac-address;PCpp rxEt_s_idx 0:RX_MPDUTYPE_R uppo/* rxEt_s_idx 1:RX_CMD}

/*  uppoe_ve(rxEt_s_idx pte  rxEt_s_idx < eron to MAX RX_80211p rxEt_s_idx++)	boor		 e_vce r
 *rxlrd3)[RX_MPDUTYPE_R {
uPS_RX_CMD}

/* ].idx2 51 hedfirst1one{
uPSnew trx flow,nd51nothcon{
ust_a8192S!E)
pport_out_new_trx_flowDO
			_halE)
p	/*Nrxlrd3)[rxEt_s_idx].{
ci) boorrtlhal =rstrrxl{
ci *entry;
	NULLpr&&
	E)
p	/*Nrxlrd3)[rxEt_s_idx].idx21te P	uue_ve(idate  i < E)
p	/*NrxEt_scoun__ i++)	boorrrentry;
	&{
			/*Nrxlrd3)[rxEt_s_idx].{
ci[i];_a8		l_hac-address_artl;	*rE)
pport_cfst_ops->ee80{
cie(u8 *)entry,rtl;	*rpm;
	 ,rHWi");
	RXBUFF_ADDRg(sk
		meafau((u8 *)entry_, 0 ,
c-;	uuuu u tizeof(*E)
p	/*Nrxlrd3)
c-;	uuuu u [rxEt_s_idx].{
ci));/*clearPoneDentryst_a8	gL_ASE)
pport_out_new_trx_flow)	boortorE)
pport_cfst_ops->se80{
ciect rtlsc-> *rSu8 *)entry, pm;
	 rtlsc-> *rHWi");
	RX_PREPARE rtlsc-> *rSu8 *)&l_hac-address)rfm	
	} e;
		boorttrE)
pport_cfst_ops->se80{
ciect rtlsc-> *rSu8 *)entry, pm;
	 rtlsc-> *rHWi");
	RXBUFF_ADDR rtlsc-> *rSu8 *)&l_hac-address)rfmrttrE)
pport_cfst_ops->se80{
ciect rtlsc-> *rSu8 *)entry, pm;
	 rtlsc-> *rHWi");
	RXPKTeg_N rtlsc-> *rSu8 *)&{
			/*Nrxl_hac-tize)TL8rttrE)
pport_cfst_ops->se80{
ciect rtlsc-> *rSu8 *)entry, pm;
	 rtlsc-> *rHWi");
	RXOWN rtlsc-> *rSu8 *)&tmp_oneEL_A	. }erm }ermrE)
pport_cfst_ops->se80{
ciect  Su8 *)entry, pm;
	 rtlsc-> *rHWi");
	RXERO  Su8 *)&tmp_oneEL_A
}->uE)
p	/*Nrxlrd3)[rxEt_s_idx].idx21te P	}pdaReq
 *afsc-orese8,erIlea
		previous pendt_s2rackI_,
	 u hale_vce r
 *ptx idx2 51 hedfirst1one{
 uppospin_.
		_trqsave(&E)
pport_.
		s.irq_th_.
		, flags);
	e_ve(idate  i < eron to MAX TXi80211_COUNT_ i++)	boor192SE)
p	/*Ntxlrd3)[i].{
ci ||			hal E)
p	/*Ntxlrd3)[i].l_hac-	{
ci) boorrtlhal =rst			r	txlrd3) {rt_s;
	&{
			/*Ntxlrd3)[i](s
i8
whtion(tkb0211_islen(&Et_st_211_i))	booror
	p*entryrfa8orv(hw);
 rtl_hal(rtl_artlsc-__tkb0detruct &Et_st_211_i);oorrt192SE)
pport_out_new_trx_flow)isu}	}entry;
	(u8 *)(&Et_st_l_hac-	{
ci->su} c		[Et_st_idx])_ini8ie;
	isu}	}entry;
	(u8 *)(&Et_st_{
ci[Et_st_idx])_i i8i8ak;
unmap_sd3)le:{
			/*Npr =,
c-;	;	aE)
pport_cfst_ops->->su} c	see80{
cie(u8 *)->su} c	sentry,rtlsc-c	 	pps,->su} c	lHWi");
	TXBUFF_ADDR),
c-;	;	atkbt_.en, ID,
DMA_TODEVICE)_i		rtr =_kd/or_tkb_irqpping(sk
		=t_st_idx;
	(=t_st_idx;+ 1)	% Et_st_entri}s_iim }ermrEt_st_idx;
	0pri8}e t espin_un.
		_irqmtstode(&E)
pport_.
		s.irq_th_.
		, flags);

 ue_inde0;s}ee80211_h_backr80211_htxlchk0waitqhisser;esk_buff *skb)
{
	struct rtl	oriv *rtlpriv = rtlswasuswa rtl	oriv *rtl rtl_hal(rtlhw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	strswalt fo *swalentry;
	NULLpri
	ptidua t_queue_t_apping(skeex = skb_get_queue_mapping(s
i192S!tl2)isuue_indepm;
			bswalentry;
	(v(hw);
	strswalt fo *)swat_{rv= rtlprda192S!E)
pport_c = rt.earlymodulnitialpe == HARDepm;
			bHIGH_QUEUE;

	return ac_to_hwqe == HARDepm;
			bHIGH_QUEUE;

	retqoeturn ac_to_hwqe == HARDepm;
			bHIGH_QUEUE;

	retpspollo_hwqe == HARDepm;
			bHIGHswalentry*Ntids[tid].agg.agg_ng(se != eronAGG_OPERAaticALqe == HARDepm;
			bHIGHee802mdependent11_i(ct  ping >  O8 _rtl)e == HARDepm;
			bHIGHtidp> 7)&&
= HARDepm;
			po/* maybrpevery1tidpneral Pbrpchecr(RduppoHIGH!E)
pport_.inklt fo.hig
 r_busytxtraffic[tid])&&
= HARDepm;
			pospin_.
		_bh(&E)
pport_.
		s.waitq_.
		e_intkb0211_isopy  &E)
pport_macb)
{
.tkb0waitq[tid], ping(skspin_un.
		_bh(&E)
pport_.
		s.waitq_.
		e_idate PCI 	ppsc-gs*80211_hintpr80211_htxesk_buff *skb)
{
	struct rtluuu u tv *rtlpriv = rtlswasuswa rtluuu u tv *rtl rtl_hal(rtl,->suuu u tv *rtlrrtltcb
{
ci *ptcb
{
ci)w);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	strswalt fo *swalentry;
	NULLpriv(hw);
 *skb)
{
	txlt fo *t fo = IEEEb)
{
	SKB_CBpping(sk(hw));
	__			r	txlrd3) {rt_s;riv(hw);
rtl_txl{
ci *p{
cipriv(hw);
rtl_txll_hac-	{
ci *ptxlld	{
ci asNULLpri
= sidx 0n
	p(ie211_ida ee802mdependent11_i(ct  ping 0n
nsignedpto thflagsap	v(hw);
 *skb)
{
	_dru*_dr_ast_queue__drpping(skeex = skb_get_queue_mapping(sr
	p*p*h_addr pphdr->a	dr1priv(hw);
	structu8 pcibridge_vendor = pcipriv->ndis_adap	/*ssnduppo
	ptidua 0pri
= sseqtnumbc- pp0;PC
	pown;PC
	ptemp_oneD
	1_p	bHIGH_QUEUE;

	retn MGo_hwqe ==tl_txln MG_procect  rtl); suL_ASr)
pport_M)
.nwtps_l |= (Ru	boorL_AS_QUEUE;

	ret*hwae_hw O
 !_QUEUE;

	return ac_to_hwh)
			 !_QUEUE;

	sas_pmo_hwq			 hdr->framu_ot lrolo|=lcpupendx = (IEEEb)
{
	FCTL_PMg(skt su=tl_acui80_procect  rtl, 	pps)tee L_AS_ (mtruicast_er
 r_addr(p*h_addrwqe ==tlpport_tl21s.txbytesmtruicast_+=atkbt_.enrfpe;
		192Sf (iroadcast_er
 r_addr(p*h_addrwqe ==tlpport_tl21s.txbytesiroadcast_+=atkbt_.enrfpe;
	e ==tlpport_tl21s.txbytesunicast_+=atkbt_.enrfpospin_.
		_trqsave(&E)
pport_.
		s.irq_th_.
		, flags);
	rt_s;
	&{
			/*Ntxlrd3)[(ie211_i];oo192S(ie211_id!= || ieee80211)	boor192SE)
pport_out_new_trx_flow)isu}idx;
	Et_st_cur>txlwp_inie;
	isu}idx;
	(=t_st_idx;+ tkb0211_islen(&Et_st_211_i))	%
c-;uuu   Et_st_entri}s_iit e;
		booridx21te P	}pdap{
ci as&{t_st_{
ci[idx];_aL_ASr)
pport_out_new_trx_flow)	boor1txlld	{
ci as&Et_st_l_hac-	{
ci[idx];_at e;
		boorown;
	(u8)rE)
pport_cfst_ops->ee80{
cie(u8 *)p{
ci,->su}	pps,rHWi");
	OWN);o
i8192S(own;
= 1)	O
 S(ie211_id!= || ieee80211)		boorrer.TRACE(riv *pc, COMP_ERR, DBG_WARNING,
c-;	a"Nodved aTX1 cci@%d,n=t_st_idx;
	%d,nidx;
	%d,ntkb0211_islend1_0x%x\n" rtlg_ (ie211_i, Et_st_idx, idx,
c-;	atkb0211_islen(&Et_st_211_i)h_i&&
	spin_un.
		_irqmtstode(&E)
pport_.
		s.irq_th_.
		,rtlsc-> *rrrrflags);
	&
= HARDetkbt_.enrfps}e t suL_ASr)
pport_cfst_ops->ee80lvailtial
{
ci O
		hal E)
pport_cfst_ops->ee80lvailtial
{
ciect  _ie211_igd=_g0)	boorrer.TRACE(riv *pc, COMP_ERR, DBG_WARNING,
c-;	a"ee80lvailtial
{
ci fpy \n")te =gspin_un.
		_irqmtstode(&E)
pport_.
		s.irq_th_.
		,rtlsc-> *rrrrflags);
	&
= HARDetkbt_.enrfp}p	bHIGH_QUEUE;

	ret*hwatqoee_hwq	boort_ada t_queue_t_apping(sk HIGHtl2) boorrtlalentry;
	(v(hw);
	strswalt fo *)swat_{rv= rtlprorrteqtnumbc- pp(x = pendcpu(_dr->teqtctrl)	O
c-;	uuuu uIEEEb)
{
	SCTL_SEQg >>	4;
t	rteqtnumbc- +
	1_p	ba8192S!_QUEUE;

	sas_ved frags(hdr->framu_ot lrol))isu} swalentry*Ntids[tid].teqtnumbc- ppteqtnumbc-rfps}e t suL_ASfQUEUE;

	ret*hwae_hwq
mrE)
pport_cfst_ops->led_ot lrolect  LED_CTL_TXtl_suE)
pport_cfst_ops->di *_txl{
ciect  _dr  Su8 *)p{
ci,->suSu8 *)ptxlld	{
ci  t fo  swa,atkb, (ie211_i, ptcb
{
ci);eindetkb0211_isopy  &Et_st_211_i  rtl); suL_ASr)
pport_out_new_trx_flow)	boorE)
pport_cfst_ops->se80{
ciect  Su8 *)p{
ci  	pps,rtlsc-> *rHWi");
	OWN, &(ie211_ig(sk} e;
		boorE)
pport_cfst_ops->se80{
ciect  Su8 *)p{
ci  	pps,rtlsc-> *rHWi");
	OWN, &temp_oneEL_A}

8192S(Et_st_entri}s - tkb0211_islen(&Et_st_211_i)h < 2 O
		hal (ie211_id!= || ieee80211)	boorer.TRACE(riv *pc, COMP_ERR, DBG_LOUD 
c-;u"less_{
ci lef Instop tkb0211_i@%d,n=t_st_idx;
	%d,nidx;
	%d,ntkb0211_islend1_0x%x\n" rtlg (ie211_i, Et_st_idx, idx,
c-;atkb0211_islen(&Et_st_211_i)h_i&&
priv = rtlswop_t11_i(ct  pineee80211_is_beaconpping);er}
 espin_un.
		_irqmtstode(&E)
pport_.
		s.irq_th_.
		, flags);

 u)
pport_cfst_ops->txlpolld3)ect *(ie211_ig(srtue_inde0;s}ee80211_hct iee80211_hdlushesk_buff *skb)
{
	struct *u32 211_isen backdrophw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	structl_pci_priv->ndidge_vendo rtl_pcipriv(hw);
	stre16 fc = rtl_get_fc(skb);_pcidev(rtl_pcipriv(hw))mac *mac =
	strmac rtl_pcidev(rtl_p
= si21te P	intp211_isidpro	struct rt			r	txlrd3) {rt_s;r
8192Smac->skip_scans_mgmt(fc);s
	e_ve(211_isid = eron to MAX TXi80211_COUNT -a1;p211_isid >1te )	boor
32 211_i_.enrfpouL_AS((211_is >>	211_isid) & 0x1gd=_g0)	boorr211_isid--;oortot linpsc->s}->uEt_s;
	&ndo rtlt_{
v.txlrd3)[211_isid];_a8211_islend1_tkb0211_islen(&Et_st_211_i)(sk HIGH211_islend1_g0 || 211_isid == || ieee80211 ||			-211_isid == TXCMD}

/* )	boorr211_isid--;oortot linpsc->s} e;
		boortmsleea(20)f i8	i++PM. }eisu/
sweejusthwast_1NSe_veall 211_is st_a8192SE)
pport_M)
.rfpwr_ng(se == ERFOFF ||			-retc(slswopSE)
		i)dEUE; >1t200q[queue_ind;rit }ee80211_hct iee80211_hdeisc-(iv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	structu8 pcibridge_vendor = pcipriv->ndis_adap
tee80211_hdeisc->trxlrd3)epcipr
	synchronize_irqp{
			/*Npr =*Nirqipritaskleucki *(&E)
pport_works.irq_taskleu)te	c*ncel_work_sync(&E)
pport_works.l_ucthang__work)c-
	elush_workt11_i(E)
pport_works.e802wqipri{
clroy_workt11_i(E)
pport_works.e802wqipr-gs*80211_hintpr80211_hisc-(iv *rtlpriv = rtl_priv(, tv *rtlp1_h e_ps_r =hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriintperrap
tee80211_hisc->v(hw);ev(, _r =hap
tude 
	ee80211_hisc->trxlrd3)epcipr8192Sude		boorer.TRACE(riv *pc, COMP_ERR, DBG_EMERG,psc->"tx Et_sfeg_rfps_l Scieefpy ed\n")te =ue_indeerrapit suue_indee Pgs*80211_hintpr80211_h802r;esk_buff *skb)
{
	structhw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	stre16 fc = rtl_get_fc(skb);_pcidev(rtl_pcipriv(hw))_
	u8 pcibridge_vendor = pcipriv->ndis_adapt(hw));
	__l_uctt6 fppsc =
	strpsckb);_pcidev(rtl_suintperrap
tr80211_hrese80trxlrd3)epcipr
	{
			/*Ndcidc-	retgod3)endun.
ad ppsc->conuL_ASr)
pport_cfst_ops->ee80btc_d021us_O
		hal E)
pport_cfst_ops->ee80btc_d021us())	boorE)
pport_btcoexist.btc_ops->btc_isc->varitialskb); rtl)_ptrE)
pport_btcoexist.btc_ops->btc_isc->c(slvarskb); rtl)_pt}
tude 
	E)
pport_cfst_ops->(ieisc-(pcipr8192Sude		boorer.TRACE(riv *pc, COMP_INIT, DBG_DMESG,psc->"Fpy ed2 51ot fig
hardeet,!\n")te =ue_indeerrapit suu)
pport_cfst_ops->nitial
iclude <l(pciprier.TRACE(riv *pc, COMP_INIT, DBG_LOUD  "nitial
iclude <l OK\n")tee r802isc->rxlot fig pcipr
	/*neral Pbrpafsc-os	ppsc- 802r;  haliclude <l nitial.gst_ase80c(slsw2r;eE)
		i)(srter.CLEAR_PS_LEVEL(ppsc,ser.RF_OFF_LEVL_HALT_NICtl_suE)
p	/*Nup_first_time_aspm;
			poer.TRACE(riv *pc, COMP_INIT, DBG_DMESG, "r80211_h802r; OK\n")tetue_inde0;s}ee80211_hct iee80211_hswopSiv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	structu8 pcibridge_vendor = pcipriv->ndis_adap	v(hw);
	str_uctt6 fppsc =
	strpsckb);_pcidev(rtl_iv(hw);
	stre16 fc = rtl_get_fc(skb);
	u8 queue_in
nsignedpto thflagsap	u8 RFInProgressTimeOut21te PP	L_ASr)
pport_cfst_ops->ee80btc_d021us(wq
mrE)
pport_btcoexist.btc_ops->btc_c(st_notify()UpdaReq
 *neral Pbrpitfed a RF_LPSliclude <l&s	ppsc-
	 u halTY; wdonst_immedi(sely.
	 uppose80c(slswopSE)
		i)pr
	{
			/*Ndcidc-	retgod3)endun.
ad pp	ppsc->E)
pport_cfst_ops-> RF_LPS
islude <l(pcipr	c*ncel_work_sync(&E)
pport_works.l_ucthang__work)c-
	spin_.
		_trqsave(&E)
pport_.
		s.rfr_uc.
		, flags);
	whtion(ppsc->rfthang__inprogress)	boorvpin_un.
		_irqmtstode(&E)
pport_.
		s.rfr_uc.
		, flags);
		L_ASRFInProgressTimeOut2>g100) boorrtpin_.
		_trqsave(&E)
pport_.
		s.rfr_uc.
		, flags);
			breakc->s}->umdelay(1);
		RFInProgressTimeOut++PM. tpin_.
		_trqsave(&E)
pport_.
		s.rfr_uc.
		, flags);
	}->ppsc->rfthang__inprogress pp	ppsc->vpin_un.
		_irqmtstode(&E)
pport_.
		s.rfr_uc.
		, flags);
->E)
pport_cfst_ops->(ie RF_LPS(pcipr	pp 8omudthcons_et, n= fne(Redpif firmeet, n= favailtialduppoHIGH!E)
pport_max_fw_tizes_mgmt(fc);suE)
pport_cfst_ops->led_ot lrolect  LED_CTL_POWER_OFF)c-
	spin_.
		_trqsave(&E)
pport_.
		s.rfr_uc.
		, flags);
	ppsc->rfthang__inprogress pppm;
			bspin_un.
		_irqmtstode(&E)
pport_.
		s.rfr_uc.
		, flags);
->E)
211_hnitial
aspm(pcipr}ee80211_h backee80211_hdind>s	ppsc-(tv *rtlp1_h e_ps_r = rtltlhal =priv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	structl_pci_priv->ndidge_vendo rtl_pcipriv(hw);
	stre16 fc = rtl_get_fc(skb);_pcidev(rtl_pcipriv(p1_h e_psbridg__pr =iprpr =t_l_s->selfl_p
= svenderidpro
X] {
viceidpro
8 reviscieidpro
X] irqlin			b
	ptmp;
->ndo rtlt_n RF>s	ppsc-.ndobridg__vendor;
	 to BRIDGE_VENDOR_UNKNOWN		bvenderidiprpr =t_vendorpri{
viceidiprpr =t_{
vice;{
11_hne d_ot fig_byte:pr =,s0x8, &Eeviscieid);{
11_hne d_ot fig_word:pr =,s0x3C  &nrqlin	)UpdaRe	 to ID 0x10ec:0x8192 occurNSe_veboth ))
			rE, whtchhouts
LPS_r			reendo,  hal))
			ret, whtchhouts  Pis dcidc-._If the
LPS_reviscie ID is eron to REVISION_ID_			r toE (0x01), then
		 *ofewcorreiv(dcidc- is r			reendo, thus  Pis routin	pneral 
LPS_reHARDepm;
	.
	 uppoHIGH{
viceidip= eron to 			ret_DID O
		hal Eeviscieidip= eron to REVISION_ID_			r toE)&&
= HARDepm;
			poHIGH{
viceidip= eron to 			r_DID ||
	uuuu{
viceidip= eron to 0044_DID ||
	uuuu{
viceidip= eron to 0047_DID ||
	uuuu{
viceidip= eron to 			ret_DID ||
	uuuu{
viceidip= eron to 		74_DID ||
	uuuu{
viceidip= eron to 		73_DID ||
	uuuu{
viceidip= eron to 		7r_DID ||
	uuuu{
viceidip= eron to 		71_DID)	boorvwitchh(Eeviscieid)	boorca
		eron to REVISION_ID_			r toE:oorrer.TRACE(riv *pc, COMP_INIT, DBG_DMESG,psc-->"8192  to-EaENSe_und -avid/did=%x/%x\n" rtlg_ venderid,u{
viceid);
	&
=)
		if (ieee8021_is_nullfunc(fc))
			rE;
	&
= HARDepm;
			b	ca
		eron to REVISION_ID_			rSE:oorrer.TRACE(riv *pc, COMP_INIT, DBG_DMESG,psc-->"8192SEaENSe_und -avid/did=%x/%x\n" rtlg_ venderid,u{
viceid);
	&
=)
		if (ieee8021_is_nullfunc(fc))
			rSE;
			breakc->spdate_d:oorrer.TRACE(riv *pc, COMP_ERR, DBG_WARNING,
c-;	a"Err: Unknown;{
vice -avid/did=%x/%x\n" rtlg_ venderid,u{
viceid);
	&
=)
		if (ieee8021_is_nullfunc(fc))
			rSE;
			breakc-
i8}ek} e;
		HIGH{
viceidip= eron to 	723At_DID)	boor=)
		if (ieee8021_is_nullfunc(fc))
	723At;oorer.TRACE(riv *pc, COMP_INIT, DBG_DMESG,psc->"	723At  to-EaENSe_und -a"psc->"vid/did=%x/%x\n"  venderid,u{
viceid);
	} e;
		HIGH{
viceidip= eron to 			rCET_DID ||
		uuu{
viceidip= eron to 			rCE_DID ||
		uuu{
viceidip= eron to 			1CE_DID ||
		uuu{
viceidip= eron to 		88Ct_DID)	boor=)
		if (ieee8021_is_nullfunc(fc))
			rCE;oorer.TRACE(riv *pc, COMP_INIT, DBG_DMESG,psc->"			rC  to-EaENSe_und -avid/did=%x/%x\n" rtlg venderid,u{
viceid);
	} e;
		HIGH{
viceidip= eron to 			rDE_DID ||
		uuu{
viceidip= eron to 			rDE_DID2)	boor=)
		if (ieee8021_is_nullfunc(fc))
			rDE;oorer.TRACE(riv *pc, COMP_INIT, DBG_DMESG,psc->"			rD  to-EaENSe_und -avid/did=%x/%x\n" rtlg venderid,u{
viceid);
	} e;
		HIGH{
viceidip= eron to 		88EE_DID)	boor=)
		if (ieee8021_is_nullfunc(fc))
		88EE;oorer.TRACE(riv *pc, COMP_INIT, DBG_LOUD 
c-;u"Findos	ppsc-, Hardeet, ee802ENS		88EE\n")te t e;
		HIGH{
viceidip= eron to 	723BE_DID)	boorr=)
		if (ieee8021_is_nullfunc(fc))
	723BE;
			er.TRACE(riv *pc, COMP_INIT , DBG_LOUD 
c-;;u"Findos	ppsc-, Hardeet, ee802ENS	723BE\n")te t e;
		HIGH{
viceidip= eron to 			rEt_DID)	boorr=)
		if (ieee8021_is_nullfunc(fc))
			rEt;
			er.TRACE(riv *pc, COMP_INIT , DBG_LOUD 
c-;;u"Findos	ppsc-, Hardeet, ee802ENS			rEt\n")te t e;
		HIGH{
viceidip= eron to 	821At_DID)	boorr=)
		if (ieee8021_is_nullfunc(fc))
	821At;
			er.TRACE(riv *pc, COMP_INIT , DBG_LOUD 
c-;;u"Findos	ppsc-, Hardeet, ee802ENS	821At\n")te t e;
		HIGH{
viceidip= eron to 	812At_DID)	boorr=)
		if (ieee8021_is_nullfunc(fc))
	812At;
			er.TRACE(riv *pc, COMP_INIT , DBG_LOUD 
c-;;u"Findos	ppsc-, Hardeet, ee802ENS	812At\n")te t e;
		boorer.TRACE(riv *pc, COMP_ERR, DBG_WARNING,
c-; "Err: Unknown;{
vice -avid/did=%x/%x\n" rtlg venderid,u{
viceid);
oor=)
		if (ieee8021_eronDEFAULT_is_nullfunc(f;e t suL_ASr)
		if (ieee80211_is_nullfunc(fc))
			rD1)	boor192SEeviscieidip= 0 || Eeviscieidip= 1)	boorr192SEeviscieidip= 0)	boororer.TRACE(riv *pc, COMP_INIT, DBG_LOUD 
c-;;;u"Findo	rD1 MAC0\n")te =gr=)
		if isludfaceindex21te P	uut e;
		HIGHEeviscieidip= 1)	boorrrer.TRACE(riv *pc, COMP_INIT, DBG_LOUD 
c-;;;u"Findo	rD1 MAC1\n")te =gr=)
		if isludfaceindex21t1;oort}erm} e;
		boorter.TRACE(riv *pc, COMP_INIT, DBG_LOUD 
c-;; "Unknown;{
vice -aVendorID/D
viceID=%x/%x, Reviscie=%x\n" rtlg_ venderid,u{
viceid, Eeviscieid);{
gr=)
		if isludfaceindex21te P	u}->}psuppo	reehoutenew trx flow uppoHIGHr)
		if (ieee80211_is_nullfunc(fc))
	1	rEturn E)
pport_out_new_trx_flowDpp	ppsc->e;
	e ==tlpport_out_new_trx_flowDpppm;
			po/*dind bus t fo */->ndo rtlt_n RF>s	ppsc-.busnumbc- pppr =t_l_s->numbc-rfpndo rtlt_n RF>s	ppsc-.r =numbc- pp to SLOT(pr =t_{
vfn)rfpndo rtlt_n RF>s	ppsc-.ac_tnumbc- pp to FUNC(pr =t_{
vfn)rfpo/*dind bridg_ t fo */->ndo rtlt_n RF>s	ppsc-.ndobridg__vendor;
	 to BRIDGE_VENDOR_UNKNOWN		bpp 8omudARM hav, n= bridg__pr =i halTY; wcrash here
LPS_sosweeneral Pchecr	HIGbridg__pr =iENSNULL
	 uppoHIGHbridg__pr =)	boor		dind bridg_ t fo HIGavailtialduppo>ndo rtlt_n RF>s	ppsc-.ndobridg__vendoridiprbridg__pr =t_vendorpri	e_ve(tmpdate ptmp <	 to BRIDGE_VENDOR_MAX ptmp++)	boorrHIGHbridg__pr =t_vendor211_ndobridg__vendors[tmp]		boorr>ndo rtlt_n RF>s	ppsc-.ndobridg__vendor;
	tmp;
orrrer.TRACE(riv *pc, COMP_INIT, DBG_DMESG,psc--; "Pci Bridg_ VendoraENSe_und index: %d\n" rtlg_		tmp)te =grbreakc->st}erm}e t suL_ASndo rtlt_n RF>s	ppsc-.ndobridg__vendor;!artl to BRIDGE_VENDOR_UNKNOWN)	boor11_ rtlt_n RF>s	ppsc-.ndobridg__busnum artl_haabridg__pr =t_l_s->numbc-rfpr11_ rtlt_n RF>s	ppsc-.ndobridg__r =num artl_haa to SLOT(bridg__pr =t_{
vfn)rfpr11_ rtlt_n RF>s	ppsc-.ndobridg__ac_tnum artl_haa to FUNC(bridg__pr =t_{
vfn)rfpr11_ rtlt_n RF>s	ppsc-.ndobridg__ndoehdr_offnI_Dartl_haa11_hndoe_capHbridg__pr =)rfpr11_ rtlt_n RF>s	ppsc-.num4bytespartl_haaSndo rtlt_n RF>s	ppsc-.ndobridg__ndoehdr_offnI_D+ 0x10) /	4;
e ==tlructlee80.inkot lrol_field pcipr
	uL_ASndo rtlt_n RF>s	ppsc-.ndobridg__vendor;=artl_haa to BRIDGE_VENDOR_AMD)	boorrndo rtlt_n RF>s	ppsc-.amd_l1_patchhartlshal E)
ructlee80amd_l1_patch(pcipri8}e t suer.TRACE(riv *pc, COMP_INIT, DBG_DMESG,psc "ndor = busnumbc-:r =numbc-:ac_tnumbc-:vendor:.inkltt6 %d:%d:%d:%x:%x\n" rtl ndo rtlt_n RF>s	ppsc-.busnumbc- rtl ndo rtlt_n RF>s	ppsc-.r =numbc- rtl ndo rtlt_n RF>s	ppsc-.ac_tnumbc- rtl nr =t_vendor, ndo rtlt_n RF>s	ppsc-..inkotrl_reg)		poer.TRACE(riv *pc, COMP_INIT, DBG_DMESG,psc "ndo_bridg_ busnumbc-:r =numbc-:ac_tnumbc-:vendor:ndoe_cap:.inkltt6_reg:amd %d:%d:%d:%x:%x:%x:%x\n" rtl ndo rtlt_n RF>s	ppsc-.ndobridg__busnum rtl ndo rtlt_n RF>s	ppsc-.ndobridg__r =num rtl ndo rtlt_n RF>s	ppsc-.ndobridg__ac_tnum rtl ndobridg__vendors[ndo rtlt_n RF>s	ppsc-.ndobridg__vendor] rtl ndo rtlt_n RF>s	ppsc-.ndobridg__ndoehdr_offnI_ rtl ndo rtlt_n RF>s	ppsc-.ndobridg__.inkotrlreg rtl ndo rtlt_n RF>s	ppsc-.amd_l1_patch);
->E)
211_hparse_ot figuu Scie:pr =,spciprilis->s	dsopy  &E)
pport_lis-,*&E)
pport_glblvart_glblppor_lis-e_idate PCI 	ppsc-gs*80211_hintpr80211_hintr_modulmsiSiv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	structl_pci_priv->ndidge_vendo rtl_pcipriv(hw);
	structu8 pcibridge_vendor = ndo rtlipriintprI_;p
rrI_D
	11_hnitial
msiS{
			/*Npr =)rfp192SrI_ <	0)&&
= HARDerI_;p
rrI_D
	retrus->irqp{
			/*Npr =*Nirq,*&ee80211_hislude <l,
c-;uuIRQF_Sis_ED, KBUILD_MODNAME,spcipri192SrI_ <	0)	boor11_h RF_LPS
msiS{
			/*Npr =)rfpeue_indprI_;po}pdaE)
p	/*Nusd3)
msiDpp	ppsc-poer.TRACE(riv *pc, COMP_INIT|COMP_INTR, DBG_DMESG,psc "MSI Iclude <l Modu!\n")tetue_inde0;s}ee80211_hintpr80211_hintr_modullegacySiv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	structl_pci_priv->ndidge_vendo rtl_pcipriv(hw);
	structu8 pcibridge_vendor = ndo rtlipriintprI_;p
rrI_D
	retrus->irqp{
			/*Npr =*Nirq,*&ee80211_hislude <l,
c-;uuIRQF_Sis_ED, KBUILD_MODNAME,spcipri192SrI_ <	0)&&
= HARDerI_;p
rr)
p	/*Nusd3)
msiDpppm;
			ber.TRACE(riv *pc, COMP_INIT|COMP_INTR, DBG_DMESG,psc "Pin-based Iclude <l Modu!\n")tetue_inde0;s}ee80211_hintpr80211_hintr_moduldedor esk_buff *skb)
{
	structhw);
	struct rtl_
	l_pci_priv->ndidge_vendo rtl_pcipriv(hw);
	structu8 pcibridge_vendor = ndo rtlipriintprI_;p
r192SE)
p	/*Nmsi_sue;
		)	boorrI_D
	r80211_hintr_modulmsiSpcipri8192SrI_ <	0)&&
rrI_D
	r80211_hintr_modullegacySpciprit e;
		boorEI_D
	r80211_hintr_modullegacySpciprit
	Ee_inderI_;p}eeintpr80211_hprobe(tv *rtlp1_h e_ps_r = rtlshal _pcit tv *rtlp1_h e_icesid *id)w);
	struct *skb)
{
	struct;
	NULLpr&&	struct rtl_pci_priv *pciprNULLpritlhal = rtl_
	l_pci_priv->ndidgNULLpritlhal = rtl_
	u8 pcibr_in
nsignedpto thpmemh802r;,hpmemh.en, pmemhflagsap	intperrap
tude 
	11_hnitial
 e_ice(pr =)rfp192Sude		boorer.ASSERT(pm;
	  "%s : Cann= fnitialenew  to  e_ice\n" rtlg 	11_hname(pr =))te =ue_indeerrapit suHIGH!11_hse80{ma_mask:pr =,sDMA_BIT_MASK(32)))	boor192S11_hse80_pcioslunu0{ma_mask:pr =,sDMA_BIT_MASK(32)))	boorrer.ASSERT(pm;
	 rtlg_  "Untialeto obopyn 32b veDMASe_ve_pcioslunufal.
	hwcies\n")te =gude 
	-ENOMEMpri8ego 51fpy 1pri8}e t su11_hse80massc-(_r =hap
tct;
	 *skb)
{
	al.
	_hw(tizeof(tlhal = rtl_
	l_pci) +rtlsctizeof(tlhal = rtl_pci),*&E)
_ops)rfp192S!hw		boorer.ASSERT(pm;
	 rtlg 	"%s :  *skb)
{
i	<.
	Dfpy ed\n",	11_hname(pr =))te =ude 
	-ENOMEMpri8go 51fpy 1prit suSET_IEEEb)
{
	DEVect  &pr =t_{
v);{
11_hse80{rv*hwaepr =,spciprsuE)
ppor pphw*NprtlproE)
pport_(i pphwc->riv->ndidg(ct ie*)E)
pport_prtlprondo rtlt_{
v.pr =iprpr =pr	isc->compleScie:&E)
pport_firmeet,_.
add3)
compleSeipr	ppproximotydinitphereuppoE)
pport_proximoty.proxim_onDpppm;
			poriv->ndidg(ct ie*)E)
pport_prtlprondo rtlt_{
v.pr =iprpr =prin/
sinitpcfsd&hintf_ops uppoE)
pport_r)
		i.isludface = INTFn to;suE)
pport_cfs;
	(v(hw);
	strc(slcfs;*)(id*Ndcidc-	*hwa);suE)
pport_intf_ops 
	&{
	l_
	lops;suE)
pport_glblvar 
	&{
	lglob(slvarUpdaReq
 *initpdbgp flagspitfed a	<.q
 *or
 r ac_twcies, becaout weeTY; q
 *out st_in or
 r ac_wcies  BEAq
 *er.TRACE/er.PRINT/eron RINT_DATAq
 *you can n= fout r
 ut _bcroq
 *befed a Pis
	 uppoE)
_dbgphflageisc-(pciprin/
sMEM _bp uppoude 
	11_hretrus->regciesepr =,sKBUILD_MODNAME)rfp192Sude		boorer.ASSERT(pm;
	  "Can't obopyn  to resources\n")te =go 51fpy 1prit supmemh802r; 
	11_hresourcelsw2r;epr =,sE)
pport_cfst_bar_id);{
1memh.en 
	11_hresourcellen(pr =,sE)
pport_cfst_bar_id);{
1memhflagsp
	11_hresourcelflags(pr =,sE)
pport_cfst_bar_id);{
	/*neet,d mea 802r; uppoE)
pport_io.ndo_memh802r; 

>suSunsignedpto t)11_hio_bp(_r = rtlsE)
pport_cfst_bar_id,hpmemh.enipri192Sr)
pport_io.ndo_memh802r; 
= 0)	boorer.ASSERT(pm;
	  "Can't _bp  to mem\n")te =ude 
	-ENOMEMpri8go 51fpy 2;e t suer.TRACE(riv *pc, COMP_INIT, DBG_DMESG,psc "mea _beaedpspace: 802r;:_0x%08lx .en:%08lx flags:%08lx,pafsc-o_be:0x%08lx\n" rtl nmemh802r;,hpmemh.en, pmemhflags rtl r)
pport_io.ndo_memh802r;iprin/
sDRF_LPSlClk Retrus-2) {
11_hwrite_ot fig_byte:pr =,s0x81,s0ipr	pp leav, D3 modu2) {
11_hwrite_ot fig_byte:pr =,s0x44,s0ipr	11_hwrite_ot fig_byte:pr =,s0x04,s0x06ipr	11_hwrite_ot fig_byte:pr =,s0x04,s0x07iprin/
sfindos	ppsc-duppoHIGH!ee80211_hdind>s	ppsc-(pr =,spci)	boorude 
	-ENODEVpri8go 51fpy 3;->}psuppoInitpIO handler uppoee80211_hio_handlercpsc-(&pr =t_{
v,spciprsu/* BEA ne d eepromi halso on uppoE)
pport_cfst_ops->ne d_eepromlt fo(pciprinL_ASr)
pport_cfst_ops->isc->vwlvarskpci)	boorer.TRACE(riv *pc, COMP_ERR, DBG_EMERG, "Can't isc->vwlvars\n")te =ude 
	-ENODEVpri8go 51fpy 3;->}poE)
pport_cfst_ops->isc->vwl eds(pciprin/
aspm uppoE)
_p1_hisc->aspm(pciprsuppoInitp_bcb)
{
 sw uppoude 
	r802isc->code(pcipr8192Sude		boorer.TRACE(riv *pc, COMP_ERR, DBG_EMERG,psc->"Can't al.
	hw)lsw e_ve_bcb)
{
\n")te =go 51fpy 3;->}psuppoInitp to sw uppoude 
	r802p1_hisc-ev(, _r =hap8192Sude		boorer.TRACE(riv *pc, COMP_ERR, DBG_EMERG,>"Fpy ed2 51initp to\n")te =go 51fpy 3;->}psuude 
	 *skb)
{
	regcssc-_hw(pcipr8192Sude		boorer.TRACE(riv *pc, COMP_ERR, DBG_EMERG,psc->"Can't regcssc-p_bcb)
{
 hw.\n")te =ude 
	-ENODEVpri8go 51fpy 3;->}poE)
pport_macb)
{
.macb)
{
	regcssc-edD
	1_p	bude 
	sysfs_cne te_group(&pr =t_{
v.kobj,*&E)
_attribute_groupipr8192Sude		boorer.TRACE(riv *pc, COMP_ERR, DBG_EMERG,psc->"fpy ed2 51one te	sysfs;{
vice attributes\n")te =go 51fpy 3;->}psuppinitprfki * uppoE)
_isc->rfki *(pcipuppoInitp to sw upp
rr)
p	/idge_vendor = ndo rtlipriude 
	r802p1_histr_moduldedor epcipr8192Sude		boorer.TRACE(riv *pc, COMP_INIT, DBG_DMESG,psc->"%s: fpy ed2 51regcssc-pIRQ handler\n" rtlg wiphyhname(hw*Nwiphy))te =go 51fpy 3;->}prr)
p	/*Nirq_	<.
	D
	1_p	bse80b v(eronSTATUS_INTERFACEnSTART,*&E)
pport_d021us)tetue_inde0;s
fpy 3:{
11_hse80{rv*hwaepr =,sNULL);suE)
hdeisc->code(pciprri192Sr)
pport_io.ndo_memh802r