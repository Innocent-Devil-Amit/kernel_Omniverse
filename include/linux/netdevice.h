/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the Interfaces handler.
 *
 * Version:	@(#)dev.h	1.0.10	08/12/93
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Donald J. Becker, <becker@cesdis.gsfc.nasa.gov>
 *		Alan Cox, <alan@lxorguk.ukuu.org.uk>
 *		Bjorn Ekwall. <bj0rn@blox.se>
 *              Pekka Riikonen <priikone@poseidon.pspt.fi>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *		Moved to /usr/include/linux for NET3
 */
#ifndef _LINUX_NETDEVICE_H
#define _LINUX_NETDEVICE_H

#include <linux/pm_qos.h>
#include <linux/timer.h>
#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/atomic.h>
#include <linux/prefetch.h>
#include <asm/cache.h>
#include <asm/byteorder.h>

#include <linux/percpu.h>
#include <linux/rculist.h>
#include <linux/dmaengine.h>
#include <linux/workqueue.h>
#include <linux/dynamic_queue_limits.h>

#include <linux/ethtool.h>
#include <net/net_namespace.h>
#include <net/dsa.h>
#ifdef CONFIG_DCB
#include <net/dcbnl.h>
#endif
#include <net/netprio_cgroup.h>

#include <linux/netdev_features.h>
#include <linux/neighbour.h>
#include <uapi/linux/netdevice.h>

struct netpoll_info;
struct device;
struct phy_device;
/* 802.11 specific */
struct wireless_dev;

void netdev_set_default_ethtool_ops(struct net_device *dev,
				    const struct ethtool_ops *ops);

/* Backlog congestion levels */
#define NET_RX_SUCCESS		0	/* keep 'em coming, baby */
#define NET_RX_DROP		1	/* packet dropped */

/*
 * Transmit return codes: transmit return codes originate from three different
 * namespaces:
 *
 * - qdisc return codes
 * - driver transmit return codes
 * - errno values
 *
 * Drivers are allowed to return any one of those in their hard_start_xmit()
 * function. Real network devices commonly used with qdiscs should only return
 * the driver transmit return codes though - when qdiscs are used, the actual
 * transmission happens asynchronously, so the value is not propagated to
 * higher layers. Virtual network devices transmit synchronously, in this case
 * the driver transmit return codes are consumed by dev_queue_xmit(), all
 * others are propagated to higher layers.
 */

/* qdisc ->enqueue() return codes. */
#define NET_XMIT_SUCCESS	0x00
#define NET_XMIT_DROP		0x01	/* skb dropped			*/
#define NET_XMIT_CN		0x02	/* congestion notification	*/
#define NET_XMIT_POLICED	0x03	/* skb is shot by police	*/
#define NET_XMIT_MASK		0x0f	/* qdisc flags in net/sch_generic.h */

/* NET_XMIT_CN is special. It does not guarantee that this packet is lost. It
 * indicates that the device will soon be dropping packets, or already drops
 * some packets of the same priority; prompting us to send less aggressively. */
#define net_xmit_eval(e)	((e) == NET_XMIT_CN ? 0 : (e))
#define net_xmit_errno(e)	((e) != NET_XMIT_CN ? -ENOBUFS : 0)

/* Driver transmit return codes */
#define NETDEV_TX_MASK		0xf0

enum netdev_tx {
	__NETDEV_TX_MIN	 = INT_MIN,	/* make sure enum is signed */
	NETDEV_TX_OK	 = 0x00,	/* driver took care of packet */
	NETDEV_TX_BUSY	 = 0x10,	/* driver tx path was busy*/
	NETDEV_TX_LOCKED = 0x20,	/* driver tx lock was already taken */
};
typedef enum netdev_tx netdev_tx_t;

/*
 * Current order: NETDEV_TX_MASK > NET_XMIT_MASK >= 0 is significant;
 * hard_start_xmit() return < NET_XMIT_MASK means skb was consumed.
 */
static inline bool dev_xmit_complete(int rc)
{
	/*
	 * Positive cases with an skb consumed by a driver:
	 * - successful transmission (rc == NETDEV_TX_OK)
	 * - error while transmitting (rc < 0)
	 * - error while queueing to a different device (rc & NET_XMIT_MASK)
	 */
	if (likely(rc < NET_XMIT_MASK))
		return true;

	return false;
}

/*
 *	Compute the worst case header length according to the protocols
 *	used.
 */

#if defined(CONFIG_WLAN) || IS_ENABLED(CONFIG_AX25)
# if defined(CONFIG_MAC80211_MESH)
#  define LL_MAX_HEADER 128
# else
#  define LL_MAX_HEADER 96
# endif
#else
# define LL_MAX_HEADER 32
#endif

#if !IS_ENABLED(CONFIG_NET_IPIP) && !IS_ENABLED(CONFIG_NET_IPGRE) && \
    !IS_ENABLED(CONFIG_IPV6_SIT) && !IS_ENABLED(CONFIG_IPV6_TUNNEL)
#define MAX_HEADER LL_MAX_HEADER
#else
#define MAX_HEADER (LL_MAX_HEADER + 48)
#endif

/*
 *	Old network device statistics. Fields are native words
 *	(unsigned long) so they can be read and written atomically.
 */

struct net_device_stats {
	unsigned long	rx_packets;
	unsigned long	tx_packets;
	unsigned long	rx_bytes;
	unsigned long	tx_bytes;
	unsigned long	rx_errors;
	unsigned long	tx_errors;
	unsigned long	rx_dropped;
	unsigned long	tx_dropped;
	unsigned long	multicast;
	unsigned long	collisions;
	unsigned long	rx_length_errors;
	unsigned long	rx_over_errors;
	unsigned long	rx_crc_errors;
	unsigned long	rx_frame_errors;
	unsigned long	rx_fifo_errors;
	unsigned long	rx_missed_errors;
	unsigned long	tx_aborted_errors;
	unsigned long	tx_carrier_errors;
	unsigned long	tx_fifo_errors;
	unsigned long	tx_heartbeat_errors;
	unsigned long	tx_window_errors;
	unsigned long	rx_compressed;
	unsigned long	tx_compressed;
};


#include <linux/cache.h>
#include <linux/skbuff.h>

#ifdef CONFIG_RPS
#include <linux/static_key.h>
extern struct static_key rps_needed;
#endif

struct neighbour;
struct neigh_parms;
struct sk_buff;

struct netdev_hw_addr {
	struct list_head	list;
	unsigned char		addr[MAX_ADDR_LEN];
	unsigned char		type;
#define NETDEV_HW_ADDR_T_LAN		1
#define NETDEV_HW_ADDR_T_SAN		2
#define NETDEV_HW_ADDR_T_SLAVE		3
#define NETDEV_HW_ADDR_T_UNICAST	4
#define NETDEV_HW_ADDR_T_MULTICAST	5
	bool			global_use;
	int			sync_cnt;
	int			refcount;
	int			synced;
	struct rcu_head		rcu_head;
};

struct netdev_hw_addr_list {
	struct list_head	list;
	int			count;
};

#define netdev_hw_addr_list_count(l) ((l)->count)
#define netdev_hw_addr_list_empty(l) (netdev_hw_addr_list_count(l) == 0)
#define netdev_hw_addr_list_for_each(ha, l) \
	list_for_each_entry(ha, &(l)->list, list)

#define netdev_uc_count(dev) netdev_hw_addr_list_count(&(dev)->uc)
#define netdev_uc_empty(dev) netdev_hw_addr_list_empty(&(dev)->uc)
#define netdev_for_each_uc_addr(ha, dev) \
	netdev_hw_addr_list_for_each(ha, &(dev)->uc)

#define netdev_mc_count(dev) netdev_hw_addr_list_count(&(dev)->mc)
#define netdev_mc_empty(dev) netdev_hw_addr_list_empty(&(dev)->mc)
#define netdev_for_each_mc_addr(ha, dev) \
	netdev_hw_addr_list_for_each(ha, &(dev)->mc)

struct hh_cache {
	u16		hh_len;
	u16		__pad;
	seqlock_t	hh_lock;

	/* cached hardware header; allow for machine alignment needs.        */
#define HH_DATA_MOD	16
#define HH_DATA_OFF(__len) \
	(HH_DATA_MOD - (((__len - 1) & (HH_DATA_MOD - 1)) + 1))
#define HH_DATA_ALIGN(__len) \
	(((__len)+(HH_DATA_MOD-1))&~(HH_DATA_MOD - 1))
	unsigned long	hh_data[HH_DATA_ALIGN(LL_MAX_HEADER) / sizeof(long)];
};

/* Reserve HH_DATA_MOD byte aligned hard_header_len, but at least that much.
 * Alternative is:
 *   dev->hard_header_len ? (dev->hard_header_len +
 *                           (HH_DATA_MOD - 1)) & ~(HH_DATA_MOD - 1) : 0
 *
 * We could use other alignment values, but we must maintain the
 * relationship HH alignment <= LL alignment.
 */
#define LL_RESERVED_SPACE(dev) \
	((((dev)->hard_header_len+(dev)->needed_headroom)&~(HH_DATA_MOD - 1)) + HH_DATA_MOD)
#define LL_RESERVED_SPACE_EXTRA(dev,extra) \
	((((dev)->hard_header_len+(dev)->needed_headroom+(extra))&~(HH_DATA_MOD - 1)) + HH_DATA_MOD)

struct header_ops {
	int	(*create) (struct sk_buff *skb, struct net_device *dev,
			   unsigned short type, const void *daddr,
			   const void *saddr, unsigned int len);
	int	(*parse)(const struct sk_buff *skb, unsigned char *haddr);
	int	(*rebuild)(struct sk_buff *skb);
	int	(*cache)(const struct neighbour *neigh, struct hh_cache *hh, __be16 type);
	void	(*cache_update)(struct hh_cache *hh,
				const struct net_device *dev,
				const unsigned char *haddr);
};

/* These flag bits are private to the generic network queueing
 * layer, they may not be explicitly referenced by any other
 * code.
 */

enum netdev_state_t {
	__LINK_STATE_START,
	__LINK_STATE_PRESENT,
	__LINK_STATE_NOCARRIER,
	__LINK_STATE_LINKWATCH_PENDING,
	__LINK_STATE_DORMANT,
};


/*
 * This structure holds at boot time configured netdevice settings. They
 * are then used in the device probing.
 */
struct netdev_boot_setup {
	char name[IFNAMSIZ];
	struct ifmap map;
};
#define NETDEV_BOOT_SETUP_MAX 8

int __init netdev_boot_setup(char *str);

/*
 * Structure for NAPI scheduling similar to tasklet but with weighting
 */
struct napi_struct {
	/* The poll_list must only be managed by the entity which
	 * changes the state of the NAPI_STATE_SCHED bit.  This means
	 * whoever atomically sets that bit can add this napi_struct
	 * to the per-cpu poll_list, and whoever clears that bit
	 * can remove from the list right before clearing the bit.
	 */
	struct list_head	poll_list;

	unsigned long		state;
	int			weight;
	unsigned int		gro_count;
	int			(*poll)(struct napi_struct *, int);
#ifdef CONFIG_NETPOLL
	spinlock_t		poll_lock;
	int			poll_owner;
#endif
	struct net_device	*dev;
	struct sk_buff		*gro_list;
	struct sk_buff		*skb;
	struct list_head	dev_list;
	struct hlist_node	napi_hash_node;
	unsigned int		napi_id;
};

enum {
	NAPI_STATE_SCHED,	/* Poll is scheduled */
	NAPI_STATE_DISABLE,	/* Disable pending */
	NAPI_STATE_NPSVC,	/* Netpoll - don't dequeue from poll_list */
	NAPI_STATE_HASHED,	/* In NAPI hash */
};

enum gro_result {
	GRO_MERGED,
	GRO_MERGED_FREE,
	GRO_HELD,
	GRO_NORMAL,
	GRO_DROP,
};
typedef enum gro_result gro_result_t;

/*
 * enum rx_handler_result - Possible return values for rx_handlers.
 * @RX_HANDLER_CONSUMED: skb was consumed by rx_handler, do not process it
 * further.
 * @RX_HANDLER_ANOTHER: Do another round in receive path. This is indicated in
 * case skb->dev was changed by rx_handler.
 * @RX_HANDLER_EXACT: Force exact delivery, no wildcard.
 * @RX_HANDLER_PASS: Do nothing, passe the skb as if no rx_handler was called.
 *
 * rx_handlers are functions called from inside __netif_receive_skb(), to do
 * special processing of the skb, prior to delivery to protocol handlers.
 *
 * Currently, a net_device can only have a single rx_handler registered. Trying
 * to register a second rx_handler will return -EBUSY.
 *
 * To register a rx_handler on a net_device, use netdev_rx_handler_register().
 * To unregister a rx_handler on a net_device, use
 * netdev_rx_handler_unregister().
 *
 * Upon return, rx_handler is expected to tell __netif_receive_skb() what to
 * do with the skb.
 *
 * If the rx_handler consumed to skb in some way, it should return
 * RX_HANDLER_CONSUMED. This is appropriate when the rx_handler arranged for
 * the skb to be delivered in some other ways.
 *
 * If the rx_handler changed skb->dev, to divert the skb to another
 * net_device, it should return RX_HANDLER_ANOTHER. The rx_handler for the
 * new device will be called if it exists.
 *
 * If the rx_handler consider the skb should be ignored, it should return
 * RX_HANDLER_EXACT. The skb will only be delivered to protocol handlers that
 * are registered on exact device (ptype->dev == skb->dev).
 *
 * If the rx_handler didn't changed skb->dev, but want the skb to be normally
 * delivered, it should return RX_HANDLER_PASS.
 *
 * A device without a registered rx_handler will behave as if rx_handler
 * returned RX_HANDLER_PASS.
 */

enum rx_handler_result {
	RX_HANDLER_CONSUMED,
	RX_HANDLER_ANOTHER,
	RX_HANDLER_EXACT,
	RX_HANDLER_PASS,
};
typedef enum rx_handler_result rx_handler_result_t;
typedef rx_handler_result_t rx_handler_func_t(struct sk_buff **pskb);

void __napi_schedule(struct napi_struct *n);

static inline bool napi_disable_pending(struct napi_struct *n)
{
	return test_bit(NAPI_STATE_DISABLE, &n->state);
}

/**
 *	napi_schedule_prep - check if napi can be scheduled
 *	@n: napi context
 *
 * Test if NAPI routine is already running, and if not mark
 * it as running.  This is used as a condition variable
 * insure only one NAPI poll instance runs.  We also make
 * sure there is no pending NAPI disable.
 */
static inline bool napi_schedule_prep(struct napi_struct *n)
{
	return !napi_disable_pending(n) &&
		!test_and_set_bit(NAPI_STATE_SCHED, &n->state);
}

/**
 *	napi_schedule - schedule NAPI poll
 *	@n: napi context
 *
 * Schedule NAPI poll routine to be called if it is not already
 * running.
 */
static inline void napi_schedule(struct napi_struct *n)
{
	if (napi_schedule_prep(n))
		__napi_schedule(n);
}

/* Try to reschedule poll. Called by dev->poll() after napi_complete().  */
static inline bool napi_reschedule(struct napi_struct *napi)
{
	if (napi_schedule_prep(napi)) {
		__napi_schedule(napi);
		return true;
	}
	return false;
}

/**
 *	napi_complete - NAPI processing complete
 *	@n: napi context
 *
 * Mark NAPI processing as complete.
 */
void __napi_complete(struct napi_struct *n);
void napi_complete(struct napi_struct *n);

/**
 *	napi_by_id - lookup a NAPI by napi_id
 *	@napi_id: hashed napi_id
 *
 * lookup @napi_id in napi_hash table
 * must be called under rcu_read_lock()
 */
struct napi_struct *napi_by_id(unsigned int napi_id);

/**
 *	napi_hash_add - add a NAPI to global hashtable
 *	@napi: napi context
 *
 * generate a new napi_id and store a @napi under it in napi_hash
 */
void napi_hash_add(struct napi_struct *napi);

/**
 *	napi_hash_del - remove a NAPI from global table
 *	@napi: napi context
 *
 * Warning: caller must observe rcu grace period
 * before freeing memory containing @napi
 */
void napi_hash_del(struct napi_struct *napi);

/**
 *	napi_disable - prevent NAPI from scheduling
 *	@n: napi context
 *
 * Stop NAPI from being scheduled on this context.
 * Waits till any outstanding processing completes.
 */
static inline void napi_disable(struct napi_struct *n)
{
	might_sleep();
	set_bit(NAPI_STATE_DISABLE, &n->state);
	while (test_and_set_bit(NAPI_STATE_SCHED, &n->state))
		msleep(1);
	clear_bit(NAPI_STATE_DISABLE, &n->state);
}

/**
 *	napi_enable - enable NAPI scheduling
 *	@n: napi context
 *
 * Resume NAPI from being scheduled on this context.
 * Must be paired with napi_disable.
 */
static inline void napi_enable(struct napi_struct *n)
{
	BUG_ON(!test_bit(NAPI_STATE_SCHED, &n->state));
	smp_mb__before_atomic();
	clear_bit(NAPI_STATE_SCHED, &n->state);
}

#ifdef CONFIG_SMP
/**
 *	napi_synchronize - wait until NAPI is not running
 *	@n: napi context
 *
 * Wait until NAPI is done being scheduled on this context.
 * Waits till any outstanding processing completes but
 * does not disable future activations.
 */
static inline void napi_synchronize(const struct napi_struct *n)
{
	while (test_bit(NAPI_STATE_SCHED, &n->state))
		msleep(1);
}
#else
# define napi_synchronize(n)	barrier()
#endif

enum netdev_queue_state_t {
	__QUEUE_STATE_DRV_XOFF,
	__QUEUE_STATE_STACK_XOFF,
	__QUEUE_STATE_FROZEN,
};

#define QUEUE_STATE_DRV_XOFF	(1 << __QUEUE_STATE_DRV_XOFF)
#define QUEUE_STATE_STACK_XOFF	(1 << __QUEUE_STATE_STACK_XOFF)
#define QUEUE_STATE_FROZEN	(1 << __QUEUE_STATE_FROZEN)

#define QUEUE_STATE_ANY_XOFF	(QUEUE_STATE_DRV_XOFF | QUEUE_STATE_STACK_XOFF)
#define QUEUE_STATE_ANY_XOFF_OR_FROZEN (QUEUE_STATE_ANY_XOFF | \
					QUEUE_STATE_FROZEN)
