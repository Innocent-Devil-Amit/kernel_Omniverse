/*
 * SiI8620 Linux Driver
 *
 * Copyright (C) 2013-2014 Silicon Image, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 * This program is distributed AS-IS WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; INCLUDING without the implied warranty
 * of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE or NON-INFRINGEMENT.
 * See the GNU General Public License for more details at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 */

#ifndef _SI_8620_REGS_H_
#define _SI_8620_REGS_H_

#ifndef ALT_I2C_ADDR
	#define SA_TX_PAGE_0 (0x72)
	#define SA_TX_PAGE_1 (0x7A)
	#define SA_TX_PAGE_2 (0x92)
	#define SA_TX_PAGE_3 (0x9A)
	#define SA_TX_PAGE_4 (0xBA)
	#define SA_TX_PAGE_6 (0xB2)
	#define SA_TX_PAGE_7 (0xC2)
	#define SA_TX_PAGE_8 (0xD2)
#else
	#define SA_TX_PAGE_0 (0x76)
	#define SA_TX_PAGE_1 (0x7E)
	#define SA_TX_PAGE_2 (0x96)
	#define SA_TX_PAGE_3 (0x9E)
	#define SA_TX_PAGE_4 (0xBE)
	#define SA_TX_PAGE_6 (0xB6)
	#define SA_TX_PAGE_7 (0xC6)
	#define SA_TX_PAGE_8 (0xD6)
#endif

#define SA_TX_CBUS (0xC8)

#define TX_PAGE_0 (SA_TX_PAGE_0 << 8)
#define TX_PAGE_1 (SA_TX_PAGE_1 << 8)
#define TX_PAGE_2 (SA_TX_PAGE_2 << 8)
#define TX_PAGE_3 (SA_TX_PAGE_3 << 8)
#define TX_PAGE_4 (SA_TX_PAGE_4 << 8)
#define TX_PAGE_5 (SA_TX_CBUS << 8)
#define TX_PAGE_6 (SA_TX_PAGE_6 << 8)
#define TX_PAGE_7 (SA_TX_PAGE_7 << 8)
#define TX_PAGE_8 (SA_TX_PAGE_8 << 8)

/* Registers in TX_PAGE_0 (0x00-0xFF)                                         */

/* 0x00 Vendor ID Low byte Register                        (Default: 0x01)    */
#define REG_VND_IDL                                 (TX_PAGE_0 | 0x00)
#define MSK_VND_IDL_VHDL_IDL                        (0xFF)

/* 0x01 Vendor ID High byte Register                       (Default: 0x00)    */
#define REG_VND_IDH                                 (TX_PAGE_0 | 0x01)
#define MSK_VND_IDH_VHDL_IDH                        (0xFF)

/* 0x02 Device ID Low byte Register                        (Default: 0x60)    */
#define REG_DEV_IDL                                 (TX_PAGE_0 | 0x02)
#define MSK_DEV_IDL_DEV_IDL                         (0xFF)

/* 0x03 Device ID High byte Register                       (Default: 0x86)    */
#define REG_DEV_IDH                                 (TX_PAGE_0 | 0x03)
#define MSK_DEV_IDH_DEV_IDH                         (0xFF)

/* 0x04 Device Revision Register                           (Default: 0x10)    */
#define REG_DEV_REV                                 (TX_PAGE_0 | 0x04)
#define MSK_DEV_REV_DEV_REV_ID                      (0xFF)

/* 0x06 OTP DBYTE510 Register                              (Default: 0x00)    */
#define REG_OTP_DBYTE510                            (TX_PAGE_0 | 0x06)
#define MSK_OTP_DBYTE510_OTP_DBYTE510               (0xFF)

/* 0x08 System Control #1 Register                         (Default: 0x00)    */
#define REG_SYS_CTRL1                               (TX_PAGE_0 | 0x08)
#define BIT_SYS_CTRL1_OTPVMUTEOVR_SET               (0x80)
#define BIT_SYS_CTRL1_VSYNCPIN                      (0x40)
#define BIT_SYS_CTRL1_OTPADROPOVR_SET               (0x20)
#define BIT_SYS_CTRL1_BLOCK_DDC_BY_HPD              (0x10)
#define BIT_SYS_CTRL1_OTP2XVOVR_EN                  (0x08)
#define BIT_SYS_CTRL1_OTP2XAOVR_EN                  (0x04)
#define BIT_SYS_CTRL1_TX_CONTROL_HDMI               (0x02)
#define BIT_SYS_CTRL1_OTPAMUTEOVR_SET               (0x01)

/* 0x0B System Control DPD                                 (Default: 0x90)    */
#define REG_DPD                                     (TX_PAGE_0 | 0x0B)
#define BIT_DPD_PWRON_PLL                           (0x80)
#define BIT_DPD_PDNTX12                             (0x40)
#define BIT_DPD_PDNRX12                             (0x20)
#define BIT_DPD_OSC_EN                              (0x10)
#define BIT_DPD_PWRON_HSIC                          (0x08)
#define BIT_DPD_PDIDCK_N                            (0x04)
#define BIT_DPD_PD_MHL_CLK_N                        (0x02)

/* 0x0D Dual link Control Register                         (Default: 0x00)    */
#define REG_DCTL                                    (TX_PAGE_0 | 0x0D)
#define BIT_DCTL_TDM_LCLK_PHASE                     (0x80)
#define BIT_DCTL_HSIC_CLK_PHASE                     (0x40)
#define BIT_DCTL_CTS_TCK_PHASE                      (0x20)
#define BIT_DCTL_EXT_DDC_SEL                        (0x10)
#define BIT_DCTL_TRANSCODE                          (0x08)
#define BIT_DCTL_HSIC_RX_STROBE_PHASE               (0x04)
#define BIT_DCTL_HSIC_TX_BIST_START_SEL             (0x02)
#define BIT_DCTL_TCLKNX_PHASE                       (0x01)

/* 0x0E PWD Software Reset Register                        (Default: 0x20)    */
#define REG_PWD_SRST                                (TX_PAGE_0 | 0x0E)
#define BIT_PWD_SRST_COC_DOC_RST                    (0x80)
#define BIT_PWD_SRST_CBUS_RST_SW                    (0x40)
#define BIT_PWD_SRST_CBUS_RST_SW_EN                 (0x20)
#define BIT_PWD_SRST_MHLFIFO_RST                    (0x10)
#define BIT_PWD_SRST_CBUS_RST                       (0x08)
#define BIT_PWD_SRST_SW_RST_AUTO                    (0x04)
#define BIT_PWD_SRST_HDCP2X_SW_RST                  (0x02)
#define BIT_PWD_SRST_SW_RST                         (0x01)

/* 0x1D AKSV_1 Register                                    (Default: 0x00)    */
#define REG_AKSV_1                                  (TX_PAGE_0 | 0x1D)
#define MSK_AKSV_1_AKSV0                            (0xFF)

/* 0x3A Video H Resolution #1 Register                     (Default: 0x00)    */
#define REG_H_RESL                                  (TX_PAGE_0 | 0x3A)
#define MSK_H_RESL_HRESOUT_7_0                      (0xFF)

/* 0x4A Video Mode Register                                (Default: 0x00)    */
#define REG_VID_MODE                                (TX_PAGE_0 | 0x4A)
#define BIT_VID_MODE_M1080P                         (0x40)
#define VAL_VID_MODE_M1080P_DISABLE                 (0x00)
#define VAL_VID_MODE_M1080P_ENABLE                  (0x40)

/* 0x51 Video Input Mode Register                          (Default: 0xC0)    */
#define REG_VID_OVRRD                               (TX_PAGE_0 | 0x51)
#define REG_VID_OVRRD_DEFVAL                        0x80
#define BIT_VID_OVRRD_PP_AUTO_DISABLE               (0x80)
#define BIT_VID_OVRRD_M1080P_OVRRD                  (0x40)
#define BIT_VID_OVRRD_MINIVSYNC_ON                  (0x20)

#define BIT_VID_OVRRD_3DCONV_EN                     (0x10)
#define VAL_VID_OVRRD_3DCONV_EN_NORMAL              (0x00)
#define VAL_VID_OVRRD_3DCONV_EN_FRAME_PACK          (0x10)

#define BIT_VID_OVRRD_ENABLE_AUTO_PATH_EN           (0x08)
#define BIT_VID_OVRRD_ENRGB2YCBCR_OVRRD             (0x04)
#define BIT_VID_OVRRD_ENDOWNSAMPLE_OVRRD            (0x01)

/* I2C Address reassignment   (Default: 0x00)    */
#define REG_PAGE_MHLSPEC_ADDR                       (TX_PAGE_0 | 0x57)
#define REG_PAGE7_ADDR                              (TX_PAGE_0 | 0x58)
#define REG_PAGE8_ADDR                              (TX_PAGE_0 | 0x5C)

/* 0x5F Fast Interrupt Status Register                     (Default: 0x00)    */
#define REG_FAST_INTR_STAT                          (TX_PAGE_0 | 0x5F)
#define BIT_FAST_INTR_STAT_FAST_INTR_STAT_4         (0x10)
#define BIT_FAST_INTR_STAT_FAST_INTR_STAT_3         (0x08)
#define BIT_FAST_INTR_STAT_FAST_INTR_STAT_2         (0x04)
#define BIT_FAST_INTR_STAT_FAST_INTR_STAT_1         (0x02)
#define BIT_FAST_INTR_STAT_FAST_INTR_STAT_0         (0x01)

/* 0x6E GPIO Control Register 1                            (Default: 0x15)    */
#define REG_GPIO_CTRL1                              (TX_PAGE_0 | 0x6E)
#define BIT_CTRL1_GPIO_I_8                     (0x20)
#define BIT_CTRL1_GPIO_OEN_8                   (0x10)
#define BIT_CTRL1_GPIO_I_7                     (0x08)
#define BIT_CTRL1_GPIO_OEN_7                   (0x04)
#define BIT_CTRL1_GPIO_I_6                     (0x02)
#define BIT_CTRL1_GPIO_OEN_6                   (0x01)

/* 0x6F Interrupt Control Register                         (Default: 0x06)    */
#define REG_INT_CTRL                                (TX_PAGE_0 | 0x6F)
#define BIT_INT_CTRL_SOFTWARE_WP                    (0x80)
#define BIT_INT_CTRL_INTR_OD                        (0x04)
#define BIT_INT_CTRL_INTR_POLARITY                  (0x02)

/* 0x70 Interrupt State Register                           (Default: 0x00)    */
#define REG_INTR_STATE                              (TX_PAGE_0 | 0x70)
#define BIT_INTR_STATE_INTR_STATE                   (0x01)

/* 0x71 Interrupt Source #1 Register                       (Default: 0x00)    */
#define REG_INTR1                                   (TX_PAGE_0 | 0x71)
#define BIT_INTR1_STAT7                       (0x80)
#define BIT_INTR1_STAT6                       (0x40)
#define BIT_INTR1_STAT5                       (0x20)
#define BIT_INTR1_STAT2                       (0x04)

