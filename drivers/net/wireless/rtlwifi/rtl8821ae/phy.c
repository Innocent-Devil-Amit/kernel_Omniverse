/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
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

#include "../wifi.h"
#include "../pci.h"
#include "../ps.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "rf.h"
#include "dm.h"
#include "table.h"
#include "trx.h"
#include "../btcoexist/halbt_precomp.h"
#include "hw.h"
#include "../efuse.h"

#define READ_NEXT_PAIR(array_table, v1, v2, i) \
	do { \
		i += 2; \
		v1 = array_table[i]; \
		v2 = array_table[i+1]; \
	} while (0)

static u32 _rtl8821ae_phy_rf_serial_read(struct ieee80211_hw *hw,
					 enum radio_path rfpath, u32 offset);
static void _rtl8821ae_phy_rf_serial_write(struct ieee80211_hw *hw,
					   enum radio_path rfpath, u32 offset,
					   u32 data);
static u32 _rtl8821ae_phy_calculate_bit_shift(u32 bitmask);
static bool _rtl8821ae_phy_bb8821a_config_parafile(struct ieee80211_hw *hw);
/*static bool _rtl8812ae_phy_config_mac_with_headerfile(struct ieee80211_hw *hw);*/
static bool _rtl8821ae_phy_config_mac_with_headerfile(struct ieee80211_hw *hw);
static bool _rtl8821ae_phy_config_bb_with_headerfile(struct ieee80211_hw *hw,
						     u8 configtype);
static bool _rtl8821ae_phy_config_bb_with_pgheaderfile(struct ieee80211_hw *hw,
						       u8 configtype);
static void phy_init_bb_rf_register_definition(struct ieee80211_hw *hw);

static long _rtl8821ae_phy_txpwr_idx_to_dbm(struct ieee80211_hw *hw,
					    enum wireless_mode wirelessmode,
					    u8 txpwridx);
static void rtl8821ae_phy_set_rf_on(struct ieee80211_hw *hw);
static void rtl8821ae_phy_set_io(struct ieee80211_hw *hw);

static void rtl8812ae_fixspur(struct ieee80211_hw *hw,
			      enum ht_channel_width band_width, u8 channel)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	/*C cut Item12 ADC FIFO CLOCK*/
	if (IS_VENDOR_8812A_C_CUT(rtlhal->version)) {
		if (band_width == HT_CHANNEL_WIDTH_20_40 && channel == 11)
			rtl_set_bbreg(hw, RRFMOD, 0xC00, 0x3);
			/* 0x8AC[11:10] = 2'b11*/
		else
			rtl_set_bbreg(hw, RRFMOD, 0xC00, 0x2);
			/* 0x8AC[11:10] = 2'b10*/

		/* <20120914, Kordan> A workarould to resolve
		 * 2480Mhz spur by setting ADC clock as 160M. (Asked by Binson)
		 */
		if (band_width == HT_CHANNEL_WIDTH_20 &&
		    (channel == 13 || channel == 14)) {
			rtl_set_bbreg(hw, RRFMOD, 0x300, 0x3);
			/*0x8AC[9:8] = 2'b11*/
			rtl_set_bbreg(hw, RADC_BUF_CLK, BIT(30), 1);
			/* 0x8C4[30] = 1*/
		} else if (band_width == HT_CHANNEL_WIDTH_20_40 &&
			   channel == 11) {
			rtl_set_bbreg(hw, RADC_BUF_CLK, BIT(30), 1);
			/*0x8C4[30] = 1*/
		} else if (band_width != HT_CHANNEL_WIDTH_80) {
			rtl_set_bbreg(hw, RRFMOD, 0x300, 0x2);
			/*0x8AC[9:8] = 2'b10*/
			rtl_set_bbreg(hw, RADC_BUF_CLK, BIT(30), 0);
			/*0x8C4[30] = 0*/
		}
	} else if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
		/* <20120914, Kordan> A workarould to resolve
		 * 2480Mhz spur by setting ADC clock as 160M.
		 */
		if (band_width == HT_CHANNEL_WIDTH_20 &&
		    (channel == 13 || channel == 14))
			rtl_set_bbreg(hw, RRFMOD, 0x300, 0x3);
			/*0x8AC[9:8] = 11*/
		else if (channel  <= 14) /*2.4G only*/
			rtl_set_bbreg(hw, RRFMOD, 0x300, 0x2);
			/*0x8AC[9:8] = 10*/
	}
}

u32 rtl8821ae_phy_query_bb_reg(struct ieee80211_hw *hw, u32 regaddr,
			       u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 returnvalue, originalvalue, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x)\n",
		 regaddr, bitmask);
	originalvalue = rtl_read_dword(rtlpriv, regaddr);
	bitshift = _rtl8821ae_phy_calculate_bit_shift(bitmask);
	returnvalue = (originalvalue & bitmask) >> bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "BBR MASK=0x%x Addr[0x%x]=0x%x\n",
		 bitmask, regaddr, originalvalue);
	return returnvalue;
}

void rtl8821ae_phy_set_bb_reg(struct ieee80211_hw *hw,
			      u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 originalvalue, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x)\n",
		 regaddr, bitmask, data);

	if (bitmask != MASKDWORD) {
		originalvalue = rtl_read_dword(rtlpriv, regaddr);
		bitshift = _rtl8821ae_phy_calculate_bit_shift(bitmask);
		data = ((originalvalue & (~bitmask)) |
			((data << bitshift) & bitmask));
	}

	rtl_write_dword(rtlpriv, regaddr, data);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x)\n",
		 regaddr, bitmask, data);
}

u32 rtl8821ae_phy_query_rf_reg(struct ieee80211_hw *hw,
			       enum radio_path rfpath, u32 regaddr,
			       u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, readback_value, bitshift;
	unsigned long flags;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), rfpath(%#x), bitmask(%#x)\n",
		 regaddr, rfpath, bitmask);

	spin_lock_irqsave(&rtlpriv->locks.rf_lock, flags);

	original_value = _rtl8821ae_phy_rf_serial_read(hw, rfpath, regaddr);
	bitshift = _rtl8821ae_phy_calculate_bit_shift(bitmask);
	readback_value = (original_value & bitmask) >> bitshift;

	spin_unlock_irqrestore(&rtlpriv->locks.rf_lock, flags);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), rfpath(%#x), bitmask(%#x), original_value(%#x)\n",
		 regaddr, rfpath, bitmask, original_value);

	return readback_value;
}

void rtl8821ae_phy_set_rf_reg(struct ieee80211_hw *hw,
			   enum radio_path rfpath,
			   u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, bitshift;
	unsigned long flags;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		  regaddr, bitmask, data, rfpath);

	spin_lock_irqsave(&rtlpriv->locks.rf_lock, flags);

	if (bitmask != RFREG_OFFSET_MASK) {
		original_value =
		   _rtl8821ae_phy_rf_serial_read(hw, rfpath, regaddr);
		bitshift = _rtl8821ae_phy_calculate_bit_shift(bitmask);
		data = ((original_value & (~bitmask)) | (data << bitshift));
	}

	_rtl8821ae_phy_rf_serial_write(hw, rfpath, regaddr, data);

	spin_unlock_irqrestore(&rtlpriv->locks.rf_lock, flags);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		 regaddr, bitmask, data, rfpath);
}

static u32 _rtl8821ae_phy_rf_serial_read(struct ieee80211_hw *hw,
					 enum radio_path rfpath, u32 offset)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool is_pi_mode = false;
	u32 retvalue = 0;

	/* 2009/06/17 MH We can not execute IO for power
	save or other accident mode.*/
	if (RT_CANNOT_IO(hw)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "return all one\n");
		return 0xFFFFFFFF;
	}
	/* <20120809, Kordan> CCA OFF(when entering),
		asked by James to avoid reading the wrong value.
	    <20120828, Kordan> Toggling CCA would affect RF 0x0, skip it!*/
	if (offset != 0x0 &&
	    !((rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) ||
	    (IS_VENDOR_8812A_C_CUT(rtlhal->version))))
		rtl_set_bbreg(hw, RCCAONSEC, 0x8, 1);
	offset &= 0xff;

	if (rfpath == RF90_PATH_A)
		is_pi_mode = (bool)rtl_get_bbreg(hw, 0xC00, 0x4);
	else if (rfpath == RF90_PATH_B)
		is_pi_mode = (bool)rtl_get_bbreg(hw, 0xE00, 0x4);

	rtl_set_bbreg(hw, RHSSIREAD_8821AE, 0xff, offset);

	if ((rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) ||
	    (IS_VENDOR_8812A_C_CUT(rtlhal->version)))
		udelay(20);

	if (is_pi_mode) {
		if (rfpath == RF90_PATH_A)
			retvalue =
			  rtl_get_bbreg(hw, RA_PIREAD_8821A, BLSSIREADBACKDATA);
		else if (rfpath == RF90_PATH_B)
			retvalue =
			  rtl_get_bbreg(hw, RB_PIREAD_8821A, BLSSIREADBACKDATA);
	} else {
		if (rfpath == RF90_PATH_A)
			retvalue =
			  rtl_get_bbreg(hw, RA_SIREAD_8821A, BLSSIREADBACKDATA);
		else if (rfpath == RF90_PATH_B)
			retvalue =
			  rtl_get_bbreg(hw, RB_SIREAD_8821A, BLSSIREADBACKDATA);
	}

	/*<20120809, Kordan> CCA ON(when exiting),
	 * asked by James to avoid reading the wrong value.
	 *   <20120828, Kordan> Toggling CCA would affect RF 0x0, skip it!
	 */
	if (offset != 0x0 &&
	    !((rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) ||
	    (IS_VENDOR_8812A_C_CUT(rtlhal->version))))
		rtl_set_bbreg(hw, RCCAONSEC, 0x8, 0);
	return retvalue;
}

static void _rtl8821ae_phy_rf_serial_write(struct ieee80211_hw *hw,
					   enum radio_path rfpath, u32 offset,
					   u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];
	u32 data_and_addr;
	u32 newoffset;

	if (RT_CANNOT_IO(hw)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "stop\n");
		return;
	}
	offset &= 0xff;
	newoffset = offset;
	data_and_addr = ((newoffset << 20) |
			 (data & 0x000fffff)) & 0x0fffffff;
	rtl_set_bbreg(hw, pphyreg->rf3wire_offset, MASKDWORD, data_and_addr);
	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "RFW-%d Addr[0x%x]=0x%x\n",
		 rfpath, pphyreg->rf3wire_offset, data_and_addr);
}

static u32 _rtl8821ae_phy_calculate_bit_shift(u32 bitmask)
{
	u32 i;

	for (i = 0; i <= 31; i++) {
		if (((bitmask >> i) & 0x1) == 1)
			break;
	}
	return i;
}

bool rtl8821ae_phy_mac_config(struct ieee80211_hw *hw)
{
	bool rtstatus = 0;

	rtstatus = _rtl8821ae_phy_config_mac_with_headerfile(hw);

	return rtstatus;
}

bool rtl8821ae_phy_bb_config(struct ieee80211_hw *hw)
{
	bool rtstatus = true;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 regval;
	u8 crystal_cap;

	phy_init_bb_rf_register_definition(hw);

	regval = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN);
	regval |= FEN_PCIEA;
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, regval);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN,
		       regval | FEN_BB_GLB_RSTN | FEN_BBRSTB);

	rtl_write_byte(rtlpriv, REG_RF_CTRL, 0x7);
	rtl_write_byte(rtlpriv, REG_OPT_CTRL + 2, 0x7);

	rtstatus = _rtl8821ae_phy_bb8821a_config_parafile(hw);

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
		crystal_cap = rtlefuse->crystalcap & 0x3F;
		rtl_set_bbreg(hw, REG_MAC_PHY_CTRL, 0x7FF80000,
			      (crystal_cap | (crystal_cap << 6)));
	} else {
		crystal_cap = rtlefuse->crystalcap & 0x3F;
		rtl_set_bbreg(hw, REG_MAC_PHY_CTRL, 0xFFF000,
			      (crystal_cap | (crystal_cap << 6)));
	}
	rtlphy->reg_837 = rtl_read_byte(rtlpriv, 0x837);

	return rtstatus;
}

bool rtl8821ae_phy_rf_config(struct ieee80211_hw *hw)
{
	return rtl8821ae_phy_rf6052_config(hw);
}

u32 phy_get_tx_swing_8812A(struct ieee80211_hw *hw, u8	band,
			   u8 rf_path)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_dm *rtldm = rtl_dm(rtlpriv);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	char reg_swing_2g = -1;/* 0xff; */
	char reg_swing_5g = -1;/* 0xff; */
	char swing_2g = -1 * reg_swing_2g;
	char swing_5g = -1 * reg_swing_5g;
	u32  out = 0x200;
	const char auto_temp = -1;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD,
		 "===> PHY_GetTxBBSwing_8812A, bbSwing_2G: %d, bbSwing_5G: %d,autoload_failflag=%d.\n",
		 (int)swing_2g, (int)swing_5g,
		 (int)rtlefuse->autoload_failflag);

	if (rtlefuse->autoload_failflag) {
		if (band == BAND_ON_2_4G) {
			rtldm->swing_diff_2g = swing_2g;
			if (swing_2g == 0) {
				out = 0x200; /* 0 dB */
			} else if (swing_2g == -3) {
				out = 0x16A; /* -3 dB */
			} else if (swing_2g == -6) {
				out = 0x101; /* -6 dB */
			} else if (swing_2g == -9) {
				out = 0x0B6; /* -9 dB */
			} else {
				rtldm->swing_diff_2g = 0;
				out = 0x200;
			}
		} else if (band == BAND_ON_5G) {
			rtldm->swing_diff_5g = swing_5g;
			if (swing_5g == 0) {
				out = 0x200; /* 0 dB */
			} else if (swing_5g == -3) {
				out = 0x16A; /* -3 dB */
			} else if (swing_5g == -6) {
				out = 0x101; /* -6 dB */
			} else if (swing_5g == -9) {
				out = 0x0B6; /* -9 dB */
			} else {
				if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
					rtldm->swing_diff_5g = -3;
					out = 0x16A;
				} else {
					rtldm->swing_diff_5g = 0;
					out = 0x200;
				}
			}
		} else {
			rtldm->swing_diff_2g = -3;
			rtldm->swing_diff_5g = -3;
			out = 0x16A; /* -3 dB */
		}
	} else {
	    u32 swing = 0, swing_a = 0, swing_b = 0;

	    if (band == BAND_ON_2_4G) {
			if (reg_swing_2g == auto_temp) {
				efuse_shadow_read(hw, 1, 0xC6, (u32 *)&swing);
				swing = (swing == 0xFF) ? 0x00 : swing;
			} else if (swing_2g ==  0) {
				swing = 0x00; /* 0 dB */
			} else if (swing_2g == -3) {
				swing = 0x05; /* -3 dB */
			} else if (swing_2g == -6) {
				swing = 0x0A; /* -6 dB */
			} else if (swing_2g == -9) {
				swing = 0xFF; /* -9 dB */
			} else {
				swing = 0x00;
			}
		} else {
			if (reg_swing_5g == auto_temp) {
				efuse_shadow_read(hw, 1, 0xC7, (u32 *)&swing);
				swing = (swing == 0xFF) ? 0x00 : swing;
			} else if (swing_5g ==  0) {
				swing = 0x00; /* 0 dB */
			} else if (swing_5g == -3) {
				swing = 0x05; /* -3 dB */
			} else if (swing_5g == -6) {
				swing = 0x0A; /* -6 dB */
			} else if (swing_5g == -9) {
				swing = 0xFF; /* -9 dB */
			} else {
				swing = 0x00;
			}
		}

		swing_a = (swing & 0x3) >> 0; /* 0xC6/C7[1:0] */
		swing_b = (swing & 0xC) >> 2; /* 0xC6/C7[3:2] */
		RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD,
			 "===> PHY_GetTxBBSwing_8812A, swingA: 0x%X, swingB: 0x%X\n",
			 swing_a, swing_b);

		/* 3 Path-A */
		if (swing_a == 0x0) {
			if (band == BAND_ON_2_4G)
				rtldm->swing_diff_2g = 0;
			else
				rtldm->swing_diff_5g = 0;
			out = 0x200; /* 0 dB */
		} else if (swing_a == 0x1) {
			if (band == BAND_ON_2_4G)
				rtldm->swing_diff_2g = -3;
			else
				rtldm->swing_diff_5g = -3;
			out = 0x16A; /* -3 dB */
		} else if (swing_a == 0x2) {
			if (band == BAND_ON_2_4G)
				rtldm->swing_diff_2g = -6;
			else
				rtldm->swing_diff_5g = -6;
			out = 0x101; /* -6 dB */
		} else if (swing_a == 0x3) {
			if (band == BAND_ON_2_4G)
				rtldm->swing_diff_2g = -9;
			else
				rtldm->swing_diff_5g = -9;
			out = 0x0B6; /* -9 dB */
		}
		/* 3 Path-B */
		if (swing_b == 0x0) {
			if (band == BAND_ON_2_4G)
				rtldm->swing_diff_2g = 0;
			else
				rtldm->swing_diff_5g = 0;
			out = 0x200; /* 0 dB */
		} else if (swing_b == 0x1) {
			if (band == BAND_ON_2_4G)
				rtldm->swing_diff_2g = -3;
			else
				rtldm->swing_diff_5g = -3;
			out = 0x16A; /* -3 dB */
		} else if (swing_b == 0x2) {
			if (band == BAND_ON_2_4G)
				rtldm->swing_diff_2g = -6;
			else
				rtldm->swing_diff_5g = -6;
			out = 0x101; /* -6 dB */
		} else if (swing_b == 0x3) {
			if (band == BAND_ON_2_4G)
				rtldm->swing_diff_2g = -9;
			else
				rtldm->swing_diff_5g = -9;
			out = 0x0B6; /* -9 dB */
		}
	}

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD,
		 "<=== PHY_GetTxBBSwing_8812A, out = 0x%X\n", out);
	 return out;
}