#define QUEUE_STATE_DRV_XOFF_OR_FROZEN (QUEUE_STATE_DRV_XOFF | \
					QUEUE_STATE_FROZEN)

/*
 * __QUEUE_STATE_DRV_XOFF is used by drivers to stop the transmit queue.  The
 * netif_tx_* functions below are used to manipulate this flag.  The
 * __QUEUE_STATE_STACK_XOFF flag is used by the stack to stop the transmit
 * queue independently.  The netif_xmit_*stopped functions below are called
 * to check if the queue has been stopped by the driver or stack (either
 * of the XOFF bits are set in the state).  Drivers should not need to call
 * netif_xmit*stopped functions, they should only be using netif_tx_*.
 */

struct netdev_queue {
/*
 * read mostly part
 */
	struct net_device	*dev;
	struct Qdisc __rcu	*qdisc;
	struct Qdisc		*qdisc_sleeping;
#ifdef CONFIG_SYSFS
	struct kobject		kobj;
#endif
#if defined(CONFIG_XPS) && defined(CONFIG_NUMA)
	int			numa_node;
#endif
/*
 * write mostly part
 */
	spinlock_t		_xmit_lock ____cacheline_aligned_in_smp;
	int			xmit_lock_owner;
	/*
	 * please use this field instead of dev->trans_start
	 */
	unsigned long		trans_start;

	/*
	 * Number of TX timeouts for this queue
	 * (/sys/class/net/DEV/Q/trans_timeout)
	 */
	unsigned long		trans_timeout;

	unsigned long		state;

#ifdef CONFIG_BQL
	struct dql		dql;
#endif
} ____cacheline_aligned_in_smp;

static inline int netdev_queue_numa_node_read(const struct netdev_queue *q)
{
#if defined(CONFIG_XPS) && defined(CONFIG_NUMA)
	return q->numa_node;
#else
	return NUMA_NO_NODE;
#endif
}

static inline void netdev_queue_numa_node_write(struct netdev_queue *q, int node)
{
#if defined(CONFIG_XPS) && defined(CONFIG_NUMA)
	q->numa_node = node;
#endif
}

#ifdef CONFIG_RPS
/*
 * This structure holds an RPS map which can be of variable length.  The
 * map is an array of CPUs.
 */
struct rps_map {
	unsigned int len;
	struct rcu_head rcu;
	u16 cpus[0];
};
#define RPS_MAP_SIZE(_num) (sizeof(struct rps_map) + ((_num) * sizeof(u16)))

/*
 * The rps_dev_flow structure contains the mapping of a flow to a CPU, the
 * tail pointer for that CPU's input queue at the time of last enqueue, and
 * a hardware filter index.
 */
struct rps_dev_flow {
	u16 cpu;
	u16 filter;
	unsigned int last_qtail;
};
#define RPS_NO_FILTER 0xffff

/*
 * The rps_dev_flow_table structure contains a table of flow mappings.
 */
struct rps_dev_flow_table {
	unsigned int mask;
	struct rcu_head rcu;
	struct rps_dev_flow flows[0];
};
#define RPS_DEV_FLOW_TABLE_SIZE(_num) (sizeof(struct rps_dev_flow_table) + \
    ((_num) * sizeof(struct rps_dev_flow)))

/*
 * The rps_sock_flow_table contains mappings of flows to the last CPU
 * on which they were processed by the application (set in recvmsg).
 */
struct rps_sock_flow_table {
	unsigned int mask;
	u16 ents[0];
};
#define	RPS_SOCK_FLOW_TABLE_SIZE(_num) (sizeof(struct rps_sock_flow_table) + \
    ((_num) * sizeof(u16)))

#define RPS_NO_CPU 0xffff

static inline void rps_record_sock_flow(struct rps_sock_flow_table *table,
					u32 hash)
{
	if (table && hash) {
		unsigned int cpu, index = hash & table->mask;

		/* We only give a hint, preemption can change cpu under us */
		cpu = raw_smp_processor_id();

		if (table->ents[index] != cpu)
			table->ents[index] = cpu;
	}
}

static inline void rps_reset_sock_flow(struct rps_sock_flow_table *table,
				       u32 hash)
{
	if (table && hash)
		table->ents[hash & table->mask] = RPS_NO_CPU;
}

extern struct rps_sock_flow_table __rcu *rps_sock_flow_table;

#ifdef CONFIG_RFS_ACCEL
bool rps_may_expire_flow(struct net_device *dev, u16 rxq_index, u32 flow_id,
			 u16 filter_id);
#endif
#endif /* CONFIG_RPS */