/* 0x72 Interrupt Source #2 Register                       (Default: 0x00)    */
#define REG_INTR2                                   (TX_PAGE_0 | 0x72)
#define BIT_INTR2_STAT7                       (0x80)
#define BIT_INTR2_STAT5                       (0x20)
#define BIT_INTR2_STAT2                       (0x04)
#define BIT_INTR2_STAT1                       (0x02)
#define BIT_INTR2_STAT0                       (0x01)

/* 0x73 Interrupt Source #3 Register                       (Default: 0x01)    */
#define REG_INTR3                                   (TX_PAGE_0 | 0x73)
#define BIT_INTR3_STAT7                       (0x80)
#define BIT_INTR3_STAT6                       (0x40)
#define BIT_INTR3_STAT5                       (0x20)
#define BIT_INTR3_STAT4                       (0x10)
#define BIT_INTR3_STAT3                       (0x08)
#define BIT_INTR3_STAT2                       (0x04)
#define BIT_INTR3_STAT1                       (0x02)
#define BIT_INTR3_STAT0                       (0x01)

/* 0x74 Interrupt Source #5 Register                       (Default: 0x00)    */
#define REG_INTR5                                   (TX_PAGE_0 | 0x74)
#define BIT_INTR5_STAT7                       (0x80)
#define BIT_INTR5_STAT6                       (0x40)
#define BIT_INTR5_STAT5                       (0x20)
#define BIT_INTR5_STAT4                       (0x10)
#define BIT_INTR5_STAT3                       (0x08)
#define BIT_INTR5_STAT2                       (0x04)
#define BIT_INTR5_STAT1                       (0x02)
#define BIT_INTR5_STAT0                       (0x01)

/* 0x75 Interrupt #1 Mask Register                         (Default: 0x00)    */
#define REG_INTR1_MASK                              (TX_PAGE_0 | 0x75)
#define BIT_INTR1_MASK7                  (0x80)
#define BIT_INTR1_MASK6                  (0x40)
#define BIT_INTR1_MASK5                  (0x20)
#define BIT_INTR1_MASK2                  (0x04)

/* 0x76 Interrupt #2 Mask Register                         (Default: 0x00)    */
#define REG_INTR2_MASK                              (TX_PAGE_0 | 0x76)
#define BIT_INTR2_MASK7                  (0x80)
#define BIT_INTR2_MASK5                  (0x20)
#define BIT_INTR2_MASK2                  (0x04)
#define BIT_INTR2_MASK1                  (0x02)
#define BIT_INTR2_MASK0                  (0x01)

/* 0x77 Interrupt #3 Mask Register                         (Default: 0x00)    */
#define REG_INTR3_MASK                              (TX_PAGE_0 | 0x77)
#define BIT_INTR3_MASK7                  (0x80)
#define BIT_INTR3_MASK6                  (0x40)
#define BIT_INTR3_MASK5                  (0x20)
#define BIT_INTR3_MASK4                  (0x10)
#define BIT_INTR3_MASK3                  (0x08)
#define BIT_INTR3_MASK2                  (0x04)
#define BIT_INTR3_MASK1                  (0x02)
#define BIT_INTR3_MASK0                  (0x01)

/* 0x78 Interrupt #5 Mask Register                         (Default: 0x00)    */
#define REG_INTR5_MASK                              (TX_PAGE_0 | 0x78)
#define BIT_INTR5_MASK7                  (0x80)
#define BIT_INTR5_MASK6                  (0x40)
#define BIT_INTR5_MASK5                  (0x20)
#define BIT_INTR5_MASK4                  (0x10)
#define BIT_INTR5_MASK3                  (0x08)
#define BIT_INTR5_MASK2                  (0x04)
#define BIT_INTR5_MASK1                  (0x02)
#define BIT_INTR5_MASK0                  (0x01)

/* 0x79 Hot Plug Connection Control Register               (Default: 0x45)    */
#define REG_HPD_CTRL                                (TX_PAGE_0 | 0x79)
#define BIT_HPD_CTRL_HPD_DS_SIGNAL                  (0x80)
#define BIT_HPD_CTRL_HPD_OUT_OD_EN                  (0x40)

#define BIT_HPD_CTRL_HPD_OUT_OVR_VAL                (0x20)
#define VAL_HPD_CTRL_HPD_LOW                        (0x00)
#define VAL_HPD_CTRL_HPD_HIGH                       (0x20)

#define BIT_HPD_CTRL_HPD_OUT_OVR_EN                 (0x10)
#define VAL_HPD_CTRL_HPD_OUT_OVR_EN_OFF             (0x00)
#define VAL_HPD_CTRL_HPD_OUT_OVR_EN_ON              (0x10)

#define BIT_HPD_CTRL_GPIO_I_1                       (0x08)
#define BIT_HPD_CTRL_GPIO_OEN_1                     (0x04)
#define BIT_HPD_CTRL_GPIO_I_0                       (0x02)
#define BIT_HPD_CTRL_GPIO_OEN_0                     (0x01)

/* 0x7A GPIO Control Register                              (Default: 0x55)    */
#define REG_GPIO_CTRL                               (TX_PAGE_0 | 0x7A)
#define BIT_CTRL_GPIO_I_5                      (0x80)
#define BIT_CTRL_GPIO_OEN_5                    (0x40)
#define BIT_CTRL_GPIO_I_4                      (0x20)
#define BIT_CTRL_GPIO_OEN_4                    (0x10)
#define BIT_CTRL_GPIO_I_3                      (0x08)
#define BIT_CTRL_GPIO_OEN_3                    (0x04)
#define BIT_CTRL_GPIO_I_2                      (0x02)
#define BIT_CTRL_GPIO_OEN_2                    (0x01)

/* 0x7B Interrupt Source 7 Register                        (Default: 0x00)    */
#define REG_INTR7                                   (TX_PAGE_0 | 0x7B)
#define BIT_INTR7_STAT7                       (0x80)
#define BIT_INTR7_STAT6                       (0x40)
#define BIT_INTR7_STAT4                       (0x10)
#define BIT_INTR7_STAT3                       (0x08)
#define BIT_INTR7_STAT2                       (0x04)
#define BIT_INTR7_STAT1                       (0x02)

/* 0x7C Interrupt Source 8 Register                        (Default: 0x00)    */
#define REG_INTR8                                   (TX_PAGE_0 | 0x7C)
#define BIT_INTR8_STAT6                       (0x40)
#define BIT_INTR8_STAT5                       (0x20)
#define BIT_INTR8_STAT4                       (0x10)
#define BIT_INTR8_STAT3                       (0x08)
#define BIT_INTR8_STAT2                       (0x04)
#define BIT_INTR8_STAT1                       (0x02)
#define BIT_INTR8_STAT0                       (0x01)

/* 0x7D Interrupt #7 Mask Register                         (Default: 0x00)    */
#define REG_INTR7_MASK                              (TX_PAGE_0 | 0x7D)
#define BIT_INTR7_MASK7                  (0x80)
#define BIT_INTR7_MASK6                  (0x40)
#define BIT_INTR7_MASK4                  (0x10)
#define BIT_INTR7_MASK3                  (0x08)
#define BIT_INTR7_MASK2                  (0x04)
#define BIT_INTR7_MASK1                  (0x02)

/* 0x7E Interrupt #8 Mask Register                         (Default: 0x00)    */
#define REG_INTR8_MASK                              (TX_PAGE_0 | 0x7E)
#define BIT_INTR8_MASK6                  (0x40)
#define BIT_INTR8_MASK5                  (0x20)
#define BIT_INTR8_MASK4                  (0x10)
#define BIT_INTR8_MASK3                  (0x08)
#define BIT_INTR8_MASK2                  (0x04)
#define BIT_INTR8_MASK1                  (0x02)
#define BIT_INTR8_MASK0                  (0x01)

/* 0x80 IEEE                                               (Default: 0x10)    */
#define REG_TMDS_CCTRL                              (TX_PAGE_0 | 0x80)
#define BIT_TMDS_CCTRL_TMDS_OE                      (0x10)

/* 0x85 TMDS Control #4 Register                           (Default: 0x02)    */
#define REG_TMDS_CTRL4                              (TX_PAGE_0 | 0x85)
#define BIT_TMDS_CTRL4_SCDT_CKDT_SEL                (0x02)
#define BIT_TMDS_CTRL4_TX_EN_BY_SCDT                (0x01)

/* 0xBB BIST CNTL Register                                 (Default: 0x00)    */
#define REG_BIST_CTRL                               (TX_PAGE_0 | 0xBB)
#define BIT_RXBIST_VGB_EN                 (0x80)
#define BIT_TXBIST_VGB_EN                 (0x40)
#define BIT_BIST_START_SEL                (0x20)
#define BIT_BIST_START_BIT                (0x10)
#define BIT_BIST_ALWAYS_ON                (0x08)
#define BIT_BIST_TRANS                    (0x04)
#define BIT_BIST_RESET                    (0x02)
#define BIT_BIST_EN                       (0x01)

/* 0xBD BIST DURATION0 Register                            (Default: 0x00)    */
#define REG_BIST_TEST_SEL                           (TX_PAGE_0 | 0xBD)
#define MSK_BIST_TEST_SEL_BIST_PATT_SEL             (0x0F)

/* 0xBE BIST VIDEO_MODE Register                           (Default: 0x00)    */
#define REG_BIST_VIDEO_MODE                         (TX_PAGE_0 | 0xBE)
#define MSK_BIST_VIDEO_MODE_BIST_VIDEO_MODE_3_0     (0x0F)

/* 0xBF BIST DURATION0 Register                            (Default: 0x00)    */
#define REG_BIST_DURATION_0                         (TX_PAGE_0 | 0xBF)
#define MSK_BIST_DURATION_0_BIST_DURATION_7_0       (0xFF)

/* 0xC0 BIST DURATION1 Register                            (Default: 0x00)    */
#define REG_BIST_DURATION_1                         (TX_PAGE_0 | 0xC0)
#define MSK_BIST_DURATION_1_BIST_DURATION_15_8      (0xFF)

/* 0xC1 BIST DURATION2 Register                            (Default: 0x00)    */
#define REG_BIST_DURATION_2                         (TX_PAGE_0 | 0xC1)
#define MSK_BIST_DURATION_2_BIST_DURATION_22_16     (0x7F)

/* 0xC2 BIST 8BIT_PATTERN Register                         (Default: 0x00)    */
#define REG_BIST_8BIT_PATTERN                       (TX_PAGE_0 | 0xC2)
#define MSK_BIST_8BIT_PATTERN_BIST_10BIT_PATT28LSB  (0xFF)

/* 0xC7 LM DDC Register                                    (Default: 0x80)    */
#define REG_LM_DDC                                  (TX_PAGE_0 | 0xC7)
#define BIT_LM_DDC_SW_TPI_EN                        (0x80)
#define VAL_LM_DDC_SW_TPI_EN_ENABLED                (0x00)
#define VAL_LM_DDC_SW_TPI_EN_DISABLED               (0x80)

