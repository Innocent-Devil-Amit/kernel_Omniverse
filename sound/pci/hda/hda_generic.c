/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * Generic widget tree parser
 *
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/sort.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/tlv.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_beep.h"
#include "hda_generic.h"


/* initialize hda_gen_spec struct */
int snd_hda_gen_spec_init(struct hda_gen_spec *spec)
{
	snd_array_init(&spec->kctls, sizeof(struct snd_kcontrol_new), 32);
	snd_array_init(&spec->paths, sizeof(struct nid_path), 8);
	snd_array_init(&spec->loopback_list, sizeof(struct hda_amp_list), 8);
	mutex_init(&spec->pcm_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_gen_spec_init);

struct snd_kcontrol_new *
snd_hda_gen_add_kctl(struct hda_gen_spec *spec, const char *name,
		     const struct snd_kcontrol_new *temp)
{
	struct snd_kcontrol_new *knew = snd_array_new(&spec->kctls);
	if (!knew)
		return NULL;
	*knew = *temp;
	if (name)
		knew->name = kstrdup(name, GFP_KERNEL);
	else if (knew->name)
		knew->name = kstrdup(knew->name, GFP_KERNEL);
	if (!knew->name)
		return NULL;
	return knew;
}
EXPORT_SYMBOL_GPL(snd_hda_gen_add_kctl);

static void free_kctls(struct hda_gen_spec *spec)
{
	if (spec->kctls.list) {
		struct snd_kcontrol_new *kctl = spec->kctls.list;
		int i;
		for (i = 0; i < spec->kctls.used; i++)
			kfree(kctl[i].name);
	}
	snd_array_free(&spec->kctls);
}

static void snd_hda_gen_spec_free(struct hda_gen_spec *spec)
{
	if (!spec)
		return;
	free_kctls(spec);
	snd_array_free(&spec->paths);
	snd_array_free(&spec->loopback_list);
}

/*
 * store user hints
 */
static void parse_user_hints(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	int val;

	val = snd_hda_get_bool_hint(codec, "jack_detect");
	if (val >= 0)
		codec->no_jack_detect = !val;
	val = snd_hda_get_bool_hint(codec, "inv_jack_detect");
	if (val >= 0)
		codec->inv_jack_detect = !!val;
	val = snd_hda_get_bool_hint(codec, "trigger_sense");
	if (val >= 0)
		codec->no_trigger_sense = !val;
	val = snd_hda_get_bool_hint(codec, "inv_eapd");
	if (val >= 0)
		codec->inv_eapd = !!val;
	val = snd_hda_get_bool_hint(codec, "pcm_format_first");
	if (val >= 0)
		codec->pcm_format_first = !!val;
	val = snd_hda_get_bool_hint(codec, "sticky_stream");
	if (val >= 0)
		codec->no_sticky_stream = !val;
	val = snd_hda_get_bool_hint(codec, "spdif_status_reset");
	if (val >= 0)
		codec->spdif_status_reset = !!val;
	val = snd_hda_get_bool_hint(codec, "pin_amp_workaround");
	if (val >= 0)
		codec->pin_amp_workaround = !!val;
	val = snd_hda_get_bool_hint(codec, "single_adc_amp");
	if (val >= 0)
		codec->single_adc_amp = !!val;

	val = snd_hda_get_bool_hint(codec, "auto_mute");
	if (val >= 0)
		spec->suppress_auto_mute = !val;
	val = snd_hda_get_bool_hint(codec, "auto_mic");
	if (val >= 0)
		spec->suppress_auto_mic = !val;
	val = snd_hda_get_bool_hint(codec, "line_in_auto_switch");
	if (val >= 0)
		spec->line_in_auto_switch = !!val;
	val = snd_hda_get_bool_hint(codec, "auto_mute_via_amp");
	if (val >= 0)
		spec->auto_mute_via_amp = !!val;
	val = snd_hda_get_bool_hint(codec, "need_dac_fix");
	if (val >= 0)
		spec->need_dac_fix = !!val;
	val = snd_hda_get_bool_hint(codec, "primary_hp");
	if (val >= 0)
		spec->no_primary_hp = !val;
	val = snd_hda_get_bool_hint(codec, "multi_io");
	if (val >= 0)
		spec->no_multi_io = !val;
	val = snd_hda_get_bool_hint(codec, "multi_cap_vol");
	if (val >= 0)
		spec->multi_cap_vol = !!val;
	val = snd_hda_get_bool_hint(codec, "inv_dmic_split");
	if (val >= 0)
		spec->inv_dmic_split = !!val;
	val = snd_hda_get_bool_hint(codec, "indep_hp");
	if (val >= 0)
		spec->indep_hp = !!val;
	val = snd_hda_get_bool_hint(codec, "add_stereo_mix_input");
	if (val >= 0)
		spec->add_stereo_mix_input = !!val;
	/* the following two are just for compatibility */
	val = snd_hda_get_bool_hint(codec, "add_out_jack_modes");
	if (val >= 0)
		spec->add_jack_modes = !!val;
	val = snd_hda_get_bool_hint(codec, "add_in_jack_modes");
	if (val >= 0)
		spec->add_jack_modes = !!val;
	val = snd_hda_get_bool_hint(codec, "add_jack_modes");
	if (val >= 0)
		spec->add_jack_modes = !!val;
	val = snd_hda_get_bool_hint(codec, "power_down_unused");
	if (val >= 0)
		spec->power_down_unused = !!val;
	val = snd_hda_get_bool_hint(codec, "add_hp_mic");
	if (val >= 0)
		spec->hp_mic = !!val;
	val = snd_hda_get_bool_hint(codec, "hp_mic_detect");
	if (val >= 0)
		spec->suppress_hp_mic_detect = !val;

	if (!snd_hda_get_int_hint(codec, "mixer_nid", &val))
		spec->mixer_nid = val;
}

/*
 * pin control value accesses
 */

#define update_pin_ctl(codec, pin, val) \
	snd_hda_codec_update_cache(codec, pin, 0, \
				   AC_VERB_SET_PIN_WIDGET_CONTROL, val)

/* restore the pinctl based on the cached value */
static inline void restore_pin_ctl(struct hda_codec *codec, hda_nid_t pin)
{
	update_pin_ctl(codec, pin, snd_hda_codec_get_pin_target(codec, pin));
}

/* set the pinctl target value and write it if requested */
static void set_pin_target(struct hda_codec *codec, hda_nid_t pin,
			   unsigned int val, bool do_write)
{
	if (!pin)
		return;
	val = snd_hda_correct_pin_ctl(codec, pin, val);
	snd_hda_codec_set_pin_target(codec, pin, val);
	if (do_write)
		update_pin_ctl(codec, pin, val);
}

/* set pinctl target values for all given pins */
static void set_pin_targets(struct hda_codec *codec, int num_pins,
			    hda_nid_t *pins, unsigned int val)
{
	int i;
	for (i = 0; i < num_pins; i++)
		set_pin_target(codec, pins[i], val, false);
}

/*
 * parsing paths
 */

/* return the position of NID in the list, or -1 if not found */
static int find_idx_in_nid_list(hda_nid_t nid, const hda_nid_t *list, int nums)
{
	int i;
	for (i = 0; i < nums; i++)
		if (list[i] == nid)
			return i;
	return -1;
}

/* return true if the given NID is contained in the path */
static bool is_nid_contained(struct nid_path *path, hda_nid_t nid)
{
	return find_idx_in_nid_list(nid, path->path, path->depth) >= 0;
}

static struct nid_path *get_nid_path(struct hda_codec *codec,
				     hda_nid_t from_nid, hda_nid_t to_nid,
				     int anchor_nid)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->paths.used; i++) {
		struct nid_path *path = snd_array_elem(&spec->paths, i);
		if (path->depth <= 0)
			continue;
		if ((!from_nid || path->path[0] == from_nid) &&
		    (!to_nid || path->path[path->depth - 1] == to_nid)) {
			if (!anchor_nid ||
			    (anchor_nid > 0 && is_nid_contained(path, anchor_nid)) ||
			    (anchor_nid < 0 && !is_nid_contained(path, anchor_nid)))
				return path;
		}
	}
	return NULL;
}

/* get the path between the given NIDs;
 * passing 0 to either @pin or @dac behaves as a wildcard
 */
struct nid_path *snd_hda_get_nid_path(struct hda_codec *codec,
				      hda_nid_t from_nid, hda_nid_t to_nid)
{
	return get_nid_path(codec, from_nid, to_nid, 0);
}
EXPORT_SYMBOL_GPL(snd_hda_get_nid_path);

/* get the index number corresponding to the path instance;
 * the index starts from 1, for easier checking the invalid value
 */
int snd_hda_get_path_idx(struct hda_codec *codec, struct nid_path *path)
{
	struct hda_gen_spec *spec = codec->spec;
	struct nid_path *array = spec->paths.list;
	ssize_t idx;

	if (!spec->paths.used)
		return 0;
	idx = path - array;
	if (idx < 0 || idx >= spec->paths.used)
		return 0;
	return idx + 1;
}
EXPORT_SYMBOL_GPL(snd_hda_get_path_idx);

/* get the path instance corresponding to the given index number */
struct nid_path *snd_hda_get_path_from_idx(struct hda_codec *codec, int idx)
{
	struct hda_gen_spec *spec = codec->spec;

	if (idx <= 0 || idx > spec->paths.used)
		return NULL;
	return snd_array_elem(&spec->paths, idx - 1);
}
EXPORT_SYMBOL_GPL(snd_hda_get_path_from_idx);

/* check whether the given DAC is already found in any existing paths */
static bool is_dac_already_used(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->paths.used; i++) {
		struct nid_path *path = snd_array_elem(&spec->paths, i);
		if (path->path[0] == nid)
			return true;
	}
	return false;
}

/* check whether the given two widgets can be connected */
static bool is_reachable_path(struct hda_codec *codec,
			      hda_nid_t from_nid, hda_nid_t to_nid)
{
	if (!from_nid || !to_nid)
		return false;
	return snd_hda_get_conn_index(codec, to_nid, from_nid, true) >= 0;
}

/* nid, dir and idx */
#define AMP_VAL_COMPARE_MASK	(0xffff | (1U << 18) | (0x0f << 19))

/* check whether the given ctl is already assigned in any path elements */
static bool is_ctl_used(struct hda_codec *codec, unsigned int val, int type)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	val &= AMP_VAL_COMPARE_MASK;
	for (i = 0; i < spec->paths.used; i++) {
		struct nid_path *path = snd_array_elem(&spec->paths, i);
		if ((path->ctls[type] & AMP_VAL_COMPARE_MASK) == val)
			return true;
	}
	return false;
}

/* check whether a control with the given (nid, dir, idx) was assigned */
static bool is_ctl_associated(struct hda_codec *codec, hda_nid_t nid,
			      int dir, int idx, int type)
{
	unsigned int val = HDA_COMPOSE_AMP_VAL(nid, 3, idx, dir);
	return is_ctl_used(codec, val, type);
}

static void print_nid_path(struct hda_codec *codec,
			   const char *pfx, struct nid_path *path)
{
	char buf[40];
	char *pos = buf;
	int i;

	*pos = 0;
	for (i = 0; i < path->depth; i++)
		pos += scnprintf(pos, sizeof(buf) - (pos - buf), "%s%02x",
				 pos != buf ? ":" : "",
				 path->path[i]);

	codec_dbg(codec, "%s path: depth=%d '%s'\n", pfx, path->depth, buf);
}

/* called recursively */
static bool __parse_nid_path(struct hda_codec *codec,
			     hda_nid_t from_nid, hda_nid_t to_nid,
			     int anchor_nid, struct nid_path *path,
			     int depth)
{
	const hda_nid_t *conn;
	int i, nums;

	if (to_nid == anchor_nid)
		anchor_nid = 0; /* anchor passed */
	else if (to_nid == (hda_nid_t)(-anchor_nid))
		return false; /* hit the exclusive nid */

	nums = snd_hda_get_conn_list(codec, to_nid, &conn);
	for (i = 0; i < nums; i++) {
		if (conn[i] != from_nid) {
			/* special case: when from_nid is 0,
			 * try to find an empty DAC
			 */
			if (from_nid ||
			    get_wcaps_type(get_wcaps(codec, conn[i])) != AC_WID_AUD_OUT ||
			    is_dac_already_used(codec, conn[i]))
				continue;
		}
		/* anchor is not requested or already passed? */
		if (anchor_nid <= 0)
			goto found;
	}
	if (depth >= MAX_NID_PATH_DEPTH)
		return false;
	for (i = 0; i < nums; i++) {
		unsigned int type;
		type = get_wcaps_type(get_wcaps(codec, conn[i]));
		if (type == AC_WID_AUD_OUT || type == AC_WID_AUD_IN ||
		    type == AC_WID_PIN)
			continue;
		if (__parse_nid_path(codec, from_nid, conn[i],
				     anchor_nid, path, depth + 1))
			goto found;
	}
	return false;

 found:
	path->path[path->depth] = conn[i];
	path->idx[path->depth + 1] = i;
	if (nums > 1 && get_wcaps_type(get_wcaps(codec, to_nid)) != AC_WID_AUD_MIX)
		path->multi[path->depth + 1] = 1;
	path->depth++;
	return true;
}

/* parse the widget path from the given nid to the target nid;
 * when @from_nid is 0, try to find an empty DAC;
 * when @anchor_nid is set to a positive value, only paths through the widget
 * with the given value are evaluated.
 * when @anchor_nid is set to a negative value, paths through the widget
 * with the negative of given value are excluded, only other paths are chosen.
 * when @anchor_nid is zero, no special handling about path selection.
 */
bool snd_hda_parse_nid_path(struct hda_codec *codec, hda_nid_t from_nid,
			    hda_nid_t to_nid, int anchor_nid,
			    struct nid_path *path)
{
	if (__parse_nid_path(codec, from_nid, to_nid, anchor_nid, path, 1)) {
		path->path[path->depth] = to_nid;
		path->depth++;
		return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(snd_hda_parse_nid_path);

/*
 * parse the path between the given NIDs and add to the path list.
 * if no valid path is found, return NULL
 */
struct nid_path *
snd_hda_add_new_path(struct hda_codec *codec, hda_nid_t from_nid,
		     hda_nid_t to_nid, int anchor_nid)
{
	struct hda_gen_spec *spec = codec->spec;
	struct nid_path *path;

	if (from_nid && to_nid && !is_reachable_path(codec, from_nid, to_nid))
		return NULL;

	/* check whether the path has been already added */
	path = get_nid_path(codec, from_nid, to_nid, anchor_nid);
	if (path)
		return path;

	path = snd_array_new(&spec->paths);
	if (!path)
		return NULL;
	memset(path, 0, sizeof(*path));
	if (snd_hda_parse_nid_path(codec, from_nid, to_nid, anchor_nid, path))
		return path;
	/* push back */
	spec->paths.used--;
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_hda_add_new_path);

/* clear the given path as invalid so that it won't be picked up later */
static void invalidate_nid_path(struct hda_codec *codec, int idx)
{
	struct nid_path *path = snd_hda_get_path_from_idx(codec, idx);
	if (!path)
		return;
	memset(path, 0, sizeof(*path));
}

/* return a DAC if paired to the given pin by codec driver */
static hda_nid_t get_preferred_dac(struct hda_codec *codec, hda_nid_t pin)
{
	struct hda_gen_spec *spec = codec->spec;
	const hda_nid_t *list = spec->preferred_dacs;

	if (!list)
		return 0;
	for (; *list; list += 2)
		if (*list == pin)
			return list[1];
	return 0;
}

/* look for an empty DAC slot */
static hda_nid_t look_for_dac(struct hda_codec *codec, hda_nid_t pin,
			      bool is_digital)
{
	struct hda_gen_spec *spec = codec->spec;
	bool cap_digital;
	int i;

	for (i = 0; i < spec->num_all_dacs; i++) {
		hda_nid_t nid = spec->all_dacs[i];
		if (!nid || is_dac_already_used(codec, nid))
			continue;
		cap_digital = !!(get_wcaps(codec, nid) & AC_WCAP_DIGITAL);
		if (is_digital != cap_digital)
			continue;
		if (is_reachable_path(codec, nid, pin))
			return nid;
	}
	return 0;
}

/* replace the channels in the composed amp value with the given number */
static unsigned int amp_val_replace_channels(unsigned int val, unsigned int chs)
{
	val &= ~(0x3U << 16);
	val |= chs << 16;
	return val;
}

static bool same_amp_caps(struct hda_codec *codec, hda_nid_t nid1,
			  hda_nid_t nid2, int dir)
{
	if (!(get_wcaps(codec, nid1) & (1 << (dir + 1))))
		return !(get_wcaps(codec, nid2) & (1 << (dir + 1)));
	return (query_amp_caps(codec, nid1, dir) ==
		query_amp_caps(codec, nid2, dir));
}

/* look for a widget suitable for assigning a mute switch in the path */
static hda_nid_t look_for_out_mute_nid(struct hda_codec *codec,
				       struct nid_path *path)
{
	int i;

	for (i = path->depth - 1; i >= 0; i--) {
		if (nid_has_mute(codec, path->path[i], HDA_OUTPUT))
			return path->path[i];
		if (i != path->depth - 1 && i != 0 &&
		    nid_has_mute(codec, path->path[i], HDA_INPUT))
			return path->path[i];
	}
	return 0;
}

/* look for a widget suitable for assigning a volume ctl in the path */
static hda_nid_t look_for_out_vol_nid(struct hda_codec *codec,
				      struct nid_path *path)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	for (i = path->depth - 1; i >= 0; i--) {
		hda_nid_t nid = path->path[i];
		if ((spec->out_vol_mask >> nid) & 1)
			continue;
		if (nid_has_volume(codec, nid, HDA_OUTPUT))
			return nid;
	}
	return 0;
}

/*
 * path activation / deactivation
 */

/* can have the amp-in capability? */
static bool has_amp_in(struct hda_codec *codec, struct nid_path *path, int idx)
{
	hda_nid_t nid = path->path[idx];
	unsigned int caps = get_wcaps(codec, nid);
	unsigned int type = get_wcaps_type(caps);

	if (!(caps & AC_WCAP_IN_AMP))
		return false;
	if (type == AC_WID_PIN && idx > 0) /* only for input pins */
		return false;
	return true;
}

/* can have the amp-out capability? */
static bool has_amp_out(struct hda_codec *codec, struct nid_path *path, int idx)
{
	hda_nid_t nid = path->path[idx];
	unsigned int caps = get_wcaps(codec, nid);
	unsigned int type = get_wcaps_type(caps);

	if (!(caps & AC_WCAP_OUT_AMP))
		return false;
	if (type == AC_WID_PIN && !idx) /* only for output pins */
		return false;
	return true;
}

/* check whether the given (nid,dir,idx) is active */
static bool is_active_nid(struct hda_codec *codec, hda_nid_t nid,
			  unsigned int dir, unsigned int idx)
{
	struct hda_gen_spec *spec = codec->spec;
	int i, n;

	for (n = 0; n < spec->paths.used; n++) {
		struct nid_path *path = snd_array_elem(&spec->paths, n);
		if (!path->active)
			continue;
		for (i = 0; i < path->depth; i++) {
			if (path->path[i] == nid) {
				if (dir == HDA_OUTPUT || path->idx[i] == idx)
					return true;
				break;
			}
		}
	}
	return false;
}

/* check whether the NID is referred by any active paths */
#define is_active_nid_for_any(codec, nid) \
	is_active_nid(codec, nid, HDA_OUTPUT, 0)

/* get the default amp value for the target state */
static int get_amp_val_to_activate(struct hda_codec *codec, hda_nid_t nid,
				   int dir, unsigned int caps, bool enable)
{
	unsigned int val = 0;

	if (caps & AC_AMPCAP_NUM_STEPS) {
		/* set to 0dB */
		if (enable)
			val = (caps & AC_AMPCAP_OFFSET) >> AC_AMPCAP_OFFSET_SHIFT;
	}
	if (caps & (AC_AMPCAP_MUTE | AC_AMPCAP_MIN_MUTE)) {
		if (!enable)
			val |= HDA_AMP_MUTE;
	}
	return val;
}

/* is this a stereo widget or a stereo-to-mono mix? */
static bool is_stereo_amps(struct hda_codec *codec, hda_nid_t nid, int dir)
{
	unsigned int wcaps = get_wcaps(codec, nid);
	hda_nid_t conn;

	if (wcaps & AC_WCAP_STEREO)
		return true;
	if (dir != HDA_INPUT || get_wcaps_type(wcaps) != AC_WID_AUD_MIX)
		return false;
	if (snd_hda_get_num_conns(codec, nid) != 1)
		return false;
	if (snd_hda_get_connections(codec, nid, &conn, 1) < 0)
		return false;
	return !!(get_wcaps(codec, conn) & AC_WCAP_STEREO);
}

/* initialize the amp value (only at the first time) */
static void init_amp(struct hda_codec *codec, hda_nid_t nid, int dir, int idx)
{
	unsigned int caps = query_amp_caps(codec, nid, dir);
	int val = get_amp_val_to_activate(codec, nid, dir, caps, false);

	if (is_stereo_amps(codec, nid, dir))
		snd_hda_codec_amp_init_stereo(codec, nid, dir, idx, 0xff, val);
	else
		snd_hda_codec_amp_init(codec, nid, 0, dir, idx, 0xff, val);
}

/* update the amp, doing in stereo or mono depending on NID */
static int update_amp(struct hda_codec *codec, hda_nid_t nid, int dir, int idx,
		      unsigned int mask, unsigned int val)
{
	if (is_stereo_amps(codec, nid, dir))
		return snd_hda_codec_amp_stereo(codec, nid, dir, idx,
						mask, val);
	else
		return snd_hda_codec_amp_update(codec, nid, 0, dir, idx,
						mask, val);
}

/* calculate amp value mask we can modify;
 * if the given amp is controlled by mixers, don't touch it
 */
static unsigned int get_amp_mask_to_modify(struct hda_codec *codec,
					   hda_nid_t nid, int dir, int idx,
					   unsigned int caps)
{
	unsigned int mask = 0xff;

	if (caps & (AC_AMPCAP_MUTE | AC_AMPCAP_MIN_MUTE)) {
		if (is_ctl_associated(codec, nid, dir, idx, NID_PATH_MUTE_CTL))
			mask &= ~0x80;
	}
	if (caps & AC_AMPCAP_NUM_STEPS) {
		if (is_ctl_associated(codec, nid, dir, idx, NID_PATH_VOL_CTL) ||
		    is_ctl_associated(codec, nid, dir, idx, NID_PATH_BOOST_CTL))
			mask &= ~0x7f;
	}
	return mask;
}

static void activate_amp(struct hda_codec *codec, hda_nid_t nid, int dir,
			 int idx, int idx_to_check, bool enable)
{
	unsigned int caps;
	unsigned int mask, val;

	caps = query_amp_caps(codec, nid, dir);
	val = get_amp_val_to_activate(codec, nid, dir, caps, enable);
	mask = get_amp_mask_to_modify(codec, nid, dir, idx_to_check, caps);
	if (!mask)
		return;

	val &= mask;
	update_amp(codec, nid, dir, idx, mask, val);
}

static void check_and_activate_amp(struct hda_codec *codec, hda_nid_t nid,
				   int dir, int idx, int idx_to_check,
				   bool enable)
{
	/* check whether the given amp is still used by others */
	if (!enable && is_active_nid(codec, nid, dir, idx_to_check))
		return;
	activate_amp(codec, nid, dir, idx, idx_to_check, enable);
}

static void activate_amp_out(struct hda_codec *codec, struct nid_path *path,
			     int i, bool enable)
{
	hda_nid_t nid = path->path[i];
	init_amp(codec, nid, HDA_OUTPUT, 0);
	check_and_activate_amp(codec, nid, HDA_OUTPUT, 0, 0, enable);
}

static void activate_amp_in(struct hda_codec *codec, struct nid_path *path,
			    int i, bool enable, bool add_aamix)
{
	struct hda_gen_spec *spec = codec->spec;
	const hda_nid_t *conn;
	int n, nums, idx;
	int type;
	hda_nid_t nid = path->path[i];

	nums = snd_hda_get_conn_list(codec, nid, &conn);
	type = get_wcaps_type(get_wcaps(codec, nid));
	if (type == AC_WID_PIN ||
	    (type == AC_WID_AUD_IN && codec->single_adc_amp)) {
		nums = 1;
		idx = 0;
	} else
		idx = path->idx[i];

	for (n = 0; n < nums; n++)
		init_amp(codec, nid, HDA_INPUT, n);

	/* here is a little bit tricky in comparison with activate_amp_out();
	 * when aa-mixer is available, we need to enable the path as well
	 */
	for (n = 0; n < nums; n++) {
		if (n != idx) {
			if (conn[n] != spec->mixer_merge_nid)
				continue;
			/* when aamix is disabled, force to off */
			if (!add_aamix) {
				activate_amp(codec, nid, HDA_INPUT, n, n, false);
				continue;
			}
		}
		check_and_activate_amp(codec, nid, HDA_INPUT, n, idx, enable);
	}
}

/* activate or deactivate the given path
 * if @add_aamix is set, enable the input from aa-mix NID as well (if any)
 */
void snd_hda_activate_path(struct hda_codec *codec, struct nid_path *path,
			   bool enable, bool add_aamix)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	if (!enable)
		path->active = false;

