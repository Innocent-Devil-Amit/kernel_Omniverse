#ifndef __NET_CFG80211_H
#define __NET_CFG80211_H
/*
 * 802.11 device and configuration interface
 *
 * Copyright 2006-2010	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014 Intel Mobile Communications GmbH
 * Copyright 2015      Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/netdevice.h>
#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/bug.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/nl80211.h>
#include <linux/if_ether.h>
#include <linux/ieee80211.h>
#include <linux/net.h>
#include <net/regulatory.h>

/**
 * DOC: Introduction
 *
 * cfg80211 is the configuration API for 802.11 devices in Linux. It bridges
 * userspace and drivers, and offers some utility functionality associated
 * with 802.11. cfg80211 must, directly or indirectly via mac80211, be used
 * by all modern wireless drivers in Linux, so that they offer a consistent
 * API through nl80211. For backward compatibility, cfg80211 also offers
 * wireless extensions to userspace, but hides them from drivers completely.
 *
 * Additionally, cfg80211 contains code to help enforce regulatory spectrum
 * use restrictions.
 */


/**
 * DOC: Device registration
 *
 * In order for a driver to use cfg80211, it must register the hardware device
 * with cfg80211. This happens through a number of hardware capability structs
 * described below.
 *
 * The fundamental structure for each device is the 'wiphy', of which each
 * instance describes a physical wireless device connected to the system. Each
 * such wiphy can have zero, one, or many virtual interfaces associated with
 * it, which need to be identified as such by pointing the network interface's
 * @ieee80211_ptr pointer to a &struct wireless_dev which further describes
 * the wireless part of the interface, normally this struct is embedded in the
 * network interface's private data area. Drivers can optionally allow creating
 * or destroying virtual interfaces on the fly, but without at least one or the
 * ability to create some the wireless device isn't useful.
 *
 * Each wiphy structure contains device capability information, and also has
 * a pointer to the various operations the driver offers. The definitions and
 * structures here describe these capabilities in detail.
 */

struct wiphy;

#define SUPPORT_WDEV_CFG80211_VENDOR_EVENT_ALLOC 1
#define CFG80211_DEL_STA_V2 1
#define CFG80211_SCAN_BSSID 1
#define CFG80211_CONNECT_PREV_BSSID 1
#define CFG80211_CONNECT_BSS 1
#define CFG80211_ABORT_SCAN 1
#define CFG80211_DISCONNECTED_V2 1
#define CFG80211_CONNECT_TIMEOUT 1
#define CFG80211_INFORM_BSS_FRAME_DATA 1
#define CFG80211_SCAN_RANDOM_MAC_ADDR 1
#define CFG80211_MULTI_SCAN_PLAN_BACKPORT 1
#define CFG80211_UPDATE_CONNECT_PARAMS 1
#define CFG80211_BEACON_TX_RATE_CUSTOM_BACKPORT 1
#define CFG80211_RAND_TA_FOR_PUBLIC_ACTION_FRAME 1
#define CFG80211_REPORT_BETTER_BSS_IN_SCHED_SCAN 1
#define CFG80211_BEACON_INTERVAL_BACKPORT 1

/*
 * wireless hardware capability structures
 */

/**
 * enum ieee80211_band - supported frequency bands
 *
 * The bands are assigned this way because the supported
 * bitrates differ in these bands.
 *
 * @IEEE80211_BAND_2GHZ: 2.4GHz ISM band
 * @IEEE80211_BAND_5GHZ: around 5GHz band (4.9-5.7)
 * @IEEE80211_BAND_60GHZ: around 60 GHz band (58.32 - 64.80 GHz)
 * @IEEE80211_NUM_BANDS: number of defined bands
 */
enum ieee80211_band {
	IEEE80211_BAND_2GHZ = NL80211_BAND_2GHZ,
	IEEE80211_BAND_5GHZ = NL80211_BAND_5GHZ,
	IEEE80211_BAND_60GHZ = NL80211_BAND_60GHZ,

	/* keep last */
	IEEE80211_NUM_BANDS
};

/**
 * enum ieee80211_channel_flags - channel flags
 *
 * Channel flags set by the regulatory control code.
 *
 * @IEEE80211_CHAN_DISABLED: This channel is disabled.
 * @IEEE80211_CHAN_NO_IR: do not initiate radiation, this includes
 * 	sending probe requests or beaconing.
 * @IEEE80211_CHAN_RADAR: Radar detection is required on this channel.
 * @IEEE80211_CHAN_NO_HT40PLUS: extension channel above this channel
 * 	is not permitted.
 * @IEEE80211_CHAN_NO_HT40MINUS: extension channel below this channel
 * 	is not permitted.
 * @IEEE80211_CHAN_NO_OFDM: OFDM is not allowed on this channel.
 * @IEEE80211_CHAN_NO_80MHZ: If the driver supports 80 MHz on the band,
 *	this flag indicates that an 80 MHz channel cannot use this
 *	channel as the control or any of the secondary channels.
 *	This may be due to the driver or due to regulatory bandwidth
 *	restrictions.
 * @IEEE80211_CHAN_NO_160MHZ: If the driver supports 160 MHz on the band,
 *	this flag indicates that an 160 MHz channel cannot use this
 *	channel as the control or any of the secondary channels.
 *	This may be due to the driver or due to regulatory bandwidth
 *	restrictions.
 * @IEEE80211_CHAN_INDOOR_ONLY: see %NL80211_FREQUENCY_ATTR_INDOOR_ONLY
 * @IEEE80211_CHAN_GO_CONCURRENT: see %NL80211_FREQUENCY_ATTR_GO_CONCURRENT
 * @IEEE80211_CHAN_NO_20MHZ: 20 MHz bandwidth is not permitted
 *	on this channel.
 * @IEEE80211_CHAN_NO_10MHZ: 10 MHz bandwidth is not permitted
 *	on this channel.
 *
 */
enum ieee80211_channel_flags {
	IEEE80211_CHAN_DISABLED		= 1<<0,
	IEEE80211_CHAN_NO_IR		= 1<<1,
	/* hole at 1<<2 */
	IEEE80211_CHAN_RADAR		= 1<<3,
	IEEE80211_CHAN_NO_HT40PLUS	= 1<<4,
	IEEE80211_CHAN_NO_HT40MINUS	= 1<<5,
	IEEE80211_CHAN_NO_OFDM		= 1<<6,
	IEEE80211_CHAN_NO_80MHZ		= 1<<7,
	IEEE80211_CHAN_NO_160MHZ	= 1<<8,
	IEEE80211_CHAN_INDOOR_ONLY	= 1<<9,
	IEEE80211_CHAN_GO_CONCURRENT	= 1<<10,
	IEEE80211_CHAN_NO_20MHZ		= 1<<11,
	IEEE80211_CHAN_NO_10MHZ		= 1<<12,
};

#define IEEE80211_CHAN_NO_HT40 \
	(IEEE80211_CHAN_NO_HT40PLUS | IEEE80211_CHAN_NO_HT40MINUS)

#define IEEE80211_DFS_MIN_CAC_TIME_MS		60000
#define IEEE80211_DFS_MIN_NOP_TIME_MS		(30 * 60 * 1000)

/**
 * struct ieee80211_channel - channel definition
 *
 * This structure describes a single channel for use
 * with cfg80211.
 *
 * @center_freq: center frequency in MHz
 * @hw_value: hardware-specific value for the channel
 * @flags: channel flags from &enum ieee80211_channel_flags.
 * @orig_flags: channel flags at registration time, used by regulatory
 *	code to support devices with additional restrictions
 * @band: band this channel belongs to.
 * @max_antenna_gain: maximum antenna gain in dBi
 * @max_power: maximum transmission power (in dBm)
 * @max_reg_power: maximum regulatory transmission power (in dBm)
 * @beacon_found: helper to regulatory code to indicate when a beacon
 *	has been found on this channel. Use regulatory_hint_found_beacon()
 *	to enable this, this is useful only on 5 GHz band.
 * @orig_mag: internal use
 * @orig_mpwr: internal use
 * @dfs_state: current state of this channel. Only relevant if radar is required
 *	on this channel.
 * @dfs_state_entered: timestamp (jiffies) when the dfs state was entered.
 * @dfs_cac_ms: DFS CAC time in milliseconds, this is valid for DFS channels.
 */
struct ieee80211_channel {
	enum ieee80211_band band;
	u16 center_freq;
	u16 hw_value;
	u32 flags;
	int max_antenna_gain;
	int max_power;
	int max_reg_power;
	bool beacon_found;
	u32 orig_flags;
	int orig_mag, orig_mpwr;
	enum nl80211_dfs_state dfs_state;
	unsigned long dfs_state_entered;
	unsigned int dfs_cac_ms;
};

/**
 * enum ieee80211_rate_flags - rate flags
 *
 * Hardware/specification flags for rates. These are structured
 * in a way that allows using the same bitrate structure for
 * different bands/PHY modes.
 *
 * @IEEE80211_RATE_SHORT_PREAMBLE: Hardware can send with short
 *	preamble on this bitrate; only relevant in 2.4GHz band and
 *	with CCK rates.
 * @IEEE80211_RATE_MANDATORY_A: This bitrate is a mandatory rate
 *	when used with 802.11a (on the 5 GHz band); filled by the
 *	core code when registering the wiphy.
 * @IEEE80211_RATE_MANDATORY_B: This bitrate is a mandatory rate
 *	when used with 802.11b (on the 2.4 GHz band); filled by the
 *	core code when registering the wiphy.
 * @IEEE80211_RATE_MANDATORY_G: This bitrate is a mandatory rate
 *	when used with 802.11g (on the 2.4 GHz band); filled by the
 *	core code when registering the wiphy.
 * @IEEE80211_RATE_ERP_G: This is an ERP rate in 802.11g mode.
 * @IEEE80211_RATE_SUPPORTS_5MHZ: Rate can be used in 5 MHz mode
 * @IEEE80211_RATE_SUPPORTS_10MHZ: Rate can be used in 10 MHz mode
 */
enum ieee80211_rate_flags {
	IEEE80211_RATE_SHORT_PREAMBLE	= 1<<0,
	IEEE80211_RATE_MANDATORY_A	= 1<<1,
	IEEE80211_RATE_MANDATORY_B	= 1<<2,
	IEEE80211_RATE_MANDATORY_G	= 1<<3,
	IEEE80211_RATE_ERP_G		= 1<<4,
	IEEE80211_RATE_SUPPORTS_5MHZ	= 1<<5,
	IEEE80211_RATE_SUPPORTS_10MHZ	= 1<<6,
};

/**
 * enum ieee80211_bss_type - BSS type filter
 *
 * @IEEE80211_BSS_TYPE_ESS: Infrastructure BSS
 * @IEEE80211_BSS_TYPE_PBSS: Personal BSS
 * @IEEE80211_BSS_TYPE_IBSS: Independent BSS
 * @IEEE80211_BSS_TYPE_MBSS: Mesh BSS
 * @IEEE80211_BSS_TYPE_ANY: Wildcard value for matching any BSS type
 */
enum ieee80211_bss_type {
	IEEE80211_BSS_TYPE_ESS,
	IEEE80211_BSS_TYPE_PBSS,
	IEEE80211_BSS_TYPE_IBSS,
	IEEE80211_BSS_TYPE_MBSS,
	IEEE80211_BSS_TYPE_ANY
};

/**
 * enum ieee80211_privacy - BSS privacy filter
 *
 * @IEEE80211_PRIVACY_ON: privacy bit set
 * @IEEE80211_PRIVACY_OFF: privacy bit clear
 * @IEEE80211_PRIVACY_ANY: Wildcard value for matching any privacy setting
 */
enum ieee80211_privacy {
	IEEE80211_PRIVACY_ON,
	IEEE80211_PRIVACY_OFF,
	IEEE80211_PRIVACY_ANY
};

#define IEEE80211_PRIVACY(x)	\
	((x) ? IEEE80211_PRIVACY_ON : IEEE80211_PRIVACY_OFF)

/**
 * struct ieee80211_rate - bitrate definition
 *
 * This structure describes a bitrate that an 802.11 PHY can
 * operate with. The two values @hw_value and @hw_value_short
 * are only for driver use when pointers to this structure are
 * passed around.
 *
 * @flags: rate-specific flags
 * @bitrate: bitrate in units of 100 Kbps
 * @hw_value: driver/hardware value for this rate
 * @hw_value_short: driver/hardware value for this rate when
 *	short preamble is used
 */
struct ieee80211_rate {
	u32 flags;
	u16 bitrate;
	u16 hw_value, hw_value_short;
};

/**
 * struct ieee80211_sta_ht_cap - STA's HT capabilities
 *
 * This structure describes most essential parameters needed
 * to describe 802.11n HT capabilities for an STA.
 *
 * @ht_supported: is HT supported by the STA
 * @cap: HT capabilities map as described in 802.11n spec
 * @ampdu_factor: Maximum A-MPDU length factor
 * @ampdu_density: Minimum A-MPDU spacing
 * @mcs: Supported MCS rates
 */
struct ieee80211_sta_ht_cap {
	u16 cap; /* use IEEE80211_HT_CAP_ */
	bool ht_supported;
	u8 ampdu_factor;
	u8 ampdu_density;
	struct ieee80211_mcs_info mcs;
};

/**
 * struct ieee80211_sta_vht_cap - STA's VHT capabilities
 *
 * This structure describes most essential parameters needed
 * to describe 802.11ac VHT capabilities for an STA.
 *
 * @vht_supported: is VHT supported by the STA
 * @cap: VHT capabilities map as described in 802.11ac spec
 * @vht_mcs: Supported VHT MCS rates
 */
struct ieee80211_sta_vht_cap {
	bool vht_supported;
	u32 cap; /* use IEEE80211_VHT_CAP_ */
	struct ieee80211_vht_mcs_info vht_mcs;
};

/**
 * struct ieee80211_supported_band - frequency band definition
 *
 * This structure describes a frequency band a wiphy
 * is able to operate in.
 *
 * @channels: Array of channels the hardware can operate in
 *	in this band.
 * @band: the band this structure represents
 * @n_channels: Number of channels in @channels
 * @bitrates: Array of bitrates the hardware can operate with
 *	in this band. Must be sorted to give a valid "supported
 *	rates" IE, i.e. CCK rates first, then OFDM.
 * @n_bitrates: Number of bitrates in @bitrates
 * @ht_cap: HT capabilities in this band
 * @vht_cap: VHT capabilities in this band
 */
struct ieee80211_supported_band {
	struct ieee80211_channel *channels;
	struct ieee80211_rate *bitrates;
	enum ieee80211_band band;
	int n_channels;
	int n_bitrates;
	struct ieee80211_sta_ht_cap ht_cap;
	struct ieee80211_sta_vht_cap vht_cap;
};

/*
 * Wireless hardware/device configuration structures and methods
 */

/**
 * DOC: Actions and configuration
 *
 * Each wireless device and each virtual interface offer a set of configuration
 * operations and other actions that are invoked by userspace. Each of these
 * actions is described in the operations structure, and the parameters these
 * operations use are described separately.
 *
 * Additionally, some operations are asynchronous and expect to get status
 * information via some functions that drivers need to call.
 *
 * Scanning and BSS list handling with its associated functionality is described
 * in a separate chapter.
 */

/**
 * struct vif_params - describes virtual interface parameters
 * @use_4addr: use 4-address frames
 * @macaddr: address to use for this virtual interface. This will only
 * 	be used for non-netdevice interfaces. If this parameter is set
 * 	to zero address the driver may determine the address as needed.
 */
struct vif_params {
       int use_4addr;
       u8 macaddr[ETH_ALEN];
};

/**
 * struct key_params - key information
 *
 * Information about a key
 *
 * @key: key material
 * @key_len: length of key material
 * @cipher: cipher suite selector
 * @seq: sequence counter (IV/PN) for TKIP and CCMP keys, only used
 *	with the get_key() callback, must be in little endian,
 *	length given by @seq_len.
 * @seq_len: length of @seq.
 */
struct key_params {
	const u8 *key;
	const u8 *seq;
	int key_len;
	int seq_len;
	u32 cipher;
};

/**
 * struct cfg80211_chan_def - channel definition
 * @chan: the (control) channel
 * @width: channel width
 * @center_freq1: center frequency of first segment
 * @center_freq2: center frequency of second segment
 *	(only with 80+80 MHz)
 */
struct cfg80211_chan_def {
	struct ieee80211_channel *chan;
	enum nl80211_chan_width width;
	u32 center_freq1;
	u32 center_freq2;
};

/**
 * cfg80211_get_chandef_type - return old channel type from chandef
 * @chandef: the channel definition
 *
 * Return: The old channel type (NOHT, HT20, HT40+/-) from a given
 * chandef, which must have a bandwidth allowing this conversion.
 */
static inline enum nl80211_channel_type
cfg80211_get_chandef_type(const struct cfg80211_chan_def *chandef)
{
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
		return NL80211_CHAN_NO_HT;
	case NL80211_CHAN_WIDTH_20:
		return NL80211_CHAN_HT20;
	case NL80211_CHAN_WIDTH_40:
		if (chandef->center_freq1 > chandef->chan->center_freq)
			return NL80211_CHAN_HT40PLUS;
		return NL80211_CHAN_HT40MINUS;
	default:
		WARN_ON(1);
		return NL80211_CHAN_NO_HT;
	}
}

/**
 * cfg80211_chandef_create - create channel definition using channel type
 * @chandef: the channel definition struct to fill
 * @channel: the control channel
 * @chantype: the channel type
 *
 * Given a channel type, create a channel definition.
 */
void cfg80211_chandef_create(struct cfg80211_chan_def *chandef,
			     struct ieee80211_channel *channel,
			     enum nl80211_channel_type chantype);

/**
 * cfg80211_chandef_identical - check if two channel definitions are identical
 * @chandef1: first channel definition
 * @chandef2: second channel definition
 *
 * Return: %true if the channels defined by the channel definitions are
 * identical, %false otherwise.
 */
static inline bool
cfg80211_chandef_identical(const struct cfg80211_chan_def *chandef1,
			   const struct cfg80211_chan_def *chandef2)
{
	return (chandef1->chan == chandef2->chan &&
		chandef1->width == chandef2->width &&
		chandef1->center_freq1 == chandef2->center_freq1 &&
		chandef1->center_freq2 == chandef2->center_freq2);
}

/**
 * cfg80211_chandef_compatible - check if two channel definitions are compatible
 * @chandef1: first channel definition
 * @chandef2: second channel definition
 *
 * Return: %NULL if the given channel definitions are incompatible,
 * chandef1 or chandef2 otherwise.
 */