void rtl8821ae_phy_switch_wirelessband(struct ieee80211_hw *hw, u8 band)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_dm *rtldm = rtl_dm(rtlpriv);
	u8 current_band = rtlhal->current_bandtype;
	u32 txpath, rxpath;
	char bb_diff_between_band;

	txpath = rtl8821ae_phy_query_bb_reg(hw, RTXPATH, 0xf0);
	rxpath = rtl8821ae_phy_query_bb_reg(hw, RCCK_RX, 0x0f000000);
	rtlhal->current_bandtype = (enum band_type) band;
	/* reconfig BB/RF according to wireless mode */
	if (rtlhal->current_bandtype == BAND_ON_2_4G) {
		/* BB & RF Config */
		rtl_set_bbreg(hw, ROFDMCCKEN, BOFDMEN|BCCKEN, 0x03);

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
			/* 0xCB0[15:12] = 0x7 (LNA_On)*/
			rtl_set_bbreg(hw, RA_RFE_PINMUX, 0xF000, 0x7);
			/* 0xCB0[7:4] = 0x7 (PAPE_A)*/
			rtl_set_bbreg(hw, RA_RFE_PINMUX, 0xF0, 0x7);
		}

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			/*0x834[1:0] = 0x1*/
			rtl_set_bbreg(hw, 0x834, 0x3, 0x1);
		}

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
			/* 0xC1C[11:8] = 0 */
			rtl_set_bbreg(hw, RA_TXSCALE, 0xF00, 0);
		} else {
			/* 0x82C[1:0] = 2b'00 */
			rtl_set_bbreg(hw, 0x82c, 0x3, 0);
		}
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			rtl_set_bbreg(hw, RA_RFE_PINMUX, BMASKDWORD,
				      0x77777777);
			rtl_set_bbreg(hw, RB_RFE_PINMUX, BMASKDWORD,
				      0x77777777);
			rtl_set_bbreg(hw, RA_RFE_INV, 0x3ff00000, 0x000);
			rtl_set_bbreg(hw, RB_RFE_INV, 0x3ff00000, 0x000);
		}

		rtl_set_bbreg(hw, RTXPATH, 0xf0, 0x1);
		rtl_set_bbreg(hw, RCCK_RX, 0x0f000000, 0x1);

		rtl_write_byte(rtlpriv, REG_CCK_CHECK, 0x0);
	} else {/* 5G band */
		u16 count, reg_41a;

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
			/*0xCB0[15:12] = 0x5 (LNA_On)*/
			rtl_set_bbreg(hw, RA_RFE_PINMUX, 0xF000, 0x5);
			/*0xCB0[7:4] = 0x4 (PAPE_A)*/
			rtl_set_bbreg(hw, RA_RFE_PINMUX, 0xF0, 0x4);
		}
		/*CCK_CHECK_en*/
		rtl_write_byte(rtlpriv, REG_CCK_CHECK, 0x80);

		count = 0;
		reg_41a = rtl_read_word(rtlpriv, REG_TXPKT_EMPTY);
		RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD,
			 "Reg41A value %d", reg_41a);
		reg_41a &= 0x30;
		while ((reg_41a != 0x30) && (count < 50)) {
			udelay(50);
			RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD, "Delay 50us\n");

			reg_41a = rtl_read_word(rtlpriv, REG_TXPKT_EMPTY);
			reg_41a &= 0x30;
			count++;
			RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD,
				 "Reg41A value %d", reg_41a);
		}
		if (count != 0)
			RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
				 "PHY_SwitchWirelessBand8812(): Switch to 5G Band. Count = %d reg41A=0x%x\n",
				 count, reg_41a);

		/* 2012/02/01, Sinda add registry to switch workaround
		without long-run verification for scan issue. */
		rtl_set_bbreg(hw, ROFDMCCKEN, BOFDMEN|BCCKEN, 0x03);

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			/*0x834[1:0] = 0x2*/
			rtl_set_bbreg(hw, 0x834, 0x3, 0x2);
		}

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
			/* AGC table select */
			/* 0xC1C[11:8] = 1*/
			rtl_set_bbreg(hw, RA_TXSCALE, 0xF00, 1);
		} else
			/* 0x82C[1:0] = 2'b00 */
			rtl_set_bbreg(hw, 0x82c, 0x3, 1);

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			rtl_set_bbreg(hw, RA_RFE_PINMUX, BMASKDWORD,
				      0x77337777);
			rtl_set_bbreg(hw, RB_RFE_PINMUX, BMASKDWORD,
				      0x77337777);
			rtl_set_bbreg(hw, RA_RFE_INV, 0x3ff00000, 0x010);
			rtl_set_bbreg(hw, RB_RFE_INV, 0x3ff00000, 0x010);
		}

		rtl_set_bbreg(hw, RTXPATH, 0xf0, 0);
		rtl_set_bbreg(hw, RCCK_RX, 0x0f000000, 0xf);

		RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD,
			 "==>PHY_SwitchWirelessBand8812() BAND_ON_5G settings OFDM index 0x%x\n",
			 rtlpriv->dm.ofdm_index[RF90_PATH_A]);
	}

	if ((rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) ||
	    (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)) {
		/* 0xC1C[31:21] */
		rtl_set_bbreg(hw, RA_TXSCALE, 0xFFE00000,
			      phy_get_tx_swing_8812A(hw, band, RF90_PATH_A));
		/* 0xE1C[31:21] */
		rtl_set_bbreg(hw, RB_TXSCALE, 0xFFE00000,
			      phy_get_tx_swing_8812A(hw, band, RF90_PATH_B));

		/* <20121005, Kordan> When TxPowerTrack is ON,
		 *	we should take care of the change of BB swing.
		 *   That is, reset all info to trigger Tx power tracking.
		 */
		if (band != current_band) {
			bb_diff_between_band =
				(rtldm->swing_diff_2g - rtldm->swing_diff_5g);
			bb_diff_between_band = (band == BAND_ON_2_4G) ?
						bb_diff_between_band :
						(-1 * bb_diff_between_band);
			rtldm->default_ofdm_index += bb_diff_between_band * 2;
		}
		rtl8821ae_dm_clear_txpower_tracking_state(hw);
	}

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE,
		 "<==rtl8821ae_phy_switch_wirelessband():Switch Band OK.\n");
	return;
}

static bool _rtl8821ae_check_condition(struct ieee80211_hw *hw,
				       const u32 condition)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u32 _board = rtlefuse->board_type; /*need efuse define*/
	u32 _interface = 0x01; /* ODM_ITRF_PCIE */
	u32 _platform = 0x08;/* ODM_WIN */
	u32 cond = condition;

	if (condition == 0xCDCDCDCD)
		return true;

	cond = condition & 0xFF;
	if ((_board != cond) && cond != 0xFF)
		return false;

	cond = condition & 0xFF00;
	cond = cond >> 8;
	if ((_interface & cond) == 0 && cond != 0x07)
		return false;

	cond = condition & 0xFF0000;
	cond = cond >> 16;
	if ((_platform & cond) == 0 && cond != 0x0F)
		return false;
	return true;
}

static void _rtl8821ae_config_rf_reg(struct ieee80211_hw *hw,
				     u32 addr, u32 data,
				     enum radio_path rfpath, u32 regaddr)
{
	if (addr == 0xfe || addr == 0xffe) {
		/* In order not to disturb BT music when
		 * wifi init.(1ant NIC only)
		 */
		mdelay(50);
	} else {
		rtl_set_rfreg(hw, rfpath, regaddr, RFREG_OFFSET_MASK, data);
		udelay(1);
	}
}

static void _rtl8821ae_config_rf_radio_a(struct ieee80211_hw *hw,
					 u32 addr, u32 data)
{
	u32 content = 0x1000; /*RF Content: radio_a_txt*/
	u32 maskforphyset = (u32)(content & 0xE000);

	_rtl8821ae_config_rf_reg(hw, addr, data,
				 RF90_PATH_A, addr | maskforphyset);
}

static void _rtl8821ae_config_rf_radio_b(struct ieee80211_hw *hw,
					 u32 addr, u32 data)
{
	u32 content = 0x1001; /*RF Content: radio_b_txt*/
	u32 maskforphyset = (u32)(content & 0xE000);

	_rtl8821ae_config_rf_reg(hw, addr, data,
				 RF90_PATH_B, addr | maskforphyset);
}

static void _rtl8821ae_config_bb_reg(struct ieee80211_hw *hw,
				     u32 addr, u32 data)
{
	if (addr == 0xfe)
		mdelay(50);
	else if (addr == 0xfd)
		mdelay(5);
	else if (addr == 0xfc)
		mdelay(1);
	else if (addr == 0xfb)
		udelay(50);
	else if (addr == 0xfa)
		udelay(5);
	else if (addr == 0xf9)
		udelay(1);
	else
		rtl_set_bbreg(hw, addr, MASKDWORD, data);

	udelay(1);
}

static void _rtl8821ae_phy_init_tx_power_by_rate(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 band, rfpath, txnum, rate_section;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; ++band)
		for (rfpath = 0; rfpath < TX_PWR_BY_RATE_NUM_RF; ++rfpath)
			for (txnum = 0; txnum < TX_PWR_BY_RATE_NUM_RF; ++txnum)
				for (rate_section = 0;
				     rate_section < TX_PWR_BY_RATE_NUM_SECTION;
				     ++rate_section)
					rtlphy->tx_power_by_rate_offset[band]
					    [rfpath][txnum][rate_section] = 0;
}

static void _rtl8821ae_phy_set_txpower_by_rate_base(struct ieee80211_hw *hw,
					  u8 band, u8 path,
					  u8 rate_section,
					  u8 txnum, u8 value)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	if (path > RF90_PATH_D) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			"Invalid Rf Path %d in phy_SetTxPowerByRatBase()\n", path);
		return;
	}

	if (band == BAND_ON_2_4G) {
		switch (rate_section) {
		case CCK:
			rtlphy->txpwr_by_rate_base_24g[path][txnum][0] = value;
			break;
		case OFDM:
			rtlphy->txpwr_by_rate_base_24g[path][txnum][1] = value;
			break;
		case HT_MCS0_MCS7:
			rtlphy->txpwr_by_rate_base_24g[path][txnum][2] = value;
			break;
		case HT_MCS8_MCS15:
			rtlphy->txpwr_by_rate_base_24g[path][txnum][3] = value;
			break;
		case VHT_1SSMCS0_1SSMCS9:
			rtlphy->txpwr_by_rate_base_24g[path][txnum][4] = value;
			break;
		case VHT_2SSMCS0_2SSMCS9:
			rtlphy->txpwr_by_rate_base_24g[path][txnum][5] = value;
			break;
		default:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Invalid RateSection %d in Band 2.4G,Rf Path %d, %dTx in PHY_SetTxPowerByRateBase()\n",
				 rate_section, path, txnum);
			break;
		};
	} else if (band == BAND_ON_5G) {
		switch (rate_section) {
		case OFDM:
			rtlphy->txpwr_by_rate_base_5g[path][txnum][0] = value;
			break;
		case HT_MCS0_MCS7:
			rtlphy->txpwr_by_rate_base_5g[path][txnum][1] = value;
			break;
		case HT_MCS8_MCS15:
			rtlphy->txpwr_by_rate_base_5g[path][txnum][2] = value;
			break;
		case VHT_1SSMCS0_1SSMCS9:
			rtlphy->txpwr_by_rate_base_5g[path][txnum][3] = value;
			break;
		case VHT_2SSMCS0_2SSMCS9:
			rtlphy->txpwr_by_rate_base_5g[path][txnum][4] = value;
			break;
		default:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				"Invalid RateSection %d in Band 5G, Rf Path %d, %dTx in PHY_SetTxPowerByRateBase()\n",
				rate_section, path, txnum);
			break;
		};
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			"Invalid Band %d in PHY_SetTxPowerByRateBase()\n", band);
	}
}

static u8 _rtl8821ae_phy_get_txpower_by_rate_base(struct ieee80211_hw *hw,
						  u8 band, u8 path,
						  u8 txnum, u8 rate_section)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 value = 0;

	if (path > RF90_PATH_D) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Rf Path %d in PHY_GetTxPowerByRateBase()\n",
			 path);
		return 0;
	}

	if (band == BAND_ON_2_4G) {
		switch (rate_section) {
		case CCK:
			value = rtlphy->txpwr_by_rate_base_24g[path][txnum][0];
			break;
		case OFDM:
			value = rtlphy->txpwr_by_rate_base_24g[path][txnum][1];
			break;
		case HT_MCS0_MCS7:
			value = rtlphy->txpwr_by_rate_base_24g[path][txnum][2];
			break;
		case HT_MCS8_MCS15:
			value = rtlphy->txpwr_by_rate_base_24g[path][txnum][3];
			break;
		case VHT_1SSMCS0_1SSMCS9:
			value = rtlphy->txpwr_by_rate_base_24g[path][txnum][4];
			break;
		case VHT_2SSMCS0_2SSMCS9:
			value = rtlphy->txpwr_by_rate_base_24g[path][txnum][5];
			break;
		default:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Invalid RateSection %d in Band 2.4G, Rf Path %d, %dTx in PHY_GetTxPowerByRateBase()\n",
				 rate_section, path, txnum);
			break;
		};
	} else if (band == BAND_ON_5G) {
		switch (rate_section) {
		case OFDM:
			value = rtlphy->txpwr_by_rate_base_5g[path][txnum][0];
			break;
		case HT_MCS0_MCS7:
			value = rtlphy->txpwr_by_rate_base_5g[path][txnum][1];
			break;
		case HT_MCS8_MCS15:
			value = rtlphy->txpwr_by_rate_base_5g[path][txnum][2];
			break;
		case VHT_1SSMCS0_1SSMCS9:
			value = rtlphy->txpwr_by_rate_base_5g[path][txnum][3];
			break;
		case VHT_2SSMCS0_2SSMCS9:
			value = rtlphy->txpwr_by_rate_base_5g[path][txnum][4];
			break;
		default:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Invalid RateSection %d in Band 5G, Rf Path %d, %dTx in PHY_GetTxPowerByRateBase()\n",
				 rate_section, path, txnum);
			break;
		};
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Band %d in PHY_GetTxPowerByRateBase()\n", band);
	}

	return value;
}

static void _rtl8821ae_phy_store_txpower_by_rate_base(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u16 rawValue = 0;
	u8 base = 0, path = 0;

	for (path = RF90_PATH_A; path <= RF90_PATH_B; ++path) {
		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_1TX][0] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G, path, CCK, RF_1TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_1TX][2] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G, path, OFDM, RF_1TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_1TX][4] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G, path, HT_MCS0_MCS7, RF_1TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_2TX][6] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G, path, HT_MCS8_MCS15, RF_2TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_1TX][8] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G, path, VHT_1SSMCS0_1SSMCS9, RF_1TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_2TX][11] >> 8) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G, path, VHT_2SSMCS0_2SSMCS9, RF_2TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_5G][path][RF_1TX][2] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_5G, path, OFDM, RF_1TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_5G][path][RF_1TX][4] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_5G, path, HT_MCS0_MCS7, RF_1TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_5G][path][RF_2TX][6] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_5G, path, HT_MCS8_MCS15, RF_2TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_5G][path][RF_1TX][8] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_5G, path, VHT_1SSMCS0_1SSMCS9, RF_1TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_5G][path][RF_2TX][11] >> 8) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_5G, path, VHT_2SSMCS0_2SSMCS9, RF_2TX, base);
	}
}

static void _phy_convert_txpower_dbm_to_relative_value(u32 *data, u8 start,
						u8 end, u8 base_val)
{
	char i = 0;
	u8 temp_value = 0;
	u32 temp_data = 0;

	for (i = 3; i >= 0; --i) {
		if (i >= start && i <= end) {
			/* Get the exact value */
			temp_value = (u8)(*data >> (i * 8)) & 0xF;
			temp_value += ((u8)((*data >> (i * 8 + 4)) & 0xF)) * 10;

			/* Change the value to a relative value */
			temp_value = (temp_value > base_val) ? temp_value -
					base_val : base_val - temp_value;
		} else {
			temp_value = (u8)(*data >> (i * 8)) & 0xFF;
		}
		temp_data <<= 8;
		temp_data |= temp_value;
	}
	*data = temp_data;
}

static void _rtl8812ae_phy_cross_reference_ht_and_vht_txpower_limit(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 regulation, bw, channel, rate_section;
	char temp_pwrlmt = 0;

	for (regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation) {
		for (bw = 0; bw < MAX_5G_BANDWITH_NUM; ++bw) {
			for (channel = 0; channel < CHANNEL_MAX_NUMBER_5G; ++channel) {
				for (rate_section = 0; rate_section < MAX_RATE_SECTION_NUM; ++rate_section) {
					temp_pwrlmt = rtlphy->txpwr_limit_5g[regulation]
						[bw][rate_section][channel][RF90_PATH_A];
					if (temp_pwrlmt == MAX_POWER_INDEX) {
						if (bw == 0 || bw == 1) { /*5G 20M 40M VHT and HT can cross reference*/
							RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
								"No power limit table of the specified band %d, bandwidth %d, ratesection %d, channel %d, rf path %d\n",
								1, bw, rate_section, channel, RF90_PATH_A);
							if (rate_section == 2) {
								rtlphy->txpwr_limit_5g[regulation][bw][2][channel][RF90_PATH_A] =
									rtlphy->txpwr_limit_5g[regulation][bw][4][channel][RF90_PATH_A];
							} else if (rate_section == 4) {
								rtlphy->txpwr_limit_5g[regulation][bw][4][channel][RF90_PATH_A] =
									rtlphy->txpwr_limit_5g[regulation][bw][2][channel][RF90_PATH_A];
							} else if (rate_section == 3) {
								rtlphy->txpwr_limit_5g[regulation][bw][3][channel][RF90_PATH_A] =
									rtlphy->txpwr_limit_5g[regulation][bw][5][channel][RF90_PATH_A];
							} else if (rate_section == 5) {
								rtlphy->txpwr_limit_5g[regulation][bw][5][channel][RF90_PATH_A] =
									rtlphy->txpwr_limit_5g[regulation][bw][3][channel][RF90_PATH_A];
							}

							RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "use other value %d", temp_pwrlmt);
						}
					}
				}
			}
		}
	}
}

static u8 _rtl8812ae_phy_get_txpower_by_rate_base_index(struct ieee80211_hw *hw,
						   enum band_type band, u8 rate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 index = 0;
	if (band == BAND_ON_2_4G) {
		switch (rate) {
		case MGN_1M:
		case MGN_2M:
		case MGN_5_5M:
		case MGN_11M:
			index = 0;
			break;

		case MGN_6M:
		case MGN_9M:
		case MGN_12M:
		case MGN_18M:
		case MGN_24M:
		case MGN_36M:
		case MGN_48M:
		case MGN_54M:
			index = 1;
			break;

		case MGN_MCS0:
		case MGN_MCS1:
		case MGN_MCS2:
		case MGN_MCS3:
		case MGN_MCS4:
		case MGN_MCS5:
		case MGN_MCS6:
		case MGN_MCS7:
			index = 2;
			break;

		case MGN_MCS8:
		case MGN_MCS9:
		case MGN_MCS10:
		case MGN_MCS11:
		case MGN_MCS12:
		case MGN_MCS13:
		case MGN_MCS14:
		case MGN_MCS15:
			index = 3;
			break;

		default:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				"Wrong rate 0x%x to obtain index in 2.4G in PHY_GetTxPowerByRateBaseIndex()\n",
				rate);
			break;
		}
	} else if (band == BAND_ON_5G) {
		switch (rate) {
		case MGN_6M:
		case MGN_9M:
		case MGN_12M:
		case MGN_18M:
		case MGN_24M:
		case MGN_36M:
		case MGN_48M:
		case MGN_54M:
			index = 0;
			break;

		case MGN_MCS0:
		case MGN_MCS1:
		case MGN_MCS2:
		case MGN_MCS3:
		case MGN_MCS4:
		case MGN_MCS5:
		case MGN_MCS6:
		case MGN_MCS7:
			index = 1;
			break;

		case MGN_MCS8:
		case MGN_MCS9:
		case MGN_MCS10:
		case MGN_MCS11:
		case MGN_MCS12:
		case MGN_MCS13:
		case MGN_MCS14:
		case MGN_MCS15:
			index = 2;
			break;

		case MGN_VHT1SS_MCS0:
		case MGN_VHT1SS_MCS1:
		case MGN_VHT1SS_MCS2:
		case MGN_VHT1SS_MCS3:
		case MGN_VHT1SS_MCS4:
		case MGN_VHT1SS_MCS5:
		case MGN_VHT1SS_MCS6:
		case MGN_VHT1SS_MCS7:
		case MGN_VHT1SS_MCS8:
		case MGN_VHT1SS_MCS9:
			index = 3;
			break;

		case MGN_VHT2SS_MCS0:
		case MGN_VHT2SS_MCS1:
		case MGN_VHT2SS_MCS2:
		case MGN_VHT2SS_MCS3:
		case MGN_VHT2SS_MCS4:
		case MGN_VHT2SS_MCS5:
		case MGN_VHT2SS_MCS6:
		case MGN_VHT2SS_MCS7:
		case MGN_VHT2SS_MCS8:
		case MGN_VHT2SS_MCS9:
			index = 4;
			break;

		default:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				"Wrong rate 0x%x to obtain index in 5G in PHY_GetTxPowerByRateBaseIndex()\n",
				rate);
			break;
		}
	}

	return index;
}

