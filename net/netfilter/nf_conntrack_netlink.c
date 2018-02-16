/* Connection tracking via netlink socket. Allows for user space
 * protocol helpers and general trouble making from userspace.
 *
 * (C) 2001 by Jay Schulist <jschlst@samba.org>
 * (C) 2002-2006 by Harald Welte <laforge@gnumonks.org>
 * (C) 2003 by Patrick Mchardy <kaber@trash.net>
 * (C) 2005-2012 by Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * Initial connection tracking via netlink development funded and
 * generally made possible by Network Robots, Inc. (www.networkrobots.com)
 *
 * Further development of this code funded by Astaro AG (http://www.astaro.com)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/rculist.h>
#include <linux/rculist_nulls.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/security.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/netlink.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#include <linux/netfilter.h>
#include <net/netlink.h>
#include <net/sock.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#include <net/netfilter/nf_conntrack_acct.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_conntrack_timestamp.h>
#include <net/netfilter/nf_conntrack_labels.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/nf_conntrack_synproxy.h>
#ifdef CONFIG_NF_NAT_NEEDED
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_l4proto.h>
#include <net/netfilter/nf_nat_helper.h>
#endif

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

MODULE_LICENSE("GPL");

static char __initdata version[] = "0.93";

static inline int
ctnetlink_dump_tuples_proto(struct sk_buff *skb,
			    const struct nf_conntrack_tuple *tuple,
			    struct nf_conntrack_l4proto *l4proto)
{
	int ret = 0;
	struct nlattr *nest_parms;

	nest_parms = nla_nest_start(skb, CTA_TUPLE_PROTO | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (nla_put_u8(skb, CTA_PROTO_NUM, tuple->dst.protonum))
		goto nla_put_failure;

	if (likely(l4proto->tuple_to_nlattr))
		ret = l4proto->tuple_to_nlattr(skb, tuple);

	nla_nest_end(skb, nest_parms);

	return ret;

nla_put_failure:
	return -1;
}

static inline int
ctnetlink_dump_tuples_ip(struct sk_buff *skb,
			 const struct nf_conntrack_tuple *tuple,
			 struct nf_conntrack_l3proto *l3proto)
{
	int ret = 0;
	struct nlattr *nest_parms;

	nest_parms = nla_nest_start(skb, CTA_TUPLE_IP | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;

	if (likely(l3proto->tuple_to_nlattr))
		ret = l3proto->tuple_to_nlattr(skb, tuple);

	nla_nest_end(skb, nest_parms);

	return ret;

nla_put_failure:
	return -1;
}

static int
ctnetlink_dump_tuples(struct sk_buff *skb,
		      const struct nf_conntrack_tuple *tuple)
{
	int ret;
	struct nf_conntrack_l3proto *l3proto;
	struct nf_conntrack_l4proto *l4proto;

	rcu_read_lock();
	l3proto = __nf_ct_l3proto_find(tuple->src.l3num);
	ret = ctnetlink_dump_tuples_ip(skb, tuple, l3proto);

	if (ret >= 0) {
		l4proto = __nf_ct_l4proto_find(tuple->src.l3num,
					       tuple->dst.protonum);
		ret = ctnetlink_dump_tuples_proto(skb, tuple, l4proto);
	}
	rcu_read_unlock();
	return ret;
}

static inline int
ctnetlink_dump_status(struct sk_buff *skb, const struct nf_conn *ct)
{
	if (nla_put_be32(skb, CTA_STATUS, htonl(ct->status)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static inline int
ctnetlink_dump_timeout(struct sk_buff *skb, const struct nf_conn *ct)
{
	long timeout = ((long)ct->timeout.expires - (long)jiffies) / HZ;

	if (timeout < 0)
		timeout = 0;

	if (nla_put_be32(skb, CTA_TIMEOUT, htonl(timeout)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static inline int
ctnetlink_dump_protoinfo(struct sk_buff *skb, struct nf_conn *ct)
{
	struct nf_conntrack_l4proto *l4proto;
	struct nlattr *nest_proto;
	int ret;

	l4proto = __nf_ct_l4proto_find(nf_ct_l3num(ct), nf_ct_protonum(ct));
	if (!l4proto->to_nlattr)
		return 0;

	nest_proto = nla_nest_start(skb, CTA_PROTOINFO | NLA_F_NESTED);
	if (!nest_proto)
		goto nla_put_failure;

	ret = l4proto->to_nlattr(skb, nest_proto, ct);

	nla_nest_end(skb, nest_proto);

	return ret;

nla_put_failure:
	return -1;
}

static inline int
ctnetlink_dump_helpinfo(struct sk_buff *skb, const struct nf_conn *ct)
{
	struct nlattr *nest_helper;
	const struct nf_conn_help *help = nfct_help(ct);
	struct nf_conntrack_helper *helper;

	if (!help)
		return 0;

	helper = rcu_dereference(help->helper);
	if (!helper)
		goto out;

	nest_helper = nla_nest_start(skb, CTA_HELP | NLA_F_NESTED);
	if (!nest_helper)
		goto nla_put_failure;
	if (nla_put_string(skb, CTA_HELP_NAME, helper->name))
		goto nla_put_failure;

	if (helper->to_nlattr)
		helper->to_nlattr(skb, ct);

	nla_nest_end(skb, nest_helper);
out:
	return 0;

nla_put_failure:
	return -1;
}

static int
dump_counters(struct sk_buff *skb, struct nf_conn_acct *acct,
	      enum ip_conntrack_dir dir, int type)
{
	enum ctattr_type attr = dir ? CTA_COUNTERS_REPLY: CTA_COUNTERS_ORIG;
	struct nf_conn_counter *counter = acct->counter;
	struct nlattr *nest_count;
	u64 pkts, bytes;

	if (type == IPCTNL_MSG_CT_GET_CTRZERO) {
		pkts = atomic64_xchg(&counter[dir].packets, 0);
		bytes = atomic64_xchg(&counter[dir].bytes, 0);
	} else {
		pkts = atomic64_read(&counter[dir].packets);
		bytes = atomic64_read(&counter[dir].bytes);
	}

	nest_count = nla_nest_start(skb, attr | NLA_F_NESTED);
	if (!nest_count)
		goto nla_put_failure;

	if (nla_put_be64(skb, CTA_COUNTERS_PACKETS, cpu_to_be64(pkts)) ||
	    nla_put_be64(skb, CTA_COUNTERS_BYTES, cpu_to_be64(bytes)))
		goto nla_put_failure;

	nla_nest_end(skb, nest_count);

	return 0;

nla_put_failure:
	return -1;
}

static int
ctnetlink_dump_acct(struct sk_buff *skb, const struct nf_conn *ct, int type)
{
	struct nf_conn_acct *acct = nf_conn_acct_find(ct);

	if (!acct)
		return 0;

	if (dump_counters(skb, acct, IP_CT_DIR_ORIGINAL, type) < 0)
		return -1;
	if (dump_counters(skb, acct, IP_CT_DIR_REPLY, type) < 0)
		return -1;

	return 0;
}

static int
ctnetlink_dump_timestamp(struct sk_buff *skb, const struct nf_conn *ct)
{
	struct nlattr *nest_count;
	const struct nf_conn_tstamp *tstamp;

	tstamp = nf_conn_tstamp_find(ct);
	if (!tstamp)
		return 0;

	nest_count = nla_nest_start(skb, CTA_TIMESTAMP | NLA_F_NESTED);
	if (!nest_count)
		goto nla_put_failure;

	if (nla_put_be64(skb, CTA_TIMESTAMP_START, cpu_to_be64(tstamp->start)) ||
	    (tstamp->stop != 0 && nla_put_be64(skb, CTA_TIMESTAMP_STOP,
					       cpu_to_be64(tstamp->stop))))
		goto nla_put_failure;
	nla_nest_end(skb, nest_count);

	return 0;

nla_put_failure:
	return -1;
}

#ifdef CONFIG_NF_CONNTRACK_MARK
static inline int
ctnetlink_dump_mark(struct sk_buff *skb, const struct nf_conn *ct)
{
	if (nla_put_be32(skb, CTA_MARK, htonl(ct->mark)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}
#else
#define ctnetlink_dump_mark(a, b) (0)
#endif

#ifdef CONFIG_NF_CONNTRACK_SECMARK
static inline int
ctnetlink_dump_secctx(struct sk_buff *skb, const struct nf_conn *ct)
{
	struct nlattr *nest_secctx;
	int len, ret;
	char *secctx;

	ret = security_secid_to_secctx(ct->secmark, &secctx, &len);
	if (ret)
		return 0;

	ret = -1;
	nest_secctx = nla_nest_start(skb, CTA_SECCTX | NLA_F_NESTED);
	if (!nest_secctx)
		goto nla_put_failure;

	if (nla_put_string(skb, CTA_SECCTX_NAME, secctx))
		goto nla_put_failure;
	nla_nest_end(skb, nest_secctx);

	ret = 0;
nla_put_failure:
	security_release_secctx(secctx, len);
	return ret;
}
#else
#define ctnetlink_dump_secctx(a, b) (0)
#endif

#ifdef CONFIG_NF_CONNTRACK_LABELS
static int ctnetlink_label_size(const struct nf_conn *ct)
{
	struct nf_conn_labels *labels = nf_ct_labels_find(ct);

	if (!labels)
		return 0;
	return nla_total_size(labels->words * sizeof(long));
}

static int
ctnetlink_dump_labels(struct sk_buff *skb, const struct nf_conn *ct)
{
	struct nf_conn_labels *labels = nf_ct_labels_find(ct);
	unsigned int len, i;

	if (!labels)
		return 0;

	len = labels->words * sizeof(long);
	i = 0;
	do {
		if (labels->bits[i] != 0)
			return nla_put(skb, CTA_LABELS, len, labels->bits);
		i++;
	} while (i < labels->words);

	return 0;
}
#else
#define ctnetlink_dump_labels(a, b) (0)
#define ctnetlink_label_size(a)	(0)
#endif

#define master_tuple(ct) &(ct->master->tuplehash[IP_CT_DIR_ORIGINAL].tuple)

static inline int
ctnetlink_dump_master(struct sk_buff *skb, const struct nf_conn *ct)
{
	struct nlattr *nest_parms;

	if (!(ct->status & IPS_EXPECTED))
		return 0;

	nest_parms = nla_nest_start(skb, CTA_TUPLE_MASTER | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, master_tuple(ct)) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	return 0;

nla_put_failure:
	return -1;
}

static int
dump_ct_seq_adj(struct sk_buff *skb, const struct nf_ct_seqadj *seq, int type)
{
	struct nlattr *nest_parms;

	nest_parms = nla_nest_start(skb, type | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;

	if (nla_put_be32(skb, CTA_SEQADJ_CORRECTION_POS,
			 htonl(seq->correction_pos)) ||
	    nla_put_be32(skb, CTA_SEQADJ_OFFSET_BEFORE,
			 htonl(seq->offset_before)) ||
	    nla_put_be32(skb, CTA_SEQADJ_OFFSET_AFTER,
			 htonl(seq->offset_after)))
		goto nla_put_failure;

	nla_nest_end(skb, nest_parms);

	return 0;

nla_put_failure:
	return -1;
}

static inline int
ctnetlink_dump_ct_seq_adj(struct sk_buff *skb, const struct nf_conn *ct)
{
	struct nf_conn_seqadj *seqadj = nfct_seqadj(ct);
	struct nf_ct_seqadj *seq;

	if (!(ct->status & IPS_SEQ_ADJUST) || !seqadj)
		return 0;

	seq = &seqadj->seq[IP_CT_DIR_ORIGINAL];
	if (dump_ct_seq_adj(skb, seq, CTA_SEQ_ADJ_ORIG) == -1)
		return -1;

	seq = &seqadj->seq[IP_CT_DIR_REPLY];
	if (dump_ct_seq_adj(skb, seq, CTA_SEQ_ADJ_REPLY) == -1)
		return -1;

	return 0;
}

static inline int
ctnetlink_dump_id(struct sk_buff *skb, const struct nf_conn *ct)
{
	if (nla_put_be32(skb, CTA_ID, htonl((unsigned long)ct)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static inline int
ctnetlink_dump_use(struct sk_buff *skb, const struct nf_conn *ct)
{
	if (nla_put_be32(skb, CTA_USE, htonl(atomic_read(&ct->ct_general.use))))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static int
ctnetlink_fill_info(struct sk_buff *skb, u32 portid, u32 seq, u32 type,
		    struct nf_conn *ct)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	struct nlattr *nest_parms;
	unsigned int flags = portid ? NLM_F_MULTI : 0, event;

	event = (NFNL_SUBSYS_CTNETLINK << 8 | IPCTNL_MSG_CT_NEW);
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*nfmsg), flags);
	if (nlh == NULL)
		goto nlmsg_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family = nf_ct_l3num(ct);
	nfmsg->version      = NFNETLINK_V0;
	nfmsg->res_id	    = 0;

	nest_parms = nla_nest_start(skb, CTA_TUPLE_ORIG | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, nf_ct_tuple(ct, IP_CT_DIR_ORIGINAL)) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	nest_parms = nla_nest_start(skb, CTA_TUPLE_REPLY | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, nf_ct_tuple(ct, IP_CT_DIR_REPLY)) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	if (nf_ct_zone(ct) &&
	    nla_put_be16(skb, CTA_ZONE, htons(nf_ct_zone(ct))))
		goto nla_put_failure;

	if (ctnetlink_dump_status(skb, ct) < 0 ||
	    ctnetlink_dump_timeout(skb, ct) < 0 ||
	    ctnetlink_dump_acct(skb, ct, type) < 0 ||
	    ctnetlink_dump_timestamp(skb, ct) < 0 ||
	    ctnetlink_dump_protoinfo(skb, ct) < 0 ||
	    ctnetlink_dump_helpinfo(skb, ct) < 0 ||
	    ctnetlink_dump_mark(skb, ct) < 0 ||
	    ctnetlink_dump_secctx(skb, ct) < 0 ||
	    ctnetlink_dump_labels(skb, ct) < 0 ||
	    ctnetlink_dump_id(skb, ct) < 0 ||
	    ctnetlink_dump_use(skb, ct) < 0 ||
	    ctnetlink_dump_master(skb, ct) < 0 ||
	    ctnetlink_dump_ct_seq_adj(skb, ct) < 0)
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return skb->len;

nlmsg_failure:
nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -1;
}

static inline size_t
ctnetlink_proto_size(const struct nf_conn *ct)
{
	struct nf_conntrack_l3proto *l3proto;
	struct nf_conntrack_l4proto *l4proto;
	size_t len = 0;

	rcu_read_lock();
	l3proto = __nf_ct_l3proto_find(nf_ct_l3num(ct));
	len += l3proto->nla_size;

	l4proto = __nf_ct_l4proto_find(nf_ct_l3num(ct), nf_ct_protonum(ct));
	len += l4proto->nla_size;
	rcu_read_unlock();

	return len;
}

static inline size_t
ctnetlink_acct_size(const struct nf_conn *ct)
{
	if (!nf_ct_ext_exist(ct, NF_CT_EXT_ACCT))
		return 0;
	return 2 * nla_total_size(0) /* CTA_COUNTERS_ORIG|REPL */
	       + 2 * nla_total_size(sizeof(uint64_t)) /* CTA_COUNTERS_PACKETS */
	       + 2 * nla_total_size(sizeof(uint64_t)) /* CTA_COUNTERS_BYTES */
	       ;
}