const struct cfg80211_chan_def *
cfg80211_chandef_compatible(const struct cfg80211_chan_def *chandef1,
			    const struct cfg80211_chan_def *chandef2);

/**
 * cfg80211_chandef_valid - check if a channel definition is valid
 * @chandef: the channel definition to check
 * Return: %true if the channel definition is valid. %false otherwise.
 */
bool cfg80211_chandef_valid(const struct cfg80211_chan_def *chandef);

/**
 * cfg80211_chandef_usable - check if secondary channels can be used
 * @wiphy: the wiphy to validate against
 * @chandef: the channel definition to check
 * @prohibited_flags: the regulatory channel flags that must not be set
 * Return: %true if secondary channels are usable. %false otherwise.
 */
bool cfg80211_chandef_usable(struct wiphy *wiphy,
			     const struct cfg80211_chan_def *chandef,
			     u32 prohibited_flags);

/**
 * cfg80211_chandef_dfs_required - checks if radar detection is required
 * @wiphy: the wiphy to validate against
 * @chandef: the channel definition to check
 * @iftype: the interface type as specified in &enum nl80211_iftype
 * Returns:
 *	1 if radar detection is required, 0 if it is not, < 0 on error
 */
int cfg80211_chandef_dfs_required(struct wiphy *wiphy,
				  const struct cfg80211_chan_def *chandef,
				  enum nl80211_iftype iftype);

/**
 * ieee80211_chandef_rate_flags - returns rate flags for a channel
 *
 * In some channel types, not all rates may be used - for example CCK
 * rates may not be used in 5/10 MHz channels.
 *
 * @chandef: channel definition for the channel
 *
 * Returns: rate flags which apply for this channel
 */
static inline enum ieee80211_rate_flags
ieee80211_chandef_rate_flags(struct cfg80211_chan_def *chandef)
{
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_5:
		return IEEE80211_RATE_SUPPORTS_5MHZ;
	case NL80211_CHAN_WIDTH_10:
		return IEEE80211_RATE_SUPPORTS_10MHZ;
	default:
		break;
	}
	return 0;
}

/**
 * ieee80211_chandef_max_power - maximum transmission power for the chandef
 *
 * In some regulations, the transmit power may depend on the configured channel
 * bandwidth which may be defined as dBm/MHz. This function returns the actual
 * max_power for non-standard (20 MHz) channels.
 *
 * @chandef: channel definition for the channel
 *
 * Returns: maximum allowed transmission power in dBm for the chandef
 */
static inline int
ieee80211_chandef_max_power(struct cfg80211_chan_def *chandef)
{
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_5:
		return min(chandef->chan->max_reg_power - 6,
			   chandef->chan->max_power);
	case NL80211_CHAN_WIDTH_10:
		return min(chandef->chan->max_reg_power - 3,
			   chandef->chan->max_power);
	default:
		break;
	}
	return chandef->chan->max_power;
}

/**
 * enum survey_info_flags - survey information flags
 *
 * @SURVEY_INFO_NOISE_DBM: noise (in dBm) was filled in
 * @SURVEY_INFO_IN_USE: channel is currently being used
 * @SURVEY_INFO_CHANNEL_TIME: channel active time (in ms) was filled in
 * @SURVEY_INFO_CHANNEL_TIME_BUSY: channel busy time was filled in
 * @SURVEY_INFO_CHANNEL_TIME_EXT_BUSY: extension channel busy time was filled in
 * @SURVEY_INFO_CHANNEL_TIME_RX: channel receive time was filled in
 * @SURVEY_INFO_CHANNEL_TIME_TX: channel transmit time was filled in
 *
 * Used by the driver to indicate which info in &struct survey_info
 * it has filled in during the get_survey().
 */
enum survey_info_flags {
	SURVEY_INFO_NOISE_DBM = 1<<0,
	SURVEY_INFO_IN_USE = 1<<1,
	SURVEY_INFO_CHANNEL_TIME = 1<<2,
	SURVEY_INFO_CHANNEL_TIME_BUSY = 1<<3,
	SURVEY_INFO_CHANNEL_TIME_EXT_BUSY = 1<<4,
	SURVEY_INFO_CHANNEL_TIME_RX = 1<<5,
	SURVEY_INFO_CHANNEL_TIME_TX = 1<<6,
};

/**
 * struct survey_info - channel survey response
 *
 * @channel: the channel this survey record reports, mandatory
 * @filled: bitflag of flags from &enum survey_info_flags
 * @noise: channel noise in dBm. This and all following fields are
 *	optional
 * @channel_time: amount of time in ms the radio spent on the channel
 * @channel_time_busy: amount of time the primary channel was sensed busy
 * @channel_time_ext_busy: amount of time the extension channel was sensed busy
 * @channel_time_rx: amount of time the radio spent receiving data
 * @channel_time_tx: amount of time the radio spent transmitting data
 *
 * Used by dump_survey() to report back per-channel survey information.
 *
 * This structure can later be expanded with things like
 * channel duty cycle etc.
 */
struct survey_info {
	struct ieee80211_channel *channel;
	u64 channel_time;
	u64 channel_time_busy;
	u64 channel_time_ext_busy;
	u64 channel_time_rx;
	u64 channel_time_tx;
	u32 filled;
	s8 noise;
};

/**
 * struct cfg80211_crypto_settings - Crypto settings
 * @wpa_versions: indicates which, if any, WPA versions are enabled
 *	(from enum nl80211_wpa_versions)
 * @cipher_group: group key cipher suite (or 0 if unset)
 * @n_ciphers_pairwise: number of AP supported unicast ciphers
 * @ciphers_pairwise: unicast key cipher suites
 * @n_akm_suites: number of AKM suites
 * @akm_suites: AKM suites
 * @control_port: Whether user space controls IEEE 802.1X port, i.e.,
 *	sets/clears %NL80211_STA_FLAG_AUTHORIZED. If true, the driver is
 *	required to assume that the port is unauthorized until authorized by
 *	user space. Otherwise, port is marked authorized by default.
 * @control_port_ethertype: the control port protocol that should be
 *	allowed through even on unauthorized ports
 * @control_port_no_encrypt: TRUE to prevent encryption of control port
 *	protocol frames.
 */
struct cfg80211_crypto_settings {
	u32 wpa_versions;
	u32 cipher_group;
	int n_ciphers_pairwise;
	u32 ciphers_pairwise[NL80211_MAX_NR_CIPHER_SUITES];
	int n_akm_suites;
	u32 akm_suites[NL80211_MAX_NR_AKM_SUITES];
	bool control_port;
	__be16 control_port_ethertype;
	bool control_port_no_encrypt;
};

/**
 * struct cfg80211_beacon_data - beacon data
 * @head: head portion of beacon (before TIM IE)
 *	or %NULL if not changed
 * @tail: tail portion of beacon (after TIM IE)
 *	or %NULL if not changed
 * @head_len: length of @head
 * @tail_len: length of @tail
 * @beacon_ies: extra information element(s) to add into Beacon frames or %NULL
 * @beacon_ies_len: length of beacon_ies in octets
 * @proberesp_ies: extra information element(s) to add into Probe Response
 *	frames or %NULL
 * @proberesp_ies_len: length of proberesp_ies in octets
 * @assocresp_ies: extra information element(s) to add into (Re)Association
 *	Response frames or %NULL
 * @assocresp_ies_len: length of assocresp_ies in octets
 * @probe_resp_len: length of probe response template (@probe_resp)
 * @probe_resp: probe response template (AP mode only)
 */
struct cfg80211_beacon_data {
	const u8 *head, *tail;
	const u8 *beacon_ies;
	const u8 *proberesp_ies;
	const u8 *assocresp_ies;
	const u8 *probe_resp;

	size_t head_len, tail_len;
	size_t beacon_ies_len;
	size_t proberesp_ies_len;
	size_t assocresp_ies_len;
	size_t probe_resp_len;
};

struct mac_address {
	u8 addr[ETH_ALEN];
};

/**
 * struct cfg80211_acl_data - Access control list data
 *
 * @acl_policy: ACL policy to be applied on the station's
 *	entry specified by mac_addr
 * @n_acl_entries: Number of MAC address entries passed
 * @mac_addrs: List of MAC addresses of stations to be used for ACL
 */
struct cfg80211_acl_data {
	enum nl80211_acl_policy acl_policy;
	int n_acl_entries;

	/* Keep it last */
	struct mac_address mac_addrs[];
};

/*
 * cfg80211_bitrate_mask - masks for bitrate control
 */
struct cfg80211_bitrate_mask {
	struct {
		u32 legacy;
		u8 ht_mcs[IEEE80211_HT_MCS_MASK_LEN];
		u16 vht_mcs[NL80211_VHT_NSS_MAX];
		enum nl80211_txrate_gi gi;
	} control[IEEE80211_NUM_BANDS];
};

/**
 * struct cfg80211_ap_settings - AP configuration
 *
 * Used to configure an AP interface.
 *
 * @chandef: defines the channel to use
 * @beacon: beacon data
 * @beacon_interval: beacon interval
 * @dtim_period: DTIM period
 * @ssid: SSID to be used in the BSS (note: may be %NULL if not provided from
 *	user space)
 * @ssid_len: length of @ssid
 * @hidden_ssid: whether to hide the SSID in Beacon/Probe Response frames
 * @crypto: crypto settings
 * @privacy: the BSS uses privacy
 * @auth_type: Authentication type (algorithm)
 * @smps_mode: SMPS mode
 * @inactivity_timeout: time in seconds to determine station's inactivity.
 * @p2p_ctwindow: P2P CT Window
 * @p2p_opp_ps: P2P opportunistic PS
 * @acl: ACL configuration used by the drivers which has support for
 *	MAC address based access control
 * @pbss: If set, start as a PCP instead of AP. Relevant for DMG
 *	networks.
 * @beacon_rate: bitrate to be used for beacons
 */
struct cfg80211_ap_settings {
	struct cfg80211_chan_def chandef;

	struct cfg80211_beacon_data beacon;

	int beacon_interval, dtim_period;
	const u8 *ssid;
	size_t ssid_len;
	enum nl80211_hidden_ssid hidden_ssid;
	struct cfg80211_crypto_settings crypto;
	bool privacy;
	enum nl80211_auth_type auth_type;
	enum nl80211_smps_mode smps_mode;
	int inactivity_timeout;
	u8 p2p_ctwindow;
	bool p2p_opp_ps;
	const struct cfg80211_acl_data *acl;
	bool pbss;
	struct cfg80211_bitrate_mask beacon_rate;
};

/**
 * struct cfg80211_csa_settings - channel switch settings
 *
 * Used for channel switch
 *
 * @chandef: defines the channel to use after the switch
 * @beacon_csa: beacon data while performing the switch
 * @counter_offsets_beacon: offsets of the counters within the beacon (tail)
 * @counter_offsets_presp: offsets of the counters within the probe response
 * @n_counter_offsets_beacon: number of csa counters the beacon (tail)
 * @n_counter_offsets_presp: number of csa counters in the probe response
 * @beacon_after: beacon data to be used on the new channel
 * @radar_required: whether radar detection is required on the new channel
 * @block_tx: whether transmissions should be blocked while changing
 * @count: number of beacons until switch
 */
struct cfg80211_csa_settings {
	struct cfg80211_chan_def chandef;
	struct cfg80211_beacon_data beacon_csa;
	const u16 *counter_offsets_beacon;
	const u16 *counter_offsets_presp;
	unsigned int n_counter_offsets_beacon;
	unsigned int n_counter_offsets_presp;
	struct cfg80211_beacon_data beacon_after;
	bool radar_required;
	bool block_tx;
	u8 count;
};

/**
 * struct iface_combination_params - input parameters for interface combinations
 *
 * Used to pass interface combination parameters
 *
 * @num_different_channels: the number of different channels we want
 *	to use for verification
 * @radar_detect: a bitmap where each bit corresponds to a channel
 *	width where radar detection is needed, as in the definition of
 *	&struct ieee80211_iface_combination.@radar_detect_widths
 * @iftype_num: array with the number of interfaces of each interface
 *	type.  The index is the interface type as specified in &enum
 *	nl80211_iftype.
 * @beacon_int_gcd: a value specifying GCD of all beaconing interfaces,
 *	the GCD of a single value is considered the value itself, so for
 *	a single interface this should be set to that interface's beacon
 *	interval
 * @beacon_int_different: a flag indicating whether or not all beacon
 *	intervals (of beaconing interfaces) are different or not.
 */
struct iface_combination_params {
	int num_different_channels;
	u8 radar_detect;
	int iftype_num[NUM_NL80211_IFTYPES];
	u32 beacon_int_gcd;
	bool beacon_int_different;
};

/**
 * enum station_parameters_apply_mask - station parameter values to apply
 * @STATION_PARAM_APPLY_UAPSD: apply new uAPSD parameters (uapsd_queues, max_sp)
 * @STATION_PARAM_APPLY_CAPABILITY: apply new capability
 * @STATION_PARAM_APPLY_PLINK_STATE: apply new plink state
 *
 * Not all station parameters have in-band "no change" signalling,
 * for those that don't these flags will are used.
 */
enum station_parameters_apply_mask {
	STATION_PARAM_APPLY_UAPSD = BIT(0),
	STATION_PARAM_APPLY_CAPABILITY = BIT(1),
	STATION_PARAM_APPLY_PLINK_STATE = BIT(2),
};

/**
 * struct station_parameters - station parameters
 *
 * Used to change and create a new station.
 *
 * @vlan: vlan interface station should belong to
 * @supported_rates: supported rates in IEEE 802.11 format
 *	(or NULL for no change)
 * @supported_rates_len: number of supported rates
 * @sta_flags_mask: station flags that changed
 *	(bitmask of BIT(NL80211_STA_FLAG_...))
 * @sta_flags_set: station flags values
 *	(bitmask of BIT(NL80211_STA_FLAG_...))
 * @listen_interval: listen interval or -1 for no change
 * @aid: AID or zero for no change
 * @plink_action: plink action to take
 * @plink_state: set the peer link state for a station
 * @ht_capa: HT capabilities of station
 * @vht_capa: VHT capabilities of station
 * @uapsd_queues: bitmap of queues configured for uapsd. same format
 *	as the AC bitmap in the QoS info field
 * @max_sp: max Service Period. same format as the MAX_SP in the
 *	QoS info field (but already shifted down)
 * @sta_modify_mask: bitmap indicating which parameters changed
 *	(for those that don't have a natural "no change" value),
 *	see &enum station_parameters_apply_mask
 * @local_pm: local link-specific mesh power save mode (no change when set
 *	to unknown)
 * @capability: station capability
 * @ext_capab: extended capabilities of the station
 * @ext_capab_len: number of extended capabilities
 * @supported_channels: supported channels in IEEE 802.11 format
 * @supported_channels_len: number of supported channels
 * @supported_oper_classes: supported oper classes in IEEE 802.11 format
 * @supported_oper_classes_len: number of supported operating classes
 * @opmode_notif: operating mode field from Operating Mode Notification
 * @opmode_notif_used: information if operating mode field is used
 */
struct station_parameters {
	const u8 *supported_rates;
	struct net_device *vlan;
	u32 sta_flags_mask, sta_flags_set;
	u32 sta_modify_mask;
	int listen_interval;
	u16 aid;
	u8 supported_rates_len;
	u8 plink_action;
	u8 plink_state;
	const struct ieee80211_ht_cap *ht_capa;
	const struct ieee80211_vht_cap *vht_capa;
	u8 uapsd_queues;
	u8 max_sp;
	enum nl80211_mesh_power_mode local_pm;
	u16 capability;
	const u8 *ext_capab;
	u8 ext_capab_len;
	const u8 *supported_channels;
	u8 supported_channels_len;
	const u8 *supported_oper_classes;
	u8 supported_oper_classes_len;
	u8 opmode_notif;
	bool opmode_notif_used;
};

/**
 * struct station_del_parameters - station deletion parameters
 *
 * Used to delete a station entry (or all stations).
 *
 * @mac: MAC address of the station to remove or NULL to remove all stations
 * @subtype: Management frame subtype to use for indicating removal
 *	(10 = Disassociation, 12 = Deauthentication)
 * @reason_code: Reason code for the Disassociation/Deauthentication frame
 */
struct station_del_parameters {
	const u8 *mac;
	u8 subtype;
	u16 reason_code;
};

/**
 * enum cfg80211_station_type - the type of station being modified
 * @CFG80211_STA_AP_CLIENT: client of an AP interface
 * @CFG80211_STA_AP_MLME_CLIENT: client of an AP interface that has
 *	the AP MLME in the device
 * @CFG80211_STA_AP_STA: AP station on managed interface
 * @CFG80211_STA_IBSS: IBSS station
 * @CFG80211_STA_TDLS_PEER_SETUP: TDLS peer on managed interface (dummy entry
 *	while TDLS setup is in progress, it moves out of this state when
 *	being marked authorized; use this only if TDLS with external setup is
 *	supported/used)
 * @CFG80211_STA_TDLS_PEER_ACTIVE: TDLS peer on managed interface (active
 *	entry that is operating, has been marked authorized by userspace)
 * @CFG80211_STA_MESH_PEER_KERNEL: peer on mesh interface (kernel managed)
 * @CFG80211_STA_MESH_PEER_USER: peer on mesh interface (user managed)
 */
enum cfg80211_station_type {
	CFG80211_STA_AP_CLIENT,
	CFG80211_STA_AP_MLME_CLIENT,
	CFG80211_STA_AP_STA,
	CFG80211_STA_IBSS,
	CFG80211_STA_TDLS_PEER_SETUP,
	CFG80211_STA_TDLS_PEER_ACTIVE,
	CFG80211_STA_MESH_PEER_KERNEL,
	CFG80211_STA_MESH_PEER_USER,
};

/**
 * cfg80211_check_station_change - validate parameter changes
 * @wiphy: the wiphy this operates on
 * @params: the new parameters for a station
 * @statype: the type of station being modified
 *
 * Utility function for the @change_station driver method. Call this function
 * with the appropriate station type looking up the station (and checking that
 * it exists). It will verify whether the station change is acceptable, and if
 * not will return an error code. Note that it may modify the parameters for
 * backward compatibility reasons, so don't use them before calling this.
 */
int cfg80211_check_station_change(struct wiphy *wiphy,
				  struct station_parameters *params,
				  enum cfg80211_station_type statype);

