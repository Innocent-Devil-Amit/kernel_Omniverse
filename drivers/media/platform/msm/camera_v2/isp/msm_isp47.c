/* Copyright (c) 2013-2017, The Linux Foundation. All rights reserved.
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
#include <linux/ratelimit.h>


#include "msm_isp_util.h"
#include "msm_isp_axi_util.h"
#include "msm_isp_stats_util.h"
#include "msm_isp.h"
#include "msm.h"
#include "msm_camera_io_util.h"
#include "cam_hw_ops.h"
#include "msm_isp47.h"
#include "cam_soc_api.h"
#include "msm_isp48.h"

#undef CDBG
#define CDBG(fmt, args...) pr_debug(fmt, ##args)

#define VFE47_8996V1_VERSION   0x70000000

#define VFE47_BURST_LEN 3
#define VFE47_FETCH_BURST_LEN 3
#define VFE47_STATS_BURST_LEN 3
#define VFE47_UB_SIZE_VFE0 2048
#define VFE47_UB_SIZE_VFE1 1536
#define VFE47_UB_STATS_SIZE 144
#define MSM_ISP47_TOTAL_IMAGE_UB_VFE0 (VFE47_UB_SIZE_VFE0 - VFE47_UB_STATS_SIZE)
#define MSM_ISP47_TOTAL_IMAGE_UB_VFE1 (VFE47_UB_SIZE_VFE1 - VFE47_UB_STATS_SIZE)
#define VFE47_WM_BASE(idx) (0xA0 + 0x2C * idx)
#define VFE47_RDI_BASE(idx) (0x46C + 0x4 * idx)
#define VFE47_XBAR_BASE(idx) (0x90 + 0x4 * (idx / 2))
#define VFE47_XBAR_SHIFT(idx) ((idx%2) ? 16 : 0)
/*add ping MAX and Pong MAX*/
#define VFE47_PING_PONG_BASE(wm, ping_pong) \
	(VFE47_WM_BASE(wm) + 0x4 * (1 + (((~ping_pong) & 0x1) * 2)))
#define SHIFT_BF_SCALE_BIT 1

#define VFE47_BUS_RD_CGC_OVERRIDE_BIT 16

#define VFE47_VBIF_CLK_OFFSET    0x4

#ifdef CONFIG_MSM_CAMER147_X_XBAR_BA2cess_stats_irq = msm_i: 0)
/*add ping MAX 
#define VFE47_WM_BASE(idx) (0xA0 + 0x2C * idx)
#define VFs i:US_RD_CGC_OVERRIDE_BIT 1eOVERRIDE_BIT 1eOVERRIDEMSM_ISP_STATS_BF |0x46C + 0x4 * idx)
#define VFE47_XBAR_BASE(idx)_XBE47EQU0x4SBASET ERRIDE_BIT 1eOVNUx)_X 0xAelsedefine VFE47_XBAR_BASE(idx)_XBE47_XBADEFAULTERRIDE_BIT 1eOVNUx)_X 7xAen wi

#def_isuint32_t #definbase_VERr[] = {
	pinD4, /* HD + Elude	pi254, /* ZE_AWB_BG)lude	pi214, /* ZFlude	pi1F4, /* HD + HISTlude	pi294, /* RSlude	pi2B4, /* CSlude	pi2D4, /* IHISTlude	pi274, /* ZHISTl(SKIN+ HIST)lude	pi234, /* AEC_BGlude};

#def_isuint8_t #defin_i: )
/*_offset_map[] = {
	 8, /* HD + Elude	12, /* ZE_AWB_BG)lude	10, /* ZFlude	 9, /* HD + HISTlude	14, /* RSlude	15, /* CSlude	16, /* IHISTlude	13, /* ZHISTl(SKIN+ HIST)lude	11, /* AEC_BGlude};

#def_isuint8_t #definirq_map_comp_mask[] = {
	16, /* HD + Elude	17, /* ZE_AWB_BG)lude	18, /* ZFlEARLY DONE/ ZFlude	19, /* HD + HISTlude	20, /* RSlude	21, /* CSlude	22, /* IHISTlude	23, /* ZHISTl(SKIN+ HIST)lude	15, /* AEC_BGlude};
7_UB_STATS_SIZE)
#def(((~ping_p#definbase_VERr[pin]fine SHIFT_BF_SCE)
#deSET    0x4

#ifpinf CONFIG_MSM_CAMER147_XE)
#def(((~ping_s_stats_CAME~(CONFIG_MS >>_p#defin_i: )
/*_offset_map[pin]fadd ping MAX VERRIDE_BIT 1eOVSRCDE_BIDTSI_IDX 5ERRIDE_BI"
#DidxTO_IDX(h6

leg_ph6

ledd piFF)

#def_isPubuct TATSbus_vectors ef CDBG
init_vectors[] = {
	{
		.src = idx)
#deMASTERXBAR,
		.dst = idx)
#deSLAVE_EBI_CH0,
		.ab  = 0,
		.ib  = 0,
	},e};

/See tDurDE_B mon no47_request mensab/ib bus b6

width whichl be s neednclto successcluof
enabledbus clocksclude#def_isPubuct TATSbus_vectors ef CDBG
CONFIvectors[] = {
	{
		.src = idx)
#deMASTERXBAR,
		.dst = idx)
#deSLAVE_EBI_CH0,
		.ab  = idx) (0_MIN+AB,
		.ib  = idx) (0_MIN+IB,
	},e};

#def_isPubuct TATSbus_vectors ef CDBG
CoNFIvectors[] = {
	{
		.src = idx)
#deMASTERXBAR,
		.dst = idx)
#deSLAVE_EBI_CH0,
		.ab  = 0,
		.ib  = 0,
	},e};

#def_isPubuct TATSbus_paths ef CDBG
bus_c#innt_config[] = {
	{
		ncluY2) ? (ef CDBG
init_vectors),
		ef CDBG
init_vectors,
	},e	{
		ncluY2) ? (ef CDBG
CONFIvectors),
		ef CDBG
CONFIvectors,
	},e	{
		ncluY2) ? (ef CDBG
CoNFIvectors),
		ef CDBG
CoNFIvectors,
	},e};

#def_isPubuct TATSbus_scale_pURPa ef CDBG
bus_c#innt_pURPa = {
	ef CDBG
bus_c#innt_config,
	ncluY2) ? (ef CDBG
bus_c#innt_config),
	.n6V1 = 47_8996V1_VERsp",e};

uint32_t 7_89vfeeOVub_re*_offset(Pubuct vfee Vv.h" *vfee Vv,cenils CDM_BA{
	return ER147_X_XBAR_BA2c_ping_s_st18);
}

uint32_t 7_89vfeeOVget_ub_size(Pubuct vfee Vv.h" *vfee VvBA{
	if (vfee Vv->p Vv->id ==  (0_ 0x4)
		return C * idx)
#define VFE47_RDI_BA;
	return C * idx)
#define VFE47_RDI_B1;
}

void 7_89vfeeOVconfignirq(Pubuct vfee Vv.h" *vfee Vv,
		uint32_t irq0_mask, uint32_t irq1_mask,
		enum ef CDBG
irq_ morRPOSEB morBA{
	sinuch ( morB {
	case idx) (0_IRQ_ENABLE:
		vfee Vv->irq0_mask |= irq0_mask;
		vfee Vv->irq1_mask |= irq1_mask;
		break;
	case idx) (0_IRQ_DISABLE:
		vfee Vv->irq0_mask &= ~irq0_mask;
		vfee Vv->irq1_mask &= ~irq1_mask;
		break;
	case idx) (0_IRQ_eOV:
		vfee Vv->irq0_mask = irq0_mask;
		vfee Vv->irq1_mask = irq1_mask;
		break;
	}
	ef C96V1_VERSIw_mb(vfee Vv->irq0_mask,
				vfee Vv->vfeebase_s_st5C);
	ef C96V1_VERSIw_mb(vfee Vv->irq1_mask,
				vfee Vv->vfeebase_s_st60);
}

#def_isint32_t 7_89vfeeOVinit_dt_parms(Pubuct vfee Vv.h" *vfee Vv,
	Pubuct TATSvfeehwVinit_parms *dt_parms, void _ERSmem *dev_memebaseBA{
	subuct  Vv.h"_no47_*of_no47;
	int32_t i = 0, rc = A;
	uint32_t *dt_settONFs = NULL, *dt_re*s = NULL, num_dt_nntries = A;

	of_no47 = vfee Vv->p Vv-> Vv.of_no47;

	rc = of_pr morty_read_u32(of_no47, dt_parms->nntries,
		&num_dt_nntries);
	if (rc < 0 || !num_dt_nntries) {
		pr_err("%s: NO QOS nntries fR PU\n", _Efunc__);
		return -EINVAL;
	} else {
		dt_settONFs = kzalloc(sizeof(uint32_tg MAnum_dt_nntries,
			GFP_KERNEL);
		if (!dt_settONFs) {
			pr_err("%s:%d No memory\n", _Efunc__, _ELINE__);
			return -ENOMEM;
		}
		dt_re*s = kzalloc(sizeof(uint32_tg MAnum_dt_nntries,
			GFP_KERNEL);
		if (!dt_re*s) {
			pr_err("%s:%d No memory\n", _Efunc__, _ELINE__);
			k Thi(dt_settONFs);
			return -ENOMEM;
		}
		rc = of_pr morty_read_u32_de "y(of_no47, dt_parms->re*s,
			dt_re*s, num_dt_nntries);
		if (rc < 0) {
			pr_err("%s: NO QOS 
#d BDGsinfo\n", _Efunc__);
			k Thi(dt_settONFs);
			k Thi(dt_re*s);
			return -EINVAL;
		} else {
			if (dt_parms->settONFs) {
				rc = of_pr morty_read_u32_de "y(of_no47,
					dt_parms->settONFs,
					dt_settONFs, num_dt_nntries);
				if (rc < 0) {
					pr_err("%s: NO QOS settONFs\n",
						_Efunc__);
					k Thi(dt_settONFs);
					k Thi(dt_re*s);
				} else {
					clud(i = 0; i < num_dt_nntries; i++) {
						ef C96V1_VERSIw(dt_settONFs[i],
							dev_memebase +
								dt_re*s[i]);
					}
					k Thi(dt_settONFs);
					k Thi(dt_re*s);
				}
			} else {
				k Thi(dt_settONFs);
				k Thi(dt_re*s);
			}
		}
	}
	return 0;
}

#def_isenum RST_ahb_c#k_vote 
#define Vget_RST_c#k_vote(
	 enum ef Cvfeeahb_c#k_vote voteBA{
	sinuch (voteB {
	case idx) (0_6C + 0x4HB_SVS_VOTE:
		return 6C x4HB_SVS_VOTE;
	case idx) (0_6C + 0x4HB_TURBO_VOTE:
		return 6C x4HB_TURBO_VOTE;
	case idx) (0_6C + 0x4HB_NOMINineVOTE:
		return 6C x4HB_NOMINineVOTE;
	case idx) (0_6C + 0x4HB_SUSPEND_VOTE:
		return 6C x4HB_SUSPEND_VOTE;
	}
	return 0;
}

#def_isenil
#define Vahb_c#k_cfg(Pubuct vfee Vv.h" *vfee Vv,
			Pubuct TATSncludhb_c#k_cfg *dhb_cfgBA{
	inilrc = A;
	enum RST_ahb_c#k_vote vote;

	vote = 
#define Vget_RST_c#k_vote(dhb_cfg->voteB;

	if (vote && vfee Vv->dhb_vote != voteB {
		rc = RST_confignahb_c#k(NULL, 0,
			( (0_ 0x4 == vfee Vv->p Vv->id ?
			6C x4HB_CLIENT_ 0x4 : 6C x4HB_CLIENT_ 0x1), voteB;
		if (rc)
			pr_err("%s: failnclto set ahb votelto %x\n",
				_Efunc__, voteB;
		elsed			vfee Vv->dhb_vote = vote;
	}
	return rc;
}

enil
#devfeeOVinit_hardTICU(Pubuct vfee Vv.h" *vfee VvBA{
	inilrc = -1;
	enum RST_ahb_c#k_c#innt id;

	if (vfee Vv->p Vv->id == 4)
		id = 6C x4HB_CLIENT_ 0xA;
	elsed		id = 6C x4HB_CLIENT_ 0x1;

	rc = vfee Vv->hwVinfo->vfeeVFE4platclumeVFE4enable_re*ulators(
								vfee Vv,c1);
	if (rc)
		goto enable_re*ulators_failnc;

	rc = vfee Vv->hwVinfo->vfeeVFE4platclumeVFE4enable_c#ks(
							vfee Vv,c1);
	if (rc)
		goto c#k_enable_failnc;

	rc = RST_confignahb_c#k(NULL, 0, id, 6C x4HB_SVS_VOTE);
	if (rc < 0) {
		pr_err("%s: failnclto vote clud4HB\n", _Efunc__);
		goto dhb_vote_fail;
	}
	vfee Vv->dhb_vote = 6C x4HB_SVS_VOTE;

	vfee Vv->common_URPa-> ualCvfeeres->vfeebase[vfee Vv->p Vv->id] =
		vfee Vv->vfeebase;

	rc = h"
#inclupURPeeba

width( (0_ 0x4 + vfee Vv->p Vv->id,
					idx) (0_MIN+AB, idx) (0_MIN+IB);
	if (rc)
		goto bw_enable_fail;

	rc = h"
#96V1_VEenable_irq(vfee Vv->vfeeirq,c1);
	if (rc < 0)
		goto irq_enable_fail;

	return rc;
irq_enable_fail:
	ef CDBG
upURPeeba

width( (0_ 0x4 + vfee Vv->p Vv->id, 0, 0);
bw_enable_fail:
	vfee Vv->common_URPa-> ualCvfeeres->vfeebase[vfee Vv->p Vv->id] = NULL;
	if (RST_confignahb_c#k(NULL, 0, id, 6C x4HB_SUSPEND_VOTE) < 0)
		pr_err("%s: failnclto remove vote clud4HB\n", _Efunc__);
	vfee Vv->dhb_vote = 6C x4HB_SUSPEND_VOTE;
dhb_vote_fail:
	vfee Vv->hwVinfo->vfeeVFE4platclumeVFE4enable_c#ks(vfee Vv,c0);
c#k_enable_failnc:
	vfee Vv->hwVinfo->vfeeVFE4platclumeVFE4enable_re*ulators(vfee Vv,c0);
enable_re*ulators_failnc:
	return rc;
}

void 7_89vfeeOVrelease_hardTICU(Pubuct vfee Vv.h" *vfee VvBA{
	enum RST_ahb_c#k_c#innt id;

	/* when closDE_Bno47, disabledall irqlude	vfee Vv->irq0_mask = 0;
	vfee Vv->irq1_mask = 0;
	vfee Vv->hwVinfo->vfeeVFE4irq_ ms.confignirq(vfee Vv,
				vfee Vv->irq0_mask, vfee Vv->irq1_mask,
				idx) (0_IRQ_eOV);
	ef C96V1_VEenable_irq(vfee Vv->vfeeirq,c0);
	tasklet_kill(&vfee Vv->vfeetasklet);
	ef CDBG
flushetasklet(vfee Vv);

	vfee Vv->common_URPa-> ualCvfeeres->vfeebase[vfee Vv->p Vv->id] = NULL;

	ef CDBG
upURPeeba

width( (0_ 0x4 + vfee Vv->p Vv->id, 0, 0);

	if (vfee Vv->p Vv->id == 4)
		id = 6C x4HB_CLIENT_ 0xA;
	elsed		id = 6C x4HB_CLIENT_ 0x1;

	if (RST_confignahb_c#k(NULL, 0, id, 6C x4HB_SUSPEND_VOTE) < 0)
		pr_err("%s: failnclto vote clud4HB\n", _Efunc__);

	vfee Vv->dhb_vote = 6C x4HB_SUSPEND_VOTE;

	vfee Vv->hwVinfo->vfeeVFE4platclumeVFE4enable_c#ks(
							vfee Vv,c0);
	vfee Vv->hwVinfo->vfeeVFE4platclumeVFE4enable_re*ulators(vfee Vv,c0);
}

void 7_89vfeeOVinit_hardTICU_re*(Pubuct vfee Vv.h" *vfee VvBA{
	Pubuct TATSvfeehwVinit_parms qos_parms;
	Pubuct TATSvfeehwVinit_parms vbif_parms;
	Pubuct TATSvfeehwVinit_parms ds_parms;

	eemset(&qos_parms, 0, sizeof(Pubuct TATSvfeehwVinit_parms));
	eemset(&vbif_parms, 0, sizeof(Pubuct TATSvfeehwVinit_parms));
	eemset(&ds_parms, 0, sizeof(Pubuct TATSvfeehwVinit_parms));

	qos_parms.nntries = "qos-nntries";
	qos_parms.re*s = "qos-re*s";
	qos_parms.settONFs = "qos-settONFs";
	vbif_parms.nntries = "vbif-nntries";
	vbif_parms.re*s = "vbif-re*s";
	vbif_parms.settONFs = "vbif-settONFs";
	ds_parms.nntries = "ds-nntries";
	ds_parms.re*s = "ds-re*s";
	ds_parms.settONFs = "ds-settONFs";

	ef CvfeeOVinit_dt_parms(vfee Vv,c&qos_parms, vfee Vv->vfeebase);
	ef CvfeeOVinit_dt_parms(vfee Vv,c&ds_parms, vfee Vv->vfeebase);
	ef CvfeeOVinit_dt_parms(vfee Vv,c&vbif_parms, vfee Vv->vfeevbif_base);


	/* 
#deCFGlude	ef C96V1_VERSIw(0x
#def101, vfee Vv->vfeebase_s_st84);
	/* IRQ_MASK/CLEARlude	ef CvfeeOVconfignirq(vfee Vv,c0x81#defE0, 0xFFFFFF7E,
				idx) (0_IRQ_ENABLE);
	ef C96V1_VERSIw(0xFFFFFFFF, vfee Vv->vfeebase_s_st64);
	ef C96V1_VERSIw_mb(0xFFFFFFFF, vfee Vv->vfeebase_s_st68);
	ef C96V1_VERSIw_mb(0x1, vfee Vv->vfeebase_s_st58);
}

void 7_89vfeeOVclear
#defus_re*(Pubuct vfee Vv.h" *vfee VvBA{
	ef CvfeeOVconfignirq(vfee Vv,c0x844
#def,c0x0,
				idx) (0_IRQ_eOV);
	ef C96V1_VERSIw(0xFFFFFFFF, vfee Vv->vfeebase_s_st64);
	ef C96V1_VERSIw_mb(0xFFFFFFFF, vfee Vv->vfeebase_s_st68);
	ef C96V1_VERSIw_mb(0x1, vfee Vv->vfeebase_s_st58);
}

void 7_89vfeeOVprocessereset_irq(Pubuct vfee Vv.h" *vfee Vv,
	uint32_t irq
#defus0, uint32_t irq
#defus1BA{
	if (irq
#defus0 &_irq<< 31)) {
		complete(&vfee Vv->reset_complete);
		vfee Vv->reset_pen wE_B= 0;
	}
}

void 7_89vfeeOVprocessehalt_irq(Pubuct vfee Vv.h" *vfee Vv,
	uint32_t irq
#defus0, uint32_t irq
#defus1BA{
	uint32_t val = A;

	if (irq
#defus1 &_irq<< 8)) {
		complete(&vfee Vv->halt_complete);
		ef C96V1_VERSIw(0x
, vfee Vv->vfeebase_s_st400);
	}

	val = ef C96V1_VERSIr(vfee Vv->vfeevbif_base_s_T 1eOVERRIDE_BIT 1eOV);
	val &= ~(0x1);
	ef C96V1_VERSIw(val, vfee Vv->vfeevbif_base_s_T 1eOVERRIDE_BIT 1eOV);
}

void 7_89vfeeOVprocesseinput_irq(Pubuct vfee Vv.h" *vfee Vv,
	uint32_t irq
#defus0, uint32_t irq
#defus1,
	Pubuct TATSDBG
time#demp *tsBA{
	if (!(irq
#defus0 &_0x1
#def3))
		return;

	if (irq
#defus0 &_irq<< 0)) {
		 (0_IZE_"vfe %d: SOF IRQ, fr6V1 id %d\n",
			vfee Vv->p Vv->id,
			vfee Vv->dxi_URPa.srcVinfo[T 1_PIX_0].fr6V1_id);
		ef CDBG
incremnnt_fr6V1_id(vfee Vv,cT 1_PIX_0, ts);
	}

	if (irq
#defus0 &_irq<< 24)) {
		 (0_IZE_"%s: Feuch EngE_BIRead IRQ\n", _Efunc__);
		ef CDBG
feuchEengE_B_don"_notify(vfee Vv,
			&vfee Vv->feuchEengE_B_info);
	}


	if (irq
#defus0 &_irq<< 1))
		 (0_IZE_"%s: EOF IRQ\n", _Efunc__);
}

void 7_89vfeeOVprocesseviolRPOSE
#defus(
	Pubuct vfee Vv.h" *vfee VvBA{
	uint32_t violRPOSE
#defus = vfee Vv->error_info.violRPOSE
#defus;

	if (violRPOSE
#defus > 39) {
		pr_err("%s: invalid violRPOSE #defus %d\n",
			_Efunc__, violRPOSE
#defus);
		return;
	}

	pr_err("%s: T 1 pipelE_BIviolRPOSE #defus %d\n", _Efunc__,
		violRPOSE
#defus);
}

void 7_89vfeeOVprocesseerror_#defus(Pubuct vfee Vv.h" *vfee VvBA{
	uint32_t error_#defus1 = vfee Vv->error_info.error_mask1;

	if (error_#defus1 &_irq<< 0)) {
		pr_err("%s: 96Vif error #defus:_0x%x\n",
			_Efunc__, vfee Vv->error_info.96Vif
#defus);
		/* dump 96Vif re*isters SE 96Vif error ude		ef C96V1_VERSIdump(vfee Vv->vfeebase_s_st478,c0x3C,c1);
		/* te#dgen ude		if (TESTGEN == vfee Vv->dxi_URPa.srcVinfo[T 1_PIX_0].input_mux)
			ef C96V1_VERSIdump(vfee Vv->vfeebase_s_stC58,c0x28,c1);
	}
	if (error_#defus1 &_irq<< 1))
		pr_err("%s: #defi bhist overwrite\n", _Efunc__);
	if (error_#defus1 &_irq<< 2))
		pr_err("%s: #defi cs overwrite\n", _Efunc__);
	if (error_#defus1 &_irq<< 3))
		pr_err("%s: #defi ihist overwrite\n", _Efunc__);
	if (error_#defus1 &_irq<< 4))
		pr_err("%s: realign buf y overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 5))
		pr_err("%s: realign buf cb overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 6))
		pr_err("%s: realign buf cr overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 7)) {
		7_89vfeeOVprocesseviolRPOSE
#defus(vfee Vv);
	}
	if (error_#defus1 &_irq<< 9))
		pr_err("%s: image master 0dbus overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 10))
		pr_err("%s: image master 1dbus overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 11))
		pr_err("%s: image master 2dbus overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 12))
		pr_err("%s: image master 3dbus overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 13))
		pr_err("%s: image master 4dbus overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 14))
		pr_err("%s: image master 5dbus overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 15))
		pr_err("%s: image master 6dbus overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 16))
		pr_err("%s: #defus hdr*/