static inline int
ctnetlink_secctx_size(const struct nf_conn *ct)
{
#ifdef CONFIG_NF_CONNTRACK_SECMARK
	int len, ret;

	ret = security_secid_to_secctx(ct->secmark, NULL, &len);
	if (ret)
		return 0;

	return nla_total_size(0) /* CTA_SECCTX */
	       + nla_total_size(sizeof(char) * len); /* CTA_SECCTX_NAME */
#else
	return 0;
#endif
}

static inline size_t
ctnetlink_timestamp_size(const struct nf_conn *ct)
{
#ifdef CONFIG_NF_CONNTRACK_TIMESTAMP
	if (!nf_ct_ext_exist(ct, NF_CT_EXT_TSTAMP))
		return 0;
	return nla_total_size(0) + 2 * nla_total_size(sizeof(uint64_t));
#else
	return 0;
#endif
}

static inline size_t
ctnetlink_nlmsg_size(const struct nf_conn *ct)
{
	return NLMSG_ALIGN(sizeof(struct nfgenmsg))
	       + 3 * nla_total_size(0) /* CTA_TUPLE_ORIG|REPL|MASTER */
	       + 3 * nla_total_size(0) /* CTA_TUPLE_IP */
	       + 3 * nla_total_size(0) /* CTA_TUPLE_PROTO */
	       + 3 * nla_total_size(sizeof(u_int8_t)) /* CTA_PROTO_NUM */
	       + nla_total_size(sizeof(u_int32_t)) /* CTA_ID */
	       + nla_total_size(sizeof(u_int32_t)) /* CTA_STATUS */
	       + ctnetlink_acct_size(ct)
	       + ctnetlink_timestamp_size(ct)
	       + nla_total_size(sizeof(u_int32_t)) /* CTA_TIMEOUT */
	       + nla_total_size(0) /* CTA_PROTOINFO */
	       + nla_total_size(0) /* CTA_HELP */
	       + nla_total_size(NF_CT_HELPER_NAME_LEN) /* CTA_HELP_NAME */
	       + ctnetlink_secctx_size(ct)
#ifdef CONFIG_NF_NAT_NEEDED
	       + 2 * nla_total_size(0) /* CTA_NAT_SEQ_ADJ_ORIG|REPL */
	       + 6 * nla_total_size(sizeof(u_int32_t)) /* CTA_NAT_SEQ_OFFSET */
#endif
#ifdef CONFIG_NF_CONNTRACK_MARK
	       + nla_total_size(sizeof(u_int32_t)) /* CTA_MARK */
#endif
#ifdef CONFIG_NF_CONNTRACK_ZONES
	       + nla_total_size(sizeof(u_int16_t)) /* CTA_ZONE */
#endif
	       + ctnetlink_proto_size(ct)
	       + ctnetlink_label_size(ct)
	       ;
}

#ifdef CONFIG_NF_CONNTRACK_EVENTS
static int
ctnetlink_conntrack_event(unsigned int events, struct nf_ct_event *item)
{
	struct net *net;
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	struct nlattr *nest_parms;
	struct nf_conn *ct = item->ct;
	struct sk_buff *skb;
	unsigned int type;
	unsigned int flags = 0, group;
	int err;

	/* ignore our fake conntrack entry */
	if (nf_ct_is_untracked(ct))
		return 0;

	if (events & (1 << IPCT_DESTROY)) {
		type = IPCTNL_MSG_CT_DELETE;
		group = NFNLGRP_CONNTRACK_DESTROY;
	} else  if (events & ((1 << IPCT_NEW) | (1 << IPCT_RELATED))) {
		type = IPCTNL_MSG_CT_NEW;
		flags = NLM_F_CREATE|NLM_F_EXCL;
		group = NFNLGRP_CONNTRACK_NEW;
	} else  if (events) {
		type = IPCTNL_MSG_CT_NEW;
		group = NFNLGRP_CONNTRACK_UPDATE;
	} else
		return 0;

	net = nf_ct_net(ct);
	if (!item->report && !nfnetlink_has_listeners(net, group))
		return 0;

	skb = nlmsg_new(ctnetlink_nlmsg_size(ct), GFP_ATOMIC);
	if (skb == NULL)
		goto errout;

	type |= NFNL_SUBSYS_CTNETLINK << 8;
	nlh = nlmsg_put(skb, item->portid, 0, type, sizeof(*nfmsg), flags);
	if (nlh == NULL)
		goto nlmsg_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family = nf_ct_l3num(ct);
	nfmsg->version	= NFNETLINK_V0;
	nfmsg->res_id	= 0;

	rcu_read_lock();
	nest_parms = nla_nest_start(skb, CTA_TUPLE_ORIG | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, nf_ct_tuple(ct, IP_CT_DIR_ORIGINAL)) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	nest_parms = nla_nest_start(skb, CTA_TUPLE_REPLY | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, nf_ct_tuple(ct, IP_CT_DIR_REPLY)) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	if (nf_ct_zone(ct) &&
	    nla_put_be16(skb, CTA_ZONE, htons(nf_ct_zone(ct))))
		goto nla_put_failure;

	if (ctnetlink_dump_id(skb, ct) < 0)
		goto nla_put_failure;

	if (ctnetlink_dump_status(skb, ct) < 0)
		goto nla_put_failure;

	if (events & (1 << IPCT_DESTROY)) {
		if (ctnetlink_dump_acct(skb, ct, type) < 0 ||
		    ctnetlink_dump_timestamp(skb, ct) < 0)
			goto nla_put_failure;
	} else {
		if (ctnetlink_dump_timeout(skb, ct) < 0)
			goto nla_put_failure;

		if (events & (1 << IPCT_PROTOINFO)
		    && ctnetlink_dump_protoinfo(skb, ct) < 0)
			goto nla_put_failure;

		if ((events & (1 << IPCT_HELPER) || nfct_help(ct))
		    && ctnetlink_dump_helpinfo(skb, ct) < 0)
			goto nla_put_failure;

#ifdef CONFIG_NF_CONNTRACK_SECMARK
		if ((events & (1 << IPCT_SECMARK) || ct->secmark)
		    && ctnetlink_dump_secctx(skb, ct) < 0)
			goto nla_put_failure;
#endif
		if (events & (1 << IPCT_LABEL) &&
		     ctnetlink_dump_labels(skb, ct) < 0)
			goto nla_put_failure;

		if (events & (1 << IPCT_RELATED) &&
		    ctnetlink_dump_master(skb, ct) < 0)
			goto nla_put_failure;

		if (events & (1 << IPCT_SEQADJ) &&
		    ctnetlink_dump_ct_seq_adj(skb, ct) < 0)
			goto nla_put_failure;

		if (events & (1 << IPCT_COUNTER) &&
		    ctnetlink_dump_acct(skb, ct, 0) < 0)
			goto nla_put_failure;
	}

#ifdef CONFIG_NF_CONNTRACK_MARK
	if ((events & (1 << IPCT_MARK) || ct->mark)
	    && ctnetlink_dump_mark(skb, ct) < 0)
		goto nla_put_failure;
#endif
	rcu_read_unlock();

	nlmsg_end(skb, nlh);
	err = nfnetlink_send(skb, net, item->portid, group, item->report,
			     GFP_ATOMIC);
	if (err == -ENOBUFS || err == -EAGAIN)
		return -ENOBUFS;

	return 0;

nla_put_failure:
	rcu_read_unlock();
	nlmsg_cancel(skb, nlh);
nlmsg_failure:
	kfree_skb(skb);
errout:
	if (nfnetlink_set_err(net, 0, group, -ENOBUFS) > 0)
		return -ENOBUFS;

	return 0;
}
#endif /* CONFIG_NF_CONNTRACK_EVENTS */