	for (i = path->depth - 1; i >= 0; i--) {
		hda_nid_t nid = path->path[i];
		if (enable && spec->power_down_unused) {
			/* make sure the widget is powered up */
			if (!snd_hda_check_power_state(codec, nid, AC_PWRST_D0))
				snd_hda_codec_write(codec, nid, 0,
						    AC_VERB_SET_POWER_STATE,
						    AC_PWRST_D0);
		}
		if (enable && path->multi[i])
			snd_hda_codec_update_cache(codec, nid, 0,
					    AC_VERB_SET_CONNECT_SEL,
					    path->idx[i]);
		if (has_amp_in(codec, path, i))
			activate_amp_in(codec, path, i, enable, add_aamix);
		if (has_amp_out(codec, path, i))
			activate_amp_out(codec, path, i, enable);
	}

	if (enable)
		path->active = true;
}
EXPORT_SYMBOL_GPL(snd_hda_activate_path);

/* if the given path is inactive, put widgets into D3 (only if suitable) */
static void path_power_down_sync(struct hda_codec *codec, struct nid_path *path)
{
	struct hda_gen_spec *spec = codec->spec;
	bool changed = false;
	int i;

	if (!spec->power_down_unused || path->active)
		return;

	for (i = 0; i < path->depth; i++) {
		hda_nid_t nid = path->path[i];
		if (!snd_hda_check_power_state(codec, nid, AC_PWRST_D3) &&
		    !is_active_nid_for_any(codec, nid)) {
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_POWER_STATE,
					    AC_PWRST_D3);
			changed = true;
		}
	}

	if (changed) {
		msleep(10);
		snd_hda_codec_read(codec, path->path[0], 0,
				   AC_VERB_GET_POWER_STATE, 0);
	}
}

/* turn on/off EAPD on the given pin */
static void set_pin_eapd(struct hda_codec *codec, hda_nid_t pin, bool enable)
{
	struct hda_gen_spec *spec = codec->spec;
	if (spec->own_eapd_ctl ||
	    !(snd_hda_query_pin_caps(codec, pin) & AC_PINCAP_EAPD))
		return;
	if (spec->keep_eapd_on && !enable)
		return;
	if (codec->inv_eapd)
		enable = !enable;
	snd_hda_codec_update_cache(codec, pin, 0,
				   AC_VERB_SET_EAPD_BTLENABLE,
				   enable ? 0x02 : 0x00);
}

/* re-initialize the path specified by the given path index */
static void resume_path_from_idx(struct hda_codec *codec, int path_idx)
{
	struct nid_path *path = snd_hda_get_path_from_idx(codec, path_idx);
	if (path)
		snd_hda_activate_path(codec, path, path->active, false);
}


/*
 * Helper functions for creating mixer ctl elements
 */

static int hda_gen_mixer_mute_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol);
static int hda_gen_bind_mute_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol);

enum {
	HDA_CTL_WIDGET_VOL,
	HDA_CTL_WIDGET_MUTE,
	HDA_CTL_BIND_MUTE,
};
static const struct snd_kcontrol_new control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	/* only the put callback is replaced for handling the special mute */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.subdevice = HDA_SUBDEV_AMP_FLAG,
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = hda_gen_mixer_mute_put, /* replaced */
		.private_value = HDA_COMPOSE_AMP_VAL(0, 3, 0, 0),
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_bind_switch_get,
		.put = hda_gen_bind_mute_put, /* replaced */
		.private_value = HDA_COMPOSE_AMP_VAL(0, 3, 0, 0),
	},
};

/* add dynamic controls from template */
static struct snd_kcontrol_new *
add_control(struct hda_gen_spec *spec, int type, const char *name,
		       int cidx, unsigned long val)
{
	struct snd_kcontrol_new *knew;

	knew = snd_hda_gen_add_kctl(spec, name, &control_templates[type]);
	if (!knew)
		return NULL;
	knew->index = cidx;
	if (get_amp_nid_(val))
		knew->subdevice = HDA_SUBDEV_AMP_FLAG;
	knew->private_value = val;
	return knew;
}

static int add_control_with_pfx(struct hda_gen_spec *spec, int type,
				const char *pfx, const char *dir,
				const char *sfx, int cidx, unsigned long val)
{
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	snprintf(name, sizeof(name), "%s %s %s", pfx, dir, sfx);
	if (!add_control(spec, type, name, cidx, val))
		return -ENOMEM;
	return 0;
}

#define add_pb_vol_ctrl(spec, type, pfx, val)			\
	add_control_with_pfx(spec, type, pfx, "Playback", "Volume", 0, val)
#define add_pb_sw_ctrl(spec, type, pfx, val)			\
	add_control_with_pfx(spec, type, pfx, "Playback", "Switch", 0, val)
#define __add_pb_vol_ctrl(spec, type, pfx, cidx, val)			\
	add_control_with_pfx(spec, type, pfx, "Playback", "Volume", cidx, val)
#define __add_pb_sw_ctrl(spec, type, pfx, cidx, val)			\
	add_control_with_pfx(spec, type, pfx, "Playback", "Switch", cidx, val)

static int add_vol_ctl(struct hda_codec *codec, const char *pfx, int cidx,
		       unsigned int chs, struct nid_path *path)
{
	unsigned int val;
	if (!path)
		return 0;
	val = path->ctls[NID_PATH_VOL_CTL];
	if (!val)
		return 0;
	val = amp_val_replace_channels(val, chs);
	return __add_pb_vol_ctrl(codec->spec, HDA_CTL_WIDGET_VOL, pfx, cidx, val);
}

/* return the channel bits suitable for the given path->ctls[] */
static int get_default_ch_nums(struct hda_codec *codec, struct nid_path *path,
			       int type)
{
	int chs = 1; /* mono (left only) */
	if (path) {
		hda_nid_t nid = get_amp_nid_(path->ctls[type]);
		if (nid && (get_wcaps(codec, nid) & AC_WCAP_STEREO))
			chs = 3; /* stereo */
	}
	return chs;
}

static int add_stereo_vol(struct hda_codec *codec, const char *pfx, int cidx,
			  struct nid_path *path)
{
	int chs = get_default_ch_nums(codec, path, NID_PATH_VOL_CTL);
	return add_vol_ctl(codec, pfx, cidx, chs, path);
}

/* create a mute-switch for the given mixer widget;
 * if it has multiple sources (e.g. DAC and loopback), create a bind-mute
 */
static int add_sw_ctl(struct hda_codec *codec, const char *pfx, int cidx,
		      unsigned int chs, struct nid_path *path)
{
	unsigned int val;
	int type = HDA_CTL_WIDGET_MUTE;

	if (!path)
		return 0;
	val = path->ctls[NID_PATH_MUTE_CTL];
	if (!val)
		return 0;
	val = amp_val_replace_channels(val, chs);
	if (get_amp_direction_(val) == HDA_INPUT) {
		hda_nid_t nid = get_amp_nid_(val);
		int nums = snd_hda_get_num_conns(codec, nid);
		if (nums > 1) {
			type = HDA_CTL_BIND_MUTE;
			val |= nums << 19;
		}
	}
	return __add_pb_sw_ctrl(codec->spec, type, pfx, cidx, val);
}

static int add_stereo_sw(struct hda_codec *codec, const char *pfx,
				  int cidx, struct nid_path *path)
{
	int chs = get_default_ch_nums(codec, path, NID_PATH_MUTE_CTL);
	return add_sw_ctl(codec, pfx, cidx, chs, path);
}

/* playback mute control with the software mute bit check */
static void sync_auto_mute_bits(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;

	if (spec->auto_mute_via_amp) {
		hda_nid_t nid = get_amp_nid(kcontrol);
		bool enabled = !((spec->mute_bits >> nid) & 1);
		ucontrol->value.integer.value[0] &= enabled;
		ucontrol->value.integer.value[1] &= enabled;
	}
}

static int hda_gen_mixer_mute_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	sync_auto_mute_bits(kcontrol, ucontrol);
	return snd_hda_mixer_amp_switch_put(kcontrol, ucontrol);
}

static int hda_gen_bind_mute_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	sync_auto_mute_bits(kcontrol, ucontrol);
	return snd_hda_mixer_bind_switch_put(kcontrol, ucontrol);
}

/* any ctl assigned to the path with the given index? */
static bool path_has_mixer(struct hda_codec *codec, int path_idx, int ctl_type)
{
	struct nid_path *path = snd_hda_get_path_from_idx(codec, path_idx);
	return path && path->ctls[ctl_type];
}

static const char * const channel_name[4] = {
	"Front", "Surround", "CLFE", "Side"
};

/* give some appropriate ctl name prefix for the given line out channel */
static const char *get_line_out_pfx(struct hda_codec *codec, int ch,
				    int *index, int ctl_type)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;

	*index = 0;
	if (cfg->line_outs == 1 && !spec->multi_ios &&
	    !cfg->hp_outs && !cfg->speaker_outs)
		return spec->vmaster_mute.hook ? "PCM" : "Master";

	/* if there is really a single DAC used in the whole output paths,
	 * use it master (or "PCM" if a vmaster hook is present)
	 */
	if (spec->multiout.num_dacs == 1 && !spec->mixer_nid &&
	    !spec->multiout.hp_out_nid[0] && !spec->multiout.extra_out_nid[0])
		return spec->vmaster_mute.hook ? "PCM" : "Master";

	/* multi-io channels */
	if (ch >= cfg->line_outs)
		return channel_name[ch];

	switch (cfg->line_out_type) {
	case AUTO_PIN_SPEAKER_OUT:
		/* if the primary channel vol/mute is shared with HP volume,
		 * don't name it as Speaker
		 */
		if (!ch && cfg->hp_outs &&
		    !path_has_mixer(codec, spec->hp_paths[0], ctl_type))
			break;
		if (cfg->line_outs == 1)
			return "Speaker";
		if (cfg->line_outs == 2)
			return ch ? "Bass Speaker" : "Speaker";
		break;
	case AUTO_PIN_HP_OUT:
		/* if the primary channel vol/mute is shared with spk volume,
		 * don't name it as Headphone
		 */
		if (!ch && cfg->speaker_outs &&
		    !path_has_mixer(codec, spec->speaker_paths[0], ctl_type))
			break;
		/* for multi-io case, only the primary out */
		if (ch && spec->multi_ios)
			break;
		*index = ch;
		return "Headphone";
	}

	/* for a single channel output, we don't have to name the channel */
	if (cfg->line_outs == 1 && !spec->multi_ios)
		return "PCM";

	if (ch >= ARRAY_SIZE(channel_name)) {
		snd_BUG();
		return "PCM";
	}

	return channel_name[ch];
}

/*
 * Parse output paths
 */

/* badness definition */
enum {
	/* No primary DAC is found for the main output */
	BAD_NO_PRIMARY_DAC = 0x10000,
	/* No DAC is found for the extra output */
	BAD_NO_DAC = 0x4000,
	/* No possible multi-ios */
	BAD_MULTI_IO = 0x120,
	/* No individual DAC for extra output */
	BAD_NO_EXTRA_DAC = 0x102,
	/* No individual DAC for extra surrounds */
	BAD_NO_EXTRA_SURR_DAC = 0x101,
	/* Primary DAC shared with main surrounds */
	BAD_SHARED_SURROUND = 0x100,
	/* No independent HP possible */
	BAD_NO_INDEP_HP = 0x10,
	/* Primary DAC shared with main CLFE */
	BAD_SHARED_CLFE = 0x10,
	/* Primary DAC shared with extra surrounds */
	BAD_SHARED_EXTRA_SURROUND = 0x10,
	/* Volume widget is shared */
	BAD_SHARED_VOL = 0x10,
};

/* look for widgets in the given path which are appropriate for
 * volume and mute controls, and assign the values to ctls[].
 *
 * When no appropriate widget is found in the path, the badness value
 * is incremented depending on the situation.  The function returns the
 * total badness for both volume and mute controls.
 */
static int assign_out_path_ctls(struct hda_codec *codec, struct nid_path *path)
{
	struct hda_gen_spec *spec = codec->spec;
	hda_nid_t nid;
	unsigned int val;
	int badness = 0;

	if (!path)
		return BAD_SHARED_VOL * 2;

	if (path->ctls[NID_PATH_VOL_CTL] ||
	    path->ctls[NID_PATH_MUTE_CTL])
		return 0; /* already evaluated */

	nid = look_for_out_vol_nid(codec, path);
	if (nid) {
		val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
		if (spec->dac_min_mute)
			val |= HDA_AMP_VAL_MIN_MUTE;
		if (is_ctl_used(codec, val, NID_PATH_VOL_CTL))
			badness += BAD_SHARED_VOL;
		else
			path->ctls[NID_PATH_VOL_CTL] = val;
	} else
		badness += BAD_SHARED_VOL;
	nid = look_for_out_mute_nid(codec, path);
	if (nid) {
		unsigned int wid_type = get_wcaps_type(get_wcaps(codec, nid));
		if (wid_type == AC_WID_PIN || wid_type == AC_WID_AUD_OUT ||
		    nid_has_mute(codec, nid, HDA_OUTPUT))
			val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
		else
			val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_INPUT);
		if (is_ctl_used(codec, val, NID_PATH_MUTE_CTL))
			badness += BAD_SHARED_VOL;
		else
			path->ctls[NID_PATH_MUTE_CTL] = val;
	} else
		badness += BAD_SHARED_VOL;
	return badness;
}

const struct badness_table hda_main_out_badness = {
	.no_primary_dac = BAD_NO_PRIMARY_DAC,
	.no_dac = BAD_NO_DAC,
	.shared_primary = BAD_NO_PRIMARY_DAC,
	.shared_surr = BAD_SHARED_SURROUND,
	.shared_clfe = BAD_SHARED_CLFE,
	.shared_surr_main = BAD_SHARED_SURROUND,
};
EXPORT_SYMBOL_GPL(hda_main_out_badness);

const struct badness_table hda_extra_out_badness = {
	.no_primary_dac = BAD_NO_DAC,
	.no_dac = BAD_NO_DAC,
	.shared_primary = BAD_NO_EXTRA_DAC,
	.shared_surr = BAD_SHARED_EXTRA_SURROUND,
	.shared_clfe = BAD_SHARED_EXTRA_SURROUND,
	.shared_surr_main = BAD_NO_EXTRA_SURR_DAC,
};
EXPORT_SYMBOL_GPL(hda_extra_out_badness);

/* get the DAC of the primary output corresponding to the given array index */
static hda_nid_t get_primary_out(struct hda_codec *codec, int idx)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;

	if (cfg->line_outs > idx)
		return spec->private_dac_nids[idx];
	idx -= cfg->line_outs;
	if (spec->multi_ios > idx)
		return spec->multi_io[idx].dac;
	return 0;
}

/* return the DAC if it's reachable, otherwise zero */
static inline hda_nid_t try_dac(struct hda_codec *codec,
				hda_nid_t dac, hda_nid_t pin)
{
	return is_reachable_path(codec, dac, pin) ? dac : 0;
}

