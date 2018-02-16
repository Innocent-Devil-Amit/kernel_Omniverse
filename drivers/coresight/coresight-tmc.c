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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_coresight.h>
#include <linux/coresight.h>
#include <linux/coresight-cti.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/usb/usb_qdss.h>
#include <linux/dma-mapping.h>
#include <linux/msm-sps.h>
#include <linux/usb_bam.h>
#include <asm/cacheflush.h>
#include <soc/qcom/memory_dump.h>
#include <soc/qcom/jtag.h>

#include "coresight-priv.h"

#define tmc_writel(drvdata, val, off)	__raw_writel((val), drvdata->base + off)
#define tmc_readl(drvdata, off)		__raw_readl(drvdata->base + off)

#define tmc_readl_no_log(drvdata, off)	__raw_readl_no_log(drvdata->base + off)

#define TMC_LOCK(drvdata)						\
do {									\
	mb();								\
	tmc_writel(drvdata, 0x0, CORESIGHT_LAR);			\
} while (0)
#define TMC_UNLOCK(drvdata)						\
do {									\
	tmc_writel(drvdata, CORESIGHT_UNLOCK, CORESIGHT_LAR);		\
	mb();								\
} while (0)

#define TMC_RSZ				(0x004)
#define TMC_STS				(0x00C)
#define TMC_RRD				(0x010)
#define TMC_RRP				(0x014)
#define TMC_RWP				(0x018)
#define TMC_TRG				(0x01C)
#define TMC_CTL				(0x020)
#define TMC_RWD				(0x024)
#define TMC_MODE			(0x028)
#define TMC_LBUFLEVEL			(0x02C)
#define TMC_CBUFLEVEL			(0x030)
#define TMC_BUFWM			(0x034)
#define TMC_RRPHI			(0x038)
#define TMC_RWPHI			(0x03C)
#define TMC_AXICTL			(0x110)
#define TMC_DBALO			(0x118)
#define TMC_DBAHI			(0x11C)
#define TMC_FFSR			(0x300)
#define TMC_FFCR			(0x304)
#define TMC_PSCR			(0x308)
#define TMC_ITMISCOP0			(0xEE0)
#define TMC_ITTRFLIN			(0xEE8)
#define TMC_ITATBDATA0			(0xEEC)
#define TMC_ITATBCTR2			(0xEF0)
#define TMC_ITATBCTR1			(0xEF4)
#define TMC_ITATBCTR0			(0xEF8)

#define BYTES_PER_WORD			4
#define TMC_ETR_BAM_PIPE_INDEX		0
#define TMC_ETR_BAM_NR_PIPES		2

#define TMC_ETFETB_DUMP_MAGIC_OFF	(0)
#define TMC_ETFETB_DUMP_MAGIC		(0x5D1DB1BF)
#define TMC_ETFETB_DUMP_MAGIC_V2	(0x42445953)
#define TMC_ETFETB_DUMP_VER_OFF		(4)
#define TMC_ETFETB_DUMP_VER		(1)
#define TMC_REG_DUMP_MAGIC_OFF		(0)
#define TMC_REG_DUMP_MAGIC		(0x5D1DB1BF)
#define TMC_REG_DUMP_MAGIC_V2		(0x42445953)
#define TMC_REG_DUMP_VER_OFF		(4)
#define TMC_REG_DUMP_VER		(1)

#define TMC_ETR_SG_ENT_TO_BLK(phys_pte)	(((phys_addr_t)phys_pte >> 4)	\
					 << PAGE_SHIFT);
#define TMC_ETR_SG_ENT(phys_pte)	(((phys_pte >> PAGE_SHIFT) << 4) | 0x2);
#define TMC_ETR_SG_NXT_TBL(phys_pte)	(((phys_pte >> PAGE_SHIFT) << 4) | 0x3);
#define TMC_ETR_SG_LST_ENT(phys_pte)	(((phys_pte >> PAGE_SHIFT) << 4) | 0x1);

enum tmc_config_type {
	TMC_CONFIG_TYPE_ETB,
	TMC_CONFIG_TYPE_ETR,
	TMC_CONFIG_TYPE_ETF,
};

enum tmc_mode {
	TMC_MODE_CIRCULAR_BUFFER,
	TMC_MODE_SOFTWARE_FIFO,
	TMC_MODE_HARDWARE_FIFO,
};

enum tmc_etr_out_mode {
	TMC_ETR_OUT_MODE_NONE,
	TMC_ETR_OUT_MODE_MEM,
	TMC_ETR_OUT_MODE_USB,
};

static const char * const str_tmc_etr_out_mode[] = {
	[TMC_ETR_OUT_MODE_NONE]	= "none",
	[TMC_ETR_OUT_MODE_MEM]		= "mem",
	[TMC_ETR_OUT_MODE_USB]		= "usb",
};

enum tmc_etr_mem_type {
	TMC_ETR_MEM_TYPE_CONTIG,
	TMC_ETR_MEM_TYPE_SG,
};

static const char * const str_tmc_etr_mem_type[] = {
	[TMC_ETR_MEM_TYPE_CONTIG]	= "contig",
	[TMC_ETR_MEM_TYPE_SG]		= "sg",
};

enum tmc_mem_intf_width {
	TMC_MEM_INTF_WIDTH_32BITS	= 0x2,
	TMC_MEM_INTF_WIDTH_64BITS	= 0x3,
	TMC_MEM_INTF_WIDTH_128BITS	= 0x4,
	TMC_MEM_INTF_WIDTH_256BITS	= 0x5,
};

struct tmc_etr_bam_data {
	struct sps_bam_props	props;
	unsigned long		handle;
	struct sps_pipe		*pipe;
	struct sps_connect	connect;
	uint32_ONTIG,
	TMC_ETR_MEM_TYP] = ETM_ADDR_TYPE_RANGE;
	drvdata->addpdule.h>
#indxps_connect	connectdesdrvdata->addpddesd.h>
#indxps_ = ETM_ADDRINTFbufferddesc_fifops_ = ETM_ADDRINTFbufferddstr_fifops_bool_ptenabint3unsigned long		\
} whiuct void __ioINTR_M
	mbps_ = ETM_
#inclR_M
#ips_ = ETM_aw_writellude <l	*cs
#ips_ = ETM_b.h>
#incl	b.h>
#ips_ = ETM_a
#i		byte_cnand
#ips_ = ETM_alassR_M
yte_cnandalassps_ = ETM_alkR_Malkps_ lude <lddpdulude <lps_ = ETM_aw_writelllud_Mati_m/jtaps_ = ETM_aw_writelllud_Mati_tribtps_ = ETM_butex						_e <lps_ta-							_countps_bool_pt				
#ips_bool_ptabort
#ips_ONTI_pt*regFbufps_ = ETM_bsmv.h"

	str	regF	strps_ONTI_pt*bufps_ = ETM_bsmv.h"

	str	bufF	strps_b_bTR_SG_E		pR_SG;t void __ioINTR_MvR_SG;t ata->addpduizeps_ = ETM_butex		morye <lps_ = ETM_sm-sps.h_ch	*sm-caps_ = ETM_ng		handle;
	str_M
	m	strps__MODE_MEM,
	TMC_ETR_O	MC_ETR_Ops_bool_ptenabin_todle;ps_bool_ptenabint3	E_ETR,
	TMC_CONFIG_T	MC_CONFIG_T;t ata->addpdtrigger_cnanps_ta-			
yte_cnandirqps_atomicddpd
yte_cnandirq_cna;t ata->addpd
yte_cnand__rueps_ = ETM_butex		
yte_cnand				_e <lps_ = ETM_butex		
yte_cnande <lps_ata->addpd
yte_cnandbe <lduizeps_bool_pt
yte_cnandoverflowps_bool_pt
yte_cnandptribntps_bool_pt
yte_cnandenabint3	ata->addpd
yte_cnandoverflow_cna;t bool_pt
yte_cnand				_aativnt3	>
#i_queue_h			_t	wqps_ONTI_pt*byte_cnandnR_Ops_ata->addpdINTFuizeps_bool_ptsMEMkydenabint3	bool_ptforce_regF	h"
t3	bool_pt.h"

regps_bool_ptsg_enabint3	E_ETR,
	TTMC_ETR_MEM_	ETR_MEM_t3	E_ETR,
	TTMC_ETR_MEM_	ETRIG_T;t ata->addpddeltr_bottomps_ta-			sg_blk__ETps_bool_ptnotifyps_ = ETM_notifiendbe <l	data_save_blk;TMC_ETR_MEM_void __ase + gF	h"
