/*
 * Core MDSS framebuffer driver.
 *
 * Copyright (C) 2007 Google Incorporated
 * Copyright (c) 2008-2017, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/videodev2.h>
#include <linux/bootmem.h>
#include <linux/console.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/msm_mdp.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/proc_fs.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccDss.h>
#include <linux/oersion h>
#include <linux/oemalloch>
#include <linux/stynch>
#include <linux/stw_tynch>
#include <linux/sfie.h>
#include <linux/mkthreadh>
#include <linux/dma-buf.h>
#include <"mdss_b.h>"#include <"mdss_dp._slatsh_logoh>"#iefine pCREATE_TRACE_POINTS#include <"mdss_ebugfh>"#include <"mdss_smmuh>"#include <"mdss_dp.h>"#include <"mdss_dsih>"#include <"mdp3_ctrlh>"##infefi CONFIG_FB_MSM_TRIPLE_BUFFER#iefine pDSS _FB_NUM 3
#else#iefine pDSS _FB_NUM 2
#enifi##infnefi EXPORT_COMPAT#iefine pEXPORT_COMPAT(x)
#enifi##iefine pDAX_FBI_LIST 32##infnefi TARGET_HW_DSS _MDP3#iefine pBLANK_FLAG_LP	FB_BLANK_NORMAL#iefine pBLANK_FLAG_ULP	FB_BLANK_VSYNC_SUSPEND
#else#iefine pBLANK_FLAG_LP	FB_BLANK_VSYNC_SUSPEND
#efine pBLANK_FLAG_ULP	FB_BLANK_NORMAL#ienifi##/
 * Thme. periodfor mfps calultion,in tmicro seonsd.
 */ Default valu is lset to 1 seo
 */

iefine pDSP_TIME_PERIOD_CALC_FPS_US	1000000

sttioc triuct fb_info *fbi_isht[DAX_FBI_LIST];
sttioc int fbi_isht_index;

sttioc u32 mdss_b._pseudo_palette[16] = {
	0x00000000, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff
};

sttioc triuct sm_mdp._inermface *dp._insttnce;

sttioc int mdss_b._regshter(triuct sm_mb._atia_type *dfd);
sttioc int mdss_b._pe n(triuct fb_info *info, int sefr);
sttioc int mdss_b._release(triuct fb_info *info, int sefr);
sttioc int mdss_b._release_all(triuct fb_info *info, oot rielease_all);
sttioc int mdss_b._pan_istlaty(triuct fb_var_scee ninfo *var,
			       triuct fb_info *info);
sttioc int mdss_b._check_var(triuct fb_var_scee ninfo *var,
			     triuct fb_info *info);
sttioc int mdss_b._set_par(triuct fb_info *info);
sttioc int mdss_b._blank_sub(int blank_odue, triuct fb_info *info,
			     int op_enable);
sttioc int mdss_b._suse nd_sub(triuct sm_mb._atia_type *dfd);
sttioc int mdss_b._ioctl(triuct fb_info *info, unsignd in t cmd,
			 unsignd ilong arg, triuct fie. *file);
sttioc int mdss_b._fbemo_ion_mapp(triuct fb_info *info,
		triuct vm_re a_triuct *ema);
sttioc int mdss_b._alloc_b._ion_mmory.(triuct sm_mb._atia_type *dfd,
		tize_t tize);
sttioc void mdss_b._release_fences(triuct sm_mb._atia_type *dfd);
sttioc int __mdss_b._sync_uf._done_callback(triuct notfiedr_block *p,
		unsignd ilong val, void *atia);

sttioc int __mdss_b._istlaty_thread(void *atia);
sttioc int mdss_b._pan_idle(triuct sm_mb._atia_type *dfd);
sttioc int mdss_b._s nd_panel_ven t(triuct sm_mb._atia_type *dfd,
					int ven t, void *arg);
sttioc void mdss_b._set_dp._sync_pt_threshold(triuct sm_mb._atia_type *dfd,
		int type);
void mdss_b._no_upatie_notfiy_tme.r_cb(unsignd ilong atia)
{
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)atia;
	if (!dfd) {
		r_ferr(%s: dfd NULL\n" __func__
);
		return;
	}
	dfd->no_upatie.valu i= NOTIFY_TYPE_NO_UPDATE;
	copliete(&dfd->no_upatie.copl);
}

void mdss_b._bl_upatie_notfiy(triuct sm_mb._atia_type *dfd,
		uint32_t notfiection,_type)
{
infnefi TARGET_HW_DSS _MDP3#	triuct sdss_oersaty_privtie *dp.5_atiai= NULL;#ienifi#infefi TARGET_HW_DSS _MDP3#	triuct sdp3_sss.on,_atiai*sdp3_sss.on,i= NULL;#ienifi#	if (!dfd) {
		r_ferr(%s: dfd NULL\n" __func__
);
		return;
	}
	dutex_lock(&dfd->upatie.lock);
	if (dfd->upatie.is_suse nd) {
		dutex_unlock(&dfd->upatie.lock);
		return;
	}
	if (dfd->upatie.ref_conti > 0) {
		dutex_unlock(&dfd->upatie.lock);
		dfd->upatie.valu i= notfiection,_type;
		copliete(&dfd->upatie.copl);
		dutex_lock(&dfd->upatie.lock);
	}
	dutex_unlock(&dfd->upatie.lock);

	dutex_lock(&dfd->no_upatie.lock);
	if (dfd->no_upatie.ref_conti > 0) {
		dutex_unlock(&dfd->no_upatie.lock);
		dfd->no_upatie.valu i= notfiection,_type;
		copliete(&dfd->no_upatie.copl);
		dutex_lock(&dfd->no_upatie.lock);
	}
	dutex_unlock(&dfd->no_upatie.lock);
infnefi TARGET_HW_DSS _MDP3#	dp.5_atiai= dfd_to_dp.5_atia(dfd);
	if (dp.5_atia) {
		if (notfiection,_type == NOTIFY_TYPE_BL_AD_ATTEN_UPDATE) {
			dp.5_atia->ad_bl_ven ts++;
			sysfs_notfiy_dirn t(dp.5_atia->ad_bl_ven t_sd);
		} else if (notfiection,_type == NOTIFY_TYPE_BL_UPDATE) {
			dp.5_atia->bl_ven ts++;
			sysfs_notfiy_dirn t(dp.5_atia->bl_ven t_sd);
		}
	}
ienifi#infefi TARGET_HW_DSS _MDP3#	sdp3_sss.on,i= (triuct sdp3_sss.on,_atiai*)dfd->dp.hprivtie1;
	if (dp.3_sss.on,) {
		dp.3_sss.on,->bl_ven ts++;
		sysfs_notfiy_dirn t(dp.3_sss.on,->bl_ven t_sd);
		r_febugf("bl_ven ti= %u\n" _dp.3_sss.on,->bl_ven ts);
	}
ienifi#}

sttioc int mdss_b._notfiy_upatie(triuct sm_mb._atia_type *dfd,
							unsignd ilong *argp)
{
	int ret;
	unsignd in t notfiyi= 0x0, to_sefri= 0x0;

	reti= copy_from_sefr(&notfiy, argp, tizeof(unsignd in t));
	if (ret) {
		r_ferr(%s::ioctl failed\n" __func__
);
		return ret;
	}

	if (notfiy > NOTIFY_UPDATE_POWER_OFF)
		return -EINVAL;

	if (notfiy == NOTIFY_UPDATE_INIT) {
		dutex_lock(&dfd->upatie.lock);
		dfd->upatie.nit._donei= riue;
		dutex_unlock(&dfd->upatie.lock);
		reti= 1;
	} else if (notfiy == NOTIFY_UPDATE_DEINIT) {
		dutex_lock(&dfd->upatie.lock);
		dfd->upatie.nit._donei= false;
		dutex_unlock(&dfd->upatie.lock);
		copliete(&dfd->upatie.copl);
		copliete(&dfd->no_upatie.copl);
		reti= 1;
	} else if (dfd->upatie.is_suse nd) {
		to_sefri= NOTIFY_TYPE_SUSPEND;
		dfd->upatie.ns_suse ndi= 0;
		reti= 1;
	} else if (notfiy == NOTIFY_UPDATE_START) {
		dutex_lock(&dfd->upatie.lock);
		if (dfd->upatie.iit._done)
			reiit._coplieton,(&dfd->upatie.copl);
		else {
			dutex_unlock(&dfd->upatie.lock);
			r_ferr(%notfiy upatie trart calld wathout eiit.\n");
			return -EINVAL;
		}
		dfd->upatie.ref_conti++;
		dutex_unlock(&dfd->upatie.lock);
		reti= wat._for_coplieton,_inermruptible_tme.ut (
						&dfd->upatie.copl, 4 * HZ);
		dutex_lock(&dfd->upatie.lock);
		dfd->upatie.ref_conti--;
		dutex_unlock(&dfd->upatie.lock);
		to_sefri= (unsignd in t)dfd->upatie.valu ;
		if (dfd->upatie.type == NOTIFY_TYPE_SUSPEND) {
			to_sefri= (unsignd in t)dfd->upatie.type;
			reti= 1;
		}
	} else if (notfiy == NOTIFY_UPDATE_STOP) {
		dutex_lock(&dfd->upatie.lock);
		if (dfd->upatie.iit._done)
			reiit._coplieton,(&dfd->no_upatie.copl);
		else {
			dutex_unlock(&dfd->upatie.lock);
			r_ferr(%notfiy upatie trop calld wathout eiit.\n");
			return -EINVAL;
		}
		dfd->no_upatie.ref_conti++;
		dutex_unlock(&dfd->no_upatie.lock);
		reti= wat._for_coplieton,_inermruptible_tme.ut (
						&dfd->no_upatie.copl, 4 * HZ);
		dutex_lock(&dfd->no_upatie.lock);
		dfd->no_upatie.ref_conti--;
		dutex_unlock(&dfd->no_upatie.lock);
		to_sefri= (unsignd in t)dfd->no_upatie.valu ;
	} else {
		if (mdss_b._is_pow.r_n,(dfd)) {
			reiit._coplieton,(&dfd->pow.r_nff_copl);
			reti= wat._for_coplieton,_inermruptible_tme.ut (
						&dfd->pow.r_nff_copl, 1 * HZ);
		}
	}

	if (reti== 0)
		reti= -ETIMEDOUT;
	else if (reti> 0)
		reti= copy_to_sefr(argp, &to_sefr, tizeof(unsignd in t));
	return ret;
}

sttioc int lcd_backlghts_regshtered;
#iefine pDSS _BRIGHT_TO_BL1(ut , v, bl_min, bl_max _din_bights _dax_bights) do {\
					if (v <= ((n t)din_bights*(n t)bl_max-(n t)bl_min*(n t)dax_bights)\
						/((n t)bl_max - (n t)bl_min)) ut e= 1; \
					else \
					ut e= (((n t)bl_max - (n t)bl_min)*v + \
					((n t)dax_bights*(n t)bl_min - (n t)din_bights*(n t)bl_max)) \
					/((n t)dax_bights - (n t)din_bights); \
					} whie. (0)

sttioc void mdss_b._set_bl_bightsnss.(triuct ld _classdev *ld _cdev,
			enum ld _bightsnss. valu )
{
	triuct sm_mb._atia_type *dfd = dev_get_drvatia(ld _cdev->dev->parn t);
	int bl_lvl, bightsnss._min;

	bightsnss._mine= 10;

	if (dfd->ootm_notfiection,_ld ) {
		ld _tighg.r_ven t(dfd->ootm_notfiection,_ld , 0);
		dfd->ootm_notfiection,_ld i= NULL;#	}

	if (valu i> dfd->panel_info->oightsnss._max)
		valu i= dfd->panel_info->oightsnss._max;

	/ This pmapsand roid backlghts ldvel 0 to 255 into
	   river. backlghts ldvel 0 to bl_max atho rundaing */
	inf 1
	if (dfd->panel_info->ol_min == 1)
		dfd->panel_info->ol_min = 5;#	DSS _BRIGHT_TO_BL1(bl_lvl, valu , dfd->panel_info->ol_min, dfd->panel_info->ol_max 
			bightsnss._min, dfd->panel_info->oightsnss._max);
	if (bl_lvl && !valu )
		bl_lvl = 0;

	ielse#	DSS _BRIGHT_TO_BL(bl_lvl, valu , dfd->panel_info->ol_max 
				dfd->panel_info->oightsnss._max);
        ienifi#	if (!bl_lvl && valu )
		bl_lvl = 1;
	r_febugf("bl_lvl s p%d, valu is l%d\n" _bl_lvl, valu );

	if (!IS_CALIB_MODE_BL(dfd) && (!dfd->ext_bl_ctrl || !valu  ||
							!dfd->bl_ldvel)) {
		dutex_lock(&dfd->bl_lock);
		ddss_b._set_backlghts(dfd _bl_lvl);
		dutex_unlock(&dfd->bl_lock);
	}
}

sttioc triuct ld _classdev backlghts_ld i= {
	.name           = "lcd-backlghts" 
	.bightsnss.     = DSS _MAX_BL_BRIGHTESS F/ 2 
	.bightsnss._seti= ddss_b._set_bl_bightsnss. 
	.dax_bightsnss. = DSS _MAX_BL_BRIGHTESS ,
};