/**
 * enum station_info_flags - station information flags
 *
 * Used by the driver to indicate which info in &struct station_info
 * it has filled in during get_station() or dump_station().
 *
 * @STATION_INFO_INACTIVE_TIME: @inactive_time filled
 * @STATION_INFO_RX_BYTES: @rx_bytes filled
 * @STATION_INFO_TX_BYTES: @tx_bytes filled
 * @STATION_INFO_RX_BYTES64: @rx_bytes filled with 64-bit value
 * @STATION_INFO_TX_BYTES64: @tx_bytes filled with 64-bit value
 * @STATION_INFO_LLID: @llid filled
 * @STATION_INFO_PLID: @plid filled
 * @STATION_INFO_PLINK_STATE: @plink_state filled
 * @STATION_INFO_SIGNAL: @signal filled
 * @STATION_INFO_TX_BITRATE: @txrate fields are filled
 *	(tx_bitrate, tx_bitrate_flags and tx_bitrate_mcs)
 * @STATION_INFO_RX_PACKETS: @rx_packets filled with 32-bit value
 * @STATION_INFO_TX_PACKETS: @tx_packets filled with 32-bit value
 * @STATION_INFO_TX_RETRIES: @tx_retries filled
 * @STATION_INFO_TX_FAILED: @tx_failed filled
 * @STATION_INFO_RX_DROP_MISC: @rx_dropped_misc filled
 * @STATION_INFO_SIGNAL_AVG: @signal_avg filled
 * @STATION_INFO_RX_BITRATE: @rxrate fields are filled
 * @STATION_INFO_BSS_PARAM: @bss_param filled
 * @STATION_INFO_CONNECTED_TIME: @connected_time filled
 * @STATION_INFO_ASSOC_REQ_IES: @assoc_req_ies filled
 * @STATION_INFO_STA_FLAGS: @sta_flags filled
 * @STATION_INFO_BEACON_LOSS_COUNT: @beacon_loss_count filled
 * @STATION_INFO_T_OFFSET: @t_offset filled
 * @STATION_INFO_LOCAL_PM: @local_pm filled
 * @STATION_INFO_PEER_PM: @peer_pm filled
 * @STATION_INFO_NONPEER_PM: @nonpeer_pm filled
 * @STATION_INFO_CHAIN_SIGNAL: @chain_signal filled
 * @STATION_INFO_CHAIN_SIGNAL_AVG: @chain_signal_avg filled
 * @STATION_INFO_EXPECTED_THROUGHPUT: @expected_throughput filled
 */
enum station_info_flags {
	STATION_INFO_INACTIVE_TIME		= BIT(0),
	STATION_INFO_RX_BYTES			= BIT(1),
	STATION_INFO_TX_BYTES			= BIT(2),
	STATION_INFO_LLID			= BIT(3),
	STATION_INFO_PLID			= BIT(4),
	STATION_INFO_PLINK_STATE		= BIT(5),
	STATION_INFO_SIGNAL			= BIT(6),
	STATION_INFO_TX_BITRATE			= BIT(7),
	STATION_INFO_RX_PACKETS			= BIT(8),
	STATION_INFO_TX_PACKETS			= BIT(9),
	STATION_INFO_TX_RETRIES			= BIT(10),
	STATION_INFO_TX_FAILED			= BIT(11),
	STATION_INFO_RX_DROP_MISC		= BIT(12),
	STATION_INFO_SIGNAL_AVG			= BIT(13),
	STATION_INFO_RX_BITRATE			= BIT(14),
	STATION_INFO_BSS_PARAM			= BIT(15),
	STATION_INFO_CONNECTED_TIME		= BIT(16),
	STATION_INFO_ASSOC_REQ_IES		= BIT(17),
	STATION_INFO_STA_FLAGS			= BIT(18),
	STATION_INFO_BEACON_LOSS_COUNT		= BIT(19),
	STATION_INFO_T_OFFSET			= BIT(20),
	STATION_INFO_LOCAL_PM			= BIT(21),
	STATION_INFO_PEER_PM			= BIT(22),
	STATION_INFO_NONPEER_PM			= BIT(23),
	STATION_INFO_RX_BYTES64			= BIT(24),
	STATION_INFO_TX_BYTES64			= BIT(25),
	STATION_INFO_CHAIN_SIGNAL		= BIT(26),
	STATION_INFO_CHAIN_SIGNAL_AVG		= BIT(27),
	STATION_INFO_EXPECTED_THROUGHPUT	= BIT(28),
};

/**
 * enum station_info_rate_flags - bitrate info flags
 *
 * Used by the driver to indicate the specific rate transmission
 * type for 802.11n transmissions.
 *
 * @RATE_INFO_FLAGS_MCS: mcs field filled with HT MCS
 * @RATE_INFO_FLAGS_VHT_MCS: mcs field filled with VHT MCS
 * @RATE_INFO_FLAGS_40_MHZ_WIDTH: 40 MHz width transmission
 * @RATE_INFO_FLAGS_80_MHZ_WIDTH: 80 MHz width transmission
 * @RATE_INFO_FLAGS_80P80_MHZ_WIDTH: 80+80 MHz width transmission
 * @RATE_INFO_FLAGS_160_MHZ_WIDTH: 160 MHz width transmission
 * @RATE_INFO_FLAGS_SHORT_GI: 400ns guard interval
 * @RATE_INFO_FLAGS_60G: 60GHz MCS
 */
enum rate_info_flags {
	RATE_INFO_FLAGS_MCS			= BIT(0),
	RATE_INFO_FLAGS_VHT_MCS			= BIT(1),
	RATE_INFO_FLAGS_40_MHZ_WIDTH		= BIT(2),
	RATE_INFO_FLAGS_80_MHZ_WIDTH		= BIT(3),
	RATE_INFO_FLAGS_80P80_MHZ_WIDTH		= BIT(4),
	RATE_INFO_FLAGS_160_MHZ_WIDTH		= BIT(5),
	RATE_INFO_FLAGS_SHORT_GI		= BIT(6),
	RATE_INFO_FLAGS_60G			= BIT(7),
};

/**
 * struct rate_info - bitrate information
 *
 * Information about a receiving or transmitting bitrate
 *
 * @flags: bitflag of flags from &enum rate_info_flags
 * @mcs: mcs index if struct describes a 802.11n bitrate
 * @legacy: bitrate in 100kbit/s for 802.11abg
 * @nss: number of streams (VHT only)
 */
struct rate_info {
	u8 flags;
	u8 mcs;
	u16 legacy;
	u8 nss;
};

/**
 * enum station_info_rate_flags - bitrate info flags
 *
 * Used by the driver to indicate the specific rate transmission
 * type for 802.11n transmissions.
 *
 * @BSS_PARAM_FLAGS_CTS_PROT: whether CTS protection is enabled
 * @BSS_PARAM_FLAGS_SHORT_PREAMBLE: whether short preamble is enabled
 * @BSS_PARAM_FLAGS_SHORT_SLOT_TIME: whether short slot time is enabled
 */
enum bss_param_flags {
	BSS_PARAM_FLAGS_CTS_PROT	= 1<<0,
	BSS_PARAM_FLAGS_SHORT_PREAMBLE	= 1<<1,
	BSS_PARAM_FLAGS_SHORT_SLOT_TIME	= 1<<2,
};

/**
 * struct sta_bss_parameters - BSS parameters for the attached station
 *
 * Information about the currently associated BSS
 *
 * @flags: bitflag of flags from &enum bss_param_flags
 * @dtim_period: DTIM period for the BSS
 * @beacon_interval: beacon interval
 */
struct sta_bss_parameters {
	u8 flags;
	u8 dtim_period;
	u16 beacon_interval;
};

#define IEEE80211_MAX_CHAINS	4

/**
 * struct station_info - station information
 *
 * Station information filled by driver for get_station() and dump_station.
 *
 * @filled: bitflag of flags from &enum station_info_flags
 * @connected_time: time(in secs) since a station is last connected
 * @inactive_time: time since last station activity (tx/rx) in milliseconds
 * @rx_bytes: bytes received from this station
 * @tx_bytes: bytes transmitted to this station
 * @llid: mesh local link id
 * @plid: mesh peer link id
 * @plink_state: mesh peer link state
 * @signal: The signal strength, type depends on the wiphy's signal_type.
 *	For CFG80211_SIGNAL_TYPE_MBM, value is expressed in _dBm_.
 * @signal_avg: Average signal strength, type depends on the wiphy's signal_type.
 *	For CFG80211_SIGNAL_TYPE_MBM, value is expressed in _dBm_.
 * @chains: bitmask for filled values in @chain_signal, @chain_signal_avg
 * @chain_signal: per-chain signal strength of last received packet in dBm
 * @chain_signal_avg: per-chain signal strength average in dBm
 * @txrate: current unicast bitrate from this station
 * @rxrate: current unicast bitrate to this station
 * @rx_packets: packets received from this station
 * @tx_packets: packets transmitted to this station
 * @tx_retries: cumulative retry counts
 * @tx_failed: number of failed transmissions (retries exceeded, no ACK)
 * @rx_dropped_misc:  Dropped for un-specified reason.
 * @bss_param: current BSS parameters
 * @generation: generation number for nl80211 dumps.
 *	This number should increase every time the list of stations
 *	changes, i.e. when a station is added or removed, so that
 *	userspace can tell whether it got a consistent snapshot.
 * @assoc_req_ies: IEs from (Re)Association Request.
 *	This is used only when in AP mode with drivers that do not use
 *	user space MLME/SME implementation. The information is provided for
 *	the cfg80211_new_sta() calls to notify user space of the IEs.
 * @assoc_req_ies_len: Length of assoc_req_ies buffer in octets.
 * @sta_flags: station flags mask & values
 * @beacon_loss_count: Number of times beacon loss event has triggered.
 * @t_offset: Time offset of the station relative to this host.
 * @local_pm: local mesh STA power save mode
 * @peer_pm: peer mesh STA power save mode
 * @nonpeer_pm: non-peer mesh STA power save mode
 * @expected_throughput: expected throughput in kbps (including 802.11 headers)
 *	towards this station.
 */
struct station_info {
	u32 filled;
	u32 connected_time;
	u32 inactive_time;
	u64 rx_bytes;
	u64 tx_bytes;
	u16 llid;
	u16 plid;
	u8 plink_state;
	s8 signal;
	s8 signal_avg;

	u8 chains;
	s8 chain_signal[IEEE80211_MAX_CHAINS];
	s8 chain_signal_avg[IEEE80211_MAX_CHAINS];

	struct rate_info txrate;
	struct rate_info rxrate;
	u32 rx_packets;
	u32 tx_packets;
	u32 tx_retries;
	u32 tx_failed;
	u32 rx_dropped_misc;
	struct sta_bss_parameters bss_param;
	struct nl80211_sta_flag_update sta_flags;

	int generation;

	const u8 *assoc_req_ies;
	size_t assoc_req_ies_len;

	u32 beacon_loss_count;
	s64 t_offset;
	enum nl80211_mesh_power_mode local_pm;
	enum nl80211_mesh_power_mode peer_pm;
	enum nl80211_mesh_power_mode nonpeer_pm;

	u32 expected_throughput;

	/*
	 * Note: Add a new enum station_info_flags value for each new field and
	 * use it to check which fields are initialized.
	 */
};

/**
 * cfg80211_get_station - retrieve information about a given station
 * @dev: the device where the station is supposed to be connected to
 * @mac_addr: the mac address of the station of interest
 * @sinfo: pointer to the structure to fill with the information
 *
 * Returns 0 on success and sinfo is filled with the available information
 * otherwise returns a negative error code and the content of sinfo has to be
 * considered undefined.
 */
int cfg80211_get_station(struct net_device *dev, const u8 *mac_addr,
			 struct station_info *sinfo);

/**
 * enum monitor_flags - monitor flags
 *
 * Monitor interface configuration flags. Note that these must be the bits
 * according to the nl80211 flags.
 *
 * @MONITOR_FLAG_FCSFAIL: pass frames with bad FCS
 * @MONITOR_FLAG_PLCPFAIL: pass frames with bad PLCP
 * @MONITOR_FLAG_CONTROL: pass control frames
 * @MONITOR_FLAG_OTHER_BSS: disable BSSID filtering
 * @MONITOR_FLAG_COOK_FRAMES: report frames after processing
 * @MONITOR_FLAG_ACTIVE: active monitor, ACKs frames on its MAC address
 */
enum monitor_flags {
	MONITOR_FLAG_FCSFAIL		= 1<<NL80211_MNTR_FLAG_FCSFAIL,
	MONITOR_FLAG_PLCPFAIL		= 1<<NL80211_MNTR_FLAG_PLCPFAIL,
	MONITOR_FLAG_CONTROL		= 1<<NL80211_MNTR_FLAG_CONTROL,
	MONITOR_FLAG_OTHER_BSS		= 1<<NL80211_MNTR_FLAG_OTHER_BSS,
	MONITOR_FLAG_COOK_FRAMES	= 1<<NL80211_MNTR_FLAG_COOK_FRAMES,
	MONITOR_FLAG_ACTIVE		= 1<<NL80211_MNTR_FLAG_ACTIVE,
};

/**
 * enum mpath_info_flags -  mesh path information flags
 *
 * Used by the driver to indicate which info in &struct mpath_info it has filled
 * in during get_station() or dump_station().
 *
 * @MPATH_INFO_FRAME_QLEN: @frame_qlen filled
 * @MPATH_INFO_SN: @sn filled
 * @MPATH_INFO_METRIC: @metric filled
 * @MPATH_INFO_EXPTIME: @exptime filled
 * @MPATH_INFO_DISCOVERY_TIMEOUT: @discovery_timeout filled
 * @MPATH_INFO_DISCOVERY_RETRIES: @discovery_retries filled
 * @MPATH_INFO_FLAGS: @flags filled
 */
enum mpath_info_flags {
	MPATH_INFO_FRAME_QLEN		= BIT(0),
	MPATH_INFO_SN			= BIT(1),
	MPATH_INFO_METRIC		= BIT(2),
	MPATH_INFO_EXPTIME		= BIT(3),
	MPATH_INFO_DISCOVERY_TIMEOUT	= BIT(4),
	MPATH_INFO_DISCOVERY_RETRIES	= BIT(5),
	MPATH_INFO_FLAGS		= BIT(6),
};

/**
 * struct mpath_info - mesh path information
 *
 * Mesh path information filled by driver for get_mpath() and dump_mpath().
 *
 * @filled: bitfield of flags from &enum mpath_info_flags
 * @frame_qlen: number of queued frames for this destination
 * @sn: target sequence number
 * @metric: metric (cost) of this mesh path
 * @exptime: expiration time for the mesh path from now, in msecs
 * @flags: mesh path flags
 * @discovery_timeout: total mesh path discovery timeout, in msecs
 * @discovery_retries: mesh path discovery retries
 * @generation: generation number for nl80211 dumps.
 *	This number should increase every time the list of mesh paths
 *	changes, i.e. when a station is added or removed, so that
 *	userspace can tell whether it got a consistent snapshot.
 */
struct mpath_info {
	u32 filled;
	u32 frame_qlen;
	u32 sn;
	u32 metric;
	u32 exptime;
	u32 discovery_timeout;
	u8 discovery_retries;
	u8 flags;

	int generation;
};

/**
 * struct bss_parameters - BSS parameters
 *
 * Used to change BSS parameters (mainly for AP mode).
 *
 * @use_cts_prot: Whether to use CTS protection
 *	(0 = no, 1 = yes, -1 = do not change)
 * @use_short_preamble: Whether the use of short preambles is allowed
 *	(0 = no, 1 = yes, -1 = do not change)
 * @use_short_slot_time: Whether the use of short slot time is allowed
 *	(0 = no, 1 = yes, -1 = do not change)
 * @basic_rates: basic rates in IEEE 802.11 format
 *	(or NULL for no change)
 * @basic_rates_len: number of basic rates
 * @ap_isolate: do not forward packets between connected stations
 * @ht_opmode: HT Operation mode
 * 	(u16 = opmode, -1 = do not change)
 * @p2p_ctwindow: P2P CT Window (-1 = no change)
 * @p2p_opp_ps: P2P opportunistic PS (-1 = no change)
 */
struct bss_parameters {
	int use_cts_prot;
	int use_short_preamble;
	int use_short_slot_time;
	const u8 *basic_rates;
	u8 basic_rates_len;
	int ap_isolate;
	int ht_opmode;
	s8 p2p_ctwindow, p2p_opp_ps;
};

/**
 * struct mesh_config - 802.11s mesh configuration
 *
 * These parameters can be changed while the mesh is active.
 *
 * @dot11MeshRetryTimeout: the initial retry timeout in millisecond units used
 *	by the Mesh Peering Open message
 * @dot11MeshConfirmTimeout: the initial retry timeout in millisecond units
 *	used by the Mesh Peering Open message
 * @dot11MeshHoldingTimeout: the confirm timeout in millisecond units used by
 *	the mesh peering management to close a mesh peering
 * @dot11MeshMaxPeerLinks: the maximum number of peer links allowed on this
 *	mesh interface
 * @dot11MeshMaxRetries: the maximum number of peer link open retries that can
 *	be sent to establish a new peer link instance in a mesh
 * @dot11MeshTTL: the value of TTL field set at a source mesh STA
 * @element_ttl: the value of TTL field set at a mesh STA for path selection
 *	elements
 * @auto_open_plinks: whether we should automatically open peer links when we
 *	detect compatible mesh peers
 * @dot11MeshNbrOffsetMaxNeighbor: the maximum number of neighbors to
 *	synchronize to for 11s default synchronization method
 * @dot11MeshHWMPmaxPREQretries: the number of action frames containing a PREQ
 *	that an originator mesh STA can send to a particular path target
 * @path_refresh_time: how frequently to refresh mesh paths in milliseconds
 * @min_discovery_timeout: the minimum length of time to wait until giving up on
 *	a path discovery in milliseconds
 * @dot11MeshHWMPactivePathTimeout: the time (in TUs) for which mesh STAs
 *	receiving a PREQ shall consider the forwarding information from the
 *	root to be valid. (TU = time unit)
 * @dot11MeshHWMPpreqMinInterval: the minimum interval of time (in TUs) during
 *	which a mesh STA can send only one action frame containing a PREQ
 *	element
 * @dot11MeshHWMPperrMinInterval: the minimum interval of time (in TUs) during
 *	which a mesh STA can send only one Action frame containing a PERR
 *	element
 * @dot11MeshHWMPnetDiameterTraversalTime: the interval of time (in TUs) that
 *	it takes for an HWMP information element to propagate across the mesh
 * @dot11MeshHWMPRootMode: the configuration of a mesh STA as root mesh STA
 * @dot11MeshHWMPRannInterval: the interval of time (in TUs) between root
 *	announcements are transmitted
 * @dot11MeshGateAnnouncementProtocol: whether to advertise that this mesh
 *	station has access to a broader network beyond the MBSS. (This is
 *	missnamed in draft 12.0: dot11MeshGateAnnouncementProtocol set to true
 *	only means that the station will announce others it's a mesh gate, but
 *	not necessarily using the gate announcement protocol. Still keeping the
 *	same nomenclature to be in sync with the spec)
 * @dot11MeshForwarding: whether the Mesh STA is forwarding or non-forwarding
 *	entity (default is TRUE - forwarding entity)
 * @rssi_threshold: the threshold for average signal strength of candidate
 *	station to establish a peer link
 * @ht_opmode: mesh HT protection mode
 *
 * @dot11MeshHWMPactivePathToRootTimeout: The time (in TUs) for which mesh STAs
 *	receiving a proactive PREQ shall consider the forwarding information to
 *	the root mesh STA to be valid.
 *
 * @dot11MeshHWMProotInterval: The interval of time (in TUs) between proactive
 *	PREQs are transmitted.
 * @dot11MeshHWMPconfirmationInterval: The minimum interval of time (in TUs)
 *	during which a mesh STA can send only one Action frame containing
 *	a PREQ element for root path confirmation.
 * @power_mode: The default mesh power save mode which will be the initial
 *	setting for new peer links.
 * @dot11MeshAwakeWindowDuration: The duration in TUs the STA will remain awake
 *	after transmitting its beacon.
 * @plink_timeout: If no tx activity is seen from a STA we've established
 *	peering with for longer than this time (in seconds), then remove it
 *	from the STA's list of peers.  Default is 30 minutes.
 */