static void _rtl8812ae_phy_convert_txpower_limit_to_power_index(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 bw40_pwr_base_dbm2_4G, bw40_pwr_base_dbm5G;
	u8 regulation, bw, channel, rate_section;
	u8 base_index2_4G = 0;
	u8 base_index5G = 0;
	char temp_value = 0, temp_pwrlmt = 0;
	u8 rf_path = 0;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		"=====> _rtl8812ae_phy_convert_txpower_limit_to_power_index()\n");

	_rtl8812ae_phy_cross_reference_ht_and_vht_txpower_limit(hw);

	for (regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation) {
		for (bw = 0; bw < MAX_2_4G_BANDWITH_NUM; ++bw) {
			for (channel = 0; channel < CHANNEL_MAX_NUMBER_2G; ++channel) {
				for (rate_section = 0; rate_section < MAX_RATE_SECTION_NUM; ++rate_section) {
					/* obtain the base dBm values in 2.4G band
					 CCK => 11M, OFDM => 54M, HT 1T => MCS7, HT 2T => MCS15*/
					if (rate_section == 0) { /*CCK*/
						base_index2_4G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_2_4G, MGN_11M);
					} else if (rate_section == 1) { /*OFDM*/
						base_index2_4G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_2_4G, MGN_54M);
					} else if (rate_section == 2) { /*HT IT*/
						base_index2_4G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_2_4G, MGN_MCS7);
					} else if (rate_section == 3) { /*HT 2T*/
						base_index2_4G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_2_4G, MGN_MCS15);
					}

					temp_pwrlmt = rtlphy->txpwr_limit_2_4g[regulation]
						[bw][rate_section][channel][RF90_PATH_A];

					for (rf_path = RF90_PATH_A;
						rf_path < MAX_RF_PATH_NUM;
						++rf_path) {
						if (rate_section == 3)
							bw40_pwr_base_dbm2_4G =
							rtlphy->txpwr_by_rate_base_24g[rf_path][RF_2TX][base_index2_4G];
						else
							bw40_pwr_base_dbm2_4G =
							rtlphy->txpwr_by_rate_base_24g[rf_path][RF_1TX][base_index2_4G];

						if (temp_pwrlmt != MAX_POWER_INDEX) {
							temp_value = temp_pwrlmt - bw40_pwr_base_dbm2_4G;
							rtlphy->txpwr_limit_2_4g[regulation]
								[bw][rate_section][channel][rf_path] =
								temp_value;
						}

						RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
							"TxPwrLimit_2_4G[regulation %d][bw %d][rateSection %d][channel %d] = %d\n(TxPwrLimit in dBm %d - BW40PwrLmt2_4G[channel %d][rfPath %d] %d)\n",
							regulation, bw, rate_section, channel,
							rtlphy->txpwr_limit_2_4g[regulation][bw]
							[rate_section][channel][rf_path], (temp_pwrlmt == 63)
							? 0 : temp_pwrlmt/2, channel, rf_path,
							bw40_pwr_base_dbm2_4G);
					}
				}
			}
		}
	}
	for (regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation) {
		for (bw = 0; bw < MAX_5G_BANDWITH_NUM; ++bw) {
			for (channel = 0; channel < CHANNEL_MAX_NUMBER_5G; ++channel) {
				for (rate_section = 0; rate_section < MAX_RATE_SECTION_NUM; ++rate_section) {
					/* obtain the base dBm values in 5G band
					 OFDM => 54M, HT 1T => MCS7, HT 2T => MCS15,
					VHT => 1SSMCS7, VHT 2T => 2SSMCS7*/
					if (rate_section == 1) { /*OFDM*/
						base_index5G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_5G, MGN_54M);
					} else if (rate_section == 2) { /*HT 1T*/
						base_index5G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_5G, MGN_MCS7);
					} else if (rate_section == 3) { /*HT 2T*/
						base_index5G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_5G, MGN_MCS15);
					} else if (rate_section == 4) { /*VHT 1T*/
						base_index5G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_5G, MGN_VHT1SS_MCS7);
					} else if (rate_section == 5) { /*VHT 2T*/
						base_index5G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_5G, MGN_VHT2SS_MCS7);
					}

					temp_pwrlmt = rtlphy->txpwr_limit_5g[regulation]
						[bw][rate_section][channel]
						[RF90_PATH_A];

					for (rf_path = RF90_PATH_A;
					     rf_path < MAX_RF_PATH_NUM;
					     ++rf_path) {
						if (rate_section == 3 || rate_section == 5)
							bw40_pwr_base_dbm5G =
							rtlphy->txpwr_by_rate_base_5g[rf_path]
							[RF_2TX][base_index5G];
						else
							bw40_pwr_base_dbm5G =
							rtlphy->txpwr_by_rate_base_5g[rf_path]
							[RF_1TX][base_index5G];

						if (temp_pwrlmt != MAX_POWER_INDEX) {
							temp_value =
								temp_pwrlmt - bw40_pwr_base_dbm5G;
							rtlphy->txpwr_limit_5g[regulation]
								[bw][rate_section][channel]
								[rf_path] = temp_value;
						}

						RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
							"TxPwrLimit_5G[regulation %d][bw %d][rateSection %d][channel %d] =%d\n(TxPwrLimit in dBm %d - BW40PwrLmt5G[chnl group %d][rfPath %d] %d)\n",
							regulation, bw, rate_section,
							channel, rtlphy->txpwr_limit_5g[regulation]
							[bw][rate_section][channel][rf_path],
							temp_pwrlmt, channel, rf_path, bw40_pwr_base_dbm5G);
					}
				}
			}
		}
	}
	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 "<===== _rtl8812ae_phy_convert_txpower_limit_to_power_index()\n");
}

static void _rtl8821ae_phy_init_txpower_limit(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 i, j, k, l, m;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "=====> _rtl8821ae_phy_init_txpower_limit()!\n");

	for (i = 0; i < MAX_REGULATION_NUM; ++i) {
		for (j = 0; j < MAX_2_4G_BANDWITH_NUM; ++j)
			for (k = 0; k < MAX_RATE_SECTION_NUM; ++k)
				for (m = 0; m < CHANNEL_MAX_NUMBER_2G; ++m)
					for (l = 0; l < MAX_RF_PATH_NUM; ++l)
						rtlphy->txpwr_limit_2_4g
								[i][j][k][m][l]
							= MAX_POWER_INDEX;
	}
	for (i = 0; i < MAX_REGULATION_NUM; ++i) {
		for (j = 0; j < MAX_5G_BANDWITH_NUM; ++j)
			for (k = 0; k < MAX_RATE_SECTION_NUM; ++k)
				for (m = 0; m < CHANNEL_MAX_NUMBER_5G; ++m)
					for (l = 0; l < MAX_RF_PATH_NUM; ++l)
						rtlphy->txpwr_limit_5g
								[i][j][k][m][l]
							= MAX_POWER_INDEX;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "<===== _rtl8821ae_phy_init_txpower_limit()!\n");
}

static void _rtl8821ae_phy_convert_txpower_dbm_to_relative_value(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 base = 0, rfPath = 0;

	for (rfPath = RF90_PATH_A; rfPath <= RF90_PATH_B; ++rfPath) {
		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_2_4G, rfPath, RF_1TX, CCK);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][0],
			0, 3, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_2_4G, rfPath, RF_1TX, OFDM);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][1],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][2],
			0, 3, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_2_4G, rfPath, RF_1TX, HT_MCS0_MCS7);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][3],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][4],
			0, 3, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_2_4G, rfPath, RF_2TX, HT_MCS8_MCS15);

		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_2TX][5],
			0, 3, base);

		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_2TX][6],
			0, 3, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_2_4G, rfPath, RF_1TX, VHT_1SSMCS0_1SSMCS9);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][7],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][8],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][9],
			0, 1, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_2_4G, rfPath, RF_2TX, VHT_2SSMCS0_2SSMCS9);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][9],
			2, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_2TX][10],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_2TX][11],
			0, 3, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_5G, rfPath, RF_1TX, OFDM);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_1TX][1],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_1TX][2],
			0, 3, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_5G, rfPath, RF_1TX, HT_MCS0_MCS7);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_1TX][3],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_1TX][4],
			0, 3, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_5G, rfPath, RF_2TX, HT_MCS8_MCS15);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_2TX][5],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_2TX][6],
			0, 3, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_5G, rfPath, RF_1TX, VHT_1SSMCS0_1SSMCS9);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_1TX][7],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_1TX][8],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_1TX][9],
			0, 1, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_5G, rfPath, RF_2TX, VHT_2SSMCS0_2SSMCS9);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_1TX][9],
			2, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_2TX][10],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_2TX][11],
			0, 3, base);
	}

	RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE,
		"<===_rtl8821ae_phy_convert_txpower_dbm_to_relative_value()\n");
}

static void _rtl8821ae_phy_txpower_by_rate_configuration(struct ieee80211_hw *hw)
{
	_rtl8821ae_phy_store_txpower_by_rate_base(hw);
	_rtl8821ae_phy_convert_txpower_dbm_to_relative_value(hw);
}

/* string is in decimal */
static bool _rtl8812ae_get_integer_from_string(char *str, u8 *pint)
{
	u16 i = 0;
	*pint = 0;

	while (str[i] != '\0') {
		if (str[i] >= '0' && str[i] <= '9') {
			*pint *= 10;
			*pint += (str[i] - '0');
		} else {
			return false;
		}
		++i;
	}

	return true;
}

static bool _rtl8812ae_eq_n_byte(u8 *str1, u8 *str2, u32 num)
{
	if (num == 0)
		return false;
	while (num > 0) {
		num--;
		if (str1[num] != str2[num])
			return false;
	}
	return true;
}

static char _rtl8812ae_phy_get_chnl_idx_of_txpwr_lmt(struct ieee80211_hw *hw,
					      u8 band, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	char channel_index = -1;
	u8 channel_5g[CHANNEL_MAX_NUMBER_5G] = {
		36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64,
		100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122,
		124, 126, 128, 130, 132, 134, 136, 138, 140, 142, 144, 149,
		151, 153, 155, 157, 159, 161, 163, 165, 167, 168, 169, 171,
		173, 175, 177};
	u8  i = 0;
	if (band == BAND_ON_2_4G)
		channel_index = channel - 1;
	else if (band == BAND_ON_5G) {
		for (i = 0; i < sizeof(channel_5g)/sizeof(u8); ++i) {
			if (channel_5g[i] == channel)
				channel_index = i;
		}
	} else
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD, "Invalid Band %d in %s",
			 band,  __func__);

	if (channel_index == -1)
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "Invalid Channel %d of Band %d in %s", channel,
			 band, __func__);

	return channel_index;
}

static void _rtl8812ae_phy_set_txpower_limit(struct ieee80211_hw *hw, u8 *pregulation,
				      u8 *pband, u8 *pbandwidth,
				      u8 *prate_section, u8 *prf_path,
				      u8 *pchannel, u8 *ppower_limit)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 regulation = 0, bandwidth = 0, rate_section = 0, channel;
	u8 channel_index;
	char power_limit = 0, prev_power_limit, ret;

	if (!_rtl8812ae_get_integer_from_string((char *)pchannel, &channel) ||
	    !_rtl8812ae_get_integer_from_string((char *)ppower_limit,
						&power_limit)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Illegal index of pwr_lmt table [chnl %d][val %d]\n",
			  channel, power_limit);
	}

	power_limit = power_limit > MAX_POWER_INDEX ?
		      MAX_POWER_INDEX : power_limit;

	if (_rtl8812ae_eq_n_byte(pregulation, (u8 *)("FCC"), 3))
		regulation = 0;
	else if (_rtl8812ae_eq_n_byte(pregulation, (u8 *)("MKK"), 3))
		regulation = 1;
	else if (_rtl8812ae_eq_n_byte(pregulation, (u8 *)("ETSI"), 4))
		regulation = 2;
	else if (_rtl8812ae_eq_n_byte(pregulation, (u8 *)("WW13"), 4))
		regulation = 3;

	if (_rtl8812ae_eq_n_byte(prate_section, (u8 *)("CCK"), 3))
		rate_section = 0;
	else if (_rtl8812ae_eq_n_byte(prate_section, (u8 *)("OFDM"), 4))
		rate_section = 1;
	else if (_rtl8812ae_eq_n_byte(prate_section, (u8 *)("HT"), 2) &&
		 _rtl8812ae_eq_n_byte(prf_path, (u8 *)("1T"), 2))
		rate_section = 2;
	else if (_rtl8812ae_eq_n_byte(prate_section, (u8 *)("HT"), 2) &&
		 _rtl8812ae_eq_n_byte(prf_path, (u8 *)("2T"), 2))
		rate_section = 3;
	else if (_rtl8812ae_eq_n_byte(prate_section, (u8 *)("VHT"), 3) &&
		 _rtl8812ae_eq_n_byte(prf_path, (u8 *)("1T"), 2))
		rate_section = 4;
	else if (_rtl8812ae_eq_n_byte(prate_section, (u8 *)("VHT"), 3) &&
		 _rtl8812ae_eq_n_byte(prf_path, (u8 *)("2T"), 2))
		rate_section = 5;

	if (_rtl8812ae_eq_n_byte(pbandwidth, (u8 *)("20M"), 3))
		bandwidth = 0;
	else if (_rtl8812ae_eq_n_byte(pbandwidth, (u8 *)("40M"), 3))
		bandwidth = 1;
	else if (_rtl8812ae_eq_n_byte(pbandwidth, (u8 *)("80M"), 3))
		bandwidth = 2;
	else if (_rtl8812ae_eq_n_byte(pbandwidth, (u8 *)("160M"), 4))
		bandwidth = 3;

	if (_rtl8812ae_eq_n_byte(pband, (u8 *)("2.4G"), 4)) {
		ret = _rtl8812ae_phy_get_chnl_idx_of_txpwr_lmt(hw,
							       BAND_ON_2_4G,
							       channel);

		if (ret == -1)
			return;

		channel_index = ret;

		prev_power_limit = rtlphy->txpwr_limit_2_4g[regulation]
						[bandwidth][rate_section]
						[channel_index][RF90_PATH_A];

		if (power_limit < prev_power_limit)
			rtlphy->txpwr_limit_2_4g[regulation][bandwidth]
				[rate_section][channel_index][RF90_PATH_A] =
								   power_limit;

		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "2.4G [regula %d][bw %d][sec %d][chnl %d][val %d]\n",
			  regulation, bandwidth, rate_section, channel_index,
			  rtlphy->txpwr_limit_2_4g[regulation][bandwidth]
				[rate_section][channel_index][RF90_PATH_A]);
	} else if (_rtl8812ae_eq_n_byte(pband, (u8 *)("5G"), 2)) {
		ret = _rtl8812ae_phy_get_chnl_idx_of_txpwr_lmt(hw,
							       BAND_ON_5G,
							       channel);

		if (ret == -1)
			return;

		channel_index = ret;

		prev_power_limit = rtlphy->txpwr_limit_5g[regulation][bandwidth]
						[rate_section][channel_index]
						[RF90_PATH_A];

		if (power_limit < prev_power_limit)
			rtlphy->txpwr_limit_5g[regulation][bandwidth]
			[rate_section][channel_index][RF90_PATH_A] = power_limit;

		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "5G: [regul %d][bw %d][sec %d][chnl %d][val %d]\n",
			  regulation, bandwidth, rate_section, channel,
			  rtlphy->txpwr_limit_5g[regulation][bandwidth]
				[rate_section][channel_index][RF90_PATH_A]);
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Cannot recognize the band info in %s\n", pband);
		return;
	}
}

static void _rtl8812ae_phy_config_bb_txpwr_lmt(struct ieee80211_hw *hw,
					  u8 *regulation, u8 *band,
					  u8 *bandwidth, u8 *rate_section,
					  u8 *rf_path, u8 *channel,
					  u8 *power_limit)
{
	_rtl8812ae_phy_set_txpower_limit(hw, regulation, band, bandwidth,
					 rate_section, rf_path, channel,
					 power_limit);
}

static void _rtl8821ae_phy_read_and_config_txpwr_lmt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u32 i = 0;
	u32 array_len;
	u8 **array;

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
		array_len = RTL8812AE_TXPWR_LMT_ARRAY_LEN;
		array = RTL8812AE_TXPWR_LMT;
	} else {
		array_len = RTL8821AE_TXPWR_LMT_ARRAY_LEN;
		array = RTL8821AE_TXPWR_LMT;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 "\n");

	for (i = 0; i < array_len; i += 7) {
		u8 *regulation = array[i];
		u8 *band = array[i+1];
		u8 *bandwidth = array[i+2];
		u8 *rate = array[i+3];
		u8 *rf_path = array[i+4];
		u8 *chnl = array[i+5];
		u8 *val = array[i+6];

		_rtl8812ae_phy_config_bb_txpwr_lmt(hw, regulation, band,
						   bandwidth, rate, rf_path,
						   chnl, val);
	}
}

static bool _rtl8821ae_phy_bb8821a_config_parafile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	bool rtstatus;

	_rtl8821ae_phy_init_txpower_limit(hw);

	/* RegEnableTxPowerLimit == 1 for 8812a & 8821a */
	if (rtlefuse->eeprom_regulatory != 2)
		_rtl8821ae_phy_read_and_config_txpwr_lmt(hw);

	rtstatus = _rtl8821ae_phy_config_bb_with_headerfile(hw,
						       BASEBAND_CONFIG_PHY_REG);
	if (rtstatus != true) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Write BB Reg Fail!!");
		return false;
	}
	_rtl8821ae_phy_init_tx_power_by_rate(hw);
	if (rtlefuse->autoload_failflag == false) {
		rtstatus = _rtl8821ae_phy_config_bb_with_pgheaderfile(hw,
						    BASEBAND_CONFIG_PHY_REG);
	}
	if (rtstatus != true) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "BB_PG Reg Fail!!");
		return false;
	}

	_rtl8821ae_phy_txpower_by_rate_configuration(hw);

	/* RegEnableTxPowerLimit == 1 for 8812a & 8821a */
	if (rtlefuse->eeprom_regulatory != 2)
		_rtl8812ae_phy_convert_txpower_limit_to_power_index(hw);

	rtstatus = _rtl8821ae_phy_config_bb_with_headerfile(hw,
						BASEBAND_CONFIG_AGC_TAB);

	if (rtstatus != true) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "AGC Table Fail\n");
		return false;
	}
	rtlphy->cck_high_power = (bool)(rtl_get_bbreg(hw,
			RFPGA0_XA_HSSIPARAMETER2, 0x200));
	return true;
}

static bool _rtl8821ae_phy_config_mac_with_headerfile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u32 i, v1, v2;
	u32 arraylength;
	u32 *ptrarray;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "Read MAC_REG_Array\n");
	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
		arraylength = RTL8821AEMAC_1T_ARRAYLEN;
		ptrarray = RTL8821AE_MAC_REG_ARRAY;
	} else {
		arraylength = RTL8812AEMAC_1T_ARRAYLEN;
		ptrarray = RTL8812AE_MAC_REG_ARRAY;
	}
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Img: MAC_REG_ARRAY LEN %d\n", arraylength);
	for (i = 0; i < arraylength; i += 2) {
		v1 = ptrarray[i];
		v2 = (u8)ptrarray[i + 1];
		if (v1 < 0xCDCDCDCD) {
			rtl_write_byte(rtlpriv, v1, (u8)v2);
			continue;
		} else {
			if (!_rtl8821ae_check_condition(hw, v1)) {
				/*Discard the following (offset, data) pairs*/
				READ_NEXT_PAIR(ptrarray, v1, v2, i);
				while (v2 != 0xDEAD &&
				       v2 != 0xCDEF &&
				       v2 != 0xCDCD && i < arraylength - 2) {
					READ_NEXT_PAIR(ptrarray, v1, v2, i);
				}
				i -= 2; /* prevent from for-loop += 2*/
			} else {/*Configure matched pairs and skip to end of if-else.*/
				READ_NEXT_PAIR(ptrarray, v1, v2, i);
				while (v2 != 0xDEAD &&
				       v2 != 0xCDEF &&
				       v2 != 0xCDCD && i < arraylength - 2) {
					rtl_write_byte(rtlpriv, v1, v2);
					READ_NEXT_PAIR(ptrarray, v1, v2, i);
				}

				while (v2 != 0xDEAD && i < arraylength - 2)
					READ_NEXT_PAIR(ptrarray, v1, v2, i);
			}
		}
	}
	return true;
}

static bool _rtl8821ae_phy_config_bb_with_headerfile(struct ieee80211_hw *hw,
						     u8 configtype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	int i;
	u32 *array_table;
	u16 arraylen;
	u32 v1 = 0, v2 = 0;

	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			arraylen = RTL8812AEPHY_REG_1TARRAYLEN;
			array_table = RTL8812AE_PHY_REG_ARRAY;
		} else {
			arraylen = RTL8821AEPHY_REG_1TARRAYLEN;
			array_table = RTL8821AE_PHY_REG_ARRAY;
		}

		for (i = 0; i < arraylen; i += 2) {
			v1 = array_table[i];
			v2 = array_table[i + 1];
			if (v1 < 0xCDCDCDCD) {
				_rtl8821ae_config_bb_reg(hw, v1, v2);
				continue;
			} else {/*This line is the start line of branch.*/
				if (!_rtl8821ae_check_condition(hw, v1)) {
					/*Discard the following (offset, data) pairs*/
					READ_NEXT_PAIR(array_table, v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < arraylen - 2) {
						READ_NEXT_PAIR(array_table, v1,
								v2, i);
					}

					i -= 2; /* prevent from for-loop += 2*/
				} else {/*Configure matched pairs and skip to end of if-else.*/
					READ_NEXT_PAIR(array_table, v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < arraylen - 2) {
						_rtl8821ae_config_bb_reg(hw, v1,
									 v2);
						READ_NEXT_PAIR(array_table, v1,
							       v2, i);
					}

					while (v2 != 0xDEAD &&
					       i < arraylen - 2) {
						READ_NEXT_PAIR(array_table, v1,
							       v2, i);
					}
				}
			}
		}
	} else if (configtype == BASEBAND_CONFIG_AGC_TAB) {
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			arraylen = RTL8812AEAGCTAB_1TARRAYLEN;
			array_table = RTL8812AE_AGC_TAB_ARRAY;
		} else {
			arraylen = RTL8821AEAGCTAB_1TARRAYLEN;
			array_table = RTL8821AE_AGC_TAB_ARRAY;
		}

		for (i = 0; i < arraylen; i = i + 2) {
			v1 = array_table[i];
			v2 = array_table[i+1];
			if (v1 < 0xCDCDCDCD) {
				rtl_set_bbreg(hw, v1, MASKDWORD, v2);
				udelay(1);
				continue;
			} else {/*This line is the start line of branch.*/
				if (!_rtl8821ae_check_condition(hw, v1)) {
					/*Discard the following (offset, data) pairs*/
					READ_NEXT_PAIR(array_table, v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < arraylen - 2) {
						READ_NEXT_PAIR(array_table, v1,
								v2, i);
					}
					i -= 2; /* prevent from for-loop += 2*/
				} else {/*Configure matched pairs and skip to end of if-else.*/
					READ_NEXT_PAIR(array_table, v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < arraylen - 2) {
						rtl_set_bbreg(hw, v1, MASKDWORD,
							      v2);
						udelay(1);
						READ_NEXT_PAIR(array_table, v1,
							       v2, i);
					}

					while (v2 != 0xDEAD &&
						i < arraylen - 2) {
						READ_NEXT_PAIR(array_table, v1,
								v2, i);
					}
				}
				RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
					 "The agctab_array_table[0] is %x Rtl818EEPHY_REGArray[1] is %x\n",
					  array_table[i],  array_table[i + 1]);
			}
		}
	}
	return true;
}

