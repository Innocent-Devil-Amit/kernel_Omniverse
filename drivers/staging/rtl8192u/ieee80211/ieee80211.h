/*
 * Merged with mainline ieee80211.h in Aug 2004.  Original ieee802_11
 * remains copyright by the original authors
 *
 * Portions of the merged code are based on Host AP (software wireless
 * LAN access point) driver for Intersil Prism2/2.5/3.
 *
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <jkmaline@cc.hut.fi>
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 *
 * Adaption to a generic IEEE 802.11 stack by James Ketrenos
 * <jketreno@linux.intel.com>
 * Copyright (c) 2004, Intel Corporation
 *
 * Modified for Realtek's wi-fi cards by Andrea Merello
 * <andrea.merello@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 */
#ifndef IEEE80211_H
#define IEEE80211_H
#include <linux/if_ether.h> /* ETH_ALEN */
#include <linux/kernel.h>   /* ARRAY_SIZE */
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/interrupt.h>

#include <linux/delay.h>
#include <linux/wireless.h>

#include "rtl819x_HT.h"
#include "rtl819x_BA.h"
#include "rtl819x_TS.h"


#ifndef IW_MODE_MONITOR
#define IW_MODE_MONITOR 6
#endif

#ifndef IWEVCUSTOM
#define IWEVCUSTOM 0x8c02
#endif


#ifndef container_of
/**
 * container_of - cast a member of a structure out to the containing structure
 *
 * @ptr:        the pointer to the member.
 * @type:       the type of the container struct this is embedded in.
 * @member:     the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({                      \
	const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#define KEY_TYPE_NA		0x0
#define KEY_TYPE_WEP40		0x1
#define KEY_TYPE_TKIP		0x2
#define KEY_TYPE_CCMP		0x4
#define KEY_TYPE_WEP104		0x5

/* added for rtl819x tx procedure */
#define MAX_QUEUE_SIZE		0x10

//
// 8190 queue mapping
//
#define BK_QUEUE                               0
#define BE_QUEUE                               1
#define VI_QUEUE                               2
#define VO_QUEUE                               3
#define HCCA_QUEUE                             4
#define TXCMD_QUEUE                            5
#define MGNT_QUEUE                             6
#define HIGH_QUEUE                             7
#define BEACON_QUEUE                           8

#define LOW_QUEUE                              BE_QUEUE
#define NORMAL_QUEUE                           MGNT_QUEUE

//added by amy for ps
#define SWRF_TIMEOUT				50

//added by amy for LEAP related
#define IE_CISCO_FLAG_POSITION		0x08	// Flag byte: byte 8, numbered from 0.
#define SUPPORT_CKIP_MIC			0x08	// bit3
#define SUPPORT_CKIP_PK			0x10	// bit4
/* defined for skb cb field */
/* At most 28 byte */
typedef struct cb_desc {
	/* Tx Desc Related flags (8-9) */
	u8 bLastIniPkt:1;
	u8 bCmdOrInit:1;
	u8 bFirstSeg:1;
	u8 bLastSeg:1;
	u8 bEncrypt:1;
	u8 bTxDisableRateFallBack:1;
	u8 bTxUseDriverAssingedRate:1;
	u8 bHwSec:1; //indicate whether use Hw security. WB

	u8 reserved1;

	/* Tx Firmware Relaged flags (10-11)*/
	u8 bCTSEnable:1;
	u8 bRTSEnable:1;
	u8 bUseShortGI:1;
	u8 bUseShortPreamble:1;
	u8 bTxEnableFwCalcDur:1;
	u8 bAMPDUEnable:1;
	u8 bRTSSTBC:1;
	u8 RTSSC:1;

	u8 bRTSBW:1;
	u8 bPacketBW:1;
	u8 bRTSUseShortPreamble:1;
	u8 bRTSUseShortGI:1;
	u8 bMulticast:1;
	u8 bBroadcast:1;
	//u8 reserved2:2;
	u8 drv_agg_enable:1;
	u8 reserved2:1;

	/* Tx Desc related element(12-19) */
	u8 rata_index;
	u8 queue_index;
	//u8 reserved3;
	//u8 reserved4;
	u16 txbuf_size;
	//u8 reserved5;
	u8 RATRIndex;
	u8 reserved6;
	u8 reserved7;
	u8 reserved8;

	/* Tx firmware related element(20-27) */
	u8 data_rate;
	u8 rts_rate;
	u8 ampdu_factor;
	u8 ampdu_density;
	//u8 reserved9;
	//u8 reserved10;
	//u8 reserved11;
	u8 DrvAggrNum;
	u16 pkt_size;
	u8 reserved12;
}cb_desc, *pcb_desc;

/*--------------------------Define -------------------------------------------*/
#define MGN_1M                  0x02
#define MGN_2M                  0x04
#define MGN_5_5M                0x0b
#define MGN_11M                 0x16

#define MGN_6M                  0x0c
#define MGN_9M                  0x12
#define MGN_12M                 0x18
#define MGN_18M                 0x24
#define MGN_24M                 0x30
#define MGN_36M                 0x48
#define MGN_48M                 0x60
#define MGN_54M                 0x6c

#define MGN_MCS0                0x80
#define MGN_MCS1                0x81
#define MGN_MCS2                0x82
#define MGN_MCS3                0x83
#define MGN_MCS4                0x84
#define MGN_MCS5                0x85
#define MGN_MCS6                0x86
#define MGN_MCS7                0x87
#define MGN_MCS8                0x88
#define MGN_MCS9                0x89
#define MGN_MCS10               0x8a
#define MGN_MCS11               0x8b
#define MGN_MCS12               0x8c
#define MGN_MCS13               0x8d
#define MGN_MCS14               0x8e
#define MGN_MCS15               0x8f

//----------------------------------------------------------------------------
//		802.11 Management frame Reason Code field
//----------------------------------------------------------------------------
enum	_ReasonCode{
	unspec_reason	= 0x1,
	auth_not_valid	= 0x2,
	deauth_lv_ss	= 0x3,
	inactivity		= 0x4,
	ap_overload	= 0x5,
	class2_err		= 0x6,
	class3_err		= 0x7,
	disas_lv_ss	= 0x8,
	asoc_not_auth	= 0x9,

	//----MIC_CHECK
	mic_failure	= 0xe,
	//----END MIC_CHECK

	// Reason code defined in 802.11i D10.0 p.28.
	invalid_IE		= 0x0d,
	four_way_tmout	= 0x0f,
	two_way_tmout	= 0x10,
	IE_dismatch	= 0x11,
	invalid_Gcipher = 0x12,
	invalid_Pcipher = 0x13,
	invalid_AKMP	= 0x14,
	unsup_RSNIEver = 0x15,
	invalid_RSNIE	= 0x16,
	auth_802_1x_fail= 0x17,
	ciper_reject		= 0x18,

	// Reason code defined in 7.3.1.7, 802.1e D13.0, p.42. Added by Annie, 2005-11-15.
	QoS_unspec		= 0x20, // 32
	QAP_bandwidth	= 0x21, // 33
	poor_condition	= 0x22, // 34
	no_facility	= 0x23, // 35
							// Where is 36???
	req_declined	= 0x25, // 37
	invalid_param	= 0x26, // 38
	req_not_honored= 0x27,	// 39
	TS_not_created	= 0x2F, // 47
	DL_not_allowed	= 0x30, // 48
	dest_not_exist	= 0x31, // 49
	dest_not_QSTA	= 0x32, // 50
};



#define aSifsTime ((priv->ieee80211->current_network.mode == IEEE_A || \
		    priv->ieee80211->current_network.mode == IEEE_N_24G || \
		    priv->ieee80211->current_network.mode == IEEE_N_5G) ? \
		   16 : 10)

#define MGMT_QUEUE_NUM 5

#define IEEE_CMD_SET_WPA_PARAM			1
#define	IEEE_CMD_SET_WPA_IE			2
#define IEEE_CMD_SET_ENCRYPTION			3
#define IEEE_CMD_MLME				4

#define IEEE_PARAM_WPA_ENABLED			1
#define IEEE_PARAM_TKIP_COUNTERMEASURES		2
#define IEEE_PARAM_DROP_UNENCRYPTED		3
#define IEEE_PARAM_PRIVACY_INVOKED		4
#define IEEE_PARAM_AUTH_ALGS			5
#define IEEE_PARAM_IEEE_802_1X			6
//It should consistent with the driver_XXX.c
//   David, 2006.9.26
#define IEEE_PARAM_WPAX_SELECT			7
//Added for notify the encryption type selection
//   David, 2006.9.26
#define IEEE_PROTO_WPA				1
#define IEEE_PROTO_RSN				2
//Added for notify the encryption type selection
//   David, 2006.9.26
#define IEEE_WPAX_USEGROUP			0
#define IEEE_WPAX_WEP40				1
#define IEEE_WPAX_TKIP				2
#define IEEE_WPAX_WRAP				3
#define IEEE_WPAX_CCMP				4
#define IEEE_WPAX_WEP104			5

#define IEEE_KEY_MGMT_IEEE8021X			1
#define IEEE_KEY_MGMT_PSK			2

#define IEEE_MLME_STA_DEAUTH			1
#define IEEE_MLME_STA_DISASSOC			2


#define IEEE_CRYPT_ERR_UNKNOWN_ALG		2
#define IEEE_CRYPT_ERR_UNKNOWN_ADDR		3
#define IEEE_CRYPT_ERR_CRYPT_INIT_FAILED	4
#define IEEE_CRYPT_ERR_KEY_SET_FAILED		5
#define IEEE_CRYPT_ERR_TX_KEY_SET_FAILED	6
#define IEEE_CRYPT_ERR_CARD_CONF_FAILED		7


#define	IEEE_CRYPT_ALG_NAME_LEN			16

#define MAX_IE_LEN  0xff

// added for kernel conflict
#define ieee80211_crypt_deinit_entries	ieee80211_crypt_deinit_entries_rsl
#define ieee80211_crypt_deinit_handler	ieee80211_crypt_deinit_handler_rsl
#define ieee80211_crypt_delayed_deinit	ieee80211_crypt_delayed_deinit_rsl
#define ieee80211_register_crypto_ops	ieee80211_register_crypto_ops_rsl
#define ieee80211_unregister_crypto_ops ieee80211_unregister_crypto_ops_rsl
#define ieee80211_get_crypto_ops	ieee80211_get_crypto_ops_rsl

#define ieee80211_ccmp_null		ieee80211_ccmp_null_rsl

#define ieee80211_tkip_null		ieee80211_tkip_null_rsl

#define ieee80211_wep_null		ieee80211_wep_null_rsl

#define free_ieee80211			free_ieee80211_rsl
#define alloc_ieee80211			alloc_ieee80211_rsl

#define ieee80211_rx			ieee80211_rx_rsl
#define ieee80211_rx_mgt		ieee80211_rx_mgt_rsl

#define ieee80211_get_beacon		ieee80211_get_beacon_rsl
#define ieee80211_wake_queue		ieee80211_wake_queue_rsl
#define ieee80211_stop_queue		ieee80211_stop_queue_rsl
#define ieee80211_reset_queue		ieee80211_reset_queue_rsl
#define ieee80211_softmac_stop_protocol	ieee80211_softmac_stop_protocol_rsl
#define ieee80211_softmac_start_protocol ieee80211_softmac_start_protocol_rsl
#define ieee80211_is_shortslot		ieee80211_is_shortslot_rsl
#define ieee80211_is_54g		ieee80211_is_54g_rsl
#define ieee80211_wpa_supplicant_ioctl	ieee80211_wpa_supplicant_ioctl_rsl
#define ieee80211_ps_tx_ack		ieee80211_ps_tx_ack_rsl
#define ieee80211_softmac_xmit		ieee80211_softmac_xmit_rsl
#define ieee80211_stop_send_beacons	ieee80211_stop_send_beacons_rsl
#define notify_wx_assoc_event		notify_wx_assoc_event_rsl
#define SendDisassociation		SendDisassociation_rsl
#define ieee80211_disassociate		ieee80211_disassociate_rsl
#define ieee80211_start_send_beacons	ieee80211_start_send_beacons_rsl
#define ieee80211_stop_scan		ieee80211_stop_scan_rsl
#define ieee80211_send_probe_requests	ieee80211_send_probe_requests_rsl
#define ieee80211_softmac_scan_syncro	ieee80211_softmac_scan_syncro_rsl
#define ieee80211_start_scan_syncro	ieee80211_start_scan_syncro_rsl