struct mesh_config {
	u16 dot11MeshRetryTimeout;
	u16 dot11MeshConfirmTimeout;
	u16 dot11MeshHoldingTimeout;
	u16 dot11MeshMaxPeerLinks;
	u8 dot11MeshMaxRetries;
	u8 dot11MeshTTL;
	u8 element_ttl;
	bool auto_open_plinks;
	u32 dot11MeshNbrOffsetMaxNeighbor;
	u8 dot11MeshHWMPmaxPREQretries;
	u32 path_refresh_time;
	u16 min_discovery_timeout;
	u32 dot11MeshHWMPactivePathTimeout;
	u16 dot11MeshHWMPpreqMinInterval;
	u16 dot11MeshHWMPperrMinInterval;
	u16 dot11MeshHWMPnetDiameterTraversalTime;
	u8 dot11MeshHWMPRootMode;
	u16 dot11MeshHWMPRannInterval;
	bool dot11MeshGateAnnouncementProtocol;
	bool dot11MeshForwarding;
	s32 rssi_threshold;
	u16 ht_opmode;
	u32 dot11MeshHWMPactivePathToRootTimeout;
	u16 dot11MeshHWMProotInterval;
	u16 dot11MeshHWMPconfirmationInterval;
	enum nl80211_mesh_power_mode power_mode;
	u16 dot11MeshAwakeWindowDuration;
	u32 plink_timeout;
};

/**
 * struct mesh_setup - 802.11s mesh setup configuration
 * @chandef: defines the channel to use
 * @mesh_id: the mesh ID
 * @mesh_id_len: length of the mesh ID, at least 1 and at most 32 bytes
 * @sync_method: which synchronization method to use
 * @path_sel_proto: which path selection protocol to use
 * @path_metric: which metric to use
 * @auth_id: which authentication method this mesh is using
 * @ie: vendor information elements (optional)
 * @ie_len: length of vendor information elements
 * @is_authenticated: this mesh requires authentication
 * @is_secure: this mesh uses security
 * @user_mpm: userspace handles all MPM functions
 * @dtim_period: DTIM period to use
 * @beacon_interval: beacon interval to use
 * @mcast_rate: multicat rate for Mesh Node [6Mbps is the default for 802.11a]
 * @basic_rates: basic rates to use when creating the mesh
 * @beacon_rate: bitrate to be used for beacons
 *
 * These parameters are fixed when the mesh is created.
 */
struct mesh_setup {
	struct cfg80211_chan_def chandef;
	const u8 *mesh_id;
	u8 mesh_id_len;
	u8 sync_method;
	u8 path_sel_proto;
	u8 path_metric;
	u8 auth_id;
	const u8 *ie;
	u8 ie_len;
	bool is_authenticated;
	bool is_secure;
	bool user_mpm;
	u8 dtim_period;
	u16 beacon_interval;
	int mcast_rate[IEEE80211_NUM_BANDS];
	u32 basic_rates;
	struct cfg80211_bitrate_mask beacon_rate;
};

/**
 * struct ieee80211_txq_params - TX queue parameters
 * @ac: AC identifier
 * @txop: Maximum burst time in units of 32 usecs, 0 meaning disabled
 * @cwmin: Minimum contention window [a value of the form 2^n-1 in the range
 *	1..32767]
 * @cwmax: Maximum contention window [a value of the form 2^n-1 in the range
 *	1..32767]
 * @aifs: Arbitration interframe space [0..255]
 */
struct ieee80211_txq_params {
	enum nl80211_ac ac;
	u16 txop;
	u16 cwmin;
	u16 cwmax;
	u8 aifs;
};

/**
 * DOC: Scanning and BSS list handling
 *
 * The scanning process itself is fairly simple, but cfg80211 offers quite
 * a bit of helper functionality. To start a scan, the scan operation will
 * be invoked with a scan definition. This scan definition contains the
 * channels to scan, and the SSIDs to send probe requests for (including the
 * wildcard, if desired). A passive scan is indicated by having no SSIDs to
 * probe. Additionally, a scan request may contain extra information elements
 * that should be added to the probe request. The IEs are guaranteed to be
 * well-formed, and will not exceed the maximum length the driver advertised
 * in the wiphy structure.
 *
 * When scanning finds a BSS, cfg80211 needs to be notified of that, because
 * it is responsible for maintaining the BSS list; the driver should not
 * maintain a list itself. For this notification, various functions exist.
 *
 * Since drivers do not maintain a BSS list, there are also a number of
 * functions to search for a BSS and obtain information about it from the
 * BSS structure cfg80211 maintains. The BSS list is also made available
 * to userspace.
 */

/**
 * struct cfg80211_ssid - SSID description
 * @ssid: the SSID
 * @ssid_len: length of the ssid
 */
struct cfg80211_ssid {
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 ssid_len;
};

/**
 * struct cfg80211_scan_request - scan request description
 *
 * @ssids: SSIDs to scan for (active scan only)
 * @n_ssids: number of SSIDs
 * @channels: channels to scan on.
 * @n_channels: total number of channels to scan
 * @scan_width: channel width for scanning
 * @ie: optional information element(s) to add into Probe Request or %NULL
 * @ie_len: length of ie in octets
 * @flags: bit field of flags controlling operation
 * @rates: bitmap of rates to advertise for each band
 * @wiphy: the wiphy this was for
 * @scan_start: time (in jiffies) when the scan started
 * @wdev: the wireless device to scan for
 * @aborted: (internal) scan request was notified as aborted
 * @notified: (internal) scan request was notified as done or aborted
 * @no_cck: used to send probe requests at non CCK rate in 2GHz band
 * @mac_addr: MAC address used with randomisation
 * @mac_addr_mask: MAC address mask used with randomisation, bits that
 *	are 0 in the mask should be randomised, bits that are 1 should
 *	be taken from the @mac_addr
 * @bssid: BSSID to scan for (most commonly, the wildcard BSSID)
 */
struct cfg80211_scan_request {
	struct cfg80211_ssid *ssids;
	int n_ssids;
	u32 n_channels;
	enum nl80211_bss_scan_width scan_width;
	const u8 *ie;
	size_t ie_len;
	u32 flags;

	u32 rates[IEEE80211_NUM_BANDS];

	struct wireless_dev *wdev;

	u8 bssid[ETH_ALEN] __aligned(2);

	u8 mac_addr[ETH_ALEN] __aligned(2);
	u8 mac_addr_mask[ETH_ALEN] __aligned(2);

	/* internal */
	struct wiphy *wiphy;
	unsigned long scan_start;
	bool aborted, notified;
	bool no_cck;

	/* keep last */
	struct ieee80211_channel *channels[0];
};

static inline void get_random_mask_addr(u8 *buf, const u8 *addr, const u8 *mask)
{
	int i;

	get_random_bytes(buf, ETH_ALEN);
	for (i = 0; i < ETH_ALEN; i++) {
		buf[i] &= ~mask[i];
		buf[i] |= addr[i] & mask[i];
	}
}

/**
 * struct cfg80211_match_set - sets of attributes to match
 *
 * @ssid: SSID to be matched; may be zero-length for no match (RSSI only)
 * @rssi_thold: don't report scan results below this threshold (in s32 dBm)
 */
struct cfg80211_match_set {
	struct cfg80211_ssid ssid;
	s32 rssi_thold;
};

/**
 * struct cfg80211_sched_scan_plan - scan plan for scheduled scan
 *
 * @interval: interval between scheduled scan iterations. In seconds.
 * @iterations: number of scan iterations in this scan plan. Zero means
 *	infinite loop.
 *	The last scan plan will always have this parameter set to zero,
 *	all other scan plans will have a finite number of iterations.
 */
struct cfg80211_sched_scan_plan {
	u32 interval;
	u32 iterations;
};

/**
 * struct cfg80211_bss_select_adjust - BSS selection with RSSI adjustment.
 *
 * @band: band of BSS which should match for RSSI level adjustment.
 * @delta: value of RSSI level adjustment.
 */
struct cfg80211_bss_select_adjust {
	enum nl80211_band band;
	s8 delta;
};

/**
 * struct cfg80211_sched_scan_request - scheduled scan request description
 *
 * @ssids: SSIDs to scan for (passed in the probe_reqs in active scans)
 * @n_ssids: number of SSIDs
 * @n_channels: total number of channels to scan
 * @scan_width: channel width for scanning
 * @ie: optional information element(s) to add into Probe Request or %NULL
 * @ie_len: length of ie in octets
 * @flags: bit field of flags controlling operation
 * @match_sets: sets of parameters to be matched for a scan result
 * 	entry to be considered valid and to be passed to the host
 * 	(others are filtered out).
 *	If ommited, all results are passed.
 * @n_match_sets: number of match sets
 * @wiphy: the wiphy this was for
 * @dev: the interface
 * @scan_start: start time of the scheduled scan
 * @scan_plans: scan plans to be executed in this scheduled scan. Lowest
 *	index must be executed first.
 * @n_scan_plans: number of scan plans, at least 1.
 * @channels: channels to scan
 * @min_rssi_thold: for drivers only supporting a single threshold, this
 *	contains the minimum over all matchsets
 * @mac_addr: MAC address used with randomisation
 * @mac_addr_mask: MAC address mask used with randomisation, bits that
 *	are 0 in the mask should be randomised, bits that are 1 should
 *	be taken from the @mac_addr
 * @owner_nlportid: netlink portid of owner (if this should is a request
 *	owned by a particular socket)
 * @relative_rssi_set: Indicates whether @relative_rssi is set or not.
 * @relative_rssi: Relative RSSI threshold in dB to restrict scan result
 *	reporting in connected state to cases where a matching BSS is determined
 *	to have better or slightly worse RSSI than the current connected BSS.
 *	The relative RSSI threshold values are ignored in disconnected state.
 * @rssi_adjust: delta dB of RSSI preference to be given to the BSSs that belong
 *	to the specified band while deciding whether a better BSS is reported
 *	using @relative_rssi. If delta is a negative number, the BSSs that
 *	belong to the specified band will be penalized by delta dB in relative
 *	comparisions.
 * @owner_nlportid: netlink portid of owner (if this should is a request
 *	owned by a particular socket)
 */
struct cfg80211_sched_scan_request {
	struct cfg80211_ssid *ssids;
	int n_ssids;
	u32 n_channels;
	enum nl80211_bss_scan_width scan_width;
	const u8 *ie;
	size_t ie_len;
	u32 flags;
	struct cfg80211_match_set *match_sets;
	int n_match_sets;
	s32 min_rssi_thold;

	u8 mac_addr[ETH_ALEN] __aligned(2);
	u8 mac_addr_mask[ETH_ALEN] __aligned(2);

	bool relative_rssi_set;
	s8 relative_rssi;
	struct cfg80211_bss_select_adjust rssi_adjust;

	/* internal */
	struct wiphy *wiphy;
	struct net_device *dev;
	unsigned long scan_start;
	struct cfg80211_sched_scan_plan *scan_plans;
	int n_scan_plans;

	u32 owner_nlportid;

	/* keep last */
	struct ieee80211_channel *channels[0];
};

/**
 * enum cfg80211_signal_type - signal type
 *
 * @CFG80211_SIGNAL_TYPE_NONE: no signal strength information available
 * @CFG80211_SIGNAL_TYPE_MBM: signal strength in mBm (100*dBm)
 * @CFG80211_SIGNAL_TYPE_UNSPEC: signal strength, increasing from 0 through 100
 */
enum cfg80211_signal_type {
	CFG80211_SIGNAL_TYPE_NONE,
	CFG80211_SIGNAL_TYPE_MBM,
	CFG80211_SIGNAL_TYPE_UNSPEC,
};

/**
 * struct cfg80211_inform_bss - BSS inform data
 * @chan: channel the frame was received on
 * @scan_width: scan width that was used
 * @signal: signal strength value, according to the wiphy's
 *	signal type
 * @boottime_ns: timestamp (CLOCK_BOOTTIME) when the information was
 *	received; should match the time when the frame was actually
 *	received by the device (not just by the host, in case it was
 *	buffered on the device) and be accurate to about 10ms.
 *	If the frame isn't buffered, just passing the return value of
 *	ktime_get_boot_ns() is likely appropriate.
 */
struct cfg80211_inform_bss {
	struct ieee80211_channel *chan;
	enum nl80211_bss_scan_width scan_width;
	s32 signal;
	u64 boottime_ns;
};

/**
 * struct cfg80211_bss_ie_data - BSS entry IE data
 * @tsf: TSF contained in the frame that carried these IEs
 * @rcu_head: internal use, for freeing
 * @len: length of the IEs
 * @from_beacon: these IEs are known to come from a beacon
 * @data: IE data
 */
struct cfg80211_bss_ies {
	u64 tsf;
	struct rcu_head rcu_head;
	int len;
	bool from_beacon;
	u8 data[];
};

/**
 * struct cfg80211_bss - BSS description
 *
 * This structure describes a BSS (which may also be a mesh network)
 * for use in scan results and similar.
 *
 * @channel: channel this BSS is on
 * @scan_width: width of the control channel
 * @bssid: BSSID of the BSS
 * @beacon_interval: the beacon interval as from the frame
 * @capability: the capability field in host byte order
 * @ies: the information elements (Note that there is no guarantee that these
 *	are well-formed!); this is a pointer to either the beacon_ies or
 *	proberesp_ies depending on whether Probe Response frame has been
 *	received. It is always non-%NULL.
 * @beacon_ies: the information elements from the last Beacon frame
 *	(implementation note: if @hidden_beacon_bss is set this struct doesn't
 *	own the beacon_ies, but they're just pointers to the ones from the
 *	@hidden_beacon_bss struct)
 * @proberesp_ies: the information elements from the last Probe Response frame
 * @hidden_beacon_bss: in case this BSS struct represents a probe response from
 *	a BSS that hides the SSID in its beacon, this points to the BSS struct
 *	that holds the beacon data. @beacon_ies is still valid, of course, and
 *	points to the same data as hidden_beacon_bss->beacon_ies in that case.
 * @signal: signal strength value (type depends on the wiphy's signal_type)
 * @priv: private area for driver use, has at least wiphy->bss_priv_size bytes
 */
struct cfg80211_bss {
	struct ieee80211_channel *channel;
	enum nl80211_bss_scan_width scan_width;

	const struct cfg80211_bss_ies __rcu *ies;
	const struct cfg80211_bss_ies __rcu *beacon_ies;
	const struct cfg80211_bss_ies __rcu *proberesp_ies;

	struct cfg80211_bss *hidden_beacon_bss;

	s32 signal;

	u16 beacon_interval;
	u16 capability;

	u8 bssid[ETH_ALEN];

	u8 priv[0] __aligned(sizeof(void *));
};

/**
 * ieee80211_bss_get_ie - find IE with given ID
 * @bss: the bss to search
 * @ie: the IE ID
 *
 * Note that the return value is an RCU-protected pointer, so
 * rcu_read_lock() must be held when calling this function.
 * Return: %NULL if not found.
 */
const u8 *ieee80211_bss_get_ie(struct cfg80211_bss *bss, u8 ie);


/**
 * struct cfg80211_auth_request - Authentication request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * authentication.
 *
 * @bss: The BSS to authenticate with, the callee must obtain a reference
 *	to it if it needs to keep it.
 * @auth_type: Authentication type (algorithm)
 * @ie: Extra IEs to add to Authentication frame or %NULL
 * @ie_len: Length of ie buffer in octets
 * @key_len: length of WEP key for shared key authentication
 * @key_idx: index of WEP key for shared key authentication
 * @key: WEP key for shared key authentication
 * @sae_data: Non-IE data to use with SAE or %NULL. This starts with
 *	Authentication transaction sequence number field.
 * @sae_data_len: Length of sae_data buffer in octets
 */
struct cfg80211_auth_request {
	struct cfg80211_bss *bss;
	const u8 *ie;
	size_t ie_len;
	enum nl80211_auth_type auth_type;
	const u8 *key;
	u8 key_len, key_idx;
	const u8 *sae_data;
	size_t sae_data_len;
};

/**
 * enum cfg80211_assoc_req_flags - Over-ride default behaviour in association.
 *
 * @ASSOC_REQ_DISABLE_HT:  Disable HT (802.11n)
 * @ASSOC_REQ_DISABLE_VHT:  Disable VHT
 * @ASSOC_REQ_USE_RRM: Declare RRM capability in this association
 */
enum cfg80211_assoc_req_flags {
	ASSOC_REQ_DISABLE_HT		= BIT(0),
	ASSOC_REQ_DISABLE_VHT		= BIT(1),
	ASSOC_REQ_USE_RRM		= BIT(2),
};