/* try to assign DACs to pins and return the resultant badness */
static int try_assign_dacs(struct hda_codec *codec, int num_outs,
			   const hda_nid_t *pins, hda_nid_t *dacs,
			   int *path_idx,
			   const struct badness_table *bad)
{
	struct hda_gen_spec *spec = codec->spec;
	int i, j;
	int badness = 0;
	hda_nid_t dac;

	if (!num_outs)
		return 0;

	for (i = 0; i < num_outs; i++) {
		struct nid_path *path;
		hda_nid_t pin = pins[i];

		path = snd_hda_get_path_from_idx(codec, path_idx[i]);
		if (path) {
			badness += assign_out_path_ctls(codec, path);
			continue;
		}

		dacs[i] = get_preferred_dac(codec, pin);
		if (dacs[i]) {
			if (is_dac_already_used(codec, dacs[i]))
				badness += bad->shared_primary;
		}

		if (!dacs[i])
			dacs[i] = look_for_dac(codec, pin, false);
		if (!dacs[i] && !i) {
			/* try to steal the DAC of surrounds for the front */
			for (j = 1; j < num_outs; j++) {
				if (is_reachable_path(codec, dacs[j], pin)) {
					dacs[0] = dacs[j];
					dacs[j] = 0;
					invalidate_nid_path(codec, path_idx[j]);
					path_idx[j] = 0;
					break;
				}
			}
		}
		dac = dacs[i];
		if (!dac) {
			if (num_outs > 2)
				dac = try_dac(codec, get_primary_out(codec, i), pin);
			if (!dac)
				dac = try_dac(codec, dacs[0], pin);
			if (!dac)
				dac = try_dac(codec, get_primary_out(codec, i), pin);
			if (dac) {
				if (!i)
					badness += bad->shared_primary;
				else if (i == 1)
					badness += bad->shared_surr;
				else
					badness += bad->shared_clfe;
			} else if (is_reachable_path(codec, spec->private_dac_nids[0], pin)) {
				dac = spec->private_dac_nids[0];
				badness += bad->shared_surr_main;
			} else if (!i)
				badness += bad->no_primary_dac;
			else
				badness += bad->no_dac;
		}
		if (!dac)
			continue;
		path = snd_hda_add_new_path(codec, dac, pin, -spec->mixer_nid);
		if (!path && !i && spec->mixer_nid) {
			/* try with aamix */
			path = snd_hda_add_new_path(codec, dac, pin, 0);
		}
		if (!path) {
			dac = dacs[i] = 0;
			badness += bad->no_dac;
		} else {
			/* print_nid_path(codec, "output", path); */
			path->active = true;
			path_idx[i] = snd_hda_get_path_idx(codec, path);
			badness += assign_out_path_ctls(codec, path);
		}
	}

	return badness;
}

/* return NID if the given pin has only a single connection to a certain DAC */
static hda_nid_t get_dac_if_single(struct hda_codec *codec, hda_nid_t pin)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;
	hda_nid_t nid_found = 0;

	for (i = 0; i < spec->num_all_dacs; i++) {
		hda_nid_t nid = spec->all_dacs[i];
		if (!nid || is_dac_already_used(codec, nid))
			continue;
		if (is_reachable_path(codec, nid, pin)) {
			if (nid_found)
				return 0;
			nid_found = nid;
		}
	}
	return nid_found;
}

/* check whether the given pin can be a multi-io pin */
static bool can_be_multiio_pin(struct hda_codec *codec,
			       unsigned int location, hda_nid_t nid)
{
	unsigned int defcfg, caps;

	defcfg = snd_hda_codec_get_pincfg(codec, nid);
	if (get_defcfg_connect(defcfg) != AC_JACK_PORT_COMPLEX)
		return false;
	if (location && get_defcfg_location(defcfg) != location)
		return false;
	caps = snd_hda_query_pin_caps(codec, nid);
	if (!(caps & AC_PINCAP_OUT))
		return false;
	return true;
}

/* count the number of input pins that are capable to be multi-io */
static int count_multiio_pins(struct hda_codec *codec, hda_nid_t reference_pin)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int defcfg = snd_hda_codec_get_pincfg(codec, reference_pin);
	unsigned int location = get_defcfg_location(defcfg);
	int type, i;
	int num_pins = 0;

	for (type = AUTO_PIN_LINE_IN; type >= AUTO_PIN_MIC; type--) {
		for (i = 0; i < cfg->num_inputs; i++) {
			if (cfg->inputs[i].type != type)
				continue;
			if (can_be_multiio_pin(codec, location,
					       cfg->inputs[i].pin))
				num_pins++;
		}
	}
	return num_pins;
}

/*
 * multi-io helper
 *
 * When hardwired is set, try to fill ony hardwired pins, and returns
 * zero if any pins are filled, non-zero if nothing found.
 * When hardwired is off, try to fill possible input pins, and returns
 * the badness value.
 */
static int fill_multi_ios(struct hda_codec *codec,
			  hda_nid_t reference_pin,
			  bool hardwired)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int type, i, j, num_pins, old_pins;
	unsigned int defcfg = snd_hda_codec_get_pincfg(codec, reference_pin);
	unsigned int location = get_defcfg_location(defcfg);
	int badness = 0;
	struct nid_path *path;

	old_pins = spec->multi_ios;
	if (old_pins >= 2)
		goto end_fill;

	num_pins = count_multiio_pins(codec, reference_pin);
	if (num_pins < 2)
		goto end_fill;

	for (type = AUTO_PIN_LINE_IN; type >= AUTO_PIN_MIC; type--) {
		for (i = 0; i < cfg->num_inputs; i++) {
			hda_nid_t nid = cfg->inputs[i].pin;
			hda_nid_t dac = 0;

			if (cfg->inputs[i].type != type)
				continue;
			if (!can_be_multiio_pin(codec, location, nid))
				continue;
			for (j = 0; j < spec->multi_ios; j++) {
				if (nid == spec->multi_io[j].pin)
					break;
			}
			if (j < spec->multi_ios)
				continue;

			if (hardwired)
				dac = get_dac_if_single(codec, nid);
			else if (!dac)
				dac = look_for_dac(codec, nid, false);
			if (!dac) {
				badness++;
				continue;
			}
			path = snd_hda_add_new_path(codec, dac, nid,
						    -spec->mixer_nid);
			if (!path) {
				badness++;
				continue;
			}
			/* print_nid_path(codec, "multiio", path); */
			spec->multi_io[spec->multi_ios].pin = nid;
			spec->multi_io[spec->multi_ios].dac = dac;
			spec->out_paths[cfg->line_outs + spec->multi_ios] =
				snd_hda_get_path_idx(codec, path);
			spec->multi_ios++;
			if (spec->multi_ios >= 2)
				break;
		}
	}
 end_fill:
	if (badness)
		badness = BAD_MULTI_IO;
	if (old_pins == spec->multi_ios) {
		if (hardwired)
			return 1; /* nothing found */
		else
			return badness; /* no badness if nothing found */
	}
	if (!hardwired && spec->multi_ios < 2) {
		/* cancel newly assigned paths */
		spec->paths.used -= spec->multi_ios - old_pins;
		spec->multi_ios = old_pins;
		return badness;
	}

	/* assign volume and mute controls */
	for (i = old_pins; i < spec->multi_ios; i++) {
		path = snd_hda_get_path_from_idx(codec, spec->out_paths[cfg->line_outs + i]);
		badness += assign_out_path_ctls(codec, path);
	}

	return badness;
}

/* map DACs for all pins in the list if they are single connections */
static bool map_singles(struct hda_codec *codec, int outs,
			const hda_nid_t *pins, hda_nid_t *dacs, int *path_idx)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;
	bool found = false;
	for (i = 0; i < outs; i++) {
		struct nid_path *path;
		hda_nid_t dac;
		if (dacs[i])
			continue;
		dac = get_dac_if_single(codec, pins[i]);
		if (!dac)
			continue;
		path = snd_hda_add_new_path(codec, dac, pins[i],
					    -spec->mixer_nid);
		if (!path && !i && spec->mixer_nid)
			path = snd_hda_add_new_path(codec, dac, pins[i], 0);
		if (path) {
			dacs[i] = dac;
			found = true;
			/* print_nid_path(codec, "output", path); */
			path->active = true;
			path_idx[i] = snd_hda_get_path_idx(codec, path);
		}
	}
	return found;
}

static inline bool has_aamix_out_paths(struct hda_gen_spec *spec)
{
	return spec->aamix_out_paths[0] || spec->aamix_out_paths[1] ||
		spec->aamix_out_paths[2];
}

/* create a new path including aamix if available, and return its index */
static int check_aamix_out_path(struct hda_codec *codec, int path_idx)
{
	struct hda_gen_spec *spec = codec->spec;
	struct nid_path *path;
	hda_nid_t path_dac, dac, pin;

	path = snd_hda_get_path_from_idx(codec, path_idx);
	if (!path || !path->depth ||
	    is_nid_contained(path, spec->mixer_nid))
		return 0;
	path_dac = path->path[0];
	dac = spec->private_dac_nids[0];
	pin = path->path[path->depth - 1];
	path = snd_hda_add_new_path(codec, dac, pin, spec->mixer_nid);
	if (!path) {
		if (dac != path_dac)
			dac = path_dac;
		else if (spec->multiout.hp_out_nid[0])
			dac = spec->multiout.hp_out_nid[0];
		else if (spec->multiout.extra_out_nid[0])
			dac = spec->multiout.extra_out_nid[0];
		else
			dac = 0;
		if (dac)
			path = snd_hda_add_new_path(codec, dac, pin,
						    spec->mixer_nid);
	}
	if (!path)
		return 0;
	/* print_nid_path(codec, "output-aamix", path); */
	path->active = false; /* unused as default */
	return snd_hda_get_path_idx(codec, path);
}

/* check whether the independent HP is available with the current config */
static bool indep_hp_possible(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	struct nid_path *path;
	int i, idx;

	if (cfg->line_out_type == AUTO_PIN_HP_OUT)
		idx = spec->out_paths[0];
	else
		idx = spec->hp_paths[0];
	path = snd_hda_get_path_from_idx(codec, idx);
	if (!path)
		return false;

	/* assume no path conflicts unless aamix is involved */
	if (!spec->mixer_nid || !is_nid_contained(path, spec->mixer_nid))
		return true;

	/* check whether output paths contain aamix */
	for (i = 0; i < cfg->line_outs; i++) {
		if (spec->out_paths[i] == idx)
			break;
		path = snd_hda_get_path_from_idx(codec, spec->out_paths[i]);
		if (path && is_nid_contained(path, spec->mixer_nid))
			return false;
	}
	for (i = 0; i < cfg->speaker_outs; i++) {
		path = snd_hda_get_path_from_idx(codec, spec->speaker_paths[i]);
		if (path && is_nid_contained(path, spec->mixer_nid))
			return false;
	}

	return true;
}

/* fill the empty entries in the dac array for speaker/hp with the
 * shared dac pointed by the paths
 */
static void refill_shared_dacs(struct hda_codec *codec, int num_outs,
			       hda_nid_t *dacs, int *path_idx)
{
	struct nid_path *path;
	int i;

	for (i = 0; i < num_outs; i++) {
		if (dacs[i])
			continue;
		path = snd_hda_get_path_from_idx(codec, path_idx[i]);
		if (!path)
			continue;
		dacs[i] = path->path[0];
	}
}

/* fill in the dac_nids table from the parsed pin configuration */
static int fill_and_eval_dacs(struct hda_codec *codec,
			      bool fill_hardwired,
			      bool fill_mio_first)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i, err, badness;

	/* set num_dacs once to full for look_for_dac() */
	spec->multiout.num_dacs = cfg->line_outs;
	spec->multiout.dac_nids = spec->private_dac_nids;
	memset(spec->private_dac_nids, 0, sizeof(spec->private_dac_nids));
	memset(spec->multiout.hp_out_nid, 0, sizeof(spec->multiout.hp_out_nid));
	memset(spec->multiout.extra_out_nid, 0, sizeof(spec->multiout.extra_out_nid));
	spec->multi_ios = 0;
	snd_array_free(&spec->paths);

	/* clear path indices */
	memset(spec->out_paths, 0, sizeof(spec->out_paths));
	memset(spec->hp_paths, 0, sizeof(spec->hp_paths));
	memset(spec->speaker_paths, 0, sizeof(spec->speaker_paths));
	memset(spec->aamix_out_paths, 0, sizeof(spec->aamix_out_paths));
	memset(spec->digout_paths, 0, sizeof(spec->digout_paths));
	memset(spec->input_paths, 0, sizeof(spec->input_paths));
	memset(spec->loopback_paths, 0, sizeof(spec->loopback_paths));
	memset(&spec->digin_path, 0, sizeof(spec->digin_path));

	badness = 0;

	/* fill hard-wired DACs first */
	if (fill_hardwired) {
		bool mapped;
		do {
			mapped = map_singles(codec, cfg->line_outs,
					     cfg->line_out_pins,
					     spec->private_dac_nids,
					     spec->out_paths);
			mapped |= map_singles(codec, cfg->hp_outs,
					      cfg->hp_pins,
					      spec->multiout.hp_out_nid,
					      spec->hp_paths);
			mapped |= map_singles(codec, cfg->speaker_outs,
					      cfg->speaker_pins,
					      spec->multiout.extra_out_nid,
					      spec->speaker_paths);
			if (!spec->no_multi_io &&
			    fill_mio_first && cfg->line_outs == 1 &&
			    cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
				err = fill_multi_ios(codec, cfg->line_out_pins[0], true);
				if (!err)
					mapped = true;
			}
		} while (mapped);
	}

	badness += try_assign_dacs(codec, cfg->line_outs, cfg->line_out_pins,
				   spec->private_dac_nids, spec->out_paths,
				   spec->main_out_badness);

	if (!spec->no_multi_io && fill_mio_first &&
	    cfg->line_outs == 1 && cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
		/* try to fill multi-io first */
		err = fill_multi_ios(codec, cfg->line_out_pins[0], false);
		if (err < 0)
			return err;
		/* we don't count badness at this stage yet */
	}

	if (cfg->line_out_type != AUTO_PIN_HP_OUT) {
		err = try_assign_dacs(codec, cfg->hp_outs, cfg->hp_pins,
				      spec->multiout.hp_out_nid,
				      spec->hp_paths,
				      spec->extra_out_badness);
		if (err < 0)
			return err;
		badness += err;
	}
	if (cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
		err = try_assign_dacs(codec, cfg->speaker_outs,
				      cfg->speaker_pins,
				      spec->multiout.extra_out_nid,
				      spec->speaker_paths,
				      spec->extra_out_badness);
		if (err < 0)
			return err;
		badness += err;
	}
	if (!spec->no_multi_io &&
	    cfg->line_outs == 1 && cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
		err = fill_multi_ios(codec, cfg->line_out_pins[0], false);
		if (err < 0)
			return err;
		badness += err;
	}

	if (spec->mixer_nid) {
		spec->aamix_out_paths[0] =
			check_aamix_out_path(codec, spec->out_paths[0]);
		if (cfg->line_out_type != AUTO_PIN_HP_OUT)
			spec->aamix_out_paths[1] =
				check_aamix_out_path(codec, spec->hp_paths[0]);
		if (cfg->line_out_type != AUTO_PIN_SPEAKER_OUT)
			spec->aamix_out_paths[2] =
				check_aamix_out_path(codec, spec->speaker_paths[0]);
	}

	if (!spec->no_multi_io &&
	    cfg->hp_outs && cfg->line_out_type == AUTO_PIN_SPEAKER_OUT)
		if (count_multiio_pins(codec, cfg->hp_pins[0]) >= 2)
			spec->multi_ios = 1; /* give badness */

	/* re-count num_dacs and squash invalid entries */
	spec->multiout.num_dacs = 0;
	for (i = 0; i < cfg->line_outs; i++) {
		if (spec->private_dac_nids[i])
			spec->multiout.num_dacs++;
		else {
			memmove(spec->private_dac_nids + i,
				spec->private_dac_nids + i + 1,
				sizeof(hda_nid_t) * (cfg->line_outs - i - 1));
			spec->private_dac_nids[cfg->line_outs - 1] = 0;
		}
	}

	spec->ext_channel_count = spec->min_channel_count =
		spec->multiout.num_dacs * 2;

	if (spec->multi_ios == 2) {
		for (i = 0; i < 2; i++)
			spec->private_dac_nids[spec->multiout.num_dacs++] =
				spec->multi_io[i].dac;
	} else if (spec->multi_ios) {
		spec->multi_ios = 0;
		badness += BAD_MULTI_IO;
	}

	if (spec->indep_hp && !indep_hp_possible(codec))
		badness += BAD_NO_INDEP_HP;

	/* re-fill the shared DAC for speaker / headphone */
	if (cfg->line_out_type != AUTO_PIN_HP_OUT)
		refill_shared_dacs(codec, cfg->hp_outs,
				   spec->multiout.hp_out_nid,
				   spec->hp_paths);
	if (cfg->line_out_type != AUTO_PIN_SPEAKER_OUT)
		refill_shared_dacs(codec, cfg->speaker_outs,
				   spec->multiout.extra_out_nid,
				   spec->speaker_paths);

	return badness;
}

#define DEBUG_BADNESS