#define ieee80211_wx_get_essid		ieee80211_wx_get_essid_rsl
#define ieee80211_wx_set_essid		ieee80211_wx_set_essid_rsl
#define ieee80211_wx_set_rate		ieee80211_wx_set_rate_rsl
#define ieee80211_wx_get_rate		ieee80211_wx_get_rate_rsl
#define ieee80211_wx_set_wap		ieee80211_wx_set_wap_rsl
#define ieee80211_wx_get_wap		ieee80211_wx_get_wap_rsl
#define ieee80211_wx_set_mode		ieee80211_wx_set_mode_rsl
#define ieee80211_wx_get_mode		ieee80211_wx_get_mode_rsl
#define ieee80211_wx_set_scan		ieee80211_wx_set_scan_rsl
#define ieee80211_wx_get_freq		ieee80211_wx_get_freq_rsl
#define ieee80211_wx_set_freq		ieee80211_wx_set_freq_rsl
#define ieee80211_wx_set_rawtx		ieee80211_wx_set_rawtx_rsl
#define ieee80211_wx_get_name		ieee80211_wx_get_name_rsl
#define ieee80211_wx_set_power		ieee80211_wx_set_power_rsl
#define ieee80211_wx_get_power		ieee80211_wx_get_power_rsl
#define ieee80211_wlan_frequencies	ieee80211_wlan_frequencies_rsl
#define ieee80211_wx_set_rts		ieee80211_wx_set_rts_rsl
#define ieee80211_wx_get_rts		ieee80211_wx_get_rts_rsl

#define ieee80211_txb_free		ieee80211_txb_free_rsl

#define ieee80211_wx_set_gen_ie		ieee80211_wx_set_gen_ie_rsl
#define ieee80211_wx_get_scan		ieee80211_wx_get_scan_rsl
#define ieee80211_wx_set_encode		ieee80211_wx_set_encode_rsl
#define ieee80211_wx_get_encode		ieee80211_wx_get_encode_rsl
#define ieee80211_wx_set_mlme		ieee80211_wx_set_mlme_rsl
#define ieee80211_wx_set_auth		ieee80211_wx_set_auth_rsl
#define ieee80211_wx_set_encode_ext	ieee80211_wx_set_encode_ext_rsl
#define ieee80211_wx_get_encode_ext	ieee80211_wx_get_encode_ext_rsl


typedef struct ieee_param {
	u32 cmd;
	u8 sta_addr[ETH_ALEN];
	union {
		struct {
			u8 name;
			u32 value;
		} wpa_param;
		struct {
			u32 len;
			u8 reserved[32];
			u8 data[0];
		} wpa_ie;
		struct{
			int command;
			int reason_code;
		} mlme;
		struct {
			u8 alg[IEEE_CRYPT_ALG_NAME_LEN];
			u8 set_tx;
			u32 err;
			u8 idx;
			u8 seq[8]; /* sequence counter (set: RX, get: TX) */
			u16 key_len;
			u8 key[0];
		} crypt;
	} u;
}ieee_param;


// linux under 2.6.9 release may not support it, so modify it for common use
#define MSECS(t) msecs_to_jiffies(t)
#define msleep_interruptible_rsl  msleep_interruptible

#define IEEE80211_DATA_LEN		2304
/* Maximum size for the MA-UNITDATA primitive, 802.11 standard section
   6.2.1.1.2.

   The figure in section 7.1.2 suggests a body size of up to 2312
   bytes is allowed, which is a bit confusing, I suspect this
   represents the 2304 bytes of real data, plus a possible 8 bytes of
   WEP IV and ICV. (this interpretation suggested by Ramiro Barreiro) */
#define IEEE80211_1ADDR_LEN 10
#define IEEE80211_2ADDR_LEN 16
#define IEEE80211_3ADDR_LEN 24
#define IEEE80211_4ADDR_LEN 30
#define IEEE80211_FCS_LEN    4
#define IEEE80211_HLEN                  (IEEE80211_4ADDR_LEN)
#define IEEE80211_FRAME_LEN             (IEEE80211_DATA_LEN + IEEE80211_HLEN)
#define IEEE80211_MGMT_HDR_LEN 24
#define IEEE80211_DATA_HDR3_LEN 24
#define IEEE80211_DATA_HDR4_LEN 30

#define MIN_FRAG_THRESHOLD     256U
#define MAX_FRAG_THRESHOLD     2346U


/* Frame control field constants */
#define IEEE80211_FCTL_VERS		0x0003
#define IEEE80211_FCTL_FTYPE		0x000c
#define IEEE80211_FCTL_STYPE		0x00f0
#define IEEE80211_FCTL_FRAMETYPE	0x00fc
#define IEEE80211_FCTL_TODS		0x0100
#define IEEE80211_FCTL_FROMDS		0x0200
#define IEEE80211_FCTL_DSTODS		0x0300 //added by david
#define IEEE80211_FCTL_MOREFRAGS	0x0400
#define IEEE80211_FCTL_RETRY		0x0800
#define IEEE80211_FCTL_PM		0x1000
#define IEEE80211_FCTL_MOREDATA		0x2000
#define IEEE80211_FCTL_WEP		0x4000
#define IEEE80211_FCTL_ORDER		0x8000

#define IEEE80211_FTYPE_MGMT		0x0000
#define IEEE80211_FTYPE_CTL		0x0004
#define IEEE80211_FTYPE_DATA		0x0008

/* management */
#define IEEE80211_STYPE_ASSOC_REQ	0x0000
#define IEEE80211_STYPE_ASSOC_RESP	0x0010
#define IEEE80211_STYPE_REASSOC_REQ	0x0020
#define IEEE80211_STYPE_REASSOC_RESP	0x0030
#define IEEE80211_STYPE_PROBE_REQ	0x0040
#define IEEE80211_STYPE_PROBE_RESP	0x0050
#define IEEE80211_STYPE_BEACON		0x0080
#define IEEE80211_STYPE_ATIM		0x0090
#define IEEE80211_STYPE_DISASSOC	0x00A0
#define IEEE80211_STYPE_AUTH		0x00B0
#define IEEE80211_STYPE_DEAUTH		0x00C0
#define IEEE80211_STYPE_MANAGE_ACT	0x00D0

/* control */
#define IEEE80211_STYPE_PSPOLL		0x00A0
#define IEEE80211_STYPE_RTS		0x00B0
#define IEEE80211_STYPE_CTS		0x00C0
#define IEEE80211_STYPE_ACK		0x00D0
#define IEEE80211_STYPE_CFEND		0x00E0
#define IEEE80211_STYPE_CFENDACK	0x00F0
#define IEEE80211_STYPE_BLOCKACK   0x0094

/* data */
#define IEEE80211_STYPE_DATA		0x0000
#define IEEE80211_STYPE_DATA_CFACK	0x0010
#define IEEE80211_STYPE_DATA_CFPOLL	0x0020
#define IEEE80211_STYPE_DATA_CFACKPOLL	0x0030
#define IEEE80211_STYPE_NULLFUNC	0x0040
#define IEEE80211_STYPE_CFACK		0x0050
#define IEEE80211_STYPE_CFPOLL		0x0060
#define IEEE80211_STYPE_CFACKPOLL	0x0070
#define IEEE80211_STYPE_QOS_DATA	0x0080 //added for WMM 2006/8/2
#define IEEE80211_STYPE_QOS_NULL	0x00C0

#define IEEE80211_SCTL_FRAG		0x000F
#define IEEE80211_SCTL_SEQ		0xFFF0

/* QOS control */
#define IEEE80211_QCTL_TID              0x000F

#define	FC_QOS_BIT					BIT7
#define IsDataFrame(pdu)			( ((pdu[0] & 0x0C)==0x08) ? true : false )
#define	IsLegacyDataFrame(pdu)	(IsDataFrame(pdu) && (!(pdu[0]&FC_QOS_BIT)) )
//added by wb. Is this right?
#define IsQoSDataFrame(pframe)  ((*(u16 *)pframe&(IEEE80211_STYPE_QOS_DATA|IEEE80211_FTYPE_DATA)) == (IEEE80211_STYPE_QOS_DATA|IEEE80211_FTYPE_DATA))
#define Frame_Order(pframe)     (*(u16 *)pframe&IEEE80211_FCTL_ORDER)
#define SN_LESS(a, b)		(((a-b)&0x800)!=0)
#define SN_EQUAL(a, b)	(a == b)
#define MAX_DEV_ADDR_SIZE 8
typedef enum _ACT_CATEGORY{
	ACT_CAT_QOS = 1,
	ACT_CAT_DLS = 2,
	ACT_CAT_BA  = 3,
	ACT_CAT_HT  = 7,
	ACT_CAT_WMM = 17,
} ACT_CATEGORY, *PACT_CATEGORY;

typedef enum _TS_ACTION{
	ACT_ADDTSREQ = 0,
	ACT_ADDTSRSP = 1,
	ACT_DELTS    = 2,
	ACT_SCHEDULE = 3,
} TS_ACTION, *PTS_ACTION;

typedef enum _BA_ACTION{
	ACT_ADDBAREQ = 0,
	ACT_ADDBARSP = 1,
	ACT_DELBA    = 2,
} BA_ACTION, *PBA_ACTION;

typedef enum _InitialGainOpType{
	IG_Backup=0,
	IG_Restore,
	IG_Max
}InitialGainOpType;