#define BIT_LM_DDC_VIDEO_MUTE_EN                    (0x20)
#define BIT_LM_DDC_DDC_TPI_SW                       (0x04)
#define BIT_LM_DDC_DDC_GRANT                        (0x02)
#define BIT_LM_DDC_DDC_GPU_REQUEST                  (0x01)

/* 0xEC DDC I2C Manual Register                            (Default: 0x03)    */
#define REG_DDC_MANUAL                              (TX_PAGE_0 | 0xEC)
#define BIT_DDC_MANUAL_MAN_DDC                      (0x80)
#define BIT_DDC_MANUAL_VP_SEL                       (0x40)
#define BIT_DDC_MANUAL_DSDA                         (0x20)
#define BIT_DDC_MANUAL_DSCL                         (0x10)
#define BIT_DDC_MANUAL_GCP_HW_CTL_EN                (0x08)
#define BIT_DDC_MANUAL_DDCM_ABORT_WP                (0x04)
#define BIT_DDC_MANUAL_IO_DSDA                      (0x02)
#define BIT_DDC_MANUAL_IO_DSCL                      (0x01)

/* 0xED DDC I2C Target Slave Address Register              (Default: 0x00)    */
#define REG_DDC_ADDR                                (TX_PAGE_0 | 0xED)
#define MSK_DDC_ADDR_DDC_ADDR                       (0xFE)

/* 0xEE DDC I2C Target Segment Address Register            (Default: 0x00)    */
#define REG_DDC_SEGM                                (TX_PAGE_0 | 0xEE)
#define MSK_DDC_SEGM_DDC_SEGMENT                    (0xFF)

/* 0xEF DDC I2C Target Offset Adress Register              (Default: 0x00)    */
#define REG_DDC_OFFSET                              (TX_PAGE_0 | 0xEF)
#define MSK_DDC_OFFSET_DDC_OFFSET                   (0xFF)

/* 0xF0 DDC I2C Data In count #1 Register                  (Default: 0x00)    */
#define REG_DDC_DIN_CNT1                            (TX_PAGE_0 | 0xF0)
#define MSK_DDC_DIN_CNT1_DDC_DIN_CNT_7_0            (0xFF)

/* 0xF1 DDC I2C Data In count #2 Register                  (Default: 0x00)    */
#define REG_DDC_DIN_CNT2                            (TX_PAGE_0 | 0xF1)
#define MSK_DDC_DIN_CNT2_DDC_DIN_CNT_9_8            (0x03)

/* 0xF2 DDC I2C Status Register                            (Default: 0x04)    */
#define REG_DDC_STATUS                              (TX_PAGE_0 | 0xF2)
#define BIT_DDC_STATUS_DDC_BUS_LOW                  (0x40)
#define BIT_DDC_STATUS_DDC_NO_ACK                   (0x20)
#define BIT_DDC_STATUS_DDC_I2C_IN_PROG              (0x10)
#define BIT_DDC_STATUS_DDC_FIFO_FULL                (0x08)
#define BIT_DDC_STATUS_DDC_FIFO_EMPTY               (0x04)
#define BIT_DDC_STATUS_DDC_FIFO_READ_IN_SUE         (0x02)
#define BIT_DDC_STATUS_DDC_FIFO_WRITE_IN_USE        (0x01)

/* 0xF3 DDC I2C Command Register                           (Default: 0x70)    */
#define REG_DDC_CMD                                 (TX_PAGE_0 | 0xF3)
#define BIT_DDC_CMD_HDCP_DDC_EN                     (0x40)
#define BIT_DDC_CMD_SDA_DEL_EN                      (0x20)
#define BIT_DDC_CMD_DDC_FLT_EN                      (0x10)

#define MSK_DDC_CMD_DDC_CMD                         (0x0F)
#define VAL_DDC_CMD_ENH_DDC_READ_NO_ACK             (0x04)
#define VAL_DDC_CMD_DDC_CMD_CLEAR_FIFO              (0x09)
#define VAL_DDC_CMD_DDC_CMD_ABORT                   (0x0F)

/* 0xF4 DDC I2C FIFO Data In/Out Register                  (Default: 0x00)    */
#define REG_DDC_DATA                                (TX_PAGE_0 | 0xF4)
#define MSK_DDC_DATA_DDC_DATA_OUT                   (0xFF)

/* 0xF5 DDC I2C Data Out Counter Register                  (Default: 0x00)    */
#define REG_DDC_DOUT_CNT                            (TX_PAGE_0 | 0xF5)
#define BIT_DDC_DOUT_CNT_DDC_DELAY_CNT_8            (0x80)
#define MSK_DDC_DOUT_CNT_DDC_DATA_OUT_CNT           (0x1F)

/* 0xF6 DDC I2C Delay Count Register                       (Default: 0x14)    */
#define REG_DDC_DELAY_CNT                           (TX_PAGE_0 | 0xF6)
#define MSK_DDC_DELAY_CNT_DDC_DELAY_CNT_7_0         (0xFF)

/* 0xF7 Test Control Register                              (Default: 0x80)    */
#define REG_TEST_TXCTRL                             (TX_PAGE_0 | 0xF7)
#define BIT_TEST_TXCTRL_RCLK_REF_SEL                (0x80)
#define BIT_TEST_TXCTRL_PCLK_REF_SEL                (0x40)
#define MSK_TEST_TXCTRL_BYPASS_PLL_CLK              (0x3C)
#define BIT_TEST_TXCTRL_HDMI_MODE                   (0x02)
#define BIT_TEST_TXCTRL_TST_PLLCK                   (0x01)

/* 0xF8 CBUS Address Register                              (Default: 0x00)    */
#define REG_PAGE_CBUS_ADDR                          (TX_PAGE_0 | 0xF8)
#define MSK_PAGE_CBUS_ADDR_PAGE_CBUS_ADDR_7_0       (0xFF)

/* I2C Device Address re-assignment */
#define REG_PAGE1_ADDR                             (TX_PAGE_0 | 0xFC)
#define REG_PAGE2_ADDR                             (TX_PAGE_0 | 0xFD)
#define REG_PAGE3_ADDR                             (TX_PAGE_0 | 0xFE)
#define REG_HW_TPI_ADDR                            (TX_PAGE_0 | 0xFF)

/* Registers in TX_PAGE_1 (0x00-0xFF)                                         */

/* 0x00 USBT CTRL0 Register                                (Default: 0x00)    */
#define REG_UTSRST                                  (TX_PAGE_1 | 0x00)
#define BIT_UTSRST_FC_SRST                          (0x20)
#define BIT_UTSRST_KEEPER_SRST                      (0x10)
#define BIT_UTSRST_HTX_SRST                         (0x08)
#define BIT_UTSRST_TRX_SRST                         (0x04)
#define BIT_UTSRST_TTX_SRST                         (0x02)
#define BIT_UTSRST_HRX_SRST                         (0x01)

/* 0x04 HSIC RX Control3 Register                          (Default: 0x07)    */
#define REG_HRXCTRL3                                (TX_PAGE_1 | 0x04)
#define MSK_HRXCTRL3_HRX_AFFCTRL                    (0xF0)
#define BIT_HRXCTRL3_HRX_OUT_EN                     (0x04)
#define BIT_HRXCTRL3_STATUS_EN                      (0x02)
#define BIT_HRXCTRL3_HRX_STAY_RESET                 (0x01)

/* HSIC RX INT Registers */
#define REG_HRXINTL                                 (TX_PAGE_1 | 0x11)
#define REG_HRXINTH                                 (TX_PAGE_1 | 0x12)

/* 0x16 TDM TX NUMBITS Register                            (Default: 0x0C)    */
#define REG_TTXNUMB                                 (TX_PAGE_1 | 0x16)
#define MSK_TTXNUMB_TTX_AFFCTRL_3_0                 (0xF0)
#define BIT_TTXNUMB_TTX_COM1_AT_SYNC_WAIT           (0x08)
#define MSK_TTXNUMB_TTX_NUMBPS_2_0                  (0x07)

/* 0x17 TDM TX NUMSPISYM Register                          (Default: 0x04)    */
#define REG_TTXSPINUMS                              (TX_PAGE_1 | 0x17)
#define MSK_TTXSPINUMS_TTX_NUMSPISYM                (0xFF)

/* 0x18 TDM TX NUMHSICSYM Register                         (Default: 0x14)    */
#define REG_TTXHSICNUMS                             (TX_PAGE_1 | 0x18)
#define MSK_TTXHSICNUMS_TTX_NUMHSICSYM              (0xFF)

/* 0x19 TDM TX NUMTOTSYM Register                          (Default: 0x18)    */
#define REG_TTXTOTNUMS                              (TX_PAGE_1 | 0x19)
#define MSK_TTXTOTNUMS_TTX_NUMTOTSYM                (0xFF)

/* 0x36 TDM TX INT Low Register                            (Default: 0x00)    */
#define REG_TTXINTL                                 (TX_PAGE_1 | 0x36)
#define BIT_TTXINTL_TTX_INTR7                       (0x80)
#define BIT_TTXINTL_TTX_INTR6                       (0x40)
#define BIT_TTXINTL_TTX_INTR5                       (0x20)
#define BIT_TTXINTL_TTX_INTR4                       (0x10)
#define BIT_TTXINTL_TTX_INTR3                       (0x08)
#define BIT_TTXINTL_TTX_INTR2                       (0x04)
#define BIT_TTXINTL_TTX_INTR1                       (0x02)
#define BIT_TTXINTL_TTX_INTR0                       (0x01)

/* 0x37 TDM TX INT High Register                           (Default: 0x00)    */
#define REG_TTXINTH                                 (TX_PAGE_1 | 0x37)
#define BIT_TTXINTH_TTX_INTR15                      (0x80)
#define BIT_TTXINTH_TTX_INTR14                      (0x40)
#define BIT_TTXINTH_TTX_INTR13                      (0x20)
#define BIT_TTXINTH_TTX_INTR12                      (0x10)
#define BIT_TTXINTH_TTX_INTR11                      (0x08)
#define BIT_TTXINTH_TTX_INTR10                      (0x04)
#define BIT_TTXINTH_TTX_INTR9                       (0x02)
#define BIT_TTXINTH_TTX_INTR8                       (0x01)

/* 0x3B TDM RX Control Register                            (Default: 0x1C)    */
#define REG_TRXCTRL                                 (TX_PAGE_1 | 0x3B)
#define BIT_TRXCTRL_TRX_CLR_WVALLOW                 (0x10)
#define BIT_TRXCTRL_TRX_FROM_SE_COC                 (0x08)
#define MSK_TRXCTRL_TRX_NUMBPS_2_0                  (0x07)

/* 0x3C TDM RX NUMSPISYM Register                          (Default: 0x04)    */
#define REG_TRXSPINUMS                       (TX_PAGE_1 | 0x3C)