#ifdef DEBUG_BADNESS
#define debug_badness(fmt, ...)						\
	codec_dbg(codec, fmt, ##__VA_ARGS__)
#else
#define debug_badness(fmt, ...)						\
	do { if (0) codec_dbg(codec, fmt, ##__VA_ARGS__); } while (0)
#endif

#ifdef DEBUG_BADNESS
static inline void print_nid_path_idx(struct hda_codec *codec,
				      const char *pfx, int idx)
{
	struct nid_path *path;

	path = snd_hda_get_path_from_idx(codec, idx);
	if (path)
		print_nid_path(codec, pfx, path);
}

static void debug_show_configs(struct hda_codec *codec,
			       struct auto_pin_cfg *cfg)
{
	struct hda_gen_spec *spec = codec->spec;
	static const char * const lo_type[3] = { "LO", "SP", "HP" };
	int i;

	debug_badness("multi_outs = %x/%x/%x/%x : %x/%x/%x/%x (type %s)\n",
		      cfg->line_out_pins[0], cfg->line_out_pins[1],
		      cfg->line_out_pins[2], cfg->line_out_pins[3],
		      spec->multiout.dac_nids[0],
		      spec->multiout.dac_nids[1],
		      spec->multiout.dac_nids[2],
		      spec->multiout.dac_nids[3],
		      lo_type[cfg->line_out_type]);
	for (i = 0; i < cfg->line_outs; i++)
		print_nid_path_idx(codec, "  out", spec->out_paths[i]);
	if (spec->multi_ios > 0)
		debug_badness("multi_ios(%d) = %x/%x : %x/%x\n",
			      spec->multi_ios,
			      spec->multi_io[0].pin, spec->multi_io[1].pin,
			      spec->multi_io[0].dac, spec->multi_io[1].dac);
	for (i = 0; i < spec->multi_ios; i++)
		print_nid_path_idx(codec, "  mio",
				   spec->out_paths[cfg->line_outs + i]);
	if (cfg->hp_outs)
		debug_badness("hp_outs = %x/%x/%x/%x : %x/%x/%x/%x\n",
		      cfg->hp_pins[0], cfg->hp_pins[1],
		      cfg->hp_pins[2], cfg->hp_pins[3],
		      spec->multiout.hp_out_nid[0],
		      spec->multiout.hp_out_nid[1],
		      spec->multiout.hp_out_nid[2],
		      spec->multiout.hp_out_nid[3]);
	for (i = 0; i < cfg->hp_outs; i++)
		print_nid_path_idx(codec, "  hp ", spec->hp_paths[i]);
	if (cfg->speaker_outs)
		debug_badness("spk_outs = %x/%x/%x/%x : %x/%x/%x/%x\n",
		      cfg->speaker_pins[0], cfg->speaker_pins[1],
		      cfg->speaker_pins[2], cfg->speaker_pins[3],
		      spec->multiout.extra_out_nid[0],
		      spec->multiout.extra_out_nid[1],
		      spec->multiout.extra_out_nid[2],
		      spec->multiout.extra_out_nid[3]);
	for (i = 0; i < cfg->speaker_outs; i++)
		print_nid_path_idx(codec, "  spk", spec->speaker_paths[i]);
	for (i = 0; i < 3; i++)
		print_nid_path_idx(codec, "  mix", spec->aamix_out_paths[i]);
}
#else
#define debug_show_configs(codec, cfg) /* NOP */
#endif

/* find all available DACs of the codec */
static void fill_all_dac_nids(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;
	hda_nid_t nid = codec->start_nid;

	spec->num_all_dacs = 0;
	memset(spec->all_dacs, 0, sizeof(spec->all_dacs));
	for (i = 0; i < codec->num_nodes; i++, nid++) {
		if (get_wcaps_type(get_wcaps(codec, nid)) != AC_WID_AUD_OUT)
			continue;
		if (spec->num_all_dacs >= ARRAY_SIZE(spec->all_dacs)) {
			codec_err(codec, "Too many DACs!\n");
			break;
		}
		spec->all_dacs[spec->num_all_dacs++] = nid;
	}
}

static int parse_output_paths(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	struct auto_pin_cfg *best_cfg;
	unsigned int val;
	int best_badness = INT_MAX;
	int badness;
	bool fill_hardwired = true, fill_mio_first = true;
	bool best_wired = true, best_mio = true;
	bool hp_spk_swapped = false;

	best_cfg = kmalloc(sizeof(*best_cfg), GFP_KERNEL);
	if (!best_cfg)
		return -ENOMEM;
	*best_cfg = *cfg;

	for (;;) {
		badness = fill_and_eval_dacs(codec, fill_hardwired,
					     fill_mio_first);
		if (badness < 0) {
			kfree(best_cfg);
			return badness;
		}
		debug_badness("==> lo_type=%d, wired=%d, mio=%d, badness=0x%x\n",
			      cfg->line_out_type, fill_hardwired, fill_mio_first,
			      badness);
		debug_show_configs(codec, cfg);
		if (badness < best_badness) {
			best_badness = badness;
			*best_cfg = *cfg;
			best_wired = fill_hardwired;
			best_mio = fill_mio_first;
		}
		if (!badness)
			break;
		fill_mio_first = !fill_mio_first;
		if (!fill_mio_first)
			continue;
		fill_hardwired = !fill_hardwired;
		if (!fill_hardwired)
			continue;
		if (hp_spk_swapped)
			break;
		hp_spk_swapped = true;
		if (cfg->speaker_outs > 0 &&
		    cfg->line_out_type == AUTO_PIN_HP_OUT) {
			cfg->hp_outs = cfg->line_outs;
			memcpy(cfg->hp_pins, cfg->line_out_pins,
			       sizeof(cfg->hp_pins));
			cfg->line_outs = cfg->speaker_outs;
			memcpy(cfg->line_out_pins, cfg->speaker_pins,
			       sizeof(cfg->speaker_pins));
			cfg->speaker_outs = 0;
			memset(cfg->speaker_pins, 0, sizeof(cfg->speaker_pins));
			cfg->line_out_type = AUTO_PIN_SPEAKER_OUT;
			fill_hardwired = true;
			continue;
		}
		if (cfg->hp_outs > 0 &&
		    cfg->line_out_type == AUTO_PIN_SPEAKER_OUT) {
			cfg->speaker_outs = cfg->line_outs;
			memcpy(cfg->speaker_pins, cfg->line_out_pins,
			       sizeof(cfg->speaker_pins));
			cfg->line_outs = cfg->hp_outs;
			memcpy(cfg->line_out_pins, cfg->hp_pins,
			       sizeof(cfg->hp_pins));
			cfg->hp_outs = 0;
			memset(cfg->hp_pins, 0, sizeof(cfg->hp_pins));
			cfg->line_out_type = AUTO_PIN_HP_OUT;
			fill_hardwired = true;
			continue;
		}
		break;
	}

	if (badness) {
		debug_badness("==> restoring best_cfg\n");
		*cfg = *best_cfg;
		fill_and_eval_dacs(codec, best_wired, best_mio);
	}
	debug_badness("==> Best config: lo_type=%d, wired=%d, mio=%d\n",
		      cfg->line_out_type, best_wired, best_mio);
	debug_show_configs(codec, cfg);

	if (cfg->line_out_pins[0]) {
		struct nid_path *path;
		path = snd_hda_get_path_from_idx(codec, spec->out_paths[0]);
		if (path)
			spec->vmaster_nid = look_for_out_vol_nid(codec, path);
		if (spec->vmaster_nid) {
			snd_hda_set_vmaster_tlv(codec, spec->vmaster_nid,
						HDA_OUTPUT, spec->vmaster_tlv);
			if (spec->dac_min_mute)
				spec->vmaster_tlv[3] |= TLV_DB_SCALE_MUTE;
		}
	}

	/* set initial pinctl targets */
	if (spec->prefer_hp_amp || cfg->line_out_type == AUTO_PIN_HP_OUT)
		val = PIN_HP;
	else
		val = PIN_OUT;
	set_pin_targets(codec, cfg->line_outs, cfg->line_out_pins, val);
	if (cfg->line_out_type != AUTO_PIN_HP_OUT)
		set_pin_targets(codec, cfg->hp_outs, cfg->hp_pins, PIN_HP);
	if (cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
		val = spec->prefer_hp_amp ? PIN_HP : PIN_OUT;
		set_pin_targets(codec, cfg->speaker_outs,
				cfg->speaker_pins, val);
	}

	/* clear indep_hp flag if not available */
	if (spec->indep_hp && !indep_hp_possible(codec))
		spec->indep_hp = 0;

	kfree(best_cfg);
	return 0;
}

/* add playback controls from the parsed DAC table */
static int create_multi_out_ctls(struct hda_codec *codec,
				 const struct auto_pin_cfg *cfg)
{
	struct hda_gen_spec *spec = codec->spec;
	int i, err, noutputs;

	noutputs = cfg->line_outs;
	if (spec->multi_ios > 0 && cfg->line_outs < 3)
		noutputs += spec->multi_ios;

	for (i = 0; i < noutputs; i++) {
		const char *name;
		int index;
		struct nid_path *path;

		path = snd_hda_get_path_from_idx(codec, spec->out_paths[i]);
		if (!path)
			continue;

		name = get_line_out_pfx(codec, i, &index, NID_PATH_VOL_CTL);
		if (!name || !strcmp(name, "CLFE")) {
			/* Center/LFE */
			err = add_vol_ctl(codec, "Center", 0, 1, path);
			if (err < 0)
				return err;
			err = add_vol_ctl(codec, "LFE", 0, 2, path);
			if (err < 0)
				return err;
		} else {
			err = add_stereo_vol(codec, name, index, path);
			if (err < 0)
				return err;
		}

		name = get_line_out_pfx(codec, i, &index, NID_PATH_MUTE_CTL);
		if (!name || !strcmp(name, "CLFE")) {
			err = add_sw_ctl(codec, "Center", 0, 1, path);
			if (err < 0)
				return err;
			err = add_sw_ctl(codec, "LFE", 0, 2, path);
			if (err < 0)
				return err;
		} else {
			err = add_stereo_sw(codec, name, index, path);
			if (err < 0)
				return err;
		}
	}
	return 0;
}

static int create_extra_out(struct hda_codec *codec, int path_idx,
			    const char *pfx, int cidx)
{
	struct nid_path *path;
	int err;

	path = snd_hda_get_path_from_idx(codec, path_idx);
	if (!path)
		return 0;
	err = add_stereo_vol(codec, pfx, cidx, path);
	if (err < 0)
		return err;
	err = add_stereo_sw(codec, pfx, cidx, path);
	if (err < 0)
		return err;
	return 0;
}

/* add playback controls for speaker and HP outputs */
static int create_extra_outs(struct hda_codec *codec, int num_pins,
			     const int *paths, const char *pfx)
{
	int i;

	for (i = 0; i < num_pins; i++) {
		const char *name;
		char tmp[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
		int err, idx = 0;

		if (num_pins == 2 && i == 1 && !strcmp(pfx, "Speaker"))
			name = "Bass Speaker";
		else if (num_pins >= 3) {
			snprintf(tmp, sizeof(tmp), "%s %s",
				 pfx, channel_name[i]);
			name = tmp;
		} else {
			name = pfx;
			idx = i;
		}
		err = create_extra_out(codec, paths[i], name, idx);
		if (err < 0)
			return err;
	}
	return 0;
}

static int create_hp_out_ctls(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	return create_extra_outs(codec, spec->autocfg.hp_outs,
				 spec->hp_paths,
				 "Headphone");
}

static int create_speaker_out_ctls(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	return create_extra_outs(codec, spec->autocfg.speaker_outs,
				 spec->speaker_paths,
				 "Speaker");
}

/*
 * independent HP controls
 */

static void call_hp_automute(struct hda_codec *codec,
			     struct hda_jack_callback *jack);
static int indep_hp_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	return snd_hda_enum_bool_helper_info(kcontrol, uinfo);
}

static int indep_hp_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	ucontrol->value.enumerated.item[0] = spec->indep_hp_enabled;
	return 0;
}

static void update_aamix_paths(struct hda_codec *codec, bool do_mix,
			       int nomix_path_idx, int mix_path_idx,
			       int out_type);

static int indep_hp_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	unsigned int select = ucontrol->value.enumerated.item[0];
	int ret = 0;

	mutex_lock(&spec->pcm_mutex);
	if (spec->active_streams) {
		ret = -EBUSY;
		goto unlock;
	}

	if (spec->indep_hp_enabled != select) {
		hda_nid_t *dacp;
		if (spec->autocfg.line_out_type == AUTO_PIN_HP_OUT)
			dacp = &spec->private_dac_nids[0];
		else
			dacp = &spec->multiout.hp_out_nid[0];

		/* update HP aamix paths in case it conflicts with indep HP */
		if (spec->have_aamix_ctl) {
			if (spec->autocfg.line_out_type == AUTO_PIN_HP_OUT)
				update_aamix_paths(codec, spec->aamix_mode,
						   spec->out_paths[0],
						   spec->aamix_out_paths[0],
						   spec->autocfg.line_out_type);
			else
				update_aamix_paths(codec, spec->aamix_mode,
						   spec->hp_paths[0],
						   spec->aamix_out_paths[1],
						   AUTO_PIN_HP_OUT);
		}

		spec->indep_hp_enabled = select;
		if (spec->indep_hp_enabled)
			*dacp = 0;
		else
			*dacp = spec->alt_dac_nid;

		call_hp_automute(codec, NULL);
		ret = 1;
	}
 unlock:
	mutex_unlock(&spec->pcm_mutex);
	return ret;
}

static const struct snd_kcontrol_new indep_hp_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Independent HP",
	.info = indep_hp_info,
	.get = indep_hp_get,
	.put = indep_hp_put,
};


static int create_indep_hp_ctls(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	hda_nid_t dac;

	if (!spec->indep_hp)
		return 0;
	if (spec->autocfg.line_out_type == AUTO_PIN_HP_OUT)
		dac = spec->multiout.dac_nids[0];
	else
		dac = spec->multiout.hp_out_nid[0];
	if (!dac) {
		spec->indep_hp = 0;
		return 0;
	}

	spec->indep_hp_enabled = false;
	spec->alt_dac_nid = dac;
	if (!snd_hda_gen_add_kctl(spec, NULL, &indep_hp_ctl))
		return -ENOMEM;
	return 0;
}

/*
 * channel mode enum control
 */

static int ch_mode_info(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	int chs;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = spec->multi_ios + 1;
	if (uinfo->value.enumerated.item > spec->multi_ios)
		uinfo->value.enumerated.item = spec->multi_ios;
	chs = uinfo->value.enumerated.item * 2 + spec->min_channel_count;
	sprintf(uinfo->value.enumerated.name, "%dch", chs);
	return 0;
}

static int ch_mode_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	ucontrol->value.enumerated.item[0] =
		(spec->ext_channel_count - spec->min_channel_count) / 2;
	return 0;
}

static inline struct nid_path *
get_multiio_path(struct hda_codec *codec, int idx)
{
	struct hda_gen_spec *spec = codec->spec;
	return snd_hda_get_path_from_idx(codec,
		spec->out_paths[spec->autocfg.line_outs + idx]);
}

static void update_automute_all(struct hda_codec *codec);

/* Default value to be passed as aamix argument for snd_hda_activate_path();
 * used for output paths
 */
static bool aamix_default(struct hda_gen_spec *spec)
{
	return !spec->have_aamix_ctl || spec->aamix_mode;
}

static int set_multi_io(struct hda_codec *codec, int idx, bool output)
{
	struct hda_gen_spec *spec = codec->spec;
	hda_nid_t nid = spec->multi_io[idx].pin;
	struct nid_path *path;

	path = get_multiio_path(codec, idx);
	if (!path)
		return -EINVAL;

	if (path->active == output)
		return 0;

	if (output) {
		set_pin_target(codec, nid, PIN_OUT, true);
		snd_hda_activate_path(codec, path, true, aamix_default(spec));
		set_pin_eapd(codec, nid, true);
	} else {
		set_pin_eapd(codec, nid, false);
		snd_hda_activate_path(codec, path, false, aamix_default(spec));
		set_pin_target(codec, nid, spec->multi_io[idx].ctl_in, true);
		path_power_down_sync(codec, path);
	}

	/* update jack retasking in case it modifies any of them */
	update_automute_all(codec);

	return 0;
}

static int ch_mode_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	int i, ch;

	ch = ucontrol->value.enumerated.item[0];
	if (ch < 0 || ch > spec->multi_ios)
		return -EINVAL;
	if (ch == (spec->ext_channel_count - spec->min_channel_count) / 2)
		return 0;
	spec->ext_channel_count = ch * 2 + spec->min_channel_count;
	for (i = 0; i < spec->multi_ios; i++)
		set_multi_io(codec, i, i < ch);
	spec->multiout.max_channels = max(spec->ext_channel_count,
					  spec->const_channel_count);
	if (spec->need_dac_fix)
		spec->multiout.num_dacs = spec->multiout.max_channels / 2;
	return 1;
}

static const struct snd_kcontrol_new channel_mode_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Channel Mode",
	.info = ch_mode_info,
	.get = ch_mode_get,
	.put = ch_mode_put,
};

static int create_multi_channel_mode(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;

	if (spec->multi_ios > 0) {
		if (!snd_hda_gen_add_kctl(spec, NULL, &channel_mode_enum))
			return -ENOMEM;
	}
	return 0;
}

/*
 * aamix loopback enable/disable switch
 */

#define loopback_mixing_info	indep_hp_info

static int loopback_mixing_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	ucontrol->value.enumerated.item[0] = spec->aamix_mode;
	return 0;
}

static void update_aamix_paths(struct hda_codec *codec, bool do_mix,
			       int nomix_path_idx, int mix_path_idx,
			       int out_type)
{
	struct hda_gen_spec *spec = codec->spec;
	struct nid_path *nomix_path, *mix_path;

	nomix_path = snd_hda_get_path_from_idx(codec, nomix_path_idx);
	mix_path = snd_hda_get_path_from_idx(codec, mix_path_idx);
	if (!nomix_path || !mix_path)
		return;

	/* if HP aamix path is driven from a different DAC and the
	 * independent HP mode is ON, can't turn on aamix path
	 */
	if (out_type == AUTO_PIN_HP_OUT && spec->indep_hp_enabled &&
	    mix_path->path[0] != spec->alt_dac_nid)
		do_mix = false;

	if (do_mix) {
		snd_hda_activate_path(codec, nomix_path, false, true);
		snd_hda_activate_path(codec, mix_path, true, true);
		path_power_down_sync(codec, nomix_path);
	} else {
		snd_hda_activate_path(codec, mix_path, false, false);
		snd_hda_activate_path(codec, nomix_path, true, false);
		path_power_down_sync(codec, mix_path);
	}
}

/* re-initialize the output paths; only called from loopback_mixing_put() */
static void update_output_paths(struct hda_codec *codec, int num_outs,
				const int *paths)
{
	struct hda_gen_spec *spec = codec->spec;
	struct nid_path *path;
	int i;

	for (i = 0; i < num_outs; i++) {
		path = snd_hda_get_path_from_idx(codec, paths[i]);
		if (path)
			snd_hda_activate_path(codec, path, path->active,
					      spec->aamix_mode);
	}
}

static int loopback_mixing_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	const struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int val = ucontrol->value.enumerated.item[0];

	if (val == spec->aamix_mode)
		return 0;
	spec->aamix_mode = val;
	if (has_aamix_out_paths(spec)) {
		update_aamix_paths(codec, val, spec->out_paths[0],
				   spec->aamix_out_paths[0],
				   cfg->line_out_type);
		update_aamix_paths(codec, val, spec->hp_paths[0],
				   spec->aamix_out_paths[1],
				   AUTO_PIN_HP_OUT);
		update_aamix_paths(codec, val, spec->speaker_paths[0],
				   spec->aamix_out_paths[2],
				   AUTO_PIN_SPEAKER_OUT);
	} else {
		update_output_paths(codec, cfg->line_outs, spec->out_paths);
		if (cfg->line_out_type != AUTO_PIN_HP_OUT)
			update_output_paths(codec, cfg->hp_outs, spec->hp_paths);
		if (cfg->line_out_type != AUTO_PIN_SPEAKER_OUT)
			update_output_paths(codec, cfg->speaker_outs,
					    spec->speaker_paths);
	}
	return 1;
}

static const struct snd_kcontrol_new loopback_mixing_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Loopback Mixing",
	.info = loopback_mixing_info,
	.get = loopback_mixing_get,
	.put = loopback_mixing_put,
};

static int create_loopback_mixing_ctl(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;

	if (!spec->mixer_nid)
		return 0;
	if (!snd_hda_gen_add_kctl(spec, NULL, &loopback_mixing_enum))
		return -ENOMEM;
	spec->have_aamix_ctl = 1;
	/* if no explicit aamix path is present (e.g. for Realtek codecs),
	 * enable aamix as default -- just for compatibility
	 */
	spec->aamix_mode = !has_aamix_out_paths(spec);
	return 0;
}

/*
 * shared headphone/mic handling
 */

static void call_update_outputs(struct hda_codec *codec);

/* for shared I/O, change the pin-control accordingly */
static void update_hp_mic(struct hda_codec *codec, int adc_mux, bool force)
{
	struct hda_gen_spec *spec = codec->spec;
	bool as_mic;
	unsigned int val;
	hda_nid_t pin;

	pin = spec->hp_mic_pin;
	as_mic = spec->cur_mux[adc_mux] == spec->hp_mic_mux_idx;

	if (!force) {
		val = snd_hda_codec_get_pin_target(codec, pin);
		if (as_mic) {
			if (val & PIN_IN)
				return;
		} else {
			if (val & PIN_OUT)
				return;
		}
	}

	val = snd_hda_get_default_vref(codec, pin);
	/* if the HP pin doesn't support VREF and the codec driver gives an
	 * alternative pin, set up the VREF on that pin instead
	 */
	if (val == AC_PINCTL_VREF_HIZ && spec->shared_mic_vref_pin) {
		const hda_nid_t vref_pin = spec->shared_mic_vref_pin;
		unsigned int vref_val = snd_hda_get_default_vref(codec, vref_pin);
		if (vref_val != AC_PINCTL_VREF_HIZ)
			snd_hda_set_pin_ctl_cache(codec, vref_pin,
						  PIN_IN | (as_mic ? vref_val : 0));
	}

	if (!spec->hp_mic_jack_modes) {
		if (as_mic)
			val |= PIN_IN;
		else
			val = PIN_HP;
		set_pin_target(codec, pin, val, true);
		call_hp_automute(codec, NULL);
	}
}

/* create a shared input with the headphone out */
static int create_hp_mic(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int defcfg;
	hda_nid_t nid;

	if (!spec->hp_mic) {
		if (spec->suppress_hp_mic_detect)
			return 0;
		/* automatic detection: only if no input or a single internal
		 * input pin is found, try to detect the shared hp/mic
		 */
		if (cfg->num_inputs > 1)
			return 0;
		else if (cfg->num_inputs == 1) {
			defcfg = snd_hda_codec_get_pincfg(codec, cfg->inputs[0].pin);
			if (snd_hda_get_input_pin_attr(defcfg) != INPUT_PIN_ATTR_INT)
				return 0;
		}
	}

	spec->hp_mic = 0; /* clear once */
	if (cfg->num_inputs >= AUTO_CFG_MAX_INS)
		return 0;

	nid = 0;
	if (cfg->line_out_type == AUTO_PIN_HP_OUT && cfg->line_outs > 0)
		nid = cfg->line_out_pins[0];
	else if (cfg->hp_outs > 0)
		nid = cfg->hp_pins[0];
	if (!nid)
		return 0;

	if (!(snd_hda_query_pin_caps(codec, nid) & AC_PINCAP_IN))
		return 0; /* no input */

	cfg->inputs[cfg->num_inputs].pin = nid;
	cfg->inputs[cfg->num_inputs].type = AUTO_PIN_MIC;
	cfg->inputs[cfg->num_inputs].is_headphone_mic = 1;
	cfg->num_inputs++;
	spec->hp_mic = 1;
	spec->hp_mic_pin = nid;
	/* we can't handle auto-mic together with HP-mic */
	spec->suppress_auto_mic = 1;
	codec_dbg(codec, "Enable shared I/O jack on NID 0x%x\n", nid);
	return 0;
}