static u8 _rtl8821ae_get_rate_section_index(u32 regaddr)
{
	u8 index = 0;
	regaddr &= 0xFFF;
	if (regaddr >= 0xC20 && regaddr <= 0xC4C)
		index = (u8)((regaddr - 0xC20) / 4);
	else if (regaddr >= 0xE20 && regaddr <= 0xE4C)
		index = (u8)((regaddr - 0xE20) / 4);
	else
		RT_ASSERT(!COMP_INIT,
			  "Invalid RegAddr 0x%x\n", regaddr);
	return index;
}

static void _rtl8821ae_store_tx_power_by_rate(struct ieee80211_hw *hw,
					      u32 band, u32 rfpath,
					      u32 txnum, u32 regaddr,
					      u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 rate_section = _rtl8821ae_get_rate_section_index(regaddr);

	if (band != BAND_ON_2_4G && band != BAND_ON_5G) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_WARNING, "Invalid Band %d\n", band);
		band = BAND_ON_2_4G;
	}
	if (rfpath >= MAX_RF_PATH) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_WARNING, "Invalid RfPath %d\n", rfpath);
		rfpath = MAX_RF_PATH - 1;
	}
	if (txnum >= MAX_RF_PATH) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_WARNING, "Invalid TxNum %d\n", txnum);
		txnum = MAX_RF_PATH - 1;
	}
	rtlphy->tx_power_by_rate_offset[band][rfpath][txnum][rate_section] = data;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "TxPwrByRateOffset[Band %d][RfPath %d][TxNum %d][RateSection %d] = 0x%x\n",
		 band, rfpath, txnum, rate_section,
		 rtlphy->tx_power_by_rate_offset[band][rfpath][txnum][rate_section]);
}

static bool _rtl8821ae_phy_config_bb_with_pgheaderfile(struct ieee80211_hw *hw,
							u8 configtype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	int i;
	u32 *array;
	u16 arraylen;
	u32 v1, v2, v3, v4, v5, v6;

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
		arraylen = RTL8812AEPHY_REG_ARRAY_PGLEN;
		array = RTL8812AE_PHY_REG_ARRAY_PG;
	} else {
		arraylen = RTL8821AEPHY_REG_ARRAY_PGLEN;
		array = RTL8821AE_PHY_REG_ARRAY_PG;
	}

	if (configtype != BASEBAND_CONFIG_PHY_REG) {
		RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE,
			 "configtype != BaseBand_Config_PHY_REG\n");
		return true;
	}
	for (i = 0; i < arraylen; i += 6) {
		v1 = array[i];
		v2 = array[i+1];
		v3 = array[i+2];
		v4 = array[i+3];
		v5 = array[i+4];
		v6 = array[i+5];

		if (v1 < 0xCDCDCDCD) {
			if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE &&
				(v4 == 0xfe || v4 == 0xffe)) {
				msleep(50);
				continue;
			}

			if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
				if (v4 == 0xfe)
					msleep(50);
				else if (v4 == 0xfd)
					mdelay(5);
				else if (v4 == 0xfc)
					mdelay(1);
				else if (v4 == 0xfb)
					udelay(50);
				else if (v4 == 0xfa)
					udelay(5);
				else if (v4 == 0xf9)
					udelay(1);
			}
			_rtl8821ae_store_tx_power_by_rate(hw, v1, v2, v3,
							  v4, v5, v6);
			continue;
		} else {
			 /*don't need the hw_body*/
			if (!_rtl8821ae_check_condition(hw, v1)) {
				i += 2; /* skip the pair of expression*/
				v1 = array[i];
				v2 = array[i+1];
				v3 = array[i+2];
				while (v2 != 0xDEAD) {
					i += 3;
					v1 = array[i];
					v2 = array[i+1];
					v3 = array[i+2];
				}
			}
		}
	}

	return true;
}

bool rtl8812ae_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
					     enum radio_path rfpath)
{
	int i;
	bool rtstatus = true;
	u32 *radioa_array_table_a, *radioa_array_table_b;
	u16 radioa_arraylen_a, radioa_arraylen_b;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 v1 = 0, v2 = 0;

	radioa_arraylen_a = RTL8812AE_RADIOA_1TARRAYLEN;
	radioa_array_table_a = RTL8812AE_RADIOA_ARRAY;
	radioa_arraylen_b = RTL8812AE_RADIOB_1TARRAYLEN;
	radioa_array_table_b = RTL8812AE_RADIOB_ARRAY;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Radio_A:RTL8821AE_RADIOA_ARRAY %d\n", radioa_arraylen_a);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Radio No %x\n", rfpath);
	rtstatus = true;
	switch (rfpath) {
	case RF90_PATH_A:
		for (i = 0; i < radioa_arraylen_a; i = i + 2) {
			v1 = radioa_array_table_a[i];
			v2 = radioa_array_table_a[i+1];
			if (v1 < 0xcdcdcdcd) {
				_rtl8821ae_config_rf_radio_a(hw, v1, v2);
				continue;
			} else{/*This line is the start line of branch.*/
				if (!_rtl8821ae_check_condition(hw, v1)) {
					/*Discard the following (offset, data) pairs*/
					READ_NEXT_PAIR(radioa_array_table_a, v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD && i < radioa_arraylen_a-2)
						READ_NEXT_PAIR(radioa_array_table_a, v1, v2, i);

					i -= 2; /* prevent from for-loop += 2*/
				} else {/*Configure matched pairs and skip to end of if-else.*/
					READ_NEXT_PAIR(radioa_array_table_a, v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD && i < radioa_arraylen_a - 2) {
						_rtl8821ae_config_rf_radio_a(hw, v1, v2);
						READ_NEXT_PAIR(radioa_array_table_a, v1, v2, i);
					}

					while (v2 != 0xDEAD && i < radioa_arraylen_a-2)
						READ_NEXT_PAIR(radioa_array_table_a, v1, v2, i);

				}
			}
		}
		break;
	case RF90_PATH_B:
		for (i = 0; i < radioa_arraylen_b; i = i + 2) {
			v1 = radioa_array_table_b[i];
			v2 = radioa_array_table_b[i+1];
			if (v1 < 0xcdcdcdcd) {
				_rtl8821ae_config_rf_radio_b(hw, v1, v2);
				continue;
			} else{/*This line is the start line of branch.*/
				if (!_rtl8821ae_check_condition(hw, v1)) {
					/*Discard the following (offset, data) pairs*/
					READ_NEXT_PAIR(radioa_array_table_b, v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD && i < radioa_arraylen_b-2)
						READ_NEXT_PAIR(radioa_array_table_b, v1, v2, i);

					i -= 2; /* prevent from for-loop += 2*/
				} else {/*Configure matched pairs and skip to end of if-else.*/
					READ_NEXT_PAIR(radioa_array_table_b, v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD && i < radioa_arraylen_b-2) {
						_rtl8821ae_config_rf_radio_b(hw, v1, v2);
						READ_NEXT_PAIR(radioa_array_table_b, v1, v2, i);
					}

					while (v2 != 0xDEAD && i < radioa_arraylen_b-2)
						READ_NEXT_PAIR(radioa_array_table_b, v1, v2, i);
				}
			}
		}
		break;
	case RF90_PATH_C:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not process\n");
		break;
	case RF90_PATH_D:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not process\n");
		break;
	}
	return true;
}

bool rtl8821ae_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
						enum radio_path rfpath)
{
	#define READ_NEXT_RF_PAIR(v1, v2, i) \
	do { \
		i += 2; \
		v1 = radioa_array_table[i]; \
		v2 = radioa_array_table[i+1]; \
	} \
	while (0)

	int i;
	bool rtstatus = true;
	u32 *radioa_array_table;
	u16 radioa_arraylen;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	/* struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw)); */
	u32 v1 = 0, v2 = 0;

	radioa_arraylen = RTL8821AE_RADIOA_1TARRAYLEN;
	radioa_array_table = RTL8821AE_RADIOA_ARRAY;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Radio_A:RTL8821AE_RADIOA_ARRAY %d\n", radioa_arraylen);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Radio No %x\n", rfpath);
	rtstatus = true;
	switch (rfpath) {
	case RF90_PATH_A:
		for (i = 0; i < radioa_arraylen; i = i + 2) {
			v1 = radioa_array_table[i];
			v2 = radioa_array_table[i+1];
			if (v1 < 0xcdcdcdcd)
				_rtl8821ae_config_rf_radio_a(hw, v1, v2);
			else{/*This line is the start line of branch.*/
				if (!_rtl8821ae_check_condition(hw, v1)) {
					/*Discard the following (offset, data) pairs*/
					READ_NEXT_RF_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
						v2 != 0xCDEF &&
						v2 != 0xCDCD && i < radioa_arraylen - 2)
						READ_NEXT_RF_PAIR(v1, v2, i);

					i -= 2; /* prevent from for-loop += 2*/
				} else {/*Configure matched pairs and skip to end of if-else.*/
					READ_NEXT_RF_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD && i < radioa_arraylen - 2) {
						_rtl8821ae_config_rf_radio_a(hw, v1, v2);
						READ_NEXT_RF_PAIR(v1, v2, i);
					}

					while (v2 != 0xDEAD && i < radioa_arraylen - 2)
						READ_NEXT_RF_PAIR(v1, v2, i);
				}
			}
		}
		break;

	case RF90_PATH_B:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not process\n");
		break;
	case RF90_PATH_C:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not process\n");
		break;
	case RF90_PATH_D:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not process\n");
		break;
	}
	return true;
}

void rtl8821ae_phy_get_hw_reg_originalvalue(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	rtlphy->default_initialgain[0] =
	    (u8)rtl_get_bbreg(hw, ROFDM0_XAAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[1] =
	    (u8)rtl_get_bbreg(hw, ROFDM0_XBAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[2] =
	    (u8)rtl_get_bbreg(hw, ROFDM0_XCAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[3] =
	    (u8)rtl_get_bbreg(hw, ROFDM0_XDAGCCORE1, MASKBYTE0);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 "Default initial gain (c50=0x%x, c58=0x%x, c60=0x%x, c68=0x%x\n",
		  rtlphy->default_initialgain[0],
		  rtlphy->default_initialgain[1],
		  rtlphy->default_initialgain[2],
		  rtlphy->default_initialgain[3]);

	rtlphy->framesync = (u8)rtl_get_bbreg(hw,
					       ROFDM0_RXDETECTOR3, MASKBYTE0);
	rtlphy->framesync_c34 = rtl_get_bbreg(hw,
					      ROFDM0_RXDETECTOR2, MASKDWORD);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 "Default framesync (0x%x) = 0x%x\n",
		  ROFDM0_RXDETECTOR3, rtlphy->framesync);
}

static void phy_init_bb_rf_register_definition(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfs = RFPGA0_XAB_RFINTERFACESW;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfs = RFPGA0_XAB_RFINTERFACESW;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfo = RFPGA0_XA_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfo = RFPGA0_XB_RFINTERFACEOE;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfe = RFPGA0_XA_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfe = RFPGA0_XB_RFINTERFACEOE;

	rtlphy->phyreg_def[RF90_PATH_A].rf3wire_offset = RA_LSSIWRITE_8821A;
	rtlphy->phyreg_def[RF90_PATH_B].rf3wire_offset = RB_LSSIWRITE_8821A;

	rtlphy->phyreg_def[RF90_PATH_A].rfhssi_para2 = RHSSIREAD_8821AE;
	rtlphy->phyreg_def[RF90_PATH_B].rfhssi_para2 = RHSSIREAD_8821AE;

	rtlphy->phyreg_def[RF90_PATH_A].rf_rb = RA_SIREAD_8821A;
	rtlphy->phyreg_def[RF90_PATH_B].rf_rb = RB_SIREAD_8821A;

	rtlphy->phyreg_def[RF90_PATH_A].rf_rbpi = RA_PIREAD_8821A;
	rtlphy->phyreg_def[RF90_PATH_B].rf_rbpi = RB_PIREAD_8821A;
}

void rtl8821ae_phy_get_txpower_level(struct ieee80211_hw *hw, long *powerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 txpwr_level;
	long txpwr_dbm;

	txpwr_level = rtlphy->cur_cck_txpwridx;
	txpwr_dbm = _rtl8821ae_phy_txpwr_idx_to_dbm(hw,
						 WIRELESS_MODE_B, txpwr_level);
	txpwr_level = rtlphy->cur_ofdm24g_txpwridx;
	if (_rtl8821ae_phy_txpwr_idx_to_dbm(hw,
					 WIRELESS_MODE_G,
					 txpwr_level) > txpwr_dbm)
		txpwr_dbm =
		    _rtl8821ae_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_G,
						 txpwr_level);
	txpwr_level = rtlphy->cur_ofdm24g_txpwridx;
	if (_rtl8821ae_phy_txpwr_idx_to_dbm(hw,
					 WIRELESS_MODE_N_24G,
					 txpwr_level) > txpwr_dbm)
		txpwr_dbm =
		    _rtl8821ae_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_N_24G,
						 txpwr_level);
	*powerlevel = txpwr_dbm;
}

static bool _rtl8821ae_phy_get_chnl_index(u8 channel, u8 *chnl_index)
{
	u8 channel_5g[CHANNEL_MAX_NUMBER_5G] = {
		36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62,
		64, 100, 102, 104, 106, 108, 110, 112, 114, 116, 118,
		120, 122, 124, 126, 128, 130, 132, 134, 136, 138, 140,
		142, 144, 149, 151, 153, 155, 157, 159, 161, 163, 165,
		167, 168, 169, 171, 173, 175, 177
	};
	u8 i = 0;
	bool in_24g = true;

	if (channel <= 14) {
		in_24g = true;
		*chnl_index = channel - 1;
	} else {
		in_24g = false;

		for (i = 0; i < CHANNEL_MAX_NUMBER_5G; ++i) {
			if (channel_5g[i] == channel) {
				*chnl_index = i;
				return in_24g;
			}
		}
	}
	return in_24g;
}

static char _rtl8821ae_phy_get_ratesection_intxpower_byrate(u8 path, u8 rate)
{
	char rate_section = 0;
	switch (rate) {
	case DESC_RATE1M:
	case DESC_RATE2M:
	case DESC_RATE5_5M:
	case DESC_RATE11M:
		rate_section = 0;
		break;
	case DESC_RATE6M:
	case DESC_RATE9M:
	case DESC_RATE12M:
	case DESC_RATE18M:
		rate_section = 1;
		break;
	case DESC_RATE24M:
	case DESC_RATE36M:
	case DESC_RATE48M:
	case DESC_RATE54M:
		rate_section = 2;
		break;
	case DESC_RATEMCS0:
	case DESC_RATEMCS1:
	case DESC_RATEMCS2:
	case DESC_RATEMCS3:
		rate_section = 3;
		break;
	case DESC_RATEMCS4:
	case DESC_RATEMCS5:
	case DESC_RATEMCS6:
	case DESC_RATEMCS7:
		rate_section = 4;
		break;
	case DESC_RATEMCS8:
	case DESC_RATEMCS9:
	case DESC_RATEMCS10:
	case DESC_RATEMCS11:
		rate_section = 5;
		break;
	case DESC_RATEMCS12:
	case DESC_RATEMCS13:
	case DESC_RATEMCS14:
	case DESC_RATEMCS15:
		rate_section = 6;
		break;
	case DESC_RATEVHT1SS_MCS0:
	case DESC_RATEVHT1SS_MCS1:
	case DESC_RATEVHT1SS_MCS2:
	case DESC_RATEVHT1SS_MCS3:
		rate_section = 7;
		break;
	case DESC_RATEVHT1SS_MCS4:
	case DESC_RATEVHT1SS_MCS5:
	case DESC_RATEVHT1SS_MCS6:
	case DESC_RATEVHT1SS_MCS7:
		rate_section = 8;
		break;
	case DESC_RATEVHT1SS_MCS8:
	case DESC_RATEVHT1SS_MCS9:
	case DESC_RATEVHT2SS_MCS0:
	case DESC_RATEVHT2SS_MCS1:
		rate_section = 9;
		break;
	case DESC_RATEVHT2SS_MCS2:
	case DESC_RATEVHT2SS_MCS3:
	case DESC_RATEVHT2SS_MCS4:
	case DESC_RATEVHT2SS_MCS5:
		rate_section = 10;
		break;
	case DESC_RATEVHT2SS_MCS6:
	case DESC_RATEVHT2SS_MCS7:
	case DESC_RATEVHT2SS_MCS8:
	case DESC_RATEVHT2SS_MCS9:
		rate_section = 11;
		break;
	default:
		RT_ASSERT(true, "Rate_Section is Illegal\n");
		break;
	}

	return rate_section;
}

static char _rtl8812ae_phy_get_world_wide_limit(char  *limit_table)
{
	char min = limit_table[0];
	u8 i = 0;

	for (i = 0; i < MAX_REGULATION_NUM; ++i) {
		if (limit_table[i] < min)
			min = limit_table[i];
	}
	return min;
}

static char _rtl8812ae_phy_get_txpower_limit(struct ieee80211_hw *hw,
					     u8 band,
					     enum ht_channel_width bandwidth,
					     enum radio_path rf_path,
					     u8 rate, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtlpriv);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	short band_temp = -1, regulation = -1, bandwidth_temp = -1,
		 rate_section = -1, channel_temp = -1;
	u16 bd, regu, bdwidth, sec, chnl;
	char power_limit = MAX_POWER_INDEX;

	if (rtlefuse->eeprom_regulatory == 2)
		return MAX_POWER_INDEX;

	regulation = TXPWR_LMT_WW;

	if (band == BAND_ON_2_4G)
		band_temp = 0;
	else if (band == BAND_ON_5G)
		band_temp = 1;

	if (bandwidth == HT_CHANNEL_WIDTH_20)
		bandwidth_temp = 0;
	else if (bandwidth == HT_CHANNEL_WIDTH_20_40)
		bandwidth_temp = 1;
	else if (bandwidth == HT_CHANNEL_WIDTH_80)
		bandwidth_temp = 2;

	switch (rate) {
	case DESC_RATE1M:
	case DESC_RATE2M:
	case DESC_RATE5_5M:
	case DESC_RATE11M:
		rate_section = 0;
		break;
	case DESC_RATE6M:
	case DESC_RATE9M:
	case DESC_RATE12M:
	case DESC_RATE18M:
	case DESC_RATE24M:
	case DESC_RATE36M:
	case DESC_RATE48M:
	case DESC_RATE54M:
		rate_section = 1;
		break;
	case DESC_RATEMCS0:
	case DESC_RATEMCS1:
	case DESC_RATEMCS2:
	case DESC_RATEMCS3:
	case DESC_RATEMCS4:
	case DESC_RATEMCS5:
	case DESC_RATEMCS6:
	case DESC_RATEMCS7:
		rate_section = 2;
		break;
	case DESC_RATEMCS8:
	case DESC_RATEMCS9:
	case DESC_RATEMCS10:
	case DESC_RATEMCS11:
	case DESC_RATEMCS12:
	case DESC_RATEMCS13:
	case DESC_RATEMCS14:
	case DESC_RATEMCS15:
		rate_section = 3;
		break;
	case DESC_RATEVHT1SS_MCS0:
	case DESC_RATEVHT1SS_MCS1:
	case DESC_RATEVHT1SS_MCS2:
	case DESC_RATEVHT1SS_MCS3:
	case DESC_RATEVHT1SS_MCS4:
	case DESC_RATEVHT1SS_MCS5:
	case DESC_RATEVHT1SS_MCS6:
	case DESC_RATEVHT1SS_MCS7:
	case DESC_RATEVHT1SS_MCS8:
	case DESC_RATEVHT1SS_MCS9:
		rate_section = 4;
		break;
	case DESC_RATEVHT2SS_MCS0:
	case DESC_RATEVHT2SS_MCS1:
	case DESC_RATEVHT2SS_MCS2:
	case DESC_RATEVHT2SS_MCS3:
	case DESC_RATEVHT2SS_MCS4:
	case DESC_RATEVHT2SS_MCS5:
	case DESC_RATEVHT2SS_MCS6:
	case DESC_RATEVHT2SS_MCS7:
	case DESC_RATEVHT2SS_MCS8:
	case DESC_RATEVHT2SS_MCS9:
		rate_section = 5;
		break;
	default:
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			"Wrong rate 0x%x\n", rate);
		break;
	}

	if (band_temp == BAND_ON_5G  && rate_section == 0)
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "Wrong rate 0x%x: No CCK in 5G Band\n", rate);

	/*workaround for wrong index combination to obtain tx power limit,
	  OFDM only exists in BW 20M*/
	if (rate_section == 1)
		bandwidth_temp = 0;

	/*workaround for wrong index combination to obtain tx power limit,
	 *HT on 80M will reference to HT on 40M
	 */
	if ((rate_section == 2 || rate_section == 3) && band == BAND_ON_5G &&
	    bandwidth_temp == 2)
		bandwidth_temp = 1;

	if (band == BAND_ON_2_4G)
		channel_temp = _rtl8812ae_phy_get_chnl_idx_of_txpwr_lmt(hw,
		BAND_ON_2_4G, channel);
	else if (band == BAND_ON_5G)
		channel_temp = _rtl8812ae_phy_get_chnl_idx_of_txpwr_lmt(hw,
		BAND_ON_5G, channel);
	else if (band == BAND_ON_BOTH)
		;/* BAND_ON_BOTH don't care temporarily */

	if (band_temp == -1 || regulation == -1 || bandwidth_temp == -1 ||
		rate_section == -1 || channel_temp == -1) {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "Wrong index value to access power limit table [band %d][regulation %d][bandwidth %d][rf_path %d][rate_section %d][chnl %d]\n",
			 band_temp, regulation, bandwidth_temp, rf_path,
			 rate_section, channel_temp);
		return MAX_POWER_INDEX;
	}

	bd = band_temp;
	regu = regulation;
	bdwidth = bandwidth_temp;
	sec = rate_section;
	chnl = channel_temp;

	if (band == BAND_ON_2_4G) {
		char limits[10] = {0};
		u8 i;

		for (i = 0; i < 4; ++i)
			limits[i] = rtlphy->txpwr_limit_2_4g[i][bdwidth]
			[sec][chnl][rf_path];

		power_limit = (regulation == TXPWR_LMT_WW) ?
			_rtl8812ae_phy_get_world_wide_limit(limits) :
			rtlphy->txpwr_limit_2_4g[regu][bdwidth]
					[sec][chnl][rf_path];
	} else if (band == BAND_ON_5G) {
		char limits[10] = {0};
		u8 i;

		for (i = 0; i < MAX_REGULATION_NUM; ++i)
			limits[i] = rtlphy->txpwr_limit_5g[i][bdwidth]
			[sec][chnl][rf_path];

		power_limit = (regulation == TXPWR_LMT_WW) ?
			_rtl8812ae_phy_get_world_wide_limit(limits) :
			rtlphy->txpwr_limit_5g[regu][chnl]
			[sec][chnl][rf_path];
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "No power limit table of the specified band\n");
	}
	return power_limit;
}