#define MSK_TRXSPINUMS_TRX_NUMSPISYM                (0xFF << 0)

/* 0x3D TDM RX NUMHSICSYM Register                         (Default: 0x14)    */
#define REG_TRXHSICNUMS                      (TX_PAGE_1 | 0x3D)

#define MSK_TRXHSICNUMS_TRX_NUMHSICSYM              (0xFF << 0)

/* 0x3E TDM RX NUMTOTSYM Register                          (Default: 0x18)    */
#define REG_TRXTOTNUMS                       (TX_PAGE_1 | 0x3E)

#define MSK_TRXTOTNUMS_TRX_NUMTOTSYM                (0xFF << 0)


/* 0x5C TDM RX Status 2nd Register                         (Default: 0x00)    */
#define REG_TRXSTA2                                 (TX_PAGE_1 | 0x5C)
#define MSK_TRXSTA2_TRX_STATUS_15_8                 (0xFF)

/* 0x63 TDM RX INT Low Register                            (Default: 0x00)    */
#define REG_TRXINTL                                 (TX_PAGE_1 | 0x63)
#define BIT_TRXINTL_TRX_INTR7                       (0x80)
#define BIT_TRXINTL_TRX_INTR6                       (0x40)
#define BIT_TRXINTL_TRX_INTR5                       (0x20)
#define BIT_TRXINTL_TRX_INTR4                       (0x10)
#define BIT_TRXINTL_TRX_INTR3                       (0x08)
#define BIT_TRXINTL_TRX_INTR2                       (0x04)
#define BIT_TRXINTL_TRX_INTR1                       (0x02)
#define BIT_TRXINTL_TRX_INTR0                       (0x01)

/* 0x64 TDM RX INT High Register                           (Default: 0x00)    */
#define REG_TRXINTH                                 (TX_PAGE_1 | 0x64)
#define BIT_TRXINTH_TRX_INTR15                      (0x80)
#define BIT_TRXINTH_TRX_INTR14                      (0x40)
#define BIT_TRXINTH_TRX_INTR13                      (0x20)
#define BIT_TRXINTH_TRX_INTR12                      (0x10)
#define BIT_TRXINTH_TRX_INTR11                      (0x08)
#define BIT_TRXINTH_TRX_INTR10                      (0x04)
#define BIT_TRXINTH_TRX_INTR9                       (0x02)
#define BIT_TRXINTH_TRX_INTR8                       (0x01)

/* 0x66 TDM RX INTMASK High Register                       (Default: 0x00)    */
#define REG_TRXINTMH                                (TX_PAGE_1 | 0x66)
#define BIT_TRXINTMH_TRX_INTRMASK15                 (0x80)
#define BIT_TRXINTMH_TRX_INTRMASK14                 (0x40)
#define BIT_TRXINTMH_TRX_INTRMASK13                 (0x20)
#define BIT_TRXINTMH_TRX_INTRMASK12                 (0x10)
#define BIT_TRXINTMH_TRX_INTRMASK11                 (0x08)
#define BIT_TRXINTMH_TRX_INTRMASK10                 (0x04)
#define BIT_TRXINTMH_TRX_INTRMASK9                  (0x02)
#define BIT_TRXINTMH_TRX_INTRMASK8                  (0x01)

/* 0x69 HSIC TX CRTL Register                              (Default: 0x00)    */
#define REG_HTXCTRL                                 (TX_PAGE_1 | 0x69)
#define BIT_HTXCTRL_HTX_ALLSBE_SOP                  (0x10)
#define BIT_HTXCTRL_HTX_RGDINV_USB                  (0x08)
#define BIT_HTXCTRL_HTX_RSPTDM_BUSY                 (0x04)
#define BIT_HTXCTRL_HTX_DRVCONN1                    (0x02)
#define BIT_HTXCTRL_HTX_DRVRST1                     (0x01)

/* 0x7D HSIC TX INT Low Register                           (Default: 0x00)    */
#define REG_HTXINTL                                 (TX_PAGE_1 | 0x7D)
#define BIT_HTXINTL_HTX_INTR7                       (0x80)
#define BIT_HTXINTL_HTX_INTR6                       (0x40)
#define BIT_HTXINTL_HTX_INTR5                       (0x20)
#define BIT_HTXINTL_HTX_INTR4                       (0x10)
#define BIT_HTXINTL_HTX_INTR3                       (0x08)
#define BIT_HTXINTL_HTX_INTR2                       (0x04)
#define BIT_HTXINTL_HTX_INTR1                       (0x02)
#define BIT_HTXINTL_HTX_INTR0                       (0x01)

/* 0x7E HSIC TX INT High Register                          (Default: 0x00)    */
#define REG_HTXINTH                                 (TX_PAGE_1 | 0x7E)
#define BIT_HTXINTH_HTX_INTR15                      (0x80)
#define BIT_HTXINTH_HTX_INTR14                      (0x40)
#define BIT_HTXINTH_HTX_INTR13                      (0x20)
#define BIT_HTXINTH_HTX_INTR12                      (0x10)
#define BIT_HTXINTH_HTX_INTR11                      (0x08)
#define BIT_HTXINTH_HTX_INTR10                      (0x04)
#define BIT_HTXINTH_HTX_INTR9                       (0x02)
#define BIT_HTXINTH_HTX_INTR8                       (0x01)

/* 0x81 HSIC Keeper Register                               (Default: 0x00)    */
#define REG_KEEPER                                  (TX_PAGE_1 | 0x81)
#define MSK_KEEPER_KEEPER_MODE_1_0                  (0x03)

/* 0x83 HSIC Flow Control General Register                 (Default: 0x02)    */
#define REG_FCGC                                    (TX_PAGE_1 | 0x83)
#define BIT_FCGC_HSIC_FC_HOSTMODE                   (0x02)
#define BIT_FCGC_HSIC_FC_ENABLE                     (0x01)

/*----------------------------------------------------------------------------*/
/* 0x91 HSIC Flow Control CTR13 Register                   (Default: 0xFC)    */
#define REG_FCCTR13                                 (TX_PAGE_1 | 0x91)

#define MSK_FCCTR13_HFC_CONF13                      (0xFF)

/*----------------------------------------------------------------------------*/
/* 0x92 HSIC Flow Control CTR14 Register                   (Default: 0xFF)    */
#define REG_FCCTR14                                 (TX_PAGE_1 | 0x92)

#define MSK_FCCTR14_HFC_CONF14                      (0xFF)

/*----------------------------------------------------------------------------*/
/* 0x93 HSIC Flow Control CTR15 Register                   (Default: 0xFF)    */
#define REG_FCCTR15                                 (TX_PAGE_1 | 0x93)

#define MSK_FCCTR15_HFC_CONF15                      (0xFF)

/* 0xB6 HSIC Flow Control CTR50 Register                   (Default: 0x03)    */
#define REG_FCCTR50                                 (TX_PAGE_1 | 0xB6)

#define MSK_FCCTR50_HFC_CONF50                      (0xFF)




/* 0xEC HSIC Flow Control INTR0 Register                   (Default: 0x00)    */
#define REG_FCINTR0                                 (TX_PAGE_1 | 0xEC)
#define REG_FCINTR1                                 (TX_PAGE_1 | 0xED)
#define REG_FCINTR2                                 (TX_PAGE_1 | 0xEE)
#define REG_FCINTR3                                 (TX_PAGE_1 | 0xEF)
#define REG_FCINTR4                                 (TX_PAGE_1 | 0xF0)
#define REG_FCINTR5                                 (TX_PAGE_1 | 0xF1)
#define REG_FCINTR6                                 (TX_PAGE_1 | 0xF2)
#define REG_FCINTR7                                 (TX_PAGE_1 | 0xF3)

/* 0xFC TDM Low Latency Register                           (Default: 0x20)    */
#define REG_TDMLLCTL                                (TX_PAGE_1 | 0xFC)
#define MSK_TDMLLCTL_TRX_LL_SEL_MANUAL              (0xC0)
#define MSK_TDMLLCTL_TRX_LL_SEL_MODE                (0x30)
#define MSK_TDMLLCTL_TTX_LL_SEL_MANUAL              (0x0C)
#define BIT_TDMLLCTL_TTX_LL_TIE_LOW                 (0x02)
#define BIT_TDMLLCTL_TTX_LL_SEL_MODE                (0x01)

/* Registers in TX_PAGE_2 (0x00-0xFF)                                         */

/* 0x10 TMDS 0 Clock Control Register 1                    (Default: 0x10)    */
#define REG_TMDS0_CCTRL1                            (TX_PAGE_2 | 0x10)
#define MSK_TMDS0_CCTRL1_TEST_SEL                   (0xC0)
#define MSK_TMDS0_CCTRL1_CLK1X_CTL                  (0x30)

/* 0x11 TMDS Clock Enable Register                         (Default: 0x00)    */
#define REG_TMDS_CLK_EN                             (TX_PAGE_2 | 0x11)
#define BIT_TMDS_CLK_EN_CLK_EN                      (0x01)

/* 0x12 TMDS Channel Enable Register                       (Default: 0x00)    */
#define REG_TMDS_CH_EN                              (TX_PAGE_2 | 0x12)
#define BIT_TMDS_CH_EN_CH0_EN                       (0x10)
#define BIT_TMDS_CH_EN_CH12_EN                      (0x01)

/* 0x15 BGR_BIAS Register                                  (Default: 0x07)    */
#define REG_BGR_BIAS                                (TX_PAGE_2 | 0x15)
#define BIT_BGR_BIAS_BGR_EN                         (0x80)
#define MSK_BGR_BIAS_BIAS_BGR_D                     (0x0F)

/* 0x31 TMDS 0 Digital I2C BW Register                     (Default: 0x0A)    */
#define REG_ALICE0_BW_I2C                           (TX_PAGE_2 | 0x31)
#define MSK_ALICE0_BW_I2C_TMDS0_BW_I2C_7_0          (0xFF)

/* 0x4C TMDS 0 Digital Zone Control Register               (Default: 0xE0)    */
#define REG_ALICE0_ZONE_CTRL                        (TX_PAGE_2 | 0x4C)
#define BIT_ALICE0_ZONE_CTRL_ICRST_N                (0x80)
#define BIT_ALICE0_ZONE_CTRL_USE_INT_DIV20          (0x40)
#define MSK_ALICE0_ZONE_CTRL_SZONE_I2C              (0x30)
#define MSK_ALICE0_ZONE_CTRL_ZONE_CTRL              (0x0F)

/* 0x4D TMDS 0 Digital PLL Mode Control Register           (Default: 0x00)    */
#define REG_ALICE0_MODE_CTRL                        (TX_PAGE_2 | 0x4D)
#define MSK_ALICE0_MODE_CTRL_PLL_MODE_I2C           (0x0C)
#define MSK_ALICE0_MODE_CTRL_DIV20_CTRL             (0x03)

