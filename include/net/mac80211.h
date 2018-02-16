/*
 * mac80211 <-> driver interface
 *
 * Copyright 2002-2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007-2010	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MAC80211_H
#define MAC80211_H

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <asm/unaligned.h>

/**
 * DOC: Introduction
 *
 * mac80211 is the Linux stack for 802.11 hardware that implements
 * only partial functionality in hard- or firmware. This document
 * defines the interface between mac80211 and low-level hardware
 * drivers.
 */

/**
 * DOC: Calling mac80211 from interrupts
 *
 * Only ieee80211_tx_status_irqsafe() and ieee80211_rx_irqsafe() can be
 * called in hardware interrupt context. The low-level driver must not call any
 * other functions in hardware interrupt context. If there is a need for such
 * call, the low-level driver should first ACK the interrupt and perform the
 * IEEE 802.11 code call after this, e.g. from a scheduled workqueue or even
 * tasklet function.
 *
 * NOTE: If the driver opts to use the _irqsafe() functions, it may not also
 *	 use the non-IRQ-safe functions!
 */

/**
 * DOC: Warning
 *
 * If you're reading this document and not the header file itself, it will
 * be incomplete because not all documentation has been converted yet.
 */

/**
 * DOC: Frame format
 *
 * As a general rule, when frames are passed between mac80211 and the driver,
 * they start with the IEEE 802.11 header and include the same octets that are
 * sent over the air except for the FCS which should be calculated by the
 * hardware.
 *
 * There are, however, various exceptions to this rule for advanced features:
 *
 * The first exception is for hardware encryption and decryption offload
 * where the IV/ICV may or may not be generated in hardware.
 *
 * Secondly, when the hardware handles fragmentation, the frame handed to
 * the driver from mac80211 is the MSDU, not the MPDU.
 */

/**
 * DOC: mac80211 workqueue
 *
 * mac80211 provides its own workqueue for drivers and internal mac80211 use.
 * The workqueue is a single threaded workqueue and can only be accessed by
 * helpers for sanity checking. Drivers must ensure all work added onto the
 * mac80211 workqueue should be cancelled on the driver stop() callback.
 *
 * mac80211 will flushed the workqueue upon interface removal and during
 * suspend.
 *
 * All work performed on the mac80211 workqueue must not acquire the RTNL lock.
 *
 */

struct device;

/**
 * enum ieee80211_max_queues - maximum number of queues
 *
 * @IEEE80211_MAX_QUEUES: Maximum number of regular device queues.
 * @IEEE80211_MAX_QUEUE_MAP: bitmap with maximum queues set
 */
enum ieee80211_max_queues {
	IEEE80211_MAX_QUEUES =		16,
	IEEE80211_MAX_QUEUE_MAP =	BIT(IEEE80211_MAX_QUEUES) - 1,
};

#define IEEE80211_INVAL_HW_QUEUE	0xff

/**
 * enum ieee80211_ac_numbers - AC numbers as used in mac80211
 * @IEEE80211_AC_VO: voice
 * @IEEE80211_AC_VI: video
 * @IEEE80211_AC_BE: best effort
 * @IEEE80211_AC_BK: background
 */
enum ieee80211_ac_numbers {
	IEEE80211_AC_VO		= 0,
	IEEE80211_AC_VI		= 1,
	IEEE80211_AC_BE		= 2,
	IEEE80211_AC_BK		= 3,
};
#define IEEE80211_NUM_ACS	4

/**
 * struct ieee80211_tx_queue_params - transmit queue configuration
 *
 * The information provided in this structure is required for QoS
 * transmit queue configuration. Cf. IEEE 802.11 7.3.2.29.
 *
 * @aifs: arbitration interframe space [0..255]
 * @cw_min: minimum contention window [a value of the form
 *	2^n-1 in the range 1..32767]
 * @cw_max: maximum contention window [like @cw_min]
 * @txop: maximum burst time in units of 32 usecs, 0 meaning disabled
 * @acm: is mandatory admission control required for the access category
 * @uapsd: is U-APSD mode enabled for the queue
 */
struct ieee80211_tx_queue_params {
	u16 txop;
	u16 cw_min;
	u16 cw_max;
	u8 aifs;
	bool acm;
	bool uapsd;
};

struct ieee80211_low_level_stats {
	unsigned int dot11ACKFailureCount;
	unsigned int dot11RTSFailureCount;
	unsigned int dot11FCSErrorCount;
	unsigned int dot11RTSSuccessCount;
};

/**
 * enum ieee80211_chanctx_change - change flag for channel context
 * @IEEE80211_CHANCTX_CHANGE_WIDTH: The channel width changed
 * @IEEE80211_CHANCTX_CHANGE_RX_CHAINS: The number of RX chains changed
 * @IEEE80211_CHANCTX_CHANGE_RADAR: radar detection flag changed
 * @IEEE80211_CHANCTX_CHANGE_CHANNEL: switched to another operating channel,
 *	this is used only with channel switching with CSA
 * @IEEE80211_CHANCTX_CHANGE_MIN_WIDTH: The min required channel width changed
 */
enum ieee80211_chanctx_change {
	IEEE80211_CHANCTX_CHANGE_WIDTH		= BIT(0),
	IEEE80211_CHANCTX_CHANGE_RX_CHAINS	= BIT(1),
	IEEE80211_CHANCTX_CHANGE_RADAR		= BIT(2),
	IEEE80211_CHANCTX_CHANGE_CHANNEL	= BIT(3),
	IEEE80211_CHANCTX_CHANGE_MIN_WIDTH	= BIT(4),
};

/**
 * struct ieee80211_chanctx_conf - channel context that vifs may be tuned to
 *
 * This is the driver-visible part. The ieee80211_chanctx
 * that contains it is visible in mac80211 only.
 *
 * @def: the channel definition
 * @min_def: the minimum channel definition currently required.
 * @rx_chains_static: The number of RX chains that must always be
 *	active on the channel to receive MIMO transmissions
 * @rx_chains_dynamic: The number of RX chains that must be enabled
 *	after RTS/CTS handshake to receive SMPS MIMO transmissions;
 *	this will always be >= @rx_chains_static.
 * @radar_enabled: whether radar detection is enabled on this channel.
 * @drv_priv: data area for driver use, will always be aligned to
 *	sizeof(void *), size is determined in hw information.
 */
struct ieee80211_chanctx_conf {
	struct cfg80211_chan_def def;
	struct cfg80211_chan_def min_def;

	u8 rx_chains_static, rx_chains_dynamic;

	bool radar_enabled;

	u8 drv_priv[0] __aligned(sizeof(void *));
};

/**
 * enum ieee80211_chanctx_switch_mode - channel context switch mode
 * @CHANCTX_SWMODE_REASSIGN_VIF: Both old and new contexts already
 *	exist (and will continue to exist), but the virtual interface
 *	needs to be switched from one to the other.
 * @CHANCTX_SWMODE_SWAP_CONTEXTS: The old context exists but will stop
 *      to exist with this call, the new context doesn't exist but
 *      will be active after this call, the virtual interface switches
 *      from the old to the new (note that the driver may of course
 *      implement this as an on-the-fly chandef switch of the existing
 *      hardware context, but the mac80211 pointer for the old context
 *      will cease to exist and only the new one will later be used
 *      for changes/removal.)
 */
enum ieee80211_chanctx_switch_mode {
	CHANCTX_SWMODE_REASSIGN_VIF,
	CHANCTX_SWMODE_SWAP_CONTEXTS,
};

/**
 * struct ieee80211_vif_chanctx_switch - vif chanctx switch information
 *
 * This is structure is used to pass information about a vif that
 * needs to switch from one chanctx to another.  The
 * &ieee80211_chanctx_switch_mode defines how the switch should be
 * done.
 *
 * @vif: the vif that should be switched from old_ctx to new_ctx
 * @old_ctx: the old context to which the vif was assigned
 * @new_ctx: the new context to which the vif must be assigned
 */
struct ieee80211_vif_chanctx_switch {
	struct ieee80211_vif *vif;
	struct ieee80211_chanctx_conf *old_ctx;
	struct ieee80211_chanctx_conf *new_ctx;
};

/**
 * enum ieee80211_bss_change - BSS change notification flags
 *
 * These flags are used with the bss_info_changed() callback
 * to indicate which BSS parameter changed.
 *
 * @BSS_CHANGED_ASSOC: association status changed (associated/disassociated),
 *	also implies a change in the AID.
 * @BSS_CHANGED_ERP_CTS_PROT: CTS protection changed
 * @BSS_CHANGED_ERP_PREAMBLE: preamble changed
 * @BSS_CHANGED_ERP_SLOT: slot timing changed
 * @BSS_CHANGED_HT: 802.11n parameters changed
 * @BSS_CHANGED_BASIC_RATES: Basic rateset changed
 * @BSS_CHANGED_BEACON_INT: Beacon interval changed
 * @BSS_CHANGED_BSSID: BSSID changed, for whatever
 *	reason (IBSS and managed mode)
 * @BSS_CHANGED_BEACON: Beacon data changed, retrieve
 *	new beacon (beaconing modes)
 * @BSS_CHANGED_BEACON_ENABLED: Beaconing should be
 *	enabled/disabled (beaconing modes)
 * @BSS_CHANGED_CQM: Connection quality monitor config changed
 * @BSS_CHANGED_IBSS: IBSS join status changed
 * @BSS_CHANGED_ARP_FILTER: Hardware ARP filter address list or state changed.
 * @BSS_CHANGED_QOS: QoS for this association was enabled/disabled. Note
 *	that it is only ever disabled for station mode.
 * @BSS_CHANGED_IDLE: Idle changed for this BSS/interface.
 * @BSS_CHANGED_SSID: SSID changed for this BSS (AP and IBSS mode)
 * @BSS_CHANGED_AP_PROBE_RESP: Probe Response changed for this BSS (AP mode)
 * @BSS_CHANGED_PS: PS changed for this BSS (STA mode)
 * @BSS_CHANGED_TXPOWER: TX power setting changed for this interface
 * @BSS_CHANGED_P2P_PS: P2P powersave settings (CTWindow, opportunistic PS)
 *	changed (currently only in P2P client mode, GO mode will be later)
 * @BSS_CHANGED_BEACON_INFO: Data from the AP's beacon became available:
 *	currently dtim_period only is under consideration.
 * @BSS_CHANGED_BANDWIDTH: The bandwidth used by this interface changed,
 *	note that this is only called when it changes after the channel
 *	context had been assigned.
 */
enum ieee80211_bss_change {
	BSS_CHANGED_ASSOC		= 1<<0,
	BSS_CHANGED_ERP_CTS_PROT	= 1<<1,
	BSS_CHANGED_ERP_PREAMBLE	= 1<<2,
	BSS_CHANGED_ERP_SLOT		= 1<<3,
	BSS_CHANGED_HT			= 1<<4,
	BSS_CHANGED_BASIC_RATES		= 1<<5,
	BSS_CHANGED_BEACON_INT		= 1<<6,
	BSS_CHANGED_BSSID		= 1<<7,
	BSS_CHANGED_BEACON		= 1<<8,
	BSS_CHANGED_BEACON_ENABLED	= 1<<9,
	BSS_CHANGED_CQM			= 1<<10,
	BSS_CHANGED_IBSS		= 1<<11,
	BSS_CHANGED_ARP_FILTER		= 1<<12,
	BSS_CHANGED_QOS			= 1<<13,
	BSS_CHANGED_IDLE		= 1<<14,
	BSS_CHANGED_SSID		= 1<<15,
	BSS_CHANGED_AP_PROBE_RESP	= 1<<16,
	BSS_CHANGED_PS			= 1<<17,
	BSS_CHANGED_TXPOWER		= 1<<18,
	BSS_CHANGED_P2P_PS		= 1<<19,
	BSS_CHANGED_BEACON_INFO		= 1<<20,
	BSS_CHANGED_BANDWIDTH		= 1<<21,

	/* when adding here, make sure to change ieee80211_reconfig */
};

/*
 * The maximum number of IPv4 addresses listed for ARP filtering. If the number
 * of addresses for an interface increase beyond this value, hardware ARP
 * filtering will be disabled.
 */
#define IEEE80211_BSS_ARP_ADDR_LIST_LEN 4

/**
 * enum ieee80211_rssi_event - RSSI threshold event
 * An indicator for when RSSI goes below/above a certain threshold.
 * @RSSI_EVENT_HIGH: AP's rssi crossed the high threshold set by the driver.
 * @RSSI_EVENT_LOW: AP's rssi crossed the low threshold set by the driver.
 */
enum ieee80211_rssi_event {
	RSSI_EVENT_HIGH,
	RSSI_EVENT_LOW,
};

/**
 * struct ieee80211_bss_conf - holds the BSS's changing parameters
 *
 * This structure keeps information about a BSS (and an association
 * to that BSS) that can change during the lifetime of the BSS.
 *
 * @assoc: association status
 * @ibss_joined: indicates whether this station is part of an IBSS
 *	or not
 * @ibss_creator: indicates if a new IBSS network is being created
 * @aid: association ID number, valid only when @assoc is true
 * @use_cts_prot: use CTS protection
 * @use_short_preamble: use 802.11b short preamble;
 *	if the hardware cannot handle this it must set the
 *	IEEE80211_HW_2GHZ_SHORT_PREAMBLE_INCAPABLE hardware flag
 * @use_short_slot: use short slot time (only relevant for ERP);
 *	if the hardware cannot handle this it must set the
 *	IEEE80211_HW_2GHZ_SHORT_SLOT_INCAPABLE hardware flag
 * @dtim_period: num of beacons before the next DTIM, for beaconing,
 *	valid in station mode only if after the driver was notified
 *	with the %BSS_CHANGED_BEACON_INFO flag, will be non-zero then.
 * @sync_tsf: last beacon's/probe response's TSF timestamp (could be old
 *	as it may have been received during scanning long ago). If the
 *	HW flag %IEEE80211_HW_TIMING_BEACON_ONLY is set, then this can
 *	only come from a beacon, but might not become valid until after
 *	association when a beacon is received (which is notified with the
 *	%BSS_CHANGED_DTIM flag.)
 * @sync_device_ts: the device timestamp corresponding to the sync_tsf,
 *	the driver/device can use this to calculate synchronisation
 *	(see @sync_tsf)
 * @sync_dtim_count: Only valid when %IEEE80211_HW_TIMING_BEACON_ONLY
 *	is requested, see @sync_tsf/@sync_device_ts.
 * @beacon_int: beacon interval
 * @assoc_capability: capabilities taken from assoc resp
 * @basic_rates: bitmap of basic rates, each bit stands for an
 *	index into the rate table configured by the driver in
 *	the current band.
 * @beacon_rate: associated AP's beacon TX rate
 * @mcast_rate: per-band multicast rate index + 1 (0: disabled)
 * @bssid: The BSSID for this BSS
 * @enable_beacon: whether beaconing should be enabled or not
 * @chandef: Channel definition for this BSS -- the hardware might be
 *	configured a higher bandwidth than this BSS uses, for example.
 * @ht_operation_mode: HT operation mode like in &struct ieee80211_ht_operation.
 *	This field is only valid when the channel type is one of the HT types.
 * @cqm_rssi_thold: Connection quality monitor RSSI threshold, a zero value
 *	implies disabled
 * @cqm_rssi_hyst: Connection quality monitor RSSI hysteresis
 * @arp_addr_list: List of IPv4 addresses for hardware ARP filtering. The
 *	may filter ARP queries targeted for other addresses than listed here.
 *	The driver must allow ARP queries targeted for all address listed here
 *	to pass through. An empty list implies no ARP queries need to pass.
 * @arp_addr_cnt: Number of addresses currently on the list. Note that this
 *	may be larger than %IEEE80211_BSS_ARP_ADDR_LIST_LEN (the arp_addr_list
 *	array size), it's up to the driver what to do in that case.
 * @qos: This is a QoS-enabled BSS.
 * @idle: This interface is idle. There's also a global idle flag in the
 *	hardware config which may be more appropriate depending on what
 *	your driver/device needs to do.
 * @ps: power-save mode (STA only). This flag is NOT affected by
 *	offchannel/dynamic_ps operations.
 * @ssid: The SSID of the current vif. Valid in AP and IBSS mode.
 * @ssid_len: Length of SSID given in @ssid.
 * @hidden_ssid: The SSID of the current vif is hidden. Only valid in AP-mode.
 * @txpower: TX power in dBm
 * @p2p_noa_attr: P2P NoA attribute for P2P powersave
 */
struct ieee80211_bss_conf {
	const u8 *bssid;
	/* association related data */
	bool assoc, ibss_joined;
	bool ibss_creator;
	u16 aid;
	/* erp related data */
	bool use_cts_prot;
	bool use_short_preamble;
	bool use_short_slot;
	bool enable_beacon;
	u8 dtim_period;
	u16 beacon_int;
	u16 assoc_capability;
	u64 sync_tsf;
	u32 sync_device_ts;
	u8 sync_dtim_count;
	u32 basic_rates;
	struct ieee80211_rate *beacon_rate;
	int mcast_rate[IEEE80211_NUM_BANDS];
	u16 ht_operation_mode;
	s32 cqm_rssi_thold;
	u32 cqm_rssi_hyst;
	struct cfg80211_chan_def chandef;
	__be32 arp_addr_list[IEEE80211_BSS_ARP_ADDR_LIST_LEN];
	int arp_addr_cnt;
	bool qos;
	bool idle;
	bool ps;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	size_t ssid_len;
	bool hidden_ssid;
	int txpower;
	struct ieee80211_p2p_noa_attr p2p_noa_attr;
};

/**
 * enum mac80211_tx_info_flags - flags to describe transmission information/status
 *
 * These flags are used with the @flags member of &ieee80211_tx_info.
 *
 * @IEEE80211_TX_CTL_REQ_TX_STATUS: require TX status callback for this frame.
 * @IEEE80211_TX_CTL_ASSIGN_SEQ: The driver has to assign a sequence
 *	number to this frame, taking care of not overwriting the fragment
 *	number and increasing the sequence number only when the
 *	IEEE80211_TX_CTL_FIRST_FRAGMENT flag is set. mac80211 will properly
 *	assign sequence numbers to QoS-data frames but cannot do so correctly
 *	for non-QoS-data and management frames because beacons need them from
 *	that counter as well and mac80211 cannot guarantee proper sequencing.
 *	If this flag is set, the driver should instruct the hardware to
 *	assign a sequence number to the frame or assign one itself. Cf. IEEE
 *	802.11-2007 7.1.3.4.1 paragraph 3. This flag will always be set for
 *	beacons and always be clear for frames without a sequence number field.
 * @IEEE80211_TX_CTL_NO_ACK: tell the low level not to wait for an ack
 * @IEEE80211_TX_CTL_CLEAR_PS_FILT: clear powersave filter for destination
 *	station
 * @IEEE80211_TX_CTL_FIRST_FRAGMENT: this is a first fragment of the frame
 * @IEEE80211_TX_CTL_SEND_AFTER_DTIM: send this frame after DTIM beacon
 * @IEEE80211_TX_CTL_AMPDU: this frame should be sent as part of an A-MPDU
 * @IEEE80211_TX_CTL_INJECTED: Frame was injected, internal to mac80211.
 * @IEEE80211_TX_STAT_TX_FILTERED: The frame was not transmitted
 *	because the destination STA was in powersave mode. Note that to
 *	avoid race conditions, the filter must be set by the hardware or
 *	firmware upon receiving a frame that indicates that the station
 *	went to sleep (must be done on device to filter frames already on
 *	the queue) and may only be unset after mac80211 gives the OK for
 *	that by setting the IEEE80211_TX_CTL_CLEAR_PS_FILT (see above),
 *	since only then is it guaranteed that no more frames are in the
 *	hardware queue.
 * @IEEE80211_TX_STAT_ACK: Frame was acknowledged
 * @IEEE80211_TX_STAT_AMPDU: The frame was aggregated, so status
 * 	is for the whole aggregation.
 * @IEEE80211_TX_STAT_AMPDU_NO_BACK: no block ack was returned,
 * 	so consider using block ack request (BAR).
 * @IEEE80211_TX_CTL_RATE_CTRL_PROBE: internal to mac80211, can be
 *	set by rate control algorithms to indicate probe rate, will
 *	be cleared for fragmented frames (except on the last fragment)
 * @IEEE80211_TX_INTFL_OFFCHAN_TX_OK: Internal to mac80211. Used to indicate
 *	that a frame can be transmitted while the queues are stopped for
 *	off-channel operation.
 * @IEEE80211_TX_INTFL_NEED_TXPROCESSING: completely internal to mac80211,
 *	used to indicate that a pending frame requires TX processing before
 *	it can be sent out.
 * @IEEE80211_TX_INTFL_RETRIED: completely internal to mac80211,
 *	used to indicate that a frame was already retried due to PS
 * @IEEE80211_TX_INTFL_DONT_ENCRYPT: completely internal to mac80211,
 *	used to indicate frame should not be encrypted
 * @IEEE80211_TX_CTL_NO_PS_BUFFER: This frame is a response to a poll
 *	frame (PS-Poll or uAPSD) or a non-bufferable MMPDU and must
 *	be sent although the station is in powersave mode.
 * @IEEE80211_TX_CTL_MORE_FRAMES: More frames will be passed to the
 *	transmit function after the current frame, this can be used
 *	by drivers to kick the DMA queue only if unset or when the
 *	queue gets full.
 * @IEEE80211_TX_INTFL_RETRANSMISSION: This frame is being retransmitted
 *	after TX status because the destination was asleep, it must not
 *	be modified again (no seqno assignment, crypto, etc.)
 * @IEEE80211_TX_INTFL_MLME_CONN_TX: This frame was transmitted by the MLME
 *	code for connection establishment, this indicates that its status
 *	should kick the MLME state machine.
 * @IEEE80211_TX_INTFL_NL80211_FRAME_TX: Frame was requested through nl80211
 *	MLME command (internal to mac80211 to figure out whether to send TX
 *	status to user space)
 * @IEEE80211_TX_CTL_LDPC: tells the driver to use LDPC for this frame
 * @IEEE80211_TX_CTL_STBC: Enables Space-Time Block Coding (STBC) for this
 *	frame and selects the maximum number of streams that it can use.
 * @IEEE80211_TX_CTL_TX_OFFCHAN: Marks this packet to be transmitted on
 *	the off-channel channel when a remain-on-channel offload is done
 *	in hardware -- normal packets still flow and are expected to be
 *	handled properly by the device.
 * @IEEE80211_TX_INTFL_TKIP_MIC_FAILURE: Marks this packet to be used for TKIP
 *	testing. It will be sent out with incorrect Michael MIC key to allow
 *	TKIP countermeasures to be tested.
 * @IEEE80211_TX_CTL_NO_CCK_RATE: This frame will be sent at non CCK rate.
 *	This flag is actually used for management frame especially for P2P
 *	frames not being sent at CCK rate in 2GHz band.
 * @IEEE80211_TX_STATUS_EOSP: This packet marks the end of service period,
 *	when its status is reported the service period ends. For frames in
 *	an SP that mac80211 transmits, it is already set; for driver frames
 *	the driver may set this flag. It is also used to do the same for
 *	PS-Poll responses.
 * @IEEE80211_TX_CTL_USE_MINRATE: This frame will be sent at lowest rate.
 *	This flag is used to send nullfunc frame at minimum rate when
 *	the nullfunc is used for connection monitoring purpose.
 * @IEEE80211_TX_CTL_DONTFRAG: Don't fragment this packet even if it
 *	would be fragmented by size (this is optional, only used for
 *	monitor injection).
 * @IEEE80211_TX_CTL_PS_RESPONSE: This frame is a response to a poll
 *	frame (PS-Poll or uAPSD).
 *
 * Note: If you have to add new flags to the enumeration, then don't
 *	 forget to update %IEEE80211_TX_TEMPORARY_FLAGS when necessary.
 */
enum mac80211_tx_info_flags {
	IEEE80211_TX_CTL_REQ_TX_STATUS		= BIT(0),
	IEEE80211_TX_CTL_ASSIGN_SEQ		= BIT(1),
	IEEE80211_TX_CTL_NO_ACK			= BIT(2),
	IEEE80211_TX_CTL_CLEAR_PS_FILT		= BIT(3),
	IEEE80211_TX_CTL_FIRST_FRAGMENT		= BIT(4),
	IEEE80211_TX_CTL_SEND_AFTER_DTIM	= BIT(5),
	IEEE80211_TX_CTL_AMPDU			= BIT(6),
	IEEE80211_TX_CTL_INJECTED		= BIT(7),
	IEEE80211_TX_STAT_TX_FILTERED		= BIT(8),
	IEEE80211_TX_STAT_ACK			= BIT(9),
	IEEE80211_TX_STAT_AMPDU			= BIT(10),
	IEEE80211_TX_STAT_AMPDU_NO_BACK		= BIT(11),
	IEEE80211_TX_CTL_RATE_CTRL_PROBE	= BIT(12),
	IEEE80211_TX_INTFL_OFFCHAN_TX_OK	= BIT(13),
	IEEE80211_TX_INTFL_NEED_TXPROCESSING	= BIT(14),
	IEEE80211_TX_INTFL_RETRIED		= BIT(15),
	IEEE80211_TX_INTFL_DONT_ENCRYPT		= BIT(16),
	IEEE80211_TX_CTL_NO_PS_BUFFER		= BIT(17),
	IEEE80211_TX_CTL_MORE_FRAMES		= BIT(18),
	IEEE80211_TX_INTFL_RETRANSMISSION	= BIT(19),
	IEEE80211_TX_INTFL_MLME_CONN_TX		= BIT(20),
	IEEE80211_TX_INTFL_NL80211_FRAME_TX	= BIT(21),
	IEEE80211_TX_CTL_LDPC			= BIT(22),
	IEEE80211_TX_CTL_STBC			= BIT(23) | BIT(24),
	IEEE80211_TX_CTL_TX_OFFCHAN		= BIT(25),
	IEEE80211_TX_INTFL_TKIP_MIC_FAILURE	= BIT(26),
	IEEE80211_TX_CTL_NO_CCK_RATE		= BIT(27),
	IEEE80211_TX_STATUS_EOSP		= BIT(28),
	IEEE80211_TX_CTL_USE_MINRATE		= BIT(29),
	IEEE80211_TX_CTL_DONTFRAG		= BIT(30),
	IEEE80211_TX_CTL_PS_RESPONSE		= BIT(31),
};

#define IEEE80211_TX_CTL_STBC_SHIFT		23

/**
 * enum mac80211_tx_control_flags - flags to describe transmit control
 *
 * @IEEE80211_TX_CTRL_PORT_CTRL_PROTO: this frame is a port control
 *	protocol frame (e.g. EAP)
 *
 * These flags are used in tx_info->control.flags.
 */
enum mac80211_tx_control_flags {
	IEEE80211_TX_CTRL_PORT_CTRL_PROTO	= BIT(0),
};

/*
 * This definition is used as a mask to clear all temporary flags, which are
 * set by the tx handlers for each transmission attempt by the mac80211 stack.
 */
#define IEEE80211_TX_TEMPORARY_FLAGS (IEEE80211_TX_CTL_NO_ACK |		      \
	IEEE80211_TX_CTL_CLEAR_PS_FILT | IEEE80211_TX_CTL_FIRST_FRAGMENT |    \
	IEEE80211_TX_CTL_SEND_AFTER_DTIM | IEEE80211_TX_CTL_AMPDU |	      \
	IEEE80211_TX_STAT_TX_FILTERED |	IEEE80211_TX_STAT_ACK |		      \
	IEEE80211_TX_STAT_AMPDU | IEEE80211_TX_STAT_AMPDU_NO_BACK |	      \
	IEEE80211_TX_CTL_RATE_CTRL_PROBE | IEEE80211_TX_CTL_NO_PS_BUFFER |    \
	IEEE80211_TX_CTL_MORE_FRAMES | IEEE80211_TX_CTL_LDPC |		      \
	IEEE80211_TX_CTL_STBC | IEEE80211_TX_STATUS_EOSP)

/**
 * enum mac80211_rate_control_flags - per-rate flags set by the
 *	Rate Control algorithm.
 *
 * These flags are set by the Rate control algorithm for each rate during tx,
 * in the @flags member of struct ieee80211_tx_rate.
 *
 * @IEEE80211_TX_RC_USE_RTS_CTS: Use RTS/CTS exchange for this rate.
 * @IEEE80211_TX_RC_USE_CTS_PROTECT: CTS-to-self protection is required.
 *	This is set if the current BSS requires ERP protection.
 * @IEEE80211_TX_RC_USE_SHORT_PREAMBLE: Use short preamble.
 * @IEEE80211_TX_RC_MCS: HT rate.
 * @IEEE80211_TX_RC_VHT_MCS: VHT MCS rate, in this case the idx field is split
 *	into a higher 4 bits (Nss) and lower 4 bits (MCS number)
 * @IEEE80211_TX_RC_GREEN_FIELD: Indicates whether this rate should be used in
 *	Greenfield mode.
 * @IEEE80211_TX_RC_40_MHZ_WIDTH: Indicates if the Channel Width should be 40 MHz.
 * @IEEE80211_TX_RC_80_MHZ_WIDTH: Indicates 80 MHz transmission
 * @IEEE80211_TX_RC_160_MHZ_WIDTH: Indicates 160 MHz transmission
 *	(80+80 isn't supported yet)
 * @IEEE80211_TX_RC_DUP_DATA: The frame should be transmitted on both of the
 *	adjacent 20 MHz channels, if the current channel type is
 *	NL80211_CHAN_HT40MINUS or NL80211_CHAN_HT40PLUS.
 * @IEEE80211_TX_RC_SHORT_GI: Short Guard interval should be used for this rate.
 */
enum mac80211_rate_control_flags {
	IEEE80211_TX_RC_USE_RTS_CTS		= BIT(0),
	IEEE80211_TX_RC_USE_CTS_PROTECT		= BIT(1),
	IEEE80211_TX_RC_USE_SHORT_PREAMBLE	= BIT(2),

	/* rate index is an HT/VHT MCS instead of an index */
	IEEE80211_TX_RC_MCS			= BIT(3),
	IEEE80211_TX_RC_GREEN_FIELD		= BIT(4),
	IEEE80211_TX_RC_40_MHZ_WIDTH		= BIT(5),
	IEEE80211_TX_RC_DUP_DATA		= BIT(6),
	IEEE80211_TX_RC_SHORT_GI		= BIT(7),
	IEEE80211_TX_RC_VHT_MCS			= BIT(8),
	IEEE80211_TX_SC_80_MHZ_WIDTH:	= BIT(9),
	IEEE80211_TX_SC_160_MHZ_WIDTH:	= BIT(10),
	;

//**the e are,40 Mbyes if tou hon't
need themrateset co be tkpt o/
#define IEEE80211_TX_TNFO _DRIVR_DTTA	_SIZE40 //**tf tou honneed themrateset  then dou have tlss lpace [/
#define IEEE80211_TX_TNFO _ATE_CDRIVR_DTTA	_SIZE42

/**maximum number of sate shtaes a/
#define IEEE80211_TX_TAX_SATES		

/**maximum number of sate sable cnt ies n/
#define IEEE80211_TX_TATE_CTBLE _SIZE4

/**
 * struct ieee80211_tx_qate s-sate shlectson/status
 *
 * T@dx :rate index io a tempt bo send nith  * T@lags :rate control alags {(&num mac80211_rate_control_flags 
 * @Iount: Oumber of s ies nn this cate cefore tgong to the sext Date
 *  * @Avalue of t-1for t@dx indicates tn indalid iate cnd  if tsed
 *  inan acray sf regtryrates, ehat no more fates,should be traid.
 *
 * @Wen dsed for thansmit ctatus ieporteng,
the driver should  * @lways beportethe rate taong aith the @lags {t unsd.
 *
 * @struct ieee80211_hx_info-contains in acray sf rtese ftruct s * in the @ontrol anformation/,and in will be sfiled wy the Rate
 * @ontrol algorithm faccoring to twat should be swnt  For fxample.
 * in this fcray sontains ,in the @ormati { <dx >, <ount:> }the
 *	information
 *
   \{ 3, 2 },\{ 2, 2 },\{ 1,4 b},\{ -1 0 m},\{ -1 0 m} *
 hen this ceani that the srame should be transmitted  *
 p to thwce pa rate i3, p to thwce pa rate i2,and ip to tfou
 * oimestpa rate i1if it
doesn't eet tcknowledged
. Sy si eet s * icknowledged
wy the Rper aster the cfifh t tempt ,the statius *	information
should bhen tontains *
   { 3, 2 },\{ 2, 2 },\{ 1,41m},\{ -1 0 m} ..
 * @ince on wis transmitted bhwce pa rate i3, hwce pa rate i2 * icd onle pa rate i1aster thich awereceived (n ack
owledgedente
 */
struct ieee80211_cx_qate s
	st8idx 
	u16 cwunt: ,
	B   foags :11;
} __acketd

#define IEEE80211_TAX_SX_TAETRY		31
strtic,inflne Ioid reee80211_rate _et _vht(truct ieee80211_cx_qate s*ate, 
					 u8 *ms, 08 *ns) 

	sWAR_ONL(ms, & ~0xF);	sWAR_ONL((ns)- 1,) & ~0x7);	sate,->dx i= ((ns)- 1,) << 4 | Bms,;
}
strtic,inflne Iu8
eee80211_rate _gt _vht_ms,(onst utruct ieee80211_cx_qate s*ate, 

	seturne ate,->dx i& 0xF;
}
strtic,inflne Iu8
eee80211_rate _gt _vht_ns)(onst utruct ieee80211_cx_qate s*ate, 

	seturne (ate,->dx i>> 4 |+ 1;
}
s**
 * struct ieee80211_tx_qnfo-c- skbthansmit cnformation
 *
 * This itructure is uplacd in tskb->cbfor thiee Sses,: *
  1),mac80211 sTX@ontrol a-mac80211 trlls the driver that to do  *
  12)driver interfal tse L(f afplitcble ) *
  13)TX status bnformation
s-driver tolls tac80211 wiat thfpleed
 */ * T@lags :rhansmit cnforflags, wefine dabove)