bus overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 17))
		pr_err("%s: #defus bg
bus overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 18))
		pr_err("%s: #defus bf
bus overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 19))
		pr_err("%s: #defus hdr*/hist bus overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 20))
		pr_err("%s: #defus rs bus overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 21))
		pr_err("%s: #defui cs bus overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 22))
		pr_err("%s: #defui ihist bus overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 23))
		pr_err("%s: #defus skin*/hist bus overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 24))
		pr_err("%s: #defus aec bg
bus overflow\n", _Efunc__);
	if (error_#defus1 &_irq<< 25))
		pr_err("%s: #defus dsp error\n", _Efunc__);
}

void 7_89vfeeOVread_irq
#defus_andVclear(Pubuct vfee Vv.h" *vfee Vv,
	uint32_t *irq
#defus0, uint32_t *irq
#defus1BA{
	*irq
#defus0 = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st6C);
	*irq
#defus1 = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st70);
	/* Mask off
bifi e detICULnot enabledlude	ef C96V1_VERSIw(*irq
#defus0, vfee Vv->vfeebase_s_st64);
	ef C96V1_VERSIw(*irq
#defus1, vfee Vv->vfeebase_s_st68);
	ef C96V1_VERSIw_mb(1, vfee Vv->vfeebase_s_st58);
	*irq
#defus0 &= vfee Vv->irq0_mask;
	*irq
#defus1 &= vfee Vv->irq1_mask;

	if (*irq
#defus1 &_irq<< 0)) {
		vfee Vv->error_info.96Vif
#defus =
		ef C96V1_VERSIr(vfee Vv->vfeebase_s_st4A4);
		/* mask off
96Vif error after first occue "mh" */
		7_89vfeeOVconfignirq(vfee Vv,c0,_irq<< 0), idx) (0_IRQ_DISABLE);
	}

	if (*irq
#defus1 &_irq<< 7))
		vfee Vv->error_info.violRPOSE
#defus =
		ef C96V1_VERSIr(vfee Vv->vfeebase_s_st7C);

}

void 7_89vfeeOVread_irq
#defus(Pubuct vfee Vv.h" *vfee Vv,
	uint32_t *irq
#defus0, uint32_t *irq
#defus1BA{
	*irq
#defus0 = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st6C);
	*irq
#defus1 = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st70);
}

void 7_89vfeeOVprocessereg
upURPe(Pubuct vfee Vv.h" *vfee Vv,
	uint32_t irq
#defus0, uint32_t irq
#defus1,
	Pubuct TATSDBG
time#demp *tsBA{
	enum ef Cvfeeinput_src i;
	uint32_t shift_irq;
	uint8_t reg
upURPed = A;
	unsigned l_MS flags;

	if (!(irq
#defus0 &_0xF0))
		return;
	/* Shift #defus bifi so e detPIX SOF is 1st bit */
	shift_irq = ((irq
#defus0 &_0xF0) >>_4);

	clud(i = T 1_PIX_0; i <= T 1_RAW_2; i++) {
		if (shift_irq & BIT(i)) {
			reg
upURPed |= BIT(i);
			 (0_IZE_"%s REG_UPDATE IRQ %x\n", _Efunc__,
				(uint32_tgBIT(i));
			sinuch (i) {
			case T 1_PIX_0:
				ef CDBG
notify(vfee Vv,  (0_EVENT_REG_UPDATE,
					T 1_PIX_0, ts);
				if (atomicVread(
					&vfee Vv->#definURPa.sdefinepURPe))
					ef CDBG
sdefinPubeam
upURPe(vfee Vv);
				if (vfee Vv->dxi_URPa.96Vif
#defe ==
					CAMIF_STOPSET )
					vfee Vv->hwVinfo->vfeeVFE4coreeVFE4
						reg
upURPe(vfee Vv, i);
				break;
			case T 1_RAW_0:
			case T 1_RAW_1:
			case T 1_RAW_2:
				ef CDBG
incremnnt_fr6V1_id(vfee Vv,ci, ts);
				ef CDBG
notify(vfee Vv,  (0_EVENT_SOF,ci, ts);
				ef CDBG
upURPeefr6V1drop_re*(vfee Vv, i);
				/*
				 * Reg UpURPe is pseudo SOF cludRDI,
				 * so_request every fr6V1
				 */
				vfee Vv->hwVinfo->vfeeVFE4coreeVFE4
					reg
upURPe(vfee Vv, i);
				break;
			default:
				pr_err("%s: Error case\n", _Efunc__);
				return;
			}
			if (vfee Vv->dxi_URPa.Pubeam
upURPe[i])
				ef CDBG
dxi_Pubeam
upURPe(vfee Vv, i);
			ef CDBG
saveefr6V1drop_values(vfee Vv, i);
			if (atomicVread(&vfee Vv->dxi_URPa.dxi_cfg
upURPe[i])) {
				ef CDBG
dxi_cfg
upURPe(vfee Vv, i);
				if (atomicVread(
					&vfee Vv->dxi_URPa.dxi_cfg
upURPe[i]) ==
					0)
					ef CDBG
notify(vfee Vv,
						 (0_EVENT_STREAM_UPDATE_DONE,
						i, ts);
			}
		}
	}

	spin_lock_irqsave(&vfee Vv->reg
upURPe_lock, flags);
	if (reg
upURPed & BIT(T 1_PIX_0))
		vfee Vv->reg
upURPed = 1;

	vfee Vv->reg
upURPe_requested &= ~reg
upURPed;
	spin_unlock_irqrestore(&vfee Vv->reg
upURPe_lock, flags);
}

void 7_89vfeeOVprocesseepoch_irq(Pubuct vfee Vv.h" *vfee Vv,
	uint32_t irq
#defus0, uint32_t irq
#defus1,
	Pubuct TATSDBG
time#demp *tsBA{
	if (!(irq
#defus0 &_0xc))
		return;

	if (irq
#defus0 &_BIT(2)) {
		 (0_IZE_"%s: EPOCH0 IRQ\n", _Efunc__);
		ef CDBG
upURPeefr6V1drop_re*(vfee Vv, T 1_PIX_0);
		ef CDBG
upURPeesdefinfr6V1drop_re*(vfee Vv);
		ef CDBG
upURPeeerror_fr6V1_count(vfee Vv);
		ef CDBG
notify(vfee Vv,  (0_EVENT_SOF,cT 1_PIX_0, ts);
		if (vfee Vv->dxi_URPa.PrcVinfo[T 1_PIX_0].raw_Pubeam
count > 0
			&& vfee Vv->dxi_URPa.srcVinfo[T 1_PIX_0].
			pix_Pubeam
count == 4) {
			if (vfee Vv->dxi_URPa.Pubeam
upURPe[T 1_PIX_0])
				ef CDBG
dxi_Pubeam
upURPe(vfee Vv, T 1_PIX_0);
			vfee Vv->hwVinfo->vfeeVFE4coreeVFE4reg
upURPe(
				vfee Vv, T 1_PIX_0);
		}
	}
}

void 7_89fine Vprocesseeof_irq(Pubuct vfee Vv.h" *vfee Vv,
	uint32_t irq
#defus0BA{
	if (irq
#defus0 &_BIT(1))
		vfee Vv->dxi_URPa.srcVinfo[T 1_PIX_0].eof_id++;
}

void 7_89vfeeOVreg
upURPe(Pubuct vfee Vv.h" *vfee Vv,
	enum ef Cvfeeinput_src fr6V1_src)
{
	uint32_t upURPeemask = 0;
	unsigned l_MS flags;

	/ the
 *HW supporfi upto T 1_RAW_2 */
	if (fr6V1_src > T 1_RAW_2 && fr6V1_src != T 1_SRCDMAX) {
		pr_err("%s Error case\n", _Efunc__);
		return;
	}

	/*
	 * If fr6V1_src == T 1_SRCDMAX_request reg
upURPe SE all
	 * supporfed INTF
	 */
	if (fr6V1_src == T 1_SRCDMAX)
		upURPeemask = 0xF;
	elsed		upURPeemask = BIT((uint32_tgfr6V1_src);
	 (0_IZE_"%s upURPeemask %x\n", _Efunc__, upURPeemask);

	spin_lock_irqsave(&vfee Vv->reg
upURPe_lock, flags);
	vfee Vv->dxi_URPa.PrcVinfo[T 1_PIX_0].reg
upURPe_fr6V1_id =
		vfee Vv->dxi_URPa.srcVinfo[T 1_PIX_0].fr6V1_id;
	vfee Vv->reg
upURPe_requested |= upURPeemask;
	vfee Vv->common_URPa-> ualCvfeeres->reg
upURPe_mask[vfee Vv->p Vv->id] =
		vfee Vv->reg
upURPe_requested;
	if ((vfee Vv->iinPplit && vfee Vv->p Vv->id ==  (0_ 0x1) &&
		((fr6V1_src == T 1_PIX_0) || (fr6V1_src == T 1_SRCDMAX))) {
		if (!vfee Vv->common_URPa-> ualCvfeeres->vfeebase[ (0_ 0x4]) {
			pr_err("%s vfeebase_clud (0_ 0x4 
 *NULL\n", _Efunc__);
			spin_unlock_irqrestore(&vfee Vv->reg
upURPe_lock,
				flags);
			return;
		}
		ef C96V1_VERSIw_mb(upURPe_mask,
			vfee Vv->common_URPa-> ualCvfeeres->
			vfeebase[ (0_ 0x4]_s_st4AC);
		ef C96V1_VERSIw_mb(upURPe_mask,
			vfee Vv->vfeebase_s_st4AC);
	} else if (!vfee Vv->iinPplit ||
		((fr6V1_src == T 1_PIX_0) &&
		(vfee Vv->dxi_URPa.96Vif
#defe == CAMIF_STOPSET )) ||
		(fr6V1_src >= T 1_RAW_0 && fr6V1_src <= T 1_SRCDMAX)) {
		7_8996V1_VERSIw_mb(upURPe_mask,
			vfee Vv->vfeebase_s_st4AC);
	}
	spin_unlock_irqrestore(&vfee Vv->reg
upURPe_lock, flags);
}

l_MS 7_89vfeeOVreset_hardTICU(Pubuct vfee Vv.h" *vfee Vv,
	uint32_t first
#dert, uint32_t blockONFIcall)
{
	l_MS rc = A;
	uint32_t reset;

	init_completion(&vfee Vv->reset_complete);

	if (blockONFIcall)
		vfee Vv->reset_pen wE_B= 1;

	if (first
#dert) {
		if (ef Cvfeeis9vfee8(vfee Vv))
			reset = 0x3F7;
		elsed			reset = 0x3FF;
		ef C96V1_VERSIw_mb(reset, vfee Vv->vfeebase_s_st18);
	} else {
		if (ef Cvfeeis9vfee8(vfee Vv))
			reset = 0x3E7;
		elsed			reset = 0x3EF;
		ef C96V1_VERSIw_mb(reset, vfee Vv->vfeebase_s_st18);
		ef C96V1_VERSIw(0x7FFFFFFF, vfee Vv->vfeebase_s_st64);
		ef C96V1_VERSIw(0xFFFFFEFF, vfee Vv->vfeebase_s_st68);
		ef C96V1_VERSIw(0x1, vfee Vv->vfeebase_s_st58);
		vfee Vv->hwVinfo->vfeeVFE4dxi_VFE4
			reload_wm(vfee Vv, vfee Vv->vfeebase,c0x0001FFFF);
	}

	if (blockONFIcall) {
		rc = wait_clu_completion
timeout(
			&vfee Vv->reset_complete, 7_ecs_to_jiffies(50)B;
		if (rc <= 0) {
			pr_err("%s:%d failnc: reset timeout\n", _Efunc__,
				_ELINE__);
			vfee Vv->reset_pen wE_B= 0;
		}
	}

	return rc;
}

void 7_89vfeeOVdxi_reload_wm(Pubuct vfee Vv.h" *vfee Vv,
	void _ERSmem *vfeebase,cuint32_t reload_mask)A{
	ef C96V1_VERSIw_mb(reload_mask, vfeebase_s_st80);
}

void 7_89vfeeOVde "mpURPe_cgc_overridU(Pubuct vfee Vv.h" *vfee Vv,
	uint8_t 2c_pin,suint8_t enableBA{
	uint32_t val;

	/ tChange CGC overridUlude	val = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st3C);
	if (enableBA		val |= irq<< 2c_ping;
	elsed		val &= ~(rq<< 2c_ping;
	ef C96V1_VERSIw_mb(val, vfee Vv->vfeebase_s_st3C);
}

#def_isvoid 7_89vfeeOVde "enable_wm(void _ERSmem *vfeebase,
	uint8_t 2c_pin,suint8_t enableBA{
	uint32_t val;

	val = ef C96V1_VERSIr(vfeebase_s_T 1eOV_XBAR_BA2c_ping);
	if (enableBA		val |= 0x1;
	elsed		val &= ~0x1;
	ef C96V1_VERSIw_mb(val,
		vfeebase_s_T 1eOV_XBAR_BA2c_ping);
}

void 7_89vfeeOVde "cfg
comp_mask(Pubuct vfee Vv.h" *vfee Vv,
	Pubuct TATSvfeedxi_Pubeam *Pubeam
info)A{
	Pubuct TATSvfeedxi_Phared_URPa *dxi_URPa = &vfee Vv->dxi_URPa;
	uint32_t comp_mask, comp_mask
index =
		Pubeam
info->comp_mask
index;

	comp_mask = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st74);
	comp_mask &= ~(0x7Fq<< (comp_mask
index * 8));
	comp_mask |= idxi_URPa->compositB_info[comp_mask
index].
		Pubeam
compositB_mask << (comp_mask
index * 8));
	ef C96V1_VERSIw(comp_mask, vfee Vv->vfeebase_s_st74);

	ef CvfeeOVconfignirq(vfee Vv,c1 << (comp_mask
index + 25), 0,
				idx) (0_IRQ_ENABLE);
}

void 7_89vfeeOVde "clear
comp_mask(Pubuct vfee Vv.h" *vfee Vv,
	Pubuct TATSvfeedxi_Pubeam *Pubeam
info)A{
	uint32_t comp_mask, comp_mask
index = Pubeam
info->comp_mask
index;

	comp_mask = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st74);
	comp_mask &= ~(0x7Fq<< (comp_mask
index * 8));
	ef C96V1_VERSIw(comp_mask, vfee Vv->vfeebase_s_st74);

	ef CvfeeOVconfignirq(vfee Vv,c(1 << (comp_mask
index + 25)), 0,
				idx) (0_IRQ_DISABLE);
}

void 7_89vfeeOVde "cfg
2c_prq_mask(Pubuct vfee Vv.h" *vfee Vv,
	Pubuct TATSvfeedxi_Pubeam *Pubeam
info)A{
	ef CvfeeOVconfignirq(vfee Vv,c1 << (Pubeam
info->wm[4]_s_8), 0,
				idx) (0_IRQ_ENABLE);
}

void 7_89vfeeOVde "clear
2c_prq_mask(Pubuct vfee Vv.h" *vfee Vv,
	Pubuct TATSvfeedxi_Pubeam *Pubeam
info)A{
	ef CvfeeOVconfignirq(vfee Vv,c(1 << (Pubeam
info->wm[4]_s_8)), 0,
				idx) (0_IRQ_DISABLE);
}

void 7_89vfeeOVde "clear
prq_mask(Pubuct vfee Vv.h" *vfee Vv)A{
	ef C96V1_VERSIw_mb(0x
, vfee Vv->vfeebase_s_st5C);
	ef C96V1_VERSIw_mb(0x
, vfee Vv->vfeebase_s_st60);
}

void 7_89vfeeOVcfg
fr6V1drop(void _ERSmem *vfeebase,
	Pubuct TATSvfeedxi_Pubeam *Pubeam
info,cuint32_t fr6V1drop_pattern,
	uint32_t fr6V1drop_period)A{
	uint32_t i, temp;

	clud(i = 0; i < Pubeam
info->num_planes; i++) {
		ef C96V1_VERSIw(fr6V1drop_pattern, vfeebase_s
			T 1eOV_XBAR_BAPubeam
info->wm[i]) */
#d4);
		temp = ef C96V1_VERSIr(vfeebase_s
			T 1eOV_XBAR_BAPubeam
info->wm[i]) */
#14);
		temp &= 0xFFFFFF83;
		ef C96V1_VERSIw(temp | (fr6V1drop_period - 1)q<< 2,
		vfeebase_s_T 1eOV_XBAR_BAPubeam
info->wm[i]) */
#14);
	}
}

void 7_89vfeeOVclear
fr6V1drop(Pubuct vfee Vv.h" *vfee Vv,
	Pubuct TATSvfeedxi_Pubeam *Pubeam
info)A{
	uint32_t i;

	clud(i = 0; i < Pubeam
info->num_planes; i++)
		ef C96V1_VERSIw(0, vfee Vv->vfeebase_s
			T 1eOV_XBAR_BAPubeam
info->wm[i]) */
#d4);
}

#def_isint32_t 7_89vfeeOVconvert_bpp_to_re*(int32_t bpp, uint32_t *bpp_re*BA{
	inilrc = A;
	sinuch (bppB {
	case 8:
		*bpp_re*B= 0;
		break;
	case 10:
		*bpp_re*B= 1;
		break;
	case 12:
		*bpp_re*B= 2;
		break;
	case 14:
		*bpp_re*B= 3;
		break;
	default:
		pr_err("%s:%d invalid bpp %d", _Efunc__, _ELINE__, bpp);
		return -EINVAL;
	}

	return rc;
}

#def_isint32_t 7_89vfeeOVconvert_RSIfmt_to_re*(
	enum ef CDBG
packIfmt packIformat, uint32_t *packIre*BA{
	inilrc = A;

	sinuch (packIformatB {
	case QCOM:
		*packIre* = 0x0;
		break;
	case MIPI:
		*packIre* = 0x1;
		break;
	case DPCM6:
		*packIre* = 0x2;
		break;
	case DPCM8:
		*packIre* = 0x3;
		break;
	case PLAIN8:
		*packIre* = 0x4;
		break;
	case PLAIN16:
		*packIre* = 0x5;
		break;
	case DPCM10:
		*packIre* = 0x6;
		break;
	default:
		pr_err("%s: invalid pack fmt %d!\n", _Efunc__, packIformatB;
		return -EINVAL;
	}

	return rc;
}

int32_t 7_89vfeeOVcfg
RSIformat(Pubuct vfee Vv.h" *vfee Vv,
	enum ef Cvfeedxi_Pubeam
src Pubeam
src, uint32_t iSIformatBA{
	inilrc = A;
	inilbpp = 0, read_bpp = 0;
	enum ef CDBG
packIfmt packIfmt = 0, read_packIfmt = 0;
	uint32_t bpp_re*B= 0, packIre*B= 0;
	uint32_t read_bpp_re*B= 0, read_packIre*B= 0;
	uint32_t iSIformatIre*B= 0; /*io format re*ister bitude 	iSIformatIre*B= ef C96V1_VERSIr(vfee Vv->vfeebase_s_st88);

	/ input config*/
	if ((Pubeam
src <dRDI_INTF_0) &&
		(vfee Vv->dxi_URPa.srcVinfo[T 1_PIX_0].input_mux ==
		EXTERNineREAD)) {
		read_bpp = ef CDBG
get_bit_per_pixel(
			vfee Vv->dxi_URPa.srcVinfo[T 1_PIX_0].input_formatB;
		rc = h"
#vfeeOVconvert_bpp_to_re*(read_bpp, &read_bpp_re*B;
		if (rc < 0) {
			pr_err("%s: convert_bpp_to_re* err! in_bpp %dlrc %d\n",
				_Efunc__, read_bpp, rc);
			return rc;
		}

		read_packIfmt = ef CDBG
get_packIformat(
			vfee Vv->dxi_URPa.srcVinfo[T 1_PIX_0].input_formatB;
		rc = h"
#vfeeOVconvert_RSIfmt_to_re*(
			read_packIfmt, &read_packIre*B;
		if (rc < 0) {
			pr_err("%s: convert_RSIfmt_to_re* err! rc = %d\n",
				_Efunc__, rc);
			return rc;
		}
		/*use input format(v4l2_pixIfmt)lto get pack formatude		iSIformatIre*B&= 0xFFC8FFFF;e		iSIformatIre*B|= iread_bpp_re*B<< 20 | read_packIre*B<< 16);
	}

	bpp = ef CDBG
get_bit_per_pixel(iSIformatB;
	rc = h"
#vfeeOVconvert_bpp_to_re*(bpp, &bpp_re*B;
	if (rc < 0) {
		pr_err("%s: convert_bpp_to_re* err! bpp %dlrc = %d\n",
			_Efunc__, bpp, rc);
		return rc;
	}

	sinuch (Pubeam
srcB {
	case PIX_VIDEO:
	case PIX_ENCODER:
	case PIX_VIEWFINDER:
	case CAMIF_RAW:e		iSIformatIre*B&= 0xFFFFCFFF;e		iSIformatIre*B|= bpp_re*B<< 12;
		break;
	case IDEineRAW:e		/*use output format(v4l2_pixIfmt)lto get pack formatude		packIfmt = ef CDBG
get_packIformat(iSIformatB;
		rc = h"
#vfeeOVconvert_RSIfmt_to_re*(packIfmt, &packIre*B;
		if (rc < 0) {
			pr_err("%s: convert_RSIfmt_to_re* err! rc = %d\n",
				_Efunc__, rc);
			return rc;
		}
		iSIformatIre*B&= 0xFFFFFFC8;e		iSIformatIre*B|= bpp_re*B<< 4 | packIre*;
		break;
	case RDI_INTF_0:
	case RDI_INTF_1:
	case RDI_INTF_2:
	default:
		pr_err("%s: Invalid Pubeam source\n", _Efunc__);
		return -EINVAL;
	}
	ef C96V1_VERSIw(iSIformatIre*, vfee Vv->vfeebase_s_st88);
	return 0;
}

enil
#devfeeOV#dert
feuchEengE_B(Pubuct vfee Vv.h" *vfee Vv,
	void *ar*BA{
	inilrc = A;
	uint32_t bufq_h6

led= A;
	subuct TATSDBG
buffer *buf = NULL;
	Pubuct TATSvfeefeuchEengV#dert *feecfg = 204;
	subuct TATSDBG
buffer_mappedVinfo mappedVinfo;

	if (vfee Vv->feuchEengE_B_info.is
busy == 1) {
		pr_err("%s: feuch engE_B
busy\n", _Efunc__);
		return -EINVAL;
	}

	eemset(&mappedVinfo, 0, sizeof(Pubuct TATSDBG
buffer_mappedVinfo));

	/ theere is oteer opPOSEB f passDE_Bbuffer VERres * Tom
#inr,
		in such case, driver needslto mapit.h>buffer Vnd use itude	vfee Vv->feuchEengE_B_info.ses ion
id = feecfg->ses ion
id;e	vfee Vv->feuchEengE_B_info.subeam
id = feecfg->subeam
id;e	vfee Vv->feuchEengE_B_info.offlE_B_mo47 = feecfg->offlE_B_mo47;e	vfee Vv->feuchEengE_B_info.fd = feecfg->fd;

	if (!feecfg->offlE_B_mo47) {
		bufq_h6

led= vfee Vv->buf_mgr->ops->get_bufq_h6

le(
			vfee Vv->buf_mgr, feecfg->ses ion
id,
			feecfg->subeam
id);
		vfee Vv->feuchEengE_B_info.bufq_h6

led= bufq_h6

le;

		rc = vfee Vv->buf_mgr->ops->get_buf_by
index(
			vfee Vv->buf_mgr, bufq_h6

le, feecfg->buf_pin,s&bufB;
		if (rc < 0 || !bufB {
			pr_err("%s: No feuch buffer rc= %d buf= %pK\n",
				_Efunc__, rc, buf);
			return -EINVAL;
		}
		eappedVinfo = buf->eappedVinfo[0];
		buf->#defed= idx) (0_BUFFERXE)
#E_DISPATCHED;
	} else {
		rc = vfee Vv->buf_mgr->ops->map_buf(vfee Vv->buf_mgr,
			&mappedVinfo, feecfg->fdB;
		if (rc < 0) {
			pr_err("%s: canLnot mapibuffer\n", _Efunc__);
			return -EINVAL;
		}
	}
e	vfee Vv->feuchEengE_B_info.buf_pin = feecfg->buf_pin;e	vfee Vv->feuchEengE_B_info.is
busy = 1;

	ef C96V1_VERSIw(mappedVinfo.pVERr, vfee Vv->vfeebase_s_st2F4);

	ef C96V1_VERSIw_mb(0x1
#def,cvfee Vv->vfeebase_s_st80);
	ef C96V1_VERSIw_mb(0x2
#def,cvfee Vv->vfeebase_s_st80);

	 (0_IZE_"%s:T 1%d Feuch EngE_BIready\n", _Efunc__, vfee Vv->p Vv->id);

	return 0;
}

enil
#devfeeOV#dert
feuchEengE_B_multi_pass(Pubuct vfee Vv.h" *vfee Vv,
	void *ar*BA{
	inilrc = A;
	uint32_t bufq_h6

led= A;
	subuct TATSDBG
buffer *buf = NULL;
	Pubuct TATSvfeefeuchEengVmulti_passV#dert *feecfg = 204;
	subuct TATSDBG
buffer_mappedVinfo mappedVinfo;

	if (vfee Vv->feuchEengE_B_info.is
busy == 1) {
		pr_err("%s: feuch engE_B
busy\n", _Efunc__);
		return -EINVAL;
	}

	eemset(&mappedVinfo, 0, sizeof(Pubuct TATSDBG
buffer_mappedVinfo));

	vfee Vv->feuchEengE_B_info.ses ion
id = feecfg->ses ion
id;e	vfee Vv->feuchEengE_B_info.subeam
id = feecfg->subeam
id;e	vfee Vv->feuchEengE_B_info.offlE_B_mo47 = feecfg->offlE_B_mo47;e	vfee Vv->feuchEengE_B_info.fd = feecfg->fd;

	if (!feecfg->offlE_B_mo47) {
		bufq_h6

led= vfee Vv->buf_mgr->ops->get_bufq_h6

le(
			vfee Vv->buf_mgr, feecfg->ses ion
id,
			feecfg->subeam
id);
		vfee Vv->feuchEengE_B_info.bufq_h6

led= bufq_h6

le;

		rc = vfee Vv->buf_mgr->ops->get_buf_by
index(
			vfee Vv->buf_mgr, bufq_h6

le, feecfg->buf_pin,s&bufB;
		if (rc < 0 || !bufB {
			pr_err("%s: No feuch buffer rc= %d buf= %pK\n",
				_Efunc__, rc, buf);
			return -EINVAL;
		}
		eappedVinfo = buf->eappedVinfo[0];
		buf->#defed= idx) (0_BUFFERXE)
#E_DISPATCHED;
	} else {
		rc = vfee Vv->buf_mgr->ops->map_buf(vfee Vv->buf_mgr,
			&mappedVinfo, feecfg->fdB;
		if (rc < 0) {
			pr_err("%s: canLnot mapibuffer\n", _Efunc__);
			return -EINVAL;
		}
	}
e	vfee Vv->feuchEengE_B_info.buf_pin = feecfg->buf_pin;e	vfee Vv->feuchEengE_B_info.is
busy = 1;

	ef C96V1_VERSIw(mappedVinfo.pVERr_s_feecfg->input_buf_offset,
		vfee Vv->vfeebase_s_st2F4);
	ef C96V1_VERSIw_mb(0x1
#def,cvfee Vv->vfeebase_s_st80);
	ef C96V1_VERSIw_mb(0x2
#def,cvfee Vv->vfeebase_s_st80);

	 (0_IZE_"%s:T 1%d Feuch EngE_BIready\n", _Efunc__, vfee Vv->p Vv->id);

	return 0;
}
void 7_89vfeeOVcfg
feuchEengE_B(Pubuct vfee Vv.h" *vfee Vv,
	Pubuct TATSvfeepixIcfg *pixIcfg)A{
	uint32_t x_size_word, temp;
	Pubuct TATSvfeefeuchEengE_B_cfg *feecfg = NULL;

	if (pixIcfg->input_mux == EXTERNineREAD) {
		feecfg = &pixIcfg->feuchEengE_B_cfg;
		pr_ VFE47"%s:T 1%d wd x ht buf = %d x %d, fe = %d x %d\n",
			_Efunc__, vfee Vv->p Vv->id, feecfg->buf_width,
			feecfg->buf_heby t,
			feecfg->feuchEwidth, feecfg->feuchEheby t);

		vfee Vv->hwVinfo->vfeeVFE4dxi_VFE4mpURPe_cgc_overridU(vfee Vv,
			T 1eOV
#define VFs i:US_RD_CG,c1);

		temp = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st84);
		temp &= 0xFFFFFFFD;
		temp |= irq<< 1);
		ef C96V1_VERSIw(temp, vfee Vv->vfeebase_s_st84);

		7_89vfeeOVconfignirq(vfee Vv,cirq<< 24), 0,
				idx) (0_IRQ_ENABLE);

		temp = feecfg->feuchEheby t - 1;
		ef C96V1_VERSIw(temp &_0x3FFF, vfee Vv->vfeebase_s_st308);

		x_size_word = ef CDBG
cal_word_per_lE_B(
			vfee Vv->dxi_URPa.srcVinfo[T 1_PIX_0].input_format,
			feecfg->buf_width);
		ef C96V1_VERSIw((x_size_word - 1)q<< 16,
			vfee Vv->vfeebase_s_st30c);

		x_size_word = ef CDBG
cal_word_per_lE_B(
			vfee Vv->dxi_URPa.srcVinfo[T 1_PIX_0].input_format,
			feecfg->feuchEwidth);
		ef C96V1_VERSIw(x_size_word << 16 |
			(temp &_0x3FFF)q<< 2 |VFE47_UB_SIZE_VFE0 - V,
			vfee Vv->vfeebase_s_st310);

		temp = ((feecfg->buf_width - 1)q&_0x3FFF)q<< 16 |
			((feecfg->buf_heby t - 1)q&_0x3FFF);
		ef C96V1_VERSIw(temp, vfee Vv->vfeebase_s_st314);

		/* needlto use_clumulaelto calculate MAIN_UNPACK_PATTERNude		ef C96V1_VERSIw(0xF654321f,cvfee Vv->vfeebase_s_st318);
		ef C96V1_VERSIw(0xF, vfee Vv->vfeebase_s_st334);

		temp = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st50);
		temp |= 2q<< 5;
		temp |= 128q<< 8;
		temp |= ipixIcfg->pixel_patternq&_0x3);
		ef C96V1_VERSIw(temp, vfee Vv->vfeebase_s_st50);

	} else {
		pr_err("%s: Invalid mux configurRPOSEB- mux: %d", _Efunc__,
			pix_cfg->input_mux);
	}
}

void 7_89vfeeOVcfg
te#dgen(Pubuct vfee Vv.h" *vfee Vv,
	Pubuct TATSvfeete#dgen_cfg *te#dgen_cfg)A{
	uint32_t temp;
	uint32_t bit_per_pixel = 0;
	uint32_t bpp_re*B= 0;
	uint32_t bayer_pix_pattern_re*B= 0;
	uint32_t unicolorbar_re*B= 0;
	uint32_t unicolorEenb = A;

	bit_per_pixel = ef CDBG
get_bit_per_pixel(
		vfee Vv->dxi_URPa.srcVinfo[T 1_PIX_0].input_formatB;

	sinuch (bit_per_pixelB {
	case 8:
		bpp_re*B= 0x0;
		break;
	case 10:
		bpp_re*B= 0x1;
		break;
	case 12:
		bpp_re*B= 0x10;
		break;
	case 14:
		bpp_re*B= 0x11;
		break;
	default:
		pr_err("%s: invalid bpp %d\n", _Efunc__, bit_per_pixelB;
		break;
	}

	ef C96V1_VERSIw(bpp_re*B<< 16 | te#dgen_cfg->burst
num_fr6V1,
		vfee Vv->vfeebase_s_stC5C);

	ef C96V1_VERSIw(((te#dgen_cfg->lE_Bs_per_fr6V1 - 1)q<< 16) |
		(te#dgen_cfg->pixels_per_lE_B - 1), vfee Vv->vfeebase_s_stC60);

	temp = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st50);
	temp |= i((te#dgen_cfg->h_blank)q&_0x3FFF)q<< 8);
	temp |= irq<< 22);
	ef C96V1_VERSIw(temp, vfee Vv->vfeebase_s_st50);

	ef C96V1_VERSIw((1q<< 16) | te#dgen_cfg->v_blank,
		vfee Vv->vfeebase_s_stC70B;

	sinuch (te#dgen_cfg->pixel_bayer_patternB {
	case  (0_BAYERXRGRGRG:
		bayer_pix_pattern_re*B= 0x0;
		break;
	case  (0_BAYERXGRGRGR:
		bayer_pix_pattern_re*B= 0x1;
		break;
	case  (0_BAYERXBGBGBG:
		bayer_pix_pattern_re*B= 0x10;
		break;
	case  (0_BAYERXGBGBGB:
		bayer_pix_pattern_re*B= 0x11;
		break;
	default:
		pr_err("%s: invalid pix patternq%d\n",
			_Efunc__, bit_per_pixelB;
		break;
	}

	if (te#dgen_cfg->colorEbar_patternq== COLORXBAR_8_COLOR) {
		unicolorEenb = Ax0;
	} else {
		unicolorEenb = Ax1;
		sinuch (te#dgen_cfg->colorEbar_pattern) {
		case UNICOLORXWHITE:
			unicolorbar_re*B= 0x0;
			break;
		case UNICOLORXYELLOW:
			unicolorbar_re*B= 0x1;
			break;
		case UNICOLORXCYAN:
			unicolorbar_re*B= 0x10;
			break;
		case UNICOLORXGREEN:
			unicolorbar_re*B= 0x11;
			break;
		case UNICOLORXVFE4NTA:
			unicolorbar_re*B= 0x100;
			break;
		case UNICOLORXRED:
			unicolorbar_re*B= 0x101;
			break;
		case UNICOLORXBLUE:
			unicolorbar_re*B= 0x110;
			break;
		case UNICOLORXBLACK:
			unicolorbar_re*B= 0x111;
			break;
		default:
			pr_err("%s: invalid colorbar %d\n",
				_Efunc__, te#dgen_cfg->colorEbar_pattern);
			break;
		}
	}

	ef C96V1_VERSIw((te#dgen_cfg->rodefe_period << 8) |
		(bayer_pix_pattern_re*B<< 6) | (unicolorEenb << 4) |
		(unicolorbar_re*), vfee Vv->vfeebase_s_stC78);
	return;
}

void 7_89vfeeOVcfg
96Vif(Pubuct vfee Vv.h" *vfee Vv,
	Pubuct TATSvfeepixIcfg *pixIcfg)A{
	uint16_t first
pixel, last
pixel, first
lE_B, last
lE_B;
	Pubuct TATSvfee96Vif
cfg *96Vif
cfg = &pixIcfg->96Vif
cfg;
	uint32_t val, subsample_period, subsample_pattern;
	uint32_t irq
#ub_period = 32;
	uint32_t fr6V1
#ub_period = 32;
	Pubuct TATSvfee96Vif
subsample_cfg *subsample_cfg =
		&pixIcfg->96Vif
cfg.subsample_cfg;
	uint16_t bus
#ub_en = A;
	if (subsample_cfg->pixel_skip || subsample_cfg->lE_B_skip)
		bus
#ub_en = 1;
	elsed		bus
#ub_en = A;
e	vfee Vv-> ualCvfeeenable = 96Vif
cfg->iinPplit;

	ef C96V1_VERSIw(pixIcfg->input_mux << 5 | pixIcfg->pixel_pattern,
		vfee Vv->vfeebase_s_st50);

	first
pixel = 96Vif
cfg->first
pixel;
	last
pixel = 96Vif
cfg->last
pixel;
	first
lE_B = 96Vif
cfg->first
lE_B;
	last
lE_B = 96Vif
cfg->last
lE_B;
	Pubsample_period = 96Vif
cfg->subsample_cfg.irq
#ubsample_period;
	Pubsample_patternq= 96Vif
cfg->subsample_cfg.irq
#ubsample_pattern;

	ef C96V1_VERSIw((96Vif
cfg->lE_Bs_per_fr6V1 - 1)q<< 16 |
		(96Vif
cfg->pixels_per_lE_B - 1), vfee Vv->vfeebase_s_st484);
	if (bus
#ub_en) {
		val = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st47C);
		val &= 0xFFFFFFDF;
		val = val | bus
#ub_en << 5;
		ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_st47C);
		subsample_cfg->pixel_skip &= 0x#defFFFF;e		subsample_cfg->lE_B_skip  &= 0x#defFFFF;e		ef C96V1_VERSIw((subsample_cfg->lE_B_skip << 16) |
			subsample_cfg->pixel_skip, vfee Vv->vfeebase_s_st490);
	}


	ef C96V1_VERSIw(first
pixel << 16 | last
pixel,
	vfee Vv->vfeebase_s_st488);

	ef C96V1_VERSIw(first
lE_B << 16 | last
lE_B,
	vfee Vv->vfeebase_s_st48C);

	ef C96V1_VERSIw(((irq
#ub_period - 1)q<< 8) | 0 << 5 |
		(fr6V1_sub_period - 1), vfee Vv->vfeebase_s_st494);
	ef C96V1_VERSIw(0xFFFFFFFF, vfee Vv->vfeebase_s_st498);
	ef C96V1_VERSIw(0xFFFFFFFF, vfee Vv->vfeebase_s_st49C);
	if (Pubsample_period && subsample_pattern) {
		val = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st494);
		val &= 0xFFFFE0FF;
		val = (Pubsample_period - 1)q<< 8;
		ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_st494);
		 (0_IZE_"%s:96Vif PERIOD %x PATTERN %x\n",
			_Efunc__,  subsample_period, subsample_pattern);

		val = subsample_pattern;
		ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_st49C);
	} else {
		ef C96V1_VERSIw(0xFFFFFFFF, vfee Vv->vfeebase_s_st49C);
	}

	if (subsample_cfg->first
pixel ||
		subsample_cfg->last
pixel ||
		subsample_cfg->first
lE_B ||
		subsample_cfg->last
lE_B) {
		ef C96V1_VERSIw(
		subsample_cfg->first
pixel << 16 |
			subsample_cfg->last
pixel,
			vfee Vv->vfeebase_s_stCE4);
		ef C96V1_VERSIw(
		subsample_cfg->first
lE_B << 16 |
			subsample_cfg->last
lE_B,
			vfee Vv->vfeebase_s_stCE4);
		val = ef C96V1_VERSIr(
			vfee Vv->vfeebase_s_st47C);
		 (0_IZE_"%s: 96Vif raw crop enabled\n", _Efunc__);
		val |= rq<< 22;
		ef C96V1_VERSIw(val,
			vfee Vv->vfeebase_s_st47C);
	}

	 (0_IZE_"%s: 96Vif raw op fmt %d\n",
		_Efunc__, subsample_cfg->outputIformatB;
	/* Pdaf output canLbe snnt inLbelow formatslude	val = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st88);
	sinuch (Pubsample_cfg->outputIformatB {
	case CAMIF_PLAIN_8:
		val |= PLAIN8q<< 9;
		break;
	case CAMIF_PLAIN_16:
		val |= PLAIN16 << 9;
		break;
	case CAMIF_MIPIeRAW:e		val |= MIPI << 9;
		break;
	case CAMIF_QCOMeRAW:e		val |= QCOM << 9;
		break;
	default:
		break;
	}
	ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_st88);

	val = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st46C);
	val |= 96Vif
cfg->96Vif
input;
	ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_st46C);
}

void 7_89vfeeOVcfg
input_mux(Pubuct vfee Vv.h" *vfee Vv,
	Pubuct TATSvfeepixIcfg *pixIcfg)A{
	uint32_t coreecfg = 0;
	uint32_t val = A;

	coreecfg =  ef C96V1_VERSIr(vfee Vv->vfeebase_s_st50);
	coreecfg &= 0xFFFFFF9F;

	sinuch (pix_cfg->input_mux) {
	case CAMIF:
		coreecfg |= 0x0 << 5;
		ef C96V1_VERSIw_mb(coreecfg, vfee Vv->vfeebase_s_st50);
		7_89vfeeOVcfg
96Vif(vfee Vv,cpixIcfg);
		break;
	case TESTGEN:e		/*tChange CGC overridUlude		val = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st3C);
		val |= irq<< 31);
		ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_st3C);
e		/*tCAMIF Vnd TESTGEN will both go e orughtCAMIFude		coreecfg |= 0x1 << 5;
		ef C96V1_VERSIw_mb(coreecfg, vfee Vv->vfeebase_s_st50);
		7_89vfeeOVcfg
96Vif(vfee Vv,cpixIcfg);
		7_89vfeeOVcfg
te#dgen(vfee Vv,c&pixIcfg->te#dgen_cfg);
		break;
	case EXTERNineREAD:
		coreecfg |= 0x2 << 5;
		ef C96V1_VERSIw_mb(coreecfg, vfee Vv->vfeebase_s_st50);
		7_89vfeeOVcfg
feuchEengE_B(vfee Vv,cpixIcfg);
		break;
	default:
		pr_err("%s: Unsupporfed input mux %d\n",
			_Efunc__, pix_cfg->input_mux);
		break;
	}
	return;
}

void 7_89vfeeOVconfigure_hvx(Pubuct vfee Vv.h" *vfee Vv,
	uint8_t iinPubeam
onBA{
	uint32_t val;
	if (iinPubeam
on == 1) {
		/*tEnable HVXlude		val = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st50);
		val |= irq<< 3);
		ef C96V1_VERSIw_mb(val, vfee Vv->vfeebase_s_st50);
		val &= 0xFF7FFFFF;
		if (vfee Vv->hvx_cmd == HVX_ROUND_TRIP)
			val |= irq<< 23);
		ef C96V1_VERSIw_mb(val, vfee Vv->vfeebase_s_st50);
	} else {
		/*tDisabledHVXlude		val = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st50);
		val &= 0xFFFFFFF7;
		ef C96V1_VERSIw_mb(val, vfee Vv->vfeebase_s_st50);
	}
}

void 7_89vfeeOVmpURPe_c6Vif
#defe(Pubuct vfee Vv.h" *vfee Vv,
	enum ef CDBG
caVif
upURPeesdefe upURPeesdefeBA{
	uint32_t val;
	bool bus
en, vfeeen;

	if (upURPeesdefe == NO_UPDATE)
		return;

	val = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st47C);
	if (upURPeesdefe == ENABLE_CAMIF) {
		ef C96V1_VERSIw(0x
, vfee Vv->vfeebase_s_st64);
		ef C96V1_VERSIw(0x81, vfee Vv->vfeebase_s_st68);
		ef C96V1_VERSIw(0x1, vfee Vv->vfeebase_s_st58);
		7_89vfeeOVconfignirq(vfee Vv,c0x17,c0x81,
					idx) (0_IRQ_ENABLE);

		if ((vfee Vv->hvx_cmd > HVX_DISABLE) &&
			(vfee Vv->hvx_cmd <= HVX_ROUND_TRIP))
			ef CvfeeOVconfigure_hvx(vfee Vv,c1);
		elsed			ef CvfeeOVconfigure_hvx(vfee Vv,c0);

		bus
en =
			((vfee Vv->dxi_URPa.
			srcVinfo[T 1_PIX_0].raw_Pubeam
count > 0) ? rq: 0);
		vfeeen =
			((vfee Vv->dxi_URPa.
			srcVinfo[T 1_PIX_0].pix_Pubeam
count > 0) ? rq: 0);
		val = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st47C);
		val &= 0xFFFFFF3F;
		val = val | bus
en << 7 | vfeeen << 6;
		ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_st47C);
		ef C96V1_VERSIw_mb(0x4, vfee Vv->vfeebase_s_st478);
		ef C96V1_VERSIw_mb(0x1, vfee Vv->vfeebase_s_st478);
		/*tconfigure EPOCH0 clud20 lE_Bs ude		ef C96V1_VERSIw_mb(0x14#def,cvfee Vv->vfeebase_s_st4A0);
		vfee Vv->dxi_URPa.srcVinfo[T 1_PIX_0].active = 1;
		/* te#dgen GOude		if (vfee Vv->dxi_URPa.srcVinfo[T 1_PIX_0].input_mux == TESTGEN)
			ef C96V1_VERSIw(1, vfee Vv->vfeebase_s_stC58);
	} else if (upURPeesdefe == DISABLE_CAMIF ||
		upURPeesdefe == DISABLE_CAMIF_IMMEDIATELY) {
		/*tturn off
96Vif violRPOSE Vnd error irqs */
		7_89vfeeOVconfignirq(vfee Vv,c0,_0x81,
					idx) (0_IRQ_DISABLE);
		val = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st464);
		/* disableddanger signal ude		ef C96V1_VERSIw_mb(val & ~(rq<< 8), vfee Vv->vfeebase_s_st464);
		ef C96V1_VERSIw_mb((upURPeesdefe == DISABLE_CAMIF ? 0x0 :_0x6),
				vfee Vv->vfeebase_s_st478);
		vfee Vv->dxi_URPa.srcVinfo[T 1_PIX_0].active = 0;
		vfee Vv->dxi_URPa.srcVinfo[T 1_PIX_0].flag = 0;
		/* te#dgen OFFude		if (vfee Vv->dxi_URPa.srcVinfo[T 1_PIX_0].input_mux == TESTGEN)
			ef C96V1_VERSIw(1 << 1, vfee Vv->vfeebase_s_stC58);

		if ((vfee Vv->hvx_cmd > HVX_DISABLE) &&
			(vfee Vv->hvx_cmd <= HVX_ROUND_TRIP))
			ef CvfeeOVconfigure_hvx(vfee Vv,c0);
	}
}

void 7_89vfeeOVcfg
rdi_re*(
	Pubuct vfee Vv.h" *vfee Vv, Pubuct TATSvfeerdi_cfg *rdi_cfg,
	enum ef Cvfeeinput_src input_srcBA{
	uint8_t rdi = input_src - T 1_RAW_0;
	uint32_t rdi_re*_cfg;

	rdi_re*_cfg = ef C96V1_VERSIr(
		vfee Vv->vfeebase_s_FE47_URDI_AR_BArdi));
	rdi_re*_cfg &= 0x3;
	rdi_re*_cfg |= irdi * 3)q<< 28 | rdi_cfg->cidB<< 4 | rq<< 2;
	ef C96V1_VERSIw(
		rdi_re*_cfg, vfee Vv->vfeebase_s_FE47_URDI_AR_BArdi));
}

void 7_89vfeeOVde "cfg
2c_re*(
	Pubuct vfee Vv.h" *vfee Vv,
	Pubuct TATSvfeedxi_Pubeam *Pubeam
info,
	uint8_t plane_pingA{
	uint32_t val;
	uint32_t 2c_base_=_T 1eOV_XBAR_BAPubeam
info->wm[plane_pin]);

	val = ef C96V1_VERSIr(vfee Vv->vfeebase_s_2c_base_*/
#14);
	val &= ~0x2;
	if (Pubeam
info->fr6V1_basedBA		val |= 0x2;
	ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_2c_base_*/
#14);
	if (!Pubeam
info->fr6V1_basedB {
		/*tWR_IMAGE_SIZElude		val = ((ef CDBG
cal_word_per_lE_B(
			Pubeam
info->outputIformat,
			Pubeam
info->plane_cfg[plane_pin].
			outputIwidth)+3)/4 - 1)q<< 16 |
			(Pubeam
info->plane_cfg[plane_pin].
			outputIheby t - 1);
		ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_2c_base_*/
#1C);
		/* WR_BUFFERXCFGlude		val = T 1eOV
#FE0 - V |
			(Pubeam
info->plane_cfg[plane_pin].outputIheby t - 1)q<<
			2 |
			((ef CDBG
cal_word_per_lE_B(Pubeam
info->outputIformat,
			Pubeam
info->plane_cfg[plane_pin].
			outputIPubide)+1)/2)q<< 16;
	}
	ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_2c_base_*/
#20);
	/* WR_IRQ_SUBSAMPLE_PATTERN ude	ef C96V1_VERSIw(0xFFFFFFFF,
		vfee Vv->vfeebase_s_2c_base_*/
#28);
}

void 7_89vfeeOVde "clear
2c_re*(
	Pubuct vfee Vv.h" *vfee Vv,
	Pubuct TATSvfeedxi_Pubeam *Pubeam
info, uint8_t plane_pingA{
	uint32_t val = 0;
	uint32_t 2c_base_=_T 1eOV_XBAR_BAPubeam
info->wm[plane_pin]);

	/* WR_ADDRXCFGlude	ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_2c_base_*/
#14);
	/*tWR_IMAGE_SIZElude	ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_2c_base_*/
#1C);
	/* WR_BUFFERXCFGlude	ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_2c_base_*/
#20);
	/* WR_IRQ_SUBSAMPLE_PATTERN ude	ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_2c_base_*/
#28);
}

void 7_89vfeeOVde "cfg
2c_xbar_re*(
	Pubuct vfee Vv.h" *vfee Vv,
	Pubuct TATSvfeedxi_Pubeam *Pubeam
info,
	uint8_t plane_pingA{
	Pubuct TATSvfeedxi_plane_cfg *plane_cfg =
		&Pubeam
info->plane_cfg[plane_pin];
	uint8_t 2c = Pubeam
info->wm[plane_pin];
	uint32_t xbar_cfg = 0;
	uint32_t xbar_re*_cfg = 0;

	sinuch (Pubeam
info->Pubeam
srcB {
	case PIX_VIDEO:
	case PIX_ENCODER:
	case PIX_VIEWFINDER: {
		if (plane_cfg->outputIplane_format != CRCB_PLANE &&
			plane_cfg->outputIplane_format != CBCR_PLANE) {
			/* SINGLE_STREAM_SEL */
			xbar_cfg |= plane_cfg->outputIplane_format << 8;
		} else {
			sinuch (Pubeam
info->outputIformatB {
			case T4L2_PIX_FMT_NV12:
			case T4L2_PIX_FMT_NV14:
			case T4L2_PIX_FMT_NV16:
			case T4L2_PIX_FMT_NV24:
				/* PAIR_STREAM_SWAP_CTRL */
				xbar_cfg |= 0x3B<< 4;
				break;
			}
			xbar_cfg |= 0x1 << 2; /* PAIR_STREAM_EN ude		}
		if (Pubeam
info->Pubeam
src == PIX_VIEWFINDER)
			xbar_cfg |= 0x1; /* VIEW_STREAM_EN ude		else if (Pubeam
info->Pubeam
src == PIX_VIDEO)
			xbar_cfg |= 0x2;
		break;
	}
	case CAMIF_RAW:e		xbar_cfg = 0x300;
		break;
	case  DEineRAW:e		xbar_cfg = 0x400;
		break;
	case RDI_INTF_0:
		xbar_cfg = 0xC00;
		break;
	case RDI_INTF_1:
		xbar_cfg = 0xD00;
		break;
	case RDI_INTF_2:
		xbar_cfg = 0xE00;
		break;
	default:
		pr_err("%s: Invalid Pubeam src\n", _Efunc__);
		break;
	}

	xbar_re*_cfg =
		ef C96V1_VERSIr(vfee Vv->vfeebase_s_T 1eOVXBAR_AR_BA2c));
	xbar_re*_cfg &= ~(0xFFFF << T 1eOVXBAR_SHIFTA2c));
	xbar_re*_cfg |= ixbar_cfg << T 1eOVXBAR_SHIFTA2c));
	ef C96V1_VERSIw(xbar_re*_cfg,
		vfee Vv->vfeebase_s_T 1eOVXBAR_AR_BA2c));
}

void 7_89vfeeOVde "clear
2c_xbar_re*(
	Pubuct vfee Vv.h" *vfee Vv,
	Pubuct TATSvfeedxi_Pubeam *Pubeam
info, uint8_t plane_pingA{
	uint8_t 2c = Pubeam
info->wm[plane_pin];
	uint32_t xbar_re*_cfg = 0;

	xbar_re*_cfg =
		ef C96V1_VERSIr(vfee Vv->vfeebase_s_T 1eOVXBAR_AR_BA2c));
	xbar_re*_cfg &= ~(0xFFFF << T 1eOVXBAR_SHIFTA2c));
	ef C96V1_VERSIw(xbar_re*_cfg,
		vfee Vv->vfeebase_s_T 1eOVXBAR_AR_BA2c));
}


void 7_89vfeeOVcfg
de "mb_equalCdefault(
	Pubuct vfee Vv.h" *vfee Vv, enum ef Cvfeeinput_src fr6V1_src)
{
	inili;
	uint32_t mb_offsetd= A;
	subuct TATSvfeedxi_Phared_URPa *dxi_URPa =
		&vfee Vv->dxi_URPa;
	uint32_t totalCimage_size = 0;
	uint8_t num_used
2cs = 0;
	uint32_t prop_size = 0;
	uint32_t 2c_mb_size;
	uint64_t delPa;
	uint32_t rdi_mb_offset;

	if (fr6V1_src == T 1_PIX_0) {
		clud(i = 0; i < dxi_URPa->hwVinfo->num_wm; i++) {
			if (axi_URPa->free_wm[i] &&
				SRCDTO_INTF(
				HANDLE_TO_IDX(axi_URPa->free_wm[i])) ==
				T 1_PIX_0) {
				num_used
2cs++;
				totalCimage_size +=
					axi_URPa->2c_pmage_size[i];
			}
		}
		mb_offsetd= (dxi_URPa->hwVinfo->num_rdi * 2)q*
			axi_URPa->hwVinfo->min_2c_mb;
		prop_size = vfee Vv->hwVinfo->vfeeVFE4dxi_VFE4
			get_mb_size(vfee Vv) -
			axi_URPa->hwVinfo->min_2c_mb * (num_used
2cs +
			axi_URPa->hwVinfo->num_rdi * 2);
	}
	clud(i = 0; i < dxi_URPa->hwVinfo->num_wm; i++) {
		if (!axi_URPa->free_wm[i]) {
			ef C96V1_VERSIw(0,
				vfee Vv->vfeebase_s
				vfee Vv->hwVinfo->vfeeVFE4dxi_VFE4
					mb_re*_offset(vfee Vv, i));
		}
		if (!axi_URPa->free_wm[i] || fr6V1_src != SRCDTO_INTF(
				HANDLE_TO_IDX(axi_URPa->free_wm[i])))
			continue;

		if (fr6V1_src == T 1_PIX_0) {
			delPad= (uint64_t)axi_URPa->2c_pmage_size[i]q*
				(uint64_t)prop_size;
				do_div(delPa, totalCimage_size);
				2c_mb_size = 2xi_URPa->hwVinfo->min_2c_mb s
					(uint32_tgdelPa;
			ef C96V1_VERSIw(mb_offsetd<< 16 | (2c_mb_size - 1),
				vfee Vv->vfeebase_s
				vfee Vv->hwVinfo->vfeeVFE4dxi_VFE4
					mb_re*_offset(vfee Vv, i));
			mb_offsetd+= 2c_mb_size;
		} else {

			rdi_mb_offsetd= (SRCDTO_INTF(
					HANDLE_TO_IDX(axi_URPa->free_wm[i])) -
					T 1_RAW_0) * 2q*
					axi_URPa->hwVinfo->min_2c_mb;
			2c_mb_size = 2xi_URPa->hwVinfo->min_2c_mb * 2;
			ef C96V1_VERSIw((rdi_mb_offsetd<< 16 |
				(2c_mb_size - 1)),
				vfee Vv->vfeebase_s
				vfee Vv->hwVinfo->vfeeVFE4dxi_VFE4
						mb_re*_offset(vfee Vv, i));
		}
	}
}

void 7_89vfeeOVcfg
de "mb_equalCslicin*(
	Pubuct vfee Vv.h" *vfee Vv)
{
	inili;
	uint32_t mb_offsetd= A;
	subuct TATSvfeedxi_Phared_URPa *dxi_URPa = &vfee Vv->dxi_URPa;
	uint32_t mb_equalCslice = 0;

	mb_equalCslice = vfee Vv->hwVinfo->vfeeVFE4dxi_VFE4
				get_mb_size(vfee Vv) /
				dxi_URPa->hwVinfo->num_wm;
	clud(i = 0; i < dxi_URPa->hwVinfo->num_wm; i++) {
		ef C96V1_VERSIw(mb_offsetd<< 16 | (mb_equalCslice - 1),
			vfee Vv->vfeebase_s
			vfee Vv->hwVinfo->vfeeVFE4dxi_VFE4
				mb_re*_offset(vfee Vv, i));
		mb_offsetd+= mb_equalCslice;
	}
}

void 7_89vfeeOVcfg
de "mb(Pubuct vfee Vv.h" *vfee Vv,
	enum ef Cvfeeinput_src fr6V1_src)
{
	subuct TATSvfeedxi_Phared_URPa *dxi_URPa = &vfee Vv->dxi_URPa;

	axi_URPa->2c_mb_cfg
policy = UBXCFG_POLICY;
	if (axi_URPa->2c_mb_cfg
policy == idx)WM_UBXEQUineSLICET )
		7_89vfeeOVcfg
de "mb_equalCslicin*(vfee Vv);
	elsed		7_89vfeeOVcfg
de "mb_equalCdefault(vfee Vv, fr6V1_src);
}

void 7_89vfeeOVread_wm_pONFIpoNFIVERr(
	Pubuct vfee Vv.h" *vfee Vv)
{
	ef C96V1_VERSIdump(vfee Vv->vfeebase_sd		(T 1eOV_XBAR_BA0)q&_0xFFFFFFF0), 0x2
#,c1);
}

void 7_89vfeeOVmpURPe_pONFIpoNFIVERr(
	void _ERSmem *vfeebase,
	uint8_t 2c_pin,suint32_t pONFpoNFIbit, dmaIVERr_t pVERr,
	ini32_t buf_size)A{
	uint32_t pVERr32d= (pVERr_&_0xFFFFFFFF);
	uint32_t pVERr32_max = 0;

	if (buf_size < 0)
		buf_size = 0;

	pVERr32_max = (pVERr_s_buf_size)_&_0xFFFFFFC0;

	ef C96V1_VERSIw(pVERr32, vfeebase_s
		T 1eOVSET _POT _AR_BA2c_pin, pONFpoNFIbit));
	ef C96V1_VERSIw(pVERr32_max, vfeebase_s
		T 1eOVSET _POT _AR_BA2c_pin, pONFpoNFIbit)_s_st4);

}

#def_isvoid 7_89vfeeOVset_haltIre#dert
mask(Pubuct vfee Vv.h" *vfee Vv)A{
	ef CvfeeOVconfignirq(vfee Vv,cBIT(31), BIT(8), idx) (0_IRQ_SET);
}

enil
#devfeeOVde "halt(Pubuct vfee Vv.h" *vfee Vv,
	uint32_t blockONFBA{
	inilrc = A;
	enum ef Cvfeeinput_src i;
	uint32_t val = A;

	val = ef C96V1_VERSIr(vfee Vv->vfeevbifebase_s_T 1eOVVBIF_CLK_OFFSET);
	val |= 0x1;
	ef C96V1_VERSIw(val, vfee Vv->vfeevbifebase_s_T 1eOVVBIF_CLK_OFFSET);

	/* Keep only halt Vnd reset mask ude	ef CvfeeOVset_haltIre#dert
mask(vfee Vv);

	/*Clear IRQ Sdefus0, only leave reset irq maskude	ef C96V1_VERSIw(0x7FFFFFFF, vfee Vv->vfeebase_s_st64);

	/*Clear IRQ Sdefus1, only leave halt irq maskude	ef C96V1_VERSIw(0xFFFFFEFF, vfee Vv->vfeebase_s_st68);

	/*push clear cmdude	ef C96V1_VERSIw(0x1, vfee Vv->vfeebase_s_st58);


	if (atomicVread(&vfee Vv->error_info.overflowesdefeBA		== s i:FLOW_DETECTED)
		pr_err_refelimited("%s: T 1%d halt cludrecovery, blockONF %d\n",
			_Efunc__, vfee Vv->p Vv->id, blockONFB;

	if (blockONF) {
		init_completion(&vfee Vv->haltIcomplete);
		/* Halt AXI Bus Bridge ude		ef C96V1_VERSIw_mb(0x1, vfee Vv->vfeebase_s_st400);
		rc = wait_clu_completion
timeout(
			&vfee Vv->haltIcomplete, 7_ecs_to_jiffies(500)B;
		if (rc <= 0)
			pr_err("%s:T 1%d halt timeout rc=%d\n", _Efunc__,
				vfee Vv->p Vv->id, rc);

	} else {
		/* Halt AXI Bus Bridge ude		ef C96V1_VERSIw_mb(0x1, vfee Vv->vfeebase_s_st400);
	}

	clud(i = T 1_PIX_0; i <= T 1_RAW_2; i++) {
		/* if any Pubeam is waitONF cludmpURPe, signal complete ude		if (vfee Vv->dxi_URPa.subeam
mpURPe[i]) {
			 (0_IZE_"%s: 9omplete Pubeam mpURPe\n", _Efunc__);
			ef CDBG
dxi_Pubeam
mpURPe(vfee Vv, i);
			if (vfee Vv->dxi_URPa.subeam
mpURPe[i])
				ef CDBG
dxi_Pubeam
mpURPe(vfee Vv, i);
		}
		if (atomicVread(&vfee Vv->dxi_URPa.de "cfg
mpURPe[i])) {
			 (0_IZE_"%s: 9omplete SE Vxitconfig mpURPe\n",
				_Efunc__);
			ef CDBG
dxi_cfg
mpURPe(vfee Vv, i);
			if (atomicVread(&vfee Vv->dxi_URPa.de "cfg
mpURPe[i]))
				ef CDBG
dxi_cfg
mpURPe(vfee Vv, i);
		}
	}

	if (atomicVread(&vfee Vv->sdefs_URPa.suefs_mpURPe)) {
		 (0_IZE_"%s: 9omplete SE suefs mpURPe\n", _Efunc__);
		ef CDBG
suefs_Pubeam
mpURPe(vfee VvB;
		if (atomicVread(&vfee Vv->sdefs_URPa.suefs_mpURPe))
			ef CDBG
suefs_Pubeam
mpURPe(vfee VvB;
	}

	return rc;
}

int 7_89vfeeOVdxi_re#dert(Pubuct vfee Vv.h" *vfee Vv,
	uint32_t blockONF,suint32_t enable_96Vif)A{
	ef C96V1_VERSIw(0x7FFFFFFF, vfee Vv->vfeebase_s_st64);
	ef C96V1_VERSIw(0xFFFFFEFF, vfee Vv->vfeebase_s_st68);
	ef C96V1_VERSIw(0x1, vfee Vv->vfeebase_s_st58);

	/* Sdert AXI ude	ef C96V1_VERSIw(0xf,cvfee Vv->vfeebase_s_st400);

	eemset(&vfee Vv->error_info, 0, sizeof(vfee Vv->error_info)B;
	atomicVset(&vfee Vv->error_info.overflowesdefe, NO_s i:FLOW);

	/* reset the irq masks without caVif violRPOSE Vnd errors ude	ef CvfeeOVconfignirq(vfee Vv,cvfee Vv->recoverynirq0_mask,
		vfee Vv->recoverynirq1_mask, idx) (0_IRQ_SET);

	vfee Vv->hwVinfo->vfeeVFE4coreeVFE4re*_mpURPe(vfee Vv, T 1_SRCDMAXB;

	if (enable_96Vif) {
		vfee Vv->hwVinfo->vfeeVFE4coreeVFE4
		upURPeec6Vif
#defe(vfee Vv, ENABLE_CAMIF);
	}

	return 0;
}

uint32_t ef CvfeeOVget_2c_mask(
	uint32_t irq
#defus0, uint32_t irq
#defus1)A{
	return (irq
#defus0 >> 8) &_0x7F;
}

uint32_t ef CvfeeOVget_comp_mask(
	uint32_t irq
#defus0, uint32_t irq
#defus1)A{
	return (irq
#defus0 >> 25)_&_0xF;
}

uint32_t ef CvfeeOVget_pONFpoNFI#defus(
	Pubuct vfee Vv.h" *vfee Vv)
{
	return ef C96V1_VERSIr(vfee Vv->vfeebase_s_st338);
}

enil
#devfeeOVget_suefs_pin(enum ef CDBG
suefs_type suefs_type)
{
	/*pin use_clu compositB, needlto mapito irq #defusude	sinuch (Puefs_type) {
	case idx) (0_E)
#S_HDR_BE:
		return E)
#S_COMP_IDX_HDR_BE;
	case idx) (0_E)
#S_BG:
		return E)
#S_COMP_IDX_BG;
	case idx) (0_E)
#S_BF:
		return E)
#S_COMP_IDX_BF;
	case idx) (0_E)
#S_HDR_BHIST:
		return E)
#S_COMP_IDX_HDR_BHIST;
	case idx) (0_E)
#S_RS:
		return E)
#S_COMP_IDX_RS;
	case idx) (0_E)
#S_CS:
		return E)
#S_COMP_IDX_CS;
	case idx) (0_E)
#S_IHIST:
		return E)
#S_COMP_IDX_IHIST;
	case idx) (0_E)
#S_BHIST:
		return E)
#S_COMP_IDX_BHIST;
	case idx) (0_E)
#S_AEC_BG:
		return E)
#S_COMP_IDX_AEC_BG;
	default:
		pr_err("%s: Invalid Puefs type\n", _Efunc__);
		return -EINVAL;
	}
}

enil
#devfeeOV#defs_check_Pubeams(
	Pubuct ef Cvfeesuefs_Pubeam *Pubeam
info)A{
	return 0;
}

void 7_89vfeeOVsdefs_cfg
9omp_mask(
	Pubuct vfee Vv.h" *vfee Vv, uint32_t sdefs_mask,
	uint8_t reque#d
9omp_index, uint8_t enable)A{
	uint32_t comp_maskIre*;
	atomicVt *Puefs_comp_mask;
	subuct TATSvfeesuefs_Phared_URPa *sdefs_URPa = &vfee Vv->sdefs_URPa;

	if (vfee Vv->hwVinfo->sdefs_hwVinfo->num_Puefs_comp_mask < 1)
		return;

	if (reque#d
9omp_index >= MAX_NUM_E)
#S_COMP_MASK) {
		pr_err("%s: num of comp masks %d exceedlmax %d\n",
			_Efunc__, reque#d
9omp_index,
			MAX_NUM_E)
#S_COMP_MASK);
		return;
	}

	if (vfee Vv->hwVinfo->sdefs_hwVinfo->num_Puefs_comp_mask >
			MAX_NUM_E)
#S_COMP_MASK) {
		pr_err("%s: num of comp masks %d exceedlmax %d\n",
			_Efunc__,
			vfee Vv->hwVinfo->sdefs_hwVinfo->num_Puefs_comp_mask,
			MAX_NUM_E)
#S_COMP_MASK);
		return;
	}

	sdefs_mask = Puefs_mask &_0x1FF;

	suefs_comp_mask = &sdefs_URPa->sdefs_comp_mask[reque#d
9omp_index];
	comp_maskIre* = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st78B;

	if (enable) {
		comp_maskIre* |= Puefs_mask << (reque#d
9omp_index * 16);
		atomicVset(Puefs_comp_mask, Puefs_mask |
				atomicVread(Puefs_comp_mask));
		7_89vfeeOVconfignirq(vfee Vv,c1 << (29_s_reque#d
9omp_index),
				0, idx) (0_IRQ_ENABLE);
	} else {
		if (!(atomicVread(Puefs_comp_mask) &_Puefs_mask))
			return;

		atomicVset(Puefs_comp_mask,
				~Puefs_mask &_atomicVread(Puefs_comp_mask));
		comp_maskIre* &= ~(Puefs_mask << (reque#d
9omp_index * 16));
		7_89vfeeOVconfignirq(vfee Vv,c1 << (29_s_reque#d
9omp_index),
				0, idx) (0_IRQ_DISABLE);
	}

	ef C96V1_VERSIw(comp_maskIre*,cvfee Vv->vfeebase_s_st78B;

	 (0_IZE_"%s: 9omp_maskIre*: %x comp mask0 %x mask1: %x\n",
		_Efunc__, comp_maskIre*,
		atomicVread(&sdefs_URPa->sdefs_comp_mask[0]),
		atomicVread(&sdefs_URPa->sdefs_comp_mask[1]));

	return;
}

void 7_89vfeeOVsdefs_cfg
2c_prq
mask(
	Pubuct vfee Vv.h" *vfee Vv,
	Pubuct ef Cvfeesuefs_Pubeam *Pubeam
info)A{
	sinuch (E)
#S_IDX(Pubeam
info->Pubeam
h6

le)) {
	case E)
#S_COMP_IDX_AEC_BG:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 15, 0, idx) (0_IRQ_ENABLE);
		break;
	case E)
#S_COMP_IDX_HDR_BE:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 16, 0, idx) (0_IRQ_ENABLE);
		break;
	case E)
#S_COMP_IDX_BG:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 17, 0, idx) (0_IRQ_ENABLE);
		break;
	case E)
#S_COMP_IDX_BF:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 18,c1 << 26,
				idx) (0_IRQ_ENABLE);
		break;
	case E)
#S_COMP_IDX_HDR_BHIST:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 19, 0, idx) (0_IRQ_ENABLE);
		break;
	case E)
#S_COMP_IDX_RS:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 20, 0, idx) (0_IRQ_ENABLE);
		break;
	case E)
#S_COMP_IDX_CS:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 21, 0, idx) (0_IRQ_ENABLE);
		break;
	case E)
#S_COMP_IDX_IHIST:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 22, 0, idx) (0_IRQ_ENABLE);
		break;
	case E)
#S_COMP_IDX_BHIST:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 23, 0, idx) (0_IRQ_ENABLE);
		break;
	default:
		pr_err("%s: Invalid Puefs pin %d\n", _Efunc__,
			E)
#S_IDX(Pubeam
info->Pubeam
h6

le));
	}
}

void 7_89vfeeOVsdefs_clear
2c_prq
mask(
	Pubuct vfee Vv.h" *vfee Vv,
	Pubuct ef Cvfeesuefs_Pubeam *Pubeam
info)A{
	uint32_t irq
mask, irq
mask_1;

	irq
mask = vfee Vv->irq0_mask;
	irq
mask_1 = vfee Vv->irq1_mask;

	sinuch (E)
#S_IDX(Pubeam
info->Pubeam
h6

le)) {
	case E)
#S_COMP_IDX_AEC_BG:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 15, 0, idx) (0_IRQ_DISABLE);
		break;
	case E)
#S_COMP_IDX_HDR_BE:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 16, 0, idx) (0_IRQ_DISABLE);
		break;
	case E)
#S_COMP_IDX_BG:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 17, 0, idx) (0_IRQ_DISABLE);
		break;
	case E)
#S_COMP_IDX_BF:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 18,c1 << 26,
				idx) (0_IRQ_DISABLE);
		break;
	case E)
#S_COMP_IDX_HDR_BHIST:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 19, 0, idx) (0_IRQ_DISABLE);
		break;
	case E)
#S_COMP_IDX_RS:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 20, 0, idx) (0_IRQ_DISABLE);
		break;
	case E)
#S_COMP_IDX_CS:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 21, 0, idx) (0_IRQ_DISABLE);
		break;
	case E)
#S_COMP_IDX_IHIST:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 22, 0, idx) (0_IRQ_DISABLE);
		break;
	case E)
#S_COMP_IDX_BHIST:
		7_89vfeeOVconfignirq(vfee Vv,c1 << 23, 0, idx) (0_IRQ_DISABLE);
		break;
	default:
		pr_err("%s: Invalid Puefs pin %d\n", _Efunc__,
			E)
#S_IDX(Pubeam
info->Pubeam
h6

le));
	}
}

void 7_89vfeeOVsdefs_cfg
2c_re*(
	Pubuct vfee Vv.h" *vfee Vv,
	Pubuct TATSvfeesuefs_Pubeam *Pubeam
info)A{
	enilsuefs_pin = E)
#S_IDX(Pubeam
info->Pubeam
h6

le);
	uint32_t suefs_base_=_T 1eOVE)
#S_BR_BAPuefs_pin);

	/* WR_ADDRXCFGlude	ef C96V1_VERSIw(Pubeam
info->fr6V1drop_period << 2,
		vfee Vv->vfeebase_s_suefs_base_*/
#10);
	/* WR_IRQ_FRAMEDROP_PATTERN ude	ef C96V1_VERSIw(Pubeam
info->fr6V1drop_pattern,
		vfee Vv->vfeebase_s_suefs_base_*/
#18);
	/* WR_IRQ_SUBSAMPLE_PATTERN ude	ef C96V1_VERSIw(0xFFFFFFFF,
		vfee Vv->vfeebase_s_suefs_base_*/
#1C);
}

void 7_89vfeeOVsdefs_clear
2c_re*(
	Pubuct vfee Vv.h" *vfee Vv,
	Pubuct TATSvfeesuefs_Pubeam *Pubeam
info)A{
	uint32_t val = 0;
	enilsuefs_pin = E)
#S_IDX(Pubeam
info->Pubeam
h6

le);
	uint32_t suefs_base_=_T 1eOVE)
#S_BR_BAPuefs_pin);

	/* WR_ADDRXCFGlude	ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_suefs_base_*/
#10);
	/* WR_IRQ_FRAMEDROP_PATTERN ude	ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_suefs_base_*/
#18);
	/* WR_IRQ_SUBSAMPLE_PATTERN ude	ef C96V1_VERSIw(val, vfee Vv->vfeebase_s_suefs_base_*/
#1C);
}

void 7_89vfeeOVsdefs_cfg
mb(Pubuct vfee Vv.h" *vfee Vv)
{
	inili;
	uint32_t mb_offsetd= A;
	uint32_t mb_size[T 1eOVNUM_E)
#S_TYPE]d= {
		16, /* idx) (0_E)
#S_HDR_BE ude		16, /* idx) (0_E)
#S_BG ude		16, /* idx) (0_E)
#S_BF ude		16, /* idx) (0_E)
#S_HDR_BHIST ude		16, /* idx) (0_E)
#S_RS ude		16, /* idx) (0_E)
#S_CS ude		16, /* idx) (0_E)
#S_IHIST ude		16, /* idx) (0_E)
#S_BHIST ude		16, /* idx) (0_E)
#S_AEC_BG ude	};
	if (vfee Vv->p Vv->id ==  (0_T 11)
		mb_offsetd= T 1eOVUB_SIZE_T 11;
	else if (vfee Vv->p Vv->id ==  (0_T 10)
		mb_offsetd= T 1eOVUB_SIZE_T 10;
	elsed		pr_err("%s: incorrect T 1  Vv.h"\n", _Efunc__);

	clud(i = 0; i < T 1eOVNUM_E)
#S_TYPE; i++) {
		mb_offsetd-= mb_size[i];
		ef C96V1_VERSIw(T 1eOVE)
#S_B#FE0 - V << 30 |
			mb_offsetd<< 16 | (mb_size[i]q- 1),
			vfee Vv->vfeebase_s_T 1eOVE)
#S_BR_BAi)_*/
#14);
	}
}

void 7_89vfeeOVsdefs_mpURPe_cgc_overridU(Pubuct vfee Vv.h" *vfee Vv,
	uint32_t sdefs_mask, uint8_t enable)A{
	inili;
	uint32_t moduleecfg, cgc_mask = 0;

	clud(i = 0; i < T 1eOVNUM_E)
#S_TYPE; i++) {
		if ((Puefs_mask >> i)_&_0x1) {
			sinuch (iB {
			case E)
#S_COMP_IDX_HDR_BE:
				cgc_mask |= 1;
				break;
			case E)
#S_COMP_IDX_BG:
				cgc_mask |= irq<< 3);
				break;
			case E)
#S_COMP_IDX_BHIST:
				cgc_mask |= irq<< 4);
				break;
			case E)
#S_COMP_IDX_RS:
				cgc_mask |= irq<< 5);
				break;
			case E)
#S_COMP_IDX_CS:
				cgc_mask |= irq<< 6);
				break;
			case E)
#S_COMP_IDX_IHIST:
				cgc_mask |= irq<< 7);
				break;
			case E)
#S_COMP_IDX_AEC_BG:
				cgc_mask |= irq<< 8);
				break;
			case E)
#S_COMP_IDX_BF:
				cgc_mask |= irq<< 2);
				break;
			case E)
#S_COMP_IDX_HDR_BHIST:
				cgc_mask |= irq<< 1);
				break;
			default:
				pr_err("%s: Invalid Puefs mask\n", _Efunc__);
				return;
			}
		}
	}

	/* CGC overridU: encluh" BAF_clu DMI ude	eoduleecfg = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st30);
	if (enable)A		eoduleecfg |= 9gc_mask;
	elsed		eoduleecfg &= ~9gc_mask;
	ef C96V1_VERSIw(moduleecfg, vfee Vv->vfeebase_s_st30);
}

bool 7_89vfeeOVis_moduleecfg_lock_needed(
	uint32_t re*_offset)A{
	return false;
}

void 7_89vfeeOVsdefs_enable_module(Pubuct vfee Vv.h" *vfee Vv,
	uint32_t sdefs_mask, uint8_t enable)A{
	inili;
	uint32_t moduleecfg, moduleecfg_mask = 0;

	/* BF Puefs pnvolve DMI cfg, ignoreude	clud(i = 0; i < T 1eOVNUM_E)
#S_TYPE; i++) {
		if ((Puefs_mask >> i)_&_0x1) {
			sinuch (iB {
			case E)
#S_COMP_IDX_HDR_BE:
				moduleecfg_mask |= 1;
				break;
			case E)
#S_COMP_IDX_HDR_BHIST:
				moduleecfg_mask |= 1q<< 1;
				break;
			case E)
#S_COMP_IDX_BF:
				moduleecfg_mask |= 1q<< 2;
				break;
			case E)
#S_COMP_IDX_BG:
				moduleecfg_mask |= 1q<< 3;
				break;
			case E)
#S_COMP_IDX_BHIST:
				moduleecfg_mask |= 1q<< 4;
				break;
			case E)
#S_COMP_IDX_RS:
				moduleecfg_mask |= 1q<< 5;
				break;
			case E)
#S_COMP_IDX_CS:
				moduleecfg_mask |= 1q<< 6;
				break;
			case E)
#S_COMP_IDX_IHIST:
				moduleecfg_mask |= 1q<< 7;
				break;
			case E)
#S_COMP_IDX_AEC_BG:
				moduleecfg_mask |= 1q<< 8;
				break;
			default:
				pr_err("%s: Invalid Puefs mask\n", _Efunc__);
				return;
			}
		}
	}

	eoduleecfg = ef C96V1_VERSIr(vfee Vv->vfeebase_s_st44);
	if (enable)A		eoduleecfg |= moduleecfg_mask;
	elsed		eoduleecfg &= ~moduleecfg_mask;

	ef C96V1_VERSIw(moduleecfg, vfee Vv->vfeebase_s_st44);
	/*tenable wm if needed ude	if (vfee Vv->hwVinfo->vfeeVFE4Puefs_VFE4enable_Puefs_wm)
		vfee Vv->hwVinfo->vfeeVFE4Puefs_VFE4enable_Puefs_wm(vfee Vv,
						sdefs_mask, enable);
}

void 7_89vfeeOVsdefs_mpURPe_pONFIpoNFIVERr(
	void _ERSmem *vfeebase, Pubuct TATSvfeesuefs_Pubeam *Pubeam
info,
	uint32_t pONFpoNFI#defus, dmaIVERr_t pVERr)A{
	uint32_t pVERr32d= (pVERr_&_0xFFFFFFFF);
	enilsuefs_pin = E)
#S_IDX(Pubeam
info->Pubeam
h6

le);

	ef C96V1_VERSIw(pVERr32, vfeebase_s
		T 1eOVE)
#S_SET _POT _AR_BAsuefs_pin, pONFpoNFI#defus));
}

uint32_t ef CvfeeOVsuefs_get_2c_mask(
	uint32_t irq
#defus0, uint32_t irq
#defus1)A{
	/*tTODO:  VfE_B  bf early done irq in #defus_0 Vnd
		bf pONFpoNF done in  #defus_1ude	uint32_t comp_mapped_irq
mask = 0;
	enili = 0;

	/*
	* remove early done Vnd h6

le seperefely,
	* VER bf pin on #defus 1
	*/
	irq
#defus0 &= ~(1 << 18);

	clud(i = 0; i < T 1eOVNUM_E)
#S_TYPE; i++)
		if ((irq
#defus0 >> suefs_prq
map_comp_mask[i]) &_0x1)
			comp_mapped_irq
mask |= irq<< i);
	if ((irq
#defus1 >> 26) &_0x1)
		comp_mapped_irq
mask |= irq<< E)
#S_COMP_IDX_BF);