/* 0x85 MHL Tx Control 6th Register                        (Default: 0xA0)    */
#define REG_MHLTX_CTL6                              (TX_PAGE_2 | 0x85)
#define MSK_MHLTX_CTL6_EMI_SEL                      (0xE0)
#define MSK_MHLTX_CTL6_TX_CLK_SHAPE_9_8             (0x03)

/* 0x90 Packet Filter0 Register                            (Default: 0x00)    */
#define REG_PKT_FILTER_0                            (TX_PAGE_2 | 0x90)
#define BIT_PKT_FILTER_0_DROP_CEA_GAMUT_PKT         (0x80)
#define BIT_PKT_FILTER_0_DROP_CEA_CP_PKT            (0x40)
#define BIT_PKT_FILTER_0_DROP_MPEG_PKT              (0x20)
#define BIT_PKT_FILTER_0_DROP_SPIF_PKT              (0x10)
#define BIT_PKT_FILTER_0_DROP_AIF_PKT               (0x08)
#define BIT_PKT_FILTER_0_DROP_AVI_PKT               (0x04)
#define BIT_PKT_FILTER_0_DROP_CTS_PKT               (0x02)
#define BIT_PKT_FILTER_0_DROP_GCP_PKT               (0x01)

/* 0x91 Packet Filter1 Register                            (Default: 0x00)    */
#define REG_PKT_FILTER_1                            (TX_PAGE_2 | 0x91)
#define BIT_PKT_FILTER_1_VSI_OVERRIDE_DIS           (0x80)
#define BIT_PKT_FILTER_1_AVI_OVERRIDE_DIS           (0x40)
#define BIT_PKT_FILTER_1_DROP_AUDIO_PKT             (0x08)
#define BIT_PKT_FILTER_1_DROP_GEN2_PKT              (0x04)
#define BIT_PKT_FILTER_1_DROP_GEN_PKT               (0x02)
#define BIT_PKT_FILTER_1_DROP_VSIF_PKT              (0x01)

/* 0xA0 TMDS Clock Status Register                         (Default: 0x10)    */
#define REG_TMDS_CSTAT_P3                           (TX_PAGE_2 | 0xA0)
#define BIT_TMDS_CSTAT_P3_RX_HDMI_CP_CLR_MUTE       (0x80)
#define BIT_TMDS_CSTAT_P3_RX_HDMI_CP_SET_MUTE       (0x40)
#define BIT_TMDS_CSTAT_P3_RX_HDMI_CP_NEW_CP         (0x20)
#define BIT_TMDS_CSTAT_P3_CLR_AVI                   (0x08)
#define BIT_TMDS_CSTAT_P3_SCDT_CLR_AVI_DIS          (0x04)
#define BIT_TMDS_CSTAT_P3_SCDT                      (0x02)

#define BIT_TMDS_CSTAT_P3_CKDT                      (0x01)
#define VAL_TMDS_CSTAT_P3_CKDT_STOPPED              (0x00)
#define VAL_TMDS_CSTAT_P3_CKDT_DETECTED             (0x01)

/* 0xA1 RX_HDMI Control Register0                          (Default: 0x10)    */
#define REG_RX_HDMI_CTRL0                           (TX_PAGE_2 | 0xA1)
#define BIT_RX_HDMI_CTRL0_BYP_DVIFILT_SYNC          (0x20)
#define BIT_RX_HDMI_CTRL0_HDMI_MODE_EN_ITSELF_CLR   (0x10)
#define BIT_RX_HDMI_CTRL0_HDMI_MODE_SW_VALUE        (0x08)
#define BIT_RX_HDMI_CTRL0_HDMI_MODE_OVERWRITE       (0x04)
#define BIT_RX_HDMI_CTRL0_RX_HDMI_HDMI_MODE_EN      (0x02)
#define BIT_RX_HDMI_CTRL0_RX_HDMI_HDMI_MODE         (0x01)

/* 0xA3 RX_HDMI Control Register2                          (Default: 0x38)    */
#define REG_RX_HDMI_CTRL2                           (TX_PAGE_2 | 0xA3)
#define MSK_RX_HDMI_CTRL2_IDLE_CNT                  (0xF0)
#define BIT_RX_HDMI_CTRL2_USE_AV_MUTE               (0x08)
#define VAL_RX_HDMI_CTRL2_USE_AV_MUTE_DISABLE       (0x00)
#define VAL_RX_HDMI_CTRL2_USE_AV_MUTE_ENABLE        (0x08)

#define BIT_RX_HDMI_CTRL2_VSI_MON_SEL               (0x01)
#define VAL_RX_HDMI_CTRL2_VSI_MON_SEL_AVI           (0x00)
#define VAL_RX_HDMI_CTRL2_VSI_MON_SEL_VSI           (0x01)

/* 0xA4 RX_HDMI Control Register3                          (Default: 0x0F)    */
#define REG_RX_HDMI_CTRL3                           (TX_PAGE_2 | 0xA4)
#define MSK_RX_HDMI_CTRL3_PP_MODE_CLK_EN            (0x0F)

/* 0xAC rx_hdmi Clear Buffer Register                      (Default: 0x00)    */
#define REG_RX_HDMI_CLR_BUFFER                      (TX_PAGE_2 | 0xAC)
#define MSK_RX_HDMI_CLR_BUFFER_AIF4VSI_CMP          (0xC0)
#define BIT_RX_HDMI_CLR_BUFFER_USE_AIF4VSI          (0x20)
#define BIT_RX_HDMI_CLR_BUFFER_VSI_CLR_W_AVI        (0x10)
#define BIT_RX_HDMI_CLR_BUFFER_VSI_IEEE_ID_CHK_EN   (0x08)
#define BIT_RX_HDMI_CLR_BUFFER_SWAP_VSI_IEEE_ID     (0x04)
#define BIT_RX_HDMI_CLR_BUFFER_AIF_CLR_EN           (0x02)

#define BIT_RX_HDMI_CLR_BUFFER_VSI_CLR_EN           (0x01)
#define VAL_RX_HDMI_CLR_BUFFER_VSI_CLR_EN_STALE     (0x00)
#define VAL_RX_HDMI_CLR_BUFFER_VSI_CLR_EN_CLEAR     (0x01)

/* 0xB8 RX_HDMI VSI Header1 Register                       (Default: 0x00)    */
#define REG_RX_HDMI_MON_PKT_HEADER1                 (TX_PAGE_2 | 0xB8)
#define MSK_RX_HDMI_MON_PKT_HEADER1_RX_HDMI_MON_PKT_HEADER_7_0 (0xFF)

/* 0xD7 RX_HDMI VSI MHL Monitor Register                   (Default: 0x3C)    */
#define REG_RX_HDMI_VSIF_MHL_MON                    (TX_PAGE_2 | 0xD7)

#define MSK_RX_HDMI_VSIF_MHL_MON_RX_HDMI_MHL_3D_FORMAT (0x3C)
#define MSK_RX_HDMI_VSIF_MHL_MON_RX_HDMI_MHL_VID_FORMAT (0x03)

/* 0xE0 Interrupt Source 9 Register                        (Default: 0x00)    */
#define REG_INTR9                                   (TX_PAGE_2 | 0xE0)
#define BIT_INTR9_STAT6                       (0x40)
#define BIT_INTR9_STAT5                       (0x20)
#define BIT_INTR9_STAT4                       (0x10)
#define BIT_INTR9_STAT1                       (0x02)
#define BIT_INTR9_STAT0                       (0x01)

/* 0xE1 Interrupt 9 Mask Register                          (Default: 0x00)    */
#define REG_INTR9_MASK                              (TX_PAGE_2 | 0xE1)
#define BIT_INTR9_MASK6                  (0x40)
#define BIT_INTR9_MASK5                  (0x20)
#define BIT_INTR9_MASK4                  (0x10)
#define BIT_INTR9_MASK1                  (0x02)
#define BIT_INTR9_MASK0                  (0x01)

/* 0xE2 TPI CBUS Start Register                            (Default: 0x00)    */
#define REG_TPI_CBUS_START                          (TX_PAGE_2 | 0xE2)
#define BIT_TPI_CBUS_START_RCP_REQ_START            (0x80)
#define BIT_TPI_CBUS_START_RCPK_REPLY_START         (0x40)
#define BIT_TPI_CBUS_START_RCPE_REPLY_START         (0x20)
#define BIT_TPI_CBUS_START_PUT_LINK_MODE_START      (0x10)
#define BIT_TPI_CBUS_START_PUT_DCAPCHG_START        (0x08)
#define BIT_TPI_CBUS_START_PUT_DCAPRDY_START        (0x04)
#define BIT_TPI_CBUS_START_GET_EDID_START_0         (0x02)
#define BIT_TPI_CBUS_START_GET_DEVCAP_START         (0x01)

/* 0xE3 EDID Control Register                              (Default: 0x10)    */
#define REG_EDID_CTRL                               (TX_PAGE_2 | 0xE3)
#define BIT_EDID_CTRL_EDID_PRIME_VALID              (0x80)
#define VAL_EDID_CTRL_EDID_PRIME_VALID_DISABLE      (0x00)
#define VAL_EDID_CTRL_EDID_PRIME_VALID_ENABLE       (0x80)

#define BIT_EDID_CTRL_XDEVCAP_EN                    (0x40)
#define BIT_EDID_CTRL_DEVCAP_SEL                    (0x20)
#define VAL_EDID_CTRL_DEVCAP_SELECT_EDID            (0x00)
#define VAL_EDID_CTRL_DEVCAP_SELECT_DEVCAP          (0x20)

#define BIT_EDID_CTRL_EDID_FIFO_ADDR_AUTO           (0x10)
#define VAL_EDID_CTRL_EDID_FIFO_ADDR_AUTO_DISABLE   (0x00)
#define VAL_EDID_CTRL_EDID_FIFO_ADDR_AUTO_ENABLE    (0x10)

#define BIT_EDID_CTRL_EDID_FIFO_ACCESS_ALWAYS_EN    (0x08)
#define BIT_EDID_CTRL_EDID_FIFO_BLOCK_SEL           (0x04)
#define BIT_EDID_CTRL_INVALID_BKSV                  (0x02)

#define BIT_EDID_CTRL_EDID_MODE_EN                  (0x01)
#define VAL_EDID_CTRL_EDID_MODE_EN_DISABLE          (0x00)
#define VAL_EDID_CTRL_EDID_MODE_EN_ENABLE           (0x01)

/* 0xE9 EDID FIFO Addr Register                            (Default: 0x00)    */
#define REG_EDID_FIFO_ADDR                          (TX_PAGE_2 | 0xE9)
#define MSK_EDID_FIFO_ADDR_EDID_FIFO_ADDR           (0xFF)

/* 0xEA EDID FIFO Write Data Register                      (Default: 0x00)    */
#define REG_EDID_FIFO_WR_DATA                       (TX_PAGE_2 | 0xEA)
#define MSK_EDID_FIFO_WR_DATA_EDID_FIFO_WR_DATA     (0xFF)