static int ctnetlink_done(struct netlink_callback *cb)
{
	if (cb->args[1])
		nf_ct_put((struct nf_conn *)cb->args[1]);
	kfree(cb->data);
	return 0;
}

struct ctnetlink_dump_filter {
	struct {
		u_int32_t val;
		u_int32_t mask;
	} mark;
};

static int
ctnetlink_dump_table(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct nf_conn *ct, *last;
	struct nf_conntrack_tuple_hash *h;
	struct hlist_nulls_node *n;
	struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	u_int8_t l3proto = nfmsg->nfgen_family;
	int res;
	spinlock_t *lockp;

#ifdef CONFIG_NF_CONNTRACK_MARK
	const struct ctnetlink_dump_filter *filter = cb->data;
#endif

	last = (struct nf_conn *)cb->args[1];

	local_bh_disable();
	for (; cb->args[0] < net->ct.htable_size; cb->args[0]++) {
restart:
		lockp = &nf_conntrack_locks[cb->args[0] % CONNTRACK_LOCKS];
		spin_lock(lockp);
		if (cb->args[0] >= net->ct.htable_size) {
			spin_unlock(lockp);
			goto out;
		}
		hlist_nulls_for_each_entry(h, n, &net->ct.hash[cb->args[0]],
					 hnnode) {
			if (NF_CT_DIRECTION(h) != IP_CT_DIR_ORIGINAL)
				continue;
			ct = nf_ct_tuplehash_to_ctrack(h);
			/* Dump entries of a given L3 protocol number.
			 * If it is not specified, ie. l3proto == 0,
			 * then dump everything. */
			if (l3proto && nf_ct_l3num(ct) != l3proto)
				continue;
			if (cb->args[1]) {
				if (ct != last)
					continue;
				cb->args[1] = 0;
			}
#ifdef CONFIG_NF_CONNTRACK_MARK
			if (filter && !((ct->mark & filter->mark.mask) ==
					filter->mark.val)) {
				continue;
			}
#endif
			rcu_read_lock();
			res =
			ctnetlink_fill_info(skb, NETLINK_CB(cb->skb).portid,
					    cb->nlh->nlmsg_seq,
					    NFNL_MSG_TYPE(cb->nlh->nlmsg_type),
					    ct);
			rcu_read_unlock();
			if (res < 0) {
				nf_conntrack_get(&ct->ct_general);
				cb->args[1] = (unsigned long)ct;
				spin_unlock(lockp);
				goto out;
			}
		}
		spin_unlock(lockp);
		if (cb->args[1]) {
			cb->args[1] = 0;
			goto restart;
		}
	}
out:
	local_bh_enable();
	if (last)
		nf_ct_put(last);

	return skb->len;
}

static inline int
ctnetlink_parse_tuple_ip(struct nlattr *attr, struct nf_conntrack_tuple *tuple)
{
	struct nlattr *tb[CTA_IP_MAX+1];
	struct nf_conntrack_l3proto *l3proto;
	int ret = 0;

	ret = nla_parse_nested(tb, CTA_IP_MAX, attr, NULL);
	if (ret < 0)
		return ret;

	rcu_read_lock();
	l3proto = __nf_ct_l3proto_find(tuple->src.l3num);

	if (likely(l3proto->nlattr_to_tuple)) {
		ret = nla_validate_nested(attr, CTA_IP_MAX,
					  l3proto->nla_policy);
		if (ret == 0)
			ret = l3proto->nlattr_to_tuple(tb, tuple);
	}

	rcu_read_unlock();

	return ret;
}

static const struct nla_policy proto_nla_policy[CTA_PROTO_MAX+1] = {
	[CTA_PROTO_NUM]	= { .type = NLA_U8 },
};

static inline int
ctnetlink_parse_tuple_proto(struct nlattr *attr,
			    struct nf_conntrack_tuple *tuple)
{
	struct nlattr *tb[CTA_PROTO_MAX+1];
	struct nf_conntrack_l4proto *l4proto;
	int ret = 0;

	ret = nla_parse_nested(tb, CTA_PROTO_MAX, attr, proto_nla_policy);
	if (ret < 0)
		return ret;

	if (!tb[CTA_PROTO_NUM])
		return -EINVAL;
	tuple->dst.protonum = nla_get_u8(tb[CTA_PROTO_NUM]);

	rcu_read_lock();
	l4proto = __nf_ct_l4proto_find(tuple->src.l3num, tuple->dst.protonum);

	if (likely(l4proto->nlattr_to_tuple)) {
		ret = nla_validate_nested(attr, CTA_PROTO_MAX,
					  l4proto->nla_policy);
		if (ret == 0)
			ret = l4proto->nlattr_to_tuple(tb, tuple);
	}

	rcu_read_unlock();

	return ret;
}

static const struct nla_policy tuple_nla_policy[CTA_TUPLE_MAX+1] = {
	[CTA_TUPLE_IP]		= { .type = NLA_NESTED },
	[CTA_TUPLE_PROTO]	= { .type = NLA_NESTED },
};

static int
ctnetlink_parse_tuple(const struct nlattr * const cda[],
		      struct nf_conntrack_tuple *tuple,
		      enum ctattr_type type, u_int8_t l3num)
{
	struct nlattr *tb[CTA_TUPLE_MAX+1];
	int err;

	memset(tuple, 0, sizeof(*tuple));

	err = nla_parse_nested(tb, CTA_TUPLE_MAX, cda[type], tuple_nla_policy);
	if (err < 0)
		return err;

	if (!tb[CTA_TUPLE_IP])
		return -EINVAL;

	tuple->src.l3num = l3num;

	err = ctnetlink_parse_tuple_ip(tb[CTA_TUPLE_IP], tuple);
	if (err < 0)
		return err;

	if (!tb[CTA_TUPLE_PROTO])
		return -EINVAL;

	err = ctnetlink_parse_tuple_proto(tb[CTA_TUPLE_PROTO], tuple);
	if (err < 0)
		return err;

	/* orig and expect tuples get DIR_ORIGINAL */
	if (type == CTA_TUPLE_REPLY)
		tuple->dst.dir = IP_CT_DIR_REPLY;
	else
		tuple->dst.dir = IP_CT_DIR_ORIGINAL;

	return 0;
}

static int
ctnetlink_parse_zone(const struct nlattr *attr, u16 *zone)
{
	if (attr)
#ifdef CONFIG_NF_CONNTRACK_ZONES
		*zone = ntohs(nla_get_be16(attr));
#else
		return -EOPNOTSUPP;
#endif
	else
		*zone = 0;

	return 0;
}

static const struct nla_policy help_nla_policy[CTA_HELP_MAX+1] = {
	[CTA_HELP_NAME]		= { .type = NLA_NUL_STRING,
				    .len = NF_CT_HELPER_NAME_LEN - 1 },
};

static inline int
ctnetlink_parse_help(const struct nlattr *attr, char **helper_name,
		     struct nlattr **helpinfo)
{
	int err;
	struct nlattr *tb[CTA_HELP_MAX+1];

	err = nla_parse_nested(tb, CTA_HELP_MAX, attr, help_nla_policy);
	if (err < 0)
		return err;

	if (!tb[CTA_HELP_NAME])
		return -EINVAL;

	*helper_name = nla_data(tb[CTA_HELP_NAME]);

	if (tb[CTA_HELP_INFO])
		*helpinfo = tb[CTA_HELP_INFO];

	return 0;
}

static const struct nla_policy ct_nla_policy[CTA_MAX+1] = {
	[CTA_TUPLE_ORIG]	= { .type = NLA_NESTED },
	[CTA_TUPLE_REPLY]	= { .type = NLA_NESTED },
	[CTA_STATUS] 		= { .type = NLA_U32 },
	[CTA_PROTOINFO]		= { .type = NLA_NESTED },
	[CTA_HELP]		= { .type = NLA_NESTED },
	[CTA_NAT_SRC]		= { .type = NLA_NESTED },
	[CTA_TIMEOUT] 		= { .type = NLA_U32 },
	[CTA_MARK]		= { .type = NLA_U32 },
	[CTA_ID]		= { .type = NLA_U32 },
	[CTA_NAT_DST]		= { .type = NLA_NESTED },
	[CTA_TUPLE_MASTER]	= { .type = NLA_NESTED },
	[CTA_NAT_SEQ_ADJ_ORIG]  = { .type = NLA_NESTED },
	[CTA_NAT_SEQ_ADJ_REPLY] = { .type = NLA_NESTED },
	[CTA_ZONE]		= { .type = NLA_U16 },
	[CTA_MARK_MASK]		= { .type = NLA_U32 },
	[CTA_LABELS]		= { .type = NLA_BINARY,
				    .len = NF_CT_LABELS_MAX_SIZE },
	[CTA_LABELS_MASK]	= { .type = NLA_BINARY,
				    .len = NF_CT_LABELS_MAX_SIZE },
};

static int
ctnetlink_del_conntrack(struct sock *ctnl, struct sk_buff *skb,
			const struct nlmsghdr *nlh,
			const struct nlattr * const cda[])
{
	struct net *net = sock_net(ctnl);
	struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_tuple tuple;
	struct nf_conn *ct;
	struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	u16 zone;
	int err;

	err = ctnetlink_parse_zone(cda[CTA_ZONE], &zone);
	if (err < 0)
		return err;

	if (cda[CTA_TUPLE_ORIG])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_TUPLE_ORIG, u3);
	else if (cda[CTA_TUPLE_REPLY])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_TUPLE_REPLY, u3);
	else {
		/* Flush the whole table */
		nf_conntrack_flush_report(net,
					 NETLINK_CB(skb).portid,
					 nlmsg_report(nlh));
		return 0;
	}

	if (err < 0)
		return err;

	h = nf_conntrack_find_get(net, zone, &tuple);
	if (!h)
		return -ENOENT;

	ct = nf_ct_tuplehash_to_ctrack(h);

	if (cda[CTA_ID]) {
		u_int32_t id = ntohl(nla_get_be32(cda[CTA_ID]));
		if (id != (u32)(unsigned long)ct) {
			nf_ct_put(ct);
			return -ENOENT;
		}
	}

	if (del_timer(&ct->timeout))
		nf_ct_delete(ct, NETLINK_CB(skb).portid, nlmsg_report(nlh));

	nf_ct_put(ct);

	return 0;
}

static int
ctnetlink_get_conntrack(struct sock *ctnl, struct sk_buff *skb,
			const struct nlmsghdr *nlh,
			const struct nlattr * const cda[])
{
	struct net *net = sock_net(ctnl);
	struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_tuple tuple;
	struct nf_conn *ct;
	struct sk_buff *skb2 = NULL;
	struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	u16 zone;
	int err;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = ctnetlink_dump_table,
			.done = ctnetlink_done,
		};
#ifdef CONFIG_NF_CONNTRACK_MARK
		if (cda[CTA_MARK] && cda[CTA_MARK_MASK]) {
			struct ctnetlink_dump_filter *filter;

			filter = kzalloc(sizeof(struct ctnetlink_dump_filter),
					 GFP_ATOMIC);
			if (filter == NULL)
				return -ENOMEM;

			filter->mark.val = ntohl(nla_get_be32(cda[CTA_MARK]));
			filter->mark.mask =
				ntohl(nla_get_be32(cda[CTA_MARK_MASK]));
			c.data = filter;
		}