/**
 * struct cfg80211_assoc_request - (Re)Association request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * (re)association.
 * @bss: The BSS to associate with. If the call is successful the driver is
 *	given a reference that it must give back to cfg80211_send_rx_assoc()
 *	or to cfg80211_assoc_timeout(). To ensure proper refcounting, new
 *	association requests while already associating must be rejected.
 * @ie: Extra IEs to add to (Re)Association Request frame or %NULL
 * @ie_len: Length of ie buffer in octets
 * @use_mfp: Use management frame protection (IEEE 802.11w) in this association
 * @crypto: crypto settings
 * @prev_bssid: previous BSSID, if not %NULL use reassociate frame
 * @flags:  See &enum cfg80211_assoc_req_flags
 * @ht_capa:  HT Capabilities over-rides.  Values set in ht_capa_mask
 *	will be used in ht_capa.  Un-supported values will be ignored.
 * @ht_capa_mask:  The bits of ht_capa which are to be used.
 * @vht_capa: VHT capability override
 * @vht_capa_mask: VHT capability mask indicating which fields to use
 */
struct cfg80211_assoc_request {
	struct cfg80211_bss *bss;
	const u8 *ie, *prev_bssid;
	size_t ie_len;
	struct cfg80211_crypto_settings crypto;
	bool use_mfp;
	u32 flags;
	struct ieee80211_ht_cap ht_capa;
	struct ieee80211_ht_cap ht_capa_mask;
	struct ieee80211_vht_cap vht_capa, vht_capa_mask;
};

/**
 * struct cfg80211_deauth_request - Deauthentication request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * deauthentication.
 *
 * @bssid: the BSSID of the BSS to deauthenticate from
 * @ie: Extra IEs to add to Deauthentication frame or %NULL
 * @ie_len: Length of ie buffer in octets
 * @reason_code: The reason code for the deauthentication
 * @local_state_change: if set, change local state only and
 *	do not set a deauth frame
 */
struct cfg80211_deauth_request {
	const u8 *bssid;
	const u8 *ie;
	size_t ie_len;
	u16 reason_code;
	bool local_state_change;
};

/**
 * struct cfg80211_disassoc_request - Disassociation request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * disassocation.
 *
 * @bss: the BSS to disassociate from
 * @ie: Extra IEs to add to Disassociation frame or %NULL
 * @ie_len: Length of ie buffer in octets
 * @reason_code: The reason code for the disassociation
 * @local_state_change: This is a request for a local state only, i.e., no
 *	Disassociation frame is to be transmitted.
 */
struct cfg80211_disassoc_request {
	struct cfg80211_bss *bss;
	const u8 *ie;
	size_t ie_len;
	u16 reason_code;
	bool local_state_change;
};

/**
 * struct cfg80211_ibss_params - IBSS parameters
 *
 * This structure defines the IBSS parameters for the join_ibss()
 * method.
 *
 * @ssid: The SSID, will always be non-null.
 * @ssid_len: The length of the SSID, will always be non-zero.
 * @bssid: Fixed BSSID requested, maybe be %NULL, if set do not
 *	search for IBSSs with a different BSSID.
 * @chandef: defines the channel to use if no other IBSS to join can be found
 * @channel_fixed: The channel should be fixed -- do not search for
 *	IBSSs to join on other channels.
 * @ie: information element(s) to include in the beacon
 * @ie_len: length of that
 * @beacon_interval: beacon interval to use
 * @privacy: this is a protected network, keys will be configured
 *	after joining
 * @control_port: whether user space controls IEEE 802.1X port, i.e.,
 *	sets/clears %NL80211_STA_FLAG_AUTHORIZED. If true, the driver is
 *	required to assume that the port is unauthorized until authorized by
 *	user space. Otherwise, port is marked authorized by default.
 * @userspace_handles_dfs: whether user space controls DFS operation, i.e.
 *	changes the channel when a radar is detected. This is required
 *	to operate on DFS channels.
 * @basic_rates: bitmap of basic rates to use when creating the IBSS
 * @mcast_rate: per-band multicast rate index + 1 (0: disabled)
 * @ht_capa:  HT Capabilities over-rides.  Values set in ht_capa_mask
 *	will be used in ht_capa.  Un-supported values will be ignored.
 * @ht_capa_mask:  The bits of ht_capa which are to be used.
 */
struct cfg80211_ibss_params {
	const u8 *ssid;
	const u8 *bssid;
	struct cfg80211_chan_def chandef;
	const u8 *ie;
	u8 ssid_len, ie_len;
	u16 beacon_interval;
	u32 basic_rates;
	bool channel_fixed;
	bool privacy;
	bool control_port;
	bool userspace_handles_dfs;
	int mcast_rate[IEEE80211_NUM_BANDS];
	struct ieee80211_ht_cap ht_capa;
	struct ieee80211_ht_cap ht_capa_mask;
};

/**
 * struct cfg80211_bss_selection - connection parameters for BSS selection.
 *
 * @behaviour: requested BSS selection behaviour.
 * @param: parameters for requestion behaviour.
 * @band_pref: preferred band for %NL80211_BSS_SELECT_ATTR_BAND_PREF.
 * @adjust: parameters for %NL80211_BSS_SELECT_ATTR_RSSI_ADJUST.
 */
struct cfg80211_bss_selection {
	enum nl80211_bss_select_attr behaviour;
	union {
		enum ieee80211_band band_pref;
		struct cfg80211_bss_select_adjust adjust;
	} param;
};

/**
 * struct cfg80211_connect_params - Connection parameters
 *
 * This structure provides information needed to complete IEEE 802.11
 * authentication and association.
 *
 * @channel: The channel to use or %NULL if not specified (auto-select based
 *	on scan results)
 * @channel_hint: The channel of the recommended BSS for initial connection or
 *	%NULL if not specified
 * @bssid: The AP BSSID or %NULL if not specified (auto-select based on scan
 *	results)
 * @bssid_hint: The recommended AP BSSID for initial connection to the BSS or
 *	%NULL if not specified. Unlike the @bssid parameter, the driver is
 *	allowed to ignore this @bssid_hint if it has knowledge of a better BSS
 *	to use.
 * @ssid: SSID
 * @ssid_len: Length of ssid in octets
 * @auth_type: Authentication type (algorithm)
 * @ie: IEs for association request
 * @ie_len: Length of assoc_ie in octets
 * @privacy: indicates whether privacy-enabled APs should be used
 * @mfp: indicate whether management frame protection is used
 * @crypto: crypto settings
 * @key_len: length of WEP key for shared key authentication
 * @key_idx: index of WEP key for shared key authentication
 * @key: WEP key for shared key authentication
 * @flags:  See &enum cfg80211_assoc_req_flags
 * @bg_scan_period:  Background scan period in seconds
 *	or -1 to indicate that default value is to be used.
 * @ht_capa:  HT Capabilities over-rides.  Values set in ht_capa_mask
 *	will be used in ht_capa.  Un-supported values will be ignored.
 * @ht_capa_mask:  The bits of ht_capa which are to be used.
 * @vht_capa:  VHT Capability overrides
 * @vht_capa_mask: The bits of vht_capa which are to be used.
 * @pbss: if set, connect to a PCP instead of AP. Valid for DMG
 *	networks.
 * @bss_select: criteria to be used for BSS selection.
 * @prev_bssid: previous BSSID, if not %NULL use reassociate frame
 */
struct cfg80211_connect_params {
	struct ieee80211_channel *channel;
	struct ieee80211_channel *channel_hint;
	const u8 *bssid;
	const u8 *bssid_hint;
	const u8 *ssid;
	size_t ssid_len;
	enum nl80211_auth_type auth_type;
	const u8 *ie;
	size_t ie_len;
	bool privacy;
	enum nl80211_mfp mfp;
	struct cfg80211_crypto_settings crypto;
	const u8 *key;
	u8 key_len, key_idx;
	u32 flags;
	int bg_scan_period;
	struct ieee80211_ht_cap ht_capa;
	struct ieee80211_ht_cap ht_capa_mask;
	struct ieee80211_vht_cap vht_capa;
	struct ieee80211_vht_cap vht_capa_mask;
	bool pbss;
	struct cfg80211_bss_selection bss_select;
	const u8 *prev_bssid;
};

/**
 * enum cfg80211_connect_params_changed - Connection parameters being updated
 *
 * This enum provides information of all connect parameters that
 * have to be updated as part of update_connect_params() call.
 *
 * @UPDATE_ASSOC_IES: Indicates whether association request IEs are updated
 */
enum cfg80211_connect_params_changed {
	UPDATE_ASSOC_IES		= BIT(0),
};

/**
 * enum wiphy_params_flags - set_wiphy_params bitfield values
 * @WIPHY_PARAM_RETRY_SHORT: wiphy->retry_short has changed
 * @WIPHY_PARAM_RETRY_LONG: wiphy->retry_long has changed
 * @WIPHY_PARAM_FRAG_THRESHOLD: wiphy->frag_threshold has changed
 * @WIPHY_PARAM_RTS_THRESHOLD: wiphy->rts_threshold has changed
 * @WIPHY_PARAM_COVERAGE_CLASS: coverage class changed
 * @WIPHY_PARAM_DYN_ACK: dynack has been enabled
 */
enum wiphy_params_flags {
	WIPHY_PARAM_RETRY_SHORT		= 1 << 0,
	WIPHY_PARAM_RETRY_LONG		= 1 << 1,
	WIPHY_PARAM_FRAG_THRESHOLD	= 1 << 2,
	WIPHY_PARAM_RTS_THRESHOLD	= 1 << 3,
	WIPHY_PARAM_COVERAGE_CLASS	= 1 << 4,
	WIPHY_PARAM_DYN_ACK		= 1 << 5,
};

/**
 * struct cfg80211_pmksa - PMK Security Association
 *
 * This structure is passed to the set/del_pmksa() method for PMKSA
 * caching.
 *
 * @bssid: The AP's BSSID.
 * @pmkid: The PMK material itself.
 */
struct cfg80211_pmksa {
	const u8 *bssid;
	const u8 *pmkid;
};

/**
 * struct cfg80211_pkt_pattern - packet pattern
 * @mask: bitmask where to match pattern and where to ignore bytes,
 *	one bit per byte, in same format as nl80211
 * @pattern: bytes to match where bitmask is 1
 * @pattern_len: length of pattern (in bytes)
 * @pkt_offset: packet offset (in bytes)
 *
 * Internal note: @mask and @pattern are allocated in one chunk of
 * memory, free @mask only!
 */
struct cfg80211_pkt_pattern {
	const u8 *mask, *pattern;
	int pattern_len;
	int pkt_offset;
};

/**
 * struct cfg80211_wowlan_tcp - TCP connection parameters
 *
 * @sock: (internal) socket for source port allocation
 * @src: source IP address
 * @dst: destination IP address
 * @dst_mac: destination MAC address
 * @src_port: source port
 * @dst_port: destination port
 * @payload_len: data payload length
 * @payload: data payload buffer
 * @payload_seq: payload sequence stamping configuration
 * @data_interval: interval at which to send data packets
 * @wake_len: wakeup payload match length
 * @wake_data: wakeup payload match data
 * @wake_mask: wakeup payload match mask
 * @tokens_size: length of the tokens buffer
 * @payload_tok: payload token usage configuration
 */
struct cfg80211_wowlan_tcp {
	struct socket *sock;
	__be32 src, dst;
	u16 src_port, dst_port;
	u8 dst_mac[ETH_ALEN];
	int payload_len;
	const u8 *payload;
	struct nl80211_wowlan_tcp_data_seq payload_seq;
	u32 data_interval;
	u32 wake_len;
	const u8 *wake_data, *wake_mask;
	u32 tokens_size;
	/* must be last, variable member */
	struct nl80211_wowlan_tcp_data_token payload_tok;
};

/**
 * struct cfg80211_wowlan - Wake on Wireless-LAN support info
 *
 * This structure defines the enabled WoWLAN triggers for the device.
 * @any: wake up on any activity -- special trigger if device continues
 *	operating as normal during suspend
 * @disconnect: wake up if getting disconnected
 * @magic_pkt: wake up on receiving magic packet
 * @patterns: wake up on receiving packet matching a pattern
 * @n_patterns: number of patterns
 * @gtk_rekey_failure: wake up on GTK rekey failure
 * @eap_identity_req: wake up on EAP identity request packet
 * @four_way_handshake: wake up on 4-way handshake
 * @rfkill_release: wake up when rfkill is released
 * @tcp: TCP connection establishment/wakeup parameters, see nl80211.h.
 *	NULL if not configured.
 */
struct cfg80211_wowlan {
	bool any, disconnect, magic_pkt, gtk_rekey_failure,
	     eap_identity_req, four_way_handshake,
	     rfkill_release;
	struct cfg80211_pkt_pattern *patterns;
	struct cfg80211_wowlan_tcp *tcp;
	int n_patterns;
};

/**
 * struct cfg80211_coalesce_rules - Coalesce rule parameters
 *
 * This structure defines coalesce rule for the device.
 * @delay: maximum coalescing delay in msecs.
 * @condition: condition for packet coalescence.
 *	see &enum nl80211_coalesce_condition.
 * @patterns: array of packet patterns
 * @n_patterns: number of patterns
 */
struct cfg80211_coalesce_rules {
	int delay;
	enum nl80211_coalesce_condition condition;
	struct cfg80211_pkt_pattern *patterns;
	int n_patterns;
};

/**
 * struct cfg80211_coalesce - Packet coalescing settings
 *
 * This structure defines coalescing settings.
 * @rules: array of coalesce rules
 * @n_rules: number of rules
 */
struct cfg80211_coalesce {
	struct cfg80211_coalesce_rules *rules;
	int n_rules;
};

/**
 * struct cfg80211_wowlan_wakeup - wakeup report
 * @disconnect: woke up by getting disconnected
 * @magic_pkt: woke up by receiving magic packet
 * @gtk_rekey_failure: woke up by GTK rekey failure
 * @eap_identity_req: woke up by EAP identity request packet
 * @four_way_handshake: woke up by 4-way handshake
 * @rfkill_release: woke up by rfkill being released
 * @pattern_idx: pattern that caused wakeup, -1 if not due to pattern
 * @packet_present_len: copied wakeup packet data
 * @packet_len: original wakeup packet length
 * @packet: The packet causing the wakeup, if any.
 * @packet_80211:  For pattern match, magic packet and other data
 *	frame triggers an 802.3 frame should be reported, for
 *	disconnect due to deauth 802.11 frame. This indicates which
 *	it is.
 * @tcp_match: TCP wakeup packet received
 * @tcp_connlost: TCP connection lost or failed to establish
 * @tcp_nomoretokens: TCP data ran out of tokens
 */
struct cfg80211_wowlan_wakeup {
	bool disconnect, magic_pkt, gtk_rekey_failure,
	     eap_identity_req, four_way_handshake,
	     rfkill_release, packet_80211,
	     tcp_match, tcp_connlost, tcp_nomoretokens;
	s32 pattern_idx;
	u32 packet_present_len, packet_len;
	const void *packet;
};

/**
 * struct cfg80211_gtk_rekey_data - rekey data
 * @kek: key encryption key (NL80211_KEK_LEN bytes)
 * @kck: key confirmation key (NL80211_KCK_LEN bytes)
 * @replay_ctr: replay counter (NL80211_REPLAY_CTR_LEN bytes)
 */
struct cfg80211_gtk_rekey_data {
	const u8 *kek, *kck, *replay_ctr;
};

/**
 * struct cfg80211_update_ft_ies_params - FT IE Information
 *
 * This structure provides information needed to update the fast transition IE
 *
 * @md: The Mobility Domain ID, 2 Octet value
 * @ie: Fast Transition IEs
 * @ie_len: Length of ft_ie in octets
 */
struct cfg80211_update_ft_ies_params {
	u16 md;
	const u8 *ie;
	size_t ie_len;
};

/**
 * struct cfg80211_mgmt_tx_params - mgmt tx parameters
 *
 * This structure provides information needed to transmit a mgmt frame
 *
 * @chan: channel to use
 * @offchan: indicates wether off channel operation is required
 * @wait: duration for ROC
 * @buf: buffer to transmit
 * @len: buffer length
 * @no_cck: don't use cck rates for this frame
 * @dont_wait_for_ack: tells the low level not to wait for an ack
 * @n_csa_offsets: length of csa_offsets array
 * @csa_offsets: array of all the csa offsets in the frame
 */
struct cfg80211_mgmt_tx_params {
	struct ieee80211_channel *chan;
	bool offchan;
	unsigned int wait;
	const u8 *buf;
	size_t len;
	bool no_cck;
	bool dont_wait_for_ack;
	int n_csa_offsets;
	const u16 *csa_offsets;
};

/**
 * struct cfg80211_dscp_exception - DSCP exception
 *
 * @dscp: DSCP value that does not adhere to the user priority range definition
 * @up: user priority value to which the corresponding DSCP value belongs
 */
struct cfg80211_dscp_exception {
	u8 dscp;
	u8 up;
};

/**
 * struct cfg80211_dscp_range - DSCP range definition for user priority
 *
 * @low: lowest DSCP value of this user priority range, inclusive
 * @high: highest DSCP value of this user priority range, inclusive
 */
struct cfg80211_dscp_range {
	u8 low;
	u8 high;
};

/* QoS Map Set element length defined in IEEE Std 802.11-2012, 8.4.2.97 */
#define IEEE80211_QOS_MAP_MAX_EX	21
#define IEEE80211_QOS_MAP_LEN_MIN	16
#define IEEE80211_QOS_MAP_LEN_MAX \
	(IEEE80211_QOS_MAP_LEN_MIN + 2 * IEEE80211_QOS_MAP_MAX_EX)

/**
 * struct cfg80211_qos_map - QoS Map Information
 *
 * This struct defines the Interworking QoS map setting for DSCP values
 *
 * @num_des: number of DSCP exceptions (0..21)
 * @dscp_exception: optionally up to maximum of 21 DSCP exceptions from
 *	the user priority DSCP range definition
 * @up: DSCP range definition for a particular user priority
 */
struct cfg80211_qos_map {
	u8 num_des;
	struct cfg80211_dscp_exception dscp_exception[IEEE80211_QOS_MAP_MAX_EX];
	struct cfg80211_dscp_range up[8];
};