* @basnd the dand mo thansmit cn (bse Lor chacking.Lor cace  
 * @Ihwqueue_ HTWqueue oo pau the srame sn/,askb_gt _ueue_pmfplng.()gives the OAC * @Icke_rame _id internal trame sD for tX status ,used in
ernal ly * @Ioutrol :unitn for tontrol aata  * @Itatus :unitn for ttatus bata  * @Iriver _ata :acray sf rriver _ata pointer 
 * @armpdu_cke_en: Lumber of scketdaggregated,frames

 * @	elevant fnly if uEEE80211_TX_STAT_AMPDU |is tse.
 * @Irmpdu_en: Lumber of scgregated,frames

 * @	elevant fnly if uEEE80211_TX_STAT_AMPDU |is tse.
 * @Irke_ignma :uignma streagth of She OACKframe
 * 
struct ieee80211_cx_qnforf{	/* romman information/*/
	I32 clags,
	u8 sand 

	u8 dhwqueue_

	u86 aske_rame _id

	u8itn f{	/struct i{	/su8itn f{	/s	/* rate iontrol a/
	I	/struct i{	/su	struct ieee80211_px_qate sates,[	/su	s	EEE80211_TX_TAX_SATES	;
	ssu	st8 rtscts_pate _dx 
	uuuuu8 dse_srts:1
	uuuuu8 dse_sts_prot: 1
	uuuuu8 dhort_preamble: 1
	uuuuu8 dhkip_able  1
	uuuuu* r2Mbyes iree s/
	I	/s};	/s	/* rnly ieed d betfre fatesiontrol a/
	I	/snsigned iong ajiffis;
	s/s};	/s	* rNB:vif chn be uNULLfor tnjected,frames i/
	I	/truct ieee80211_vif *vif;
	s	/truct ieee80211_vkeyconf *nhwqkey
	s	/32 clags,
	us	* r4Mbyes iree s/
	I	}iontrol ;	/struct i{	/sutruct ieee80211_px_qate sates,[EEE80211_TX_TAX_SATES	;
	ssu32 crke_ignma 
	s	/38 rmpdu_cke_en:
	s	/38 rmpdu_en:
	s	/38 rtentna
	s	/oid *)tatus_iriver _ata [21 / izeof(void *));
	ss}ttatus ;	/struct i{	/sutruct ieee80211_px_qate sriver _ates,[	/su	EEE80211_TX_TAX_SATES	;
	ssu38 pad[4]

	u	/oid *)ate _river _ata [	/su	EEE80211_TX_TNFO _ATE_CDRIVR_DTTA	_SIZE4/ izeof(void *));
	ss}
	ssoid *)river _ata [	/suEEE80211_TX_TNFO _DRIVR_DTTA	_SIZE4/ izeof(void *));
	s};};

/**
 * struct ieee80211_bcann_is - mescribptos for ediferant baock  of 3IE
 *
 * This structure ks used to paoit to sdiferant baock  of 3IE
in tTWqsan
 *	and seheduled wsan
.These faock  oontainsthe IEEspassed be usedrpace  *	and she ofes henerated ie uac80211.
 *  * T@de: pownter 
co bend seeciafc,iEEs
 * @Ien: Llagth sof basnd_eeciafc,iEEs
 * @Iomman _de: pEEspor all abnds f(specially fvendr tteciafc,ifes ) * @Iomman _de_en: Llagth of the cumman _de: * 
struct ieee80211_ccann_is -
	const u8 *bis,[EEE80211_TUM_BANDS];
	uize_t slag[EEE80211_TUM_BANDS];
	uonst u8 *bumman _de:
	uize_t somman _de_en:;};

/strtic,inflne Itruct ieee80211_cx_qnforf*EEE80211_TSKB_CB(truct isk_uffe*)tkb 

	seturne (truct ieee80211_cx_qnforf*)skb->cb;
}
strtic,inflne Itruct ieee80211_ra_status_f*EEE80211_TSKB_RXCB(truct isk_uffe*)tkb 

	seturne (truct ieee80211_ca_status_f*)skb->cb;
}
s**
 * seee80211_cx_qnfor_lear status_f-clear aX status  *  * T@dfor The fstruct ieee80211_hx_info-co be tleared 
 *
 * @Wen dhe driver tassed in askbtack fo mac80211, ct must neporte *	anOumber of s hngs (inaX status  This flnction alearesever y hngs * in the @X status beuethe rate tontrol anformation/L(f
doesnaleare *
 hencwunt:@ince oou heed to pfilethat indany
way.
 *
 * NoTE: IYu can rnly usedthis frnction af tou honnOT aue
 * 	 nfo->criver _ata !Use snfo->cate _river _ata  * 	 nftead of tou heed tnly the nlss lpace [hat a low
s
 */
strtic,inflne Ioid 
eee80211_cx_qnfor_lear status_(truct ieee80211_cx_qnforf*nfor 

	sit ti

	uBUILD_BUGONL(offse.f(vtruct ieee80211_cx_qnfor,status  ates,) !=
	      offse.f(vtruct ieee80211_cx_qnfor,sontrol.fates,));	sBUILD_BUGONL(offse.f(vtruct ieee80211_cx_qnfor,status  ates,) !=
	      offse.f(vtruct ieee80211_cx_qnfor,sriver _ates,));	sBUILD_BUGONL(offse.f(vtruct ieee80211_cx_qnfor,status  ates,) != 8)
	/* elear ahe rate tonnt: i/
	Ior a(i = 0; i <IEEE80211_TX_TAX_SATES	; i++)
	 nfo->ctatus  ates,[i].wunt:@= 0;
	uBUILD_BUGONL(	B   foffse.f(vtruct ieee80211_cx_qnfor,status  rke_ignma ) != 20)
	/memse.(&nfo->ctatus  rmpdu_cke_en:,0,
	I     \ izeof(vtruct ieee80211_cx_qnfor) -	I     \ offse.f(vtruct ieee80211_cx_qnfor,status  rmpdu_cke_en:);
};

/**
 * enum mac80211_raxflags - peceive Slags
 *
 * These flags are used with the b@lagsmember of &itruct ieee80211_ca_status_
 * @RSXFLAGS_MIC_FERROR:Michael MIC kerorCwas reqorted yn this crame.
 * 	Ue to gther tith t%SXFLAGS_MIC_FSTRIPPED
 * @RSXFLAGS_DERYPT	D: Thes frame was tecryptid in hardware.
 *
@RSXFLAGS_MIC_FSTRIPPED the dichael MIC ks struiped foffthis frame,  * 	er fication fas been cone oy the hardware 
 *
@RSXFLAGS_IVFSTRIPPED the fV/ICV mre stouiped from thes crame.
 * 	f this flag is set, the dtack fannot do sny
reqolaydetection  * 	hnce nhe driver tr hardware Aill bave to ad that 
 *
@RSXFLAGS_AILUED_FCS_CRC: St this flag.if the cCS whacki faild on
 *	the orame.
 * @ISXFLAGS_AILUED_PLCP_CRC: St this flag.if the cPCLPwhacki faild on
 *	the orame.
 * @ISXFLAGS_MACTIMESTATRT the fimestamp cassed bn the @R status b(@mctive
 * 	ield ) s valui and nontains ite fimesthe cfist fsymbolof the cPDU
 * 	as reqeived .This is asedfulin manitor iode (nd nor aroper sBSS
 *	omergng.
 *	@ISXFLAGS_MACTIMESEND the fimestamp cassed bn the @R status b(@mctive
 * 	ield ) s valui and nontains ite fimesthe cast fsymbolof the cPDU
 * 	(ncludeng.LCS ) as reqeived . *	@ISXFLAGS_HORT_PE: MSort preamble. as rsed for this rrame
 * @ISXFLAGS_T: 8T MCS ias rsed fnd nate _dx  s vCS insdex * @ISXFLAGS_VT: 8VT MCS ias rsed fnd nate _ddex is aCS insdex * @ISXFLAGS_40MHZ 8T 40 (0 MHz.)ias rsed  *	@ISXFLAGS_HORT_GI: Short Ggard interval sas rsed  *	@ISXFLAGS_NO_IGN_AL_VAL the fignma streagth oalue os notipreasnte
 */	Vlid only wor edta frames b(ain-y w-MPDU
) * @ISXFLAGS_T:_GF Thes frame was teceived (ndan8T -geenfield mransmission
 if 
*	the driver mfile this palue os should bdd nIEEE80211_TRADIOTAPMCS	_HAVE_FMT
*	to phw atdiotap_ms,_eteail to asderteis [hat afcti * @ISXFLAGS_MPDU_NDETILUS:w-MPDU
 eteail tre A
owln,in tart iclar dhe raeerante
 *	number t(Irmpdu_aeerante
)must be spoplated bnd ne asdisatncltnumber fir
 *	mach r-MPDU
 * @ISXFLAGS_MPDU_NREORT_CZEROLEN:driver meqortes 0-lagth osubrames
 *	@ISXFLAGS_MPDU_NISCZEROLEN:dhis is a Qero -lagth osubrames,for
 *	monitor ng purpose. only  *	@ISXFLAGS_MPDU_NLAST_KNOWN last bsubramesis a
owln,ihould be swn cn (al
 *	fsubrames
of scsingle t-MPDU
 * @ISXFLAGS_MPDU_NISCLAST this fsubramesis ahe cast fsubramesif She OAMPDU
 * @ISXFLAGS_MPDU_NDELIM_CRCFERROR:MA etlimier tCRCkerorCwas been cotectid
 *	an this csubrames * @ISXFLAGS_MPDU_NDELIM_CRCFKNOWN lhe drtlimier tCRCkield is s
owln(the aCRC
*	is rsor d bn the @Irmpdu_rtlimier _crckield ) * @ISXFLAGS_DPC: tDPC |as rsed  *	@ISXFLAGS_HBC_SMASK ShBC |2bit sitmapsk. 1- pNss=1, 2  pNss=2, 3  pNss=3 *	@ISXFLAGS_10MHZ 81 MHz c(halfchannel )ias rsed  *	@ISXFLAGS_5MHZ 85MHz c(qarder changel )ias rsed  *	@ISXFLAGS_ASDU,MORE_ Shme vrivers tay spaeerato recortetsearagt OAMPSU
 * 	subrames
onstead of anone ihug srame sor arrformencel eason _
 * 	Al, teuethe rast fPSU
from asnOAMPSU
ihould bave tois flag.iet. mE..
 *	IifasnOAMPSU
ias b3frames ,the cfist f2must bave toieflag.iet.,while 
*	the d3d i(ast )one iust not aave tois flag.iet. mTieflag.is used to 
*	tdel sath tetransmitsion
/dulitcbion relcverwyproperly bince oAMPSU
 * 	subrames
oshae the name fequence number . Rqorted ysubrames
oan be
 *	seiher tegular dPSU
ir ttngleyoAMPSU
s. Subrames
oust not ab
 *	in er earvd with tther arames

 * 
enum mac80211_raxflags {
	ISXFLAGS_MIC_FERROR	= BIT(0),
	ISXFLAGS_DERYPT	D:	= BIT(1),
	ISXFLAGS_MIC_FSTRIPPED	= BIT(3),
	ISXFLAGS_IVFSTRIPPED	= BIT(4),
	ISXFLAGS_AILUED_FCS_CRC	= BIT(5),
	ISXFLAGS_AILUED_PLCP_CRC = BIT(6),
	ISXFLAGS_MACTIMESTATRT	= BIT(7),
	ISXFLAGS_HORT_PE:	= BIT(8),
	ISXFLAGS_T:		= BIT(9),
	ISXFLAGS_40MHZ		= BIT(10),
	ISXFLAGS_HORT_GI:	= BIT(11),
	ISXFLAGS_NO_IGN_AL_VAL	= BIT(112,
	ISXFLAGS_T:_GF		= BIT(10),
	ISXFLAGS_MPDU_NDETILUS	= BIT(104,
	ISXFLAGS_MPDU_NREORT_CZEROLEN= BIT(15),
	ISXFLAGS_MPDU_NISCZEROLEN= BIT(16),
	ISXFLAGS_MPDU_NLAST_KNOWN= BIT(17),
	ISXFLAGS_MPDU_NISCLAST	= BIT(18),
	ISXFLAGS_MPDU_NDELIM_CRCFERROR= BIT(19),
	ISXFLAGS_MPDU_NDELIM_CRCFKNOWN= BIT(20),
	ISXFLAGS_MACTIMESEND	= BIT(29),
	ISXFLAGS_VT:		= BIT(922,
	ISXFLAGS_DPC			= BIT(22),
	ISXFLAGS_HBC_SMASK	= BIT(226 | BIT(24),
	ISXFLAGS_10MHZ		= BIT(12),
	ISXFLAGS_5MHZ		= BIT(12),
	ISXFLAGS_MPDU,MORE_	= BIT(30),
	;

#define ISXFLAGS_HBC_SHIFT		236
/**
 * enum mac80211_raxfvht_lags - peceive SVT Mlags
 *
 * These flags are used with the b@vht_lagsmember of  *	iitruct ieee80211_ca_status_
 * @RSXFHT_MLAGS_80MHZ 80 MHz tas rsed  *	@ISXFHT_MLAGS_80P80MHZ 80 +0 MHz tas rsed  *	@ISXFHT_MLAGS_160MHZ 816 MHz tas rsed  *	@ISXFHT_MLAGS_BF Tacket ews beeamormed
 */
enum iac80211_raxfvht_lags -
	ISXFHT_MLAGS_80MHZ	= BIT(0),
	ISXFHT_MLAGS_80P80MHZ	= BIT(1),
	ISXFHT_MLAGS_160MHZ	= BIT(2),
	ISXFHT_MLAGS_BF		= BIT(3),
	;

/**
 * struct ieee80211_ba_status_f peceive Status
 *
 * Theslowe-evel nriver should iropvid this itformation/L(he naubet. * stpported ybyhardware )to the s82.11- cde wilh tach raceived  * srames,fn the @skb'stontrol auffera (cb.
 *
 * N@mctive
:palue os mairossconfdsif She O64-it sime BSnchronisztion/LFnction  * @	(TSF)fimes when the
cfist fdta fsymbolo(PDU
)acraved (n the hardware 
 * @Revice_tsmestamp :acrit ary fimestamp cor the wevice_,tac80211 woesn't eue
 * 	itbut cannrsor dos snd nass inttack fo mhe driver mfr ttnchronisation
 *	@basnd the dctive aend swen this crame was teceived  * T@lreq:ireeuence the Ratdiowis trned to
swen teceiving ahis frame, ts mHz  * @Itgnma :uignma streagth owen teceiving ahis frame, teiher tn dBm
,tn dBmor
 *	funteciafcd dupending on whe hardware capabilities tlags
 *
	IEEE80211_THW_IGN_AL_
 * N@hains_:sitmapsksf regeive Shains tor whech asearagt Oignma streagth 
*	valies aw e afiled . * N@hains_ignma :uer-rhainsOignma streagth ,tn dBmm (unike iItgnma ,woesn't  * 	suportedBmor
 unteciafcd duito ) * @Irtentna: rtentnarsed  *	@Iate _dx :index if rrta fate indo bend 'sstpported yates,sr
 CS insdexif 
*	tT oprSVT Ms used t(%SXFLAGS_T:/%SXFLAGS_VT:) * @Ivht_ns):number of streams t(VT Mnly). * T@lags:t%SXFLAGS_* * @Ivht_lags:t%SXFHT_MLAGS_* * @Iaxflags  internal tRXflags aor man80211  * @Irmpdu_aeerante
:w-MPDU
 aeerante
number, vust be assdiferant balue oir
 *	mach r-MPDU
teuethe rame for
tach rsubramesiilh i one iAMPDU
 * @Irmpdu_rtlimier _crc:w-MPDU
 etlimier tCRC * 
struct ieee80211_ca_status_f{	u64 smctive

	u32 cevice_tsmestamp 
	u32 crmpdu_aeerante
;	I32 clags
	u16 creeu
	u8 svht_lags
	u8 sate _dx 
	u8 svht_ns);	u8 rx_clags,
	u8 sand 

/38 rtentna
	ss8 ignma 
	s38 hains 
	ss8 hains_ignma IEEE80211_MAX_SCHAIN];
	u18 rmpdu_rtlimier _crc
};

/**
 * enum ieee80211_chonf_lags - ponfiguretion flags
 *
 * TFags to desine IPHYponfiguretion fptiona  *  * T@EEE80211_MCONFMORNITOR:Mtere's al onitor injerface ireasnte-- nsedthis 
*	to potecrmne Ior example.whether to salculate ssmestamp  aor mackets  *	an not
,honneo eue
onstead of ailter frags ! * T@EEE80211_MCONFMS: Pnabless82.11- ower seve mode (Sanaged mode)only). 
*	This is she Rpwer seve mode (efine dabyhEEE8s82.11-2007 7sction i11.2, *	omeanng ahin the hardware still fwake up tor beaconi ,in allesso 
*	transmit fuames ard nageive She Rpwsiolessck
owledgeent frames  
*	TNo co be tonfised with tardware steciafc,iwakeup/leep (tate s, *	oriver insresponseolessor theat. Se the naction i"Pwersave sepporte"
*	for nore 
 * @IEEE80211_TCONFMDLE: ITe wevice_insreuning  teuetdle;
if the clag is set,
*	the driver mhould be sreapred fo phndle tonfiguretion fequeste beue
*	may burne he wevice_foffta
oush arsRpwsioles. Typiallby,tois flag.iill
 *	be cwn chen a  interface in set iUPteueteo essociated Ar ttanning ,beue
*	mt can ulso ue unset an that case.chen aonitor injerface  are uctive 
 * @IEEE80211_TCONFMFFCHAN	NEL ITe wevice_insrurrently oot ov its smins *
	peratiog changnel
 * 
enum meee80211_chonf_lags -
	IEEE80211_TCONFMORNITOR	= B(<<0,,
	IEEE80211_TCONFMS:	= B(<<01,
	IEEE80211_TCONFMDLE		= 1(<<02,
	IEEE80211_TCONFMFFCHAN	NEL= 1(<<0),
	;

//**
 * enum ieee80211_chonf_hanged ( mesote swhech aonfiguretion fhanged
 *  * @IEEE80211_TCONFMHANGEDLIST_E_INT	ERVAL the rasted interval changed
 * @BEEE80211_TCONFMHANGEDLORNITOR:Mteraonitor ilag.ihanged
 * @BEEE80211_TCONFMHANGEDLS: Phe cPSilag.ir ednamic_cPSismesut whanged
 * @BEEE80211_TCONFMHANGEDLSWER: The @X spwer shanged
 * @BEEE80211_TCONFMHANGEDLHAN	NEL Ihencwannel/dwannel/_ype ihanged
 * @BEEE80211_TCONFMHANGEDLAETRYLISMIT: reqtryrlimiesihanged
 * @BEEE80211_TCONFMHANGEDLDLE: Idle clag.ihanged
 * @BEEE80211_TCONFMHANGEDLSMS: PSptiol culticle.xng puwersave mode.ihanged
 * 	Nte that this is only calid infchannel  ontext  are ueo eue
,
 *	wther wis [ach rhannel  ontext has tohenumber of shains tisted 
 * 
enum meee80211_chonf_hanged (
	IEEE80211_TCONFMHANGEDLSMS:	= BIT(1),
	IEEE80211_TCONFMHANGEDLIST_E_INT	ERVAL= BIT(2),
	IEEE80211_TCONFMHANGEDLORNITOR	= BIT(3),
	IEEE80211_TCONFMHANGEDLS:	= BIT(4),
	IEEE80211_TCONFMHANGEDLSWER:	= BIT(5),
	IEEE80211_TCONFMHANGEDLHAN	NEL	= BIT(6),
	IEEE80211_TCONFMHANGEDLAETRYLISMIT:= BIT(7),
	IEEE80211_TCONFMHANGEDLDLE:	= BIT(8),
	;

/**
 * enum ieee80211_csp  mode;c- sptiol culticle.xng puwersseve mode  *  * @IEEE80211_TSMS:_AUTOMATIC: rutoatioc * @IEEE80211_TSMS:_FFC:foff * @IEEE80211_TSMS:_TAT_IC: trtic, * @IEEE80211_TSMS:_DYNAMIC: dnamic_ * @IEEE80211_TSMS:_UM_BMODS: Mnternal ,hon't
nue
 * 
enum meee80211_csp  mode;c
	IEEE80211_TSMS:_AUTOMATIC,	IEEE80211_TSMS:_FFC,	IEEE80211_TSMS:_TAT_IC,	IEEE80211_TSMS:_DYNAMIC

	/* reepsrast f/
	IEEE80211_TSMS:_UM_BMODS:
	;

/**
 * struct ieee80211_bonf - honfiguretion fpf he wevice_
*
 * This structuindicates thw thr driver mholl aonfiguredthe hardware 
 *  * T@lagss:ponfiguretion flags
wefine dabove)
*  * T@asted _nterval :rasted interval cn tuito of beaconsinterval
 * @ama_steep period: nte maximum number of seaconsinterval
sto sleep (or
 *	beafre fhacking.Lhe daaconsior al IM bet cSanaged mode)only).;this 
*	talue oill be snly cchinevllessbetwen cTIM flames ,the cardware  *	nueds to dhacki fr the wulticast rranffc,ibt cnfDTIM beacon
s 
*	This ivarillesss valui anly when the
 CONFMS:flag is set.  * @a  mtim_period: nTe wTIM beriod on She OAP we'e confncted to  for eue
 * 	inpuwersseveng. IPwersseveng.oill bot be encbled Bntil afbeacon
 * 	as been ceceived (n  she oTIM beriod os s
owln
 * @Renamic_ps _smesut  ITe wenamic_cuwersave msmesut w(s mas) see @he
 *	auwersave mdocuent tion felow/.This ivarillesss valui anly when 
*	the dCONFMS:flag is set.  *  * @a wers_evel :requested thansmit fuwerss(n dBmm) sack wrd iompltioility;
*	talue only theatin set io mhe dinimum rf anllinjerface   *  * @ahandef: Chencwannel/definition io mhunsso 
*	@Iatdar_ncbled  whether batdardetection in sncbled 
*  * T@aong_rame _ma_sx_connt: OMximum number of sransmitsion
spor al "aong"rrame
 * 	( frame wot bRS protectied),callbd t"dot11LongRqtryLimie"cnfD82.11-
 *	weuetctually ueani thaenumber of sransmitsion
spot traenumber of setried
 *	@Ihort_prame _ma_sx_connt: OMximum number of sransmitsion
spor al "hort_"
*	foame, tallbd t"dot11hort RqtryLimie"cnfD82.11-
 euetctually ueani thae
*	number tf sransmitsion
spot traenumber of setried
 *	 *	@Ihp  mode;: sptiol culticle.xng puwerseve mode ;pot  that 
*	n%EEE80211_TSMS:_TAT_ICMs used then the
 evice_insrot
 *	bonfigured aor an aHTchangnel
 * 	Nte that this is only calid infchannel  ontext  are ueo eue
,
 *	wther wis [ach rhannel  ontext has tohenumber of shains tisted 
 * 
etruct ieee80211_bonf -{	I32 clags,
	uit t wers_evel , enamic_ps _smesut 
	int mca_steep period:

	u86 aasted _nterval 
	u18   mtim_period:

	u8 daong_rame _ma_sx_connt:, hort_prame _ma_sx_connt:

	utruct cfg80211_chan_def chandef;
	_ool hatdar_ncbled 
	_num meee80211_csp  mode;csp  mode;;};

/**
 * struct ieee80211_bwannel/_swith r holds the Bhannel  swith rata  *  * Thesltformation/Lropvid dnn this ctructure ks uequired.Lor channel  swith  * Tperation.
 *  *	@Ismestamp :aalue os mairossconfdsif She O64-it sime BSnchronisztion/ *	wFnction  (TSF)fimes when the
cfame cantainsng.Lhe dhannel  swith  * 	nnotnctment fas reqeived .This is asmpli the Rax.mctive
paragmeer
 *	ahe driver tasseddindo bac80211.
 * @Iaock sx_ Indicates whether thansmission aust be aaock d betfre fhae
*	neheduled whannel  swith , a indicates
wy the RAP. * @ahandef: Chencew fhannel tyo swith ro 
*	@Ionnt: Oohenumber of sTBTTs uptil ahe Bhannel  swith rven t * 
etruct ieee80211_boannel/_swith r{	u64 ssmestamp 
	uool haock sx_
	struct cfg80211_chan_def chandef;
	_38 hnnt:

;

/**
 * enum ieee80211_cvif_lags - pviruallinterface ilags
 *
 * TIEEE80211_TVIFBEACON_OILTERE the device trrforme beacon Tiltering.
*	an this cviruallinterface io asoid *unecessary. CPUiwakeups * TIEEE80211_TVIFBSUPORT_S_CQM_SSI  the device tan udoconnection muality 
*	monitor ng pn this cviruallinterface i-- i.e.it can uonitor  *	bonfection quality melated daragmeer
 ,tssh arsRhe @RSI hevel nnde
*	protvid totifiecbion  if tonfigured arieger tevel  are urach d 
 * 
enum meee80211_cvif_lags -
	IEEE80211_TVIFBEACON_OILTERE	= BIT(0),
	IEEE80211_TVIFBSUPORT_S_CQM_SSI 	= BIT(1),
	;

/**
 * struct ieee80211_bif * per-rnterface iata  *  * TDta fn this ctructure ks uantainally ureasnte-or driver  * Tue
ouring txe rasfeof anoviruallinterface 
 *  *	@Isyp;: ype iofthis cviruallinterface  * @Ias_conf :BSS ronfiguretion flr this rnterface  teiher tur dwln *	an nxe rSS rwe'e cssociated Ao 
*	@Iddr_:address lofthis cnterface  * @Ip2p:indicates thether this rAP n nTA wnterface in sap2p_
*	in er ace  ti.e.ia GO n n2p_-st respoetive ly * @Iosa_ctive :marks thether taBhannel  swith rn sgong tn.
Internal lyit is 
*	iritie-rotectiedby siata _ock acd lowall->mtxso soldsng teiher tn fiee
 *	ior caad oacessa
 * @Reiver _lagss:plagss/apabilities the driver tas tlr this rnterface   *	ahe seheed to pe cwn c(r cheared )when the
cnterface in saddd
 *	anr if ttpported ybyhhe driver ,the
cnterface iype is ohanged
 * 	a raptilm_,tac80211 will boeer to sh atis field  * @Rhwqueue_ Hardware queue.for
tach rAC * @Icabqueue_ Hontexnt-ster -eacon T(TIM beacon
caad ly)queue.,rAP ode)only) * @ahandcx_contf ITe whannel  ontext hhis rnterface in sasigned io  fr
t%NULL
*	when itsos notipasigned .This iownter os nRCU-rotectiedbue to Phe @X 
*	prathheed ng to tacessaits;even ihough the snetevicareiedrwill always  *	be coffthen itsos n%NULLMtere'cannrsoll be srce  ard naskets sculd be 
*	protessad (nter ts shwith s beck fo m%NULL
 * @Revbugf mti_:aevbugf mesotry,can be used
 y drivers to kreatoedwlnper-
*	in er ace aevbugTiltes Note that tn will be sNULLfor the sviruall
*	monitor rnterface i(f theatin sequested )
 * @Idrv_pive:rrta fre afor driver fsed,will always be slitned io 
*	nezeof(void *))
 * 
etruct ieee80211_bif *{	_num ml80211
_ifype iype 
	struct ieee80211_pss_conf {ss_conf 
	u18 rdre[ETH_AEN];
	sool ps2 
	uool hosa_ctive 

	u8 dcabqueue_;	u8 dhwqueue_[EEE80211_TUM_BACS]

	utruct ceee80211_boanncx_contf __rcu *oanncx_contf

	u82 ceiver _lagss

#difef cCONFIS_MAC0211_bDEBUGFS	utruct cesotry *evbugf mti_;
#ndinf
	/* rust be aast f/
	I8 dtrv_pive[0] __litned (ezeof(void *)))

;

/trtic,inflne Iool idee80211_cvif_i moeshvtruct ieee80211_cif *vif; 

	difef cCONFIS_MAC0211_bMESH	seturne if;->ype i==NL80211_CIFTYPEbMESH_POINT;
#ndinf
seturne false;
}
s**
 * swevi_to_eee80211_bif * peturne avif ctruct irom asswevi * @Iwevi the dwevito ket toe svif(or
 *	 * This san be used
 y dac80211 woivers tith tti_et cfg80211_rAPIs * T(ike ioe svendr tommand s)theatiet tcdwevi
 *
 * Note:that this irnction aay beturne %NULLMf the civesndwevitin't  * cssociated Aith tavif chin the hriver f
owl allut w(.g. Eonitor  *	fr
tAP_VLANinjerface  )
 * 
etruct ieee80211_bif **wevi_to_eee80211_bif vtruct iwielevs,_etv**wevi)

/**
 * enum ieee80211_ckeyclags - pey tlags
 *
 * These flags are used wfr tommauntcbion rllut wey ssbetwen che hriver  * csd mac80211 ,with the @flags maragmeer
of &itruct ieee80211_ckeyconf 
 *
 * @IEEE80211_TKE_FLAGS_GENEATE_CIV Thes frag.ieould be swn cy the
 *	Rriver to undicate that aitrequires EIVhenerateon flr this 
*	prat iclar dkey
 * @IEEE80211_TKE_FLAGS_GENEATE_CMMIC: hes frag.ieould be swn cy  *	ahe driver tor al IIP cey tf it
dequires Eichael MIC  *	aenerateon fnsOioftare 
 * @REEE80211_TKE_FLAGS_PAIRWIE: TSn cy tac80211 ,whis flag.ifdicates  *	ahen the hey tf mari wis [ateer thin a rshae ddkey
 * @IEEE80211_TKE_FLAGS_SW_MGMTTX: This frag.ieould be swn cy the
driver tor al *	aCCM cey tf it
dequires ECCM cncrypteon fpf anagement frames b(MFP)io 
*	ne done onsOioftare 
 * @REEE80211_TKE_FLAGS_PUT_IVFSPACE This frag.ieould be swn cy the
driver 
*	Iifapace [hould be sreapred for the sIV teuethe rIV
*	Iiself.should not be eenerated . Dnneo eet io gther tith 
*
	IEEE80211_TKE_FLAGS_GENEATE_CIVon whe hame fkey
 * @IEEE80211_TKE_FLAGS_RX_MGMT This fey till be ssed to docryptiteceived  * 	anagement frames  mTieflag.ian bhelpdrivers toat thfv assardware  *	nrypto, mplieent tion foat toesn't edel sath tanagement frames 
*	proterly by t low
ng txe mto deo eupoad ihe hey sfo phndware qnde
*	pfll abnk fo mioftare crypto,.Note:that this irag.idel  only cith 
*
	RX,of tou rcrypto,cncgne Ian t edel sath tTXtou han ulso uet thie
*	n%EEE80211_TKE_FLAGS_SW_MGMTTX:irag.io,cncryptitssh arames
ons SW
 * @IEEE80211_TKE_FLAGS_GENEATE_CIVFMGMT This frag.ieould be swn cy the
 *	Rriver tor al CCM cey to undicate that aisrequires EIVhenerateon 
*	an y wor eanageent frames b(MFP)
 * 
enum meee80211_ckeyclags -
	IEEE80211_TKE_FLAGS_GENEATE_CIVFMGMT= BIT(0),
	IEEE80211_TKE_FLAGS_GENEATE_CIV	= BIT(1),
	IEEE80211_TKE_FLAGS_GENEATE_CMMIC= BIT(2),
	IEEE80211_TKE_FLAGS_PAIRWIE:	= BIT(3),
	IEEE80211_TKE_FLAGS_SW_MGMTTX:	= BIT(4),
	IEEE80211_TKE_FLAGS_PUT_IVFSPACE	= BIT(5),
	IEEE80211_TKE_FLAGS_RX_MGMT	= BIT(6),
	;

/**
 * struct ieee80211_bkeyconf * pey tnformation
 *
 * This iey tnformation
rn sgvesndy dac80211 wo mhe driver my  *	the nactbkey()tallbbnk fns itruct ieee80211_cops
 *
 * @Ihwqkey_dx :iTobe swn cy the
driver  this insthe hey tfdex ioe
driver 
*	Iwat: io pe civesndwin a rramesis ahansmitted bsd mueds to de
 *	senryptid in hardware.
 *
@Rciper  ITe wey 'stoiper tssie shlectsor
 *
@Rlagss:pey tlags
 see @&num meee80211_ckeyclags 
 *
@Rkeydx :ihe hey tfdex i(0-3) *
@Rkeyen: Ley tatierol clagth 
*	@Rkey Ley tatierol  For fALGTKIP_the hey tf menrodd as a m256-it s(32Mbyes) *
@	rta faock : *
 	- Tmporarl Ecrypteon fKy t(128bits ) *
 	- Tmporarl Auhin icasor rTxMIC kKy t(64bits ) *
 	- Tmporarl Auhin icasor rRxMIC kKy t(64bits ) *
 @icv_en: LTe wCV mlagth olr this rey tope  *
 @iv_en: LTe wC mlagth olr this rey tope  *

etruct ieee80211_bkeyconf *{	I32 coiper ;	u8 dicv_en:;	u8 div_en:;	u8 dhwqkey_dx ;	u8 dlags,
	us8 keydx ;	u8 dkeyen:;	u8 dkey[0];};

/**
 * struct ieee80211_bwiper _ehedm;c- oiper tshedm;
*
 * This structure kontains in oiper tshedm;tnformation
refinitgs * ihe nactre kacket erypto,chndleng.
 *	 *
@Rciper  Iatoiper tssie shlectsor *
 @ifsyp;: atoiper tifype iit sapsksndicateng tn ulsowerdtoiper tusgem * @Ihdr_en: LaLlagth of tanactrety mheadr fsed ihe hoiper  * @Ipn_en: LaLlagth of tanacket eumber on the @sctrety mheadr  * @Ipn_off: rt offse.of tpnfrom thessbegnesng.Lf She Osctrety mheadr  * @Ikey_dx _off: rt offse.of tey tfdex ibyeson the @sctrety mheadr  * @Ikey_dx _apsk LaLit sapsksf tey _dx  its  * @Ikey_dx _shify LaLit sshifyieed d bo ket tkey_dx  * @@@@@ey _dx  alue oalculateon
: * @@@@@ (sct_headr _base[key_dx _off] & key_dx _apsk)i>> key_dx _shify * @Iic_pen: LaLic_clagth oinMbyes  * 
etruct ieee80211_boiper _ehedm;c{	I32 coiper ;	u816tifype ;	u8 dhdr_en:
	u18  n_en:
	u18  n_off;	u8 dkey_dx _off;	u8 dkey_dx _apsk;	u8 dkey_dx _shify;	u8 dic_pen:

;

/**
 * enum iactbkey_cmd* pey tommand  *	 *
@Ued with the bactbkey()tallbbnk fns itruct ieee80211_cops,this 
*	indicates thether tahey tf meing sremovd Ar taddd

 *	 *
@RSETTKE_:tahey tf met. * s@DISBLE _KE_:tahey tust be adisbled 
* 
enum mactbkey_cmd*{	ISETTKE_, DISBLE _KE_
	;

/**
 * enum ieee80211_csta_tate m- trticn
shtte
 *  * @IEEE80211_TSTA_oTE:XIST ttrticn
soesn't eexis at lllb  *	ahes is a Qpeciallshtte
tor aldd/removdahansmiiona  * @IEEE80211_TSTA_oTNE ttrticn
sexis  tith ut wpeciallshtte
 * @IEEE80211_TSTA_AUH: Itrticn
ss a uhin icasod
 * @BEEE80211_TSTA_ASSOC Itrticn
ss a sociated  * @IEEE80211_TSTA_AUH:ORIZED ttrticn
ss a uhiorizd t(82.11X) * 
enum meee80211_csta_tate m{	/* roTE: ITe seheed to pe cordr rdtorrect ly!f/
	IEEE80211_TSTA_oTE:XIST,	IEEE80211_TSTA_oTNE,	IEEE80211_TSTA_AUH:,	IEEE80211_TSTA_ASSOC,	IEEE80211_TSTA_AUH:ORIZED
	;

/**
 * enum ieee80211_csta_rx_and wdth s- trticn
sRX and wdth  * @IEEE80211_TSTA_RX_BW_20 ttrticn
san rnly uageive S0 MHz  * @IEEE80211_TSTA_RX_BW_40 ttrticn
san rageive Sp to t4 MHz  * @IEEE80211_TSTA_RX_BW_80 ttrticn
san rageive Sp to t8 MHz  * @IEEE80211_TSTA_RX_BW_160 ttrticn
san rageive Sp to t16 MHz 
* 	(ncludeng.L0 +0 MHz 
 *
 * TIplieent tion fnte: I0 Must be aero to pe cnitioalizd  *	nrrrect ly,the
calies aust be asrted 
 * 
enum meee80211_csta_rx_and wdth s{	IEEE80211_TSTA_RX_BW_20@= 0,	IEEE80211_TSTA_RX_BW_40,	IEEE80211_TSTA_RX_BW_80,	IEEE80211_TSTA_RX_BW_16 
	;

/**
 * struct ieee80211_bsta_rtes t- trticn
sate shlectson/sable  *  * @Ircu_head:nRCUmheadused wfr tree ng txe rable cn/spdate  * @Irae: Ihansmit frtes /lags to de used
 y drefaule
 */	Oer risng tet ies ner-racket eisRpwsiolesbe useog chbtx hontrol.f * 
etruct ieee80211_bsta_rtes t{	utruct crcu_headcrcu_head
	struct i{	/st8idx 
	u_38 hnnt:

u_38 hnnt:sts_

u_38 hnnt:srs_

u_36 crags,
	u}frtes[EEE80211_TX_TATE_CTBLE _SIZE];};

/**
 * struct ieee80211_bst r- trticn
sable cnt iy
*  * @Avtrticn
sable cnt iyrecoeasnte a Qprticn
sw are,4pwsiole  *	tommauntcbiog aith . Snce oprticn
 are uRCU-anaged mns *
 ac80211 ,wny
reee80211_bst rownter oou het tckessaio bast  * eniher te sretectiedby srcu_aad _ock ()texlitcily or rnmlitcily , *	fr
tou hust btak tgoodcareeto deo eue ftsh arrownter onter ta *	toll teotou rcsta_removdaallbbnk fhat aremovd Ai.  *  * @addr_:aMACaddress  * @adid iAIDsw arsigned io the barticn
ssfrwe'e csn AP * @atppo_rtes : Btmappof stpported yates,s(er sand 
 * @Ih:stap 8T Mapabilities tofthis cSTA;resp ieted to tur dwlnMapabilities  * @Ivh:stap 8VT Mapabilities tofthis cSTA;resp ieted to tur dwlnMapabilities  * @Iwe
:pndicates thether thienTA wtpportes QoS/WME
 * @Reiv_pive:rrta fre afor driver fsed,will always be slitned io 
*	nezeof(void *)), izeoeisRotecrmne  in hawtnformation

 * @Ruapsdqueue__:sitmappof sueue__ onfigured aor auapsd. Oly calid 
*	Iifawesis atpported 
 * @Rca_stp:marx Serice tPriod:. Oly calid  ifawesis atpported 
 * @Rand wdth :current cand wdth she barticn
san rageive Sith  * T@rx_ns):ni HT/VHT ,nte maximum number of ssptiol ctreams thae
*	nerticn
san rageive Sn the hmoent ,ohanged
by speratiog code  * 	otifiecbion  ind nopabilities  mTiefalue os nnly calid inter  *	ahe derticn
smovd to associated Atate 
 * @Rhp  mode;: urrent cSMS:mode (Soff, trtic,ir ednamic_) * @Irae: :rate control ahlectson/sable  *  @tdls:pndicates thether thienTA ws a QTDLSRper  * 
etruct ieee80211_bstac{	I32 ctppo_rtes [EEE80211_TUM_BANDS];
	u18 rdre[ETH_AEN];
	s86 asid
	struct ieee80211_bsta_h:stap h:stap
	struct ieee80211_bsta_vh:stap vh:stap
	sool iwm ;	u8 duapsdqueue__;	u8 dia_stp;	u8 rx_cns);	unum meee80211_csta_rx_and wdth sand wdth 
	_num meee80211_csp  mode;csp  mode;;}struct ieee80211_bsta_ates,s__rcu *ates,
	sool itdls;
	/* rust be aast f/
	I8 dtrv_pive[0] __litned (ezeof(void *)))

;

/**
 * enum iata_otifiy_cmd* pstacotifiytommand  *	 *
@Ued with the bata_otifiy()tallbbnk fns itruct ieee80211_cops,this 
*	indicates tifasnOssociated Atateon aaye (npuwersseate mhansmiiona
 *	 *
@RSTA_oTEIFY_SLEEP:a Qprticn
ss notwsleep tgs * iRSTA_oTEIFY_AWAKE:a Qpeep tgsQprticn
swok Sp 
* 
enum mata_otifiy_cmd*{	ISTA_oTEIFY_SLEEP, STA_oTEIFY_AWAKE
	;

/**
 * struct ieee80211_bx_control a-mTX@ontrol aata  *  * TItat:vtrticn
sable cnt iy,this ctrrrownter oay be sNULLfnd  *	 	tsos notipasowerdto krop the Rpwnter ,bue to PRCU
 */
struct ieee80211_cx_qontrol a{}struct ieee80211_bsta*)tat

;

/**
 * enum ieee80211_chwclags - phndware qlags
 *
 * These flags are used wo undicate tardware capabilities to 
*	@he batack. Gneratlby,tlags aere'chould bave toieir meanng 
*	@one onsOa waychin the hsmpliet bavdware coesn't eeed tsettng 
*	@ny
rrat iclar dlags 
These are,4sme vexcetiona io thensreue.
 * ihwerer  tsotou are ucdvied wo urevie thr e flags aareefulby
 *
 * @IEEE80211_THW_HASTATE_CCONTROL: * 	Te hardware sr trirmare sncludeesrate control ,ind nopnot ab
 *	iontrol ed wy the Ratack. Asftsh ,no mate control algorithm 
*	neould be snsteantated ,(n  she oTX@ate saqorted yo usedrpace  *		ill be stak nfrom thessX status bnftead of ahe Rate control  *		lgorithm 
 * 	Nte that this iequires Ehin the hriver fmplieent anOumber of  *	iollbbnk stsoti has tohenrrrect anformation/,aitmueds to dave  *	ahe d@actbrtschieesoldstallbbnk fsd mast blookSn the hSS ronfigu *	a@se_sts_prot:aor aG/Nsretection/,a@se_short_pslt:aor aslt: *	ahimtgsQnsO2.4 GHzfsd m@se_short_preamble. or aroamble.s(or
 *	bCCKframe
s
 *
 * @IEEE80211_THW_R_TNFCLUDES_FCS: * 	ndicates what areeived (rames basseddio the bartk fnsludee *	ahe dCS wn the hend
 *
 * @IEEE80211_THW_HOST_BROADCAST_PS_BUFFERING: * 	hme vwielevs, LANichipsee beufera broadast /ulticast rrames 
*	por arwersseveng.oprticn
 ai whe hardware /rirmare scd onher  
*	peleyon whe haot fsyteamaor assh aeuferang. Ihis sptionaMs used 
*	to ponfiguredthe hEEE8s82.11-Sp er slayr to ueufera broadast qnde
*	pulticast rrames when the
r are,4pwersseveng.oprticn
 as that  *	ahe driver fan rfeth roe mtith teee80211_cgt _euferaed_bc()
 *
 * @IEEE80211_THW_2GHZ_HORT_GSLOTTNFCAPBLE : * 	Hrdware ss notipapabil iofthort_aslt:Tperation.on whe h2.4 GHzfand 
 *
 * @IEEE80211_THW_2GHZ_HORT_GREAMBLE	TNFCAPBLE : * 	Hrdware ss notipapabil iofteceiving arames whth tsort preamble. n
 *	the o2.4 GHzfand 
 *
 * @IEEE80211_THW_IGN_AL_UNSPEC: * 	Hrdware san rrotvid tignma salies aeuetwehon't
n
owlits suito . W
 *	sexoetisalies aeetwen c0fsd m@ia_stgnma 
 * 	f tpwsiolesbliese.crotvid tBmor
 Bmm nftead 
 *
 * @IEEE80211_THW_IGN_AL_DBM: * 	Hrdware sives tignma salies an dBm
,tdecibl/deiferante ilom 
*	an  dinlliwatt.This is ahe Rpaeeraed amtheod@ince on ws ctrrndrdwizd  *	neetwen cdiferant bevice  . @ia_stgnma doesnaotipeed to pe cwn 
 *
 * @IEEE80211_THW_IPECTRM_BMGMT  *	 	Hrdware stpportes poetirm iacagement fefine danfD82.11-  * T	Meseredent ,oCannel  Swith , Quieeng,
tTPC *
 * @IEEE80211_THW_MPDU_NAGGREGATION: * 	Hrdware stpportes 11nw-MPDU
 cgregateona
 *	 *
@REEE80211_THW_IUPORT_S_PS: * 	Hrdware shrsRpwersseve msuported(i.e.ian rgoto sleep )
 *
 * @IEEE80211_THW_S:_UMLLFUN_FSTACK: * 	Hrdware sequires Enulbrnctrramesihndleng.onsOirtk ,rnmlits 
*	partk fsuportedfr ednamic_cPS
 *	 *
@REEE80211_THW_IUPORT_S_DYNAMIC_PS: * 	Hrdware shrsRsuportedfr ednamic_cPS
 *	 *
@REEE80211_THW_MFP_CAPBLE : * 	Hrdware stpportes anagement framessretection/b(MFP,hEEE8s82.11-w)
 *
 * @IEEE80211_THW_IUPORT_S_UAPSD: * 	Hrdware stpportes Uneheduled wAutoatiocIPwerssSve mDelver   *	a(U-APSD)os managed mode) mTiefode;ci_ onfigured aith 
*
	honf_tx()tperation.
 *  *	@IEEE80211_THW_REORT_S_TX_ACK_TAT_US: * 	Hrdware san rrotvid ttk fsatus beqortes f sTxarames wo 
*	tre Ratack. *  *	@IEEE80211_THW_CONNECTIONLORNITOR: * 	Te hardware srrforme bto ofwnconnection monitor ng , ncludeng. * 	eriod:ocIeeps-alivd to ahe RAPard narobng txe rAP nnbeacon
closs
 *
 * @IEEE80211_THW_NEED_TIM _BEFORE_ASSOC 
*	This ievice tueds to det tdta fram teacon
cetfre fssociateon/L(f..
 * 	tim_period:)
 *
 * @IEEE80211_THW_IUPORT_S_PERTSTA_GTK ITe wevice_'stoypto,cncgne Itpportes * 	eri-trticn
sGTK a sused
 y dBSS
 RSNor
 Bring tfst rransmiiona
 I 
*	the drvice toesn't esuportederi-trticn
sGTK ,but cannre sls d bot
 *	bo docryptitgroupaddress d (rames ,thin aBSS
 RSNosuporteds ctrll
 *	bpwsiolesbet wpoftare crypto,till be ssed . Aderteis [ha vwiph tlags
*	an y wn that case.
 *
 * @IEEE80211_THW_AP_LINKLS: PWen dperatiog cinrAP ode)ohe drvice  *		lutonomousy uenaged ahe RPSfsatus bofconfncted tprticn
 .PWen 
*	ahes ilag is set.tac80211 will bot trieger tS:mode (or tontnctid
 *	aprticn
 abasedon whe hP bet cofcnclomng arames 
 * 	Ue teee80211_bstat_prs()/eee80211_endprs()fo macnally uonfigured
*	the dS:mode (ofconfncted tprticn
 . *
 * @IEEE80211_THW_TX_APDU_NSETUPTNFTHW ITe wevice_ihndle ssX s-MPDU
 sesion  *	apetup p ietey wn tHW.tac80211 whould not battemptto ad thas rnt *	apoftare 
 *  * @IEEE80211_THW_WANTLORNITORTVIF ITe weiver thuld nike io pe cniormed
of  *	inoviruallionitor rnterface ihen aonitor injerface  are uhe dnly) * 	ctive anjerface  ) *  * @IEEE80211_THW_QUEU_CCONTROL:ITe weiver that: io pontrol aer-rnterface  * 	ueue.fmfplng.wn tordr yo usedcdiferant bueue__ (ot bjst bn  der sAC)
*	por adiferant bairuallinterface s. Se the ndocnaction io tTWqueue_ *	iontrol  or nore  eteail 
 *
 * @IEEE80211_THW_IUPORT_S_RCCTBLE :ITe weiver ttpportes seog c fate  *	apeectson/sable Lropvid dnbyahe Rate control  lgorithm 
 *  * @IEEE80211_THW_S2P_DEV_ADDR_FORINT	F IUs [ha vS2PmDeice_iddress lor an ) * 	S2PmIterface 
This iill be shonored aven iifnore  hat one interface  * 	s atpported 
 *  * @IEEE80211_THW_TIMINGBEACON_OONLY IUs [tnch himtgsQram teacon
crames 
*	pnly),to assowedet iog cTBTTof tanTIM beacon

 *
 * @IEEE80211_THW_IUPORT_S_HT_CCK_ATES	: Hrdware stpportes aixng pHT/CCKfrtes  *	and nopnkrop wilh tCCKfrtes indany cgregateona sesion w(.g. Ebybot
 *	bseog c gregateona or assh arames 
) *
 * @IEEE80211_THW_HANGCTX_STA_CSA: Suported82.11- abasedohannel -swith r(CSA)
*	por acsingle tctive ahannel  hile useog channel  ontext  .PWen stpporte * 	s aot bncbled Bhe drvfauletctivnaMs uo sdisonfncteihen aet iog chae
*	nCSAorame.
 *  * @IEEE80211_THW_IUPORT_S_CLONED_SKBS:ITe weiver thll boeer todefiythe Rpayoad 
*	an nxailroo rf aX stkb tith ut wrop ng txe mtfist 
 *  * @IEEE80211_TSINGLETHW_ICA_OON_ALLBANDS]:ITe wTWqspportes panning cn (al
abnds  * 	s one iommand ,tac80211 woesn't eave to arunasearagt Oicni ter sand 
 * 
enum meee80211_chwclags -{	IEEE80211_THW_HASTATE_CCONTROL		= B<<0,,	IEEE80211_THW_R_TNFCLUDES_FCS		= B<<01,	IEEE80211_THW_HOST_BROADCAST_PS_BUFFERING= B<<02,	IEEE80211_THW_2GHZ_HORT_GSLOTTNFCAPBLE 	= B<<03,	IEEE80211_THW_2GHZ_HORT_GREAMBLE	TNFCAPBLE = B<<04,	IEEE80211_THW_IGN_AL_UNSPEC		= B<<05,	IEEE80211_THW_IGN_AL_DBM			= B<<06,	IEEE80211_THW_NEED_TIM _BEFORE_ASSOC	= B<<07,	IEEE80211_THW_IPECTRM_BMGMT		= B<<08,	IEEE80211_THW_MPDU_NAGGREGATION		= B<<09,	IEEE80211_THW_IUPORT_S_PS		= B<<01,,	IEEE80211_THW_S:_UMLLFUN_FSTACK		= B<<011,	IEEE80211_THW_IUPORT_S_DYNAMIC_PS	= B<<012,	IEEE80211_THW_MFP_CAPBLE 		= B<<013,	IEEE80211_THW_WANTLORNITORTVIF		= B<<014,	I/*tree aslt:sf/
	IEEE80211_THW_IUPORT_S_UAPSD		= B<<017,	IEEE80211_THW_REORT_S_TX_ACK_TAT_US	= B<<018,	IEEE80211_THW_CONNECTIONLORNITOR		= B<<019,	IEEE80211_THW_QUEU_CCONTROL		= B<<02,,	IEEE80211_THW_IUPORT_S_PERTSTA_GTK	= B<<021,	IEEE80211_THW_AP_LINKLS:			= B<<022,	IEEE80211_THW_TX_APDU_NSETUPTNFTHW	= B<<023,	IEEE80211_THW_IUPORT_S_RCCTBLE 		= B<<024,	IEEE80211_THW_S2P_DEV_ADDR_FORINT	F	= B<<025,	IEEE80211_THW_TIMINGBEACON_OONLY		= B<<026,	IEEE80211_THW_IUPORT_S_HT_CCK_ATES		= B<<027,	IEEE80211_THW_HANGCTX_STA_CSA		= B<<028,	IEEE80211_THW_IUPORT_S_CLONED_SKBS	= B<<029,	IEEE80211_TSINGLETHW_ICA_OON_ALLBANDS]	= B<<03 
	;

/**
 * struct ieee80211_bhw- phndware qnformation
rnd nhtte
 *  * @his structure kontains iohenrrfiguretion fnd nardware  *	qnformation
ror an a82.11-SPHY
 *  * @Iwiph :This iownte to ahe R&truct iwiph tssowates
wlr this 
*	p82.11-SPHY
IYu cast bfiletn the @Ier m_ddrefsd m@evi * 	ember  tofthis ctructure kseog cSETTEEE80211_TDEV() *	and nSETTEEE80211_TPERM_ADDR(). Addiionatlby,tal
atpported  *	nends  (ilh thannel  ,but ares,) re seqgsteded ae
r 
 *	 *
@Rcntf Iitruct ieee80211_ccntf,device tarfiguretion ,hon't
nue
  *  * @a ive:rownter oo a ivegt Ore afhat cas rssowates
wlr triver fsed *	anaongwith thes ctructure 
 *  * T@lagss:phndware qlags
 see @&num meee80211_chwclags 
 *  * T@xt racx_qheadroo : headroo wo uresere anjtach rhansmit ftkb
*	por asedcy the
driver t(.g. Elr thansmit fheadr  
) *
 * @Ixt raceacon
_xailroo :nxailroo ro uresere anjtach reacon
ctxsokb
 * 	Cn be used
 y drivers to kdd nxt ra IE 
 *  * T@ia_stgnma  OMximum nalue oir
tignma s(rsio)os mRXanformation/,ased 
*	tnly when tIEEE80211_THW_IGN_AL_UNSPECor
 IEEE80211_THW_IGN_AL_DB *  * T@ia_sasted _nterval :rarx asted interval cn tuito of beaconsinterval
 * ahen tTWqspportes *  * T@ueue__:sumber of savailble Lhndware qhansmit fueue__ or
 *	bdta faskets .PWMM/QoSsequires En tieseedfru ,the
s  * 	ueue.speed to pave tarfiguretlessckessaiaragmeer
 
 *  * T@ate _ontrol _lgorithm :Rate control  lgorithm olr this rardware.
 *
	f tnset a(UMLL),Bhe drvfauletcgorithm oill be ssed . Mst be  *	apetcetfre fallbng.wnee80211_caqgstedechw()
 *
 * @Ivif_ata _ezeo: izeoe(inMbyes )fpf he werv_piverrta fre a
*		ilh i oitruct ieee80211_cvif
 * @Rha _ata _ezeo: izeoe(inMbyes )fpf he werv_piverrta fre a
*		ilh i oitruct ieee80211_cha . * @ahandcx_cata _ezeo: izeoe(inMbyes )fpf he werv_piverrta fre a
*		ilh i oitruct ieee80211_coanncx_contf
 *  * T@ia_srae: :raximum number of salernalte@ate saq iyrha ed ahe Rhw *	ion bhndle 
 * @Rca_seqortesrae: :raximum number of salernalte@ate saq iyrha ed 
*	the dhw-an ragortedback. * T@ia_srae:_ ies :raximum number of s ies nor
tach rstgem *  * T@ia_srx_ gregateona_subrames
:raximum neufera izeoe(umber of  *	isub-rames
)to de used
 or
t-MPDU
teock ack areeived
 *	bcgregateona
 *	This is snly uaglevantMf the cevice_ihnsresp ietecn
 an whe  *	Tumber of ssubrames
,tf it
deqles tondac80211 wo md ureordr ng. * 	s should 't
ne cwn 
 *
 * @Ima_sx_c gregateona_subrames
:raximum number of ssubrames
indany *	bcgregateean aHTceiver thll bhansmit ,used
 y dhe Rper tasal *	ahnt mo slzeoeitsureordr neufera
 *
 * @Ioffwannel/_yx_hwqueue_ HTWqueue_ IDyo usedcor
toffwannel/@X 
*	p(f t%EEE80211_THW_QUEU_CCONTROLis set.) *  * T@atdiotap_ms,_eteail :rasteswhech aCS insormation
ran rte wTW
*	pelortes, y drefauleon ws ctt io mMCS	, _GIfsd m_BWbet woesn't  * 	nsludee _FMT.IUs [IEEE80211_TRADIOTAPMCS	_HAVE_*salies ,dnly) * 	cdsng t_BWbs atpported io day. *  * T@atdiotap_vh:seteail :rasteswhech aVT MCS insormation
rte wTWbeqortes  *	ahe drefauleons _GIf| BANDSWIDTH
 * 	Ue the dIEEE80211_TRADIOTAPMHT_MKNOWN_*salies . *  * T@netevi_feaure _:suetevicfeaure _to pe cwn cnjtach rnetevicaeatoee
*	pfom thens HW.tNte tnly uTWbhackism nfeaure _tre crrrently  *	ionpltioilesath tan80211.
 Oher tfeaure  its oill be srejcted . *  * T@uapsdqueue__:shis iitmappos rntludeedanfD(re)ssociateon/Lramesso undicate 
*	por aach rckessaiate ori tf it
ds usAPSDtrieger -ncbled Bsd mdelver  -
*	senbled .IUs [EEE80211_TWMMTEE_STA_QOSINFO_AC_*st uet this iitmapp
 * 	Ech ret carrecponsd uo sdiferant bAC. Vlue o'1'onsOieciafc,iit saani  * ahen tarrecponsdng tACMs uboh thieger -Bsd mdelver  -enbled .I'0'saani  * anniher tenbled . *  * T@uapsdqia_stppen: Laximum number of s ota aufferad (rames bhe dWMMrAP oa  *	idelver to kddWMMrTA wBring tny
rSerice tPriod:thieger d
 y dhe RWMMrTA 
 * 	Ue tEEE80211_TWMMTEE_STA_QOSINFO_SP_*sor tonrect aalies . *  * T@nboiper _ehedm;s:a QpzeoeofasnOsrray(ofcoiper tshedm;sdefinition s
 *
@Rciper _ehedm;s:a Qownter oo asnOsrray(ofcoiper tshedm;definition s *	isuported ybyhHW
 */
struct ieee80211_chw-{}struct ieee80211_bonf *onf 
	utruct iwiph t*wiph 
	uon
s whanr *ates_ontrol _lgorithm 
	uoid *)pive;	I32 clags_;	u8nigned int mxt racx_qheadroo ;	u8nigned int mxt raceacon
_xailroo 
	int mvif_ata _ezeo
	int mha _ata _ezeo
	int mhandcx_cata _ezeo
	s86 aueue__;	u86 aia_sasted _nterval 
	us8 ia_stgnma ;	u8 dia_sates,
	s8 dia_saqortesrae: ;	u8 dia_sates_ ies ;	u8 dia_sa_c gregateona_subrames
;	u8 dia_sx_c gregateona_subrames
;	u8 doffwannel/_yx_hwqueue_
	u8 satdiotap_ms,_eteail ;	u86 aatdiotap_vh:seteail ;	unetevi_feaure __t netevi_feaure _;	u8 duapsdqueue__;	u8 duapsdqia_stppen:;	u8 dnboiper _ehedm;s
	uon
s wtruct ieee80211_boiper _ehedm;c*oiper _ehedm;s
	;

/**
 * struct ieee80211_bsan _equeste- phwOicnisequeste *  * T@es :rownter ssdiferant brat  of bIE e(inMequ.ie) * @Ireq:ifg80211_requestef * 
etruct ieee80211_bsan _equeste-{}struct ieee80211_bsan _es tes ;		I/*tKepsrast f/
	Itruct cfg80211_csan _equeste-eeu
	;

/**
 * swiph _to_eee80211_bhw- peturne avac80211 woiversphwOiruct irom asswiph  *  * @Iwiph :The R&truct iwiph thech awethat:oo auesiy
*  * @ac80211 woivers tcnissedthis to ket tootoieir espoetive  * @itruct ieee80211_chw. Divers titshng to tet tootoieir wlnpeivegt  * struct ue san rhin aacessaitsmviaphw->eive.Note:that tac80211  woivers thould  * seo eue fwiph _eive()fo m iyro ket toe ira ivegt Oeiver ttruct ue ss tohs 
*	in rssaad yused
 iternal lyiy dac80211 . *  * TRturne:ITe wac80211 woiversphwOiruct if bIwiph 
 */
struct ieee80211_chw-*wiph _to_eee80211_bhw(truct iwiph t*wiph )

/**
 * eSETTEEE80211_TDEV* pse bevice sor t82.11-Sardware  *	 * @Ihw:The R&truct ieee80211_chw-t uet thiebevice sor  * @Ievi the d&truct ievice_fofthis t82.11-Srvice  *	//trtic,inflne Ioid *SETTEEE80211_TDEV(truct ieee80211_chw-*hw, truct ievice_f*evi)
{}stet_wiph _evi(hw->wiph ,devi);
}
s**
 * sSETTEEE80211_TPERM_ADDR* pse bhe Rpematnnt bMACaddress sor t82.11-Sardware  *	 * @Ihw:The R&truct ieee80211_chw-t uet thiebMACaddress sor 
* @addr_:ahiebddress st uet  *	//trtic,inflne Ioid *SETTEEE80211_TPERM_ADDR(truct ieee80211_chw-*hw, 8 d*ddre)
{}smemcpy(hw->wiph ->er m_ddre, ddre, ETH_AEN]);
}
strtic,inflne Itruct ieee80211_cagt O*
eee80211_cgt _t_sates(on
s wtruct ieee80211_bhw-*hw,
		@@@@@ on
s wtruct ieee80211_bt_snsorc*o)
{}sf i(WAR_OON_ONCE(c->ontrol.frtes [0].dx  < 0))
		eturne UMLL;
seturne &hw->wiph ->ends [c->ends]->et ares,[c->ontrol.frtes [0].dx ];
}
strtic,inflne Itruct ieee80211_cagt O*
eee80211_cgt _rtscts_pates(on
s wtruct ieee80211_bhw-*hw,
			@@ on
s wtruct ieee80211_bt_snsorc*o)
{}sf i(c->ontrol.frtscts_pates_dx  < 0)
		eturne UMLL;
seturne &hw->wiph ->ends [c->ends]->et ares,[c->ontrol.frtscts_pates_dx ];
}
strtic,inflne Itruct ieee80211_cagt O*
eee80211_cgt _al _rq iypates(on
s wtruct ieee80211_bhw-*hw,
			@@ @ on
s wtruct ieee80211_bt_snsorc*o,int mdx )
{}sf i(c->ontrol.frres,[dx  + 1].dx  < 0)
		eturne UMLL;
seturne &hw->wiph ->ends [c->ends]->et ares,[c->ontrol.frtes [dx  + 1].dx ];
}
s**
 * seee80211_bree bt_skb* pree aX stkb * @Ihw:The Rardware  *	q@tkb:The Rtkb *  *	qFee aarhansmit ftkb.IUs [has irncicn
swen stme vfailred
*	fo m insmit fhappend (n  sheusfsatus bopnot ab
saqorted 
 */
soid *eee80211_bree bt_skb(truct ieee80211_chw-*hw, truct isk_eufe *skb)

/**
 * eDOC IHrdware saypto,taceslrateon 
*	 * @ac80211 ws sanabil iofttaing.Ladvantagefpf anayRardware  *	qaceslrateon devtgnm nor
tacrypteon fsd mderypteon fperation. . *  * TTe bactbkey()tallbbnk fns he R&truct ieee80211_copspor al ivesn * Tevice_insrullbd to,cncble Lhndware qaceslrateon dpf acrypteon fsd  * Tevrypteon  mTiefallbbnk fhake ua@Rha maragmeer
ohat call be sNULL * Tlr trefauleoey sforoey sfsed
 or
thansmission anly),tr arwnt mo 
*	@he bataeon fnsormation
ror ahe Rper tfr injdividalliey s
 *
@Mlticle.thansmission aey sfith the bame fkeytfdex iay be ssed then  *
@VLAN_tre crnfigured aor an ackessaiawnt . *  * TWen shansmitteng , hessX sontrol aata call bue the dIhwqkey_dx  * steectsd ybyhhe driver iy dadefiyng txe ritruct ieee80211_ckeyconf  * sownter to peyhhe dIkeymaragmeer
ohothe bactbkey()trnction . *  * TTe bactbkey()tallbror ahe R%SETTKE_iommand whould neturne 0 if
*	@he bey tf mowlitnfsed,w-%EOPNOTIUPOtr a-%ENOSPCtf it
dculd 't
ne  *	qaddd
;of tou neturne 0 hin ahwqkey_dx rust be arsigned io the  *	qhndware qkeytfdex ,tou are uree ao usedche bfulb 8 satnge. *  * TWen shiefam os s%DISBLE _KE_ hin at sast bsukesd . *  * Tote that tn wi ter issiole Lo deo eocryptit rramesiven iifnahey  * Tlr ti has teen cupoad d to pavdware ,the bartk fill bot tmak tn ) * tdeciion abasedon whether tahey tas teen cupoad d tr tot abut[ateer  * tbasedon whe hageive Slags 
 *  * TTe ritruct ieee80211_ckeyconf ttruct ue sownter to peyhhe dIkey * soragmeer
on sguragterd to pe calid intil afnnher toll teotactbkey() * sremovdaits,but ct can unly ue ssed ts a mcookieuo sdiferant igt  * sey s
 *
 * sIn IIP ctme vHWpeed to pe cropvid dn mahse.c1sey  for eRXTevrypteon  *	qaceslrateon d(i.e.iiwlwifi) mTiosewoivers thould rrotvid tpdate _tkip_key * shndle r
 *
@Te rpdate _tkip_key()tallbrpdate  the driver tith the bew fahse.c1sey 
 *
@Tes rarppensiveniyrove
phe biv16 wrap_treoud w(veniyr65536faskets ) mTi  * stctbkey()tallbrill barppenunly uonc.for
tach rey t(unevs, xe rAP dd 
*	srekeydng),tn will bot ansludee acalid iahse.c1sey 
mTiefalud iahse.c1sey is 
*	Lropvid dnbyapdate _tkip_keyunly 
mTiefrieger that tacke uac80211 woll tehs 
*	ihndle rbs atoftare cderypteon fith twraptreoud wofcnv16. *  * TTe bactbrefaule_untcbst_key()tallbrpdate  the drefauleoWEPqkeytfdex  * Tonfigured aroThe Rardware for
tWEPqacrypteon fype .This is aequired. * Tlr treice  that tsuportedoffoad iofcdta faskets t(.g. EARP ecponsses)
 * 
e/**
 * eDOC IPwerseve mtpporte *  * @ac80211 whrsRsuportedfr evariouspuwerseve mmplieent tion s
 *
 * sFist ,ct can usuportedhndware qhat thfdle ssllbruwerseveng tbyit elf.
 * issh ahndware qhould rsmpli tet thieb%EEE80211_THW_IUPORT_S_PSRardware  *	qlags
Intthat case.,tn will be stldstllut whe dresred.Luwerseve mode  * swih the b%EEE80211_TCONFMS:flag idepesdng tn whe hssociateon/Lsatus 
 *
@Te rhndware qust btak tareetf ssesdng tnulbrnctrrames when tecessary.
 * ii.e.ihen tenering.acd loeveng tuwerseve mode .@Te rhndware qs aequired. * TroTlookSn the hAIDsinbeacon
 ind nignma so ahe RAPahat tn wiok Sp then  *
@i bfisd uoanffc,iti_et d wo un . *  * T%EEE80211_TCONFMS:flag incbled Beani than the huwerseve mode fefine danf * sIEE8s82.11--2007naction i11.2in sncbled .This is aot tr pe confised 
*	swih thndware qwakeupind nieep (htte
s. Diversqs aeqponssile. or awaing.
*	sp toe rhndware qetfre fssiuog chmmand saroThe Rardware frd nau iog cit * tbak fo mieep (t llproterite mhie
s
 *
 * @Wen sS:fn sncbled ,Rardware fueds to dwakeupior aeacon
 ind nageive She  *	qufferad (ulticast /broadast qrames wnter the Reacon

 Alsoti hmst be  *	tpwsiolesbeotacn (rames bnd nageive She ack owled ent frames
 *
 * @Oher tavdware covtgnm nopnot aacn (nulbrnctrrames weyhhe melfvs bnd nlso  * seed tsoftare csuportedfr eorasng txe rIM bet app
This is also uepported  *	dy dac80211 wbytommbnsng.Lhe d%EEE80211_THW_IUPORT_S_PSRsd  * T%EEE80211_THW_S:_UMLLFUN_FSTACKdlags 
Thesrhndware qs aofconurs [trll
 *	uequired.Lo a ssosp teacon
s Thesrhndware qs asoll bequired.Lo ahfdle 
*	swaing.Lupior aulticast roanffc,;tf it
dcpnot ahe driver tmst bhfdle that  *	 s teet qnsit
dcpn,@ac80211 ws sto mieowto ad thaa . *  * TDnamic_cuwersave ms al
sextenson io mnrmatlcuwersave msnthech ahe  *	qhndware qhtt sfawakepor al sedr-ieciafcd.Luriod:tofttve
pnter tsesdng ta *	qlamesis that saqoy woames weed tot ab
sufferad (n  she rtfre fdelayrdmo 
*	@he bnxt hwakeup
Int'stoomrotmis [oftet iog cgoodcenugh tatedncythen  *
@he rt'stdta foanffc,ind nhtll beveng.opgnmfiecbtly Rpwersssntile 
*	suriod:s. *  * TDnamic_cuwersave ms asmpli teuported ybyhac80211 wncbleng.acd ldisbleng.
*	sPSRbasedon whanffc,. Diversqueds to dnly uet t%EEE80211_THW_IUPORT_S_PS *	qlagscsd mac80211 rill bardle tveniythng tautoatiocl ly. Addiionatlby, *	qhndware qhveng.opuportedfr ehe drnamic_cPStfeaure  ay bet thie
*	t%EEE80211_THW_IUPORT_S_DYNAMIC_PSirag.io,cndicate that aitran usuporte * tdnamic_cPStode;ci elf..ITe weiver tueds to dlookSn the  * @Ienamic_ps _smesut tardware carfiguretion falue osd msedcitthat aalue 
*	swen eer t%EEE80211_TCONFMS:fs ctt 
Intthansrulsemac80211 rill bdisbled * tdnamic_cPStfeaure  nsOirtk osd mill bjst beepst%EEE80211_TCONFMS: * tncbled Bwen eer tsedrwhrsRncbled Buwersave . *  * TDiversqsnorme bU-APSD clint fpuportedbyhncbleng.
*	t%EEE80211_THW_IUPORT_S_UAPSDqlags
ITiefode;ci_ onfigured athrugh the 
*	spapsdsoragmeer
onnconnf_tx()tperation.
IHrdware sueds to dacn (he dQo: * tNulbrnctrrames wnd nhttyfawakepptil ahe Bserice turiod:thrsRncdd .Th 
*	@uil zeoeU-APSD,tdnamic_cuwersave ms adisbled dfr evoiptACMnd nlslcrames 
*	from theattACMne qhansmit ed Aith tuwersave menbled . *  * TNte: IU-APSD clint fode;ci_ ot ayetteuported yith  * T%EEE80211_THW_S:_UMLLFUN_FSTACK
 * 
e/**
 * eDOC IBacon Tilterimtpporte *  * @hme vhndware qhvee bacon Tilterimtpportero ureduc haot fcpuiwakeups * Thech awll beqduc hsyteamapwersson
sumteon  mI eueally uwork as that  *	che bfirmare saeatoe a mcackism npf he weacon
cet ttmitssllbron
s atly  *	ohangeng teieent  t(TSF,rIM betc).PWen eer toiefaackism nhangedsShe  *	quaconsinspor aredd aroThe Raot ,cnher wis [n will be sjst bdotepd .Thit  *	cwaychi haot fill bnly uageive Seacon
 iwere'chme vaglevantMfformation
 *
 (or
taxaplie ERP retection/br
tWMMtsettng s)pave taanged
. *  * TBacon Tilterimtpportein saderteis dswih the b%EEE80211_TVIFBEACON_OILTERE *
@iterface iapabilitiy.ITe weiver tueds to dncble Lbacon Tilterimtpporte
*	swen eer tpwersseve mn sncbled ,Rhat aisr%EEE80211_TCONFMS:fs ctt 
IWen 
*	tpwersseve mn sncbled ,Rha bartk fill bot taackiior aeacon
closs(n  she  * tdiver tueds to dotifiytllut wloss(f beaconsstith teee80211_ceacon
_loss()
 *
 * @Tiefrve
p(r cumber of seaconsstissid )wptil ahe Bfirmare sotifiedsShe  *	qdiver tf taneacon
closs(ven t (hech ans hrne cased the driver to poal
 *	ueee80211_ceacon
_loss())ieould be sarfiguretlesscd mill bb control ld  *	dy dac80211 wn  she oroamtgsQcgorithm ons he Rfuure 
 *  * TSnce ohe rtiay be son
s atly ohangeng tnsormation
reieent  that annheng.
*	tn the @softare csrtk faree allut ,awethll ,ons he Rfuure ,pave tac80211  *	chellthe driver tiech ansormation
reieent  tre qnfededsiog cinrhe Bsensd
*	foat caethat:oo aee @hangedsSinrhe m
This iill bnsludee *	  -LaLlis aofcncormation
reieent  IDs *	  -LaLlis aofcOUIsdfr ehe dvendr tncormation
reieent  *
 * sIdetlby,the Rardware fhuld nilterimut wnayReaconsstith ut wrangedsSinrhe  *	uequisted reieent  ,but ct it
dcpnot atpporteroat aitray ,wn the hexpensd
*	ff ssme veffc,idncy,nilterimut wnly uassubtt 
IFr
taxaplie,Mf the cevice_ *	qdesn't esuportedaackitgsQrr
tOUIsds should a ssosp tllbroangedsSinral
 *	uvendr tncormation
reieent s. *  * Tote that toanged,dfr ehe dsakepf ssnmlitiecbion ,also uncludeesrfformation
 *
 eieent  trepdaing.ar adisrepdaing.arom thessbecon

 *
 * @hme vhndware qspportes n u"gnmre flis " nftead ,sjst bmak teredannheng.
*	that cas requisted ri an whe  gnmre flis ,(n  snsludee hmmanly uhangeng 
*	tn ormation
reieent  IDscinrhe Bgnmre flis ,(or
taxaplie 1 w(SS
 oad )Rsd  * The dvariouspvendr -rsigned iIE eith tun owln ontext  t(128, 129, 133-136, *	q149, 150, 155, 156, 173, 176, 178, 179, 219);wfr trr ared onpltioilitiy *
@i bculd blso uncludeessme vrrrently tunsed tIDs
 *
 *  * sIn addiionaaroThe s capabilities ,ahndware qhould rsuportedotifiyog chae
*	haot fofcoangedsSinrhe neacon
cRSSI.This is aeqlevantMo, mplieent oroamtgs
*	swen no moanffc,is ilaw
ng t(wen shanffc,is ilaw
ng tweaee @he nRSSIof  *	whe hageive dcdta faskets ).This san bonssis aindotifiyog chaehaot fien  *
@he nRSSIooangedsSpgnmfiecbtly Rr awin at sdote teeeowtr arise allue  * @arfiguretlesshieesoldss
Inttha Rfuure The s chieesoldsswill alws de
 *	 onfigured ay dac80211 w(hech age  tha mtfom tsedrpace )Mo, mplieent  *
@he m s tohenroamtgsQcgorithm oequires 
 *
 * sIfthe Rardware fcpnot amplieent ohans,the driver thould rpsksn mo 
*	@eriod:oclly u ssoseacon
crames aroThe Raot is that spoftare cranad tha  * stgnma streagth ohieesoldstaackitgs
 * 
e/**
 * eDOC ISptiol cmlticle.xng tuwersseve  *
 * @hMS:m(Sptiol cmlticle.xng tuwersseve )ws a Qmeoangismio pontsere 
*	tpwerssndany 82.11-nmmplieent tion 
IFr
teteail an whe  meoangism *	 sn yateonatld,dliese.caeerato t8 .11-S(s a mncdd ay d82.11-n-2009) * s"11.2.3@hMtuwersseve "
 *
 * @Tiefac80211 wsplieent tion fs sanabil ioftsesdng taceon/Lramess * TroTpdate ahe RAPallut whe dataeon 'scSMS:mode ,scd mill bnfteuct  *
@he nriver to pener the Rieciafc,iode .@Itwill alws dpnotuce ohe  *	uequisted rSMS:mode (Bring the hssociateon/Lardlshak 
IHrdware  * issportedfr ehes ileaure  n aequired.,ind nopnpe cniicate dmy  *	thndware qlags

 *
 * @Tiefrefauleoode (ill bb c"autoatioc",tiech anl0211 /fg80211_ *	qdfine _to pe cdnamic_cSMS:mnfD(reglar )tuwersave ,ind nSMS: *
@hrned
of fcnher wis 
 *
 * @Toatpporteroas ileaure ,the driver tast bst thieblproterite  *	qhndware qhsportedfags
 snd nardle thaecSMS:mrag.io,coiefanfigu() * speration.
IItwill ahin aith thes cmeoangismie snsteuct rdmo 
*	@ener the Requisted rSMS:mode (hile ussociated Ao asnOHTRAP
 * 
e/**
 * eDOC IFamesiilteritgs
*	
* @ac80211 wequires Eh aee @anayRanagement frames bor aropera * speration.,osd msedrsiay bhat:oo aee @anayRaoe uremes when 
*	tn tonitor iode .@Hwerer  tor aeat bCPUtusgemfrd nawersson
sumteon , *	qhneng.oa ilewrrames wnsRpwsiolesberacolte aherugh the bartk fns *	qdfsietles.@Hnte ,the Rardware fhould rilterima aush rcsRpwsioles
 *
 * @Toaachieveohans,tac80211 wsed tilterimlags -(ee @eeeow)fo m el
 *	uhe driver '_ onfigured_ilteri()trnction tiech arames weould be 
*	tpsseddio tac80211 wn  siech aeould be silterid
ofut. *  * TBafre fanfigured_ilteri()ts rntvokd ,Rha bprearae_ulticast () * sallbbnk fn rntvokd swih the baragmeer
  @mc_hnnt:fsd m@icsaste
* Tlr toiefanmbnsd (ulticast bddress slis aofcllbrairuallinterface s. * sIt'stsedci sptionaa ,ind nt
deqhrne a Qu64Rhat aisrpsseddio  *	 onfigured_ilteri(). Addiionatlby,tanfigured_ilteri()tas tohe *	 srguent  t@aanged
clags - el
ng twech arags -w rtihanged
bsd  * T@ ota clags -ith the bew frag.iette
s. *
 * sIftou rcevice_ihnsrno(ulticast bddress silteristou rceiver thll 
* seed to poackiiboh the b%FIF_ALLMULTIqlagscsd mhe dImc_hnnt:
*	tpsagmeer
ohotee @hether tulticast rrames weould be sacesped  *	dr tritepd . *
 * sAlbrpneuported ylags -n t@ ota clags -ust be aciesrd 
 * @Hrdware soesnaotiphsportedailag is it
ds unclnabil ioft_psseng _ *	che bfamesso uhe bartk 
 Oher wis [ha vriver tast bgnmre  *	che bfag ,but cot taiesrun . * IYu cast b_nly _taiesruhe bfag S(snotuce onoopuportedfr ehe 
*	qlagsco tac80211 )of tou nre soti alesbeot ssoshe barket eope  *
 o the bartk f(soThe Rardware frways bilteristit)
 *
@hm(or
taxaplie,tou aeould baiesru@FIF_CONTROL,of tou  tavdware  *	 sways bilteristontrol  oames 
sIftou rcardware frways bpsseds *	 onfrol  oames  o the bkerel/@nd nt unclnabil ioftilteritgs@he m, *	qou ad t_oti_taiesruhe b@FIF_CONTROLqlags
 * @Tinsreue.blprlid to asl bnher tFIFflags ar -w ll
 * 
e/**
 * eDOC IAPasuportedfr eowerseveng tclint s *  * TI tordr yo umplieent oAPard nS2PmGOiode s,tac80211 whrsRsuportedfr  *	 olint fowerseveng ,iboh t"legacy"cPSt(PS-Poll/nulbcdta )osd msAPSD
 *
@Te e crrrently ci_ otasuportedfr esAPSD
 *
 *
@Te e ci an  ussoumteon  hat tac80211 tacke , amileyohat ta olint 
*	swll bot tpolbrilh tPS-Pollcsd mhieger tith tuAPSD n the hsmessoies
 *
 Boh tre qhsported.,ind nboh tcn be used
 y dhe hsmessolint ,but  *	che ytcn t
ne csed
 onfrrrently cy dhe hsmessolint .This ssnmlitiess * Tra vriver tcde . *
 * @Tieffist  thng to teepstn toid nt uhan the e ci aailag ior tonpliet  *	qdiver tsplieent tion :T%EEE80211_THW_AP_LINKLS:.sIfthes ilag is set.,
* @ac80211 wexoeti the driver to pardle tmot fofche dataeefac8hne Ifr  *	 owerseveng tclint sscd mill bnnmre fhe dP bet csnticlomng arames 
 *  Divers thin aue teee80211_bstaps _sansmiiona()fo mn orma@ac80211 wf  *	wprticn
 'tuwersave mhansmiiona 
Intthansrode ,sac80211 wnws doesn't  * pardle tPS-Poll/uAPSD
 *
 *
@nttha Rode (ilh ut w%EEE80211_THW_AP_LINKLS:,mac80211 rill boackiihe 
*	qP bet csnticlomng arames ior tolint fowerseve mhansmiiona 
IWen sa *	wprticn
 gos Eh aeeep ,awethll wpropshansmitteng wo un .@Te e ci 
 * ihwerer  t fate tarfdiiona:a Qprticn
smightrgoto sleep (hile uhe e ci  *	qdta fufferad (n/Lardware fueue__.sIfthe Revice_ihnsrssportedfr ehes  *
@i bwll beqjcte(rames ,tsd mhe driver thould rgve She arames weak fo 
* @ac80211 wwih the b%EEE80211_TTX_STATTTX_ILTEREEDfrag.ieetThech awll  * salusemac80211 ro ure iyroe bfamesshen the
Qprticn
swcke uup mTi  * sdiversqs anws dotified:toftowerseve mhansmiiona wbytollbng.wnt  * @Iata_otifiysallbbnk . *  * TWen shieftrticn
ss a seep ,ai has tohre @haoce  :aitran uwakeppp, *
@i bcantPS-Poll,tr ai bcantpwsiole  stat_a QuAPSD serice turiod:
 *  Waing.Lupit unplieent d
 y dsmpli thansmitteng wal
abfferad ((sd  * Tilterid
) oames  o the bs tion 
Ihis is ahe Reasiat base.
IWen 
*	thieftrticn
ssesd aaiPS-Pollcr al sAPSDtrieger  oames,tac80211  *	cill bnforma@he driver tofthis twih the b@ssowe_euferaed_ramess * Tallbbnk ; hansrullbbnk fn rptionaa .mac80211 rill bten shansmitt *	che bfames a suseallind nht thieb%EEE80211_TTX_CTL_NO_PS_BUFFER *	drjtach rrames
@Tieflst rramescinrhe Bserice turiod:t(r toiefnly) *  ecponsseto kddPS-Poll)anws das t%EEE80211_TTX_STATUS_EOSPctt io 
*	tn icate that aitresd ahe Bserice turiod:;ss tohs rramescmst bhfe 
*	tX status bagorteditalso uee  t%EEE80211_TTX_CTL_REQTTX_STATUS. * TWen sX status bn bagortes
wlr this  oames,the Bserice turiod:ts  *
@markd sas thneng.oncdd and nlbew fne ion be ustat_d
 y dhe Rper . *
 * sAddiionatlby,tnon-euferabil iMPDU
 san blws de
qhansmit ed Ay  *	tac80211 wwih the b%EEE80211_TTX_CTL_NO_PS_BUFFERcwn cnjthe m
 *
 * sAnnher tate tarfdiiona on bhnppenunlssme vreice  tike iiwlwifi
*	swen nte
r are,4fames aueue_ddfr ehe dsttion fnd nitswcke uup *	dr tpolbs;che bfames ahat tae frwaad yuueue_ddculd bcn (p teang.
*	thansmit ed Afist  nftead ,salusng.oreordr ng.fnd /r awrog.
*	tproessang.arfthe REOSP mTiefalsedci shat talaw
ng toames  o te 
*	thansmit ed Ao kddcerainsftrticn
ss aut -of-bnd nommauntcbionaaro * Tra vreice .@Toaasowedhis  arobieeto pe cwolvd ,Rha briver fan  * Tallbteee80211_bstapeock _awake()of tfames a r
sufferad (win at 
*	tnsdotified:thin the hsrticn
sw t:oo aeeep 
IWen sal btenseLramess * Thvee baensilterid
o(ee @llue ),tn wmst boll tehetrnction taganf * so,cndicate that ahieftrticn
ss an dloner  eock d . *
 * sIfthe Reiver iyfferastfames ainrhe Beiver ior algregateona ndany  *	twy ,wn wmst bsedche beee80211_bstapst _euferaed()tallbriin at ss  *
@otified:tofthieftrticn
sgong wo uleep (o mn orma@ac80211 wf any  *	tTIDschat thfv arames weuferaed.Note:that tiin aaQprticn
swcke uup * Traisrfformation
qs aeqpt a(hece ohe wequiresm t:oo aallbtetwhen 
*	tn ormed
of thieftrticn
sgong wo uleep ) mTi n,tiin aaQperice 
*	@eriod:ustat_ lor an )weqasn/,a@eqlese._euferaed_ramessinsrullbd  *	cilh the bember of soames  o te  eqlese.dwn  siech aTIDscha ytaed
*	fo mcme vfrom
Intthansrulse,Rha briver fs aeqponssile. or asettng 
*	@he REOSP (or
tuAPSD)on  sMORE_DATA its oi whe haglese.dwrames ,
*	fo mhelpmhe dImre _dta fasagmeer
on sasseddio thellthe driver tif
*	@he e ci aore  eta fn/brher tTIDsc--the dTIDschohaglese.crames 
*	from tre qnnmre d@ince oac80211 woesn't e
owlihwe@anayRrames bhe 
*	fyfferastfr eheosewTIDscontains. *
 * sIfthe Reiver ilso unplieent oGOiode ,iwere'cabsece ouriod:s oa  *	tsort n sterice turiod:st(r tllutedPS-Pollcecponsses),wn wmst  * Tilterieheosewecponssetrames bexcetioi whe hulsemf soames  oat  *	 sr
sufferad (inrhe Beiver i--theosewmst besminsfufferad (o kdoid  *  ecordr ng.. Bealsedci aisrpwsiolesbeat anntfames a r
saglese.d
*	tn thansrulse,Rha briver fmst boll teee80211_bstapeosp() * so,cndicate th tac80211 what ahiefterice turiod:tncdd andyway. *  * TFiatlby,tf tfames arom tmlticle.wTIDsc r
saglese.darom tmc80211  *	cbu ahe driver tmightrreordr nhe m,tn wmst boiesru&nht thieblags
 *
blproterite i t(nly utieflst rramescay bhfv a%EEE80211_TTX_STATUS_EOSP) * snd nlso btak tareetf she REOSP n  sMORE_DATA its oi whe hrames
 *
ITe weiver tay blso bue teee80211_bstapeosp()tn thansrulse. *  * Tote that tnfthe Reiver ieer iyfferastfames arher that oQoS-ata  * (rames ,tn wmst btak tareeto doeer iacn (atnon-QoS-ata rramescas * Tra vlst rramescinrafterice turiod:, ddrog c fQoS-nulbata rrames * snter tatnon-QoS-ata rramescnfteed d 
 */
s/**
 * eDOC IHWqueue_ ontrol  *	 * TBafre fHWqueue_ ontrol cas ritrolduc  ,tac80211 wnly uhadacsingle ttrtic, * snsigneent ooftor-rnterface tACMpoftare cueue.spo pavdware fueue__.sTes  *
@as rarobieetic,ior al lewreqasn/s  *	 1)fpff-wannel/@hansmission ssmightrgettetukiibehid onher crames 
*	f2)tmlticle.wairuallinterface sdculd 't
ne pardle dtonrect y) *  3)snter -TIM bfames aculd bgettetukiibehid onher crames 
*	
* @Toatolvdohans,thndware qhypoclly used tmlticle.wdiferant bueue__ or all
 *	uhe driferant busgem ,tsd mheistueds to de croppagted Ante tac80211 wsoti  *
@ao't eave toe hsmessarobieetith the baoftare cueue.s
 *
 *
@Te e fre ,tac80211 wowlioferasthe b%EEE80211_THW_QUEU_CCONTROLiapabilitiy
*	qlagscoat ahellscitthat ahe driver tiplieent  bto ofwncueue_ ontrol .@Toado * Tso,Rha briver fhll wpettp toe rvariouspueue__ njtach ritruct ieee80211_cvif * snd noiefnffwannel/@ueue_ i oitruct ieee80211_chw. Inwecponsse,mac80211 rill  * ssedcheosewueue_ ID ai whe hawqueue_Tilel
of titruct ieee80211_ct_snsorcsd  * Tnfteeessary.fhll wueue_Toe bfamessn whe haightraoftare cueue. hat tairrors * Tra vavdware fueue_. * sAddiionatlby,tha briver fas toothin aue ttenseLTWqueue_ IDsdfr ehe dueue_ *	Ranagement frnction  e(iee80211_bstopqueue_()te tal
) *
 * @Te weiver ts  oaee-t uet tp toe rueue.fmfplng. a sueed d ,tmlticle.wairuall
*	tn erface sday bappoo the bsam vavdware fueue_scnfteed d 
TTe bactp tas too * ThvppenuBring tndd_nterface tr toanged_iterface iaplbbnk s
IFr
taxaplie,Ma * sdiversqssporteng.oprticn
+sttion fnd nprticn
+AP ode)ssmightrdecide o pave  *	 10vavdware fueue_sco pardle triferant bscenarios  *	 *	 4tACMTWqueue_sdfr e1s mvif: 0, 1, 2, 3 *	 4tACMTWqueue_sdfr e2d nvif: 4, 5, 6, 7 * snter -TIM bueue_Tir
t-P:   8 *	drff-wannel/@ueue_ HHHHHHHHH9 *
 * sItfhuld nhin aet tp toe ravdware fike ioeis  *	  haw.offwannel/_yx_hwqueue_ =H9 *
 * snd noieffist  airuallinterface  hat aisraddd
oa ilosowes  *	  hvif
hwqueue_[EEE80211_TAC_VO] =H0 *	  hvif
hwqueue_[EEE80211_TAC_VI] =H1 *	  hvif
hwqueue_[EEE80211_TAC_BE] =H2 *	  hvif
hwqueue_[EEE80211_TAC_BK] =H3 *	  hvif
cabqueue_ =H8 //cnftAP ode),cnher wis [%EEE80211_TINVALTHW_QUEU_ * snd noiefsearfd airuallinterface  ith t4-7. *
 * sIftueue_ 6age  tfulb,(or
taxaplie,tac80211 riuld nnly ueropshiefsearfd * sairuallinterface 's BEtueue_ ince oairuallinterface  ueue_sdre srrfbAC. *  * Tote that toe rvif
cabqueue_ alue oeould be stt io m%EEE80211_TINVALTHW_QUEU_ * swen eer toiefueue_ isseo eue dd(i.e.ihe beterface  isseo einrAP ode))tnfthe  * sueue_ onld a otnt igly ue sshae d@ince oac80211 whll wlookSn tcabqueue_ hen 
*	tafueue_ isseropped/iok niven iifnhe beterface  isseo einrAP ode)
 */
s/**
 * enum meee80211_ciltericlags - phndware qilterimlags  *
 * @Te seblags
teter isn_ het toe rilterimi/Lardware feould be 
*	tprogamemd (o klt thirugh tn  sieatwhould not be crsseddio tha  * strtk 
 I aisraways bsafsbeot ssosaoe uremes what oequisted , *	cbu ahes rarsueeateoe mmplat ifnapwersson
sumteon   *
 * @@FIF_PROMISCTNFTBSS: rotmiscuouspode (ilh i/Lou rcBSS  *	aheinktf she RSS
 astou rcnetworksttgent fnd noie thansrurrecponsd 
*	bo dhe hagglar  ther ne bevice srotmiscuouspode   *
 * @@FIF_ALLMULTI:t ssosll tulticast rrames ,Traisrfsfsed
 ifnequisted 
*	by dhe Rsedrwr aifthe Rardware fisseo elnabil ioftilteritgs@y  *		ulticast bddress   *
 * @@FIF_FCSFAIL:t ssosremes whth tfaild
 FCS (bu aou aeed to pet thie
*		%RX_FLAG_FAILED_FCS_CRCdfr ehe m) *
 * @@FIF_PLCPFAIL:t ssosremes whth tfaild
 PLCP CRCd(bu aou aeed to pet 
*	ahe d%RX_FLAG_FAILED_PLCP_CRCdfr ehe m *
 * @@FIF_BCN_PRBRESP_PROMISC:shis ilag is set.tBring tpanning co undicate 
*	proThe Rardware fhat aitrhould not bilterimeaconsstr aropbewecponsses
*	by dSS
ID
IFlteritgs@he m on bgeatoy uagduc hhe Ramnnt:foftoroessang. *		uc80211 woeds to ddosnd noiefamnnt:foftCPUtwakeups,wsotou aeould  *		honore hes ilag isfRpwsioles
 *
 * @@FIF_CONTROL:t ssosonfrol  oames  (excetiofr ePS Poll),tf tPROMISCTNFTBSS * @	s aot bet thienunly uheosewddress d (t thas rs tion 
 *
 * @@FIF_OTHERTBSS: rssosremes wddsiogd (t tnher cBSSs 
*	
* @@FIF_PSPOLL:t ssosPS Poll(rames ,tn tPROMISCTNFTBSS s aot bet thienunly  * @	heosewddress d (t thas rs tion 
 *
 * @@FIF_PROBE_REQ:t ssosropbewecueste-rames 
*	
enum meee80211_ciltericlags -{
	FIF_PROMISCTNFTBSS= B<<0,,	IFIF_ALLMULTI	= B<<01,	IFIF_FCSFAIL	= B<<02,	IFIF_PLCPFAIL	= B<<03,	IFIF_BCN_PRBRESP_PROMISC= B<<04,	IFIF_CONTROL	= B<<05,	IFIF_OTHERTBSS	= B<<06,	IFIF_PSPOLL	= B<<07,	IFIF_PROBE_REQ	= B<<08,	;

/**
 * snum meee80211_capldu_mlme_aceon/L-t-MPDU
taceon/  *
 * @Te seblags
tar ssed thth the bapldu_aceon/()tallbbnk fns * @&truct ieee80211_copspo,cndicate tiech activnaMs ueed d 
 */ * Tote that toivers tMUSTbe salesbeotdetlthth tasX slgregateona * stesion weang.seroppediven ietfre fha ytOK'e:ustat_ng.wntAy  *	tallbng.wnee80211_cstat__yx_ba_cb_irqsafs,ietalsedche Rper  *	Raightrreeive She acddBArramescad nhtn (atdelBAraightraays! *
 * @@EEE80211_TAPDU_NRX_START: stat_aR slgregateona * s@EEE80211_TAPDU_NRX_STOP:ueropsR slgregateona * s@EEE80211_TAPDU_NTX_START: stat_aX slgregateona * s@EEE80211_TAPDU_NTX_OPERATIONAL:IT slgregateonatas teecme vperation.ll
*	t@EEE80211_TAPDU_NTX_STOP_CONT:ueropsT slgregateonatbt wrot inuethansmitteng 
* 	ueue.dfaskets ,wowliuncgregatee 
TAter talbruskets tne qhansmit ed Ahie
*		river fas tootoll teee80211_bstop_yx_ba_cb_irqsafs()
 *
t@EEE80211_TAPDU_NTX_STOP_FLUSH:ueropsT slgregateonatad nflushtalbruskets   *	aullbd then the
Qprticn
ss aeqmovdd.@Te e ' an deed tr arqasn/to poal
 *		eee80211_bstop_yx_ba_cb_irqsafs()tn thansrulsema auc80211 wnsoums bhe 
*		tesion wn sgn  usd nagmovdaihe bs tion 
 *
t@EEE80211_TAPDU_NTX_STOP_FLUSH_CONT:uullbd then tT slgregateonatisseropped
*	byu ahe driver tas 't eullbd teee80211_bstop_yx_ba_cb_irqsafs()tyettsd  * 	nwedhi_ ontnetivnaMs uritepd snd noiefsrticn
swll be sremovdd.@Divers 
*		tould baiesn upind nritebesminsng tuskets twie thansrnsrullbd .
*	
enum meee80211_capldu_mlme_aceon/L{	IEEE80211_TAPDU_NRX_START,	IEEE80211_TAPDU_NRX_STOP,	IEEE80211_TAPDU_NTX_START,	IEEE80211_TAPDU_NTX_STOP_CONT,	IEEE80211_TAPDU_NTX_STOP_FLUSH,	IEEE80211_TAPDU_NTX_STOP_FLUSH_CONT,	IEEE80211_TAPDU_NTX_OPERATIONAL,	;

/**
 * snum meee80211_crames_eqlese._ope * pream
saglese.arqasn/ *
t@EEE80211_TFRAME_RELEASE_PSPOLL:tream
saglese.dofr ePS-Poll *
t@EEE80211_TFRAME_RELEASE_UAPSD:tream
(s)saglese.doduetho * 	ream
sageive dcn whaeger -ncbled BAC
*	
enum meee80211_ciames_eqlese._ope *{	IEEE80211_TFRAME_RELEASE_PSPOLL,	IEEE80211_TFRAME_RELEASE_UAPSD,	;

/**
 * snum meee80211_cates_ontrol _hanged
b-blags
to,cndicate tiet toangedd *
 * @@EEE80211_TRC_BW_HANGGED:ITe wbnd widh theattcn be used
 o m insmit 
*	proThes rs tion taanged
.ITe waceallibnd widh ti ai whe hs tion  *		eformation
q--ofr eHT20/40 hintEEE80211_THT_CAP_IUP_WIDTH_20_40 * 	rag ioangeds,ofr eHTind nVT Mte wbnd widh tilel
ooangeds
 *
t@EEE80211_TRC_SMS:_HANGGED:ITe wSMS:mataeeff thieftrticn
saanged
. * t@EEE80211_TRC_SUPP_ATES	_HANGGED:ITe weuported yagt Oet tofthis tper  *		hanged
b(inMIBSS ode))tduethoadiscovditgs@aoe unformation
rnlut 
*	ahe dper . *
t@EEE80211_TRC_NS	_HANGGED:IN_SS (umber of ssptiol ctreaams)sas roangedd *
	y dhe Rper 
*	
enum meee80211_cates_ontrol _hanged
b{	IEEE80211_TRC_BW_HANGGED	= BBIT(0),	IEEE80211_TRC_SMS:_HANGGED= BBIT(1),	IEEE80211_TRC_SUPP_ATES	_HANGGED= BBIT(2),	IEEE80211_TRC_NS	_HANGGED= BBIT(3),	;

/**
 * snum meee80211_caoc_ope * pesminsfn
saangel/@hpe  *
 * sWth the basportedfr eulticsaangel/@ontexx sscd multicsaangel/@peration. , *	cesminsfn
saangel/@peration. Raightrb fikit ed/deeraagd/lluted
 y dnher 
*	qlaows/peration. Riech aave thighera ivrithy((sd  vis [ers a)
 *
@heciafyog chaehROC@hpe tcn be used
 y dreice  thoa ivrithzeoehaehROC *	dreration. Ronpltrd (t tnher creration. /laows. *
 * @@EEE80211_TROC_TYPE_NORMAL:ITe
r are,4otaseciaa bequiredent  blr this  ROC. *
t@EEE80211_TROC_TYPE_MGMTTTX:ITe wesminsfn
saangel/@ecueste-s aequired. * 	or asesdng tanageent frames boffwannel/.
*	
enum meee80211_caoc_ope *{	IEEE80211_TROC_TYPE_NORMAL =H0,	IEEE80211_TROC_TYPE_MGMTTTX,	;

/**
 * struct ieee80211_bopsp-iaplbbnk sarom tmc80211 oo the briver  *
 * @Tei ctructure kontainssrvariouspaplbbnk sahat ahe driver toa  *	tardle tor,ons sme vrse.s,tast bhfdle ,(or
taxaplie o pontigured
* Tra vavdware fo kddew faangel/@pr o m insmit t rrames. *
 * @@tx IHrdle rbhat a8 .11-Sodeue.baplb nor
tach rhansmit ed Afames. *
	skb*ontainssrte wbufera itat_ng.wrom thessIEE8s82.11-fheadr 
 *	Thi fiow-lvenldriver thould racn (he dfamessnutRbasedon  *		hrfiguretion fi whe hX sontrol aata .@Tes rardle rbhould   *	apaeerabley,tneer tfailfnd nprtebueue_sdrproterite i 
 *	TMst be artomic. *
 * @@itat_: Cllbd tetfre fha ffist  netevice sattch d aroThe Rardware  *		e sncbled .This ihould rhrne n whe hardware frd nmst btrne n  * 	ream
sageiteon  (fr eowsiole  ncbled Benitor interface s.) *
	Rqhrne aeeateoe merrortcde s,the
s iay be ssed inttsedrpace   *	aortzero
 *	TWen shiefevice_insrstat_d
 itrhould not bave tabMACaddress 
*	proTdoid ack owled gng toames  etfre fatnon-enitor ievice_ *		israddd

 *	TMst be anplieent d
 nd nopnpeeep 
 *
 * @@itop: Cllbd tnter tlst rnetevice sattch d aroThe Rardware  *		e sdisbled .This ihould rhrne nffthe Rardware f(t alese  * 	ntnmst btrne nff ream
sageiteon .) *
	My be sollbd taightrater tadd_nterface tifthet saqjcte 
*	paninterface 
sIftou raddd
oan uwork n roThe Rac80211 riurkueue_ *		ou aeould beneredao poancebtetwo thansrullbbnk . * TMst be anplieent d
 nd nopnpeeep 
 *
 * @@iuspesd: Suspesdshiefevice_;fac80211 ws elf.fhll wueiesc qetfre fsd  * 	propshansmitteng wnd nrong tny
rnher torfiguretion ,tsd mhe   * 	pskshiefevice_io peuspesd
Ihis is anly untvokd swen sWoWLANss  *
	hrfigured ,tnher wis [hiefevice_insrdeonfigured aonpliet y uad  * 	reonfigured at saqoumssoies
 *
	Te weiver tay blso bnplosewseciaa barfdiionas undr tiech an  * 	hat: thoasedche R"nrmatl"peuspesd (deonfigured), sa tf it
dnly) * 	spportes WoWLANswen shiefevice_insrssociated 
Intthansrulse,Rn  * 	mst bestrne 1 fom thens rnction . *  * T@aqoums:sIftWoWLANswa_ onfigured ,Traisrfdicate sahat aac80211 ws  * 	nwedaqoumng.wnt speration.,oster theisshiefevice_iust be afulb) * 	rnction llinganf.sIfthes ieqhrne a nmerror,tha bnly uwaycnutRi 
*	proTdso bunregstedrshiefevice_.sIftt
deqhrne a1,thienumc80211  *		ill alws dgotoerugh the bagglar  onpliet daqotat_aonsaqoums
 *
 * @@iet_wakeup: Ecble Lr adisrilesaakeupiwen sWoWLANshrfiguretion fi 
*	padefiid
.ITe wrqasn/ti shat tevice__iet_wakeup_ncbled()ts  * 	spporsd to pe cullbd then the
Qhrfiguretion foangeds,oot bnly) * 	ns suspesd()
 *
 * @@add_nterface : Cllbd tiin aaQnetevice sattch d aroThe Rardware ts  * 	ncbled .TBealsedci aisreo elnled dfr eonitor iode dreice  ,@@itat_
*	pand@@itopiust be anplieent d

 *
	Te weiver thould a rfarma@ny
rnitioaliztion fitwoeds tetfre 
*	ahe device scn be uncbled .ThiernitioalQhrfiguretion ffr ehe 
*		eterface  issivesnoi whe hunf tasagmeer

 *
	Te wallbbnk fay baeeuseto kdd
 ndbeterface  bydeqhrneng ta *		eeateoe merrortcde w(hech awll be ssed inttsedrpace .) *
	Mst be anplieent d
 nd nopnpeeep 
 *
 * @@oanged_iterface : Cllbd tiin aaQnetevice shangedsShpe .This iallbbnk  *		e sptionaa ,ibt wnly us it
ds ueuported ycndbeterface  hpe sbe 
*		swith d ahile uhe beterface  issUP mTiefallbbnk fay beeep 
 *
	ote:that tiile usnbeterface  isseang.sewith d ,tn will bot ae 
*		foud wy dhe Reterface  itrateon daplbbnk s
 *  * T@aqmovd_iterface : NtifiedsSadriver toat tanbeterface  issgong wdown
 *
	Te w@itopiullbbnk fn rcllbd tnter traisrf it
ds ura vlst reterface 
*	pand@no(unitor interface sdre sreqptt . * 	Wen sal bnterface sdre sremovdd,tha bMACaddress si whe hardware  * 	mst be aciesrd  soThe Revice sn dloner  ck owled gesruskets   *	ahe Rac8_ddre meber of she hunf ttructure kns,thwerer  tst tootoie *
	MACaddress softhe Revice_igong waway. * 	Hnte ,thensrullbbnk fust be anplieent d

 I aopnpeeep 
 *
 * @@orfigu IHrdle rbor tonfiguretion fecueste_.sIEE8s82.11-fcde waplb nhes  *
	rnction to poangedtardware carfiguretion , .g. , wannel/.
*		Tens rnction rhould noeer tfailfbut[aqhrne a Qeeateoe merrortcde  *		e it
doesn mTiefallbbnk fopnpeeep 
 *
 * @@bsssnsor_hanged
 IHrdle rbor tonfiguretion fecueste_saglted Ao aBSS * 	aragmeer
  hat aacyrvarytBring tBSS' tikfepac ,tsd may blferc wlow * 	lvenldriver t(.g. Essoci/disrsocistatus , .rpbaragmeer
 ).
*		Tens rnction rhould noo
ne csed
 nfte aBSStas teen cet., unevs, * 	or associateon/Lfdicatein  mTief@aanged
soragmeer
onnicate sahech  *	aof he wesosrragmeer
  hs roangeddtiin aaQoll te auce .@Te rallbbnk  *		opnpeeep 
 *
 * @@prearae_ulticast : Prearaeior aulticast rilterimonfiguretion .
*		Tens ullbbnk fn rptionaa ,ind nt
 ieqhrne alue on sassedd
*	proTonfigured_ilteri(). Tensrullbbnk fust be artomic. *
 * @@onfigured_ilteri: Cnfiguredthe Revice_' tRXrilteri.
*		Se @he naction i"Famesiilteritgs"dfr eone unformation
.
*		Tens ullbbnk fust be anplieent d
 nd nopnpeeep 
 *
 * @@it _tim: St tIM bet .mac80211 raplb nhes trnction tien aaQIM bet  * @	mst be aet toraciesrd  or al ivesn STA. Mst be artomic. *
 * @@ictbkey: St @he naction i"Hrdware saypto,taceslrateon "
*		Tens ullbbnk fn rply uhllbd tettwen cadd_nterface tad  * 	removd_iterface raplb ,ii.e.ihele uhe bivesn airuallinterface  *		e sncbled . *
	Rqhrne a Qeeateoe merrortcde aifthe Rk ytcn t
ne caddd

 *	TTiefallbbnk fopnpeeep 
 *
 * @@pdate _tkip_key: St @he naction i"Hrdware saypto,taceslrateon "
*	 	Tens ullbbnk fill bb collbd te whe hunfexx softRx. Cllbd tlr trivers 
*	 	iech aet tEEE80211_TKEY_FLAG_IIP _REQTRX_P_TKEY
 *
	Te wallbbnk fast be artomic. *
 * @@ictbrekey_dta :sIfthe Revice_ispportes GTKsrekeydng,(or
taxaplie hele uhe 
*		hos
ds ueuspesdd ,tn wan blsignethensrullbbnk fo ure iieveoha  eta  *		eeessary.fo ddosGTKsrekeydng,(hansrnsrha  KEK, KCKusd nagplaytomuneri.
*		Ater trekeydngswa_ dn  uitrhould n(or
taxaplie Bring taqoums)dotifiy
*		sedrpace softhe Rnewreqplaytomuneri usng.oeee80211_cgtkbrekey_otifiy()
 *
 * @@actbrefaule_untcbst_key: St the Revfauleo(untcbst)qkeytfdex ,tsedfulIfr  *		WEPqwen shiefevice_isesd adta faskets tautonomousey,t.g. Eir
t-RP *	aoffoad ng.. hierniex icn be u0-3,tr a-1Eir
tunsettng un . *  * TIhwqpann: Askshiefavdware fo kterice the nact oequiste,an deed to kttat_
*	phe nact oataeefac8hne InsOirtk 
TTe bact oast bhonore hee wannel/ *		hrfiguretion fdn  uy dhe Ragglaror  blgenioi whe hwiph 's * 	regstedrd terdls Thesrhndware q(r toiefrivers)woeds to dmak tered
*	pheattpwersseve mn sdisbled . *
	Te w@equoee/ie_lenumeber sdre srewrt ednay dac80211 wroTonfainsfhe 
*		nt ireiIE enter the RS
ID,is that soivers teed tot alookSn the s 
*	patwal
abftsjst bacn (he menter the RS
IDq--oac80211 wscludeesrhe 
*		(extendd
) euported yagt sscd mHTunformation
r(were'caprlicbled)
 *	TWen shiefact ofnitshs ,tnee80211_bsct _onpliet d()tmst be acllbd ; * 	nwe that aitrlws dmst be acllbd then the
Qpan bopnot afnitshoduetho * 	ny
rerrortunevs, xensrullbbnk feqhrneeda Qeeateoe merrortcde 
 *	TTiefallbbnk fopnpeeep 
 *
 * @@oanceb_hwqpann: Askshiefiow-lvenldtppoancebtte waceoe mhwQpan 
 *
	Te weiver thould aaskshiefavdware fo koancebtte wpan b(sfRpwsioles)  *	ayu ahe dpan bill bb conpliet dwnly uater the Rriver fhll woal
 *		eee80211_bsct _onpliet d().
*		Tens ullbbnk fn reed d  or awwlec ,thoa iven t enueue_og c fnew
*		sct _work ater the Riow-lvenldriver twa_ rwaad yueuspesdd 
 *	TTiefallbbnk fopnpeeep 
 *
 * @@sh d bsct _itat_: Askshiefavdware fo kttat_apanning ceqpatoedy ua_
*	pieciafc,interfalus TITe weiver tast boll tehe *		eee80211_bsc d bsct _aqoults()trnction tien eer ti bfisd uaqoults.
*		Tens oroessafhll woot inuetptil ash d bsct _itoprnsrullbd .
*	 * @@sh d bsct _itop: Tellthe davdware fo kttopindwnlgong wsh d ubd tpan 
 *
	ntthansrulse,Rnee80211_bsc d bsct _eropped()tmst boo
ne cullbd .
*	 * @@swbsct _itat_: Ntifiedrtrnction that aisrcllbd tjst be fre fatsoftare csan  * 	nsrstat_d
. Cl be uUMLL,tnfthe Reiver ioesn't eeed tohnsdotifiection
.
*		Teefallbbnk fopnpeeep 
 *
 * @@swbsct _onpliet : Ntifiedrtrnction that aisrcllbd tjst bater ta
*	pioftare csan ofnitshs
. Cl be uUMLL,tnfthe Reiver ioesn't eeed 
*	phensdotifiection
.
*		Teefallbbnk fopnpeeep 
 *
 * @@gctbataes: RqhrneRiow-lvenldtrticsicas
 *
t	Rqhrne azerotnfttrticsicastae frvailales
 *
	Teefallbbnk fopnpeeep 
 *
 * @@gctbtkip_seq:sIftou rcevice_iiplieent  bIIP cacrypteon fi/Lardware fhes  *
	allbbnk feould be sropvid dno ureadshiefIIP c insmit tIVs (boh tIV32
*	pand@IV16)dfr ehe divesn keytfom tardware 
 *
	Te wallbbnk fast be artomic. *
 * @@ictbfrag_hieesolds: Cnfiguretion
rf soamgent tion fhieesolds. Asignethens *		e ihiefevice_ioesnaoamgent tion fbyit elf.;tnfthens ullbbnk fn  *		eplieent d
 ten the
Qprtk fill bot tdoaoamgent tion .
*		Teefallbbnk fopnpeeep 
 *
 * @@sctbrts_hieesolds: Cnfiguretion
rf sRTSfhieesoldsb(sfRevice sneds tit)
*		Teefallbbnk fopnpeeep 
 *
 * @@sta_ddr: NtifiedsSowedlvenldriver tllut waddiionaaf anyussociated Atrticn
  *	aAP,MIBSS/WDS/mesoRper betc. Tensrullbbnk fopnpeeep 
 *
 * @@sta_removd: NtifiedsSowedlvenldriver tllut wremovllif anyussociated 
*	pirticn
  AP,MIBSS/WDS/mesoRper betc. ote:that tater the Rallbbnk  *		aqhrne at
ds 't esafsbeotsedche Rponterf,oot bven iRCU retectid ; * 	nwiRCU gate turiod:ts  guainsted tettwen ceqhrneng tere'can (raeang.
*		te bs tion 
 St @@sta_ iv_rcu_removdcnfteed d 
 */	Tensrullbbnk fopnpeeep 
 *
 * @@sta_add_debugfs: Divers topnpsedchensrullbbnk fo udd
 debugfsiiltes
*	biin aaQprticn
sisraddd
oe tac80211 ' rs tion tlis .This iallbbnk  *		and@@ita_removd_debugfsfeould be silh i/La CONFIG_MAC0211_bDEBUGFS *		hrfdiionatl. Tensrullbbnk fopnpeeep 
 *
 * @@sta_removd_debugfs: RemovdchiefevbugfsiiltesThech awre'caddd
ouang. *		@sta_add_debugfs. Tensrullbbnk fopnpeeep 
 *
 * @@sta_otifiy: NtifiedsSowedlvenldriver tllut wpwerssetaeefhansmiionaif any *		asociated Atrticn
  AP,MMIBSS/WDS/mesoRper betc. Fr al VIFsperatiog. *		inrAP ode),thensrullbbnk fill bot ae acllbd then the
Qrag  *		%EEE80211_THW_AP_LINKLS:fs ctt 
IMst be artomic. *
 * @@itabataed: NtifiedsSowedlvenldriver tllut wetaeefhansmiionaif an
*	pirticn
w(hech acn be uhe
QAP,Masolint ,bIBSS/WDS/mesoRper betc.)
*		Tens ullbbnk fn rmutally uexludsoe mhth t@sta_add/@sta_removd
 *
	nttmst boo
nfailflr trfwnchansmiiona wbu aacyrfailflr thansmiiona 
*		spshieflis aofchtte
s. Aws doti:that tater the Rallbbnk  aqhrne at
 * 	ns't esafsbeotsedche Rponterf,oot bven iRCU retectid  - nwiRCU gate  * 	ariod:ts  guainsted tettwen ceqhrneng tere'can (raeang. te bs tion 

*		Se @@sta_ iv_rcu_removdcnfteed d 
 */	Teefallbbnk fopnpeeep 
 *
 * @@sta_ iv_rcu_removd: Ntifiydriver tllut wetaeon fecmovllie fre fRCU
*	piynchroitstion 
Ihis is asedfulIifSadriver toeds to dave ts tion  *		ponterfs retectid  usng.oRCU,tn wan btin aue ttens ullbfo koiesr
*		te bponterfs nftead aofcwaittgsQrr
taniRCU gate turiod:to pelaps 
*		et@@itabataed
 */	Teefallbbnk fopnpeeep 
 *
 * @@sta_rc_pdate : NtifiedsShe Reiver iofcoangedsSroThe Rbitrte sahat acn be 
*		sed
 o m insmit  o the bs tion 
Ihi shangedsSae frderteis dswih tbnt  * 	fom t&num meee80211_cates_ontrol _hanged
bnd noiefalue sdre sreflctid 
*		et@he bs tion aata .@Tes rallbbnk feould bnly ue csed
 wen shiefeiver  *
	sed tardware fagt Oontrol a(%EEE80211_THW_HAS_ATES_CONTROL)@ince  *	aoher wis [hiefagt Oontrol acgorithm onsdotified:tdiect y)
 *	TMst be artomic. *
 * @@onnf_tx: CnfiguredtTXfueue_ rragmeer
  (EDCF (aifs, cw_min, cw_max)  *	ayursiog ) or al ardware fTXfueue_. *
	Rqhrne a Qeeateoe merrortcde an ffailue 
 *
	Te wallbbnk fopnpeeep 
 *
 * @@gctbtsf: Gt the Rrrrentl TSFsoiesr alue ofom tfirmare /ardware 
 Crrently   *	ahei is anly uue.dofr eIBSS ode)dSS
ID esrgng wnd nrebuggng.. Isreo ea *		aquired. rnction . * 	Teefallbbnk fopnpeeep 
 *
 * @@sctbtsf: St the RTSFsoiesr o the bseciafcd. alue on the
Qrirmare /ardware 
 * 	Crrently   hei is anly uue.dofr eIBSS ode)drebuggng.. Isreo ea *		aquired. rnction . * 	Teefallbbnk fopnpeeep 
 *
 * @@eqpt btsf: Rest the RTSFsoiesr nd nlslwedrirmare /ardware fo ktynchroitz  *	awih tnher tSTA si whe hIBSS
Ihis is anly used
 nnMIBSS ode).sTes  *
	rnction tn rptionaa tnfthe Rrirmare /ardware focke ufulbtareetf  * 	TSFstynchroitztion .
*		Teefallbbnk fopnpeeep 
 *
 * @@tx_lst ceacon
: Dter isn_ hether tra vlst rIBSS eacon
cwa_ ptt fbyius.
*		Tens n reed d  nly ufr eIBSS ode)dn  she orqoulttofthis trnction tn 
*		sed
 o mdter isn_ hether troreqpl.fo dPopbewRcueste_. *
	Rqhrne anon-zerotnfthis tevice_isesttra vlst rbecon

 *
	Teefallbbnk fopnpeeep 
 *
 * @@apldu_aceon/: Prfarma@ndcerainsf-MPDU
taceon/
*	 	TeewRA/TID anmbnstion aater isn_sshiefevsiogteonatad nTID aethat:
* @	he baplduactivnaMo de crrfarma.dofr .ITe wacevnaMs urfine dtoerugh 
* @	eee80211_capldu_mlme_aceon/
 Stat_ng.wscuesce onmber o(@ssn)
* @	s ahe Rrirt rramescwewexoetithoa rfarma@te wacevnaMn/
 Ntife 
*	@pheattTX/RX_STOPfopnp ssosUMLLblr this  asagmeer

 *
	Te w@buf_szeoeasagmeer
on snly ualui
 wen shiefacevnaMs utt io 
*		%EEE80211_TAPDU_NTX_OPERATIONAL(n  snsicate saha Rper 'srreordr  *	ayufera izeoe(umber of ssuboames )blr this  tesion w--the deiver  *
	acyrneiher thtn (agregateescontainstgs@aoe usuboames  hat ohens *		no thtn (agregateesci/La waychia wlos frames biuld nexced Ahie
*		yufera izeo.sIftjst bikit ng the hsgregatee izeo  hei iwuld be 
*		pwsiolesbhth tasbuf_szeoef s8  *		 - TX:I1.....7 *		 - RX:I 2....7 (los frames #1) *		 - TX:IIIIIIII8..1...
*	biich an rntvlui
 ince o#1cwa_ nwedaq-hansmit ed Aw llp sstAhie
*		yufera izeoef s8
 Cnrect  ays bo ure insmit  #1iwuld be : *		 - TX:IIIIIII1tr a18tr a8  *		Een i"189"iwuld be awrog. ince o1 onld ab fios fnganf. *
 * 	Rqhrne a Qeeateoe merrortcde an ffailue 
 *
	Te wallbbnk fopnpeeep 
 *
 * @@gctbsurvey: RqhrneRor-raangel/@surveyrfformation
 *
 * @@efkll _polb: Poll(efkll Lardware fetaed
sIftou reed tohns,tou alws  *		ned to pet twiph ->efkll _polbio m%tru qetfre fregsterticn
  *	aand@ned to pollbriiph _efkll _pt bhwqptaed()tn tha wallbbnk . * 	Teefallbbnk fopnpeeep 
 *
 * @@sctbcovdiags_olsts: St tslo eoiesblr tivesn covdiags olstsma aseciafcd.
*		et@IEE8s82.11--2007naction i17.3.8.6tsd madefiy ACKeoiesut 
*	aaccordngley; covdiags olstsmcueal bo u- wroTncbled ACKeoiesut 
*	avsioation
rngorithm o(dnamck).@Toadisrilesdnamckpet tvlui
 alue ofr  *		covdiags olsts.@Tes rallbbnk fisreo eaquired. sd may beeep 
 *
 * @@tvsiode)_cmd: Iplieent ondcg80211_ tvsi ode)dommaan .Thierrsseddi@vnftoa  *		b f%UMLL mTiefallbbnk fopnpeeep 
 *
@@tvsiode)_dump: Iplieent ondcg80211_ tvsi ode)ddump mTiefallbbnk fopnpeeep 
 *
 * @@flush: Flushtalbruesdng tfames arom the davdware fueue_,tacing.Lered
*	pheatthe davdware fueue_sdre sempty mTief@ueue_sdasagmeer
on satbntmap
*	aof ueue_sco pflush,tiech as asedfulIifSriferant bairuallinterface s
*		sedSriferant bavdware fueue_s;tn wmy blso bndicate tal wueue__. *
	Ifthe Rasagmeer
o@ritebs utt io m%tru ,ruesdng tfames aay be sritepd . *
	ote:that tvnftcl be uUMLL
 *	TTiefallbbnk fopnpeeep 
 *
 * @@oannel/_ewith : Divers thia eeed t(r that:)fo moffoad  hee wannel/ *		ewith speration.blr tCSAssageive dcrom the dAP oa tfplieent ohans
*
	allbbnk  mTieynmst bten soll teee80211_bchewith _dn  ()fo mn icate 
*	ponplietonaif ahee wannel/sewith 
 *
 * @@sctbnstenna: St tnstennatonfiguretion f(tx_at ,brx_at ) n whe hevice_.
*	pPragmeer
  sr
suntmapsaofcllbowd. sdtenna thoasedclr tTX/RX.@Divers toa  *		eqjcte(TX/RXtoask anmbnstion scha ytopnot aasportedbydeqhrneng t-EINVAL *		(lso ueeeanl0211 .h @NL0211_TATTR_WIPHY_ANTENNANTX)
 *
 * @@gctbnstenna: Gt trrrentl nstennatonfiguretion from tevice_i(tx_at ,brx_at )
 *  * T@aqmanf_on_wannel/: Stat_ a nmrff-wannel/@uriod:tn whe hivesn cannel/,wmst  * 	allbweak fo meee80211_caad y_on_wannel/()fwienunlahat acannel/. ote:
*	pheattnrmatlsaangel/@hraffc,in aot beroppedis tohs rn rnttendd
clr thw
*	aoffoad .IFamess o m insmit  n whe hrff-wannel/@wannel/@ne qhansmit ed  *		no mlly uexletiofr ehe h%EEE80211_TTX_CTL_TX_OFFHANGqlags
TWen shie *		dretion f(hech awll baways bbeanon-zero)wexores ,Rha briver fmst boll 
*		eee80211_baqmanf_on_wannel/_exoresd().
*		ote that toens ullbbnk fuy be sollbd thele uhe bevice_insret@IDLEtad  * 	ast be arcesped tn thansrulse. * 	Tens ullbbnk fuy beeep 
 *
@@oanceb_aqmanf_on_wannel/: Rcueste_ oat tanbnlgong wrff-wannel/@uriod:tns
*
	lluted
 ytfre fitrexores . Tensrullbbnk fuy beeep 
 *
 * @@sctbrng asagm: St thxusd nax ing tpzeos
 *
 * @@gctbrng asagm: Gt thxusd nax ing trrrentl ns mayximumtpzeos
 *
 * @@tx_fames _uesdng : Cackiinfthe e ci aanyruesdng tfamessi whe hardware  * 	ueue_scytfre fener ng tuwersseve 
 *
 * @@sctbbitrte _oask: St tntoask ofcrte sahoue csed
 fr eagt Oontrol aseleceon/
*	biin ahansmitteng wnrrames
@Crrently snly ulegacyyagt sscr pardle d
 *	TTiefallbbnk fopnpeeep 
 *
T@asio_allbbnk : Ntifiydriver twen shiefavdiags RSSI gos Ellue /eeeow
*	pheeesolds_ oat twre'cregstedrd t iveiousey mTiefallbbnk fopnpeeep 
 *
 * @@eqlese._euferaed_ramess: Rclese.aufferad (fames a ccordngl o the bivesn
*		pragmeer
 .@nttha Rulsemwere'cha briver fyfferastsme vfrmes ior  *		eeep ng.oprticn
 auc80211 wwll bsedchensrullbbnk fo uhellthe driver 
*	phohaglese.csme vfrmes , eiher tfr ePS-pollcr auAPSD
 *
	ote that tnfthe RImre _dta fasagmeer
on s%flso [ha vriver tast boacki
*		e ihier are,4aoe uremes wn whe hivesn TIDs,ind nt ihier are,4aoe uhat 
*		te boames  etng ceqlese.dotin at sast bsiol wpettte baoe -ata ret csn
*		te boames.sIfthe RImre _dta fasagmeer
on s%tru ,rtin aofcoours uhe 
*		aoe -ata ret cast baways bbeatt 
 *
	Te w@tidsdasagmeer
ohellsche Rriver fhech aTIDschohaglese.crames 
*		fom ,tfr ePS-pollcn will baways bave tnly uasingle tet ctt 
 *
	nttha Rulsemraisrfsfsed
 or al PS-pollcnitioaed yaglese.,uhe 
*		@num_ramessiasagmeer
owll baways bbea wsotcde wapnue sshae d.@nt *	ahei iulsemra vriver tast blso uee t%EEE80211_TTX_STATUS_EOSPcrag  *		o whe hX status b(rd nmst bagortedX status )is that she hPS-poll
* 	ariod:ts  roteere  ncded
Ihis is ased (o kdoid asesdng talticle. *		eqponsses or al re iied PS-pollcfames. *
	nttha Rulsemraisrfsfsed
 or auAPSD,Rha b@num_ramessiasagmeer
ouy be  *		beger  hat on  ,cbu ahe driver tmy betn (fewr crames  (t sast bsed  * 	t alese on  ,chwerer )
Intthansrulseit
ds ulso ueqponssile. or  *		eettng uhe dEOSPcrag si whe hQoSfheadr if ahee oames 
sAws ,twen shie *		eerice turiod:tncd ,Rha briver fmst bee t%EEE80211_TTX_STATUS_EOSP *		o whe hlst rramescinrhe BSP mAterinteoe l ,wn wmayboll tehetrnction 
*		eee80211_bstapeosp()to mn orma@ac80211 wf aehettn (f aehetSP  * 	Tens ullbbnk fust be artomic. *
b@ssowe_euferaed_ramess: Prearaeievice_io pasowedhi hivesn ember of soames 
*	phohgocnutRo the bivesnbs tion 
Ihi sremes whtl be sset fbyimc80211  *		viathe bsealliX spah tater traisrullb
Ihi sX snformation
ror aoames 
*	peqlese.doill alws dave toe h%EEE80211_TTX_CTL_NO_PS_BUFFERcrag.ieet *	aand@he hlst rn  uill alws dave t%EEE80211_TTX_STATUS_EOSPctt 
Inttulse
*		fomes arom tmlticle.wTIDsc r
saglese.dasd mhe driver tmightrreordr 
*		te mtettwen che hXIDs,it sast bse thieb%EEE80211_TTX_STATUS_EOSPcrag  *		o whe hlst rramescnd noiesrut  n wll tnher ssnd nlso bardle the dEOSP *		betsi whe hQoSfheadr ionrect y) mAterinteoe l ,wn wan blws doll tehe *		eee80211_bstapeosp()trnction . * 	Teef@tidsdasagmeer
on satbntmapasd mhellsche Rriver fhech aTIDschhe
*		fomes ahtl be son;cn will battmot fave towo its ott 
 *
	Tensrullbbnk fust be artomic. *
 * @@gctbctbssctbcount:  Ethtol aAPIRo tgettet ng -se tcount. *
 * @@gctbctbstaes:  Ethtol aAPIRo tgettaOet toftu64bs tis. *
 * @@gctbctbst ng s:  Ethtol aAPIRo tgettaOet toftst ng s o mdtscrie ustae 
*	pan a rfhapsaoher thuported yhpe sboftethtol aata -se s
 *
 * @@gctbrsio: Gt trrrentl ignel ctreangh tin dBm,tehetrnction tn rptionaa 
*	pan aopnpeeep 
 *
 * @@mgd_prearae_tx: Prearaeior ahansmitteng wnranagement framesiir associateon/ *		b fre fasociated 
Inttmlticraangel/@scenarios,Masairuallinterface  ns
*
	boud wo kddcangel/@ytfre fitrnsrssociated ,cbu aa at
ds 't essociated 
*	pyettitwoedsaot beeessaryiy ue civesnbairuies,tinp sruiclar  ince ony  *		hansmission to kddP2PoGOiueds to de ctynchroitz dfnganft btenoGO's * 	owerseve metaed
sac80211 rill boll tehs trnction tetfre fhansmitteng wn
*		anagement framesi ivrito daveng.Lercessafulb)ussociated Ao pasowedhi  *		diver toorgve Sitsaangel/@hiesblr ttenohansmission ,Ro tgettaOeqponsse *	aand@hobe salesbeottynchroitz thth the bGO
 *	TTiefallbbnk fill bb collbd tytfre fech rhansmitsion tan (p o ceqhrne
*		an80211 rill btinsmit  oi sremesraightraays
 *	TTiefallbbnk fn rptionaa tan aopnp(eould !)peeep 
 *
 * @@mgd_prtecti_tdls_discovdi: PrtectiaaQIDLSadiscovdiy tesion 
TAter tsesdng  *	aaQIDLSadiscovdiy-equiste,awewexoetitareqpl.fo dariverwn whe hAP's * 	cannel/. Wesast bsiaycnnahee wannel/s(no PSM,csan ,betc.), ince onQIDLS *		eetup-ecponssetn satdiect fasketsbot aefferad (y dhe RAP  * 	ac80211 rill boll tehs trnction tjst be fre ftenohansmission aofclQIDLS *		discovdiy-equiste.ITe wrqommaendd
curiod:tnf retecticn
sisra alese  * 	2* @(TIM buriod:)
 *	TTiefallbbnk fn rptionaa tan aopnpeeep 
 *
 * @@add_wannctx: NtifiedsSevice_ioiver tllut wew faangel/@unfexx sceatoon 
 *
t@removd_wannctx: NtifiedsSevice_ioiver tllut waangel/@unfexx sevsiuctuon 
 *
t@oanged_wannctx: NtifiedsSevice_ioiver tllut waangel/@unfexx soangedsSrat  *		ay bhfppenuwen sanmbnsng wdiferant bairuallinterface scnnahee smes * 	aangel/@unfexx shth triferant bsettng s * @@asignecvif_wannctx: NtifiedsSevice_ioiver tllut waangel/@unfexx setng cboud 
*	phohvif
 Pwsiolesbsedci slr thwfueue_ aqmaplng.
 *
t@unasignecvif_wannctx: NtifiedsSevice_ioiver tllut waangel/@unfexx setng 
*		snboud wrom tvif
 * @@swith _vif_wannctx: ewith sa ember of svif arom tne ioannctxtho * 	nyoher ,ma aseciafcd.si whe hlis aof
*		@eee80211_cvif_wannctx_ewith crsseddio tha ioiver ,a ccordngl
*	phohte baoe)dreine dti oieee80211_bchnnctx_ewith _ode   *
 * @@stat__ap: Stat_speration.bn whe hAPinterface   hei is acllbd tnter tll tehe *		eformation
qsnweso_unf ts utt ian aeacon
capnue sre iieves. A wannel/ *		hrfexx sisseoud wy fre ftei is acllbd .Tote that tnfthe Reiver iuses
*	bioftare csan or tROC  hei i(and@@itop_ap)ds 't ecllbd then the
QAPins
*	bjst b"plsedd"dfr epanning /ROC  iich an rnticate  (y dhe Reacon
cetng 
*		disbled /ncbled Bviat@bsssnsor_hanged

 * @@stop_ap: Stopsperation.bn whe hAPinterface 
 *
 * @@eqptat__onpliet : Cllbd tnter ta ullbfo keee80211_baqptat__hw(),twen shie *		reonfigureteonatas tonpliet d. Tensrulnmhelpmhe driver tiplieent shie *		reonfigureteonatstp 
 Aws dcllbd then treonfigureng cbtalsedche  *		diver 'ssaqoumssrnction teqhrneeda1,is tohs rn rjst bikk usnb"inlne "
*		ardware faqptat_. Tensrullbbnk fuy beeep 
 *
 * @@ipv6_ddre_hanged: IPv6address snsigneent oo whe hivesn iterface raanged

 * 	Crrently   hei is anly ulnled dfr eonagem tr aP2Poolint interface s. *
	Tensrullbbnk fn rptionaa ;it sast bot beeep 
 *
 * @@oannel/_ewith ceacon
: Stat_ a  wannel/sewith fo kddew faangel/. *
	Baconsstre,4aoefiid
to mn ludeetCSAtr aECSAtIE ey fre fallbng.whes  *
	rnction  mTiefarrecponsdng trnnt:filel
 si whe seiIE eust be  *		decreent d
,tn  sien shieydeqch r1Rha briver fmst boll 
*		eee80211_bcsa_fnitsh(). Divers tiich aue teee80211_beacon
_get() * 	gt the Rrsatomuneri decreent d
fbyimc80211 ,cbu aast boackirf it
ds  * 	1 usng.oeee80211_ccsa_is_onpliet ()tater the Reacon
cas teen  *		hansmistt.dasd mhe  soll teee80211_bcsa_fnitsh().
*
	Ifthe RCSAtrnnt:fstat_ a  azerotr a1,tehs trnction till bot ae acllbd   *	aince ohier aao't eb ony @hiesbhobe con
cetfre ftenoewith sadyway. *  * T@join_ibts: Joi/LanMIBSS (o/LanMIBSS nterface ); hei is acllbd tnter tll  *		eformation
qsnweso_unf ts utt iupind nhe Reacon
capnue sre iieves. A * 	aangel/@unfexx sisseoud wy fre ftei is acllbd . * T@lesvd_ibts: Leve toe hIBSS nganf. *
 * @@gctbcxoetied_oerugh put:wexhanc the Rcxoetied oerugh putbhoared bhe 
*		teciafcd.ss tion 
Ihi seqhrneedaalue on scxoess d (snwKbps
 I aaqhrne a0
*		e ihie RCrngorithm ooesnaot bave troteeradta fhoa ipvid . *
/
truct ieee80211_bopsp{
	oid a(*tx)(truct ieee80211_chw *hw,
		  struct ieee80211_btx_ontrol a*ontrol ,
		  struct isk_eufe *skb);
	etea(*stat_)(truct ieee80211_chw *hw);
	oid a(*stop)(truct ieee80211_chw *hw);
#ifrei CONFIG_PM
	etea(*suspesd)(truct ieee80211_chw *hw,struct icg80211__wwlec  *wwlec );
	etea(*aqoums)(truct ieee80211_chw *hw);
	oid a(*set_wakeup)(truct ieee80211_chw *hw,sbol ancbled );
#esdnf
	etea(*add_nterface )(truct ieee80211_chw *hw,
			IIIIItruct ieee80211_cvif *vif);
	etea(*oanged_iterface )(truct ieee80211_chw *hw,
				truct ieee80211_cvif *vif,
				num mnl0211 _ifope *ew _ope ,sbol ap2p);
	oid a(*removd_iterface )(truct ieee80211_chw *hw,
				Itruct ieee80211_cvif *vif);
	etea(*onfigu)(truct ieee80211_chw *hw,su32raanged
);
	oid a(*bsssnsor_hanged
)(truct ieee80211_chw *hw,
				Itruct ieee80211_cvif *vif,
				Itruct ieee80211_ceso_unf t*nsor,
				Iu32raanged
);

	etea(*stat__ap)(truct ieee80211_chw *hw,struct ieee80211_cvif *vif);
	oid a(*stop_ap)(truct ieee80211_chw *hw,struct ieee80211_cvif *vif);

	u64b(*prearae_ulticast )(truct ieee80211_chw *hw,
				Itruct inetevibhwqddre_lis a*mc_lis );
	oid a(*onfigured_ilteri)(truct ieee80211_chw *hw,
				Iunigned (sn toangeddclags ,
				Iunigned (sn t*totalclags ,
				Iu64bulticast );
	etea(*st _tim)(truct ieee80211_chw *hw,struct ieee80211_csta *sta,
		  sssssbol ast );
	etea(*st _key)(truct ieee80211_chw *hw,snum mst _key_cmd cmd,
		  ssssstruct ieee80211_cvif *vif,struct ieee80211_csta *sta,
		  ssssstruct ieee80211_ckey_cnf t*key);
	oid a(*pdate _tkip_key)(truct ieee80211_chw *hw,
				truct ieee80211_cvif *vif,
				truct ieee80211_ckey_cnf t*cnf ,
				truct ieee80211_csta *sta,
				u32riv32,su16 *pas e1key);
	oid a(*ictbrekey_dta )(truct ieee80211_chw *hw,
			IIIIIsstruct ieee80211_cvif *vif,
			IIIIIsstruct icg80211__gtkbrekey_dta f*dta );
	oid a(*ictbrefaule_untcbst_key)(truct ieee80211_chw *hw,
					truct ieee80211_cvif *vif,(sn tidx);
	etea(*hwqpann)(truct ieee80211_chw *hw,struct ieee80211_cvif *vif,
		  ssssstruct ieee80211_csct _aqueste-*req);
	oid a(*oanceb_hwqpann)(truct ieee80211_chw *hw,
			IIIIIsstruct ieee80211_cvif *vif);
	etea(*sh d bsct _itat_)(truct ieee80211_chw *hw,
				truct ieee80211_cvif *vif,
				truct icg80211__sc d bsct _aqueste-*req,
				truct ieee80211_csct _edsS*eds);
	etea(*sh d bsct _itop)(truct ieee80211_chw *hw,
			IIIIIsstruct ieee80211_cvif *vif);
	oid a(*iwbsct _itat_)(truct ieee80211_chw *hw);
	oid a(*iwbsct _onpliet )(truct ieee80211_chw *hw);
	etea(*gctbataes)(truct ieee80211_chw *hw,
			Itruct ieee80211_cowe_lvenlbataes *stats);
	oid a(*gctbtkip_seq)(truct ieee80211_chw *hw,su8 hwqkey_idx,
			IIIIIu32r*iv32,su16 *iv16);
	etea(*st _frag_hieesolds)(truct ieee80211_chw *hw,su32ralue );
	etea(*st _rts_hieesolds)(truct ieee80211_chw *hw,su32ralue );
	etea(*sta_add)(truct ieee80211_chw *hw,struct ieee80211_cvif *vif,
		  ssssstruct ieee80211_csta *sta);
	etea(*sta_removd)(truct ieee80211_chw *hw,struct ieee80211_cvif *vif,
			sstruct ieee80211_csta *sta);
#ifrei CONFIG_MAC0211_bDEBUGFS 	oid a(*sta_add_debugfs)(truct ieee80211_chw *hw,
				truct ieee80211_cvif *vif,
				truct ieee80211_csta *sta,
				truct idnt ryf*dir);
	oid a(*sta_removd_debugfs)(truct ieee80211_chw *hw,
				Isstruct ieee80211_cvif *vif,
				Isstruct ieee80211_csta *sta,
				Isstruct idnt ryf*dir);
#esdnf
	oid a(*sta_otifiy)(truct ieee80211_chw *hw,struct ieee80211_cvif *vif,
			num msta_otifiy_cmd,struct ieee80211_csta *sta);
	etea(*sta_etaed)(truct ieee80211_chw *hw,struct ieee80211_cvif *vif,
			struct ieee80211_csta *sta,
			snum meee80211_csta_etaed lds_etaed,
			snum meee80211_csta_etaed ew _etaed);
	oid a(*sta_ iv_rcu_removd)(truct ieee80211_chw *hw,
				Isstruct ieee80211_cvif *vif,
				Isstruct ieee80211_csta *sta);
	oid a(*sta_rc_pdate )(truct ieee80211_chw *hw,
			IIIIIstruct ieee80211_cvif *vif,
			IIIIIstruct ieee80211_csta *sta,
			sIIIIIu32raanged
);
	etea(*onfi_tx)(truct ieee80211_chw *hw,
		  ssssstruct ieee80211_cvif *vif,su16 ac,
		  sssssonsststruct ieee80211_btx_ueue__asagms *asagms);
	u64b(*gctbtsf)(truct ieee80211_chw *hw,struct ieee80211_cvif *vif);
	oid a(*sctbtsf)(truct ieee80211_chw *hw,struct ieee80211_cvif *vif,
			u64btsf);
	oid a(*resctbtsf)(truct ieee80211_chw *hw,struct ieee80211_cvif *vif);
	etea(*tx_lst ceacon
)(truct ieee80211_chw *hw);
	etea(*apldu_aceon/)(truct ieee80211_chw *hw,
			IIIItruct ieee80211_cvif *vif,
			IIIInum meee80211_capldu_mlme_aceon/Laceon/,
			IIIItruct ieee80211_csta *sta,su16 tid,su16 *ss/,
			IIIIu8 buf_szeo);
	etea(*gctbaurvey)(truct ieee80211_chw *hw,ssn tidx,
		truct isurveysnsor *survey);
	oid a(*rfkll _polb)(truct ieee80211_chw *hw);
	oid a(*set_covdiags_olsts)(truct ieee80211_chw *hw,st16 covdiags_olsts);
#ifrei CONFIG_NL0211_TTESTMODE
	etea(*tvsiode)_cmd)(truct ieee80211_chw *hw,struct ieee80211_cvif *vif,
			ss  oid a*dta ,ssn tle );
	etea(*tvsiode)_dump)(truct ieee80211_chw *hw,struct isk_eufe *skb,
			IIIIItruct inetlink_allbbnk t*cb,
			IIIIIoid a*dta ,ssn tle );
#esdnf
	oid a(*flush)(truct ieee80211_chw *hw,struct ieee80211_cvif *vif,
		  ssssu32rueue_s,sbol arite);
	oid a(*oannel/_ewith )(truct ieee80211_chw *hw,
			IIIIIsstruct ieee80211_coannel/_ewith t*ch_ewith );
	etea(*st _nstenna)(truct ieee80211_chw *hw,su32rtx_at ,bu32rrx_at );
	etea(*gctbnstenna)(truct ieee80211_chw *hw,su32r*tx_at ,bu32r*rx_at );

	etea(*aqmanf_on_wannel/)(truct ieee80211_chw *hw,
				Itruct ieee80211_cvif *vif,
				Itruct ieee80211_caangel/@*oann,
				Ieteadretion ,
				Inum meee80211_caoc_ope *ope );
	etea(*oanceb_aqmanf_on_wannel/)(truct ieee80211_chw *hw);
	etea(*sctbrng asagm)(truct ieee80211_chw *hw,su32rtx,bu32rrx);
	oid a(*gctbrng asagm)(truct ieee80211_chw *hw,
			sIIIIIu32r*tx,bu32r*tx_max,bu32r*rx,bu32r*rx_max);
	bol a(*tx_fames _uesdng )(truct ieee80211_chw *hw);
	etea(*sctbbitrte _oask)(truct ieee80211_chw *hw,struct ieee80211_cvif *vif,
				onsststruct icg80211__bitrte _oask *oask);
	oid a(*rsio_allbbnk )(truct ieee80211_chw *hw,
			IIIIIstruct ieee80211_cvif *vif,
			IIIIIsnum meee80211_casio_ven t asio_ven t);

	oid a(*ssowe_euferaed_ramess)(truct ieee80211_chw *hw,
				IssIsstruct ieee80211_csta *sta,
				IssIIIu16 tids,ssn tnum_ramess,
				IssIIInum meee80211_ciames_eqlese._ope *rqasn/,
				IssIIIbol amre _dta );
	oid a(*relese._euferaed_ramess)(truct ieee80211_chw *hw,
					truct ieee80211_csta *sta,
					u16 tids,ssn tnum_ramess,
					num meee80211_ciames_eqlese._ope *rqasn/,
					bol amre _dta );

	ete	(*gctbctbssctbcount)(truct ieee80211_chw *hw,
				IssIstruct ieee80211_cvif *vif,(sn tsst );
	oid 	(*gctbctbstaes)(truct ieee80211_chw *hw,
				truct ieee80211_cvif *vif,
				truct iethtol bataes *stats,Iu64b*dta );
	oid 	(*gctbctbst ng s)(truct ieee80211_chw *hw,
				Istruct ieee80211_cvif *vif,
				Isu32rset., u8b*dta );
	ete	(*gctbasio)(truct ieee80211_chw *hw,struct ieee80211_cvif *vif,
			ss  truct ieee80211_csta *sta,ss8b*asio_dbm);

	oid 	(*mgd_prearae_tx)(truct ieee80211_chw *hw,
				Istruct ieee80211_cvif *vif);

	oid 	(*mgd_prtecti_tdls_discovdi)(truct ieee80211_chw *hw,
					IIIsstruct ieee80211_cvif *vif);

	etea(*add_chnnctx)(truct ieee80211_chw *hw,
			IIItruct ieee80211_caangctx_ont t*ctx);
	oid a(*removd_wannctx)(truct ieee80211_chw *hw,
			IIIIIsstruct ieee80211_coannctx_ont t*ctx);
	oid a(*oanged_wannctx)(truct ieee80211_chw *hw,
			IIIIIsstruct ieee80211_coannctx_ont t*ctx,
			IIIIIssu32raanged
);
	etea(*asignecvif_wannctx)(truct ieee80211_chw *hw,
				Istruct ieee80211_cvif *vif,
				Istruct ieee80211_coannctx_ont t*ctx);
	oid a(*unasignecvif_wannctx)(truct ieee80211_chw *hw,
				IssIstruct ieee80211_cvif *vif,
				IssIstruct ieee80211_coannctx_ont t*ctx);
	etea(*swith _vif_wannctx)(truct ieee80211_chw *hw,
				Istruct ieee80211_cvif_wannctx_ewith c*vifs,
				Issn tn_vifs,
				Isnum meee80211_cchnnctx_ewith _ode baoe));

	oid a(*eqptat__onpliet )(truct ieee80211_chw *hw);

#if IS_ENABLED(CONFIG_IPV6)
	oid a(*ipv6_ddre_hanged)(truct ieee80211_chw *hw,
				Itruct ieee80211_cvif *vif,
				Itruct ienet6_dev *idev);
#esdnf
	oid a(*oannel/_ewith ceacon
)(truct ieee80211_chw *hw,
				IssIsstruct ieee80211_cvif *vif,
				IssIsstruct icg80211__oannbref@*oannref);

	etea(*join_ibts)(truct ieee80211_chw *hw,struct ieee80211_cvif *vif);
	oid a(*lesvd_ibts)(truct ieee80211_chw *hw,struct ieee80211_cvif *vif);
	u32r(*gctbcxoetied_oerugh put)(truct ieee80211_csta *sta);
;

/**
 * seee80211_casowcchw -  Asowcte tadew fardware fevice_ *	
* @Tei cmst be acllbd toce oor
tach rardware fevice_
Ihi seqhrneedaponterf
* @mst be ased (o kaeeraio thas tevice_iwe  soll ng wrher tfnction s
 *
tac80211 rasowcte  a  privte tdta fre ablr ttenoriver fponterd@hobe  *	t@privti oitruct ieee80211_chw,tehetizeoef shas tre abissivesnoas *	t@priv_dta _len. *
 * @@priv_dta _len:tle gh tnf reivte tdta  * @@ops:rullbbnk sblr this  evice_ *	
* @Rqhrne: Aaponterfio tha iew fardware fevice_,tr a%UMLLoo werror. *
/
truct ieee80211_bhw *eee80211_casowcchw(izeo_t priv_dta _len,
					onsststruct ieee80211_bopsp*ops)

/**
 * seee80211_cregstedrchw - Regstedrsardware fevice_ *	
* @Youtast boll tehs trnction tetfre fny
rnher tfnction scsn
*	tac80211 .Tote that te fre fatardware capnue sregstedrd ,tou 
*	tned to pfil tehetontains.doilph 'sunformation
.
*	
* TIhw:uhe bevice_io kaegstedrsasseqhrneedabyitee80211_casowcchw() *	
* @Rqhrne: 0oo wercessa. Anmerrortcde anter wis . *
/
sn tiee80211_cregstedrchw(truct ieee80211_chw *hw);

**
 * struct ieee80211_btptbblink - oerugh putbblink dtscripeon/
*	 @oerugh put:woerugh putbsnwKbit/sec
*	 @blink_time:bblink hiesbsnwmil is hrfd  * 	(fulbtaycle,Rne.tne inff +tne innburiod:) *
/
truct ieee80211_btptbblink {
	eteaoerugh put;
	eteablink_time;
;

/**
 * snum meee80211_ctptbled_oreger clags  - oerugh putboreger crag s *	t@EEE80211_TTPT_LEDTRIG_FL_RADIO:Tncbled blinkdngswth tradio *	t@EEE80211_TTPT_LEDTRIG_FL_WORK:Tncbled blinkdngswe  sworkng 
*	t@EEE80211_TTPT_LEDTRIG_FL_CONNECTED:Tncbled blinkdngswe  st alese on   *		eferface  isshrfnetied nsOime vwa ,wn ludeng cbtng wnd AP *
/
num meee80211_ctptbled_oreger clags  {
	EEE80211_TTPT_LEDTRIG_FL_RADIO		= BIT(0),
	EEE80211_TTPT_LEDTRIG_FL_WORK		= BIT(1),
	EEE80211_TTPT_LEDTRIG_FL_CONNECTED	= BIT(2),
};

#ifrei CONFIG_MAC0211_bLEDS
oanr *__eee80211_cgctbtxbled_nmes(truct ieee80211_chw *hw);
oanr *__eee80211_cgctbrxbled_nmes(truct ieee80211_chw *hw);
oanr *__eee80211_cgctbssocibled_nmes(truct ieee80211_chw *hw);
oanr *__eee80211_cgctbradiobled_nmes(truct ieee80211_chw *hw);
oanr *__eee80211_cceatoectptbled_oreger (truct ieee80211_chw *hw,
					Iunigned (sn tlags ,
					sonsststruct ieee80211_btptbblink *blink_tbled,
					Iunigned (sn tblink_tbled_le );
#esdnf
**
 * seee80211_cgctbtxbled_nmes - gt tnmes nf TX LED *	
* @ac80211 raeatoe a  tinsmit  LEDboreger crr
tach rwieqlesshardware  *  hat acn be ased (o kriver LEDsrf iou rceiver fregstedr a  LEDbevice_.
*	 Tens rnction raqhrne aha iemes (r a%UMLLonfte tTonfigured dfr eLEDs)
* @f aehetoreger csotou aan blutoatioclly ulink hhe LEDbevice_.
*	
* TIhw:uhe bardware fo kgt the RLEDboreger cnmesiir  *	
* @Rqhrne: Ta iemes f aehetLEDboreger .a%UMLLonfte tTonfigured dfr eLEDs. *
/
trtioc inlne  oanr *eee80211_cgctbtxbled_nmes(truct ieee80211_chw *hw)
{
#ifrei CONFIG_MAC0211_bLEDS
	aqhrne __eee80211_cgctbtxbled_nmes(hw);
#else
	aqhrne UMLL;
#esdnf
}
/**
 * seee80211_cgctbrxbled_nmes - gt tnmes nf RX LED *	
* @ac80211 raeatoe a  ageive  LEDboreger crr
tach rwieqlesshardware  *  hat acn be ased (o kriver LEDsrf iou rceiver fregstedr a  LEDbevice_.
*	 Tens rnction raqhrne aha iemes (r a%UMLLonfte tTonfigured dfr eLEDs)
* @f aehetoreger csotou aan blutoatioclly ulink hhe LEDbevice_.
*	
* TIhw:uhe bardware fo kgt the RLEDboreger cnmesiir  *	
* @Rqhrne: Ta iemes f aehetLEDboreger .a%UMLLonfte tTonfigured dfr eLEDs. *
/
trtioc inlne  oanr *eee80211_cgctbrxbled_nmes(truct ieee80211_chw *hw)
{
#ifrei CONFIG_MAC0211_bLEDS
	aqhrne __eee80211_cgctbrxbled_nmes(hw);
#else
	aqhrne UMLL;
#esdnf
}
/**
 * seee80211_cgctbssocibled_nmes - gt tnmes nf ssociateon/LLED *	
* @ac80211 raeatoe a  ssociateon/LLEDboreger crr
tach rwieqlesshardware  *  hat acn be ased (o kriver LEDsrf iou rceiver fregstedr a  LEDbevice_.
*	 Tens rnction raqhrne aha iemes (r a%UMLLonfte tTonfigured dfr eLEDs)
* @f aehetoreger csotou aan blutoatioclly ulink hhe LEDbevice_.
*	
* TIhw:uhe bardware fo kgt the RLEDboreger cnmesiir  *	
* @Rqhrne: Ta iemes f aehetLEDboreger .a%UMLLonfte tTonfigured dfr eLEDs. *
/
trtioc inlne  oanr *eee80211_cgctbssocibled_nmes(truct ieee80211_chw *hw)
{
#ifrei CONFIG_MAC0211_bLEDS
	aqhrne __eee80211_cgctbssocibled_nmes(hw);
#else
	aqhrne UMLL;
#esdnf
}
/**
 * seee80211_cgctbradiobled_nmes - gt tnmes nf radio LED *	
* @ac80211 raeatoe a  aadio hangedLLEDboreger crr
tach rwieqlesshardware  *  hat acn be ased (o kriver LEDsrf iou rceiver fregstedr a  LEDbevice_.
*	 Tens rnction raqhrne aha iemes (r a%UMLLonfte tTonfigured dfr eLEDs)
* @f aehetoreger csotou aan blutoatioclly ulink hhe LEDbevice_.
*	
* TIhw:uhe bardware fo kgt the RLEDboreger cnmesiir  *	
* @Rqhrne: Ta iemes f aehetLEDboreger .a%UMLLonfte tTonfigured dfr eLEDs. *
/
trtioc inlne  oanr *eee80211_cgctbradiobled_nmes(truct ieee80211_chw *hw)
{
#ifrei CONFIG_MAC0211_bLEDS
	aqhrne __eee80211_cgctbradiobled_nmes(hw);
#else
	aqhrne UMLL;
#esdnf
}
/**
 * seee80211_cceatoectptbled_oreger  - ceatoe oerugh putbLEDboreger 
* TIhw:uhe bardware fo kceatoe oeeboreger crr

* @@fla s: oreger crag s,ueeea&num meee80211_ctptbled_oreger clags 
*	 @blink_tbled:uhe bblink hbled --iueds to de cordr   (y dherugh put
*	 @blink_tbled_len:tizeoef sha bblink hbled *	
* @Rqhrne: %UMLLo(ittulsebofterror,tr anfte bLEDboreger  tre 
* @onfigured ) r ttenoemes f aehetew foreger . *	
* @ote : Tens rnction rmst be acllbd tytfre fiee80211_cregstedrchw(). *
/
trtioc inlne  oanr *
eee80211_cceatoectptbled_oreger (truct ieee80211_chw *hw,Iunigned (sn tlags ,
				sonsststruct ieee80211_btptbblink *blink_tbled,
				Iunigned (sn tblink_tbled_le )
{
#ifrei CONFIG_MAC0211_bLEDS
	aqhrne __eee80211_cceatoectptbled_oreger (hw,Irag s,ublink_tbled,
						 tblink_tbled_le );
#else
	aqhrne UMLL;
#esdnf
}
/**
 * seee80211_cunregstedrchw - Unaegstedrsafardware fevice_ *	
* @Tei crnction tnntruct  auc80211 wo pfreerasowcte  yagsu re s
*	tan (pnaegstedrsneteviics arom the dnetworkng usubsytedm.
*	
* TIhw:uhe bardware fo kpnaegstedr *
/
oid aeee80211_cunregstedrchw(truct ieee80211_chw *hw);

**
 * seee80211_ciaeechw - freerardware fevscriper  *	
* @Tei crnction tfreesbvenryh i/g hat awa_ rwowcte  ,wn ludeng che 
*	 privte tdta flr ttenoriver .@Youtast boll teee80211_cunregstedrchw()
* @y fre fallbng.whes trnction . * 
* TIhw:uhe bardware fo kfree *
/
oid aeee80211_ciaeechw(truct ieee80211_chw *hw);

**
 * seee80211_caqptat__hw - aqptat_tardware canpliet ly * 
* TCll tehs trnction then the
Qardware cwasseqstat_d
dfr epme vrqasn/
* T(ardware cerror,t...)asd mhe driver tfsfsnalesbeoteqstre fit rs ti 
*	 byit elf.
sac80211 rssoum sahat at toens pontemhe driver /ardware  *  iscanpliet lyfsnnitioalie.dasd meropped,wn wstat_ ahe doroessafe  *	tallbng.whed ->stat_()speration..ITe weiver till boed (o kaett ial  *	 nterfel ctrae that aitras t ivrito dallbng.whes trnction . * 
* TIhw:uhe bardware fo keqstat_ *
/
oid aeee80211_caqptat__hw(truct ieee80211_chw *hw);

**
 * seee80211_cnapi_ddr - nitioalieoeac80211 rNAPIRunfexx 
* TIhw:uhe bardware fo knitioalieoehe bNAPIRunfexx  n/
*	 @napi:ehe bNAPIRunfexx  o knitioalieo
*	 @napi_dev:ddummybNAPIRneteviics,tere'ct doticwasoe oeebspce  ifdhi  *		diver toesn't eue tNAPI * @@polb: pollcfnction 
*	 @weight: refaule weight * 
* TSeerass doetifcnapi_ddr(). *
/
oid aeee80211_cnapi_ddr(truct ieee80211_chw *hw,struct inapi_truct i*napi,
			truct inet_evice_i*napi_dev,
			etea(*polb)(truct inapi_truct i*,(sn ),
			eteaweight);

**
 * seee80211_cax - aqeive  iames * 
* TUsemraisrrnction th bardlsageive dcromess o mac80211 .TTe wrqoive 
*	 bufera et@@ikbsast bsiar shth tat@IEE8s82.11-fheadr 
Inttulseif an
*	 pgem t@ikbsfsfsed
,mhe driver tfsfrqommaendd
choa u ahe deee80211_
*	 headr if ahee oamesoo whe hlne nr pat_spfthe RIikbso kdoid amemor  *	trwowcteon tan /r eoemcpy(y dhe Rstnk . * 
* @Tei crnction tacyrnt ae acllbd tet@IRQRunfexx .TCll sio thas tfnction 
*	 or al ingle tardware fmst be atynchroitz dfnganft bach rnter .TCll sio  *  has tfnction ,seee80211_cax_ni()asd meee80211_cax_irqsafs()aacyrnt ae 
* @aixd
 or al ingle tardware 
IMst beo eau sanncrrently shth  * seee80211_ctx_eatus () r teee80211_ctx_eatus _ni(). * 
* @nttoroessafunfexx  ue teftead aeee80211_cax_ni(). * 
* TIhw:uhe bardware foas tfamesocmescinrn 
*	 @ikb:uhe bbufera o keqoive ,trwnd
fbyimc80211 tater traisrullb *
/
oid aeee80211_cax(truct ieee80211_chw *hw,struct isk_eufe *skb);

**
 * seee80211_cax_irqsafs - aqeive  iames * 
* TLkk ueee80211_cax()cbu acn be acllbd tet@IRQRunfexx 
* T(nterfel y sdeerasto kddtaskiet.) *	
* @Cll sio thas tfnction ,ueee80211_cax()cr teee80211_cax_ni()aacyrnt 
* @y @aixd
 or al ingle tardware 
Mst beo eau sanncrrently shth  * seee80211_ctx_eatus () r teee80211_ctx_eatus _ni(). * 
* @Ihw:uhe bardware foas tfamesocmescinrn 
*	 @ikb:uhe bbufera o keqoive ,trwnd
fbyimc80211 tater traisrullb *
/
oid aeee80211_cax_irqsafs(truct ieee80211_chw *hw,struct isk_eufe *skb);

**
 * seee80211_cax_ni - aqeive  iameso(ittoroessafunfexx ) * 
* TLkk ueee80211_cax()cbu acn be acllbd tet@oroessafunfexx 
* T(nterfel y sdisbledsseottm thalves). * 
* @Cll sio thas tfnction ,ueee80211_cax()csd meee80211_cax_irqsafs()aacy
* @nt ae aaixd
 or al ingle tardware 
IMst beo eau sanncrrently shth  * seee80211_ctx_eatus () r teee80211_ctx_eatus _ni(). * 
* @Ihw:uhe bardware foas tfamesocmescinrn 
*	 @ikb:uhe bbufera o keqoive ,trwnd
fbyimc80211 tater traisrullb *
/
trtioc inlne  oid aeee80211_cax_ni(truct ieee80211_chw *hw,
				Isstruct isk_eufe *skb)
{
	owctl_bh_disbled();
	eee80211_cax(hw,stkb);
	owctl_bh_ncbled();
}
/**
 * seee80211_csta_ s_hansmiionai- PSfhansmiionaifortcdfnetied sa  *  * sWen speratiog. inrAP ode)thth the b%EEE80211_THW_AP_LINKLS:
*	 oag.ieet,bsedchensrrnction th bn orma@ac80211 wllut watcdfnetied sa ion 
*	 ener ng /lesvog. PS ode). * 
* @Tei crnction tacyrnt ae acllbd tet@IRQRunfexx  r thth tioftirqsancbled . * 
* @Cll sio thas tfnction  or al ingle tardware fmst be atynchroitz dfnganft 
*	 ech rnter . * 
* @Isa : crrently scdfnetied sa  * @@stat_:bsiar sr eptopsS:
*	
* @Rqhrne: 0oo wercessa. -EINVALthen the
Qaquesteed PS ode)ts ulsaad yott 
 *
/
sn tiee80211_csta_ s_hansmiiona(truct ieee80211_csta *sta,sbol aitat_);

**
 * seee80211_csta_ s_hansmiiona_ni - PSfhansmiionaifortcdfnetied sa  *                                   (ittoroessafunfexx ) * 
* TLkk ueee80211_csta_ s_hansmiiona()cbu acn be acllbd tet@oroessafunfexx 
* T(nterfel y sdisbledsseottm thalves). Cnfrrrentl oll taqptriceonatstil  *	 applios
 *
 * @@sa : crrently scdfnetied sa  * @@stat_:bsiar sr eptopsS:
*	
* @Rqhrne: Lkk ueee80211_csta_ s_hansmiiona(). *
/
trtioc inlne  sn tiee80211_csta_ s_hansmiiona_ni(truct ieee80211_csta *sta,
						 sbol aitat_)
{
	etearet;

	owctl_bh_disbled();
	ret =tiee80211_csta_ s_hansmiiona(tra,sstat_);
	owctl_bh_ncbled();

	aqhrne ret;
}
/**
* @Te sX sheadrom taettrvd
fbyimc80211 tfortit rrwn tx_eatus tfnction s
 *
this is aenugh flr ttenoradiotapfheadr 
 *
/
#reine  EEE80211_TTX_STATUS_HEADROOM	14

**
 * seee80211_csta_sctbbfferad (-bn orma@ac80211 wllut wriver -ufferad (fames  * @@sa : itruct ieee80211_csta ponterfilr ttenoeeep ng.oprticn
 * @@tid:uhe bTID hat aas tefferad (fames  * @@efferad :snsicate sahether tr tnt afames a r.aufferad (fr this  TID * 
* @nfclQriver fyfferastfrmes ior a  pwerseve metaeon tnntrad anf rasigg 
*	tte mtenk fo umc80211 tfortre insmitsion ,Roh metaeon tuy betll boed 
*	tt de ctold hat she r are,4ufferad (fames aviathe bIM bbit. * 
* @Tei crnction tn orma auc80211 wwether tr tnt ahe r are,4oames  hatttre 
* @ufferad (i whe hriver for a  ivesn TID;@ac80211 rat ohesn sedchensrdta  * @o pet the bIM bbit (NOTE: Tens mayboll tbnk fnno tha ioiver ' utt _tim *	tallb! Bewreetf whe hlockng !) * 
* @nfclllcfamessc r
saglese.dao the bstaeon t(du'ct dPS-pollcr auAPSD)
*	tte  whe hriver fueds to dn orma@ac80211 what she r anohloner cre 
* @oames  efferad . Hwerer ,twen shiebstaeon twcke uupsac80211 rssoum s
*  hat all tbfferad (fames aill bb chansmistt.dasd moiesrschensrdta   *	hriver sboed (o kmckeLered hieydn orma@ac80211 wllut wal tbfferad 
* @oames  nnahee seep fhansmiionai(sta_otifiy()fwth t%STA_NOTIFY_SLEEP). *	
* @ote what shechnoclly uac80211 wfly uueds to dknwedhins pr cAC,tnt apr 
* TTID,cbu aince oriver fyfferadngswtllcnievitble bhfppenupr cTID (ince  * settfsfrqlted Ao pagregateon/)settfsfeseira o kmckeLac80211 wmapahe 
*	 TID h the bACsassequired. nntrad anf kep ng.ohanckci/Lallhriver sbrat  *	 sedchensrAPI. *
/
oid aeee80211_csta_sctbbfferad (truct ieee80211_csta *sta,
				u8 tid,sbol abfferad );

**
 * seee80211_cgctbtxbagt ss-kgt the Rseleced Aoinsmit  agt ssor a  pskets * 
* TCll tehs trnction ti/La eiver tilh tor-rasketsbagt Oseleceon/thuporte * @o panmbnsettenoratedn or(i whe hasketsbtxdn or(hth the bmot feqoit:
* @agt Oseleceon/thbled lr ttenoetaeon tnt ry
 *
 * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . * @@sa : te wrqoive roetaeon to piich ahis  asketsbs uttnt. *
 @ikb:uhe bfamesot de ctansmistt.d. *
 @evsi:bbufera rr
taxhanc   yagt /re iyrfformation
 *
 @maxbagt s:mayximumtember of srte sahoufetch *
/
oid aeee80211_cgctbtxbagt s(truct ieee80211_cvif *vif,
			ss  truct ieee80211_csta *sta,
			ss  truct isk_eufe *skb,
			IIIItruct ieee80211_btx_agt O*evsi,
			IIIIeteamaxbagt s);

**
 * seee80211_ctx_eatus t-Aoinsmit  eatus tallbbnk  * 
* TCll tehs trnction tor a l btinsmit td (fames a ter the ybave teen  *	ctansmistt.d. I ans pr itsiolesbeote tToll tehs trnction tor 
* @alticast @oames  ef toens ul/Laferc  eatuietlcs. * 
* @Tei crnction tacyrnt ae acllbd tet@IRQRunfexx .TCll sio thas tfnction 
*	 or al ingle tardware fmst be atynchroitz dfnganft bach rnter .TCll s * @o phas tfnction ,ueee80211_ctx_eatus _ni()csd meee80211_ctx_eatus _irqsafs()
* @acyrnt ae aaixd
 or al ingle tardware 
IMst beo eau sanncrrently shth  * seee80211_cax()cr teee80211_cax_ni(). * 
* @Ihw:uhe bardware foa bfamesowasstinsmit td (e  *	t@ikb:uhe bfamesotat awa_ tinsmit td ,trwnd
fbyimc80211 tater traisrullb *
/
oid aeee80211_ctx_eatus (truct ieee80211_chw *hw,
			Itruct isk_eufe *skb);

**
 * seee80211_ctx_eatus _nit-Aoinsmit  eatus tallbbnk  (ittoroessafunfexx ) * 
* TLkk ueee80211_ctx_eatus () bu acn be acllbd tet@oroessafunfexx . * 
* @Cll sio thas tfnction ,ueee80211_ctx_eatus () ad  * meee80211_ctx_eatus _irqsafs()@acyrnt ae aaixd

*	 or al ingle tardware . * 
* @Ihw:uhe bardware foa bfamesowasstinsmit td (e  *	t@ikb:uhe bfamesotat awa_ tinsmit td ,trwnd
fbyimc80211 tater traisrullb *
/
trtioc inlne  oid aeee80211_ctx_eatus _ni(truct ieee80211_chw *hw,
					IItruct isk_eufe *skb)
{
	owctl_bh_disbled();
	eee80211_ctx_eatus (hw,stkb);
	owctl_bh_ncbled();
}
/**
 * seee80211_ctx_eatus _irqsafst-AIRQ-safstoinsmit  eatus tallbbnk  * 
* TLkk ueee80211_ctx_eatus () bu acn be acllbd tet@IRQRunfexx 
* T(nterfel y sdeerasto kddtaskiet.) *	
* @Cll sio thas tfnction ,ueee80211_ctx_eatus () ad  * meee80211_ctx_eatus _ni()aacyrnt ae aaixd
 or al ingle tardware 
 * 
* @Ihw:uhe bardware foa bfamesowasstinsmit td (e  *	t@ikb:uhe bfamesotat awa_ tinsmit td ,trwnd
fbyimc80211 tater traisrullb *
/
oid aeee80211_ctx_eatus _irqsafs(truct ieee80211_chw *hw,
				Itruct isk_eufe *skb);

**
 * seee80211_cagortecowe_nk  -bagortednon-ecponsdng tprticn
 *  * sWen speratiog. inrAP-ode),Toll tehs trnction to keqortedadnon-ecponsdng  *	tadfnetied STA
 *
 * @@sa : he dnon-ecponsdng tcdfnetied sa  * @@num_asketss:member of sasketsssset fo k@sa shth ut wateqponsse *	/
oid aeee80211_caqortecowe_nk (truct ieee80211_csta *sta,su32rnum_asketss);

#reine  EEE80211_TMAX_CSA_COUNTERS_NUM 2

**
 * struct ieee80211_bmutbled_offse s -bmutbledReacon
coffse s * @@tim_offse : pomiionainf TM beieent  * @@tim_le gh :tizeoef sTM beieent  * @@csa_omuneri_offs:dariaycnf EEE80211_TMAX_CSA_COUNTERS_NUM offse s * phohCSAtrnnt:r
 .@@Tei cariayccn bontainsazerotalue  tiich  * peould ae aignor.d. *
/
truct ieee80211_bmutbled_offse s {
	u16 tim_offse ;
	u16 tim_le gh ;

	u16 csa_omuneri_offs[EEE80211_TMAX_CSA_COUNTERS_NUM];
;

/**
 * seee80211_ceacon
_get_templted -beacon
ctemplted genration.bfnction 
*	 @hw:uponterfiobains.dolom ttee80211_casowcchw(). * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . * @@offs:ditruct ieee80211_cmutbled_offse s ponterfio ttruct itat awil  *		aqeive  he doffse s tat auy be  pdate  (y dhe Rriver . * 
* @nfche driver tfplieent sbeacon
ng tade)s,it sast bsedchensrrnction th 
* @obainsuhe bbacon
ctemplted. * 
* @Tei crnction teould ae ased (e ihie bacon
cfamessc r
sgenrati  (y dhe  *	hrviics,tsd mhe  she driver tast bsedche seqhrneedabacon
ca ahe dtemplted
* @Te sriver tr ttenorvice_ire faqponssile. o kpdate ttenoTIM bsd ,twen  *	 applicbled,ttenoCSAtrnnt:. * 
* @Te driver tfsfrqponssile. or kfreeng.whed eqhrneedaskb. * 
* @Rqhrne: Ta ibacon
ctemplted.a%UMLLoo werror. *
/
truct isk_eufe *
eee80211_ceacon
_get_templted(truct ieee80211_chw *hw,
			IIIIIstruct ieee80211_cvif *vif,
			IIIIIstruct ieee80211_cmutbled_offse s *offs);

**
 * seee80211_ceacon
_get_tim -beacon
cgenration.bfnction 
*	 @hw:uponterfiobains.dolom ttee80211_casowcchw(). * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . * @@tim_offse : ponterfio tvarialesbeat awil  aqeive  he dIM bIEdoffse .
*
	Se fo k0rf itnalud a(nsunon-AP ode)s). * @@tim_le gh :tponterfio tvarialesbeat awil  aqeive  he dIM bIEdle gh   *	a(n ludeng che  IDtsd mle gh tbytes!).
*
	Se fo k0rf itnalud a(nsunon-AP ode)s). * 
* @nfche driver tfplieent sbeacon
ng tade)s,it sast bsedchensrrnction th 
* @obainsuhe bbacon
cfames. *

* @nfche dbacon
cfamessc r
sgenrati  (y dhe  hot fsytedma(n.e.,tnt asn
*	tardware /firmare ),mhe driver tusestehs trnction to kgt tach rbacon

* @oamesarom tmc80211 t--settfsfrqponssile. or kallbng.whes trnction  exat y)
* @oce oetfre ftenobacon
cisboed   ((e.g. bse.dan
casdware fnterfrupt). * 
* @Te driver tfsfrqponssile. or kfreeng.whed eqhrneedaskb. * 
* @Rqhrne: Ta ibacon
ctemplted.a%UMLLoo werror. *
/
truct isk_eufe *eee80211_ceacon
_get_tim(truct ieee80211_chw *hw,
					Itruct ieee80211_cvif *vif,
					su16 *tim_offse ,su16 *tim_le gh );

**
 * seee80211_ceacon
_get -beacon
cgenration.bfnction 
*	 @hw:uponterfiobains.dolom ttee80211_casowcchw(). * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  * @Seereee80211_ceacon
_get_tim(). * 
* @Rqhrne: Seereee80211_ceacon
_get_tim(). * /
trtioc inlne  truct isk_eufe *eee80211_ceacon
_get(truct ieee80211_chw *hw,
						Isstruct ieee80211_cvif *vif)
{
	aqhrne eee80211_ceacon
_get_tim(hw,svif,(UMLL,(UMLL);
}
/**
 * seee80211_ccsa_pdate _omuneri -Qaquesteauc80211 wo pdecreent the drsatomuneri * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  * @Te drsatomuneriteould ae asdate  (ater tach rbacon
ohansmission .
*	 Tens rnction rs acllbd tfpliicily shen  *	 eee80211_ceacon
_get/eee80211_ceacon
_get_tim ar acllbd   hoerer  ifdhi  *	dbacon
cfamessc r
sgenrati  (y dhe  rviics,the driver teould aoll tehs 
* @onction rater tach rbacon
ohansmission beottyncauc80211 'sdrsatomuneris. * 
* @Rqhrne: ew fasatomuneritalue  * /
u8seee80211_ccsa_pdate _omuneri(truct ieee80211_cvif *vif);

**
 * seee80211_ccsa_fnitsh -Qotifiy@ac80211 wllut wwannel/sewith  * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  * @Ater ta uannel/sewith fnnemunceent twa_ sh d ul.dasd mhe tomuneritnsuheis
*	tanemunceent thit r1,tehs trnction tmst be acllbd ty dhe Rriver th 
* @otifiy@ac80211 what she waangel/@un be acanged

 * /
oid aeee80211_ccsa_fnitsh(truct ieee80211_cvif *vif);

**
 * seee80211_ccsa_is_onpliet  - fid mut wif omunerisdeqch eda1 * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  * @Tens rnction raqhrne awether the waangel/@ewith fomunerisdeqch edazero
 * /
bol aeee80211_ccsa_is_onpliet (truct ieee80211_cvif *vif);


**
 * seee80211_cproberqpo_get -bre iievekddProbe@Rqponssettemplted
* @@hw:uponterfiobains.dolom ttee80211_casowcchw(). * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  * @Ceatoe a  Probe@Rqponssettempltedpiich aan ,brr
taxaplie,ae asdloadd
cho
*	tardware .ITe westeinteon tadress seould ae aet ty dhe Rcllbd . * 
* @Cn orny ue ccllbd tet@AP ode). * 
* @Rqhrne: Ta iProbe@Rqponssettemplted.a%UMLLoo werror. *
/
truct isk_eufe *eee80211_cproberqpo_get(truct ieee80211_chw *hw,
					truct ieee80211_cvif *vif);

**
 * seee80211_cpspoll_get -bre iievekddPS Pollctemplted
* @@hw:uponterfiobains.dolom ttee80211_casowcchw(). * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  * @Ceatoe a  PS Pollcattempltedpiich aan ,brr
taxaplie,asdloadd
cho
*	tardware .ITe wtempltedpmst be asdate  (ater tssociateon/Lsowhat sarrecc  * @AID,cBSSIDtsd mMACsadress sfsfsed
. *	
* @ote : Cllbdr (r aardware )tfsfrqponssile. or ksettng dhi  *	d&EEE80211_TFCTL_P bbit. * 
* @Rqhrne: Ta iPS Pollctemplted.a%UMLLoo werror. *
/
truct isk_eufe *eee80211_cpspoll_get(truct ieee80211_chw *hw,
				IssIstruct ieee80211_cvif *vif);

**
 * seee80211_cnullrnct_get -bre iievekddnullrnctctemplted
* @@hw:uponterfiobains.dolom ttee80211_casowcchw(). * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  * @Ceatoe a  Nullrnctctempltedpiich aan ,brr
taxaplie,asdloadd
cho
*	tardware .ITe wtempltedpmst be asdate  (ater tssociateon/Lsowhat sarrecc  * @BSSIDtsd madress sfsfsed
. *	
* @ote : Cllbdr (r aardware )tfsfrqponssile. or ksettng dhi  *	d&EEE80211_TFCTL_P bbitca awellcas Dreteonatsd mSquesce oCntrol ailel
 . * 
* @Rqhrne: Ta inullrnctctemplted.a%UMLLoo werror. *
/
truct isk_eufe *eee80211_cnullrnct_get(truct ieee80211_chw *hw,
				IssIssstruct ieee80211_cvif *vif);

**
 * seee80211_cproberqq_get -bre iievekddProbe@Rquesteatemplted
* @@hw:uponterfiobains.dolom ttee80211_casowcchw(). * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  @siod: SSIDtbufera *  @siod_len:tle gh tnf SSID * @@tailrom : tailrom (o kaettrvekdt en anf SKB or kIEs *  * @Ceatoe a  Probe@Rquesteatempltedpiich aan ,brr
taxaplie,ae asdloadd
cho
*	tardware . * 
* @Rqhrne: Ta iProbe@Rquesteatemplted.a%UMLLoo werror. *
/
truct isk_eufe *eee80211_cproberqq_get(truct ieee80211_chw *hw,
				IssIssstruct ieee80211_cvif *vif,
				IssIsssonsstsu8b*siod, izeo_t siod_len,
				IssIssstzeo_t tailrom );

**
 * seee80211_cats_get -bRTS@oamesagenration.bfnction 
*	 @hw:uponterfiobains.dolom ttee80211_casowcchw(). * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  @oames:aponterfio tha ifamesotat aissiong dh de cprtectid ty dhe RRTS. *  @oames_len:tha ifamesole gh t(nsuotidts). * @@oames_txctl: itruct ieee80211_ctxsnsor f ahee oames. * @@rss:mTe bbufera were'ct dstre fhe RRTScfames. *

* @nfche dRTScfamessc r
sgenrati  (y dhe  hot fsytedma(n.e.,tnt asn
*	tardware /firmare ),mhe dlow-lvenldriver tusestehs trnction to krqoive 
*	 he dnxx  RTS@oamesalom the d82.11-fcde .ITe wlow-lvenldfsfrqponssile.
*	 or aallbng.whes trnction  etfre fnyd RTS@oamesaisboed   
 * /
oid aeee80211_cats_get(truct ieee80211_chw *hw,struct ieee80211_cvif *vif,
		  sssssonsstsoid a*oames, izeo_t oames_len,
		  sssssonsststruct ieee80211_btx_nsor *oames_txctl,
		  ssssstruct ieee80211_ct_ a*rts)

/**
 * seee80211_crts_dretion  -bGt the Rdretion  ilel
 or aln RTS@oames
*	 @hw:uponterfiobains.dolom ttee80211_casowcchw(). * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  @oames_len:tha ile gh tnf ha ifamesotat aissiong dh de cprtectid ty dhe RRTS. *  @oames_txctl: itruct ieee80211_ctxsnsor f ahee oames. * 
* @nfche dRTScissienrati  (i  ilrmare , ef toe  hot fsytedmamst b ipvid 
*	 he ddretion  ilel
,mhe dlow-lvenldriver tusestehs trnction to krqoive 
*	 he ddretion  ilel
 alue onn little-esdnn beyteordr . * 
* @Rqhrne: Ta idretion 
 * /
__le16seee80211_crts_dretion (truct ieee80211_chw *hw,
			IIIIIstruct ieee80211_cvif *vif, izeo_t oames_len,
			 sssssonsststruct ieee80211_btx_nsor *oames_txctl);

**
 * seee80211_cctstrelf._get -bCTS-to-elf.@oamesagenration.bfnction 
*	 @hw:uponterfiobains.dolom ttee80211_casowcchw(). * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  @oames:aponterfio tha ifamesotat aissiong dh de cprtectid ty dhe RCTS-to-elf.. *  @oames_len:tha ifamesole gh t(nsuotidts). * @@oames_txctl: itruct ieee80211_ctxsnsor f ahee oames. * @@css:mTe bbufera were'ct dstre fhe RCTS-to-elf.@oames. * 
* @nfche dCTS-to-elf.@oamessc r
sgenrati  (y dhe  hot fsytedma(n.e.,tnt asn
*	tardware /firmare ),mhe dlow-lvenldriver tusestehs trnction to krqoive 
*	 he dnxx  CTS-to-elf.@oamesalom the d82.11-fcde .ITe wlow-lvenldfsfrqponssile.
*	 or aallbng.whes trnction  etfre fnyd CTS-to-elf.@oamesaisboed   
 * /
oid aeee80211_cctstrelf._get(truct ieee80211_chw *hw,
			IIIIItruct ieee80211_cvif *vif,
			IIIIIonsstsoid a*oames, izeo_t oames_len,
			sssssonsststruct ieee80211_btx_nsor *oames_txctl,
			IssIstruct ieee80211_co_ a*cts)

/**
 * seee80211_cctstrelf._dretion  -bGt the Rdretion  ilel
 or al CTS-to-elf.@oames
*	 @hw:uponterfiobains.dolom ttee80211_casowcchw(). * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  @oames_len:tha ile gh tnf ha ifamesotat aissiong dh de cprtectid ty dhe RCTS-to-elf.. *  @oames_txctl: itruct ieee80211_ctxsnsor f ahee oames. * 
* @nfche dCTS-to-elf.@issienrati  (i  ilrmare , ef toe  hot fsytedmamst b ipvid 
*	 he ddretion  ilel
,mhe dlow-lvenldriver tusestehs trnction to krqoive 
*	 he ddretion  ilel
 alue onn little-esdnn beyteordr . * 
* @Rqhrne: Ta idretion 
 * /
__le16seee80211_cctstrelf._dretion (truct ieee80211_chw *hw,
				IssItruct ieee80211_cvif *vif,
				IssIizeo_t oames_len,
				ssssonsststruct ieee80211_btx_nsor *oames_txctl);

**
 * seee80211_cienraicciames_dretion  -bCalculte ttenodretion  ilel
 or al oames
*	 @hw:uponterfiobains.dolom ttee80211_casowcchw(). * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  @bnyd:uhe bbsd mh dallculte ttenofamesodretion  n 
*	 @oames_len:tha ile gh tnf ha ifames. * @@ratd:uhe bagt Ot awich ahie@oamesaisbiong dh de ctansmistt.d. *

* @Cnlculte ttenodretion  ilel
 nf pme vienraic oames, ivesn i s * @le gh tsd mhansmission bagt O(nsu100kbps). * 
* @Rqhrne: Ta idretion 
 * /
__le16seee80211_cienraicciames_dretion (truct ieee80211_chw *hw,
					truct ieee80211_cvif *vif,
					num meee80211_cbsd mbsd ,
					tzeo_t oames_len,
					truct ieee80211_cagt O*raed);

**
 * seee80211_cgctbeuferaed_bc -bacessang cbfferad (broadast @sd malticast @oames 
*	 @hw:uponterfiasiobains.dolom ttee80211_casowcchw(). * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  * @Fnction tor a cessang cbfferad (broadast @sd malticast @oames .@nf
*	tardware /firmare toesntnt asplieent fyfferadngsnf broadast /alticast 
  @oames  wen spwers ssvog. fsfsed
,m82.11-fcde fyfferastte mtnsuhe bhot 
  @memor .ITe wlow-lvenldriver tusestehs trnction to kfetchdnxx  bfferad 
* @oames
Inttmot fulses,tehs tfsfsed
 wen sgenratiog cbtcon
cfames. *

* @Rqhrne: Aaponterfio tha iewx  bfferad askbtr tUMLLonfte amre tbfferad 
* @oames  re tavailales. *	
* @ote : bfferad (fames are faqhrneedarny uater tTIM bbtcon
cfamestwa_
* @ienrati  (hth teee80211_ceacon
_get()asd mhe dlow-lvenldriver tmst bthu_
* @oll teee80211_ceacon
_get()afirst.seee80211_cgctbeuferaed_bc()aaqhrne 
* @oMLLonfthe dprevioussienrati  (btcon
cwa_ nt aTIM ,Lsowha dlow-lvenldriver 
* @oesntnt aoed (o kchecktor aTIM bbtcon
ssseasagt lyfsd meould ae aalesbeo
* @sedcommaonfcde for a l bbtcon
s. *
/
truct isk_eufe *
eee80211_cgctbeuferaed_bc(truct ieee80211_chw *hw,struct ieee80211_cvif *vif);

**
 * seee80211_cgctbtkip_p1k_ivs-kgt ta TKIP phlsei1 key or kIV32
*  * @Tens rnction raqhrne ahe dIKIP phlsei1 key or khe divesn IV32
 *
 * @@keyonfi:uhe basagmeer trasi  (hth the bet tkey * @@iv32: IV32fo kgt the RP1Kcrr

* @@p1k:ta bufera o kwich ahie@key ill bb cwrstt.n,iasi5su16 alue   * /
oid aeee80211_cgctbtkip_p1k_iv(truct ieee80211_ckey_ont t*keyonfi,
			IIIIIssu32riv32,su16 *p1k);

**
 * seee80211_cgctbtkip_p1ks-kgt ta TKIP phlsei1 key
*  * @Tens rnction raqhrne ahe dIKIP phlsei1 key or khe dIV32foake

* @oam the divesn askets
 *
 * @@keyonfi:uhe basagmeer trasi  (hth the bet tkey * @@ikb:uhe basketsbtofoakekhe dIV32falue ooam thet awil  b cencryptd 
* 	hth thens P1K
* @@p1k:ta bufera o kwich ahie@key ill bb cwrstt.n,iasi5su16 alue   * /
trtioc inlne  oid aeee80211_cgctbtkip_p1k(truct ieee80211_ckey_ont t*keyonfi,
					IItruct isk_eufe *skb,su16 *p1k)
{
	truct ieee80211_chdr *hdr = (truct ieee80211_chdr *)skb->dta ;
	onsstsu8b*dta f= (u8b*)hdr +ieee80211_chdrlen(hdr->oames_cntrol );
	u32riv32f= gctbunalgned _le32(&dta [4]);

	eee80211_cgctbtkip_p1k_iv(keyonfi,riv32,sp1k);
}
/**
 * seee80211_cgctbtkip_rx_p1ks-kgt ta TKIP phlsei1 key or kRX
*  * @Tens rnction raqhrne ahe dIKIP phlsei1 key or khe divesn IV32
 *tsd mhansmisterfiadress 
 *
 * @@keyonfi:uhe basagmeer trasi  (hth the bet tkey * @@a : TAthet awil  b cui  (hth the bkey * @@iv32: IV32fo kgt the RP1Kcrr

* @@p1k:ta bufera o kwich ahie@key ill bb cwrstt.n,iasi5su16 alue   * /
oid aeee80211_cgctbtkip_rx_p1k(truct ieee80211_ckey_ont t*keyonfi,
			IIIIIssonsstsu8b*ta,su32riv32,su16 *p1k);

**
 * seee80211_cgctbtkip_p2ks-kgt ta TKIP phlsei2 key
*  * @Tens rnction ronplue sahe dIKIP RC4 key or khe dIV alue   * (i whe haskets
 *
 * @@keyonfi:uhe basagmeer trasi  (hth the bet tkey * @@ikb:uhe basketsbtofoakekhe dIV32/IV16 alue  ooam thet awil  b 
* 	encryptd  hth thens key * @@p2k:ta bufera o kwich ahie@key ill bb cwrstt.n,i16 bytes * /
oid aeee80211_cgctbtkip_p2k(truct ieee80211_ckey_ont t*keyonfi,
			IIIItruct isk_eufe *skb,su8b*p2k);

**
 * seee80211_caes_cmac_allculte _k_ck2 - cnlculte ttenoAES-CMACssub@keys
*  * @Tens rnction ronplue sahe dtwooAES-CMACssub-keys, bse.dan
cte 
*	 previously nntrllbd tmatedrskey
 *
 * @@keyonfi:uhe basagmeer trasi  (hth the bet tkey * @@k1:ta bufera o kb cfil   (hth the b1ststub-key * @@k2:ta bufera o kb cfil   (hth the b2d meub-key * /
oid aeee80211_caes_cmac_allculte _k_ck2(truct ieee80211_ckey_ont t*keyonfi,
					u8b*k1,su8b*k2);

**
 * struct ieee80211_bkey_seq - key squesce oomuneri *  * @@akip:dIKIP dta  bontainsog cIV32fsd mIV16 i whot fbytecordr  * @@ccmp:dPN dta  bmot figneifcasn fbytecfirst (big esdnn   *	arrer secordr thet ti/Laskets)
* @@aes_cmac:dPN dta  bmot figneifcasn fbytecfirst (big esdnn   *	arrer secordr thet ti/Laskets)
* /
truct ieee80211_bkey_seq {
	unon r{
		truct i{
			u32riv32;
			u16 iv16;
		} akip;
		truct i{
			u8 pn[6];
		} ccmp;
		truct i{
			u8 pn[6];
		} aes_cmac;
	};
;

/**
 * seee80211_cgctbkey_tx_eeq - gt tkeysX ssquesce oomuneri *  * @@keyonfi:uhe basagmeer trasi  (hth the bet tkey * @@ieq: bufera o kaqeive  he dsquesce odta  *  * @Tens rnction rasowwsLa eiver to kaq iievekhe Rcrrentl X sIV/PN
*	 or ahe divesn key
 I amst beo ee ccllbd tefdIV genration.bis
*	toffloadd
chottenodvice_.
*	
* Tote what shei crnction tacyrrny ue ccllbd twen se aX soroessang  *	tan be adne ianncrrently ,brr
taxaplietwen suesus are feropped
 *tsd mhh feropaas teen stynchroitz d
 * /
oid aeee80211_cgctbkey_tx_eeq(truct ieee80211_ckey_ont t*keyonfi,
			IIIIIstruct ieee80211_bkey_seq *seq);

**
 * seee80211_cgctbkey_rx_eeq - gt tkeysR ssquesce oomuneri *  * @@keyonfi:uhe basagmeer trasi  (hth the bet tkey * @@tid:uTe wTID,cr
t-1 or ahe dmanageent ffamestalue o(CCMPrrny );
* phhstalue oon TIDk0rfsrass dui  (fr tntn-QoS@oames .@Fr

* 	CMAC,rrny uTIDk0rfsralud . * @@ieq: bufera o kaqeive  he dsquesce odta  *  * @Tens rnction rasowwsLa eiver to kaq iievekhe Rcrrentl R sIV/PNs
*	 or ahe divesn key
 I amst beo ee ccllbd tefdIV checkog. fsfdn   *	(y dhe  rviicstsd meo eey@ac80211 .
*	
* Tote what shei crnction tacyrrny ue ccllbd twen se aR soroessang  *	tan be adne ianncrrently 
 * /
oid aeee80211_cgctbkey_rx_eeq(truct ieee80211_ckey_ont t*keyonfi,
			IIIIIsntemhod, iruct ieee80211_bkey_seq *seq);

**
 * seee80211_csctbkey_tx_eeq - st tkeysX ssquesce oomuneri *  * @@keyonfi:uhe basagmeer trasi  (hth the bet tkey * @@ieq: ew fsquesce odta  *  * @Tens rnction rasowwsLa eiver to ket the bcrrentl X sIV/PNsblr thi  *	(ivesn key
 Ths tfsfsedfultwen sessumog. oam tWoWLAN seep fsd mhh 
* @oviicstacyrave ttinsmit td (fames ausng.whed PTK, e.g. repliosbeo
* @ARPQaquestes.
*	
* Tote what shei crnction tacyrrny ue ccllbd twen se aX soroessang  *	tan be adne ianncrrently . *
/
oid aeee80211_csctbkey_tx_eeq(truct ieee80211_ckey_ont t*keyonfi,
			IIIIIstruct ieee80211_bkey_seq *seq);

**
 * seee80211_csctbkey_rx_eeq - st tkeysR ssquesce oomuneri *  * @@keyonfi:uhe basagmeer trasi  (hth the bet tkey * @@tid:uTe wTID,cr
t-1 or ahe dmanageent ffamestalue o(CCMPrrny );
* phhstalue oon TIDk0rfsrass dui  (fr tntn-QoS@oames .@Fr

* 	CMAC,rrny uTIDk0rfsralud . * @@ieq: ew fsquesce odta  *  * @Tens rnction rasowwsLa eiver to ket the bcrrentl R sIV/PNsblr thi  *	(ivesn key
 Ths tfsfsedfultwen sessumog. oam tWoWLAN seep fsd mGTK
* @rekeysacyrave teen sdne iwicleLerspendd

 I aeould aeo ee ccllbd  * sefdIV checkog. fsfdn  (y dhe  rviicstsd meo eey@ac80211 .
*	
* Tote what shei crnction tacyrrny ue ccllbd twen se aR soroessang  *	tan be adne ianncrrently 
 * /
oid aeee80211_csctbkey_rx_eeq(truct ieee80211_ckey_ont t*keyonfi,
			IIIIIsntemhod, iruct ieee80211_bkey_seq *seq);

**
 * seee80211_cremovebkey -bremoveahe divesn key * @@keyonfi:uhe basagmeer trasi  (hth the bet tkey * 
* @Rqmoveahe divesn key.@nfche dkey iasasdloadd
chouhe bhsdware ft she 
* @hiesbtens rnction rs acllbd ,settfsfeo edeiet  (i whe hhsdware fbut
*	 nntrad assoum dth bare teen sremovedulsaad y.
*	
* Tote what sdu'ct dlockng sonssidration.stehs trnction tan b(crrently )
* @fny ue ccllbd tduadngskey itration.b(eee80211_citrabkeys().) * /
oid aeee80211_cremovebkey(truct ieee80211_ckey_ont t*keyonfi);

**
 * seee80211_cgtk_rekey_ddr - ddr amGTK key orm taekeydngsduadngsWoWLAN
* @@vif: hhstairtual nterface wo kdddche dkey n 
*	 @keyonfi:uew fkey dta  *  * @Wen sGTK aekeydngsiasadne iwicleLhe beytedmawa_ srspendd
, (a)uew  * @key(t) ill bb cavailales.uTe se ill bb coed   (byimc80211 tfortoropr 
* TR soroessang ,Lsowhans rnction rasowwsLsettng dhi m.
*	
* TTe wrnction raqhrne aha ieewlyfssowcte  ykey sruct ue , wich awil  *	bare tsimilarRunfext sbhouhe brasi  (key onfiguretion.bef tpontemho
* @mc80211 -rwnd
fmemor .IIttulsebofterrors,mhe drnction raqhrne aa 
*	 ERR_PTR(),bsedcIS_ERR() etc.
*	
* Tote what shei crnction tssoum saha dkey in't eddd dth bardware  *   ceslration.,Lsowe aX sill bb cdne iwth the bkey. Snce oit's amGTK
* @fndmanagedi(staeon/)snetworks,tehs tfsfrucstsdyway.@nfche driver 
* @cll sioei crnction tfrm the dessum wallbbnk fsd meubsquescly suses
*	 he daqhrne cde f1 o kaqenfigureddhe  rviics,thens keysill bb cpat_ *
tnf ha iaqenfiguretion 
 * 
* Tote what she driver teould aass doll teee80211_csctbkey_rx_eeq()
*	 or ahe dew fkey rr
tach rTID h tet tupssquesce oomuneristoropr ly.
*	
* TIMPORTANT:@nfchefsfrqplace a  key tat aisspaettn ti whe hhsdware ,
*	 he n i sill battempt o kaqmoveaitsduadngsraisrullb
Inttmsdyfulses
*	 hes tfs't ewat aou awant,Lsowoll teee80211_cremovebkey()afirstcrr

* @ha dkey tat ' teedngsrqplaced. *
/
truct ieee80211_bkey_ont t*
eee80211_cgtk_rekey_ddr(truct ieee80211_cvif *vif,
			truct ieee80211_ckey_ont t*keyonfi);

**
 * seee80211_cgtk_rekey_otifiy@-Qotifiy@userspce  hupolcasn ff srekeydng
* @@vif: airtual nterface woa iaqkeydngsiasadne in 
*	 @bsiod: Te wBSSIDtnf ha iAP,brr
tcheckog. ssociateon/
*	 @rqplay_otr:ahe dew frqplaytomuneritater tGTK aekeydng
*	 @gfp:trwowcteon tlags 
*	/
oid aeee80211_cgtk_rekey_otifiy(truct ieee80211_cvif *vif,sonsstsu8b*bsiod,
				onsstsu8b*rqplay_otr, gfp_t gfp);

**
 * seee80211_cwcke_uesus@-QwckeLepecifcasuesus
*	 @hw:uponterfiasiobains.dolom ttee80211_casowcchw(). * @@uesus:suesusmember o(omunerdolom tzero). * 
* @Diver sbeould asedchensrrnction tnntrad anf oetifcwcke_uesus
 * /
oid aeee80211_cwcke_uesus(truct ieee80211_chw *hw,sntemuesus);

**
 * seee80211_cstop_uesus@-Qeropaepecifcasuesus
*	 @hw:uponterfiasiobains.dolom ttee80211_casowcchw(). * @@uesus:suesusmember o(omunerdolom tzero). * 
* @Diver sbeould asedchensrrnction tnntrad anf oetifcstop_uesus
 * /
oid aeee80211_cstop_uesus(truct ieee80211_chw *hw,sntemuesus);

**
 * seee80211_cuesus_eroppedt-Aoes  eatus tnf ha iuesus
*	 @hw:uponterfiasiobains.dolom ttee80211_casowcchw(). * @@uesus:suesusmember o(omunerdolom tzero). * 
* @Diver sbeould asedchensrrnction tnntrad anf oetifcstop_uesus
 * 
* @Rqhrne: %rucstif ha iuesusbs utropped. %fassernter wiss
 * /

sn tiee80211_cuesus_eropped(truct ieee80211_chw *hw,sntemuesus);

**
 * seee80211_cstop_uesuss@-Qeropall tuesuss
*	 @hw:uponterfiasiobains.dolom ttee80211_casowcchw(). * 
* @Diver sbeould asedchensrrnction tnntrad anf oetifcstop_uesus
 * /
oid aeee80211_cstop_uesus (truct ieee80211_chw *hw);

**
 * seee80211_cwcke_uesuss@-QwckeLll tuesuss
*	 @hw:uponterfiasiobains.dolom ttee80211_casowcchw(). * 
* @Diver sbeould asedchensrrnction tnntrad anf oetifcwcke_uesus
 * /
oid aeee80211_cwcke_uesus (truct ieee80211_chw *hw);

**
 * seee80211_csasn_onpliet dt-Aonpliet dthsdware fsasn *  * @Wen shsdware fsasntoffloadtfsfsed
 (n.e.whe hhwcsasn()aallbbnk fis
*	tasigned )chensrrnction tueds to de ccllbd ty dhe Rriver th Qotifiy
* @mc80211 what she dsasntfnitshed. Ths trnction tan be ccllbd tlom 
 *tsdyRunfexx ,wn ludeng chsdwirqfunfexx . * 
* @Ihw:uhe bardware foat sfnitshedshe dsasn
* @Iabot_d
:ket tho rucstif sasntwa_ rbot_d
 * /
oid aeee80211_csasn_onpliet d(truct ieee80211_chw *hw,sbol arbot_d
);

**
 * seee80211_csahedcsasn_essul s -bgo eassul s lom tsh d ul.dasasn *  * @Wen satsh d ul.dasasndfsfrunnng ,Lhensrrnction tueds to de ccllbd ty dhe 
* @oiver tien rer  te r are,4ew fsasndassul s availales. *	
* @Ihw:uhe bardware foat sns pr ormang tph d ul.dasasn 
*	/
oid aeee80211_csahedcsasn_essul s(truct ieee80211_chw *hw);

**
 * seee80211_csahedcsasn_eroppedt-An orma@hat she dsa d ul.dasasndas teropped
 * * @Wen satsh d ul.dasasndfsfrunnng ,Lhensrrnction tan be ccllbd te  *	the Rriver tf itttueds to deropahe dsasnthoa r ormatsdnter dtask. * @Usual scenario are friver sbrat tan e tTonfeinueshe dsa d ul.dasasn * @wicleLssociateog ,Lfortintrlncs. *	
* @Ihw:uhe bardware foat sns pr ormang tph d ul.dasasn 
*	/
oid aeee80211_csahedcsasn_eropped(truct ieee80211_chw *hw);

**
 * snum meee80211_cnterface _itration._lags t-An erface witration.blags 
*	 @EEE80211_TIFACE_ITER_NORMAL:@ntratierner tll te erface sbrat tare 
* 	een sddd dth bhe Rriver ; Hwerer ,tnte what sdurng chsdware  * 	aqenfiguretion  (ater taqptat__hw) i sill bitratierner tluew  * 	n erface wsd mner tll the Rexietlg. inerface sbeesn if ha   *		are 't eeen sre-ddd dth bhe Rriver  yet. *
 @EEE80211_TIFACE_ITER_RESUME_ALL:@Durng cessum ,bitratierner tll  *		inerface s,beesn if ha   are 't eeen sre-ddd dth bhe Rriver  yet. *
/
num meee80211_cnterface _itration._lags t{
	EEE80211_TIFACE_ITER_NORMAL	= 0,
	EEE80211_TIFACE_ITER_RESUME_ALL	= BIT(0),
;

/**
 * seee80211_citratie_atiovebinerface sb-bitratieratiove inerface s *  * @Tens rnction ritratiesrner the Rinerface sbssociate  (hth t  ivesn *	bardware foat sar acrrently satiove sd moll sioe wallbbnk for ahe m.
*	 Tens rnction rasowwsLhe Ritratir kfnction to kseep ,twen shiebitratir 
*	 onction rs atirmcas@eee80211_citratie_atiovebinerface s_tirmcasasn * @b cui  .
*	 Desntnt astratierner tluew An erface wdurng cadd_nterface (). * 
* @Ihw:uhe bardware ftruct inf wich ahie@inerface sbeould ae aitrati  (oer 
* @@itrablags :witration.blags ,kete &num meee80211_cnterface _itration._lags 
* @@itratir :Lhe Ritratir kfnction to kullb *
 @dta :afirstcarguent fnf ha iitratir kfnction 
*	/
oid aeee80211_citratie_atiovebinerface s(truct ieee80211_chw *hw,
					Iu32ritrablags ,
					Ioid a(*itratir )(oid a*dta  bu8b*mc8,
						truct ieee80211_cvif *vif),
					Ioid a*dta );

**
 * seee80211_citratie_atiovebinerface s_tirmcas-bitratieratiove inerface s *  * @Tens rnction ritratiesrner the Rinerface sbssociate  (hth t  ivesn *	bardware foat sar acrrently satiove sd moll sioe wallbbnk for ahe m.
*	 Tens rnction requiredsLhe Ritratir kallbbnk fonction to kb cairmca,
*	 if haattfsfeo edesred.,bsedc@eee80211_citratie_atiovebinerface stnntrad .
*	 Desntnt astratierner tluew An erface wdurng cadd_nterface (). * 
* @Ihw:uhe bardware ftruct inf wich ahie@inerface sbeould ae aitrati  (oer 
* @@itrablags :witration.blags ,kete &num meee80211_cnterface _itration._lags 
* @@itratir :Lhe Ritratir kfnction to kullb,tan e tTseep  *
 @dta :afirstcarguent fnf ha iitratir kfnction 
*	/
oid aeee80211_citratie_atiovebinerface s_tirmca(truct ieee80211_chw *hw,
						u32ritrablags ,
						oid a(*itratir )(oid a*dta  
						Issbu8b*mc8,
						Issstruct ieee80211_cvif *vif),
						oid a*dta );

**
 * seee80211_citratie_atiovebinerface s_rtnls-bitratieratiove inerface s *  * @Tens rnction ritratiesrner the Rinerface sbssociate  (hth t  ivesn *	bardware foat sar acrrently satiove sd moll sioe wallbbnk for ahe m.
*	 Tens er son tan bfny ue csed
 wecleLholeng che  RTNL. * 
* @Ihw:uhe bardware ftruct inf wich ahie@inerface sbeould ae aitrati  (oer 
* @@itrablags :witration.blags ,kete &num meee80211_cnterface _itration._lags 
* @@itratir :Lhe Ritratir kfnction to kullb,tan e tTseep  *
 @dta :afirstcarguent fnf ha iitratir kfnction 
*	/
oid aeee80211_citratie_atiovebinerface s_rtnl(truct ieee80211_chw *hw,
					IIIIssu32ritrablags ,
					IIIIIIoid a(*itratir )(oid a*dta  
						u8b*mc8,
						truct ieee80211_cvif *vif),
					IIIIIIoid a*dta );

**
 * seee80211_cuesus_work - ddr work ono tha iuc80211 wworkuesus
*	
* @Diver sbsd mac80211 wsedchensro kdddcwork ono tha iuc80211 wworkuesus.
*	 Tens helpr ceneredsfriver sbre,4eoemuesusdngsiork wen shieyaeould aeo ee . * 
* @Ihw:uhe bardware ftruct ior ahe dn erface ww are,4ddddngsiork rr

* @@iork:uhe biork weawantro kdddcono tha iuc80211 wworkuesus
*	/
oid aeee80211_cuesus_work(truct ieee80211_chw *hw,struct iwork_truct i*work);

**
 * seee80211_cuesus_delayedcwork - ddr work ono tha iuc80211 wworkuesus
*	
* @Diver sbsd mac80211 wsedchensro kuesusbdelayed work ono tha iuc80211  * @workuesus.
*	
* @Ihw:uhe bardware ftruct ior ahe dn erface ww are,4ddddngsiork rr

* @@diork:udelayalesbiork o kuesusbono tha iuc80211 wworkuesus
*	 @evlay:member of sjiffiosbeoawait etfre fuesusdng
*	/
oid aeee80211_cuesus_delayedcwork(truct ieee80211_chw *hw,
				Istruct idelayedcwork *diork,
				Isunigned hloneidelay);

**
 * seee80211_cstartbtxbba_scsion b- Siar sabtxdBlock Ack scsion . * @@ia : he detaeon tor awich ahobsiar sa BA scsion 
* @@tid:utherTID h tBA n . * @@hiesou_:bscsion bhiesou_talue o(in TUs) *	
* @Rqhrne: ercessa if dddBA aquesteawa_ sent,Lfailreddnter wiss *	
* @Alh utghiuc80211 /oww lvenldriver /user spce  applicbion tan bsteimted
* @he dew dth bsiar sagregateon/@fnda cerainsuRA/TID,che betsion blvenl * @wll bb cmanagediy dhe Rac80211 .
*	/
sn tiee80211_cstartbtxbba_scsion (truct ieee80211_csta *sta,su16 tid,
				Isu16 timsou_);

**
 * seee80211_cstartbtxbba_cb_irqsafst-Aoww lvenldriver  aad yAo pagregatee. * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk  * @@r : rqoive roadress snf ha iBA scsion  rqoipitnt. *
 @tid:utherTID h tBA n . *  * @Tens rnction rmst be acllbd ty doww lvenldriver  oce oitdas 
*	 onitshedsilh toreasagton.stor ahe dBA scsion 
 I aan be ccllbd 
*	 oom tsdyRunfexx 
 * /
oid aeee80211_cstartbtxbba_cb_irqsafs(truct ieee80211_cvif *vif,sonsstsu8b*ra,
				IssIssu16 tid);

**
 * seee80211_cstop_txbba_scsion b- SiopaldBlock Ack scsion . * @@ia : he detaeon twhos iBA scsion  o derop *
 @tid:utherTID h teropaBA
 *
 * @Rqhrne: ewateove errortif ha iTID s tfnalud ,tr tntsagregateon/@atiove *	
* @Alh utghiuc80211 /oww lvenldriver /user spce  applicbion tan bsteimted
* @he dew dth bsiopalgregateon/@fnda cerainsuRA/TID,che betsion blvenl * @wll bb cmanagediy dhe Rac80211 .
*	/
sn tiee80211_cstop_txbba_scsion (truct ieee80211_csta *sta,su16 tid);

**
 * seee80211_cstop_txbba_cb_irqsafst-Aoww lvenldriver  aad yAo psiopalgregatee. * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk  * @@r : rqoive roadress snf ha iBA scsion  rqoipitnt. *
 @tid:utherdesred.rTID h tBA n . *  * @Tens rnction rmst be acllbd ty doww lvenldriver  oce oitdas 
*	 onitshedsilh toreasagton.stor ahe dBA scsion  radr dow 
 I 
 *tan be ccllbd tlom tsdyRunfexx 
 * /
oid aeee80211_cstop_txbba_cb_irqsafs(truct ieee80211_cvif *vif,sonsstsu8b*ra,
				IssIsu16 tid);

**
 * seee80211_cfid csta - fid matprticn
 *  * s@vif: airtual nterface wo dloo for aetaeon tn 
* @@adre:aetaeon 's adress  *
 * @Rqhrne: Te detaeon ,tif found.a%UMLLooter wiss
 * 
  @ote : Tens rnction rmst be acllbd tundr  RCUdlockfsd mhh 
* @essul dngsponterfins fny ualud aundr  RCUdlockfs awell. *
/
truct ieee80211_bsta *eee80211_cfid csta(truct ieee80211_cvif *vif,
					Ionsstsu8b*adre);

**
 * seee80211_cfid csta_by_ifadre - fid matprticn
an
casdware 
*	
* @Ihw:uponterfiasiobains.dolom ttee80211_casowcchw()
* @@adre:aremoteaetaeon 's adress  *
 @owctladre:aowctloadress s(vif->sdta ->vif.adre). Us iUMLLoor a'sdy'. * 
* @Rqhrne: Ta ietaeon ,tif found.a%UMLLooter wiss
 * 
  @ote : Tens rnction rmst be acllbd tundr  RCUdlockfsd mhh 
* @essul dngsponterfins fny ualud aundr  RCUdlockfs awell. *

  @oOTE: YoutacyrrasiiUMLLoor aowctladre, ef toe naou awll bjst bge 
 *tttttthe drirstcSTAthet amtechdsLhe Rremoteaadress s'adre'. * ttttttWe asndasvemalticlietSTAtssociate  (hth talticlie * ttttttlogictloetaeon s((e.g. onssidramatprticn
acdfneting dh dsdnter  * ttttttBSSIDtn shiebsmestAPbardware fhth ut wdiscdfneting drirst). * ttttttInsraisrulss,the dessul snf haisrmeh u (hth towctladreiUMLL * ttttttfsfeo ereliales. *	
* @DO@oOT USE THIS FUNCTION(hth towctladreiUMLL if dttll tposioles. *
/
truct ieee80211_bsta *eee80211_cfid csta_by_ifadre(truct ieee80211_chw *hw,
					IIIIssIonsstsu8b*adre,
					IIIIssIonsstsu8b*owctladre);

**
 * seee80211_csta_block_awckeL- blockdetaeon toom twckng dup
* @Ihw:uhe bardware 
* @Ipubia : he detaeon 
* @Iblock:awether tho blockdr aunblock *	
* @Sme vrviicssrequiredfoat sal tfames aoat sar an shiebuesuss
*	 or al ipecifcasetaeon thet awet fo kseep fsr aflushedsetfre 
*	 a pollteqponsse r kfrmes arter tra detaeon twok asdtan be 
*	 delver  dth bheattft.Tote what serchkfrmes amst be arejctid  * @b dhe Rriver ts afilteed.,bwth the bapproprate  eatus tlags. *  * @Tens rnction rasowwsLsplieent dngsraisrode)ti wa rce -free
* @mcfne . * 
* @To dosrais,La eiver tmst bkep ftrnk fnf ha iember of soames 
*	 teilltenuesusd or al ipecifcasetaeon .@nfchefsfember ofsfeo 
*	 zerotwen shiebetaeon tgoosbeoaseep ,the Rriver tmst bullb *
 ehs trnction to kforc iuc80211 wo kunssidramhiebetaeon tho
* @b caseep fegatrdlss snf ha ietaeon 's actual etaee. One woa 
* @ember of sut trlndog. oames aeqch es zero,the Rriver tmst 
 *tanl tehs trnction taginsuhoaunblock ha ietaeon . Tht awil  *	tansedcuc80211 wo ke aalesbeo send ps-pollteqponsses,tsd mif
* @he detaeon tuesri  (i whe hmeanhiesbtee
cfamesscill balso
* @b cttn tut waswateqpul snf hais. Addiional y ,the Rriver  * @wll bb cotifiiedshet she dstaeon twok asdtpme vhiesbrter  * settfsfunblockd.,begatrdlss snf wether the wstaeon tactualy)
* @wok asdtwecleLblockd.tr tnt 
 * /
oid aeee80211_csta_block_awcke(truct ieee80211_chw *hw,
			IIIIIsstruct ieee80211_csta *pubia ,sbol ablock);

**
 * seee80211_csta_eosp -Qotifiy@ac80211 wllut wen anf SP
* @Ipubia : he detaeon 
* 
* @Wen satrviicsttinsmit scfamessci wa way tat ai aan 'eatel  *	tac80211 wi whe hX ssatus tllut whe hEOSP,it sast bcladr oa 
* @%EEE80211_TTX_STATUS_EOSPbbitcandtanl tehs trnction tnntrad .
*	 Tei cappliosbor aPS-Pollca awellcas uAPSD
 * 
* Tote what sjst blik iwth tctx_eatus ()asd m_rx()ariver sbmst 
 *teo emixmoll sioo irqsafs/ntn-irqsafster son s,Lhensrrnction  *	tast beo ee cmix  (hth theos ieiher . Us ihe bal birqsafs,tr 
*	 al bntn-irqsafs,adne' emix! * 
* ToB: he d_irqsafster son snf haisrrnction toesne' eexiet,tnt * tttttriver tueds titcrigh beow. Dne' eanl tehs trnction tnf * tttttou 'ddew dthe d_irqsafster son ,dloo ft she dgitdaistrey * tttttsd maqptre fhe R_irqsafster son ! * /
oid aeee80211_csta_eosp(truct ieee80211_csta *pubia );

**
 * seee80211_citrabkeyss-bitratierkeyssprogamem  (i hottenodvice_
* @Ihw:uponterfiobains.dolom ttee80211_casowcchw()
* @@vif: airtual nterface wo ditratie,auy be  %UMLLoor allb *
 @itra:Ritratir kfnction toet awil  b ccllbd tlr
tach rkey * @@itrabdta :acst m tdta fhoa asiihottenoitratir kfnction 
*	 * @Tens rnction ran be csed
 o ditratietll the Rkeysskeow tho
* @ac80211 ,beesn heos ihet awer 't epreviously progamem  (i ho
* @he ddvice_. Ths tfsfnterndd
tlr
tsedci wWoWLAN if ha idvice_
* @ueds treprogamemdngsnf he Rkeyssdurng csrspend.Tote what sdu'
* @h dlockng seqcsn s,Lettfsfass dfny usafsto kullbtehs tt sfw  * @pon scsnce oittast bholeche  RTNLtsd me aalesbeo seep .
*	
* TTe wordr tinkwich ahie@keysbre,4itrati  (mtechdsLhe Rordr  * @inkwich ahieyawer Rorigial y  nntrllbd tsd mhand dth bhe  * @pctbkeywallbbnk . * /
oid aeee80211_citrabkeys(truct ieee80211_chw *hw,
			Itruct ieee80211_cvif *vif,
			Ioid a(*itra)(truct ieee80211_chw *hw,
				IssIsstruct ieee80211_cvif *vif,
				IssIsstruct ieee80211_csta *sta,
				IssIsstruct ieee80211_ckey_ont t*key,
				IssIssoid a*dta ),
			Ioid a*itrabdta );

**
 * seee80211_citrabaang_unfexx s_tirmcas-bitratieraangel/@unfexx s
* @Ihw:uponter Robains.dolom ttee80211_casowcchw(). * @@itra:Ritratir kfnction  * @@itrabdta :adta frasi  (o ditratir kfnction 
*	 * @Itratietll tatiove aangel/@unfexx s. Ths trnction ts atirmcasand
*	 desne' eacuiredfsdyRlocksfnterral y  het amigh be ahel
 nsuoter  * tplace awecleLallbng.wi hottenodiver .
*	
* TTe witratir kwil  eo efid matunfexx  tat ' teedngsddd dt(durng 
* @he ddiver tallbbnk fo kdddcit) ef twil  fid mitawecleLi ' teedng
* @esmoved.
*	
* Tote what sdurng chsdware taqptat_,tll tunfexx swhat sexietd  * @btfre ftenoaqptat_sar acnssidraedulsaad yspaettn ts dwil  b 
*  foundawecleLi ratiog , wether the y'e teen sre-ddd dtlsaad y
*  r tnt 
 * /
oid aeee80211_citrabaang_unfexx s_tirmca(
	truct ieee80211_chw *hw,
	oid a(*itra)(truct ieee80211_chw *hw,
		IssIstruct ieee80211_coangctx_ont t*oangctx_ont ,
		IssIsoid a*dta ),
	oid a*itrabdta );

**
 * seee80211_capcproberqq_get -bre iievekddProbe@Rquesteatemplted
* @@hw:uponterfiobains.dolom ttee80211_casowcchw(). * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  * @Ceatoe a  Probe@Rquesteatempltedpiich aan ,brr
taxaplie,ae asdloadd
cho
*	tardware .ITe wtempltedpiscfil   (hth tbsiod, iiodfsd meuppot_d
bagt  * @inormaaeon . Thns rnction rmst bfny ue ccllbd toom twth i
cte 
*	 .bsi_nsor_canged
kallbbnk fonction tsd mnny  nncmanagedimde .ITe wfnction  * @ns fny usedfultwen she dn erface wisbssociate  ,ooter wiss i sill baqhrne
* @%UMLL. * 
* @Rqhrne: Ta iProbe@Rquesteatemplted.a%UMLLoo werror. *
/
truct isk_eufe *eee80211_capcproberqq_get(truct ieee80211_chw *hw,
					IItruct ieee80211_cvif *vif);

**
 * seee80211_ceacon
_losst-An orma@hsdware toesntnt aaqeive  btcon
s *  * s@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  * @Wen sbtcon
cfilteeog. fsfenales (hth t%EEE80211_TVIF_BEACON_FILTERsand
*	 %EEE80211_TCONF_PScisspct,the Rriver tueds to dn orma@ien rer  te 
*	tardware tfsfeo ereeiveog cbtcon
s hth thens fnction 
 * /
oid aeee80211_ceacon
_loss(truct ieee80211_cvif *vif);

**
 * seee80211_ccdfnetinn
_losst-An orma@hsdware tas tlot fudfnetinn
ihottenoAP *  * s@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  * @Wen sbtcon
cfilteeog. fsfenales (hth t%EEE80211_TVIF_BEACON_FILTER,sand
*	 %EEE80211_TCONF_PScsd m%EEE80211_THWTCONNECTION_MONITORare fect,the Rriver 
* @ueds to dn orma@if ha iudfnetinn
ihottenoAPaas teen slot .
* TTe wrnction tacyrass de ccllbd tefdha iudfnetinn
iueds to de cteeminted 
*	 or aeme vnter deqcsn ,beesn if %EEE80211_THWTCONNECTION_MONITORafs't ests
 *
 * @Thns rnction rill bansedciem  itieraanggsbeo disssociate  (etaee, * @wlh ut wudfnetinn
iaqener ybattempts
 * /
oid aeee80211_ccdfnetinn
_loss(truct ieee80211_cvif *vif);

**
 * seee80211_cessum _discdfnetit-Adiscdfnetitlom tAPaater taqpume *  * s@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  * @Intruct scuc80211 wo kdiscdfnetitlom ttenoAPaater taqpume.
*	 Diver sban bsedchensrater tWoWLAN if ha yskeowshet she  *	tadfnetinn
ian e tTbe@kep tup,brr
taxaplietbeansedckeysbwer  *	tsed
 wecleLha idvice_twa_ rseep fef toe  rqplaytomuneristr 
*	 similarRun e tTbe@re iievedtlom ttenodvice_tdurng cessum .
*	
* Tote what sdu'ct dsplieent tion.bissues,tifche driver suses
*	 he daqenfiguretion  rnction alitytdurng cessum she dn erface  * @wll bteilltbesddd dtasbssociate  (rirstcdurng cessum ssd mhh e
* @discdfnetitnrmaa y  ltedr
 *
 * @Thns rnction ran bfny ue ccllbd toom the dessum wallbbnk fsd 
* @he ddiver tast beo ee choleng csdyRo ittstrwnRlocksfwecleLi 
 *tanl sioei crnction ,tr tt sleat beo esdyRlocksfnttueds ti
cte 
*	 key onfiguretion.bpaths (f ittteuppot_s HW crypto). * /
oid aeee80211_cresum _discdfneti(truct ieee80211_cvif *vif);

**
 * seee80211_ccqm_riio_otifiy@-Qn orma@a onfigured
kadfnetinn
iqualitytmoniir dng
* 	riiocteresholechriggrad 
*  * s@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  @riio_eesnt: he dRSSIchriggrabeesn toypd
* @@gfp:tunfexx  lags 
*  * @Wen she d%EEE80211_TVIF_SUPPORTS_CQM_RSSIcisspct,tad matunfnetinn
iquality
* @aoniir dngrs acnfigured
khth t 
iaiioctereshole,the Rriver till bi orma * @wen rer  te iaiioclvenldeqch es te itereshole
 * /
oid aeee80211_ccqm_riio_otifiy(truct ieee80211_cvif *vif,
			IIIIIssnum mnl0211_ccqm_riio_tereshole_eesnt riio_eesnt,
			IIIIIssgfp_t gfp);

**
 * seee80211_cradaabdeectid t-An orma@hat sa rcdaasiasadeectid 
*	
* @Ihw:uponterfiasiobains.dolom ttee80211_casowcchw()
* /
oid aeee80211_cradaabdeectid (truct ieee80211_chw *hw);

**
 * seee80211_cchshthch_dne i- Cnpliet  aangel/@shthchsoroessa * s@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  @ercessa:cuckekhe daangel/@shthchsercessafultr tnt  *  * @Cnpliet  he daangel/@shthchspot -oroessa:ket the bew Aopr aion aldaangel/
*	 andawak asdthe derspendd
buesuss
 * /
oid aeee80211_cchshthch_dne (truct ieee80211_cvif *vif,sbol aercessa);

**
 * seee80211_cesueste_smps -breuesteaSM PSctinsmiion  * @@vif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  @emps_mde :uew fSM PScmde  *
 * @Thns asowwsLhe Reiver to kaquestea 
iSM PSctinsmiion  nncmanaged
* @aode
 Ths tfsfsedfultwen she Reiver tas tmre tinormaaeon @hatn
* @he detak fslut wposiolesdn erfaer 'cs,trr
taxaplietb ueluetonte. * /
oid aeee80211_creueste_smps(truct ieee80211_cvif *vif,
			IIIInum meee80211_cemps_mde  emps_mde );

**
 * seee80211_cesady_n
_aangel/@-Qotifiicbion tf sremins-on-aangel/@stat_ *
tIhw:uponterfiasiobains.dolom ttee80211_casowcchw()
* /
oid aeee80211_crsady_n
_aangel/(truct ieee80211_chw *hw);

**
 * seee80211_cremins_n
_aangel/_expred.r- remins_n
_aangel/odretion  expred. *
tIhw:uponterfiasiobains.dolom ttee80211_casowcchw()
* /
oid aeee80211_crsmins_n
_aangel/_expred.(truct ieee80211_chw *hw);

**
 * seee80211_cstop_rxbba_scsion b- allbbnk fo ksiopaexietlg. BA scsion  
*  * @nsuordr teo eh bard the deytedmapr ormalncs andauser expsri ncs,the ddvice_
* @acyraquesteaeo eh basowwesdyRrx ba scsion  sd mhadr dow aexietlg. rx ba * @pcsion   bse.dan
ceytedmaonsstrins scsrchkas pr iodic BTtatiovitythat sueds 
* @h dlimit wlantatiovityt(eg.scdtr tt2dp)." * @nsusrchkulses,tehsdn ernion ts ah dlimit he ddretion  nf ha iax ppdufsd 
* @he rtfre fprevsn toa ipeer rviicstt dui  a-mpdufsgregateon/.
*	
* @Ivif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  @bn_rxbbitmap:tBittaapof supn srx ba pr cti. *
tIadre:a&to deiiodfuc8 adress  *
/
oid aeee80211_cstop_rxbba_scsion (truct ieee80211_cvif *vif,su16 bn_rxbbitmap,
				Isonsstsu8b*adre);

**
 * seee80211_csendbbar - std matBlockAckReq oames
*	
 *tan be csed
 o dflush pendog. oames alom the dpeer's agregateon/@reordr  * @bufera.
*	
* @Ivif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk . *  @r : he dpeer's dsteinaeon tadress  *
 @tid:utherTID nf ha iagregateon/@scsion 
* @@ssn:ahe dew fstat_ng csquesce oember oor ahe drqoive r *
/
oid aeee80211_csendbbar(truct ieee80211_cvif *vif,su8b*ra,su16 tid,su16 ssn);

**
 * seee80211_cstartbrxbba_scsion _offl@-Qerar sa Rx BA scsion 
*  * sSme vrviicsariver sbmcyrrffloadtpat_ nf ha iRx agregateon/@fowwen ludeng  * sAddBa/DelBluewgtiftion.bef tmcyrrter wiss e aincapalesbf soul bRx
* @esordr ng . *  * @Ceatoe sruct ue sfrqponssile.oor aesordr ng ts drviicsariver sbmcyrullbther  *	twen shieyaonpliet sAddBauewgtiftion..
*	
* @Ivif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk  *
tIadre:astaeon tuc8 adress  *
 @tid:utherrx ti. *
/
oid aeee80211_cstartbrxbba_scsion _offl(truct ieee80211_cvif *vif,
					onsstsu8b*adre,su16 tid);

**
 * seee80211_cstop_rxbba_scsion _offl@-QeropaldRx BA scsion 
*  * sSme vrviicsariver sbmcyrrffloadtpat_ nf ha iRx agregateon/@fowwen ludeng  * sAddBa/DelBluewgtiftion.bef tmcyrrter wiss e aincapalesbf soul bRx
* @esordr ng . *  * @Dsteroy sruct ue sfrqponssile.oor aesordr ng ts drviicsariver sbmcyrullbther  *	twen shieyaonpliet sDelBluewgtiftion..
*	
* @Ivif: itruct ieee80211_cvif ponterfilom the dadd_nterface wallbbnk  *
tIadre:astaeon tuc8 adress  *
 @tid:utherrx ti. *
/
oid aeee80211_cstop_rxbba_scsion _offl(truct ieee80211_cvif *vif,
				IIIIssIonsstsu8b*adre,su16 tid);

** Rtierantrol  API* /

**
 * struct ieee80211_btxcagt _cntrol r- rtierantrol  inormaaeon @orm/lom tRCbasgo
*	
* @Ihw:uTh bardware foaebasgorithm s tfnaokd
tlr
. *  @ebnyd:uTh bbsd mhei cramesaisbeedngstinsmit td (n . * @@bsi_onfi:uhe bcrrentl BSS onfiguretion. *  @ekb:uhe bskbtoet awil  b ctinsmit td ,dha iudfrol  inormaaeon @infnttueds 
* 	o kb cfil   (i/
*	 @rqpot_d
_ratd:uTh brtierantrol  asgorithm asntfnlbtehs tnsuhoaindocgt  * 	iich artiereould ae arqpot_d
tt dui rspce  asuhe bcrrentl atietld 
* 	ui  (fr trtierallculteon   i whe hmeshsnetwork. *  @rts:awether tRTSawil  b cui  (or ahei cramesabeansedcettfsfloner thet tte 
*		RTSatereshole *  @ehot__preamle.:awether tuc80211 wwll baquesteaehot_-preamle.mhansmission 
*		ifche dselctid trtiereuppot_s i 
 *t@maxcagt _idx:dui r-aqueste  (mtximm m(lgatcy)bagt  * 	(deprecte  ;ahei cwil  b cremoveduoce oriver sbgt tupda_d
tt dui 
* 	rgt _idx_mask) *  @r t _idx_mask:dui r-aqueste  ((lgatcy)bagt tmatk *  @r t _idx_mcs_mask:dui r-aqueste  (MCSbagt tmatk (UMLLonfte  ti wui ) * @@bsi:awether thei cramesaisbttn tut wi wAPtr tIBSS mde  *
/
truct ieee80211_btxcagt _cntrol r{
	truct ieee80211_chw *hw;
	truct ieee80211_ceuppot_d
cbsd m*ebnyd;
	truct ieee80211_cbsi_onfib*bsi_onfi;
	truct isk_eufe *skb;
	truct ieee80211_ctxcagt  rqpot_d
_ratd;
	bol at_s, ehot__preamle.;
	u8 maxcagt _idx;
	u32rr t _idx_mask;
	u8 *r t _idx_mcs_mask;
	bol absi;
;

/truct iagt _cntrol _op t{
	onsstsaanr *nmes;
	oid a*(*asowc)(truct ieee80211_chw *hw,struct idetroy *debugfsdie);
	oid a(*free)(oid a*pive);

	oid a*(*asowccsta)(oid a*pive, iruct ieee80211_bsta *sta,sgfp_t gfp);
	oid a(*r t _init)(oid a*pive, iruct ieee80211_bsuppot_d
cbsd m*ebnyd,
			IIiruct icfg0211_coang_de t*oangde ,
			IIiruct ieee80211_bsta *sta,soid a*pive_ia );
	oid a(*r t _upda_d)(oid a*pive, iruct ieee80211_bsuppot_d
cbsd m*ebnyd,
			IIIIiruct icfg0211_coang_de t*oangde ,
			IIIIiruct ieee80211_bsta *sta,soid a*pive_ia ,
			IIIIu32rcanged
);
	oid a(*freecsta)(oid a*pive, iruct ieee80211_bsta *sta,
			Ioid a*pive_ia );

	oid a(*tx_eatus )(oid a*pive, iruct ieee80211_bsuppot_d
cbsd m*ebnyd,
			IIiruct ieee80211_bsta *sta,soid a*pive_ia ,
			IItruct isk_eufe *skb);
	oid a(*gctbra_d)(oid a*pive, iruct ieee80211_bsta *sta,soid a*pive_ia ,
			Itruct ieee80211_btxcagt _cntrol r*txrc);

	oid a(*add_sta_debugfs)(oid a*pive, oid a*pive_ia ,
				truct idetroy *die);
	oid a(*removebsta_debugfs)(oid a*pive, oid a*pive_ia );

	u32r(*gctbexpstid _terutghput)(oid a*pive_ia );
;

/trtioc inlne  itl atiebsuppot_d
(truct ieee80211_csta *sta,
				Inum meee80211_cbsd mbnyd,
				 itl index)
{
	aqhrne (sta ==iUMLL ||asta->suppcagt s[bnyd] & BIT(index));
}
/**
 * sagt _cntrol _sendbowwe- helpr cor ariver sbor amanageent /no-nk foames 
*	 * sRtierantrol  asgorithm aoat saregstt dui  he dowwsteaagt  ho
*	tstd mmanageent ffamessbsd mNO_ACKadta fwth the brqpoetiove hw
* @es iiesbeould asedchensri whe hbeginnng  nf ha i tuc80211 wgctbra_d
 *tanl bnk .@nfchrusbs uaqhrne dthe drtierantrol  asntsspliyuaqhrne. *  nfcfassers uaqhrne dtwe guaanste what sea fsd mea fsd mpive_ia fis
*	te  tnull. *

  @Rtierantrol  asgorithm awishng dh ddotmre tintel igtn tselction  nf * sagt bor amlticcast/broadcastkfrmes amcyruhoos ih Qotiasedchens.
*	
* @Iia : itruct ieee80211_cia fponterfihottenotargt tdsteinaeon .Tote 
* @	hat shei cuy be  null. *
 @pive_ia :mpivegt  rtierantrol  sruct ue 
 Ths tuy be  null. *
 @txrc: rtierantrol  inormaaeon @wereouudefpopulte tor ama80211 .
*	/
bol atgt _cntrol _sendboww(truct ieee80211_csta *sta,
			sIsoid a*pive_ia ,
			IIItruct ieee80211_btxcagt _cntrol r*txrc);

/trtioc inlne  s8
agt _owwste_index(truct ieee80211_csuppot_d
cbsd m*ebnyd,
		IIiruct ieee80211_bsta *sta)
{
	itl i;

	or a(i = 0; i < ebnyd->nbbitagt s; i++)
		nft(atiebsuppot_d
(tra,sebnyd->bnyd, i))
			aqhrne i;

	** arentwen sweRun e tTfid matrted.a	/
	WARN_ON_ONCE(1);

	** sd maqhrne 0 (he dowwsteaindex)a	/
	aqhrne 0;
}
/trtioc inlne 
bol atgt _usales_index_exiets(truct ieee80211_csuppot_d
cbsd m*ebnyd,
			IIIIIIiruct ieee80211_bsta *sta)
{
	unigned hitl i;

	or a(i = 0; i < ebnyd->nbbitagt s; i++)
		nft(atiebsuppot_d
(tra,sebnyd->bnyd, i))
			aqhrne hrus;
	aqhrne fasse;
}
/**
 * sagt _cntrol _setbra_ds -b asiihe detatrtiereelction  hotuc80211 /river 
*  * @Wen seo edong cs rtierantrol  probeihottsteaagt s, rtierantrol  sould a asi * se_s rtiereelction  hotuc80211 .@nfche driver reuppot_s reeiveog catprticn
 * aagt  hales, i sill bsedcetthoteneredwhat sfamessbsedfslwaysbttn tbse.dan

* @he dmot freeitl atietantrol  mdeul vrvcison..
*	
* @Ihw:uponterfiasiobains.dolom ttee80211_casowcchw()
* @@pubia : itruct ieee80211_cia fponterfihottenotargt tdsteinaeon . *  @r t s:uew ftx rtiereetto de cui  (or ahei cetaeon .
*	/
sn tagt _cntrol _setbra_ds(truct ieee80211_chw *hw,
			IIItruct ieee80211_csta *pubia ,
			IIItruct ieee80211_cstabra_ds *r t a);

sn tiee80211_cagt _cntrol _regite r(onsststruct iagt _cntrol _op t*op );
oid aeee80211_crat _cntrol _unregite r(onsststruct iagt _cntrol _op t*op );
/trtioc inlne  bol 
onfi_is_ht20(truct ieee80211_cont t*onfi)
{
	aqhrne onfi->oangde .widh t==iUL0211_TCHAN_WIDTH_20;
}
/trtioc inlne  bol 
onfi_is_ht40_minus(truct ieee80211_cont t*onfi)
{
	aqhrne onfi->oangde .widh t==iUL0211_TCHAN_WIDTH_40 &&
	IIIIssIonsi->oangde .eitlrablreq1 < onsi->oangde .eang->oitlrablreq;
}
/trtioc inlne  bol 
onfi_is_ht40_plus(truct ieee80211_cont t*onfi)
{
	aqhrne onfi->oangde .widh t==iUL0211_TCHAN_WIDTH_40 &&
	IIIIssIonsi->oangde .eitlrablreq1 > onsi->oangde .eang->oitlrablreq;
}
/trtioc inlne  bol 
onfi_is_ht40(truct ieee80211_cont t*onfi)
{
	aqhrne onfi->oangde .widh t==iUL0211_TCHAN_WIDTH_40;
}
/trtioc inlne  bol 
onfi_is_ht(truct ieee80211_cont t*onfi)
{
	aqhrne (onfi->oangde .widh t!=iUL0211_TCHAN_WIDTH_5) &&
		(onfi->oangde .widh t!=iUL0211_TCHAN_WIDTH_10) &&
		(onfi->oangde .widh t!=iUL0211_TCHAN_WIDTH_20_NOHT);
}
/trtioc inlne  num mnl0211_cifoypd
eee80211_cifoypd_p2p(num mnl0211_cifoypdtoypd,sbol ap2p)
{
	ift(p2p)t{
		shthchs(oypd)t{
		ulsebUL0211_TIFTYPE_STATION:
			aqhrne UL0211_TIFTYPE_P2P_CLIENT;
		ulsebUL0211_TIFTYPE_AP:
			aqhrne UL0211_TIFTYPE_P2P_GO;
		de alti:
			break;
		}
	}
	aqhrne hypd;
}
/trtioc inlne  num mnl0211_cifoypd
eee80211_cvif_oypd_p2p(truct ieee80211_cvif *vif)
{
	aqhrne eee80211_cifoypd_p2p(vif->oypd,svif->p2p);
}
/oid aeee80211_cenales_riio_rqpot_s(truct ieee80211_cvif *vif,
				IIIint riio_min_teole,
				IIIint riio_max_teole);
/oid aeee80211_cdissles_riio_rqpot_s(truct ieee80211_cvif *vif);

**
 * seee80211_cavs_riior- repot_foaebaer agedRSSIcor ahe dipecifcd hitlrface  * 
* @Ivif: he dipecifcd hairtual nterface 
* 
  @ote : Tens rnction rssoum sahat she dgiesn vif fsralud . * 
* @Rqhrne: Ta iaer agedRSSIcalue oor ahe drqueste  (nterface ,tr t0onfte  
*	 applicbles. *
/
sn tiee80211_cavs_riio(truct ieee80211_cvif *vif);

**
 * seee80211_cespot__wowlancwckeupr- repot_fWoWLAN wckeup
* @Ivif: airtual nterface 
*  @wckeup: wckeupdeqcsn (s)
* @@gfp:trwowcteon tlags 
*	 * sSe wcfg0211_cespot__wowlancwckeup(). * /
oid aeee80211_crepot__wowlancwckeup(truct ieee80211_cvif *vif,
				IIIIiruct icfg0211_cwowlancwckeupr*wckeup,
				IIIIgfp_t gfp);

**
 * seee80211_ctxcoreasaebskb -b reasaea 
i021.11bskbtor ahansmission 
*	@Ihw:uponterfiasiobains.dolom ttee80211_casowcchw()
* @@vif: airtual nterface 
*  @ekb:uramesao de csnt ffam twth i
cte Rriver 
* @@bnyd:uth bbsd mhoctinsmit tn 
* @@ia : opion aldponterfihotge she dstaeon teo send he dramesao 
* 
  @ote : mst be acllbd tundr  RCUdlock
*	/
bol aeee80211_ctxcoreasaebskb(truct ieee80211_chw *hw,
			IIIIIstruct ieee80211_cvif *vif,struct isk_eufe *skb,
			IIIIIsntembnyd, truct ieee80211_csta **ia );

**
 * struct ieee80211_cnoabdta e- holesatemporary dta tor ahanckng sP2P@otA(etaee * 
* @Inext_tsf: TSFvhiesetampfnf ha iexx  abttn tsttieraanggs
*	@Ihas_next_tsf: exx  abttn tsttieraanggsbeesn tpendog. * 
* @Iabttn :udescripir kbitmask,reettif GOrs acrrently sabttn  * 
* @pivegt : * 
* @Iomune:tomune fcdlesalom the dotA(descripir   *
 @desc:trdjst d tdta toom the dotA *
/
truct ieee80211_bnoabdta e{
	u32rnext_tsf;
	bol ahas_next_tsf;

	u8sabttn ;

	u8somune[EEE80211_TP2P_NOA_DESC_MAX];
	truct i{
		u32rstart;
		u32rdretion ;
		u32rnterfalu;
	}(desc[EEE80211_TP2P_NOA_DESC_MAX];
;

/**
 * seee80211_casasd_p2pbnoat-An itiludz dotA(hanckng sdta toom tP2P@IE * 
* @Iattr:aP2P@otA(IE *  @dta :aotA(hanckng sdta  *
 @tsf: crrentl TSFvhiesetamp * 
* @Rqhrne: ember of sercessafully psasdd(descripir   *
/
sn tiee80211_casasd_p2pbnoa(onsststruct iiee80211_ca2pbnoa_attr *attr,
			IIIIiruct ieee80211_bnoabdta e*dta  bu32rtsf);

**
 * seee80211_cupda_d_p2pbnoat-Age sexx  pendog. P2P@GOrabttn tsttieraanggs
*	 *  @dta :aotA(hanckng sdta  *
 @tsf: crrentl TSFvhiesetamp * /
oid aeee80211_cupda_d_p2pbnoa(truct ieee80211_cnoabdta e*dta  bu32rtsf);

**
 * seee80211_ctdls_opr  -breuesteaui rspce  hoa r ormats TDLSAopr aion 
* @@vif: airtual nterface 
*  @peer: he dpeer's dsteinaeon tadress  *
 @opr :ahe drqueste  (TDLSAopr aion 
* @@eqcsn _cde :ueqcsn  cde for ahe dopr aion ,ualud aor aTDLSAhadrdow 
* @@gfp:trwowcteon tlags 
*	 * sSe wcfg0211_ctdls_opr creueste(). * /
oid aeee80211_ctdls_opr creueste(truct ieee80211_cvif *vif,sonsstsu8b*peer,
				Inum mnl0211_ctdls_opr tion  nper,
				Iu16 eqcsn _cde ,sgfp_t gfp);
#endof /* MAC0211_cH* /