#endif
		return netlink_dump_start(ctnl, skb, nlh, &c);
	}

	err = ctnetlink_parse_zone(cda[CTA_ZONE], &zone);
	if (err < 0)
		return err;

	if (cda[CTA_TUPLE_ORIG])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_TUPLE_ORIG, u3);
	else if (cda[CTA_TUPLE_REPLY])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_TUPLE_REPLY, u3);
	else
		return -EINVAL;

	if (err < 0)
		return err;

	h = nf_conntrack_find_get(net, zone, &tuple);
	if (!h)
		return -ENOENT;

	ct = nf_ct_tuplehash_to_ctrack(h);

	err = -ENOMEM;
	skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb2 == NULL) {
		nf_ct_put(ct);
		return -ENOMEM;
	}

	rcu_read_lock();
	err = ctnetlink_fill_info(skb2, NETLINK_CB(skb).portid, nlh->nlmsg_seq,
				  NFNL_MSG_TYPE(nlh->nlmsg_type), ct);
	rcu_read_unlock();
	nf_ct_put(ct);
	if (err <= 0)
		goto free;

	err = netlink_unicast(ctnl, skb2, NETLINK_CB(skb).portid, MSG_DONTWAIT);
	if (err < 0)
		goto out;

	return 0;

free:
	kfree_skb(skb2);
out:
	/* this avoids a loop in nfnetlink. */
	return err == -EAGAIN ? -ENOBUFS : err;
}

static int ctnetlink_done_list(struct netlink_callback *cb)
{
	if (cb->args[1])
		nf_ct_put((struct nf_conn *)cb->args[1]);
	return 0;
}

static int
ctnetlink_dump_list(struct sk_buff *skb, struct netlink_callback *cb, bool dying)
{
	struct nf_conn *ct, *last;
	struct nf_conntrack_tuple_hash *h;
	struct hlist_nulls_node *n;
	struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	u_int8_t l3proto = nfmsg->nfgen_family;
	int res;
	int cpu;
	struct hlist_nulls_head *list;
	struct net *net = sock_net(skb->sk);

	if (cb->args[2])
		return 0;

	last = (struct nf_conn *)cb->args[1];

	for (cpu = cb->args[0]; cpu < nr_cpu_ids; cpu++) {
		struct ct_pcpu *pcpu;

		if (!cpu_possible(cpu))
			continue;

		pcpu = per_cpu_ptr(net->ct.pcpu_lists, cpu);
		spin_lock_bh(&pcpu->lock);
		list = dying ? &pcpu->dying : &pcpu->unconfirmed;
restart:
		hlist_nulls_for_each_entry(h, n, list, hnnode) {
			ct = nf_ct_tuplehash_to_ctrack(h);
			if (l3proto && nf_ct_l3num(ct) != l3proto)
				continue;
			if (cb->args[1]) {
				if (ct != last)
					continue;
				cb->args[1] = 0;
			}
			rcu_read_lock();
			res = ctnetlink_fill_info(skb, NETLINK_CB(cb->skb).portid,
						  cb->nlh->nlmsg_seq,
						  NFNL_MSG_TYPE(cb->nlh->nlmsg_type),
						  ct);
			rcu_read_unlock();
			if (res < 0) {
				if (!atomic_inc_not_zero(&ct->ct_general.use))
					continue;
				cb->args[0] = cpu;
				cb->args[1] = (unsigned long)ct;
				spin_unlock_bh(&pcpu->lock);
				goto out;
			}
		}
		if (cb->args[1]) {
			cb->args[1] = 0;
			goto restart;
		}
		spin_unlock_bh(&pcpu->lock);
	}
	cb->args[2] = 1;
out:
	if (last)
		nf_ct_put(last);

	return skb->len;
}

static int
ctnetlink_dump_dying(struct sk_buff *skb, struct netlink_callback *cb)
{
	return ctnetlink_dump_list(skb, cb, true);
}

static int
ctnetlink_get_ct_dying(struct sock *ctnl, struct sk_buff *skb,
		       const struct nlmsghdr *nlh,
		       const struct nlattr * const cda[])
{
	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = ctnetlink_dump_dying,
			.done = ctnetlink_done_list,
		};
		return netlink_dump_start(ctnl, skb, nlh, &c);
	}

	return -EOPNOTSUPP;
}

static int
ctnetlink_dump_unconfirmed(struct sk_buff *skb, struct netlink_callback *cb)
{
	return ctnetlink_dump_list(skb, cb, false);
}

static int
ctnetlink_get_ct_unconfirmed(struct sock *ctnl, struct sk_buff *skb,
			     const struct nlmsghdr *nlh,
			     const struct nlattr * const cda[])
{
	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = ctnetlink_dump_unconfirmed,
			.done = ctnetlink_done_list,
		};
		return netlink_dump_start(ctnl, skb, nlh, &c);
	}

	return -EOPNOTSUPP;
}

#ifdef CONFIG_NF_NAT_NEEDED
static int
ctnetlink_parse_nat_setup(struct nf_conn *ct,
			  enum nf_nat_manip_type manip,
			  const struct nlattr *attr)
{
	typeof(nfnetlink_parse_nat_setup_hook) parse_nat_setup;
	int err;

	parse_nat_setup = rcu_dereference(nfnetlink_parse_nat_setup_hook);
	if (!parse_nat_setup) {
#ifdef CONFIG_MODULES
		rcu_read_unlock();
		nfnl_unlock(NFNL_SUBSYS_CTNETLINK);
		if (request_module("nf-nat") < 0) {
			nfnl_lock(NFNL_SUBSYS_CTNETLINK);
			rcu_read_lock();
			return -EOPNOTSUPP;
		}
		nfnl_lock(NFNL_SUBSYS_CTNETLINK);
		rcu_read_lock();
		if (nfnetlink_parse_nat_setup_hook)
			return -EAGAIN;
#endif
		return -EOPNOTSUPP;
	}

	err = parse_nat_setup(ct, manip, attr);
	if (err == -EAGAIN) {
#ifdef CONFIG_MODULES
		rcu_read_unlock();
		nfnl_unlock(NFNL_SUBSYS_CTNETLINK);
		if (request_module("nf-nat-%u", nf_ct_l3num(ct)) < 0) {
			nfnl_lock(NFNL_SUBSYS_CTNETLINK);
			rcu_read_lock();
			return -EOPNOTSUPP;
		}
		nfnl_lock(NFNL_SUBSYS_CTNETLINK);
		rcu_read_lock();
#else
		err = -EOPNOTSUPP;
#endif
	}
	return err;
}
#endif

static int
ctnetlink_change_status(struct nf_conn *ct, const struct nlattr * const cda[])
{
	unsigned long d;
	unsigned int status = ntohl(nla_get_be32(cda[CTA_STATUS]));
	d = ct->status ^ status;

	if (d & (IPS_EXPECTED|IPS_CONFIRMED|IPS_DYING))
		/* unchangeable */
		return -EBUSY;

	if (d & IPS_SEEN_REPLY && !(status & IPS_SEEN_REPLY))
		/* SEEN_REPLY bit can only be set */
		return -EBUSY;

	if (d & IPS_ASSURED && !(status & IPS_ASSURED))
		/* ASSURED bit can only be set */
		return -EBUSY;

	/* Be careful here, modifying NAT bits can screw up things,
	 * so don't let users modify them directly if they don't pass
	 * nf_nat_range. */
	ct->status |= status & ~(IPS_NAT_DONE_MASK | IPS_NAT_MASK);
	return 0;
}

static int
ctnetlink_setup_nat(struct nf_conn *ct, const struct nlattr * const cda[])
{
#ifdef CONFIG_NF_NAT_NEEDED
	int ret;

	if (!cda[CTA_NAT_DST] && !cda[CTA_NAT_SRC])
		return 0;

	ret = ctnetlink_parse_nat_setup(ct, NF_NAT_MANIP_DST,
					cda[CTA_NAT_DST]);
	if (ret < 0)
		return ret;

	ret = ctnetlink_parse_nat_setup(ct, NF_NAT_MANIP_SRC,
					cda[CTA_NAT_SRC]);
	return ret;
#else
	if (!cda[CTA_NAT_DST] && !cda[CTA_NAT_SRC])
		return 0;
	return -EOPNOTSUPP;
#endif
}

static inline int
ctnetlink_change_helper(struct nf_conn *ct, const struct nlattr * const cda[])
{
	struct nf_conntrack_helper *helper;
	struct nf_conn_help *help = nfct_help(ct);
	char *helpname = NULL;
	struct nlattr *helpinfo = NULL;
	int err;

	/* don't change helper of sibling connections */
	if (ct->master)
		return -EBUSY;

	err = ctnetlink_parse_help(cda[CTA_HELP], &helpname, &helpinfo);
	if (err < 0)
		return err;

	if (!strcmp(helpname, "")) {
		if (help && help->helper) {
			/* we had a helper before ... */
			nf_ct_remove_expectations(ct);
			RCU_INIT_POINTER(help->helper, NULL);
		}

		return 0;
	}

	helper = __nf_conntrack_helper_find(helpname, nf_ct_l3num(ct),
					    nf_ct_protonum(ct));
	if (helper == NULL) {
#ifdef CONFIG_MODULES
		spin_unlock_bh(&nf_conntrack_expect_lock);

		if (request_module("nfct-helper-%s", helpname) < 0) {
			spin_lock_bh(&nf_conntrack_expect_lock);
			return -EOPNOTSUPP;
		}

		spin_lock_bh(&nf_conntrack_expect_lock);
		helper = __nf_conntrack_helper_find(helpname, nf_ct_l3num(ct),
						    nf_ct_protonum(ct));
		if (helper)
			return -EAGAIN;
#endif
		return -EOPNOTSUPP;
	}

	if (help) {
		if (help->helper == helper) {
			/* update private helper data if allowed. */
			if (helper->from_nlattr)
				helper->from_nlattr(helpinfo, ct);
			return 0;
		} else
			return -EBUSY;
	}

	/* we cannot set a helper for an existing conntrack */
	return -EOPNOTSUPP;
}

static inline int
ctnetlink_change_timeout(struct nf_conn *ct, const struct nlattr * const cda[])
{
	u_int32_t timeout = ntohl(nla_get_be32(cda[CTA_TIMEOUT]));

	if (!del_timer(&ct->timeout))
		return -ETIME;

	ct->timeout.expires = jiffies + timeout * HZ;
	add_timer(&ct->timeout);

/* Refresh the NAT type entry. */
#if defined(CONFIG_IP_NF_TARGET_NATTYPE_MODULE)
	(void)nattype_refresh_timer(ct->nattype_entry, ct->timeout.expires);
#endif

	return 0;
}

static const struct nla_policy protoinfo_policy[CTA_PROTOINFO_MAX+1] = {
	[CTA_PROTOINFO_TCP]	= { .type = NLA_NESTED },
	[CTA_PROTOINFO_DCCP]	= { .type = NLA_NESTED },
	[CTA_PROTOINFO_SCTP]	= { .type = NLA_NESTED },
};

static inline int
ctnetlink_change_protoinfo(struct nf_conn *ct, const struct nlattr * const cda[])
{
	const struct nlattr *attr = cda[CTA_PROTOINFO];
	struct nlattr *tb[CTA_PROTOINFO_MAX+1];
	struct nf_conntrack_l4proto *l4proto;
	int err = 0;

	err = nla_parse_nested(tb, CTA_PROTOINFO_MAX, attr, protoinfo_policy);
	if (err < 0)
		return err;

	rcu_read_lock();
	l4proto = __nf_ct_l4proto_find(nf_ct_l3num(ct), nf_ct_protonum(ct));
	if (l4proto->from_nlattr)
		err = l4proto->from_nlattr(tb, ct);
	rcu_read_unlock();

	return err;
}