/**
 * struct cfg80211_ops - backend description for wireless configuration
 *
 * This struct is registered by fullmac card drivers and/or wireless stacks
 * in order to handle configuration requests on their interfaces.
 *
 * All callbacks except where otherwise noted should return 0
 * on success or a negative error code.
 *
 * All operations are currently invoked under rtnl for consistency with the
 * wireless extensions but this is subject to reevaluation as soon as this
 * code is used more widely and we have a first user without wext.
 *
 * @suspend: wiphy device needs to be suspended. The variable @wow will
 *	be %NULL or contain the enabled Wake-on-Wireless triggers that are
 *	configured for the device.
 * @resume: wiphy device needs to be resumed
 * @set_wakeup: Called when WoWLAN is enabled/disabled, use this callback
 *	to call device_set_wakeup_enable() to enable/disable wakeup from
 *	the device.
 *
 * @add_virtual_intf: create a new virtual interface with the given name,
 *	must set the struct wireless_dev's iftype. Beware: You must create
 *	the new netdev in the wiphy's network namespace! Returns the struct
 *	wireless_dev, or an ERR_PTR. For P2P device wdevs, the driver must
 *	also set the address member in the wdev.
 *
 * @del_virtual_intf: remove the virtual interface
 *
 * @change_virtual_intf: change type/configuration of virtual interface,
 *	keep the struct wireless_dev's iftype updated.
 *
 * @add_key: add a key with the given parameters. @mac_addr will be %NULL
 *	when adding a group key.
 *
 * @get_key: get information about the key with the given parameters.
 *	@mac_addr will be %NULL when requesting information for a group
 *	key. All pointers given to the @callback function need not be valid
 *	after it returns. This function should return an error if it is
 *	not possible to retrieve the key, -ENOENT if it doesn't exist.
 *
 * @del_key: remove a key given the @mac_addr (%NULL for a group key)
 *	and @key_index, return -ENOENT if the key doesn't exist.
 *
 * @set_default_key: set the default key on an interface
 *
 * @set_default_mgmt_key: set the default management frame key on an interface
 *
 * @set_rekey_data: give the data necessary for GTK rekeying to the driver
 *
 * @start_ap: Start acting in AP mode defined by the parameters.
 * @change_beacon: Change the beacon parameters for an access point mode
 *	interface. This should reject the call when AP mode wasn't started.
 * @stop_ap: Stop being an AP, including stopping beaconing.
 *
 * @add_station: Add a new station.
 * @del_station: Remove a station
 * @change_station: Modify a given station. Note that flags changes are not much
 *	validated in cfg80211, in particular the auth/assoc/authorized flags
 *	might come to the driver in invalid combinations -- make sure to check
 *	them, also against the existing state! Drivers must call
 *	cfg80211_check_station_change() to validate the information.
 * @get_station: get station information for the station identified by @mac
 * @dump_station: dump station callback -- resume dump at index @idx
 *
 * @add_mpath: add a fixed mesh path
 * @del_mpath: delete a given mesh path
 * @change_mpath: change a given mesh path
 * @get_mpath: get a mesh path for the given parameters
 * @dump_mpath: dump mesh path callback -- resume dump at index @idx
 * @join_mesh: join the mesh network with the specified parameters
 *	(invoked with the wireless_dev mutex held)
 * @leave_mesh: leave the current mesh network
 *	(invoked with the wireless_dev mutex held)
 *
 * @get_mesh_config: Get the current mesh configuration
 *
 * @update_mesh_config: Update mesh parameters on a running mesh.
 *	The mask is a bitfield which tells us which parameters to
 *	set, and which to leave alone.
 *
 * @change_bss: Modify parameters for a given BSS.
 *
 * @set_txq_params: Set TX queue parameters
 *
 * @libertas_set_mesh_channel: Only for backward compatibility for libertas,
 *	as it doesn't implement join_mesh and needs to set the channel to
 *	join the mesh instead.
 *
 * @set_monitor_channel: Set the monitor mode channel for the device. If other
 *	interfaces are active this callback should reject the configuration.
 *	If no interfaces are active or the device is down, the channel should
 *	be stored for when a monitor interface becomes active.
 *
 * @scan: Request to do a scan. If returning zero, the scan request is given
 *	the driver, and will be valid until passed to cfg80211_scan_done().
 *	For scan results, call cfg80211_inform_bss(); you can call this outside
 *	the scan/scan_done bracket too.
 * @abort_scan: Tell the driver to abort an ongoing scan. The driver shall
 *	indicate the status of the scan through cfg80211_scan_done().
 *
 * @auth: Request to authenticate with the specified peer
 *	(invoked with the wireless_dev mutex held)
 * @assoc: Request to (re)associate with the specified peer
 *	(invoked with the wireless_dev mutex held)
 * @deauth: Request to deauthenticate from the specified peer
 *	(invoked with the wireless_dev mutex held)
 * @disassoc: Request to disassociate from the specified peer
 *	(invoked with the wireless_dev mutex held)
 *
 * @connect: Connect to the ESS with the specified parameters. When connected,
 *	call cfg80211_connect_result()/cfg80211_connect_bss() with status code
 *	%WLAN_STATUS_SUCCESS. If the connection fails for some reason, call
 *	cfg80211_connect_result()/cfg80211_connect_bss() with the status code
 *	from the AP or cfg80211_connect_timeout() if no frame with status code
 *	was received.
 *	The driver is allowed to roam to other BSSes within the ESS when the
 *	other BSS matches the connect parameters. When such roaming is initiated
 *	by the driver, the driver is expected to verify that the target matches
 *	the configured security parameters and to use Reassociation Request
 *	frame instead of Association Request frame.
 *	The connect function can also be used to request the driver to perform a
 *	specific roam when connected to an ESS. In that case, the prev_bssid
 *	parameter is set to the BSSID of the currently associated BSS as an
 *	indication of requesting reassociation.
 *	In both the driver-initiated and new connect() call initiated roaming
 *	cases, the result of roaming is indicated with a call to
 *	cfg80211_roamed() or cfg80211_roamed_bss().
 *	(invoked with the wireless_dev mutex held)
 * @update_connect_params: Update the connect parameters while connected to a
 *	BSS. The updated parameters can be used by driver/firmware for
 *	subsequent BSS selection (roaming) decisions and to form the
 *	Authentication/(Re)Association Request frames. This call does not
 *	request an immediate disassociation or reassociation with the current
 *	BSS, i.e., this impacts only subsequent (re)associations. The bits in
 *	changed are defined in &enum cfg80211_connect_params_changed.
 *	(invoked with the wireless_dev mutex held)
 * @disconnect: Disconnect from the BSS/ESS.
 *	(invoked with the wireless_dev mutex held)
 *
 * @join_ibss: Join the specified IBSS (or create if necessary). Once done, call
 *	cfg80211_ibss_joined(), also call that function when changing BSSID due
 *	to a merge.
 *	(invoked with the wireless_dev mutex held)
 * @leave_ibss: Leave the IBSS.
 *	(invoked with the wireless_dev mutex held)
 *
 * @set_mcast_rate: Set the specified multicast rate (only if vif is in ADHOC or
 *	MESH mode)
 *
 * @set_wiphy_params: Notify that wiphy parameters have changed;
 *	@changed bitfield (see &enum wiphy_params_flags) describes which values
 *	have changed. The actual parameter values are available in
 *	struct wiphy. If returning an error, no value should be changed.
 *
 * @set_tx_power: set the transmit power according to the parameters,
 *	the power passed is in mBm, to get dBm use MBM_TO_DBM(). The
 *	wdev may be %NULL if power was set for the wiphy, and will
 *	always be %NULL unless the driver supports per-vif TX power
 *	(as advertised by the nl80211 feature flag.)
 * @get_tx_power: store the current TX power into the dbm variable;
 *	return 0 if successful
 *
 * @set_wds_peer: set the WDS peer for a WDS interface
 *
 * @rfkill_poll: polls the hw rfkill line, use cfg80211 reporting
 *	functions to adjust rfkill hw state
 *
 * @dump_survey: get site survey information.
 *
 * @remain_on_channel: Request the driver to remain awake on the specified
 *	channel for the specified duration to complete an off-channel
 *	operation (e.g., public action frame exchange). When the driver is
 *	ready on the requested channel, it must indicate this with an event
 *	notification by calling cfg80211_ready_on_channel().
 * @cancel_remain_on_channel: Cancel an on-going remain-on-channel operation.
 *	This allows the operation to be terminated prior to timeout based on
 *	the duration value.
 * @mgmt_tx: Transmit a management frame.
 * @mgmt_tx_cancel_wait: Cancel the wait time from transmitting a management
 *	frame on another channel
 *
 * @testmode_cmd: run a test mode command; @wdev may be %NULL
 * @testmode_dump: Implement a test mode dump. The cb->args[2] and up may be
 *	used by the function, but 0 and 1 must not be touched. Additionally,
 *	return error codes other than -ENOBUFS and -ENOENT will terminate the
 *	dump and return to userspace with an error, so be careful. If any data
 *	was passed in from userspace then the data/len arguments will be present
 *	and point to the data contained in %NL80211_ATTR_TESTDATA.
 *
 * @set_bitrate_mask: set the bitrate mask configuration
 *
 * @set_pmksa: Cache a PMKID for a BSSID. This is mostly useful for fullmac
 *	devices running firmwares capable of generating the (re) association
 *	RSN IE. It allows for faster roaming between WPA2 BSSIDs.
 * @del_pmksa: Delete a cached PMKID.
 * @flush_pmksa: Flush all cached PMKIDs.
 * @set_power_mgmt: Configure WLAN power management. A timeout value of -1
 *	allows the driver to adjust the dynamic ps timeout value.
 * @set_cqm_rssi_config: Configure connection quality monitor RSSI threshold.
 * @set_cqm_txe_config: Configure connection quality monitor TX error
 *	thresholds.
 * @sched_scan_start: Tell the driver to start a scheduled scan.
 * @sched_scan_stop: Tell the driver to stop an ongoing scheduled scan. This
 *	call must stop the scheduled scan and be ready for starting a new one
 *	before it returns, i.e. @sched_scan_start may be called immediately
 *	after that again and should not fail in that case. The driver should
 *	not call cfg80211_sched_scan_stopped() for a requested stop (when this
 *	method returns 0.)
 *
 * @mgmt_frame_register: Notify driver that a management frame type was
 *	registered. Note that this callback may not sleep, and cannot run
 *	concurrently with itself.
 *
 * @set_antenna: Set antenna configuration (tx_ant, rx_ant) on the device.
 *	Parameters are bitmaps of allowed antennas to use for TX/RX. Drivers may
 *	reject TX/RX mask combinations they cannot support by returning -EINVAL
 *	(also see nl80211.h @NL80211_ATTR_WIPHY_ANTENNA_TX).
 *
 * @get_antenna: Get current antenna configuration from device (tx_ant, rx_ant).
 *
 * @tdls_mgmt: Transmit a TDLS management frame.
 * @tdls_oper: Perform a high-level TDLS operation (e.g. TDLS link setup).
 *
 * @probe_client: probe an associated client, must return a cookie that it
 *	later passes to cfg80211_probe_status().
 *
 * @set_noack_map: Set the NoAck Map for the TIDs.
 *
 * @get_channel: Get the current operating channel for the virtual interface.
 *	For monitor interfaces, it should return %NULL unless there's a single
 *	current monitoring channel.
 *
 * @start_p2p_device: Start the given P2P device.
 * @stop_p2p_device: Stop the given P2P device.
 *
 * @set_mac_acl: Sets MAC address control list in AP and P2P GO mode.
 *	Parameters include ACL policy, an array of MAC address of stations
 *	and the number of MAC addresses. If there is already a list in driver
 *	this new list replaces the existing one. Driver has to clear its ACL
 *	when number of MAC addresses entries is passed as 0. Drivers which
 *	advertise the support for MAC based ACL have to implement this callback.
 *
 * @start_radar_detection: Start radar detection in the driver.
 *
 * @update_ft_ies: Provide updated Fast BSS Transition information to the
 *	driver. If the SME is in the driver/firmware, this information can be
 *	used in building Authentication and Reassociation Request frames.
 *
 * @crit_proto_start: Indicates a critical protocol needs more link reliability
 *	for a given duration (milliseconds). The protocol is provided so the
 *	driver can take the most appropriate actions.
 * @crit_proto_stop: Indicates critical protocol no longer needs increased link
 *	reliability. This operation can not fail.
 * @set_coalesce: Set coalesce parameters.
 *
 * @channel_switch: initiate channel-switch procedure (with CSA). Driver is
 *	responsible for veryfing if the switch is possible. Since this is
 *	inherently tricky driver may decide to disconnect an interface later
 *	with cfg80211_stop_iface(). This doesn't mean driver can accept
 *	everything. It should do it's best to verify requests and reject them
 *	as soon as possible.
 *
 * @set_qos_map: Set QoS mapping information to the driver
 *
 * @set_ap_chanwidth: Set the AP (including P2P GO) mode channel width for the
 *	given interface This is used e.g. for dynamic HT 20/40 MHz channel width
 *	changes during the lifetime of the BSS.
 *
 * @add_tx_ts: validate (if admitted_time is 0) or add a TX TS to the device
 *	with the given parameters; action frame exchange has been handled by
 *	userspace so this just has to modify the TX path to take the TS into
 *	account.
 *	If the admitted time is 0 just validate the parameters to make sure
 *	the session can be created at all; it is valid to just always return
 *	success for that but that may result in inefficient behaviour (handshake
 *	with the peer followed by immediate teardown when the addition is later
 *	rejected)
 * @del_tx_ts: remove an existing TX TS
 */
struct cfg80211_ops {
	int	(*suspend)(struct wiphy *wiphy, struct cfg80211_wowlan *wow);
	int	(*resume)(struct wiphy *wiphy);
	void	(*set_wakeup)(struct wiphy *wiphy, bool enabled);

	struct wireless_dev * (*add_virtual_intf)(struct wiphy *wiphy,
						  const char *name,
						  enum nl80211_iftype type,
						  u32 *flags,
						  struct vif_params *params);
	int	(*del_virtual_intf)(struct wiphy *wiphy,
				    struct wireless_dev *wdev);
	int	(*change_virtual_intf)(struct wiphy *wiphy,
				       struct net_device *dev,
				       enum nl80211_iftype type, u32 *flags,
				       struct vif_params *params);

	int	(*add_key)(struct wiphy *wiphy, struct net_device *netdev,
			   u8 key_index, bool pairwise, const u8 *mac_addr,
			   struct key_params *params);
	int	(*get_key)(struct wiphy *wiphy, struct net_device *netdev,
			   u8 key_index, bool pairwise, const u8 *mac_addr,
			   void *cookie,
			   void (*callback)(void *cookie, struct key_params*));
	int	(*del_key)(struct wiphy *wiphy, struct net_device *netdev,
			   u8 key_index, bool pairwise, const u8 *mac_addr);
	int	(*set_default_key)(struct wiphy *wiphy,
				   struct net_device *netdev,
				   u8 key_index, bool unicast, bool multicast);
	int	(*set_default_mgmt_key)(struct wiphy *wiphy,
					struct net_device *netdev,
					u8 key_index);

	int	(*start_ap)(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_ap_settings *settings);
	int	(*change_beacon)(struct wiphy *wiphy, struct net_device *dev,
				 struct cfg80211_beacon_data *info);
	int	(*stop_ap)(struct wiphy *wiphy, struct net_device *dev);


	int	(*add_station)(struct wiphy *wiphy, struct net_device *dev,
			       const u8 *mac,
			       struct station_parameters *params);
	int	(*del_station)(struct wiphy *wiphy, struct net_device *dev,
			       struct station_del_parameters *params);
	int	(*change_station)(struct wiphy *wiphy, struct net_device *dev,
				  const u8 *mac,
				  struct station_parameters *params);
	int	(*get_station)(struct wiphy *wiphy, struct net_device *dev,
			       const u8 *mac, struct station_info *sinfo);
	int	(*dump_station)(struct wiphy *wiphy, struct net_device *dev,
				int idx, u8 *mac, struct station_info *sinfo);

	int	(*add_mpath)(struct wiphy *wiphy, struct net_device *dev,
			       const u8 *dst, const u8 *next_hop);
	int	(*del_mpath)(struct wiphy *wiphy, struct net_device *dev,
			       const u8 *dst);
	int	(*change_mpath)(struct wiphy *wiphy, struct net_device *dev,
				  const u8 *dst, const u8 *next_hop);
	int	(*get_mpath)(struct wiphy *wiphy, struct net_device *dev,
			     u8 *dst, u8 *next_hop, struct mpath_info *pinfo);
	int	(*dump_mpath)(struct wiphy *wiphy, struct net_device *dev,
			      int idx, u8 *dst, u8 *next_hop,
			      struct mpath_info *pinfo);
	int	(*get_mesh_config)(struct wiphy *wiphy,
				struct net_device *dev,
				struct mesh_config *conf);
	int	(*update_mesh_config)(struct wiphy *wiphy,
				      struct net_device *dev, u32 mask,
				      const struct mesh_config *nconf);
	int	(*join_mesh)(struct wiphy *wiphy, struct net_device *dev,
			     const struct mesh_config *conf,
			     const struct mesh_setup *setup);
	int	(*leave_mesh)(struct wiphy *wiphy, struct net_device *dev);

	int	(*change_bss)(struct wiphy *wiphy, struct net_device *dev,
			      struct bss_parameters *params);

	int	(*set_txq_params)(struct wiphy *wiphy, struct net_device *dev,
				  struct ieee80211_txq_params *params);

	int	(*libertas_set_mesh_channel)(struct wiphy *wiphy,
					     struct net_device *dev,
					     struct ieee80211_channel *chan);

	int	(*set_monitor_channel)(struct wiphy *wiphy,
				       struct cfg80211_chan_def *chandef);

	int	(*scan)(struct wiphy *wiphy,
			struct cfg80211_scan_request *request);
	void	(*abort_scan)(struct wiphy *wiphy, struct wireless_dev *wdev);

	int	(*auth)(struct wiphy *wiphy, struct net_device *dev,
			struct cfg80211_auth_request *req);
	int	(*assoc)(struct wiphy *wiphy, struct net_device *dev,
			 struct cfg80211_assoc_request *req);
	int	(*deauth)(struct wiphy *wiphy, struct net_device *dev,
			  struct cfg80211_deauth_request *req);
	int	(*disassoc)(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_disassoc_request *req);

	int	(*connect)(struct wiphy *wiphy, struct net_device *dev,
			   struct cfg80211_connect_params *sme);
	int	(*update_connect_params)(struct wiphy *wiphy,
					 struct net_device *dev,
					 struct cfg80211_connect_params *sme,
					 u32 changed);
	int	(*disconnect)(struct wiphy *wiphy, struct net_device *dev,
			      u16 reason_code);

	int	(*join_ibss)(struct wiphy *wiphy, struct net_device *dev,
			     struct cfg80211_ibss_params *params);
	int	(*leave_ibss)(struct wiphy *wiphy, struct net_device *dev);

	int	(*set_mcast_rate)(struct wiphy *wiphy, struct net_device *dev,
				  int rate[IEEE80211_NUM_BANDS]);

	int	(*set_wiphy_params)(struct wiphy *wiphy, u32 changed);