/* debug macros */
#define CONFIG_IEEE80211_DEBUG
#ifdef CONFIG_IEEE80211_DEBUG
extern u32 ieee80211_debug_level;
#define IEEE80211_DEBUG(level, fmt, args...) \
do { if (ieee80211_debug_level & (level)) \
  printk(KERN_DEBUG "ieee80211: " fmt, ## args); } while (0)
//wb added to debug out data buf
//if you want print DATA buffer related BA, please set ieee80211_debug_level to DATA|BA
#define IEEE80211_DEBUG_DATA(level, data, datalen)	\
	do{ if ((ieee80211_debug_level & (level)) == (level))	\
		{	\
			int i;					\
			u8 *pdata = (u8 *) data;			\
			printk(KERN_DEBUG "ieee80211: %s()\n", __func__);	\
			for(i=0; i<(int)(datalen); i++)			\
			{						\
				printk("%2x ", pdata[i]);		\
				if ((i+1)%16 == 0) printk("\n");	\
			}				\
			printk("\n");			\
		}					\
	} while (0)
#else
#define IEEE80211_DEBUG(level, fmt, args...) do {} while (0)
#define IEEE80211_DEBUG_DATA(level, data, datalen) do {} while(0)
#endif	/* CONFIG_IEEE80211_DEBUG */

/* debug macros not dependent on CONFIG_IEEE80211_DEBUG */

/*
 * To use the debug system;
 *
 * If you are defining a new debug classification, simply add it to the #define
 * list here in the form of:
 *
 * #define IEEE80211_DL_xxxx VALUE
 *
 * shifting value to the left one bit from the previous entry.  xxxx should be
 * the name of the classification (for example, WEP)
 *
 * You then need to either add a IEEE80211_xxxx_DEBUG() macro definition for your
 * classification, or use IEEE80211_DEBUG(IEEE80211_DL_xxxx, ...) whenever you want
 * to send output to that classification.
 *
 * To add your debug level to the list of levels seen when you perform
 *
 * % cat /proc/net/ipw/debug_level
 *
 * you simply need to add your entry to the ipw_debug_levels array.
 *
 * If you do not see debug_level in /proc/net/ipw then you do not have
 * CONFIG_IEEE80211_DEBUG defined in your kernel configuration
 *
 */

#define IEEE80211_DL_INFO          (1<<0)
#define IEEE80211_DL_WX            (1<<1)
#define IEEE80211_DL_SCAN          (1<<2)
#define IEEE80211_DL_STATE         (1<<3)
#define IEEE80211_DL_MGMT          (1<<4)
#define IEEE80211_DL_FRAG          (1<<5)
#define IEEE80211_DL_EAP           (1<<6)
#define IEEE80211_DL_DROP          (1<<7)

#define IEEE80211_DL_TX            (1<<8)
#define IEEE80211_DL_RX            (1<<9)

#define IEEE80211_DL_HT		   (1<<10)  //HT
#define IEEE80211_DL_BA		   (1<<11)  //ba
#define IEEE80211_DL_TS		   (1<<12)  //TS
#define IEEE80211_DL_QOS           (1<<13)
#define IEEE80211_DL_REORDER	   (1<<14)
#define IEEE80211_DL_IOT	   (1<<15)
#define IEEE80211_DL_IPS	   (1<<16)
#define IEEE80211_DL_TRACE	   (1<<29)  //trace function, need to user net_ratelimit() together in order not to print too much to the screen
#define IEEE80211_DL_DATA	   (1<<30)   //use this flag to control whether print data buf out.
#define IEEE80211_DL_ERR	   (1<<31)   //always open
#define IEEE80211_ERROR(f, a...) printk(KERN_ERR "ieee80211: " f, ## a)
#define IEEE80211_WARNING(f, a...) printk(KERN_WARNING "ieee80211: " f, ## a)
#define IEEE80211_DEBUG_INFO(f, a...)   IEEE80211_DEBUG(IEEE80211_DL_INFO, f, ## a)

#define IEEE80211_DEBUG_WX(f, a...)     IEEE80211_DEBUG(IEEE80211_DL_WX, f, ## a)
#define IEEE80211_DEBUG_SCAN(f, a...)   IEEE80211_DEBUG(IEEE80211_DL_SCAN, f, ## a)
#define IEEE80211_DEBUG_STATE(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_STATE, f, ## a)
#define IEEE80211_DEBUG_MGMT(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_MGMT, f, ## a)
#define IEEE80211_DEBUG_FRAG(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_FRAG, f, ## a)
#define IEEE80211_DEBUG_EAP(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_EAP, f, ## a)
#define IEEE80211_DEBUG_DROP(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_DROP, f, ## a)
#define IEEE80211_DEBUG_TX(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_TX, f, ## a)
#define IEEE80211_DEBUG_RX(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_RX, f, ## a)
#define IEEE80211_DEBUG_QOS(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_QOS, f, ## a)

#ifdef CONFIG_IEEE80211_DEBUG
/* Added by Annie, 2005-11-22. */
#define MAX_STR_LEN     64
/* I want to see ASCII 33 to 126 only. Otherwise, I print '?'. Annie, 2005-11-22.*/
#define PRINTABLE(_ch)  (_ch>'!' && _ch<'~')
#define IEEE80211_PRINT_STR(_Comp, _TitleString, _Ptr, _Len)					\
			if((_Comp) & level)							\
			{                                                                       \
				int             __i;                                            \
				u8  buffer[MAX_STR_LEN];					\
				int length = (_Len<MAX_STR_LEN)? _Len : (MAX_STR_LEN-1) ;	\
				memset(buffer, 0, MAX_STR_LEN);					\
				memcpy(buffer, (u8 *)_Ptr, length );				\
				for( __i=0; __i<MAX_STR_LEN; __i++ )                            \
				{                                                               \
				     if( !PRINTABLE(buffer[__i]) )   buffer[__i] = '?';		\
				}                                                               \
				buffer[length] = '\0';                                          \
				printk("Rtl819x: ");						\
				printk(_TitleString);                                         \
				printk(": %d, <%s>\n", _Len, buffer);                         \
			}
#else
#define IEEE80211_PRINT_STR(_Comp, _TitleString, _Ptr, _Len)  do {} while (0)
#endif

#include <linux/netdevice.h>
#include <linux/if_arp.h> /* ARPHRD_ETHER */

#ifndef WIRELESS_SPY
#define WIRELESS_SPY		// enable iwspy support
#endif
#include <net/iw_handler.h>	// new driver API

#ifndef ETH_P_PAE
#define ETH_P_PAE 0x888E /* Port Access Entity (IEEE 802.1X) */
#endif /* ETH_P_PAE */

#define ETH_P_PREAUTH 0x88C7 /* IEEE 802.11i pre-authentication */

#ifndef ETH_P_80211_RAW
#define ETH_P_80211_RAW (ETH_P_ECONET + 1)
#endif

/* IEEE 802.11 defines */

#define P80211_OUI_LEN 3

struct ieee80211_snap_hdr {

	u8    dsap;   /* always 0xAA */
	u8    ssap;   /* always 0xAA */
	u8    ctrl;   /* always 0x03 */
	u8    oui[P80211_OUI_LEN];    /* organizational universal id */

} __attribute__ ((packed));

#define SNAP_SIZE sizeof(struct ieee80211_snap_hdr)

#define WLAN_FC_GET_VERS(fc) ((fc) & IEEE80211_FCTL_VERS)
#define WLAN_FC_GET_TYPE(fc) ((fc) & IEEE80211_FCTL_FTYPE)
#define WLAN_FC_GET_STYPE(fc) ((fc) & IEEE80211_FCTL_STYPE)

#define WLAN_FC_GET_FRAMETYPE(fc) ((fc) & IEEE80211_FCTL_FRAMETYPE)
#define WLAN_GET_SEQ_FRAG(seq) ((seq) & IEEE80211_SCTL_FRAG)
#define WLAN_GET_SEQ_SEQ(seq)  (((seq) & IEEE80211_SCTL_SEQ) >> 4)

/* Authentication algorithms */
#define WLAN_AUTH_OPEN 0
#define WLAN_AUTH_SHARED_KEY 1
#define WLAN_AUTH_LEAP 2

#define WLAN_AUTH_CHALLENGE_LEN 128

#define WLAN_CAPABILITY_BSS (1<<0)
#define WLAN_CAPABILITY_IBSS (1<<1)
#define WLAN_CAPABILITY_CF_POLLABLE (1<<2)
#define WLAN_CAPABILITY_CF_POLL_REQUEST (1<<3)
#define WLAN_CAPABILITY_PRIVACY (1<<4)
#define WLAN_CAPABILITY_SHORT_PREAMBLE (1<<5)
#define WLAN_CAPABILITY_PBCC (1<<6)
#define WLAN_CAPABILITY_CHANNEL_AGILITY (1<<7)
#define WLAN_CAPABILITY_SPECTRUM_MGMT (1<<8)
#define WLAN_CAPABILITY_QOS (1<<9)
#define WLAN_CAPABILITY_SHORT_SLOT (1<<10)
#define WLAN_CAPABILITY_DSSS_OFDM (1<<13)

/* 802.11g ERP information element */
#define WLAN_ERP_NON_ERP_PRESENT (1<<0)
#define WLAN_ERP_USE_PROTECTION (1<<1)
#define WLAN_ERP_BARKER_PREAMBLE (1<<2)

/* Status codes */
enum ieee80211_statuscode {
	WLAN_STATUS_SUCCESS = 0,
	WLAN_STATUS_UNSPECIFIED_FAILURE = 1,
	WLAN_STATUS_CAPS_UNSUPPORTED = 10,
	WLAN_STATUS_REASSOC_NO_ASSOC = 11,
	WLAN_STATUS_ASSOC_DENIED_UNSPEC = 12,
	WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG = 13,
	WLAN_STATUS_UNKNOWN_AUTH_TRANSACTION = 14,
	WLAN_STATUS_CHALLENGE_FAIL = 15,
	WLAN_STATUS_AUTH_TIMEOUT = 16,
	WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA = 17,
	WLAN_STATUS_ASSOC_DENIED_RATES = 18,
	/* 802.11b */
	WLAN_STATUS_ASSOC_DENIED_NOSHORTPREAMBLE = 19,
	WLAN_STATUS_ASSOC_DENIED_NOPBCC = 20,
	WLAN_STATUS_ASSOC_DENIED_NOAGILITY = 21,
	/* 802.11h */
	WLAN_STATUS_ASSOC_DENIED_NOSPECTRUM = 22,
	WLAN_STATUS_ASSOC_REJECTED_BAD_POWER = 23,
	WLAN_STATUS_ASSOC_REJECTED_BAD_SUPP_CHAN = 24,
	/* 802.11g */
	WLAN_STATUS_ASSOC_DENIED_NOSHORTTIME = 25,
	WLAN_STATUS_ASSOC_DENIED_NODSSSOFDM = 26,
	/* 802.11i */
	WLAN_STATUS_INVALID_IE = 40,
	WLAN_STATUS_INVALID_GROUP_CIPHER = 41,
	WLAN_STATUS_INVALID_PAIRWISE_CIPHER = 42,
	WLAN_STATUS_INVALID_AKMP = 43,
	WLAN_STATUS_UNSUPP_RSN_VERSION = 44,
	WLAN_STATUS_INVALID_RSN_IE_CAP = 45,
	WLAN_STATUS_CIPHER_SUITE_REJECTED = 46,
};

/* Reason codes */
enum ieee80211_reasoncode {
	WLAN_REASON_UNSPECIFIED = 1,
	WLAN_REASON_PREV_AUTH_NOT_VALID = 2,
	WLAN_REASON_DEAUTH_LEAVING = 3,
	WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY = 4,
	WLAN_REASON_DISASSOC_AP_BUSY = 5,
	WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA = 6,
	WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA = 7,
	WLAN_REASON_DISASSOC_STA_HAS_LEFT = 8,
	WLAN_REASON_STA_REQ_ASSOC_WITHOUT_AUTH = 9,
	/* 802.11h */
	WLAN_REASON_DISASSOC_BAD_POWER = 10,
	WLAN_REASON_DISASSOC_BAD_SUPP_CHAN = 11,
	/* 802.11i */
	WLAN_REASON_INVALID_IE = 13,
	WLAN_REASON_MIC_FAILURE = 14,
	WLAN_REASON_4WAY_HANDSHAKE_TIMEOUT = 15,
	WLAN_REASON_GROUP_KEY_HANDSHAKE_TIMEOUT = 16,
	WLAN_REASON_IE_DIFFERENT = 17,
	WLAN_REASON_INVALID_GROUP_CIPHER = 18,
	WLAN_REASON_INVALID_PAIRWISE_CIPHER = 19,
	WLAN_REASON_INVALID_AKMP = 20,
	WLAN_REASON_UNSUPP_RSN_VERSION = 21,
	WLAN_REASON_INVALID_RSN_IE_CAP = 22,
	WLAN_REASON_IEEE8021X_FAILED = 23,
	WLAN_REASON_CIPHER_SUITE_REJECTED = 24,
};

#define IEEE80211_STATMASK_SIGNAL (1<<0)
#define IEEE80211_STATMASK_RSSI (1<<1)
#define IEEE80211_STATMASK_NOISE (1<<2)
#define IEEE80211_STATMASK_RATE (1<<3)
#define IEEE80211_STATMASK_WEMASK 0x7

#define IEEE80211_CCK_MODULATION    (1<<0)
#define IEEE80211_OFDM_MODULATION   (1<<1)

#define IEEE80211_24GHZ_BAND     (1<<0)
#define IEEE80211_52GHZ_BAND     (1<<1)

#define IEEE80211_CCK_RATE_LEN			4
#define IEEE80211_CCK_RATE_1MB			0x02
#define IEEE80211_CCK_RATE_2MB			0x04
#define IEEE80211_CCK_RATE_5MB			0x0B
#define IEEE80211_CCK_RATE_11MB			0x16
#define IEEE80211_OFDM_RATE_LEN			8
#define IEEE80211_OFDM_RATE_6MB			0x0C
#define IEEE80211_OFDM_RATE_9MB			0x12
#define IEEE80211_OFDM_RATE_12MB		0x18
#define IEEE80211_OFDM_RATE_18MB		0x24
#define IEEE80211_OFDM_RATE_24MB		0x30
#define IEEE80211_OFDM_RATE_36MB		0x48
#define IEEE80211_OFDM_RATE_48MB		0x60
#define IEEE80211_OFDM_RATE_54MB		0x6C
#define IEEE80211_BASIC_RATE_MASK		0x80

#define IEEE80211_CCK_RATE_1MB_MASK		(1<<0)
#define IEEE80211_CCK_RATE_2MB_MASK		(1<<1)
#define IEEE80211_CCK_RATE_5MB_MASK		(1<<2)
#define IEEE80211_CCK_RATE_11MB_MASK		(1<<3)
#define IEEE80211_OFDM_RATE_6MB_MASK		(1<<4)
#define IEEE80211_OFDM_RATE_9MB_MASK		(1<<5)
#define IEEE80211_OFDM_RATE_12MB_MASK		(1<<6)
#define IEEE80211_OFDM_RATE_18MB_MASK		(1<<7)
#define IEEE80211_OFDM_RATE_24MB_MASK		(1<<8)
#define IEEE80211_OFDM_RATE_36MB_MASK		(1<<9)
#define IEEE80211_OFDM_RATE_48MB_MASK		(1<<10)
#define IEEE80211_OFDM_RATE_54MB_MASK		(1<<11)

#define IEEE80211_CCK_RATES_MASK		0x0000000F
#define IEEE80211_CCK_BASIC_RATES_MASK	(IEEE80211_CCK_RATE_1MB_MASK | \
	IEEE80211_CCK_RATE_2MB_MASK)
#define IEEE80211_CCK_DEFAULT_RATES_MASK	(IEEE80211_CCK_BASIC_RATES_MASK | \
	IEEE80211_CCK_RATE_5MB_MASK | \
	IEEE80211_CCK_RATE_11MB_MASK)