static const struct nla_policy seqadj_policy[CTA_SEQADJ_MAX+1] = {
	[CTA_SEQADJ_CORRECTION_POS]	= { .type = NLA_U32 },
	[CTA_SEQADJ_OFFSET_BEFORE]	= { .type = NLA_U32 },
	[CTA_SEQADJ_OFFSET_AFTER]	= { .type = NLA_U32 },
};

static inline int
change_seq_adj(struct nf_ct_seqadj *seq, const struct nlattr * const attr)
{
	int err;
	struct nlattr *cda[CTA_SEQADJ_MAX+1];

	err = nla_parse_nested(cda, CTA_SEQADJ_MAX, attr, seqadj_policy);
	if (err < 0)
		return err;

	if (!cda[CTA_SEQADJ_CORRECTION_POS])
		return -EINVAL;

	seq->correction_pos =
		ntohl(nla_get_be32(cda[CTA_SEQADJ_CORRECTION_POS]));

	if (!cda[CTA_SEQADJ_OFFSET_BEFORE])
		return -EINVAL;

	seq->offset_before =
		ntohl(nla_get_be32(cda[CTA_SEQADJ_OFFSET_BEFORE]));

	if (!cda[CTA_SEQADJ_OFFSET_AFTER])
		return -EINVAL;

	seq->offset_after =
		ntohl(nla_get_be32(cda[CTA_SEQADJ_OFFSET_AFTER]));

	return 0;
}

static int
ctnetlink_change_seq_adj(struct nf_conn *ct,
			 const struct nlattr * const cda[])
{
	struct nf_conn_seqadj *seqadj = nfct_seqadj(ct);
	int ret = 0;

	if (!seqadj)
		return 0;

	if (cda[CTA_SEQ_ADJ_ORIG]) {
		ret = change_seq_adj(&seqadj->seq[IP_CT_DIR_ORIGINAL],
				     cda[CTA_SEQ_ADJ_ORIG]);
		if (ret < 0)
			return ret;

		ct->status |= IPS_SEQ_ADJUST;
	}

	if (cda[CTA_SEQ_ADJ_REPLY]) {
		ret = change_seq_adj(&seqadj->seq[IP_CT_DIR_REPLY],
				     cda[CTA_SEQ_ADJ_REPLY]);
		if (ret < 0)
			return ret;

		ct->status |= IPS_SEQ_ADJUST;
	}

	return 0;
}

static int
ctnetlink_attach_labels(struct nf_conn *ct, const struct nlattr * const cda[])
{
#ifdef CONFIG_NF_CONNTRACK_LABELS
	size_t len = nla_len(cda[CTA_LABELS]);
	const void *mask = cda[CTA_LABELS_MASK];

	if (len & (sizeof(u32)-1)) /* must be multiple of u32 */
		return -EINVAL;

	if (mask) {
		if (nla_len(cda[CTA_LABELS_MASK]) == 0 ||
		    nla_len(cda[CTA_LABELS_MASK]) != len)
			return -EINVAL;
		mask = nla_data(cda[CTA_LABELS_MASK]);
	}

	len /= sizeof(u32);

	return nf_connlabels_replace(ct, nla_data(cda[CTA_LABELS]), mask, len);
#else
	return -EOPNOTSUPP;
#endif
}

static int
ctnetlink_change_conntrack(struct nf_conn *ct,
			   const struct nlattr * const cda[])
{
	int err;

	/* only allow NAT changes and master assignation for new conntracks */
	if (cda[CTA_NAT_SRC] || cda[CTA_NAT_DST] || cda[CTA_TUPLE_MASTER])
		return -EOPNOTSUPP;

	if (cda[CTA_HELP]) {
		err = ctnetlink_change_helper(ct, cda);
		if (err < 0)
			return err;
	}

	if (cda[CTA_TIMEOUT]) {
		err = ctnetlink_change_timeout(ct, cda);
		if (err < 0)
			return err;
	}

	if (cda[CTA_STATUS]) {
		err = ctnetlink_change_status(ct, cda);
		if (err < 0)
			return err;
	}

	if (cda[CTA_PROTOINFO]) {
		err = ctnetlink_change_protoinfo(ct, cda);
		if (err < 0)
			return err;
	}

#if defined(CONFIG_NF_CONNTRACK_MARK)
	if (cda[CTA_MARK])
		ct->mark = ntohl(nla_get_be32(cda[CTA_MARK]));
#endif

	if (cda[CTA_SEQ_ADJ_ORIG] || cda[CTA_SEQ_ADJ_REPLY]) {
		err = ctnetlink_change_seq_adj(ct, cda);
		if (err < 0)
			return err;
	}

	if (cda[CTA_LABELS]) {
		err = ctnetlink_attach_labels(ct, cda);
		if (err < 0)
			return err;
	}

	return 0;
}

static struct nf_conn *
ctnetlink_create_conntrack(struct net *net, u16 zone,
			   const struct nlattr * const cda[],
			   struct nf_conntrack_tuple *otuple,
			   struct nf_conntrack_tuple *rtuple,
			   u8 u3)
{
	struct nf_conn *ct;
	int err = -EINVAL;
	struct nf_conntrack_helper *helper;
	struct nf_conn_tstamp *tstamp;

	ct = nf_conntrack_alloc(net, zone, otuple, rtuple, GFP_ATOMIC);
	if (IS_ERR(ct))
		return ERR_PTR(-ENOMEM);

	if (!cda[CTA_TIMEOUT])
		goto err1;
	ct->timeout.expires = ntohl(nla_get_be32(cda[CTA_TIMEOUT]));

	ct->timeout.expires = jiffies + ct->timeout.expires * HZ;

	rcu_read_lock();
 	if (cda[CTA_HELP]) {
		char *helpname = NULL;
		struct nlattr *helpinfo = NULL;

		err = ctnetlink_parse_help(cda[CTA_HELP], &helpname, &helpinfo);
 		if (err < 0)
			goto err2;

		helper = __nf_conntrack_helper_find(helpname, nf_ct_l3num(ct),
						    nf_ct_protonum(ct));
		if (helper == NULL) {
			rcu_read_unlock();
#ifdef CONFIG_MODULES
			if (request_module("nfct-helper-%s", helpname) < 0) {
				err = -EOPNOTSUPP;
				goto err1;
			}

			rcu_read_lock();
			helper = __nf_conntrack_helper_find(helpname,
							    nf_ct_l3num(ct),
							    nf_ct_protonum(ct));
			if (helper) {
				err = -EAGAIN;
				goto err2;
			}
			rcu_read_unlock();
#endif
			err = -EOPNOTSUPP;
			goto err1;
		} else {
			struct nf_conn_help *help;

			help = nf_ct_helper_ext_add(ct, helper, GFP_ATOMIC);
			if (help == NULL) {
				err = -ENOMEM;
				goto err2;
			}
			/* set private helper data if allowed. */
			if (helper->from_nlattr)
				helper->from_nlattr(helpinfo, ct);

			/* not in hash table yet so not strictly necessary */
			RCU_INIT_POINTER(help->helper, helper);
		}
	} else {
		/* try an implicit helper assignation */
		err = __nf_ct_try_assign_helper(ct, NULL, GFP_ATOMIC);
		if (err < 0)
			goto err2;
	}

	err = ctnetlink_setup_nat(ct, cda);
	if (err < 0)
		goto err2;

	nf_ct_acct_ ct->mark = ntohl(nloe(coeRut_f} else {
		/ttr p	goto err1;
			INIut_f} else {
0{
0{
		/ttr p	goto err1;
urn nf_t_f} else oto erseqadj = _t_f} else oto erseqayn	  xy_t_f} else oto-EBUSY;
32)-1 elre our fake xtensing ceq->offink_dumlper f_nat_range. */
	ct-S_EXPECTED|IP
		return 0;

	if (cda[CTA_STATUS]) {
		err = ctnetlink_change_status(ct, cda);
		if (err < 0)
			goto ;
#endif

	if (cda[CTA_SEQ_ADJ_ORIG] || cda[CTA_SEQ_ADJ_REPLY]) {
		err = ctnetlink_change_seq_adj(ct, cda);
		if (err < 0)
			goto int err	add_	    emset(tuple,add_	    protonum(;
	}

	if (cda[CTA_PROTOINFO]) {
		err = ctnetlink_change_protoinfo(ct, cda);
		if (err < 0)
			goton err;
	}

#if defined(CONFIG_NF_CONNTRACK_MARK)
	if (cda[CTA_MARK])
		ct->mark = ntohl(nla_get_be32(cda[CTA_MARK]))2;
			upchanges e our fak:);
outout:fink_dume	/* origlper assigu3);
	else if (cda[CTA_TUPNLM_F_DUMP) {   struct nf_connthanges;M_F_DUMP) {   struct nf_conntrack_tuples(sh;M_F_DUMP) {   str_tuples(sctinfo = NULL;

		err = ctnetlink_parse_tuples(e_nested(tb, CTA_TTUPLE_RElpinfo);
 		if (err < 0)
			gotuples(shrn err;

	h = nf_conntrack_find_get(n/
	if (_RElpinfuples(shrn	if (help == ULL) {
			return ENOMEM;
				go}gotuples(snode) {
			ct = nf_ct_tuplehashuples(sh(_REl_ (nfnbit

	if (d & (I_BITl, sange. */
(_RElns */
	if de)uples(sctino}goct nf_c=er;
	struct nf_rack_h	nf_ct_putct nf_TUPLEt nf_nge. rode)kfies= nto		}l_ns(to_ctrack(hhelper = __nf_f_ctINI_nfinser;
	nf_ct_put(ct);
	if (err < 0)
		go(tb, ct);
	rcu_read_unlock()ctinf 0)
put_failure:
	rcu_readP;
	:
 table */
		nf_gs[1]tTA_NAT_SRC])
		retuP;
, cb, false);
}