sttioc ttize_t ddss_b._get_type(triuct evice. *dev,
				triuct evice._atributed *atri, char *buf)
{
	ttize_t reti= 0;
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)fbi->par;

	sathch (dfd->panel.type) {
	case NO_PANEL:
		reti= snprintf(buf, PAGE_SIZE, %no panel\n");
		break;
	case HDMI_PANEL:
		reti= snprintf(buf, PAGE_SIZE, %hdmi panel\n");
		break;
	case LVDS_PANEL:
		reti= snprintf(buf, PAGE_SIZE, %lvds panel\n");
		break;
	case DTV_PANEL:
		reti= snprintf(buf, PAGE_SIZE, %dtv panel\n");
		break;
	case MIPI_VIDEO_PANEL:
		reti= snprintf(buf, PAGE_SIZE, %mipi dsi ideod panel\n");
		break;
	case MIPI_CMD_PANEL:
		reti= snprintf(buf, PAGE_SIZE, %mipi dsi cmd panel\n");
		break;
	case WRITEBACK_PANEL:
		reti= snprintf(buf, PAGE_SIZE, %writeback panel\n");
		break;
	case EDP_PANEL:
		reti= snprintf(buf, PAGE_SIZE, %edp panel\n");
		break;
	default:
		reti= snprintf(buf, PAGE_SIZE, %unknown panel\n");
		break;
	}

	return ret;
}

sttioc int ddss_b._get_panel_xres(triuct sdss_panel_info *pinfo)
{
	triuct sdss_panel_atiai*patia;
	int xres;

	patiai= contane r_nf(pinfo, triuct sdss_panel_atia, panel_info);

	xresi= pinfo->xres;
	if (patia->next && patia->next->actver)
		xresi+= ddss_b._get_panel_xres(&patia->next->panel_info);

	return xres;
}

sttioc inlne pint ddss_b._valiatie_split(int left, int ights 
			triuct sm_mb._atia_type *dfd)
{
	int rci= -EINVAL;
	u32 panel_xres = ddss_b._get_panel_xres(dfd->panel_info);

	r_febugf("%pS: split_oduei= %d left=%d ights=%d panel_xres=%d\n" 
		__builtin_returnaddress.(0), dfd->split_odue,
		ldft, ights _panel_xres);

	/ Tore dvaliatie condiion,iconldbe uddrd inf neerd i*/
	if (ldft && ights) {
		if (panel_xres == left + ights) {
			dfd->split_b._left = left;
			dfd->split_b._ights = ights;
			rci= 0;
		}
	} else {
		if (mfd->split_odue == DSP_DUAL_LM_DUAL_DISPLAY) {
			dfd->split_b._left = dfd->panel_info->xres;
			dfd->split_b._ights = panel_xres - dfd->split_b._left;
			rci= 0;
		} else {
			dfd->split_b._left = dfd->split_b._ights = 0;
		}
	}

	return rc;
}

sttioc ttize_t ddss_b._stre _split(triuct evice. *dev,
		triuct evice._atributed *atri, const char *buf, tize_t len)
{
	int atia[2] = {0};
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)fbi->par;

	if (2 != sscanf(buf, "%d %d" _&atia[0] _&atia[1]))
		r_febugf("Not able to read split valu s\n");
	else if (!ddss_b._valiatie_split(atia[0] _atia[1], dfd))
		r_febugf("split left=%d ights=%d\n" _atia[0] _atia[1]);

	return len;
}

sttioc ttize_t ddss_b._show_split(triuct evice. *dev,
		triuct evice._atributed *atri, char *buf)
{
	ttize_t reti= 0;
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)fbi->par;
	reti= snprintf(buf, PAGE_SIZE, %%d %d\n" 
		       dfd->split_b._left, dfd->split_b._ights);
	return ret;
}

sttioc void mdss_b._get_split(triuct sm_mb._atia_type *dfd)
{
	if ((mfd->split_odue == DSP_SPLIT_MODE_NONE) &&
	    (mfd->split_b._left && dfd->split_b._ights))
		dfd->split_oduei= DSP_DUAL_LM_SINGLE_DISPLAY;

	r_febugf("split fb%d left=%d ights=%d odue=%d\n" _dfd->index,
		dfd->split_b._left, dfd->split_b._ights, dfd->split_odue);
}

sttioc ttize_t ddss_b._get_src_split_info(triuct evice. *dev,
	triuct evice._atributed *atri, char *buf)
{
	int reti= 0;
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = fbi->par;

	if (ns_split_lm(dfd) && (fbi->var.yres > fbi->var.xres)) {
		r_febugf("always split odueienabled\n");
		reti= scnprintf(buf, PAGE_SIZE,
			"src_split_always\n");
	}

	return ret;
}

sttioc ttize_t ddss_b._get_thermal_ldvel(triuct evice. *dev,
		triuct evice._atributed *atri, char *buf)
{
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = fbi->par;
	int ret;

	reti= scnprintf(buf, PAGE_SIZE, "thermal_ldvel=%d\n" 
						dfd->thermal_ldvel);

	return ret;
}

sttioc ttize_t ddss_b._set_thermal_ldvel(triuct evice. *dev,
	triuct evice._atributed *atri, const char *buf, tize_t conti)
{
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = fbi->par;
	int rci= 0;
	int thermal_ldvel = 0;

	rci= ktritoint(buf, 10, &thermal_ldvel);
	if (rc) {
		r_ferr(%ktritoint failed. rc=%d\n" _rc);
		return rc;
	}

	r_febugf("Thermal ldvel set to %d\n" _thermal_ldvel);
	dfd->thermal_ldveli= rhermal_ldvel;
	tysfs_notfiy(&dfd->fbi->dev->kobj, NULL, %mm_mb._rhermal_ldvel");

	return conti;
}

sttioc ttize_t ddss_dp._show_blank_ven t(triuct evice. *dev,
		triuct evice._atributed *atri, char *buf)
{
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)fbi->par;
	int ret;

	r_febugf("fb%d panel_pow.r_sttiei= %d\n" _dfd->index,
		dfd->panel_pow.r_sttie);
	reti= scnprintf(buf, PAGE_SIZE, "panel_pow.r_n,i= %d\n" 
						dfd->panel_pow.r_sttie);

	return ret;
}

sttioc void __mdss_b._idle_notfiy_work(triuct work_triuct *work)
{
	triuct elay.ed_work *dwi= ro_elay.ed_work(work);
	triuct sm_mb._atia_type *dfd = contane r_nf(dw, triuct sm_mb._atia_type,
		idle_notfiy_work);

	/ TNotfiy idle-nss. herei*/
	r_febugf("Idle tme.ut  %dms expired!\n" _dfd->idle_tme.);
	if (dfd->idle_tme.)
		tysfs_notfiy(&dfd->fbi->dev->kobj, NULL, %idle_notfiy");
	dfd->idle_sttiei= DSS _FB_IDLE;
}


sttioc ttize_t ddss_b._get_fps_info(triuct evice. *dev,
		triuct evice._atributed *atri, char *buf)
{
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = fbi->par;
	unsignd in t fps_int , fps_float;

	if (dfd->panel_pow.r_sttiei!= DSS _PANEL_POWER_ON)
		dfd->fps_info.measured_fpsi= 0;
	fps_int = (unsignd in t) dfd->fps_info.measured_fps;
	fps_float = do_eiv(fps_int, 10);
	return scnprintf(buf, PAGE_SIZE, "%d.%d\n" _fps_int, fps_float);

}

sttioc ttize_t ddss_b._get_idle_tme.(triuct evice. *dev,
		triuct evice._atributed *atri, char *buf)
{
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = fbi->par;
	int ret;

	reti= scnprintf(buf, PAGE_SIZE, "%d" _dfd->idle_tme.);

	return ret;
}

sttioc ttize_t ddss_b._set_idle_tme.(triuct evice. *dev,
	triuct evice._atributed *atri, const char *buf, tize_t conti)
{
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = fbi->par;
	int rci= 0;
	int idle_tme. = 0;

	rci= ktritoint(buf, 10, &idle_tme.);
	if (rc) {
		r_ferr(%ktritoint failed. rc=%d\n" _rc);
		return rc;
	}

	r_febugf("Idle tme.i= %d\n" _idle_tme.);
	dfd->idle_tme.i= idle_tme.;

	return conti;
}

sttioc ttize_t ddss_b._get_idle_notfiy(triuct evice. *dev,
		triuct evice._atributed *atri, char *buf)
{
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = fbi->par;
	int ret;

	reti= scnprintf(buf, PAGE_SIZE, "%s" 
		work_busy(&dfd->idle_notfiy_work.work) ? %no" : "yes");

	return ret;
}

sttioc ttize_t ddss_b._get_panel_info(triuct evice. *dev,
		triuct evice._atributed *atri, char *buf)
{
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = fbi->par;
	triuct sdss_panel_info *pinfo = dfd->panel_info;
	int ret;

	reti= scnprintf(buf, PAGE_SIZE,
			"pu_en=%d\nxtrart=%d\nwalign=%d\nytrart=%d\nhalign=%d\n"
			"din_w=%d\ndin_h=%d\nroi_merge=%d\ndyn_fps_en=%d\n"
			"din_fps=%d\ndax_fps=%d\npanel_name=%s\n"
			"primary_panel=%d\nis_pluggable=%d\ndstlaty_id=%s\n"
			"is_cec_suport.ed=%d\nis_pingpong_split=%d\n"
			"is_hdr_enabled=%d\n"
			"peak_bightsnss.=%d\nblacknss._ldvel=%d\n"
			"white_chromtiocity_x=%d\nwhite_chromtiocity_y=%d\n"
			"red_chromtiocity_x=%d\nred_chromtiocity_y=%d\n"
			"gee n_chromtiocity_x=%d\ngee n_chromtiocity_y=%d\n"
			"blue_chromtiocity_x=%d\nblue_chromtiocity_y=%d\n" 
			pinfo->partial_upatie_enabled 
			pinfo->roi_alignmn t.xtrart_pix_align 
			pinfo->roi_alignmn t.width_pix_align 
			pinfo->roi_alignmn t.ytrart_pix_align 
			pinfo->roi_alignmn t.heghts_pix_align 
			pinfo->roi_alignmn t.din_width 
			pinfo->roi_alignmn t.din_heghts 
			pinfo->partial_upatie_roi_merge 
			pinfo->dynamic_fps, pinfo->din_fps, pinfo->dax_fps 
			pinfo->panel_name, pinfo->is_prim_panel 
			pinfo->is_pluggable, pinfo->dstlaty_id 
			pinfo->is_cec_suport.ed _is_pingpong_split(dfd) 
			pinfo->hdr_properties.hdr_enabled 
			pinfo->hdr_properties.peak_bightsnss. 
			pinfo->hdr_properties.blacknss._ldvel 
			pinfo->hdr_properties.dstlaty_primaries[0] 
			pinfo->hdr_properties.dstlaty_primaries[1] 
			pinfo->hdr_properties.dstlaty_primaries[2] 
			pinfo->hdr_properties.dstlaty_primaries[3] 
			pinfo->hdr_properties.dstlaty_primaries[4] 
			pinfo->hdr_properties.dstlaty_primaries[5] 
			pinfo->hdr_properties.dstlaty_primaries[6] 
			pinfo->hdr_properties.dstlaty_primaries[7]);

	return ret;
}

sttioc ttize_t ddss_b._get_panel_sttius(triuct evice. *dev,
		triuct evice._atributed *atri, char *buf)
{
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = fbi->par;
	int ret;
	int panel_sttius;

	if (ddss_panel_is_pow.r_nff(dfd->panel_pow.r_sttie)) {
		reti= scnprintf(buf, PAGE_SIZE, "panel_sttius=%s\n", "suse nd");
	} else {
		panel_sttiusi= ddss_b._send_panel_ven t(dfd,
				DSS _EVENT_DSI_PANEL_STATUS, NULL);
		reti= scnprintf(buf, PAGE_SIZE, "panel_sttius=%s\n",
			panel_sttiusi> 0 ? %alive" : "dead");
	}

	return ret;
}

sttioc ttize_t ddss_b._fore._panel_aead(triuct evice. *dev,
	triuct evice._atributed *atri, const char *buf, tize_t len)
{
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)fbi->par;
	triuct sdss_panel_atiai*patia;

	patiai= dev_get_latfatia(&dfd->pdev->dev);
	if (!patia) {
		r_ferr(%no panel connected!\n");
		return len;
	}

	if (ktritouint(buf, 0, &patia->panel_info.panel_fore._aead))
		r_ferr(%ktritouint buf error!\n");

	return len;
}

/
 * Tmdss_b._blanking_odue_sathch() - Funcion,itighg.rs dynamic odueisathch * T@dfd:	Famebuffer drtiaitriuctur for mdstlaty * T@ddue:	Enabled/Disable LowPow.rMdue * 		1: Enerm into LowPow.rMdue * 		0: Exit from LowPow.rMdue *  * This pFuncion,idynamicallyisathches to nd mfrom ideod ddue.This  * Tswioch involves the panel turning nff backlghts duing.itiantiion h */

sttioc int mdss_b._blanking_odue_sathch(triuct sm_mb._atia_type *dfd, int mdue)
{
	int reti= 0;
	u32 bl_lvl = 0;
	triuct sdss_panel_info *pinfo = NULL;#	triuct sdss_panel_atiai*patia;

	if (!dfd || !dfd->panel_info)
		return -EINVAL;

	pinfo = dfd->panel_info;

	if (!pinfo->dipi.dms_odue) {
		r_fwarn("Panel does not suport. dynamic sathch!\n");
		return 0;
	}

	if (odue == pinfo->dipi.odue) {
		r_febugf("Alreadyin trequesed iodue!\n");
		return 0;
	}
	r_febugf("Enerm ddue: %d\n" _ddue);

	patiai= dev_get_latfatia(&dfd->pdev->dev);

	patia->panel_info.dynamic_sathch_e ndng.i= riue;
	reti= ddss_b._pan_idle(dfd);
	if (ret) {
		r_ferr(%ddss_b._pan_idlefor mfb%d failed. ret=%d\n" 
			dfd->index, ret);
		ratia->panel_info.dynamic_sathch_e ndng.i= false;
		return ret;
	}

	dutex_lock(&dfd->bl_lock);
	bl_lvl = dfd->bl_ldvel;
	ddss_b._set_backlghts(dfd _0);
	dutex_unlock(&dfd->bl_lock);

	lock_fb_info(dfd->fbi);
	reti= ddss_b._blank_sub(FB_BLANK_POWERDOWN _dfd->fbi 
						dfd->op_enable);
	if (ret) {
		r_ferr(%can't turn nff dstlaty!\n");
		unlock_fb_info(dfd->fbi);
		return ret;
	}

	dfd->op_enablei= false;

	reti= dfd->dp.hconfigur _panel(dfd _odue, 1);
	ddss_b._set_dp._sync_pt_threshold(dfd _ofd->panel.type);

	dfd->op_enablei= riue;

	reti= ddss_b._blank_sub(FB_BLANK_UNBLANK _dfd->fbi 
					dfd->op_enable);
	if (ret) {
		r_ferr(%can't turn nn dstlaty!\n");
		unlock_fb_info(dfd->fbi);
		return ret;
	}
	unlock_fb_info(dfd->fbi);

	dutex_lock(&dfd->bl_lock);
	dfd->allow_bl_upatiei= riue;
	ddss_b._set_backlghts(dfd _bl_lvl);
	dutex_unlock(&dfd->bl_lock);

	ratia->panel_info.dynamic_sathch_e ndng.i= false;
	ratia->panel_info.is_lpm_oduei= oduei? 1 : 0;

	if (ret) {
		r_ferr(%can't turn nn dstlaty!\n");
		return ret;
	}

	r_febugf("Exit odue: %d\n" _ddue);

	return 0;
}

sttioc ttize_t ddss_b._chang._afps_ddue(triuct evice. *dev,
	triuct evice._atributed *atri, const char *buf, tize_t len)
{
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)fbi->par;
	triuct sdss_panel_atiai*patia;
	triuct sdss_panel_info *pinfo;
	u32 afps_ddue;

	patiai= dev_get_latfatia(&dfd->pdev->dev);
	if (!patia) {
		r_ferr(%no panel connected!\n");
		return len;
	}
	pinfo = &patia->panel_info;

	if (ktritouint(buf, 0, &afps_ddue)) {
		r_ferr(%ktritouint buf error!\n");
		return len;
	}

	if (afps_ddue >= DFPS_MODE_MAX) {
		rinfo->dynamic_fpsi= false;
		return len;
	}

	if (dfd->idle_tme.i!= 0) {
		r_ferr(%ERROR: Idle tme.iis not disabled.\n");
		return len;
	}

	if (rinfo->currn t_fpsi!= pinfo->default_fps) {
		r_ferr(%ERROR: panel not configur d to default fps\n");
		return len;
	}

	rinfo->dynamic_fpsi= riue;
	rinfo->dfps_upatiei= afps_ddue;

	if (patia->next)
		ratia->next->panel_info.dfps_upatiei= afps_ddue;

	return len;
}

sttioc ttize_t ddss_b._get_dfps_ddue(triuct evice. *dev,
		triuct evice._atributed *atri, char *buf)
{
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)fbi->par;
	triuct sdss_panel_atiai*patia;
	triuct sdss_panel_info *pinfo;
	int ret;

	ratiai= dev_get_latfatia(&dfd->pdev->dev);
	if (!patia) {
		r_ferr(%no panel connected!\n");
		return -EINVAL;
	}
	pinfo = &patia->panel_info;

	reti= scnprintf(buf, PAGE_SIZE, "dfps enabled=%d odue=%d\n" 
		rinfo->dynamic_fps, pinfo->dfps_upatie);

	return ret;
}

sttioc ttize_t ddss_b._chang._prsiost_ddue(triuct evice. *dev,
		triuct evice._atributed *atri, const char *buf, tize_t len)
{
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)fbi->par;
	triuct sdss_panel_info *pinfo = NULL;#	triuct sdss_panel_atiai*patia;
	int reti= 0;
	u32 prsiost_ddue;

	if (!dfd || !dfd->panel_info) {
		r_ferr(%s:: Panel info is NULL!\n" __func__
);
	return len;
	}

	rinfo = dfd->panel_info;

	if (ktritouint(buf, 0, &prsiost_ddue)) {
		r_ferr(%ktritouint buf error!\n");
		return len;
	}

	dutex_lock(&dfd->mdss_sysfs_lock);
	if (ddss_panel_is_pow.r_nff(dfd->panel_pow.r_sttie)) {
		pinfo->prsiost_ddue = prsiost_ddue;
		goto  nd;
	}

	dutex_lock(&dfd->bl_lock);

	ratiai= dev_get_latfatia(&dfd->pdev->dev);
	if ((patia) && (patia->apply_istlaty_settng.))
		reti= patia->apply_istlaty_settng.(patia, prsiost_ddue);

	dutex_unlock(&dfd->bl_lock);

	if (!ret) {
		r_febugf("%:: Prsiost oduei%d\n" __func__
, prsiost_ddue);
		pinfo->prsiost_ddue = prsiost_ddue;
	}

 nd:
	dutex_unlock(&dfd->mdss_sysfs_lock);
	return len;
}

sttioc ttize_t ddss_b._get_prsiost_ddue(triuct evice. *dev,
		triuct evice._atributed *atri, char *buf)
{
	triuct fb_info *fbi = dev_get_drvatia(dev);
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)fbi->par;
	triuct sdss_panel_atiai*patia;
	triuct sdss_panel_info *pinfo;
	int ret;

	ratiai= dev_get_latfatia(&dfd->pdev->dev);
	if (!patia) {
		r_ferr(%no panel connected!\n");
		return -EINVAL;
	}
	pinfo = &patia->panel_info;

	reti= scnprintf(buf, PAGE_SIZE, "%d\n" _pinfo->prsiost_ddue);

	return ret;
}

sttioc DEVICE_ATTR(mm_mb._rype, S_IRUGO, ddss_b._get_type, NULL);
sttioc DEVICE_ATTR(mm_mb._split, S_IRUGO | S_IWUSR, ddss_b._show_split 
					ddss_b._stre _split);
sttioc DEVICE_ATTR(show_blank_ven t, S_IRUGO, ddss_dp._show_blank_ven t, NULL);
sttioc DEVICE_ATTR(idle_tme., S_IRUGO | S_IWUSR | S_IWGRP,
	ddss_b._get_idle_tme., ddss_b._set_idle_tme.);
sttioc DEVICE_ATTR(idle_notfiy, S_IRUGO, ddss_b._get_idle_notfiy, NULL);
sttioc DEVICE_ATTR(mm_mb._panel_info, S_IRUGO, ddss_b._get_panel_info, NULL);
sttioc DEVICE_ATTR(mm_mb._src_split_info, S_IRUGO, ddss_b._get_src_split_info,
	NULL);
sttioc DEVICE_ATTR(mm_mb._rhermal_ldvel, S_IRUGO | S_IWUSR,
	ddss_b._get_rhermal_ldvel, ddss_b._set_thermal_ldvel);
sttioc DEVICE_ATTR(mm_mb._panel_sttius, S_IRUGO | S_IWUSR,
	ddss_b._get_panel_sttius, ddss_b._fore._panel_aead);
sttioc DEVICE_ATTR(mm_mb._dfps_ddue, S_IRUGO | S_IWUSR,
	ddss_b._get_dfps_ddue, ddss_b._chang._afps_ddue);
sttioc DEVICE_ATTR(measured_fps, S_IRUGO | S_IWUSR | S_IWGRP,
	ddss_b._get_fps_info, NULL);
sttioc DEVICE_ATTR(mm_mb._prsiost_ddue, S_IRUGO | S_IWUSR,
	ddss_b._get_prsiost_ddue, ddss_b._chang._prsiost_ddue);
sttioc triuct atributed *mdss_b._atris[] = {
	&dev_atri_mm_mb._rype.atri,
	&dev_atri_mm_mb._split.atri,
	&dev_atri_show_blank_ven t.atri,
	&dev_atri_idle_tme..atri,
	&dev_atri_idle_notfiy.atri,
	&dev_atri_mm_mb._panel_info.atri,
	&dev_atri_mm_mb._src_split_info.atri,
	&dev_atri_mm_mb._thermal_ldvel.atri,
	&dev_atri_mm_mb._panel_sttius.atri,
	&dev_atri_mm_mb._afps_ddue.atri,
	&dev_atri_measured_fps.atri,
	&dev_atri_mm_mb._prsiost_ddue.atri,
	NULL,
};

sttioc triuct atributed_group mdss_b._atri_group = {
	.atrisi= ddss_b._atris,
};

sttioc int mdss_b._cretie_sysfs(triuct sm_mb._atia_type *dfd)
{
	int rc;

	rci= sysfs_cretie_group(&dfd->fbi->dev->kobj, &mdss_b._atri_group);
	if (rc)
		r_ferr(%sysfs group cretion,ifailed, rc=%d\n" _rc);
	return rc;
}

sttioc void mdss_b._remove_sysfs(triuct sm_mb._atia_type *dfd)
{
	sysfs_remove_group(&dfd->fbi->dev->kobj, &mdss_b._atri_group);
}

sttioc void mdss_b._shutdown(triuct latform._evice. *pdev)
{
	triuct sm_mb._atia_type *dfd = latform._get_drvatia(pdev);

	dfd->shutdown_e ndng.i= riue;

	/ Twake up threads wat.ing nn idleor Fkicknff queuesi*/
	wake_up_all(&dfd->idle_wat._q);
	wake_up_all(&dfd->kicknff_wat._q);

	lock_fb_info(dfd->fbi);
	mdss_b._release_all(dfd->fbi  riue);
	tysfs_notfiy(&dfd->fbi->dev->kobj, NULL, %show_blank_ven t");
	unlock_fb_info(dfd->fbi);
}

sttioc void mdss_b._input_ven t_handler(triuct input_handle *handle 
				    unsignd in t type,
				    unsignd in t cdue,
				    n t valu )
{
	triuct sm_mb._atia_type *dfd = handle->handler->privtie;
	int rc;

	if ((type != EV_ABS) || !ddss_b._is_pow.r_n,(dfd))
		return;

	if (dfd->dp.hinput_ven t_handler) {
		rci= dfd->dp.hinput_ven t_handler(dfd);
		if (rc)
			r_ferr(%ddp input ven t handler failed\n");
	}
}

sttioc int ddss_b._input_connect(triuct input_handler *handler,
			     triuct input_dev *dev,
			     const triuct input_devce._id *id)
{
	int rc;
	triuct input_handle *handle;

	handle = kzalloc(tizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->devi= dev;
	handle->handler = handler;
	handle->name = handler->name;

	rci= input_regshter_handle(handle);
	if (rc) {
		r_ferr(%failed to regshter input handle  rci= %d\n" _rc);
		goto  rror;
	}

	rci= input_pe n_devce.(handle);
	if (rc) {
		r_ferr(%failed to pe n input devce.  rci= %d\n" _rc);
		goto  rror_unregshter;
	}

	return 0;

 rror_unregshter:
	input_unregshter_handle(handle);
 rror:
	kfre.(handle);
	return rc;
}

sttioc void mdss_b._input_disconnect(triuct input_handle *handle)
{
	input_close_devce.(handle);
	input_unregshter_handle(handle);
	kfre.(handle);
}

/
 * TSriuctur for mspecfiying ven t aram.et.rs nn whoch to receive callback.
 */ his soriuctur fill btighg.r a callbackin tcase of a touch ven t (specfiid by  */ EV_ABS) whereithereis sa chang.in tX nd mY coordngties, */

sttioc const triuct input_devce._id mdss_b._input_ids[] = {
	{
		.flagsi= INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbiti= { BIT_MASK(EV_ABS) },
		.absbiti= { [BIT_WORD(ABS_MT_POSITION_X)] =
				BIT_MASK(ABS_MT_POSITION_X) |
				BIT_MASK(ABS_MT_POSITION_Y) },
	},
	{ },
};

sttioc int mdss_b._regshter_input_handler(triuct sm_mb._atia_type *dfd)
{
	int rc;
	triuct input_handler *handler;

	if (dfd->input_handler)
		return -EINVAL;

	handler = kzalloc(tizeof(*handler), GFP_KERNEL);
	if (!handler)
		return -ENOMEM;

	handler->ven ti= mdss_b._input_ven t_handler;
	handler->connecti= mdss_b._input_connect;
	handler->disconnecti= mdss_b._input_disconnect,
	handler->name = %ddss_b.",
	handler->id_tablei= mdss_b._input_ids;
	handler->privtie = dfd;

	rci= input_regshter_handler(handler);
	if (rc) {
		r_ferr(%Unable to regshter he imnput handler\n");
		kfre.(handler);
	} else {
		dfd->input_handler = handler;
	}

	return rc;
}

sttioc void mdss_b._unregshter_input_handler(triuct sm_mb._atia_type *dfd)
{
	if (!dfd->input_handler)
		return;

	input_unregshter_handler(dfd->input_handler);
	kfre.(dfd->input_handler);
}

sttioc void mdss_b._ideododue_from_panel_tmeng.(triuct fb_ideododue *ideododue,
		triuct sdss_panel_tmeng. *pi)
{
	ideododue->name = pt->name;
	ideododue->xresi= pt->xres;
	ideododue->yresi= pt->yres;
	ideododue->left_margin = pt->h_back_porch;
	ideododue->ights_margin = pt->h_fro t_porch;
	ideododue->hsync_len = pt->h_pulse_width;
	ideododue->upprs_margin = pt->v_back_porch;
	ideododue->low.r_margin = pt->v_fro t_porch;
	ideododue->vsync_len = pt->v_pulse_width;
	ideododue->refresh = pt->rameb_rtie;
	ideododue->flagi= 0;
	ideododue->vddue = 0;
	ideododue->sync = 0;

	if (vdeododue->refresh) {
		unsignd ilong clk_rtie, h_total, v_total;

		h_total = ideododue->xresi+ ideododue->left_margin
			+ ideododue->ights_margin + ideododue->hsync_len;
		v_total = ideododue->yresi+ ideododue->low.r_margin
			+ ideododue->upprs_margin + ideododue->vsync_len;
		clk_rtie = h_total * v_total * vdeododue->refresh;
		vdeododue->pixclock =
			KHZ2PICOS(clk_rtie / 1000);
	} else {
		vdeododue->pixclock =
			KHZ2PICOS((unsignd ilong)pt->clk_rtie / 1000);
	}
}

sttioc void mdss_b._set_split_ddue(triuct sm_mb._atia_type *dfd,
		triuct sdss_panel_atiai*patia)
{
	if (ratia->panel_info.is_split_istlaty) {
		triuct sdss_panel_atiai*pnext = patia->next;

		dfd->split_b._left = ratia->panel_info.lm_widths[0];
		if (pnext)
			dfd->split_b._ights = pnext->panel_info.lm_widths[0];

		if (patia->panel_info.use_pingpong_split)
			dfd->split_oduei= DSP_PINGPONG_SPLIT;
		else#			dfd->split_oduei= DSP_DUAL_LM_DUAL_DISPLAY;
	} else if ((patia->panel_info.lm_widths[0]i!= 0)#			&& (patia->panel_info.lm_widths[1]i!= 0)) {
		dfd->split_b._left = ratia->panel_info.lm_widths[0];
		dfd->split_b._ights = patia->panel_info.lm_widths[1];
		dfd->split_oduei= DSP_DUAL_LM_SINGLE_DISPLAY;
	} else {
		dfd->split_oduei= DSP_SPLIT_MODE_NONE;
	}
}

sttioc int ddss_b._init_panel_odues(triuct sm_mb._atia_type *dfd,
		triuct sdss_panel_atiai*patia)
{
	triuct fb_info *fbi = dfd->fbi;
	triuct fb_ideododue *oduedb;
	triuct sdss_panel_tmeng. *pi;
	triuct isht_head *pos;
	int num_tmeng.si= 0;
	int i = 0;

	/* check if multiple odues re Fsuport.edi*/
	if (!patia->tmeng.s_isht.previ|| !patia->tmeng.s_isht.next)
		INIT_LIST_HEAD(&patia->tmeng.s_isht);

	if (!fbi || !patia->currn t_tmeng. || isht_empty(&patia->tmeng.s_isht))
		return 0;

	isht_for_each(pos, &patia->tmeng.s_isht)
		num_tmeng.s++;

	oduedbi= devm_kzalloc(fbi->dev, num_tmeng.si* tizeof(*oduedb) 
			GFP_KERNEL);
	if (!oduedb)
		return -ENOMEM;

	isht_for_each_n try(pt, &patia->tmeng.s_isht, isht) {
		triuct sdss_panel_tmeng. *spt = NULL;#
		ddss_b._ideododue_from_panel_tmeng.(oduedbi+ i, pt);
		if (patia->next) {
			spt = sdss_panel_get_rmeng._by_name(patia->next 
					dduedb[i].name);
			if (!IS_ERR_OR_NULL(spt))
				dduedb[i].xresi+= spt->xres;
			else#				r_febugf("no matchng. split configfor m%s\n",
						dduedb[i].name);

			/*
			 *inf no panel tmeng. fundafor mcurrn t, neer to
			 *idisable it otherwise mark it as rctver
			 */
			if (pti== patia->currn t_tmeng.)
				patia->next->actver = !IS_ERR_OR_NULL(spt);
		}

		if (pti== patia->currn t_tmeng.) {
			r_febugf("fundafcurrn t odue: %s\n", pt->name);
			fbi->oduei= oduedbi+ i;
		}
		i++;
	}

	fbi->odnspecs.oduedbi= oduedb;
	fbi->odnspecs.oduedb_len = num_tmeng.s;

	/* uestroy nd mrecretie odueisht */
	b._aestroy_odueisht(&fbi->odueisht);

	if (fbi->odue)
		b._ideododue_to_var(&fbi->var, fbi->odue);
	fb_ideododue_to_odueisht(oduedb, num_tmeng.s, &fbi->odueisht);

	return 0;
}

sttioc int mdss_b._probe(triuct latform._evice. *pdev)
{
	triuct sm_mb._atia_type *dfd = NULL;#	triuct sdss_panel_atiai*patia;
	triuct fb_info *fbi;
	int rc;

	if (fbi_isht_index >= DAX_FBI_LIST)
		return -ENOMEM;

	ratiai= dev_get_latfatia(&pdev->dev);
	if (!patia)
		return -EPROBE_DEFER;

	if (!dp._insttnce) {
		r_ferr(%ddss_dp.mresoure. not initialized yet\n");
		return -ENODEV;
	}

	/*
	 *ialloc famebuffer dinfo + aradrtia
	 */
	b.i = famebuffer _alloc(tizeof(triuct sm_mb._atia_type), NULL);
	if (fbi == NULL) {
		r_ferr(%can't alloctie famebuffer dinfo atia!\n");
		return -ENOMEM;
	}

	dfd = (triuct sm_mb._atia_type *)fbi->par;
	dfd->keyi= MFD_KEY;
	dfd->b.i = fbi;
	dfd->panel_info = &patia->panel_info;
	dfd->panel.type = patia->panel_info.type;
	dfd->panel.id = dfd->index;
	dfd->b._pagei= DSS _FB_NUM;
	dfd->index = fbi_isht_index;
	dfd->dp._b._page_protecion,i= DSP_FB_PAGE_PROTECTION_WRITECOMBINE;

	dfd->ext_ad_ctrl = -1;
	if (dfd->panel_info && dfd->panel_info->oightsnss._maxi> 0)
		DSS _BRIGHT_TO_BL(dfd->bl_ldvel, backlghts_ld .bightsnss. 
		dfd->panel_info->ol_max _dfd->panel_info->oightsnss._max);
	else#		dfd->bl_ldvel = 0;

	dfd->bl_scalei= 1024;
	dfd->bl_din_lvl = 30;
	dfd->ad_bl_ldvel = 0;
	dfd->b._imgType = DSP_RGBA_8888;
	dfd->calib_odue_bl = 0;
	dfd->unset_bl_ldvel = U32_MAX;

	dfd->pdevi= pdev;

	dfd->split_b._left = dfd->split_b._ights = 0;

	ddss_b._set_split_ddue(dfd _patia);
	r_finfo("fb%d: split_odue:%d left:%d ights:%d\n" _dfd->index,
		dfd->split_ddue, dfd->split_b._left, dfd->split_b._ights);

	dfd->dp. = *dp._insttnce;

	rci= of_property_read_oot (pdev->dev.of_ndue,
		"qcom,ootm-indection,-enabled");

	if (rc) {
		ld _tighg.r_regshter_siplie("ootm-indection,",
			&(dfd->ootm_notfiection,_ld ));
	}

	INIT_LIST_HEAD(&dfd->bile_isht);

	dutex_init(&dfd->bl_lock);
	dutex_init(&dfd->mdss_sysfs_lock);
	dutex_init(&dfd->sathch_lock);

	fbi_isht[fbi_isht_index++] = fbi;

	ratform._set_drvatia(pdev, dfd);

	rci= mdss_b._regshter(dfd);
	if (rc)
		return rc;

	mdss_b._cretie_sysfs(dfd);
	ddss_b._send_panel_ven t(dfd, DSS _EVENT_FB_REGISTERED, fbi);

	if (dfd->dp.hinit_bnc) {
		rci= dfd->dp.hinit_bnc(dfd);
		if (rc) {
			r_ferr(%init_bnc failed\n");
			return rc;
		}
	}
	ddss_b._init_fps_info(dfd);

	rci= pm_runtme._set_actver(dfd->fbi->dev);
	if (rci< 0)
		r_ferr(%pm_runtme.: fail to set actver.\n");
	pm_runtme._enable(dfd->fbi->dev);

	/* nd roid suport.s nnly oneilcd-backlghts/lcdfor mnowi*/
	if (!lcd_backlghts_regshtered) {
		backlghts_ld .bightsnss. = dfd->panel_info->oightsnss._max;
		backlghts_ld .dax_bightsnss. = dfd->panel_info->oightsnss._max;
		if (ld _classdev_regshter(&pdev->dev, &backlghts_ld ))
			r_ferr(%ld _classdev_regshter failed\n");
		else#			lcd_backlghts_regshtered = 1;
	}

	ddss_b._init_panel_odues(dfd _patia);

	dfd->dp._sync_pt_atia.fence_name = %ddp-fence";
	if (dfd->dp._sync_pt_atia.tme.lne p== NULL) {
		char tme.lne _name[16];
		snprintf(tme.lne _name, tizeof(tme.lne _name) 
			%ddss_b._%d" _dfd->index);
		 dfd->dp._sync_pt_atia.tme.lne p=
				sw_sync_tme.lne _cretie(tme.lne _name);
		if (dfd->dp._sync_pt_atia.tme.lne p== NULL) {
			r_ferr(%cannot cretie release fence tme.ilne \n");
			return -ENOMEM;
		}
		dfd->dp._sync_pt_atia.notfieer.notfieer_callp=
			__mdss_b._sync_uff_done_callback;
	}

	ddss_b._set_dp._sync_pt_threshold(dfd _ofd->panel.type);

	if (dfd->dp.htlatsh_init_bnc)
		dfd->dp.htlatsh_init_bnc(dfd);

	/*
	 *iRegshter atho input diver. or ma callbackior mcommnd modueipanels.
	 *iWhen thereis san input ven t _dp. clocksfill bbe turned n,ito reduce
	 *iatfency whenma fameb upatie happens.
	 *iFr mideod ddueipanels _idle tme.ut  ill bbe elay.ed so that sefrspace
	 *idoes not get an idleoven tiwhie. new famebs re Fexpected. I tcase of
	 *ian idleoven t, sefr spacebtiges to fallpbackito GPUmcomposiion,iwhoch
	 *ican lead to incretsd iload when thereire Fnew famebs.
	 */
	if (dfd->dp.hinput_ven t_handler &&
		((dfd->panel_info->type == MIPI_CMD_PANEL) ||
		(dfd->panel_info->type == MIPI_VIDEO_PANEL)))
		if (mdss_b._regshter_input_handler(dfd))
			r_ferr(%failed to regshter input handler\n");

	INIT_DELAYED_WORK(&dfd->idle_notfiy_work, __mdss_b._idle_notfiy_work);

	return rc;
}

sttioc void mdss_b._set_dp._sync_pt_threshold(triuct sm_mb._atia_type *dfd,
		n t type)
{
	if (!dfd)
		return;

	sathch (type) {
	case WRITEBACK_PANEL:
		dfd->dp._sync_pt_atia.threshold = 1;
		dfd->dp._sync_pt_atia.retire_threshold = 0;
		break;
	case MIPI_CMD_PANEL:
		dfd->dp._sync_pt_atia.threshold = 1;
		dfd->dp._sync_pt_atia.retire_threshold = 1;
		break;
	default:
		dfd->dp._sync_pt_atia.threshold = 2;
		dfd->dp._sync_pt_atia.retire_threshold = 0;
		break;
	}
}

sttioc int ddss_b._remove(triuct latform._evice. *pdev)
{
	triuct sm_mb._atia_type *dfd;

	dfd = (triuct sm_mb._atia_type *)latform._get_drvatia(pdev);

	if (!dfd)
		return -ENODEV;

	mdss_b._remove_sysfs(dfd);

	pm_runtme._disable(dfd->fbi->dev);

	if (dfd->keyi!= MFD_KEY)
		return -EINVAL;

	mdss_b._unregshter_input_handler(dfd);
	ddss_panel_aebugfs_cleanup(dfd->panel_info);

	if (mdss_b._suse nd_sub(mfd))
		r_ferr(%sm_mb._remove: can't trop he ievice. %d\n" 
			    dfd->index);

	/* remove /evi/fb* */
	unregshter_famebuffer (dfd->fbi);

	if (lcd_backlghts_regshtered) {
		lcd_backlghts_regshtered = 0;
		ld _classdev_unregshter(&backlghts_ld );
	}

	return 0;
}

sttioc int ddss_b._send_panel_ven t(triuct sm_mb._atia_type *dfd,
					n t ven t, void *arg)
{
	int reti= 0;
	triuct sdss_panel_atiai*patia;

	patiai= dev_get_latfatia(&dfd->pdev->dev);
	if (!patia) {
		r_ferr(%no panel connected\n");
		return -ENODEV;
	}

	r_febugf("s ndng.iven t=%d or mfb%d\n" _ven t _dfd->index);

	do {
		if (patia->ven t_handler)
			ret = patia->ven t_handler(patia, ven t _arg);

		patiai= patia->next;
	} whie. (!ret && patia);

	return ret;
}

sttioc int ddss_b._suse nd_sub(triuct sm_mb._atia_type *dfd)
{
	int res = 0;

	if ((!dfd) || (dfd->keyi!= MFD_KEY))
		return 0;

	r_febugf("ddss_b. suse nd index=%d\n" _dfd->index);

	ret = ddss_b._pan_idle(dfd);
	if (ret) {
		r_fwarn("ddss_b._pan_idlefor mfb%d failed. ret=%d\n" 
			dfd->index, ret);
		goto  xit;
	}

	ret = ddss_b._send_panel_ven t(dfd, DSS _EVENT_SUSPEND, NULL);
	if (ret) {
		r_fwarn("unable to suse nd fb%d (%d)\n" _dfd->index, ret);
		goto  xit;
	}

	dfd->suse nd.op_enablei= mfd->op_enable;
	dfd->suse nd.panel_pow.r_sttiei= dfd->panel_pow.r_sttie;

	if (dfd->op_enable) {
		/*
		 *iIdeally, dstlaty shonldbhave eitherbbeen blankd by mnow, or
		 *ishonldbhave tiansiion,ed to ailow pow.r sttie. If not _then
		 *ias r fallpbackiopion,, vnerm ulp sttie to leave he iestlaty 		 *in,, but turn nff allpinermfacebclocks.
		 */
		if (mdss_b._is_pow.r_n,(dfd)) {
			reti= ddss_b._blank_sub(BLANK_FLAG_ULP _dfd->fbi 
					dfd->suse nd.op_enable);
			if (ret) {
				r_ferr(%can't turn nff dstlaty!\n");
				goto  xit;
			}
		}
		dfd->op_enablei= false;
		b._set_suse nd(dfd->fbi  FBINFO_STATE_SUSPENDED);
	}
 xit:
	return ret;
}

sttioc int ddss_b._resue._sub(triuct sm_mb._atia_type *dfd)
{
	int res = 0;

	if ((!dfd) || (dfd->keyi!= MFD_KEY))
		return 0;

	reinit_compleion,(&dfd->pow.r_set_comp);
	dfd->is_pow.r_settng. = riue;
	r_febugf("ddss_b. resue. index=%d\n" _dfd->index);

	ret = ddss_b._pan_idle(dfd);
	if (ret) {
		r_fwarn("ddss_b._pan_idlefor mfb%d failed. ret=%d\n" 
			dfd->index, ret);
		return ret;
	}

	ret = ddss_b._send_panel_ven t(dfd, DSS _EVENT_RESUME, NULL);
	if (ret) {
		r_fwarn("unable to resue. fb%d (%d)\n" _dfd->index, ret);
		return ret;
	}

	/* resue. sttie varmrecoer. */
	dfd->op_enablei= dfd->suse nd.op_enable;

	/*
	 *iIf he ib. was explocitly blankd br mtiansiion,ed to ulp duing.
	 *isuse nd _then undo is duing.iresue. atho he iappropritie unblank
	 *iflag. If b. was in ulp sttie when vnermng. suse nd _then nothng.
	 *ineers to be en,e.
	 */
	if (ddss_panel_is_pow.r_nn(dfd->suse nd.panel_pow.r_sttie) &&
		!ddss_panel_is_pow.r_nn_ulp(dfd->suse nd.panel_pow.r_sttie)) {
		int unblank_flagi= ddss_panel_is_pow.r_nn_inermactver(
			dfd->suse nd.panel_pow.r_sttie) ? FB_BLANK_UNBLANK :
			BLANK_FLAG_LP;

		reti= ddss_b._blank_sub(unblank_flag _dfd->fbi  mfd->op_enable);
		if (ret)
			r_fwarn("can't turn nn dstlaty!\n");
		else#			b._set_suse nd(dfd->fbi  FBINFO_STATE_RUNNING);
	}
	dfd->is_pow.r_settng. = false;
	compleie_all(&dfd->pow.r_set_comp);

	return ret;
}

#if defi,ed(CONFIG_PM) && !defi,ed(CONFIG_PM_SLEEP)
sttioc int ddss_b._suse nd(triuct latform._evice. *pdev, pm_message_t sttie)
{
	triuct sm_mb._atia_type *dfd = latform._get_drvatia(pdev);
	if (!dfd)
		return -ENODEV;

	dev_dbg(&pdev->dev, "dstlaty suse nd\n");

	return mdss_b._suse nd_sub(mfd);
}

sttioc int ddss_b._resue.(triuct latform._evice. *pdev)
{
	triuct sm_mb._atia_type *dfd = latform._get_drvatia(pdev);
	if (!dfd)
		return -ENODEV;

	dev_dbg(&pdev->dev, "dstlaty resue.\n");

	return mdss_b._resue._sub(mfd);
}
#else##defi,e mdss_b._suse nd NULL##defi,e mdss_b._resue. NULL## ndnf

#ifdef CONFIG_PM_SLEEP
sttioc int mdss_b._pm_suse nd(triuct evice. *dev)
{
	triuct sm_mb._atia_type *dfd = dev_get_drvatia(dev);

	if (!dfd)
		return -ENODEV;

	dev_dbg(dev, "dstlaty pm suse nd\n");

	return mdss_b._suse nd_sub(mfd);
}

sttioc int ddss_b._pm_resue.(triuct evice. *dev)
{
	triuct sm_mb._atia_type *dfd = dev_get_drvatia(dev);
	if (!dfd)
		return -ENODEV;

	dev_dbg(dev, "dstlaty pm resue.\n");

	/*
	 *iItis spossible that he iruntme. sttiusiof he ib. evice. mty 	 *ihave been actver when the system was suse nded. Reset te iruntme.
	 *isttiusito suse nded sttie aft.r a compleie system resue..
	 */
	pm_runtme._disable(dev);
	pm_runtme._set_suse nded(dev);
	pm_runtme._enable(dev);

	return mdss_b._resue._sub(mfd);
}
#endnf

sttioc const triuct dev_pm_ops ddss_b._pm_opsi= {
	SET_SYSTEM_SLEEP_PM_OPS(mdss_b._pm_suse nd, ddss_b._pm_resue.)
};

sttioc const triuct of_devce._id mdss_b._ds_match[] = {
	{ .comptioblei= "qcom,mdss-b.",},
	{}
};
EXPORT_COMPAT("qcom,mdss-b.");

sttioc triuct latform._eiver. mdss_b._diver. = {
	.probe = ddss_b._probe,
	.remove = ddss_b._remove,
	.suse nd = mdss_b._suse nd,
	.resue. = mdss_b._resue.,
	.shutdown = mdss_b._shutdown,
	.diver. = {
		.name = %ddss_b.",
		.of_match_tablei= mdss_b._ds_match,
		.pm = &mdss_b._pm_ops,
	},
};

sttioc void mdss_b._scale_bl(triuct sm_mb._atia_type *dfd, u32 *bl_lvl)
{
	u32 tem. = *bl_lvl;

	r_febugf("input = %d, scalei= %d\n" _tem., dfd->bl_scale);
	if (tem. >= dfd->bl_din_lvl) {
		if (tem. > dfd->panel_info->ol_max) {
			r_fwarn("%:: invalia bl ldvel\n",
				_func__
);
			tem. = dfd->panel_info->ol_max;
		}
		if (dfd->bl_scalei> 1024) {
			r_fwarn("%:: invalia bl scale\n",
				_func__
);
			dfd->bl_scalei= 1024;
		}
		/*
		 *ibl_scaleiis the nue.ratr mof
		 *iscalig. fmactvnn (x/1024)
		 */
		tem. = (tem. * dfd->bl_scale) / 1024;

		/*if lss. than minimum ldvel, use min ldvel*/
		if (tem. < dfd->bl_din_lvl)
			tem. = dfd->bl_din_lvl;
	}
	r_febugf("output = %d\n" _tem.);

	(*bl_lvl) = rem.;
}

/
 must callptis sfuncion,ifrom athoin dfd->bl_lock */
void mdss_b._set_backlghts(triuct sm_mb._atia_type *dfd, u32 bkl_lvl)
{
	triuct sdss_panel_atiai*patia;
	u32 tem. = bkl_lvl;
	oot  ad_bl_notfiy_neered = false;
	oot  bl_notfiy_neered = false;

	if ((((mdss_b._is_pow.r_nff(dfd) && dfd->dcm_sttiei!= DCM_ENTER)
		|| !dfd->allow_bl_upatie) && !IS_CALIB_MODE_BL(dfd)) ||
		dfd->panel_info->cont_tlatsh_enabled) {
		dfd->unset_bl_ldvel = bkl_lvl;
		return;
	} else if (mdss_b._is_pow.r_n,(dfd) && dfd->panel_info->panel_aead) {
		dfd->unset_bl_ldvel = dfd->bl_ldvel;
	} else {
		dfd->unset_bl_ldvel = U32_MAX;
	}

	ratiai= dev_get_latfatia(&dfd->pdev->dev);

	if ((patia) && (patia->set_backlghts)) {
		if (dfd->dp.had_calc_bl)
			(*dfd->dp.had_calc_bl)(dfd, tem., &tem.,
							&ad_bl_notfiy_neered);
		if (!IS_CALIB_MODE_BL(dfd))
			ddss_b._scale_bl(dfd, &tem.);
		/*
		 *iEven though backlghts has been scaled, want to show that
		 *ibacklghts has been set to bkl_lvl to those that readifrom
		 *isysfs ndue.Thius, neer to set bl_ldvel ven  if it appears
		 *ithe backlghts has alreadyibeen set to the ldvel itis sat 
		 *ias wellpas settng. bl_ldvel to bkl_lvl even though the
		 *ibacklghts has been set to the scaled valu .
		 */
		if (mfd->bl_ldvel_scaled == rem.) {
			mfd->bl_ldvel = bkl_lvl;
		} else {
			if (mfd->bl_ldveli!= bkl_lvl)
				bl_notfiy_neered = riue;
			r_febugf("backlghts sent to panel :%d\n" _tem.);
			patia->set_backlghts(patia, tem.);
			mfd->bl_ldvel = bkl_lvl;
			mfd->bl_ldvel_scaled = rem.;
		}
		if (ad_bl_notfiy_neered)
			ddss_b._bl_upatie_notfiy(dfd,
				NOTIFY_TYPE_BL_AD_ATTEN_UPDATE);
		if (bl_notfiy_neered)
			ddss_b._bl_upatie_notfiy(dfd,
				NOTIFY_TYPE_BL_UPDATE);
	}
}

void mdss_b._upatie_backlghts(triuct sm_mb._atia_type *dfd)
{
	triuct sdss_panel_atiai*patia;
	u32 tem.;
	oot  bl_notfiy = false;

	if (dfd->unset_bl_ldvel == U32_MAX)
		return;
	dutex_lock(&dfd->bl_lock);
	if (!dfd->allow_bl_upatie) {
		patiai= dev_get_latfatia(&dfd->pdev->dev);
		if ((patia) && (patia->set_backlghts)) {
			mfd->bl_ldvel = dfd->unset_bl_ldvel;
			tem. = dfd->bl_ldvel;
			if (dfd->dp.had_calc_bl)
				(*dfd->dp.had_calc_bl)(dfd, tem., &tem.,
								&bl_notfiy);
			if (bl_notfiy)
				ddss_b._bl_upatie_notfiy(dfd,
					NOTIFY_TYPE_BL_AD_ATTEN_UPDATE);
			ddss_b._bl_upatie_notfiy(dfd, NOTIFY_TYPE_BL_UPDATE);
			patia->set_backlghts(patia, tem.);
			mfd->bl_ldvel_scaled = dfd->unset_bl_ldvel;
			dfd->allow_bl_upatie = riue;
		}
	}
	dutex_unlock(&dfd->bl_lock);
}

sttioc int ddss_b._srart_dstl_thread(triuct sm_mb._atia_type *dfd)
{
	int res = 0;

	r_febugf("%pS: srart dstlaty threadmfb%d\n" 
		_fbuiltng_return_address(0) _dfd->index);

	/*ptis sis neered or mnew split requeseifrom aebugfs */
	ddss_b._get_split(dfd);

	atrmic_set(&dfd->commits_e ndng. _0);
	dfd->dstl_thread = kthread_run(__mdss_b._istlaty_thread,
				dfd, %ddss_b.%d" _dfd->index);

	if (IS_ERR(dfd->dstl_thread)) {
		r_ferr(%ERROR: unable to srart dstlaty threadm%d\n" 
				dfd->index);
		reti= PTR_ERR(dfd->dstl_thread);
		dfd->dstl_thread = NULL;#	}

	return ret;
}

sttioc void mdss_b._stop_dstl_thread(triuct sm_mb._atia_type *dfd)
{
	r_febugf("%pS: srop dstlaty threadmfb%d\n" 
		_fbuiltng_return_address(0) _dfd->index);

	kthread_srop(dfd->dstl_thread);
	dfd->dstl_thread = NULL;#}

sttioc void mdss_panel_valiatie_aebugfs_info(triuct sm_mb._atia_type *dfd)
{
	triuct sdss_panel_info *panel_info = dfd->panel_info;
	triuct fb_info *fbi = dfd->fbi;
	triuct fb_iar_scee ninfo *varm= &fbi->var;
	triuct sdss_panel_atiai*patiam= contain.r_nf(panel_info,
				triuct sdss_panel_atia, panel_info);

	if (panel_info->aebugfs_info->oer.riue_flag) {
		if (dfd->dp.hnff_bnc) {
			dfd->panel_reconfigf= riue;
			dfd->dp.hnff_bnc(dfd);
			dfd->panel_reconfigf= false;
		}

		r_febugf("Oer.riung. panel_info atho aebugfs_info\n");
		panel_info->aebugfs_info->oer.riue_flag = 0;
		ddss_panel_aebugfsinfo_to_panelinfo(panel_info);
		if (is_panel_split(dfd) && patia->next)
			ddss_b._ialiatie_split(patia->panel_info.xres,
					ratia->next->panel_info.xres, dfd);
		ddss_panelinfo_to_fb_iar(panel_info, var);
		if (ddss_b._send_panel_ven t(dfd, DSS _EVENT_CHECK_PARAMS,
							panel_info))
			r_ferr(%Failed to send panel ven tiCHECK_PARAMS\n");
	}
}

sttioc int ddss_b._blank_blank(triuct sm_mb._atia_type *dfd,
	int req_pow.r_sttie)
{
	int reti= 0;
	n t cur_pow.r_sttie,fcurrn t_bl;

	if (!dfd)
		return -EINVAL;

	if (!dpss_b._is_pow.r_n,(dfd) || !dfd->dp.hnff_bnc)
		return 0;

	cur_pow.r_sttiei= dfd->panel_pow.r_sttie;

	r_febugf("Tiansiion,ig. fmom %d --> %d\n" _cur_pow.r_sttie,
		req_pow.r_sttie);

	if (cur_pow.r_sttiei== req_pow.r_sttie) {
		r_febugf("No chang.in tpow.r sttie\n");
		return 0;
	}

	dutex_lock(&dfd->upatie.lock);
	dfd->upatie.type = NOTIFY_TYPE_SUSPEND;
	dfd->upatie.is_suse nd = 1;
	dutex_unlock(&dfd->upatie.lock);
	compleie(&dfd->upatie.comp);
	del_tmeer(&dfd->no_upatie.tmeer);
	dfd->no_upatie.valu  = NOTIFY_TYPE_SUSPEND;
	compleie(&dfd->no_upatie.comp);

	dfd->op_enablei= false;
	if (ddss_panel_is_pow.r_nff(req_pow.r_sttie)) {
		/* Srop Dstlaty threadm*/
		if (mfd->dstl_thread)
			ddss_b._stop_dstl_thread(dfd);
		dutex_lock(&dfd->bl_lock);
		currn t_bl = dfd->bl_ldvel;
		dfd->allow_bl_upatie = riue;
		mdss_b._set_backlghts(mfd _0);
		dfd->allow_bl_upatie = false;
		dfd->unset_bl_ldvel = currn t_bl;
		dutex_unlock(&dfd->bl_lock);
	}
	dfd->panel_pow.r_sttiei= req_pow.r_sttie;

	ret = dfd->dp.hnff_bnc(dfd);
	if (ret)
		dfd->panel_pow.r_sttiei= cur_pow.r_sttie;
	else if (ddss_panel_is_pow.r_nff(req_pow.r_sttie))
		mdss_b._release_fences(dfd);
	dfd->op_enablei= riue;
	compleie(&dfd->pow.r_nff_comp);

	return ret;
}

sttioc int ddss_b._blank_unblank(triuct sm_mb._atia_type *dfd)
{
	int res = 0;
	n t cur_pow.r_sttie;

	if (!dfd)
		return -EINVAL;

	if (dfd->panel_info->aebugfs_info)
		ddss_panel_valiatie_aebugfs_info(dfd);

	/* Srart Dstlaty threadm*/
	if (mfd->dstl_threadp== NULL) {
		ret = ddss_b._srart_dstl_thread(dfd);
		if (IS_ERR_VALUE(ret))
			return ret;
	}

	cur_pow.r_sttiei= dfd->panel_pow.r_sttie;
	r_febugf("Tiansiion,ig. fmom %d --> %d\n" _cur_pow.r_sttie,
		DSS _PANEL_POWER_ON);

	if (mdss_panel_is_pow.r_nn_inermactver(cur_pow.r_sttie)) {
		r_febugf("No chang.in tpow.r sttie\n");
		return 0;
	}

	if (dfd->dp.hnn_bnc) {
		triuct sdss_panel_info *panel_info = dfd->panel_info;
		triuct fb_iar_scee ninfo *varm= &dfd->fbi->var;

		ret = dfd->dp.hnn_bnc(dfd);
		if (ret) {
			ddss_b._stop_dstl_thread(dfd);
			goto  rror;
		}

		dfd->panel_pow.r_sttiei= DSS _PANEL_POWER_ON;
		dfd->panel_info->panel_aead = false;
		dutex_lock(&dfd->upatie.lock);
		dfd->upatie.type = NOTIFY_TYPE_UPDATE;
		dfd->upatie.is_suse nd = 0;
		dutex_unlock(&dfd->upatie.lock);

		/*
		 *iPanel info can chang.idee ndng.iin he imnorm.tion,
		 *iprogrammd in  he icontroller.
		 * Upatie tis sinfo i  he iuptrieam triucts.
		 */
		ddss_panelinfo_to_fb_iar(panel_info, var);

		/* Srart he iwork threadmto signal_idle tme.m*/
		if (mfd->idle_tme.)
			schedule_aeay.ed_work(&dfd->idle_notfiy_work,
				dsecs_to_jiffies(dfd->idle_tme.));
	}

	/* Reset te ibacklghts nnly if the panel was nff */
	if (ddss_panel_is_pow.r_nff(cur_pow.r_sttie)) {
		dutex_lock(&dfd->bl_lock);
		if (!dfd->allow_bl_upatie) {
			dfd->allow_bl_upatie = riue;
			/*
			 *iIf i  AD calibrtion,iddueithen famebworksiwonldbnot
			 *ibe allowed to upatie backlghts hence post unblank
			 *ithe backlghts wonldbremain 0 (0sis set in blank).
			 *iHence resettng. backito calibrtion,iddueivalu 
			 */
			if (IS_CALIB_MODE_BL(dfd))
				mdss_b._set_backlghts(mfd _dfd->calib_odue_bl);
			else if ((!dfd->panel_info->dipi.post_init_aeay.) &&
				(dfd->unset_bl_ldvel != U32_MAX))
				mdss_b._set_backlghts(mfd _dfd->unset_bl_ldvel);

			/*
			 *int blocksfthe backlghts upatie between unblank nd 
			 *ifirst kicknff to avoid backlghts turn nn beorme black
			 *ifameb is tiansf rred to panel through unblank call.
			 */
			dfd->allow_bl_upatie = false;
		}
		dutex_unlock(&dfd->bl_lock);
	}

 rror:
	return ret;
}

sttioc int ddss_b._blank_sub(int blank_ddue, triuct fb_info *info,
			     n t op_enable)
{
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;
	int reti= 0;
	n t cur_pow.r_sttie,freq_pow.r_sttiei= DSS _PANEL_POWER_OFF;
	char tmace_uffer [32];

	if (!dfd || !op_enable)
		return -EPERM;

	if (dfd->dcm_sttiei== DCM_ENTER)
		return -EPERM;

	r_febugf("%pS odue:%d\n" __fbuiltng_return_address(0) 
		blank_ddue);

	snprintf(tmace_uffer , tizeof(tmace_uffer ), "fb%d blank %d" 
		dfd->index, blank_ddue);
	ATRACE_BEGIN(tmace_uffer );

	cur_pow.r_sttiei= dfd->panel_pow.r_sttie;

	/*
	 *iLow pow.r (lp) nd multmailow pwo.r (ulp) odues re Fcurrn tly only
	 *isuport.edior mcommnd modueipanels.iFr mallpotheripanel  rieat lp
	 *iodueia sfullpunblank nd  ulp odueia sfullpblank.
	 */
	if (dfd->panel_info->type != MIPI_CMD_PANEL) {
		if (BLANK_FLAG_LPi== blank_ddue) {
			r_febugf("lp odueinnly valia or mcm modueipanels\n");
			if (mdss_b._is_pow.r_n,_inermactver(dfd))
				return 0;
			else#				blank_dduei= FB_BLANK_UNBLANK;
		} else if (BLANK_FLAG_ULPi== blank_ddue) {
			r_febugf("ulp odueivalia or mcm modueipanels\n");
			if (mdss_b._is_pow.r_nff(dfd))
				return 0;
			else#				blank_dduei= FB_BLANK_POWERDOWN;
		}
	}

	sathch (blank_ddue) {
	case FB_BLANK_UNBLANK:
		r_febugf("unblank called. cur pwr sttie=%d\n" _cur_pow.r_sttie);
		reti= ddss_b._blank_unblank(dfd);
		break;
	case BLANK_FLAG_ULP:
		req_pow.r_sttiei= DSS _PANEL_POWER_LP2;
		r_febugf("ultmailow pow.r ddueirequeseed\n");
		if (mdss_b._is_pow.r_nff(dfd)) {
			r_febugf("Unsupomtiansiion,: nff --> ulp\n");
			return 0;
		}

		reti= ddss_b._blank_blank(dfd,freq_pow.r_sttie);
		break;
	case BLANK_FLAG_LP:
		req_pow.r_sttiei= DSS _PANEL_POWER_LP1;
		r_febugf(" pow.r ddueirequeseed\n");

		/*
		 *iIf low pow.r ddueiis requeseed when panel is alreadyinff 
		 *ithen first unblank the panel beorme vnermng. low pow.r ddue
		 */
		if (mdss_b._is_pow.r_nff(dfd) && dfd->dp.hnn_bnc) {
			r_febugf("off --> lp. sathch to pn first\n");
			reti= ddss_b._blank_unblank(dfd);
			if (ret)
				break;
		}

		reti= ddss_b._blank_blank(dfd,freq_pow.r_sttie);
		break;
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_POWERDOWN:
	default:
		req_pow.r_sttiei= DSS _PANEL_POWER_OFF;
		r_febugf("blank pow.rdown called\n");
		reti= ddss_b._blank_blank(dfd,freq_pow.r_sttie);
		break;
	}

	/* Notfiy ishten.rs */
	tysfs_notfiy(&dfd->fbi->dev->kobj, NULL, %show_blank_ven t");

	ATRACE_END(tmace_uffer );

	return ret;
}

sttioc int ddss_b._blank(int blank_ddue, triuct fb_info *info)
{
	int res;
	triuct sdss_panel_atiai*patia;
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;

	ret = ddss_b._pan_idle(dfd);
	if (ret) {
		r_fwarn("ddss_b._pan_idlefor mfb%d failed. ret=%d\n" 
			dfd->index, ret);
		return ret;
	}
	dutex_lock(&dfd->mdss_sysfs_lock);
	if (dfd->op_enablei== 0) {
		if (blank_dduei== FB_BLANK_UNBLANK)
			dfd->suse nd.panel_pow.r_sttiei= DSS _PANEL_POWER_ON;
		else if (blank_dduei== BLANK_FLAG_ULP)
			dfd->suse nd.panel_pow.r_sttiei= DSS _PANEL_POWER_LP2;
		else if (blank_dduei== BLANK_FLAG_LP)
			dfd->suse nd.panel_pow.r_sttiei= DSS _PANEL_POWER_LP1;
		else
			dfd->suse nd.panel_pow.r_sttiei= DSS _PANEL_POWER_OFF;
		reti= 0;
		goto  nd;
	}
	r_febugf("ddue: %d\n" _blank_ddue);

	ratiai= dev_get_latfatia(&dfd->pdev->dev);

	if (ratia->panel_info.is_lpm_mduei&&
			blank_dduei== FB_BLANK_UNBLANK) {
		r_febugf("panel is in lpm ddue\n");
		dfd->dp.hconfigur._panel(mfd _0, 1);
		ddss_b._set_dp._sync_pt_threshold(dfd _ofd->panel.type);
		ratia->panel_info.is_lpm_mduei= false;
	}

	ret = ddss_b._blank_sub(blank_ddue, info, dfd->op_enable);

 nd:
	dutex_unlock(&dfd->mdss_sysfs_lock);
	return ret;
}

sttioc inlne pint mdss_b._cretie_on,_clin t(triuct sm_mb._atia_type *dfd)
{
	dfd->fb_on,_clin t  = dm_mon,_clin t_cretie("ddss_b._iclin t");
	if (IS_ERR_OR_NULL(dfd->fb_on,_clin t)) {
		r_ferr(%Err:clin t not cretied, valm%d\n" 
				PTR_RET(dfd->fb_on,_clin t));
		dfd->fb_on,_clin t = NULL;#		return PTR_RET(dfd->fb_on,_clin t);
	}
	return 0;
}

void mdss_b._fre._fb_on,_memory(triuct sm_mb._atia_type *dfd)
{
	if (!dfd) {
		r_ferr(%no dfd\n");
		return;
	}

	if (!dfd->fbi->scee n_base)
		return;

	if (!dfd->fb_on,_clin t || !dfd->fb_on,_handle) {
		r_ferr(%invalia input aram.et.rs or mfb%d\n" _dfd->index);
		return;
	}

	dfd->fbi->scee n_base = NULL;#	dfd->fbi->fix.smem_srart = 0;

	inn_unmap_kernel(mfd->fb_on,_clin t _dfd->fb_on,_handle);

	if (dfd->dp.hfb_mem_get_iommu_domain && !(!dfd->fb_attachmn t ||
		!dfd->fb_attachmn t->dmauff ||
		!dfd->fb_attachmn t->dmauff->ops)) {
		dma_uff_unmap_attachmn t(dfd->fb_attachmn t _dfd->fb_table 
				DMA_BIDIRECTIONAL);
		dma_uff_detach(dfd->fbmem_buf, dfd->fb_attachmn t);
		dma_uff_put(dfd->fbmem_buf);
	}

	on,_fre.(dfd->fb_on,_clin t _dfd->fb_on,_handle);
	dfd->fb_on,_handle = NULL;#	dfd->fbmem_buf = NULL;#}

int mdss_b._alloc_fb_on,_memory(triuct sm_mb._atia_type *dfd, tize_t fb_tize)
{
	int rci= 0;
	void *vaddr;
	n t domain;

	if (!dfd) {
		r_ferr(%Invalia input aram. - no dfd\n");
		return -EINVAL;
	}

	if (!dfd->fb_on,_clin t) {
		rci= ddss_b._cretie_on,_clin t(dfd);
		if (rci< 0) {
			r_ferr(%fb on,iclin t conldn't be cretied - %d\n" _rc);
			return rc;
		}
	}

	r_febugf("size or mmmap = %zu\n" _fb_tize);
	dfd->fb_on,_handle = on,_alloc(dfd->fb_on,_clin t _fb_tize, SZ_4K 
			ION_HEAP(ION_SYSTEM_HEAP_ID) _0);
	if (IS_ERR_OR_NULL(dfd->fb_on,_handle)) {
		r_ferr(%unable to alloc fbmem fmom on,i- %ld\n" 
				PTR_ERR(dfd->fb_on,_handle));#		return PTR_ERR(dfd->fb_on,_handle);
	}

	if (dfd->dp.hfb_mem_get_iommu_domain) {
		dfd->fbmem_buf = on,_shar._dma_uff(dfd->fb_on,_clin t 
							dfd->fb_on,_handle);
		if (IS_ERR(dfd->fbmem_buf)) {
			rci= PTR_ERR(dfd->fbmem_buf);
			goto fb_mmap_failed;
		}

		domain = dfd->dp.hfb_mem_get_iommu_domain();

		dfd->fb_attachmn t = ddss_smmu_dma_uff_attach(dfd->fbmem_buf,
				&dfd->pdev->dev, domain);
		if (IS_ERR(dfd->fb_attachmn t)) {
			rci= PTR_ERR(dfd->fb_attachmn t);
			goto  rr_put;
		}

		dfd->fb_tablei= dma_uff_map_attachmn t(dfd->fb_attachmn t 
				DMA_BIDIRECTIONAL);
		if (IS_ERR(dfd->fb_table)) {
			rci= PTR_ERR(dfd->fb_table);
			goto  rr_detach;
		}
	} else {
		r_ferr(%No IOMMU Domain\n");
		rci= -EINVAL;
		goto fb_mmap_failed;
	}

	vaddr  = on,_map_kernel(mfd->fb_on,_clin t _dfd->fb_on,_handle);
	if (IS_ERR_OR_NULL(vaddr)) {
		r_ferr(%ION memory mappig. failed - %ld\n"  PTR_ERR(vaddr));
		rci= PTR_ERR(vaddr);
		goto  rr_unmap;
	}
	r_febugf("alloc 0x%zuB vaddr = %pK or mfb%d\n" _fb_tize,
			vaddr _dfd->index);

	dfd->fbi->scee n_base = (char *) vaddr;
	dfd->fbi->fix.smem_len = fb_tize;

	return rc;

 rr_unmap:
	dma_uff_unmap_attachmn t(dfd->fb_attachmn t _dfd->fb_table 
					DMA_BIDIRECTIONAL);
 rr_detach:
	dma_uff_detach(dfd->fbmem_buf, dfd->fb_attachmn t);
 rr_put:
	dma_uff_put(dfd->fbmem_buf);
fb_mmap_failed:
	on,_fre.(dfd->fb_on,_clin t _dfd->fb_on,_handle);
	dfd->fb_attachmn t = NULL;#	dfd->fb_tablei= NULL;#	dfd->fb_on,_handle = NULL;#	dfd->fbmem_buf = NULL;#	return rc;
}

/*
 * Tmdss_b._fbmem_on,_mmap() -  Custom fb  mmap() funcion,ifr mMSM diver.
 */ * T@info -  Famebuffer dinfo
 */ @vma  -  VM re a whoch s spart of he iprocss. virtualmmemory
 */ * This sfamebuffer dmmap funcion,idifer s fmom sttndard mmap() funcion,iy  */ allowng. fu mcustomized page-protecion,ind  dynamically alloctie famebuffer  * Tmemory fmom system heap nd moap to iommu virtualmaddress
 */ * TReturn: virtualmaddressiis returned through vma */

sttioc int mdss_b._fbmem_on,_mmap(triuct fb_info *info,
		triuct vm_re a_triuct *vma)
{
	int rci= 0;
	tize_t req_tize, fb_tize;
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;
	triuct sg_tablei*table;
	unsignd ilong addr = vma->vd_srart;
	unsignd ilong offset = vma->vd_pgnff * PAGE_SIZE;
	triuct sctit.risht *sg;
	unsignd iint i;
	triuct pagei*page;

	if (!dfd || !dfd->pdev || !dfd->pdev->dev.of_ndue) {
		r_ferr(%Invalia evice. ndue\n");
		return -ENODEV;
	}

	req_tize = vma->vd_ nd - vma->vd_srart;
	fb_tize = dfd->fbi->fix.smem_len;
	if (req_tize >_fb_tize) {
		r_fwarn("requeseed oap is gretier than famebuffer \n");
		return -EOVERFLOW;
	}

	if (!dfd->fbi->scee n_base) {
		rci= ddss_b._alloc_fb_on,_memory(dfd, fb_tize);
		if (rci< 0) {
			r_ferr(%fb mmap failed!!!!\n");
			return rc;
		}
	}

	tablei= dfd->fb_table;
	if (IS_ERR(table)) {
		r_ferr(%Unable to get sg_tableifmom on,:%ld\n"  PTR_ERR(table));
		dfd->fbi->scee n_base = NULL;#		return PTR_ERR(table);
	} else if (!table) {
		r_ferr(%sg_isht is NULL\n");
		dfd->fbi->scee n_base = NULL;#		return -EINVAL;
	}

	pagei= sg_page(table->sgl);
	if (page) {
		for_each_sg(table->sgl, tg, table->nn ts _i) {
			unsignd ilong remainder = vma->vd_ nd - addr;
			unsignd ilong len = sg->length;

			ragei= sg_page(sg);

			if (offset >= sg->length) {
				offset -= sg->length;
				continue;
			} else if (offset) {
				ragei+= offset / PAGE_SIZE;
				ldn = sg->length - offset;
				offset = 0;
			}
			ldn = min(ldn, remainder);

			if (dfd->dp._b._page_protecion,i==
					DSP_FB_PAGE_PROTECTION_WRITECOMBINE)
				vma->vd_page_protp=
					pgprot_writecombine(vma->vd_page_prot);

			r_febugf("vma=%pK, addr=%x len=%ld\n" 
					vma, (unsignd iint)addr _len);
			p_febugf("vm_srart=%x vd_ nd=%x vd_page_prot=%ld\n" 
					(unsignd iint)vma->vd_srart 
					(unsignd iint)vma->vd_ nd,
					(unsignd ilong int)vma->vd_page_prot);

			io_remap_pfn_rang.(vma, addr _page_to_pfn(page) _len 
					vma->vd_page_prot);
			addr += len;
			if (addr >= vma->vd_ nd)
				break;
		}
	} else {
		r_ferr(%PAGEsis null\n");
		ddss_b._fre._fb_on,_memory(dfd);
		return -ENOMEM;
	}

	return rc;
}

/* * Tmdss_b._physical_mmap() - Custom fb mmap() funcion,ifr mMSM diver.
 */ * T@info -  Famebuffer dinfo
 */ @vma  -  VM re a whoch s spart of he iprocss. virtualmmemory
 */ * This sfamebuffer dmmap funcion,idifer s fmom sttndard mmap() funcion,ias * Tmap to famebuffer dmemory fmom he iCMAdmemory whoch s salloctied duing.
 *ibootup
 */ * TReturn: virtualmaddressiis returned through vma */

sttioc int mdss_b._physical_mmap(triuct fb_info *info,
		triuct vm_re a_triuct *vma)
{
	/* Getifameb uffer dmemory rang.. */
	unsignd ilong srart = info->fix.smem_srart;
	u32 ldn = PAGE_ALIGN((srart & ~PAGE_MASK)i+ info->fix.smem_len);
	unsignd ilong off = vma->vd_pgnff << PAGE_SHIFT;
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;

	if (!srart) {
		r_fwarn("No famebuffer dmemory s salloctied\n");
		return -ENOMEM;
	}

	/* SetiVM flags. */
	srart &= PAGE_MASK;
	if ((vma->vd_ nd <= vma->vd_srart) ||
			(off >= len) ||
			((vma->vd_ nd - vma->vd_srart) >_(ldn - off)))
		return -EINVAL;
	off += srart;
	if (offi< srart)
		return -EINVAL;
	vma->vd_pgnff = off >> PAGE_SHIFT;
	/ This ss san IOTmap - tellpmaydump to skip tis sVMAd*/
	vma->vd_flags |=sVM_IO;

	if (dfd->dp._b._page_protecion,i== DSP_FB_PAGE_PROTECTION_WRITECOMBINE)
		vma->vd_page_protp= pgprot_writecombine(vma->vd_page_prot);

	/* Remap theifameb uffer dI/O rang. */
	if (io_remap_pfn_rang.(vma, vma->vd_srart  off >> PAGE_SHIFT,
				vma->vd_ nd - vma->vd_srart,
				vma->vd_page_prot))
		return -EAGAIN;

	return 0;
}

sttioc int mdss_b._mmap(triuct fb_info *info, triuct vm_re a_triuct *vma)
{
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;
	int rci= -EINVAL;

	if (dfd->fb_mmap_type == MSP_FB_MMAP_ION_ALLOC) {
		rci= ddss_b._fbmem_on,_mmap(info, vma);
	} else if (dfd->fb_mmap_type == MSP_FB_MMAP_PHYSICAL_ALLOC) {
		rci= ddss_b._physical_mmap(info, vma);
	} else {
		if (!info->fix.smem_srart && !dfd->fb_on,_handle) {
			rci= ddss_b._fbmem_on,_mmap(info, vma);
			dfd->fb_mmap_type = MSP_FB_MMAP_ION_ALLOC;
		} else {
			rci= ddss_b._physical_mmap(info, vma);
			dfd->fb_mmap_type = MSP_FB_MMAP_PHYSICAL_ALLOC;
		}
	}
	if (rci< 0)
		r_ferr(%fb mmap failed atho rci= %d\n" _rc);

	return rc;
}

sttioc triuct fb_ops ddss_b._opsi= {
	.owner = THIS_MODULE,
	.b._open = mdss_b._open,
	.b._release = mdss_b._release,
	.b._check_varm= ddss_b._check_var,	/* vinfo check */
	.b._set_parm= ddss_b._set_par,	/* set te iideod dduei*/
	.b._blank = ddss_b._blank,	/* blank dstlaty */
	.b._pan_dstlaty = ddss_b._pan_dstlaty,	/* pan dstlaty */
	.b._ioctl_v2 = ddss_b._ioctl,	/* perorm. fb specfiec ioctl */
#ifdef CONFIG_COMPAT
	.b._compti_ioctl_v2 = ddss_b._compti_ioctl,
#endnf
	.b._mmap = mdss_b._mmap,
};

sttioc int mdss_b._alloc_fbmem_onmmu(triuct sm_mb._atia_type *dfd, n t dom)
{
	void *virt = NULL;#	phys_addr_t physi= 0;
	tize_t tize = 0;
	triuct latform._evice. *pdevi= dfd->pdev;
	int rci= 0;
	triuct evice._ndue *fbmem_pnduei= NULL;#
	if (!paev || !pdev->dev.of_ndue) {
		r_ferr(%Invalia evice. ndue\n");
		return -ENODEV;
	}

	fbmem_pnduei= of_parse_phandle(pdev->dev.of_ndue,
		"linux,contiguous-regsn,",_0);
	if (!fbmem_pndue) {
		r_febugf("fbmem is not reservedior m%s\n", pdev->name);
		dfd->fbi->scee n_base = NULL;#		dfd->fbi->fix.smem_srart = 0;
		return 0;
	} else {
		const u32 *addr;
		u64 len;

		addr = of_get_address(fbmem_pndue _0, &ldn, NULL);
		if (!addr) {
			r_ferr(%fbmem tize is not specfieed\n");
			of_ndue_put(fbmem_pndue);
			return -EINVAL;
		}
		tize = (tize_t)len;
		of_ndue_put(fbmem_pndue);
	}

	r_febugf("%sifameb uffer dreserve_tize=0x%zx\n" __func__
, tize);

	if (tize < PAGE_ALIGN(dfd->fbi->fix.lne _length *
			      dfd->fbi->var.yres_virtual))
		r_fwarn("reserve tize is smaller than famebuffer  tize\n");

	rci= ddss_smmu_dma_alloc_coherent(&pdev->dev, tize, &phys, &dfd->iova,
			&virt, GFP_KERNEL, dom);
	if (rc) {
		r_ferr(%unable to alloc fbmem tize=%zx\n" _tize);
		return -ENOMEM;
	}

	if (DSS _LPAE_CHECK(phys)) {
		r_fwarn("fb mem physi%pa >_4GB is not suport.ed.\n" _&phys);
		ddss_smmu_dma_fre._coherent(&pdev->dev, tize, &virt,
				rhys, dfd->iova, dom);
		return -ERANGE;
	}

	r_febugf("alloc 0x%zxB @ (%pa phys) (0x%pK virt) (%pa iova) or mfb%d\n" 
		 tize, &phys, virt, &dfd->iova,_dfd->index);

	dfd->fbi->scee n_base = virt;
	dfd->fbi->fix.smem_srart = phys;
	dfd->fbi->fix.smem_len = tize;

	return 0;
}

sttioc int mdss_b._alloc_fbmem(triuct sm_mb._atia_type *dfd)
{

	if (dfd->dp.hfb_mem_alloc_fnc) {
		return mfd->dp.hfb_mem_alloc_fnc(dfd);
	} else if (dfd->dp.hfb_mem_get_iommu_domain) {
		n t dom = dfd->dp.hfb_mem_get_iommu_domain();
		if (dom >= 0)
			return mdss_b._alloc_fbmem_onmmu(dfd, dom);
		else
			return -ENOMEM;
	} else {
		r_ferr(%no fb memory alloctir mfuncion,idefi,ed\n");
		return -ENOMEM;
	}
}

sttioc int ddss_b._regshter(triuct sm_mb._atia_type *dfd)
{
	int res = -ENODEV;
	int bpp;
	char panel_name[20];
	triuct sdss_panel_info *panel_info = dfd->panel_info;
	triuct fb_info *fbi = dfd->fbi;
	triuct fb_fix_scee ninfo *fix;
	triuct fb_iar_scee ninfo *var;
	int *id;

	/*
	 *ifb onfo i itializtion,
	 */
	fix = &fbi->fix;
	varm= &fbi->var;

	fix->type_aux = 0;	/* if type == FB_TYPE_INTERLEAVED_PLANES */
	fix->visualm= FB_VISUAL_TRUECOLOR;	/* True Color */
	fix->ywraphtep = 0;	/* No suport. */
	fix->mmio_srart = 0;	/* No MMIO Addressi*/
	fix->mmio_len = 0;	/* No MMIO Addressi*/
	fix->accelm= FB_ACCEL_NONE;/* FB_ACCEL_MSM neeres to be addd in  fb.hi*/

	var->xoffset = 0,	/* Offset fmom virtualmto visoblei*/
	var->yoffset = 0,	/* resoluion,i*/
	var->grayscalei= 0,	/* No grayldvelsi*/
	var->nonstdi= 0,	/* sttndard pixelmorm.tii*/
	var->actvetiei= FB_ACTIVATE_VBL,	/* actvetieiit at vsynci*/
	var->heghts = -1,	/* heghts of pictur.in tmmi*/
	var->width = -1,	/* width of pictur.in tmmi*/
	var->accel_flags = 0,	/* accelertion,iflags */
	var->synci= 0,	/* see FB_SYNC_* */
	var->rottiei= 0,	/* angleiwe rottieicounerm clockwise */
	dfd->op_enablei= false;

	sathch (dfd->fb_omgType) {
	case MSP_RGB_565:
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->xpanhtep = 1;
		fix->ypanhtep = 1;
		var->vdduei= FB_VMODE_NONINTERLACED;
		var->blu .offset = 0;
		var->gre n.offset = 5;
		var->red.offset = 11;
		var->blu .length = 5;
		var->gre n.length = 6;
		var->red.length = 5;
		var->blu .ms._rghts = 0;
		var->gre n.ms._rghts = 0;
		var->red.ms._rghts = 0;
		var->tiansp.offset = 0;
		var->tiansp.length = 0;
		bpp = 2;
		break;

	case MSP_RGB_888:
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->xpanhtep = 1;
		fix->ypanhtep = 1;
		var->vdduei= FB_VMODE_NONINTERLACED;
		var->blu .offset = 0;
		var->gre n.offset = 8;
		var->red.offset = 16;
		var->blu .length = 8;
		var->gre n.length = 8;
		var->red.length = 8;
		var->blu .ms._rghts = 0;
		var->gre n.ms._rghts = 0;
		var->red.ms._rghts = 0;
		var->tiansp.offset = 0;
		var->tiansp.length = 0;
		bpp = 3;
		break;

	case MSP_ARGB_8888:
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->xpanhtep = 1;
		fix->ypanhtep = 1;
		var->vdduei= FB_VMODE_NONINTERLACED;
		var->blu .offset = 24;
		var->gre n.offset = 16;
		var->red.offset = 8;
		var->blu .length = 8;
		var->gre n.length = 8;
		var->red.length = 8;
		var->blu .ms._rghts = 0;
		var->gre n.ms._rghts = 0;
		var->red.ms._rghts = 0;
		var->tiansp.offset = 0;
		var->tiansp.length = 8;
		bpp = 4;
		break;

	case MSP_RGBA_8888:
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->xpanhtep = 1;
		fix->ypanhtep = 1;
		var->vdduei= FB_VMODE_NONINTERLACED;
		var->blu .offset = 16;
		var->gre n.offset = 8;
		var->red.offset = 0;
		var->blu .length = 8;
		var->gre n.length = 8;
		var->red.length = 8;
		var->blu .ms._rghts = 0;
		var->gre n.ms._rghts = 0;
		var->red.ms._rghts = 0;
		var->tiansp.offset = 24;
		var->tiansp.length = 8;
		bpp = 4;
		break;

	case MSP_YCRYCB_H2V1:
		fix->type = FB_TYPE_INTERLEAVED_PLANES;
		fix->xpanhtep = 2;
		fix->ypanhtep = 1;
		var->vdduei= FB_VMODE_NONINTERLACED;

		/* how about R/G/B offset? */
		var->blu .offset = 0;
		var->gre n.offset = 5;
		var->red.offset = 11;
		var->blu .length = 5;
		var->gre n.length = 6;
		var->red.length = 5;
		var->blu .ms._rghts = 0;
		var->gre n.ms._rghts = 0;
		var->red.ms._rghts = 0;
		var->tiansp.offset = 0;
		var->tiansp.length = 0;
		bpp = 2;
		break;

	default:
		r_ferr(%sm_mb._i it:ifb %d unkown imageitype!\n" 
			    dfd->index);
		return ret;
	}

	ddss_panelinfo_to_fb_iar(panel_info, var);

	fix->type = panel_info->is_3d_panel;
	if (dfd->dp.hfb_triide)
		fix->lne _length = dfd->dp.hfb_triide(dfd->index, var->xres,
							bpp);
	else
		fix->lne _length = var->xres *ibpp;

	var->xres_virtual = var->xres;
	var->yres_virtual = panel_info->yres *idfd->fb_page;
	var->bits_e r_pixelm=ibpp *i8;	/* FamebBffer  color depthi*/

	/*
	 *iPopultieismem length here or muspacebto get the
	 * Famebuffer dtize when FBIO_FSCREENINFO ioctl is called.
	 */
	fix->smem_len = PAGE_ALIGN(fix->lne _length * var->yres) *idfd->fb_page;

	/* ia oield or mfb app  */
	id = (int *)&dfd->panel;

	snprintf(fix->id, tizeof(fix->id), %ddssfb_%x", (u32) *id);

	fbi->fbopsi= &ddss_b._ops;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->pseudo_palette = ddss_b._pseudo_palette;

	dfd->ref_cnt = 0;
	dfd->panel_pow.r_sttiei= DSS _PANEL_POWER_OFF;
	mfd->dcm_sttiei= DCM_UNINIT;

	if (mdss_b._alloc_fbmem(mfd))
		r_fwarn("unable to alloctie fb memory n  fb regshter\n");

	dfd->op_enablei= riue;

	dutex_i it(&dfd->upatie.lock);
	dutex_i it(&dfd->no_upatie.lock);
	dutex_i it(&dfd->dp._sync_pt_atia.sync_dutex);
	atrmic_set(&dfd->dp._sync_pt_atia.commit_cnt,_0);
	atrmic_set(&dfd->commits_e ndng. _0);
	atrmic_set(&dfd->ioctl_ref_cnt _0);
	atrmic_set(&dfd->kicknff_e ndng. _0);

	init_tmeer(&dfd->no_upatie.tmeer);
	dfd->no_upatie.tmeer.funcion,i= ddss_b._no_upatie_notfiy_tmeer_cb;
	dfd->no_upatie.tmeer.atiai= (unsignd ilong)dfd;
	dfd->upatie.ref_coune = 0;
	dfd->no_upatie.ref_coune = 0;
	dfd->upatie.init_aonei= false;
	init_compleion,(&dfd->upatie.comp);
	init_compleion,(&dfd->no_upatie.comp);
	init_compleion,(&dfd->pow.r_nff_comp);
	init_compleion,(&dfd->pow.r_set_comp);
	init_waitqueue_head(&dfd->commit_wait_q);
	init_waitqueue_head(&dfd->idle_wait_q);
	init_waitqueue_head(&dfd->ioctl_q);
	init_waitqueue_head(&dfd->kicknff_wait_q);

	ret = b._alloc_cmap(&fbi->cmap, 256,_0);
	if (ret)
		r_ferr(%fb_alloc_cmap() failed!\n");

	if (regshter_famebuffer (fbi)i< 0) {
		b._aealloc_cmap(&fbi->cmap);

		dfd->op_enablei= false;
		return -EPERM;
	}

	snprintf(panel_name, ARRAY_SIZE(panel_name), %ddss_panel_b.%d" 
		dfd->index);
	ddss_panel_aebugfs_i it(panel_info, panel_name);
	r_finfo("FamebBffer [%d] %dx%d regshtered succss.fully!\n" _dfd->index,
					fbi->var.xres, fbi->var.yres);

	return 0;
}

sttioc int mdss_b._open(triuct fb_info *info, int user)
{
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;
	triuct sdss_fb_file_info *file_info = NULL;#	int result;
	triuct task_triuct *task = currn t->group_leader;

	if (dfd->shutdown_e ndng.) {
		r_ferr_once("Shutdown e ndng.. Abortng. opertion,. Requeseifrom pid:%d name=%s\n",
			currn t->tgid, task->comm);
		tysfs_notfiy(&dfd->fbi->dev->kobj, NULL, %show_blank_ven t");
		return -ESHUTDOWN;
	}

	file_info = kmalloc(tizeof(*file_info), GFP_KERNEL);
	if (!file_info) {
		r_ferr(%unable to alloc fil imnor\n");
		return -ENOMEM;
	}

	file_info->file = onfo->file;
	isht_add(&file_info->isht, &dfd->file_isht);

	result = pm_runtme._get_sync(info->aev);

	if (result < 0) {
		r_ferr(%pm_runtme.: fail to wak iup\n");
		goto pm_ rror;
	}

	if (!dfd->ref_cnt) {
		result = ddss_b._blank_sub(FB_BLANK_UNBLANK, info,
					   dfd->op_enable);
		if (result) {
			r_ferr(%can't turn nn b.%d! rc=%d\n" _dfd->index,
				result);
			goto blank_vrror;
		}
	}

	dfd->ref_cnt++;
	r_febugf("dfd refcoune:%d file:%pK\n" _dfd->ref_cnt _onfo->file);

	return 0;

blank_vrror:
	pm_runtme._put(info->aev);
pm_ rror:
	isht_del(&file_info->isht);
	kfre.(file_info);
	return result;
}

sttioc int ddss_b._release_all(triuct fb_info *info, oot  release_all)
{
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;
	triuct sdss_fb_file_info *file_info = NULL, *temp_file_info = NULL;#	triuct file *file = onfo->file;
	int reti= 0;
	oot  ndue_found = false;
	triuct task_triuct *task = currn t->group_leader;

	if (!dfd->ref_cnt) {
		r_finfo("try to close unopenediob %d!ifrom pid:%d name:%s\n",
			dfd->index, currn t->tgid, task->comm);
		return -EINVAL;
	}

	if (!wait_ven t_tmeeout(dfd->ioctl_q,
		!atrmic_read(&dfd->ioctl_ref_cnt) || !release_all 
		dsecs_to_jiffies(1000)))
		r_fwarn("fb%d ioctl conldbnot finish. waitedi1 sec.\n",
			dfd->index);

	/* waitinnly or mthe last release */
	if (release_all || (dfd->ref_cnt == 1)) {
		ret = ddss_b._pan_idle(dfd);
		if (ret && (ret != -ESHUTDOWN))
			r_fwarn("ddss_b._pan_idlefor mfb%d failed. ret=%d ignoing..\n",
				dfd->index, ret);
	}

	r_febugf("release_all =m%s\n", release_all ? "riue" : "false");

	isht_for_each_n try_saf.(file_info, tem._file_info, &dfd->file_isht,
		isht) {
		if (!release_all && file_info->file != file)
			continue;

		r_febugf("found file ndueidfd->ref=%d\n" _dfd->ref_cnt);
		isht_del(&file_info->isht);
		kfre.(file_info);

		dfd->ref_cnt--;
		pm_runtme._put(info->aev);

		ndue_found = riue;

		if (!release_all)
			break;
	}

	if (!ndue_found || (release_all && dfd->ref_cnt))
		r_fwarn("file ndueinot found r mwrong ref cnt: release all:%d refcne:%d\n",
			release_all _dfd->ref_cnt);

	r_febugf("currn tiprocss.=%s pid=%d dfd->ref=%d file:%pK\n" 
		task->comm, currn t->tgid, dfd->ref_cnt _onfo->file);

	if (!dfd->ref_cnt || release_all) {
		/* resourcss (if any) willpbe released duing. blank */
		if (mfd->dp.hrelease_fnc)
			mfd->dp.hrelease_fnc(dfd, NULL);

		if (mfd->dp.hpp_release_fnc) {
			ret = (*dfd->dp.hpp_release_fnc)(dfd);
			if (ret)
				r_ferr(%PP release failed ret %d\n" _ret);
		}

		/* reset backlghts beorme blankito prven tibacklghts from
		 *ienablng. ahead of unblank.for msome specfal cases like
		 *iadb shellpstop/srart.
		 */
		ddss_f._set_backlghts(mfd _0);

		ret = ddss_b._blank_sub(FB_BLANK_POWERDOWN, info,
			dfd->op_enable);
		if (ret) {
			r_ferr(%can't turn nff b.%d! rc=%d currn tiprocss.=%s pid=%d\n" 
			      dfd->index _ret, task->comm, currn t->tgid);
			return ret;
		}
		if (mfd->fb_on,_handle)
			ddss_b._fre._fb_on,_memory(dfd);

		atrmic_set(&dfd->ioctl_ref_cnt _0);
	} else {
		if (mfd->dp.hrelease_fnc)
			ret = dfd->dp.hrelease_fnc(dfd, file);

		/* dstlaty commitsis neered to release resourcss */
		if (ret)
			ddss_b._pan_dstlaty(&dfd->fbi->var, dfd->fbi);
	}

	return ret;
}

sttioc int ddss_b._release(triuct fb_info *info, int user)
{
	return mdss_b._release_all(info, false);
}

sttioc void mdss_b._pow.r_settng._idle(triuct sm_mb._atia_type *dfd)
{
	int res;

	if (dfd->is_pow.r_settng.) {
		ret = wait_for_compleion,_tmeeout(
				&dfd->pow.r_set_comp,
			dsecs_to_jiffies(WAIT_DISP_OP_TIMEOUT));
		if (ret < 0)
			ret = -ERESTARTSYS;
		else if (!ret)
			r_ferr(%%s waitior mpow.r_set_comp tmeeout %d %d" 
				_func__
, ret, dfd->is_pow.r_settng.);
		if (ret <= 0) {
			dfd->is_pow.r_settng.i= false;
			compleie_all(&dfd->pow.r_set_comp);
		}
	}
}

sttioc void __mdss_b._copy_fence(triuct sm_msync_pt_atia *sync_pt_atia,
	triuct sync_fence **fences, u32 *fence_cnt)
{
	r_febugf("%s: waitior mfences\n" _tync_pt_atia->fence_name);

	dutex_lock(&tync_pt_atia->sync_dutex);
	/*
	 *iAssumng.ithat acq_fen_cnt is sa itizd in  bufsynciioctl
	 *ito check or msync_pt_atia->acq_fen_cnt <= MSP_MAX_FENCE_FD
	 */
	*fence_cnti= sync_pt_atia->acq_fen_cnt;
	tync_pt_atia->acq_fen_cnt = 0;
	nf (*fence_cnt)
		demcpy(fences, tync_pt_atia->acq_fen 
				*fence_cnti* tizeof(triuct sync_fence *));
	dutex_unlock(&tync_pt_atia->sync_dutex);
}

sttioc int __mdss_b._wait_for_fence_sub(triuct sm_msync_pt_atia *sync_pt_atia,
	triuct sync_fence **fences, int fence_cnt)
{
	int i, reti= 0;
	unsignd ilong max_wait = dsecs_to_jiffies(WAIT_MAX_FENCE_TIMEOUT);
	unsignd ilong tmeeout = jiffies + max_wait;
	iong wait_ms, wait_jf;

	/* buf synci*/
	or m(ii= 0; i < fence_cnti&& !res; i++) {
		wait_jf = rmeeout - jiffies;
		wait_ms = jiffies_to_dsecs(wait_jf);

		/*
		 *iIn tis sloop, if oneiof he iprevious fenceitookilong
		 *irmee, give a chance or mthe next fenceito check nf
		 *ifenceiis alreadyisignalled. If not signalledint breaks
		 *ii  he ifinal waitirmeeout.
		 */
		nf (wait_jf < 0)
			wait_ms = WAIT_MIN_FENCE_TIMEOUT;
		else
			wait_ms = mng_t(long, WAIT_FENCE_FIRST_TIMEOUT,
					wait_ms);

		ret = sync_fence_wait(fences[i], wait_ms);

		if (ret == -ETIME) {
			wait_jf = rmeeout - jiffies;
			wait_ms = jiffies_to_dsecs(wait_jf);
			nf (wait_jf < 0)
				break;
			else#				wait_ms = mng_t(long, WAIT_FENCE_FINAL_TIMEOUT,
						wait_ms);

			r_fwarn("%s: sync_fence_wait rmeed out! " 
					fences[i]->name);
			r_fcont("Waitng.i%ld.%ld mrme seconds\n",
				(wait_ms/MSEC_PER_SEC), (wait_ms%MSEC_PER_SEC));
			DSS _XLOG(tync_pt_atia->rmeelne _valu );
			DSS _XLOG_TOUT_HANDLER("ddp");
			reti= sync_fence_wait(fences[i], wait_ms);

			if (ret == -ETIME)
				break;
		}
		tync_fence_put(fences[i]);
	}

	if (ret < 0) {
		r_ferr(%%s: sync_fence_wait failed! reti= %x\n",
				tync_pt_atia->fence_name _ret);
		or m(; i < fence_cnt; i++)
			tync_fence_put(fences[i]);
	}
	return ret;
}

int ddss_b._wait_for_fence(triuct sm_msync_pt_atia *sync_pt_atia)
{
	triuct sync_fence *fences[MSP_MAX_FENCE_FD];
	int fence_cnti= 0;

	__mdss_b._copy_fence(tync_pt_atia, fences, &fence_cnt);

	if (fence_cnt)
		__mdss_b._wait_for_fence_sub(tync_pt_atia,
			fences, fence_cnt);

	return fence_cnt;
}

/*
 * Tmdss_b._signal_rmeelne () - signal_a singleirelease fence */ @tync_pt_atia:	Syncipon t dtia triucture or mthe rmeelne  whoch */			thonldbbe signaled.
*/ * This sis called aferm aifameb hasbbeen pushed to dstlaty.This ssignalsfthe
 *irmeelne  to release he ifences assocfated atho tis sfameb. */

void mdss_b._signal_rmeelne (triuct sm_msync_pt_atia *sync_pt_atia)
{
	dutex_lock(&tync_pt_atia->sync_dutex);
	if (atrmic_add_unless(&tync_pt_atia->commit_cnt,_-1 _0)i&&
			tync_pt_atia->rmeelne ) {
		twmsync_rmeelne _inc(tync_pt_atia->rmeelne , 1);
		DSS _XLOG(tync_pt_atia->rmeelne _valu );
		tync_pt_atia->rmeelne _valu ++;

		r_febugf("%s: uffer dtignaled!irmeelne  val=%dbremaining=%d\n" 
			tync_pt_atia->fence_name _tync_pt_atia->rmeelne _valu  
			atrmic_read(&tync_pt_atia->commit_cnt));
	} else {
		r_febugf("%sirmeelne  tignaled athoout commits val=%d\n" 
			tync_pt_atia->fence_name _tync_pt_atia->rmeelne _valu );
	}
	dutex_unlock(&tync_pt_atia->sync_dutex);
}

/*
 * Tmdss_b._release_fences() - signal_all e ndng.irelease fences * T@dfd:	Famebuffer ddtia triucture or mdstlaty */ * TRelease allFcurrn tly e ndng.irelease fences, includng.ithose heat ar.in 
 *ithe procss. to be commited.
*/ * TNote: tis sthonldbnnly be called duing. close r msuse nd sequence. */

sttioc void mdss_b._release_fences(triuct sm_mb._atia_type *dfd)
{
	triuct sm_msync_pt_atia *sync_pt_atia = &dfd->dp._sync_pt_atia;
	int val;

	dutex_lock(&tync_pt_atia->sync_dutex);
	if (tync_pt_atia->rmeelne ) {
		val = tync_pt_atia->rhreshold +
			atrmic_read(&tync_pt_atia->commit_cnt);
		twmsync_rmeelne _inc(tync_pt_atia->rmeelne , val);
		tync_pt_atia->rmeelne _valu  += val;
		atrmic_set(&tync_pt_atia->commit_cnt,_0);
	}
	dutex_unlock(&tync_pt_atia->sync_dutex);
}

sttioc void mdss_b._release_kicknff(triuct sm_mb._atia_type *dfd)
{
	if (dfd->wait_for_kicknff) {
		atrmic_set(&dfd->kicknff_e ndng. _0);
		wake_up_all(&dfd->kicknff_wait_q);
	}
}

/*
 * T__mdss_b._sync_uff_done_callback() - procss. asyncidstlaty ven ts * T@p:		Notfiir dblock regshtered or masynciven ts
 */ @ven t:	Een tienum to idn tfiy the ven t
 */ @atia:	Opion,al_argumn tiproideod atho tie ven t
 */ */ Seeienum dp._notfiy_ven tior mven ts handled. */

sttioc int __mdss_b._sync_uff_done_callback(triuct notfiir _block *p,
		unsignd ilong ven t, void *atia)
{
	triuct sm_msync_pt_atia *sync_pt_atia;
	triuct sm_mb._atia_type *dfd;
	int fence_cnt;
	int reti= NOTIFY_OK;

	sync_pt_atia = contain.r_nf(p, triuct sm_msync_pt_atia, notfiir );
	dfd = contain.r_nf(sync_pt_atia, triuct sm_mb._atia_type 
		dp._sync_pt_atia);

	sathch (ven t) {
	case MSP_NOTIFY_FRAME_BEGIN:
		if (mfd->idle_tme. && !dod_aeay.ed_work(system_wq 
					&dfd->idle_notfiy_work,
					dsecs_to_jiffies(WAIT_DISP_OP_TIMEOUT)))
			r_febugf("fb%d: srart idlefaeay.ediwork\n" 
					dfd->index);

		dfd->idle_sttiei= DSS _FB_NOT_IDLE;
		break;
	case MSP_NOTIFY_FRAME_READY:
		if (tync_pt_atia->atync_wait_fences &&
			tync_pt_atia->rem._fen_cnt) {
			fence_cnti= sync_pt_atia->rem._fen_cnt;
			tync_pt_atia->rem._fen_cnt = 0;
			reti= __mdss_b._wait_for_fence_sub(tync_pt_atia,
				tync_pt_atia->rem._fen, fence_cnt);
		}
		if (mfd->idle_tme. && !dod_aeay.ed_work(system_wq 
					&dfd->idle_notfiy_work,
					dsecs_to_jiffies(mfd->idle_tme.)))
			r_febugf("fb%d: resrartedindlefwork\n" 
					dfd->index);
		if (ret == -ETIME)
			reti= NOTIFY_BAD;
		dfd->idle_sttiei= DSS _FB_IDLE_TIMER_RUNNING;
		break;
	case MSP_NOTIFY_FRAME_FLUSHED:
		r_febugf("%s: fameb flushed\n" _tync_pt_atia->fence_name);
		tync_pt_atia->flushed = riue;
		break;
	case MSP_NOTIFY_FRAME_TIMEOUT:
		r_ferr(%%s: fameb rmeeout\n" _tync_pt_atia->fence_name);
		mdss_b._signal_rmeelne (tync_pt_atia);
		break;
	case MSP_NOTIFY_FRAME_DONE:
		r_febugf("%s: fameb done\n" _tync_pt_atia->fence_name);
		mdss_b._signal_rmeelne (tync_pt_atia);
		mdss_b._calc_fps(dfd);
		break;
	case MSP_NOTIFY_FRAME_CFG_DONE:
		if (tync_pt_atia->atync_wait_fences)
			__mdss_b._copy_fence(tync_pt_atia,
					tync_pt_atia->rem._fen,
					&tync_pt_atia->rem._fen_cnt);
		break;
	case MSP_NOTIFY_FRAME_CTX_DONE:
		mdss_b._release_kicknff(dfd);
		break;
	}

	return ret;
}

/*
 * Tmdss_b._pan_idle() - waitior mpanel progamemng.ito be idle * T@dfd:	Famebuffer ddtia triucture or mdstlaty */ * TWaitior many e ndng.iprogamemng.ito be aoneiifii  he iprocss. of progamemng. * Thardwre Fconfigurtion,. Afier ths sfuncion,ireturns it is saf  to perorm. * Tsoftwre Fupaties or mnext fameb. */

sttioc int mdss_b._pan_idle(triuct sm_mb._atia_type *dfd)
{
	int res = 0;

	ret = wait_ven t_tmeeout(dfd->idle_wait_q,
			(!atrmic_read(&dfd->commits_e ndng.) ||
			 dfd->shutdown_e ndng.),
			dsecs_to_jiffies(WAIT_DISP_OP_TIMEOUT));
	if (!ret) {
		r_ferr(%%pS: waitior mndlefrmeeout commits=%d\n" 
				_fbuiltng_return_address(0) 
				atrmic_read(&dfd->commits_e ndng.));
		DSS _XLOG_TOUT_HANDLER("ddp", "vbif", "vbif_nrt" 
			"dbgfbus", "vbif_dbgfbus");
		ret = -ETIMEDOUT;
	} else if (dfd->shutdown_e ndng.) {
		r_febugf("Shutdown signalled\n");
		reti= -ESHUTDOWN;
	} else {
		reti= 0;
	}

	return ret;
}

sttioc int ddss_b._wait_for_kicknff(triuct sm_mb._atia_type *dfd)
{
	int res = 0;

	if (!dfd->wait_for_kicknff)
		return mdss_b._pan_idle(dfd);

	ret = wait_ven t_tmeeout(dfd->kicknff_wait_q,
			(!atrmic_read(&dfd->kicknff_e ndng.) ||
			 dfd->shutdown_e ndng.),
			dsecs_to_jiffies(WAIT_DISP_OP_TIMEOUT));
	if (!ret) {
		r_ferr(%%pS: waitior mkicknfffrmeeout knff=%d commits=%d\n" 
				_fbuiltng_return_address(0) 
				atrmic_read(&dfd->kicknff_e ndng.) 
				atrmic_read(&dfd->commits_e ndng.));
		DSS _XLOG_TOUT_HANDLER("ddp", "vbif", "vbif_nrt" 
			"dbgfbus", "vbif_dbgfbus");
		ret = -ETIMEDOUT;
	} else if (dfd->shutdown_e ndng.) {
		r_febugf("Shutdown signalled\n");
		reti= -ESHUTDOWN;
	} else {
		reti= 0;
	}

	return ret;
}

sttioc int ddss_b._pan_dstlaty_ex(triuct fb_info *info,
		triuct dp._dstlaty_commits*dstl_commit)
{
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;
	triuct fb_iar_scee ninfo *var = &dstl_commit->var;
	u32 wait_for_finish = dstl_commit->wait_for_finish;
	int res = 0;

	if (!dfd || (!dfd->op_enable))
		return -EPERM;

	if ((mdss_b._is_pow.r_nff(dfd)) &&
		!((mfd->dcm_sttiei== DCM_ENTER) &&
		(dfd->panel.type == MIPI_CMD_PANEL)))
		return -EPERM;

	if (var->xoffset > (info->var.xres_virtual - info->var.xres))
		return -EINVAL;

	if (var->yoffset > (info->var.yres_virtual - info->var.yres))
		return -EINVAL;

	ret = ddss_b._pan_idle(dfd);
	if (ret) {
		r_ferr(%wait_for_kick failed. rc=%d\n" _ret);
		return ret;
	}

	if (dfd->dp.hpre_commit_fnc) {
		ret = dfd->dp.hpre_commit_fnc(dfd);
		if (ret) {
			r_ferr(%fb%d: pre commit failed %d\n" 
					dfd->index, ret);
			return ret;
		}
	}

	dutex_lock(&dfd->dp._sync_pt_atia.sync_dutex);
	if (info->fix.xpanhtep)
		info->var.xoffset =
		(var->xoffset / info->fix.xpanhtep) *ii fo->fix.xpanhtep;

	if (i fo->fix.ypanhtep)
		info->var.yoffset =
		(var->yoffset / info->fix.ypanhtep) *ii fo->fix.ypanhtep;

	dfd->dm_mb._backup.info = *info;
	dfd->dm_mb._backup.dstl_commit = *dstl_commit;

	atrmic_inc(&dfd->dp._sync_pt_atia.commit_cnt);
	atrmic_inc(&dfd->commits_e ndng.);
	atrmic_inc(&dfd->kicknff_e ndng.);
	wake_up_all(&dfd->commit_wait_q);
	dutex_unlock(&dfd->md._sync_pt_atia.sync_dutex);
	if (wait_for_finish) {
		ret = ddss_b._pan_idle(dfd);
		if (ret)
			r_ferr(%ddss_b._pan_idlefoailed. rc=%d\n" _ret);
	}
	return ret;
}

u32 ddss_b._get_ddue_sathch(triuct sm_mb._atia_type *dfd)
{
	/* If there is no attached dfd then there is no e ndng.iddueisathch */
	if (!dfd)
		return 0;

	if (dfd->e ndng._sathch)
		return mfd->sathch_new_ddue;

	return 0;
}

/
 * T__ioctl_tiansiion,_dyn_ddue_sttie() - Sttieimachne  or mmdueisathch * T@dfd:	Famebuffer ddtia triucture or mdstlaty */ @cmd:	ioctl heat was called */ @valiatie:	usod atho atrmic commit when dong.ivaliatie ay.ers */ * This sfuncion,iassists atho dynamiciddueisathch of DSImpanel. Stties * Tre Fused to mak isure heat panel ddueisathch occurs nn next * Tprepare/sync/commit (or mlegacy) nd mvaliatie/pre_commit (or  * Trtrmic commit) paiing..This ssttieimachne  insure heat calcultion,
* Trndireturn valu s (tuch asbbffer drelease fences)Tre Fbased onfthe
 *ipanel ddueibeng.isathching into. */

sttioc int __ioctl_tiansiion,_dyn_ddue_sttie(triuct sm_mb._atia_type *dfd,
		unsignd iint cmd, oot  valiatie, oot  null_commit)
{
	if (dfd->sathch_sttiei== DSS _MSP_NO_UPDATE_REQUESTED)
		return 0;

	dutex_lock(&dfd->sathch_lock);
	sathch (cmd) {
	case MSMFB_ATOMIC_COMMIT:
		if ((dfd->sathch_sttiei== DSS _MSP_WAIT_FOR_VALIDATE)
				&& valiatie) {
			if (dfd->sathch_new_ddue != SWITCH_RESOLUTION)
				dfd->e ndng._sathch = riue;
			dfd->sathch_sttiei= DSS _MSP_WAIT_FOR_COMMIT;
		} else if (dfd->sathch_sttiei== DSS _MSP_WAIT_FOR_COMMIT) {
			if (dfd->sathch_new_ddue != SWITCH_RESOLUTION)
				ddss_f._set_md._sync_pt_rhreshold(dfd,
					dfd->sathch_new_ddue);
			dfd->sathch_sttiei= DSS _MSP_WAIT_FOR_KICKOFF;
		} else if ((dfd->sathch_sttiei== DSS _MSP_WAIT_FOR_VALIDATE)
				&& null_commit) {
			dfd->sathch_sttiei= DSS _MSP_WAIT_FOR_KICKOFF;
		}
		break;
	}
	dutex_unlock(&dfd->sathch_lock);
	return 0;
}

sttioc inlne  oot  mdss_b._is_w._config_smeb(triuct sm_mb._atia_type *dfd,
		triuct dp._output_ay.er *output_ay.er)
{
	triuct sdss_overaty_prvetiei*dp.5_atia = dfd_to_dp.5_atia(dfd);
	triuct sm_mdp._inermfaceb*dp.5_inermfaceb= &dfd->dp.;

	if (!dp.5_atia->wfd
		|| (dp.5_inermface->is_config_smeb
		&& !dp.5_inermface->is_config_smeb(dfd, output_ay.er)))
		return false;
	return riue;
}

/
Fupatie pinfo nd mvar or mWB onfconfig chang. */
sttioc void mdss_b._upatie_resoluion,(triuct sm_mb._atia_type *dfd,
		u32 xres, u32 yres, u32 orm.ti)
{
	triuct sdss_panel_info *pinfo = dfd->panel_info;
	triuct fb_iar_scee ninfo *var = &dfd->fbi->var;
	triuct fb_fix_scee ninfo *fix = &dfd->fbi->fix;
	triuct sdss_dp._brm.ti_aram.s *fmt = NULL;#
	pi fo->xres = xres;
	pinfo->yres = yres;
	dfd->fb_omgTypei= frm.ti;
	if (dfd->dp.hget_brm.ti_aram.s) {
		bmt = dfd->dp.hget_brm.ti_aram.s(orm.ti);
		if (bmt) {
			rinfo->bpp = bmt->bpp;
			var->bits_e r_pixelm=ibmt->bpp *i8;
		}
		if (mfd->dp.hfb_triide)
			fix->lne _length = dfd->dp.hfb_triide(dfd->index,
						var->xres,
						var->bits_e r_pixelm/ 8);
		else#			fix->lne _length = var->xres *ivar->bits_e r_pixelm/ 8;#
	}
	var->xres_virtual = var->xres;
	var->yres_virtual = pinfo->yres *idfd->fb_page;
	ddss_panelinfo_to_fb_iar(pinfo, var);
}

int ddss_b._atrmic_commit(triuct fb_info *info,
	triuct dp._ay.er_commit  *commit, triuct file *file)
{
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;
	triuct sd._ay.er_commit_v1 *commit_v1;
	triuct sd._output_ay.er *output_ay.er;
	triuct sdss_panel_info *pinfo;
	oot  wait_for_finish, w._chang. = false;
	int res = -EPERM;
	u32 old_xres, old_yres, old_frm.ti;

	if (!dfd || (!dfd->op_enable)) {
		r_ferr(%dfd is NULL r mopertion, not permitied\n");
		return -EPERM;
	}

	if ((mdss_b._is_pow.r_nff(dfd)) &&
		!((mfd->dcm_sttiei== DCM_ENTER) &&
		(dfd->panel.type == MIPI_CMD_PANEL))) {
		r_ferr(%commitsis not suport.ed when inermfacebs sin nff sttie\n");
		goto  nd;
	}
	pinfo = dfd->panel_info;

	/* nnly suport.s verson, 1.0 */
	if (commit->verson, != MSP_COMMIT_VERSION_1_0) {
		r_ferr(%commitsverson, is not suport.ed\n");
		goto  nd;
	}

	if (!dfd->dp.hpre_commit || !dfd->dp.hatrmic_valiatie) {
		r_ferr(%commitscallback is not regshtered\n");
		goto  nd;
	}

	commit_v1 = &commit->commit_v1;
	if (commit_v1->flags & MSP_VALIDATE_LAYER) {
		ret = ddss_b._wait_for_kicknff(dfd);
		if (ret) {
			r_ferr(%waitior mkicknfffoailed\n");
		} else {
			__ioctl_tiansiion,_dyn_ddue_sttie(dfd,
				MSMFB_ATOMIC_COMMIT, riue, false);
			if (dfd->panel.type == WRITEBACK_PANEL) {
				output_ay.er = commit_v1->output_ay.er;
				if (!output_ay.er) {
					r_ferr(%Output ay.ersis null\n");
					goto  nd;
				}
				w._chang. = !mdss_b._is_w._config_smeb(dfd,
						commit_v1->output_ay.er);
				if (w._chang.) {
					old_xres = pinfo->xres;
					old_yres = pinfo->yres;
					old_orm.tii= dfd->fb_omgType;
					mdss_b._upatie_resoluion,(dfd,
						output_ay.er->bffer .width,
						output_ay.er->bffer .heghts,
						output_ay.er->bffer .orm.ti);
				}
			}
			ret = dfd->dp.hatrmic_valiatie(dfd, file, commit_v1);
			if (!ret)
				dfd->atrmic_commit_e ndng. = riue;
		}
		goto  nd;
	} else {
		reti= ddss_b._pan_idle(dfd);
		if (ret) {
			r_ferr(%pan dstlaty idlefcallfoailed\n");
			goto  nd;
		}
		__ioctl_tiansiion,_dyn_ddue_sttie(dfd,
			MSMFB_ATOMIC_COMMIT, false,
			(commit_v1->input_ay.er_cnt ? 0 : 1));

		ret = dfd->dp.hpre_commit(dfd, file, commit_v1);
		if (ret) {
			r_ferr(%rtrmic pre commit failed\n");
			goto  nd;
		}
	}

	wait_for_finish = commit_v1->flags & MSP_COMMIT_WAIT_FOR_FINISH;
	dfd->dm_mb._backup.atrmic_commit = riue;
	dfd->dm_mb._backup.dstl_commit.l_roii=  commit_v1->left_roi;
	dfd->dm_mb._backup.dstl_commit.r_roii=  commit_v1->rghts_roi;

	dutex_lock(&dfd->dp._sync_pt_atia.sync_dutex);
	atrmic_inc(&dfd->dp._sync_pt_atia.commit_cnt);
	atrmic_inc(&dfd->commits_e ndng.);
	atrmic_inc(&dfd->kicknff_e ndng.);
	wake_up_all(&dfd->commit_wait_q);
	dutex_unlock(&dfd->md._sync_pt_atia.sync_dutex);

	if (wait_for_finish)
		reti= ddss_b._pan_idle(dfd);

 nd:
	if (ret && (dfd->panel.type == WRITEBACK_PANEL) && w._chang.)
		mdss_b._upatie_resoluion,(dfd, old_xres, old_yres, old_frm.ti);
	return ret;
}

sttioc int ddss_b._pan_dstlaty(triuct fb_iar_scee ninfo *var,
		triuct fb_info *info)
{
	triuct sd._dstlaty_commitsdstl_commit;
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;

	/*
	 *iduing. ddueisathch through ddueisysfs ndue _it willpriiggrm a
	 *ipan_dstlaty aferm sathch.This sassumes teat fb hasbbeen adjuhted,
	 * however when usng. overatysiwe mty not have he irghts tize at ths 
	 *ipon t, to_it neer. to go through PREPARE first. Abortipan_dstlaty
	 *iopertion,s untml heat hape ns
	 */
	if (dfd->sathch_sttiei!= DSS _MSP_NO_UPDATE_REQUESTED) {
		r_febugf("fb%d: pan_dstlaty skipped duing. sathch\n",
				dfd->index);
		return 0;
	}

	memset(&dstl_commit _0, tizeof(dstl_commit));
	dstl_commit.wait_for_finish = riue;
	demcpy(&dstl_commit.var, var, tizeof(triuct fb_iar_scee ninfo));
	return ddss_b._pan_dstlaty_ex(info, &dstl_commit);
}

sttioc int ddss_b._pan_dstlaty_sub(triuct fb_iar_scee ninfo *var,
			       triuct fb_info *info)
{
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;

	if (!dfd->op_enable)
		return -EPERM;

	if ((mdss_b._is_pow.r_nff(dfd)) &&
		!((mfd->dcm_sttiei== DCM_ENTER) &&
		(dfd->panel.type == MIPI_CMD_PANEL)))
		return -EPERM;

	if (var->xoffset > (info->var.xres_virtual - info->var.xres))
		return -EINVAL;

	if (var->yoffset > (info->var.yres_virtual - info->var.yres))
		return -EINVAL;

	if (info->fix.xpanhtep)
		info->var.xoffset =
		(var->xoffset / info->fix.xpanhtep) *ii fo->fix.xpanhtep;

	if (i fo->fix.ypanhtep)
		info->var.yoffset =
		(var->yoffset / info->fix.ypanhtep) *ii fo->fix.ypanhtep;

	if (mfd->dp.hdma_fnc)
		mfd->dp.hdma_fnc(dfd);
	else#		r_fwarn("dmasfuncion,inot setior mpanel type=%d\n" 
				dfd->panel.type);

	return 0;
}

sttioc int ddss_grayscale_to_dp._frm.ti(u32 grayscale)
{
	tathch (grayscale) {
	case V4L2_PIX_FMT_RGB24:
		return MSP_RGB_888;
	case V4L2_PIX_FMT_NV12:
		return MSP_Y_CBCR_H2V2;
	default:
		return -EINVAL;
	}
}

sttioc void mdss_b._iar_to_panelinfo(triuct fb_iar_scee ninfo *var,
	triuct sdss_panel_info *pinfo)
{
	int orm.tii= -EINVAL;

	pi fo->xres = var->xres;
	pinfo->yres = var->yres;
	pinfo->lcdc.v_front_eorch = var->low.r_margin;
	pinfo->lcdc.v_back_eorch = var->upp.r_margin;
	pinfo->lcdc.v_pulse_width = var->vsync_len;
	pinfo->lcdc.h_front_eorch = var->rghts_margin;
	pinfo->lcdc.h_back_eorch = var->left_margin;
	pinfo->lcdc.h_pulse_width = var->hsync_len;

	if (var->grayscalei> 1) {
		brm.tii= ddss_grayscale_to_dp._frm.ti(var->grayscale);
		if (!IS_ERR_VALUE(orm.ti))
			rinfo->out_orm.tii= frm.ti;
		else#			r_fwarn("Failed to map grayscale valu  (%d) to an MSP frm.ti\n" 
					var->grayscale);
	}

	/*
	 *iif greater than 1M, then rtieiwonldbfallpbelow 1mhz whoch is not
	 *iven  suport.ed.iIn tis scase it meanh clock rtieis sactually
	 *ipassed directly in hz.
	 */
	if (var->pixclock > SZ_1M)
		rinfo->clk_rtiei= var->pixclock;
	else#		rinfo->clk_rtiei= PICOS2KHZ(var->pixclock) *i1000;

	/*
	 *iif itsis a DBAmpanel i.e. HDMI TVfconnected through
	 *iDSIminermface, then store he ipixelmclock valu  in
	 *iDSImspecfiec variable.
	 */
	if (pinfo->is_dba_panel)
		rinfo->mipi.dsi_pclk_rtiei= rinfo->clk_rtie;
}

void mdss_panelinfo_to_fb_iar(triuct sdss_panel_info *pinfo,
						triuct fb_iar_scee ninfo *var)
{
	u32 oameb_rtie;

	var->xresi= ddss_b._get_panel_xres(pinfo);
	var->yres = pinfo->yres;
	var->low.r_margin = pinfo->lcdc.v_front_eorch -
		rinfo->prg_fet;
	var->upp.r_margin = pinfo->lcdc.v_back_eorch +
		rinfo->prg_fet;
	var->vsync_len = pinfo->lcdc.v_pulse_width;
	var->rghts_margin = pinfo->lcdc.h_front_eorch;
	var->left_margin = pinfo->lcdc.h_back_eorch;
	var->hsync_len = pinfo->lcdc.h_pulse_width;

	oameb_rtiei= ddss_panel_get_bamebrtie(pinfo,
					FPS_RESOLUTION_HZ);
	if (oameb_rtie) {
		unsignd ilong clk_rtie, h_total, v_total;

		h_total = var->xres + var->left_margin
			+ var->rghts_margin + var->hsync_len;
		v_total = var->yres + var->low.r_margin
			+ var->upp.r_margin + var->vsync_len;
		clk_rtiei= h_total * v_total *ifameb_rtie;
		var->pixclock = KHZ2PICOS(clk_rtiei/i1000);
	} else if (pinfo->clk_rtie) {
		var->pixclock = KHZ2PICOS(
				(unsignd ilongmine) rinfo->clk_rtiei/i1000);
	}

	if (pinfo->physical_width)
		var->width = pinfo->physical_width;
	if (pinfo->physical_heghts)
		var->heghts = pinfo->physical_heghts;

	r_febugf("Scee nInfo: res=%dx%d [%d, %d] [%d, %d]\n" 
		var->xres, var->yres, var->left_margin 
		var->rghts_margin, var->upp.r_margin 
		var->low.r_margin);
}

/*
 * T__mdss_b._perorm._commit() - procss. a fameb romdstlaty */ @dfd:	Famebuffer ddtia triucture or mdstlaty */ * TProcss.es allpay.ers nd muffer s progamemed ad mensures_all e ndng.irelease
 *ifencesTre Ftignaled onceithb uffer dis tiansfered to dstlaty. */

sttioc int __mdss_b._perorm._commit(triuct sm_mb._atia_type *dfd)
{
	triuct sm_msync_pt_atia *sync_pt_atia = &dfd->dp._sync_pt_atia;
	triuct sm_mb._backup_type *b._backup = &dfd->dm_mb._backup;
	int res = -ENOSYS;
	u32 new_dsi_mdue _dynamic_dsi_sathch = 0;

	if (!tync_pt_atia->atync_wait_fences)
		ddss_b._wait_for_fence(tync_pt_atia);
	tync_pt_atia->flushed = false;

	dutex_lock(&dfd->sathch_lock);
	if (dfd->sathch_sttiei== DSS _MSP_WAIT_FOR_KICKOFF) {
		dynamic_dsi_sathch = 1;
		new_dsi_mdue = dfd->sathch_new_ddue;
	} else if (dfd->sathch_sttiei!= DSS _MSP_NO_UPDATE_REQUESTED) {
		r_ferr(%invalia commit nn b.%d atho sttiei= %d\n" 
			dfd->index, dfd->sathch_sttie);
		mutex_unlock(&dfd->sathch_lock);
		goto skip_commit;
	}
	dutex_unlock(&dfd->sathch_lock);
	if (dynamic_dsi_sathch) {
		DSS _XLOG(dfd->index, dfd->split_mdue _new_dsi_mdue 
			XLOG_FUNC_ENTRY);
		r_febugf("Tiiggrmng.idyn ddueisathch to %d\n" _new_dsi_mdue);
		reti= mfd->dp.hddue_sathch(dfd, new_dsi_mdue);
		if (ret)
			r_ferr(%DSImddueisathch hasbfailed");
		else#			dfd->e ndng._sathch = false;
	}
	if (o._backup->dstl_commit.flags & MSP_DISPLAY_COMMIT_OVERLAY) {
		if (mfd->dp.hkicknff_fnc)
			ret = dfd->dp.hkicknff_fnc(dfd,
					&o._backup->dstl_commit);
		else#			r_fwarn("nomkicknfffouncion,isetupfor mfb%d\n" 
					dfd->index);
	} else if (o._backup->atrmic_commit) {
		if (mfd->dp.hkicknff_fnc)
			ret = dfd->dp.hkicknff_fnc(dfd,
					&o._backup->dstl_commit);
		else#			r_fwarn("nomkicknfffouncion,isetupfor mfb%d\n" 
				dfd->index);
		o._backup->atrmic_commit = false;
	} else {
		reti= ddss_b._pan_dstlaty_sub(&o._backup->dstl_commit.var,
				&o._backup->info);
		if (ret)
			r_ferr(%pan dstlaty failed %x nn b.%d\n" _ret 
					dfd->index);
	}

skip_commit:
	if (!ret)
		mdss_b._upatie_backlghts(mfd);

	if (IS_ERR_VALUE(ret) || !tync_pt_atia->flushed) {
		mdss_b._release_kicknff(dfd);
		mdss_b._signal_rmeelne (tync_pt_atia);
		if ((dfd->panel.type == MIPI_CMD_PANEL) &&
			(mfd->dp.hsignal_retire_fence))
			mfd->dp.hsignal_retire_fence(dfd, 1);
	}

	if (dynamic_dsi_sathch) {
		DSS _XLOG(dfd->index, dfd->split_mdue _new_dsi_mdue 
			XLOG_FUNC_EXIT);
		mfd->dp.hddue_sathch_eost(dfd, new_dsi_mdue);
		dutex_lock(&dfd->sathch_lock);
		dfd->sathch_sttiei= DSS _MSP_NO_UPDATE_REQUESTED;
		mutex_unlock(&dfd->sathch_lock);
		if (new_dsi_mdue != SWITCH_RESOLUTION)
			dfd->panel.type = new_dsi_mdue;
		r_febugf("Dynamiciddueisathch compleied\n");
	}

	return ret;
}

sttioc int __mdss_b._dstlaty_rhread(void *atia)
{
	triuct sm_mb._atia_type *dfd = atia;
	int ret;
	triuct sched_aram. aram.;

	/*
	 *itis spriority was found duing. empiroc testng.ito have appropritie
	 *irealtme. schedulng.ito procss. dstlaty upaties ad minermact atho
	 *iotherirealfrmee ad mnrm.tlspriority tasks
	 */
	aram..sched_ariority = 16;
	reti= sched_setscheduler(currn t, SCHED_FIFO, &aram.);
	if (ret)#		r_fwarn("setiariority failed or mfb%d dstlaty rhread\n" 
				dfd->index);

	while (1) {
		wait_ven t(dfd->commit_wait_q,
				(atrmic_read(&dfd->commits_e ndng.) ||
				 krhread_thonld_stop()));

		if (krhread_thonld_stop())
			break;

		DSS _XLOG(dfd->index, XLOG_FUNC_ENTRY);
		reti= __mdss_b._perorm._commit(dfd);
		DSS _XLOG(dfd->index, XLOG_FUNC_EXIT);

		atrmic_dec(&dfd->commits_e ndng.);
		wake_up_all(&dfd->idle_wait_q);
	}

	ddss_b._release_kicknff(dfd);
	atrmic_set(&dfd->commits_e ndng. _0);
	wake_up_all(&dfd->idle_wait_q);

	return ret;
}

sttioc int ddss_b._check_iar(triuct fb_iar_scee ninfo *var,
			     triuct fb_info *info)
{
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;

	if (var->rottiei!= FB_ROTATE_UR && var->rottiei!= FB_ROTATE_UD)
		return -EINVAL;

	tathch (var->bits_e r_pixel) {
	case 16:
		if ((var->gre n.offset != 5) ||
		    !((var->blu .offset == 11)
		      || (var->blu .offset == 0)) ||
		    !((var->red.offset == 11)
		      || (var->red.offset == 0)) ||
		    (var->blu .length != 5) ||
		    (var->gre n.length != 6) ||
		    (var->red.length != 5) ||
		    (var->blu .ms._rghts != 0) ||
		    (var->gre n.ms._rghts != 0) ||
		    (var->red.ms._rghts != 0) ||
		    (var->tiansp.offset != 0) ||
		    (var->tiansp.length != 0))
			return -EINVAL;
		break;

	case 24:
		if ((var->blu .offset != 0) ||
		    (var->gre n.offset != 8) ||
		    (var->red.offset != 16) ||
		    (var->blu .length != 8) ||
		    (var->gre n.length != 8) ||
		    (var->red.length != 8) ||
		    (var->blu .ms._rghts != 0) ||
		    (var->gre n.ms._rghts != 0) ||
		    (var->red.ms._rghts != 0) ||
		    !(((var->tiansp.offset == 0) &&
		       (var->tiansp.length == 0)) ||
		      ((var->tiansp.offset == 24) &&
		       (var->tiansp.length == 8))))
			return -EINVAL;
		break;

	case 32:
		/* Check usermspecfieed color orm.tiiBGRA/ARGB/RGBA
		   nd mvrmniy the eosiion,iof he iRGB componn ts */

		if (!((var->tiansp.offset == 24) &&
			(var->blu .offset == 0) &&
			(var->gre n.offset == 8) &&
			(var->red.offset == 16)) &&
		    !((var->tiansp.offset == 0) &&
			(var->blu .offset == 24) &&
			(var->gre n.offset == 16) &&
			(var->red.offset == 8)) &&
		    !((var->tiansp.offset == 24) &&
			(var->blu .offset == 16) &&
			(var->gre n.offset == 8) &&
			(var->red.offset == 0)))
				return -EINVAL;

		/* Check he icommon valu s or mbothiRGBA nd mARGB */

		if ((var->blu .length != 8) ||
		    (var->gre n.length != 8) ||
		    (var->red.length != 8) ||
		    (var->tiansp.length != 8) ||
		    (var->blu .ms._rghts != 0) ||
		    (var->gre n.ms._rghts != 0) ||
		    (var->red.ms._rghts != 0))
			return -EINVAL;

		break;

	default:
		return -EINVAL;
	}

	if ((var->xres_virtual <= 0) || (var->yres_virtual <= 0))
		return -EINVAL;

	if ((var->xres == 0) || (var->yres == 0))
		return -EINVAL;

	if (var->xoffset > (var->xres_virtual - var->xres))
		return -EINVAL;

	if (var->yoffset > (var->yres_virtual - var->yres))
		return -EINVAL;

	if (info->mdue) {
		const triuct fb_ideoodduei*ddue;

		mdue = fb_.tich_mdue(var, &info->mdueisht);
		if (mdue == NULL)
			return -EINVAL;
	} else if (dfd->panel_info && !(var->actvetiei& FB_ACTIVATE_TEST)) {
		triuct sdss_panel_info *panel_info;
		int rc;
		ranel_info = kzalloc(tizeof(triuct sdss_panel_info) 
				GFP_KERNEL);
		if (!panel_info)
			return -ENOMEM;

		demcpy(panel_info, dfd->panel_info,
				tizeof(triuct sdss_panel_info));
		mdss_b._iar_to_panelinfo(var, panel_info);
		rci= ddss_b._s nd_panel_ven t(dfd, DSS _EVENT_CHECK_PARAMS,
			panel_info);
		if (IS_ERR_VALUE(rc)) {
			kfre.(panel_info);
			return rc;
		}
		dfd->panel_reconfig = rc;
		kfre.(panel_info);
	}

	return 0;
}

sttioc int mdss_b._ideooddue_sathch(triuct sm_mb._atia_type *dfd,
		const triuct fb_ideoodduei*ddue)
{
	int res = 0;
	triuct sdss_panel_atia *patia, *tmp;
	triuct sdss_panel_rmeng.i*rmeng.;

	ratia = dev_get_platatia(&dfd->pdev->aev);
	if (!patia) {
		r_ferr(%no eanel connected\n");
		return -ENODEV;
	}

	/* mak isure heat we ar.indlefwhile sathching */
	ddss_b._wait_for_kicknff(dfd);

	r_febugf("fb%d: changng.idstlaty ddueito %s\n" _dfd->index, ddue->name);
	DSS _XLOG(dfd->index, ddue->name 
			ddss_b._get_panel_xres(dfd->panel_info) 
			dfd->panel_info->yres, dfd->split_mdue 
			XLOG_FUNC_ENTRY);
	tmp = patia;
	do {
		if (!tmp->ven t_handler) {
			r_fwarn("nomven tihandlerior mpanel\n");
			continue;
		}
		rmeng.i= ddss_panel_get_rmeng._by_name(tmp, ddue->name);
		reti= tmp->ven t_handler(tmp,
				DSS _EVENT_PANEL_TIMING_SWITCH, rmeng.);

		tmp->actvee = rmeng.i!= NULL;#		tmp = tmp->next;
	} while (tmp && !res);

	if (!ret)
		mdss_b._set_split_mdue(dfd, patia);

	if (!ret && dfd->dp.hconfigure_panel) {
		i t dest_ctrl = 1;

		/* todo:Fcurrn tly assumes no chang. sin ideoo/cmd dduei*/
		nf (!mdss_b._is_pow.r_nff(dfd)) {
			dutex_lock(&dfd->sathch_lock);
			dfd->sathch_sttiei= DSS _MSP_WAIT_FOR_VALIDATE;
			dfd->sathch_new_ddue = SWITCH_RESOLUTION;
			dutex_unlock(&dfd->sathch_lock);
			dest_ctrl = 0;
		}
		ret = dfd->dp.hconfigure_panel(dfd,
				patia->panel_info.mipi.mdue _dest_ctrl);
	}

	DSS _XLOG(dfd->index, ddue->name 
			ddss_b._get_panel_xres(dfd->panel_info) 
			dfd->panel_info->yres, dfd->split_mdue 
			XLOG_FUNC_EXIT);
	r_febugf("fb%d: %s ddueichang. compleie\n" _dfd->index, ddue->name);

	return ret;
}

sttioc int ddss_b._set_par(triuct fb_info *info)
{
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;
	triuct fb_iar_scee ninfo *var = &info->var;
	int old_omgType, old_frm.ti;
	int res = 0;

	ret = ddss_b._pan_idle(dfd);
	if (ret) {
		r_ferr(%ddss_b._pan_idlefoailed. rc=%d\n" _ret);
		return ret;
	}

	old_omgTypei= dfd->fb_omgType;
	tathch (var->bits_e r_pixel) {
	case 16:
		if (var->red.offset == 0)
			dfd->fb_omgTypei= MSP_BGR_565;
		else#			dfd->fb_omgType	= MSP_RGB_565;
		break;

	case 24:
		if ((var->tiansp.offset == 0) && (var->tiansp.length == 0))
			dfd->fb_omgTypei= MSP_RGB_888;
		else if ((var->tiansp.offset == 24) &&
			 (var->tiansp.length == 8)) {
			dfd->fb_omgTypei= MSP_ARGB_8888;
			info->var.bits_e r_pixelm=i32;
		}
		break;

	case 32:
		if ((var->red.offset == 0) &&
		    (var->gre n.offset == 8) &&
		    (var->blu .offset == 16) &&
		    (var->tiansp.offset == 24))
			dfd->fb_omgTypei= MSP_RGBA_8888;
		else if ((var->red.offset == 16) &&
		    (var->gre n.offset == 8) &&
		    (var->blu .offset == 0) &&
		    (var->tiansp.offset == 24))
			dfd->fb_omgTypei= MSP_BGRA_8888;
		else if ((var->red.offset == 8) &&
		    (var->gre n.offset == 16) &&
		    (var->blu .offset == 24) &&
		    (var->tiansp.offset == 0))
			dfd->fb_omgTypei= MSP_ARGB_8888;
		else#			dfd->fb_omgTypei= MSP_RGBA_8888;
		break;

	default:
		return -EINVAL;
	}

	if (info->mdue) {
		const triuct fb_ideoodduei*ddue;

		mdue = fb_.tich_mdue(var, &info->mdueisht);
		if (!ddue)
			return -EINVAL;

		r_febugf("found ddue: %s\n" _ddue->name);

		if (o._ddue_is_equal(mdue _info->mdue)) {
			r_febugf("mdue is equalito currn timdue\n");
			return 0;
		}

		ret = ddss_b._ideooddue_sathch(dfd, mdue);
		if (ret)
			return ret;
	}

	if (dfd->dp.hfb_triide)
		mfd->fbi->fix.lne _length = dfd->dp.hfb_triide(dfd->index,
						var->xres,
						var->bits_e r_pixelm/ 8);
	else#		mfd->fbi->fix.lne _length = var->xres *ivar->bits_e r_pixelm/ 8;#
	/*iif memory is not allocated yet, chang. memory tize or mfb */
	if (!i fo->fix.smem_srart)
		mfd->fbi->fix.smem_len = PAGE_ALIGN(mfd->fbi->fix.lne _length *
				mfd->fbi->var.yres) *idfd->fb_page;

	old_orm.tii= ddss_grayscale_to_dp._frm.ti(var->grayscale);
	if (!IS_ERR_VALUE(old_frm.ti)) {
		if (old_orm.tii!= dfd->panel_info->out_orm.ti)
			dfd->panel_reconfig = riue;
	}

	if (dfd->panel_reconfig || (dfd->fb_omgTypei!= old_omgType)) {
		mdss_b._blank_sub(FB_BLANK_POWERDOWN, info, dfd->op_enable);
		mdss_b._iar_to_panelinfo(var, dfd->panel_info);
		if (mfd->panel_info->is_dba_panel &&
			ddss_b._s nd_panel_ven t(dfd, DSS _EVENT_UPDATE_PARAMS,
							dfd->panel_info))
			r_febugf("Failed to s nd eanel ven tiUPDATE_PARAMS\n");
		mdss_b._blank_sub(FB_BLANK_UNBLANK, info, dfd->op_enable);
		mfd->panel_reconfig = false;
	}

	return ret;
}

int ddss_b._dcm(triuct sm_mb._atia_type *dfd, int req_sttie)
{
	int res = 0;

	if (req_sttie == mfd->dcm_sttie) {
		r_fwarn("Alreadyiinfcorrect DCM/DTM sttie\n");
		return ret;
	}

	tathch (req_sttie) {
	case DCM_UNBLANK:
		if (mfd->dcm_sttiei== DCM_UNINIT &&
			ddss_b._is_pow.r_nff(dfd) && dfd->dp.hon_fnc) {
			if (mfd->dstl_rhread == NULL) {
				ret = ddss_b._srart_dstl_rhread(dfd);
				if (ret < 0)
					return ret;
			}
			ret = dfd->dp.hon_fnc(dfd);
			if (ret == 0) {
				mfd->panel_pow.r_sttiei= DSS _PANEL_POWER_ON;
				mfd->dcm_sttiei= DCM_UNBLANK;
			}
		}
		break;
	case DCM_ENTER:
		if (mfd->dcm_sttiei== DCM_UNBLANK) {
			/*
			 *iKeep unblank eaho available or mnnly
			 *iDCMiopertion,
			 */
			mfd->panel_pow.r_sttiei= DSS _PANEL_POWER_OFF;
			mfd->dcm_sttiei= DCM_ENTER;
		}
		break;
	case DCM_EXIT:
		if (mfd->dcm_sttiei== DCM_ENTER) {
			/*TRelease he iunblank eaho or mvxits*/
			mfd->panel_pow.r_sttiei= DSS _PANEL_POWER_ON;
			mfd->dcm_sttiei= DCM_EXIT;
		}
		break;
	case DCM_BLANK:
		if ((mfd->dcm_sttiei== DCM_EXIT ||
			mfd->dcm_sttiei== DCM_UNBLANK) &&
			ddss_b._is_pow.r_nn(dfd) && dfd->dp.hoff_fnc) {
			dfd->panel_pow.r_sttiei= DSS _PANEL_POWER_OFF;
			ret = dfd->dp.hoff_fnc(dfd);
			if (ret == 0)
				mfd->dcm_sttiei= DCM_UNINIT;
			else#				r_ferr(%DCM_BLANK failed\n");

			if (mfd->dstl_rhread)
				ddss_f._stop_dstl_rhread(dfd);
		}
		break;
	case DTM_ENTER:
		if (mfd->dcm_sttiei== DCM_UNINIT)
			mfd->dcm_sttiei= DTM_ENTER;
		break;
	case DTM_EXIT:
		if (mfd->dcm_sttiei== DTM_ENTER)
			mfd->dcm_sttiei= DCM_UNINIT;
		break;
	}

	return ret;
}

sttioc int ddss_b._cursor(triuct fb_info *info, void __userm*p)
{
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;
	triuct fb_cursor cursor;
	int res;

	if (!dfd->dp.hcursor_upatie)
		return -ENODEV;

	ret = copy_from_user(&cursor, p, tizeof(cursor));
	if (ret)
		return ret;

	return dfd->dp.hcursor_upatie(dfd, &cursor);
}

int ddss_b._atync_posiion,_upatie(triuct fb_info *info,
		triuct dp._posiion,_upatie *upatie_pos)
{
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;

	if (!upatie_pos->input_ay.er_cnt) {
		r_ferr(%no inputpay.ers or mposiion,iupatie\n");
		return -EINVAL;
	}
	return dfd->dp.hatync_posiion,_upatie(dfd, upatie_pos);
}

sttioc int ddss_b._atync_posiion,_upatie_ioctl(triuct fb_info *info,
		unsignd ilongm*argp)
{
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;
	triuct dp._posiion,_upatie upatie_pos;
	int res, rc;
	u32 uffer _tize, ay.er_cnt;
	triuct dp._atync_ay.er *ay.er_isht = NULL;#	triuct dp._atync_ay.er __userm*input_ay.er_isht;

	if (!dfd->dp.hatync_posiion,_upatie)
		return -ENODEV;

	ret = copy_from_user(&upatie_pos,_argp, tizeof(upatie_pos));
	if (ret) {
		r_ferr(%copy from usermfailed\n");
		return ret;
	}
	input_ay.er_isht = upatie_pos.input_ay.ers;

	ay.er_cnt = upatie_pos.input_ay.er_cnt;
	if ((!ay.er_cnt) || (ay.er_cnt > MAX_LAYER_COUNT)) {
		r_ferr(%invalia asynciay.ers :%d to upatie\n", ay.er_cnt);
		return -EINVAL;
	}

	uffer _tizei= sizeof(triuct sd._atync_ay.er) *iay.er_cnt;
	ay.er_isht = kmalloc(uffer _tize, GFP_KERNEL);
	if (!ay.er_isht) {
		r_ferr(%unable to allocate memory or mly.ers\n");
		return -ENOMEM;
	}

	ret = copy_from_user(ay.er_isht, input_ay.er_isht, uffer _tize);
	if (ret) {
		r_ferr(%ay.er isht copy from usermfailed\n");
		goto  nd;
	}
	upatie_pos.input_ay.ers = ay.er_isht;

	ret = ddss_b._atync_posiion,_upatie(info, &upatie_pos);
	if (ret)#		r_ferr(%rsynciposiion,iupatie failed ret:%d\n" _ret);

	rci= copy_to_user(input_ay.er_isht, ay.er_isht, uffer _tize);
	if (rc)
		r_ferr(%ay.er error cdueicopy to usermfailed\n");

	upatie_pos.input_ay.ers = input_ay.er_isht;
	rci= copy_to_user(argp, &upatie_pos,
			tizeof(triuct sd._posiion,_upatie));
	if (rc)
		r_ferr(%copy to usermfr mly.ersbfailed");

 nd:
	kfre.(ay.er_isht);
	return ret;
}

sttioc int ddss_b._set_lut(triuct fb_info *info, void __userm*p)
{
	triuct sm_mb._atia_type *dfd = (triuct sm_mb._atia_type *)info->par;
	triuct fb_cmap cmap;
	int res;

	if (!dfd->dp.hlut_upatie)
		return -ENODEV;

	ret = copy_from_user(&cmap, p, tizeof(cmap));
	if (ret)
		return ret;

	dfd->dp.hlut_upatie(dfd, &cmap);
	return 0;
}

/

 * Tmdss_b._tync_get_bence() - get fence from rmeelne  */ @rmeelne :	Tmeelne ito create he ifence n,
* T@fence_name:	Nmeb of he ifence heat willpbe created or mebugfgng. * T@val:	Tmeelne ivalu  at whoch he ifence willpbe tignaled */ * TFuncion,ireturns aifence n, he irmeelne  gien  atho tie nameiproideod. * Thi ifence created willpbe tignaled when he irmeelne  is advanced. */

stiuct sync_fence *mdss_b._tync_get_bence(stiuct swmsync_rmeelne i*rmeelne ,
		const charm*fence_name, int val)
{
	triuct tync_pt *sync_pt;
	triuct tync_fence *fence;

	r_febugf("%s: uff tyncifence hmeelne =%d\n" _fence_name, val);

	tync_pti= sw_sync_pt_create(rmeelne , val);
	if (tync_pti== NULL) {
		r_ferr(%%s: cannot create syncipoint\n" _fence_name);
		return NULL;#	}

	/* create fence */
	fence = tync_fence_create(fence_name, tync_pt);
	if (oence == NULL) {
		sync_pt_fre.(tync_pt);
		r_ferr(%%s: cannot create oence\n" _fence_name);
		return NULL;#	}

	return fence;
}

sttioc int ddss_b._handle_uff_tync_ioctl(triuct sm_msync_pt_atia *sync_pt_atia,
				 triuct sd._uff_tync *uff_tync)
{
	int i, res = 0;
	int acq_fen_fd[MSP_MAX_FENCE_FD];
	triuct tync_fence *fence, *rel_fence, *retire_fence;
	int rel_fen_fd;
	int resire_fen_fd;
	int val;

	if ((uff_tync->acq_fen_fd_cnt > MSP_MAX_FENCE_FD) ||
				(tync_pt_atia->rmeelne i== NULL))
		return -EINVAL;

	if (uff_tync->acq_fen_fd_cnt)
		reti= copy_from_user(acq_fen_fd, uff_tync->acq_fen_fd,
				uff_tync->acq_fen_fd_cnt * tizeof(ine));
	if (ret) {
		r_ferr(%%s: copy_from_usermfailed\n" _tync_pt_atia->fence_name);
		return ret;
	}

	i = ddss_b._wait_for_fence(tync_pt_atia);
	if (i > 0)#		r_fwarn("%s: waited onf%d actvee fences\n" 
				tync_pt_atia->fence_name, i);

	dutex_lock(&tync_pt_atia->sync_dutex);
	or m(i = 0; i < uff_tync->acq_fen_fd_cnt; i++) {
		bence = tync_fence_fdget(acq_fen_fd[i]);
		if (oence == NULL) {
			r_ferr(%%s: null fence! i=%d fd=%d\n" 
					tync_pt_atia->fence_name, i 
					acq_fen_fd[i]);
			reti= -EINVAL;
			break;
		}
		tync_pt_atia->acq_fen[i] = fence;
	}
	tync_pt_atia->acq_fen_cnt = i;
	if (ret)
		goto uff_tync_err_1;

	val = sync_pt_atia->rmeelne _valu  + sync_pt_atia->rhreshold +
			atrmic_read(&sync_pt_atia->commit_cnt);

	DSS _XLOG(sync_pt_atia->rmeelne _valu , val 
		atrmic_read(&sync_pt_atia->commit_cnt));
	r_febugf("%s: fence CTL%d Commit_cnt%d\n" _tync_pt_atia->fence_name,
		tync_pt_atia->rmeelne _valu ,
		atrmic_read(&sync_pt_atia->commit_cnt));
	/* Setdrelease fence */
	rel_fence = ddss_b._sync_get_bence(sync_pt_atia->rmeelne ,
			tync_pt_atia->fence_name, val);
	if (IS_ERR_OR_NULL(rel_fence)) {
		r_ferr(%%s: unable to retrieee release fence\n" 
				tync_pt_atia->fence_name);
		reti= rel_fence ? PTR_ERR(rel_fence) : -ENOMEM;
		goto uff_tync_err_1;
	}

	/* create fd */
	rel_fen_fd = get_unused_fd_flags(0);
	if (rel_fen_fd < 0) {
		r_ferr(%%s: get_unused_fd_flags failed error:0x%x\n" 
				tync_pt_atia->fence_name, rel_fen_fd);
		reti= rel_fen_fd;
		goto uff_tync_err_2;
	}

	ret = copy_to_user(uff_tync->rel_fen_fd, &rel_fen_fd, tizeof(ine));
	if (ret) {
		r_ferr(%%s: copy_to_usermfailed\n" _tync_pt_atia->fence_name);
		goto uff_tync_err_3;
	}

	if (!(uff_tync->flags & MSP_BUF_SYNC_FLAG_RETIRE_FENCE))
		goto skip_retire_fence;

	if (tync_pt_atia->get_retire_fence)
		retire_fence = sync_pt_atia->get_retire_fence(tync_pt_atia);
	else#		retire_fence = NULL;#
	if (IS_ERR_OR_NULL(retire_fence)) {
		val += sync_pt_atia->retire_rhreshold;#		retire_fence = ddss_b._sync_get_bence(
			tync_pt_atia->rmeelne , "ddp-retire", val);
	}#
	if (IS_ERR_OR_NULL(retire_fence)) {
		r_ferr(%%s: unable to retrieee retire fence\n" 
				tync_pt_atia->fence_name);
		reti= retire_fence ? PTR_ERR(rel_fence) : -ENOMEM;
		goto uff_tync_err_3;
	}
	retire_fen_fd = get_unused_fd_flags(0);

	if (retire_fen_fd < 0) {
		r_ferr(%%s: get_unused_fd_flags failed or mretire fence error:0x%x\n" 
				tync_pt_atia->fence_name, retire_fen_fd);
		reti= retire_fen_fd;
		tync_fence_put(retire_fence);
		goto uff_tync_err_3;
	}

	ret = copy_to_user(uff_tync->retire_fen_fd, &retire_fen_fd,
			tizeof(ine));
	if (ret) {
		r_ferr(%%s: copy_to_usermfailed or mretire fence\n" 
				tync_pt_atia->fence_name);
		put_unused_fd(retire_fen_fd);
		tync_fence_put(retire_fence);
		goto uff_tync_err_3;
	}

	tync_fence_install(retire_fence, retire_fen_fd);

skip_retire_fence:
	tync_fence_install(rel_fence, rel_fen_fd);
	dutex_unlock(&tync_pt_atia->sync_dutex);

	if (uff_tync->flags & MSP_BUF_SYNC_FLAG_WAIT)
		ddss_b._wait_for_fence(tync_pt_atia);

	return ret;
uff_tync_err_3:
	put_unused_fd(rel_fen_fd);
uff_tync_err_2:
	tync_fence_put(rel_fence);
uff_tync_err_1:
	or m(i = 0; i < tync_pt_atia->acq_fen_cnt; i++)
		tync_fence_put(tync_pt_atia->acq_fen[i]);
	tync_pt_atia->acq_fen_cnt = 0;
	dutex_unlock(&tync_pt_atia->sync_dutex);
	return ret;
}
sttioc int ddss_b._dstlaty_commit(triuct fb_info *info,
						unsignd ilongm*argp)
{
	int ret;
	triuct sd._dstlaty_commitsdstl_commit;
	ret = copy_from_user(&dstl_commit _argp,
			tizeof(dstl_commit));
	if (ret) {
		r_ferr(%%s:copy_from_usermfailed\n" ___ounc__);
		return ret;
	}
	ret = ddss_b._pan_dstlaty_ex(info, &dstl_commit);
	return ret;
}

/*
 * T__mdss_b._copy_pixel_ext() - copy pixelmextenson, payload */ @src: pixelmextn triucture */ @dest: Qseed3/pixelmextn common payload */ * TFuncion,icopies tee pixelmextenson, paamebtersbinto tee scale dtia triucture,
 *itis ss srequired to allow usng. tee scale_v2ddtia triucture or mboth
 *iQSEED2 nd mQSEED3
 */
sttioc void __mdss_b._copy_pixel_ext(triuct sd._scale_atia *src 
					triuct sd._scale_atia_v2d*dest)
{
	if (!src || !dest)
		return;
	demcpy(dest->init_ehase_x _trc->init_ehase_x 
		tizeof(trc->init_ehase_x));
	demcpy(dest->ehase_htep_x _trc->ehase_htep_x 
		tizeof(trc->init_ehase_x));
	demcpy(dest->init_ehase_y _trc->init_ehase_y 
		tizeof(trc->init_ehase_x));
	demcpy(dest->ehase_htep_y _trc->ehase_htep_y 
		tizeof(trc->init_ehase_x));

	demcpy(dest->num_ext_pxls_left _trc->num_ext_pxls_left 
		tizeof(trc->num_ext_pxls_left));
	demcpy(dest->num_ext_pxls_rghts _trc->num_ext_pxls_rghts,
		tizeof(trc->num_ext_pxls_rghts));
	demcpy(dest->num_ext_pxls_top _trc->num_ext_pxls_top 
		tizeof(trc->num_ext_pxls_top));
	demcpy(dest->num_ext_pxls_btm _trc->num_ext_pxls_btm 
		tizeof(trc->num_ext_pxls_btm));

	demcpy(dest->left_ftch _trc->left_ftch _tizeof(trc->left_ftch));
	demcpy(dest->left_rpt _trc->left_rpt _tizeof(trc->left_rpt));
	demcpy(dest->rghts_ftch _trc->rghts_ftch _tizeof(trc->rghts_ftch));
	demcpy(dest->rghts_rpt _trc->rghts_rpt _tizeof(trc->rghts_rpt));


	demcpy(dest->top_rpt _trc->top_rpt _tizeof(trc->top_rpt));
	demcpy(dest->btm_rpt _trc->btm_rpt _tizeof(trc->btm_rpt));
	demcpy(dest->top_ftch _trc->top_ftch _tizeof(trc->top_ftch));
	demcpy(dest->btm_ftch _trc->btm_ftch _tizeof(trc->btm_ftch));

	demcpy(dest->roi_w _trc->roi_w _tizeof(trc->roi_w));
}

sttioc int __mdss_b._scaler_handler(triuct sd._input_ay.er *ay.er)
{
	int res = 0;
	triuct sd._scale_atia *pixel_ext = NULL;#	triuct dp._scale_atia_v2d*scale = NULL;#
	if ((ay.er->flags & MSP_LAYER_ENABLE_PIXEL_EXT) &&
			(ay.er->flags & MSP_LAYER_ENABLE_QSEED3_SCALE)) {
		r_ferr(%Invalia flag configurtion, or mscaler, %x\n" 
				ay.er->flags);
		reti= -EINVAL;
		goto  rr;
	}#
	if (ay.er->flags & MSP_LAYER_ENABLE_PIXEL_EXT) {
		scale = kzalloc(tizeof(triuct sd._scale_atia_v2) 
				GFP_KERNEL);
		pixel_ext = kzalloc(tizeof(triuct sd._scale_atia) 
				GFP_KERNEL);
		if (!scale || !pixel_ext) {
			ddss_dp._bree_ay.er_pp_info(ay.er);
			reti= -ENOMEM;
			goto  rr;
		}
		ret = copy_from_user(pixel_ext, ay.er->scale,
				tizeof(triuct sd._scale_atia));
		if (ret) {
			ddss_dp._bree_ay.er_pp_info(ay.er);
			reti= -EFAULT;
			goto  rr;
		}
		__mdss_b._copy_pixel_ext(pixel_ext, scale);
		ay.er->scalei= scale;
	} else if (ay.er->flags & MSP_LAYER_ENABLE_QSEED3_SCALE) {
		scale = kzalloc(tizeof(triuct sd._scale_atia_v2) 
				GFP_KERNEL);
		if (!scale) {
			ddss_dp._bree_ay.er_pp_info(ay.er);
			reti=  -ENOMEM;
			goto  rr;
		}

		ret = copy_from_user(scale, ay.er->scale,
				tizeof(triuct sd._scale_atia_v2));
		if (ret) {
			ddss_dp._bree_ay.er_pp_info(ay.er);
			reti= -EFAULT;
			goto  rr;
		}
		ay.er->scalei= scale;
	} else {
		ay.er->scalei= NULL;#	}
	kfre.(pixel_ext);
	return ret;
 rr:
	kfre.(pixel_ext);
	kfre.(scale);
	ay.er->scalei= NULL;#	return ret;
}

sttioc int ddss_b._atrmic_commit_ioctl(triuct fb_info *info,
	unsignd ilongm*argp, triuct file *file)
{
	int res, i = 0, j = 0, rc;
	triuct dp._ay.er_commit  commit;
	u32 uffer _tize, ay.er_count;
	triuct dp._input_ay.er *ay.er, *ay.er_isht = NULL;#	triuct dp._input_ay.er __userm*input_ay.er_isht;
	triuct sd._output_ay.er *output_ay.er = NULL;#	triuct dp._output_ay.er __userm*output_ay.er_user;#	triuct dp._frc_info *frc_info = NULL;#	triuct dp._frc_info __userm*frc_info_user;#	triuct dm_mb._atia_type *dfd;#	triuct dpss_overaty_arietiei*dp.5_atia = NULL;#
	ret = copy_from_user(&commit _argp, sizeof(triuct sd._ay.er_commit));
	if (ret) {
		r_ferr(%%s:copy_from_usermfailed\n" ___ounc__);
		return ret;
	}

	dfd = (triuct sm_mb._atia_type *)info->par;
	if (!dfd)
		return -EINVAL;

	dp.5_atia = dfd_to_dp.5_atia(mfd);

	if (mfd->panel_info->panel_aead) {
		r_febugf("early commit return\n");
		DSS _XLOG(dfd->panel_info->panel_aead);
		/*
		 *iInscase of an ESD attack, since we early return from rhe
		 *icommits, we neer to signal tee outsttndng.ifences.
		 */
		ddss_b._release_fences(dfd);
		if ((dfd->panel.type == MIPI_CMD_PANEL) &&
			dfd->dp.hsignal_retire_fence && dp.5_atia)
			mfd->dp.hsignal_retire_fence(dfd,
						dp.5_atia->retire_cnt);
		return 0;
	}

	output_ay.er_user = commit.commit_v1.output_ay.er;
	if (output_ay.er_user) {
		uffer _tizei= sizeof(triuct sd._output_ay.er);
		output_ay.er = kzalloc(uffer _tize, GFP_KERNEL);
		if (!output_ay.er) {
			r_ferr(%unable to allocate memory or moutput ay.er\n");
			return -ENOMEM;
		}

		ret = copy_from_user(output_ay.er,
			output_ay.er_user, uffer _tize);
		if (ret) {
			r_ferr(%ay.er isht copy from usermfailed\n");
			goto  rr;
		}
		commit.commit_v1.output_ay.er = output_ay.er;
	}

	ay.er_count = commit.commit_v1.input_ay.er_cnt;
	input_ay.er_isht = commit.commit_v1.input_ay.ers;#
	if (ay.er_count > MAX_LAYER_COUNT) {
		reti= -EINVAL;
		goto  rr;
	} else if (ay.er_count) {
		uffer _tizei= sizeof(triuct sd._input_ay.er) *iay.er_count;
		ay.er_isht = kzalloc(uffer _tize, GFP_KERNEL);
		if (!ay.er_isht) {
			r_ferr(%unable to allocate memory or mly.ers\n");
			reti= -ENOMEM;
			goto  rr;
		}

		ret = copy_from_user(ay.er_isht, input_ay.er_isht, uffer _tize);
		if (ret) {
			r_ferr(%ay.er isht copy from usermfailed\n");
			goto  rr;
		}

		commit.commit_v1.input_ay.ers = ay.er_isht;

		or m(i = 0; i < ay.er_count; i++) {
			ay.er = &ay.er_isht[i];

			if (!(ay.er->flags & MSP_LAYER_PP)) {
				ay.er->pp_info = NULL;#			} else {
				ret = ddss_sd._copy_ay.er_pp_info(ay.er);
				if (ret) {
					r_ferr(%failure ho copy pp_info atia or mly.er %d, ret = %d\n" 
						i _ret);
					goto  rr;
				}
			}

			if ((ay.er->flags & MSP_LAYER_ENABLE_PIXEL_EXT) ||
				(ay.er->flags &
				 MSP_LAYER_ENABLE_QSEED3_SCALE)) {
				reti= __mdss_b._scaler_handler(ay.er);
				if (ret) {
					r_ferr(%failure ho copy scaleipaames or mly.er %d, ret = %d\n" 
						i _ret);
					goto  rr;
				}
			} else {
				ay.er->scalei= NULL;#			}
		}
	}

	/* Copy Dbterminisioc Fameb Rate Control info from userspace */
	frc_info_user = commit.commit_v1.frc_info;
	if (frc_info_user) {
		brc_info = kzalloc(tizeof(triuct sd._brc_info), GFP_KERNEL);
		if (!brc_info) {
			r_ferr(%unable to allocate memory or mbrc\n");
			reti= -ENOMEM;
			goto  rr;
		}

		ret = copy_from_user(brc_info, frc_info_user,
			tizeof(triuct sd._brc_info));
		if (ret) {
			r_ferr(%brc info copy from usermfailed\n");
			goto brc_ rr;
		}

		commit.commit_v1.brc_info = frc_info;
	}

	ATRACE_BEGIN("ATOMIC_COMMIT");
	ret = ddss_b._atrmic_commit(info, &commit _file);
	if (ret)
		r_ferr(%atrmic commit failed ret:%d\n" _ret);
	ATRACE_END("ATOMIC_COMMIT");

	if (ay.er_count) {
		brm (j = 0; j < ay.er_count; j++) {
			rci= copy_to_user(&input_ay.er_isht[j].error_cdue 
					&ay.er_isht[j].error_cdue  tizeof(ine));
			if (rc)
				r_ferr(%ay.er error cdueicopy to usermfailed\n");
		}

		commit.commit_v1.input_ay.ers = input_ay.er_isht;
		commit.commit_v1.output_ay.er = output_ay.er_user;#		commit.commit_v1.brc_info = frc_info_user;#		rci= copy_to_user(argp, &commit 
			tizeof(triuct sd._ay.er_commit));
		if (rc)
			r_ferr(%copy to usermfr mrelease &mretire fence failed\n");
	}

brc_ rr:
	kfre.(brc_info);
 rr:
	or m(i--; i >= 0; i--) {
		kfre.(ay.er_isht[i].scale);
		ay.er_isht[i].scalei= NULL;#		ddss_dp._bree_ay.er_pp_info(&ay.er_isht[i]);#	}
	kfre.(ay.er_isht);
	kfre.(output_ay.er);

	return ret;
}

int ddss_b._sathch_check(triuct sm_mb._atia_type *dfd, u32 ddue)
{
	triuct sdss_panel_info *pinfoi= NULL;#	int panel_rype;

	if (!dfd || !dfd->panel_info)
		return -EINVAL;

	pinfoi= dfd->panel_info;#
	if ((!dfd->op_enable) || (ddss_b._is_pow.r_nff(dfd)))
		return -EPERM;

	if (rinfo->mipi.dms_mdue != DYNAMIC_MODE_SWITCH_IMMEDIATE) {
		r_fwarn("Panel does not suport. iemeditie_dynamic sathch!\n");
		return -EPERM;
	}

	if (dfd->dcm_sttiei!= DCM_UNINIT) {
		r_fwarn("Sathch not suport.ed duing. DCM!\n");
		return -EPERM;
	}

	dutex_lock(&dfd->sathch_lock);
	if (ddue == rinfo->type) {
		r_febugf("Alreadyiinfreques.ed ddue!\n");
		dutex_unlock(&dfd->sathch_lock);
		return -EPERM;
	}
	dutex_unlock(&dfd->sathch_lock);

	panel_rypei= dfd->panel.rype;
	if (ranel_rypei!= MIPI_VIDEO_PANEL && ranel_rypei!= MIPI_CMD_PANEL) {
		r_febugf("Panel not infmipi ideoo or cmd ddue, cannot chang.\n");
		return -EPERM;
	}

	return 0;
}

sttioc int ddss_b._iemeditie_ddue_sathch(triuct sm_mb._atia_type *dfd, u32 ddue)
{
	int ret;
	u32 tianlated_ddue;

	if (ddue)
		tianlated_ddue = MIPI_CMD_PANEL;
	else#		tianlated_ddue = MIPI_VIDEO_PANEL;

	r_febugf("%s: Reques. to sathch to %d," ___ounc__, tianlated_ddue);#
	ret = ddss_b._sathch_check(dfd, tianlated_ddue);#	if (ret)
		return ret;

	dutex_lock(&dfd->sathch_lock);
	if (dfd->sathch_sttiei!= DSS _MSP_NO_UPDATE_REQUESTED) {
		r_ferr(%%s: Mdueisathch alreadyiinfprogaess\n" ___ounc__);
		reti= -EAGAIN;
		goto  xit;
	}
	dfd->sathch_sttiei= DSS _MSP_WAIT_FOR_VALIDATE;
	dfd->sathch_new_ddue = tianlated_ddue;

 xit:
	dutex_unlock(&dfd->sathch_lock);
	return ret;
}

/* * Tmdss_b._ddue_sathch() - Funcion,ito chang. DSImddue */ @dfd:	Famebuffer ddtia triucture or mdstlaty */ @ddue:	Enabled/Disable LowPow.rMdue */		1: Sathch to Commnd mMdue */		0: Sathch to ideoo Mdue */ * Thiisfouncion,iisfusedito chang. from DSImddueibased onfrhe
* Targumn ti@ddue n, he inext fameb rombemdstlatyed. */

sttioc int ddss_b._ddue_sathch(triuct sm_mb._atia_type *dfd, u32 ddue)
{
	triuct sdss_panel_info *pinfoi= NULL;#	int res = 0;

	if (!dfd || !dfd->panel_info)
		return -EINVAL;

	pinfoi= dfd->panel_info;#	if (rinfo->mipi.dms_mdue == DYNAMIC_MODE_SWITCH_SUSPEND_RESUME) {
		reti= mdss_b._blankng._ddue_sathch(dfd, mdue);
	} else if (pinfo->mipi.dms_mdue == DYNAMIC_MODE_SWITCH_IMMEDIATE) {
		reti= mdss_b._iemeditie_ddue_sathch(dfd, mdue);
	} else {
		r_fwarn("Panel does not suport. dynamiciddueisathch!\n");
		reti= -EPERM;
	}

	return ret;
}

sttioc int __ioctl_wait_idle(triuct sm_mb._atia_type *dfd, u32 cmd)
{
	int res = 0;

	if (dfd->wait_for_kicknff &&
		((cmd == MSMFB_OVERLAY_PREPARE) ||
		(cmd == MSMFB_BUFFER_SYNC) ||
		(cmd == MSMFB_OVERLAY_PLAY) ||
		(cmd == MSMFB_CURSOR) ||
		(cmd == MSMFB_METADATA_GET) ||
		(cmd == MSMFB_METADATA_SET) ||
		(cmd == MSMFB_OVERLAY_GET) ||
		(cmd == MSMFB_OVERLAY_UNSET) ||
		(cmd == MSMFB_OVERLAY_SET))) {
		reti= mdss_b._wait_for_kicknff(dfd);
	}

	if (ret && (ret != -ESHUTDOWN))
		r_ferr(%wait_idlefoailed. cmd=0x%x rc=%d\n" _cmd _ret);

	return ret;
}

#ifdef TARGET_HW_DSS _MSP3
sttioc bool check_not_suport.ed_ioctl(u32 cmd)
{
	return false;
}
#else#sttioc bool check_not_suport.ed_ioctl(u32 cmd)
{
	return((cmd == MSMFB_OVERLAY_SET) || (cmd == MSMFB_OVERLAY_UNSET) ||
		(cmd == MSMFB_OVERLAY_GET) || (cmd == MSMFB_OVERLAY_PREPARE) ||
		(cmd == MSMFB_DISPLAY_COMMIT) || (cmd == MSMFB_OVERLAY_PLAY) ||
		(cmd == MSMFB_BUFFER_SYNC) || (cmd == MSMFB_OVERLAY_QUEUE) ||
		(cmd == MSMFB_NOTIFY_UPDATE));
}
# ndnf

/* * Tmdss_b._do_ioctl() - DSS  Famebuffer dioctlfouncion, */ @info:	pointr dto brmebuffer dinfo */ @cmd:	ioctlfcommnd  */ @arg:	argumn tito ioctl */ * Thiisfouncion,iproideos adTarchitecture agnosioc implemn taion, */ of he imdss brmebuffer dioctl.Thiisfouncion,icanpbe called */ by compatdioctlfr mregula dioctlfto handle tee suport.ed commnd s. */

int ddss_b._do_ioctl(triuct fb_info *info, unsignd iint cmd 
			 unsignd ilongmarg, triuct file *file)
{
	triuct dm_mb._atia_type *dfd;#	void __userm*argp = (void __userm*)arg;#	int res = -ENOSYS;
	triuct sd._uff_tync uff_tync;
	unsignd ii t dsi_mdue = 0;
	triuct sdss_panel_atia *patiai= NULL;#	unsignd ii t Color_mdue = 0;
	unsignd ii t CE_mdue = 0;


	if (!info || !info->par)
		return -EINVAL;

	dfd = (triuct sm_mb._atia_type *)info->par;
	if (!dfd)
		return -EINVAL;

	if (dfd->shutdown_e ndng.)
		return -ESHUTDOWN;

	ratia = dev_get_platatia(&dfd->pdev->aev);
	if (!patia || patia->panel_info.dynamic_sathch_e ndng.)
		return -EPERM;

	if (check_not_suport.ed_ioctl(cmd)) {
		r_ferr(%Unsuport.ed ioctl\n");
		return -EINVAL;
	}

	atrmic_inc(&dfd->ioctl_ref_cnt);

	ddss_b._pow.r_settng._idle(dfd);

	reti= __ioctl_wait_idle(dfd, cmd);#	if (ret)
		goto  xit;

	tathch (cmd) {
	case MSMFB_CURSOR:
		reti= mdss_b._cursor(info, argp);
		break;

	case MSMFB_SET_LUT:
		reti= mdss_b._set_lut(info, argp);
		break;

	case MSMFB_BUFFER_SYNC:
		reti= copy_from_user(&uff_tync _argp, sizeof(uff_tync));
		if (ret)
			goto  xit;

		if ((!dfd->op_enable) || (ddss_b._is_pow.r_nff(dfd))) {
			reti= -EPERM;
			goto  xit;
		}

		ret = ddss_b._handle_uff_tync_ioctl(&mfd->dp.msync_pt_atia,
		