#define IEEE80211_OFDM_RATES_MASK		0x00000FF0
#define IEEE80211_OFDM_BASIC_RATES_MASK	(IEEE80211_OFDM_RATE_6MB_MASK | \
	IEEE80211_OFDM_RATE_12MB_MASK | \
	IEEE80211_OFDM_RATE_24MB_MASK)
#define IEEE80211_OFDM_DEFAULT_RATES_MASK	(IEEE80211_OFDM_BASIC_RATES_MASK | \
	IEEE80211_OFDM_RATE_9MB_MASK  | \
	IEEE80211_OFDM_RATE_18MB_MASK | \
	IEEE80211_OFDM_RATE_36MB_MASK | \
	IEEE80211_OFDM_RATE_48MB_MASK | \
	IEEE80211_OFDM_RATE_54MB_MASK)
#define IEEE80211_DEFAULT_RATES_MASK (IEEE80211_OFDM_DEFAULT_RATES_MASK | \
				IEEE80211_CCK_DEFAULT_RATES_MASK)

#define IEEE80211_NUM_OFDM_RATES	    8
#define IEEE80211_NUM_CCK_RATES		    4
#define IEEE80211_OFDM_SHIFT_MASK_A         4


/* this is stolen and modified from the madwifi driver*/
#define IEEE80211_FC0_TYPE_MASK		0x0c
#define IEEE80211_FC0_TYPE_DATA		0x08
#define IEEE80211_FC0_SUBTYPE_MASK	0xB0
#define IEEE80211_FC0_SUBTYPE_QOS	0x80

#define IEEE80211_QOS_HAS_SEQ(fc) \
	(((fc) & (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) == \
	 (IEEE80211_FC0_TYPE_DATA | IEEE80211_FC0_SUBTYPE_QOS))

/* this is stolen from ipw2200 driver */
#define IEEE_IBSS_MAC_HASH_SIZE 31
struct ieee_ibss_seq {
	u8 mac[ETH_ALEN];
	u16 seq_num[17];
	u16 frag_num[17];
	unsigned long packet_time[17];
	struct list_head list;
};

/* NOTE: This data is for statistical purposes; not all hardware provides this
 *       information for frames received.  Not setting these will not cause
 *       any adverse affects. */
struct ieee80211_rx_stats {
	u32 mac_time[2];
	s8 rssi;
	u8 signal;
	u8 noise;
	u16 rate; /* in 100 kbps */
	u8 received_channel;
	u8 control;
	u8 mask;
	u8 freq;
	u16 len;
	u64 tsf;
	u32 beacon_time;
	u8 nic_type;
	u16       Length;
	//      u8        DataRate;      // In 0.5 Mbps
	u8        SignalQuality; // in 0-100 index.
	s32       RecvSignalPower; // Real power in dBm for this packet, no beautification and aggregation.
	s8        RxPower; // in dBm Translate from PWdB
	u8        SignalStrength; // in 0-100 index.
	u16       bHwError:1;
	u16       bCRC:1;
	u16       bICV:1;
	u16       bShortPreamble:1;
	u16       Antenna:1;      //for rtl8185
	u16       Decrypted:1;    //for rtl8185, rtl8187
	u16       Wakeup:1;       //for rtl8185
	u16       Reserved0:1;    //for rtl8185
	u8        AGC;
	u32       TimeStampLow;
	u32       TimeStampHigh;
	bool      bShift;
	bool      bIsQosData;             // Added by Annie, 2005-12-22.
	u8        UserPriority;

	//1!!!!!!!!!!!!!!!!!!!!!!!!!!!
	//1Attention Please!!!<11n or 8190 specific code should be put below this line>
	//1!!!!!!!!!!!!!!!!!!!!!!!!!!!

	u8        RxDrvInfoSize;
	u8        RxBufShift;
	bool      bIsAMPDU;
	bool      bFirstMPDU;
	bool      bContainHTC;
	bool      RxIs40MHzPacket;
	u32       RxPWDBAll;
	u8        RxMIMOSignalStrength[4];        // in 0~100 index
	s8        RxMIMOSignalQuality[2];
	bool      bPacketMatchBSSID;
	bool      bIsCCK;
	bool      bPacketToSelf;
	//added by amy
	u8        *virtual_address;
	u16          packetlength;              // Total packet length: Must equal to sum of all FragLength
	u16          fraglength;                        // FragLength should equal to PacketLength in non-fragment case
	u16          fragoffset;                        // Data offset for this fragment
	u16          ntotalfrag;
	bool		  bisrxaggrsubframe;
	bool		  bPacketBeacon;	//cosa add for rssi
	bool		  bToSelfBA;		//cosa add for rssi
	char	  cck_adc_pwdb[4];	//cosa add for rx path selection
	u16		  Seq_Num;

};

/* IEEE 802.11 requires that STA supports concurrent reception of at least
 * three fragmented frames. This define can be increased to support more
 * concurrent frames, but it should be noted that each entry can consume about
 * 2 kB of RAM and increasing cache size will slow down frame reassembly. */
#define IEEE80211_FRAG_CACHE_LEN 4

struct ieee80211_frag_entry {
	unsigned long first_frag_time;
	unsigned int seq;
	unsigned int last_frag;
	struct sk_buff *skb;
	u8 src_addr[ETH_ALEN];
	u8 dst_addr[ETH_ALEN];
};

struct ieee80211_stats {
	unsigned int tx_unicast_frames;
	unsigned int tx_multicast_frames;
	unsigned int tx_fragments;
	unsigned int tx_unicast_octets;
	unsigned int tx_multicast_octets;
	unsigned int tx_deferred_transmissions;
	unsigned int tx_single_retry_frames;
	unsigned int tx_multiple_retry_frames;
	unsigned int tx_retry_limit_exceeded;
	unsigned int tx_discards;
	unsigned int rx_unicast_frames;
	unsigned int rx_multicast_frames;
	unsigned int rx_fragments;
	unsigned int rx_unicast_octets;
	unsigned int rx_multicast_octets;
	unsigned int rx_fcs_errors;
	unsigned int rx_discards_no_buffer;
	unsigned int tx_discards_wrong_sa;
	unsigned int rx_discards_undecryptable;
	unsigned int rx_message_in_msg_fragments;
	unsigned int rx_message_in_bad_msg_fragments;
};

struct ieee80211_device;

#include "ieee80211_crypt.h"

#define SEC_KEY_1         (1<<0)
#define SEC_KEY_2         (1<<1)
#define SEC_KEY_3         (1<<2)
#define SEC_KEY_4         (1<<3)
#define SEC_ACTIVE_KEY    (1<<4)
#define SEC_AUTH_MODE     (1<<5)
#define SEC_UNICAST_GROUP (1<<6)
#define SEC_LEVEL         (1<<7)
#define SEC_ENABLED       (1<<8)
#define SEC_ENCRYPT       (1<<9)

#define SEC_LEVEL_0      0 /* None */
#define SEC_LEVEL_1      1 /* WEP 40 and 104 bit */
#define SEC_LEVEL_2      2 /* Level 1 + TKIP */
#define SEC_LEVEL_2_CKIP 3 /* Level 1 + CKIP */
#define SEC_LEVEL_3      4 /* Level 2 + CCMP */

#define SEC_ALG_NONE            0
#define SEC_ALG_WEP             1
#define SEC_ALG_TKIP            2
#define SEC_ALG_CCMP            3

#define WEP_KEYS		4
#define WEP_KEY_LEN		13
#define SCM_KEY_LEN             32
#define SCM_TEMPORAL_KEY_LENGTH 16

struct ieee80211_security {
	u16 active_key:2,
	    enabled:1,
	    auth_mode:2,
	    auth_algo:4,
	    unicast_uses_group:1,
	    encrypt:1;
	u8 key_sizes[WEP_KEYS];
	u8 keys[WEP_KEYS][SCM_KEY_LEN];
	u8 level;
	u16 flags;
} __attribute__ ((packed));


/*
 802.11 data frame from AP
      ,-------------------------------------------------------------------.
Bytes |  2   |  2   |    6    |    6    |    6    |  2   | 0..2312 |   4  |
      |------|------|---------|---------|---------|------|---------|------|
Desc. | ctrl | dura |  DA/RA  |   TA    |    SA   | Sequ |  frame  |  fcs |
      |      | tion | (BSSID) |         |         | ence |  data   |      |
      `-------------------------------------------------------------------'
Total: 28-2340 bytes
*/

/* Management Frame Information Element Types */
enum ieee80211_mfie {
	MFIE_TYPE_SSID = 0,
	MFIE_TYPE_RATES = 1,
	MFIE_TYPE_FH_SET = 2,
	MFIE_TYPE_DS_SET = 3,
	MFIE_TYPE_CF_SET = 4,
	MFIE_TYPE_TIM = 5,
	MFIE_TYPE_IBSS_SET = 6,
	MFIE_TYPE_COUNTRY = 7,
	MFIE_TYPE_HOP_PARAMS = 8,
	MFIE_TYPE_HOP_TABLE = 9,
	MFIE_TYPE_REQUEST = 10,
	MFIE_TYPE_CHALLENGE = 16,
	MFIE_TYPE_POWER_CONSTRAINT = 32,
	MFIE_TYPE_POWER_CAPABILITY = 33,
	MFIE_TYPE_TPC_REQUEST = 34,
	MFIE_TYPE_TPC_REPORT = 35,
	MFIE_TYPE_SUPP_CHANNELS = 36,
	MFIE_TYPE_CSA = 37,
	MFIE_TYPE_MEASURE_REQUEST = 38,
	MFIE_TYPE_MEASURE_REPORT = 39,
	MFIE_TYPE_QUIET = 40,
	MFIE_TYPE_IBSS_DFS = 41,
	MFIE_TYPE_ERP = 42,
	MFIE_TYPE_RSN = 48,
	MFIE_TYPE_RATES_EX = 50,
	MFIE_TYPE_HT_CAP= 45,
	 MFIE_TYPE_HT_INFO= 61,
	 MFIE_TYPE_AIRONET=133,
	MFIE_TYPE_GENERIC = 221,
	MFIE_TYPE_QOS_PARAMETER = 222,
};

/* Minimal header; can be used for passing 802.11 frames with sufficient
 * information to determine what type of underlying data type is actually
 * stored in the data. */
struct ieee80211_hdr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 payload[0];
} __attribute__ ((packed));

struct ieee80211_hdr_1addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 payload[0];
} __attribute__ ((packed));

struct ieee80211_hdr_2addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 payload[0];
} __attribute__ ((packed));

struct ieee80211_hdr_3addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 payload[0];
} __attribute__ ((packed));