(gned long		\
} whiu*T_UNLOCK;ETR_MEM_void ();		
#i_for_m/jta(gned long		\
} whiu*T_UNLOCK
{s_ta- countps
	/* Ensure_no m/jtaITHOin * butesscludtfor (countPE_TIME_MEMUS; BVAL(define tmc_readl_noCR			(0x), 0) !rophys_p&& count > 0; count--)hysu#incl(G_TY	E,
N(countPErop, "timeSS F00C)
#	
#i
#ilinuxCR	 m/jtanoCR			(0x: %#x\nth {     define tmc_readl_noCR			(0x)_TY}
ETR_MEM_void ();		
#i_for_ne ty(gned long		\
} whiu*T_UNLOCK
{s_ta- countps
	/* Ensure_#incatter, un#incatterndat NTIdpe thfifo e themptycludtfor (countPE_TIME_MEMUS; BVAL(define tmc_readl_noCR		STS), 2) !ro1hys_p&& count > 0; count--)hysu#incl(G_TY	E,
N(countPErop, "timeSS F00C)
#	
#i
#ilinuxCR	 ne tynoCR		STS: %#x\nth {     define tmc_readl_noCR		STS)_TY}
ETR_MEM_void ();	m/jta_dat_sto
(gned long		\
} whiu*T_UNLOCK
{s_ta- countps ata->add ffcrps
	ffcrPE_define tmc_readl_noCR			(CR_TY	ffcrP|= ops(1_SHIb();								\
} whileffcrnoCR			(CR_TY	ffcrP|= ops(6SHIb();								\
} whileffcrnoCR			(CR_TY	/* Ensure_#/jtaIcompletescludtfor (countPE_TIME_MEMUS; BVAL(define tmc_readl_noCR			(Cx), 6) !rophys_p&& count > 0; count--)hysu#incl(G_TY	E,
N(countPErop, "timeSS F00C)
##/jta
#ilCR	noCR			(Cx: %#x\nth {     define tmc_readl_noCR			(Cx)_TYIb();		
#i_for_ne ty(T_UNLOCK;E}
ETR_MEM_ta- ();	m/jta_on_powerdown( = ETM_notifiendbe <l *this,hys_p  connect	conneFOR At,_void *ptrK
{s_gned long		\
} whiu*T_UNLOCPE_tf_wa<< ndof(this, gned long		\
} whi,hys_p_p   data_save_blk);s_ta- countps ata-8_ontiopbitps_connect	conne##/agsps ata->add ffcrps
	uludde <ldirqsave(&								\ulude <l,##/ags_TY	/* { * Curr AtTICULem At will perfovers#/jtaIoper will ll allxCR	 
#incls { *  evene thenabind irr speativnsion 2 acurr AtTsink. { *udtif (!								\enabin)hysgoto SS ;
	ffcrPE_define tmc_readl_noCR			(CR_TY	tiopbitPE_BVAL(ffcrno1_SHIb/* Do_notntiop trace ll #/jtaIludtffcrPE_ffcrP& ~ops(1_SHIb();								\
} whileffcrnoCR			(CR_TY	/* x/kernte manual #/jtaIludtffcrPE_ffcrP| ops(6SHIb();								\
} whileffcrnoCR			(CR_TY	/* Ensure_#/jtaIcompletescludtfor (countPE_TIME_MEMUS; BVAL(define tmc_readl_noCR			(Cx), 6) !rophys_p&& count > 0; count--)hysu#incl(G_TY	if (countPErop)hyspr		
rn_rntelim			d("timeSS F#/jta
#ilCR	noCR			(Cx: %#x\nth {p_p    define tmc_readl_noCR			(Cx)_TY	/* Resdmodutiop trace ll #/jtaIbitPludtffcrPE_ffcrP| (tiopbitP<< 1_SHIb();								\
} whileffcrnoCR			(CR_TYSS :
	uluddune <ldirqresdmod(&								\ulude <l,##/ags_TY	return NOTIFY_DONETY}
ETR_MEM_void __ase enabin(gned long		\
} whiu*T_UNLOCK
{s_fine TMC_UNLOCK(drvdat1,ODE			(0_TY}
ETR_MEM_void __ase disabin(gned long		\
} whiu*T_UNLOCK
{s_fine TMC_UNLOCK(drvdat0,ODE			(0_TY}
ETR_MEM_void ,
	TTMC_sg_tbl_nder(ata->add MvR_SG, uta->add uize, uta->add  AtsK
{s_uta->add i rop, pte_n rop, lastMC_Cps ata->add *virt_st_tbl, *virt_C_Cps void *virt_blk;T	MC_ETR_SG_E {
	TMC_C;s_ta- total_ Ats roDIV_ROUND_UP(uize, G_TYPEIZE);s_ta-  Ats_pendbek roG_TYPEIZE/uizeof(ata->add_TYIbvirt_st_tbl rovR_SG;t3	>0C)
#di < total_ Ats)uct 	lastMC_C ro(di +  Ats_pendbek) > total_ Ats)u?
p_p   total_ Ats : di +  Ats_pendbek);s_	>0C)
#di < lastMC_C)uct 		virt_C_C rovirt_st_tbl + pte_n;t3		b/* Do_notngo beyoat _ETbernion AtriescallocaARRAludt		if (iPEro Ats)uct 	ptfder_page((connect	conne)virt_st_tbl);s_			return;s_		}t3		b{
	TMC_CO=4)	\
					 << PAGE_SHIF*virt_C_C);s_		virt_blkO=4{
	TMtodvirtc_config_t;t3		bif ((lastMC_C - i) > 1)uct 	ptfder_page((connect	conne)virt_bek);s_		b{te_n++;s_		} else if (lastMC_C r= total_ Ats)uct 	ptfder_page((connect	conne)virt_bek);s_		bfder_page((connect	conne)virt_st_tbl);s_		} else ct 	ptfder_page((connect	conne)virt_st_tbl);s_			virt_st_tbl ro(ata->add M)virt_blk;T			b{te_n rop;T			bbne k;s_		}t		bi++;s_	}t	}Y}
ETR_MEM_void ,
	TTMC_sg_tbl_n/jta(ata->add MvR_SG, uta->add uizeK
{s_uta->add i rop, pte_n rop, lastMC_Cps ata->add *virt_st_tbl, *virt_C_Cps void *virt_blk;T	MC_ETR_SG_E {
	TMC_C;s_ta- total_ Ats roDIV_ROUND_UP(uize, G_TYPEIZE);s_ta-  Ats_pendbek roG_TYPEIZE/uizeof(ata->add_TYIbvirt_st_tbl rovR_SG;t_b_b;	m/jta_range((void *)virt_st_tbl, (void *)virt_st_tbl + G_TYPEIZE);s3	>0C)
#di < total_ Ats)uct 	lastMC_C ro(di +  Ats_pendbek) > total_ Ats)u?
p_p   total_ Ats : di +  Ats_pendbek);s_	>0C)
#di < lastMC_C)uct 		virt_C_C rovirt_st_tbl + pte_n;t		b{
	TMC_CO=4)	\
					 << PAGE_SHIF*virt_C_C);s_		virt_blkO=4{
	TMtodvirtc_config_t;t3		bb_b;	m/jta_range(virt_blk, virt_blkO+ G_TYPEIZE);s3		bif ((lastMC_C - i) > 1)uct 	pt{te_n++;s_		} else if (lastMC_C != total_ Ats)uct 	ptvirt_st_tbl ro(ata->add M)virt_blk;T			b{te_n rop;T			bbne k;s_		}t		bi++;s_	}t	}Y}
E/ usefScatternga 2 r tabin laySS Fin ht-pri:usef1. Tabin tf_wa<<s 32-bitP Atriesusef2. EachP AtryANTY; witabin poiAts to 4K be <l ionht-priusef3. LastP AtryANTY; witabin poiAts to nextitabinusef4. (*) Bast	con ht-Fuize nequesded, if  2 reITHOno net	cfor nextilOR l See th   tabin, lastP AtryANTY; witabin poiAts direatly to 4K be <l ionht-pri be usep   sg_tbl__ET=0usep|---------------|<--_readl(drvvR_SGusep|   blk__ET=0   |usep|---------------|usep|   blk__ET=1   |usep|---------------|usep|   blk__ET=2   |usep|---------------|p   sg_tbl__ET=1usep|(*)NxtiTbl A_SG|------>|---------------|usep|---------------|       |   blk__ET=3   |usepppp|---------------|usepppp|   blk__ET=4   |usepppp|---------------|usepppp|   blk__ET=5   |usepppp|---------------|p   sg_tbl__ET=2usepppp|(*)NxtiTbl A_SG|------>|---------------|usepppp|---------------|p|   blk__ET=6   |useppppppp|---------------|useppppppp|   blk__ET=7   |useppppppp|---------------|useppppppp|   blk__ET=8   |useppppppp|---------------|useppppppp|               |Eat See tppppppp|---------------|-----e tppppppp_p TabinusefFor sICULAcity abovnsdiaut WIassumescfollow
#i:usefa. ht-Fuize = 36KB --> total_ Ats ro9nty o.  Ats_pendbek ro4ncludeTR_MEM_ta- ();	TMC_sg_tbl_alloc(gned long		\
} whiu*T_UNLOCK
{s_ta- retps ata->add i rop, lastMC_Cps ata->add *virt_pgdir, *virt_st_tblps void *virt_C_C;s_ta- total_ Ats roDIV_ROUND_UP(								\uize, G_TYPEIZE);s_ta-  Ats_pendbek roG_TYPEIZE/uizeof(ata->add_TYIbvirt_pgdir ro(ata->add M)get_zeroed_page(GFP_KERNEL_TY	if (!virt_pgdir)hysreturn -ENOMEMTYIbvirt_st_tbl rovirt_pgdir;s3	>0C)
#di < total_ Ats)uct 	lastMC_C ro(di +  Ats_pendbek) > total_ Ats)u?
p_p   total_ Ats : di +  Ats_pendbek);s_	>0C)
#di < lastMC_C)uct 		virt_C_C ro(void *)get_zeroed_page(GFP_KERNEL_TY			if (!virt_p_C)uct 		sret ro-ENOMEMTY 		sgoto err;s_		}t3		bif ((lastMC_C - i) > 1)uct 	pt*virt_st_tbl r {p_p     0x2);
#define Tvirt_tod_con(virt_p_C));s_			virt_st_tbl++;s_		} else if (lastMC_C r= total_ Ats)uct 	pt*virt_st_tbl r {p_p     0x2);
#defienum tmcvirt_tod_con(virt_p_C));s_		} else ct 	pt*virt_st_tbl r {p_p     0x2);
#defidefine Tvirt_tod_con(virt_p_C));s_			virt_st_tbl ro(ata->add M)virt_C_C;s_		bbne k;s_		}t		bi++;s_	}t	}Yt_beadl(drvvR_SG rovirt_pgdir;s_beadl(drvpR_SG rovirt_tod_con(virt_pgdir)ps
	/* F/jtaI; widc/qco befmoduprocet	
#illudt,
	TTMC_sg_tbl_n/jta((ata->add M)beadl(drvvR_SGw_readl(drvuizeKps
	dev_db{									\dev, "%s:itabin TR_rts at %#lx, totaln Atriesc%d\nth {p__func__, (connect	conne)beadl(drvpR_SG, total_ Ats)ps
	return p;Terr:dt,
	TTMC_sg_tbl_nder(virt_pgdir, 								\uize, i_TY	return retps}
ETR_MEM_void ,
	TTMC_sg_ht-Ftribt(ata->add MvR_SG, uta->add uizeK
{s_uta->add i rop, pte_n rop, lastMC_Cps ata->add *virt_st_tbl, *virt_C_Cps void *virt_blk;T	MC_ETR_SG_E {
	TMC_C;s_ta- total_ Ats roDIV_ROUND_UP(uize, G_TYPEIZE);s_ta-  Ats_pendbek roG_TYPEIZE/uizeof(ata->add_TYIbvirt_st_tbl rovR_SG;t3	>0C)
#di < total_ Ats)uct 	lastMC_C ro(di +  Ats_pendbek) > total_ Ats)u?
p_p   total_ Ats : di +  Ats_pendbek);s_	>0C)
#di < lastMC_C)uct 		virt_C_C rovirt_st_tbl + pte_n;t		b{
	TMC_CO=4)	\
					 << PAGE_SHIF*virt_C_C);s_		virt_blkO=4{
	TMtodvirtc_config_t;t3		bif ((lastMC_C - i) > 1)uct 	ptht-ibt(virt_blk, 0, G_TYPEIZE);s_	pt{te_n++;s_		} else if (lastMC_C r= total_ Ats)uct 	ptht-ibt(virt_blk, 0, G_TYPEIZE);s_	p} else ct 	ptvirt_st_tbl ro(ata->add M)virt_blk;T			b{te_n rop;T			bbne k;s_		}t		bi++;s_	}t	}Y
	/* F/jtaI; widc/qco befmoduprocet	
#illudt,
	TTMC_sg_tbl_n/jta(vR_SG, uizeKps}
ETR_MEM_void ,
	TTMC_fill_mory_du
	str(gned long		\
} whiu*T_UNLOCK
{s_gned long		handle;
	struM
	m	str ro								\
	m	strps
	get_ps.h_le;
NGE;
	dion_info(&
	m	str	\desth {p_p    &
	m	str	\dest.h>
#indxh {p_p    &
	m	str	\ule.h>
#indxh {p_p    &
	m	str	\desc_fifoh {p_p    &
	m	str	\dstr_fifoh {p_p    NUL0_TY}
ETR_MEM_void __ase eandenabin_todle;(gned long		\
} whiu*T_UNLOCK
{s_gned long		handle;
	struM
	m	str ro								\
	m	strps ata->add axiatlps
	if (								\enabin_todle;)hysreturnps
	/* CC_CONure_dat enabin nequired CSR negisderscludtbsmvps.h_csndenabin_le;
todmor()ps
	/* CC_CONure_dat enabin 			cfor mor le; SS putPludata {
ORESIGHT_UNLOCKTYIb();		TMC_UNLOCK(drvd
	m	str	\dstr_fifo.uize /e TMC_ETR_BAM_Nh {p   0x2)RSZSHIb();								\
} while_HARDWARE_FIFO,
};

enum e_HARDWARKTYIbaxiatlPE_define tmc_readl_noCR		HI			(SHIbaxiatlP|ro(0xFP<< 8SHIb();								\
} whileaxiatlnoCR		HI			(SHIbaxiatlP&= ~)
#dP<< 7SHIb();								\
} whileaxiatlnoCR		HI			(SHIbaxiatlPro(axiatlP& ~GE_S PAGE_HIb();								\
} whileaxiatlnoCR		HI			(SHIIb();								\
} while(ata->add_
	m	str	\dstr_fifo._conf
	mb,_FFSR			(0SHIb();								\
} while(((ata-64dd_
	m	str	\dstr_fifo._conf
	mb)ONFI32) {p   &AGEFF,_FFSR			HI_TY	/* Set FOnFlIncfor peniodEM_#/jtaIludtfine TMC_UNLOCK(drvdat133noCR			(CR_TY	fine TMC_UNLOCK(drvd								\trigger_cnan,WD				(0_TY	__ase enabin(T_UNLOCKTYIb_writel(drvdata, ;Yt_beadl(drvenabin_todle;PE_drueps}
ETR_MEM_ta- ();	handle;
enabin(gned long		\
} whiu*T_UNLOCK
{s_gned long		handle;
	struM
	m	str ro								\
	m	strps ta- retps
	if (
	m	str	\enabin)hysreturn p;TY	/* Reset le;Pto TR_rtFITNEIludtret roADDR
#inclFtribt(
	m	str	\;
	uin_TY	if (ret)hysgoto errp;TY	/* Now cC_CONure_dat enabin le;Pludat
	m	str	\h>
# roADDRalloc
endpoiAt(_TY	if (!
	m	str	\h>
#)hysreturn -ENOMEMTYIbret roADDRget_cC_CON(
	m	str	\h>
#, &
	m	str	\NGE;
	d_TY	if (ret)hysgoto err1;dat
	m	str	\NGE;
	d.TR_OU= SPStr_out_RC;at
	m	str	\NGE;
	d.sourcOU= 
	m	str	\;
	uin;at
	m	str	\NGE;
	d.OR At_thtrih _bam_;at
	m	str	\NGE;
	d.sle.h>
#inndexO=4)	\
				MC_ETFETB_DUMP;at
	m	str	\NGE;
	d.opdionsU= SPStO_AUGE_ENABLE;dat
	m	str	\NGE;
	d.destin will = 
	m	str	\desdrvd
	m	str	\NGE;
	d.dest.h>
#inndexO=4
	m	str	\dest.h>
#indxrvd
	m	str	\NGE;
	d.descO=4
	m	str	\desc_fifops_
	m	str	\NGE;
	d.dstr ro
	m	str	\dstr_fifoTYIbret roADDRNGE;
	d(
	m	str	\h>
#, &
	m	str	\NGE;
	d_TY	if (ret)hysgoto err1;dat
	m	str	\enabin E_drueps	return p;Terr1:
	uls_nder
endpoiAt(
	m	str	\h>
#);Terr0:Y	return retps}
ETR_MEM_void __ase eanddisabin_todle;(gned long		\
} whiu*T_UNLOCK
{s_if (!								\enabin_todle;)hysreturnps
	/* Ensure_peniodEM_#/jtaITHOUT abind in CSR be <l *udtbsmvps.h_csnddisabin_n/jta(KTYIb_wriORESIGHT_UNLOCKTYIb();		
#i_for_m/jta(T_UNLOCKTY	__ase disabin(T_UNLOCKTYIb_writel(drvdata, ;Yt_/* Disabin CSR cC_CONur will *udtbsmvps.h_csnddisabin_le;
todmor()ps_beadl(drvenabin_todle;PE_falseps}
ETR_MEM_void ,
	TTMC_le;
	isabin(gned long		\
} whiu*T_UNLOCK
{s_gned long		handle;
	struM
	m	str ro								\
	m	strps
	if (!
	m	str	\enabin)hysreturnps
	uls
	isNGE;
	d(
	m	str	\h>
#_TY	tls_nder
endpoiAt(
	m	str	\h>
#);Tt
	m	str	\enabin E_falseps}
ETR_MEM_void morynotifien(void *(val, connect	cta-  R At, {p_gned lops.h_nequesdu*T_neq, gned losm-sps.h_ch *chK
{s_gned long		\
} whiu*T_UNLOCPE_(val;s_connect	conne##/agsps ta- ret rop;Tdtbutexde <l(&								\morye <l_TY	if ( R At r= USB_QDSSm tmNECT)uct 	,
	TTMC_fill_mory_du
	str(T_UNLOCKTY	bret ro();	handle;
enabin(T_UNLOCKTY	bif (ret)hys	dev_err									\dev, "			cMC_ enabin faC)
d\ntt;t3		uludde <ldirqsave(&								\ulude <l,##/ags_TY		__ase eandenabin_todle;(T_UNLOCKTY	buluddune <ldirqresdmod(&								\ulude <l,##/ags_TY	} else if ( R At r= USB_QDSSmDITATmNECT)uct 	uludde <ldirqsave(&								\ulude <l,##/ags_TY		__ase eanddisabin_todle;(T_UNLOCKTY	buluddune <ldirqresdmod(&								\ulude <l,##/ags_TY		,
	TTMC_le;
	isabin(T_UNLOCKTY	}dtbutexdune <l(&								\morye <l_TY}
ETR_MEM_ata->add ,
	TTMC_get_ TMC__ptr(gned long		\
} whiu*T_UNLOCK
{s_ata->add rwp rop;Tdt_wriORESIGHT_UNLOCKTYIbrwp rodefine tmc_readl_noCR		RWPKTYIb_writel(drvdata, ;Yt_return rwpps}
ETR_MEM_void ,
	TTMC_lyte_cnandTR_rt(gned long		\
} whiu*T_UNLOCK
{s_if (!								\
yte_cnandptribnt)hysreturnps
	butexde <l(&								\
yte_cnande <lSHIbatomicdibt(&								\
yte_cnandirq_cna, 0)ps_beadl(drv
yte_cnandoverflow E_falseps_beadl(drv
yte_cnand				_aativn E_falseps_beadl(drv
yte_cnandenabin E_drueps	if (								\
yte_cnand__rue !rop)hysbeadl(drv
yte_cnandoverflow_cna ro								\uize /
pppp_p (								\
yte_cnand__rue * 8SHIbelsehysbeadl(drv
yte_cnandoverflow_cna rop;T	aw_writelllsndibt_lyte_cnan(								\
yte_cnand__rueSHIbbutexdune <l(&								\
yte_cnande <lSHI}
ETR_MEM_void ,
	TTMC_lyte_cnandTRo
(gned long		\
} whiu*T_UNLOCK
{s_tf (!								\
yte_cnandptribnt)hysreturnps
	butexde <l(&								\
yte_cnande <lSHIbaw_writelllsndibt_lyte_cnan(0)ps_beadl(drv
yte_cnand__rue rop;T	beadl(drv
yte_cnandenabin E_falseps_butexdune <l(&								\
yte_cnande <lSHI3	>
ke_up(&								\wqK;E}
ETR_MEM_ta- ();	TMC_alloc
me;(gned long		\
} whiu*T_UNLOCK
{s_ia- retps
	if (!								\vR_SG)uct 	if (								\ETRIG_T r= 	= "sg",
};

enum tmc_m)uct 	pbeadl(drvvR_SG rob_bTzalloc
co2 reAt(								\dev,
ppppp_p     								\uize,
ppppp_p     &								\pR_SG,
ppppp_p     GFP_KERNEL_TY			if (!								\vR_SG)uct 		sret ro-ENOMEMTY 		sgoto err;s_		}t	p} else ct 	pret ro();	handsg_tbl_alloc(T_UNLOCKTY	bbif (ret)hys	sgoto err;s_	}t	}Y	/* { * Net	cto reinclialize bufcfor eachP(); enabin seshis psincPubliis { * geti
#ilse veAR Pdur
#il(); etr .h"

 { *udt								\
ufc=_readl(drvvR_SGps	return p;Terr:
	dev_err									\dev, "etr .SG ht-pricallocaAis pfaC)
d\ntt;t	return retps}
ETR_MEM_void ,
	TTMC_nder
me;(gned long		\
} whiu*T_UNLOCK
{s_if (								\vR_SG)uct 	if (								\ETRIG_T r= 	= "sg",
};

enum tmc_m)3		bb_b_nder
co2 reAt(								\dev, 								\uize,
ppppp _readl(drvvR_SG, 								\pR_SGKTY	belsehyst,
	TTMC_sg_tbl_nder((ata->add M)beadl(drvvR_SGw
pppp								\uize,
ppppDIV_ROUND_UP(								\uize, G_TYPEIZE)KTY	       beadl(drvvR_SG ro0TY	       beadl(drvpR_SG ro0TY	}Y}
ETR_MEM_void ,
	TTMC_ht-Ftribt(gned long		\
} whiu*T_UNLOCK
{s_if (								\vR_SG)uct 	if (								\ETRIG_T r= 	= "sg",
};

enum tmc_m)3		bht-ibt(readl(drvvR_SG, 0w_readl(drvuizeKps	belsehyst,
	TTMC_sg_ht-Ftribt((ata->add M)beadl(drvvR_SGw
ppppp     								\uize)TY	}Y}
ETR_MEM_void __ase eab
enabin(gned long		\
} whiu*T_UNLOCK
{s_/* Zero SS I; wiht-pricto helpFITNEIdebuilludtht-ibt(readl(drv
uf, 0w_readl(drvuizeKpsata {
ORESIGHT_UNLOCKTYIb();		TMC_UNLOCK(drvd_HARDWARE_FIFO,
};

enum e_HARDWARKTYtfine TMC_UNLOCK(drvdat1133noCR			(CR_TY	fine TMC_UNLOCK(drvd								\trigger_cnan,WD				(0_TY	__ase enabin(T_UNLOCKTYIb_writel(drvdata, ;Y}
ETR_MEM_void __ase eandenabin_todme;(gned long		\
} whiu*T_UNLOCK
{s_ata->add axiatlps
	,
	TTMC_ht-Ftribt(T_UNLOCKTYIb_wriORESIGHT_UNLOCKTYIb();		TMC_UNLOCK(drvd								\uize /e TMC_ETR_BAM_Nh 0x2)RSZSHIb();								\
} while_HARDWARE_FIFO,
};

enum e_HARDWARKTYIbaxiatlPE_define tmc_readl_noCR		HI			(SHIbaxiatlP|ro(0xFP<< 8SHIb();								\
} whileaxiatlnoCR		HI			(SHIbif (								\ETRIG_T r= 	= "sg",
};

enum tmc_m)3		axiatlP&= ~)
#dP<< 7SHIbelsehysaxiatlP|ro(0xdP<< 7SHIb();								\
} whileaxiatlnoCR		HI			(SHIbaxiatlPro(axiatlP& ~GE_S PAGE_HIb();								\
} whileaxiatlnoCR		HI			(SHIIb();								\
} while(ata->add_								\pR_SG,_FFSR			(0SHIb();								\
} while(((ata-64dd_								\pR_SGKONFI32) &AGEFF, {p   0x2)			HI_TY	fine TMC_UNLOCK(drvdat1133noCR			(CR_TY	fine TMC_UNLOCK(drvd								\trigger_cnan,WD				(0_TY	__ase enabin(T_UNLOCKTYIb_writel(drvdata, ;Y}
ETR_MEM_void __ase eaf
enabin(gned long		\
} whiu*T_UNLOCK
{s_a {
ORESIGHT_UNLOCKTYIb();		TMC_UNLOCK(drvd_HARDWARE_NONE,
	TMC_ETe_HARDWARKTYtfine TMC_UNLOCK(drvdat3noCR			(CR_TY	fine TMC_UNLOCK(drvdat0,ODE		I			(_TY	__ase enabin(T_UNLOCKTYIb_writel(drvdata, ;Y}
ETR_MEM_ta- ();	Tnabin(gned long		\
} whiu*T_UNLOC, E_FIFO,
	TMC_MTMC_K
{s_ta- retps aonnect	conne##/agspsIbret roclkdptrpare enabin(T_UNLOC->cll_TY	if (ret)hysreturn retpsdtbutexde <l(&								\morye <l_TY	uludde <ldirqsave(&								\ulude <l,##/ags_TY	if (								\				
#i)uct 	ret ro-EBUSYTY	buluddune <ldirqresdmod(&								\ulude <l,##/ags_TY		goto errp;T	}Y	uluddune <ldirqresdmod(&								\ulude <l,##/ags_TYY	if (								\MC_CONFIG_TYr= 	= "
};

enum tmc_m)uct 	aw_writelllud_map_trigSS (								\Mti_m/jta, 1, 0)ps_	aw_writelllud_map_trigin(								\Mti_tribt, 2, 0)ps_} else if (								\MC_CONFIG_TYr= 	= "
};

enum tmc_R)uct 	if (								\MC_ETR_OUr= 	= "sg",

enum tmc_e)uct
ppp/* {	{ * 			cDDRiht-pricTHOnotcallocaARRAuntiled wr enabins {	{ * (); atilOastPoncP. Ifed wr speaveARHOUTffer At 			 {	{ * DDRiuize thaTY; widefauld uizel PusITNchRHObetween {	{ * tf_widuousl Puscatter-ga 2 r ht-prictG_TYafter {	{ * enabi
#il();;Y; winew sel
	dionhe impliehonored from {	{ * nextit); enabin seshis . {	{ *udt		if (								\uize !=_readl(drvht-Fuize ||
p_p   _readl(drvht-tG_TY!=_readl(drvht-FtG_T)uct 		s,
	TTMC_nder
me;(T_UNLOCKTY	bbp								\uize =_readl(drvht-FuizeTY	bbp								\ETRIG_T r_readl(drvht-FtG_T;s_		}t		bret ro();	handalloc
me;(T_UNLOCKTY	bbif (ret)hys	sgoto err0;t3		b,
	TTMC_lyte_cnandTR_rt(T_UNLOCKTY	bbaw_writelllud_map_trigSS (								\Mti_m/jta,
pppp_p  3, 0)ps_		aw_writelllud_map_trigin(								\Mti_tribt,
pppp_p 2, 0)ps__} else if (								\MC_ETR_OUr= 	= "sg",

enum tmUSB)uct 	pbeadl(drvsm-ca rosm-sps.h_open("ps.h"vd							,hys_p_p       morynotifienKTY	bbif (IS_ERR(beadl(drvsm-ca))uct 		sdev_err									\dev, "sm-sps.h_openpfaC)
d\ntt;t			bret roPg",ERR(beadl(drvsm-ca);hys	sgoto err0;t_		}t	p}
p} else ct 	if (TR_OUr= 	= "DWARE_FIFO,
};

enum)uct 		aw_writelllud_map_trigSS (								\Mti_m/jta, 1, 0)ps_		aw_writelllud_map_trigin(								\Mti_tribt, 2, 0)ps_	}t	}Y
	uludde <ldirqsave(&								\ulude <l,##/ags_TYY	if (								\MC_CONFIG_TYr= 	= "
};

enum tmc_m)uct 	__ase eab
enabin(T_UNLOCKTY	} else if (								\MC_CONFIG_TYr= 	= "
};

enum tmc_R)uct 	if (								\MC_ETR_OUr= 	= "sg",

enum tmc_e)s_		__ase eandenabin_todme;(T_UNLOCKTY	} else ct 	if (TR_OUr= 	= "DWARE_FIFO,
};

enum)s_		__ase eab
enabin(T_UNLOCKTY	belsehyst__ase eaf
enabin(T_UNLOCKTY	}s_beadl(drvenabin E_drueps	if (								\force_regF	h"
)uct 									\dh"

reg E_drueps		__ase + gF	h"
(T_UNLOCKTY	b								\dh"

reg E_falseps_}Y
	/* { * sMEMkydenabin ptrv Ats d wrs from 				
#iit); dev nR_O befmod
{ * enabi
#il(); atilOastPoncP. { *udt								\sMEMkydenabin E_drueps	uluddune <ldirqresdmod(&								\ulude <l,##/ags_TY	butexdune <l(&								\morye <l_TY
	dev_info(								\dev, "CR	 enabind\ntt;t	return 0;Terr0:Y	butexdune <l(&								\morye <l_TY	clkddisabin_unptrpare(T_UNLOC->cll_TY	return retps}
ETR_MEM_ta- ();	Tnabin_sink(gned loaw_writellude <l *cs
#iK
{s_gned long		\
} whiu*T_UNLOCPE_dev_get_T_UNLOC(cs
#i	\dev.parent ;Yt_return ase enabin(T_UNLOCle_HARDWARE_FIFO,
};

enum ;Y}
ETR_MEM_ta- ();	Tnabin_link(gned loaw_writellude <l *cs
#i,_ta- inport,
ppp   ta- SS portK
{s_gned long		\
} whiu*T_UNLOCPE_dev_get_T_UNLOC(cs
#i	\dev.parent ;Yt_return ase enabin(T_UNLOCle_HARDWARE_NONE,
	TMC_E ;Y}
ETR_MEM_void __ase + gF	h"
(gned long		\
} whiu*T_UNLOCK
{s_ONTIG]+ gFhSG;t ata->add *regFbufps
	if (!								\regFbuf)hysreturnps	else if (!								\abort
#i && !								\dh"

reg)hysreturnps
	+ gFhSG r_readl(drvregFbuf - G_TYPEIZEps	if (MSM4)
#defiJOR(bsmv.h"

tabin_* This ())ur= 1)hys*(ata->add M)(+ gFhSG +efine TMC_ETR_SG_ENT_) r {p_ppppfine TMC_ETR_SG_HIbelsehysbeadl(drvregF	str.* This p=efine TMC_ETR_SG_ps
	+ gF
ufc=_(ata->add M)beadl(drvregFbufps
	regFbuf[1] rodefine tmc_readl_noCR		RSZSHIbregFbuf[3] rodefine tmc_readl_noCR		STS)HIbregFbuf[5] rodefine tmc_readl_noCR		RRP)HIbregFbuf[6] rodefine tmc_readl_noCR		RWP)HIbregFbuf[7] rodefine tmc_readl_noCR			(0_TY	regFbuf[8] rodefine tmc_readl_noCR				(SHIbregFbuf[10] rodefine tmc_readl_noCR		DWARKTYtregFbuf[11] rodefine tmc_readl_noCR		TMC_BUFWMKTYtregFbuf[12] rodefine tmc_readl_noCR			MC_BUFWMKTYtregFbuf[13] rodefine tmc_readl_noCR		I			(_TY	if (								\MC_CONFIG_TYr= 	= "
};

enum tmc_R)uct 	regFbuf[14] rodefine tmc_readl_noCR		RRPHI_TY		regFbuf[15] rodefine tmc_readl_noCR		RWPHI_TY		regFbuf[68] rodefine tmc_readl_noCR		HI			(SHIbbregFbuf[70] rodefine tmc_readl_noCR					(0SHIbbregFbuf[71] rodefine tmc_readl_noCR					HI_TY	}YtregFbuf[192] rodefine tmc_readl_noCR			(0x)TYtregFbuf[193] rodefine tmc_readl_noCR			(CR_TY	regFbuf[194] rodefine tmc_readl_noCR		TTRFSHIbregFbuf[1000] rodefine tmc_readl_noRSZ				(0xCLAIMSETSHIbregFbuf[1001] rodefine tmc_readl_noRSZ				(0xCLAIMCLFSHIbregFbuf[1005] rodefine tmc_readl_noRSZ				(0x0SFSHIbregFbuf[1006] rodefine tmc_readl_noRSZ				(0xAUTHSTATUSSHIbregFbuf[1010] rodefine tmc_readl_noRSZ				(0xDEVIDSHIbregFbuf[1011] rodefine tmc_readl_noRSZ				(0xDEVum tSHIbregFbuf[1012] rodefine tmc_readl_noRSZ				(0xPERIPHIDR4SHIbregFbuf[1013] rodefine tmc_readl_noRSZ				(0xPERIPHIDR5SHIbregFbuf[1014] rodefine tmc_readl_noRSZ				(0xPERIPHIDR6SHIbregFbuf[1015] rodefine tmc_readl_noRSZ				(0xPERIPHIDR7SHIbregFbuf[1016] rodefine tmc_readl_noRSZ				(0xPERIPHIDR0SHIbregFbuf[1017] rodefine tmc_readl_noRSZ				(0xPERIPHIDR1SHIbregFbuf[1018] rodefine tmc_readl_noRSZ				(0xPERIPHIDR2SHIbregFbuf[1019] rodefine tmc_readl_noRSZ				(0xPERIPHIDR3SHIbregFbuf[1020] rodefine tmc_readl_noRSZ				(0xCOMPIDR0SHIbregFbuf[1021] rodefine tmc_readl_noRSZ				(0xCOMPIDR1SHIbregFbuf[1022] rodefine tmc_readl_noRSZ				(0xCOMPIDR2SHIbregFbuf[1023] rodefine tmc_readl_noRSZ				(0xCOMPIDR3_TYY	if (MSM4)
#defiJOR(bsmv.h"

tabin_* This ())ur= 1)hys*(ata->add M)(+ gFhSG +efine TMC_ETR_ine TMC_R) r {p_ppppfine TMC_ETR_ine THIbelsehysbeadl(drvregF	str.magicp=efine TMC_ETR_fine TMC;Y}
ETR_MEM_void __ase eabF	h"
(gned long		\
} whiu*T_UNLOCK
{s_	TMC_MEM_INTF_WIDTH_64BIINTH_64B;s ata-8_onINTHordsps ONTIG]hSG;t ONTIG]bufp;t ata->add 				_NLOCTYY	hSG r_readl(drvbuf - G_TYPEIZEps	if (MSM4)
#defiJOR(bsmv.h"

tabin_* This ())ur= 1)hys*(ata->add M)(hSG +efineefine TMC_REG_DUMP_) r {p_ppppfineefine TMC_REG_DHIbelsehysbeadl(drvbufF	str.* This p=efineefine TMC_REG_DHIdtht-H_64BI= BMVAL(define tmc_readl_noRSZ				(0xDEVIDS, 8, 10_TY	if (ht-H_64BI== 	= "D,
	TMC_MEM_INTF_WIDT)hysINTHordsI= 1ps	else if (ht-H_64BI== 	= "D,
	TMC_MEM_INTTF_WID)hysINTHordsI= 2ps	else if (ht-H_64BI== 	= "D,
	TMC_MEM_INT tmc_et)hysINTHordsI= 4HIbelsehysINTHordsI= 8;dat
ufp r_readl(drvbuf;3	>0C)
#d1)uct 	re		_NLOC rodefine tm)

#define TMC_LOCR		RRDKTY	bif (re		_NLOC r=AGEFFFFFFFF)s_		goto SS ;
	bif ((
ufp -_readl(drvbuf) >ro								\uize)uct 	pbev_err									\dev, "		F-ne  end mark r hissing\ntt;t			goto SS ;
	b}hysINTcpy(
ufp, &re		_NLOC,e TMC_ETR_BAM_Nt;t		
ufp +=e TMC_ETR_BAM_Nps_}Y
SS :
	if ((
ufp -_readl(drvbuf) % (ht-HordsI*e TMC_ETR_BAM_Nt)hysbev_db{									\dev, "		F-ne  NLOC THOnotc%lx lytescalnect	\nth {p_(connect	conne) ht-HordsI*e TMC_ETR_BAM_NtTYY	if (								\abort
#i)uct 	if (MSM4)
#defiJOR(bsmv.h"

tabin_* This ())ur= 1)hyss*(ata->add M)(hSG +efineefine TMC_REine TMC_R) r {p_ppppfineefine TMC_REine TTY	belsehystbeadl(drvbufF	str.magicp=efineR_OFF		(4)
#define T;Y	}Y}
ETR_MEM_void __ase eab
	isabin(gned long		\
} whiu*T_UNLOCK
{s_a {
ORESIGHT_UNLOCKTYIb();	m/jta_dat_sto
(T_UNLOCKTY	__ase eabF	h"
(T_UNLOCKTY	__ase + gF	h"
(T_UNLOCKTY	__ase disabin(T_UNLOCKTYIb_writel(drvdata, ;Y}
ETR_MEM_void ,
	TTMC_sg_rwp_pos(gned long		\
} whiu*T_UNLOC, ata->add rwpK
{s_uta->add i rop, pte_n rop, lastMC_Cps ata->add *virt_st_tbl, *virt_C_Cps void *virt_blk;T	bool ft it E_falseps_MC_ETR_SG_E {
	TMC_C;s_ta- total_ Ats roDIV_ROUND_UP(								\uize, G_TYPEIZE);s_ta-  Ats_pendbek roG_TYPEIZE/uizeof(ata->add_TYIbvirt_st_tbl ro								\vR_SG;t3	>0C)
#di < total_ Ats)uct 	lastMC_C ro(di +  Ats_pendbek) > total_ Ats)u?
p_p   total_ Ats : di +  Ats_pendbek);s_	>0C)
#di < lastMC_C)uct 		virt_C_C rovirt_st_tbl + pte_n;t		b{
	TMC_CO=4)	\
					 << PAGE_SHIF*virt_C_C);s
ppp/* {	{ * Wh A PARTtrace bufferit unull; RWP couldpliell any {	{ * 4K be <l from scatternga 2 r tabin. Compl Pubelow - {	{ * 1. Be <l _ETbernw2 reIRWP t ucurr Atlystrii	
#i {	{ * 2.IRWP posidionhNTY; at 4K be <l {	{ * 3. Deltr;			set from curr AtTRWP posidionhto eat See{	{ *    bl <li {	{ *udt		if ({
	TMC_CO<= rwp && rwp < ({
	TMC_CO+ G_TYPEIZE))uct 	ptvirt_blkO=4{
	TMtodvirtc_config_t;t	bbp								\ug_blk__ETO=4i;t	bbp								\
ufc=_virt_blkO+ rwp - {
	TMC_C;s_bbp								\deltr_bottom r {p_pp{
	TMC_CO+ G_TYPEIZE - rwppsp_ppft it E_drueps			bbne k;s_		}tdt		if ((lastMC_C - i) > 1)uct 	pt{te_n++;s_		} else if (i < (total_ Ats - 1))uct 	ptvirt_blkO=4{
	TMtodvirtc_config_t;t	bbpvirt_st_tbl ro(ata->add M)virt_blk;T			b{te_n rop;T			bbne k;s_		}tdt		i++;s_	}t		if (ft it)hyssbne k;s_}Y}
ETR_MEM_void __ase earF	h"
(gned long		\
} whiu*T_UNLOCK
{s_ata->add rwp, rwphiTYIbrwp rodefine tmc_readl_noCR		RWPKTY	rwphi rodefine tmc_readl_noCR		RWPHI_TYIbif (								\ETRIG_T r= 	= "sg",
};

enum tmc_m)uct 	if (BVAL(define tmc_readl_noCR		STS), 0))hystbeadl(drvbuf ro								\vR_SGO+ rwp - 								\pR_SG;t belsehystbeadl(drvbufc=_readl(drvvR_SGps	} else ct 	/* {	 * Reset  2 Softariabins befmoducompl 
#i sincPuwe {	 * relysoA PARir __ruesPdur
#il(); ne t
	{ *udt									\ug_blk__ETO=40TY	b								\deltr_bottom rop;Tdt	if (BVAL(define tmc_readl_noCR		STS), 0))hyst,
	TTMC_sg_rwp_pos(_readl_norwpK;t belsehystbeadl(drvbufc=_readl(drvvR_SGps	}s}
ETR_MEM_void __ase eanddisabin_todme;(gned long		\
} whiu*T_UNLOCK
{s_a {
ORESIGHT_UNLOCKTYIb();	m/jta_dat_sto
(T_UNLOCKTY	__ase earF	h"
(T_UNLOCKTY	__ase + gF	h"
(T_UNLOCKTY	__ase disabin(T_UNLOCKTYIb_writel(drvdata, ;Y}
ETR_MEM_void __ase eaf
	isabin(gned long		\
} whiu*T_UNLOCK
{s_a {
ORESIGHT_UNLOCKTYIb();	m/jta_dat_sto
(T_UNLOCKTY	__ase disabin(T_UNLOCKTYIb_writel(drvdata, ;Y}
ETR_MEM_void ,
	T	isabin(gned long		\
} whiu*T_UNLOC, E_FIFO,
	TMC_MTMC_K
{s_aonnect	conne##/agspsIbbutexde <l(&								\morye <l_TY	uludde <ldirqsave(&								\ulude <l,##/ags_TY	if (								\				
#i)
		goto SS ;
Y	if (								\MC_CONFIG_TYr= 	= "
};

enum tmc_m)uct 	__ase eab
	isabin(T_UNLOCKTY	} else if (								\MC_CONFIG_TYr= 	= "
};

enum tmc_R)uct 	if (								\MC_ETR_OUr= 	= "sg",

enum tmc_e)s_		__ase eanddisabin_todme;(T_UNLOCKTY		else if (								\MC_ETR_OUr= 	= "sg",

enum tmUSB)s_		__ase eanddisabin_todle;(T_UNLOCKTY	} else ct 	if (TR_OUr= 	= "DWARE_FIFO,
};

enum)s_		__ase eab
	isabin(T_UNLOCKTY	belsehyst__ase eaf
	isabin(T_UNLOCKTY	}dt								\enabin E_falseps_uluddune <ldirqresdmod(&								\ulude <l,##/ags_TYY	if (								\MC_CONFIG_TYr= 	= "
};

enum tmc_m)uct 	aw_writelllud_unmap_trigin(								\Mti_tribt, 2, 0)ps_	aw_writelllud_unmap_trigSS (								\Mti_m/jta, 1, 0)ps_} else if (								\MC_CONFIG_TYr= 	= "
};

enum tmc_R)uct 	if (								\MC_ETR_OUr= 	= "sg",

enum tmc_e)uct		b,
	TTMC_lyte_cnandTRo
(T_UNLOCKTY	 	aw_writelllud_unmap_trigin(								\Mti_tribt,hys_p_p   2, 0)ps_		aw_writelllud_unmap_trigSS (								\Mti_m/jta,hys_p_p    3, 0)ps__} else if (								\MC_ETR_OUr= 	= "sg",

enum tmUSB)uct 	p,
	TTMC_le;
	isabin(T_UNLOCKTY			moryps.h_close(T_UNLOC->sm-ca);hys}
p} else ct 	if (TR_OUr= 	= "DWARE_FIFO,
};

enum)uct 		aw_writelllud_unmap_trigin(								\Mti_tribt, 2, 0)ps_		aw_writelllud_unmap_trigSS (								\Mti_m/jta, 1, 0)ps_	}t	}Y	butexdune <l(&								\morye <l_TY
	clkddisabin_unptrpare(T_UNLOC->cll_TY
	dev_info(								\dev, "CR	 UT abind\ntt;t	returnTYSS :
									\enabin E_falseps_uluddune <ldirqresdmod(&								\ulude <l,##/ags_TY	butexdune <l(&								\morye <l_TY
	clkddisabin_unptrpare(T_UNLOC->cll_TY
	dev_info(								\dev, "CR	 UT abind\ntt;t}
ETR_MEM_void ,
	T	isabin_sink(gned loaw_writellude <l *cs
#iK
{s_gned long		\
} whiu*T_UNLOCPE_dev_get_T_UNLOC(cs
#i	\dev.parent ;Yt_ase disabin(T_UNLOCle_HARDWARE_FIFO,
};

enum ;Y}
ETR_MEM_void ,
	T	isabin_link(gned loaw_writellude <l *cs
#i,_ta- inport,
ppp     ta- SS portK
{s_gned long		\
} whiu*T_UNLOCPE_dev_get_T_UNLOC(cs
#i	\dev.parent ;Yt_ase disabin(T_UNLOCle_HARDWARE_NONE,
	TMC_E ;Y}
ETR_MEM_void ase abort(gned loaw_writellude <l *cs
#iK
{s_gned long		\
} whiu*T_UNLOCPE_dev_get_T_UNLOC(cs
#i	\dev.parent ;Y_connect	conne##/agsps E_FIFO,
	TMC_MTMC_;Yt_beadl(drvabort
#i E_druepsY	uludde <ldirqsave(&								\ulude <l,##/ags_TY	if (								\				
#i)
		goto SS 0;
Y	if (								\MC_CONFIG_TYr= 	= "
};

enum tmc_m)uct 	__ase eab
	isabin(T_UNLOCKTY	} else if (								\MC_CONFIG_TYr= 	= "
};

enum tmc_R)uct 	if (								\MC_ETR_OUr= 	= "sg",

enum tmc_e)s_		__ase eanddisabin_todme;(T_UNLOCKTY		else if (								\MC_ETR_OUr= 	= "sg",

enum tmUSB)s_		__ase eanddisabin_todle;(T_UNLOCKTY	} else ct 	TR_OU= define tmc_readl_noCR		DWARKTYt	if (TR_OUr= 	= "DWARE_FIFO,
};

enum)s_		__ase eab
	isabin(T_UNLOCKTY	belsehystgoto SS 1TY	}dSS 0:
									\enabin E_falseps_uluddune <ldirqresdmod(&								\ulude <l,##/ags_TY
	dev_info(								\dev, "CR	 abortnd\ntt;t	returnTYSS 1:
	uluddune <ldirqresdmod(&								\ulude <l,##/ags_TY}
ETR_MEM_= "contigd loaw_writellops_sink defisinklops E_SG].enabin32BI();	Tnabin_sink,
p.	isabin2BI();		isabin_sink,
p.abort32BI();	abort[TMC_ETR_MEM_TYPE_Ctigd loaw_writellops_link defilinklops E_SG].enabin32BI();	Tnabin_link,
p.	isabin2BI();		isabin_link,
MC_ETR_MEM_TYPE_Ctigd loaw_writellops ase eab
cslops E_SG].sinklops2BI&defisinklops,
MC_ETR_MEM_TYPE_Ctigd loaw_writellops ase ear
cslops E_SG].sinklops2BI&defisinklops,
MC_ETR_MEM_TYPE_Ctigd loaw_writellops ase eaf
cslops E_SG].sinklops2BI&defisinklops,
	.linklops2BI&defilinklops,
MC_ETR_MEM_ta- ();	re		_ptrpare(gned long		\
} whiu*T_UNLOCK
{s_ta- retps aonnect	conne##/agsps E_FIFO,
	TMC_MTMC_;Yt_butexde <l(&								\morye <l_TY	uludde <ldirqsave(&								\ulude <l,##/ags_TY	if (!								\sMEMkydenabin)uct 		ev_err									\dev, "enabin (); oncP befmodu				
#i\ntt;t		ret ro-EPERMTY 	goto err;s_}s_if (!								\enabin)
		goto SS ;
Y	if (								\MC_CONFIG_TYr= 	= "
};

enum tmc_m)uct 	__ase eab
	isabin(T_UNLOCKTY	} else if (								\MC_CONFIG_TYr= 	= "
};

enum tmc_R)uct 	if (								\MC_ETR_OUr= 	= "sg",

enum tmc_e)uct 		__ase eanddisabin_todme;(T_UNLOCKTY		} else ct 	pret ro-ENODEV;t			goto err;s_	}t	} else ct 	TR_OU= define tmc_readl_noCR		DWARKTYt	if (TR_OUr= 	= "DWARE_FIFO,
};

enum)uct 		__ase eab
	isabin(T_UNLOCKTY	b} else ct 	pret ro-ENODEV;t			goto err;s_	}t	}YSS :
									\				
#iiE_drueps	uluddune <ldirqresdmod(&								\ulude <l,##/ags_TY	butexdune <l(&								\morye <l_TY
	dev_info(								\dev, "CR	 				 TR_rt\ntt;t	return 0;Terr:
	uluddune <ldirqresdmod(&								\ulude <l,##/ags_TY	butexdune <l(&								\morye <l_TY	return retps}
ETR_MEM_void ,
	Tre		_unptrpare(gned long		\
} whiu*T_UNLOCK
{s_aonnect	conne##/agsps E_FIFO,
	TMC_MTMC_;Yt_uludde <ldirqsave(&								\ulude <l,##/ags_TY	if (!								\enabin)
		goto SS ;
Y	if (								\MC_CONFIG_TYr= 	= "
};

enum tmc_m)uct 	__ase eab
enabin(T_UNLOCKTY	} else if (								\MC_CONFIG_TYr= 	= "
};

enum tmc_R)uct 	if (								\MC_ETR_OUr= 	= "sg",

enum tmc_e)s_		__ase eandenabin_todme;(T_UNLOCKTY	} else ct 	TR_OU= define tmc_readl_noCR		DWARKTYt	if (TR_OUr= 	= "DWARE_FIFO,
};

enum)s_		__ase eab
enabin(T_UNLOCKTY	}sSS :
									\				
#iiE_falseps_uluddune <ldirqresdmod(&								\ulude <l,##/ags_TY
	dev_info(								\dev, "CR	 				 end\ntt;t}
ETR_MEM_ta- ();	open(gned loinR_O *inR_O, gned lofC)
#*fC)
K
{s_gned long		\
} whiu*T_UNLOCPE_tf_wa<< ndof(fC)
->(valate_NLOC,hys_p_p   gned long		\
} whi, hisc
#iK;s ta- ret rop;Tdtbutexde <l(&								\				_e <l_TY	if (								\				_count++)
		goto SS ;
Y	ret ro();	re		_ptrpare(T_UNLOCKTY	if (ret)hysgoto errTYSS :
	butexdune <l(&								\				_e <l_TY	nYPEeekabin_open(inR_O, fC)
Kps
	dev_db{									\dev, "%s:isuccessnullyIopect	\nth __func__t;t	return 0;Terr:
									\				_count--TY	butexdune <l(&								\				_e <l_TY	return retps}
E/ usefCR	 				 logicpwh A scatternga 2 r featureITHOenabind:be usep   sg_tbl__ET=0usep|---------------|<--_readl(drvvR_SGusep|   blk__ET=0	|usep| blk__ET	rel=5	|usep|---------------|usep|   blk__ET=1	|usep| blk__ET	rel=6	|usep|---------------|usep|   blk__ET=2	|usep| blk__ET	rel=7	|usep|---------------|p   sg_tbl__ET=1usep|  NextiTabin	|------>|---------------|usep|  A_SG		|p|   blk__ET=3	|usep|---------------|p| blk__ET	rel=8	|usepppp|---------------|usepp  4k Be <l A_SG	|   blk__ET=4	|usepp |--------------| blk__ET	rel=0	|usepp |pp|---------------|usepp |pp|   blk__ET=5	|usepp |pp| blk__ET	rel=1	|usepp |pp|---------------|p   sg_tbl__ET=2usep |---------------|      |  NextiTabin	|------>|---------------|usep |pp |p|  A_SG		|p|   blk__ET=6	|usep |pp |p|---------------|p| blk__ET	rel=2 |usep |    				_offp |pppp|---------------|usep |pp |p	pp|   blk__ET=7	|usep |pp | pposp	pp| blk__ET	rel=3	|usep |---------------|-----pppp|---------------|usep |pp |p	pp|   blk__ET=8	|usep |    deltr_upp |p	pp| blk__ET	rel=4	|usep |pp | RWP/beadl(drvbufpp|---------------|usep |---------------|-----------------		|p	|usep |pp |   |p	pp|pp|Eat See tp |pp |   |p	pp|---------------|-----e tp |pp | 								\deltr_bottom	_p Tabinusep |pp |   |usep |_______________|  _|_usep      4K Be <lbe usefFor sICULAcity abovnsdiaut WIassumescfollow
#i:usefa. ht-Fuize = 36KB --> total_ Ats ro9nty o.  Ats_pendbek ro4ncl c.IRWP t uon 5th be <l (blk__ETO=45); so we havePto TR_rtF				
#iifrom RWPe th   posidionncludeTR_MEM_void ,
	TTMC_sg_compl e	re		(gned long		\
} whiu*T_UNLOC, loffdd Mpposh {p_p    ONTIG]]bufpp, uizedd MlenK
{s_uta->add i rop, blk__ET	rel rop, 				_een rop;T	uta->add blk__ET, sg_tbl__ET, blk__ET	e <, 				_off;s ata->add *virt_pte, *virt_st_tblps void *virt_blk;T	MC_ETR_SG_E {
	TMC_C rop;T	ta- total_ Ats roDIV_ROUND_UP(								\uize, G_TYPEIZE);s_ta-  Ats_pendbek roG_TYPEIZE/uizeof(ata->add_TYIb/* { * Fiat relativn be <l _ETbernfrom ppos_dat 				
#ii			set { * ITNEin be <l dat fiat actual be <l _ETbernbast	con relativn { * be <l _ETber { *udtif (beadl(drvbufUr= readl(drvvR_SG)uct 	blk__ETO=4Mppos / G_TYPEIZEps						_offO=4Mppos % G_TYPEIZEps	} else ct 	if (Mppos < 								\deltr_bottom) ct 	pre		_offO=4G_TYPEIZE - 								\deltr_bottomTY	b} else ct 	pblk__ET	rel ro(Mppos / G_TYPEIZE) + 1ps		pre		_offO=4(Mppos - 								\deltr_bottom) % G_TYPEIZEps		}tdt	blk__ETO=4(								\ug_blk__ETO+ blk__ET	rel) % total_ Ats;s_}Y
	virt_st_tbl ro(ata->add M)readl(drvvR_SGps
	/* CCmpl Putabin nndexOdat be <l  AtryANTdexOITNEin ; at tabin *udtif (blk__ETO&& (blk__ETO== (total_ Ats - 1))u&&s		p!(blk__ETO% ( Ats_pendbek - 1)))uct 	ug_tbl__ET roblk__ETO/  Ats_pendbek;dt	blk__ET	e < =  Ats_pendbek - 1ps	} else ct 	ug_tbl__ET roblk__ETO/ ( Ats_pendbek - 1);dt	blk__ET	e < = blk__ETO% ( Ats_pendbek - 1);s_}Y
	for (i rop; i < ug_tbl__ET; i++) ct 	virt_C_C rovirt_st_tbl + ( Ats_pendbek - 1);dt	{
	TMC_CO=4)	\
					 << PAGE_SHIF*virt_C_C);s_	virt_st_tbl ro(ata->add M){
	TMtodvirtc_config_t;t	}Y
	virt_C_C rovirt_st_tbl + blk__ET	e <;T	MC_ETC_CO=4)	\
					 << PAGE_SHIF*virt_C_C);s_virt_blkO=4{
	TMtodvirtc_config_t;t3	]bufppc=_virt_blkO+ r			_off;sdtif (Mlen > (G_TYPEIZE - r			_off))hys*een roG_TYPEIZE - r			_offTYIb/* { * Wh A bufferit uwrappt	cart it dat try
#il(o 				 lastPrelativn { * be <l (i.e. deltr_up),ucompl e een UTffer Atly { *udtif (blk__ET	rel && (blk__ETO== 								\ug_blk__ET))uct 	re		_een roG_TYPEIZE - 								\deltr_bottom - r			_offTY	tif (Mlen > re		_een)hyss*een rore		_een;t	}Y
	dev_db{_ratelimited									\dev,
	"%s:i				 at %p, _con %pa een %zu blk %d, 		l blk %dIRWP blk %d\nth { __func__, ]bufpp, &_config_, Mlen, blk__ET, blk__ET	rel,
									\ug_blk__ETt;t}
ETR_MEM_suizedd ();	re		(gned lofC)
#*fC)
, ONTIG__d wr *NLOC, uizedd len,hyssloffdd MpposK
{s_gned long		\
} whiu*T_UNLOCPE_tf_wa<< ndof(fC)
->(valate_NLOC,hys_p_p   gned long		\
} whi, hisc
#iK;s ONTIG]bufp, Mend;Yt_butexde <l(&								\morye <l_TYat
ufp  r_readl(drvbuf + Mpposps E_t E_(ONTIG])(readl(drvvR_SG + 								\uize_TYY	if (Mppos + len > 								\uize_hyseen ro								\uize - MppospsIb/* { * WnsdoOnotcexpecd lenl(o become zeroYafter ; t upoiAt. HencP bail SS  { * from 2 reITf lenlt uzero { *udtif (een rrop)hysgoto SS ;
Y	if (								\MC_CONFIG_TYr= 	= "
};

enum tmc_R)uct 	if (								\ETRIG_T r= 	= "sg",
};

enum tmc_m)uct 	pif (bufp r=  At)hysst
ufp r_readl(drvvR_SGps			else if (
ufp >  At)hysst
ufp -ro								\uize;t 	pif ((
ufp + len) >  At)hyssteen roE_t - bufp;t b} else ct 	p,
	TTMC_sg_compl e	re		(\
} whi, pposh &
ufp, &len);s_	}t	}YY	if (copy
todmoer( whi, 
ufp, len))uct 		ev_db{									\dev, "%s:icopy
todmoerpfaC)
d\nth __func__t;t		butexdune <l(&								\morye <l_TY	sreturn -EFAULT;t	}Y
	Mppos += een;tSS :
		ev_db{									\dev, "%s:i%zu bytesccopied, %dIbytescleft\nth {p__func__, len, (iAt) (								\uize - Mppos));Yt_butexdune <l(&								\morye <l_TY	return een;t}
ETR_MEM_ta- ();	releast(gned loinR_O *inR_O, gned lofC)
#*fC)
K
{s_gned long		\
} whiu*T_UNLOCPE_tf_wa<< ndof(fC)
->(valate_NLOC,hys_p_p   gned long		\
} whi, hisc
#iK;sdtbutexde <l(&								\				_e <l_TY	if (--								\				_count)uct 	if (								\				_count < 0)uct 	pWARN_ONCE(1, "hismaNchRd close\ntt;t											\				_count =40TY	b}hysgoto SS ;
	}Y
	,
	Tre		_unptrpare(T_UNLOCKTYSS :
	butexdune <l(&								\				_e <l_TY		ev_db{									\dev, "%s:ireleast	\nth __func__t;t	return 0;T}
ETR_MEM_= "contigd lofC)
_oper wills ase fops E_SG].ow< n32BITHIStr_oULE,
	.opec32BI();	opec,
	.				32BI();					,
	.		least2BI();			least,
	.llEeek32BI

#dlEeek,
MC_ETR_MEM_ta- ();	TMC_lyte_cnandopen(gned loinR_O *inR_O, gned lofC)
#*fC)
K
{s_gned long		\
} whiu*T_UNLOCPE_tf_wa<< ndof(inR_O->i_cdev,
ppppp_   gned long		\
} whi,
ppppp_   lyte_cnand
#iK;sdtif (								\MC_ETR_OU!= 	= "sg",

enum tmc_e ||
p    !								\
yte_cnandenabin)hysreturn -EPERMTYY	if (!butexdtrye <l(&								\
yte_cnand				_e <l_)hysreturn -EPERMTYY	fC)
->(valate_NLOC r_readl(dTY	nYPEeekabin_open(inR_O, fC)
Kps	beadl(drv
yte_cnandbe <l_uize =_readl(drv
yte_cnand__rue * 8ps	beadl(drv
yte_cnand				_aativn E_drueps	dev_db{									\dev, "%s:isuccessnullyIopect	\nth __func__t;t	return 0;T}
ETR_MEM_void ,
	TTMC_sg_re		_pos(gned long		\
} whiu*T_UNLOC, loffdd Mpposh {p_puizedd bytes, 
ool noirq, uizedd Mlenh {p_pONTIG]]bufppK
{s_ata->add rwp, i rop;T	uta->add blk__ET, sg_tbl__ET, blk__ET	e <, 				_off;s ata->add *virt_pte, *virt_st_tblps void *virt_blk;T	MC_ETR_SG_E {
	TMC_C;T	ta- total_ Ats roDIV_ROUND_UP(								\uize, G_TYPEIZE);s_ta-  Ats_pendpg roG_TYPEIZE/uizeof(ata->add_TYIbif (Mlen rrop)hysreturnps
	blk__ETO=4Mppos / G_TYPEIZEps					_offO=4Mppos % G_TYPEIZEps
	virt_st_tbl ro(ata->add M)readl(drvvR_SGps
	/* CCmpl Putabin nndexOdat be <l  AtryANTdexOITNEin ; at tabin *udtif (blk__ETO&& (blk__ETO== (total_ Ats - 1))u&&s	    !(blk__ETO% ( Ats_pendpg - 1)))uct 	ug_tbl__ET roblk__ETO/  Ats_pendpg;dt	blk__ET	e < =  Ats_pendpg - 1ps	} else ct 	ug_tbl__ET roblk__ETO/ ( Ats_pendpg - 1);dt	blk__ET	e < = blk__ETO% ( Ats_pendpg - 1);dt}Y
	for (i rop; i < ug_tbl__ET; i++) ct 	virt_C_C rovirt_st_tbl + ( Ats_pendpg - 1);dt	{
	TMC_CO=4)	\
					 << PAGE_SHIF*virt_C_C);s_	virt_st_tbl ro(ata->add M){
	TMtodvirtc_config_t;t	}Y
	virt_C_C rovirt_st_tbl + blk__ET	e <;T	MC_ETC_CO=4)	\
					 << PAGE_SHIF*virt_C_C);s_virt_blkO=4{
	TMtodvirtc_config_t;t3	]bufppc=_virt_blkO+ r			_off;sdtif (noirq)uct 	rwp rodefine tmc_readl_noCR		RWPKTY	t,
	TTMC_sg_rwp_pos(_readl_norwpK;t bif (								\ug_blk__ETO== blk__ETO&&s		    	wp >= ({
	TMC_CO+ r			_off))hyss*een rorwp - {
	TMC_C - r			_offTY	telse if (								\ug_blk__ETO> blk__ET)hyss*een roG_TYPEIZE - r			_offTY	belsehyst*een robytesps	} else ctY	tif (Mlen > (G_TYPEIZE - r			_off))hyss*een roG_TYPEIZE - r			_offTYIbtif (Mlen >= (bytesc- ((ata->add)Mppos % bytes)))hyss*een robytesc- ((ata->add)Mppos % bytes)TYIbtif ((Mlen + (ata->add)Mppos) % bytes rrop)hysbatomicddec(&								\
yte_cnandirq_cnat;t	}Y
	/* { * In__ri			e c/qco rangP befmodu				
#i. T t ue impmake sure_; at CPU
	 * readscl		est_tf_w Ats from DDR { *udt	mac_inv_rangP((void *)(]bufppK, (void *)(]bufppK + Mlen);s}
ETR_MEM_void ,
	TTMC_r			_bytes(gned long		\
} whiu*T_UNLOC, loffdd Mpposh {p_       uizedd bytes, uizedd MlenK
{s_if (Mlen >= bytes)uct 	atomicddec(&								\
yte_cnandirq_cnat;t	t*een robytesps	} else ctbtif (((ata->add)Mppos % bytes) + MlenO> bytes) {p_p*een robytesc- ((ata->add)Mppos % bytes)TYbtif ((Mlen + (ata->add)Mppos) % bytes rrop)hysbatomicddec(&								\
yte_cnandirq_cnat;t	}Y}
ETR_MEM_sizedd ();	TMC_n/jta_bytes(gned long		\
} whiu*T_UNLOC, loffdd Mpposh {p_	  uizedd bytesK
{s_ata->add rwp rop;T	uizedd len robytespsIbrwp rodefiTMC_get_ TMC__ptr(T_UNLOCKTY	if (rwp >= (								\pR_SG + Mppos)) ctbtif (len > (rwp - 								\pR_SG - Mppos))
ssteen rorwp - 								\pR_SG - Mppos;Y	}Ytreturn een;t}
ETR_MEM_ssizedd ();	TMC_
yte_cnand				(gned lofC)
#*fC)
, ONTIG__d wr *NLOC, {p_	  uizedd len, loffdd MpposK
{s_gned long		\
} whiu*T_UNLOCPE_fC)
->(valate_NLOC;s ONTIG]bufp ro								\vR_SGO+ Mppos;Y	uizedd bytes =_readl(drv
yte_cnandbe <l_uize;s ta- ret rop;Tdtif (!	LOCK
ysreturn -EINVALTY	if (								\
yte_cnandoverflowK
ysreturn -EIOps
	butexde <l(&								\
yte_cnande <lSHIb/* In c/se PART
yte counterit uenabindOdat UT abind multipin (imes { * ptrv At unexpecdR PdLOCPfrom be
#ilgivnnl(o PARTd wr { *udtif (!								\
yte_cnand				_aativn)hysgoto 				_err0;t3	if (!								\
yte_cnandenabin)uct 	if (!atomicd				(&								\
yte_cnandirq_cnat)uct 	p/* Read PARTlastP'be <l' ofPdLOCPwhich mitelplieneeded {p_ *l(o bei				 parlially. Ifeal				yi				, 		turn 0 {p_ *udt		if (								\ETRIG_T r= 	= "sg",
};

enum tmc_m)3		bteen ro();	TMC_n/jta_bytes(\
} whi, pposh bytes)TYbtbelsehyst	,
	TTMC_sg_re		_pos(\
} whi, pposh bytes,hys_p_p    drue, &lenh &
ufp);t 	pif (!een)hysssgoto 				_err0;t b} else ct 	p/* Keep 				
#iiuntileyou 			chP(ARTlastPbe <l ofPdLOC {p_ *udt		if (								\ETRIG_T r= 	= "sg",
};

enum tmc_m)3		bt,
	TTMC_r			_bytes(\
} whi, pposh bytes, &len);s_	belsehyst	,
	TTMC_sg_re		_pos(\
} whi, pposh bytes,hys_p_p    false, &lenh &
ufp);t 	}
p} else ct 	if (!atomicd				(&								\
yte_cnandirq_cnat)uct 	pbutexdune <l(&								\
yte_cnande <lSHIt		if (	
#i_OR At_interruptibin(T_UNLOC->wqh {p_    (atomicd				(&								\
yte_cnandirq_cnat > 0) ||
p_p   _!								\
yte_cnandenabin))uct 		sret ro-ERESTARTSYS;hysssgoto 				_err1ps		p}t 	pbutexde <l(&								\
yte_cnande <lSHIt		if (!								\
yte_cnand				_aativn)uct 		sret ro0;hysssgoto 				_err0;t_		}t	p}
p	if (								\
yte_cnandoverflowK ct 	pret ro-EIOpssssgoto 				_err0;t b}
		if (!								\
yte_cnandenabin &&s		    !atomicd				(&								\
yte_cnandirq_cnat)uct 	pif (								\ETRIG_T r= 	= "sg",
};

enum tmc_m)3		bteen ro();	TMC_n/jta_bytes(\
} whi, pposh bytes)TYbtbelsehyst	,
	TTMC_sg_re		_pos(\
} whi, pposh bytes,hys_p_p    drue, &lenh &
ufp);t 	pif (!een)uct 		sret ro0;hysssgoto 				_err0;t_		}t	p} else ct 	pif (								\ETRIG_T r= 	= "sg",
};

enum tmc_m)3		bt,
	TTMC_r			_bytes(\
} whi, pposh bytes, &len);s_	belsehyst	,
	TTMC_sg_re		_pos(\
} whi, pposh bytes,hys_p_p    false, &lenh &
ufp);t 	}
p}YY	if (copy
todmoer( whi, 
ufp, len))uct 	butexdune <l(&								\
yte_cnande <lSHIt		ev_db{									\dev, "%s:icopy
todmoerpfaC)
d\nth __func__t;t		ret ro-EFAULT;t	sgoto 				_err1ps	}Y	butexdune <l(&								\
yte_cnande <lSHI3	if (Mppos + len >ro								\uize)t	t*ppos ro0;hyelsehysMppos += een;t
		ev_db{									\dev, "%s:i%zu bytesccopied, %dIbytescleft\nth {p__func__, len, (iAt) (								\uize - Mppos));Ytreturn een;t
				_err0:
	butexdune <l(&								\
yte_cnande <lSHI				_err1:Y	return retps}
ETR_MEM_ta- ();	TMC_
yte_cnand		least(gned loinR_O *inR_O, gned lofC)
#*fC)
K
{s_gned long		\
} whiu*T_UNLOCPE_fC)
->(valate_NLOC;s
	butexde <l(&								\
yte_cnande <lSHIbbeadl(drv
yte_cnand				_aativn E_falseps_butexdune <l(&								\
yte_cnande <lSHI_butexdune <l(&								\
yte_cnand				_e <l_TY		ev_db{									\dev, "%s:ireleast	\nth __func__t;t	return 0;T}
ETR_MEM_= "contigd lofC)
_oper wills 
yte_cnandfops E_SG].ow< n32BITHIStr_oULE,
	.opec32BI();	TMC_lyte_cnandopen,
	.				32BI();	TMC_
yte_cnand				,
	.		least2BI();	TMC_
yte_cnand		least,
	.llEeek32BI

#dlEeek,
MC_ETR_MEM_ssizedd ();	show_trigger_cnan(gned loude <l *dev,
pppp     uned loude <l_attribl Pu*attr, ONTIG*buf)h{s_gned long		\
} whiu*T_UNLOCPE_dev_get_T_UNLOC(
#i	\parent ;Y_connect	conne#val ro								\trigger_cnan;Yt_return scn(vantf(
uf, G_TYPEIZE, "%#lx\nth valt;t}
ETR_MEM_suizedd ();	sdmod_trigger_cnan(gned loude <l *dev,
pppp      uned loude <l_attribl Pu*attr,
pppp      = "conONTIG*buf, uizedd uize)t{s_gned long		\
} whiu*T_UNLOCPE_dev_get_T_UNLOC(
#i	\parent ;Y_connect	conne#valHI3	if (sscanf(
uf, "%lxth &valtU!= 1K
ysreturn -EINVALTYIbbeadl(drvtrigger_cnanc=_valHI_return size;s}ETR_MEM_DEVICE_ATTR(trigger_cnan,WS_IRUGO |WS_IWUSR, ();	show_trigger_cnan, {p   ();	sdmod_trigger_cnan)C_ETR_MEM_ssizedd ();	TMC_show_MC_ETR_O(gned loude <l *dev,
pppp      uned loude <l_attribl Pu*attr, ONTIG*buf)h{s_gned long		\
} whiu*T_UNLOCPE_dev_get_T_UNLOC(
#i	\parent ;Yt_return scn(vantf(
uf, G_TYPEIZE, "%s\nth {p_gne_ase eandMC_ETR_O[								\MC_ETR_O]t;t}
ETR_MEM_suizedd ();	eandTRood_MC_ETR_O(gned loude <l *dev,
pppp       uned loude <l_attribl Pu*attr,
pppp       = "conONTIG*buf, uizedd uize)t{s_gned long		\
} whiu*T_UNLOCPE_dev_get_T_UNLOC(
#i	\parent ;Y_ONTIGgne[10] ro"";Y_connect	conne##/agsps ia- retps
	if (gneeen(buf) >ro10K
ysreturn -EINVALTY	if (sscanf(
uf, "%h"vdgnetU!= 1K
ysreturn -EINVALTYIbbutexde <l(&								\morye <l_TY	if (!gnec"
(gnevdgne_ase eandMC_ETR_O[	= "sg",

enum tmc_e]))uct 	if (								\MC_ETR_OUr= 	= "sg",

enum tmc_e)s_		goto SS ;
Y	_uludde <ldirqsave(&								\ulude <l,##/ags_TY		if (!								\enabin)uct 	pbeadl(drvMC_ETR_OUr 	= "sg",

enum tmc_e;s_	buluddune <ldirqresdmod(&								\ulude <l,##/ags_TY			goto SS ;
	b}hys__ase eanddisabin_todle;(T_UNLOCKTY		__ase eandenabin_todme;(T_UNLOCKTY	pbeadl(drvMC_ETR_OUr 	= "sg",

enum tmc_e;s_	uluddune <ldirqresdmod(&								\ulude <l,##/ags_TY
		aw_writelllud_map_trigSS (								\Mti_m/jta, 3, 0)ps_	aw_writelllud_map_trigin(								\Mti_tribt, 2, 0)ps
	p,
	TTMC_le;
	isabin(T_UNLOCKTY		moryps.h_close(T_UNLOC->sm-ca);hy} else if (!gnec"
(gnevdgne_ase eandMC_ETR_O[	= "sg",

enum tmUSB]))uct 	if (								\MC_ETR_OUr= 	= "sg",

enum tmUSB)s_		goto SS ;
Y	_uludde <ldirqsave(&								\ulude <l,##/ags_TY		if (!								\enabin)uct 	pbeadl(drvMC_ETR_OUr 	= "sg",

enum tmUSB;s_	buluddune <ldirqresdmod(&								\ulude <l,##/ags_TY			goto SS ;
	b}hysif (								\				
#i)uct 		ret ro-EBUSYTY	bsgoto err1;
	b}hys__ase eanddisabin_todme;(T_UNLOCKTY	pbeadl(drvMC_ETR_OUr 	= "sg",

enum tmUSB;s_	uluddune <ldirqresdmod(&								\ulude <l,##/ags_TY
		aw_writelllud_unmap_trigin(								\Mti_tribt, 2, 0)ps_	aw_writelllud_unmap_trigSS (								\Mti_m/jta, 3, 0)ps
	pT_UNLOC->sm-ca rosm-sps.h_open("ps.h"vd							,hys_p_       morynotifienKTY	bif (IS_ERR(beadl(drvsm-ca))uct 		dev_err									\dev, "sm-sps.h_openpfaC)
d\ntt;t			ret roPg",ERR(beadl(drvsm-ca);hys	goto err0;t_	}t	}YSS :
	butexdune <l(&								\morye <l_TY	return size;serr1:Y	uluddune <ldirqresdmod(&								\ulude <l,##/ags_TYerr0:Y	butexdune <l(&								\morye <l_TY	return retps}
TR_MEM_DEVICE_ATTR(MC_ETR_O,WS_IRUGO |WS_IWUSR, ();	TMC_show_MC_ETR_O, {p   ();	eandTRood_MC_ETR_O)C_ETR_MEM_ssizedd ();	TMC_show_avaC)abin_oC_ETR_Os(gned loude <l *dev,
pppp      uned loude <l_attribl Pu*attr, ONTIG*buf)h{s_guizedd len rop;T	ta- i;Y
	for (i rop; i < ARRAYPEIZE(gne_ase eandMC_ETR_O); i++)
bteen += scn(vantf(
uf + len,oG_TYPEIZE - len,o"%h "h {p_pune_ase eandMC_ETR_O[i])ps
	een += scn(vantf(
uf + len,oG_TYPEIZE - len,o"\ntt;t	return een;t}
TR_MEM_DEVICE_ATTR(avaC)abin_oC_ETR_Os,WS_IRUGOh {p_();	TMC_show_avaC)abin_oC_ETR_Os, NULL)C_ETR_MEM_ssizedd ();	TMC_show_
yte_cnand__rue(gned loude <l *dev,
pppp	uned loude <l_attribl Pu*attr,
pppp	ONTIG*buf)h{s_gned long		\
} whiu*T_UNLOCPE_dev_get_T_UNLOC(
#i	\parent ;Y_connect	conne#val ro								\
yte_cnand__rue;t3	if (!								\
yte_cnandptribntK
ysreturn -EPERMTYY	return scn(vantf(
uf, G_TYPEIZE, "%#lx\nth valt;t}
ETR_MEM_suizedd ();	eandTRood_
yte_cnand__rue(gned loude <l *dev,
pppp	 uned loude <l_attribl Pu*attr,
pppp	 = "conONTIG*buf, uizedd uize)t{s_gned long		\
} whiu*T_UNLOCPE_dev_get_T_UNLOC(
#i	\parent ;Y_connect	conne#valHI3	butexde <l(&								\
yte_cnande <lSHIbif (!								\
yte_cnandptribnt ||o								\
yte_cnandenabin)uct 	butexdune <l(&								\
yte_cnande <lSHIt	return -EPERMTY_}s_if (sscanf(
uf, "%lxth &valtU!= 1Kuct 	butexdune <l(&								\
yte_cnande <lSHIt	return -EINVALTY	}s_if ((								\uize / 8) < valtUct 	butexdune <l(&								\
yte_cnande <lSHIt	return -EINVALTY	}s_if (val &&o								\uize % (val * 8) != 0)uct 	butexdune <l(&								\
yte_cnande <lSHIt	return -EINVALTY	}sIbbeadl(drv
yte_cnand__rue =_valHI_butexdune <l(&								\
yte_cnande <lSHItreturn size;s}ETR_MEM_DEVICE_ATTR(
yte_cnand__rue,WS_IRUGO |WS_IWUSR, {p   ();	eandThow_
yte_cnand__rue, ();	eandTRood_
yte_cnand__rue)C_ETR_MEM_ssizedd ();	TMC_show_ht-Fuize(gned loude <l *dev,
pppp     uned loude <l_attribl Pu*attr,
pppp     ONTIG*buf)h{s_gned long		\
} whiu*T_UNLOCPE_dev_get_T_UNLOC(
#i	\parent ;Y_connect	conne#val ro								\ht-FuizeTYY	return scn(vantf(
uf, G_TYPEIZE, "%#lx\nth valt;t}
ETR_MEM_suizedd ();	eandTRood_ht-Fuize(gned loude <l *dev,
pppp      uned loude <l_attribl Pu*attr,
pppp      = "conONTIG*buf, uizedd uize)t{s_gned long		\
} whiu*T_UNLOCPE_dev_get_T_UNLOC(
#i	\parent ;Y_connect	conne#valHI3	butexde <l(&								\morye <l_TY	if (sscanf(
uf, "%lxth &valtU!= 1Kuct 	butexdune <l(&								\morye <l_TY	sreturn -EINVALTY	}sIbbeadl(drvht-Fuize = valHI_butexdune <l(&								\morye <l_TY	return size;s}ETR_MEM_DEVICE_ATTR(ht-Fuize,WS_IRUGO |WS_IWUSR, {p   ();	eandThow_ht-Fuize,W();	eandTRood_ht-Fuize)C_ETR_MEM_ssizedd ();	TMC_show_ht-FIG_T(gned loude <l *dev,
pppp     uned loude <l_attribl Pu*attr,
pppp     ONTIG*buf)h{s_gned long		\
} whiu*T_UNLOCPE_dev_get_T_UNLOC(
#i	\parent ;Yt_return scn(vantf(
uf, G_TYPEIZE, "%s\nth {p_gne_ase eandht-FIG_T[beadl(drvht-FIG_T]t;t}
ETR_MEM_suizedd ();	eandTRood_ht-FIG_T(gned loude <l *dev,
pppp      uned loude <l_attribl Pu*attr,
pppp      = "conONTIG*buf,
pppp      uizedd uize)t{s_gned long		\
} whiu*T_UNLOCPE_dev_get_T_UNLOC(
#i	\parent ;Y_ONTIGgne[10] ro"";Y
	if (gneeen(buf) >ro10K
ysreturn -EINVALTY	if (sscanf(
uf, "%h"vdgnetU!= 1K
ysreturn -EINVALTYIbbutexde <l(&								\morye <l_TY	if (!gnec"
(gnevdgne_ase eandht-FIG_T[	= "sg",
};

enum tmc_m]))uct 	beadl(drvht-FIG_T = 	= "sg",
};

enum tmc_m;hy} else if (!gnec"
(gnevdgne_ase eandht-FIG_T[	= "sg",
};

enumSm])t 	&&o								\ugdenabin)uct 		eadl(drvht-FIG_T = 	= "sg",
};

enumSm;hy} else ct 	butexdune <l(&								\morye <l_TY	sreturn -EINVALTY	}s	butexdune <l(&								\morye <l_TY
	return size;s}ETR_MEM_DEVICE_ATTR(ht-FIG_T,WS_IRUGO |WS_IWUSR, {p   ();	eandThow_ht-FIG_T,W();	eandTRood_ht-FIG_T)C_ETR_MEM_ssizedd ();	TMC_show_avaC)abin_ht-FIG_Ts(gned loude <l *dev,
pppp     uned loude <l_attribl Pu*attr,
pppp     ONTIG*buf)h{s_gned long		\
} whiu*T_UNLOCPE_dev_get_T_UNLOC(
#i	\parent ;Y_guizedd len rop;T	ta- i;Y
	for (i rop; i < ARRAYPEIZE(gne_ase eandht-FIG_T)C i++) ct 	if (i r= 	= "sg",
};

enumSG && !								\ugdenabin)
ppptf_winue;t3	teen += scn(vantf(
uf + len,oG_TYPEIZE - len,o"%h "h {p_gne_ase eandht-FIG_T[i])ps	}sIbeen += scn(vantf(
uf + len,oG_TYPEIZE - len,o"\ntt;t	return een;t}
TR_MEM_DEVICE_ATTR(avaC)abin_ht-FIG_Ts,WS_IRUGOh {p();	TMC_show_avaC)abin_ht-FIG_Ts, NULL)C_ETR_MEM_sned loattribl Pu*();	attrs[] E_SG]&dev_attr_trigger_cnan.attr,
pNULL,
MC_ETR_MEM_sned loattribl P_group ();	attr_grp E_SG].attrs ro();	attrs,
MC_ETR_MEM_sned loattribl Pu*();	TMC_attrs[] E_SG]&dev_attr_oC_ETR_O.attr,
p&dev_attr_avaC)abin_oC_ETR_Os.attr,
p&dev_attr_
yte_cnand__rue.attr,
p&dev_attr_ht-Fuize.attr,
p&dev_attr_ht-FIG_T.attr,
p&dev_attr_avaC)abin_ht-FIG_Ts.attr,
pNULL,
MC_ETR_MEM_sned loattribl P_group ();	TMC_attr_grp E_SG].attrs ro();	TMC_attrs,
MC_ETR_MEM_TYPE_Ctigd loattribl P_group *ase eab
attr_grps[] E_SG]&();	attr_grp,
pNULL,
MC_ETR_MEM_TYPE_Ctigd loattribl P_group *ase ear
attr_grps[] E_SG]&();	attr_grp,
p&ase ear
attr_grp,
pNULL,
MC_ETR_MEM_TYPE_Ctigd loattribl P_group *ase eaf
attr_grps[] E_SG]&();	attr_grp,
pNULL,
MC_ETR_MEM_ta- ();	TMC_
aTF_Wit(gned loplatformlude <l *pdev,
pppp      uned long		\
} whiu*T_UNLOCK
{s_gned loude <l *dev E_&p
#i	\dev;s_gned lotriour<l *resps	uned long		TMC_le;
	whiu*le;NLOC;s
	le;NLOCPE_devm_kzale <(dev, uizeof(*le;NLOC), GFP_KERNWMKTYtif (!le;NLOC)Y	sreturn -ENOc_e;s_								\
e;NLOCPE_le;NLOC;s
	res =_platformlget_triour<l_
yname(pdev, ISZ		OURCtmc_e,o"le;-bast"KTYtif (!res) {preturn -ENODEV;t
	le;NLOC->(vops.MC_ETR_SG rores	\sMar ;
	le;NLOC->(vops.virt_R_SG rodevm_ioodmap(dev, res	\sMar ,hys_p_ptriour<l_uize(res)KTYtif (!le;NLOC->(vops.virt_R_SG)Y	sreturn -ENOc_e;s_le;NLOC->(vops.virt_uize = triour<l_uize(res);t
	le;NLOC->(vops.OR At_thresholt E_0x4; /* Pi_T rv At thresholt *udtle;NLOC->(vops.summing_thresholt E_0x10; /* BAM rv At thresholt *udtle;NLOC->(vops.irq rop;T	le;NLOC->(vops._ET	pi_Ts = 	= "sg",BAM_NR_PIPESTY
	return sps + gisteC_le;
	de <l(&le;NLOC->(vopsh &
e;NLOC->hand)
Kps}
ETR_MEM_void ,
	TTMC_le;
exit(gned long		\
} whiu*T_UNLOCK
{s_gned long		TMC_le;
	whiu*le;NLOC ro								\
e;NLOC;s
	if (!le;NLOC->hand)
K
ysreturnps	sps de+ gisteC_le;
	de <l(
e;NLOC->hand)
Kps}
ETR_MEM_irqreturndd ();	TMC_
yte_cnandirq(ta- irq, void *NLOCK
{s_gned long		\
} whiu*T_UNLOCPE_dLOC;s
	atomicdinc(&								\
yte_cnandirq_cnat;t	if (atomicd				(&								\
yte_cnandirq_cnat >t 	pbeadl(drv
yte_cnandoverflow_cnat {
		dev_err_ratelimited									\dev, "Byte counterioverflow\ntt;t		beadl(drv
yte_cnandoverflow E_drueps	}s	wake_up(&								\wqt;t	return IRQ_HANDLEDps}
ETR_MEM_ta- ();	TMC_
yte_cnanddev_+ gisteC(gned long		\
} whiu*T_UNLOCK
{s_ta- retps gned loude <l *dev <lTY		ev_loude;
Y	ret roale <_chrdev_+ gion(&dev, 0, 1, beadl(drv
yte_cnandnR_O)C_	if (ret)hysgoto err0;t3	cdev_init(&								\
yte_cnanddev, &
yte_cnandfops)TYIbbeadl(drv
yte_cnanddev.ow< n BITHIStr_oULEHIbbeadl(drv
yte_cnanddev.ops E_&
yte_cnandfops;Y	ret rocdev_R_S(&								\
yte_cnanddev, dev, 1)C_	if (ret)hysgoto err1TYIbbeadl(drv
yte_cnandclass E_class_c			te(THIStr_oULE,
						beadl(drv
yte_cnandnR_O)C_	if (IS_ERR(beadl(drv
yte_cnandclass))uct 	ret roPg",ERR(beadl(drv
yte_cnandclass);hysgoto err2;t	}Y
	dev <l =oude <l_c			te(beadl(drv
yte_cnandclass, NULLh {p_       beadl(drv
yte_cnanddev.dev, d						,hys_       beadl(drv
yte_cnandnR_O)C_	if (IS_ERR(bde <l))uct 	ret roPg",ERR(bde <l);hysgoto err3;t	}Y
	return 0;Terr3:
	class__Ostroy(beadl(drv
yte_cnandclass);herr2:3	cdev_del(&								\
yte_cnanddev);serr1:Y	un+ gisteC_chrdev_+ gion(beadl(drv
yte_cnanddev.dev, 1_TYerr0:Y	return retps}
ETR_MEM_void ,
	TTMC_
yte_cnanddev_de+ gisteC(gned long		\
} whiu*T_UNLOCK
{s_ude <l__Ostroy(beadl(drv
yte_cnandclass, beadl(drv
yte_cnanddev.dev ;Y_Olass__Ostroy(beadl(drv
yte_cnandclass);h	cdev_del(&								\
yte_cnanddev);s	un+ gisteC_chrdev_+ gion(beadl(drv
yte_cnanddev.dev, 1_TY}
ETR_MEM_ta- ();	TMC_
yte_cnand_Wit(gned loplatformlude <l *pdev,
pppp  gned long		\
} whiu*T_UNLOCK
{s_ta- ret rop;T	uizedd nR_O_uize = gneeen("-gneeam") + 1ps	ONTIG*nR_O_name E_(ONTIG])((gned loaw_writellplatformluwhiu*)
ppp(p
#i	\dev.platformluwhi))->name;t3	if (!								\
yte_cnandptribntK {
		dev_info(&p
#i	\dev, "Byte CounterifeatureIabibnt\ntt;t		goto SS ;
	}Y
	_Wit_	
#iqueue_h			(&								\wqt;tIbbeadl(drv
yte_cnandirq roplatformlget_irq_
yname(pdev,
							"
yte-cnan-irq"KTYtif (beadl(drv
yte_cnandirq < 0)uct 	/* Evnnl(hough ; t uis_da error_TYPdidion, we doOnotcfaC)
p_ *l(he (vobeIas PART
yte counterifeatureITHOopdiona)
p_ */
		dev_err(&p
#i	\dev, "Byte-cnan-irqOnotcspecified\ntt;t		goto err;s_}sY	ret rodevm_request_irq(&p
#i	\dev, beadl(drv
yte_cnandirqh {p_();	TMC_
yte_cnandirqh {p_IRQF_TRIGGER_RISING | IRQF_SHAREDh {p_nR_O_name, beadl(d)C_	if (ret)uct 		ev_err	&p
#i	\dev, "Request irqpfaC)
d\ntt;t		goto err;s_}sY	nR_O_uize += gneeen(nR_O_namet;tIbbeadl(drv
yte_cnandnR_OUr devm_kzale <(&p
#i	\dev,hys_p_       nR_O_uize, GFP_KERNWMKTYtif (!beadl(drv
yte_cnandnR_O)uct 		ev_err	&p
#i	\dev, "Byte cnancnR_OUname ale < willpfaC)
d\ntt;t		ret ro-ENOc_e;s_	goto err;s_}sY	gneecpy(beadl(drv
yte_cnandnR_O, nR_O_name, nR_O_uize);Y	gneecat(beadl(drv
yte_cnandnR_O, "-gneeam", nR_O_uize);YY	ret ro();	TMC_
yte_cnanddev_+ gisteC(beadl(d)C_	if (ret)uct 		ev_err	&p
#i	\dev, "Byte cnancnR_OUno- registeC
d\ntt;t		goto err;s_}sY	dev_info(&p
#i	\dev, "Byte CounterifeatureIenabind\ntt;t	return 0;Terr:
									\
yte_cnandptribnt E_falsepsSS :
	return retps}
ETR_MEM_void ,
	TTMC_
yte_cnandexit(gned long		\
} whiu*T_UNLOCK
{s_if (beadl(drv
yte_cnandptribntK
ys,
	TTMC_
yte_cnanddev_de+ gisteC(beadl(d)C_}
ETR_MEM_ta- ();	(vobe(gned loplatformlude <l *pdevK
{s_ta- retps ata->add ude dps gned loude <l *dev E_&p
#i	\dev;s_gned loaw_writellplatformluwhiu*pdLOC;s_gned long		\
} whiu*T_UNLOC;s_gned lotriour<l *resps	ata->add reg_uize;s TR_MEM_ta- etfeab
count;s TR_MEM_ta- count;s void *bR_SGps	gned lomsmdcli At_	h"
 	h"
ps	gned lomsmd	h"
_ AtryA	h"
_ Atry;s_gned loaw_writellMti_ whiu*MtiNLOC;s_gned loaw_writelludsc *desc;YY	pNLOCPE_oflget_aw_writellplatformluwhi(dev, p
#i	\dev.oflnR_O)C_	if (IS_ERR(puwhi))Y	sreturn Pg",ERR(pdl(d)C_	p
#i	\dev.platformluwhi ropdLOC;s
	T_UNLOCPE_devm_kzale <(dev, uizeof(*beadl(d), GFP_KERNWMKTYtif (!beadl(d)Y	sreturn -ENOc_e;s_								\dev E_&p
#i	\dev;s_platformlset_T_UNLOC(pdev, beadl(d);s
	res =_platformlget_triour<l_
yname(pdev, ISZ		OURCtmc_e,o"ng	-bast"KTYtif (!res) {preturn -ENODEV;t	reg_uize = triour<l_uize(res);t
									\
esOUr devm_ioodmap(dev, res	\sMar , triour<l_uize(res)KTYtif (!beadl(drv
esO)Y	sreturn -ENOc_e;st_uludde <ldinit(&								\ulude <lSHI_butexdinit(&								\				_e <l_TY	butexdinit(&								\morye <l_TY	butexdinit(&								\
yte_cnande <lSHI_butexdinit(&								\
yte_cnand				_e <l_TY	atomicdset(&								\
yte_cnandirq_cna, 0)ps
									\clkO=4devm_clkdget(dev, "aw_w_clk")C_	if (IS_ERR(								\clk))Y	sreturn Pg",ERR(								\clk);YY	ret roclkdset_r	te(beadl(drvclk, CSZ		IGHT_CLK_RATE_TRACE)C_	if (ret)hysreturn retpsY	ret roclkdptrpare
enabin(T_UNLOC	\clk);Y	if (ret)hysreturn retpsY	if (!aw_writellauthTR_Mus
enabind									\
esO))uct 	alkddisabin_unptrpare(T_UNLOC->cll_TYt	return -EPERMTY_}s
									\force	reg_	h"
 E_ofl(voperty_r			_bool
pppp  (p
#i	\dev.oflnR_O,
pppp  "qcom,force-reg-	h"
");t
		evid rodefine tmc_readl_noCSZ		IGHT_DEVIDSHIbbeadl(drvMC_CONFIG_TYr BMVAL(bde d, 6, 7SHI3	if (beadl(drvMC_CONFIG_TYr= 	= "
};

enum tmc_R)uct 	ret roofl(voperty_r			_u32(p
#i	\dev.oflnR_O,
pppp_   "qcom,memory-uize",
pppp_   &								\uize);Y		if (ret)uct 		alkddisabin_unptrpare(T_UNLOC->cll_TYt	sreturn retps	b}hysbeadl(drvht-Fuize = 								\uize;hy} else ct 									\uize rodefine tmc_readl_noCR		RSZ) *lBYTES_PER_WORDTY_}s
	clkddisabin_unptrpare(T_UNLOC->cll_TY
	if (beadl(drvMC_CONFIG_TYr= 	= "
};

enum tmc_R)uct 	beadl(drvMC_ETR_OUr 	= "sg",

enum tmc_e;s_									\ugdenabin E_ofl(voperty_r			_bool(p
#i	\dev.oflnR_O,
pppp_p_   "qcom,sg-enabin");t
	bif (								\ug_enabin)
pppbeadl(drvht-IG_T = 	= "sg",
};

enumSm;hybelsehystbeadl(drvht-IG_T = 	= "sg",
};

enum tmc_m;ht 		eadl(drvht-FIG_T = beadl(drvht-IG_T;ht 		eadl(drv
yte_cnandptribnt E_!ofl(voperty_r			_bool
pppp_     (p
#i	\dev.oflnR_O,
pppp_     "qcom,
yte-cnan-abibnttt;t		ret ro();	TMC_
yte_cnand_Wit(pdev, beadl(d);s		if (ret)hys	goto err0;t_	ret ro();	TMC_
aTF_Wit(pdev, beadl(d);s		if (ret)hys	goto err1ps	} else ct 	if (MSM_DUMP_MAJOR(msmd	h"
_tabin_version())u== 1Kuct 		bR_SG rodevm_kzale <(dev, G_TYPEIZE + 								\uize,
pppp_     GFP_KERNWMKTYt		if (!le_SG)Y	s	sreturn -ENOc_e;s_ 		eadl(drv
ufUr bR_SGO+ G_TYPEIZEps
	 		h"
.id roMSM_	= "sgFETBO+ etfeab
count;s  		h"
.sMar _R_SG rov