static intnewnetlink_get_conntrack(struct sock *ctnl, struct sk_buff *skb,
			const struct nlmsghdr *nlh,
			const struct nlattr * const cda[])
{
	struct net *net = sock_net(ctnl);
	struct nf_conn(net, zone, otuk_net(ctnl);
	struct nf_conntrack_tu_buff *skb2 = NULL;
	struct nfgenmsg *nfmsg = nlmsg_d u8 u3)
{
	struct nf_ata(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	u16 zone;
	int err;

	err = ctnetlink_parse_zone(cda[CTA_ZONE], &zone);
	if (err < 0)
		return err;

	if (cda[CTA_TPROTOINFO]) {
		err = ctnetlink_parse_tnet, zona, &tuple, CTA_TUPLE_Oct, cda);
		if (err < 0)
			return err;
	}

	if (cda[CTA_TUPROTOINFO]) {
		err = ctnetlink_parse_tret, zona, &tuple, CTA_TUPLE_REct, cda);
		if (err < 0)
			return err;
	}

	if (cda[CTA_TUPLhrn err;

	h = nf_conntrack_find_get(noet, zone,RIG, u3);
	else if (cda[CTA_TUPLEhrn err;

	h = nf_conntrack_find_get(nret, zoneOTSUPP;rn	if (help ==ULL) {
			return  cda[])
{
	if (nlh->nlmsg_flags = Nelp == U*ct,ipt
ctnetlink_connsnsignedlpinfoENOMEM);

	if  (cda[CTA_Q_AD!
	else if (cda[CTA_TUPLE of u32 */
		return hnnode) nf_conn *
ctnetlink_createk_find_get(arse_tnet, zo_l3num(ctret, zonPLE_REcP_ATOMIC);
	if (IS_ of u32 *retC);
	if urn hnroto;
	inEcP_ATOt			ibit

	if (d & (I_BITl, sange. */
((IS_ o_connsn=nts & (1 << IinEcPurn 0;
	o_connsn=nts & TNL_MnEcP_ATO;
	}

	if (cda[CT_COUUNTER) &&
		    ctnetlink_attach_labe;
		if (reo_connsnct- (events & (1 << _MnEcPn_lock_bh(&nf_conn_LABrtid, nl (events & (1IPS_ |_l3num(ct), - (events & atus & I |_l3num(ct), - (events & (1 << IPC_l3num(ct), - (events & (1 << IPCTPC_l3num(ct), - (events & (1 << IPC_l3num(ct), - (events & (1 << Insigned _l3num(ct), -_ct_delete(ct, NETLINK_CB(s_l3num(ct), -tid,
					 nlmsg_repor= NULL) {
		nf_ct_, NULL);
		}
			retu	int /* try a'urn 'RACK_=ULL) {
	EXIPS_SEnode) {
			ct = nf_ct_tuplehash_to_cENOMEa[])
{
	if (nlh->nlmsg_flaEATEelpnamePP;
		}

		spin_lock_bh(&nf_conntrack_expecINFO]) {
		err = ctnetlink_create_protoinfo(c_MODULES
		spin_unlock_bh(&nf_conntrack_expEct, cda);
		ifed long)ctck_bh(&nf_conn_LABrtid, nl (events & (1IPS_ |_l3num(ct), - (events & atus & I |_l3num(ct), - (events & (1 << IPC_l3num(ct), - (events & (1 << IC_l3num(ct), - (events & (1 << IPCTPC_l3num(ct), - (events & (1 << IPC_l3num(ct), - (events & (1 << _l3num(ct), -_ct_delete(ct, NETLINK_CB(s_l3num(ct), -tid,
					 nlmsg_repor-ENOENT NULL) {
		nf_ctnlock();

	return err;
}

static int
s = nt	pcpu ctnetlink_confirmed(struct sk_ultiNK_CB(skultinlh->nlms__n_fapcpnf_conn *ct, coipt
ctnetlink= ntuct nf_conn *ct)
;
	struct nlmsghdr *nlh;
	struct nfgenmsint type;
	unsigned iNK_CB( ?msg_flaMULTI :mset_conne;
	iuct n= 		nfnl_lock(NFNL_SUBSYS_CTNT_DON		type = IPCG_IPf (c(NFPUoto eNK << 8;
	nlh = nlmsNK_CB(skct_seiuct id, 0, type, sizeof(*nfmsg), flags);
	if (nlh == NULL)
		goto nlmsg_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgeAF_UNSd &g_data(nlh);
	nfmct), -g->version	= NFNETLINK_V0;
	nfm), -g- CTA_Zcpu_lig), flag &&
	   (nla_put_be1f (c(NSEARCHEDb, CTAlk_cadj(arINId))MASK] == 0 ||&
	   (nla_put_be1f (c(NFOUNDb, CTAlk_cadfound))MASK] == 0 ||&
	   (nla_put_be1f (c(NTNLb, CTAlk_cadnew))MASK] == 0 ||&
	   (nla_put_be1f (c(N		retIDb, CTAlk_cadin	ret ))MASK] == 0 ||&
	   (nla_put_be1f (c(N	GNOREb, CTAlk_cadierr;
))MASK] == 0 ||&
	   (nla_put_be1f (c(NTNL_MSb, CTAlk_cad))
		n))MASK] == 0 ||&
	   (nla_put_be1f (c(NTNL_MS_LIPSb, CTAlk_cad))
		netlin))MASK] == 0 ||&
	   (nla_put_be1f (c(N		SERTb, CTAlk_cadinser;))MASK] == 0 ||&
	   (nla_put_be1f (c(N		SERT_FAILEDbrom_nlCTAlk_cadinser;	gotoId))MASK] == 0 ||&
	   (nla_put_be1f (c(NDROPb, CTAlk_cad)rop))MASK] == 0 ||&
	   (nla_put_be1f (c(NEARLYNDROPb, CTAlk_cad(arly_)rop))MASK] == 0 ||&
	   (nla_put_be1f (c(NERRORb, CTAlk_cad(rror))MASK] == 0 ||&
	   (nla_put_be1f (c(NSEARCH (1f (RTbrom_nlCTAlk_cadj(arIN_0;
			gct_zone(ct))))
		goto nla_put_nlock();

	nlmsg_end(st(last);

	return	return 0;

nla_puskb, nlh);
nlmsgunlock();
	nlmsg_cancel(of u32 */1	return err;
}

static int
s = nt	pcpurn nunconfirmed(struct sk_buff *skb, struct netlink_callba	int res;
	int cpt;
	struct net *net = sock_net(skb->sk);

	if		cb=s[0]; cpu <if (!seqadj)
		rargs[1];

	for (cpu = cb->args[0]; cpu < nr_cpu_ids; _conn *ct, coipt
ctnetlink= ntuct _pcpu *pcpu;

		if (!cpu_possible(cpu))
			colock)

		pcpu = per_cpu_p= nt.pcpu_listctionsatic int
s = nt	pcpu ctnetlink_ sk_bufum(ct),_info(skb, NETLINK_CB(cb->skb).portitid,
						  cb->nlh->nlmsg_se apcpnfs_adj(skb, c	bctnk;cpu->lock);
	}		cb->argsput(last);

	return skb->len;
}

static int= nt	pt	pcpet_ct_dying(struct sock *ctnl, struct sk_buff *sk,
		       const struct nlmsghdr *nlh,
			     const struct nlattr * const cda[])
{
	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump =
s = nt	pcpurn nk_done_list,
		};
		return netlink_dump_start(ctnl, skb, nlh, &c);
	);
	return 0;
}

static int
 nt	pt	 ctnetlink_confirmed(struct sk_ultiNK_CB(skultinlh-kultitattrCOUUNTER	int cpt;
	strunf_conn *ct)
;
	struct nlmsghdr *nlh;
	struct nfgenmsint type;
	unsigned iNK_CB( ?msg_flaMULTI :mset_conne;sint type;
	un[0];n for new = {
				ilure(ntry(h, ncounf urn iuct n= 		nfnl_lock(NFNL_SUBSYS_CTNT_DON		type = IPCG_IPf (c(oto eNK << 8;
	nlh = nlmsNK_CB(skct_seiuct id, 0, type, sizeof(*nfmsg), flags);
	if (nlh == NULL)
		goto nlmsg_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgeAF_UNSd &g_data(nlh);
	nfmct), -g->version	= NFNETLINK_V0;
	nfm), -g-0lig), flag &&
	   (nla_put_be1f (c(NGLOBAL_ENTRIESb, CTAlk[0];n for newct_zone(ct))))
		goto nla_put_nlock();

	nlmsg_end(st(last);

	return	return 0;

nla_puskb, nlh);
nlmsgunlock();
	nlmsg_cancel(of u32 */1	return err;
}

static int
 nt	ptet_ct_dying(struct sock *ctnl, struct sk_buff,
		       const struct nlmsghdrr *nlh,
			const struct nlattr * const cda[])t;
	struct sk;
	u16 zone;
	 -ENOMEM;
	skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (sk
 of u32 */
		return err2;
	}

	err =  nt	pt	 ctnetlink_ast(ctnl, skb2, NETLINK_CB(s
lmsg_se			    cb->nlh->nlmsg_sseq,
				  NFNL_MSG_TYPE(nlh->n>nlmsg_snet *net = sock_nONE], &zone);
	if (err <= 0)
		goto free;

	err = netlink_unicast(ctnl, skb2, NETLINK_CB(skb).portid, MSG_DONTWAIT);
	if (err < 0)
		goto out;

	return 0;

free:
	kfree_skb(skb2);
out:
	/* this avoids a loop in nfnetlink. */
	return err == -EAGAIN ? -ENOBUFS : err;
}

static const struct ex_policy help_nla_ (d & [CTA_SEQADJ_MAX+1] (d & [CTTA_TUPLE_MASTER]	= { .type = NLA_NESTE (d & [f (cdUPLE_MASTER]	= { .type = NLA_NESTE (d & [CTA_LABELS_MASK]	= { .type = NLA_NESTE (d & [f2(cda[CSET_BEFORE]	= { .type = NLA_U3 (d & [2 },
	[CTA_ID]		= { .type = NLA_U3 (d & [ = {
	[CTA_ELP_NAME]		= { .type = NLA_NUL_STRING,
				    .len = NF_CT_HELPER_NAME_NLA_U3 (d & [D },
	CTA_ZONE]		= { .type = NLA_U1 (d & [FLAGRRECTION_POS]	= { .type = NLA_U3 (d & [CLASRRECTION_POS]	= { .type = NLA_U3 (d & [NA[CSET_BEFORE]	= { .type = NLA_NESTE (d & [FNA_HELP_NAME]		= { .type = NLAe = NLA_U32 },
et(ctnl);
	struct nf* orig ct nf_conn *_connf* orig_parse_help(const strut nlattr * c;
}

static struct nf_cct), -
et(ctnl);
	struct nf_conntrack_helnf_cct), -
et(ctnl);
	struct nf_conntrack_tuple *tupl
et(ctnl);
	struct nf_conntrVAL;
;PNOTSUPP;
}

#ifdETFILT NF_nl, skbQUEUE  ._U32 },
eK_LAB
static intnfqueue
	sild->ct._parse_help(conic struct allback *cb)3on'tlictotal->ct._0eof(u
	if  (cda[CTA|(1IP|CTTA_T nfne *tupl
+)3on'tlictotal->ct._0eof(u
	if  (cdaIP nfne *tupl
+)3on'tlictotal->ct._0eof(u
	if  (cda(1 << nfne *tupl
+)3on'tlictotal->ct._}

	len ta(nlh))eof(u
	if_u8(tb[CT nfne *tupl
+)tlictotal->ct._}

	len ta(n])
{)eof(u
	ifID nfne *tupl
+)tlictotal->ct._}

	len ta(n])
{)eof(u
	iff (cda nfne *tupl
+)tlictotal->ct._}

	len ta(n])
{)eof(u
	iff2(cda[ nfne *tupl
+)tlictotal->ct._0eof(u
	if_u8(t IPC nfne *tupl
+)tlictotal->ct._0eof(u
	ifen = nfne *tupl
+)tlictotal->ct._    .len = NF_CT_HELPeof(u
	ifen =F_CT_ nfne *tupl
+)	}

	err = cink_->ct._pt{
	if (attr)
#ifdef CONFIG_NF_N *tupl
+)2on'tlictotal->ct._0eof(u
	ifD },
	[CTA_NAT_S|(1IP nfne *tupl
+)6on'tlictotal->ct._}

	len ta(n])
{)eof(u
	ifD },
	[CCTA_SEtype _MARK]done,
		};
#ifdef CONFIG_NF_CONNT *tupl
+)tlictotal->ct._}

	len ta(n])
{)eof(u
	if_CONtype _MARK]done,
		};
#ifdef CONFIG_NF_CONNTR *tupl
+)tlictotal->ct._}

	len ta(n16
{)eof(u
	if_CONtype _MARK]e *tupl
+)	}

	err = __nf_>ct._pt{
e *tupl
 cb, false);
}

static intnfqueue
	sildunconfirmed(struct sk_buff *skic struct allbahelp(const stru nla= ctms	return err;

	rcu_rea nla= ctmsVAL;
		 nla=tlink_a_put_be1  (cda[CTAT_D { .F.type =to_cENOME nla= ctms_zone(ct))))
		goto nla_putctionsatic int ctnetnd exlmsg_ca{
			ct = e_proTION(h) != IP_CT_DIark(skb, ct) < 0)
		goto nla_pu	;
		 nla=nfnetlink_sla= ctms_put_nnla= ctmsVAL;
		 nla=tlink_a_put_be1  (cda
		/* _D { .F.type =to_cENOME nla= ctms_zone(ct))))
		goto nla_putctionsatic int ctnetnd exlmsg_ca{
			ct = e_proTION(h) !=& IPS_Srk(skb, ct) < 0)
		goto nla_pu	;
		 nla=nfnetlink_sla= ctms_put__lock(
			link_pt)

	if (mask) {&
	   16_a_put_be1_CON,- CTA_Zk(
			link_pt)
)if (err <  0)
		goto nla_pu	turn err;satic int ctneinetlinkctSrk(skb, ct) < 0)
		goto nla_purn err;satic int ctneink_chanlinkctSrk(skb, ct) < 0)
		goto nla_purn err;satic int ctneink_changlinkctSrk(skb, ct) < 0)
		goto nla_purn err;satic int ctneink_change_linkctSrk(skb, ct) < 0)
		goto nla_purn err;satic int ctnerom_nlate_linkctSrk(skb, ct) < 0)
		goto nla_purdone,
		};
#ifdef CONFIG_NFSEC_CONNTctions * ci && !(&
			.dump = ctne cink_e_linkctSrk(skb, ct) < 0)
		goto nla_pu _MARK]ections */
	if !(&
			.dump = ctne/
	if e_linkctSrk(skb, ct) < 0)
		goto nla_purn err(range. */
	&->status |= IPS)CT_CO *nlh		.dump = ctneseqadj_changlinkctSrk(skb, ct) < 0)
		goto nla_purdone,
		};
#ifdef CONFIG_NF_CONNTctions * && !(&
			.dump = ctne && e_linkctSrk(skb, ct) < 0)
		goto nla_pu _MARK]ections
	return ctnetnk_attalinkctSrk(skb, ct) < 0)
		goto nla_puG_MODULES
		rcu_read_oto out;

	return 0;

nla_puG_MODULES
		rcu_read_oto out -EASPC cb, false);
}

static intnfqueue
 ctnetig_parse_help(const strutr * c;
}

static struct const cda[])
{
	 err;
	}

	if (cda[CTA_TIMEOUT]) {
		err = ctnetlink_change_timeout(ct, cda);
		if (err < 0)
			retun err;
	}

	if (cda[CTA_STATUS]) {
		err = ctnetlink_change_status(ct, cda);
		if (err < 0)
			retuPNOTSUPP;

	if (cda[CTA_HELP]) {
		err = ctnetlink_change_helper(ct, cda);
		if (err < 0)
			retun err;
	}

	if (cda[CTA_LABELS]) {
		err = ctnetlink_attach_labels(ct, cda);
		if (err < 0)
			retun err;
	}

#if defined(CONFIG_NF_CONNTRACK_MARK)
	if (cdada[CTAype-EINVAL0TA_Lrknk_sw_Lrklistction_get_be32(cda[CTA_f (e-EINVAL~mask =
				ntohl(nla_get_be32(cda[CTA_MAR
(e-E])
		ct->mark = ntohl(nla_get_be32(cda[CT		_sw_Lrkn= 	ns * && !( VAL;

^ _Lrklistctio_sw_Lrkn!TUS]))_Lrkssible_MARK])
		csw_Lrklis filter;
	 &c);
	);
	return 0;
}

static intnfqueue
 ctne_parse_help(const struct nlabuff *skic struct allbahelp(const strua_get_be32TA_TUPLE_MA)
		return re];

	err = nla_parse_nesteCTA_SEQADJ_nla_policy ctTA_NAT_DST]);
	if (ret < 0)
		retPP;
		}

		spin_lock_bh(&nf_conntrack_expeurn ret;

	ret =nfqueue
 ctnetig__parse_help(const stru*)rse_n	nf_ct_MODULES
		spin_unlock_bh(&nf_conntrack_expecret < 0)
		rUFS : err;
}

static intnfqueue
ex_p ctne_parse_help(const stru;
}

stua_g_DIR_REPLY]drr *nlh,
			conck(struct nf_cocct), -
et(ctnl);
	struct nf_conntrack_tuplele *tupl
et(ctnl);
	struct nf_conntrVAL;
onst cda[])
{
	INFO]) {
		err = ctnetlink_parse_et, zona, & (d & [f (cduplele *tuf-nat-%u", nf_ctNE], &zone);
	if (err < 0)
		retck *cb)
{
	return ctnetlink_parse__LABELESTE (d & [CTA_uplele *tupf-nat-%u", nf_ctNE	return 0;
}

static intnfqueue
 ctnetl* orig_parse_help(const struct nlabuff *skic struct upleleultiNK_CB(skulti				 nallbahelp(const strua_get_be (d & [CTA_SEk_net(ctnl);
	struct nf_conn(et, zon_LABk_net(ctnl);
	struct nf_conntrack_hel_buff *skb2 = NULL;
	struct nf* orig c* o;
	u16 zone;
	int er];

	err = nla_parse_neste (d & [CTA_SEQADJ_ex_policy helptNE], &zone);
	if (err < 0)
		retINFO]) {
		err =nfqueue
ex_p ctne__parse_help(const stru;
}

stu)a_g_DIR_R	 -_ct_tuple(cd&VAL;
;P], &zone);
	if (err < 0)
		return err;

	i (d & [ = {
	[CTA_ids; _conn HELP]) {
		char *eplace(ct, nla_da (d & [ = {
	[CTA_		goto err2;

		helper = __nf_conntrack_helper_find(helpname, nf_ct_l3num(ct),
						    nf_ct_protonum(ct));
		if (helpf (err < 0)f
		return -EOPNOTSx= {
			.dump =_connf* orig__parse_help(const stru;
}

stu)a_g_ t nf_cocct), NTER(heltuple(cd&VAL;
;P], &zMIC);
	* o (IS_ERR(ct)retC);
	* o o_ctrack(hhelptf_conntrrest ed					 nl_comsNK_CB(sk				 na;P], &zone);
	ib2 == NULL)_conntrlh =* o o_(err < 0)
			return err;
	}

	return 0;
}

statiq		helook) {
		err =nfqueue
look)ADJ_M.	sild->ct.	]) {
		err =nfqueue
	sild->ct.,_M.	sild		]) {
		err =nfqueue
	sild,_M. ctne		]) {
		err =nfqueue
 ctne,_M. ctnetl* orig	]) {
		err =nfqueue
 ctnetl* orig,_M.adj_chausg	])a{
			ccpqadj = _ssh_r}pu _MARKof(u
}

#ifdETFILT NF_nl, skbQUEUE  .RACK_/***********************************************************************
ru; (d & 
ruuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuu/ESTED },
};

static inline intex_p ctnetnd eunconfirmed(struct skCOUUNr *nlh,
			conck(struct nf_conntrack_tupleln *ct,iglptrf* orig nlh->llbahelp(const stru nla= ctms	retnnla= ctmsVAL;
		 nla=tlink_a_putAME]	_D { .F.type =to_cENOME nla= ctms_zone(ct))))
		goto nla_putctionsatic int ctnetnd exlmsg_cet, zork(skb, ct) < 0)
		goto nla_pu	;
		 nla=nfnetlink_sla= ctms_put_oto out;

	return 0;

nla_puG_ u32 */1	return err;
}

static inline intex_p ctneVAL;unconfirmed(struct skCOUUr *nlh,
			conck(struct nf_conntrack_tuplelr *nlh,
			conck(struct nf_conneVAL;trVAL;
onst cda)
		rFO_MAX+1];
	struct nf_3onntrack3l4proto O_MAX+1];
	struct nf_conntrack_l4proto _DUMP) {   struct nf_connth;bahelp(const stru nla= ctms	retint err	m, 0xFFid, 0, tymprotointcpyr	m.src.u3cd&VAL;->src.u3cd, 0, tym.src.u3protoi.src.u._code)upl;->src.u._cootoi.dst.	    nf_de)_conn->dst.	    nf_put_nnla= ctmsVAL;
		 nla=tlink_a_put_be1 (d & [CTA_	_D { .F.type =to_cENOME nla= ctms_zone(ct))))
		goto nla_pueturn err;

	rcu_read3lock();
	l4proto 3 __nf_ct_l4_conn->src.ame, expeurn ret;

	ret = ctnetnd ex_ip_a_put	m, num(ct) A_NAT_DST])>		ifed lod_lock();
	l4proto = __nf_ct_l4_conn->src.ame, t_l3num(ct))))_conn->dst.	    nf_expeurn ret;

	ret = ctnetnd ex_	    _a_put	m, n4m(ct) A_N}go(tb, ct);
	rcu_read_uAT_D
	rikelyDST]);
	i_zone(ct))))
		goto nla_put_nl		 nla=nfnetlink_sla= ctms_put_oto out;

	return 0;

nla_puG_ u32 */1	return err;r *nlhunnfmc4pr
stf} ela hey} ela;return 0;
}

static intex_p ctne* orig_ock *ctnl, struct sk_buff r *nlh,
			conck(struct nf* orig c* o			   u8 u3)
{
	struc/
	if de)* o */
	if read	uns	u_int32_t(( (uns* o *fies + ct->timeo- ( (uns.expire) /+ time else {
			struct nf_conn_h	if (attr)
#ifdef CONFIG_NF_Nhelp(const stru nla= ctms	r _DUMP) {   struct nf_conntlink_conntADJ}pu _MARK]ege_seq_adj(stlp = nf_corigfn c* ofnad_uAT_D	u_int32k(skb, 	u_int32_t0purn err;satic intex_p ctnetnd eun_put	* o *ft, zona, & (d & [f (cdork(skb, ct) < 0)
		goto nla_pu	 err;satic intex_p ctneVAL;un_put	* o *ft, zon&* o */
	kork(skb, ct) < 0)
		goto nla_pu	 err;satic intex_p ctnetnd eun_puf_coccn/
	if  *ft, znf_cseqadj->seq[IP_CT_DI.ack_tuplele X+1] (d & [CTTA_TSrk(skb, ct) < 0)
		goto nla_purdone,
		};
#ifdef CONFIG_NF_NANOME pr
stf} ela_

	i&* o *saved	 elaon&hey} ela)MASK] == * o *saved	m(ct)._coib2 == nla= ctmsVAL;
		 nla=tlink_a_put_be1 (d & [only_D { .F.type =to_ccENOME nla= ctms_zonne(ct))))
		goto nla_put_), flag &&
	   (nla_put_be1 (d & [onl->seb, CTAlk* o *dir
)if (err <  0)
		goto nla_pu == ink_conn.src.ame, k(hhelptfame, n/
	if (_REl ink_conn.src.
	u_i* o *saved	 ela_REl ink_conn.dst.	    nf_de)
						    nf_c/
	if (_REl ink_conn.src.
u_i* o *saved	l4protoistctionsatic intex_p ctnetnd eun_put	 ink_connt_l3num(_be1 (d & [onl-f (cdork(skb,                ct) < 0)
		goto nla_pu	        nl		 nla=nfnetlink_sla= ctms_pus filter;
	, flag &&
	   (nla_put_be1 (d & [f (cda[b, CTAlktimer(&ctMASK] == 0 ||&
	   (nla_put_be1 (d & [2 b, CTAlk>args[1] = (uns* o (MASK] == 0 ||&
	   (nla_put_be1 (d & [FLAGRb, CTAlk* o *f(*nfm(MASK] == 0 ||&
	   (nla_put_be1 (d & [CLASRb, CTAlk* o *claswct_zone(ct))))
		goto nla_pu *help;

	 *help =/
	if (_REum(ct));PNLM_F_DUMP) {   struct nf_conntrack_helpegoto err2;

at_setup = rcu_df (help && heotonum(ct));
		T_COU == 0 ||&
	 yet _get_put_be1 (d & [ = {
	[CTelp->helsh_tme)if (err <  0)
		goto nla_pu	tu	* ofnp;

			help = nf_corigfnf_connby_symbolk* o *_corigfna;P], &zo ofnp!f (helCT_CO *nl0 ||&
	 yet _get_put_be1 (d & [FNJ_ex_fnsh_tme)if (e(ct))))
		goto nla_puetuto out;

	return 0;

nla_puG_ u32 */1	return err;
}

static intex_p ctnetlink_confirmed(struct sk_ultiNK_CB(skultinlh->nlmu16 zuct idr *nlh,
			conck(struct nf* orig c* o			   u8 u3)
;
	struct nlmsghdr *nlh;
	struct nfgenmsint type;
	unsigned iNK_CB( ?msg_flaMULTI :msurn iuct n|= 	nfnl_lock(NFNL_SUBSY1 (dS_CTNto eNK << 8;
	nlh = nlmsNK_CB(skct_seiuct id, 0, type, sizeof(*nfmsg), flags);
	if (nlh == NULL)
		goto nlmsg_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfge* o *ft, z.src.ame, g_data(nlh);
	nfmm), -g->version	= NFNETLINK_V0;
	nfm), -g-0lig), flstatic intex_p ctne* orig_onlms* o	rk(skb, ct) < 0)
		goto nla_purnnlock();

	nlmsg_end(st(last);

	return	reb, nlh);
nlmsreturn 0;

nla_puGunlock();
	nlmsg_cancel(of u32 */1	retudone,
		};
#ifdef CONFIG_NFEVENTSurn err;
}

static intex_nntriuct (int type;
	unsigned h,
			conckex_piuct n*itemconst cda[])
{
	struct nf* orig c* o-g-item *_co;;
	int cpt;
	struct  NULL)_co*net * o o_( u8 u3)
;
	struct nlmsghdr *nlh;
	struct nfgenms_confirmed(struct snmsint type;
	untattr grorse_nat_ssigned i0lig), fl_connsn&- (events (d_DypeROY)

	if AME]		=ON		type =  (d_DyL_MSotongrors-g->veLGRPf CONFIG_NFE(d_DypeROY;elper);
	, fl_connsn&- (events (d_NEW)

	if AME]		=ON		type =  (d_TNL_M		signed isg_flags = N|sg_flaEATEotongrors-g->veLGRPf CONFIG_NFE(d_TNL_M	eturn 0;
!seqadj)
		retur!item *				 nTA_NAk();
		if nf_etlino(&csek_fingrors (IS_ERR(ct)0e;
	 -EOMEM;
	skb2 = nlmsg_new(NLMSG_DEFAULTtuple, GFP_ATO -EOM
	if (nlh == NU(rro0)
		gAME]	_= 	nfnl_lock(NFNL_SUBSY1 (dS_CTNto eNK << 8;
	nlh = nlmsitem *NK_CB(sk0,ntattr , 0, type, sizeof(*nfmsg), flags);
	if (nlh == NULL)
		goto nlmsg_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfge* o *ft, z.src.ame, g_data(nlh);
	nfmm), -g->version	= NFNETLINK_V0;
	nfm), -g-0lig)urn err;

	rcu_rea, flstatic intex_p ctne* orig_onlms* o	rk(skb, ct) < 0)
		goto nla_puo(tb, ct);
	rcu_read_unlock();

	nlmsg_end(sk();
		if snfnetlink_stmsitem *NK_CB(skgrorsmsitem *				 nEFAULTtuple, GFPoto out;

	return 0;

nla_puG_MODULES
		rcu_read_unlock();
	nlmsg_cancel(skb, nlh);
nlmsg0;

free:
	kfeadP;
skb(skk();
		if snL)_rrek_fin0{
0{
 -EAGAIN GFPoto out;

 filter;
 : err;
}

static intex_p ink_buff *skb, struct netlink_callba	b->sk);

	if1A_f ( NULL)_conntrlh =( cda[])
{
	struct nf* orig ct nf_conn *) GFPoto out;

 feturn 0;
}

static intex_p ctneot inunconfirmed(struct sk_buff *skb, struct netlink_callba	int cpt;
	struct net *net = sock_nett cda[])
{
	struct nf* orig c* o, *	nf_msghdr *nlh;
	struct nfgere;

	nfmsg = d,
				nettata(nlh);d3lock();
nt8_t u3 = nfmsg->nfgeturn err;

	rcu_readalock)( cda[])
{
	struct nf* orig ct nf_conn *);	rargs[;	for (cpu = rgs[NULL)_conntrh, 0,;	for (cpu = pu_ids0;
			g:gototlin_arg_enetl*->nal_comsntry(h, n_conntrhf_csfor (cpu = T_DIR_REPLY]hnodefed lon	b->d3lock()A_N* o *ft, z.src.ame, BELS_um(ct) DIR_Rcse))
					co	b->sk);

	if1A_f (res < 0)* o-ELS_out:
	iR_Rcse))
					cogs[1]) {
			cb->args[}		co	b->static intex_p ctnetlink_ sk_bufum(ct),_info(skb, NETLINK_CB(cb->skb).portitid,
						  cb->nlh->nlmsg_se aON		type =  (d_TNL->nlmsg_se a* o	rk(skf (res < 0) {
				if (!atomic_inc* o *ct_general.use))
					continue;
				cb->args[1] = (uns_co;;
 (err < ;
				goto out;
			}
		}
		if (cb->args[1]) {
			cb->args[1] = 0;
			goto res}_skb(sk_MODULES
		rcu_read_	b->dout:
	if (las_conntrlh =	nf_ct_put(last);

	return skb->len;
}

static intex_petlictneot inunconfirmed(struct sk_buff *skb, struct netlink_callba	int cpt{
	struct nf* orig c* o, *	nf_msghdr *nlh;
	struct nfgere;

	nfmsg = d,
				nettbuff *skic struct 

	for sg =ime else {
			struct nf_connp;

	 *help =	nf_ctata(nlh);d3lock();
nt8_t u3 = nfmsg->nfgetkb->sk);

	if		(IS_ERR(ct)0e;
	urn err;

	rcu_readalock)( cda[])
{
	struct nf* orig ct nf_conn *);	0;
			g:gootlin_arg_enetl*->nal_comsnf (hel_ct_remove_e, lnodefed lo	b->d3lock()A_N* o *ft, z.src.ame, BELS_um(ct) DIR_use))
					c			}
		}
		if (cb->arg< 0)* o-ELS_out:
	iR_use))
					cos[1]) {
			cb->argsut;
			}
tatic intex_p ctnetlink_ sk,_info(skb, NETLINK_CB(cb->skb).potitid,
						  cb->nlh->nlmsgse aON		type =  (d_TNL->nlmsgse a* o	rk(skf (res< 0) {
				if (!atomic_inc* o *ct_generaluse))
					cos[1]) {
			cb->args[1] = (uns_co;;
 (rr < ;
				g}retun err;		}
		if (cb->ars[1]) {
			cb->args1] = 0;
			gotou->lock);
	}		cb-1;_skb(sk_MODULES
		rcu_read_	b->dout:
	if (las_conntrlh =	nf_ct_put(last);

	return skb->len;
}
et;

	ret = ctneex_pett_conntrack(struct sock *ctnl, struct sk_bufonn *ct,
			 const struct nlmsghdonn *ct,
			 const struct nlattr * constt attr)
{
	int err{
	struct net *net = sock_net(ctnl);
	struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u_int8_t u3 = nfmsg->nfgeet(ctnl);
	struct nf_conn(et, zfgeet(ctnl);
	struct nf_conntrack_tuettbuff *skic struct ;gen_familycb->arg_DUMP) {
		struct netlink_dump_contrl c = {
			.dump =ex_petlictneot in,ntrl ilycb-static intex_p ink,nt}
{
	INFO]) {
		err = ctnetlink_parse_&ft, zona, & (d & [ CTA_TTUPLE_RE, &zone);
	if (err < 0)
		return err;

	i (d & [D },
UPROTOINFO]) {
		err = ctnetlink_parse_zo (d & [D },
a[CTA_ZONE]ct, cda);
		if (err < 0)
			returnhrn err;

	h = nf_conntrack_find_get(net, zone,< 0) hk
 of u32 */
		returSEnode) {
			ct = nf_ct_tuplehash_to_cc.ate h=)ctinfoto free;

	errrn netlink_dump_start(ctnl, skb,  NULL) {
		nf_cctnlock();

	return err;
}

static int nto* orig_ock *ctnng(struct sock *ctnl, struct sk_buff *s,
		       const struct nlmsghdr *nr *nlh,
			const struct nlattr * const cda[])
{
	struct net *net = sock_net(ctnl);
	struct nf_conn(et, zfgeet(ctnl);
	struct nf* orig c* o;
	 cda[])t;
	struct sk;
	et(ctnl);
	struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	u16 zone;
	 cda[])
{
	if (nlh->nlmsg_flags & NLM_Furn err;

	i (d & [[CTA_TUPLE_tck *cb)
{
	return ctneex_pettdump_start(ctnl,labels(c err1;
		} else {

		struct netlink_dump_controrl c = {
			.dump =ex_pictneot in,ntrtrl ilycb-static intex_p ink,ntdone_liist,
		};
		return netlink_dump_start(ctnl, skb, r-ENOENTINFO]) {
		err = ctnetlink_parse_zo (d & [D },
a[CTA_ZONE], &zone);
	if (err < 0)
		return err;

	i (d & [f (cdU)OTOINFO]) {
		err = ctnetlink_parse_&ft, zona, & (d & [f (cduUPLE_RERIG, u3);
	else i (d & [[CTA_TUPLE_INFO]) {
		err = ctnetlink_parse_&ft, zona, & (d & [ CTA_TTUPLE_REurn 0;
!seqadj/
		return -EINone);
	if (err < 0)
		retIxlp;

			he_conntr_conntrack_find_get(net, zone,< 0) * o		 of u32 */
		returSEu3);
	else i (d & [ID
UPROTO_   (n idVAL;
		 ntohl(nla_get_be (d & [ID
UNE]ct, cct->maidABELSn /= >args[1] = (uns* o ed long)ctL)_conntrlh =* o o_(eof u32 */
		retur r-ENOENTINFO])
				err = -ENOMEM;
	skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb2 == NULL)_conntrlh =* o o_(err < ;
				};
	urn err;

	rcu_reaINFO]) {
		err =ex_p ctnetlink_ st(ctnl, skb2, NETLINK_CB(s
lmsg      nl			  cb->nlh-aON		type =  (d_TNL- * o o_(_MODULES
		rcu_read_uNULL)_conntrlh =* o o_(, &zone);
	if (