/* 0xEB EDID/DEVCAP FIFO Internal Addr Register            (Default: 0x00)    */
#define REG_EDID_FIFO_ADDR_MON                      (TX_PAGE_2 | 0xEB)
#define MSK_EDID_FIFO_ADDR_MON_EDID_FIFO_ADDR_MON   (0xFF)

/* 0xEC EDID FIFO Read Data Register                       (Default: 0x00)    */
#define REG_EDID_FIFO_RD_DATA                       (TX_PAGE_2 | 0xEC)
#define MSK_EDID_FIFO_RD_DATA_EDID_FIFO_RD_DATA     (0xFF)

/* 0xED EDID DDC Segment Pointer Register                  (Default: 0x00)    */
#define REG_EDID_START_EXT                          (TX_PAGE_2 | 0xED)

/* 0xF2 TX IP BIST CNTL and Status Register                (Default: 0x00)    */
#define REG_TX_IP_BIST_CNTLSTA                      (TX_PAGE_2 | 0xF2)
#define BIT_TX_IP_BIST_CNTLSTA_TXBIST_QUARTER_CLK_SEL (0x40)
#define BIT_TX_IP_BIST_CNTLSTA_TXBIST_DONE          (0x20)
#define BIT_TX_IP_BIST_CNTLSTA_TXBIST_ON            (0x10)
#define BIT_TX_IP_BIST_CNTLSTA_TXBIST_RUN           (0x08)
#define BIT_TX_IP_BIST_CNTLSTA_TXCLK_HALF_SEL       (0x04)
#define BIT_TX_IP_BIST_CNTLSTA_TXBIST_EN            (0x02)
#define BIT_TX_IP_BIST_CNTLSTA_TXBIST_SEL           (0x01)

/* 0xF3 TX IP BIST INST LOW Register                       (Default: 0x00)    */
#define REG_TX_IP_BIST_INST_LOW                     (TX_PAGE_2 | 0xF3)
#define REG_TX_IP_BIST_INST_HIGH                    (TX_PAGE_2 | 0xF4)

/* 0xF5 TX IP BIST PATTERN LOW Register                    (Default: 0x00)    */
#define REG_TX_IP_BIST_PAT_LOW                      (TX_PAGE_2 | 0xF5)
#define REG_TX_IP_BIST_PAT_HIGH                     (TX_PAGE_2 | 0xF6)

/* 0xF7 TX IP BIST CONFIGURE LOW Register                  (Default: 0x00)    */
#define REG_TX_IP_BIST_CONF_LOW                     (TX_PAGE_2 | 0xF7)
#define REG_TX_IP_BIST_CONF_HIGH                    (TX_PAGE_2 | 0xF8)

/* Registers in TX_PAGE_3 (0x00-0xFF)                                         */

/* 0x00 E-MSC General Control Register                     (Default: 0x80)    */
#define REG_GENCTL                                  (TX_PAGE_3 | 0x00)
#define BIT_GENCTL_SPEC_TRANS_DIS                   (0x80)
#define BIT_GENCTL_DIS_XMIT_ERR_STATE               (0x40)
#define BIT_GENCTL_SPI_MISO_EDGE                    (0x20)
#define BIT_GENCTL_SPI_MOSI_EDGE                    (0x10)
#define BIT_GENCTL_CLR_EMSC_RFIFO                   (0x08)
#define BIT_GENCTL_CLR_EMSC_XFIFO                   (0x04)
#define BIT_GENCTL_START_TRAIN_SEQ                  (0x02)
#define BIT_GENCTL_EMSC_EN                          (0x01)

/* 0x05 E-MSC Comma ErrorCNT Register                      (Default: 0x03)    */
#define REG_COMMECNT                                (TX_PAGE_3 | 0x05)
#define BIT_COMMECNT_I2C_TO_EMSC_EN                 (0x80)
#define MSK_COMMECNT_COMMA_CHAR_ERR_CNT             (0x0F)

/* 0x1A E-MSC RFIFO ByteCnt Register            (Default: 0x00)    */
#define REG_EMSCRFIFOBCNTL                          (TX_PAGE_3 | 0x1A)
#define REG_EMSCRFIFOBCNTH                          (TX_PAGE_3 | 0x1B)

/* 0x1E SPI Burst Cnt Status Register                      (Default: 0x00)    */
#define REG_SPIBURSTCNT                             (TX_PAGE_3 | 0x1E)
#define MSK_SPIBURSTCNT_SPI_BURST_CNT               (0xFF)

/* 0x22 SPI Burst Status and SWRST Register                (Default: 0x00)    */
#define REG_SPIBURSTSTAT                            (TX_PAGE_3 | 0x22)
#define BIT_SPIBURSTSTAT_SPI_HDCPRST                (0x80)
#define BIT_SPIBURSTSTAT_SPI_CBUSRST                (0x40)
#define BIT_SPIBURSTSTAT_SPI_SRST                   (0x20)
#define BIT_SPIBURSTSTAT_EMSC_NORMAL_MODE           (0x01)

/* 0x23 E-MSC 1st Interrupt Register                       (Default: 0x00)    */
#define REG_EMSCINTR                                (TX_PAGE_3 | 0x23)
#define BIT_EMSCINTR_EMSC_XFIFO_EMPTY               (0x80)
#define BIT_EMSCINTR_EMSC_XMIT_ACK_TOUT             (0x40)
#define BIT_EMSCINTR_EMSC_RFIFO_READ_ERR            (0x20)
#define BIT_EMSCINTR_EMSC_XFIFO_WRITE_ERR           (0x10)
#define BIT_EMSCINTR_EMSC_COMMA_CHAR_ERR            (0x08)
#define BIT_EMSCINTR_EMSC_XMIT_DONE                 (0x04)
#define BIT_EMSCINTR_EMSC_XMIT_GNT_TOUT             (0x02)
#define BIT_EMSCINTR_SPI_DVLD                       (0x01)

/* 0x24 E-MSC Interrupt Mask Register                      (Default: 0x00)    */
#define REG_EMSCINTRMASK                            (TX_PAGE_3 | 0x24)
#define BIT_EMSCINTRMASK_EMSC_INTRMASK0_7           (0x80)
#define BIT_EMSCINTRMASK_EMSC_INTRMASK0_6           (0x40)
#define BIT_EMSCINTRMASK_EMSC_INTRMASK0_5           (0x20)
#define BIT_EMSCINTRMASK_EMSC_INTRMASK0_4           (0x10)
#define BIT_EMSCINTRMASK_EMSC_INTRMASK0_3           (0x08)
#define BIT_EMSCINTRMASK_EMSC_INTRMASK0_2           (0x04)
#define BIT_EMSCINTRMASK_EMSC_INTRMASK0_1           (0x02)
#define BIT_EMSCINTRMASK_EMSC_INTRMASK0_0           (0x01)

/* 0x2A I2C E-MSC XMIT FIFO Write Port Register            (Default: 0x00)    */
#define REG_EMSC_XMIT_WRITE_PORT                    (TX_PAGE_3 | 0x2A)

/* 0x2B I2C E-MSC RCV FIFO Write Port Register             (Default: 0x00)    */
#define REG_EMSC_RCV_READ_PORT                      (TX_PAGE_3 | 0x2B)

/* 0x2C E-MSC 2nd Interrupt Register                       (Default: 0x00)    */
#define REG_EMSCINTR1                               (TX_PAGE_3 | 0x2C)
#define BIT_EMSCINTR1_EMSC_TRAINING_COMMA_ERR       (0x01)

/* 0x2D E-MSC Interrupt Mask Register                      (Default: 0x00)    */
#define REG_EMSCINTRMASK1                           (TX_PAGE_3 | 0x2D)
#define BIT_EMSCINTRMASK1_EMSC_INTRMASK1_0          (0x01)

/* 0x30 MHL Top Ctl Register                               (Default: 0x00)    */
#define REG_MHL_TOP_CTL                             (TX_PAGE_3 | 0x30)
#define BIT_MHL_TOP_CTL_MHL3_DOC_SEL                (0x80)
#define BIT_MHL_TOP_CTL_MHL_PP_SEL                  (0x40)
#define MSK_MHL_TOP_CTL_IF_TIMING_CTL               (0x03)

/* 0x31 MHL DataPath 1st Ctl Register                      (Default: 0xBC)    */
#define REG_MHL_DP_CTL0                             (TX_PAGE_3 | 0x31)
#define BIT_MHL_DP_CTL0_DP_OE                       (0x80)
#define BIT_MHL_DP_CTL0_TX_OE_OVR                   (0x40)
#define MSK_MHL_DP_CTL0_TX_OE                       (0x3F)

/* 0x32 MHL DataPath 2nd Ctl Register                      (Default: 0xBB)    */
#define REG_MHL_DP_CTL1                             (TX_PAGE_3 | 0x32)
#define MSK_MHL_DP_CTL1_CK_SWING_CTL                (0xF0)
#define MSK_MHL_DP_CTL1_DT_SWING_CTL                (0x0F)

/* 0x33 MHL DataPath 3rd Ctl Register                      (Default: 0x2F)    */
#define REG_MHL_DP_CTL2                             (TX_PAGE_3 | 0x33)
#define BIT_MHL_DP_CTL2_CLK_BYPASS_EN               (0x80)
#define MSK_MHL_DP_CTL2_DAMP_TERM_SEL               (0x30)
#define MSK_MHL_DP_CTL2_CK_TERM_SEL                 (0x0C)
#define MSK_MHL_DP_CTL2_DT_TERM_SEL                 (0x03)

/* 0x34 MHL DataPath 4th Ctl Register                      (Default: 0x48)    */
#define REG_MHL_DP_CTL3                             (TX_PAGE_3 | 0x34)
#define MSK_MHL_DP_CTL3_DT_DRV_VNBC_CTL             (0xF0)
#define MSK_MHL_DP_CTL3_DT_DRV_VNB_CTL              (0x0F)

/* 0x35 MHL DataPath 5th Ctl Register                      (Default: 0x48)    */
#define REG_MHL_DP_CTL4                             (TX_PAGE_3 | 0x35)
#define MSK_MHL_DP_CTL4_CK_DRV_VNBC_CTL             (0xF0)
#define MSK_MHL_DP_CTL4_CK_DRV_VNB_CTL              (0x0F)

/* 0x36 MHL DataPath 6th Ctl Register                      (Default: 0x3F)    */
#define REG_MHL_DP_CTL5                             (TX_PAGE_3 | 0x36)
#define BIT_MHL_DP_CTL5_RSEN_EN_OVR                 (0x80)
#define BIT_MHL_DP_CTL5_RSEN_EN                     (0x40)
#define MSK_MHL_DP_CTL5_DAMP_TERM_VGS_CTL           (0x30)
#define MSK_MHL_DP_CTL5_CK_TERM_VGS_CTL             (0x0C)
#define MSK_MHL_DP_CTL5_DT_TERM_VGS_CTL             (0x03)