/*
 * output jack mode
 */

static int create_hp_mic_jack_mode(struct hda_codec *codec, hda_nid_t pin);

static const char * const out_jack_texts[] = {
	"Line Out", "Headphone Out",
};

static int out_jack_mode_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	return snd_hda_enum_helper_info(kcontrol, uinfo, 2, out_jack_texts);
}

static int out_jack_mode_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	if (snd_hda_codec_get_pin_target(codec, nid) == PIN_HP)
		ucontrol->value.enumerated.item[0] = 1;
	else
		ucontrol->value.enumerated.item[0] = 0;
	return 0;
}

static int out_jack_mode_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	unsigned int val;

	val = ucontrol->value.enumerated.item[0] ? PIN_HP : PIN_OUT;
	if (snd_hda_codec_get_pin_target(codec, nid) == val)
		return 0;
	snd_hda_set_pin_ctl_cache(codec, nid, val);
	return 1;
}

static const struct snd_kcontrol_new out_jack_mode_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = out_jack_mode_info,
	.get = out_jack_mode_get,
	.put = out_jack_mode_put,
};

static bool find_kctl_name(struct hda_codec *codec, const char *name, int idx)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->kctls.used; i++) {
		struct snd_kcontrol_new *kctl = snd_array_elem(&spec->kctls, i);
		if (!strcmp(kctl->name, name) && kctl->index == idx)
			return true;
	}
	return false;
}

static void get_jack_mode_name(struct hda_codec *codec, hda_nid_t pin,
			       char *name, size_t name_len)
{
	struct hda_gen_spec *spec = codec->spec;
	int idx = 0;

	snd_hda_get_pin_label(codec, pin, &spec->autocfg, name, name_len, &idx);
	strlcat(name, " Jack Mode", name_len);

	for (; find_kctl_name(codec, name, idx); idx++)
		;
}

static int get_out_jack_num_items(struct hda_codec *codec, hda_nid_t pin)
{
	struct hda_gen_spec *spec = codec->spec;
	if (spec->add_jack_modes) {
		unsigned int pincap = snd_hda_query_pin_caps(codec, pin);
		if ((pincap & AC_PINCAP_OUT) && (pincap & AC_PINCAP_HP_DRV))
			return 2;
	}
	return 1;
}

static int create_out_jack_modes(struct hda_codec *codec, int num_pins,
				 hda_nid_t *pins)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	for (i = 0; i < num_pins; i++) {
		hda_nid_t pin = pins[i];
		if (pin == spec->hp_mic_pin)
			continue;
		if (get_out_jack_num_items(codec, pin) > 1) {
			struct snd_kcontrol_new *knew;
			char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
			get_jack_mode_name(codec, pin, name, sizeof(name));
			knew = snd_hda_gen_add_kctl(spec, name,
						    &out_jack_mode_enum);
			if (!knew)
				return -ENOMEM;
			knew->private_value = pin;
		}
	}

	return 0;
}

/*
 * input jack mode
 */

/* from AC_PINCTL_VREF_HIZ to AC_PINCTL_VREF_100 */
#define NUM_VREFS	6

static const char * const vref_texts[NUM_VREFS] = {
	"Line In", "Mic 50pc Bias", "Mic 0V Bias",
	"", "Mic 80pc Bias", "Mic 100pc Bias"
};

static unsigned int get_vref_caps(struct hda_codec *codec, hda_nid_t pin)
{
	unsigned int pincap;

	pincap = snd_hda_query_pin_caps(codec, pin);
	pincap = (pincap & AC_PINCAP_VREF) >> AC_PINCAP_VREF_SHIFT;
	/* filter out unusual vrefs */
	pincap &= ~(AC_PINCAP_VREF_GRD | AC_PINCAP_VREF_100);
	return pincap;
}

/* convert from the enum item index to the vref ctl index (0=HIZ, 1=50%...) */
static int get_vref_idx(unsigned int vref_caps, unsigned int item_idx)
{
	unsigned int i, n = 0;

	for (i = 0; i < NUM_VREFS; i++) {
		if (vref_caps & (1 << i)) {
			if (n == item_idx)
				return i;
			n++;
		}
	}
	return 0;
}

/* convert back from the vref ctl index to the enum item index */
static int cvt_from_vref_idx(unsigned int vref_caps, unsigned int idx)
{
	unsigned int i, n = 0;

	for (i = 0; i < NUM_VREFS; i++) {
		if (i == idx)
			return n;
		if (vref_caps & (1 << i))
			n++;
	}
	return 0;
}

static int in_jack_mode_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	unsigned int vref_caps = get_vref_caps(codec, nid);

	snd_hda_enum_helper_info(kcontrol, uinfo, hweight32(vref_caps),
				 vref_texts);
	/* set the right text */
	strcpy(uinfo->value.enumerated.name,
	       vref_texts[get_vref_idx(vref_caps, uinfo->value.enumerated.item)]);
	return 0;
}

static int in_jack_mode_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	unsigned int vref_caps = get_vref_caps(codec, nid);
	unsigned int idx;

	idx = snd_hda_codec_get_pin_target(codec, nid) & AC_PINCTL_VREFEN;
	ucontrol->value.enumerated.item[0] = cvt_from_vref_idx(vref_caps, idx);
	return 0;
}

static int in_jack_mode_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	unsigned int vref_caps = get_vref_caps(codec, nid);
	unsigned int val, idx;

	val = snd_hda_codec_get_pin_target(codec, nid);
	idx = cvt_from_vref_idx(vref_caps, val & AC_PINCTL_VREFEN);
	if (idx == ucontrol->value.enumerated.item[0])
		return 0;

	val &= ~AC_PINCTL_VREFEN;
	val |= get_vref_idx(vref_caps, ucontrol->value.enumerated.item[0]);
	snd_hda_set_pin_ctl_cache(codec, nid, val);
	return 1;
}

static const struct snd_kcontrol_new in_jack_mode_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = in_jack_mode_info,
	.get = in_jack_mode_get,
	.put = in_jack_mode_put,
};

static int get_in_jack_num_items(struct hda_codec *codec, hda_nid_t pin)
{
	struct hda_gen_spec *spec = codec->spec;
	int nitems = 0;
	if (spec->add_jack_modes)
		nitems = hweight32(get_vref_caps(codec, pin));
	return nitems ? nitems : 1;
}

static int create_in_jack_mode(struct hda_codec *codec, hda_nid_t pin)
{
	struct hda_gen_spec *spec = codec->spec;
	struct snd_kcontrol_new *knew;
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	unsigned int defcfg;

	if (pin == spec->hp_mic_pin)
		return 0; /* already done in create_out_jack_mode() */

	/* no jack mode for fixed pins */
	defcfg = snd_hda_codec_get_pincfg(codec, pin);
	if (snd_hda_get_input_pin_attr(defcfg) == INPUT_PIN_ATTR_INT)
		return 0;

	/* no multiple vref caps? */
	if (get_in_jack_num_items(codec, pin) <= 1)
		return 0;

	get_jack_mode_name(codec, pin, name, sizeof(name));
	knew = snd_hda_gen_add_kctl(spec, name, &in_jack_mode_enum);
	if (!knew)
		return -ENOMEM;
	knew->private_value = pin;
	return 0;
}

/*
 * HP/mic shared jack mode
 */
static int hp_mic_jack_mode_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	int out_jacks = get_out_jack_num_items(codec, nid);
	int in_jacks = get_in_jack_num_items(codec, nid);
	const char *text = NULL;
	int idx;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = out_jacks + in_jacks;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	idx = uinfo->value.enumerated.item;
	if (idx < out_jacks) {
		if (out_jacks > 1)
			text = out_jack_texts[idx];
		else
			text = "Headphone Out";
	} else {
		idx -= out_jacks;
		if (in_jacks > 1) {
			unsigned int vref_caps = get_vref_caps(codec, nid);
			text = vref_texts[get_vref_idx(vref_caps, idx)];
		} else
			text = "Mic In";
	}

	strcpy(uinfo->value.enumerated.name, text);
	return 0;
}

static int get_cur_hp_mic_jack_mode(struct hda_codec *codec, hda_nid_t nid)
{
	int out_jacks = get_out_jack_num_items(codec, nid);
	int in_jacks = get_in_jack_num_items(codec, nid);
	unsigned int val = snd_hda_codec_get_pin_target(codec, nid);
	int idx = 0;

	if (val & PIN_OUT) {
		if (out_jacks > 1 && val == PIN_HP)
			idx = 1;
	} else if (val & PIN_IN) {
		idx = out_jacks;
		if (in_jacks > 1) {
			unsigned int vref_caps = get_vref_caps(codec, nid);
			val &= AC_PINCTL_VREFEN;
			idx += cvt_from_vref_idx(vref_caps, val);
		}
	}
	return idx;
}

static int hp_mic_jack_mode_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	ucontrol->value.enumerated.item[0] =
		get_cur_hp_mic_jack_mode(codec, nid);
	return 0;
}

static int hp_mic_jack_mode_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	int out_jacks = get_out_jack_num_items(codec, nid);
	int in_jacks = get_in_jack_num_items(codec, nid);
	unsigned int val, oldval, idx;

	oldval = get_cur_hp_mic_jack_mode(codec, nid);
	idx = ucontrol->value.enumerated.item[0];
	if (oldval == idx)
		return 0;

	if (idx < out_jacks) {
		if (out_jacks > 1)
			val = idx ? PIN_HP : PIN_OUT;
		else
			val = PIN_HP;
	} else {
		idx -= out_jacks;
		if (in_jacks > 1) {
			unsigned int vref_caps = get_vref_caps(codec, nid);
			val = snd_hda_codec_get_pin_target(codec, nid);
			val &= ~(AC_PINCTL_VREFEN | PIN_HP);
			val |= get_vref_idx(vref_caps, idx) | PIN_IN;
		} else
			val = snd_hda_get_default_vref(codec, nid) | PIN_IN;
	}
	snd_hda_set_pin_ctl_cache(codec, nid, val);
	call_hp_automute(codec, NULL);

	return 1;
}

static const struct snd_kcontrol_new hp_mic_jack_mode_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = hp_mic_jack_mode_info,
	.get = hp_mic_jack_mode_get,
	.put = hp_mic_jack_mode_put,
};

static int create_hp_mic_jack_mode(struct hda_codec *codec, hda_nid_t pin)
{
	struct hda_gen_spec *spec = codec->spec;
	struct snd_kcontrol_new *knew;

	knew = snd_hda_gen_add_kctl(spec, "Headphone Mic Jack Mode",
				    &hp_mic_jack_mode_enum);
	if (!knew)
		return -ENOMEM;
	knew->private_value = pin;
	spec->hp_mic_jack_modes = 1;
	return 0;
}

/*
 * Parse input paths
 */

/* add the powersave loopback-list entry */
static int add_loopback_list(struct hda_gen_spec *spec, hda_nid_t mix, int idx)
{
	struct hda_amp_list *list;

	list = snd_array_new(&spec->loopback_list);
	if (!list)
		return -ENOMEM;
	list->nid = mix;
	list->dir = HDA_INPUT;
	list->idx = idx;
	spec->loopback.amplist = spec->loopback_list.list;
	return 0;
}

/* return true if either a volume or a mute amp is found for the given
 * aamix path; the amp has to be either in the mixer node or its direct leaf
 */
static bool look_for_mix_leaf_ctls(struct hda_codec *codec, hda_nid_t mix_nid,
				   hda_nid_t pin, unsigned int *mix_val,
				   unsigned int *mute_val)
{
	int idx, num_conns;
	const hda_nid_t *list;
	hda_nid_t nid;

	idx = snd_hda_get_conn_index(codec, mix_nid, pin, true);
	if (idx < 0)
		return false;

	*mix_val = *mute_val = 0;
	if (nid_has_volume(codec, mix_nid, HDA_INPUT))
		*mix_val = HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT);
	if (nid_has_mute(codec, mix_nid, HDA_INPUT))
		*mute_val = HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT);
	if (*mix_val && *mute_val)
		return true;

	/* check leaf node */
	num_conns = snd_hda_get_conn_list(codec, mix_nid, &list);
	if (num_conns < idx)
		return false;
	nid = list[idx];
	if (!*mix_val && nid_has_volume(codec, nid, HDA_OUTPUT) &&
	    !is_ctl_associated(codec, nid, HDA_OUTPUT, 0, NID_PATH_VOL_CTL))
		*mix_val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
	if (!*mute_val && nid_has_mute(codec, nid, HDA_OUTPUT) &&
	    !is_ctl_associated(codec, nid, HDA_OUTPUT, 0, NID_PATH_MUTE_CTL))
		*mute_val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);

	return *mix_val || *mute_val;
}

/* create input playback/capture controls for the given pin */
static int new_analog_input(struct hda_codec *codec, int input_idx,
			    hda_nid_t pin, const char *ctlname, int ctlidx,
			    hda_nid_t mix_nid)
{
	struct hda_gen_spec *spec = codec->spec;
	struct nid_path *path;
	unsigned int mix_val, mute_val;
	int err, idx;

	if (!look_for_mix_leaf_ctls(codec, mix_nid, pin, &mix_val, &mute_val))
		return 0;

	path = snd_hda_add_new_path(codec, pin, mix_nid, 0);
	if (!path)
		return -EINVAL;
	print_nid_path(codec, "loopback", path);
	spec->loopback_paths[input_idx] = snd_hda_get_path_idx(codec, path);

	idx = path->idx[path->depth - 1];
	if (mix_val) {
		err = __add_pb_vol_ctrl(spec, HDA_CTL_WIDGET_VOL, ctlname, ctlidx, mix_val);
		if (err < 0)
			return err;
		path->ctls[NID_PATH_VOL_CTL] = mix_val;
	}

	if (mute_val) {
		err = __add_pb_sw_ctrl(spec, HDA_CTL_WIDGET_MUTE, ctlname, ctlidx, mute_val);
		if (err < 0)
			return err;
		path->ctls[NID_PATH_MUTE_CTL] = mute_val;
	}

	path->active = true;
	err = add_loopback_list(spec, mix_nid, idx);
	if (err < 0)
		return err;

	if (spec->mixer_nid != spec->mixer_merge_nid &&
	    !spec->loopback_merge_path) {
		path = snd_hda_add_new_path(codec, spec->mixer_nid,
					    spec->mixer_merge_nid, 0);
		if (path) {
			print_nid_path(codec, "loopback-merge", path);
			path->active = true;
			spec->loopback_merge_path =
				snd_hda_get_path_idx(codec, path);
		}
	}

	return 0;
}

static int is_input_pin(struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int pincap = snd_hda_query_pin_caps(codec, nid);
	return (pincap & AC_PINCAP_IN) != 0;
}

/* Parse the codec tree and retrieve ADCs */
static int fill_adc_nids(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	hda_nid_t nid;
	hda_nid_t *adc_nids = spec->adc_nids;
	int max_nums = ARRAY_SIZE(spec->adc_nids);
	int i, nums = 0;

	nid = codec->start_nid;
	for (i = 0; i < codec->num_nodes; i++, nid++) {
		unsigned int caps = get_wcaps(codec, nid);
		int type = get_wcaps_type(caps);

		if (type != AC_WID_AUD_IN || (caps & AC_WCAP_DIGITAL))
			continue;
		adc_nids[nums] = nid;
		if (++nums >= max_nums)
			break;
	}
	spec->num_adc_nids = nums;

	/* copy the detected ADCs to all_adcs[] */
	spec->num_all_adcs = nums;
	memcpy(spec->all_adcs, spec->adc_nids, nums * sizeof(hda_nid_t));

	return nums;
}

/* filter out invalid adc_nids that don't give all active input pins;
 * if needed, check whether dynamic ADC-switching is available
 */
static int check_dyn_adc_switch(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->input_mux;
	unsigned int ok_bits;
	int i, n, nums;

	nums = 0;
	ok_bits = 0;
	for (n = 0; n < spec->num_adc_nids; n++) {
		for (i = 0; i < imux->num_items; i++) {
			if (!spec->input_paths[i][n])
				break;
		}
		if (i >= imux->num_items) {
			ok_bits |= (1 << n);
			nums++;
		}
	}

	if (!ok_bits) {
		/* check whether ADC-switch is possible */
		for (i = 0; i < imux->num_items; i++) {
			for (n = 0; n < spec->num_adc_nids; n++) {
				if (spec->input_paths[i][n]) {
					spec->dyn_adc_idx[i] = n;
					break;
				}
			}
		}

		codec_dbg(codec, "enabling ADC switching\n");
		spec->dyn_adc_switch = 1;
	} else if (nums != spec->num_adc_nids) {
		/* shrink the invalid adcs and input paths */
		nums = 0;
		for (n = 0; n < spec->num_adc_nids; n++) {
			if (!(ok_bits & (1 << n)))
				continue;
			if (n != nums) {
				spec->adc_nids[nums] = spec->adc_nids[n];
				for (i = 0; i < imux->num_items; i++) {
					invalidate_nid_path(codec,
						spec->input_paths[i][nums]);
					spec->input_paths[i][nums] =
						spec->input_paths[i][n];
					spec->input_paths[i][n] = 0;
				}
			}
			nums++;
		}
		spec->num_adc_nids = nums;
	}

	if (imux->num_items == 1 ||
	    (imux->num_items == 2 && spec->hp_mic)) {
		codec_dbg(codec, "reducing to a single ADC\n");
		spec->num_adc_nids = 1; /* reduce to a single ADC */
	}

	/* single index for individual volumes ctls */
	if (!spec->dyn_adc_switch && spec->multi_cap_vol)
		spec->num_adc_nids = 1;

	return 0;
}

/* parse capture source paths from the given pin and create imux items */
static int parse_capture_source(struct hda_codec *codec, hda_nid_t pin,
				int cfg_idx, int num_adcs,
				const char *label, int anchor)
{
	struct hda_gen_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->input_mux;
	int imux_idx = imux->num_items;
	bool imux_added = false;
	int c;

	for (c = 0; c < num_adcs; c++) {
		struct nid_path *path;
		hda_nid_t adc = spec->adc_nids[c];

		if (!is_reachable_path(codec, pin, adc))
			continue;
		path = snd_hda_add_new_path(codec, pin, adc, anchor);
		if (!path)
			continue;
		print_nid_path(codec, "input", path);
		spec->input_paths[imux_idx][c] =
			snd_hda_get_path_idx(codec, path);

		if (!imux_added) {
			if (spec->hp_mic_pin == pin)
				spec->hp_mic_mux_idx = imux->num_items;
			spec->imux_pins[imux->num_items] = pin;
			snd_hda_add_imux_item(codec, imux, label, cfg_idx, NULL);
			imux_added = true;
			if (spec->dyn_adc_switch)
				spec->dyn_adc_idx[imux_idx] = c;
		}
	}

	return 0;
}

/*
 * create playback/capture controls for input pins
 */

/* fill the label for each input at first */
static int fill_input_pin_labels(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	const struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t pin = cfg->inputs[i].pin;
		const char *label;
		int j, idx;

		if (!is_input_pin(codec, pin))
			continue;

		label = hda_get_autocfg_input_label(codec, cfg, i);
		idx = 0;
		for (j = i - 1; j >= 0; j--) {
			if (spec->input_labels[j] &&
			    !strcmp(spec->input_labels[j], label)) {
				idx = spec->input_label_idxs[j] + 1;
				break;
			}
		}

		spec->input_labels[i] = label;
		spec->input_label_idxs[i] = idx;
	}

	return 0;
}

#define CFG_IDX_MIX	99	/* a dummy cfg->input idx for stereo mix */