	int	(*set_tx_power)(struct wiphy *wiphy, struct wireless_dev *wdev,
				enum nl80211_tx_power_setting type, int mbm);
	int	(*get_tx_power)(struct wiphy *wiphy, struct wireless_dev *wdev,
				int *dbm);

	int	(*set_wds_peer)(struct wiphy *wiphy, struct net_device *dev,
				const u8 *addr);

	void	(*rfkill_poll)(struct wiphy *wiphy);

#ifdef CONFIG_NL80211_TESTMODE
	int	(*testmode_cmd)(struct wiphy *wiphy, struct wireless_dev *wdev,
				void *data, int len);
	int	(*testmode_dump)(struct wiphy *wiphy, struct sk_buff *skb,
				 struct netlink_callback *cb,
				 void *data, int len);
#endif

	int	(*set_bitrate_mask)(struct wiphy *wiphy,
				    struct net_device *dev,
				    const u8 *peer,
				    const struct cfg80211_bitrate_mask *mask);

	int	(*dump_survey)(struct wiphy *wiphy, struct net_device *netdev,
			int idx, struct survey_info *info);

	int	(*set_pmksa)(struct wiphy *wiphy, struct net_device *netdev,
			     struct cfg80211_pmksa *pmksa);
	int	(*del_pmksa)(struct wiphy *wiphy, struct net_device *netdev,
			     struct cfg80211_pmksa *pmksa);
	int	(*flush_pmksa)(struct wiphy *wiphy, struct net_device *netdev);

	int	(*remain_on_channel)(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     struct ieee80211_channel *chan,
				     unsigned int duration,
				     u64 *cookie);
	int	(*cancel_remain_on_channel)(struct wiphy *wiphy,
					    struct wireless_dev *wdev,
					    u64 cookie);

	int	(*mgmt_tx)(struct wiphy *wiphy, struct wireless_dev *wdev,
			   struct cfg80211_mgmt_tx_params *params,
			   u64 *cookie);
	int	(*mgmt_tx_cancel_wait)(struct wiphy *wiphy,
				       struct wireless_dev *wdev,
				       u64 cookie);

	int	(*set_power_mgmt)(struct wiphy *wiphy, struct net_device *dev,
				  bool enabled, int timeout);

	int	(*set_cqm_rssi_config)(struct wiphy *wiphy,
				       struct net_device *dev,
				       s32 rssi_thold, u32 rssi_hyst);

	int	(*set_cqm_txe_config)(struct wiphy *wiphy,
				      struct net_device *dev,
				      u32 rate, u32 pkts, u32 intvl);

	void	(*mgmt_frame_register)(struct wiphy *wiphy,
				       struct wireless_dev *wdev,
				       u16 frame_type, bool reg);

	int	(*set_antenna)(struct wiphy *wiphy, u32 tx_ant, u32 rx_ant);
	int	(*get_antenna)(struct wiphy *wiphy, u32 *tx_ant, u32 *rx_ant);

	int	(*sched_scan_start)(struct wiphy *wiphy,
				struct net_device *dev,
				struct cfg80211_sched_scan_request *request);
	int	(*sched_scan_stop)(struct wiphy *wiphy, struct net_device *dev);

	int	(*set_rekey_data)(struct wiphy *wiphy, struct net_device *dev,
				  struct cfg80211_gtk_rekey_data *data);

	int	(*tdls_mgmt)(struct wiphy *wiphy, struct net_device *dev,
			     const u8 *peer, u8 action_code,  u8 dialog_token,
			     u16 status_code, u32 peer_capability,
			     bool initiator, const u8 *buf, size_t len);
	int	(*tdls_oper)(struct wiphy *wiphy, struct net_device *dev,
			     const u8 *peer, enum nl80211_tdls_operation oper);

	int	(*probe_client)(struct wiphy *wiphy, struct net_device *dev,
				const u8 *peer, u64 *cookie);

	int	(*set_noack_map)(struct wiphy *wiphy,
				  struct net_device *dev,
				  u16 noack_map);

	int	(*get_channel)(struct wiphy *wiphy,
			       struct wireless_dev *wdev,
			       struct cfg80211_chan_def *chandef);

	int	(*start_p2p_device)(struct wiphy *wiphy,
				    struct wireless_dev *wdev);
	void	(*stop_p2p_device)(struct wiphy *wiphy,
				   struct wireless_dev *wdev);

	int	(*set_mac_acl)(struct wiphy *wiphy, struct net_device *dev,
			       const struct cfg80211_acl_data *params);

	int	(*start_radar_detection)(struct wiphy *wiphy,
					 struct net_device *dev,
					 struct cfg80211_chan_def *chandef,
					 u32 cac_time_ms);
	int	(*update_ft_ies)(struct wiphy *wiphy, struct net_device *dev,
				 struct cfg80211_update_ft_ies_params *ftie);
	int	(*crit_proto_start)(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    enum nl80211_crit_proto_id protocol,
				    u16 duration);
	void	(*crit_proto_stop)(struct wiphy *wiphy,
				   struct wireless_dev *wdev);
	int	(*set_coalesce)(struct wiphy *wiphy,
				struct cfg80211_coalesce *coalesce);

	int	(*channel_switch)(struct wiphy *wiphy,
				  struct net_device *dev,
				  struct cfg80211_csa_settings *params);

	int     (*set_qos_map)(struct wiphy *wiphy,
			       struct net_device *dev,
			       struct cfg80211_qos_map *qos_map);

	int	(*set_ap_chanwidth)(struct wiphy *wiphy, struct net_device *dev,
				    struct cfg80211_chan_def *chandef);

	int	(*add_tx_ts)(struct wiphy *wiphy, struct net_device *dev,
			     u8 tsid, const u8 *peer, u8 user_prio,
			     u16 admitted_time);
	int	(*del_tx_ts)(struct wiphy *wiphy, struct net_device *dev,
			     u8 tsid, const u8 *peer);
};

/*
 * wireless hardware and networking interfaces structures
 * and registration/helper functions
 */

/**
 * enum wiphy_flags - wiphy capability flags
 *
 * @WIPHY_FLAG_NETNS_OK: if not set, do not allow changing the netns of this
 *	wiphy at all
 * @WIPHY_FLAG_PS_ON_BY_DEFAULT: if set to true, powersave will be enabled
 *	by default -- this flag will be set depending on the kernel's default
 *	on wiphy_new(), but can be changed by the driver if it has a good
 *	reason to override the default
 * @WIPHY_FLAG_4ADDR_AP: supports 4addr mode even on AP (with a single station
 *	on a VLAN interface)
 * @WIPHY_FLAG_4ADDR_STATION: supports 4addr mode even as a station
 * @WIPHY_FLAG_CONTROL_PORT_PROTOCOL: This device supports setting the
 *	control port protocol ethertype. The device also honours the
 *	control_port_no_encrypt flag.
 * @WIPHY_FLAG_IBSS_RSN: The device supports IBSS RSN.
 * @WIPHY_FLAG_MESH_AUTH: The device supports mesh authentication by routing
 *	auth frames to userspace. See @NL80211_MESH_SETUP_USERSPACE_AUTH.
 * @WIPHY_FLAG_SUPPORTS_SCHED_SCAN: The device supports scheduled scans.
 * @WIPHY_FLAG_SUPPORTS_FW_ROAM: The device supports roaming feature in the
 *	firmware.
 * @WIPHY_FLAG_AP_UAPSD: The device supports uapsd on AP.
 * @WIPHY_FLAG_SUPPORTS_TDLS: The device supports TDLS (802.11z) operation.
 * @WIPHY_FLAG_TDLS_EXTERNAL_SETUP: The device does not handle TDLS (802.11z)
 *	link setup/discovery operations internally. Setup, discovery and
 *	teardown packets should be sent through the @NL80211_CMD_TDLS_MGMT
 *	command. When this flag is not set, @NL80211_CMD_TDLS_OPER should be
 *	used for asking the driver/firmware to perform a TDLS operation.
 * @WIPHY_FLAG_HAVE_AP_SME: device integrates AP SME
 * @WIPHY_FLAG_REPORTS_OBSS: the device will report beacons from other BSSes
 *	when there are virtual interfaces in AP mode by calling
 *	cfg80211_report_obss_beacon().
 * @WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD: When operating as an AP, the device
 *	responds to probe-requests in hardware.
 * @WIPHY_FLAG_OFFCHAN_TX: Device supports direct off-channel TX.
 * @WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL: Device supports remain-on-channel call.
 * @WIPHY_FLAG_SUPPORTS_5_10_MHZ: Device supports 5 MHz and 10 MHz channels.
 * @WIPHY_FLAG_HAS_CHANNEL_SWITCH: Device supports channel switch in
 *	beaconing mode (AP, IBSS, Mesh, ...).
 * @WIPHY_FLAG_SUPPORTS_WMM_ADMISSION: the device supports setting up WMM
 *	TSPEC sessions (TID aka TSID 0-7) with the NL80211_CMD_ADD_TX_TS
 *	command. Standard IEEE 802.11 TSPEC setup is not yet supported, it
 *	needs to be able to handle Block-Ack agreements and other things.
 * @WIPHY_FLAG_DFS_OFFLOAD: The driver handles all the DFS related operations.
 */
enum wiphy_flags {
	WIPHY_FLAG_SUPPORTS_WMM_ADMISSION	= BIT(0),
	/* use hole at 1 */
	/* use hole at 2 */
	WIPHY_FLAG_NETNS_OK			= BIT(3),
	WIPHY_FLAG_PS_ON_BY_DEFAULT		= BIT(4),
	WIPHY_FLAG_4ADDR_AP			= BIT(5),
	WIPHY_FLAG_4ADDR_STATION		= BIT(6),
	WIPHY_FLAG_CONTROL_PORT_PROTOCOL	= BIT(7),
	WIPHY_FLAG_IBSS_RSN			= BIT(8),
	WIPHY_FLAG_MESH_AUTH			= BIT(10),
	WIPHY_FLAG_SUPPORTS_SCHED_SCAN		= BIT(11),
	/* use hole at 12 */
	WIPHY_FLAG_SUPPORTS_FW_ROAM		= BIT(13),
	WIPHY_FLAG_AP_UAPSD			= BIT(14),
	WIPHY_FLAG_SUPPORTS_TDLS		= BIT(15),
	WIPHY_FLAG_TDLS_EXTERNAL_SETUP		= BIT(16),
	WIPHY_FLAG_HAVE_AP_SME			= BIT(17),
	WIPHY_FLAG_REPORTS_OBSS			= BIT(18),
	WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD	= BIT(19),
	WIPHY_FLAG_OFFCHAN_TX			= BIT(20),
	WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL	= BIT(21),
	WIPHY_FLAG_SUPPORTS_5_10_MHZ		= BIT(22),
	WIPHY_FLAG_HAS_CHANNEL_SWITCH		= BIT(23),
	WIPHY_FLAG_DFS_OFFLOAD                  = BIT(24)
};

/**
 * struct ieee80211_iface_limit - limit on certain interface types
 * @max: maximum number of interfaces of these types
 * @types: interface types (bits)
 */
struct ieee80211_iface_limit {
	u16 max;
	u16 types;
};

/**
 * struct ieee80211_iface_combination - possible interface combination
 * @limits: limits for the given interface types
 * @n_limits: number of limitations
 * @num_different_channels: can use up to this many different channels
 * @max_interfaces: maximum number of interfaces in total allowed in this
 *	group
 * @beacon_int_infra_match: In this combination, the beacon intervals
 *	between infrastructure and AP types must match. This is required
 *	only in special cases.
 * @radar_detect_widths: bitmap of channel widths supported for radar detection
 * @radar_detect_regions: bitmap of regions supported for radar detection
 * @beacon_int_min_gcd: This interface combination supports different
 *	beacon intervals.
 *	= 0 - all beacon intervals for different interface must be same.
 *	> 0 - any beacon interval for the interface part of this combination AND
 *	      *GCD* of all beacon intervals from beaconing interfaces of this
 *	      combination must be greater or equal to this value.
 *
 * With this structure the driver can describe which interface
 * combinations it supports concurrently.
 *
 * Examples:
 *
 * 1. Allow #STA <= 1, #AP <= 1, matching BI, channels = 1, 2 total:
 *
 *  struct ieee80211_iface_limit limits1[] = {
 *	{ .max = 1, .types = BIT(NL80211_IFTYPE_STATION), },
 *	{ .max = 1, .types = BIT(NL80211_IFTYPE_AP}, },
 *  };
 *  struct ieee80211_iface_combination combination1 = {
 *	.limits = limits1,
 *	.n_limits = ARRAY_SIZE(limits1),
 *	.max_interfaces = 2,
 *	.beacon_int_infra_match = true,
 *  };
 *
 *
 * 2. Allow #{AP, P2P-GO} <= 8, channels = 1, 8 total:
 *
 *  struct ieee80211_iface_limit limits2[] = {
 *	{ .max = 8, .types = BIT(NL80211_IFTYPE_AP) |
 *			     BIT(NL80211_IFTYPE_P2P_GO), },
 *  };
 *  struct ieee80211_iface_combination combination2 = {
 *	.limits = limits2,
 *	.n_limits = ARRAY_SIZE(limits2),
 *	.max_interfaces = 8,
 *	.num_different_channels = 1,
 *  };
 *
 *
 * 3. Allow #STA <= 1, #{P2P-client,P2P-GO} <= 3 on two channels, 4 total.
 *
 * This allows for an infrastructure connection and three P2P connections.
 *
 *  struct ieee80211_iface_limit limits3[] = {
 *	{ .max = 1, .types = BIT(NL80211_IFTYPE_STATION), },
 *	{ .max = 3, .types = BIT(NL80211_IFTYPE_P2P_GO) |
 *			     BIT(NL80211_IFTYPE_P2P_CLIENT), },
 *  };
 *  struct ieee80211_iface_combination combination3 = {
 *	.limits = limits3,
 *	.n_limits = ARRAY_SIZE(limits3),
 *	.max_interfaces = 4,
 *	.num_different_channels = 2,
 *  };
 */
struct ieee80211_iface_combination {
	const struct ieee80211_iface_limit *limits;
	u32 num_different_channels;
	u16 max_interfaces;
	u8 n_limits;
	bool beacon_int_infra_match;
	u8 radar_detect_widths;
	u8 radar_detect_regions;
	u32 beacon_int_min_gcd;
};

struct ieee80211_txrx_stypes {
	u16 tx, rx;
};

/**
 * enum wiphy_wowlan_support_flags - WoWLAN support flags
 * @WIPHY_WOWLAN_ANY: supports wakeup for the special "any"
 *	trigger that keeps the device operating as-is and
 *	wakes up the host on any activity, for example a
 *	received packet that passed filtering; note that the
 *	packet should be preserved in that case
 * @WIPHY_WOWLAN_MAGIC_PKT: supports wakeup on magic packet
 *	(see nl80211.h)
 * @WIPHY_WOWLAN_DISCONNECT: supports wakeup on disconnect
 * @WIPHY_WOWLAN_SUPPORTS_GTK_REKEY: supports GTK rekeying while asleep
 * @WIPHY_WOWLAN_GTK_REKEY_FAILURE: supports wakeup on GTK rekey failure
 * @WIPHY_WOWLAN_EAP_IDENTITY_REQ: supports wakeup on EAP identity request
 * @WIPHY_WOWLAN_4WAY_HANDSHAKE: supports wakeup on 4-way handshake failure
 * @WIPHY_WOWLAN_RFKILL_RELEASE: supports wakeup on RF-kill release
 */
enum wiphy_wowlan_support_flags {
	WIPHY_WOWLAN_ANY		= BIT(0),
	WIPHY_WOWLAN_MAGIC_PKT		= BIT(1),
	WIPHY_WOWLAN_DISCONNECT		= BIT(2),
	WIPHY_WOWLAN_SUPPORTS_GTK_REKEY	= BIT(3),
	WIPHY_WOWLAN_GTK_REKEY_FAILURE	= BIT(4),
	WIPHY_WOWLAN_EAP_IDENTITY_REQ	= BIT(5),
	WIPHY_WOWLAN_4WAY_HANDSHAKE	= BIT(6),
	WIPHY_WOWLAN_RFKILL_RELEASE	= BIT(7),
};

struct wiphy_wowlan_tcp_support {
	const struct nl80211_wowlan_tcp_data_token_feature *tok;
	u32 data_payload_max;
	u32 data_interval_max;
	u32 wake_payload_max;
	bool seq;
};

/**
 * struct wiphy_wowlan_support - WoWLAN support data
 * @flags: see &enum wiphy_wowlan_support_flags
 * @n_patterns: number of supported wakeup patterns
 *	(see nl80211.h for the pattern definition)
 * @pattern_max_len: maximum length of each pattern
 * @pattern_min_len: minimum length of each pattern
 * @max_pkt_offset: maximum Rx packet offset
 * @tcp: TCP wakeup support information
 */
struct wiphy_wowlan_support {
	u32 flags;
	int n_patterns;
	int pattern_max_len;
	int pattern_min_len;
	int max_pkt_offset;
	const struct wiphy_wowlan_tcp_support *tcp;
};

/**
 * struct wiphy_coalesce_support - coalesce support data
 * @n_rules: maximum number of coalesce rules
 * @max_delay: maximum supported coalescing delay in msecs
 * @n_patterns: number of supported patterns in a rule
 *	(see nl80211.h for the pattern definition)
 * @pattern_max_len: maximum length of each pattern
 * @pattern_min_len: minimum length of each pattern
 * @max_pkt_offset: maximum Rx packet offset
 */
struct wiphy_coalesce_support {
	int n_rules;
	int max_delay;
	int n_patterns;
	int pattern_max_len;
	int pattern_min_len;
	int max_pkt_offset;
};

/**
 * enum wiphy_vendor_command_flags - validation flags for vendor commands
 * @WIPHY_VENDOR_CMD_NEED_WDEV: vendor command requires wdev
 * @WIPHY_VENDOR_CMD_NEED_NETDEV: vendor command requires netdev
 * @WIPHY_VENDOR_CMD_NEED_RUNNING: interface/wdev must be up & running
 *	(must be combined with %_WDEV or %_NETDEV)
 */
enum wiphy_vendor_command_flags {
	WIPHY_VENDOR_CMD_NEED_WDEV = BIT(0),
	WIPHY_VENDOR_CMD_NEED_NETDEV = BIT(1),
	WIPHY_VENDOR_CMD_NEED_RUNNING = BIT(2),
};

/**
 * struct wiphy_vendor_command - vendor command definition
 * @info: vendor command identifying information, as used in nl80211
 * @flags: flags, see &enum wiphy_vendor_command_flags
 * @doit: callback for the operation, note that wdev is %NULL if the
 *	flags didn't ask for a wdev and non-%NULL otherwise; the data
 *	pointer may be %NULL if userspace provided no data at all
 */