/* This structure contains an instance of an RX queue. */
struct netdev_rx_queue {
#ifdef CONFIG_RPS
	struct rps_map __rcu		*rps_map;
	struct rps_dev_flow_table __rcu	*rps_flow_table;
#endif
	struct kobject			kobj;
	struct net_device		*dev;
} ____cacheline_aligned_in_smp;

/*
 * RX queue sysfs structures and functions.
 */
struct rx_queue_attribute {
	struct attribute attr;
	ssize_t (*show)(struct netdev_rx_queue *queue,
	    struct rx_queue_attribute *attr, char *buf);
	ssize_t (*store)(struct netdev_rx_queue *queue,
	    struct rx_queue_attribute *attr, const char *buf, size_t len);
};

#ifdef CONFIG_XPS
/*
 * This structure holds an XPS map which can be of variable length.  The
 * map is an array of queues.
 */
struct xps_map {
	unsigned int len;
	unsigned int alloc_len;
	struct rcu_head rcu;
	u16 queues[0];
};
#define XPS_MAP_SIZE(_num) (sizeof(struct xps_map) + ((_num) * sizeof(u16)))
#define XPS_MIN_MAP_ALLOC ((L1_CACHE_BYTES - sizeof(struct xps_map))	\
    / sizeof(u16))

/*
 * This structure holds all XPS maps for device.  Maps are indexed by CPU.
 */
struct xps_dev_maps {
	struct rcu_head rcu;
	struct xps_map __rcu *cpu_map[0];
};
#define XPS_DEV_MAPS_SIZE (sizeof(struct xps_dev_maps) +		\
    (nr_cpu_ids * sizeof(struct xps_map *)))
#endif /* CONFIG_XPS */

#define TC_MAX_QUEUE	16
#define TC_BITMASK	15
/* HW offloaded queuing disciplines txq count and offset maps */
struct netdev_tc_txq {
	u16 count;
	u16 offset;
};

#if defined(CONFIG_FCOE) || defined(CONFIG_FCOE_MODULE)
/*
 * This structure is to hold information about the device
 * configured to run FCoE protocol stack.
 */
struct netdev_fcoe_hbainfo {
	char	manufacturer[64];
	char	serial_number[64];
	char	hardware_version[64];
	char	driver_version[64];
	char	optionrom_version[64];
	char	firmware_version[64];
	char	model[256];
	char	model_description[256];
};
#endif

#define MAX_PHYS_PORT_ID_LEN 32

/* This structure holds a unique identifier to identify the
 * physical port used by a netdevice.
 */
struct netdev_phys_port_id {
	unsigned char id[MAX_PHYS_PORT_ID_LEN];
	unsigned char id_len;
};

typedef u16 (*select_queue_fallback_t)(struct net_device *dev,
				       struct sk_buff *skb);

/*
 * This structure defines the management hooks for network devices.
 * The following hooks can be defined; unless noted otherwise, they are
 * optional and can be filled with a null pointer.
 *
 * int (*ndo_init)(struct net_device *dev);
 *     This function is called once when network device is registered.
 *     The network device can use this to any late stage initializaton
 *     or semantic validattion. It can fail with an error code which will
 *     be propogated back to register_netdev
 *
 * void (*ndo_uninit)(struct net_device *dev);
 *     This function is called when device is unregistered or when registration
 *     fails. It is not called if init fails.
 *
 * int (*ndo_open)(struct net_device *dev);
 *     This function is called when network device transistions to the up
 *     state.
 *
 * int (*ndo_stop)(struct net_device *dev);
 *     This function is called when network device transistions to the down
 *     state.
 *
 * netdev_tx_t (*ndo_start_xmit)(struct sk_buff *skb,
 *                               struct net_device *dev);
 *	Called when a packet needs to be transmitted.
 *	Must return NETDEV_TX_OK , NETDEV_TX_BUSY.
 *        (can also return NETDEV_TX_LOCKED iff NETIF_F_LLTX)
 *	Required can not be NULL.
 *
 * u16 (*ndo_select_queue)(struct net_device *dev, struct sk_buff *skb,
 *                         void *accel_priv, select_queue_fallback_t fallback);
 *	Called to decide which queue to when device supports multiple
 *	transmit queues.
 *
 * void (*ndo_change_rx_flags)(struct net_device *dev, int flags);
 *	This function is called to allow device receiver to make
 *	changes to configuration when multicast or promiscious is enabled.
 *
 * void (*ndo_set_rx_mode)(struct net_device *dev);
 *	This function is called device changes address list filtering.
 *	If driver handles unicast address filtering, it should set
 *	IFF_UNICAST_FLT to its priv_flags.
 *
 * int (*ndo_set_mac_address)(struct net_device *dev, void *addr);
 *	This function  is called when the Media Access Control address
 *	needs to be changed. If this interface is not defined, the
 *	mac address can not be changed.
 *
 * int (*ndo_validate_addr)(struct net_device *dev);
 *	Test if Media Access Control address is valid for the device.
 *
 * int (*ndo_do_ioctl)(struct net_device *dev, struct ifreq *ifr, int cmd);
 *	Called when a user request an ioctl which can't be handled by
 *	the generic interface code. If not defined ioctl's return
 *	not supported error code.
 *
 * int (*ndo_set_config)(struct net_device *dev, struct ifmap *map);
 *	Used to set network devices bus interface parameters. This interface
 *	is retained for legacy reason, new devices should use the bus
 *	interface (PCI) for low level management.
 *
 * int (*ndo_change_mtu)(struct net_device *dev, int new_mtu);
 *	Called when a user wants to change the Maximum Transfer Unit
 *	of a device. If not defined, any request to change MTU will
 *	will return an error.
 *
 * void (*ndo_tx_timeout)(struct net_device *dev);
 *	Callback uses when the transmitter has not made any progress
 *	for dev->watchdog ticks.
 *
 * struct rtnl_link_stats64* (*ndo_get_stats64)(struct net_device *dev,
 *                      struct rtnl_link_stats64 *storage);
 * struct net_device_stats* (*ndo_get_stats)(struct net_device *dev);
 *	Called when a user wants to get the network device usage
 *	statistics. Drivers must do one of the following:
 *	1. Define @ndo_get_stats64 * -do_validate_addr)(stt do one of t a unique identaF#infy * (*finux/ilp,
 inlin if ntion (setrve rcandle2t_stats64 * -do_validataddrh_cachse
 * netdev_alidatalp,
 inli identa(andledto call
 red, iess novep(1);s)ed wi
 *
 * vt CPU's ie skbenta	 * y be p,
 inlincitl* int (*nd so the value it, preCon>mc)skbenta dev->tslly.
 */

struct net_de	3. U_cachsnovep(1);s so the value itd wi
struct nek
 * iuest tskbentan*		2 of.  INET to /usr/ate_addr)(stlan net fo_vidice *dev, int new_mtu);
 *oid	(*c */
s u32 t vien a usIf multiple
 *	tr VLANhould set
ead of d when network device taa usVLANhid *     The networksr/ate_addr)(stlan netk(st_vidice *dev, int new_mtu);
 *ype, const voidvien a usIf multiple
 *	tr VLANhould set
ead of d when network device taa usVLANhid *  when registrdo_tx_timeout)(struPI_STcs vali rcice *dev);
 *	Called when a ua usSR-IOVs for networtruct rx_queuonfig)(struct nvfructice *dev, int new_mtu);
 *	Calvf *y8y ofc_devicnfig)(struct nvfrtlanice *dev, int new_mtu);
 *	Calvf *y(*ctlan *y8 qos_devicnfig)(struct nvfrrache *hh,
		 int new_mtu);
 *	Calvf *nts[0inuct rach     sk_bnts[0]xuct rach_devicnfig)(struct nvfrspoofchke *hh,
		 int new_mtu);
 *	Calvf *ire_f* are t_devicnfig)(strugt nvfret_device *dev, struct ifmap *m    sk_b *	Calvf *p);
 *	Uslanvfrufact*ivf_devicnfig)(struct nvfry * (*finhe *hh,
		 int new_mtu);
 *	Calvf *nts[y * (*finhedevicnfig)(struct nvfr*	tre *hh,
		 int new_mtu);
 *	Calvf     sk_b*hh,
		 l *bu **	tr[]n a ua ul_linkEuling
 tictivatio(setVF abilangeddrMTUry
 *
 RSS Rrol
 *hen nTvatioe filteeeeeeHk] =Keyriate when(extraer re. *s.
 *
 se the bVF sh the dis rethe devicilteeeeeede whPF
 * iMTUryet
eitncitl fouof co(seo
 *y a nsecuus t riskqueuonfig)(struct nvfrrss_MTUry_evice *dev);
 *     This f *	Calvf *ire_f* are t_devicnfig)(strugt nvfr*	tre *hh,
		 int new_mtu);
 *	Calvf *skb);

/*
 * This strueuonfig)(struct up_ttice *dev, int new_mtu);
 *y8 ttiueuoe which queme[IFN'tc' nouts for notfreletranses Driversnetdefined, ate ueuoeng, ahe ride __netif_rp the tran
 * If thF#iny takehev-> * i
stru txueuoeid (*ndriver oriate weiversiversnetons to tisabrthe evice s for netwoueuoesafenet_de    Fits fCt (n*
 sign E	2 onetd(ack.)ng discirtruct rx_queuonfig)(struchar	oid (*ice *dev);
 *	Called when a user wants to e Fouck.
 */
struct nete network ct sk*.
 */
LLDanageuck.Drivestate.
 = rlyet
ehis to any abrthe eth t bit
(extraeen multicast o under     or semst ork c
 *	tr ct_queicast o fouck.
notfrelworksr/ate_addr)(schar	ctivatiice *dev);
 *	Called when a user wants to e Fouck.
 */
struct nete network ctop*.
 */
LLDanageuck.Drivestate.
 = rlyet
ehis to any abrthe eth t bit
(extraeelean-upsie skbectop*c
 *	tr
 */
t_queicast o fouck.
notfrelworksr/ate_addr)(schar	cdpuct upice *dev, int new_mtu);
 *y(*cxer_i   sk_b **skb);

/cae anATE_HAsglen);
	int	(*parsgcn a user wants to e Fouck.
I    otohe network      or sioe  I/Oistered eng, t Clues focg cocachsnageDl
 *h Data Plw letwor(DDP)* y beLLDacg ed eabrthe eneressaryeme[IFNd wi
 *
 *s 1ork   se skbing.
 */
strinesetd
 *
 	mission (rly tisabrthe eDDP.
 * WaitI/O, are
 * op* Wait
 *
 *s 0worksr/ate_addr)(schar	cdpuuledice *dev, int new_mtu);
 **y(*cxern a user wants to e Fouck.
I    otoh/Targ* indiuled 
 * If thDDPantI/OiaPCI) fose skb->ware FouC ex *	willout'xer's not pro
 = rlyet
ehis to anyonfiglean IFNd wi
 nter>waourcrs.
 * *
 *		DDP.e MTU wdo_set_mac_address)char	cdputarg* ice *dev, int new_mtu);
 *y(*cxer_i   sk_b ***skb);