/* 0x37 MHL PLL 1st Ctl Register                           (Default: 0x05)    */
#define REG_MHL_PLL_CTL0                            (TX_PAGE_3 | 0x37)
#define BIT_MHL_PLL_CTL0_AUD_CLK_EN                 (0x80)

#define MSK_MHL_PLL_CTL0_AUD_CLK_RATIO              (0x70)
#define VAL_MHL_PLL_CTL0_AUD_CLK_RATIO_5_10         (0x70)
#define VAL_MHL_PLL_CTL0_AUD_CLK_RATIO_5_6          (0x60)
#define VAL_MHL_PLL_CTL0_AUD_CLK_RATIO_5_4          (0x50)
/*TODO:#define VAL_MHL_PLL_CTL0_AUD_CLK_RATIO_5_2          (0x40)*/
#define VAL_MHL_PLL_CTL0_AUD_CLK_RATIO_5_5          (0x30)
#define VAL_MHL_PLL_CTL0_AUD_CLK_RATIO_5_3          (0x20)
#define VAL_MHL_PLL_CTL0_AUD_CLK_RATIO_5_2_PRIME    (0x10)
#define VAL_MHL_PLL_CTL0_AUD_CLK_RATIO_5_1          (0x00)

#define MSK_MHL_PLL_CTL0_HDMI_CLK_RATIO             (0x0C)
#define VAL_MHL_PLL_CTL0_HDMI_CLK_RATIO_4X          (0x0C)
#define VAL_MHL_PLL_CTL0_HDMI_CLK_RATIO_2X          (0x08)
#define VAL_MHL_PLL_CTL0_HDMI_CLK_RATIO_1X          (0x04)
#define VAL_MHL_PLL_CTL0_HDMI_CLK_RATIO_HALF_X      (0x00)

#define BIT_MHL_PLL_CTL0_CRYSTAL_CLK_SEL            (0x02)
#define BIT_MHL_PLL_CTL0_ZONE_MASK_OE               (0x01)

/* 0x39 MHL PLL 3rd Ctl Register                           (Default: 0x80)    */
#define REG_MHL_PLL_CTL2                            (TX_PAGE_3 | 0x39)
#define BIT_MHL_PLL_CTL2_CLKDETECT_EN               (0x80)
#define BIT_MHL_PLL_CTL2_MEAS_FVCO                  (0x08)
#define BIT_MHL_PLL_CTL2_PLL_FAST_LOCK              (0x04)
#define MSK_MHL_PLL_CTL2_PLL_LF_SEL                 (0x03)

/* 0x40 MHL CBUS 1st Ctl Register                          (Default: 0x12)    */
#define REG_MHL_CBUS_CTL0                           (TX_PAGE_3 | 0x40)
#define BIT_MHL_CBUS_CTL0_CBUS_RGND_TEST_MODE       (0x80)

#define MSK_MHL_CBUS_CTL0_CBUS_RGND_VTH_CTL         (0x30)
#define VAL_MHL_CBUS_CTL0_CBUS_RGND_VBIAS_734       (0x00)
#define VAL_MHL_CBUS_CTL0_CBUS_RGND_VBIAS_747       (0x10)
#define VAL_MHL_CBUS_CTL0_CBUS_RGND_VBIAS_740       (0x20)
#define VAL_MHL_CBUS_CTL0_CBUS_RGND_VBIAS_754       (0x30)

#define MSK_MHL_CBUS_CTL0_CBUS_RES_TEST_SEL         (0x0C)

#define MSK_MHL_CBUS_CTL0_CBUS_DRV_SEL              (0x03)
#define VAL_MHL_CBUS_CTL0_CBUS_DRV_SEL_WEAKEST      (0x00)
#define VAL_MHL_CBUS_CTL0_CBUS_DRV_SEL_WEAK         (0x01)
#define VAL_MHL_CBUS_CTL0_CBUS_DRV_SEL_STRONG       (0x02)
#define VAL_MHL_CBUS_CTL0_CBUS_DRV_SEL_STRONGEST    (0x03)

/* 0x41 MHL CBUS 2nd Ctl Register                          (Default: 0x03)    */
#define REG_MHL_CBUS_CTL1                           (TX_PAGE_3 | 0x41)
#define MSK_MHL_CBUS_CTL1_CBUS_RGND_RES_CTL         (0x07)
#define VAL_MHL_CBUS_CTL1_0888_OHM                  (0x00)
#define VAL_MHL_CBUS_CTL1_1115_OHM                  (0x04)
#define VAL_MHL_CBUS_CTL1_1378_OHM                  (0x07)

/* 0x42 MHL CoC 1st Ctl Register                           (Default: 0xC3)    */
#define REG_MHL_COC_CTL0                            (TX_PAGE_3 | 0x42)
#define BIT_MHL_COC_CTL0_COC_BIAS_EN                (0x80)
#define MSK_MHL_COC_CTL0_COC_BIAS_CTL               (0x70)
#define MSK_MHL_COC_CTL0_COC_TERM_CTL               (0x07)

/* 0x43 MHL CoC 2nd Ctl Register                           (Default: 0x87)    */
#define REG_MHL_COC_CTL1                            (TX_PAGE_3 | 0x43)
#define BIT_MHL_COC_CTL1_COC_EN                     (0x80)
#define MSK_MHL_COC_CTL1_COC_DRV_CTL                (0x3F)

/* 0x45 MHL CoC 4th Ctl Register                           (Default: 0x00)    */
#define REG_MHL_COC_CTL3                            (TX_PAGE_3 | 0x45)
#define BIT_MHL_COC_CTL3_COC_AECHO_EN               (0x01)

/* 0x46 MHL CoC 5th Ctl Register                           (Default: 0x28)    */
#define REG_MHL_COC_CTL4                            (TX_PAGE_3 | 0x46)
#define MSK_MHL_COC_CTL4_COC_IF_CTL                 (0xF0)
#define MSK_MHL_COC_CTL4_COC_SLEW_CTL               (0x0F)

/* 0x47 MHL CoC 6th Ctl Register                           (Default: 0x0D)    */
#define REG_MHL_COC_CTL5                            (TX_PAGE_3 | 0x47)
#define MSK_MHL_COC_CTL5_COC_RSV_7_0                (0xFF)

/* 0x49 MHL DoC 1st Ctl Register                           (Default: 0x18)    */
#define REG_MHL_DOC_CTL0                            (TX_PAGE_3 | 0x49)
#define BIT_MHL_DOC_CTL0_DOC_RXDATA_EN              (0x80)
#define MSK_MHL_DOC_CTL0_DOC_DM_TERM                (0x38)
#define MSK_MHL_DOC_CTL0_DOC_OPMODE                 (0x06)
#define BIT_MHL_DOC_CTL0_DOC_RXBIAS_EN              (0x01)

/* 0x50 MHL DataPath 7th Ctl Register                      (Default: 0x2A)    */
#define REG_MHL_DP_CTL6                             (TX_PAGE_3 | 0x50)
#define BIT_MHL_DP_CTL6_DP_TAP2_SGN                 (0x20)
#define BIT_MHL_DP_CTL6_DP_TAP2_EN                  (0x10)
#define BIT_MHL_DP_CTL6_DP_TAP1_SGN                 (0x08)
#define BIT_MHL_DP_CTL6_DP_TAP1_EN                  (0x04)
#define BIT_MHL_DP_CTL6_DT_PREDRV_FEEDCAP_EN        (0x02)
#define BIT_MHL_DP_CTL6_DP_PRE_POST_SEL             (0x01)

/* 0x51 MHL DataPath 8th Ctl Register                      (Default: 0x06)    */
#define REG_MHL_DP_CTL7                             (TX_PAGE_3 | 0x51)
#define MSK_MHL_DP_CTL7_DT_DRV_VBIAS_CASCTL         (0xF0)
#define MSK_MHL_DP_CTL7_DT_DRV_IREF_CTL             (0x0F)

/* 0x61 Tx Zone Ctl1 Register                              (Default: 0x00)    */
#define REG_TX_ZONE_CTL1                            (TX_PAGE_3 | 0x61)
#define MSK_TX_ZONE_CTL1_TX_ZONE_CTRL               (0xFF)
#define VAL_TX_ZONE_CTL1_TX_ZONE_CTRL_MODE          (0x08)

/* 0x64 MHL3 Tx Zone Ctl Register                          (Default: 0x00)    */
#define REG_MHL3_TX_ZONE_CTL                        (TX_PAGE_3 | 0x64)
#define BIT_MHL3_TX_ZONE_CTL_MHL2_INTPLT_ZONE_MANU_EN (0x80)
#define MSK_MHL3_TX_ZONE_CTL_MHL3_TX_ZONE           (0x03)

#define MSK_TX_ZONE_CTL3_TX_ZONE                    (0x03)
#define VAL_TX_ZONE_CTL3_TX_ZONE_6GBPS              (0x00)
#define VAL_TX_ZONE_CTL3_TX_ZONE_3GBPS              (0x01)
#define VAL_TX_ZONE_CTL3_TX_ZONE_1_5GBPS            (0x02)

/* 0x91 HDCP Polling Control and Status Register           (Default: 0x70)    */
#define REG_HDCP2X_POLL_CS                          (TX_PAGE_3 | 0x91)

#define BIT_HDCP2X_POLL_CS_HDCP2X_MSG_SZ_CLR_OPTION (0x40)
#define BIT_HDCP2X_POLL_CS_HDCP2X_RPT_READY_CLR_OPTION (0x20)
#define BIT_HDCP2X_POLL_CS_HDCP2X_REAUTH_REQ_CLR_OPTION (0x10)
#define MSK_HDCP2X_POLL_CS_                         (0x0C)
#define BIT_HDCP2X_POLL_CS_HDCP2X_DIS_POLL_GNT      (0x02)
#define BIT_HDCP2X_POLL_CS_HDCP2X_DIS_POLL_EN       (0x01)

/* 0x98 HDCP Interrupt 0 Register                          (Default: 0x00)    */
#define REG_HDCP2X_INTR0                            (TX_PAGE_3 | 0x98)
#define BIT_HDCP2X_INTR0_HDCP2X_INTR0_STAT7         (0x80)
#define BIT_HDCP2X_INTR0_HDCP2X_INTR0_STAT6         (0x40)
#define BIT_HDCP2X_INTR0_HDCP2X_INTR0_STAT5         (0x20)
#define BIT_HDCP2X_INTR0_HDCP2X_INTR0_STAT4         (0x10)
#define BIT_HDCP2X_INTR0_HDCP2X_INTR0_STAT3         (0x08)
#define BIT_HDCP2X_INTR0_HDCP2X_INTR0_STAT2         (0x04)
#define BIT_HDCP2X_INTR0_HDCP2X_INTR0_STAT1         (0x02)
#define BIT_HDCP2X_INTR0_HDCP2X_INTR0_STAT0         (0x01)