static char _rtl8821ae_phy_get_txpower_by_rate(struct ieee80211_hw *hw,
					u8 band, u8 path, u8 rate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 shift = 0, rate_section, tx_num;
	char tx_pwr_diff = 0;
	char limit = 0;

	rate_section = _rtl8821ae_phy_get_ratesection_intxpower_byrate(path, rate);
	tx_num = RF_TX_NUM_NONIMPLEMENT;

	if (tx_num == RF_TX_NUM_NONIMPLEMENT) {
		if ((rate >= DESC_RATEMCS8 && rate <= DESC_RATEMCS15) ||
			(rate >= DESC_RATEVHT2SS_MCS2 && rate <= DESC_RATEVHT2SS_MCS9))
			tx_num = RF_2TX;
		else
			tx_num = RF_1TX;
	}

	switch (rate) {
	case DESC_RATE1M:
	case DESC_RATE6M:
	case DESC_RATE24M:
	case DESC_RATEMCS0:
	case DESC_RATEMCS4:
	case DESC_RATEMCS8:
	case DESC_RATEMCS12:
	case DESC_RATEVHT1SS_MCS0:
	case DESC_RATEVHT1SS_MCS4:
	case DESC_RATEVHT1SS_MCS8:
	case DESC_RATEVHT2SS_MCS2:
	case DESC_RATEVHT2SS_MCS6:
		shift = 0;
		break;
	case DESC_RATE2M:
	case DESC_RATE9M:
	case DESC_RATE36M:
	case DESC_RATEMCS1:
	case DESC_RATEMCS5:
	case DESC_RATEMCS9:
	case DESC_RATEMCS13:
	case DESC_RATEVHT1SS_MCS1:
	case DESC_RATEVHT1SS_MCS5:
	case DESC_RATEVHT1SS_MCS9:
	case DESC_RATEVHT2SS_MCS3:
	case DESC_RATEVHT2SS_MCS7:
		shift = 8;
		break;
	case DESC_RATE5_5M:
	case DESC_RATE12M:
	case DESC_RATE48M:
	case DESC_RATEMCS2:
	case DESC_RATEMCS6:
	case DESC_RATEMCS10:
	case DESC_RATEMCS14:
	case DESC_RATEVHT1SS_MCS2:
	case DESC_RATEVHT1SS_MCS6:
	case DESC_RATEVHT2SS_MCS0:
	case DESC_RATEVHT2SS_MCS4:
	case DESC_RATEVHT2SS_MCS8:
		shift = 16;
		break;
	case DESC_RATE11M:
	case DESC_RATE18M:
	case DESC_RATE54M:
	case DESC_RATEMCS3:
	case DESC_RATEMCS7:
	case DESC_RATEMCS11:
	case DESC_RATEMCS15:
	case DESC_RATEVHT1SS_MCS3:
	case DESC_RATEVHT1SS_MCS7:
	case DESC_RATEVHT2SS_MCS1:
	case DESC_RATEVHT2SS_MCS5:
	case DESC_RATEVHT2SS_MCS9:
		shift = 24;
		break;
	default:
		RT_ASSERT(true, "Rate_Section is Illegal\n");
		break;
	}

	tx_pwr_diff = (u8)(rtlphy->tx_power_by_rate_offset[band][path]
		[tx_num][rate_section] >> shift) & 0xff;

	/* RegEnableTxPowerLimit == 1 for 8812a & 8821a */
	if (rtlpriv->efuse.eeprom_regulatory != 2) {
		limit = _rtl8812ae_phy_get_txpower_limit(hw, band,
			rtlphy->current_chan_bw, path, rate,
			rtlphy->current_channel);

		if (rate == DESC_RATEVHT1SS_MCS8 || rate == DESC_RATEVHT1SS_MCS9  ||
			 rate == DESC_RATEVHT2SS_MCS8 || rate == DESC_RATEVHT2SS_MCS9) {
			if (limit < 0) {
				if (tx_pwr_diff < (-limit))
					tx_pwr_diff = -limit;
			}
		} else {
			if (limit < 0)
				tx_pwr_diff = limit;
			else
				tx_pwr_diff = tx_pwr_diff > limit ? limit : tx_pwr_diff;
		}
		RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"Maximum power by rate %d, final power by rate %d\n",
			limit, tx_pwr_diff);
	}

	return	tx_pwr_diff;
}