struct wiphy_vendor_command {
	struct nl80211_vendor_cmd_info info;
	u32 flags;
	int (*doit)(struct wiphy *wiphy, struct wireless_dev *wdev,
		    const void *data, int data_len);
};

/**
 * struct wiphy_iftype_ext_capab - extended capabilities per interface type
 * @iftype: interface type
 * @extended_capabilities: extended capabilities supported by the driver,
 *	additional capabilities might be supported by userspace; these are the
 *	802.11 extended capabilities ("Extended Capabilities element") and are
 *	in the same format as in the information element. See IEEE Std
 *	802.11-2012 8.4.2.29 for the defined fields.
 * @extended_capabilities_mask: mask of the valid values
 * @extended_capabilities_len: length of the extended capabilities
 */
struct wiphy_iftype_ext_capab {
	enum nl80211_iftype iftype;
	const u8 *extended_capabilities;
	const u8 *extended_capabilities_mask;
	u8 extended_capabilities_len;
};

/**
 * struct wiphy - wireless hardware description
 * @reg_notifier: the driver's regulatory notification callback,
 *	note that if your driver uses wiphy_apply_custom_regulatory()
 *	the reg_notifier's request can be passed as NULL
 * @regd: the driver's regulatory domain, if one was requested via
 * 	the regulatory_hint() API. This can be used by the driver
 *	on the reg_notifier() if it chooses to ignore future
 *	regulatory domain changes caused by other drivers.
 * @signal_type: signal type reported in &struct cfg80211_bss.
 * @cipher_suites: supported cipher suites
 * @n_cipher_suites: number of supported cipher suites
 * @retry_short: Retry limit for short frames (dot11ShortRetryLimit)
 * @retry_long: Retry limit for long frames (dot11LongRetryLimit)
 * @frag_threshold: Fragmentation threshold (dot11FragmentationThreshold);
 *	-1 = fragmentation disabled, only odd values >= 256 used
 * @rts_threshold: RTS threshold (dot11RTSThreshold); -1 = RTS/CTS disabled
 * @_net: the network namespace this wiphy currently lives in
 * @perm_addr: permanent MAC address of this device
 * @addr_mask: If the device supports multiple MAC addresses by masking,
 *	set this to a mask with variable bits set to 1, e.g. if the last
 *	four bits are variable then set it to 00-00-00-00-00-0f. The actual
 *	variable bits shall be determined by the interfaces added, with
 *	interfaces not matching the mask being rejected to be brought up.
 * @n_addresses: number of addresses in @addresses.
 * @addresses: If the device has more than one address, set this pointer
 *	to a list of addresses (6 bytes each). The first one will be used
 *	by default for perm_addr. In this case, the mask should be set to
 *	all-zeroes. In this case it is assumed that the device can handle
 *	the same number of arbitrary MAC addresses.
 * @registered: protects ->resume and ->suspend sysfs callbacks against
 *	unregister hardware
 * @debugfsdir: debugfs directory used for this wiphy, will be renamed
 *	automatically on wiphy renames
 * @dev: (virtual) struct device for this wiphy
 * @registered: helps synchronize suspend/resume with wiphy unregister
 * @wext: wireless extension handlers
 * @priv: driver private data (sized according to wiphy_new() parameter)
 * @interface_modes: bitmask of interfaces types valid for this wiphy,
 *	must be set by driver
 * @iface_combinations: Valid interface combinations array, should not
 *	list single interface types.
 * @n_iface_combinations: number of entries in @iface_combinations array.
 * @software_iftypes: bitmask of software interface types, these are not
 *	subject to any restrictions since they are purely managed in SW.
 * @flags: wiphy flags, see &enum wiphy_flags
 * @regulatory_flags: wiphy regulatory flags, see
 *	&enum ieee80211_regulatory_flags
 * @features: features advertised to nl80211, see &enum nl80211_feature_flags.
 * @ext_features: extended features advertised to nl80211, see
 *	&enum nl80211_ext_feature_index.
 * @bss_priv_size: each BSS struct has private data allocated with it,
 *	this variable determines its size
 * @max_scan_ssids: maximum number of SSIDs the device can scan for in
 *	any given scan
 * @max_sched_scan_ssids: maximum number of SSIDs the device can scan
 *	for in any given scheduled scan
 * @max_match_sets: maximum number of match sets the device can handle
 *	when performing a scheduled scan, 0 if filtering is not
 *	supported.
 * @max_scan_ie_len: maximum length of user-controlled IEs device can
 *	add to probe request frames transmitted during a scan, must not
 *	include fixed IEs like supported rates
 * @max_sched_scan_ie_len: same as max_scan_ie_len, but for scheduled
 *	scans
 * @max_sched_scan_plans: maximum number of scan plans (scan interval and number
 *	of iterations) for scheduled scan supported by the device.
 * @max_sched_scan_plan_interval: maximum interval (in seconds) for a
 *	single scan plan supported by the device.
 * @max_sched_scan_plan_iterations: maximum number of iterations for a single
 *	scan plan supported by the device.
 * @coverage_class: current coverage class
 * @fw_version: firmware version for ethtool reporting
 * @hw_version: hardware version for ethtool reporting
 * @max_num_pmkids: maximum number of PMKIDs supported by device
 * @privid: a pointer that drivers can use to identify if an arbitrary
 *	wiphy is theirs, e.g. in global notifiers
 * @bands: information about bands/channels supported by this device
 *
 * @mgmt_stypes: bitmasks of frame subtypes that can be subscribed to or
 *	transmitted through nl80211, points to an array indexed by interface
 *	type
 *
 * @available_antennas_tx: bitmap of antennas which are available to be
 *	configured as TX antennas. Antenna configuration commands will be
 *	rejected unless this or @available_antennas_rx is set.
 *
 * @available_antennas_rx: bitmap of antennas which are available to be
 *	configured as RX antennas. Antenna configuration commands will be
 *	rejected unless this or @available_antennas_tx is set.
 *
 * @probe_resp_offload:
 *	 Bitmap of supported protocols for probe response offloading.
 *	 See &enum nl80211_probe_resp_offload_support_attr. Only valid
 *	 when the wiphy flag @WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD is set.
 *
 * @max_remain_on_channel_duration: Maximum time a remain-on-channel operation
 *	may request, if implemented.
 *
 * @wowlan: WoWLAN support information
 * @wowlan_config: current WoWLAN configuration; this should usually not be
 *	used since access to it is necessarily racy, use the parameter passed
 *	to the suspend() operation instead.
 *
 * @ap_sme_capa: AP SME capabilities, flags from &enum nl80211_ap_sme_features.
 * @ht_capa_mod_mask:  Specify what ht_cap values can be over-ridden.
 *	If null, then none can be over-ridden.
 * @vht_capa_mod_mask:  Specify what VHT capabilities can be over-ridden.
 *	If null, then none can be over-ridden.
 *
 * @max_acl_mac_addrs: Maximum number of MAC addresses that the device
 *	supports for ACL.
 *
 * @extended_capabilities: extended capabilities supported by the driver,
 *	additional capabilities might be supported by userspace; these are
 *	the 802.11 extended capabilities ("Extended Capabilities element")
 *	and are in the same format as in the information element. See
 *	802.11-2012 8.4.2.29 for the defined fields. These are the default
 *	extended capabilities to be used if the capabilities are not specified
 *	for a specific interface type in iftype_ext_capab.
 * @extended_capabilities_mask: mask of the valid values
 * @extended_capabilities_len: length of the extended capabilities
 * @iftype_ext_capab: array of extended capabilities per interface type
 * @num_iftype_ext_capab: number of interface types for which extended
 *	capabilities are specified separately.
 * @coalesce: packet coalescing support information
 *
 * @vendor_commands: array of vendor commands supported by the hardware
 * @n_vendor_commands: number of vendor commands
 * @vendor_events: array of vendor events supported by the hardware
 * @n_vendor_events: number of vendor events
 *
 * @max_ap_assoc_sta: maximum number of associated stations supported in AP mode
 *	(including P2P GO) or 0 to indicate no such limit is advertised. The
 *	driver is allowed to advertise a theoretical limit that it can reach in
 *	some cases, but may not always reach.
 *
 * @max_num_csa_counters: Number of supported csa_counters in beacons
 *	and probe responses.  This value should be set if the driver
 *	wishes to limit the number of csa counters. Default (0) means
 *	infinite.
 * @max_adj_channel_rssi_comp: max offset of between the channel on which the
 *	frame was sent and the channel on which the frame was heard for which
 *	the reported rssi is still valid. If a driver is able to compensate the
 *	low rssi when a frame is heard on different channel, then it should set
 *	this variable to the maximal offset for which it can compensate.
 *	This value should be set in MHz.
 * @bss_select_support: bitmask indicating the BSS selection criteria supported
 *	by the driver in the .connect() callback. The bit position maps to the
 *	attribute indices defined in &enum nl80211_bss_select_attr.
 */
struct wiphy {
	/* assign these fields before you register the wiphy */

	/* permanent MAC address(es) */
	u8 perm_addr[ETH_ALEN];
	u8 addr_mask[ETH_ALEN];

	struct mac_address *addresses;

	const struct ieee80211_txrx_stypes *mgmt_stypes;

	const struct ieee80211_iface_combination *iface_combinations;
	int n_iface_combinations;
	u16 software_iftypes;

	u16 n_addresses;

	/* Supported interface modes, OR together BIT(NL80211_IFTYPE_...) */
	u16 interface_modes;

	u16 max_acl_mac_addrs;

	u32 flags, regulatory_flags, features;
	u8 ext_features[DIV_ROUND_UP(NUM_NL80211_EXT_FEATURES, 8)];

	u32 ap_sme_capa;

	enum cfg80211_signal_type signal_type;

	int bss_priv_size;
	u8 max_scan_ssids;
	u8 max_sched_scan_ssids;
	u8 max_match_sets;
	u16 max_scan_ie_len;
	u16 max_sched_scan_ie_len;
	u32 max_sched_scan_plans;
	u32 max_sched_scan_plan_interval;
	u32 max_sched_scan_plan_iterations;


	int n_cipher_suites;
	const u32 *cipher_suites;

	u8 retry_short;
	u8 retry_long;
	u32 frag_threshold;
	u32 rts_threshold;
	u8 coverage_class;

	char fw_version[ETHTOOL_FWVERS_LEN];
	u32 hw_version;

#ifdef CONFIG_PM
	const struct wiphy_wowlan_support *wowlan;
	struct cfg80211_wowlan *wowlan_config;
#endif

	u16 max_remain_on_channel_duration;

	u8 max_num_pmkids;

	u32 available_antennas_tx;
	u32 available_antennas_rx;

	/*
	 * Bitmap of supported protocols for probe response offloading
	 * see &enum nl80211_probe_resp_offload_support_attr. Only valid
	 * when the wiphy flag @WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD is set.
	 */
	u32 probe_resp_offload;

	const u8 *extended_capabilities, *extended_capabilities_mask;
	u8 extended_capabilities_len;

	const struct wiphy_iftype_ext_capab *iftype_ext_capab;
	unsigned int num_iftype_ext_capab;

	/* If multiple wiphys are registered and you're handed e.g.
	 * a regular netdev with assigned ieee80211_ptr, you won't
	 * know whether it points to a wiphy your driver has registered
	 * or not. Assign this to something global to your driver to
	 * help determine whether you own this wiphy or not. */
	const void *privid;

	struct ieee80211_supported_band *bands[IEEE80211_NUM_BANDS];

	/* Lets us get back the wiphy on the callback */
	void (*reg_notifier)(struct wiphy *wiphy,
			     struct regulatory_request *request);

	/* fields below are read-only, assigned by cfg80211 */

	const struct ieee80211_regdomain __rcu *regd;

	/* the item in /sys/class/ieee80211/ points to this,
	 * you need use set_wiphy_dev() (see below) */
	struct device dev;

	/* protects ->resume, ->suspend sysfs callbacks against unregister hw */
	bool registered;

	/* dir in debugfs: ieee80211/<wiphyname> */
	struct dentry *debugfsdir;

	const struct ieee80211_ht_cap *ht_capa_mod_mask;
	const struct ieee80211_vht_cap *vht_capa_mod_mask;

#ifdef CONFIG_NET_NS
	/* the network namespace this phy lives in currently */
	struct net *_net;
#endif

#ifdef CONFIG_CFG80211_WEXT
	const struct iw_handler_def *wext;
#endif

	const struct wiphy_coalesce_support *coalesce;

	const struct wiphy_vendor_command *vendor_commands;
	const struct nl80211_vendor_cmd_info *vendor_events;
	int n_vendor_commands, n_vendor_events;

	u16 max_ap_assoc_sta;

	u8 max_num_csa_counters;
	u8 max_adj_channel_rssi_comp;

	u32 bss_select_support;

	char priv[0] __aligned(NETDEV_ALIGN);
};

static inline struct net *wiphy_net(struct wiphy *wiphy)
{
	return read_pnet(&wiphy->_net);
}

static inline void wiphy_net_set(struct wiphy *wiphy, struct net *net)
{
	write_pnet(&wiphy->_net, net);
}

/**
 * wiphy_priv - return priv from wiphy
 *
 * @wiphy: the wiphy whose priv pointer to return
 * Return: The priv of @wiphy.
 */
static inline void *wiphy_priv(struct wiphy *wiphy)
{
	BUG_ON(!wiphy);
	return &wiphy->priv;
}

/**
 * priv_to_wiphy - return the wiphy containing the priv
 *
 * @priv: a pointer previously returned by wiphy_priv
 * Return: The wiphy of @priv.
 */
static inline struct wiphy *priv_to_wiphy(void *priv)
{
	BUG_ON(!priv);
	return container_of(priv, struct wiphy, priv);
}

/**
 * set_wiphy_dev - set device pointer for wiphy
 *
 * @wiphy: The wiphy whose device to bind
 * @dev: The device to parent it to
 */
static inline void set_wiphy_dev(struct wiphy *wiphy, struct device *dev)
{
	wiphy->dev.parent = dev;
}

/**
 * wiphy_dev - get wiphy dev pointer
 *
 * @wiphy: The wiphy whose device struct to look up
 * Return: The dev of @wiphy.
 */
static inline struct device *wiphy_dev(struct wiphy *wiphy)
{
	return wiphy->dev.parent;
}

/**
 * wiphy_name - get wiphy name
 *
 * @wiphy: The wiphy whose name to return
 * Return: The name of @wiphy.
 */
static inline const char *wiphy_name(const struct wiphy *wiphy)
{
	return dev_name(&wiphy->dev);
}

/**
 * wiphy_new - create a new wiphy for use with cfg80211
 *
 * @ops: The configuration operations for this device
 * @sizeof_priv: The size of the private area to allocate
 *
 * Create a new wiphy and associate the given operations with it.
 * @sizeof_priv bytes are allocated for private use.
 *
 * Return: A pointer to the new wiphy. This pointer must be
 * assigned to each netdev's ieee80211_ptr for proper operation.
 */
struct wiphy *wiphy_new(const struct cfg80211_ops *ops, int sizeof_priv);

/**
 * wiphy_register - register a wiphy with cfg80211
 *
 * @wiphy: The wiphy to register.
 *
 * Return: A non-negative wiphy index or a negative error code.
 */
int wiphy_register(struct wiphy *wiphy);

/**
 * wiphy_unregister - deregister a wiphy from cfg80211
 *
 * @wiphy: The wiphy to unregister.
 *
 * After this call, no more requests can be made with this priv
 * pointer, but the call may sleep to wait for an outstanding
 * request that is being handled.
 */
void wiphy_unregister(struct wiphy *wiphy);

/**
 * wiphy_free - free wiphy
 *
 * @wiphy: The wiphy to free
 */
void wiphy_free(struct wiphy *wiphy);

/* internal structs */
struct cfg80211_conn;
struct cfg80211_internal_bss;
struct cfg80211_cached_keys;

/**
 * struct wireless_dev - wireless device state
 *
 * For netdevs, this structure must be allocated by the driver
 * that uses the ieee80211_ptr field in struct net_device (this
 * is intentional so it can be allocated along with the netdev.)
 * It need not be registered then as netdev registration will
 * be intercepted by cfg80211 to see the new wireless device.
 *
 * For non-netdev uses, it must also be allocated by the driver
 * in response to the cfg80211 callbacks that require it, as
 * there's no netdev registration in that case it may not be
 * allocated outside of callback operations that return it.
 *
 * @wiphy: pointer to hardware description
 * @iftype: interface type
 * @list: (private) Used to collect the interfaces
 * @netdev: (private) Used to reference back to the netdev, may be %NULL
 * @identifier: (private) Identifier used in nl80211 to identify this
 *	wireless device if it has no netdev
 * @current_bss: (private) Used by the internal configuration code
 * @chandef: (private) Used by the internal configuration code to track
 *	the user-set channel definition.
 * @preset_chandef: (private) Used by the internal configuration code to
 *	track the channel to be used for AP later
 * @bssid: (private) Used by the internal configuration code
 * @ssid: (private) Used by the internal configuration code
 * @ssid_len: (private) Used by the internal configuration code
 * @mesh_id_len: (private) Used by the internal configuration code
 * @mesh_id_up_len: (private) Used by the internal configuration code
 * @wext: (private) Used by the internal wireless extensions compat code
 * @use_4addr: indicates 4addr mode is used on this interface, must be
 *	set by driver (if supported) on add_interface BEFORE registering the
 *	netdev and may otherwise be used by driver read-only, will be update
 *	by cfg80211 on change_interface
 * @mgmt_registrations: list of registrations for management frames
 * @mgmt_registrations_lock: lock for the list
 * @mtx: mutex used to lock data in this struct, may be used by drivers
 *	and some API functions require it held
 * @beacon_interval: beacon interval used on this device for transmitting
 *	beacons, 0 when not valid
 * @address: The address for this device, valid only if @netdev is %NULL
 * @p2p_started: true if this is a P2P Device that has been started
 * @cac_started: true if DFS channel availability check has been started
 * @cac_start_time: timestamp (jiffies) when the dfs state was entered.
 * @cac_time_ms: CAC time in ms
 * @ps: powersave mode is enabled
 * @ps_timeout: dynamic powersave timeout
 * @ap_unexpected_nlportid: (private) netlink port ID of application
 *	registered for unexpected class 3 frames (AP mode)
 * @conn: (private) cfg80211 software SME connection state machine data
 * @connect_keys: (private) keys to set after connection is e