struct ieee80211_hdr_4addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 addr4[ETH_ALEN];
	u8 payload[0];
} __attribute__ ((packed));

struct ieee80211_hdr_3addrqos {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 payload[0];
	__le16 qos_ctl;
} __attribute__ ((packed));

struct ieee80211_hdr_4addrqos {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 addr4[ETH_ALEN];
	u8 payload[0];
	__le16 qos_ctl;
} __attribute__ ((packed));

struct ieee80211_info_element {
	u8 id;
	u8 len;
	u8 data[0];
} __attribute__ ((packed));

struct ieee80211_authentication {
	struct ieee80211_hdr_3addr header;
	__le16 algorithm;
	__le16 transaction;
	__le16 status;
	/*challenge*/
	struct ieee80211_info_element info_element[0];
} __attribute__ ((packed));

struct ieee80211_disassoc {
	struct ieee80211_hdr_3addr header;
	__le16 reason;
} __attribute__ ((packed));

struct ieee80211_probe_request {
	struct ieee80211_hdr_3addr header;
	/* SSID, supported rates */
	struct ieee80211_info_element info_element[0];
} __attribute__ ((packed));

struct ieee80211_probe_response {
	struct ieee80211_hdr_3addr header;
	__le32 time_stamp[2];
	__le16 beacon_interval;
	__le16 capability;
	/* SSID, supported rates, FH params, DS params,
	 * CF params, IBSS params, TIM (if beacon), RSN */
	struct ieee80211_info_element info_element[0];
} __attribute__ ((packed));

/* Alias beacon for probe_response */
#define ieee80211_beacon ieee80211_probe_response

struct ieee80211_assoc_request_frame {
	struct ieee80211_hdr_3addr header;
	__le16 capability;
	__le16 listen_interval;
	/* SSID, supported rates, RSN */
	struct ieee80211_info_element info_element[0];
} __attribute__ ((packed));

struct ieee80211_reassoc_request_frame {
	struct ieee80211_hdr_3addr header;
	__le16 capability;
	__le16 listen_interval;
	u8 current_ap[ETH_ALEN];
	/* SSID, supported rates, RSN */
	struct ieee80211_info_element info_element[0];
} __attribute__ ((packed));

struct ieee80211_assoc_response_frame {
	struct ieee80211_hdr_3addr header;
	__le16 capability;
	__le16 status;
	__le16 aid;
	struct ieee80211_info_element info_element[0]; /* supported rates */
} __attribute__ ((packed));

struct ieee80211_txb {
	u8 nr_frags;
	u8 encrypted;
	u8 queue_index;
	u8 rts_included;
	u16 reserved;
	__le16 frag_size;
	__le16 payload_size;
	struct sk_buff *fragments[0];
};

#define MAX_TX_AGG_COUNT		  16
struct ieee80211_drv_agg_txb {
	u8 nr_drv_agg_frames;
	struct sk_buff *tx_agg_frames[MAX_TX_AGG_COUNT];
}__attribute__((packed));

#define MAX_SUBFRAME_COUNT		  64
struct ieee80211_rxb {
	u8 nr_subframes;
	struct sk_buff *subframes[MAX_SUBFRAME_COUNT];
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
}__attribute__((packed));

typedef union _frameqos {
	u16 shortdata;
	u8  chardata[2];
	struct {
		u16 tid:4;
		u16 eosp:1;
		u16 ack_policy:2;
		u16 reserved:1;
		u16 txop:8;
	}field;
} frameqos, *pframeqos;

/* SWEEP TABLE ENTRIES NUMBER*/
#define MAX_SWEEP_TAB_ENTRIES		  42
#define MAX_SWEEP_TAB_ENTRIES_PER_PACKET  7
/* MAX_RATES_LENGTH needs to be 12.  The spec says 8, and many APs
 * only use 8, and then use extended rates for the remaining supported
 * rates.  Other APs, however, stick all of their supported rates on the
 * main rates information element... */
#define MAX_RATES_LENGTH                  ((u8)12)
#define MAX_RATES_EX_LENGTH               ((u8)16)
#define MAX_NETWORK_COUNT                  128

#define MAX_CHANNEL_NUMBER                 161
#define IEEE80211_SOFTMAC_SCAN_TIME	   100
//(HZ / 2)
#define IEEE80211_SOFTMAC_ASSOC_RETRY_TIME (HZ * 2)

#define CRC_LENGTH                 4U

#define MAX_WPA_IE_LEN 64

#define NETWORK_EMPTY_ESSID (1<<0)
#define NETWORK_HAS_OFDM    (1<<1)
#define NETWORK_HAS_CCK     (1<<2)

/* QoS structure */
#define NETWORK_HAS_QOS_PARAMETERS      (1<<3)
#define NETWORK_HAS_QOS_INFORMATION     (1<<4)
#define NETWORK_HAS_QOS_MASK            (NETWORK_HAS_QOS_PARAMETERS | \
					 NETWORK_HAS_QOS_INFORMATION)
/* 802.11h */
#define NETWORK_HAS_POWER_CONSTRAINT    (1<<5)
#define NETWORK_HAS_CSA                 (1<<6)
#define NETWORK_HAS_QUIET               (1<<7)
#define NETWORK_HAS_IBSS_DFS            (1<<8)
#define NETWORK_HAS_TPC_REPORT          (1<<9)

#define NETWORK_HAS_ERP_VALUE           (1<<10)

#define QOS_QUEUE_NUM                   4
#define QOS_OUI_LEN                     3
#define QOS_OUI_TYPE                    2
#define QOS_ELEMENT_ID                  221
#define QOS_OUI_INFO_SUB_TYPE           0
#define QOS_OUI_PARAM_SUB_TYPE          1
#define QOS_VERSION_1                   1
#define QOS_AIFSN_MIN_VALUE             2
struct ieee80211_qos_information_element {
	u8 elementID;
	u8 length;
	u8 qui[QOS_OUI_LEN];
	u8 qui_type;
	u8 qui_subtype;
	u8 version;
	u8 ac_info;
} __attribute__ ((packed));

struct ieee80211_qos_ac_parameter {
	u8 aci_aifsn;
	u8 ecw_min_max;
	__le16 tx_op_limit;
} __attribute__ ((packed));

struct ieee80211_qos_parameter_info {
	struct ieee80211_qos_information_element info_element;
	u8 reserved;
	struct ieee80211_qos_ac_parameter ac_params_record[QOS_QUEUE_NUM];
} __attribute__ ((packed));

struct ieee80211_qos_parameters {
	__le16 cw_min[QOS_QUEUE_NUM];
	__le16 cw_max[QOS_QUEUE_NUM];
	u8 aifs[QOS_QUEUE_NUM];
	u8 flag[QOS_QUEUE_NUM];
	__le16 tx_op_limit[QOS_QUEUE_NUM];
} __attribute__ ((packed));

struct ieee80211_qos_data {
	struct ieee80211_qos_parameters parameters;
	int active;
	int supported;
	u8 param_count;
	u8 old_param_count;
};

struct ieee80211_tim_parameters {
	u8 tim_count;
	u8 tim_period;
} __attribute__ ((packed));

//#else
struct ieee80211_wmm_ac_param {
	u8 ac_aci_acm_aifsn;
	u8 ac_ecwmin_ecwmax;
	u16 ac_txop_limit;
};

struct ieee80211_wmm_ts_info {
	u8 ac_dir_tid;
	u8 ac_up_psb;
	u8 reserved;
} __attribute__ ((packed));

struct ieee80211_wmm_tspec_elem {
	struct ieee80211_wmm_ts_info ts_info;
	u16 norm_msdu_size;
	u16 max_msdu_size;
	u32 min_serv_inter;
	u32 max_serv_inter;
	u32 inact_inter;
	u32 suspen_inter;
	u32 serv_start_time;
	u32 min_data_rate;
	u32 mean_data_rate;
	u32 peak_data_rate;
	u32 max_burst_size;
	u32 delay_bound;
	u32 min_phy_rate;
	u16 surp_band_allow;
	u16 medium_time;
}__attribute__((packed));
enum eap_type {
	EAP_PACKET = 0,
	EAPOL_START,
	EAPOL_LOGOFF,
	EAPOL_KEY,
	EAPOL_ENCAP_ASF_ALERT
};

static const char *eap_types[] = {
	[EAP_PACKET]		= "EAP-Packet",
	[EAPOL_START]		= "EAPOL-Start",
	[EAPOL_LOGOFF]		= "EAPOL-Logoff",
	[EAPOL_KEY]		= "EAPOL-Key",
	[EAPOL_ENCAP_ASF_ALERT]	= "EAPOL-Encap-ASF-Alert"
};

static inline const char *eap_get_type(int type)
{
	return ((u32)type >= ARRAY_SIZE(eap_types)) ? "Unknown" : eap_types[type];
}
//added by amy for reorder
static inline u8 Frame_QoSTID(u8 *buf)
{
	struct ieee80211_hdr_3addr *hdr;
	u16 fc;
	hdr = (struct ieee80211_hdr_3addr *)buf;
	fc = le16_to_cpu(hdr->frame_ctl);
	return (u8)((frameqos *)(buf + (((fc & IEEE80211_FCTL_TODS)&&(fc & IEEE80211_FCTL_FROMDS))? 30 : 24)))->field.tid;
}

//added by amy for reorder