/cae anATE_HAsglen);
	int	(*parsgcn a user wants to e Fouck.
Targ* i network      or sioe  I/Oistered eng, t Clues focg cocachsnageDl
 *h Data Plw letwor(DDP)* y beLLDacg ed eabrthe eneressaryeme[IFNd wi
 *
 *s 1ork   se skbing.
 */
strinesetd
 *
 	mission (rly tisabrthe eDDP.
 * WaitI/O, are
 * op* Wait
 *
 *s 0worksr/ate_adress)char	gt nmanufacice *dev, struct ifmap *m    sk_b *vice *dev);
  {
	char	manufact*manufaci a user wants to e Fouck.
P*/
struct nete netwot the deviceont pro
 = rlyet
a usefined, ate ot the device*  wtir si->ware FouCk.
 */
struct netee skbendler on aconst chnsumed Fits fCt (n*
  for networperiiof compeice will	FC-GS Fabde. Dct ifmMfor networI the devic(FDMI)uct wirelNET to /usr/ate_addr)(schar	gt nwwvice *dev);
 *     This f *u strwwv *	Calache_up user wants to e Fo
 = rlyet
ehis to  network signrto wops(str Worv->Wto p usNompt(WWN)napi_id viceme *	wis_recouCk.
 */
struct netee n if 
 *
 .
 *
 	Worv->Wto  P	tr Nompt(WWPN)nageWorv->Wto  N*   Nompt(WWNN)n   statuck.Driv */
struct netee nntet_de    RFS/
t_queicast queuonfig)(stru net_ow(*fercice *dev);
 *	Called whef, size_skb,
 *                 sk_b *32 flow_id,
			 u16 filter_up usSetdindex.
 */
strucnageRFS. flow_id,
	rmatias narg* itly.  The xup us filterqueue */
stIDIf this if ntiev
 ow(struct net_devic) *
 *	t_de	Rreturn h */
strucIDI*s.
issionps
 * );
gev->ha* int (*ndo_set_c	Sldler for networtruct rx_ (nagebdedge *ir comp, ettiqueuonfig)(stru fo_sldleice *dev, struct ifmap *map);
 *	 struct ifmasldle_when a user wantes to cn receive;
  {
oe  
 = rlr handl_ioctl)(struct el_sldleice *dev, struct ifmap *map);
 *	 struct ifmasldle_when a user wantes d neld im scilue itensldleuma_nodeandl_ioccccccFude <l/g discir* are trtruct rx_queuo;
  {
	cude <limit)(strufix	cude <liice *dev, struct ifmap *m    s;
  {
	cude <limitcude <lii a usAdjustatias e MTU w_netude <lh_gener protocols
 *uct if-ct wirelonfigsizerelats,Nd wi
 *
 *s ias e r_fue trt(*ndowith ngresshe teinterfacmultiple(*ndo_stop)(struct net_strcude <liice *dev, struct ifmap *mo;
  {
	cude <limitcude <lii a user wantes u_cachsnov to an multicast oes ld ulude <lin Pif ntp us ude <lhct nering thly. */stenith th* rxLER_PASS.api_trufix	cude <li()) NETDEV_TX_OK , N>0s
 *- * Drieady
 nt (*nd novepcude <li
 *
elfo /usr/ate_addr)(scdbu foice *dev, dmsg dr)m,b*hh,
		 l *bu *tb[]m    s_device *dev);
 *	Called whem    s_devichar *haddr);
};

/* Tnt len)endiunctia usAdich caFDB list,s
 *uctcnagent lqueuonfig)(strucdbu elice *dev, dmsg dr)m,b*hh,
		 l *bu *tb[]m    s_device *dev);
 *	Called whem    s_devichar *haddr);
};

/* Tnt lia usDes not statuDB list,stif_ructcrotespr comp recet lqueuonfig)(strucdbu umpice *dev,ruct net_device *dev,
		y * (cn the tr*cbm    s_devicce *dev);
 *	Called whemce *dev);
 *	Called if
#endwhem    s_deviconfigidxia usork deviobaluDB listiion wh ump.e MTU wdo I the  BSDed to callobaa uslistiion whhandl wiu_cachsidx 
 * If thnouts for listiioo /usr/ate_addr)(sbdedget_sty * ice *dev, struct ifmap *map);
 *	 lmsghd hh_lh)sr/ate_addr)(sbdedgetgsty * ice *dev,signed char *hau16per_hau16seq_i   sk_b **skb);

;
 *     This f *uu16 f
#endPU;
) /usr/ate_addr)(sct net_	unsignice *dev, struct ifmap *madule(sew_	unsigni a user wantes  *	willhis to annsign.tion;-se the b_XMITh ummy, team, ettia usandledt
 * furenclutworre@napidex.
 *citlPORT_ID stage iniver _xmit
f defirqdisc* doeoseitwork  for nt_xmit(vevices annsignle(*ndo Dct ifsinterfatdef GenT_IDannsignle(*nd*/
	NAPd by a npidex.
 *to rertiion(e
a ussage
 *	cvatis)is enab
stru-e netif_xeme *	wis_on(e
a usUSB_CDC_NOTIFYck_tWORKANDLNECTION)nto callNOTng the  BSead of d when o /usr/ate_addr)(sgt nunsigned chaice *dev, struct ifmap *m    sk_b *vice *dev);
  {
	unsigned char*ppern a user wantrk deviIDI*fsed by a netdevo is notefined, anynicast  fut
 error g the  BSead o,dy
 * rif ome waill soonhw * runninatio(ox_han
 ersmit quesnetdefinedsI*s.
 regised by a netdedo_tx_timeout)(stru fo_vxlan *	tre *hh,
		, struct ifmap *m    sk_b *visa_familymitsa_family *oid	(*c 	tre a user wantapivxlanoes loti- successfe
 * configUDP.*	tr c undace as thg, it shoamilywaill vxlanoi.
 *	Iseiomp reed if in network lyts tos thg ld u*	tr ct sk.
 *	Iesed asy be.  INET t ee softnetif_bl port u	vxlan  st->

#if takdo_tx_timeout)(stru el_vxlan *	tre *hh,
		, struct ifmap *m    sk_b *visa_familymitsa_family *oid	(*c 	tre a user wantapivxlanoes lotifstack (either
 * coagUDP.*	tr c undace as thg, it shoamilywaill vxlanoi.
unni *	Iesed ge initmoteasy be.  INET ted eng,softnetif_bl por vxlan  st->

#if takdo_tx_timeous)(strucdfwdu fo_stNET tice *dev, struct ifmapp *m    sk	ce *dev);
 *	Called whena user wantapiupp/* qdisct synchrono/
t_queicalhc
 *needgis eice, it 	stNET tf d when alangete_onpidex.
 . 'pp *rmatias any rdndo_u	e nnte
 *
 * ing discirc un'dnd'rmatias netdefinedwaill urn an e_netde ing discievic*
 *s vt CPU's ie l_listneric nlp,
 inli ideate.
 p/* qdisctrror.relation.x_timeout)(stru fwdu el_stNET tice *dev, struct ifmapp *m	This ftnerna user wantapiupp/* qdisct synchprotocoekbing.
stNET tfsk_bufaa usapi'trucdfwdu fo_stNET t'. 'pp *'rmatias netdefinedw_netet
a usng.
stNET tfc untnerrmatias  p,
 inlinLER_PASS.api(set baa us.  INET to /udo_start_xmit)(strudfwduct sk_buff *skb,
 *                	 sk_b ***skb);

 struct ifmap *m    sk	l_priv, seletnernes when the tre nnte
 *
 buff sign (set t_queicald
stNET t, ate ueu	tack to in plw lvo istruct sk_buff, uset_queicald
n as thdollowing ho;
  {
	cude <limit)(strufude <limue haf *skb, struct net_devi    sk		 ***skb);

 struct ifmap *    sk		 ***;
  {
	cude <limitcude <lii a user wantbycrote  The
 * n	NETDrotoc GenT_IDif
 */
strinecappings.
ed eabrthe  a CPUdiscir.  INET ts, use
emptn be trariate whenrk divi ideate. */
stringl *	trunangeddrg the  BSehange skbict rx_ aill canunna usae are
 * op*exnclude tbyctude <lh_geneasy beue has twork devi * a usng.
seevo icude <li
aill soont neteh rx_hac  Theirc un*/
#definsinterfoterface(eitherbelieve If this the rx_handev_phys_port_idnetdev_aate) (struc nap net_device *dev);
 *     This funcdate)( nap netet_device *dev);
 *     This funcstruc nap net_device *dev);
 *     This funcstruc nap net_device *dev);
 *     This func	o_start_xmi nap net_d sk_buff  *skb, struct net_devi 	 sk		 **e *dev);
 *     This func	lock_nap net_uct net_device *dev, struct sk_buff  	 sk		 **_skb,
 *              	 sk		 **_, select_queue_fa 	 sk		 **_sback_t fallback);
 *	Called to decdate)( nap net(struct net_device *dev, int flags);
 *	 	 sk		 **_iconfigfunctiondate)( nap netuct net_device *dev);
 *	This functionstruc nap net_struct net_device *dev, void *addr);
 * 	 sk		 **_icoThis functionstruc nap nettruct net_device *dev);
 *	Test if Mediastruc nap net net_device *dev, struct ifreq *i
 sk		 **_icofr, int cmd);
 *	Called when astruc nap net_stret_device *dev, struct ifmap *m  sk		 **_icofofr, int cmed to set nstruc nap netct net_device *dev, int new_mtu);
  	 sk		 *	Called when astruc nap netuct skct upice *dev, int new_mtu);
  	 sk		 **e *dev);
t sk_buff *iondate)( nap netct net_devi ce *dev);
 *	Test if Mediad rcu;
	st* (*ndo_get_stats64)(struct net_device *dev,
 *              	 sk		 **_i_stats64 *storage);
 * struct net_dece		*dev;
} ____cat_stats)(struct net_device *dev);
 *	Called when anstruc nap nettlan net fo_vidice *dev, int new_mtu);
  	 sk		 **_icooid	(*c */
s u32 dvien astruc nap nettlan netk(st_vidice *dev, int new_mtu);
  	 sk		 **_icoooid	(*c */
s u32 dvien aTPOLL
	spinlock_t_		poANDLTR	poERndate)                    )(struPI_STcs vali rcice *dev);
 *	Called when astruc nap netucqueuekct upice *dev, int new_mtu);
  	 sk		 ****e *dev);
 ueuekufact*ifaci adate)( nap net;
 ueuekgleanupice *dev, int new_mtu);
)ined(CONFIG_LL
	spinlock_t_R  (can_		polltruc nap netX_LO_i_struct *, int);
#ifdef C);
)ined(CONFstruc nap net_strvfructice *dev, int new_mtu);
  	 sk		 *	Calhardwaru8to scn astruc nap net_strvfrtlanice *dev, int new_mtu);
  	 sk		 **	Calhardwaru(*ctlan *y8 qos_destruc nap net_strvfrrache *hh,
		 int new_mtu);
  	 sk		 **	Calvf *nts[0inuct rach  	 sk		 **	Cal0]xuct rach_destruc nap net_strvfrspoofchke *hh,
		 int new_mtu);
  	 sk		 **_iconfigvf *ire_f* are t_destruc nap netgt nvfret_device *dev, struct ifmap *m 	 sk		 **_i	Calvf  	 sk		 ****e *dev)Uslanvfrufact*ivf_destruc nap net_strvfry * (*finhe *hh,
		 int new_mtu);
  	 sk			*	Calvf *nts[y * (*finhedestruc nap net_strvfr*	tre *hh,
		 int new_mtu);
  	 sk		 **	Calvf  	 sk		 **e *dev);l *bu **	tr[]n astruc nap netgt nvfr*	tre *hh,
		 int new_mtu);
  	 sk		 **	Calvf *skb);