static int create_input_ctls(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	const struct auto_pin_cfg *cfg = &spec->autocfg;
	hda_nid_t mixer = spec->mixer_nid;
	int num_adcs;
	int i, err;
	unsigned int val;

	num_adcs = fill_adc_nids(codec);
	if (num_adcs < 0)
		return 0;

	err = fill_input_pin_labels(codec);
	if (err < 0)
		return err;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t pin;

		pin = cfg->inputs[i].pin;
		if (!is_input_pin(codec, pin))
			continue;

		val = PIN_IN;
		if (cfg->inputs[i].type == AUTO_PIN_MIC)
			val |= snd_hda_get_default_vref(codec, pin);
		if (pin != spec->hp_mic_pin)
			set_pin_target(codec, pin, val, false);

		if (mixer) {
			if (is_reachable_path(codec, pin, mixer)) {
				err = new_analog_input(codec, i, pin,
						       spec->input_labels[i],
						       spec->input_label_idxs[i],
						       mixer);
				if (err < 0)
					return err;
			}
		}

		err = parse_capture_source(codec, pin, i, num_adcs,
					   spec->input_labels[i], -mixer);
		if (err < 0)
			return err;

		if (spec->add_jack_modes) {
			err = create_in_jack_mode(codec, pin);
			if (err < 0)
				return err;
		}
	}

	/* add stereo mix when explicitly enabled via hint */
	if (mixer && spec->add_stereo_mix_input &&
	    snd_hda_get_bool_hint(codec, "add_stereo_mix_input") > 0) {
		err = parse_capture_source(codec, mixer, CFG_IDX_MIX, num_adcs,
					   "Stereo Mix", 0);
		if (err < 0)
			return err;
	}

	return 0;
}


/*
 * input source mux
 */

/* get the input path specified by the given adc and imux indices */
static struct nid_path *get_input_path(struct hda_codec *codec, int adc_idx, int imux_idx)
{
	struct hda_gen_spec *spec = codec->spec;
	if (imux_idx < 0 || imux_idx >= HDA_MAX_NUM_INPUTS) {
		snd_BUG();
		return NULL;
	}
	if (spec->dyn_adc_switch)
		adc_idx = spec->dyn_adc_idx[imux_idx];
	if (adc_idx < 0 || adc_idx >= AUTO_CFG_MAX_INS) {
		snd_BUG();
		return NULL;
	}
	return snd_hda_get_path_from_idx(codec, spec->input_paths[imux_idx][adc_idx]);
}

static int mux_select(struct hda_codec *codec, unsigned int adc_idx,
		      unsigned int idx);

static int mux_enum_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	return snd_hda_input_mux_info(&spec->input_mux, uinfo);
}

static int mux_enum_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	/* the ctls are created at once with multiple counts */
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static int mux_enum_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	return mux_select(codec, adc_idx,
			  ucontrol->value.enumerated.item[0]);
}

static const struct snd_kcontrol_new cap_src_temp = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Input Source",
	.info = mux_enum_info,
	.get = mux_enum_get,
	.put = mux_enum_put,
};

/*
 * capture volume and capture switch ctls
 */

typedef int (*put_call_t)(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol);

/* call the given amp update function for all amps in the imux list at once */
static int cap_put_caller(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol,
			  put_call_t func, int type)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	const struct hda_input_mux *imux;
	struct nid_path *path;
	int i, adc_idx, err = 0;

	imux = &spec->input_mux;
	adc_idx = kcontrol->id.index;
	mutex_lock(&codec->control_mutex);
	/* we use the cache-only update at first since multiple input paths
	 * urn 0;
}


/*
 * input source mux
 */

/* get the input path specified bcodec *codecpecified bcodec *cod 1) {
/*
  s; *cothe ie inodec, i

/s,_caps,edundantfied wrFAC sizec *dwrol, an hda_adc_sd.in = sp * urn 0i

/d_wrFAC*/
		nuhs[i][nums]);
					spec->input_paths[i][nuut)
		retur codec->spe_new cap_src_te
}

static v independspec, mix_ncode]snd_hda_get_path_ get_in_jack_num_items(_VOL, ctlmix_ncode];		if (errc *sinfo->value.t nid_path path specified bygoizespeon adc espeon:sp * urn 0i

/d_wrFAC*/
0;hp_ctl = {
	.ifa * urn 0;
}


/*
 * inpuet_vref_idx(vrflushnd_kcontrol_)->dynflush_capsthe in
}

/* parspec				 0) {
		erenum_ync_hooknd create enum_ync_hooke_new canfo->value.t nid_path  the given a(structntrol,
			  stgnedgen_b_pin_50pc Bias",n pin astructuet_vref_	if (nlist			  s_elem_c Bias",n pin asetutuet_vref_	if (nlist			  s_etu_c Bias",n pin astlvtuet_vref_	if (nlisttlv
ntrol,
			  put_n asa_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->sp the giput_call_t funcnfo->value.t nid_po, 2, out_jat_vref_	if (nlist			  s_nd_kc 2, out_j& nid_has_mute(coER,
	.name = "Input Source",
	.info = mux_enumn ast,
	.get = mux_enum_get,
	.put = mux_enum_put,
};

/*
 * Cntrol,
V		  sme anacidx;
	i(_get,
	.put = mACCESS_READWRITE |(kcont_get,
	.put = mACCESS_TLV_READ |(kcont_get,
	.put = mACCESS_TLV_CALLBACK)i_channel_mo pin astrucda_codec *c pin asetutruct hda_gut_n asa_ctructlv.get .cda_gut_n astlv.}kcontrolt snd_ctl_elem_valudgen_b_pin_50pc Bias",n piswstructuet_vda_gG_IDean					   elem_c Bias",n piswsetutuet_vref_	if (nlist_elem__etu_ntrol,
			  put_swsa_codec *codec = snd_kcontrol_chip(kcontrol;
	struct hda_gen_spec *spec = codec->sp the giput_call_t funcnfo->value.t nid_po, 2, out_jat_vref_	if (nlist_elem__nd_kc 2, out_j& nid_haste_val;
}ER,
	.name = "Input Source",
	.info = mux_enum_wst,
	.get = mux_enum_get,
	.put = mux_enum_put,
};

/*
 * Cntrol,
Selem_"i_channel_mo piswstrucda_codec *c piswsetutruct hda_gut_swsa_ca_gen_spec *spec  < 0)
			n astrc->spec;
	if (imux_idx < 0 || iol->id.index;
	mutex_c->spds);
	int i, numk(&codedx, m_nid_patTL_WIDGET_MUTE, ctlname, ctspec, mix_nid, idx);
	if (err < f (!spec-dx, miums])dx, mi< 3])dx, m+ int in_jacdx, mi>ctspec, dx, med by the gi_get_path_	i ctspec, dx, m - dx, mix_vth_	= get_(codec, nom {
			stru!_patTL_WIDGET_MUTE, ctlname, vref ctl inID_PATH_VOL_CTL))
		*mix_val = HDA_CO& spec_patTL_WIDGET_MUTE, ctlname, ce mux
UTPUT) &&
	    !is_ctl_associated(codec, nid,.pin);
			inID_PATH_VOL_CTL))
		*mix_val =_val)
	bel;
		sif (val &L, ctlnamei]_adcs,
			!dx, mi->hp 0;

	i
		spNULL;lisec->inp->input_labec_patTL_WIDGET_MUTE, ctlname, ce mux
UTPUT) &&
	    !is_ctl_assocs = snd_hda_get_co -mixer)		stru!_patTL_WIDGET_MUTE, 
	if (err vref ctl inID_PATHUTE_CTL))
		*mute_val = HDA_& spec_patTL_WIDGET_MUTE, 
	if (err <e mux
UTPUT) &&
	    !is_ctl_associated(codec, nid,.pin);
			inID_PATHUTE_CTL))
		*mute_val_val)
	bel;
		sif (val &L, ctlnamei]_adcs,
			!dx, mi->hp 0;

	i
		spNULL;lisec->inp->input_labec_patTL_WIDGET_MUTE, 
	if (err <e mux
UTPUT) &&
	    !is_ctl_assocs = snd_hda_get_co -mixer)	d_ctl_elem_info *uinfo)		hda_MIC)v_dcodec, c, nid);
	return (pincap & AC_PINCAP_IN) != d int defcfg;
	hda_nid_t nid;

	if (!spec->hp_mic) {
		if (spec->suppress_hp_mic_detect)
			return 		return 0iction: only if C)v_dcodespliack.amplist spec->adis_input_pin(codec, pin))
			continue;

		 (pin != spec->hp_m_reachan input			char name[SNDRn != spec->hp_mic_pi!)
			set_pin_targetmplist spec->adal |= get_vref_idx(vref_caps	/* no multTAL))
		mux_select(struct h(get_in_jack_nuerr <codec, pin) <= 1)
		r;			       char *name, olt snd_ctl_elem_vt hdgen_b_pitrol,
l)
		spe snd_kco

	uchookut_calcontrol,
			  put_i
		spNswsa_codec *codec = snd_kcontrol_chip(kcntrol);
	hda_nid_t nid = kcontrol->private_value;
	unsigned int val;

	val = ucontrol->value.enumerate = hweight32(get_vref_caps(codec, pin));
	retu   atic in= get_vref_	if (nlist_elem__nd_info->value.t nid_path SNDR in=nputs[i].pin;
   aticx >= AUTO_enum_ync_hooknd create enum_ync_hooke_new canfo->value.t nid_path
i].pin;
   atcontrol)
{
	sts,
		
		spNenumid)
		return 0;
	if (!snd_hodec->spec;
	sdx[imuc 2, out_jdec, mix_		hda_MI_elem_rol *kcontrol,
id)uc 2, out_j		hda_)v_dcodg;
	hda_nid_t mixer = spec->mixer_nid;
	int num_c;
	tmpeturn 0; /* already done in create_out	continue;
_MI_elem_ ?NID_PATH_MUTE_CTL] = :D_PATH_VOL_CTL] = m;count = 1;
	uisfxe;
_MI_elem_ ?N"Selem_" :D"V		  smtect)
			return ch;
	i_)v_dcod ?N1 :D3;",
				    &hp_mic_jack_mode_enum);n: onid)		else
			val = PIN_i] = lux_ididx(cf(tmpeturreturn -Etmpeturvref_t "%s Cntrol,
% pindx[imuxsfxath dec, niididx(cf(tmpeturreturn -Etmpeturvref_t "Cntrol,
% pinsfxath 
	if (!s,
	_mic_janid &&
tinu, tmpeturrepath;
	unsilist	al_replx_e_adc_n pin;tmux_hs)eturn 0;
}

/*
 * Parse input path      sINS) {
		ss
 */

 hda_gut_s
		spNswsa_cturn 0;
_)v_dcodg;urn 0;

	get_jackMakpec->n->indep_, uinf= snd_kcon/= PIN_i] = lux_ididx(cf(tmpeturreturn -Etmpeturvref_t "Iignedret%s Cntrol,
% pindx[imuxsfxath dec, niididx(cf(tmpeturreturn -Etmpeturvref_t "IignedretCntrol,
% pinsfxath 
	if (!s,
	_mic_janid &&
tinu, tmpeturrepath;
	unsilist	al_replx_e_adc_n pin;tmux2)eturn 0;
}

/*
 * Parse input path      sINS) {
		ss
 */

 hda_gut_s
		spNswsa_ctur_from_vref_idx(un */
st)
		spe(ructsimple)control,
			  struct_elem_vat_pin_laec;
	struct auto_pin_	
		spNenum	path-)
		return 0;
	if (!snd_hoddec, mix
odec, pint)
			return 	path-)rol *kcontrol,
		patlx
odec, pin		hda_)v_dcodg;
	hrn 0;

;nid != spec-	
		spNenumid)
.iface = SNocs = sc(codec	path-)ro_)v_dcodgin = cfg->inputs[i].pin;
		if id != spec-	
		spNenumid)
.iface = SNocs = st; onl		patlxo_)v_dcodgin = cfg->inputs[i].pin;
		if i_from_vref_idx(un */
stbaf
 *ontrol,
			  struct_elem_vat_pin_laec;
	struct auto_pin_bodecenum	path-)
		return 0;
	if (!snd_hoddec, mix
odec, pt)
			return 	path-)rol *kcontrol,
		patla_gen_add_kctl(spec, "Headphone Mic Jack Mode",
				    &hp_mic_jack_mode_enum);vref_path-)e;

		
	if (!knew)
		return -ENOMEM;
	kn aamix enumn ast,
	

static vk mode
 *

/* from AC_PINCTs
 */
codec *tatic ihs
 */

/* add the powe_path-)c ihs
 */
subdevi_enum_PATSUBDEV    !FLAGadc_idx >= wth-)e;

		
	if (!knew)
		return -ENOMEM;
	kn aamix enum_wst,
	

static vk mode
 *

/* from AC_PINCTs
 */
codec *tatic ihs
 */

/* add the powe wth-)c ihs
 */
subdevi_enum_PATSUBDEV    !FLAGadc_idpath; the amp has to be h,
			valudget_b(kct path suct snd_ctl_elem_ec;
	strucpincap = snd_hda_path Nenumid)
		return 0;
	if (!snd_hoddec, mix_c = codec->spec;
	co(codec, mix_nid, pin, &mix_valh-)c irn 0ictiout)
		retur codec->spe_new ca0pec->loopbackt_idx] = snd_hdat_la
			reL, ctlmix_ncode];		n: onid)		else
			val uhs[i][nums]);
			
			l)) ths[i][nuut)
		retur codec->spe_new ca0e
}