static u8 _rtl8821ae_get_txpower_index(struct ieee80211_hw *hw, u8 path,
					u8 rate, u8 bandwidth, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 index = (channel - 1);
	u8 txpower = 0;
	bool in_24g = false;
	char powerdiff_byrate = 0;

	if (((rtlhal->current_bandtype == BAND_ON_2_4G) &&
	    (channel > 14 || channel < 1)) ||
	    ((rtlhal->current_bandtype == BAND_ON_5G) && (channel <= 14))) {
		index = 0;
		RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"Illegal channel!!\n");
	}

	in_24g = _rtl8821ae_phy_get_chnl_index(channel, &index);
	if (in_24g) {
		if (RTL8821AE_RX_HAL_IS_CCK_RATE(rate))
			txpower = rtlefuse->txpwrlevel_cck[path][index];
		else if (DESC_RATE6M <= rate)
			txpower = rtlefuse->txpwrlevel_ht40_1s[path][index];
		else
			RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD, "invalid rate\n");

		if (DESC_RATE6M <= rate && rate <= DESC_RATE54M &&
		    !RTL8821AE_RX_HAL_IS_CCK_RATE(rate))
			txpower += rtlefuse->txpwr_legacyhtdiff[path][TX_1S];

		if (bandwidth == HT_CHANNEL_WIDTH_20) {
			if ((DESC_RATEMCS0 <= rate && rate <= DESC_RATEMCS15) ||
				(DESC_RATEVHT1SS_MCS0 <= rate && rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_ht20diff[path][TX_1S];
			if ((DESC_RATEMCS8 <= rate && rate <= DESC_RATEMCS15) ||
				(DESC_RATEVHT2SS_MCS0 <= rate && rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_ht20diff[path][TX_2S];
		} else if (bandwidth == HT_CHANNEL_WIDTH_20_40) {
			if ((DESC_RATEMCS0 <= rate && rate <= DESC_RATEMCS15) ||
				(DESC_RATEVHT1SS_MCS0 <= rate && rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_ht40diff[path][TX_1S];
			if ((DESC_RATEMCS8 <= rate && rate <= DESC_RATEMCS15) ||
				(DESC_RATEVHT2SS_MCS0 <= rate && rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_ht40diff[path][TX_2S];
		} else if (bandwidth == HT_CHANNEL_WIDTH_80) {
			if ((DESC_RATEMCS0 <= rate && rate <= DESC_RATEMCS15) ||
			    (DESC_RATEVHT1SS_MCS0 <= rate &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_ht40diff[path][TX_1S];
			if ((DESC_RATEMCS8 <= rate && rate <= DESC_RATEMCS15) ||
			    (DESC_RATEVHT2SS_MCS0 <= rate &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_ht40diff[path][TX_2S];
		}
	} else {
		if (DESC_RATE6M <= rate)
			txpower = rtlefuse->txpwr_5g_bw40base[path][index];
		else
			RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_WARNING,
				 "INVALID Rate.\n");

		if (DESC_RATE6M <= rate && rate <= DESC_RATE54M &&
		    !RTL8821AE_RX_HAL_IS_CCK_RATE(rate))
			txpower += rtlefuse->txpwr_5g_ofdmdiff[path][TX_1S];

		if (bandwidth == HT_CHANNEL_WIDTH_20) {
			if ((DESC_RATEMCS0 <= rate && rate <= DESC_RATEMCS15) ||
			    (DESC_RATEVHT1SS_MCS0 <= rate &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_5g_bw20diff[path][TX_1S];
			if ((DESC_RATEMCS8 <= rate && rate <= DESC_RATEMCS15) ||
			    (DESC_RATEVHT2SS_MCS0 <= rate &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_5g_bw20diff[path][TX_2S];
		} else if (bandwidth == HT_CHANNEL_WIDTH_20_40) {
			if ((DESC_RATEMCS0 <= rate && rate <= DESC_RATEMCS15) ||
			    (DESC_RATEVHT1SS_MCS0 <= rate &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_5g_bw40diff[path][TX_1S];
			if ((DESC_RATEMCS8 <= rate && rate <= DESC_RATEMCS15) ||
			    (DESC_RATEVHT2SS_MCS0 <= rate &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_5g_bw40diff[path][TX_2S];
		} else if (bandwidth == HT_CHANNEL_WIDTH_80) {
			u8 channel_5g_80m[CHANNEL_MAX_NUMBER_5G_80M] = {
				42, 58, 106, 122, 138, 155, 171
			};
			u8 i;

			for (i = 0; i < sizeof(channel_5g_80m) / sizeof(u8); ++i)
				if (channel_5g_80m[i] == channel)
					index = i;

			if ((DESC_RATEMCS0 <= rate && rate <= DESC_RATEMCS15) ||
			    (DESC_RATEVHT1SS_MCS0 <= rate &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower = rtlefuse->txpwr_5g_bw80base[path][index]
					+ rtlefuse->txpwr_5g_bw80diff[path][TX_1S];
			if ((DESC_RATEMCS8 <= rate && rate <= DESC_RATEMCS15) ||
			    (DESC_RATEVHT2SS_MCS0 <= rate &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower = rtlefuse->txpwr_5g_bw80base[path][index]
					+ rtlefuse->txpwr_5g_bw80diff[path][TX_1S]
					+ rtlefuse->txpwr_5g_bw80diff[path][TX_2S];
		    }
	}
	if (rtlefuse->eeprom_regulatory != 2)
		powerdiff_byrate =
		  _rtl8821ae_phy_get_txpower_by_rate(hw, (u8)(!in_24g),
						     path, rate);

	if (rate == DESC_RATEVHT1SS_MCS8 || rate == DESC_RATEVHT1SS_MCS9 ||
	    rate == DESC_RATEVHT2SS_MCS8 || rate == DESC_RATEVHT2SS_MCS9)
		txpower -= powerdiff_byrate;
	else
		txpower += powerdiff_byrate;

	if (rate > DESC_RATE11M)
		txpower += rtlpriv->dm.remnant_ofdm_swing_idx[path];
	else
		txpower += rtlpriv->dm.remnant_cck_idx;

	if (txpower > MAX_POWER_INDEX)
		txpower = MAX_POWER_INDEX;

	return txpower;
}

static void _rtl8821ae_phy_set_txpower_index(struct ieee80211_hw *hw,
					     u8 power_index, u8 path, u8 rate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (path == RF90_PATH_A) {
		switch (rate) {
		case DESC_RATE1M:
			rtl_set_bbreg(hw, RTXAGC_A_CCK11_CCK1,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATE2M:
			rtl_set_bbreg(hw, RTXAGC_A_CCK11_CCK1,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATE5_5M:
			rtl_set_bbreg(hw, RTXAGC_A_CCK11_CCK1,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATE11M:
			rtl_set_bbreg(hw, RTXAGC_A_CCK11_CCK1,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATE6M:
			rtl_set_bbreg(hw, RTXAGC_A_OFDM18_OFDM6,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATE9M:
			rtl_set_bbreg(hw, RTXAGC_A_OFDM18_OFDM6,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATE12M:
			rtl_set_bbreg(hw, RTXAGC_A_OFDM18_OFDM6,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATE18M:
			rtl_set_bbreg(hw, RTXAGC_A_OFDM18_OFDM6,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATE24M:
			rtl_set_bbreg(hw, RTXAGC_A_OFDM54_OFDM24,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATE36M:
			rtl_set_bbreg(hw, RTXAGC_A_OFDM54_OFDM24,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATE48M:
			rtl_set_bbreg(hw, RTXAGC_A_OFDM54_OFDM24,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATE54M:
			rtl_set_bbreg(hw, RTXAGC_A_OFDM54_OFDM24,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEMCS0:
			rtl_set_bbreg(hw, RTXAGC_A_MCS03_MCS00,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEMCS1:
			rtl_set_bbreg(hw, RTXAGC_A_MCS03_MCS00,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEMCS2:
			rtl_set_bbreg(hw, RTXAGC_A_MCS03_MCS00,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEMCS3:
			rtl_set_bbreg(hw, RTXAGC_A_MCS03_MCS00,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEMCS4:
			rtl_set_bbreg(hw, RTXAGC_A_MCS07_MCS04,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEMCS5:
			rtl_set_bbreg(hw, RTXAGC_A_MCS07_MCS04,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEMCS6:
			rtl_set_bbreg(hw, RTXAGC_A_MCS07_MCS04,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEMCS7:
			rtl_set_bbreg(hw, RTXAGC_A_MCS07_MCS04,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEMCS8:
			rtl_set_bbreg(hw, RTXAGC_A_MCS11_MCS08,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEMCS9:
			rtl_set_bbreg(hw, RTXAGC_A_MCS11_MCS08,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEMCS10:
			rtl_set_bbreg(hw, RTXAGC_A_MCS11_MCS08,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEMCS11:
			rtl_set_bbreg(hw, RTXAGC_A_MCS11_MCS08,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEMCS12:
			rtl_set_bbreg(hw, RTXAGC_A_MCS15_MCS12,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEMCS13:
			rtl_set_bbreg(hw, RTXAGC_A_MCS15_MCS12,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEMCS14:
			rtl_set_bbreg(hw, RTXAGC_A_MCS15_MCS12,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEMCS15:
			rtl_set_bbreg(hw, RTXAGC_A_MCS15_MCS12,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT1SS_MCS0:
			rtl_set_bbreg(hw, RTXAGC_A_NSS1INDEX3_NSS1INDEX0,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT1SS_MCS1:
			rtl_set_bbreg(hw, RTXAGC_A_NSS1INDEX3_NSS1INDEX0,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT1SS_MCS2:
			rtl_set_bbreg(hw, RTXAGC_A_NSS1INDEX3_NSS1INDEX0,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT1SS_MCS3:
			rtl_set_bbreg(hw, RTXAGC_A_NSS1INDEX3_NSS1INDEX0,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT1SS_MCS4:
			rtl_set_bbreg(hw, RTXAGC_A_NSS1INDEX7_NSS1INDEX4,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT1SS_MCS5:
			rtl_set_bbreg(hw, RTXAGC_A_NSS1INDEX7_NSS1INDEX4,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT1SS_MCS6:
			rtl_set_bbreg(hw, RTXAGC_A_NSS1INDEX7_NSS1INDEX4,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT1SS_MCS7:
			rtl_set_bbreg(hw, RTXAGC_A_NSS1INDEX7_NSS1INDEX4,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT1SS_MCS8:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX1_NSS1INDEX8,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT1SS_MCS9:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX1_NSS1INDEX8,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT2SS_MCS0:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX1_NSS1INDEX8,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT2SS_MCS1:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX1_NSS1INDEX8,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT2SS_MCS2:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX5_NSS2INDEX2,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT2SS_MCS3:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX5_NSS2INDEX2,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT2SS_MCS4:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX5_NSS2INDEX2,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT2SS_MCS5:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX5_NSS2INDEX2,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT2SS_MCS6:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX9_NSS2INDEX6,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT2SS_MCS7:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX9_NSS2INDEX6,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT2SS_MCS8:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX9_NSS2INDEX6,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT2SS_MCS9:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX9_NSS2INDEX6,
				      MASKBYTE3, power_index);
			break;
		default:
			RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
				"Invalid Rate!!\n");
			break;
		}
	} else if (path == RF90_PATH_B) {
		switch (rate) {
		case DESC_RATE1M:
			rtl_set_bbreg(hw, RTXAGC_B_CCK11_CCK1,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATE2M:
			rtl_set_bbreg(hw, RTXAGC_B_CCK11_CCK1,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATE5_5M:
			rtl_set_bbreg(hw, RTXAGC_B_CCK11_CCK1,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATE11M:
			rtl_set_bbreg(hw, RTXAGC_B_CCK11_CCK1,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATE6M:
			rtl_set_bbreg(hw, RTXAGC_B_OFDM18_OFDM6,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATE9M:
			rtl_set_bbreg(hw, RTXAGC_B_OFDM18_OFDM6,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATE12M:
			rtl_set_bbreg(hw, RTXAGC_B_OFDM18_OFDM6,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATE18M:
			rtl_set_bbreg(hw, RTXAGC_B_OFDM18_OFDM6,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATE24M:
			rtl_set_bbreg(hw, RTXAGC_B_OFDM54_OFDM24,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATE36M:
			rtl_set_bbreg(hw, RTXAGC_B_OFDM54_OFDM24,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATE48M:
			rtl_set_bbreg(hw, RTXAGC_B_OFDM54_OFDM24,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATE54M:
			rtl_set_bbreg(hw, RTXAGC_B_OFDM54_OFDM24,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEMCS0:
			rtl_set_bbreg(hw, RTXAGC_B_MCS03_MCS00,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEMCS1:
			rtl_set_bbreg(hw, RTXAGC_B_MCS03_MCS00,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEMCS2:
			rtl_set_bbreg(hw, RTXAGC_B_MCS03_MCS00,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEMCS3:
			rtl_set_bbreg(hw, RTXAGC_B_MCS03_MCS00,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEMCS4:
			rtl_set_bbreg(hw, RTXAGC_B_MCS07_MCS04,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEMCS5:
			rtl_set_bbreg(hw, RTXAGC_B_MCS07_MCS04,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEMCS6:
			rtl_set_bbreg(hw, RTXAGC_B_MCS07_MCS04,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEMCS7:
			rtl_set_bbreg(hw, RTXAGC_B_MCS07_MCS04,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEMCS8:
			rtl_set_bbreg(hw, RTXAGC_B_MCS11_MCS08,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEMCS9:
			rtl_set_bbreg(hw, RTXAGC_B_MCS11_MCS08,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEMCS10:
			rtl_set_bbreg(hw, RTXAGC_B_MCS11_MCS08,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEMCS11:
			rtl_set_bbreg(hw, RTXAGC_B_MCS11_MCS08,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEMCS12:
			rtl_set_bbreg(hw, RTXAGC_B_MCS15_MCS12,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEMCS13:
			rtl_set_bbreg(hw, RTXAGC_B_MCS15_MCS12,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEMCS14:
			rtl_set_bbreg(hw, RTXAGC_B_MCS15_MCS12,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEMCS15:
			rtl_set_bbreg(hw, RTXAGC_B_MCS15_MCS12,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT1SS_MCS0:
			rtl_set_bbreg(hw, RTXAGC_B_NSS1INDEX3_NSS1INDEX0,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT1SS_MCS1:
			rtl_set_bbreg(hw, RTXAGC_B_NSS1INDEX3_NSS1INDEX0,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT1SS_MCS2:
			rtl_set_bbreg(hw, RTXAGC_B_NSS1INDEX3_NSS1INDEX0,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT1SS_MCS3:
			rtl_set_bbreg(hw, RTXAGC_B_NSS1INDEX3_NSS1INDEX0,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT1SS_MCS4:
			rtl_set_bbreg(hw, RTXAGC_B_NSS1INDEX7_NSS1INDEX4,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT1SS_MCS5:
			rtl_set_bbreg(hw, RTXAGC_B_NSS1INDEX7_NSS1INDEX4,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT1SS_MCS6:
			rtl_set_bbreg(hw, RTXAGC_B_NSS1INDEX7_NSS1INDEX4,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT1SS_MCS7:
			rtl_set_bbreg(hw, RTXAGC_B_NSS1INDEX7_NSS1INDEX4,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT1SS_MCS8:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX1_NSS1INDEX8,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT1SS_MCS9:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX1_NSS1INDEX8,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT2SS_MCS0:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX1_NSS1INDEX8,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT2SS_MCS1:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX1_NSS1INDEX8,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT2SS_MCS2:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX5_NSS2INDEX2,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT2SS_MCS3:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX5_NSS2INDEX2,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT2SS_MCS4:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX5_NSS2INDEX2,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT2SS_MCS5:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX5_NSS2INDEX2,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT2SS_MCS6:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX9_NSS2INDEX6,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT2SS_MCS7:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX9_NSS2INDEX6,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT2SS_MCS8:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX9_NSS2INDEX6,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT2SS_MCS9:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX9_NSS2INDEX6,
				      MASKBYTE3, power_index);
			break;
		default:
			RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
				 "Invalid Rate!!\n");
			break;
		}
	} else {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "Invalid RFPath!!\n");
	}
}

static void _rtl8821ae_phy_set_txpower_level_by_path(struct ieee80211_hw *hw,
						     u8 *array, u8 path,
						     u8 channel, u8 size)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 i;
	u8 power_index;

	for (i = 0; i < size; i++) {
		power_index =
		  _rtl8821ae_get_txpower_index(hw, path, array[i],
					       rtlphy->current_chan_bw,
					       channel);
		_rtl8821ae_phy_set_txpower_index(hw, power_index, path,
						 array[i]);
	}
}

static void _rtl8821ae_phy_txpower_training_by_path(struct ieee80211_hw *hw,
						    u8 bw, u8 channel, u8 path)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	u8 i;
	u32 power_level, data, offset;

	if (path >= rtlphy->num_total_rfpath)
		return;

	data = 0;
	if (path == RF90_PATH_A) {
		power_level =
			_rtl8821ae_get_txpower_index(hw, RF90_PATH_A,
			DESC_RATEMCS7, bw, channel);
		offset =  RA_TXPWRTRAING;
	} else {
		power_level =
			_rtl8821ae_get_txpower_index(hw, RF90_PATH_B,
			DESC_RATEMCS7, bw, channel);
		offset =  RB_TXPWRTRAING;
	}

	for (i = 0; i < 3; i++) {
		if (i == 0)
			power_level = power_level - 10;
		else if (i == 1)
			power_level = power_level - 8;
		else
			power_level = power_level - 6;

		data |= (((power_level > 2) ? (power_level) : 2) << (i * 8));
	}
	rtl_set_bbreg(hw, offset, 0xffffff, data);
}

void rtl8821ae_phy_set_txpower_level_by_path(struct ieee80211_hw *hw,
					     u8 channel, u8 path)
{
	/* struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw)); */
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 cck_rates[]  = {DESC_RATE1M, DESC_RATE2M, DESC_RATE5_5M,
			      DESC_RATE11M};
	u8 sizes_of_cck_retes = 4;
	u8 ofdm_rates[]  = {DESC_RATE6M, DESC_RATE9M, DESC_RATE12M,
				DESC_RATE18M, DESC_RATE24M, DESC_RATE36M,
				DESC_RATE48M, DESC_RATE54M};
	u8 sizes_of_ofdm_retes = 8;
	u8 ht_rates_1t[]  = {DESC_RATEMCS0, DESC_RATEMCS1, DESC_RATEMCS2,
				DESC_RATEMCS3, DESC_RATEMCS4, DESC_RATEMCS5,
				DESC_RATEMCS6, DESC_RATEMCS7};
	u8 sizes_of_ht_retes_1t = 8;
	u8 ht_rates_2t[]  = {DESC_RATEMCS8, DESC_RATEMCS9,
				DESC_RATEMCS10, DESC_RATEMCS11,
				DESC_RATEMCS12, DESC_RATEMCS13,
				DESC_RATEMCS14, DESC_RATEMCS15};
	u8 sizes_of_ht_retes_2t = 8;
	u8 vht_rates_1t[]  = {DESC_RATEVHT1SS_MCS0, DESC_RATEVHT1SS_MCS1,
				DESC_RATEVHT1SS_MCS2, DESC_RATEVHT1SS_MCS3,
				DESC_RATEVHT1SS_MCS4, DESC_RATEVHT1SS_MCS5,
				DESC_RATEVHT1SS_MCS6, DESC_RATEVHT1SS_MCS7,
			     DESC_RATEVHT1SS_MCS8, DESC_RATEVHT1SS_MCS9};
	u8 vht_rates_2t[]  = {DESC_RATEVHT2SS_MCS0, DESC_RATEVHT2SS_MCS1,
				DESC_RATEVHT2SS_MCS2, DESC_RATEVHT2SS_MCS3,
				DESC_RATEVHT2SS_MCS4, DESC_RATEVHT2SS_MCS5,
				DESC_RATEVHT2SS_MCS6, DESC_RATEVHT2SS_MCS7,
				DESC_RATEVHT2SS_MCS8, DESC_RATEVHT2SS_MCS9};
	u8 sizes_of_vht_retes = 10;

	if (rtlhal->current_bandtype == BAND_ON_2_4G)
		_rtl8821ae_phy_set_txpower_level_by_path(hw, cck_rates, path, channel,
							 sizes_of_cck_retes);

	_rtl8821ae_phy_set_txpower_level_by_path(hw, ofdm_rates, path, channel,
						 sizes_of_ofdm_retes);
	_rtl8821ae_phy_set_txpower_level_by_path(hw, ht_rates_1t, path, channel,
						 sizes_of_ht_retes_1t);
	_rtl8821ae_phy_set_txpower_level_by_path(hw, vht_rates_1t, path, channel,
						 sizes_of_vht_retes);

	if (rtlphy->num_total_rfpath >= 2) {
		_rtl8821ae_phy_set_txpower_level_by_path(hw, ht_rates_2t, path,
							 channel,
							 sizes_of_ht_retes_2t);
		_rtl8821ae_phy_set_txpower_level_by_path(hw, vht_rates_2t, path,
							 channel,
							 sizes_of_vht_retes);
	}

	_rtl8821ae_phy_txpower_training_by_path(hw, rtlphy->current_chan_bw,
						channel, path);
}

/*just in case, write txpower in DW, to reduce time*/
void rtl8821ae_phy_set_txpower_level(struct ieee80211_hw *hw, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 path = 0;

	for (path = RF90_PATH_A; path < rtlphy->num_total_rfpath; ++path)
		rtl8821ae_phy_set_txpower_level_by_path(hw, channel, path);
}

static long _rtl8821ae_phy_txpwr_idx_to_dbm(struct ieee80211_hw *hw,
					    enum wireless_mode wirelessmode,
					    u8 txpwridx)
{
	long offset;
	long pwrout_dbm;

	switch (wirelessmode) {
	case WIRELESS_MODE_B:
		offset = -7;
		break;
	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		offset = -8;
		break;
	default:
		offset = -8;
		break;
	}
	pwrout_dbm = txpwridx / 2 + offset;
	return pwrout_dbm;
}

void rtl8821ae_phy_scan_operation_backup(struct ieee80211_hw *hw, u8 operation)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	enum io_type iotype = IO_CMD_PAUSE_BAND0_DM_BY_SCAN;

	if (!is_hal_stop(rtlhal)) {
		switch (operation) {
		case SCAN_OPT_BACKUP_BAND0:
			iotype = IO_CMD_PAUSE_BAND0_DM_BY_SCAN;
			rtlpriv->cfg->ops->set_hw_reg(hw,
						      HW_VAR_IO_CMD,
						      (u8 *)&iotype);

			break;
		case SCAN_OPT_BACKUP_BAND1:
			iotype = IO_CMD_PAUSE_BAND1_DM_BY_SCAN;
			rtlpriv->cfg->ops->set_hw_reg(hw,
						      HW_VAR_IO_CMD,
						      (u8 *)&iotype);

			break;
		case SCAN_OPT_RESTORE:
			iotype = IO_CMD_RESUME_DM_BY_SCAN;
			rtlpriv->cfg->ops->set_hw_reg(hw,
						      HW_VAR_IO_CMD,
						      (u8 *)&iotype);
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "Unknown Scan Backup operation.\n");
			break;
		}
	}
}

static void _rtl8821ae_phy_set_reg_bw(struct rtl_priv *rtlpriv, u8 bw)
{
	u16 reg_rf_mode_bw, tmp = 0;

	reg_rf_mode_bw = rtl_read_word(rtlpriv, REG_TRXPTCL_CTL);
	switch (bw) {
	case HT_CHANNEL_WIDTH_20:
		rtl_write_word(rtlpriv, REG_TRXPTCL_CTL, reg_rf_mode_bw & 0xFE7F);
		break;
	case HT_CHANNEL_WIDTH_20_40:
		tmp = reg_rf_mode_bw | BIT(7);
		rtl_write_word(rtlpriv, REG_TRXPTCL_CTL, tmp & 0xFEFF);
		break;
	case HT_CHANNEL_WIDTH_80:
		tmp = reg_rf_mode_bw | BIT(8);
		rtl_write_word(rtlpriv, REG_TRXPTCL_CTL, tmp & 0xFF7F);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING, "unknown Bandwidth: 0x%x\n", bw);
		break;
	}
}

static u8 _rtl8821ae_phy_get_secondary_chnl(struct rtl_priv *rtlpriv)
{
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	u8 sc_set_40 = 0, sc_set_20 = 0;

	if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_80) {
		if (mac->cur_80_prime_sc == PRIME_CHNL_OFFSET_LOWER)
			sc_set_40 = VHT_DATA_SC_40_LOWER_OF_80MHZ;
		else if (mac->cur_80_prime_sc == PRIME_CHNL_OFFSET_UPPER)
			sc_set_40 = VHT_DATA_SC_40_UPPER_OF_80MHZ;
		else
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				"SCMapping: Not Correct Primary40MHz Setting\n");

		if ((mac->cur_40_prime_sc == PRIME_CHNL_OFFSET_LOWER) &&
			(mac->cur_80_prime_sc == HAL_PRIME_CHNL_OFFSET_LOWER))
			sc_set_20 = VHT_DATA_SC_20_LOWEST_OF_80MHZ;
		else if ((mac->cur_40_prime_sc == PRIME_CHNL_OFFSET_F_80MHEgBme_sset_bbreg(hf (E0s);

			break;
		ca,
							break;
		ca N1ir4Use if ((mao
		break;
	cdex);
		8 )r_index);
			&
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				"SCMapping:lse if (mac->cur_80_prime_sc == PRIME_CHNL_OFFSEET_LOWER)
			sc_set_40 = VHT_DATA_SC_40_LOWER_OF_80MHt_20 = VHT_DATA_SC_20_LOWEST_OF_80MHZ;
		else  f ((mac->cur_40_prime_sc == PRIME_CHNL_OFFSET_F_80MH= &rtlpri(REG_TRXPTC<< 4)priOWER)
			H_A) {
		power_level =
			_rbwRACE(rcallpath rtlphy->num_total_rfpat5G) {
		char limits[10] = {0};
		u8 i;

		for (i = 0; i < MAX_REGULATION_NUM; ++i)
			limitsub_= BAND_0	limitl1pk_val
		break = VHT_DATA_SC_20_LOWESl *rF_80MHVHT_Delse f N;
			to %sSC_RATEVHTget_txp et_bbreg(hf (E0s);

			break;
		ca,
							breakBAND_  "2E_CH"
		fo et_bbreg(hf (E0s);

			break;
		ca,
							breakXPTCAND_  "4E_CH"
	 "8E_CH"))EVHT1SS_MCS5,
				DESC_R				   TA_SC_20__bbreg(hf (E0s);

			bror (iub_= BAND_;

	reg_rf_mode_bw = rtl_read_wordTRXPTCL_CTLR, DBG_EMEbyte TA_SC_20_0x0483,tsub_= BA)t band_temp =bbreg(hf (E0s);

			broriv->cfg->ops->set_hw_reg(hw,
						break;
		case DRFvel0_0x003003C30_0x00300200MP_ERR, Dbreak;
		case DADC_BUF_CLK,iv, C30)0_0static u8 =bbreg(h_TRRATEVHT1char 2SEET_LR, Dbreak;
		case DL1PEAKTH0_0x03C00000, iv->cf80MHt_20R, Dbreak;
		case DL1PEAKTH0_0x03C00000, 8		case SCAN_OPT_RESTORE:
			iotype = IO_CMD_				break;
		case DRFvel0_0x003003C30_0x00300201MP_ERR, Dbreak;
		case DADC_BUF_CLK,iv, C30)0_0staD_				break;
		case DRFvel0_0x3C,tsub_= BA)t D_				break;
		case DCCAONSEC(path0000000, sub_= BA)t bac u8 =bbreg(h_			837ativ, C2cur_80l1pk_val
		6		"SCMapp{iv = u8 =bbreg(h_TRRATEVHT1char 2SEET_L0l1pk_val
		to re[path]
		[l1pk_val
		8AN;
			fset0x848[25:22th][0x68821a0R, Dbreak;
		case DL1PEAKTH0_0x03C00000, l1pk_val)t bac u8 iub_= BANDtlpriv, COMP_ERR, DBG_EMERG,
		EET_LR, Dbreak;
		case D
			SYSTEM,iv
			SYSTEM,i1v->cf80MHt_20R, Dbreak;
		case D
			SYSTEM,iv
			SYSTEM,i0		case SCAN_8 *)&iotype);
			break;
		defa set0x8ac[21,20,9:6,1,0]=8'b111000108821a0R, Dbreak;
		case DRFvel0_0x003003C30_0x00300202		casset0x8c4[3	 rat18821a0R, Dbreak;
		case DADC_BUF_CLK,iv, C30)0_1MP_ERR, Dbreak;
		case DRFvel0_0x3C,tsub_= BA)t D_				break;
		case DCCAONSEC(path0000000, sub_= BA)t bac u8 =bbreg(h_			837ativ, C2cur_80l1pk_val
		5		"SCMapp{iv = u8 =bbreg(h_TRRATEVHT1char 2SEET_L0l1pk_val
		6o re[path]
		[l1pk_val
		7AN;
			fR, Dbreak;
		case DL1PEAKTH0_0x03C00000, l1pk_val)t bac	break;
		}
	}
}

static void _rtl8821ae_phy_set_reZ;
		else  ft rtl_prbv *rtlpriv%#X 4;
		bbreg(hf (E0s);

			bror (ase DESC_RATErESC_RATE5fixspurnel,
						 sizes_of_ofdm_ret
						 sizes_of_ofdm DESC_RA

	reg_rf_moderf6052Dbreakv *rtlprnel,
						 sizes_of_ofdm_re_CTLR, 			 s		_rbwACE(rinprogrN_5Gmit;
			eak = VHT_DATA_SC_20_LOWESl *rF_80MH->curreT_F_80A) {
		power_level =
			_rbwRACE(h,
							 channel,
							 ses_of_vhtnlhannelON_BOTH)
ATEVch)
ATE5G) {
		char limits[10] = {0};
		u8 i;

		for (i = 0; i < MAX_REGULATION_NUM; ++i)
			linel, path);
}

static long _rtl8821ae_phy_txpwr_itraimpP_BAND1:
			 sizes_of_ofdm_rereak;
	case HT_C		_rbwACE(rinprogrN_5v->phy;
	u8 LR, 			 s		_rbwACE(rinprogrN_5GmiSC_Rer_indemode wirelessmode,
				&& !( =  *rNOT_IO_txpw					 channel,
							bwRACE(rcallpath 	for (GC_B_NSS2INDEX9_NSS2INDEX6,
				hy_set_reg_bw(strlse  fFALSE dDEXer sleep or unloaae_phy_gLR, 			 s		_rbwACE(rinprogrN_5Gmit;
			eSC_RATEMCS7:
	case DESC_RGmiSmpP_Bf_mode_b{
		power_level =
		we, u8 callpath rtlphy->num_total_rfpat5G) {
		char limits[10] = {0};
		u8 i;

		for (i = 0; i < er_index(hw, RF90_PATH_B,
			DESC_RATEMCS7, bw, chor (i = 0; i < 3; i++) {
		if (i \n",
		ND1:
			 sizes_of_ofdm,
	fpath >= 2;uct rt0_PAreak = VHT_DATA_SC_20_LOWESl *rF_80MHVHT_Delse fsN;
			to ofdm,
	y_get_
						 sizes_of_ofdm DESC_ *hw,
e wirelessmode,
			
>phy;
	u8 i;*hw,36NNEL \n",
		&& d\n",
			li48	
>p
	u8 powx494; limit,
	  50NNEL \n",
		&& d\n",
			li64	
>p
	u8 powx453; limit,
	  100NNEL \n",
		&& d\n",
			li116	
>p
	u8 powx452; limit,
	  118NNEL \n",
		
>p
	u8 powx412; limit
>p
	u8 powx96a;		return;

	data = 0RFC_AREA0_0x1ffe0000, 0_PATH__rtl8821ae_phy_set_txpower_level_by_path(hw, ht_rates_2t, pa2t, 0211_hw *hw,36NNEL \n",
		&& d\n",
			li64	
>pp
	u8 powx101		"SCMapping:100NNEL \n",
		&& d\n",
			li140	
>pp
	u8 powx301		"SCMapping:1PTC<_MCS0 <= rate
	u8 powx501		"SCMaprate
	u8 powx000;				 channel,
							_TRdata = 0S_MCS5cha= PRBWrlse v, C18)|v, C17)|v, C16)|v, C9)|v, C8), 0_PATH__r		 channel,
							_TRdata = 0S_MCS5cha= PRBWrlse v RTXAGC_B_Ncase DESC_RATEVHT, COMP_POWER)p{iv = u8 =bbes_1thwRRATEVHT1HARDg_bE_TYPE_T1SS_MCS0ault:
		RT_A36NNEL \n",
		&& d\n",
			li64	
>pppp
	u8 powx114E9o re[SCMapping:100NNEL \n",
		&& d\n",
			li140	
>pppp
	u8 powx110E9o re[SCMap
>pppp
	u8 powx110E9o re[S	 channel,
							_TRdata = 0S_MCS5chaAPKSE_BANDBRFREGME_CHN RTX, 0_PATH_	}

	tx_0MH=  = VHT_DATA_SC_20_LOWESl *rF_80MHVHT_DereT_F_80A) g),
wer_level =
		we, u8 rtlphy->num_total_rfpat5G) {
		char limits[10] = {0};
		u8 i;

		for (i = 0; i < MAX_REGULATION_NUM; ++i)
			linel, path);
}

static long _rtl8821ae_phy_txpwr_it rtnnelouC_RAT000, nnelcoun= RF_TX_ (i \n",
		ND1:
			 sizes_of_ofdm,
	fpak;
	case HT_C	we, u8 baprogrN_5v->phy;
	uower_indease HT_C		_rbwACE(rinprogrN_5v->phy;
	u_pwr_diff >de wirelessmode,
				|| ( =  *rNOT_IO_txpw	_NSS2INDEX9_NSS2INDEX6,
				e);
 MASKBYTE0, power	we, u8 baprogrN_5it;
		 dDEXer sleep or unloaae_phy_gLRy;
	uower_H= whiledease HT_Clbw80aprogrN_5i&& nnelcoun= <tnnelouC	_NSS2mdelay 50hy_gLnnelcoun= +		50SC_RATEsset_bbreg(hf (E0s);

		MP_POWER_[TX_ates_1t[]  = {DESC_RATEV!al power by r_r		 channel,
				N;
		_el, pathESC_a = 0 power by r; limit,
	  _bbreg(hf (E0s);

		MP_P	li14_[TX_ates_1t[]  = {DESC_RATEV!al power b DESC_RA	 channel,
				N;
		_el, pathESC_a = 0 power b DESCC_RA

	 HT_C	we, u8 baprogrN_5GmiSC_Rer_inde \n",
		N				    \n",
		ND1reak = VHT_DATA_SC_20_LOWESl *rF_80MHVHT_Delse fsN;
			to ofdm,
	y_
	case m(strushy_get_txpD1:
			 sizes_of_ofdm,
	,X_ates_1t[]  = {DESC_RATESC_RA

	reg_rf_mode	we, u8 callpath ATEVHT1

	reg_rf_dm_clearw, ofdm_rateck patw, te		for (	 channel,
							_1t);
	_rtl882el,
						 sizes_of_ofdm DESC_RA = VHT_DATA_SC_20_LOWESl *rF_80MHVHT_DereT_F_80A

	 HT_C	we, u8 baprogrN_5Gmit;
			eSRy;
	uo180A) g), DESC_RATE5_bw righte, u8 placE5for_iqk( (i \nhw, vh		txpower +all[TARGET== PRINUM_2Gdiff[_RAth][TX_21, 2, 3, 4, 5, 6, 7, 8, 9elseels1if (andw_txpATE536, widt4eel;
		4TE546, 4idt5eel5
		}set_b56		} el6eel6
		64elseeels0(and04else if 4,
			1seels1(andATE5116E511idth20if (band2TE512set_b12idth30if 3(andwTE5136, dwidth4eel149els5;
	u8153dth == H57= H59els61if 63dth6 DESC_RAplacE =i \nhwr_diff = BANOWER)p{iv tl8821lacE =i14;AplacE C_RATEMCS0 <= ratall);AplacE021iv = u8 xpower +all[placET1SS_MCn= rate &rtlprivlacE-13;			rtlphy->c 	H_A) #define MACBB_own NUMf 4 #define AFE_own NUMf 4 #define chaown NUMf3priv->cfg->ops->set_hw_reiqk path < REGbbh,
							 channel,
							 sizt rt*REGbb path <			 sizt rt*path < REGbbRdat, t rtREG

	 DES5G) {
		char limits[10] = {0};
		u8 i;

		for (t rtiVHT1

	urn;

	data = 00x82c,iv, C31)0_0x0);A/*[31th][0 --> Page C821a/*save MACBB 		}
	}
 valul,
	_by_path(struct iREG

	 DESee8021SS2mEGbb path <VHT1SD1:
			iotdRG,
				 "Unknpath < REGbbRdatT_TRACRA = VHT_DATA_SC_20_LOWESIQKF_80MH->curree = IOMacBB SuccN_5!!eak;
		clpriv->cfg->ops->set_hw_reiqk path < af(h,
							 channel,
					 t rt*af( path <			 si_DM_BYt rt*path < af( own	 t rtaf( DES5G) {
		char limits[10] = {0};
		u8 i;

		for (t rtiVHT1

	urn;

	data = 00x82c,iv, C31)0_0x0);A/*[31th][0 --> Page C821a/*Save AFE Parameters ,
	_by_path(struct iaf( DESee8021SS2af( path <VHT1SD1:
			iotdRG,
				 "Unknpath < af( ownT_TRACE = VHT_DATA_SC_20_LOWESIQKF_80MH->curree = IOAFE SuccN_5!!eak;
		clpriv->cfg->ops->set_hw_reiqk path < rfh,
							 channel,
					 t rt*rfa path <			 si_DM_Bt rt*rfb path <	Yt rt*path < _TRdat			 si_DM_Bt rt_TRDES5G) {
		char limits[10] = {0};
		u8 i;

		for (t rtiVHT1

	urn;

	data = 00x82c,iv, C31)0_0x0);A/*[31th][0 --> Page C821a/*Save RF Parameters,
	_by_path(struct i_TRDESee80211_hw rfa path <VHT1SD1:
	_bw rfdata = 0RFindex(hw,  path < _TRdat3, power_index);v RTXDWORDhy_gLRfb path <VHT1SD1:
	_bw rfdata = 0RFindex(hwB  path < _TRdat3, power_index);v RTXDWORDhy_gH=  = VHT_DATA_SC_20_LOWESIQKF_80MH->curree = IORF SuccN_5!!eak;
		clpriv->cfg->ops->set_hw_reiqk configure REG_owe,
							 channel,
				owe5G) {
		char limits[10] = {0};
		u8 i;

		for (/*1SSSSSSSSMAC			Rister s_OFFSESSSSSSSS,
	_

	urn;

	data = 00x82c,iv, C31)0_0x0);A/*[31th][0 --> Page C821ar, DBG_EMEbyte TA_SC_20_0x5(ban0x3f_CTLR, Drn;

	data = 00x55eelv, C11)priv, C3)0_0x0);1ar, DBG_EMEbyte TA_SC_20_0x84,
owx00);asseRX ante*rtl,
	_

	urn;

	data = 00x83,
owxf
owxc);asseCCA*rtl,
	lpriv->cfg->ops->set_hw_reiqk tx_fil8 bqch,
							 channel,
							 siCE(rtlpf_vhtradioes_2t(!in_24g rtnx_x24g rtnx_y5G) {
		char limits[10] = {0};
		u8 i;

		for (id_temp e_phyriv->cfg-RFindex(hw,:casset[31th][1 --> Page C18821a0R, Dbreak;
		case 0x82c,iv, C31)0_0x1MP_ERR, DBG_EMEdRG,
				 "Unknwxc90
owx00000080MP_ERR, DBG_EMEdRG,
				 "Unknwxcc4knwx20040000MP_ERR, DBG_EMEdRG,
				 "Unknwxcc8knwx20000000MP_ERR, Dbreak;
		case 0xccc
owx000007 RF9nx_y5P_ERR, Dbreak;
		case 0xcd4
owx000007 RF9nx_NSS2IN = VHT_DATA_SC_20_LOWESIQKF_80MH->cur powerTX_Xh][%x;;TX_Yh][%x1SSSSS> fil8	to IQCget_txp	tnx_x24nx_y5P_ER = VHT_DATA_SC_20_LOWESIQKF_80MH->cur power0xcd4h][%x;;0xccch][%x1SSSS>fil8	to IQCget_txp	t1:
	_bw k;
		case 0xcd4
owx000007 R)_txp	t1:
	_bw k;
		case 0xccc
owx000007 R));
			break;
		}
	}
}

sse DESC_R	clpriv->cfg->ops->set_hw_reiqk rx_fil8 bqch,
							 channel,
							 siCE(rtlpf_vhtradioes_2t(!in_24g rtrx_x24g rtrx_y5G) {
		char limits[10] = {0};
		u8 i;

		for (id_temp e_phyriv->cfg-RFindex(hw,:cas

	urn;

	data = 00x82c,iv, C31)0_0x0);A/* [31th][0 --> Page C8821a0R, Dbreak;
		case 0xc10
owx000003 RF9rx_x>>1MP_ERR, Dbreak;
		case 0xc10
owx03 R0000, rx_y>>1MP_ER = VHT_DATA_SC_20_LOWESIQKF_80MH->cur powerrx_xh][%x;;rx_yh][%x1SSSS>fil8	to IQCget_txp	t1x_x>>1, rx_y>>1MP_ER = VHT_DATA_SC_20_LOWESIQKF_80MH->cur power0xc10h][%x1SSSS>fil8	to IQCget_txp	t1:
			iotdRG,
				 "Unkn0xc10));
			break;
		}
	}
}

sse DESC_R	clpr#define calRDESf 4 riv->cfg->ops->set_hw_reiqk txh,
							 channel,
					 f_vhtradioes_2t(!in_5G) {
		char limits[10] = {0};
		u8 i;

		for (i = 0; i < MAX_REGULATION_NUM; ++i)
			linel, path);
}

static long _rtl8821ae_phy_txpwr_ (t r	tx_fail, rx_fail, delay_coun=, iqk r	ioy, calRphyry, cale_bw &tempRdat65		"int	tx_xe_bw &tx_yh][0, rx_xh][0, rx_ye_bw &tx_average ][0, rx_average ][0		"int	tx_x0[calRDES] &tx_y0[calRDES] &tx_x0 rxk[calRDES] _gLnx_y0 rxk[calRDES]  rx_x0[calRDES]  rx_y0[calRDES]		"boolLnx0iqkokGmit;
		  rx0iqkokGmit;
				"boolLvdf_enableGmit;
				"int	i, k, vdf_y[3], vdf_x[3], nx_dt[3], rx_dt[3],RATEi, dxh][0, dye_bw &tx_finish ][0, rx_finish ][0ACRA = VHT_DATA_SC_20_LOWESIQKF_80MH->curtxp	"iv *W rate  %d.get_txp	t1:
			 sizes_of_ofdm_re_CTL;
	case HT_CHANNEL_WIDTH_20_40:
		tmp = reg_rf_modetxpvdf_enableGmiSC_Rer= whiledecale< calRDES			    u8 txpwe_phyriv-->cfg-RFindex(hw,:cas	tempRdat651SD1:
	_bw rfdata = 0!in_240x65(path == RffTH_	}
/* 		br-A LOK8821a0_

	urn;

	data = 00x82c,iv, C31)0_0x0);A/*[31th][0 --> Page C821a}
/*SSSSSSSS		br-A AFE al8	onSSSSSSSS,
	_}
/*Port[0 DAC/ADC	on821a0_

	uBG_EMEdRG,
				 "Unknwxc60
owx77777777TH_	}


	uBG_EMEdRG,
				 "Unknwxc64
owx77777777TH_	}


	uBG_EMEdRG,
				 "Unknwxc68knwx19791979TH_	}


	uBG_EMEdRG,
				 "Unknwxc6cknwx19791979TH_	}


	uBG_EMEdRG,
				 "Unknwxc70knwx19791979TH_	}


	uBG_EMEdRG,
				 "Unknwxc74knwx19791979TH_	}


	uBG_EMEdRG,
				 "Unknwxc78knwx19791979TH_	}


	uBG_EMEdRG,
				 "Unknwxc7cknwx19791979TH_	}


	uBG_EMEdRG,
				 "Unknwxc80knwx19791979TH_	}


	uBG_EMEdRG,
				 "Unknwxc84knwx19791979TH_1a0_

	urn;

	data = 00xc00
owxf
owx4);A/*hardware 3-el, *rtl,
	_	}
/* LOK8L_OFFSE ,
	_}
/*1SSSSSS LOK8SSSSSS ,
	_}
/*DAC/ADC	samplFSE     r(160 MHz)821a0_

	urn;

	data = 00xc5c,iv, C26)priv, C25)priv, C24)0_0x7TH_1a0_/*12. LoK RF L_OFFSE (at BWh][20M)8821a0_

	urn;
rfdata = 0!in_240xef 0RFown ME_CHNL RTX, 0x84002		cas_

	urn;
rfdata = 0!in_240x1,
owx00c00
owx3);AAAAA/*1BWh20M8821a0_

	urn;
rfdata = 0!in_240x30ifRFown ME_CHNL RTX, 0x20000MP_ER_

	urn;
rfdata = 0!in_240x31ifRFown ME_CHNL RTX, 0x0003 MP_ER_

	urn;
rfdata = 0!in_240x32ifRFown ME_CHNL RTX, 0xf3fc3MP_ER_

	urn;
rfdata = 0!in_240x65(pRFown ME_CHNL RTX, 0x931d5MP_ER_

	urn;
rfdata = 0!in_240x8f 0RFown ME_CHNL RTX, 0x8a001MP_ER_

	urn;

	data = 00xcb,
owxf
owxdTH_	}


	uBG_EMEdRG,
				 "Unknwx90c
owx00008000MP_ER_

	uBG_EMEdRG,
				 "Unknwxb00
owx03000100MP_ER_

	urn;

	data = 00xc94,iv, C0)0_0x1MP_ER


	uBG_EMEdRG,
				 "Unknwx978knwx29002000MP/*1TX (X,Y)8821a0_

	uBG_EMEdRG,
				 "Unknwx97c
owxa9002000MP/*1RX (X,Y)8821a0_

	uBG_EMEdRG,
				 "Unknwx984
owx00462910MP/*1[0]:C_RAen, [15]:idac_K_Mask ,
	_	}
R, Dbreak;
		case 0x82c,iv, C31)0_0x1MP set[31th][1 --> Page C18821a0


	uBG_EMEdRG,
				 "Unknwxc88e 0x821403 4TH_1a0_vht_rates_1t[]  = {DESC_RATE) re[S	 cuBG_EMEdRG,
				 "Unknwxc8c
owx68163e96MP_ER
CMap
>ppp	 cuBG_EMEdRG,
				 "Unknwxc8c
owx28163e96MP__	}


	uBG_EMEdRG,
				 "Unknwxc80knwx18008c10MP/*1TX_TONE_5g_b9:0], TxK_Mask[29]1TX_TonE =i168821a0


	uBG_EMEdRG,
				 "Unknwxc84
owx38008c10MP/*1RX_TONE_5g_b9:0], RxK_Mask[29]1821a0


	uBG_EMEdRG,
				 "Unknwxcb,
owx00100000MP/*1cb,[20] \B1N SI/PI \A8ϥ\CE\C5v\A4\C1\B5\B9 iqk dpk moduled821a0_

	uBG_EMEdRG,
				 "Unknwx980
owxfa000000MP_ER_

	uBG_EMEdRG,
				 "Unknwx980
owxf8000000MP__ER_mdelay 10);A/* Delayf 4ms1821a0


	uBG_EMEdRG,
				 "Unknwxcb,
owx00000000MP__ER_

	urn;

	data = 00x82c,iv, C31)0_0x0);A/* [31th][0 --> Page C8821a0_

	urn;
rfdata = 0!in_240x5,
owx7fe00,D1:
	_bw rfdata = 0!in_240x,
owxffc00));A/* Load LOK88211a0_nd_temp =bbreg(hf (E0s);

			broriv--->cfg-eak;
	_

	urn;
rfdata = 0!in_240x1,
owx00c00
owx1MP_ER
_BY_SCAN;
->cfg-2ak;
	_

	urn;
rfdata = 0!in_240x1,
owx00c00
owx0MP_ER
_BY_SCAN;
-lhal)) {
		s_BY_SCAN;
-}	_	}
R, Dbreak;
		case 0x82c,iv, C31)0_0x1MP set[31th][1 --> Page C188211a0_/*13.1TX RF L_OFFSE 821a0_

	urn;

	data = 00x82c,iv, C31)0_0x0);A/* [31th][0 --> Page C8821a0_

	urn;
rfdata = 0!in_240xef 0RFown ME_CHNL RTX, 0x84000MP_ER_

	urn;
rfdata = 0!in_240x30ifRFown ME_CHNL RTX, 0x20000MP_ER_

	urn;
rfdata = 0!in_240x31ifRFown ME_CHNL RTX, 0x0003 MP_ER_

	urn;
rfdata = 0!in_240x32ifRFown ME_CHNL RTX, 0xf3fc3MP_ER_

	urn;
rfdata = 0!in_240x65(pRFown ME_CHNL RTX, 0x931d5MP_ER_

	urn;
rfdata = 0!in_240x8f 0RFown ME_CHNL RTX, 0x8a001MP_ER_

	urn;
rfdata = 0!in_240xef 0RFown ME_CHNL RTX, 0x00000MP_ER_/* ODM_L_OBBRatapDM_Odm 00xcb,
owxf
owxdTHd821a0_

	uBG_EMEdRG,
				 "Unknwx90c
owx00008000MP_ER_

	uBG_EMEdRG,
				 "Unknwxb00
owx03000100MP_ER_

	urn;

	data = 00xc94,iv, C0)0_0x1MP_ER


	uBG_EMEdRG,
				 "Unknwx978knwx29002000MP/*1TX (X,Y)8821a0_

	uBG_EMEdRG,
				 "Unknwx97c
owxa9002000MP/*1RX (X,Y)8821a0_

	uBG_EMEdRG,
				 "Unknwx984
owx0046a910MP/*1[0]:C_RAen, [15]:idac_K_Mask ,
	_	}
R, Dbreak;
		case 0x82c,iv, C31)0_0x1MP set[31th][1 --> Page C18821a0


	uBG_EMEdRG,
				 "Unknwxc88e 0x821403 1MP_ER
vht_rates_1t[]  = {DESC_RATE) re[S	 cuBG_EMEdRG,
				 "Unknwxc8c
owx40163e96MP_ER
CMap
>ppp	 cuBG_EMEdRG,
				 "Unknwxc8c
owx00163e96MP__	}
vht_vdf_enableGm *rtult:
		 = VHT_DATA_SC_20_LOWESIQKF_80MH->curreVDF_enableBAND0_DM__by_pakh(strukP	li2ruk0211_hw a0_nd_temp k11_hw a0_>cfg-
	swit	}


	uBG_EMEdRG,
				 "Unknwxc80knwx18008c38MP/*1TX_TONE_5g_b9:0], TxK_Mask[29]1TX_TonE =i168821a0
a0


	uBG_EMEdRG,
				 "Unknwxc84
owx38008c38MP/*1RX_TONE_5g_b9:0], RxK_Mask[29]1821a0
ER_

	urn;

	data = 00xce8,iv, C31)0_0x0);1a0
ER_BY_SCAN;
--->cfg-eak;
	_R_

	urn;

	data = 00xc8eelv, C28)0_0x0);1a0
ER_

	urn;

	data = 00xc84elv, C28)0_0x0);1a0
ER_

	urn;

	data = 00xce8,iv, C31)0_0x0);1a0
ER_BY_SCAN;
--->cfg-2ak;
	_R_ = VHT_DATA_SC_20_LOWESIQKF_80MH->curtxp					"vdf_y[1th][%x;;;vdf_y[0th][%xget_
vdf_y[1t>>21ation00007 RF9vdf_y[0t>>21ation00007 R);1a0
ER_ = VHT_DATA_SC_20_LOWESIQKF_80MH->curtxp					"vdf_x[1th][%x;;;vdf_x[0th][%xget_
vdf_x[1t>>21ation00007 RF9vdf_x[0t>>21ation00007 R);1a0
ER_nx_dt[calth][(vdf_y[1t>>20)-(vdf_y[0t>>20);1a0
ER_nx_dt[calth][((16*nx_dt[calt)*10000/15708);1a0
ER_nx_dt[calth][(nx_dt[calth>>*rt+(nx_dt[calth&iv, C0));1a0
ER_

	uBG_EMEdRG,
				 "Unknwxc80knwx18008c20MP/*1TX_TONE_5g_b9:0], TxK_Mask[29]1TX_TonE =i168821a0
a0


	uBG_EMEdRG,
				 "Unknwxc84
owx38008c20MP/*1RX_TONE_5g_b9:0], RxK_Mask[29]1821a0
ER_

	urn;

	data = 00xce8,iv, C31)0_0x1);1a0
ER_

	urn;

	data = 00xce8,i0x3f R0000, nx_dt[calth&ion00003f R);1a0
ER_BY_SCAN;
---lhal)) {
		s_R_BY_SCAN;
---}
0
a0


	uBG_EMEdRG,
				 "Unknwxcb,
owx00100000MP/*1cb,[20] \B1N SI/PI \A8ϥ\CE\C5v\A4\C1\B5\B9 iqk dpk moduled821a0_-->clRphyry ][0		"""""whiledertult:
		R_/* onE shot8821a0
a0


	uBG_EMEdRG,
				 "Unknwx980
owxfa000000MP_ER_a0


	uBG_EMEdRG,
				 "Unknwx980
owxf8000000MP__ER_	R_mdelay 10);A/* Delayf 4ms1821a0
a0


	uBG_EMEdRG,
				 "Unknwxcb,
owx00000000MP_ER_a0
delay_coun= ][0		""""""whiledertult:
		R_	iqk r	ioy1SD1:
	_bw 
	data = 00xd0eelv, C10));1a0
ER_diff >~iqk r	ioy		|| (delay_coun= > 20M)1a0
ER_d_BY_SCAN;
---R
CMaplt:
		R_	_mdelay 1);1a0
ER_d
delay_coun=++;1a0
ER_d}
0
a0
-}	_	}
R_diff delay_coun= < 20M {	:
		R_/* If 20ms1No Res)) , nhen calRphyry++1821a0
a0
(/*1SSSSSSSSSSSSTXIQK Check==============1821a0
a0
(tx_fail1SD1:
	_bw 
	data = 00xd0eelv, C12)MP__ER_	R_diff ~tx_failtult:
		R_	


	uBG_EMEdRG,
				 "Unknwxcb,
owx02000000MP_ER_a0
xpvdf_x[k]1SD1:
	_bw 
	data = 00xd0eelwx07 R0000)<<21;t:
		R_	


	uBG_EMEdRG,
				 "Unknwxcb,
owx04000000MP_ER_a0
xpvdf_y[k]1SD1:
	_bw 
	data = 00xd0eelwx07 R0000)<<21;t:
		R_	
nx0iqkokGmiSC_Rer_0
ER_d_BY_SCAN;
---R
   channel)		R_	


	ubreak;
		case 0xccc
owx000007 RF90x0);1a0
ER_ERR, Dbreak;
		case 0xcd4
owx000007 RF90x200);t:
		R_	
nx0iqkokGmit;
			eSCCCCCCCcalRphyry++	eSCCCCCCC u8 xclRphyry ]RAT0)1a0
ER_d__BY_SCAN;
---R
 
0
a0
-}  channel)		R_	nx0iqkokGmit;
			eSCCCCCCcalRphyry++	eSCCCCCC u8 xclRphyry ]RAT0)1a0
ER_d_BY_SCAN;
---R}N;
---}
0
a0}
0
a0 u8 k ]RA3tult:
		Rtx_x0[cal]1SDvdf_x[k-1];t:
		Rtx_y0[cal]1SDvdf_y[k-1];t:
		}
0
a}  channel)		

	uBG_EMEdRG,
				 "Unknwxc80knwx18008c10MP/*1TX_TONE_5g_b9:0], TxK_Mask[29]1TX_TonE =i168821a0



	uBG_EMEdRG,
				 "Unknwxc84
owx38008c10MP/*1RX_TONE_5g_b9:0], RxK_Mask[29]1821a0



	uBG_EMEdRG,
				 "Unknwxcb,
owx00100000MP/*1cb,[20] \B1N SI/PI \A8ϥ\CE\C5v\A4\C1\B5\B9 iqk dpk moduled821a0_->clRphyry ][0		""""whiledertult:
		R/* onE shot8821a0
a0

	uBG_EMEdRG,
				 "Unknwx980
owxfa000000MP_ER_a0

	uBG_EMEdRG,
				 "Unknwx980
owxf8000000MP__ER_	Rmdelay 10);A/* Delayf 4ms1821a0
a0

	uBG_EMEdRG,
				 "Unknwxcb,
owx00000000MP_ER_a0delay_coun= ][0		"""""whiledertult:
		R_iqk r	ioy1SD1:
	_bw 
	data = 00xd0eelv, C10));1a0
ER_iff >~iqk r	ioy		|| (delay_coun= > 20M)1a0
ER_dBY_SCAN;
---RCMaplt:
		R_	mdelay 1);1a0
ER_ddelay_coun=++;1a0
ER_}N;
---}

0
ER_iff delay_coun= < 20M {	:
		R_/* If 20ms1No Res)) , nhen calRphyry++1821a0
a0
/*1SSSSSSSSSSSSTXIQK Check==============1821a0
a0
tx_fail1SD1:
	_bw 
	data = 00xd0eelv, C12)MP__ER_	R_iff ~tx_failtult:
		R_	

	uBG_EMEdRG,
				 "Unknwxcb,
owx02000000MP_ER_a0
xtx_x0[cal]1SD1:
	_bw 
	data = 00xd0eelwx07 R0000)<<21;t:
		R_	

	uBG_EMEdRG,
				 "Unknwxcb,
owx04000000MP_ER_a0
xtx_y0[cal]1SD1:
	_bw 
	data = 00xd0eelwx07 R0000)<<21;t:
		R_	nx0iqkokGmiSC_Rer_0
ER_dBY_SCAN;
---R}  channel)		R_	

	ubreak;
		case 0xccc
owx000007 RF90x0);1a0
ER_ER, Dbreak;
		case 0xcd4
owx000007 RF90x200);t:
		R_	nx0iqkokGmit;
			eSCCCCCCcalRphyry++	eSCCCCCC u8 xclRphyry ]RAT0)1a0
ER_d_BY_SCAN;
---R}N;
---}  channel)		R_nx0iqkokGmit;
			eSCCCCCcalRphyry++	eSCCCCC u8 xclRphyry ]RAT0)1a0
ER_dBY_SCAN;
---}
0
a0}
0
a}__	}
vht_nx0iqkokGmmit;
		)1a0
EBY_SCA
a0
/*1TXK fail, Don't do RXK88211a0_vht_vdf_enableGm *rtult:
		

	urn;

	data = 00xce8,iv, C31)0_0x0);AAAA/*1TX VDF DisableG821a0
a = VHT_DATA_SC_20_LOWESIQKF_80MH->curreRXVDF StartBAND0_DM__by_pakh(strukP	li2ruk0211_hw a0_/*1SSSSSS RX 	}

	TXK (RXK8Step*rtu======1821a0
a0

	urn;

	data = 00x82c,iv, C31)0_0x0);A/* [31th][0 --> Page C8821a0_0_/*11.1TX RF L_OFFSE 821a0_0_

	urn;
rfdata = 0!in_240xef 0RFown ME_CHNL RTX, 0x84000MP_ER_R_

	urn;
rfdata = 0!in_240x30ifRFown ME_CHNL RTX, 0x34000MP_ER_R_

	urn;
rfdata = 0!in_240x31ifRFown ME_CHNL RTX, 0x00029MP_ER_R_

	urn;
rfdata = 0!in_240x32ifRFown ME_CHNL RTX, 0xd7 RbMP_ER_R_

	urn;
rfdata = 0!in_240x65(pRFown ME_CHNL RTX, tempRdat65MP_ER_R_

	urn;
rfdata = 0!in_240x8f 0RFown ME_CHNL RTX, 0x8a001MP_ER_R_

	urn;
rfdata = 0!in_240xef 0RFown ME_CHNL RTX, 0x00000MP_1a0
a0

	urn;

	data = 00xcb,
owxf
owxdTH_	}
R


	uBG_EMEdRG,
				 "Unknwx978knwx29002000MP/*1TX (X,Y)8821a0_0_

	uBG_EMEdRG,
				 "Unknwx97c
owxa9002000MP/*1RX (X,Y)8821a0_0_

	uBG_EMEdRG,
				 "Unknwx984
owx0046a910MP/*1[0]:C_RAen, [15]:idac_K_Mask ,
	a0_0_

	uBG_EMEdRG,
				 "Unknwx90c
owx00008000MP_ER_R_

	uBG_EMEdRG,
				 "Unknwxb00
owx03000100MP_ER_}
R, Dbreak;
		case 0x82c,iv, C31)0_0x1MP set[31th][1 --> Page C18821a0
0_nd_temp k11_hw a0_>cfg-
	swit	}
lt:
		R_	

	uBG_EMEdRG,
				 "Unknwxc80knwx18008c38MP/*1TX_TONE_5g_b9:0], TxK_Mask[29]1TX_TonE =i168821a0
a0



	uBG_EMEdRG,
				 "Unknwxc84
owx38008c38MP/*1RX_TONE_5g_b9:0], RxK_Mask[29]1821a0
ER_	

	urn;

	data = 00xce8,iv, C30)0_0x0);1a0
ER_ 
0
a0
-BY_SCAN;
--->cfg-eak;
	_R_lt:
		R_	

	uBG_EMEdRG,
				 "Unknwxc80knwx08008c38MP/*1TX_TONE_5g_b9:0], TxK_Mask[29]1TX_TonE =i168821a0
a0



	uBG_EMEdRG,
				 "Unknwxc84
owx28008c38MP/*1RX_TONE_5g_b9:0], RxK_Mask[29]1821a0
ER_	

	urn;

	data = 00xce8,iv, C30)0_0x0);1a0
ER_ 
0
a0
-BY_SCAN;
--->cfg-2ak;
	_R_lt:
		R_	 = VHT_DATA_SC_20_LOWESIQKF_80MH->curtxp					"VDF_Y[1th][%x;;;VDF_Y[0th][%xget_txp					vdf_y[1t>>21ation00007 RF9vdf_y[0t>>21ation00007 R);1a0
ER_	 = VHT_DATA_SC_20_LOWESIQKF_80MH->curtxp					"VDF_X[1th][%x;;;VDF_X[0th][%xget_txp					vdf_x[1t>>21ation00007 RF9vdf_x[0t>>21ation00007 R);1a0
ER_	rx_dt[calth][(vdf_y[1t>>20)-(vdf_y[0t>>20);1a0
ER_a = VHT_DATA_SC_20_LOWESIQKF_80MH->curreRx_dte  %dget_
	x_dt[calt);1a0
ER_	rx_dt[calth][((16*rx_dt[calt)*10000/13823);1a0
ER_	rx_dt[calth][(rx_dt[calth>>*rt+(rx_dt[calth&iv, C0));1a0
ER__

	uBG_EMEdRG,
				 "Unknwxc80knwx18008c20MP/*1TX_TONE_5g_b9:0], TxK_Mask[29]1TX_TonE =i168821a0
a0



	uBG_EMEdRG,
				 "Unknwxc84
owx38008c20MP/*1RX_TONE_5g_b9:0], RxK_Mask[29]1821a0
ER__

	urn;

	data = 00xce8,i0x00003f R_
	x_dt[calth&ion00003f R);1a0
ER_ 
0
a0
-BY_SCAN;
---lhal)) {
		s_R_BY_SCAN;
---}
0
a0


	uBG_EMEdRG,
				 "Unknwxc88e 0x821603e0MP_ER_R_

	uBG_EMEdRG,
				 "Unknwxc8c
owx68163e96MP_ER
0


	uBG_EMEdRG,
				 "Unknwxcb,
owx00100000MP/*1cb,[20] \B1N SI/PI \A8ϥ\CE\C5v\A4\C1\B5\B9 iqk dpk moduled821a0_-->clRphyry ][0		"""""whiledertult:
		R_/* onE shot8821a0
a0


	uBG_EMEdRG,
				 "Unknwx980
owxfa000000MP_ER_a0


	uBG_EMEdRG,
				 "Unknwx980
owxf8000000MP__ER_	R_mdelay 10);A/* Delayf 4ms1821a0
a0


	uBG_EMEdRG,
				 "Unknwxcb,
owx00000000MP_ER_a0
delay_coun= ][0		""""""whiledertult:
		R_	iqk r	ioy1SD1:
	_bw 
	data = 00xd0eelv, C10));1a0
ER_diff >~iqk r	ioy		|| (delay_coun= > 20M)1a0
ER_d_BY_SCAN;;;;;;;CMaplt:
		R_	_mdelay 1);1a0
ER_d
delay_coun=++;1a0
ER_d}
0
a0
-}	_	}
R_diff delay_coun= < 20M {	:
		R_/* If 20ms1No Res)) , nhen calRphyry++1821a0
a0
(/*1SSSSSSSSSSSSTXIQK Check==============1821a0
a0
(tx_fail1SD1:
	_bw 
	data = 00xd0eelv, C12)MP__ER_	R_diff ~tx_failtult:
		R_	


	uBG_EMEdRG,
				 "Unknwxcb,
owx02000000MP_ER_a0
xptx_x0 rxk[cal]1SD1:
	_bw 
	data = 00xd0eelwx07 R0000)<<21;t:
		R_	


	uBG_EMEdRG,
				 "Unknwxcb,
owx04000000MP_ER_a0
xpnx_y0 rxk[cal]1SD1:
	_bw 
	data = 00xd0eelwx07 R0000)<<21;t:
		R_	
nx0iqkokGmiSC_Rer_0
ER_d_BY_SCAN;
---R
   cha{t:
		R_	
nx0iqkokGmit;
			eSCCCCCCCcalRphyry++	eSCCCCCCC u8 xclRphyry ]RAT0)1a0
ER_d__BY_SCAN;
---R
 
0
a0
-}  channel)		R_	nx0iqkokGmit;
			eSCCCCCCcalRphyry++	eSCCCCCC u8 xclRphyry ]RAT0)1a0
ER_d_BY_SCAN;
---R}N;
---}

0
ER_iff nx0iqkokGmmit;
		) {AAA/*1If RX 	}

	TXK fail, nhen tak
	TXK Res)) 1821a0
a0
tx_x0 rxk[cal]1SDtx_x0[cal];1a0
ER_nx_y0 rxk[cal]1SDtx_y0[cal];1a0
ER_nx0iqkokGmiSC_Rer_0
ER_ = VHT_DATA_SC_201a0
ER_d_LOWESIQKF1a0
ER_d_80MH->curtxp					reRXK8Step*r failBAND0_DM__-}

0
ER_/*1SSSSSS RX IQK ======1821a0
a0

	urn;

	data = 00x82c,iv, C31)0_0x0);A/* [31th][0 --> Page C8821a0_0_/*11.1RX RF L_OFFSE 821a0_0_

	urn;
rfdata = 0!in_240xef 0RFown ME_CHNL RTX, 0x84000MP_ER_R_

	urn;
rfdata = 0!in_240x30ifRFown ME_CHNL RTX, 0x34000MP_ER_R_

	urn;
rfdata = 0!in_240x31ifRFown ME_CHNL RTX, 0x0002fMP_ER_R_

	urn;
rfdata = 0!in_240x32ifRFown ME_CHNL RTX, 0xf RbbMP_ER_R_

	urn;
rfdata = 0!in_240x8f 0RFown ME_CHNL RTX, 0x88001MP_ER_R_

	urn;
rfdata = 0!in_240x65(pRFown ME_CHNL RTX, 0x931d8MP_ER_R_

	urn;
rfdata = 0!in_240xef 0RFown ME_CHNL RTX, 0x00000MP_1a0
a0

	urn;

	data = 00x978knwx03FF8400,[(nx_x0 rxk[cal])>>21&wx000007 R)P_ER_}
R, Dbreak;
		case 0x978knwx000007FF,[(nx_y0 rxk[cal])>>21&wx000007 R)P_ER_}
R, Dbreak;
		case 0x978knv, C31)0_0x1);1a0
ERR, Dbreak;
		case 0x97c,iv, C31)0_0x0);1a0
a0

	urn;

	data = 00xcb,
owxF240xeTH_	}
R


	uBG_EMEdRG,
				 "Unknwx90c
owx00008000MP_ER_R_

	uBG_EMEdRG,
				 "Unknwx984
owx0046a911MP_1a0
a0

	urn;

	data = 00x82c,iv, C31)0_0x1MP set[31th][1 --> Page C18821a0
0_

	urn;

	data = 00xc8eelv, C29)0_0x1);1a0
ERR, Dbreak;
		case 0xc84elv, C29)0_0x0);1a0
a0

	uBG_EMEdRG,
				 "Unknwxc88e 0x02140119MP_1a0
a0

	uBG_EMEdRG,
				 "Unknwxc8c
owx28160d00);A/* pDM_Odm->SupportInterfacE =at18821
0
ER_iff kGmmi2)1a0
ER_

	urn;

	data = 00xce8,iv, C30)0_0x1MP  /*1RX VDF EnableG821a0
a


	uBG_EMEdRG,
				 "Unknwxcb,
owx00100000MP/*1cb,[20] \B1N SI/PI \A8ϥ\CE\C5v\A4\C1\B5\B9 iqk dpk moduled8211a0_-->clRphyry ][0		"""""whiledertult:
		R_/* onE shot8821a0
a0


	uBG_EMEdRG,
				 "Unknwx980
owxfa000000MP_ER_a0


	uBG_EMEdRG,
				 "Unknwx980
owxf8000000MP__ER_	R_mdelay 10);A/* Delayf 4ms1821a0
a0


	uBG_EMEdRG,
				 "Unknwxcb,
owx00000000MP_ER_a0
delay_coun= ][0		""""""whiledertult:
		R_	iqk r	ioy1SD1:
	_bw 
	data = 00xd0eelv, C10));1a0
ER_diff >~iqk r	ioy		|| (delay_coun= > 20M)1a0
ER_d_BY_SCAN;;;;;;;CMaplt:
		R_	_mdelay 1);1a0
ER_d
delay_coun=++;1a0
ER_d}
0
a0
-}	_	}
R_diff delay_coun= < 20M {	/* If 20ms1No Res)) , nhen calRphyry++1821a0
a0
(/*1SSSSSSSSSSSSRXIQK Check==============1821a0
a0
(rx_fail1SD1:
	_bw 
	data = 00xd0eelv, C11));1a0
ER_diff rx_fail1S				ult:
		R_	


	uBG_EMEdRG,
				 "Unknwxcb,
owx06000000MP_ER_a0
xpvdf_x[k]1SD1:
	_bw 
	data = 00xd0eelwx07 R0000)<<21;t:
		R_	


	uBG_EMEdRG,
				 "Unknwxcb,
owx08000000MP_ER_a0
xpvdf_y[k]1SD1:
	_bw 
	data = 00xd0eelwx07 R0000)<<21;t:
		R_	
rx0iqkokGmiSC_Rer_0
ER_d_BY_SCAN;
---R
   channel)		R_	


	ubreak;
		case 0xc10
owx000003 RF90x200>>1MP_ER		R_	


	ubreak;
		case 0xc10
owx03 R0000, wx0>>1MP_ER		R_	

x0iqkokGmit;
			eSCCCCCCCcalRphyry++	eSCCCCCCC u8 xclRphyry ]RAT0)1a0
ER_d__BY_SCANN;
---R
 
0
a0
-}  chalt:
		R_	
x0iqkokGmit;
			eSCCCCCCcalRphyry++	eSCCCCCC u8 xclRphyry ]RAT0)1a0
ER_d_BY_SCAN;
---R}N;
---}

0
ER}
0
a0 u8 k ]RA3tult:
		Rrx_x0[cal]1SDvdf_x[k-1];t:
		Rrx_y0[cal]1SDvdf_y[k-1];t:
		}
0
a_

	urn;

	data = 00xce8,iv, C31)0_0x1);AAAA/*1TX VDF EnableG821a0
}

0
E chalt:
		/*1SSSSSS RX 	}

	TXK (RXK8Step*rtu======1821a0
a

	urn;

	data = 00x82c,iv, C31)0_0x0);A/* [31th][0 --> Page C8821a0_0/*11.1TX RF L_OFFSE 821a0_0

	urn;
rfdata = 0!in_240xef 0RFown ME_CHNL RTX, 0x84000MP_ER_R

	urn;
rfdata = 0!in_240x30ifRFown ME_CHNL RTX, 0x34000MP_ER_R

	urn;
rfdata = 0!in_240x31ifRFown ME_CHNL RTX, 0x00029MP_ER_R

	urn;
rfdata = 0!in_240x32ifRFown ME_CHNL RTX, 0xd7 RbMP_ER_R

	urn;
rfdata = 0!in_240x65(pRFown ME_CHNL RTX, tempRdat65MP_ER_R

	urn;
rfdata = 0!in_240x8f 0RFown ME_CHNL RTX, 0x8a001MP_ER_R

	urn;
rfdata = 0!in_240xef 0RFown ME_CHNL RTX, 0x00000MP_ER_R

	uBG_EMEdRG,
				 "Unknwx90c
owx00008000MP_ER_R

	uBG_EMEdRG,
				 "Unknwxb00
owx03000100MP_ER_}

	uBG_EMEdRG,
				 "Unknwx984
owx0046a910MP/*1[0]:C_RAen, [15]:idac_K_Mask ,
	1a0
a

	urn;

	data = 00x82c,iv, C31)0_0x1MP set[31th][1 --> Page C18821a0
0

	uBG_EMEdRG,
				 "Unknwxc80knwx18008c10MP/*1TX_TONE_5g_b9:0], TxK_Mask[29]1TX_TonE =i168821a0



	uBG_EMEdRG,
				 "Unknwxc84
owx38008c10MP/*1RX_TONE_5g_b9:0], RxK_Mask[29]1821a0



	uBG_EMEdRG,
				 "Unknwxc88e 0x821603e0MP_ER_R/* ODM_WG_EM4Byte pDM_Odm 00xc8c
owx68163e96MP1821a0



	uBG_EMEdRG,
				 "Unknwxcb,
owx00100000MP/*1cb,[20] \B1N SI/PI \A8ϥ\CE\C5v\A4\C1\B5\B9 iqk dpk moduled821a0_->clRphyry ][0		""""whiledertult:
		R/* onE shot8821a0
a0

	uBG_EMEdRG,
				 "Unknwx980
owxfa000000MP_ER_a0

	uBG_EMEdRG,
				 "Unknwx980
owxf8000000MP__ER_	Rmdelay 10);A/* Delayf 4ms1821a0
a0

	uBG_EMEdRG,
				 "Unknwxcb,
owx00000000MP_ER_a0delay_coun= ][0		"""""whiledertult:
		R_iqk r	ioy1SD1:
	_bw 
	data = 00xd0eelv, C10));1a0
ER_iff >~iqk r	ioy		|| (delay_coun= > 20M)1a0
ER_dBY_SCAN;
---RCMaplt:
		R_	mdelay 1);1a0
ER_ddelay_coun=++;1a0
ER_}N;
---}

0
ER_iff delay_coun= < 20M {	:
		R_/* If 20ms1No Res)) , nhen calRphyry++1821a0
a0
/*1SSSSSSSSSSSSTXIQK Check==============1821a0
a0
tx_