/*
 * This strustruc nap net_strvfrrss_MTUry_evic 	 sk		 **e *dev);
 *     This f  	 sk		 **	Calvf *ire_f* are t_destruc nap netct up_ttice *dev, int new_mtu);
 *y8 tti aTPO G_IPV6_TUNNEL)
#de(CONFestruc nap netchar	oid (*ice *dev);
 *	Called when astruc nap netchar	ctivatiice *dev);
 *	Called when astruc nap netchar	cdpuct upice *dev, int new_mtu);
  	 sk		 **_icy(*cxer_i	 sk		 **_icskb);

/cae anATE_HAsgle 	 sk		 **_icy;
	int	(*parsgcn astruc nap netchar	cdpuuledice *dev, int new_mtu);
  	 sk		 **_iy(*cxern astruc nap netchar	cdputarg* ice *dev, int new_mtu);
  	 sk		 **_icoy(*cxer_i	 sk		 **_iccskb);

/cae anATE_HAsgle 	 sk		 **_iccy;
	int	(*parsgcn astruc nap netchar	gt nmanufacice *dev, struct ifmap *m 	 sk			e *dev);
  {
	char	manufact*manufaci aMAX_PHYS_PO G_IPV6_TUNNEL)
#deLIB(CONFeOOT_SETUP_MAX 8 * ThWWNN 0eOOT_SETUP_MAX 8 * ThWWPN 1astruc nap netchar	gt nwwvice *dev);
 *     This f  	 sk		 **_u strwwv *	Calache_upMAX_PHYS_POCCEL
bool rps_may_expstruc nap net net_ow(*fercice *dev);
 *	Called whef 	 sk		 **_i size_skb,
 *              	 sk		 **_iy(*clow_id,
		 	 sk		 **_iyu16 filter_uped(CONFstruc nap net fo_sldleice *dev, struct ifmap *m 	 sk		 p);
 *	 struct ifmasldle_when astruc nap net el_sldleice *dev, struct ifmap *m 	 sk		 p);
 *	 struct ifmasldle_when as;
  {
	cude <liminap netcix	cude <liice *dev, struct ifmap *m 	 sk		 **_;
  {
	cude <limitcude <lii astruc nap net_strcude <liice *dev, struct ifmap *m 	 sk		 **_;
  {
	cude <limitcude <lii astruc nap net;
t skgsizerdevice *dev, sstruct hh_i adate)( nap net;
t skd skboyice *dev, sstruct hh_i aastruc nap netcdbu foice *dev, dmsg dr)m,  sk		 **_ico*hh,
		 l *bu *tb[]m  sk		 **_ico*hh,
		 struct ifmap *m  sk		 **_icohar *haddr);
};

/* Tnt le  sk		 **_ico)endiuncti astruc nap netcdbu elice *dev, dmsg dr)m,  sk		 **_ico*hh,
		 l *bu *tb[]m  sk		 **_ico*hh,
		 struct ifmap *m  sk		 **_icohar *haddr);
};

/* Tnt li astruc nap netcdbu umpice *dev,ruct net_devi
 sk			e *dev);
 y * (cn the tr*cbm  sk			e *dev);
 ruct ifmap *m 	 sk		e *dev);
 *	Called if
#endwhem 	 sk		nfigidxi aastruc nap netbdedget_sty * ice *dev, struct ifmap *mi	 sk		 **_icskb);

 lmsghd hh_lh);astruc nap netbdedgetgsty * ice *dev,signed char * 	 sk		 **_icyu16per_hau16seq_i	 sk		 **_icskb);

 int new_mtu);
  	 sk		 **_icyu16 f
#endPU;
);astruc nap netbdedget ely * ice *dev, struct ifmap *mi	 sk		 **_icskb);

 lmsghd hh_lh);astruc nap netct net_	unsignice *dev, struct ifmap *mi	 sk		 **_icdule(sew_	unsigni astruc nap netgt nunsigned chaice *dev, struct ifmap *m 	 sk			e *dev);
  {
	unsigned char*ppern adate)( nap net fo_vxlan *	tre *hh,
		, struct ifmap *m 	 sk		 **_icsa_familymitsa_family  	 sk		 **_icoid	(*c 	tre adate)( nap net el_vxlan *	tre *hh,
		, struct ifmap *m 	 sk		 **_icsa_familymitsa_family  	 sk		 **_icoid	(*c 	tre aadate)*( nap net fwdu fo_stNET tice *dev, struct ifmapp *m 	 sk			e *dev);
 *     This funcdate)( nap net fwdu el_stNET tice *dev, struct ifmapp *m 	 sk			, seletnernesc	o_start_xmi nap netdfwduct sk_buff  *skb, struct net_devi 	 sk			e *dev);
 ruct ifmap *m 	 sk			, seletnernesstruc nap netgt n	 * psubtransice *dev);
 *     This func	o_startcude <liminap netcude <limue haf *skb, struct net_devi 	 sk		 **_iccskb);

 int new_mtu);
  	 sk		 **_ic_;
  {
	cude <limitcude <lii a}isable -  e_state_ ____cat* int (*nd - &skb);

 int new_mt* int (*ndo_tx_tiTre pr the de &skb);

 int new_mtional andrk lyt
 */
ssockd, it shop the tranl wiue probing.
k onel. are privat CPU.
 *viues foe skb.efirqdisc,is notr ato