struct eapol {
	u8 snap[6];
	u16 ethertype;
	u8 version;
	u8 rx_stats {
eength;
	u8 qui[ibute__ ((packed));

struct ieee80211_wmm_tspescan_syncunsi{ed int rx_message_ass_oreq;
int rx_message_ass_u8 idx
int rx_message_sponse
gned int last_frate_sponse
gned int tx_retry_lieee802gned int rx_message_
#defigned int last_frage_
#defis_oreq;
int rx_message_a#defis_u8 idx
int rx_messate_
#defigned int last_frano_a#defisned int last_frano_asse
gned int tx_retry_liasse
gned int last_frage_
sse
gned int last_frate_sponse
gned int last_frarequestned int last_fraswtxstopned int last_fraswtxawakgned int rx_mp_getCp[ETH_ShowTx6 surp_int rx_mp_getg;
	sime[17]r6 surp_int rx_m_fratemit_e;

struct iQOS_AIFS0x0080ESP	0x0<0)
_ISE_OSI (1<<12 ieee80211_info_element {
	u8 i__le16 fu8 len;
	u8 dataibute__ ((packed));

/* Alias bdd yol notides. */
strs {
eTA suppsed makg312
nt */
#defiime[17y us_txop_l:4,
	   __le16 txop_l:4,
 counter6 txop_nterval;
	__le16 cuility;
	__le16 snt_ap[ETH_ALEN];
	/* SSID,un_interval;
	u8 curre
		u16 tid:4;
	questi;
	u8 ad:14,d:1;
		u16 resibute__ ((packed));

/* Alia_phy_mp[2];
	__le16 bun;
} __attrp_band__le16 ine IEEE80211_DL_INFO  ATES_MATX1<<0)
#"Pu8 uin" IEEE80211_DL_INFO  ATES_MATES_MASK |vel /1     p_type{WMM	u16s {
	u,6/8/_twos {
	u,6/8/_fours {
	u,6/8/_sixs {
	u}; MAX_SWEEP_TABPMAX_S (WMM	u16s {
	u << ne IEEE80211_OFDM_RATE_NU    0x0f QOS_AIFSN_MIDS))NOT080TA   0094(defi << 5ne IEEE80211_DEBUG_WX(TIM_MBUP (ne IEEE80211_OFDM_SHI(TIM_UUP (ne IEEE80211_OFDM_RAT(TIM_2,
	WLe IEEE80211_SOFTMAC_(TIM_RSN_IE_ ne IEEE80211_QOS_HAS_PSC_BAD   (1e IEEE80211_FC0_SUBTPORTEOUP (11_OFDM_SHI(TIM_UUP ( IEEE80211_FC0_SUBTPORMBUP (n1_DEBUG_WX(TIM_MBUP (d by amy for Dfinerder
ctur
#def6/3/ 2)WEP_KEY_LMM	Hang_6    ONFIG_ILMM	Hang_6    OunIG_ILMM	Hang_6    Oinclude EP_KEY_LMx00D_B94

/* de EP_KEY_LMx00D_BE4

/* 1e EP_KEY_LMx00D_VI4

/* 2e EP_KEY_LMx00D_VO4

/* 3e EP_KEY_LMx00DI_

#defi 3e EP_KEY_LMx00_VALUE
#defi 3e EP_KEY_LMx00CESPAM#define  MAX_RATES_EX_ECE   (BU= 17
struc9Z /  2)UP Mappe wilo AC,  suspdataMgntQuery_ScounterNumber()y APs
 ybe MA-UDSCP 2)WEP_KEY_UP2AC(up)	pe p<3nownpe p==0)?1:0)STR_up>>1ne Frame_OrUP2AC(up) (#defi) & (up) < 1nownLMx00D_BE4:i) & (up) < 3nownLMx00D_BK4:i) & (up) < 4nownLMx00D_BE4:i) & (up) < 6nownLMx00D_VI4:i) &LMx00D_VOddedAC Mappe wilo UP,  suspdataTelecris fran
	u16	 will harr_framed	 wiTXndex;
 Frame_OrAC2UP(sn;)	p
struct) & (sn;)(IEELMx00D_VOdown64:i) & (sn;)(IEELMx00D_VIdown54:i) & (sn;)(IEELMx00D_BKdown14:i) &ine QOS_QUE	

#if_)
#defin		6, su;				\
ast
n E;
	unet 
	u16  ine NETWORK_

#ifNET_HEAD17
strucfine  (1<<3 su;				\
asttwo E;
	unet 
	u16  iossibint da {
eine 
		u16	int d_	__le1lementIDnt d_dhoLEN];
if_)
#definlag[QOSDnt d_shoLEN];
if_)
#definlag[Qtype;
	uu8 qui_ibute__ ((packe));

#define MTH_P_80211EAPR_CAPne ETH_P_P	211EAPR_CAPne	* Pore	, sucap-A Pne/*/
#dxif /* ETH_ MTH_P_80211EAPR_CAIP ETH_P_P	211EAPR_CAIP#define	, suIPespotocefine Iincludeunion _f
		u16 _{
	uht  dsbToSel		d;
	u8 uht!!!!!! HT BA, plea{
	u8 i     TWORKht_ty;ubfr[3e16 bun;TWORKht_ty;u data[0]TWORKht_ment bfr[3e16 bun;TWORKht_ment  data
	HTM_MGM_1  ORKht_m {
	vSSID, /45,
	 DSSS_OFDELEr[ledHTCapEigned /45,ION)
/* 802DELEr[edHTion EignedsbToSel		ion.
	s8   ;dsbToSel		l	unsilo	u32 min}{
	uhteqos{
	uhtef enum _InitialGertypACKERP_PonERPp:1;
ntL-E/* 1,CKERP_UsePpotu16		 L-E/* 2,CKERP_Barkty;
;
	u16Mld b-E/* defi ertyp802.eee80211_info_elenetworkleme/ yol not	unsiestidesuupport iCONFifr rframdex networkleceived{
	idN];
	u8 src[ETH_
	u8 contr sucnsdefinull-what t plea fra * ocros nosg received
	idNIW1<<0)
_P_TABtruc+ 1rc[ETH_A	idu data[eee80211_qos_data {
	struc {
	stru!!!!!! amy for reorder
#defdsbToSebW_leAironetIE;dsbToSebCkipS;
	u8 parambToSebCcxRmEnnsigned n;TCcxRmodes8 rssi;!! CCXv4 S59, M     .ambToSebMBA	idVallen;
	u	MBA	idM freq;
		MBA	id6 ethe!! CCXvelS38,P_BAR D#incl V	u8 rx Number... */
#d2005-12-22.6-08-20.dsbToSebW_leCcxV	u

/* ;
		BA	CcxV	u

/bSSID, suol notidesnetworklcal purporuct ieee80211_info_ele {
	u32 m	u32 6 cuility;
	__le16 snt_nforma[S_LENGTH        ]6 snt_nformau data[0]_nformauex[S_EX_LENGTH        ]ta[0]_nformauexu data[0long first_frg;
	sscu8 cen;
	u8d frdelay_bo __attray_bog;
	squesti;
delay_bomp[2];
	__le16 bun;
nterval;
	__le16 cuilinterval;
	u8 curren;
	qod;
wind mediuu8derty the ediuu8dwpa_ie[IE_LEN 64

#dect {
ize_tdwpa_ieu data[0]_nfsn_ie[IE_LEN 64

#dect {
ize_tdfsn_ie  data
	eee80211_tim_parameters {
	u8 timetta[0]_ndod;
} __attr[0]_ndod;
8  chard_bog;
	sdod;
;
	;
	s8 rssi!!!! pum _erder
ctur[0]_nfo 16 normeee80211_wmm_ac_param {
	u8 acaram	u8 aosa r[0]_ctu_EnnsigneONFIG_ITHOMEPORURBOr[0]_Turbo_Ennsign//wspy suturbod frd,  amy for thomas Iincluden;
	C

stryIeLdata[0]_C

stryIeBfr[IE_L4

#dect {!! HT RA, ple,for reo2-22.8.04.29
	    HT	{
	htef	 by Anort h>	//  broadc    ,
nt */
#defi {
	u (1<<form.dsbToS broadc  _ty;uex
/* NsbToS rallnk_ty;uex
/* NsbToS a;
	u} __y;uex
/* NsbToS cisco__y;uex
/* NsbToS u: eap___y;uex
/* N//;
		berty16 normbToSeberty16 ny thiarambToS buupppotu16		 ef	 bw thof uhot	udclassifie */
#def. list_head list;
};

/* NOTE:e80211_statuscode {e   ds/me of decrallye
 *llnky fardwarect i1_NUM_CCK_ROLINKAPOL_Sds/me1_NUM_CCK_TRY_TI* 8NG*tidesder
ms, clinfod frdparam. */

#defis*/
	ye
 * *
 * % RX filu8 	 wiunl6  param. */de {e llyLINKE .amd yol /

#defis*/
	yju*eap_eck remaininde {e LINKE y APamd yIG_ault12.  ROLINKAremaALLaininont dade {es (;
	u16	 wamd yLINKE ME	  ee80)amd / ds/me of questi;
	u8espoceddefiw down[EAP (wq sp_edul    ct i1_NUM_CCK_TRY_TI* 8NG, i1_NUM_CCK_TRY_TI* 8NGIME (H, ds/me of questi;
	u8espoceddefin freed	 wi,
	/*frame {ct i1_NUM_CCK_TRY_TI* 8NGDE   ENTOUP 8NG, ds/me of questi;
	u8espoceddefiha frutity fully cation c pleamd yeasin freed	 wiquesti;
	u8eframe {amd / i1_NUM_CCK_TRY_TI* 8NGDE   ENTOUP ED, ds/me of llnkin fok.e of decraquesti;
d your 
ms, emallnky param.ur 
 {
	 ceeir r
	int wiqut
n  ,
easi cac6	 will hb  para/ i1_NUM_CCK_LINKE , ds/mes
	u alyLINKE  shoul. */

#defis*/
	y puly RX filu8 paramrulestis wotidesataNO_LINKd frdd20se of decrallyurpllparamlogposely llnky  should be caow debusynnitisi{e sdeveyparam. *nld bw dowbe b

#m.urLINKE yde {e.para/ i1_NUM_CCK_LINKE ME	  ee80,
uct iQOS_AIFS ATES_MAP_TAB	   AG
#de5ramHZ)iQOS_AIFS ATES_MAFTS 234e  MAX_RATECF211_DEBUG
/*1<<0RVE_FC
#define WLAN_CAPCF211_DEBUG
/*COMPUTE_FC
#defi1e WLAN_CAPCF211_DEBUG
/*RTS
/* QoS sIEEE80211_FC0_SUBTND    E   NUMBER Le IEEE80211_SOFTMAC_ND    EEL_NUMBER ne  IEEE80211_SOFTMAC_ND     = 36,
	M(1_SOFTMAC_ND    EEL_NUMBER n-    if( !1_FC0_SUBTND    E   NUMBER Ldif

 IEEE80211_SOFTMAC_ND    E   NUMBER L3  IEEE80211_SOFTMAC_ND    EEL_NUMBER ne65 IEEE80211_SOFTMAC_ND     = 36,
	M(1_SOFTMAC_ND    EEL_NUMBER n-    if( !1_FC0_SUBTND    E   NUMBER Ldif

 deunion _f
		u16 te_seed	 wypACKiefi {
gormeee80211_wmm_ac_p8 nr*8 n NOte_seed	 wyp;deunion _f
		u16 _{easwid4,
	utoswitchlemest_fragmeshm_co20Mhzto40Mhz;mest_f	agmeshm_co40Mhzto20Mhz;mebToS bremcmissx20Mhz;mebToS b	utoswitch_ennsigne} {easwid4,
	utoswitcheqos{easwid4,
	utoswitch802.1! amy for reorder
truct eIEEE8021   (1<<_W   Btru	fineIEEE8021   (1<<_PER_Y __a	fineunion _f
		u16 _RX_   (1<<_PER_Yct ieee8021d list;
}	L
/* Nsun;TWOSeq

/* ;eee80211_rxb {
	u8 nr*pr n NO RX_   (1<<_PER_YeqoPRX_   (1<<_PER_Y;.1! amy for reorder
truct enum _InitialGFsynn_odes8t iDG_aultGFsynn, iHWGFsynn, iSWGFsynn
}Fsynn_odes8se
st / in es
ved frdration
 *ed. enum _I	itialGBLE S  (1<ct ieAint s,	 by int s/TC;
inuy.  atity .paeMaxPs,ble iMaxn dBm fs
ved frd.paeF;
	Psble iF;
	n dBm fs
ved frd.p}BLE S  (1<ef enum _InitialGIPNSUPLLBACK_FUNCIONct iIPNSUPLLBACK_     POL_STIPNSUPLLBACK_MGNT_LINK = 10,
	MFIE_STIPNSUPLLBACK_JOIN = 10,
	MFI* MiIPNSUPLLBACK_FUNCIONef enum _InitialGBLEJOIN  14,
	t iBLEJOIN INFTA  MFIE_STBLEJOIN Ims, FIE_TYP(1<< "EA Ims, IE_TYPEA  = 114,
	WFIE_TY}BLEJOIN  14,
	;deunion _f
		u16 _Ib  Parmi{ed     nqod;WiataiIb  ParmieqoPIb  Parmi; MAX_SWEEP_TARATE   4
#264 e iMaxnnl Fragmore
 * format.. */
#:theniMaxnnl Fragex#d2more
 * form: 255. 061122,for rcnjko.e
st RFyde {e.penum _I	itialGBLERFONSTRAI      t ieRfOn, ieRfSleep, ieRfOffY}BLERFONSTRAI     ;deunion _f
		u16 _BLE STRAI AV;
	uER_OL   ds//f	 byIn	int s / in eS
ve(IPN)STRDispy suRFyw *nl_undo8 ccpleam//f	bToSel		bIn	int sPs;f	bToSel		bIPSMld B

#up;f	bToSel		bSwRfPpoce2.11 ;YPEA RFONSTRAI     	eIn	int sP in odes8se;eee8021work_
		u16	In	int sPsWorkIte/* ;eee8021
	s8r_d li	In	int sPspHigr!!!!!! R8)((frpoiefi er
joie a16		  SIPNSUPLLBACK_FUNCION	R8)((fPoief!!!!!! R8cthe dPs {
	u8 tider

ssp_edule dJoieRrame {ambToSel		bTmpBssctrl;STBLEJOIN  14,
			tmpJoieA16		 ef	eee80211_info_elenetworkltmpBssctrl;S!!!! R8cthe dPs {
	u8 tider

ssp_edule dMgntLlnkRrame {ambToSel		bTmpScanOnly;ambToSel		bTmp int sScan;ambToSel		bTmpFilu8 HiamynAP;ambToSel		bTmpUpdes8Parmi; [0]TWORKtmpSsidBfr[33ct {OCT(fc) Re80ORKtmpSsid2Scan;ambToSel		bTmpSsid2Scan;am0]TWORKtmpNetworkT versionTWORKtmpC	u8 co

/bSSID,un;TWORKtmpBcnP __attr[0]TWORKtmpD
	sP __attr[0n;TWORKtmpmCapt {OCT(fc) Re80ORKtmpSore   /S      ]TWORKtmpSore   /Bfr[IE_LRATE   4
];ambToSel		bTmpSore   /;amIb  ParmiWORKtmpIbpm;ambToSel		bTmpIbpm;ads//f	 byLeisrs / sin eS
veSTRDispy suRFyif do8 ccpleshoul.ra
 * allye
 *busyam//f	bToSel		bLeis *ePs;fNO RLE STRAI AV;
	uER_OLeqoPRLE STRAI AV;
	uER_OLef union _fr32 EA RFONUMBGE_SOURCE; MAX_SWEERFONUMBGE_BY_SW BIT31 MAX_SWEERFONUMBGE_BY_HW BIT30 MAX_SWEERFONUMBGE_BY_PS BIT29 MAX_SWEERFONUMBGE_BY_IPS BIT28 MAX_SWEERFONUMBGE_BY_INIT	0	 byDose
 * 	u8ges. */RFOff
} __at. DX_SWE for Be8012-22.8-01-17.f enum _Initiact i= 7,
	M
	uDE_FCC POL_ST= 7,
	M
	uDE_,
	MF1_ST= 7,
	M
	uDE_ETSIFIE_TYP= 7,
	M
	uDE_SPAIN IE_TYP= 7,
	M
	uDE_FRANCEFIE_TYP= 7,
	M
	uDE_MKKFIE_TYP= 7,
	M
	uDE_MKK1FIE_TYP= 7,
	M
	uDE_,SRAELFIE_TYP= 7,
	M
	uDE_TELECTYP= 7,
	M
	uDE_MICTYP= 7,
	M
	uDE_GLOBAL_DOMAIN
}c

stry_uld u8 quyp;deMAX_SWEERMAP_TALD<<10) __a	f0eunion _f
		u16 _BLELINK DE(1<<_T  dsa32el		

/alPoBcnInP __attr[032el		

/alPoset InP __attrr[032el		RxBcn

/[RMAP_TALD<<10) __aa addnnl ber.nd ixfor prob/ C_eckForHang_} __at rmine what typllnkid__le1r[032el		Rxset 

/[RMAP_TALD<<10) __aa addnnl ber.nd ixfstruc/ C_eckForHang_} __at rmine what typllnkid__le1r[0n;TWORSlot

/*addnnl ber.nd C_eckForHang } __at mine what typllnkid__le1r[0n;TWORSlotI8 rts_r[032el		

/TxOkInP __attr[032el		

/axOkInP __attr[bToSel		bBusyTra
 * ;Y}BLELINK DE(1<<_TeqoPRLELINK DE(1<<_T802.eee80211_info_ele
#inclct ieee8021nete
#inclc*
#ief	eee80211_info_ele {
	u16 a {
;ads//hw  {
	u16 aBA, pleN//;
	 hw {
_more
 *0-10more
 *?
;
	 hw {
_	int sua ofhw  {
	u16 a	int s.dsbToS is_silTH_A:1;
* NsbToS s_sequp;f	by amy
	u8      bToS bS;
	u8 Remote;   Up;STBLE S  (1<	dot11P in o
veMld 0-100/ in es
ved frdration
 *ed. sbToS actscu8 11 ;YPbToS beinmit_e;YPEA RFONSTRAI     	ieRFP in odes8se;EA RFONUMBGE_SOURCE	RfOffR __attrpbToS is_setekeSID, /190 s 8, BA, pleaI1wog dayif ol notinfo;e */
#defi be 2.  Themove 2ooulnd 1_info_ele
#incl
D, /190 HThis li
	PRLEHIGH_THROUGHPUT	pHTion ID, /eee8021
	s8r_d li		SwBwpHigr!!//;spinlock_ * 	nlop_spinlockef	epinlock_ *bw_spinlockeff	epinlock_ *
struct_spinlockef	100der
HTho} _;
	u8ef {e set.  wotndedgmentdefider
HThstrucf {e rt me	u8 {e dinsignfoddsspriptorsf	10. */way fill stolen finind
	u alyata. */IE   ]TRegdot11HTO} _;
	u8al   /S  [16]a addnded   R n_elem   ]Tdot11HTO} _;
	u8al   /S  [16]a addnded   R n_elem   ]TRegHTSore   /S  [16]a   ]TWORHTCp[ETH_O} _;   /;am ]TWORHTool e {O} _;   /;am//wb  amy fder

 {e o} _;
	u8e frdrrt firmvideam ]TbTxDispy s   /FallB

#;am ]TbTxUseD
#defA2.11 ed   /;amatom
	u1matm_ 	nlop;amatom
	u1matm_swbw N//;
		HTool e {O} _;   /;a//;
		HTCp[ETH_O} _;   /;af	100*/
#dee
easiWMM Tra
 *  [4];amtion  (TX) ieee8021d list;
}		Tx_TS_AdededL
/* Nseee8021d list;
}		Tx_TS_Peed	 wyL
/* Nseee8021d list;
}		Tx_TS_UnuuppyL
/* NsTX_TS_RECORD		TxTsRS_QUEUTOTAL_TS__le16 t100*/
#dee
easiWMM Tra
 *  [4];amtion  (RX) ieee8021d list;
}		Rx_TS_AdededL
/* Nseee8021d list;
}		Rx_TS_Peed	 wyL
/* Nseee8021d list;
}		Rx_TS_UnuuppyL
/* NsRX_TS_RECORD		RxTsRS_QUEUTOTAL_TS__le16 //ONFIG_ITO_DOELISTNsRX_   (1<<_PER_Y	RxRstructEstry[128ct list_head list;
}		RxRstruct_UnuuppyL
/* Nstrunclude100Qos BA, ple.y Annie, 2005-12-22.
	u1-01.a//;PASSO

#d		podeQoi; [0]TWORFemcmi;

	//1!!ble iFemcm } _-ength: p

	//1! 1~7. (IG_ault:OL_se
 *rt femcm it.

 ds/meBookkeepe wie */
#defruct ieee8021nete
#incl
	u32 m	u32 6 ceee80211_stats {
	unsigs_seq	u32 6 ceee80211_stats {
	can_syncunsi 	can_syncunsi; ds/mePponsc/ //cosa
nt */
#defict ieee8021d list;
};network_frseql
/* Nseee8021d list;
};network_l
/* Nseee80211_info_elenetworkl*networkactive;
scu8active;
scu8_*/
; dsve;
iw
	   porteo} _;
	 wi frdr(IW  (1<_*)uct ieee80211w_spy	strucspy	strueff	epinlock_ *lockef	epinlock_ *wpinteuitd lislockeff	_frate_t;
}roomporteS   rt m sloast
ne afdi
	u8al roomi be y fardfro    f( !*FragLenoc pleaTelSKB receiv32 ation
; ds/me    easiont dad;
	u8 	u8efr, pleathese wshof uhot
#inclcu16 frecei_fraor;
	wepporteS   rt 1m.ur 16 m und;
	u8 quwith sufecei_frae:2,
	   ctive;
:1;
*_onekeS 	u8geporteS   rt 1miassifiHW  be 12.  The:1;
* 	  S f( me    keS* 	u8gesd / ds/meIassifihoLE* *
 * %s {en,de}
	u8 	u8,m. *nls   rt 1mecei_frahoLE_d;
	u8 ;ei_frahoLE_d;
	u8 ze;
	;ei_frahoLE_ble;
	uID, suhoLE* *
 * %s t_octets; ble;
	u	u8eecei_frahoLE_mc_ble;
	uIDD, suhoLE*e notedeeeip IV easiICV
     ppotu16quwith sufecei sua_rae wfule 8, aw *nl providesble;
	u	u8eis beingsuuppoecei_frahoLE_eeeip_iv_icveff	_frahoLE_or;
	 {
gorm_frahoLE_build_ivorm_fra1_info__1x100 kbs2.11 requirXsuuppoeceds/me PAhstruc*/YPbToS bHalfWifr,essN24GM   ctive;
wpa_1,
	   ctive;
}rop_und;
	u8 quctive;
tkip_c

sthat __defrctive;
p

vacy_invokuct ieize_tdwpa_ieu data[0]_*wpa_ieta[0]_ap__syn_ALEN ethertyppairwis
	   u16       Le
	   	   u16     eee8021d list;
};
	u8 zdeindedl
/* Nseee80211_info_ele
	u8 zdtruc*
	u8 S];
	u8 keys[_frate_   idx100 kIG_aultiTXnkeS*u8 rt (
	u8 Ste_   idx])uct ieee8021
	s8r_d li;
	u8 zdeindedtHigr!![_fra
	u8 zquisspedeff	_frabc {
	u3ekeSI00 kunotindividacke_KEY rt overridekIG_aulti_KEY 16 n  f( ! meficieRXFragbroad/t_octets; ith sufeceds/meF[0];
};;
	u8ee */
#defruct ie iry cas4];ame sizC;
	bo aan conNseee80211_info_elery {
	unsigry {
ze witruc[1_FRAG_CACHE_LEN 4

strugned long paciefi {
genext_idx16 frag_num[si; /meF[0];
};;
	u8eTgmeshm_cine NETWORK_RATES_MASTS_THRESHOLD 234eU MAX_SWEEPIN =TS_THRESHOLD 1 MAX_RATES_EX_TS_THRESHOLD 234eU bun;
}si; /meRTS
tgmeshm_cine ds/meAuesti;
	u8einfo;eceived{
	idN];
	u8 src[D, suoln fromrmation e remaininframes, network.amd yEint da hesnetworklwotidesquesti;
d ybo INFTASTRUCTUREamd yoda hesnetworkl suppwotides cac6	 wiataMASTERd frd.pad yed-ho allya mix#defi;-).pad yNo{e rsuppi8einfrae */
#defi frd, 16 naw *nlhardwuesti;
d  paramd;
}
sd{
	id easie
	id may The thiacon),wpa_s   easie
	id_s  pad yedes.rue) alythnsumrrya hes the ls   bya hesunor via11wation
para/ ieee80211_info_elenetworklap[ETH_Anetworkc[D,e80211_statuscode {e sdes8se
ive;
s;
	usilo	ctive;
:1g
	   ctive;
	   po (1<<3 suA, B, Goecei_fra	  uls8   ;3 suCCK, (1<<1ecei_fra16 l_{eas;<3 su2.4Ghz, 5.2Ghz, Mixppoecei_fraabg_.rue;<<3 suABGo __a      1
#definne ds/me passing femc	 will h {
	 workdex;
 rt what t plpad yficiooulwait remainindynnitissed rt what t plpad / ie;
	undynnsscu8_hp[Eyup;ftive;
p*
 u16_8 signave;
wo;
	u8 signhertypprev_
	u8 addo (1<<3 suuupport }rop dupl{
	se ith sufeceds/memapFragLenowx_mp_g8 cos. 0be caummyuct ie iFIXME:ng se ber.mine _aulti.ur 
bas cha_g8 co pled deseed	 wclassifiPHYa {
e
	voiac*pDot11dI6 normbToS bGlobalDoes ictive;
:  // In 0.3 sureception	se ecei_frabas c]r6 surp//FIXME:nplry f dellb

#,ls emiasredundae;
wiciencan_synfac6defr ie;
	uney:2,
	scan;ads/me oisizC;
	boso __ats fran
	u16	6 fy:1,
	  encan_sy2more
 * eceivbandcan_synfac6defr;ads/meiassifi counter;
	u8 mamd;
}
allye
 *fillnie, 2HW eceivband	u8 arl[5rc[D, suquesti;
	u8espoceddefiion;
	__le1i counter;nl ber.eceivbanwuesti;
d_signeds/meAIDs fraRTXediquesti;
	u8efrrame {s.eceivbanwuest addrds/me dBm fs
ved frdefr, ple / ie;
	unp 6 ce;
	undu3esleepctive;
psdtHigoouctive;
psd} __attr[eee8021
asklet_
		u16
psdt freq;
datasdtheq;
datasdtleff	eh
 * fow_tx;ds/me passiasS_MACASSOC_RETXUM];
}in fretd / ie;
	undex;
	stopnede;
	undcu8 11 ;YPe;
	unppotoime;
	edeff	k_buff *emap;
	e wnter/* ;eee8021*emap;
	e scu8_er/* f	epinlock_ *mgmedtx_lockef	epinlock_ *btervallockeff	e;
	unime;
	u8x11 ;Yf	e;
	unwap_;
* Nse;
	und	id_s  ;
diuu8dwpaxu8 quy        //{ amy for Dfine2-22.6.9.28}q;
datwpaxu8 quye
 if 0-10{ amy for Dfine2-22.6.9.26}rds/mecturfr, plea __a  / ip_getindedwmmunt;
} __a;ds/me;
* 	 tindeithizau	u8eeceiuu8d {
	more
 *0rds/me fra_undecr typeupl{
	sediime[17yybo Ims, ct ieee8021d list;
}; {
	u_synhash[S_MAC_HASH_SIZE 31
stru]0rds/me fra_undecr typeupl{
	sediime[17yybo ms, ct iuilin;
	srx17];
	u16 fr3 surxi coppreviy.  } _-tid ct iuilin;
	srx[17];
	unsign/me xi {
gppreviy.  } _-tid ct iulong first_frg;
	sime[17];
	struct ds/me fraPSd frdect iulong first_frg;
	sge_ssu32 minds/me passiasS_MACASSOC_RESINGLEUM];
}in fretd / ie_buff *subframemgmeddex;
	 	 w[MGMTUM];
	__le16 t_fra	gmeddex;
	t;
}6 t_fra	gmeddex;
	
	bl Nst{  amy fder

u8  9x IEEE80211_OFDM_RATE];
	_LIMITefineiuu8AestRit_eC8 tim_pelong paciefihw_	__le16 ce_buff *subfrast;
};skb_waitQ[IE_LE];
	_stru]0rce_buff *subfrast;
};;skb_aggQ[IE_LE];
	_stru]0rce_buff *subfrast;
};;skb_frames;Q[IE_LE];
	_stru]0rc032edu3eedcam	u8 aosa r[bToS aon.
	s8   ;dse iEnnsig/Dispy suRx imime;	se BAity;
	__le1. sbToS 1,
	  sge_imi_BAormbToS b {
	coocr taomr;af	10+or reorder
<<1,080515f	10DynamecaTeledBm fder
near/far

 8gesennsig/Dispy su ,for reo 2-22.8-05-15f	bToSebdynamecu8xedBm _ennsignermbToS bCTS;		//cEnnsigned 8	CTS;		//cTHs_r[032efdynnsmp[2];
	u8 curren32efdynnsr6 s_bin_spned 8	fdynnsrd	i_tgmeshm_ctr[bToSebfdynnsennsignerm 8	fdynns_retry_frmp[2;
	u8 curble iFdynnMretry_fpHigI
	u8 cud yFdynnpHigI
	u8 curen32efdynnsag_tidinssr6 stgmeshm_ctble i line>meshm_cren32efdynnsse;
	ddinssr6 stgmeshm_ctb-100ble; supne>meshm_creFsynn_odes8 f(fdynnssdes8se;bToSelbis_anyye
nbepk2 6 c//20Mhz 40Mhz AutoSwitchlT>meshm_cre{easwid4,
	utoswitchl{easwid4,
	uto_switch80	l8185
8xedBm iionck	 wambToS FwRWRF!!!!!! amy for reorder
AP roam	 wamRLELINK DE(1<<_T	LlnkDetu16ion ID, / amy for reorder
    RLE STRAI AV;
	uER_OL	P in o
veC	u8 mask//}ds/me passiasS_MACASSOC_RETXUM];
}in fretd / ie_buff  te_seed	 wyp te_seed	 winds/me passiasS_MACASSOC_RETRY_TI* }in fretd / ie_buff 
	s8r_d li;wuesti;
d_tHigr!!!!!me passiasS_MACASSOC_RE0x0080Sin fretd / ie_buff 
	s8r_d li;ime;
	u8 ni16 ce_buff work_
		u16;wuesti;
d_comy_f
d_wq6 ce_buff work_
		u16;wuesti;
d_spoceddef_wq6 ce_buff und;
ed_worklccan_synccu8_wq6 ce_buff und;
ed_worklwuesti;
d_mit_exwq6 c e_buff und;
ed_worklce;
	u {
	uwq6 ce_buff work_
		u16;wnteynnsscu8_wq6 ce_buff workdex;
	stbuff *wq6 c100Qos BA, ple.y Annie, 2005-12-22.
	u1-01.ac10ASSO

#  odeQoi; ac10
datASSOEDCAUB_TYPosa r[//NUMBER  ACCE 6,
	M 8NG C	u8 coAtity Shese w;
 ds/meCellb

# fun__le1s.eceivoiac(*;
*_ {
	u16 )ieee8021nete
#inclc*
#i,  f( !   eee80211_info_ele {
	u16 a*;
c)!!!!!meUupport TXame from AP
by  suspd8 nreee802s.param. olen fe
 * passiasata. */ncan_synfac6defr i *in fretd. */ __a S_MACASSOC_RETXUM];
}pad / iiefi(* proime;
	uxmit)ieee80211_hdr_3add8 nr*8 n,  f( !     eee8021nete
#inclc*
#i);ftive;
(*:1;
*_e
 *)ieee8021nete
#inclc*
#i);tive;
(*is_dex;
	full) ieee8021nete
#inclc*
#i, ve;
p

);ftive;
(*h>	// _nt */
#def) ieee8021nete
#inclc*
#i,  if( !eee80211_info_elenetworkl*network, :8;
	retu;tive;
(*is_dramet6	6 ) ieee8021nete
#inclc*
#i, k_buff *subframeskb)!!!!!meScan_sy-gen _;
quwith suf(nt */
#def) edesTXedivia1. ol i *idellb

#eiassifi __a S_MACASSOC_RESINGLEUM];
}in  i *ie
 *set. Ai 	cmf decrs may h
veddinsignfodHW dex;
TA supamd yo021mighunwanti.urunot fra_e fr APs
 **/
#defi {
	u param. */opdeterminh
vedtwo dellb

#s1mighunfor paful.amd yolent
un__le1psed' *sleep.pad / iiefi(*ncan_syn proime;
	uxmit)ieee8021*subframeskb,  f( !     eee8021nete
#inclc*
#i);fti!me passinst;
};of  proime;
	uxmit (e
 *scan_syn proime;
	uxmit) i *infa. */IEMACASSOC_RETXUM];
}ifac6defin fuupport TXame fparamdhis defIm. */opdeterS_MACASSOC_RESINGLEUM];
}in falst metparam. *nlalst 
 **/
#defi {
	u  edes;
ntivia1. ol dellb

#.amd yolent
un__le1psed' *sleep.pad / ivoiac(*;can_syne;
	u proime;
	uxmit)ieee8021*subframeskb,  f( !     eee8021nete
#inclc*
#i, ve;
_;
q);fti!mestopsssifiHW dex;
 der
<ATAmdhis defUpafuli.ur voiapad yf;
	e 
	s8ort TXame from AP
w *nlwotidesrequestic6	 wamd yolent
un__le1psed*sleep.pad / ivoiac(*e;
	u proimeop)ieee8021nete
#inclc*
#i);tti!meOKm. olen fcomy_f;
};;r.mine;
	upollu proimeopd / ivoiac(*e;
	u proi:1;ume)ieee8021nete
#inclc*
#i);tti!meas#m.ur. */

#defi.uru8)(nes. */radio .amd yolent
un__le1psed*sleep.l. */

#defis*notedensdefparam. */radio hn for*nlswicicnie,efodesre)((f.pad / ivoiac(*;etea_g8)ieee8021nete
#inclc*
#i,
s;
	u ch);tti!meol notidesn
 * passiasll h _inlce;
#m.akel derloasparamdcu8 11 M(1_SO_SCAN_TIME	  ifac6defi;et).pad yIn1. ol denot 8, asifi ctea_g8in fuupp.pad amd yol /dynniti
	u8 rxin frimil;r.minininde r	sscu8shouamd yIoe fe
 *u8)((fr tiildwarep_g8 cos hn for*nlscu8 ce.param. olen fdelld ybo unor 
	u8ex  easie notedeleep, i *inten fdelld ybo a work_dex;
 w *nlswicice wilo ed-ho a frdparamoetinforhalflnd 1wd li;scu8sw *nl of decrallywuesti;
d amd yeasiro
 * pareas#m fra ;scu8.param. */
un__le1pmeopsscu8se notedeeopdbothainindynniti APamd yb

#
	  asiecu8 11 Measi ed*sleep.pad yol /
un__le1pme r	sscu8se notedindeittes. */b

#
	  asparamdcu8 11 Measi ed' *sleep.pad / ivoiac(*;cu8_eynnit)ieee8021nete
#inclc*
#i);tivoiac(*;e r	sscu8)ieee8021nete
#inclc*
#i);tivoiac(*;eopsscu8)ieee8021nete
#inclc*
#i);tti!meindicttes. */

#defi.hof uhotllnkid__lfin f 	u8gesparam fraexamy_fintemay indicttes. */decrallywuesti;
d fe
w.pad yD
#def1mighunfor;
	u8este data. iY rt  puly RX filu8 paramrulestfranimply llghun. */LINKdlespara/ ivoiac(*llnk_t	u8ge)ieee8021nete
#inclc*
#i);tti!metl nottwo 
un__le1pindicttes.minininHW w *nl opme r	amd yeasieeopdrt mend;ime;
	defT olen f passw *nl ofpad yI_MACASSOC_RE0x0080Sin fe
 *set. Fer
nline>fpad y;eopssend_be;
	din fNOT guarantbe 2.  Thedelld y 8, amd yeft dade r	ssend_bme;
	depad / ivoiac(*;e r	ssend_bme;
	d) ieee8021nete
#inclc*
#i,:8;
	}__;
q);fivoiac(*;eopssend_bme;
	d) ieee8021nete
#inclc*
#i)drds/me dBm fs
ved frdefr, pled / ivoiac(*;e _wakg_up) (eee8021nete
#inclc*
#i);tivoiac(*ps_frame {
_liack) (eee8021nete
#inclc*
#i);tivoiac(*e
	u8esleepssdes8) ieee8021nete
#inclc*
#i, y_bomh, y_bomurn (s;
	u (*ps_is_dex;
	emp6 ) (eee8021nete
#inclc*
#i);tive;
(*h>	// _ RSN */ ieee8021nete
#inclc*
#i, k_buff 1_beacon ieee8021*eee802, k_buff 1_beacon inetworkl*network);tive;
(*h>	// _sponse_frame {/ ieee8021nete
#inclc*
#i, k_buff 1_beacon isponse_frame {
	struc*:1;p, k_buff 1_beacon inetworkl*network);trds/mep_eck w *nt daTx hwefrroumcm avail;y su / ie;
	un(*p_eck_ninsenough_dssp)ieee8021nete
#inclc*
#i,
ve;
dex;
	u8 rt)ID, / amy for wbider
HThBA, pleN//;voiac(*SwChnlBypHigrH>	// r)ieee8021nete
#inclc*
#i,
ve;
p_g8 co);tivoiac(*SetBWM   H>	// r)ieee8021nete
#inclc*
#i,
45,
UMBER  WIDTH Beaswid4,,
45,EXTCHNL_OFF
	MFOff;et);N//;voiac(*Updes8Hal   RT;y sH>	// r)ieee8021nete
#incl kIGi, y8me McsR;
q);fibToS (*GetN frdS;
	u8 BySecCfg)ieee8021nete
#inclc*
#i);tivoiac(*SetWifr,essM   )ieee8021nete
#inclc*
#i,
0]_nifr,ess
	   );fibToS (*GetHalfN frdS;
	u8 ByAPsH>	// r)ieee8021nete
#inclc*
#i);tivoiac(*IndeithGainH>	// r)ieee8021net