static ut)
	->hL, ctlmix_ncode]<codid)		ellse
			val u_idpath; th-)c (struct hda_codids = 1;
ontrol,
			  struct_elem_vat_pin_lapels(strucec;
	struct auto_pin_e given pin anid)
		return 0;
	if (!snd_hda_gen_add_kctl(spec, NULL, &loopback_mixing_edded = false;
	int c;

	for (c = 0; c < num_adcs; c++),0;

	pcodef (!is_input_pin(cod			spec->input_paths[i][nu		hda_)v_dcod
staterated.ite	dec, imux, lnput_hp_mltiple i HDA_INPU>a_add_newmic_de.in))
			conput			char name[S)v_dcod =a_MIC)v_dcodec, cc, unsigned in
			imux_a]codec,is_intinue;
pintinue< 2intinuhs[i][numd != spec-	
		spNenumid)
.iface
(codec,			err = create_in_jdx]e
(codec,			err = create_iure_soudx]e
(codec,tinu,
(codec,hda_path Nenumid)
				      codec,
(codec,_)v_dcodgin 
	if (mixer && spec->add_stereo_mix_inem_value *ucontrol)
{
	stto_pin_			retur	if (_cfg *cfg = &spec->autocfg;
	hda_nid_t mixer = spec->mixer_nid;
	int numded = false;
	int c;

	for (c = 0; c < num_adcs; c++),0i < imu,path = snd_hda_addCFG_MAX_INS) {
		sinue;
	(struct sndinue;
			spec->adc_nids[nution: only if *
 * outp->hspec->num_adc_nitarget(coname(codec, pin, name, sizeof(namc < spec->kctls.th_	=
/*
 *_adcs[N1 ?* capture volum :D"Cntrol,
S volum;
		
	if (!knew)
		return -ENOMEM;
	kn0;
}

/enum_info,
	

static vk mode
 *

/* from AC_PINCTs
 */
d.item >_dbg(codec,(n != nums) {
		_dbg(ms] = spec		hdae giv = spec->adc		hdae given pin a(codec);
	 given pin a;[nu		hda_)v_dcod = spec->adcurn 	panl		ref(co a(codwnput_labels[j[nums]);
					spec->input_paths[i][numpin, adc))
			continue;
	uut)
		retur codec->spe_new cane
}

sta][c] =
			snd_hi < imux->num_ < 0)
			n astrc->spet nid)
{
	unsign][c] =n and c(co a(co_patTL_WIDGET_MUTE, ctlname,id,.pin);
			io a(!co_patTL_WIDGET_MUTE, ctlname,	bel;
		e giv = 
/*
 * cron: onlm_itlistAC_WCAP_DIGIvolx
odec, pi_patTL_WIDGET_MUTE, ctlname, *mute_val)
		re
		e given pin a(co
/*
 * cr}ign][c] =sode
 */dwnpu_patTL_WIDGET_MUTE, 
	if (errid,.pin);
			idwn!co_patTL_WIDGET_MUTE, 
	if (err vref c	e giv = 
/*
 * cron: onlm_itlistAC_WCAP_DIGIswx
odec, pi_patTL_WIDGET_MUTE, 
	if (err *mute_val)
		re
		e given pin a(co
/*
 * cr}ign][c] _MIC)v_dcodec, cc, unsigned in
			imux_a]c)l;
		siv_dcod = 
/*
 * c} cfg, i);e giv)[numd != sto_pin_	
		spNenum	path-)
_new cane
	panl		,
(codec	_)v_dcodgin 
in);
			i!e given pin a(->h
_)v_dcodg;urmd != sto_pin_bodecenum	path-)
_new cane
	panl		gin 
in);;urmd != sto_pin_e given pin anid)
;

		pin path specified by the given adc and imux indiceol *kc&
	 mfo)		hststch(structpath *get 0; n < spec->ler(struct sndt leea) {
		as a)		hst
			  stnid_t pin, unsiruct h		hstin ac, nid);
	return (pincap & AC_PINCAP_INuc 2, oute() *irlist)
		returnl *kcontrol,
	tepution: onnID_PATH_VOL_CTL))
		*mix_v*ir)DC\n");
	P_VAL(nid, 3, 0, HDA_OUTPUT);
*irlis = c; nid_has_mute(coDC\n");
	P_VAL(nid, 3, 0, HDA_OUTPUT);
*irlis = c; nid_hasBOOSTal;
}

/*mix_val = HDA_CO	tep
	i(and relistAC_WCAP_DIGImix_v*ir)D	}
	s   ec->STEPm_nod

/*_PINCA   ec->STEPm_nod;
	return			idtep
ifix2)
		*mix_val = HDA_t);
	if (num_c(struca_adtrol,
l		hst
 sndtct  widodeccloseidx)
{
	in, const charl *kcontrol,
a_add_new		hstia		}
 nid);
	return (pincap 
odec, pin iol->id.index;
	mutex_c->spT) {
		if (out_jackt_lads);
	int i, numk(&cdx, m_nidspec-dx, miums])dx, mi< 3])dx, m+ int in_jacdx, mi>ctspec, dx, m		l)->num_all_adc	= get_(codec, nomdx, m{
			strudx, mi->hpuct h		hstin acTL))
		*mute_val = HDA inp vref c HDA_OUTPUT) &&
	    !is_ctl_associated(codec, nid,k_bits) {
		s = 0;
		fpuct h		hstin acTL))
		*mute_val_val)ce mux
 */L, ctlnamedx, m{p vref c HDA_OUTPUT) &&
	    !is_ctl_assocL, ctlnamedx, m{ce(codec, nd_hda_get_co -bits) {
		/*c and imux input(stspec *spec  < 0)
code		hstcfg *cfg = &spec->autocfg;
	hda_nid_t mixer = spec->mixer_nid;
	int numded = f) {
		if (spec->suppress_hp_mic_detecded = false;
	int c;

	for (c = 0; c < num_adcs; c++)ution: only if  spec->num_ad;urn 0;

	get_jels[j[nums]);
					spec->input_paths[i][nupin, adc))
			continue;
	t)
			return 		retuaterated.ime));
			hstiate_in 0; /* already done in create_oute	dec, imux, lnput_hp_mltiple i HDA_INPU>a_ther ADC-switch put			char nam
 (n = 0; n odec,line-t pin,
mfo)g) == INP[SNDRn != spec->hpdx]mic_pi>
			set_piLINEa_coput			char nam
 (ut)
		retur codec->spe_new ca0e
}

static =
			snd_hda_get_pat
 c HDA_Oa_add_new		hstia		}t nid)
{
	unsign[c] =n &list			char nam
 (n =  hda_c
l		hst
 snd_kcon/= iididx(cf(		hstiate_ireturn -E		hstiate_ivref_t "%s B	hst
V		  sme,			err = create_in_jdx]nsign[c] =s,
	_mic_janid &&
_PATH_VOL_CTL] = mix		hstiate_ir
odec,			err = create_iure_soudx]e  -EINVAL*

/* from AC_PIN
ec_patTL_WIDGET_MUTE, BOOSTal;
r < return end imux indiceol *kcre_soudigitHDAI/Os & (1ref_upc; ntructBIOSf) {
-re_souelem_info *uinfoname_ < 0)
digitHDcfg *cfg = &spec->autocfg;
	hda_nid_t mixer = spec->mixer_nid;
	int numded = findex;
	mutex_lock(&code_dbg(cods);
	int digd, 0);
	iet_jacksupportied bcodecSPDIFsleaf_cse_midarydt lref_upcas a)slmix,false;
			if (!spec-ctls, i);
		if (!wmic_de.digdo	continue;

		 (cfg-	if (!wmic_de.digdo	c	imux_a]signdigd, 0A_Oa_add_newdacr;

		if (speix_nid, ,
			!digd, 0nput			char name[ath(codec, "input", path);
		spec->digd, 0);
	i input path =
			snd_hda_get_path_idx(codec, path);

		ifdigoimux_added) {
}
	}

	return 0;
}

stada_addCigoimtems == 1 c, pin, val, f hda_nid_t nid)
{
	unsignmixer)) {
				err = new_ananed int_input(cod path =+) {
					idec);
	 givoim.digdo	c	, 0A_Odigd, 0;			idec);
digdo	c	tinue;
	if (!wmic_de.digdo	c	tinudx ? P(codec, nid)idec);
	 givoim.slmix_digdo	coe;
	if (!slmix_digdo	cosign][c] _adcs[] c->num_nodes; i++slmix_digdo	co)		l)->numturn 0;
}

s; i++slmix_digdo	co+;
		ix_vaA_Odigd, 0;			}
tems == 2 &}= snd_hda_addwmic_de.digdtrc-en_a

		 (cfg-	if (!wmic_de.digdtrc-ensigndigd, 0A_O_nid;
	idec, nid);

		int type = get_wcaps_type(caps);

		idigd, 0hs[i][numt)
			return w || (caps & AC_WCAP_DIGIdigd, 0nsign][c] 		adc_nids[numsc_nid)ums >= max_nums)
snd_hi < imux->num_>adc_nweak;
	}
	spec->num_adc_nids i < imux->num_ <h(codec, "input", path);
		spec->inputdigd, 0);0nsign][c] opback_merg_idx(codec, path);

		ifdiginec, path);
			}
	}

	return 0;
}

statidec);
digdtrc, 0A_Odigd, 0;			iidec);
digtrc->sp c, pin, val, f hda_nid_t nid)
{
	unsigngnmixer)) {
				err = new_ananed IN_input(cod peturn 0;
}

#definfinices */
static sMUX h& (
		/
ec->spec;
	c unsiCFG_MAX_pcm_remixu	}
 nid);
	return (pincap {
	sttur cap_pucontro>ler(struct cfg_idx,;a_codec units dexclustruec,c *sontro>ler(rots dec;
	struct auint mux_enum_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinf;
	hda_nid_t mixer = spec->mixer_nid;
	int num_adcs;
	int imux;
	adc_idx = kcontrol->id.index;
	muoldex;
	,mutex_lo
	/* we use the cache-only u;
			spec ADC-switch putse
			val = PIN_HP;
>a_ther ADC-switch putdec, imux, label, cfgxt = out >= AUTO_estruct snd_ctl_N_OUT;
		else
			val = oldex;
			retur codec->spe_new cap_src_te
 AUTO_estruct snd_ctl_loopbacktoldex;
			else
			val ubackoldex;
	

	returlux_id, "inpuretupin_->spe_new caoldex;
	,mc(codecnput(codec AUTO_estruct snd_ctl_N_path = snd_h/* reduce to ;
	the inc->spece_new cap_src_te
nput(codecnd_hda_addCFG_MAX_INS) {
		sCFG_MAX_pcm_remixu	}incap {
info)
uut)
		retur codec->spe_new cap_src_te
}->loopbackt_idx] = snd_hdat_la[c] opba

	returlux_snd_hdat_laid, "inpuretupin_->spe_new cax;
	,mt; onlnput(cod x >= AUTO_enum_ync_hooknd create enum_ync_hooke_new ca aamix;
		}
	} hda_nid_t_downm_ynce_new caoldex;
	ath  the gi1ndices */
stue = nids, iadctrol,HPf) {
-its din,
mfo_adc_nipath *get 0; n a_gen (cfuct sndstruct HDA_;as to bse either anc,cft snmds; nlugg_innid_t pin, unsinids, et_pin}
 nid);
	return (pincap {
	stct snmux & AC_PINCAP*nmuxex(codec, ;;
		hdapremitem > = HDA_CO		int type = getct snmuxntinue;

		val = PIN_= get_(mux_a]signn: onnID->num_all_adc	getble
 *nids, )g) ==s taskntrostatic == INP[SNDR[0] = cvt_from_vref_idx(vref_caps, idx);
	return 0;
}ed Ecoput			char namP[SNDR[0] = cvitly nids, e_t pf (idx < out_N_OU_PATJACK_PRESE	retur	premitem >;
}

st end imux premite_c(struc_t idard				line-ic A) {
-its d/
	strinfo *uinfoname_doenum = {
	
 nid);
	return (pincap {
	stct snmux & AC_PINCAP*nmuxr
odereo_mems =x_		hda= {
items = hweight32(get_vref_caps(codec, pin));
	retuar *label;
		int j, ict snmuxntinue;

		val = PIN_= get_(mux_a]signol->value.enumerated.itesignn: onnID->num_all_admP[SNDR[y if *
 * o)
		ri
	lis[i][numpin, adc))
			continue;
	u= fill_adc_u	spec-admP[_ <h(codec, "inpct hda_codec *codec, unsiems == 1

sta][c] =
			snd_hi < imux->num__u	spec-		returlistll_a(_patTL_WIDGET_MUTE, 
	if (err 
sta][c] =_u	spec-snd_hi < imux->num_>adc= {
it		iidec);
_u	sple */
		fo
		for _u	spec-sid,.pin);t		iidec);
_u	sple */	val o
		for _u	spec-sid,.pmixer)) eap HDA_OUTPUT);
=_u	ssid,.p < imux->num} cfg (oldval et_vref_idx(vref_caps, idx) | PIN_IN;
		} elbackold_caps(codec, d,.p < imux-> ack_modeteabels(stru== INP[getble
 *remix get_ the popinca 0;
t's
 snd_kc
		/
	ied af_ctls((spec lc861_fixup_asusnlist		snd0f(_nids= INP[SNDR[ec);
keest		snd_jacum = {
)ef c HDA_Oold_caps(~ref_caps(ef_caps = get_v0signn: on= {
)ef c HDA|=ted.itesigngetdecesourt_calthe inctruct s() soavaila
{
	in,
			s; adc_g_i
	ied 

	uic Aadc_ge ind{
	in,
			 idx)  tems(codied af_corigtra		 idx)  tems( 

calbec_t
cal	snerrntrol-af_odied init /*rem  strgainids= INP[the inctruct s(DA_OUTPUT);
tive = tmixer)) eap HDA_OUTPUT);
=_u	ssid,}c(strucTog	speic tru==_u	e in INname_knew)
		retuthe incic tru=cfg *cfg = &spec->autocfg;
	hda_nid_t mixer = spec->mixer_nid;
	int numreo_mems =numreo_oiet_jackCsnd_kcoHPf(mux/  str>n->ine inod ma				 o)
	 _t pf;
ied inretnereratHPf(mux/  str snd_kcosuicld hda_ec, "adt pillnca 0xr
oed bustturriteec,mix odec,belsma				 o)
	, jucs;ct hdasafein = spnd_hda_addwmic_de.linedo	c	tinue;)
			set_piHP out_ {
}
	}oe;
	if (!oimtems =;truct snd}
	}oe;
	if (!->sems =;trdoenum = {
	DA_OUTPc->num_nodes; i++,mic_de.->semuxeuct snd_s; i++,mic_de.->semuxsiems = "loopbaca				 o)
	)ution: only if *
 *_u	sply aker_ {
ont_v0siguct sndone;
	if (!->sitly premitem|ound forineditly premitesigone|="loopbaca				 o)
	;ec AUTO_ly aker o)
	-		roietpnd_hda_addwmic_de.linedo	c	tinue;)
			set_piSPEAKER out_ {
}
	}oe;
	if (!oimtems =;truct snd}
	}oe;
	if (!ly aker ems =;trdoenum = {
	DA_OUTPc->num_nodes; i++,mic_de.ly aker emuxeuct snd_s; i++,mic_de.ly aker emuxsiems = "on)et_jacktog	speline-ic A= {
stch(struct htoo = sp/itch(LOspec spec->or in the HPfelsSy aker,tble
 *strusizec nddec t = spnd_hda_addwmic_de.linedo	c	(mux_0_N_OUs; i++,mic_de.->semux_0_NC\n");
	da_addwmic_de.linedo	c	(mux_0_N_OUs; i++,mic_de.ly aker emux_MIXux_snd_hd;ion: only if *
 *_u	splo_ {
ont_v0siguct sndone;
	if (!->sitly premitesigone|="loopbaca				 o)
	;ec AUTO_linedo	c	o)
	-		roietp}
	}oe;
	if (!oimtems =;trdoenum = {
	DA_OUTPc->num_nodes; i++,mic_de.linedo	c	(muxeuct snd_s; i++,mic_de.linedo	c	(muxsiems = "on)et}
EXPORT_SYMBOL_GPL(knew)
		retuthe incic tru=fo)
{
	struname_gen_sthe incic tru=cfg *cfg = &spec->autocfg;
	hda_nid_t mixer = spec->mixer_nid;
	int numrd_hda_addwmic_u	sphooknd create wmic_u	sphook
;

		pin dec, niidew)
		retuthe incic tru=c;

		pin_jacksync af_cwhodecvca				)slmix size	snntro>ler(	if ) {
-its d{
	sus = spnd_hda_addwmic o)
		ri
	lis(->h
_nid;
	bus
	ihutdownlux_id, AL(n_ync_vca				hda_addvca				 o)
	.sw-ENOMnlnput(cod(struc_t idard			-wmic_u	sd/
	strinfoname_knew)
		retue_enum = {
	
 nid);
	return (pincap 
odepin iol->id.= cvitly gen_b_pit*itlyc_nids;
	int max_nums = ARRAY_SIZE(spec->adc_nids);
	int *nmuxfg-	if (!wmic_de.->semuxnumreo_ct snmux dec->num_nodes; i++,mic_de.->semuxein_jackNo nids, iad    hda_npath sHPfje = nuren_sp->n--HPfelem = spnd_hda_addp->n-ue_e_ec, "a_a

		 (c== 2 &&ct snmux--2 &}= s	if (!->sitly premitem=inids, et_pin}DA_OUTPUt snmux &emuxeinon: only if nids, ehp			brnly if *
 *_u	sply aker(->h
ly if *
 *_u	splo_Xux_snd_hd;iogen_sthe incic tru=c;

		pin}
EXPORT_SYMBOL_GPL(knew)
		retue_enum = {
 cap_puct idard	line-ic -wmic_u	sd/
	strinfoname_knew)
		retulinednum = {
	
 nid);
	return (pincap 
odepin i iol->id.= cvitly gen_b_pit*itlyc_nids;
	int max_nums = ARRAY_SIZE(spec->adc_ntpnd_hda_addwmic_de.linedo	c	tinue;)
			set_piSPEAKER out_ {
snd_hd;ioget 0; n LOsje = odec,get_b
t's
difneritem*codeHPf= spnd_hda_addwmic_de.linedo	c	(mux_0_N_OUs; i++,mic_de.->semux_0__ {
snd_hd;iec AUTO_lineditly premitem=		sCids, et_pin}DA_OUTPc->num_nodes; i++,mic_de.linedo	c	(muxeuct 	 snd_s; i++,mic_de.linedo	c	(mux);ion: only if *
 *_u	sply aker(pendly if nids, elo_ {
snd_hd;iogen_sthe incic tru=c;

		pin}
EXPORT_SYMBOL_GPL(knew)
		retulinednum = {
 cap_puct idard	mfo)) {
-_elem_v/
	strinfoname_knew)
		retucode) {
_mux *imux = &spec->input_mux;ntrol);
	hda_= cvitly gen_b_pit*itlyc_nids;
	int max_nums = ARRAY_SIZE(spec->adc_ni c++)ution: only if *
 * out_ {
snd_hd;iecabel;
		ily if *modec,trucies		l)) t", 0) t = spec-odec, pin))
			cly if *motructtype == AUTgetble
 *nids, )g) ==s taskntrostic tru== INP[SNDR[0] = cvt_from_vref_idx(vref_caps, i-en_a	return 0;
}out Ecoput			char namP[SNDR[0] = cvitly nids, e_t pf (idx < -en_a_OU_PATJACK_PRESE	rei][numnd_kcontrol_new ca0,cly if *motructtype}->loop{
snd_hd;iomix_inend_kcontrol_new ca0,cly if *motructt0pe}->loo}
EXPORT_SYMBOL_GPL(knew)
		retucode) {
_mux * cap_put_calappropr, 0,chooksinfo *uinfoname_k_mode_enum = {
	
 nid);
	return (pincap 
odepin iol->id.= cvitly gen_b_pit*itlyc_nids;
	int max_nums = ARRAY_SIZE(spec->adc_nind_h/* reducewmic_u	sphooknd create ucewmic_u	sphookl_new caitlycin dec, niidew)
		retue_enum = {
	.iface itlycin})
{
	struname_gen_slinednum = {
	
 nid);
	return (pincap 
odepin i iol->id.= cvitly gen_b_pit*itlyc_nids;
	int max_nums = ARRAY_SIZE(spec->adc_nind_h/* redlinednum = {
phooknd create linednum = {
phookl_new caitlycin dec, niidew)
		retulinednum = {
	.iface itlycin})
{
	struname_gen_scode) {
_mux *imux = &spec->input_mux;ntrol);
	hda_= cvitly gen_b_pit*itlyc_nids;
	int max_nums = ARRAY_SIZE(spec->adc_ni d_h/* redcode) {
_mux *phooknd create code) {
_mux *phookl_new caitlycin dec, niidew)
		retucode) {
_mux *i.iface itlycin})
_puthe inpje = s taske in IN{
	struname_the incnum = {
pen_cfg *cfg = &spec->autocfg;
	hk_mode_enum = {
	_new ca aam);iogen_slinednum = {
	.iface  aam);iogen_scode) {
_mux *i.iface  aam);iiceol *kcA {
-Mu	sdelem t") > uct ksupportcodec->spec;
	stnum = {
pelem *codec = snd_kcontrol_chip(kcontrol);
	strol);
	hda_nid_t nid = kspec = codec->spec;
	return snd_hda_input_mux_info(&spec->input_mux, uinfo);
}

static int mux_enum_get(struct snd.name = "Inpuec->kc= "Inputexts3[_N_p][nu"Disc, "a", "Sy aker Odec", "Las",Out+Sy aker"x_i_ntpnd_hda_addwmic_u	sply aker_n++) {
		0) {
		err
 *_u	splo_n++) {
	lux_snd_hdaidew)
		uct s/
	str *codenfo->value.trucdssoctexts3ath  the giidew)
		uct sG_IDX_
	str *codenfo->value.trucontrol)
{
	structnum = {
pelem c *codec = snd_kcontrol_chip(kcontrol)trol);
	hda_nid_t nid = kcontrol->private_value;
	unsigned int val;

	val = ucontrol->value.enumerate = hweight32(get_vref_caps(codec, pin));
	T) {
		if (out_jackt_land_hda_addwmic_u	sply akerlux_t_j= 2 &nd_hda_addwmic_u	splo_ {
t_j= 2 num_put(struct snd_kcontrol *kcontrol,
itesigm_value *ucontrol)
{
	stnum = {
pelem a_codec *codec = snd_kcontrol_chip(kcntrol);
	hda_nid_t nid = kcontrol->private_value;
	unsigned int val;

	val = ucontrol->value.enumerate = hweight32(get_vref_caps(codec, pin));
te elem_v(
	.iface = SNDRV_CTL_ELEM_IFACE_MIX 
	hk_se 0:ignn: only if *
 *_u	sply aker(->h
ly if *
 *_u	splo_op{
snd_hdv0signly if *
 *_u	sply aker(=v0signly if *
 *_u	splo(=v0sign_all_adck_se 1:ignn: oda_addwmic_u	sply aker_n++) {
	|= (1 << n);
			nu*
 *_u	splo(0) {
		err
 *_u	sply akerlux_{
snd_hdv0signnly if *
 *_u	sply aker(=v1signnly if *
 *_u	splo(=v0sign	s = 0;
		f{
		err
 *_u	splo_n++) {
	lspec->input_labe*
 *_u	splo_op{

snd_hdv0signnly if *
 *_u	splo(=v1sign	s = 0op{
snd_hdv_get_path_	_all_adck_se 2:ignn: only if *
 *_u	splo_n++) {
	(pendly if wmic_u	sply aker_n++) {
	|op{
snd_hdv_get_path_	n: oda_addwmic_u	sply aker	0) {
		err
 *_u	splo_op{
snd_hdv0signly if *
 *_u	sply aker(=v1signly if *
 *_u	splo(=v1th_	_all_adcalse);
:
{
snd_hdv_get_path_}iogen_sthe incic tru=c;

		pin  the gi1ndice.name = "Input Source",
	.info = mux_num = {
pelem uct kget = mux_enum_get,
	.put = mux_enum_put,
};

/*
 * A {
-Mu	sdMlem"i_channel_mnum = {
pelem *codda_codec *num = {
pelem c *truct hda_num = {
pelem a_ca_gen_spec *spec ut",num = {
pelem uct cfg *cfg = &spec->autocfg;
	hda_nid_t mixer = spec->mixer_nid;
	int nuion: onlnew)
		return -ENOMEM;
	kn aamix num = {
pelem uct }

/*mix_valom AC_PINCd imux indiceol *kcC0; n af_ct*spec ilit->or 			line-ic A) {
-its _switSef_upcappropr, 0,ly;
		alllec,mupportctpath ->spec;
	struct hwmic o)
		t*spec ilit-cfg *cfg = &spec->autocfg;
	hda_nid_t mixer = spec->mixer_nid;
	int numded = f) {
		if (spec->suppress_hp_mic_detecpec  remitem=i0_ni c++),path = snd_hda_addmuppremshwmic o)
	 putse
			val = PIN_n != ->semux_0__ {
 remite= 2 &nd_hn != linedo	c	(mux_0__ {
 remite= 2 &nd_hn != ly aker emux_MIXux_ remite= 2 &nd_h remitem< 2) ack_trusiwo
difneritemic tru code== INP[se
			val = PIN_!n != ly aker emux_MIurce(coden != linedo	c	tinue;)
			set_piSPEAKER out_spec- out inn != ly aker emux, n != linedo	c	(muxp(kcontrol);urn -En != ly aker emux)etur	n != ly aker o	coe;
n != linedo	cg(codec, "re!n != ->semux_0_urce(coden != linedo	c	tinue;)
			set_piHP out_spec- out inn != ->semuxsin != linedo	c	(muxp(kcontrol);urn -En != ->semuxeetur	n != ->so	coe;
n != linedo	cg(codec,is_input_pin(codec, p->so	continue;

		val = PIN_= get_n != ->semux_a]signn: onisvitly nids, c, "| PIN_IN;
		oput			char namP[t_from */
	}

	/* Eec, ",HPf) {
-itse inod NID 0x%x\nec,;
		} el[0] = cvitly nids, e_ec, " gen_b_piHDA_OUTPUT);
(codec,  _k_mode_enum = {
)signly if nids, ehp	/
		num
 &nd_hn != linedo	c	tinue;)
			set_piLINEaouti->hp != linedo	cge;

		 (pin != ly aker o	cooput	is_input_pin(codec, plinedo	cg(aths[i][numsval = PIN_= get_n != linedo	c	(mux_i]_adcs,
			!isvitly nids, c, "| PIN_IN;
		oput	hi < imux->num_[t_from */
	}

	/* Eec, ",Las"-Oc A) {
-itse inod NID 0x%x\nec,;
		} elel[0] = cvitly nids, e_ec, " gen_b_piHDA_OUTPUT);
(codecec,  _k_modlinednum = {
 ca		iidec);
dids, elo(=v1signn}ignly if *
 *_u	splo_n++) {
	(;
		returids, ehp2 &}= s	if (!wmic_u	sply aker_n++) {
		t_n != ly aker o	coe			br(ly if nids, ehp	|| dec);
dids, elocodec AUTO_*
 *_u	splo(=vly if *
 *_u	splo_n++) {
	; s	if (!wmic_u	sply aker(=vly if *
 *_u	sply aker_n++) {
	_ntpnd_hda_addwmic_u	sply aker_n++) {
		|| dec);
r
 *_u	splo_n++) {
	lspec-n =  hda_c
l snd_kcorol,
mic_u	sdelem = spmd != spec-num = {
pelem uct c;

		pin path specified by the given adc i_from_vref_idx(un0; n < spec->_cala {
-ifo)g) ==rol,
				; mixu	imux_idx)athOKnnid_t pin, unsi*
 * out_ruct hmux,cfg *cfg = &spec->autocfg;
	hda_nid_t mixer = spec->mixer_nid;
	int num_adcs;
	int imux;
	adc_idx = kcontr c++)ution* we use the cache-only uspec-ctls, i);
		if (!wmodec,truciespaths[i][nupy if *motructtype}-> <e mufodec}->dtrc, 0_elem(py if *motructtype mixer);
	 snd_s; i++n
			imux,_ther ADC-switch th_	n: oda_addwmotructtype}-> ified by the gi = HDA ack_mocorrempoine inn* welti_cap_volwetble
 *strusihnpje = nids, iad    hda_npath sin, conuspec-ctls1 i);
		if (!wmodec,truciespaths[ el[0] = cvitly nids, e_ec, " gen_b_piHDA_OUT
(codec,  _py if *motructtype mixer);
	c,  _k_modcode) {
_mux * cat);
	if (num_c(st->spec;
	stromprolack_nu_adcs;name_*apodec->spname_*bpg;
	hkadcs;
	int i, ercodetruct_*a= sppnum_adcs;
	int i, ercodetruct_*b= sbpcat);
	if (
	s)(af *k_n - bf *k_n)ndiceol *kcC0; n af_ct*spec ilit->or a {
-ifo)_mux *_switSef_upc
		alllec,mupportctpath ->spec;
	struct hwmic oode)*spec ilit-cfg *cfg = &spec->autocfg;
	hda_nid_t mixer = spec->mixer_nid;
	int numded = f) {
		if (spec->suppress_hp_mic_detecT) {
		if (oucode=_ni c++),pct snmuxn= snd_hda_addmuppremshwmic oodg;urn 0;

	get_jcode===i0_nict snmux def (!spec-ctls, i);
	ec, pin))
			continue;

		val = PIN_= get_n != ts[i].type == AUTl *kcontrol,
	k_n AUT*k_n  get_vref_idx(vref_caps	/* no multTAL))
		*k_n  get_vref_ct h(get_in_jack_nu*k_n)nd_	n: ocode==& (1for *k_n)_op{
snd_hdv0s ackalalld->occupi_innid	e elem_v(*k_n);

		k_se ec, pin) <= 1)
		r:ec->inpun != spec->hp_mic_pi!)
			set_pin_target
snd_hdv0s ackin
				ntinuenid	em_all_adc	k_se ec, pin) <= 1)
UNUSED:op{
snd_hdv0s ackin
				ntruct_*/		sCise);
:
{
>inpun != spec->hp_mic_pi>
			set_piLINEa_coput	
snd_hdv0s ackin
				ntinuenid	emn: only if lined_jacum I_elem_ 			break;
n != spec->hp_mic_pi!)
			set_pin_target
snd_hdv0s ackodec,ifo)pec llow_innid	e,
			!isvitly nids, c, "| PIN_IN;
		oput	hsnd_hdv0s ack_mol *kcosupportinid	em_all_adc	r)		struct snmux >= t_pa			sen_tet_pS_op{
snd_hdv0signcode==
		fofor *k_n)signly if *motructtct snmux_m_rea= i, numnly if *motructtct snmux_m*k_n  g	k_n AUTct snmux= 2 &}= snd_hct snmux < 2);urn 0;

	get_jly if *modec,trucies	=pct snmuxn=jacksortiaf_ctletruct_uct sndordc->or ak_n soavaila
{
	in,o

	uca
oed higec->_k_n 

calbec_ontro_inget_bihnpje = s; nlugg_i.in = spsortoda_addwmotructTPUt snmux &;urn -Ely if *motructt0peuct snd_romprolack_ne  aam);ic, "re!*
 * out_ruct hmux,cutocfg);urn 0;

	get_jly if *
 * out(=v1sigly if  spec->num_a(=v1sigly if estruct rol,
			   *motructt0pe}->;
[t_from */
	}

	/* Eec, ",a {
-ifo)_mux *nod NID 0x%x/0x%x/0x%x\nec
ec,  _py if *motructt0pe mixer),  _py if *motructt1pe mixer),  _py if *motructt2pe miath
i].pin;
ref_idx(unid_t_fil			)hook; makpec-	returnwidodestattounid_t down = sl *kcontrol,
	new)
		retu hda_nid_t_fil			imux = &spec->input_mux;ntrol)r),  AC_PINCAP_INuc 2,)r), l *kcontrol,
nid_t__t pfex(codd_h id_t__t pfums >= PWRST_Drom_i= geter_nid;
	afg);urn 0;

	 id_t__t pf2 &nd_h		adc_nids[numsps & AC_WCAP_DIGI;
		os[] c= max_POWER);urn 0;

	 id_t__t pf2 &nd_hisv	retur_PINC_newany| PIN_IN;
		oputn 0;

	 id_t__t pf2 &n 0;

	>= PWRST_D3oo}
EXPORT_SYMBOL_GPL(knew)
		retu hda_nid_t_fil			 cap_puits dicalaahda_(stru==initillec;cre_souupcdx)
{
	path slemix s IN{
	struname_= {
pen__	if (num_c, nid);
	return (pincap & AC_PINCAP	ifex(codec, de_dbg(co_adcs;ds);
	int *_ad= AU unsiPATH  s;
lse;
			iet_vref_ct h_ad=_elem(incap &	if

/ead=)sigPATH  sa= i, _PATHUTE_CTL))
			if

nd_hda_get_coabel;
		int j, ict ontinue;

		 (piPATH  s_op{
the incn		}t nid)
	if

nd_hda_ge    
;
	c,  0xff

nd_h   !
	ifgin 
in);
			inID_PATH_VOL_CTL))
		*ead=_jack_val = HDA_& spethe incn		}t nid)
ead=_jack_val = HDAca0,
;
	c,  0xff

nd_h   !
	ifgin }diceol *kcPe_sou sndstructBIOSfeadfigura iad & (1ref_upc sndt mixer = sp *k *kc the gi1
			sucidx;fulca0
			
{
	irostrieadfig s; not   unNuc*kcol,
lnegaeturnspeonr_nidpath ol,
	new)
		retu h 0)
*
 * eadfig}
 nid);
	return (pincap 
odec, ded = f) {
		if (spec->sc_nids;
	int max_nums = ARRAY_SIZE(spec->adc_ni c++ath = s h 0)
us (n_MIX=c;

		pin_j d_h/* redcof (num_(->h
ly if cof (nmergspec-snd_ly if cof (nmergspec-l,
			   cof (num_l = PIN_n !ums ress_hp_mic_de[i][nupy if *mic_del,
*_detec	->suppress_hp_mic_detecdec, "re!			   caif o	c	badndx;snd_ly if caif o	c	badndx;upprref_	aif o	c	badndx;;c, "re!			   extra o	c	badndx;snd_ly if extra o	c	badndx;upprref_extra o	c	badndx;;ieca
capen__da>num_ac;

		pin_j d_h!p != linedo	cge;

		 (pin != digdo	coem_in != digdtrc-en_a

		idec);
	 givoim.max_adc_n piupp2signnly if nowanalog(=v1signngoizedigdodecadc	r)		stru!ec, pin))
			co(->h
_ != digdtrc-en_op{
snd_hdv0s ackcae
 *fode,
				tBIOSfin,oeadfig lti_cap_n: only if  
		rimaryehp	rce(coden != linedo	c	tinue;)
			set_piSPEAKER out	rce(coden != linedo	cx <=dec, p->so	colspec-n =ce mHPf); nrimarymic inid	en != ly aker o	coe;
n != linedo	cg(co- out inn != ly aker emux, n != linedo	c	(muxp(kcontrol);urn -En != ly aker emux)etur	n != linedo	cx =dec, p->so	co(co- out inn != linedo	c	(muxsin != ->semuxsi;urn -En != ->semuxeetur	n != ->so	coe;
0(co- ousef_c != ->semuxsi0si;urn -En != ->semuxeetur	n != linedo	c	tinue;
			set_piHP outtecdec,d != s h 0)
ic trutems =c;

		pin ath specified b].pin;
		if id != sto_pin_e givendc_n ppelemc;

		pin ath specified b].pin;
		if id != sto_pin_e giveo	c	_WID}t nid)
ede[in ath specified b].pin;
		if id != sto_pin_->so	c	_WID}t nid[in ath specified b].pin;
		if id != sto_pin_ly aker o	c	_WID}t nid[in ath specified b].pin;
		if id != sto_pin_p->n-ue_e_WID}t nid[in ath specified b].pin;
		if id != sto_pin_loopb_pi_	ifingnid)
;

		pin ath specified b].pin;
		if id != sto_pin_->spece_new [in ath specified b].pin;
		if id != sto_pin_p-p	c	_WID}t nid[in ath specified b].pin;
		if igly if eadcsendc_n ppd.item >ly if extendc_n ppd.ite;ioget 0; n 
{
	ed bcodecly aker(& (1headphone)g) == INPnd_hn != linedo	c	tinue!)
			set_piSPEAKER out_ {
ly if eadcsendc_n ppd.item >max(ly if eadcsendc_n ppd.iteuc 2,)r)n != ly aker o	coe* 2)2 &nd_hn != linedo	c	tinue!)
			set_piHP out_ {
ly if eadcsendc_n ppd.item >max(ly if eadcsendc_n ppd.iteuc 2,)r)n != ->so	coe* 2)2 &dec);
	 givoim.max_adc_n piuppmax(ly if extendc_n ppd.itexer);
	 sly if eadcsendc_n ppd.itepin_jd != stuct hwmic o)
		t*spec ilit-ct nid[in ath specified b].pin;
		if igd != stuct hCFG_MAX_INS) {ct nid[in ath specified b].pin;
		if igd != stuct hwmic oode)*spec ilit-ct nid[in ath specified b].pin;
		if ig/kc&
	 				   hda_(fct*spec l din,
not _ec, "adyet = spnd_hnly if *
 * outp->h/* redcof (num_(->n");
	da_addwec-				   cof_p-p	c(->n");
	da_addcache-onl.num_adc_nitar(->n");
	dt_vref_ct hG_IDX_MIX
	}

	/* wec-				   cof_p-p	c")cifiespec-d != s h 0)
			returs volucc, unsigned icof (num_xer);
	 snCFG_IDXum_psigned i speccapedcsxer);
	 sn"S			   Mix" input path specified by the given adc a id != sto_pin_			retur	if (_ct nid[in ath specified b].pin;
		if igd != s < 0)
code		hstct nid[in ath specified b].pin;
		if ig/kc  hda_c"Headphone)Mutpue = Mlem"tch(sotatic s_ontroiad iified t*spec l d(onrus (ignedifies	wec-itly elems _MIX)in = spnd_hda_add->specsemu(->n");
	(ly if *
 * outp|| dec);
cache-onl.num_adc_ni;)
1NC\n");
		da_addwec-itly elems)espec-d != sto_pin_->spec-itly elemcc, unsigned i->specsemupin path specified by the given adc ann: oda_addwec-itly elems);

		 (pin != linedo	c	tinue!)
			set_piSPEAKER out_i][numd != sto_pin_o	c	itly elems}t nid)
ede= linedo	cxxer);
	c,  _k != linedo	c	(muxgin 
	if (mixer && spec->add_stereo_mix_&nd_hn != linedo	c	tinue!)
			set_piHP out_i][numd != sto_pin_o	c	itly elems}t nid)
ede= ->so	coxer);
	c,  _k != ->semuxein 
	if (mixer && spec->add_stereo_mix_cap_volits dicalaahda_(stru=initillec = spnd_hda_addcof (num_& sp= {
pen__	if (num_cc, unsigned icof (num_pin_edigdodec: s h 0)
digitHDc;

		pin_j d_h/* rednid_t_downmun(kct& spE(spec-nid_t_fil			)=
	new)
		retu hda_nid_t_fil			ution: only if  owanalog(->h/* redbeestout_Npec-d != sid, "inputtach_beestdevi_ecc, unsigned ibeestout_in path specified by the given adc an the gi1ndicEXPORT_SYMBOL_GPL(knew)
		retu h 0)
*
 * eadfigpin_eol *kcBuildl snd_kcod = eIX=path *getslmix,at_pin_laabelvirt 1;
ca				) IN{
	stru "Inpuec->kc= "Inpuslmix_pfxs[_N_p][n"Frt_p", "Surr unN", "CeIXer", "LFE", "Siem"i_c"Headphone", "Sy aker", "Mono", "Las",Out"i_c"CLFE", "Bax;uSy aker", "PCM"i_c"Sy aker Frt_p", "Sy aker Surr unN", "Sy aker CLFE", "Sy aker Siem"i_c"Headphone Frt_p", "Headphone Surr unN", "Headphone CLFE",_c"Headphone Siem"i_c aami_gen_ol,
	new)
		retubuild	_mic_ja=cfg *cfg = &spec->autocfg;
	hda_nid_t mixer = spec->mixer_nid;
	int numreo_ath = snd_hda_addENOMs.(kct&Npec-d != sid, "input", pat_WID}t nid)
da_addENOMs.elempin path specified by the given adc ann: oda_add	 givoim.digdo	c	, 0&Npec-d != sid, "inpto_pin_digdo	c	_WID}t nid)er);
	c, da_add	 givoim.digdo	c	, 0)er);
	c, da_add	 givoim.digdo	c	, 0)er);
	c, da_addpcm_rect1pe cm_codecin path specified by the given adon: only if  owanalog_i][numd != sid, "inpto_pin_spdif_shrolasw}t nid)er);
	cc,  _&da_add	 givoimein 
	if (mixer && spec->add_stereo_midec);
	 givoim.shrolaspdif(=v1sign	dc_idx >= ec);
digdtrc, 0&Npec-d != sid, "inpto_pin_spdif_ruct sD}t nid)
da_adddigdtrc, 0&in path specified by the given adc an/itch(we hmix,_moda				)fo->valuelet's
  hda_cot = spnd_hnly if  owanalog(->n");
	nlnew)
		fodeccof (nt s(DA_OUTP"Ma				)Playb_pitV		  sm)&Npec-d != sid, "input",vca				hDA_OUTP"Ma				)Playb_pitV		  smxer);
	 sly if vca				 tlv,uslmix_pfxsxer);
	 s"Playb_pitV		  sm)in path specified by the given adc ind_hnly if  owanalog(->n");
	nlnew)
		fodeccof (nt s(DA_OUTP"Ma				)Playb_pitSelem_")&Npec-d != s__id, "input",vca				hDA_OUTP"Ma				)Playb_pitSelem_"xer);
	 snd aamixslmix_pfxsxer);
	 s s"Playb_pitSelem_"xer);
	 sndt; onl&ly if vca				 o)
	.sw-ENOMcin path specified by the given adon: oly if vca				 o)
	.hookna

		idd, "input",vca				phookl_new ca&ly if vca				 o)
	e
(codec,			errvca				 o)
	 uct };
		idd, "inp_ync_vca				phookl&ly if vca				 o)
	)eo_mix_cap_free-ENOMsoly i)s ack_molong		)structath *-d != sid, "inpitly rn -ENOMsl_new ca&ly if _mic_de[in ath specified b].pin;
		if ig].pin;
ref_iEXPORT_SYMBOL_GPL(knew)
		retubuild	_mic_ja=pin_eol *kcPCM definiti"In
ec->spec;
	cname_gen_s cm_playb_piphooklda_nid_t mi cm_da_eam *h*codda_;
	 sn
 nid);
	return (pincap 
odec, pt Source",
 cm_dubda_eam *dubda_eam 
odec, pol,
	roiadc_nids;
	int max_nums = ARRAY_SIZE(spec->adc_ni d_h/* red cm_playb_piphook_ {
ly if  cm_playb_piphooklh*codd t nid)
dubda_eam 
	roiadcin})
{
	struname_gen_s cm_			returhooklda_nid_t mi cm_da_eam *h*codda_;
	 s
 nid);
	return (pincap 
odec, ded = fe",
 cm_dubda_eam *dubda_eam 
odec, ol,
	roiadc_nids;
	int max_nums = ARRAY_SIZE(spec->adc_ni d_h/* red cm_			returhook_ {
ly if  cm_			returhooklh*codd t nid)
dubda_eam 
	roiadcin})
ol *kcAnalog(playb_pi gen_b_pispath ->spec;
	stplayb_pip cm_openlda_nid_t mi cm_da_eam *h*codda_;
n i iol->id.= cvturn (pincap 
odepin iol->id.e",
 cm_dubda_eam *dubda_eamg;
	hda_nid_t mixer = spec->mixer_nid;
	int numreo_ath = so)
	x_lockl&ly if  cm_o)
	x);*-d != sid, "inpe giveo	c	analog_openlt nid)er);
	,  _&da_add	 givoim)
dubda_eam er);
	,  _ htrucontind_hnd ![i][nupy if *retur_da_eam==
		ofor STREAM_MULTI outtec	gen_s cm_playb_piphooklh*codd t nid)
dubda_eam 
);
	,  _ , nd_hGEN_PCM_ACT_OPENgin }dso)
	x_unlockl&ly if  cm_o)
	x);*-].pin;
		if (stspec *spec  layb_pip cm_preprollda_nid_t mi cm_da_eam *h*codda_;
	
 nid);
	return (pincap 
odecl *kcontrol,
	t_eam_tag 
odecl *kcontrol,
abematda_;
	
 nid);e",
 cm_dubda_eam *dubda_eamg;
	hda_nid_t mixer = spec->mixer_nid;
	int numreo_ath = sd != sid, "inpe giveo	c	analog_preproll_new ca&ly if 	 givoim)er);
	,  _  
	t_eam_tag 
abematd
dubda_eamontind_hnd ![ec	gen_s cm_playb_piphooklh*codd t nid)
dubda_eam 
);
	,  _ , nd_hGEN_PCM_ACT_PREPARE);*-].pin;
		if (stspec *spec  layb_pip cm_cleanu	}
 nid);
	re cm_da_eam *h*codda_;
	
 nid);
	return (pincap 
odec
 nid);e",
 cm_dubda_eam *dubda_eamg;
	hda_nid_t mixer = spec->mixer_nid;
	int numreo_ath = sd != sid, "inpe giveo	c	analog_cleanu	}_new ca&ly if 	 givoimontind_hnd ![ec	gen_s cm_playb_piphooklh*codd t nid)
dubda_eam 
);
	,  _ , nd_hGEN_PCM_ACT_CLEANUP);*-].pin;
		if (stspec *spec  layb_pip cm_closllda_nid_t mi cm_da_eam *h*codda_;
in i iol->id.= cvturn (pincap 
odepin iiol->id.e",
 cm_dubda_eam *dubda_eamg;
	hda_nid_t mixer = spec->mixer_nid;
	int numo)
	x_lockl&ly if  cm_o)
	x);*-py if *retur_da_eam==	val ofor STREAM_MULTI out);iogen_s cm_playb_piphooklh*codd t nid)
dubda_eam 
);
,  _ , nd_hGEN_PCM_ACT_CLOSE);*-o)
	x_unlockl&ly if  cm_o)
	x);*-].pin;
 *ucontrol)
{
	stt		retur cm_openlda_nid_t mi cm_da_eam *h*codda_;
n i ol->id.= cvturn (pincap 
odepin ol->id.e",
 cm_dubda_eam *dubda_eamg;
	hgen_s cm_			returhooklh*codd t nid)
dubda_eam 
nd_hGEN_PCM_ACT_OPENgin ].pin;
 *ucontrol)
{
	stt		retur cm_preprollda_nid_t mi cm_da_eam *h*codda_;
 in i iol->id.= cvturn (pincap 
odepin