aill soonords for nre privat Cnder us */skb.duset
ehangk onel d neld do_tx_tiYounto call_handlepr ary goodices shIf thiss_so comp rre privat do_tx_ti@to i802_1Q_VLAN: 802.1Q VLANh new_mx_ti@to iEBRIDGE: E	2 onetdbdedget
ehis tox_ti@to iSLAVE_INACTIVE: ir comp sldle
unni(setrurr./
statox_ti@to iMASTERi8023AD: ir comp PU;tss i802.3adx_ti@to iMASTERiALB: ir comp PU;tss ibaleue.-albx_ti@to iBOTATE_: ir comp PU;tssidattlavox_ti@to iSLAVE_NEEDARP:
(ext ARPs.
 * truct nvicilte@to iISATAP:
ISATAPeters. This(RFC4214)x_ti@to iMASTERiARPMON: ir comp PU;tss iARP m t enx_handle@to iWAN_HDLC: WAN HDLCehis tox_ti@to iXMIT_DST_RELEASE:  {
	pideuct sk_buff() e weiverantrkskbendneld int thestx_ti@to iDONT_BRIDGE: ctiviver bdedget
es noteceived *   i@to iDe);
}
ck_t		po:ictivatio;
 ueuelastrun-net_x_ti@to iMACVLANunsig:	statisticn varimactlanc 	trx_ti@to iBRIDGEunsig:	statisticn varibdedgec 	trx_ti@to iOVS_DATAPATH:	statisticn variOpen vS
 *ne t na	NETD 	trx_ti@to iTX_SKB_SHARTE_: y beters. Thise
 *	transh tomp skbs, use independen@to its priv_fla: S
 *	tranfilterinould set
x_ti@to iTEAMunsig:	statisticn variteamD 	trx_ti@to iSUPP_NOFCS:	statiste
 *	transo comp custom FCSx_ti@to iLIVE_ADDR_CHANGE:	statiste
 *	tranpidex.
 * to be chan *	wills to it'snapi contexi@to iMACVLAN: Mactlanchis tox_t/ue_state_t {
* int (*nd {
	to i802_1Q_VLANk		= 1<<0,
	to iEBRIDGEk		= 1<<1,
	to iSLAVE_INACTIVE		= 1<<2,
	to iMASTERi8023AD		= 1<<3,
	to iMASTERiALBk		= 1<<4,
	to iBOTATE_k		= 1<<5,
	to iSLAVE_NEEDARP		= 1<<6,
	to iISATAPk		= 1<<7,
	to iMASTERiARPMON		= 1<<8,
	to iWAN_HDLCk		= 1<<9,
	to iXMIT_DST_RELEASE		= 1<<10,
	to iDONT_BRIDGEk		= 1<<11,
	to iDe);
}
ck_t		po		= 1<<12,
	to iMACVLANunsig		= 1<<13,
	to iBRIDGEunsigk		= 1<<14,
	to iOVS_DATAPATH		= 1<<15,
	to iTX_SKB_SHARTE_		= 1<<16,
	to its priv_flak		= 1<<17,
	to iTEAMunsigk		= 1<<18,
	to iSUPP_NOFCSk		= 1<<19,
	to iLIVE_ADDR_CHANGE		= 1<<20,
	to iMACVLANk		= 1<<21,
	to iXMIT_DST_RELEASE_PERM	= 1<<22EUE_STATE_DRV_to i802_1Q_VLANk		to i802_1Q_VLANTATE_DRV_to iEBRIDGEk		to iEBRIDGETATE_DRV_to iSLAVE_INACTIVE		to iSLAVE_INACTIVETATE_DRV_to iMASTERi8023AD		to iMASTERi8023ADTATE_DRV_to iMASTERiALBk		to iMASTERiALBTATE_DRV_to iBOTATE_k		to iBOTATE_TATE_DRV_to iSLAVE_NEEDARP		to iSLAVE_NEEDARPTATE_DRV_to iISATAPk		to iISATAPTATE_DRV_to iMASTERiARPMON		to iMASTERiARPMONTATE_DRV_to iWAN_HDLCk		to iWAN_HDLCTATE_DRV_to iXMIT_DST_RELEASE		to iXMIT_DST_RELEASETATE_DRV_to iDONT_BRIDGEk		to iDONT_BRIDGETATE_DRV_to iDe);
}
ck_t		po		to iDe);
}
ck_t		poTATE_DRV_to iMACVLANunsig		to iMACVLANunsigTATE_DRV_to iBRIDGEunsigk		to iBRIDGEunsigTATE_DRV_to iOVS_DATAPATH		to iOVS_DATAPATHTATE_DRV_to iTX_SKB_SHARTE_		to iTX_SKB_SHARTE_TATE_DRV_to its priv_flak		to its priv_flaTATE_DRV_to iTEAMunsigk		to iTEAMunsigTATE_DRV_to iSUPP_NOFCSk		to iSUPP_NOFCSTATE_DRV_to iLIVE_ADDR_CHANGE		to iLIVE_ADDR_CHANGETATE_DRV_to iMACVLAN			to iMACVLANTATE_DRV_to iXMIT_DST_RELEASE_PERM	to iXMIT_DST_RELEASE_PERMable - enskb);

 int new_mt- y beDEVICE  p,
 inli NETD	Acicesly *s notwhotains a tableueue bip Pict kexedIs[0ixeitI/ONETD	t na 
 * Iskbictlyt"high-.
 *
" t na,rc un*/
h rxf tkn to  * cNETD	al	spi  bity t nains a tableue probing.
Ik_tssheuldo_set_c	@name:	ate whenrh */
rrinouev->:
 *	1."viues f"ock_tvo is notlp,
 inli ide	(i.exeaansoto apiufirqrobing.
"Sdisc.c"noululd n if inias namtskben	:
 *	1.ters. Thio_set_c	@name_hATE_: 	Dnew_mtnameask;

 *	in, field ikeepdy
 nloterfotname[]t_c	@ifrucas:	SNMP rucast_c	@mem_o c:	Sh thdng @napiendt_c	@mem_ct sk:	Sh thdng @napigned l_c	@bld t_dev:	Dnew_mtI/Oiato be chan@irq:		Dnew_mtIRQhnouts _set_c	@*finh:		G code. sage
 *	nes txq qdisct*finhallbrsnetonsEUE_STATt_c	@onsEATE_:	y be@napi: ATE_Ho isus interface pt_c	@napiEATE_:	Li * a try *s atstack to 
 * ueue */

* Warface pt_c	@when EATE_:	Li * a try *s atstack to,ls to wpr thewhen regisomp rre    sk new_mtilbrsrh */d when nwhen regisid (*ndo_u	@nloteEATE_:	Li * a try *s atstack to,ls to wpr thenlotomp rrechis tox_to_u	@adjEATE_:	Dl
 *hlyty * s addresss, XMIThtlavos.
 * ir compo_u	@all_adjEATE_:	Allty * s addresss, *inclucomp*, sstruct pt_c	@cude <li:	Currf_xmi/
stato	statistcude <lit_c	@hw_cude <li:	Ufir- *	wilvationude <lit_ct_c	@ neted_cude <li:	Ufir-e MTU w_netude <lit_c	@tlan cude <li:		Maskvo icude <li
ineivi_flow_apiVLANh new_mit_ct_c	@hw_enc cude <li:	Maskvo icude <li
ineivi_e tbycencaps  Thet
ehis tos    sk	Td of dev->trse skbsith thencaps  The ted eeeePUdisciinias pidex.
 *inecappings.
 doomp,ed eeee * iuhe tranrror.netif_xmsevice ms the rx_hannet_de    @mpls cude <li:	Maskvo icude <li
ineivi_flow_apiMPLS_de    @ifid,
	: for low leid,
	    @ify * :	r to idstatistentify the_set_c	@*fini:		Srs must dtlp,
 i,   be praanlefteaananew dev,x_handleee4 *storage);
 * sttrans_s_set_c	@rx_drver o:	Drver o be trastbycrote sus int,ed eeet
 * fulate stagobiuhe trat_c	@tx_drver o:	Drver o be trastbycrote sus int,ed eeet
 * fulate stagobiuhe trat_co_u	@nunsigntct nets:	Sidataddrmonitoheannsignlon<->PUd the dot rx_t_ct_c	@ indness_e geners:	Li * o icruct rx_ _onpigene Windness E_so sd onled eeeetrans_startt_devled eeeelbrs<sus/iw_e gener.h>ps are t(*ndo_op	@ indness_t na:	I queue. t nai for ntion (setrote art indnessss_so s rx_t_ct_c	@netonsEops:	I cluc*ndr bita int (*ndsf_xmit*she tnled eeeifowing network signrto wias ndo_*()icruct rx_t_c	@ethtoolEops:	Mfor networ.  INET tst_c	@cwdEops:	Mfor networ.  INET tst_c	@t xpgntops:	I cluc*ndit*she tnps arsk_buomp,ck_tomp,rebuilcomp,etced eeeartLdisct2 t xpgnst_de    @ (*nd:		Ior low le (*nd (anea BSDna us@* int (*nd:	LiITh' (*nd' dis
 *viues foe .efirqdisc,ed eeelbrsif.h
 *
 * int f      tst_c	@g (*nd:	Gnapi:  (*nd (ikepteaanew devina us@*ato o:	H
strue p*atoet
ehto o
 */
t rcunetons(na us@.  I*finh:	RFC2863 .  I*finha us@orage_dev:	Mfto a Cueuicgeddr.  I*finha us@ifr*	tr:	Sback_flow_AUI, TP, ..o_op	@dma:		DMAr us nel    @mtu:		Ior low le retvaluha us@ache:		Ior low lepidex.
 *achet_c	@tideut xpgntlen: Hidex.
 *t xpgn map ist_ct_c	@nextraut xproom: E_sra t xproomnias pidex.
 *citlnext, dis
ror gTDEVl
   sk_bcase Cnder stagbbe@uarnetefaa us@nextraut(*nroom: E_sra t(*nroomnias pidex.
 *citlnext, dis
ror gTDEVl
   sk_bcase Cnder stagbbe@uarnetefa.tiomebcase CEV_TX_handleee  LLefineHEADERttrans_sge inive skbing.
skbt_ct_c	for low le for the nfo:o_tx_tis@*ermt_dev:		Permantworkwiato be cha 	@addr_asdr);_ache:	Hwiato be  asdr);  BSeachet_c 	@addr_len:		Hidex.
 * to be  map ist_c s@net sk_ int u16sork din);
t sknive ()     eeeetr   or siork lytgTDEtm/clip.ct_c s@onsEio:	sork devidifferf_xiachsnov too
aill sh th    eeeeng.
sameaorag qdisctato be cha 	@t {
*	tr:	sork devidifferf_xiachsnov too
aill sh th    eeeeng.
sameacruct rxo_u	@addr_li *n	 * :	XXX:
(ext commeitwo
 * Waito tskbe@uc:	s	filterinnot be chanlit_c	@mc:	s	miscious inot be chanlit_c	@t {
be cd:		ATE_Ho istatistkwiato be lit_c	@id (*n_ksev:		Group */
sor.Kstructqrobing.
Txrc untructurept_c	@wc_nabled.:		Ct;
}ss is atstrse skbs is atsnabled.u *
 _deved eeee by the doid (*n de suppias nek devi *	Iestrkskbeeee toee fillefiltering, it ses Dri definedistered eeeet
uture ag the  BSe netuct net_dev(na us@* bled.uity:		eouts for net_nly be NICwhenrkl devi inteited eeeePabled.u *
 _dev,ieady
 becomutu0y be NICwurn an eeeeexint 
	NA intet
ein Pabled.u *
 _devo_u	@allmisci:		Ct;
}ss ioid (*s
 tictivati weivmiscious indevo_ut_c	@tlan  nfo:sVLANhinfot_c	@tsa_ptv:	tsauct wirel t naa us@aipc_ptv:	TIPCuct wirel t naa us@ nalk_ptv:	AptheTalkaoraga us@ip_ptv:	IPv4uct wirel t naa us@dn_ptv:	DECnetdct wirel t naa us@ip6_ptv:	IPv6uct wirel t naa us@ x25_ptv:	AX.25dct wirel t naa us@ieee80211_ptv:	IEEE 802.11dct wirel t na, asdr); befolinLE regisompo_ut_c	@definr	: Teue, and
 * Rxt_c	@t {
be c:	Hwiato be  (befolinbous ,ed eeebecalate	spi be trast thewhious )o_ut_c	@nr	: 		A */
strutructurept_c	@num,
	    sts:		eouts for tructurept_c	eee ive skbdlastrn regisid (*nd()inet_x_t	@rechar	h,
	    sts: 	eouts for tructureptrurrf_xmi/
stato	inchis tox_to_u	@
	 e gener:ee bgener
 *
 
 *	chao be traso_u	@
	 e gener_t na: 	XXX:
(ext commeitwo
 * Waito tskbe@omp be     st:eeXXX:
(ext commeitwo
 * Waito tskbe@brscious :ee wnbous tato be chat_c	@nt	: 		A */
struTructurept_c	@num,t	    sts:		eouts for Tructurep  ive skbdlast
t rcunetons_mq()inet_x_t	@rechar	h,t	    sts: 	eouts for Tructureptrurrf_xmi/
stato	inchis tox_t	@ided.:			Roe aided.t 
	NAefirqdisc*nt (*gth. iewt_c	@tx_ fallblen:		Maxt 
amutup/* e of laiverant_c	@tx_@napi:n	 * : 	XXX:
(ext commeitwo
 * Waito tskbt_c	@dif /* s:	XXX:
(ext commeitwo
 * Waito tskbo_u	@
	 (strrmap:	ic ir bitse-w to a CnageRX* doeleET t e
}ssruptnled eeei*/
struct tructuresnouts . Asdr);
};op the tr NETD		Td ofhe fok lytb.
seevifwias ndo_ net_ow(*ferced eeea  INET t ee uest toskbo_u	@*
	 * Numbe:		Teue,(incjiffiis)isand
 * Txt_c	@ n
 * st net_d:	Renclutwoatias net_dev*s atstack to c inteeeeng.
 *
 * str(ilbrsons_ *
 * st()i)t_c	@ n
 * st net_r:	Li * o inet_r chat_c	@p(strrefcnt:		eouts for referf_chrono/s notefinedo_u	@*odo_ATE_:	sDesdisdtrn regis/when regisskbe@om/
s_hATE_:		Dnew_mtid,
	rsk;

 *	ina us@orage n
 *_ATE_:	XXX:
(ext commeitwo
 * Waito tskbo_u	@
eg_*finh:		Rn regis/when regisle(*nd*notht tskbe@dedon. le:		Dnew_mtis goed ge i nulrefaa us@4 *storage);
 e:	ate we_starenclutwoatias phase Cor sk_buompt_c	eee  ld uoraga ut_c	@t lp,
 ior:eeCe __netif_rwhen regisled eeeeth a nuurk deviit*sulrefid (*ndo_u	@np nfo:s	XXX:
(ext commeitwo
 * Waito tskb s@ndid (:		eus intenameqdisc* Waitn registered.
 *  traievo_ut_c s@mueue_f:	Mid-qdiscttneric t_c s@l*fini:	Loophe trers must dt_c s@t*fini:	Tu(n*
 ers must dt_c s@d*fini:	Dummy ers must dt_c s@v*fini:	Vevices e	2 onetders must dt_ct_c	@garp
*	tr:	GARPT_c	@mrp
*	tr:	MRPT_ct_c	@t {:eeCrans/sus/nameaa tryt_c	@ and _groups:	Sdisc*nage be fille new_mtilrs must dtc un indnessed eeeland fgroupsT_ct_c	@ and _te *attr,group:	Sdisc*nage be fillepir-ex e of laconst chna us@4 *storageops:	R *storageopst_ct_c	@gso_0]xus_ma:	Mfnit
 *s_maCor e code. segmeitcast o fdiscit_c	@gso_0]xusegs:	Mfnit
 *nouts for segmeit_ aill canthis if ntiev
rre    skNICwnageGSOt_c	@gso_0X quegs:	Mtr t
 *nouts for segmeit_ aill canthis if ntiev
rre    skNICwnageGSOt_ct_c	@tcbnlEops:	Data C BSDe Bdedget
e;
 y *  opst_c	@num,t.:	Nouts for notfreletranses Driversnetdefinedo_u	@*c_tetctq:	XXX:
(ext commeitwo
 * Waito tskbe@tneoountw t	XXX:
(ext commeitwo
 * Waito tskbo_u	@char	cdpuxio:	Maxtex *	willoutnageuck. LRO;op tdpchat_c	@pneomap:	XXX:
(ext commeitwo
 * Waito tskbe@thyt {:ePd by a nered.
 citl ttoth
 *
elf    sknagepidex.
 *aet_ntampompo_ut_c	@ided.,t	 X_LO	 * :	XXX:
(ext commeitwo
 * Waito tskbt_c	@group:		y be@roup,
aill soonered.
 belongsie skbe@pm_qow(stq:	Pny rmMfor networQoS structskbt_c	FIXME: gleanupcskb);

 int new_mtsue paill n regist */
strucinfot_c	movos.devdev_phhys_port_idnetdev_turer[64]		name[IFNAMSIZ]dece		*devhATE__ndev	name_hATE_scripti eee*ifrucasscr/*
	_c	I/Oict wirel  dev-s
	_c	FIXME: Mer nt_xmse c und, int cmed tte_ono ts	v_phd_len;
};
long		mem_o c;hd_len;
};
long		mem_Numbe;hd_len;
};
long		bld t_devesstruc nirqesc	/*
	_c	iomebpidex.
 * V_TXged. Ifre pridev-s *skinhaonsEATE_,
	_c	napiEATE_,when EATE_,nloteEATE_) dis
onal andrunna	_c	ck_tvo is nuurua nsetdct wirek din)Sdisc.c.s	v_phhd_len;
};
long		skinhiad rcu;
	sATE__t xp	onsEATE_;d rcu;
	sATE__t xp	napiEATE_;d rcu;
	sATE__t xp	when EATE_;d rcu;
	sATE__t xp	nloteEATE_iad rcu;
	sure rcu;
	sATE__t xpiupp/*;re rcu;
	sATE__t xpiany r;re} adjEATE_iad rcu;
	sure rcu;
	sATE__t xpiupp/*;re rcu;
	sATE__t xpiany r;re} all_adjEATE_esc	o_startcude <limincude <linc	o_startcude <liminhw_cude <linc	o_startcude <limin neted_cude <linc	o_startcude <limintlan cude <linc	o_startcude <liminhw_enc cude <linc	o_startcude <liminmpls cude <li aastruc nifid,
	esstruc nify * iad rcu;
	s;
} ____cat_stat	skini aas
struc_longmi nrx_drver o;as
struc_longmi ntx_drver o;aas
struc_teethnsigntct nets_XPS
/*
 * This sWIRELESS_EXT
	 size_skb,
 *iw_e gener_*
 * errndness_e geners;d rcu;
	siw_publuc_t nai errndness_t nauped(CONFs size_skb,
 *_idnetdev_aate)*netonsEops;Fs size_skb,
 *ethtoolEops *ethtoolEops;Fs size_skb,
 *nagx.
oet
_ct_queops *cwdEops;aas size_skb,
 *t xpgntops *t xpgntops;hhd_len;
};
truc  (*nd;hd_len;
};
truc * int (*nd;hhd_len;
};
t voi		g (*nd;hd_len;
};
t voi		*ato o;hhd_len;
};
r[64]	.  I*finh id_len;
};

typ		ATage_dev;hhd_len;
};
r[64]	ifr*	tr id_len;
};

typ		dma;hhd_len;
};
truc mtu;hd_len;
};
t voi		ache;hd_len;
};
t voi		tideut xpgntlen;hhd_len;
};
t voi		nextraut xproom;hd_len;
};
t voi		nextraut(*nroomesc	/* Ior low le for the nfo.v_phd_len;
};

typ		*ermt_devD_LENADDR_char id_len;
};

typee tor_asdr);_ache id_len;
};

typee tor_len;hd_len;
};
t voi		net sk_ int u16hd_len;
};
t voi          onsEio6hd_len;
};
t voi          onsE*	tr idspom	 * ptee tor_li *n	 * dece		*dev;
} {
	pw_ tor_li *	ucdece		*dev;
} {
	pw_ tor_li *	mcdece		*dev;
} {
	pw_ tor_li *	t {
be cd_XPS
/*
 * This sSYSFSece		*devksevee*id (*n_ksevupMAX_PHYSd_len;
};

typeename_asdr);_ache i
	dule	s	fc_nabled.;hd_len;
};
truc * bled.uity;hd_len;
};
truc eivmisci i
c	/* P*/
structt wirel nt (*ndsf_phh_PO G_IPV6_TUNNEL)
#deVLANu8021Q)ece		*devtlan  nfo __rcu	*tlan  nfoined(CONFIG_ G_IPV6_TUNNEL)
#dek_t_DSA)ece		*devtsa_c
 *ne_tref	*tsa_ptvined(CONFIG_ G_IPV6_TUNNEL)
#deTIPC)ece		*devaipc_beandr __rcu *aipc_ptvuped(CONFs, seleee* nalk_ptv;d rcu;
	sinnetdev_t__rcu	*ip_ptv;d rcu;
	sdnnetd __rcu     *dn_ptv;d rcu;
	sinet6netd __rcu	*ip6_ptv;d ate)( n* x25_ptv;d rcu;
	srrndness_tev	*ieee80211_ptvructure dCotheaoraese	spilyuurk don 
 *	chan	NETD(inclucomp*eth_ache_*
	 *())
v_phd_len;
};
long		definr	esc	/* Ior low le for the nfoeue probieth_ache_*
	 *()v_phd_len;
};

typ		*t {
be c i
cS
/*
 * This sSYSFSece		*dev;
} {
	te *attr	*nr	esc	_len;
};
truc r	h,
	    sts;hd_len;
};
truc rechar	h,
	    sts_XPSAX_PHYSd
	 e gener_cruc_t __rcu	*
	 e gener;d ate) __rcu		*
	 e gener_t naupece		*dev;
} {
	e of l__rcu *omp be     st id_len;
};

typeebrscious D_LENADDR_char iucture dCotheaoraese	spilyuurk don  The
 * n	NET
v_phde		*dev;
} {
	e of 	*ntxl____cotheorae_ruc;
};_X qump;hd_len;
};
truc r	h,t	    sts;hd_len;
};
truc rechar	h,t	    sts;hde		*devQded.ee*ided.;hd_len;
};
long		tx_ fallblen idspom	 * pteetx_@napi:n	 * _XPS
/*
 * This sXPSece		*devdif ons_maps __rcu *dif /* sined(CONFIG_LL
	spinlocps_may_expse		*dev(strrmap		*
	 (strrmapupMAX_PHYSd/tiTre prcitl* i(extraenagefutablen regis-pny r-e.
 fined i_phhd/*
	_c *
	 * Numbe*t 
 *ineexpo s vc*nagehighctt s addresssdon SMP,
	_c field ilate;
} {
	e of ->*
	 * Numbe*trans_s.s	v_phd_len;
};
long		*
	 * Numbe aastruc n n
 * st net_d;ece		*devaimer_li *	 n
 * st net_r aastru __*er(st		*p(strrefcnt;d rcu;
	sATE__t xp	*odo_ATE_upece		*devhATE__ndev	om/
s_hATE_;d rcu;
	sATE__t xp	orage n
 *_ATE_upece_sta{UP_MREGits NITIALIZED=0,
	       P_MREGiREGISTERED,d/ti doeleEsdtrn regisid (*ndw_mtu/
	       P_MREGiUNREGISTERING,d/ti e __newhen regisid (*ndw_mtu/
	       P_MREGiUNREGISTERED,d/ti doeleEsdtwhen regisl*odotu/
	       P_MREGiRELEASED,dd/ti e __nelrefid (*ndtu/
	       P_MREGiDUMMY,dd/ti ummy	statistcageNAPI ueuetu/
	} 
eg_*finh:8 i
	dule dedon. leupece_sta{re RTNL_LINK_ NITIALIZED,re RTNL_LINK_ NITIALIZING,
	} 
 *storage);
 e:16 aadate) (*t lp,
 iorice *dev);
 *	Called when anIG_LL
	spinlock_t		polle *dev);
 ueuekufact__rcu	*np nfoupMAX_PHYS_POCCEL
bool rk_t_NSece		*dev;
}		*ndid (upMAX_PHYSd/timid-qdiscttneric v_phd_lst o{re ate)( n		*mueue_f;re rcu;
	sp(strl*fini __*er(st		*lskini ae rcu;
	sp(strswid (*fini __*er(st	*tskini ae rcu;
	sp(strd*fini __*er(st		*dskini ae rcu;
	sp(strv*fini __*er(st		*vskini ae}upece		*devgarp
*	trt__rcu	*garp
*	tr;d rcu;
	smrp
*	trt__rcu	*mrp
*	tr;dd rcu;
	sdCalle	t {;Fs size_skb,
 *aconst ch,group * and _groups[4];Fs size_skb,
 *aconst ch,group * and _te *attr,group;aas size_skb,
 *4 *storageops *4 *storageopsesc	/* cage* are trk onel 

#i*aconst chdon TCPoharn *hen nme[IFN_phATE_DRV_GSOefineSIZE		65536hd_len;
};
truc gso_0]xus_ma;hATE_DRV_GSOefineSEGS		65535c	lock_ngso_0]xusegs;c	lock_ngso_0X quegs aTPOLL
	spinlocDCBas size_skb,
 *tcbnlE4 *stops *tcbnlEopsuped(CONFsu8 num,t.dece		*dev;
} {
	*c_txq *c_tetctq[TCefineQUEUEr id_8 tneoountw t[TCeBITMASK + 1];hh_PO G_IPV6_TUNNEL)
#de(CONFes_len;
};
truc  har	cdpuxioined(CONFIG_ G_IPV6_TUNNEL)
#deCGROUPck_t_	RIO)lle *dev);
 uneooed t__rcu *pneomapuped(CONFsrcu;
	sphyruct ifmaphyt {;d rcu;
	sA * ptrans_key *ided.,t	 X_LO	 * esstru group;asrcu;
	spm_qow(stqTU w	pm_qow(stq a}isATE_DRV_tet;
 ruct(d)ohartiongntof(dmce *dev);
 *	Calle,sdCa)STATE_DRV	P_MAX 8ALIGN		32hhysatel om	t ts	Calle} {
	gt nuneoountw t( size_skb,
 *_idnetdev_his f *uu16uneo)
{re_OK , Ndoveptneoountw t[tneo & TCeBITMASK];h}hhysatel om	t ts	Calle} {
	st nuneoountw t(e *dev, int new_mtu);
 *y8 uneo *y8 ttiu (stf (tc >=Ndovepnum,t.)lle_OK , N-EINVALesc	doveptneoountw t[tneo & TCeBITMASK] = tt & TCeBITMASK;re_OK , N0;h}hhysatel om	t tsate) ;
} {
	test ntcce *dev);
 *	Called whenu (sdovepnum,t. = 0;h	memst (dovep*c_tetctq, 0,*s_maof(dovep*c_tetctq));h	memst (doveptneoountw t, 0,*s_maof(doveptneoountw t));h}hhysatel om	t ts	Calle} {
	st nunt*attrce *dev, int new_mtu);
 *y8 ttaru(*cct;
}aru(*coffst iu (stf (tc >=Ndovepnum,t.)lle_OK , N-EINVALesc	dovep*c_tetctq[tc].ct;
} = ct;
};c	dovep*c_tetctq[tc].offst  = offst ;re_OK , N0;h}hhysatel om	t ts	Calle} {
	st nnum,t.ce *dev, int new_mtu);
 *y8 num,t.)l (stf (num,t. > TCefineQUEUE)lle_OK , N-EINVALesc	dovepnum,t. = num,t.dec_OK , N0;h}hhysatel om	t ts	Calle} {
	gt nnum,t.ce *dev, int new_mtu);
)
{re_OK , Ndovepnum,t.de}hhysatel om	t tse		*dev;
} {
	e of l*le} {
	gt ntx_ fall( size_skb,
 *_idnetdev_his f   sk		 y;
	int	(*parom/
s)
{re_OK , N&dovepntx[om/
s];h}hhysatel om	t t e		*dev;
} {
	e of l*skb	gt ntx_ fall( size_skb,
 *_idnetdev_his f   sk			_icohar *hskb, struct net_dev)
{re_OK , Nle} {
	gt ntx_ fall();
 *skb	gt n*attr,w to a (dev));h}hhysatel om	t t ate) ;
} {
	cag_eane_txt*attrce *dev, int new_mtu);
   sk		 **_ate) (*fice *dev);
 *	Called _i	 sk		 **_icskb);

 in {
	e of l*_i	 sk		 **_ic, sele)   sk		 **_ate) *arg)
{rey;
	int	(*paroesc	cage(i = 0; i <Ndovepnum,t	    sts; i++)llef();
 *&dovepntx[o], arg);h}hhys	*dev;
} {
	e of l*le} {
	pi* ptxce *dev, int new_mtu);
   sk	*_icskb);

ruct net_devi 	 sk **_ate) *at_queue_f)ructure dNetenameqdisc*om	t ts
v_physatel om	t tse		*dev;
}tu);
t;
 ( size_skb,
 *_idnetdev_his f)
{re_OK , Nr xp_p;
 (&dovepndid ();h}hhysatel om	t tsate) );
t;
 	st ce *dev, struct ifmap *map);
 *	 stl*le})
{r_POCCEL
bool rk_t_NSecndneld t;
 (dovepndid ();h	dovepndid ( = holdid ((d ();hed(CONF}hhysatel om	t t dule(se} {
	latsrd*ace *dev, int new_mtu);
)
{rIG_ G_IPV6_TUNNEL)
#dek_t_DSA)ectf (doveptsa_ptv != NULL)lle_OK , Ntsa_latsrtagged_ */
stru(doveptsa_ptv_uped(CONFs_OK , Nfalse;h}hhle - ente_t {
* in -set_qsitn registered.
 tneric vt naa us@dev:tn registered.
o_ut_c Gettn registered.
 tneric vt naa u/hysatel om	t t ate) *te_t {
* in( size_skb,
 *_idnetdev_his f)
{re_OK , N(

/* T)*ndt+ ALIGN(s_maof(skb,
 *_idnetdev_), P_MAX 8ALIGN);h}hhle Sel soontand fpd by a nered.
 referf_ch
 *
 * inn registlogy a nered.
t_c ifnsetdtneortes d  regINET t rror.calateantamy *  duset
e     or semst dev_phATE_DRV_S_t_N_MAX 8AX (d (, pp *)	((d ()eptev.parf_x = (pp *))hhle Sel soontand fered.
 ache
 *
 * inn registlogy a nered.
ge inivert_c _DRV-grelat	(*ntify tccast o fodifferf_xtn registered.
 aches. F unde exaoele E	2 onet, WindnsitLAN, Bluetooth, Wifin ettdev_phATE_DRV_S_t_N_MAX 8AX TYPE(d (, ereache_	((d ()eptev.ache
= (dovache_)hhle Dps(str NAPI ueue() wet srx_tiDred.
 the tranlrt e		ongmi/
dvi ntiev
* fulatebiggertvaluha uphATE_DRV_NAPI_		poAWEIGHT 64hhle - ente_if_napiEobal-      or sioe

* Wahartexta us@dev:ttn registered.
o_u	@napi:

* Wahartexta us@ueue: ueue */
cruct rxo_u	@wet sr:wops(str wet srx_t
 ho;
 if_napiEoba() he fo nuurk devi     or sioe

* Wahartextdtneortes  e _contexi*any*vo is nueceive;* Wandnskbdltruct rx_queu/sate) ;
}if_napiEoba(e *dev, struct ifmap *map);
 *	 t);
#ifdef C t);i 	  **_te_addi_struct *, int);
#ifdef C *	Ca) *	Calwet sr)isable -   ;
}if_napiEdell- removooe

* Wahartexta u  @napi:

* Wahartexta u -   ;
}if_napiEdel() removosoe

* Wahartextdtif_r* inn registdnew_mtna WaATE_ueu/sate) ;
}if_napiEdel(p);
 *	 t);
#ifdef C t);)isap);
 *	 t);
gro_cbo{rele Vevices  for theor skb	sh nfo(dev)->frags[0].paget+ offst .v_phdate) *frag0esc	/* Lap is o icrag0.v_phd_len;
};
nfigfrag0tlen;hhd/tiTr*  trse skbsith 
 *wpr the */_qsi */
ndnsk vc* whhanept na.v_phdnfigt na_offst ;rhd/tiTr*  tturen-zerovifwias be tra canunno numer nd 
 * If thnewhhan.v_phd_16	flush;rhd/tiSdle
ng.
IPiIDIh 
 *c unue hass to wprdeviuppias *
	 **	trtqdisct_phd_16	flushEio6hhd/tiNouts for segmeit_ aggr
gevfa.t_phd_16	ct;
};chd/tijiffiisss to /
rrinbe tra raansk_bufa/e of dv_phd_len;
};
long age;chd/tiork din)ipv6
gro_
 *	cha() c unfoo-sign-udpt_phd_16	 */
s;rhd/tiTr*  tturen-zerovifwias be tra citl* io is nusameacver.t_phd_8	sameet_ow:1;chd/tiork din)tu(n*
 GRO 
 *	chan_phd_8	encap,w rk:1;chd/tiGRO ue hasum ttutructn_phd_8	csumttruct:1;chd/tiNouts for ue hasumtutia CHECKSUM_ULNECESSARYn_phd_8	csumtcnt:3;chd/tiFrebing.
skb?n_phd_8	lref:2;hATE_DRV_NAPI_GRO_FREE	  *1hATE_DRV_NAPI_GRO_FREE_STOLENeHEAD 2chd/tiork din)foo-sign-udp,t
 */
s udp[46]
gro_
 *	chan_phd_8	is_ipv6:1;chd/ti7ebit hotai_phhd/*iNouts for gro_
 *	chanit*she tnp Waitbe tra alr xpy wpBSeadroughn_phd_8 
 *urs rx_ct;
}ss:4;chd/ti1ebit hotai_phhd/*iurk devie
 *	tr CHECKSUM_COMPLETE
 *
 *u(n*
 a Cu*/
strusf_ph	__wsum	csum;hhd/*iurk d
s skb	gro_
 *	cha() sver 	NETD_phde		*devruct net_defi;UE_STATE_DRV_NAPI_GRO_CB(dev) ((p);
 *	 t);
gro_cbo*)(dev)->cb)STATE_DRViGRO_RECURSION_LIMIT 15hysatel om	t t tru gro_
 *urs rx_iuc_test*skb, struct net_dev)
{re_OK , N++NAPI_GRO_CB(dev)->
 *urs rx_ct;
}ss ==iGRO_RECURSION_LIMIT;h}hhacheCCELskb, struct net_*(*gro_
 *	cha_f *skb,
 *         * *skb);

/*
 * Thi)isysatel om	t t e		*dev         *it*s	gro_
 *	cha(gro_
 *	cha_f cbm  sk			e *dev)         *t xpm  sk			e *dev)         dev)
{retf (unXMITly(gro_
 *urs rx_iuc_test*sev)))o{re NAPI_GRO_CB(dev)->flush |= 1;lle_OK , NNULL ae}
re_OK , Ncb(t xpm s stru}hhys	*devbe tra_acheo{reoid	(*			ache;d/tiTr*  tturechmi/htrx_(e	2 o_ache).v_phde		*dev;
}ruct if	*t {;d/tiNULL tturrodcartraeh 
 	 **_i_phdnfi	 naptrucf *skb, struct net_   sk		 e *dev);
 *	Called _i	 sk	 ys	*devbe tra_acheo_   sk		 e *dev);
 *	Called );h	dule	s	(*id,w tch *skb,
 *be tra_acheo_pache   sk		 **_skb, str
#i**s decdate)( n*af_be tra_ue_f;rercu;
	sATE__t xp	orfi;UE_STrcu;
	s fdisci_it*she tnp{hde		*devruct ne n*(*gsetucgmeitice *dev,signed char * 	 sk		o_startcude <limitcude <lii ase		*devruct ne n**(*gro_
 *	cha *skb,
 *         *t xpm  sk		 **_iccskb);

/*
 * This strustruc napgro_cdoeleEsice *dev,signed char *h	Callh fd);UE_STrcu;
	sbe tra_PUdiscir{reoid	(*			 ache;d/tiTr*  tturechmi/htrx_(e	2 o_ache).v_phde		*dev fdisci_it*she tnpit*she tn;rercu;
	sATE__t xp	 orfi;UE_STrcu;
	sudp_PUdiscir{reoid	(*			 *	tr id_8			 ip */
s;rde		*dev fdisci_it*she tnpit*she tn;r}isablv fIestmodirek d*fini  the ss cpu,ueceivelrt eh thdn(o_star->t_deviv_phys_portp(strswid (*fini {rey64*_iccrx_be tras;c	l64*_iccrx_bytes;c	l64*_icctx_be tras;c	l64*_icctx_bytes;c	rcu;
	su64t_stat_synciccsyncp;UE_STATE_DRV_o_start
t rcup(strsstat(ache_				\
({								\
	acheof(ache_ __*er(st *p(strsstat
= 
t rcuper(st(ache_; \retf (p(strsstat)	{					\
		nfigi;						\
		cag_eane_posues f (st(i)o{			\
			acheof(ache_ *ssta;			\
			ssta
= pgntcstrptr(p(strsstat*h	);	\
			u64t_stat_    (&_sta->tyncp);		\
		}						\
	}							\
	p(strsstat;						\
})STAincluce <	t ux/lotifier.h>sablvd (*ndw_mtlotifier
 *	in. Pield iremeuts fe .epdskbing.
rt;
 y * 
 ho;ofy tccast oexclus rx ATE_H
s rt;
 y * _eveit() w to atoet
enewt_c aches.a uphATE_DRV_N_MAX 8UP	0x0001d/tiF*
 n toyou can't vee in	statistip/e.
 fuphATE_DRV_N_MAX 8DOWN	0x0002hATE_DRV_N_MAX 8REBOOT	0x0003d/tiTeuelat */
struct neteann registfor low l 	 sk **oc Getif_abpidex.
 *crk;

c une skarufaa	 sk **- wprcantlate stage ge iki trecpt
 ss rx_t	 sk **oue. twinguphATE_DRV_N_MAX 8CHANGE	0x0004d/tiNotifststatiste(*nd* *	willuphATE_DRV_N_MAX 8REGISTER 0x0005hATE_DRV_N_MAX 8UNREGISTER	0x0006hATE_DRV_N_MAX 8CHANGEMTU	0x0007 /* lotifstaf}ss mtu* *	willhappen dv_phATE_DRV_N_MAX 8CHANGEADDR	0x0008hATE_DRV_N_MAX 8GOING8DOWN	0x0009hATE_DRV_N_MAX 8CHANGENAME	0x000AeOOT_SETUP_MAX 8 EAT8CHANGE	0x000BeOOT_SETUP_MAX 8BOTATE__FAILOVER 0x000CeOOT_SETUP_MAX 8PRE8UP		0x000DeOOT_SETUP_MAX 8PRE8TYPE8CHANGE	0x000EeOOT_SETUP_MAX 8POST8TYPE8CHANGE	0x000FeOOT_SETUP_MAX 8POST8 NIT	0x0010eOOT_SETUP_MAX 8UNREGISTER_FINAL 0x0011hATE_DRV_N_MAX 8RELEASE		0x0012hATE_DRV_N_MAX 8NOTIFY_PEERS	0x0013hATE_DRV_N_MAX 8JOIN		0x0014hATE_DRV_N_MAX 8CHANGEUPPER	0x0015hATE_DRV_N_MAX 8RESEND_IGMP	0x0016hATE_DRV_N_MAX 8PRECHANGEMTU	0x0017 /* lotifstbefolinmtu* *	willhappen dv_phATE_DRV_N_MAX 8CHANGEINFODATA	0x0018
s	Calen regisid (*ndw_m_lotifier(p);
 *	 otifier_bl
#i**nstru	Calwhen regisid (*ndw_m_lotifier(p);
 *	 otifier_bl
#i**nstruhys	*dev;
} {
	 otifier_ufact{hde		*dev;
}ruct ifmap *;UE_STrcu;
	s;
} {
	 otifier_ct net_ufact{hde		*dev;
} {
	 otifier_ufact nfou /* he fo nu/
rrin_phd_len;
};
nfigf(*nd_ct netd;UE_STrcatel om	t t ate) ;
} {
	 otifier_ufac_    (e		*dev;
} {
	 otifier_ufact*ifacm  sk		 **_iskb,
 *_idnetdev_his f)
{reifaceptev =Ndov;h}hhysatel om	t t e		*dev;
}netdev_hi
;
} {
	 otifier_ufac_toruct( size_skb,
 *_id {
	 otifier_ufac