/* 0x99 HDCP Interrupt 0 Mask Register                     (Default: 0x00)    */
#define REG_HDCP2X_INTR0_MASK                       (TX_PAGE_3 | 0x99)
#define BIT_HDCP2X_INTR0_MASK_HDCP2X_INTR0_MASK7    (0x80)
#define BIT_HDCP2X_INTR0_MASK_HDCP2X_INTR0_MASK6    (0x40)
#define BIT_HDCP2X_INTR0_MASK_HDCP2X_INTR0_MASK5    (0x20)
#define BIT_HDCP2X_INTR0_MASK_HDCP2X_INTR0_MASK4    (0x10)
#define BIT_HDCP2X_INTR0_MASK_HDCP2X_INTR0_MASK3    (0x08)
#define BIT_HDCP2X_INTR0_MASK_HDCP2X_INTR0_MASK2    (0x04)
#define BIT_HDCP2X_INTR0_MASK_HDCP2X_INTR0_MASK1    (0x02)
#define BIT_HDCP2X_INTR0_MASK_HDCP2X_INTR0_MASK0    (0x01)

/* 0xA0 HDCP General Control 0 Register                    (Default: 0x02)    */
#define REG_HDCP2X_CTRL_0                           (TX_PAGE_3 | 0xA0)
#define BIT_HDCP2X_CTRL_0_HDCP2X_ENCRYPT_EN         (0x80)
#define BIT_HDCP2X_CTRL_0_HDCP2X_POLINT_SEL         (0x40)
#define BIT_HDCP2X_CTRL_0_HDCP2X_POLINT_OVR         (0x20)
#define BIT_HDCP2X_CTRL_0_HDCP2X_PRECOMPUTE         (0x10)
#define BIT_HDCP2X_CTRL_0_HDCP2X_HDMIMODE           (0x08)
#define BIT_HDCP2X_CTRL_0_HDCP2X_REPEATER           (0x04)
#define BIT_HDCP2X_CTRL_0_HDCP2X_HDCPTX             (0x02)
#define BIT_HDCP2X_CTRL_0_HDCP2X_EN                 (0x01)

/* 0xA1 HDCP General Control 1 Register                    (Default: 0x08)    */
#define REG_HDCP2X_CTRL_1                           (TX_PAGE_3 | 0xA1)
#define MSK_HDCP2X_CTRL_1_HDCP2X_REAUTH_MSK_3_0     (0xF0)
#define BIT_HDCP2X_CTRL_1_HDCP2X_HPD_SW             (0x08)
#define BIT_HDCP2X_CTRL_1_HDCP2X_HPD_OVR            (0x04)
#define BIT_HDCP2X_CTRL_1_HDCP2X_CTL3MSK            (0x02)
#define BIT_HDCP2X_CTRL_1_HDCP2X_REAUTH_SW          (0x01)

/* 0xA5 HDCP Misc Control Register                         (Default: 0x00)    */
#define REG_HDCP2X_MISC_CTRL                        (TX_PAGE_3 | 0xA5)
#define BIT_HDCP2X_MISC_CTRL_HDCP2X_RPT_SMNG_XFER_START (0x10)
#define BIT_HDCP2X_MISC_CTRL_HDCP2X_RPT_SMNG_WR_START (0x08)
#define BIT_HDCP2X_MISC_CTRL_HDCP2X_RPT_SMNG_WR     (0x04)
#define BIT_HDCP2X_MISC_CTRL_HDCP2X_RPT_RCVID_RD_START (0x02)
#define BIT_HDCP2X_MISC_CTRL_HDCP2X_RPT_RCVID_RD    (0x01)

/* 0xA6 HDCP RPT SMNG K Register                           (Default: 0x00)    */
#define REG_HDCP2X_RPT_SMNG_K                       (TX_PAGE_3 | 0xA6)
#define MSK_HDCP2X_RPT_SMNG_K_HDCP2X_RPT_SMNG_K_7_0 (0xFF)

/* 0xA7 HDCP RPT SMNG In Register                          (Default: 0x00)    */
#define REG_HDCP2X_RPT_SMNG_IN                      (TX_PAGE_3 | 0xA7)
#define MSK_HDCP2X_RPT_SMNG_IN_HDCP2X_RPT_SMNG_IN   (0xFF)

/* 0xAA HDCP Auth Status Register                          (Default: 0x00)    */
#define REG_HDCP2X_AUTH_STAT                        (TX_PAGE_3 | 0xAA)
#define MSK_HDCP2X_AUTH_STAT_HDCP2X_AUTH_STAT_7_0   (0xFF)

/* 0xAC HDCP RPT RCVID Out Register                        (Default: 0x00)    */
#define REG_HDCP2X_RPT_RCVID_OUT                    (TX_PAGE_3 | 0xAC)
#define MSK_HDCP2X_RPT_RCVID_OUT_HDCP2X_RPT_RCVID_OUT_7_0 (0xFF)

/* 0xB4 HDCP TP1  Register                                 (Default: 0x62)    */
#define REG_HDCP2X_TP1                              (TX_PAGE_3 | 0xB4)
#define MSK_HDCP2X_TP1_HDCP2X_TP1_7_0               (0xFF)

/* 0xC7 HDCP GP Out 0 Register                             (Default: 0x00)    */
#define REG_HDCP2X_GP_OUT0                          (TX_PAGE_3 | 0xC7)
#define MSK_HDCP2X_GP_OUT0_HDCP2X_GP_OUT0_7_0       (0xFF)

/* 0xD1 HDCP Repeater RCVR ID 0 Register                   (Default: 0x00)    */
#define REG_HDCP2X_RPT_RCVR_ID0                     (TX_PAGE_3 | 0xD1)
#define MSK_HDCP2X_RPT_RCVR_ID0_HDCP2X_RCVR_ID_7_0  (0xFF)

/* 0xD8 HDCP DDCM Status Register                          (Default: 0x00)    */
#define REG_HDCP2X_DDCM_STS                         (TX_PAGE_3 | 0xD8)
#define MSK_HDCP2X_DDCM_STS_HDCP2X_DDCM_ERR_STS_3_0 (0xF0)
#define MSK_HDCP2X_DDCM_STS_HDCP2X_DDCM_CTL_CS_3_0  (0x0F)

/* 0xE0 HDMI2MHL3 Control Register                         (Default: 0x0A)    */
#define REG_M3_CTRL                                 (TX_PAGE_3 | 0xE0)
#define BIT_M3_CTRL_H2M_SWRST                       (0x10)
#define BIT_M3_CTRL_SW_MHL3_SEL                     (0x08)
#define BIT_M3_CTRL_M3AV_EN                         (0x04)
#define BIT_M3_CTRL_ENC_TMDS                        (0x02)
#define BIT_M3_CTRL_MHL3_MASTER_EN                  (0x01)

#define VAL_M3_CTRL_MHL3_VALUE (BIT_M3_CTRL_SW_MHL3_SEL \
			| BIT_M3_CTRL_M3AV_EN \
			| BIT_M3_CTRL_ENC_TMDS \
			| BIT_M3_CTRL_MHL3_MASTER_EN)

#define VAL_M3_CTRL_PEER_VERSION_PENDING_VALUE \
	VAL_M3_CTRL_MHL3_VALUE

/* 0xE1 HDMI2MHL3 Port0 Control Register                   (Default: 0x04)    */
#define REG_M3_P0CTRL                               (TX_PAGE_3 | 0xE1)
#define BIT_M3_P0CTRL_MHL3_P0_HDCP_ENC_EN           (0x              (TX_PCont)
#define BIT_MAGE_3 | 0x2A)

/* 0x     #define BIT_TX_IPRL_M3AV_EN        GE_3 | 0x2A)

/* 0x     #_ON	_IPR_M3AV_EN        GE_3 | 0x2A)

/* 0x     #_OFF	_IP0RL_MHL3_P0_HDCP_ENC_EN           (0x  )
#define BIT_M3_CTRL_
RL_MHL3_P0_HDCP_ENC_EN         PIX   (0x01)

/* RegisHL_CBUS_CTL0_CBUS_DRP_ENC_EN         PIX   (0x0   (0x0RegisHL_CBUS_CTL1_1115_OHP_ENC_EN         PIX   (0x0 PACKEDRegisHL_CBUSRL_MHL3_P0_HDCP_ENC_EN         POR0x80)
#define BIT_MHL_CTRL_MHL3_VAL            3_M	)
#define BIT_M3_P0P_CTL1_CK_SWING_       _RRP_DEC0x0_M	)
#din TX_TL1_CK_SWING_       _           _IDM	)
#din 2nd Ctl RE6trol RegistScram                   (Default: 0x04)  /
#define REG1M3_P0CTRL              S                 (TX_PAGE_3 | 0xE0)
define BIT_M3_P0OC_CTL4_COC_IF_C  S    _M3_CTRR_LENG        (TX_PAGE_3 DCP2X_CTRL_1_HDCP2XC  S    _M3_CTRCRAMBL   (0x01)

#define  RPT SMNG K RFCTR14 RDiv                        (Default: 0x00)    /
#define REG_MHL_PLL_CTL0        DIV      A    (TX_PAGE_3 | 0xA7)
#0xE0)
define BIT_M3_PFP_CTL1_CK_SWINGDIV      A       DIV      A    (TX_PAGE__PCoC_CTL1_CK_SWINGDIV      A   FB DIV      A    (TX_PAGE_3CBUS 2nd Ct_3 (0x00-0xFF)       4sHL_CB                           */

/* 0x00 E-MSC General Controster apability                 (Default: 0x00)    */
#define REG_HDCP2X_RPT_RCVR_ID0 REF_C               (TX_PAGE_3 | 0x98)
#define BI4_GENCTL_SPEC_TRANRV_IREF_C        REF_C               (TX_PAG DDCM Status 2rosterister                     (Default: 0x00)    **/
#define REG_HDCP2X_RPT_RCVR_ID0 REF_0)
#              (TX_PAGE_1 | 0xEC)define BI4_GENCD_CTRRegisterDevice            b           (Default: 0x00)    **/
#define REG_HDCP2X_RPT_RCVR_ID0 REF_* 0xA              (TX_PAGE_1 | 0xECdefine BI4_GENCDP_CTL5_CK_TERM_VGS_* 0xA  REF_* 0xA              (TX_PADoC 1st Ctl Re0       cratch Pad                 (Default: 0x00)   /
#define REG_HDCP2X_RPT_RCVR_ID0 REF_*CRPAD          (TX_PAGE_3 | 0x98)
#define BI4_GENCDP_CTL5_DAMP_TERM_VG*CRPAD   REF_*CRPAD          (TX_PAGDoC 1st Ctl R8rosterExtendedr apability                 (Default: 0/
#define REG_HDCP2X_RPT_RCVR_ID0 REF_EXTC               (TX_PAGE_3 | 0x98define BI4_GENCDOC_CTL0_DOC_DM_TERMEXTC        REF_EXTC               (DoC 1st Ctl R9erDevice Extendedr           b           (Default: 0x/
#define REG_HDCP2X_RPT_RCVR_ID0 R