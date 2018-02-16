/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2014 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitmap.h>
#include <linux/crc32.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/firmware.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <linux/init.h>
#include <linux/log2.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/rtnetlink.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/sockios.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <net/neighbour.h>
#include <net/netevent.h>
#include <net/addrconf.h>
#include <asm/uaccess.h>

#include "cxgb4.h"
#include "t4_regs.h"
#include "t4_msg.h"
#include "t4fw_api.h"
#include "cxgb4_dcb.h"
#include "l2t.h"

#include <../drivers/net/bonding/bonding.h>

#ifdef DRV_VERSION
#undef DRV_VERSION
#endif
#define DRV_VERSION "2.0.0-ko"
#define DRV_DESC "Chelsio T4/T5 Network Driver"

/*
 * Max interrupt hold-off timer value in us.  Queues fall back to this value
 * under extreme memory pressure so it's largish to give the system time to
 * recover.
 */
#define MAX_SGE_TIMERVAL 200U

enum {
	/*
	 * Physical Function provisioning constants.
	 */
	PFRES_NVI = 4,			/* # of Virtual Interfaces */
	PFRES_NETHCTRL = 128,		/* # of EQs used for ETH or CTRL Qs */
	PFRES_NIQFLINT = 128,		/* # of ingress Qs/w Free List(s)/intr
					 */
	PFRES_NEQ = 256,		/* # of egress queues */
	PFRES_NIQ = 0,			/* # of ingress queues */
	PFRES_TC = 0,			/* PCI-E traffic class */
	PFRES_NEXACTF = 128,		/* # of exact MPS filters */

	PFRES_R_CAPS = FW_CMD_CAP_PF,
	PFRES_WX_CAPS = FW_CMD_CAP_PF,

#ifdef CONFIG_PCI_IOV
	/*
	 * Virtual Function provisioning constants.  We need two extra Ingress
	 * Queues with Interrupt capability to serve as the VF's Firmware
	 * Event Queue and Forwarded Interrupt Queue (when using MSI mode) --
	 * neither will have Free Lists associated with them).  For each
	 * Ethernet/Control Egress Queue and for each Free List, we need an
	 * Egress Context.
	 */
	VFRES_NPORTS = 1,		/* # of "ports" per VF */
	VFRES_NQSETS = 2,		/* # of "Queue Sets" per VF */

	VFRES_NVI = VFRES_NPORTS,	/* # of Virtual Interfaces */
	VFRES_NETHCTRL = VFRES_NQSETS,	/* # of EQs used for ETH or CTRL Qs */
	VFRES_NIQFLINT = VFRES_NQSETS+2,/* # of ingress Qs/w Free List(s)/intr */
	VFRES_NEQ = VFRES_NQSETS*2,	/* # of egress queues */
	VFRES_NIQ = 0,			/* # of non-fl/int ingress queues */
	VFRES_TC = 0,			/* PCI-E traffic class */
	VFRES_NEXACTF = 16,		/* # of exact MPS filters */

	VFRES_R_CAPS = FW_CMD_CAP_DMAQ|FW_CMD_CAP_VF|FW_CMD_CAP_PORT,
	VFRES_WX_CAPS = FW_CMD_CAP_DMAQ|FW_CMD_CAP_VF,
#endif
};

/*
 * Provide a Port Access Rights Mask for the specified PF/VF.  This is very
 * static and likely not to be useful in the long run.  We really need to
 * implement some form of persistent configuration which the firmware
 * controls.
 */
static unsigned int pfvfres_pmask(struct adapter *adapter,
				  unsigned int pf, unsigned int vf)
{
	unsigned int portn, portvec;

	/*
	 * Give PF's access to all of the ports.
	 */
	if (vf == 0)
		return FW_PFVF_CMD_PMASK_MASK;

	/*
	 * For VFs, we'll assign them access to the ports based purely on the
	 * PF.  We assign active ports in order, wrapping around if there are
	 * fewer active ports than PFs: e.g. active port[pf % nports].
	 * Unfortunately the adapter's port_info structs haven't been
	 * initialized yet so we have to compute this.
	 */
	if (adapter->params.nports == 0)
		return 0;

	portn = pf % adapter->params.nports;
	portvec = adapter->params.portvec;
	for (;;) {
		/*
		 * Isolate the lowest set bit in the port vector.  If we're at
		 * the port number that we want, return that as the pmask.
		 * otherwise mask that bit out of the port vector and
		 * decrement our port number ...
		 */
		unsigned int pmask = portvec ^ (portvec & (portvec-1));
		if (portn == 0)
			return pmask;
		portn--;
		portvec &= ~pmask;
	}
	/*NOTREACHED*/
}

enum {
	MAX_TXQ_ENTRIES      = 16384,
	MAX_CTRL_TXQ_ENTRIES = 1024,
	MAX_RSPQ_ENTRIES     = 16384,
	MAX_RX_BUFFERS       = 16384,
	MIN_TXQ_ENTRIES      = 32,
	MIN_CTRL_TXQ_ENTRIES = 32,
	MIN_RSPQ_ENTRIES     = 128,
	MIN_FL_ENTRIES       = 16
};

/* Host shadow copy of ingress filter entry.  This is in host native format
 * and doesn't match the ordering or bit order, etc. of the hardware of the
 * firmware command.  The use of bit-field structure elements is purely to
 * remind ourselves of the field size limitations and save memory in the case
 * where the filter table is large.
 */
struct filter_entry {
	/* Administrative fields for filter.
	 */
	u32 valid:1;            /* filter allocated and valid */
	u32 locked:1;           /* filter is administratively locked */

	u32 pending:1;          /* filter action is pending firmware reply */
	u32 smtidx:8;           /* Source MAC Table index for smac */
	struct l2t_entry *l2t;  /* Layer Two Table entry for dmac */

	/* The filter itself.  Most of this is a straight copy of information
	 * provided by the extended ioctl().  Some fields are translated to
	 * internal forms -- for instance the Ingress Queue ID passed in from
	 * the ioctl() is translated into the Absolute Ingress Queue ID.
	 */
	struct ch_filter_specification fs;
};

#define DFLT_MSG_ENABLE (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK | \
			 NETIF_MSG_TIMER | NETIF_MSG_IFDOWN | NETIF_MSG_IFUP |\
			 NETIF_MSG_RX_ERR | NETIF_MSG_TX_ERR)

#define CH_DEVICE(devid, data) { PCI_VDEVICE(CHELSIO, devid), (data) }

static const struct pci_device_id cxgb4_pci_tbl[] = {
	CH_DEVICE(0xa000, 0),  /* PE10K */
	CH_DEVICE(0x4001, -1),
	CH_DEVICE(0x4002, -1),
	CH_DEVICE(0x4003, -1),
	CH_DEVICE(0x4004, -1),
	CH_DEVICE(0x4005, -1),
	CH_DEVICE(0x4006, -1),
	CH_DEVICE(0x4007, -1),
	CH_DEVICE(0x4008, -1),
	CH_DEVICE(0x4009, -1),
	CH_DEVICE(0x400a, -1),
	CH_DEVICE(0x400d, -1),
	CH_DEVICE(0x400e, -1),
	CH_DEVICE(0x4080, -1),
	CH_DEVICE(0x4081, -1),
	CH_DEVICE(0x4082, -1),
	CH_DEVICE(0x4083, -1),
	CH_DEVICE(0x4084, -1),
	CH_DEVICE(0x4085, -1),
	CH_DEVICE(0x4086, -1),
	CH_DEVICE(0x4087, -1),
	CH_DEVICE(0x4088, -1),
	CH_DEVICE(0x4401, 4),
	CH_DEVICE(0x4402, 4),
	CH_DEVICE(0x4403, 4),
	CH_DEVICE(0x4404, 4),
	CH_DEVICE(0x4405, 4),
	CH_DEVICE(0x4406, 4),
	CH_DEVICE(0x4407, 4),
	CH_DEVICE(0x4408, 4),
	CH_DEVICE(0x4409, 4),
	CH_DEVICE(0x440a, 4),
	CH_DEVICE(0x440d, 4),
	CH_DEVICE(0x440e, 4),
	CH_DEVICE(0x4480, 4),
	CH_DEVICE(0x4481, 4),
	CH_DEVICE(0x4482, 4),
	CH_DEVICE(0x4483, 4),
	CH_DEVICE(0x4484, 4),
	CH_DEVICE(0x4485, 4),
	CH_DEVICE(0x4486, 4),
	CH_DEVICE(0x4487, 4),
	CH_DEVICE(0x4488, 4),
	CH_DEVICE(0x5001, 4),
	CH_DEVICE(0x5002, 4),
	CH_DEVICE(0x5003, 4),
	CH_DEVICE(0x5004, 4),
	CH_DEVICE(0x5005, 4),
	CH_DEVICE(0x5006, 4),
	CH_DEVICE(0x5007, 4),
	CH_DEVICE(0x5008, 4),
	CH_DEVICE(0x5009, 4),
	CH_DEVICE(0x500A, 4),
	CH_DEVICE(0x500B, 4),
	CH_DEVICE(0x500C, 4),
	CH_DEVICE(0x500D, 4),
	CH_DEVICE(0x500E, 4),
	CH_DEVICE(0x500F, 4),
	CH_DEVICE(0x5010, 4),
	CH_DEVICE(0x5011, 4),
	CH_DEVICE(0x5012, 4),
	CH_DEVICE(0x5013, 4),
	CH_DEVICE(0x5014, 4),
	CH_DEVICE(0x5015, 4),
	CH_DEVICE(0x5080, 4),
	CH_DEVICE(0x5081, 4),
	CH_DEVICE(0x5082, 4),
	CH_DEVICE(0x5083, 4),
	CH_DEVICE(0x5084, 4),
	CH_DEVICE(0x5085, 4),
	CH_DEVICE(0x5086, 4),
	CH_DEVICE(0x5087, 4),
	CH_DEVICE(0x5088, 4),
	CH_DEVICE(0x5401, 4),
	CH_DEVICE(0x5402, 4),
	CH_DEVICE(0x5403, 4),
	CH_DEVICE(0x5404, 4),
	CH_DEVICE(0x5405, 4),
	CH_DEVICE(0x5406, 4),
	CH_DEVICE(0x5407, 4),
	CH_DEVICE(0x5408, 4),
	CH_DEVICE(0x5409, 4),
	CH_DEVICE(0x540A, 4),
	CH_DEVICE(0x540B, 4),
	CH_DEVICE(0x540C, 4),
	CH_DEVICE(0x540D, 4),
	CH_DEVICE(0x540E, 4),
	CH_DEVICE(0x540F, 4),
	CH_DEVICE(0x5410, 4),
	CH_DEVICE(0x5411, 4),
	CH_DEVICE(0x5412, 4),
	CH_DEVICE(0x5413, 4),
	CH_DEVICE(0x5414, 4),
	CH_DEVICE(0x5415, 4),
	CH_DEVICE(0x5480, 4),
	CH_DEVICE(0x5481, 4),
	CH_DEVICE(0x5482, 4),
	CH_DEVICE(0x5483, 4),
	CH_DEVICE(0x5484, 4),
	CH_DEVICE(0x5485, 4),
	CH_DEVICE(0x5486, 4),
	CH_DEVICE(0x5487, 4),
	CH_DEVICE(0x5488, 4),
	{ 0, }
};

#define FW4_FNAME "cxgb4/t4fw.bin"
#define FW5_FNAME "cxgb4/t5fw.bin"
#define FW4_CFNAME "cxgb4/t4-config.txt"
#define FW5_CFNAME "cxgb4/t5-config.txt"

MODULE_DESCRIPTION(DRV_DESC);
MODULE_AUTHOR("Chelsio Communications");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, cxgb4_pci_tbl);
MODULE_FIRMWARE(FW4_FNAME);
MODULE_FIRMWARE(FW5_FNAME);

/*
 * Normally we're willing to become the firmware's Master PF but will be happy
 * if another PF has already become the Master and initialized the adapter.
 * Setting "force_init" will cause this driver to forcibly establish itself as
 * the Master PF and initialize the adapter.
 */
static uint force_init;

module_param(force_init, uint, 0644);
MODULE_PARM_DESC(force_init, "Forcibly become Master PF and initialize adapter");

/*
 * Normally if the firmware we connect to has Configuration File support, we
 * use that and only fall back to the old Driver-based initialization if the
 * Configuration File fails for some reason.  If force_old_init is set, then
 * we'll always use the old Driver-based initialization sequence.
 */
static uint force_old_init;

module_param(force_old_init, uint, 0644);
MODULE_PARM_DESC(force_old_init, "Force old initialization sequence");

static int dflt_msg_enable = DFLT_MSG_ENABLE;

module_param(dflt_msg_enable, int, 0644);
MODULE_PARM_DESC(dflt_msg_enable, "Chelsio T4 default message enable bitmap");

/*
 * The driver uses the best interrupt scheme available on a platform in the
 * order MSI-X, MSI, legacy INTx interrupts.  This parameter determines which
 * of these schemes the driver may consider as follows:
 *
 * msi = 2: choose from among all three options
 * msi = 1: only consider MSI and INTx interrupts
 * msi = 0: force INTx interrupts
 */
static int msi = 2;

module_param(msi, int, 0644);
MODULE_PARM_DESC(msi, "whether to use INTx (0), MSI (1) or MSI-X (2)");

/*
 * Queue interrupt hold-off timer values.  Queues default to the first of these
 * upon creation.
 */
static unsigned int intr_holdoff[SGE_NTIMERS - 1] = { 5, 10, 20, 50, 100 };

module_param_array(intr_holdoff, uint, NULL, 0644);
MODULE_PARM_DESC(intr_holdoff, "values for queue interrupt hold-off timers "
		 "0..4 in microseconds");

static unsigned int intr_cnt[SGE_NCOUNTERS - 1] = { 4, 8, 16 };

module_param_array(intr_cnt, uint, NULL, 0644);
MODULE_PARM_DESC(intr_cnt,
		 "thresholds 1..3 for queue interrupt packet counters");

/*
 * Normally we tell the chip to deliver Ingress Packets into our DMA buffers
 * offset by 2 bytes in order to have the IP headers line up on 4-byte
 * boundaries.  This is a requirement for many architectures which will throw
 * a machine check fault if an attempt is made to access one of the 4-byte IP
 * header fields on a non-4-byte boundary.  And it's a major performance issue
 * even on some architectures which allow it like some implementations of the
 * x86 ISA.  However, some architectures don't mind this and for some very
 * edge-case performance sensitive applications (like forwarding large volumes
 * of small packets), setting this DMA offset to 0 will decrease the number of
 * PCI-E Bus transfers enough to measurably affect performance.
 */
static int rx_dma_offset = 2;

static bool vf_acls;

#ifdef CONFIG_PCI_IOV
module_param(vf_acls, bool, 0644);
MODULE_PARM_DESC(vf_acls, "if set enable virtualization L2 ACL enforcement");

/* Configure the number of PCI-E Virtual Function which are to be instantiated
 * on SR-IOV Capable Physical Functions.
 */
static unsigned int num_vf[NUM_OF_PF_WITH_SRIOV];

module_param_array(num_vf, uint, NULL, 0644);
MODULE_PARM_DESC(num_vf, "number of VFs for each of PFs 0-3");
#endif

/* TX Queue select used to determine what algorithm to use for selecting TX
 * queue. Select between the kernel provided function (select_queue=0) or user
 * cxgb_select_queue function (select_queue=1)
 *
 * Default: select_queue=0
 */
static int select_queue;
module_param(select_queue, int, 0644);
MODULE_PARM_DESC(select_queue,
		 "Select between kernel provided method of selecting or driver method of selecting TX queue. Default is kernel method.");

/*
 * The filter TCAM has a fixed portion and a variable portion.  The fixed
 * portion can match on source/destination IP IPv4/IPv6 addresses and TCP/UDP
 * ports.  The variable portion is 36 bits which can include things like Exact
 * Match MAC Index (9 bits), Ether Type (16 bits), IP Protocol (8 bits),
 * [Inner] VLAN Tag (17 bits), etc. which, if all were somehow selected, would
 * far exceed the 36-bit budget for this "compressed" header portion of the
 * filter.  Thus, we have a scarce resource which must be carefully managed.
 *
 * By default we set this up to mostly match the set of filter matching
 * capabilities of T3 but with accommodations for some of T4's more
 * interesting features:
 *
 *   { IP Fragment (1), MPS Match Type (3), IP Protocol (8),
 *     [Inner] VLAN (17), Port (3), FCoE (1) }
 */
enum {
	TP_VLAN_PRI_MAP_DEFAULT = HW_TPL_FR_MT_PR_IV_P_FC,
	TP_VLAN_PRI_MAP_FIRST = FCOE_SHIFT,
	TP_VLAN_PRI_MAP_LAST = FRAGMENTATION_SHIFT,
};

static unsigned int tp_vlan_pri_map = TP_VLAN_PRI_MAP_DEFAULT;

module_param(tp_vlan_pri_map, uint, 0644);
MODULE_PARM_DESC(tp_vlan_pri_map, "global compressed filter configuration");

static struct dentry *cxgb4_debugfs_root;

static LIST_HEAD(adapter_list);
static DEFINE_MUTEX(uld_mutex);
/* Adapter list to be accessed from atomic context */
static LIST_HEAD(adap_rcu_list);
static DEFINE_SPINLOCK(adap_rcu_lock);
static struct cxgb4_uld_info ulds[CXGB4_ULD_MAX];
static const char *uld_str[] = { "RDMA", "iSCSI" };

static void link_report(struct net_device *dev)
{
	if (!netif_carrier_ok(dev))
		netdev_info(dev, "link down\n");
	else {
		static const char *fc[] = { "no", "Rx", "Tx", "Tx/Rx" };

		const char *s = "10Mbps";
		const struct port_info *p = netdev_priv(dev);

		switch (p->link_cfg.speed) {
		case 10000:
			s = "10Gbps";
			break;
		case 1000:
			s = "1000Mbps";
			break;
		case 100:
			s = "100Mbps";
			break;
		case 40000:
			s = "40Gbps";
			break;
		}

		netdev_info(dev, "link up, %s, full-duplex, %s PAUSE\n", s,
			    fc[p->link_cfg.fc]);
	}
}

#ifdef CONFIG_CHELSIO_T4_DCB
/* Set up/tear down Data Center Bridging Priority mapping for a net device. */
static void dcb_tx_queue_prio_enable(struct net_device *dev, int enable)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	struct sge_eth_txq *txq = &adap->sge.ethtxq[pi->first_qset];
	int i;

	/* We use a simple mapping of Port TX Queue Index to DCB
	 * Priority when we're enabling DCB.
	 */
	for (i = 0; i < pi->nqsets; i++, txq++) {
		u32 name, value;
		int err;

		name = (FW_PARAMS_MNEM(FW_PARAMS_MNEM_DMAQ) |
			FW_PARAMS_PARAM_X(FW_PARAMS_PARAM_DMAQ_EQ_DCBPRIO_ETH) |
			FW_PARAMS_PARAM_YZ(txq->q.cntxt_id));
		value = enable ? i : 0xffffffff;

		/* Since we can be called while atomic (from "interrupt
		 * level") we need to issue the Set Parameters Commannd
		 * without sleeping (timeout < 0).
		 */
		err = t4_set_params_nosleep(adap, adap->mbox, adap->fn, 0, 1,
					    &name, &value);

		if (err)
			dev_err(adap->pdev_dev,
				"Can't %s DCB Priority on port %d, TX Queue %d: err=%d\n",
				enable ? "set" : "unset", pi->port_id, i, -err);
		else
			txq->dcb_prio = value;
	}
}
#endif /* CONFIG_CHELSIO_T4_DCB */

void t4_os_link_changed(struct adapter *adapter, int port_id, int link_stat)
{
	struct net_device *dev = adapter->port[port_id];

	/* Skip changes from disabled ports. */
	if (netif_running(dev) && link_stat != netif_carrier_ok(dev)) {
		if (link_stat)
			netif_carrier_on(dev);
		else {
#ifdef CONFIG_CHELSIO_T4_DCB
			cxgb4_dcb_state_init(dev);
			dcb_tx_queue_prio_enable(dev, false);
#endif /* CONFIG_CHELSIO_T4_DCB */
			netif_carrier_off(dev);
		}

		link_report(dev);
	}
}

void t4_os_portmod_changed(const struct adapter *adap, int port_id)
{
	static const char *mod_str[] = {
		NULL, "LR", "SR", "ER", "passive DA", "active DA", "LRM"
	};

	const struct net_device *dev = adap->port[port_id];
	const struct port_info *pi = netdev_priv(dev);

	if (pi->mod_type == FW_PORT_MOD_TYPE_NONE)
		netdev_info(dev, "port module unplugged\n");
	else if (pi->mod_type < ARRAY_SIZE(mod_str))
		netdev_info(dev, "%s module inserted\n", mod_str[pi->mod_type]);
}

/*
 * Configure the exact and hash address filters to handle a port's multicast
 * and secondary unicast MAC addresses.
 */
static int set_addr_filters(const struct net_device *dev, bool sleep)
{
	u64 mhash = 0;
	u64 uhash = 0;
	bool free = true;
	u16 filt_idx[7];
	const u8 *addr[7];
	int ret, naddr = 0;
	const struct netdev_hw_addr *ha;
	int uc_cnt = netdev_uc_count(dev);
	int mc_cnt = netdev_mc_count(dev);
	const struct port_info *pi = netdev_priv(dev);
	unsigned int mb = pi->adapter->fn;

	/* first do the secondary unicast addresses */
	netdev_for_each_uc_addr(ha, dev) {
		addr[naddr++] = ha->addr;
		if (--uc_cnt == 0 || naddr >= ARRAY_SIZE(addr)) {
			ret = t4_alloc_mac_filt(pi->adapter, mb, pi->viid, free,
					naddr, addr, filt_idx, &uhash, sleep);
			if (ret < 0)
				return ret;

			free = false;
			naddr = 0;
		}
	}

	/* next set up the multicast addresses */
	netdev_for_each_mc_addr(ha, dev) {
		addr[naddr++] = ha->addr;
		if (--mc_cnt == 0 || naddr >= ARRAY_SIZE(addr)) {
			ret = t4_alloc_mac_filt(pi->adapter, mb, pi->viid, free,
					naddr, addr, filt_idx, &mhash, sleep);
			if (ret < 0)
				return ret;

			free = false;
			naddr = 0;
		}
	}

	return t4_set_addr_hash(pi->adapter, mb, pi->viid, uhash != 0,
				uhash | mhash, sleep);
}

int dbfifo_int_thresh = 10; /* 10 == 640 entry threshold */
module_param(dbfifo_int_thresh, int, 0644);
MODULE_PARM_DESC(dbfifo_int_thresh, "doorbell fifo interrupt threshold");

/*
 * usecs to sleep while draining the dbfifo
 */
static int dbfifo_drain_delay = 1000;
module_param(dbfifo_drain_delay, int, 0644);
MODULE_PARM_DESC(dbfifo_drain_delay,
		 "usecs to sleep while draining the dbfifo");

/*
 * Set Rx properties of a port, such as promiscruity, address filters, and MTU.
 * If @mtu is -1 it is left unchanged.
 */
static int set_rxmode(struct net_device *dev, int mtu, bool sleep_ok)
{
	int ret;
	struct port_info *pi = netdev_priv(dev);

	ret = set_addr_filters(dev, sleep_ok);
	if (ret == 0)
		ret = t4_set_rxmode(pi->adapter, pi->adapter->fn, pi->viid, mtu,
				    (dev->flags & IFF_PROMISC) ? 1 : 0,
				    (dev->flags & IFF_ALLMULTI) ? 1 : 0, 1, -1,
				    sleep_ok);
	return ret;
}

/**
 *	link_start - enable a port
 *	@dev: the port to enable
 *
 *	Performs the MAC and PHY actions needed to enable a port.
 */
static int link_start(struct net_device *dev)
{
	int ret;
	struct port_info *pi = netdev_priv(dev);
	unsigned int mb = pi->adapter->fn;

	/*
	 * We do not set address filters and promiscuity here, the stack does
	 * that step explicitly.
	 */
	ret = t4_set_rxmode(pi->adapter, mb, pi->viid, dev->mtu, -1, -1, -1,
			    !!(dev->features & NETIF_F_HW_VLAN_CTAG_RX), true);
	if (ret == 0) {
		ret = t4_change_mac(pi->adapter, mb, pi->viid,
				    pi->xact_addr_filt, dev->dev_addr, true,
				    true);
		if (ret >= 0) {
			pi->xact_addr_filt = ret;
			ret = 0;
		}
	}
	if (ret == 0)
		ret = t4_link_start(pi->adapter, mb, pi->tx_chan,
				    &pi->link_cfg);
	if (ret == 0) {
		local_bh_disable();
		ret = t4_enable_vi_params(pi->adapter, mb, pi->viid, true,
					  true, CXGB4_DCB_ENABLED);
		local_bh_enable();
	}

	return ret;
}

int cxgb4_dcb_enabled(const struct net_device *dev)
{
#ifdef CONFIG_CHELSIO_T4_DCB
	struct port_info *pi = netdev_priv(dev);

	if (!pi->dcb.enabled)
		return 0;

	return ((pi->dcb.state == CXGB4_DCB_STATE_FW_ALLSYNCED) ||
		(pi->dcb.state == CXGB4_DCB_STATE_HOST));
#else
	return 0;
#endif
}
EXPORT_SYMBOL(cxgb4_dcb_enabled);

#ifdef CONFIG_CHELSIO_T4_DCB
/* Handle a Data Center Bridging update message from the firmware. */
static void dcb_rpl(struct adapter *adap, const struct fw_port_cmd *pcmd)
{
	int port = FW_PORT_CMD_PORTID_GET(ntohl(pcmd->op_to_portid));
	struct net_device *dev = adap->port[port];
	int old_dcb_enabled = cxgb4_dcb_enabled(dev);
	int new_dcb_enabled;

	cxgb4_dcb_handle_fw_update(adap, pcmd);
	new_dcb_enabled = cxgb4_dcb_enabled(dev);

	/* If the DCB has become enabled or disabled on the port then we're
	 * going to need to set up/tear down DCB Priority parameters for the
	 * TX Queues associated with the port.
	 */
	if (new_dcb_enabled != old_dcb_enabled)
		dcb_tx_queue_prio_enable(dev, new_dcb_enabled);
}
#endif /* CONFIG_CHELSIO_T4_DCB */

/* Clear a filter and release any of its resources that we own.  This also
 * clears the filter's "pending" status.
 */
static void clear_filter(struct adapter *adap, struct filter_entry *f)
{
	/* If the new or old filter have loopback rewriteing rules then we'll
	 * need to free any existing Layer Two Table (L2T) entries of the old
	 * filter rule.  The firmware will handle freeing up any Source MAC
	 * Table (SMT) entries used for rewriting Source MAC Addresses in
	 * loopback rules.
	 */
	if (f->l2t)
		cxgb4_l2t_release(f->l2t);

	/* The zeroing of the filter rule below clears the filter valid,
	 * pending, locked flags, l2t pointer, etc. so it's all we need for
	 * this operation.
	 */
	memset(f, 0, sizeof(*f));
}

/* Handle a filter write/deletion reply.
 */
static void filter_rpl(struct adapter *adap, const struct cpl_set_tcb_rpl *rpl)
{
	unsigned int idx = GET_TID(rpl);
	unsigned int nidx = idx - adap->tids.ftid_base;
	unsigned int ret;
	struct filter_entry *f;

	if (idx >= adap->tids.ftid_base && nidx <
	   (adap->tids.nftids + adap->tids.nsftids)) {
		idx = nidx;
		ret = GET_TCB_COOKIE(rpl->cookie);
		f = &adap->tids.ftid_tab[idx];

		if (ret == FW_FILTER_WR_FLT_DELETED) {
			/* Clear the filter when we get confirmation from the
			 * hardware that the filter has been deleted.
			 */
			clear_filter(adap, f);
		} else if (ret == FW_FILTER_WR_SMT_TBL_FULL) {
			dev_err(adap->pdev_dev, "filter %u setup failed due to full SMT\n",
				idx);
			clear_filter(adap, f);
		} else if (ret == FW_FILTER_WR_FLT_ADDED) {
			f->smtidx = (be64_to_cpu(rpl->oldval) >> 24) & 0xff;
			f->pending = 0;  /* asynchronous setup completed */
			f->valid = 1;
		} else {
			/* Something went wrong.  Issue a warning about the
			 * problem and clear everything out.
			 */
			dev_err(adap->pdev_dev, "filter %u setup failed with error %u\n",
				idx, ret);
			clear_filter(adap, f);
		}
	}
}

/* Response queue handler for the FW event queue.
 */
static int fwevtq_handler(struct sge_rspq *q, const __be64 *rsp,
			  const struct pkt_gl *gl)
{
	u8 opcode = ((const struct rss_header *)rsp)->opcode;

	rsp++;                                          /* skip RSS header */

	/* FW can send EGR_UPDATEs encapsulated in a CPL_FW4_MSG.
	 */
	if (unlikely(opcode == CPL_FW4_MSG &&
	   ((const struct cpl_fw4_msg *)rsp)->type == FW_TYPE_RSSCPL)) {
		rsp++;
		opcode = ((const struct rss_header *)rsp)->opcode;
		rsp++;
		if (opcode != CPL_SGE_EGR_UPDATE) {
			dev_err(q->adap->pdev_dev, "unexpected FW4/CPL %#x on FW event queue\n"
				, opcode);
			goto out;
		}
	}

	if (likely(opcode == CPL_SGE_EGR_UPDATE)) {
		const struct cpl_sge_egr_update *p = (void *)rsp;
		unsigned int qid = EGR_QID(ntohl(p->opcode_qid));
		struct sge_txq *txq;

		txq = q->adap->sge.egr_map[qid - q->adap->sge.egr_start];
		txq->restarts++;
		if ((u8 *)txq < (u8 *)q->adap->sge.ofldtxq) {
			struct sge_eth_txq *eq;

			eq = container_of(txq, struct sge_eth_txq, q);
			netif_tx_wake_queue(eq->txq);
		} else {
			struct sge_ofld_txq *oq;

			oq = container_of(txq, struct sge_ofld_txq, q);
			tasklet_schedule(&oq->qresume_tsk);
		}
	} else if (opcode == CPL_FW6_MSG || opcode == CPL_FW4_MSG) {
		const struct cpl_fw6_msg *p = (void *)rsp;

#ifdef CONFIG_CHELSIO_T4_DCB
		const struct fw_port_cmd *pcmd = (const void *)p->data;
		unsigned int cmd = FW_CMD_OP_GET(ntohl(pcmd->op_to_portid));
		unsigned int action =
			FW_PORT_CMD_ACTION_GET(ntohl(pcmd->action_to_len16));

		if (cmd == FW_PORT_CMD &&
		    action == FW_PORT_ACTION_GET_PORT_INFO) {
			int port = FW_PORT_CMD_PORTID_GET(
					be32_to_cpu(pcmd->op_to_portid));
			struct net_device *dev = q->adap->port[port];
			int state_input = ((pcmd->u.info.dcbxdis_pkd &
					    FW_PORT_CMD_DCBXDIS)
					   ? CXGB4_DCB_INPUT_FW_DISABLED
					   : CXGB4_DCB_INPUT_FW_ENABLED);

			cxgb4_dcb_state_fsm(dev, state_input);
		}

		if (cmd == FW_PORT_CMD &&
		    action == FW_PORT_ACTION_L2_DCB_CFG)
			dcb_rpl(q->adap, pcmd);
		else
#endif
			if (p->type == 0)
				t4_handle_fw_rpl(q->adap, p->data);
	} else if (opcode == CPL_L2T_WRITE_RPL) {
		const struct cpl_l2t_write_rpl *p = (void *)rsp;

		do_l2t_write_rpl(q->adap, p);
	} else if (opcode == CPL_SET_TCB_RPL) {
		const struct cpl_set_tcb_rpl *p = (void *)rsp;

		filter_rpl(q->adap, p);
	} else
		dev_err(q->adap->pdev_dev,
			"unexpected CPL %#x on FW event queue\n", opcode);
out:
	return 0;
}

/**
 *	uldrx_handler - response queue handler for ULD queues
 *	@q: the response queue that received the packet
 *	@rsp: the response queue descriptor holding the offload message
 *	@gl: the gather list of packet fragments
 *
 *	Deliver an ingress offload packet to a ULD.  All processing is done by
 *	the ULD, we just maintain statistics.
 */
static int uldrx_handler(struct sge_rspq *q, const __be64 *rsp,
			 const struct pkt_gl *gl)
{
	struct sge_ofld_rxq *rxq = container_of(q, struct sge_ofld_rxq, rspq);

	/* FW can send CPLs encapsulated in a CPL_FW4_MSG.
	 */
	if (((const struct rss_header *)rsp)->opcode == CPL_FW4_MSG &&
	    ((const struct cpl_fw4_msg *)(rsp + 1))->type == FW_TYPE_RSSCPL)
		rsp += 2;

	if (ulds[q->uld].rx_handler(q->adap->uld_handle[q->uld], rsp, gl)) {
		rxq->stats.nomem++;
		return -1;
	}
	if (gl == NULL)
		rxq->stats.imm++;
	else if (gl == CXGB4_MSG_AN)
		rxq->stats.an++;
	else
		rxq->stats.pkts++;
	return 0;
}

static void disable_msi(struct adapter *adapter)
{
	if (adapter->flags & USING_MSIX) {
		pci_disable_msix(adapter->pdev);
		adapter->flags &= ~USING_MSIX;
	} else if (adapter->flags & USING_MSI) {
		pci_disable_msi(adapter->pdev);
		adapter->flags &= ~USING_MSI;
	}
}

/*
 * Interrupt handler for non-data events used with MSI-X.
 */
static irqreturn_t t4_nondata_intr(int irq, void *cookie)
{
	struct adapter *adap = cookie;

	u32 v = t4_read_reg(adap, MYPF_REG(PL_PF_INT_CAUSE));
	if (v & PFSW) {
		adap->swintr = 1;
		t4_write_reg(adap, MYPF_REG(PL_PF_INT_CAUSE), v);
	}
	t4_slow_intr_handler(adap);
	return IRQ_HANDLED;
}

/*
 * Name the MSI-X interrupts.
 */
static void name_msix_vecs(struct adapter *adap)
{
	int i, j, msi_idx = 2, n = sizeof(adap->msix_info[0].desc);

	/* non-data interrupts */
	snprintf(adap->msix_info[0].desc, n, "%s", adap->port[0]->name);

	/* FW events */
	snprintf(adap->msix_info[1].desc, n, "%s-FWeventq",
		 adap->port[0]->name);

	/* Ethernet queues */
	for_each_port(adap, j) {
		struct net_device *d = adap->port[j];
		const struct port_info *pi = netdev_priv(d);

		for (i = 0; i < pi->nqsets; i++, msi_idx++)
			snprintf(adap->msix_info[msi_idx].desc, n, "%s-Rx%d",
				 d->name, i);
	}

	/* offload queues */
	for_each_ofldrxq(&adap->sge, i)
		snprintf(adap->msix_info[msi_idx++].desc, n, "%s-ofld%d",
			 adap->port[0]->name, i);

	for_each_rdmarxq(&adap->sge, i)
		snprintf(adap->msix_info[msi_idx++].desc, n, "%s-rdma%d",
			 adap->port[0]->name, i);

	for_each_rdmaciq(&adap->sge, i)
		snprintf(adap->msix_info[msi_idx++].desc, n, "%s-rdma-ciq%d",
			 adap->port[0]->name, i);
}

static int request_msix_queue_irqs(struct adapter *adap)
{
	struct sge *s = &adap->sge;
	int err, ethqidx, ofldqidx = 0, rdmaqidx = 0, rdmaciqqidx = 0;
	int msi_index = 2;

	err = request_irq(adap->msix_info[1].vec, t4_sge_intr_msix, 0,
			  adap->msix_info[1].desc, &s->fw_evtq);
	if (err)
		return err;

	for_each_ethrxq(s, ethqidx) {
		err = request_irq(adap->msix_info[msi_index].vec,
				  t4_sge_intr_msix, 0,
				  adap->msix_info[msi_index].desc,
				  &s->ethrxq[ethqidx].rspq);
		if (err)
			goto unwind;
		msi_index++;
	}
	for_each_ofldrxq(s, ofldqidx) {
		err = request_irq(adap->msix_info[msi_index].vec,
				  t4_sge_intr_msix, 0,
				  adap->msix_info[msi_index].desc,
				  &s->ofldrxq[ofldqidx].rspq);
		if (err)
			goto unwind;
		msi_index++;
	}
	for_each_rdmarxq(s, rdmaqidx) {
		err = request_irq(adap->msix_info[msi_index].vec,
				  t4_sge_intr_msix, 0,
				  adap->msix_info[msi_index].desc,
				  &s->rdmarxq[rdmaqidx].rspq);
		if (err)
			goto unwind;
		msi_index++;
	}
	for_each_rdmaciq(s, rdmaciqqidx) {
		err = request_irq(adap->msix_info[msi_index].vec,
				  t4_sge_intr_msix, 0,
				  adap->msix_info[msi_index].desc,
				  &s->rdmaciq[rdmaciqqidx].rspq);
		if (err)
			goto unwind;
		msi_index++;
	}
	return 0;

unwind:
	while (--rdmaciqqidx >= 0)
		free_irq(adap->msix_info[--msi_index].vec,
			 &s->rdmaciq[rdmaciqqidx].rspq);
	while (--rdmaqidx >= 0)
		free_irq(adap->msix_info[--msi_index].vec,
			 &s->rdmarxq[rdmaqidx].rspq);
	while (--ofldqidx >= 0)
		free_irq(adap->msix_info[--msi_index].vec,
			 &s->ofldrxq[ofldqidx].rspq);
	while (--ethqidx >= 0)
		free_irq(adap->msix_info[--msi_index].vec,
			 &s->ethrxq[ethqidx].rspq);
	free_irq(adap->msix_info[1].vec, &s->fw_evtq);
	return err;
}

static void free_msix_queue_irqs(struct adapter *adap)
{
	int i, msi_index = 2;
	struct sge *s = &adap->sge;

	free_irq(adap->msix_info[1].vec, &s->fw_evtq);
	for_each_ethrxq(s, i)
		free_irq(adap->msix_info[msi_index++].vec, &s->ethrxq[i].rspq);
	for_each_ofldrxq(s, i)
		free_irq(adap->msix_info[msi_index++].vec, &s->ofldrxq[i].rspq);
	for_each_rdmarxq(s, i)
		free_irq(adap->msix_info[msi_index++].vec, &s->rdmarxq[i].rspq);
	for_each_rdmaciq(s, i)
		free_irq(adap->msix_info[msi_index++].vec, &s->rdmaciq[i].rspq);
}

/**
 *	write_rss - write the RSS table for a given port
 *	@pi: the port
 *	@queues: array of queue indices for RSS
 *
 *	Sets up the portion of the HW RSS table for the port's VI to distribute
 *	packets to the Rx queues in @queues.
 */
static int write_rss(const struct port_info *pi, const u16 *queues)
{
	u16 *rss;
	int i, err;
	const struct sge_eth_rxq *q = &pi->adapter->sge.ethrxq[pi->first_qset];

	rss = kmalloc(pi->rss_size * sizeof(u16), GFP_KERNEL);
	if (!rss)
		return -ENOMEM;

	/* map the queue indices to queue ids */
	for (i = 0; i < pi->rss_size; i++, queues++)
		rss[i] = q[*queues].rspq.abs_id;

	err = t4_config_rss_range(pi->adapter, pi->adapter->fn, pi->viid, 0,
				  pi->rss_size, rss, pi->rss_size);
	kfree(rss);
	return err;
}

/**
 *	setup_rss - configure RSS
 *	@adap: the adapter
 *
 *	Sets up RSS for each port.
 */
static int setup_rss(struct adapter *adap)
{
	int i, err;

	for_each_port(adap, i) {
		const struct port_info *pi = adap2pinfo(adap, i);

		err = write_rss(pi, pi->rss);
		if (err)
			return err;
	}
	return 0;
}

/*
 * Return the channel of the ingress queue with the given qid.
 */
static unsigned int rxq_to_chan(const struct sge *p, unsigned int qid)
{
	qid -= p->ingr_start;
	return netdev2pinfo(p->ingr_map[qid]->netdev)->tx_chan;
}

/*
 * Wait until all NAPI handlers are descheduled.
 */
static void quiesce_rx(struct adapter *adap)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(adap->sge.ingr_map); i++) {
		struct sge_rspq *q = adap->sge.ingr_map[i];

		if (q && q->handler)
			napi_disable(&q->napi);
	}
}

/*
 * Enable NAPI scheduling and interrupt generation for all Rx queues.
 */
static void enable_rx(struct adapter *adap)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(adap->sge.ingr_map); i++) {
		struct sge_rspq *q = adap->sge.ingr_map[i];

		if (!q)
			continue;
		if (q->handler)
			napi_enable(&q->napi);
		/* 0-increment GTS to start the timer and enable interrupts */
		t4_write_reg(adap, MYPF_REG(SGE_PF_GTS),
			     SEINTARM(q->intr_params) |
			     INGRESSQID(q->cntxt_id));
	}
}

/**
 *	setup_sge_queues - configure SGE Tx/Rx/response queues
 *	@adap: the adapter
 *
 *	Determines how many sets of SGE queues to use and initializes them.
 *	We support multiple queue sets per port if we have MSI-X, otherwise
 *	just one queue set per port.
 */
static int setup_sge_queues(struct adapter *adap)
{
	int err, msi_idx, i, j;
	struct sge *s = &adap->sge;

	bitmap_zero(s->starving_fl, MAX_EGRQ);
	bitmap_zero(s->txq_maperr, MAX_EGRQ);

	if (adap->flags & USING_MSIX)
		msi_idx = 1;         /* vector 0 is for non-queue interrupts */
	else {
		err = t4_sge_alloc_rxq(adap, &s->intrq, false, adap->port[0], 0,
				       NULL, NULL);
		if (err)
			return err;
		msi_idx = -((int)s->intrq.abs_id + 1);
	}

	err = t4_sge_alloc_rxq(adap, &s->fw_evtq, true, adap->port[0],
			       msi_idx, NULL, fwevtq_handler);
	if (err) {
freeout:	t4_free_sge_resources(adap);
		return err;
	}

	for_each_port(adap, i) {
		struct net_device *dev = adap->port[i];
		struct port_info *pi = netdev_priv(dev);
		struct sge_eth_rxq *q = &s->ethrxq[pi->first_qset];
		struct sge_eth_txq *t = &s->ethtxq[pi->first_qset];

		for (j = 0; j < pi->nqsets; j++, q++) {
			if (msi_idx > 0)
				msi_idx++;
			err = t4_sge_alloc_rxq(adap, &q->rspq, false, dev,
					       msi_idx, &q->fl,
					       t4_ethrx_handler);
			if (err)
				goto freeout;
			q->rspq.idx = j;
			memset(&q->stats, 0, sizeof(q->stats));
		}
		for (j = 0; j < pi->nqsets; j++, t++) {
			err = t4_sge_alloc_eth_txq(adap, t, dev,
					netdev_get_tx_queue(dev, j),
					s->fw_evtq.cntxt_id);
			if (err)
				goto freeout;
		}
	}

	j = s->ofldqsets / adap->params.nports; /* ofld queues per channel */
	for_each_ofldrxq(s, i) {
		struct sge_ofld_rxq *q = &s->ofldrxq[i];
		struct net_device *dev = adap->port[i / j];

		if (msi_idx > 0)
			msi_idx++;
		err = t4_sge_alloc_rxq(adap, &q->rspq, false, dev, msi_idx,
				       q->fl.size ? &q->fl : NULL,
				       uldrx_handler);
		if (err)
			goto freeout;
		memset(&q->stats, 0, sizeof(q->stats));
		s->ofld_rxq[i] = q->rspq.abs_id;
		err = t4_sge_alloc_ofld_txq(adap, &s->ofldtxq[i], dev,
					    s->fw_evtq.cntxt_id);
		if (err)
			goto freeout;
	}

	for_each_rdmarxq(s, i) {
		struct sge_ofld_rxq *q = &s->rdmarxq[i];

		if (msi_idx > 0)
			msi_idx++;
		err = t4_sge_alloc_rxq(adap, &q->rspq, false, adap->port[i],
				       msi_idx, q->fl.size ? &q->fl : NULL,
				       uldrx_handler);
		if (err)
			goto freeout;
		memset(&q->stats, 0, sizeof(q->stats));
		s->rdma_rxq[i] = q->rspq.abs_id;
	}

	for_each_rdmaciq(s, i) {
		struct sge_ofld_rxq *q = &s->rdmaciq[i];

		if (msi_idx > 0)
			msi_idx++;
		err = t4_sge_alloc_rxq(adap, &q->rspq, false, adap->port[i],
				       msi_idx, q->fl.size ? &q->fl : NULL,
				       uldrx_handler);
		if (err)
			goto freeout;
		memset(&q->stats, 0, sizeof(q->stats));
		s->rdma_ciq[i] = q->rspq.abs_id;
	}

	for_each_port(adap, i) {
		/*
		 * Note that ->rdmarxq[i].rspq.cntxt_id below is 0 if we don't
		 * have RDMA queues, and that's the right value.
		 */
		err = t4_sge_alloc_ctrl_txq(adap, &s->ctrlq[i], adap->port[i],
					    s->fw_evtq.cntxt_id,
					    s->rdmarxq[i].rspq.cntxt_id);
		if (err)
			goto freeout;
	}

	t4_write_reg(adap, is_t4(adap->params.chip) ?
				MPS_TRC_RSS_CONTROL :
				MPS_T5_TRC_RSS_CONTROL,
		     RSSCONTROL(netdev2pinfo(adap->port[0])->tx_chan) |
		     QUEUENUMBER(s->ethrxq[0].rspq.abs_id));
	return 0;
}

/*
 * Allocate a chunk of memory using kmalloc or, if that fails, vmalloc.
 * The allocated memory is cleared.
 */
void *t4_alloc_mem(size_t size)
{
	void *p = kzalloc(size, GFP_KERNEL | __GFP_NOWARN);

	if (!p)
		p = vzalloc(size);
	return p;
}

/*
 * Free memory allocated through alloc_mem().
 */
static void t4_free_mem(void *addr)
{
	if (is_vmalloc_addr(addr))
		vfree(addr);
	else
		kfree(addr);
}

/* Send a Work Request to write the filter at a specified index.  We construct
 * a Firmware Filter Work Request to have the work done and put the indicated
 * filter into "pending" mode which will prevent any further actions against
 * it till we get a reply from the firmware on the completion status of the
 * request.
 */
static int set_filter_wr(struct adapter *adapter, int fidx)
{
	struct filter_entry *f = &adapter->tids.ftid_tab[fidx];
	struct sk_buff *skb;
	struct fw_filter_wr *fwr;
	unsigned int ftid;

	/* If the new filter requires loopback Destination MAC and/or VLAN
	 * rewriting then we need to allocate a Layer 2 Table (L2T) entry for
	 * the filter.
	 */
	if (f->fs.newdmac || f->fs.newvlan) {
		/* allocate L2T entry for new filter */
		f->l2t = t4_l2t_alloc_switching(adapter->l2t);
		if (f->l2t == NULL)
			return -EAGAIN;
		if (t4_l2t_set_switching(adapter, f->l2t, f->fs.vlan,
					f->fs.eport, f->fs.dmac)) {
			cxgb4_l2t_release(f->l2t);
			f->l2t = NULL;
			return -ENOMEM;
		}
	}

	ftid = adapter->tids.ftid_base + fidx;

	skb = alloc_skb(sizeof(*fwr), GFP_KERNEL | __GFP_NOFAIL);
	fwr = (struct fw_filter_wr *)__skb_put(skb, sizeof(*fwr));
	memset(fwr, 0, sizeof(*fwr));

	/* It would be nice to put most of the following in t4_hw.c but most
	 * of the work is translating the cxgbtool ch_filter_specification
	 * into the Work Request and the definition of that structure is
	 * currently in cxgbtool.h which isn't appropriate to pull into the
	 * common code.  We may eventually try to come up with a more neutral
	 * filter specification structure but for now it's easiest to simply
	 * put this fairly direct code in line ...
	 */
	fwr->op_pkd = htonl(FW_WR_OP(FW_FILTER_WR));
	fwr->len16_pkd = htonl(FW_WR_LEN16(sizeof(*fwr)/16));
	fwr->tid_to_iq =
		htonl(V_FW_FILTER_WR_TID(ftid) |
		      V_FW_FILTER_WR_RQTYPE(f->fs.type) |
		      V_FW_FILTER_WR_NOREPLY(0) |
		      V_FW_FILTER_WR_IQ(f->fs.iq));
	fwr->del_filter_to_l2tix =
		htonl(V_FW_FILTER_WR_RPTTID(f->fs.rpttid) |
		      V_FW_FILTER_WR_DROP(f->fs.action == FILTER_DROP) |
		      V_FW_FILTER_WR_DIRSTEER(f->fs.dirsteer) |
		      V_FW_FILTER_WR_MASKHASH(f->fs.maskhash) |
		      V_FW_FILTER_WR_DIRSTEERHASH(f->fs.dirsteerhash) |
		      V_FW_FILTER_WR_LPBK(f->fs.action == FILTER_SWITCH) |
		      V_FW_FILTER_WR_DMAC(f->fs.newdmac) |
		      V_FW_FILTER_WR_SMAC(f->fs.newsmac) |
		      V_FW_FILTER_WR_INSVLAN(f->fs.newvlan == VLAN_INSERT ||
					     f->fs.newvlan == VLAN_REWRITE) |
		      V_FW_FILTER_WR_RMVLAN(f->fs.newvlan == VLAN_REMOVE ||
					    f->fs.newvlan == VLAN_REWRITE) |
		      V_FW_FILTER_WR_HITCNTS(f->fs.hitcnts) |
		      V_FW_FILTER_WR_TXCHAN(f->fs.eport) |
		      V_FW_FILTER_WR_PRIO(f->fs.prio) |
		      V_FW_FILTER_WR_L2TIX(f->l2t ? f->l2t->idx : 0));
	fwr->ethtype = htons(f->fs.val.ethtype);
	fwr->ethtypem = htons(f->fs.mask.ethtype);
	fwr->frag_to_ovlan_vldm =
		(V_FW_FILTER_WR_FRAG(f->fs.val.frag) |
		 V_FW_FILTER_WR_FRAGM(f->fs.mask.frag) |
		 V_FW_FILTER_WR_IVLAN_VLD(f->fs.val.ivlan_vld) |
		 V_FW_FILTER_WR_OVLAN_VLD(f->fs.val.ovlan_vld) |
		 V_FW_FILTER_WR_IVLAN_VLDM(f->fs.mask.ivlan_vld) |
		 V_FW_FILTER_WR_OVLAN_VLDM(f->fs.mask.ovlan_vld));
	fwr->smac_sel = 0;
	fwr->rx_chan_rx_rpl_iq =
		htons(V_FW_FILTER_WR_RX_CHAN(0) |
		      V_FW_FILTER_WR_RX_RPL_IQ(adapter->sge.fw_evtq.abs_id));
	fwr->maci_to_matchtypem =
		htonl(V_FW_FILTER_WR_MACI(f->fs.val.macidx) |
		      V_FW_FILTER_WR_MACIM(f->fs.mask.macidx) |
		      V_FW_FILTER_WR_FCOE(f->fs.val.fcoe) |
		      V_FW_FILTER_WR_FCOEM(f->fs.mask.fcoe) |
		      V_FW_FILTER_WR_PORT(f->fs.val.iport) |
		      V_FW_FILTER_WR_PORTM(f->fs.mask.iport) |
		      V_FW_FILTER_WR_MATCHTYPE(f->fs.val.matchtype) |
		      V_FW_FILTER_WR_MATCHTYPEM(f->fs.mask.matchtype));
	fwr->ptcl = f->fs.val.proto;
	fwr->ptclm = f->fs.mask.proto;
	fwr->ttyp = f->fs.val.tos;
	fwr->ttypm = f->fs.mask.tos;
	fwr->ivlan = htons(f->fs.val.ivlan);
	fwr->ivlanm = htons(f->fs.mask.ivlan);
	fwr->ovlan = htons(f->fs.val.ovlan);
	fwr->ovlanm = htons(f->fs.mask.ovlan);
	memcpy(fwr->lip, f->fs.val.lip, sizeof(fwr->lip));
	memcpy(fwr->lipm, f->fs.mask.lip, sizeof(fwr->lipm));
	memcpy(fwr->fip, f->fs.val.fip, sizeof(fwr->fip));
	memcpy(fwr->fipm, f->fs.mask.fip, sizeof(fwr->fipm));
	fwr->lp = htons(f->fs.val.lport);
	fwr->lpm = htons(f->fs.mask.lport);
	fwr->fp = htons(f->fs.val.fport);
	fwr->fpm = htons(f->fs.mask.fport);
	if (f->fs.newsmac)
		memcpy(fwr->sma, f->fs.smac, sizeof(fwr->sma));

	/* Mark the filter as "pending" and ship off the Filter Work Request.
	 * When we get the Work Request Reply we'll clear the pending status.
	 */
	f->pending = 1;
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, f->fs.val.iport & 0x3);
	t4_ofld_send(adapter, skb);
	return 0;
}

/* Delete the filter at a specified index.
 */
static int del_filter_wr(struct adapter *adapter, int fidx)
{
	struct filter_entry *f = &adapter->tids.ftid_tab[fidx];
	struct sk_buff *skb;
	struct fw_filter_wr *fwr;
	unsigned int len, ftid;

	len = sizeof(*fwr);
	ftid = adapter->tids.ftid_base + fidx;

	skb = alloc_skb(len, GFP_KERNEL | __GFP_NOFAIL);
	fwr = (struct fw_filter_wr *)__skb_put(skb, len);
	t4_mk_filtdelwr(ftid, fwr, adapter->sge.fw_evtq.abs_id);

	/* Mark the filter as "pending" and ship off the Filter Work Request.
	 * When we get the Work Request Reply we'll clear the pending status.
	 */
	f->pending = 1;
	t4_mgmt_tx(adapter, skb);
	return 0;
}

static u16 cxgb_select_queue(struct net_device *dev, struct sk_buff *skb,
			     void *accel_priv, select_queue_fallback_t fallback)
{
	int txq;

#ifdef CONFIG_CHELSIO_T4_DCB
	/* If a Data Center Bridging has been successfully negotiated on this
	 * link then we'll use the skb's priority to map it to a TX Queue.
	 * The skb's priority is determined via the VLAN Tag Priority Code
	 * Point field.
	 */
	if (cxgb4_dcb_enabled(dev)) {
		u16 vlan_tci;
		int err;

		err = vlan_get_tag(skb, &vlan_tci);
		if (unlikely(err)) {
			if (net_ratelimit())
				netdev_warn(dev,
					    "TX Packet without VLAN Tag on DCB Link\n");
			txq = 0;
		} else {
			txq = (vlan_tci & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
		}
		return txq;
	}
#endif /* CONFIG_CHELSIO_T4_DCB */

	if (select_queue) {
		txq = (skb_rx_queue_recorded(skb)
			? skb_get_rx_queue(skb)
			: smp_processor_id());

		while (unlikely(txq >= dev->real_num_tx_queues))
			txq -= dev->real_num_tx_queues;

		return txq;
	}

	return fallback(dev, skb) % dev->real_num_tx_queues;
}

static inline int is_offload(const struct adapter *adap)
{
	return adap->params.offload;
}

/*
 * Implementation of ethtool operations.
 */

static u32 get_msglevel(struct net_device *dev)
{
	return netdev2adap(dev)->msg_enable;
}

static void set_msglevel(struct net_device *dev, u32 val)
{
	netdev2adap(dev)->msg_enable = val;
}

static char stats_strings[][ETH_GSTRING_LEN] = {
	"TxOctetsOK         ",
	"TxFramesOK         ",
	"TxBroadcastFrames  ",
	"TxMulticastFrames  ",
	"TxUnicastFrames    ",
	"TxErrorFrames      ",

	"TxFrames64         ",
	"TxFrames65To127    ",
	"TxFrames128To255   ",
	"TxFrames256To511   ",
	"TxFrames512To1023  ",
	"TxFrames1024To1518 ",
	"TxFrames1519ToMax  ",

	"TxFramesDropped    ",
	"TxPauseFrames      ",
	"TxPPP0Frames       ",
	"TxPPP1Frames       ",
	"TxPPP2Frames       ",
	"TxPPP3Frames       ",
	"TxPPP4Frames       ",
	"TxPPP5Frames       ",
	"TxPPP6Frames       ",
	"TxPPP7Frames       ",

	"RxOctetsOK         ",
	"RxFramesOK         ",
	"RxBroadcastFrames  ",
	"RxMulticastFrames  ",
	"RxUnicastFrames    ",

	"RxFramesTooLong    ",
	"RxJabberErrors     ",
	"RxFCSErrors        ",
	"RxLengthErrors     ",
	"RxSymbolErrors     ",
	"RxRuntFrames       ",

	"RxFrames64         ",
	"RxFrames65To127    ",
	"RxFrames128To255   ",
	"RxFrames256To511   ",
	"RxFrames512To1023  ",
	"RxFrames1024To1518 ",
	"RxFrames1519ToMax  ",

	"RxPauseFrames      ",
	"RxPPP0Frames       ",
	"RxPPP1Frames       ",
	"RxPPP2Frames       ",
	"RxPPP3Frames       ",
	"RxPPP4Frames       ",
	"RxPPP5Frames       ",
	"RxPPP6Frames       ",
	"RxPPP7Frames       ",

	"RxBG0FramesDropped ",
	"RxBG1FramesDropped ",
	"RxBG2FramesDropped ",
	"RxBG3FramesDropped ",
	"RxBG0FramesTrunc   ",
	"RxBG1FramesTrunc   ",
	"RxBG2FramesTrunc   ",
	"RxBG3FramesTrunc   ",

	"TSO                ",
	"TxCsumOffload      ",
	"RxCsumGood         ",
	"VLANextractions    ",
	"VLANinsertions     ",
	"GROpackets         ",
	"GROmerged          ",
	"WriteCoalSuccess   ",
	"WriteCoalFail      ",
};

static int get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(stats_strings);
	default:
		return -EOPNOTSUPP;
	}
}

#define T4_REGMAP_SIZE (160 * 1024)
#define T5_REGMAP_SIZE (332 * 1024)

static int get_regs_len(struct net_device *dev)
{
	struct adapter *adap = netdev2adap(dev);
	if (is_t4(adap->params.chip))
		return T4_REGMAP_SIZE;
	else
		return T5_REGMAP_SIZE;
}

static int get_eeprom_len(struct net_device *dev)
{
	return EEPROMSIZE;
}

static void get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct adapter *adapter = netdev2adap(dev);

	strlcpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(adapter->pdev),
		sizeof(info->bus_info));

	if (adapter->params.fw_vers)
		snprintf(info->fw_version, sizeof(info->fw_version),
			"%u.%u.%u.%u, TP %u.%u.%u.%u",
			FW_HDR_FW_VER_MAJOR_GET(adapter->params.fw_vers),
			FW_HDR_FW_VER_MINOR_GET(adapter->params.fw_vers),
			FW_HDR_FW_VER_MICRO_GET(adapter->params.fw_vers),
			FW_HDR_FW_VER_BUILD_GET(adapter->params.fw_vers),
			FW_HDR_FW_VER_MAJOR_GET(adapter->params.tp_vers),
			FW_HDR_FW_VER_MINOR_GET(adapter->params.tp_vers),
			FW_HDR_FW_VER_MICRO_GET(adapter->params.tp_vers),
			FW_HDR_FW_VER_BUILD_GET(adapter->params.tp_vers));
}

static void get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	if (stringset == ETH_SS_STATS)
		memcpy(data, stats_strings, sizeof(stats_strings));
}

/*
 * port stats maintained per queue of the port.  They should be in the same
 * order as in stats_strings above.
 */
struct queue_port_stats {
	u64 tso;
	u64 tx_csum;
	u64 rx_csum;
	u64 vlan_ex;
	u64 vlan_ins;
	u64 gro_pkts;
	u64 gro_merged;
};

static void collect_sge_port_stats(const struct adapter *adap,
		const struct port_info *p, struct queue_port_stats *s)
{
	int i;
	const struct sge_eth_txq *tx = &adap->sge.ethtxq[p->first_qset];
	const struct sge_eth_rxq *rx = &adap->sge.ethrxq[p->first_qset];

	memset(s, 0, sizeof(*s));
	for (i = 0; i < p->nqsets; i++, rx++, tx++) {
		s->tso += tx->tso;
		s->tx_csum += tx->tx_cso;
		s->rx_csum += rx->stats.rx_cso;
		s->vlan_ex += rx->stats.vlan_ex;
		s->vlan_ins += tx->vlan_ins;
		s->gro_pkts += rx->stats.lro_pkts;
		s->gro_merged += rx->stats.lro_merged;
	}
}

static void get_stats(struct net_device *dev, struct ethtool_stats *stats,
		      u64 *data)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	u32 val1, val2;

	t4_get_port_stats(adapter, pi->tx_chan, (struct port_stats *)data);

	data += sizeof(struct port_stats) / sizeof(u64);
	collect_sge_port_stats(adapter, pi, (struct queue_port_stats *)data);
	data += sizeof(struct queue_port_stats) / sizeof(u64);
	if (!is_t4(adapter->params.chip)) {
		t4_write_reg(adapter, SGE_STAT_CFG, STATSOURCE_T5(7));
		val1 = t4_read_reg(adapter, SGE_STAT_TOTAL);
		val2 = t4_read_reg(adapter, SGE_STAT_MATCH);
		*data = val1 - val2;
		data++;
		*data = val2;
		data++;
	} else {
		memset(data, 0, 2 * sizeof(u64));
		*data += 2;
	}
}

/*
 * Return a version number to identify the type of adapter.  The scheme is:
 * - bits 0..9: chip version
 * - bits 10..15: chip revision
 * - bits 16..23: register dump version
 */
static inline unsigned int mk_adap_vers(const struct adapter *ap)
{
	return CHELSIO_CHIP_VERSION(ap->params.chip) |
		(CHELSIO_CHIP_RELEASE(ap->params.chip) << 10) | (1 << 16);
}

static void reg_block_dump(struct adapter *ap, void *buf, unsigned int start,
			   unsigned int end)
{
	u32 *p = buf + start;

	for ( ; start <= end; start += sizeof(u32))
		*p++ = t4_read_reg(ap, start);
}

static void get_regs(struct net_device *dev, struct ethtool_regs *regs,
		     void *buf)
{
	static const unsigned int t4_reg_ranges[] = {
		0x1008, 0x1108,
		0x1180, 0x11b4,
		0x11fc, 0x123c,
		0x1300, 0x173c,
		0x1800, 0x18fc,
		0x3000, 0x30d8,
		0x30e0, 0x5924,
		0x5960, 0x59d4,
		0x5a00, 0x5af8,
		0x6000, 0x6098,
		0x6100, 0x6150,
		0x6200, 0x6208,
		0x6240, 0x6248,
		0x6280, 0x6338,
		0x6370, 0x638c,
		0x6400, 0x643c,
		0x6500, 0x6524,
		0x6a00, 0x6a38,
		0x6a60, 0x6a78,
		0x6b00, 0x6b84,
		0x6bf0, 0x6c84,
		0x6cf0, 0x6d84,
		0x6df0, 0x6e84,
		0x6ef0, 0x6f84,
		0x6ff0, 0x7084,
		0x70f0, 0x7184,
		0x71f0, 0x7284,
		0x72f0, 0x7384,
		0x73f0, 0x7450,
		0x7500, 0x7530,
		0x7600, 0x761c,
		0x7680, 0x76cc,
		0x7700, 0x7798,
		0x77c0, 0x77fc,
		0x7900, 0x79fc,
		0x7b00, 0x7c38,
		0x7d00, 0x7efc,
		0x8dc0, 0x8e1c,
		0x8e30, 0x8e78,
		0x8ea0, 0x8f6c,
		0x8fc0, 0x9074,
		0x90fc, 0x90fc,
		0x9400, 0x9458,
		0x9600, 0x96bc,
		0x9800, 0x9808,
		0x9820, 0x983c,
		0x9850, 0x9864,
		0x9c00, 0x9c6c,
		0x9c80, 0x9cec,
		0x9d00, 0x9d6c,
		0x9d80, 0x9dec,
		0x9e00, 0x9e6c,
		0x9e80, 0x9eec,
		0x9f00, 0x9f6c,
		0x9f80, 0x9fec,
		0xd004, 0xd03c,
		0xdfc0, 0xdfe0,
		0xe000, 0xea7c,
		0xf000, 0x11110,
		0x11118, 0x11190,
		0x19040, 0x1906c,
		0x19078, 0x19080,
		0x1908c, 0x19124,
		0x19150, 0x191b0,
		0x191d0, 0x191e8,
		0x19238, 0x1924c,
		0x193f8, 0x19474,
		0x19490, 0x194f8,
		0x19800, 0x19f30,
		0x1a000, 0x1a06c,
		0x1a0b0, 0x1a120,
		0x1a128, 0x1a138,
		0x1a190, 0x1a1c4,
		0x1a1fc, 0x1a1fc,
		0x1e040, 0x1e04c,
		0x1e284, 0x1e28c,
		0x1e2c0, 0x1e2c0,
		0x1e2e0, 0x1e2e0,
		0x1e300, 0x1e384,
		0x1e3c0, 0x1e3c8,
		0x1e440, 0x1e44c,
		0x1e684, 0x1e68c,
		0x1e6c0, 0x1e6c0,
		0x1e6e0, 0x1e6e0,
		0x1e700, 0x1e784,
		0x1e7c0, 0x1e7c8,
		0x1e840, 0x1e84c,
		0x1ea84, 0x1ea8c,
		0x1eac0, 0x1eac0,
		0x1eae0, 0x1eae0,
		0x1eb00, 0x1eb84,
		0x1ebc0, 0x1ebc8,
		0x1ec40, 0x1ec4c,
		0x1ee84, 0x1ee8c,
		0x1eec0, 0x1eec0,
		0x1eee0, 0x1eee0,
		0x1ef00, 0x1ef84,
		0x1efc0, 0x1efc8,
		0x1f040, 0x1f04c,
		0x1f284, 0x1f28c,
		0x1f2c0, 0x1f2c0,
		0x1f2e0, 0x1f2e0,
		0x1f300, 0x1f384,
		0x1f3c0, 0x1f3c8,
		0x1f440, 0x1f44c,
		0x1f684, 0x1f68c,
		0x1f6c0, 0x1f6c0,
		0x1f6e0, 0x1f6e0,
		0x1f700, 0x1f784,
		0x1f7c0, 0x1f7c8,
		0x1f840, 0x1f84c,
		0x1fa84, 0x1fa8c,
		0x1fac0, 0x1fac0,
		0x1fae0, 0x1fae0,
		0x1fb00, 0x1fb84,
		0x1fbc0, 0x1fbc8,
		0x1fc40, 0x1fc4c,
		0x1fe84, 0x1fe8c,
		0x1fec0, 0x1fec0,
		0x1fee0, 0x1fee0,
		0x1ff00, 0x1ff84,
		0x1ffc0, 0x1ffc8,
		0x20000, 0x2002c,
		0x20100, 0x2013c,
		0x20190, 0x201c8,
		0x20200, 0x20318,
		0x20400, 0x20528,
		0x20540, 0x20614,
		0x21000, 0x21040,
		0x2104c, 0x21060,
		0x210c0, 0x210ec,
		0x21200, 0x21268,
		0x21270, 0x21284,
		0x212fc, 0x21388,
		0x21400, 0x21404,
		0x21500, 0x21518,
		0x2152c, 0x2153c,
		0x21550, 0x21554,
		0x21600, 0x21600,
		0x21608, 0x21628,
		0x21630, 0x2163c,
		0x21700, 0x2171c,
		0x21780, 0x2178c,
		0x21800, 0x21c38,
		0x21c80, 0x21d7c,
		0x21e00, 0x21e04,
		0x22000, 0x2202c,
		0x22100, 0x2213c,
		0x22190, 0x221c8,
		0x22200, 0x22318,
		0x22400, 0x22528,
		0x22540, 0x22614,
		0x23000, 0x23040,
		0x2304c, 0x23060,
		0x230c0, 0x230ec,
		0x23200, 0x23268,
		0x23270, 0x23284,
		0x232fc, 0x23388,
		0x23400, 0x23404,
		0x23500, 0x23518,
		0x2352c, 0x2353c,
		0x23550, 0x23554,
		0x23600, 0x23600,
		0x23608, 0x23628,
		0x23630, 0x2363c,
		0x23700, 0x2371c,
		0x23780, 0x2378c,
		0x23800, 0x23c38,
		0x23c80, 0x23d7c,
		0x23e00, 0x23e04,
		0x24000, 0x2402c,
		0x24100, 0x2413c,
		0x24190, 0x241c8,
		0x24200, 0x24318,
		0x24400, 0x24528,
		0x24540, 0x24614,
		0x25000, 0x25040,
		0x2504c, 0x25060,
		0x250c0, 0x250ec,
		0x25200, 0x25268,
		0x25270, 0x25284,
		0x252fc, 0x25388,
		0x25400, 0x25404,
		0x25500, 0x25518,
		0x2552c, 0x2553c,
		0x25550, 0x25554,
		0x25600, 0x25600,
		0x25608, 0x25628,
		0x25630, 0x2563c,
		0x25700, 0x2571c,
		0x25780, 0x2578c,
		0x25800, 0x25c38,
		0x25c80, 0x25d7c,
		0x25e00, 0x25e04,
		0x26000, 0x2602c,
		0x26100, 0x2613c,
		0x26190, 0x261c8,
		0x26200, 0x26318,
		0x26400, 0x26528,
		0x26540, 0x26614,
		0x27000, 0x27040,
		0x2704c, 0x27060,
		0x270c0, 0x270ec,
		0x27200, 0x27268,
		0x27270, 0x27284,
		0x272fc, 0x27388,
		0x27400, 0x27404,
		0x27500, 0x27518,
		0x2752c, 0x2753c,
		0x27550, 0x27554,
		0x27600, 0x27600,
		0x27608, 0x27628,
		0x27630, 0x2763c,
		0x27700, 0x2771c,
		0x27780, 0x2778c,
		0x27800, 0x27c38,
		0x27c80, 0x27d7c,
		0x27e00, 0x27e04
	};

	static const unsigned int t5_reg_ranges[] = {
		0x1008, 0x1148,
		0x1180, 0x11b4,
		0x11fc, 0x123c,
		0x1280, 0x173c,
		0x1800, 0x18fc,
		0x3000, 0x3028,
		0x3060, 0x30d8,
		0x30e0, 0x30fc,
		0x3140, 0x357c,
		0x35a8, 0x35cc,
		0x35ec, 0x35ec,
		0x3600, 0x5624,
		0x56cc, 0x575c,
		0x580c, 0x5814,
		0x5890, 0x58bc,
		0x5940, 0x59dc,
		0x59fc, 0x5a18,
		0x5a60, 0x5a9c,
		0x5b9c, 0x5bfc,
		0x6000, 0x6040,
		0x6058, 0x614c,
		0x7700, 0x7798,
		0x77c0, 0x78fc,
		0x7b00, 0x7c54,
		0x7d00, 0x7efc,
		0x8dc0, 0x8de0,
		0x8df8, 0x8e84,
		0x8ea0, 0x8f84,
		0x8fc0, 0x90f8,
		0x9400, 0x9470,
		0x9600, 0x96f4,
		0x9800, 0x9808,
		0x9820, 0x983c,
		0x9850, 0x9864,
		0x9c00, 0x9c6c,
		0x9c80, 0x9cec,
		0x9d00, 0x9d6c,
		0x9d80, 0x9dec,
		0x9e00, 0x9e6c,
		0x9e80, 0x9eec,
		0x9f00, 0x9f6c,
		0x9f80, 0xa020,
		0xd004, 0xd03c,
		0xdfc0, 0xdfe0,
		0xe000, 0x11088,
		0x1109c, 0x11110,
		0x11118, 0x1117c,
		0x11190, 0x11204,
		0x19040, 0x1906c,
		0x19078, 0x19080,
		0x1908c, 0x19124,
		0x19150, 0x191b0,
		0x191d0, 0x191e8,
		0x19238, 0x19290,
		0x193f8, 0x19474,
		0x19490, 0x194cc,
		0x194f0, 0x194f8,
		0x19c00, 0x19c60,
		0x19c94, 0x19e10,
		0x19e50, 0x19f34,
		0x19f40, 0x19f50,
		0x19f90, 0x19fe4,
		0x1a000, 0x1a06c,
		0x1a0b0, 0x1a120,
		0x1a128, 0x1a138,
		0x1a190, 0x1a1c4,
		0x1a1fc, 0x1a1fc,
		0x1e008, 0x1e00c,
		0x1e040, 0x1e04c,
		0x1e284, 0x1e290,
		0x1e2c0, 0x1e2c0,
		0x1e2e0, 0x1e2e0,
		0x1e300, 0x1e384,
		0x1e3c0, 0x1e3c8,
		0x1e408, 0x1e40c,
		0x1e440, 0x1e44c,
		0x1e684, 0x1e690,
		0x1e6c0, 0x1e6c0,
		0x1e6e0, 0x1e6e0,
		0x1e700, 0x1e784,
		0x1e7c0, 0x1e7c8,
		0x1e808, 0x1e80c,
		0x1e840, 0x1e84c,
		0x1ea84, 0x1ea90,
		0x1eac0, 0x1eac0,
		0x1eae0, 0x1eae0,
		0x1eb00, 0x1eb84,
		0x1ebc0, 0x1ebc8,
		0x1ec08, 0x1ec0c,
		0x1ec40, 0x1ec4c,
		0x1ee84, 0x1ee90,
		0x1eec0, 0x1eec0,
		0x1eee0, 0x1eee0,
		0x1ef00, 0x1ef84,
		0x1efc0, 0x1efc8,
		0x1f008, 0x1f00c,
		0x1f040, 0x1f04c,
		0x1f284, 0x1f290,
		0x1f2c0, 0x1f2c0,
		0x1f2e0, 0x1f2e0,
		0x1f300, 0x1f384,
		0x1f3c0, 0x1f3c8,
		0x1f408, 0x1f40c,
		0x1f440, 0x1f44c,
		0x1f684, 0x1f690,
		0x1f6c0, 0x1f6c0,
		0x1f6e0, 0x1f6e0,
		0x1f700, 0x1f784,
		0x1f7c0, 0x1f7c8,
		0x1f808, 0x1f80c,
		0x1f840, 0x1f84c,
		0x1fa84, 0x1fa90,
		0x1fac0, 0x1fac0,
		0x1fae0, 0x1fae0,
		0x1fb00, 0x1fb84,
		0x1fbc0, 0x1fbc8,
		0x1fc08, 0x1fc0c,
		0x1fc40, 0x1fc4c,
		0x1fe84, 0x1fe90,
		0x1fec0, 0x1fec0,
		0x1fee0, 0x1fee0,
		0x1ff00, 0x1ff84,
		0x1ffc0, 0x1ffc8,
		0x30000, 0x30030,
		0x30100, 0x30144,
		0x30190, 0x301d0,
		0x30200, 0x30318,
		0x30400, 0x3052c,
		0x30540, 0x3061c,
		0x30800, 0x30834,
		0x308c0, 0x30908,
		0x30910, 0x309ac,
		0x30a00, 0x30a04,
		0x30a0c, 0x30a2c,
		0x30a44, 0x30a50,
		0x30a74, 0x30c24,
		0x30d08, 0x30d14,
		0x30d1c, 0x30d20,
		0x30d3c, 0x30d50,
		0x31200, 0x3120c,
		0x31220, 0x31220,
		0x31240, 0x31240,
		0x31600, 0x31600,
		0x31608, 0x3160c,
		0x31a00, 0x31a1c,
		0x31e04, 0x31e20,
		0x31e38, 0x31e3c,
		0x31e80, 0x31e80,
		0x31e88, 0x31ea8,
		0x31eb0, 0x31eb4,
		0x31ec8, 0x31ed4,
		0x31fb8, 0x32004,
		0x32208, 0x3223c,
		0x32600, 0x32630,
		0x32a00, 0x32abc,
		0x32b00, 0x32b70,
		0x33000, 0x33048,
		0x33060, 0x3309c,
		0x330f0, 0x33148,
		0x33160, 0x3319c,
		0x331f0, 0x332e4,
		0x332f8, 0x333e4,
		0x333f8, 0x33448,
		0x33460, 0x3349c,
		0x334f0, 0x33548,
		0x33560, 0x3359c,
		0x335f0, 0x336e4,
		0x336f8, 0x337e4,
		0x337f8, 0x337fc,
		0x33814, 0x33814,
		0x3382c, 0x3382c,
		0x33880, 0x3388c,
		0x338e8, 0x338ec,
		0x33900, 0x33948,
		0x33960, 0x3399c,
		0x339f0, 0x33ae4,
		0x33af8, 0x33b10,
		0x33b28, 0x33b28,
		0x33b3c, 0x33b50,
		0x33bf0, 0x33c10,
		0x33c28, 0x33c28,
		0x33c3c, 0x33c50,
		0x33cf0, 0x33cfc,
		0x34000, 0x34030,
		0x34100, 0x34144,
		0x34190, 0x341d0,
		0x34200, 0x34318,
		0x34400, 0x3452c,
		0x34540, 0x3461c,
		0x34800, 0x34834,
		0x348c0, 0x34908,
		0x34910, 0x349ac,
		0x34a00, 0x34a04,
		0x34a0c, 0x34a2c,
		0x34a44, 0x34a50,
		0x34a74, 0x34c24,
		0x34d08, 0x34d14,
		0x34d1c, 0x34d20,
		0x34d3c, 0x34d50,
		0x35200, 0x3520c,
		0x35220, 0x35220,
		0x35240, 0x35240,
		0x35600, 0x35600,
		0x35608, 0x3560c,
		0x35a00, 0x35a1c,
		0x35e04, 0x35e20,
		0x35e38, 0x35e3c,
		0x35e80, 0x35e80,
		0x35e88, 0x35ea8,
		0x35eb0, 0x35eb4,
		0x35ec8, 0x35ed4,
		0x35fb8, 0x36004,
		0x36208, 0x3623c,
		0x36600, 0x36630,
		0x36a00, 0x36abc,
		0x36b00, 0x36b70,
		0x37000, 0x37048,
		0x37060, 0x3709c,
		0x370f0, 0x37148,
		0x37160, 0x3719c,
		0x371f0, 0x372e4,
		0x372f8, 0x373e4,
		0x373f8, 0x37448,
		0x37460, 0x3749c,
		0x374f0, 0x37548,
		0x37560, 0x3759c,
		0x375f0, 0x376e4,
		0x376f8, 0x377e4,
		0x377f8, 0x377fc,
		0x37814, 0x37814,
		0x3782c, 0x3782c,
		0x37880, 0x3788c,
		0x378e8, 0x378ec,
		0x37900, 0x37948,
		0x37960, 0x3799c,
		0x379f0, 0x37ae4,
		0x37af8, 0x37b10,
		0x37b28, 0x37b28,
		0x37b3c, 0x37b50,
		0x37bf0, 0x37c10,
		0x37c28, 0x37c28,
		0x37c3c, 0x37c50,
		0x37cf0, 0x37cfc,
		0x38000, 0x38030,
		0x38100, 0x38144,
		0x38190, 0x381d0,
		0x38200, 0x38318,
		0x38400, 0x3852c,
		0x38540, 0x3861c,
		0x38800, 0x38834,
		0x388c0, 0x38908,
		0x38910, 0x389ac,
		0x38a00, 0x38a04,
		0x38a0c, 0x38a2c,
		0x38a44, 0x38a50,
		0x38a74, 0x38c24,
		0x38d08, 0x38d14,
		0x38d1c, 0x38d20,
		0x38d3c, 0x38d50,
		0x39200, 0x3920c,
		0x39220, 0x39220,
		0x39240, 0x39240,
		0x39600, 0x39600,
		0x39608, 0x3960c,
		0x39a00, 0x39a1c,
		0x39e04, 0x39e20,
		0x39e38, 0x39e3c,
		0x39e80, 0x39e80,
		0x39e88, 0x39ea8,
		0x39eb0, 0x39eb4,
		0x39ec8, 0x39ed4,
		0x39fb8, 0x3a004,
		0x3a208, 0x3a23c,
		0x3a600, 0x3a630,
		0x3aa00, 0x3aabc,
		0x3ab00, 0x3ab70,
		0x3b000, 0x3b048,
		0x3b060, 0x3b09c,
		0x3b0f0, 0x3b148,
		0x3b160, 0x3b19c,
		0x3b1f0, 0x3b2e4,
		0x3b2f8, 0x3b3e4,
		0x3b3f8, 0x3b448,
		0x3b460, 0x3b49c,
		0x3b4f0, 0x3b548,
		0x3b560, 0x3b59c,
		0x3b5f0, 0x3b6e4,
		0x3b6f8, 0x3b7e4,
		0x3b7f8, 0x3b7fc,
		0x3b814, 0x3b814,
		0x3b82c, 0x3b82c,
		0x3b880, 0x3b88c,
		0x3b8e8, 0x3b8ec,
		0x3b900, 0x3b948,
		0x3b960, 0x3b99c,
		0x3b9f0, 0x3bae4,
		0x3baf8, 0x3bb10,
		0x3bb28, 0x3bb28,
		0x3bb3c, 0x3bb50,
		0x3bbf0, 0x3bc10,
		0x3bc28, 0x3bc28,
		0x3bc3c, 0x3bc50,
		0x3bcf0, 0x3bcfc,
		0x3c000, 0x3c030,
		0x3c100, 0x3c144,
		0x3c190, 0x3c1d0,
		0x3c200, 0x3c318,
		0x3c400, 0x3c52c,
		0x3c540, 0x3c61c,
		0x3c800, 0x3c834,
		0x3c8c0, 0x3c908,
		0x3c910, 0x3c9ac,
		0x3ca00, 0x3ca04,
		0x3ca0c, 0x3ca2c,
		0x3ca44, 0x3ca50,
		0x3ca74, 0x3cc24,
		0x3cd08, 0x3cd14,
		0x3cd1c, 0x3cd20,
		0x3cd3c, 0x3cd50,
		0x3d200, 0x3d20c,
		0x3d220, 0x3d220,
		0x3d240, 0x3d240,
		0x3d600, 0x3d600,
		0x3d608, 0x3d60c,
		0x3da00, 0x3da1c,
		0x3de04, 0x3de20,
		0x3de38, 0x3de3c,
		0x3de80, 0x3de80,
		0x3de88, 0x3dea8,
		0x3deb0, 0x3deb4,
		0x3dec8, 0x3ded4,
		0x3dfb8, 0x3e004,
		0x3e208, 0x3e23c,
		0x3e600, 0x3e630,
		0x3ea00, 0x3eabc,
		0x3eb00, 0x3eb70,
		0x3f000, 0x3f048,
		0x3f060, 0x3f09c,
		0x3f0f0, 0x3f148,
		0x3f160, 0x3f19c,
		0x3f1f0, 0x3f2e4,
		0x3f2f8, 0x3f3e4,
		0x3f3f8, 0x3f448,
		0x3f460, 0x3f49c,
		0x3f4f0, 0x3f548,
		0x3f560, 0x3f59c,
		0x3f5f0, 0x3f6e4,
		0x3f6f8, 0x3f7e4,
		0x3f7f8, 0x3f7fc,
		0x3f814, 0x3f814,
		0x3f82c, 0x3f82c,
		0x3f880, 0x3f88c,
		0x3f8e8, 0x3f8ec,
		0x3f900, 0x3f948,
		0x3f960, 0x3f99c,
		0x3f9f0, 0x3fae4,
		0x3faf8, 0x3fb10,
		0x3fb28, 0x3fb28,
		0x3fb3c, 0x3fb50,
		0x3fbf0, 0x3fc10,
		0x3fc28, 0x3fc28,
		0x3fc3c, 0x3fc50,
		0x3fcf0, 0x3fcfc,
		0x40000, 0x4000c,
		0x40040, 0x40068,
		0x40080, 0x40144,
		0x40180, 0x4018c,
		0x40200, 0x40298,
		0x402ac, 0x4033c,
		0x403f8, 0x403fc,
		0x41304, 0x413c4,
		0x41400, 0x4141c,
		0x41480, 0x414d0,
		0x44000, 0x44078,
		0x440c0, 0x44278,
		0x442c0, 0x44478,
		0x444c0, 0x44678,
		0x446c0, 0x44878,
		0x448c0, 0x449fc,
		0x45000, 0x45068,
		0x45080, 0x45084,
		0x450a0, 0x450b0,
		0x45200, 0x45268,
		0x45280, 0x45284,
		0x452a0, 0x452b0,
		0x460c0, 0x460e4,
		0x47000, 0x4708c,
		0x47200, 0x47250,
		0x47400, 0x47420,
		0x47600, 0x47618,
		0x47800, 0x47814,
		0x48000, 0x4800c,
		0x48040, 0x48068,
		0x48080, 0x48144,
		0x48180, 0x4818c,
		0x48200, 0x48298,
		0x482ac, 0x4833c,
		0x483f8, 0x483fc,
		0x49304, 0x493c4,
		0x49400, 0x4941c,
		0x49480, 0x494d0,
		0x4c000, 0x4c078,
		0x4c0c0, 0x4c278,
		0x4c2c0, 0x4c478,
		0x4c4c0, 0x4c678,
		0x4c6c0, 0x4c878,
		0x4c8c0, 0x4c9fc,
		0x4d000, 0x4d068,
		0x4d080, 0x4d084,
		0x4d0a0, 0x4d0b0,
		0x4d200, 0x4d268,
		0x4d280, 0x4d284,
		0x4d2a0, 0x4d2b0,
		0x4e0c0, 0x4e0e4,
		0x4f000, 0x4f08c,
		0x4f200, 0x4f250,
		0x4f400, 0x4f420,
		0x4f600, 0x4f618,
		0x4f800, 0x4f814,
		0x50000, 0x500cc,
		0x50400, 0x50400,
		0x50800, 0x508cc,
		0x50c00, 0x50c00,
		0x51000, 0x5101c,
		0x51300, 0x51308,
	};

	int i;
	struct adapter *ap = netdev2adap(dev);
	static const unsigned int *reg_ranges;
	int arr_size = 0, buf_size = 0;

	if (is_t4(ap->params.chip)) {
		reg_ranges = &t4_reg_ranges[0];
		arr_size = ARRAY_SIZE(t4_reg_ranges);
		buf_size = T4_REGMAP_SIZE;
	} else {
		reg_ranges = &t5_reg_ranges[0];
		arr_size = ARRAY_SIZE(t5_reg_ranges);
		buf_size = T5_REGMAP_SIZE;
	}

	regs->version = mk_adap_vers(ap);

	memset(buf, 0, buf_size);
	for (i = 0; i < arr_size; i += 2)
		reg_block_dump(ap, buf, reg_ranges[i], reg_ranges[i + 1]);
}

static int restart_autoneg(struct net_device *dev)
{
	struct port_info *p = netdev_priv(dev);

	if (!netif_running(dev))
		return -EAGAIN;
	if (p->link_cfg.autoneg != AUTONEG_ENABLE)
		return -EINVAL;
	t4_restart_aneg(p->adapter, p->adapter->fn, p->tx_chan);
	return 0;
}

static int identify_port(struct net_device *dev,
			 enum ethtool_phys_id_state state)
{
	unsigned int val;
	struct adapter *adap = netdev2adap(dev);

	if (state == ETHTOOL_ID_ACTIVE)
		val = 0xffff;
	else if (state == ETHTOOL_ID_INACTIVE)
		val = 0;
	else
		return -EINVAL;

	return t4_identify_port(adap, adap->fn, netdev2pinfo(dev)->viid, val);
}

static unsigned int from_fw_linkcaps(unsigned int type, unsigned int caps)
{
	unsigned int v = 0;

	if (type == FW_PORT_TYPE_BT_SGMII || type == FW_PORT_TYPE_BT_XFI ||
	    type == FW_PORT_TYPE_BT_XAUI) {
		v |= SUPPORTED_TP;
		if (caps & FW_PORT_CAP_SPEED_100M)
			v |= SUPPORTED_100baseT_Full;
		if (caps & FW_PORT_CAP_SPEED_1G)
			v |= SUPPORTED_1000baseT_Full;
		if (caps & FW_PORT_CAP_SPEED_10G)
			v |= SUPPORTED_10000baseT_Full;
	} else if (type == FW_PORT_TYPE_KX4 || type == FW_PORT_TYPE_KX) {
		v |= SUPPORTED_Backplane;
		if (caps & FW_PORT_CAP_SPEED_1G)
			v |= SUPPORTED_1000baseKX_Full;
		if (caps & FW_PORT_CAP_SPEED_10G)
			v |= SUPPORTED_10000baseKX4_Full;
	} else if (type == FW_PORT_TYPE_KR)
		v |= SUPPORTED_Backplane | SUPPORTED_10000baseKR_Full;
	else if (type == FW_PORT_TYPE_BP_AP)
		v |= SUPPORTED_Backplane | SUPPORTED_10000baseR_FEC |
		     SUPPORTED_10000baseKR_Full | SUPPORTED_1000baseKX_Full;
	else if (type == FW_PORT_TYPE_BP4_AP)
		v |= SUPPORTED_Backplane | SUPPORTED_10000baseR_FEC |
		     SUPPORTED_10000baseKR_Full | SUPPORTED_1000baseKX_Full |
		     SUPPORTED_10000baseKX4_Full;
	else if (type == FW_PORT_TYPE_FIBER_XFI ||
		 type == FW_PORT_TYPE_FIBER_XAUI || type == FW_PORT_TYPE_SFP) {
		v |= SUPPORTED_FIBRE;
		if (caps & FW_PORT_CAP_SPEED_1G)
			v |= SUPPORTED_1000baseT_Full;
		if (caps & FW_PORT_CAP_SPEED_10G)
			v |= SUPPORTED_10000baseT_Full;
	} else if (type == FW_PORT_TYPE_BP40_BA)
		v |= SUPPORTED_40000baseSR4_Full;

	if (caps & FW_PORT_CAP_ANEG)
		v |= SUPPORTED_Autoneg;
	return v;
}

static unsigned int to_fw_linkcaps(unsigned int caps)
{
	unsigned int v = 0;

	if (caps & ADVERTISED_100baseT_Full)
		v |= FW_PORT_CAP_SPEED_100M;
	if (caps & ADVERTISED_1000baseT_Full)
		v |= FW_PORT_CAP_SPEED_1G;
	if (caps & ADVERTISED_10000baseT_Full)
		v |= FW_PORT_CAP_SPEED_10G;
	if (caps & ADVERTISED_40000baseSR4_Full)
		v |= FW_PORT_CAP_SPEED_40G;
	return v;
}

static int get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	const struct port_info *p = netdev_priv(dev);

	if (p->port_type == FW_PORT_TYPE_BT_SGMII ||
	    p->port_type == FW_PORT_TYPE_BT_XFI ||
	    p->port_type == FW_PORT_TYPE_BT_XAUI)
		cmd->port = PORT_TP;
	else if (p->port_type == FW_PORT_TYPE_FIBER_XFI ||
		 p->port_type == FW_PORT_TYPE_FIBER_XAUI)
		cmd->port = PORT_FIBRE;
	else if (p->port_type == FW_PORT_TYPE_SFP ||
		 p->port_type == FW_PORT_TYPE_QSFP_10G ||
		 p->port_type == FW_PORT_TYPE_QSFP) {
		if (p->mod_type == FW_PORT_MOD_TYPE_LR ||
		    p->mod_type == FW_PORT_MOD_TYPE_SR ||
		    p->mod_type == FW_PORT_MOD_TYPE_ER ||
		    p->mod_type == FW_PORT_MOD_TYPE_LRM)
			cmd->port = PORT_FIBRE;
		else if (p->mod_type == FW_PORT_MOD_TYPE_TWINAX_PASSIVE ||
			 p->mod_type == FW_PORT_MOD_TYPE_TWINAX_ACTIVE)
			cmd->port = PORT_DA;
		else
			cmd->port = PORT_OTHER;
	} else
		cmd->port = PORT_OTHER;

	if (p->mdio_addr >= 0) {
		cmd->phy_address = p->mdio_addr;
		cmd->transceiver = XCVR_EXTERNAL;
		cmd->mdio_support = p->port_type == FW_PORT_TYPE_BT_SGMII ?
			MDIO_SUPPORTS_C22 : MDIO_SUPPORTS_C45;
	} else {
		cmd->phy_address = 0;  /* not really, but no better option */
		cmd->transceiver = XCVR_INTERNAL;
		cmd->mdio_support = 0;
	}

	cmd->supported = from_fw_linkcaps(p->port_type, p->link_cfg.supported);
	cmd->advertising = from_fw_linkcaps(p->port_type,
					    p->link_cfg.advertising);
	ethtool_cmd_speed_set(cmd,
			      netif_carrier_ok(dev) ? p->link_cfg.speed : 0);
	cmd->duplex = DUPLEX_FULL;
	cmd->autoneg = p->link_cfg.autoneg;
	cmd->maxtxpkt = 0;
	cmd->maxrxpkt = 0;
	return 0;
}

static unsigned int speed_to_caps(int speed)
{
	if (speed == 100)
		return FW_PORT_CAP_SPEED_100M;
	if (speed == 1000)
		return FW_PORT_CAP_SPEED_1G;
	if (speed == 10000)
		return FW_PORT_CAP_SPEED_10G;
	if (speed == 40000)
		return FW_PORT_CAP_SPEED_40G;
	return 0;
}

static int set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	unsigned int cap;
	struct port_info *p = netdev_priv(dev);
	struct link_config *lc = &p->link_cfg;
	u32 speed = ethtool_cmd_speed(cmd);

	if (cmd->duplex != DUPLEX_FULL)     /* only full-duplex supported */
		return -EINVAL;

	if (!(lc->supported & FW_PORT_CAP_ANEG)) {
		/*
		 * PHY offers a single speed.  See if that's what's
		 * being requested.
		 */
		if (cmd->autoneg == AUTONEG_DISABLE &&
		    (lc->supported & speed_to_caps(speed)))
			return 0;
		return -EINVAL;
	}

	if (cmd->autoneg == AUTONEG_DISABLE) {
		cap = speed_to_caps(speed);

		if (!(lc->supported & cap) ||
		    (speed == 1000) ||
		    (speed == 10000) ||
		    (speed == 40000))
			return -EINVAL;
		lc->requested_speed = cap;
		lc->advertising = 0;
	} else {
		cap = to_fw_linkcaps(cmd->advertising);
		if (!(lc->supported & cap))
			return -EINVAL;
		lc->requested_speed = 0;
		lc->advertising = cap | FW_PORT_CAP_ANEG;
	}
	lc->autoneg = cmd->autoneg;

	if (netif_running(dev))
		return t4_link_start(p->adapter, p->adapter->fn, p->tx_chan,
				     lc);
	return 0;
}

static void get_pauseparam(struct net_device *dev,
			   struct ethtool_pauseparam *epause)
{
	struct port_info *p = netdev_priv(dev);

	epause->autoneg = (p->link_cfg.requested_fc & PAUSE_AUTONEG) != 0;
	epause->rx_pause = (p->link_cfg.fc & PAUSE_RX) != 0;
	epause->tx_pause = (p->link_cfg.fc & PAUSE_TX) != 0;
}

static int set_pauseparam(struct net_device *dev,
			  struct ethtool_pauseparam *epause)
{
	struct port_info *p = netdev_priv(dev);
	struct link_config *lc = &p->link_cfg;

	if (epause->autoneg == AUTONEG_DISABLE)
		lc->requested_fc = 0;
	else if (lc->supported & FW_PORT_CAP_ANEG)
		lc->requested_fc = PAUSE_AUTONEG;
	else
		return -EINVAL;

	if (epause->rx_pause)
		lc->requested_fc |= PAUSE_RX;
	if (epause->tx_pause)
		lc->requested_fc |= PAUSE_TX;
	if (netif_running(dev))
		return t4_link_start(p->adapter, p->adapter->fn, p->tx_chan,
				     lc);
	return 0;
}

static void get_sge_param(struct net_device *dev, struct ethtool_ringparam *e)
{
	const struct port_info *pi = netdev_priv(dev);
	const struct sge *s = &pi->adapter->sge;

	e->rx_max_pending = MAX_RX_BUFFERS;
	e->rx_mini_max_pending = MAX_RSPQ_ENTRIES;
	e->rx_jumbo_max_pending = 0;
	e->tx_max_pending = MAX_TXQ_ENTRIES;

	e->rx_pending = s->ethrxq[pi->first_qset].fl.size - 8;
	e->rx_mini_pending = s->ethrxq[pi->first_qset].rspq.size;
	e->rx_jumbo_pending = 0;
	e->tx_pending = s->ethtxq[pi->first_qset].q.size;
}

static int set_sge_param(struct net_device *dev, struct ethtool_ringparam *e)
{
	int i;
	const struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	struct sge *s = &adapter->sge;

	if (e->rx_pending > MAX_RX_BUFFERS || e->rx_jumbo_pending ||
	    e->tx_pending > MAX_TXQ_ENTRIES ||
	    e->rx_mini_pending > MAX_RSPQ_ENTRIES ||
	    e->rx_mini_pending < MIN_RSPQ_ENTRIES ||
	    e->rx_pending < MIN_FL_ENTRIES || e->tx_pending < MIN_TXQ_ENTRIES)
		return -EINVAL;

	if (adapter->flags & FULL_INIT_DONE)
		return -EBUSY;

	for (i = 0; i < pi->nqsets; ++i) {
		s->ethtxq[pi->first_qset + i].q.size = e->tx_pending;
		s->ethrxq[pi->first_qset + i].fl.size = e->rx_pending + 8;
		s->ethrxq[pi->first_qset + i].rspq.size = e->rx_mini_pending;
	}
	return 0;
}

static int closest_timer(const struct sge *s, int time)
{
	int i, delta, match = 0, min_delta PAsdev_privAPQ_r (i = 0; i < pi->nqSIZE(stats_s.ftinst_}

strx++s->ethPAsdev_ptins	e-s.ftinst_}

[i] (!(lc->PAsdev< eturnhPAsdev_p-PAsde (!(lc->PAsdev< lta PAsde	if (nelta PAsdev_pPAsde (!(	= 0, minnt er}eturn 0;
}

= 0, tatic int closest_timer(chres struct sge *s, int time)
{hresnt i, delta, match = 0, min_delta PAsdev_privAPQ_r (i = 0; i < pi->nqSIZE(stats_s.fstrucst_}

strx++s->ethPAsdev_pthres	e-s.fstrucst_}

[i] (!(lc->PAsdev< eturnhPAsdev_p-PAsde (!(lc->PAsdev< lta PAsde	if (nelta PAsdev_pPAsde (!(	= 0, minnt er}eturn 0;
}

= 0, taticReturn a versioport_'tatscstrupt hold-e Filins	in us if0 meansttertinststatic int deed int cap;
	qtinst_}

 struct adapter *adap,
		const  netif_ struct sge_eth_rxize  *qnsigned int cap;
	0));= q->p;
r(strucsAN_P1turn t4_ide0));<TAT_MNTIM e->? sge.ethrxqtinst_}

[idx]
	cmtaticReeturr_txqize _p;
r(strucsA-ge_psioport_'tatscstrupt holde Fistrucned sturr@q:ype oRxoport_turr@us:ype ohold-e Filins	in us, = 00entidis val;tinstturr@cnt:ype ohold-e Fis      struc, = 00entidis val;strucstturturrS    anoRxoport_'tatscstrupt hold-e Filins	ip os      struc ifA ftiastturr= Ae port. twov_pedsentibe d(dev))  = 0rt. of thentigenns.
eatscstruptsstatic int del_fi_txqize _p;
r(strucs( sge_eth_rxize  *q     led int cap;
	us, ed int cap;
	snttruct adapter *adap = netdevqter;
	 (adapte(usPORsnttTONEeturnsnt	t4_mgcmd->auntif (p->;

		err	al1, vev2pw_	skb = 	2pw_	sk->au_timer(chres >sge.ethrx,Rsntt (!(lc->qtedesUSE&vqtepk |
	_	sk-!=v2pw_	sk	if (ne/*0rt. of theen saleg(aysuccesceg( |=, epd.
eattif (cm	

	iT_CAARAMS_MNEM(T_CAARAMS_MNEM_DMAQ(CHELS netiT_CAARAMS_AARAM_X(T_CAARAMS_AARAM_DMAQ_IQNAL;CNTTHRESH(CHELS netiT_CAARAMS_AARAM_YZ>qte|
	xt
	/* M	= vlan_gk_suseparrucs(adap->fn, netdevfn, netdev_de1n_tc	    p->lin&2pw_	sk	 M	= >rx_prrnetdev t4_ide		err	aturnqtepk |
	_	sk-=v2pw_	skb f (cmudaptudapNEe>? 6
	ct_timer(const >sge.ethrx,Rubuf_sq->p;
r(strucsA= QAL;R_TIM e_IDX(us << 1snt	>Ee>? QAL;R_CNTES)
	cmd->d 0;
}

/* Deleteturnsqueue(p;
r(strucsA-ge_psio_pr  *dev,s'taRXatscstrupt holde Fistrucned!turn@ *d:ype o_prwques *dev,turn@us:ype ohold-e Filins	in us, = 00entidis val;tinsttur @cnt:ype ohold-e Fis      struc, = 00entidis val;strucstturtur S Work RRXatscstrupt hold-e Fistrucned s  = 0ao_prwques *dev,static int del_fi_txqix_p;
r(strucs( sge_etvice *dev,
			   structigned int end)
{us, ed int cap;
	snttruct delta,		err	 port_info *pi = netdev_priv(dev);
	struct adapter *adapter = >adapter;
	struct sge *s = xq *rx = &qap->sge.ethrxq[p->firsit_qset];

	memset = 0; i < pi->nqsets; ++i) {x++, q++s->ethvlan_g_txqize _p;
r(strucs(&q->ize ,{us, sntt (!(lc->prrnetde t4_ide		err	urn 0;
}

static int closes_txqr;
	siveqix_gs(strut net_device *dev, int sset)
r;
	siveqixnt i;
	const  port_info *pi = netdev_priv(dev);
	struct adapter *adapter = >adapter;
	struct sge *s = xq *rx = &qap->sge.ethrxq[p->firsit_qset];

	memset = 0; i < pi->nqsets; ++i) {x++, q++surnqteize =r;
	siveqixpter->tiiveqixturn t4_idestatic int closesgtxqr;
	siveqix_gs(strut net_device *dev, int truct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = >adapter;
	struct sge *s = xq *rx = &qap->sge.ethrxq[p->firsit_qset];

	memset t4_ideqteize =r;
	siveqixtatic int closes_txqcoalesUct net_device *dev, struct sk_bufl_cmd_speoalesUc *ctruct txqr;
	siveqix_gs(struttructc->useqr;
	siveqix_eoalesUcd->d 0;
}

_txqix_p;
r(strucs(tructc->ix_eoalesUc_usecs     lc)cax_pendineoalesUcd_frunc static int restargtxqcoalesUct net_device *dev, struct sk_bufl_cmd_speoalesUc *ctructstruct port_info *pi = netdev_priv(dev);
	const struct sge *sr *adapter = >adapter;
	structstruct sge_eth_rxize  *rqap->sge.ethrxq[p->firsit_qset];

	mesize msetc->ix_eoalesUc_usecsdevqtinst_}

 adap->rqnst sax_pendineoalesUcd_frunc >linrq->p;
r(strucsA& QAL;R_CNTES))MDIO_sge.ethrxqstrucst_}

[rqtepk |
	_	sk]
	cmta	c->useqr;
	siveqix_eoalesUc>ligtxqr;
	siveqix_gs(strut	const  0;
}

/* Deletetur	_len(stptovA-geiverl.
eaa d_sticalMSIZE;
 s = 0;  ntivirtualturr@d_stas = :ype od_sticalMSIZE;
 s = 0; turr@fn:ype oPCI fun    "r to idturr@sz:f(u32e pofun    "-specif reaeg(turturrTiverl.
eaa d_sticalMSIZE;
 s = 0;  ntivirtual schemeqset] 1K i turra  ",
 cap->oughivirtual s = 0; c >p->ad8;
	at 31K,ype ot_an i turra  ",
 cap->oughivirtual s = 0; c >p->ad8;
	at 0.turturrTe omapp8;
	is stafollow- bit	[ chiK) -> [31K..32K)bit	[1K..1K+A) -> [31K-A..31K)bit	[1K+A..ES) -> [0..ES-A-1K)bitbit	where A>li@fnur @sz,	ip oES>liSIZE;
 (u32static int del_fi_len(stptovned int caps)
d_stas = , ed int cap;
	tdeved int speed_tztructfnur_g_z(netif_d_stas =  <

statide t4_ided_stas =  + (31) | (1 (netif_d_stas =  <

sta + fntide t4_ide31744A-gfnu+ed_stas =  -

sta(netif_d_stas =  <
SIZE;
}

stide t4_ided_stas =  -

staA-gfnst  0;
}

L;

	if (ticReturnTe o_pxt twov>oud8;c >intation i_len(sy, bd/reg(afw_li d_sticalMs = 0; c static int del_fi_len(strdid_stt adapter *ap, voidap->ed int caps)
d_stas = , e= buvnt i;
	covs =  =i_len(stptovnd_stas = , fn, netdevSIZE;
PF}

stmgcmd->av= 0) {
		c= FWs =  =ime(aeg(apvpd>params.tructvs = , (u32))
		*pctvnst  0;
}

vs =  <
e>? vs =  :estatic int closes_len(stwrid_stt adapter *ap, voidap->ed int caps)
d_stas = , e= bvnt i;
	covs =  =i_len(stptovnd_stas = , fn, netdevSIZE;
PF}

stmgcmd->av= 0) {
		c= FWs =  =ime(areg(advpd>params.tructvs = , (u32))
		*pct&vnst  0;
}

vs =  <
e>? vs =  :estatice T5_REGSIZE;
_MAGICfc,
8E2F10Cic int get_eeprom_len(st net_device *dev, struct sk_bufl_cmd_sp_len(sy*		   uctignea)
{
	if (strdelta,		e	e->tx_ adapter *adapter = netdev2adap(dev);

	strlcpea)
star= kmalloc(SIZE;
}

s, GFP_KERNEL (!is_t4(
	starn -EBUSY;
NOMEM>rx_penmagic>liSIZE;
_MAGIC;et = 0; i <pene Fe_ps& ~3; !		e	&&i->nqpene Fe_ps+qpenlen 2)
		ratide		e	e-_len(strdid_sttr, p->adaructe= bu)&
	s[i]f (!netif_prrnetd(data, stats_start;pene Fe_p,qpenlen (!ikfree0, bnst  0;
}

		errtic int closes_txq_len(st net_device *dev, struct sk_bufl_cmd_sp_len(sy*	len(s	   uctignea)
{
	if (stea)
sta;
->;

		e	e->tx_e= balint c_e Fe_p,qalint c_len, netx_ adapter *adapter = netdev2adap(dev);

	strlcplc->plen(senmagic>!liSIZE;
_MAGICturn -EINVAL;

	if (adalint c_e Fe_p	e-_len(sene Fe_ps& ~3;adalint c_len>lin_len(senlen>+in_len(sene Fe_ps& 3)>+i3)>& ~3;aadapter->flags &n	>Ees->ethringsea;
	}

sta + r->flags &n	*vSIZE;
PF}

sf (!(lc->alint c_e Fe_p	<gsea;
	   (speedalint c_e Fe_p	+qalint c_len	>E+= sizevSIZE;
PF}

stturn -EINVAL;PERM	if (cmd->aalint c_e Fe_p	!e-_len(sene Fe_ps||qalint c_len	!e-_len(senlen *
		 * PHY ofRMWinfssiblyv_ped))  = 0qset] = 0lat] words/
		if (cmstar= kmalloc(alint c_len, GFP_KERNEL (!iis_t4(
	starnn -EBUSY;
NOMEM>rde		e	e-_len(strdid_sttr, p->adaalint c_e Fe_p,qte= bu), bnst netif_prr	&&ialint c_len	>Eatidee		e	e-_len(strdid_sttr, p->ad    p->lindalint c_e Fe_p	+qalint c_len	- 4d    p->lindte= bu)&
	s[alint c_len	- 4]t (!(lc->prrnetdegontioud;etd(data, start;n_len(sene Fe_ps& 3), stats__len(senlen ;se
		cmd->pstar= stat>rx_plan_gk_suslen(stwptr, p->adafacmd (!is_t4prrnetdgontioud;eet = 0; >adte= bu), b; !		e	&&ialint c_len;qalint c_len	-	ra, p++s->ethvlan_g_len(stwrid_sttr, p->adaalint c_e Fe_p,q*pt (!(alint c_e Fe_p	+	ra	if (cmd->a_prrnetdplan_gk_suslen(stwptr, p->adaadad (!oud:cmd->astar!= statnetdkfree0, bnst  0;
}

		errtic int closes_txqflatht net_device *dev, s2adap(ct sk_bufl_cmd_spflathy*	ff (strdel 0;uctstruct sge_etqsemwaeg *fwuct adapter *adapter = >ad2adap(dev);
2adap(nst ed int mk_adapbox
	iT_CACIER_BUMASTOR_GESK;
}
>rx_pf->stat[(u32))
pf->stat) -

		0x'\0'st  0;	0xted_fc _qsemwaeg(&fws__f->stat, fn, ne.tru_	if (is_t4(r_p	<geturn FW_POR 0;uc
e/*0Iport. r *adapten succesupleyk_aitializ cap-ceswe'll go ahg(a	ip 
Y oftryentigeWork Rqsemwaeg's coopns.
  "rin upgra 8;
	ntipe o_pw
Y ofqsemwaeg imag2e pe rwiseswe'll tryentidtipe o_poreg jobfw_li pe 
Y ofhouct...	ip ow. rlways " = ce"ipe oopns.
  "rin this path.
	if (capter->fs & FULL_INIT_DONE)
		returnpbox
	ir->fs pboxmset t4n_gk_sfw_upgra e adap->pboxdafw->stat, fw->(u32, 1nst  0tiase_qsemwaeg(fw (!is_t4( t4)ethPAv*pi =(fn, ne.tru_	if, "load))  semwaeg %s," stru"R 0load cxgb4 ));
	s\n"s__f->statnst  0;
}

 0;uctice T5_REGWOL_TED_Auton (WAKE_BCAST | WAKE_MAGICtue T5_REGBCAST_CRCfc,a0ccc1a6ic void get_sge_pawolt net_device *dev, struct sk_bufl_cmd_spwolpi = nwolf (stwolorted = from_fWAKE_BCAST | WAKE_MAGIC;stwolorwoloptsdev2adap(dev);

	storwol;et(buf, 0&wolortopassizeof(*s));
wolortopass)static int restars_pawolt net_device *dev, struct sk_bufl_cmd_spwolpi = nwolf (st>;

		e	e->tx_ port_info *pi = netdev_priv(dev);
	const!is_t4wolorwoloptsd& ~WOL_TED_Autonturn -EINVAL;
	t4_restarwol_magic_d(dev)(apter;
	strtx_chan, (stru structi4wolorwoloptsd& WAKE_MAGICt>? 	cotedev_s =  :eN   /;!is_t4wolorwoloptsd& WAKE_BCASTs->ethvlan_gtarwol_pat_d(dev)(apter;
	strtx_chan, (struf;
	2, ~0   d    p-~0   dzeoffacmd (!imd->a_prrnetdhvlan_gtarwol_pat_d(dev)(apter;
	strtx_chan, (struf1d    p-	~6   dz~0   dzBCAST_CRCdaadad (!e
		cmd->ptarwol_pat_d(dev)(apter;
	strtx_chan, (struf;uf;uf;uf;uffacmd (!i 0;
}

		errtic int closescxgbsusepfea;
}ect net_device *dev, struct_priv(dfea;
}ec_etqea;
}ectructstruct port_info *pi = netdev_priv(dev);
	const _priv(dfea;
}ec_et(strgrom_f	cotefea;
}ec ^tqea;
}ec;
->;

		e (!(lc->su(strgrom& NETIF_F_HW_VLAN_CTAG= 0;turn -EINVA0>rx_plan_gk_susxqixmod)(apter;
	strtx_char->fn, p->tx_ci, val);
-1u structi-1ui-1ui-1u structi!!(fea;
}ec & NETIF_F_HW_VLAN_CTAG= 0;daadad (!elc->unlikely4prrn)ethPAvtefea;
}ec = fea;
}ec ^tNETIF_F_HW_VLAN_CTAG= 0(!i 0;
}

		errtic int cle= bgs(stsc_edev) i +=t net_device *dev, int tructstruct port_info *pi = netdev_priv(dev);
	const!i 0;
}

ci, tsc_}

static int set_sggs(stsc_edev)t net_device *dev, struct = buf,nea)
keytructstruct port_info *pi = netdev_priv(dev);
	const ed int mk_adan>adaptetsc_}

stastwhile (n--)ethp[n]>adaptetsc[n]urn 0;
}

static void gl_fi_txqizc_edev)t net_device *dev, structunsigne= buf,nunsignea)
keytructed int cap;
	0tx_ port_info *pi = netdev_priv(dev);
	const!i = 0; i < pi->nqsetstsc_}

strx++sethpptetsc[i]>ada[i] (!tif_dchar->fn, p-> FULL_INIT_DONE)
		return -EBUSYreg(adass(trucpptetscturn 0;
}

static int identigs(stxnfct net_device *dev, struct ethtool_ringparxnfc *pi =	   uctige= burulectructstruct port_info *pi = netdev_priv(dev);
	constx_ wi0, m(pi =->
	if->etcaseOL_ID_INAGRXFH:->ethrd int v = 0;

	isetstsc_mod)f (!(li =-> val2;
c->ad wi0, m(pi =->flow
				cap = spseOTCP_V4_FLOW:M	= >rx_vPORT_CRSS_VI_CONFIG_CMD_IP4FOURTUPENnetdevli =-> val2;
RXH_IP_SRCf|
RXH_IP_DST |    p->lindRXH_L4_B_0_1f|
RXH_L4_B_2_3 M	= v (lc->suvPORT_CRSS_VI_CONFIG_CMD_IP4TWOTUPENnetdevli =-> val2;
RXH_IP_SRCf|
RXH_IP_DST M	= break; = spseOUDP_V4_FLOW:M	= >rx__vPORT_CRSS_VI_CONFIG_CMD_IP4FOURTUPENn    (lructi4vPORT_CRSS_VI_CONFIG_CMD_UDPENnnetdevli =-> val2;
RXH_IP_SRCf|
RXH_IP_DST |    p->lindRXH_L4_B_0_1f|
RXH_L4_B_2_3 M	= v (lc->suvPORT_CRSS_VI_CONFIG_CMD_IP4TWOTUPENnetdevli =-> val2;
RXH_IP_SRCf|
RXH_IP_DST M	= break; = spseOSCTP_V4_FLOW:M	=spseOAH_ESP_V4_FLOW:M	=spseOIPV4_FLOW:M	= >rx_vPORT_CRSS_VI_CONFIG_CMD_IP4TWOTUPENnetdevli =-> val2;
RXH_IP_SRCf|
RXH_IP_DST M	= break; = spseOTCP_V6_FLOW:M	= >rx_vPORT_CRSS_VI_CONFIG_CMD_IP6FOURTUPENnetdevli =-> val2;
RXH_IP_SRCf|
RXH_IP_DST |    p->lindRXH_L4_B_0_1f|
RXH_L4_B_2_3 M	= v (lc->suvPORT_CRSS_VI_CONFIG_CMD_IP6TWOTUPENnetdevli =-> val2;
RXH_IP_SRCf|
RXH_IP_DST M	= break; = spseOUDP_V6_FLOW:M	= >rx__vPORT_CRSS_VI_CONFIG_CMD_IP6FOURTUPENn    (lructi4vPORT_CRSS_VI_CONFIG_CMD_UDPENnnetdevli =-> val2;
RXH_IP_SRCf|
RXH_IP_DST |    p->lindRXH_L4_B_0_1f|
RXH_L4_B_2_3 M	= v (lc->suvPORT_CRSS_VI_CONFIG_CMD_IP6TWOTUPENnetdevli =-> val2;
RXH_IP_SRCf|
RXH_IP_DST M	= break; = spseOSCTP_V6_FLOW:M	=spseOAH_ESP_V6_FLOW:M	=spseOIPV6_FLOW:M	= >rx_vPORT_CRSS_VI_CONFIG_CMD_IP6TWOTUPENnetdevli =-> val2;
RXH_IP_SRCf|
RXH_IP_DST M	= break; = }urn -EINVA0>r }urcaseOL_ID_INAGRXRINGS:(!(li =-> val2;
sets; ++i) urn -EINVA0>r }ur -EINVAL;OPNOTTED_tatic int idstruct port_il_ringpaopsscxgbsl_ringpaopss0x100.ttings(strucuctign=ettings(struc	  .stings(strucuctign=estings(struc	  .ttindrvpi = uctign=ettindrvpi =	  .ttinmsgleveluctign=ettinmsglevel	  .stinmsgleveluctign=estinmsglevel	  .gs(stam *e)
{
tign=ettingam(struc	  .stintam *e)
{
tign=estingam(struc	  .gtxqcoalesUcuctign=ettincoalesUc	  .stincoalesUcuctign=estincoalesUc	  .prom_len(s_len	ign=ettin_len(s_len	  .prom_len(s  uctign=ettin	len(s	  .srom_len(s  uctign=estin	len(s	  .useparam(structign=ettinaram(struc	  .sromaram(structign=estinaram(struc	  .ttintart    uctign=el_ringpaop_ttintart	  .ttin potrucuctignn=ettingpotruc	  .sroma_stateuctignn=efy_port(adap,	  .nwayrt_aet  uctign=et_autoneg(struc	  .ttin stincountignn=ettingstincount	  .prom__ringpa intsn=ettingpnts	  .gs(stegs_len	ignnn=ettintegs_len	  .gs(stegs    uctign=egs(stegs	  .gs(swol     uctign=egs(swol	  .sromwol     uctign=ess(swol	  .gs(stxnfc   uctign=egs(stxnfc	  .gs(stxfh*pidi = ARRAY_gs(stsc_edev) i +=	  .gs(stxfh	ign=egs(stsc_edev)	  .stintxfh	ign=ess(stsc_edev)	  .flathe *dev, ctign=ess(sflath,
nt iReturndebugfsrted */
tatic int desi +=_etmemaeg(a( sge_etqslg *fslg,t(str __user)
staof(*s)_  struc,M	= loff_  *d *ctructloff_   *cn=e*d *c;ctloff_  avai
	elfslg*piod)(fslg)->i_}

sta ed int mk_adapem>adte_adptr_t)fslg->ev);a(ad val2& 3uct adapter *adapter = >adfslg->ev);a(ad val2-apemuct__be= bustat>rtrdel 0;uc(!tif_d*cn<geturn FW_PORL;
	t4_restif_d*cn>= avai
turn -EINVA0>rmd->auounti> avai
	-  *ctr	=sounti= avai
	-  *cuc(! val2;
t4_alloc_pemauount (!is_t4(statnetd -EBUSY;
NOMEM>rx_spin_ump( >sge.etwin0_ump();et t4n_gk_spemoryrtw adap->_delem,  *c, struc, stat, T4_MEMORY_READruct pin_unump( >sge.etwin0_ump();et_t4(r_pcap = k_sfree_pemastatnst   0;
}

 0;uc }ur -E->auopyps(suser0, bufstat, uount (!
 k_sfree_pemastatnst _t4(r_pcetd -EBUSY;
FAULT(!
 *d *c2;
s*c2+ uounturn 0;
}

uounturtic int idstruct port_ifslg*opns.
  "stmemadebugfs_fopss0x100.ownaptgn=eTHISYPE_ULE	  .opnn	ign=esintat*opnn	  .eg(a	ign=ememaeg(a	  .l (lek m_f	cfaulint (lek,
nt i void get_sgaddadebugfs_pema adapter *ap, voidap->struct(str *nameu structied int cap;
	0))eved int speed_t*s)_mbtruct port_iy_pory struc(! em_f	cbugfs_ceg( |_fslg(nameu S_IRUSR, fn, ne	cbugfs_root     lc(et_sg*)r = >+	0))ev&memadebugfs_fopsnst _t4( em&&f	cne	*piod))ethPAne	*piod)->i_}

sn=esis)_mb) | 2static void gl_fi_txupadebugfsa adapter *ap, voidapnt i;
	const rings

stast_t4(ISYERR_OR_N   (fn, ne	cbugfs_rooteturn -EAGAIN_mgcmdn_gk_seg(apteg adap->MA_TARGET_MEME)
		retst _t4(i2& EDRAM0E)
		ret->etht

sn=ek_seg(apteg adap->MA_EDRAM0EBARt (!(addadebugfs_pemaadap->"edc0"->MEME)DC0,	EDRAMstats_GET(
	for)uc }ur_t4(i2& EDRAM1E)
		ret->etht

sn=ek_seg(apteg adap->MA_EDRAM1EBARt (!(addadebugfs_pemaadap->"edc1"->MEME)DC1, EDRAMstats_GET(
	for)uc }ur_t4(ip->pardrams.chip)) {
		reg_rat

sn=ek_seg(apteg adap->MA_EXT_MEMORY_BARt (!(_t4(i2& EXT_MEME)
		ret    addadebugfs_pemaadap->"mc"->MEMEMCd    p-EXT_MEMEtats_GET(
	for)uc }{
		cap = _t4(i2& EXT_MEME)
		retif (net

sn=ek_seg(apteg adap->MA_EXT_MEMORY_BARt (!( addadebugfs_pemaadap->"mc0"->MEMEMC0d    p-EXT_MEMEtats_GET(
	for)uc  }urn_t4(i2& EXT_MEM1E)
		retif (net

sn=ek_seg(apteg adap->MA_EXT_MEMORY1_BARt (!( addadebugfs_pemaadap->"mc1"->MEMEMC1d    p-EXT_MEMEtats_GET(
	for)uc  }ur}(capter->fs l24)ethPAbugfs_ceg( |_fslg("l24"u S_IRUSR, fn, ne	cbugfs_root  		const  ->lin&k_st2t_fopsnst  0;
}

/* Deletturned er-lay, v));
	srted */
taticletturnAlloc.
eaan acsive-opnn	TID	ip oaet it	ntipe oted li spvalu2staticosescxgb4_alloc_oida( sge_etida*pi = nt  et_sg*{
	if (strdeloidan=eN_mgcmspin_ump(_bh(&t->oida_ump();et_t4(t->ofrees->ethrn  "raopnn__pory s_fw_l->ofreef (!(oidan=e(pA-ge->oida_ede)>+ie->oida_4_Fuuc  l->ofree ink_c_pxtuc  p-> val2;
stat>rt	e->oidatatnsuse++uc }ur pin_unump(_bh(&t->oida_ump();et 0;
}

oida* DeEXAP_ANSYMBOL(cxgb4_alloc_oida);icReturn atiaseaan acsive-opnn	TIDstaticet_sgcxgb4_free_oida( sge_etida*pi = nt  ed int speed_oida)ructed  "raopnn__pory s_fw_&e->oida_ede[oidan-ie->oida_4_Fu]mgcmspin_ump(_bh(&t->oida_ump();etk_c_pxtfw_l->ofreef  l->ofree inkf  l->oidatatnsuse--;ur pin_unump(_bh(&t->oida_ump();eDeEXAP_ANSYMBOL(cxgb4_free_oida);icReturnAlloc.
eaaoaer
	srTID	ip oaet it	ntipe oted li spvalu2staticosescxgb4_alloc_sida( sge_etida*pi = nt  p;
	tamit noet_sg*{
	if (strdelsida* cmspin_ump(_bh(&t->sida_ump();et_t4(tamit apNEPF_INETs->ethsidan=efpid_qset];zero_bit(t->sida_bmap->t->nsidast (!(_t4(sidan<>t->nsidastst  _susxqbit(sida->t->sida_bmapt (!(		cmd->psidan=eN_mg }{
		cap = sidan=ebitmap_fpid_qree_teg  "(t->sida_bmap->t->nsidas, 2t (!(_t4(sidan<>eturnhsidan=eN_mg }
(_t4(sidan{
		cmd->pt->sida_ede[sida]. val2;
stat>rt	sidan+=>t->sida_b_Fuuc  /*0IPv6xted_i}ec maxe po520ebits = 016 cet (rin TCAM
HY ofThis is ed_ivalefw_li 4rTIDs. With CLIP d(dev)) it
HY of_pedse2rTIDs.
		if (cmd->atamit apNEPF_INETsurnht->sidatatnsuse++uc (		cmd->pt->sidatatnsuse	+	ra	if (r pin_unump(_bh(&t->sida_ump();et 0;
}

_ida* DeEXAP_ANSYMBOL(cxgb4_alloc_sida);icRenAlloc.
eaaoaer
	srfslt	srTID	ip oaet it	ntipe oted li spvalu2staticosescxgb4_alloc_sfida( sge_etida*pi = nt  p;
	tamit noet_sg*{
	if (strdelsida* cmspin_ump(_bh(&t->sida_ump();et_t4(tamit apNEPF_INETs->ethsidan=efpid__pxt;zero_bit(t->sida_bmap-st  -t->nsidas>+ie->nsfidas->t->nsidast (!(_t4(sidan<>(t->nsidas>+ie->nsfidas)tst  _susxqbit(sida->t->sida_bmapt (!(		cmd->psidan=eN_mg }{
		cap = sidan=eN_mg }
(_t4(sidan{
		cmd->pt->sida_ede[sida]. val2;
stat>rt	sidan-=>t->nsidas>rt	sidan+=>t->sfida_4_Fuuc  l->sidatatnsuse++uc  (r pin_unump(_bh(&t->sida_ump();et 0;
}

_ida* DeEXAP_ANSYMBOL(cxgb4_alloc_sfida);icRen atiaseaaoaer
	srTIDstaticet_sgcxgb4_free_sida( sge_etida*pi = nt  ed int speed_tida->p;
	tamit f (st/*0Is it	aoaer
	srfslt	srTID?if (captee->nsfidasm&&f(sidan{
	t->sfida_4_Fu)s->ethsidan-=>t->sfida_4_Fuuc  sidan+=>t->nsidas>rt}{
		cap = sidan-=>t->sida_b_Fuuc } cmspin_ump(_bh(&t->sida_ump();et_t4(tamit apNEPF_INETs
  _sctiarqbit(sida->t->sida_bmapt (!	cmd->psitmap_ 0tiase_teg  "(t->sida_bmap->sida->2t (!t->sida_ede[sida]. val2;
Ncmd->a_t4(tamit apNEPF_INETs
  l->sidatatnsuse--;ur	cmd->pt->sidatatnsuse	-	ra	if pin_unump(_bh(&t->sida_ump();eDeEXAP_ANSYMBOL(cxgb4_free_sida);icReturnPopul.
eaa TID_RELEASE WR.  Call	srmust propnst at

snpe otkbstatic int deet_sgmk_ida_ 0tiase( sge_eth(_buff *tkb, ed int cap;
	sstru structed int to_fw_lda)ruct sge_etcpl_ida_ 0tiaserang msetss(swr_txq(tkb, CPL_PRIORITY_SETUP,t(steturn 0qap-( sge_etcpl_ida_ 0tiasera)_sukb_put(tkb, (*s));
ang r)uc ONE)
TP_WR(ng , ida);i	OPCODE_TID(ng )ap-htonl(MK_OPCODE_TID(CPL_TID_RELEASE, ida))* DeletturnQf thea TID  0tiaserted_fc 	ip o_t4ne ",
ary schedulhea wquesof thentturnpro ",
 itstatic int deet_sgcxgb4_of th_ida_ 0tiase( sge_etida*pi = nt  ed int speed_				     lc);
ed int to_fw_lda)ructet_sg*s_fw_&e->ida_ede[ida]uct adapter *adapter = >adcontaindev)f(t,t sge *sr *adap, idas)* cmspin_ump(_bh(&fn, neida_ 0tiase_ump();ets_fw_fn, neida_ 0tiase_hg(a;st/*0Lowe2rbits encodsnpe oTx_				nelr to idif (cfn, neida_ 0tiase_hg(aap-(et_sg*s)(te_adptr_t)_POR(steturns_t4(fn, neida_ 0tiase_tah(_busys->ethfn, neida_ 0tiase_tah(_busyfw_lruuuc  of th_wqueer->fs wque , &fn, neida_ 0tiase_tah()uc  (r pin_unump(_bh(&fn, neida_ 0tiase_ump();eDeletturnPro ",
 pe olit] =f g + 8;
	TID  0tiaserted_fc sstatic int deet_sgpro ",
_ida_ 0tiase_uit]( sge_etwque_ sge_et*wque)ruct sge_eth(_buff *tkbuct adapter *adapter =  (ada = >adcontaindev)f(wque,t sge *sr *adap, ida_ 0tiase_tah()uccmspin_ump(_bh(&fn, neida_ 0tiase_ump();etwhile (fn, neida_ 0tiase_hg(av |= SUt_sg*s_fw_fn, neida_ 0tiase_hg(a;st	ed int speed_				>adte_adptr_t)p2& 3uct	pap-(et_sg*)pA-g				f (!(on, neida_ 0tiase_hg(aap-netx_ts_fw_Ncmd->ar pin_unump(_bh(&fn, neida_ 0tiase_ump();e>arwhile (!(tkbfw_flloc_skb((*s));
 sge_etcpl_ida_ 0tiase)d    p->GFP_KERNEL )tst  schedulh(consout_untscstruptiev)t1);e>armk_ida_ 0tiase( kb, (strufpA-gfn, neidas.ida_ede); = k_soflcmd,ndaadap-> kb)->ar pin_ump(_bh(&fn, neida_ 0tiase_ump();et}(cfn, neida_ 0tiase_tah(_busyfw_facmd	if pin_unump(_bh(&fn, neida_ 0tiase_ump();eDeletturn atiaseaaoTID	ip opi =rm HW.  Ifow. rre
ed val;to_flloc.
eape ot_tiaseturnm",
ageswef	cfaptto_f wquesof thstaticet_sgcxgb4_removh_ida( sge_etida*pi = nt  ed int speed_				 
ed int to_fw_lda)ructet_sg*old;ct sge_eth(_buff *tkbuct adapter *adapter = >adcontaindev)f(t,t sge *sr *adap, idas)* cmoldfw_l->ida_ede[ida]uct kbfw_flloc_skb((*s));
 sge_etcpl_ida_ 0tiase)d>GFP_ATOMICturns_t4likely4 kb)cmd->pt->ida_ede[ida]fw_Ncmd->armk_ida_ 0tiase( kb, (strufida);i	 k_soflcmd,ndaadap-> kb)->a
		cmd->poxgb4_of th_ida_ 0tiase(t, (strufida);i	s_t4old)(!(oiomic_dec(&e->idatatnsuse);eDeEXAP_ANSYMBOL(cxgb4_removh_ida);icReturnAlloc.
eaap opiitializ npe oTID	edev)s if a vers 0 o

_u  ",
static int del_fiida*piit( sge_etida*pi = nt)ruct *s)_  }

sta ed int mk_adasida_bmap_}

sta ed int mk_adanoidatfw_l->noidatuct adapter *adapter = >adcontaindev)f(t,t sge *sr *adap, idas)* cmsida_bmap_}

s>adBITS_TO_LONGS(t->nsidas>+ie->nsfidas);
et

sn=ek->nidas>* (*s));
at->ida_ede) +
 netif_ noidatf* (*s));
at->aida_ede) +
 netif_ t->nsidas>* (*s));
at->sida_ede) +
 netif_ t->nsfidas>* (*s));
at->sida_ede) +
 netif_ sida_bmap_}

s>* (*s));
long) +
 netif_ t->nfidas>* (*s));
at->fida_ede) +
 netif_ t->nsfidas>* (*s));
at->fida_ede)(!
 k->ida_ede2;
t4_alloc_pema
	for (is_t4(t->ida_ede)etd -EBUSY;
NOMEM>rx_t->aida_ede>adted  "raopnn__pory s)&e->ida_ede[i->nidas] (!t->sida_edeap-( sge_etaer
__pory s)&e->oida_ede[noidat] (!t->sida_bmap>adted int mklong s)&e->sida_ede[i->nsidas>+ie->nsfidas] (!t->fida_edeap-( sge_etfslt	s__pory s)&e->sida_bmap[sida_bmap_}

s];cmspin_ump(_piit(&t->sida_ump();etspin_ump(_piit(&t->oida_ump();e
pt->sidatatnsuse	;
c->al->ofree inNcmd->al->oidatatnsuse	;
c->aoiomic_f, 0&e->idatatnsuse,cmd->st/*0Stxupork Rqree lit]  = 0aida_edeaap octiarnpe otidansitmap.if (captenoidatcmd->pwhile (--noidatcd->pt->oida_ede[noidat -

	._pxtfw_&e->oida_ede[noidat] (!al->ofree ine->oida_ede;et}(csitmap_zero(t->sida_bmap->t->nsidas>+ie->nsfidas);
eRen aaer
	otidan0  = 0T4/T5sr *adapsif (capte!t->sida_b_Fu    (if_ (ip->pardrams.chip)) {
		s||qip->5ardrams.chip)) {
		rs
  _susxqbit(0->t->sida_bmapt (t  0;
}

/* Delosescxgb4_clip_tti struct adaptevice *dev,
			   st _ struct sge_etin6_s =  *liptruct adapter *adap = netuct adaptefw_clip_md)
c (ada = >ad2adap(dev);
	static(buf, 0&cizeof(*s));
cr)uc c.opps(sreg(afp-htonl(FW_CMD_OP(FW_CLIP_CMD(CHELS FW_CMD_REQUEST | FW_CMD_WRITE)uc c.alloc_s(slen16fp-htonl(F_FW_CLIP_CMD_ALLOC | FW_LEN16
cr)uc c.ip_hdn_g*(__be64 s)4li.eth6_s = )uc c.ip_lon_g*(__be64 s)4li.eth6_s = 		s-);et 0;
}

tarwr_pbox_pea, adap->fn, nepboxda&ciz(*s));
crda&cizfacmd (!DeEXAP_ANSYMBOL(cxgb4_clip_ttit (tosescxgb4_clip_ 0tiase(struct adaptevice *dev,
			   st _   _ struct sge_etin6_s =  *liptruct adapter *adap = netuct adaptefw_clip_md)
c (ada = >ad2adap(dev);
	static(buf, 0&cizeof(*s));
cr)uc c.opps(sreg(afp-htonl(FW_CMD_OP(FW_CLIP_CMD(CHELS FW_CMD_REQUEST | FW_CMD_READructc.alloc_s(slen16fp-htonl(F_FW_CLIP_CMD_FREE | FW_LEN16
cr)uc c.ip_hdn_g*(__be64 s)4li.eth6_s = )uc c.ip_lon_g*(__be64 s)4li.eth6_s = 		s-);et 0;
}

tarwr_pbox_pea, adap->fn, nepboxda&ciz(*s));
crda&cizfacmd (!DeEXAP_ANSYMBOL(cxgb4_clip_ 0tiase);eletetur	cxgb4_ceg( |_aer
	sr-sceg( |aan IPoaer
	sturr@ *d:ype o *dev,tur	@sida:ype oaer
	srTIDtur	@sip: loc.l IPos = 0;  ntibip oaer
	srnttur	@s */
:ype oaer
	s'sOTCP  */
tat	@of th: of thentidirectnm",
agesfw_li peisoaer
	srntturtat	Ceg( |aan IPoaer
	s  = 0rt. given  */
aap os = 0; stat	 a vers <0 o

str= 0ap o= Ae port. %NET_XMIT_*pvalu2s o

_u  ",
staticosescxgb4_ceg( |_aer
	s(struct adaptevice *dev,
			   ed int speed_tida-st  _sbe= bsip, _sbe16fs */
, _sbe16fvl		     ed int cap;
	qf thnsigned int cap;
	s			f t sge_eth(_buff *tkbuct adapter *adapter =  (t sge_etcpl_pass*opnn_ 0qaang mstrdel 0;uc(! kbfw_flloc_skb((*s));
ang r, GFP_KERNEL (!is_t4( kb)etd -EBUSY;
NOMEM>rx_a = >ad2adap(dev);
	static 0qap-( sge_etcpl_pass*opnn_ 0qaa)_sukb_put(tkb, (*s));
ang r)uc ONE)
TP_WR(ng , 0);i	OPCODE_TID(ng )ap-htonl(MK_OPCODE_TID(CPL_E ||_OPEN_REQ->sida)atic 0q->loc.l_ 0;
	}
s */
tic 0q->pe	s_ 0;
	}
htons(0atic 0q->loc.l_ieed_tiptic 0q->pe	s_ieed_htonl(md->du			>adrxqps(sp			 >sge.ethrx,Rqf thntic 0q->opt0>adcpups(sbe64(TX_CHANu(str)ntic 0q->opt1>adcpups(sbe64(CONN_POLICY_ESK;|    pSYNCRSS_)
		rePORTYNCRSS_QUEUE(qf thn);et t4n_gk_spgmt_txaadap-> kb)->a -EBUSYvicexmit_e}

 rtit (DeEXAP_ANSYMBOL(cxgb4_ceg( |_aer
	s);elet	cxgb4_ceg( |_aer
	s6r-sceg( |aan IPv6oaer
	sturr@ *d:ype o *dev,tur	@sida:ype oaer
	srTIDtur	@sip: loc.l IPv6os = 0;  ntibip oaer
	srnttur	@s */
:ype oaer
	s'sOTCP  */
tat	@of th: of thentidirectnm",
agesfw_li peisoaer
	srntturtat	Ceg( |aan IPv6oaer
	s  = 0rt. given  */
aap os = 0; stat	 a vers <0 o

str= 0ap o= Ae port. %NET_XMIT_*pvalu2s o

_u  ",
staticosescxgb4_ceg( |_aer
	s6(struct adaptevice *dev,
			   ed int speed_tida-st   struct sge_etin6_s =  *sip, _sbe16fs */
,st   ed int cap;
	qf thnsigned int cap;
	s			f t sge_eth(_buff *tkbuct adapter *adapter =  (t sge_etcpl_pass*opnn_ 0q6aang mstrdel 0;uc(! kbfw_flloc_skb((*s));
ang r, GFP_KERNEL (!is_t4( kb)etd -EBUSY;
NOMEM>rx_a = >ad2adap(dev);
	static 0qap-( sge_etcpl_pass*opnn_ 0q6aa)_sukb_put(tkb, (*s));
ang r)uc ONE)
TP_WR(ng , 0);i	OPCODE_TID(ng )ap-htonl(MK_OPCODE_TID(CPL_E ||_OPEN_REQ6->sida)atic 0q->loc.l_ 0;
	}
s */
tic 0q->pe	s_ 0;
	}
htons(0atic 0q->loc.l_ie_hdn_g*(__be64 s)4si.eth6_s = )uc  0q->loc.l_ie_lon_g*(__be64 s)4si.eth6_s = 		s-);et 0q->pe	s_ie_hdn_gcpups(sbe64(0);et 0q->pe	s_ie_lon_gcpups(sbe64(0);etu			>adrxqps(sp			 >sge.ethrx,Rqf thntic 0q->opt0>adcpups(sbe64(TX_CHANu(str)ntic 0q->opt1>adcpups(sbe64(CONN_POLICY_ESK;|    pSYNCRSS_)
		rePORTYNCRSS_QUEUE(qf thn);et t4n_gk_spgmt_txaadap-> kb)->a -EBUSYvicexmit_e}

 rtit (DeEXAP_ANSYMBOL(cxgb4_ceg( |_aer
	s6t (tosescxgb4_removh_aer
	s(struct adaptevice *dev,
			   ed int speed_tida-st  ed int cap;
	qf th, bool ipv6)ruct sge_eth(_buff *tkbuct adapter *adapter =  (t sge_etcpl_t_tim_uit]svr_ 0qaang mstrdel 0;uc(!a = >ad2adap(dev);
	stati(! kbfw_flloc_skb((*s));
ang r, GFP_KERNEL (!is_t4( kb)etd -EBUSY;
NOMEM>rx_ 0qap-( sge_etcpl_t_tim_uit]svr_ 0qaa)_sukb_put(tkb, (*s));
ang r)uc ONE)
TP_WR(ng , 0);i	OPCODE_TID(ng )ap-htonl(MK_OPCODE_TID(CPL_CLOSE_LISTSRV_REQ->sida)atic 0q->reply_ctrl	}
htons(NO_REPLY(0 << 1ipv6 ? LISTSERNAPV6(1) :M	= 	LISTSERNAPV6(0) << QUEUENO(qf thn);et t4n_gk_spgmt_txaadap-> kb)->a -EBUSYvicexmit_e}

 rtit (DeEXAP_ANSYMBOL(cxgb4_removh_aer
	s);eletetur	cxgb4_bfc _mtuA-gfip ope o_poryrin the MTU	edev)ct_timertto_fn MTUtat	@mtus:ype oHW MTU	edev)tat	@mtu:ype otargeWoMTUtat	@idx:rindexe pos0ticfrom_poryrin the MTU	edev)turtat	 a vers the indexeap ope ovalu2rin the HW MTU	edev)s wha is t_timerttotat	 betdoeseallyexc0;
	@mtu  edl0;  @mtu is small	srt			>anyovalu2rin thetat	edev)	rin which caseO wha small	s  avai
dev)svalu2risos0ticfrostaticed int cap;
	sxgb4_bfc _mtu(structed int cash0;
	*mtus,ted int cash0;
	mtu  structied int cap;
	*	sk	ructed int cap;
	0 	if (cawhile (->nqNMTUS -

m&&fmtus[i;
}
] <=fmtu)etd++i(!is_t4	sk	r		*	sk-=vi->a -EBUSYmtus[i] (DeEXAP_ANSYMBOL(cxgb4_bfc _mtu);eletetur   _ sxgb4_bfc _alint c_mtuA-gfip obfc  MTU, [hopnupley]
statat

snalint ctur   _ @mtus:ype oHW MTU	edev)tat   _ @hg(ae = ARR: Hg(ae  SARRtat   _ @stat_sis)_max:rmaximum DtataSegion iSARRtat   _ @stat_sis)_alint:o *sired DtataSegion iSARR Alintion i(2^N)tur   _ @mtu_	skp: HW MTU	Tdev)sIndexe 0;
}

vslu2rpotscstf_d*csiblyvN   /turtur     Simi
dptto_sxgb4_bfc _mtu()o betirucg(aa pos0arch8;
	ne oHardwaegtur     MTU	Tdev)sb_Fucasolelyv "ra Maximum MTU	strucned ,swefbreakO whatur     strucned  upap;
o_f Hg(ae  SARReap oMaximum DtataSegion iSARR,	ip tur     srovideaao *sired DtataSegion iSARR Alintion .  Ifow. fip ofn MTUap;tur     ne oHardwaeg MTU	Tdev)swhich willet_auletiraaoDtataSegion iSARR withtur     ne oted_fc |=nalintion i_ip _O wha MTUapsn't "too far"fw_li pe 
ur   _ s_timertMTU, p-ceswe'll  0;
}

twha rape rrt			>ne os_timertMTUstaticed int cap;
	sxgb4_bfc _alint c_mtu(structed int cash0;
	*mtus,    lc);
ed int tosh0;
	hg(ae = ARR,    lc);
ed int tosh0;
	stat_sis)_max,    lc);
ed int tosh0;
	stat_sis)_alint     lc);
ed int to_fw_*mtu_	skp	ructed int cash0;
	max_mtuA=	hg(ae = ARR;
}stat_sis)_max;cted int cash0;
	stat_sis)_alint_mask2;
stat_sis)_alint -

mstrdelmtu_	skdaalint c_mtu_	sk->st/*0Scan the MTU	T val;till w. fip ofn MTUawhich isolargerrt			>our
Y ofMaximum MTU	= 0w oteach pe o_pde port. t val ifAlong rt. way,
Y ofrecor ope olat] MTU	founa->pf>any,swhich willet_auletiraaoDtat
Y ofSegion iLength
= 0, 8;
	ne oted_fc |=nalintion .
	if (c = 0;mtu_	sk in_dealint c_mtu_	skn=eN_mlmtu_	sk>nqNMTUSmlmtu_	sk++s->ethed int cash0;
	stat_sis) =Ymtus[mtu_	sk] -
hg(ae = ARR;e>ar/*0IportisoMTU	minus the Hg(ae  SARRewouldet_auletiraa
HY ofDtataSegion iSARR  port.  *sired alintion ,et_meo idiitst		if (cmd->a(stat_sis) &	stat_sis)_alint_masktTONEeturn	alint c_mtu_	skn=emtu_	sk->str/*0Ipowe'eg allyat pe o_pde port. Hardwaeg MTU	Tdev)sap ope 
HY of_pxlyeation iisolargerrt			>ourfMaximum MTU, dropioude p
HY ofpe oloopst		if (cmd->amtu_	sk+1>nqNMTUS &&fmtus[mtu_	sk+1] >	max_mtuturn	break; =}c
e/*0Ipow. felleoude pfpe oloopobfc (p->w otan to pe o_pde port. t val,
Y ofp-ceswe just havhentiuseope olat] [largest]m_pory.
	if (captemtu_	skn==qNMTUSturnptu_	sk--;u
e/*0Ipow. founaofn MTUawhich t_aule to_f	ne oted_fc |=nDtataSegion 
Y ofLength
alintion iap opehat's"allyfar"fw_li pe  largest MTUawhich is
Y ofl",
 pe		>or ed_al to pe omaximum MTU, p-cesuseO wha.
	if (capterlint c_mtu_	skn{
		    (if_ mtu_	skn-ealint c_mtu_	skn<=f1turnptu_	skfw_flint c_mtu_	sk->st/*0Iport. call	sren spass to_f	fn MTUaIndexepotscst,spass pe 
Y ofMTUaIndexeback if a ver the MTU	valu2st	if (captemtu_	skp	r		*mtu_	skpn=emtu_	sk->a -EBUSYmtus[mtu_	sk] (DeEXAP_ANSYMBOL(cxgb4_bfc _flint c_mtu);eletetur	cxgb4_nfo *u			>-igeWork RHW 				nelr poa  */
tat	@ *d:ype o_pro *dev,  = 0rt.  */
tattat	 a verork RHW Tx_				nelr port. given  */
staticed int cap;
	sxgb4_nfo *u			(struct adaptevice *dev,
			 	ruct -EBUSYvicap(dppi =(
	storn, (str (DeEXAP_ANSYMBOL(cxgb4_nfo *u			);eled int cap;
	sxgb4_dbfifoncount(struct adaptevice *dev,
			   p;
	lpfifotruct adapter *adap = netdev2adap(dev);
	statice= bv1, v2,	lpncount	 hpncount->stv1n=ek_seg(apteg adap->A_AT_MDBFIFO_STATUStticv2n=ek_seg(apteg adap->AT_MDBFIFO_STATUS2tst _t4(ip->pardrams.chip)) {
		reg_ralpncountn=eG_LP_COUNT(v1nst 	hpncountn=eG_HP_COUNT(v1nst }{
		cap = lpncountn=eG_LP_COUNT_T5(v1nst 	hpncountn=eG_HP_COUNT_T5(v2);et}(c -EBUSYlpfifo ?	lpncount : hpncount->DeEXAP_ANSYMBOL(cxgb4_dbfifoncount);eletetur	cxgb4_nfo *val)>-igeWork RVI idr poa  */
tat	@ *d:ype o_pro *dev,  = 0rt.  */
tattat	 a verork RVI idr port. given  */
staticed int cap;
	sxgb4_nfo *val)(struct adaptevice *dev,
			 	ruct -EBUSYvicap(dppi =(
	storval) (DeEXAP_ANSYMBOL(cxgb4_nfo *val));eletetur	cxgb4_nfo *	skn-egeWork Rindexe poa  */
tat	@ *d:ype o_pro *dev,  = 0rt.  */
tattat	 a verork Rindexe port. given  */
staticed int cap;
	sxgb4_nfo *	sk(struct adaptevice *dev,
			 	ruct -EBUSYvicap(dppi =(
	stornfo *	s (DeEXAP_ANSYMBOL(cxgb4_nfo *	sk	 Mcet_sgcxgb4_gs(stcpngpnts( sge_etme(a
	s *ptruct ethtootpstcpngpnts *v4d    t ethtootpstcpngpnts *v6truct adapter *adap = netdevme(attindrvstat(p	stati(! pin_ump( >sge.etgpnts_ump();etk_stp_gs(stcpngpnts(adap->v4d v6);etspin_unump( >sge.etgpnts_ump();eDeEXAP_ANSYMBOL(cxgb4_gs(stcpngpnts	 Mcet_sgcxgb4_iscsi*piit( sge_etvice *dev,
			   ed int speed_tag_mask  st _   _structed int ca_fw_*pgsz_or ertruct adapter *adap = netdev2adap(dev);
	statiestarweg(adaeg adap->ULP_RX_ISCSI_TAGGESK,_tag_mask);etk_sweg(adaeg adap->ULP_RX_ISCSI_PSZ, HPZ0(pgsz_or er[0](CHELS_   _HPZ1(pgsz_or er[1](CH_HPZ2(pgsz_or er[2](CHELS_   _HPZ3(pgsz_or er[3]));eDeEXAP_ANSYMBOL(cxgb4_iscsi*piitt (tosescxgb4_flush_eq_cach=t net_device *dev, int truct adapter *adap = netdev2adap(dev);
	staticrdel 0;uc(! t4n_gk_sfws = spacesweg(a adap->fn, nepboxd    lc);0xe1000000;
}A_AT_MCTXT_CMDuf;
2000000md->d 0;
}

 0;uctiEXAP_ANSYMBOL(cxgb4_flush_eq_cach=);ic void gl_fieg(apeq*pidicett adapter *ap, voidap->e16 qda->e16 *p0))eve16 *c	sk	ructe= ba =  =ik_seg(apteg adap->A_AT_MDBQMCTXT_BADDR)>+i24 ofqdan+s->et__be64 pidicetmstrdel 0;uc(! pin_ump( >sge.etwin0_ump();et t4n_gk_spemoryrtw adap->_deMEME)DC0,ba =   struct(*s));
pidicet), (__be= bu)&pidicet  structT4_MEMORY_READruct pin_unump( >sge.etwin0_ump();et_t4(!r_pcap = *c	skap-(be64ps(sppu
pidicet)AN_P25)>& 0xffff;x_ts_	skap-(be64ps(sppu
pidicet)AN_P9)>& 0xffff;x_}>d 0;
}

 0;uctitosescxgb4_sync_txq__	sk( sge_etvice *dev,
			   e16 qda->e16 p0))est  e16fs	forruct adapter *adap = netdev2adap(dev);
	statice16fhw_p0))evhw_c	sk->ardel 0;uc(! t4n_geg(apeq*pidicettadap->qda->&hw_p0))ev&hw_c	sk);et_t4(r_pcetdgontioud;eettif_dcsk-!=vhw_p0))s->ethe16fPAsde ((cmd->ap	skn{
	hw_p0))sst  PAsdev_pp	skn-ehw_p0));(!(		cmd->pPAsdev_psis) -ehw_p0))u+ed0));(!(wmb();i	 k_sweg(adaeg adap->MYPF_REG(AT_MPF_KDOORBELL)  structigQID(qda(CH_PIDX(PAsde	);et}(oud:cm 0;
}

 0;uctiEXAP_ANSYMBOL(cxgb4_sync_txq__	sk	 Mcet_sgcxgb4_dis val_dbncoalesUtrut net_device *dev, int truct port_ir *adapter =  (ada = >ad2adap(dev);
	statick_susxqieg_field adap->A_AT_MDOORBELL_CONTROL, F_NOCOALESCE  struF_NOCOALESCE);eDeEXAP_ANSYMBOL(cxgb4_dis val_dbncoalesUtru	 Mcet_sgcxgb4_d(dev)_dbncoalesUtrut net_device *dev, int truct port_ir *adapter =  (ada = >ad2adap(dev);
	statick_susxqieg_field adap->A_AT_MDOORBELL_CONTROL, F_NOCOALESCE  0);iDeEXAP_ANSYMBOL(cxgb4_d(dev)_dbncoalesUtrut (tosescxgb4_re(aptadat net_device *dev, struct = b vog, _sbe= butadatruct adapter *adap = netuct = be Fe_p,qpem				,qpema = uct = bedc0= ARR,bedc1= ARR,bmc0= ARR,bmc1= ARRuct = bedc0=ena->edc1=ena->mc0=ena->mc1=enamstrdel 0;uc(!a = >ad2adap(dev);
	stati(!e Fe_p	e-(( vogAN_P8) of32) + r->forvres. vog.auton->st/*0Figureeoudewhere pe oo Fe_p	landsrin the Memory T			/A = 0;  schem2st	iffThis codsnassunc >twha pe omemory isolaidr ut>p->ad8;
	at o Fe_p	0t	iffwith nofbreaks st: )DC0,b)DC1, MC0d MC1.nAll cardsrhavheboth )DC0t	iffip oEDC1.n Some cardsrwillehavheneipe rrMC0 norrMC1, mouctcardsrhavh
Y ofMC0,bap oaome havheboth MC0 ap oMC1.t	if (cedc0= ARR	e-EDRAMstats_GET(k_seg(apteg adap->MA_EDRAM0EBARt)) | 2sta	edc1= ARR	e-EDRAMstats_GET(k_seg(apteg adap->MA_EDRAM1EBARt)) | 2sta	mc0= ARR	e-EXT_MEMEtats_GET(k_seg(apteg adap->MA_EXT_MEMORY_BARt)) | 2sta(cedc0=_pde=bedc0= ARRta	edc1=_pde=bedc0=_pde+bedc1= ARRta	mc0=_pde=bedc1=_pde+bmc0= ARRtai	s_t4o Fe_p	<gedc0=_pds->ethpem				e=bMEME)DC0;ethpema =  =io Fe_pst }{
		cas_t4o Fe_p	<gedc1=_pds->ethpem				e=bMEME)DC1;ethpema =  =io Fe_p -eedc0=_pd;c }{
		cap = _t4(o Fe_p	<gmc0=_pds->ethhpem				e=bMEMEMC0;ethhpema =  =io Fe_p -eedc1=enamst }{
		cas_t4ip->pardrams.chip)) {
		reg_rat/*0T4 onlyren stat
ngl omemory 				nelrf (cmdgonti		err	 }{
		cap = 	mc1= ARR	e-EXT_MEMEtats_GET(    p-k_seg(apteg adap-    p-ructiMA_EXT_MEMORY1_BARt)) | 2sta	 	mc1=_pde=bmc0=_pde+bmc1= ARRuct= _t4(o Fe_p	<gmc1=_pds->ethhhpem				e=bMEMEMC1;ethhhpema =  =io Fe_p -emc0=_pd;c 	 }{
		cap = 	t/*0o Fe_p beyop ope o_pdr poanyomemory f (cmddgonti		err	  }urn}c } cmspin_ump( >sge.etwin0_ump();et t4n_gk_spemoryrtw adap->_delem				,qpema = ,f32,_tada, T4_MEMORY_READruct pin_unump( >sge.etwin0_ump();et FW_POR 0;uc
		e:
hPAv*		e(fn, ne.tru_	if, " vogA%#)evo Fe_p %#)eoude pfrtrgr\n"s = siog, o Fe_p);et FW_PORL;

	if (tiEXAP_ANSYMBOL(cxgb4_re(aptada);ele64 cxgb4_re(apgam(conssiompt net_device *dev, int truct = bhi, louct adapter *adapter =  (ada = >ad2adap(dev);
	staticlon_gk_seg(apteg adap->AT_MTIMESTAMP_LOatichdn_gGET_TS	if(k_seg(apteg adap->AT_MTIMESTAMP_HI)t (t  0;
}

((e64)hdn | 32) | (e64)louctiEXAP_ANSYMBOL(cxgb4_re(apgam(conssiomp);ic void g sge_etme(a
);
	srcxgb4_d);
	st i void get_sgcheck_neigh_updadat net_deviighbourf*viightructstruct port_i *dev, i.chen;uctstruct sge_etvice *dev, s2adap(>ad2aightedevtai	s_t42adap(->ev);_> FULL_IIFF_802_1Q_VLANsst 2adap(>advl		e *d_re(le *d
2adap(nst .chen;>ad2adap(tedev..chen;uctd->apchen;>&& pchen;ted);
	sr== &cxgb4_d);
	s.d);
	s)i	 k_st2t_updadat *d_ttindrvstat(pchen;),d2aigh)tatic void gl_fi2adeven;_cbt net_devotifier_bump( s2b, ed int calong even;  st _   _ et_sg*{
	if (st wi0, m(even;f->etcaseONETEVENT_NEIGH_UPDATE:M	=sheck_neigh_updadatstatnst  break; =caseONETEVENT_REDIRECT:
hPAfauli:
n	break; =}cn 0;
}

static int idbool 2adeven;_teg sdaped;c void g sge_etvotifier_bump( cxgb4_2adeven;_nbfw_{  .notifier_call>ad2adeven;_cb
nt i void get_sgdrain_dbnfifot adapter *ap, voidap->l_fiusecstruct = bv1, v2,	lpncount	 hpncount->stdo |= SU1n=ek_seg(apteg adap->A_AT_MDBFIFO_STATUStticcv2n=ek_seg(apteg adap->AT_MDBFIFO_STATUS2tst  _t4(ip->pardrams.chip)) {
		reg_raalpncountn=eG_LP_COUNT(v1nst 		hpncountn=eG_HP_COUNT(v1nst  }{
		cap = 	lpncountn=eG_LP_COUNT_T5(v1nst 		hpncountn=eG_HP_COUNT_T5(v2);et } cmns_t4lpncountn=
		    hpncountn=
		turn	break; =tss(scurhen;ngpnte(TASK_UNINTERRUPTI	retst  schedulh(consout(usecsps(sjiffies(usecsr)uc }{while (1)tatic void get_sgdis val_txq_dbt net_degam(cxq *q	ructed int calong > FULuccmspin_ump(_irqsavh(&qtedb_ump(, > FUL)uc qtedb_dis valde=b1uct pin_unump(_irqt_auorh(&qtedb_ump(, > FUL)uctic void get_sgd(dev)_txq_dbt net_der *ap, voidap-> net_degam(cxq *q	ructspin_ump(_irq(&qtedb_ump();et_t4(qtedb__	sk_inc *
		 *  Mak otereO wha all>weg(as to pe oTX  *scripuors
HY ofaeg commit |=nbeforhow. telleHW aboudepe mst		if (cmwmb();i	 k_sweg(adaeg adap->MYPF_REG(AT_MPF_KDOORBELL)  structigQID(q->cntx *	s(CH_PIDX(qtedb__	sk_inc );i	 qtedb__	sk_inc	;
c->a}c qtedb_dis valde=b0uct pin_unump(_irq(&qtedb_ump();etic void get_sgdis val_dbsa adapter *ap, voidapnt i;
	const(c = _eachm__rrxq >sge.ethrx,Ri)ethPis val_txq_dbt>sge.ethrx.__rixq[i].q);i	 = _eachmoflcrxq >sge.ethrx,Ri)ethPis val_txq_dbt>sge.ethrx.oflcixq[i].q);i	 = _eachmnfo  adap->i)ethPis val_txq_dbt>sge.ethrx.ctrlq[i].q);itic void get_sgd(dev)_dbsa adapter *ap, voidapnt i;
	const(c = _eachm__rrxq >sge.ethrx,Ri)ethd(dev)_txq_dbtadap->>sge.ethrx.__rixq[i].q);i	 = _eachmoflcrxq >sge.ethrx,Ri)ethd(dev)_txq_dbtadap->>sge.ethrx.oflcixq[i].q);i	 = _eachmnfo  adap->i)ethd(dev)_txq_dbtadap->>sge.ethrx.ctrlq[i].q);itic void get_sgnotifyrtdma_uldt net_der *ap, voidap->enumscxgb4_control 
	ift i;
pter->fs uld_			dle[CXGB4_ULD_RDMA])ethulds[CXGB4_ULD_RDMA].controler->fs uld_			dle[CXGB4_ULD_RDMA]-    p
	if;itic void get_sgpro ",
_dbnfull( sge_etwque_ sge_et*wque)ruct sge_etr *adapter =  (ada = >adcontaindev)f(wque,t sge *sr *adap, dbnfull_tah()uccmdrain_dbnfifotidap->dbfifondrain_delay);i	d(dev)_dbsaidapn;i	notifyrtdma_uldtidap->CXGB4_CONTROLMDB_EMPTYatick_susxqieg_field adap->AT_MINT_)
		re3  struDBFIFO_HP_INTCH_DBFIFO_LP_INT  struDBFIFO_HP_INTCH_DBFIFO_LP_INTf;itic void get_sgsync_txq__	sk( sge_etr *ap, voidap-> net_degam(cxq *q	ructe16fhw_p0))evhw_c	sk->ardel 0;uc(!spin_ump(_irq(&qtedb_ump();et t4n_geg(apeq*pidicettadap->(e16)q->cntx *	s->&hw_p0))ev&hw_c	sk);et_t4(r_pcetdgontioud;et_t4(qtedb__	sk-!=vhw_p0))s->ethe16fPAsde ((cmd->aqtedb__	sk-{
	hw_p0))sst  PAsdev_pqtedb__	sk--ehw_p0));(!(		cmd->pPAsdev_pq->(u32 -ehw_p0))u+eqtedb__	sk;(!(wmb();i	 k_sweg(adaeg adap->MYPF_REG(AT_MPF_KDOORBELL)  structigQID(q->cntx *	s(CH_PIDX(PAsde	);et}(oud:cmqtedb_dis valde=b0uctqtedb__	sk_inc	;
c->a pin_unump(_irq(&qtedb_ump();et_t4(r_pcetdCH_WARNaadap->"DB dropireco
	sy failed.\n"f;iti void get_sgreco
	s_all_of thsa adapter *ap, voidapnt i;
	const(c = _eachm__rrxq >sge.ethrx,Ri)ethsync_txq__	sk(adap->>sge.ethrx.__rixq[i].q);i	 = _eachmoflcrxq >sge.ethrx,Ri)ethsync_txq__	sk(adap->>sge.ethrx.oflcixq[i].q);i	 = _eachmnfo  adap->i)ethsync_txq__	sk(adap->>sge.ethrx.ctrlq[i].q);itic void get_sgpro ",
_dbndrop( sge_etwque_ sge_et*wque)ruct sge_etr *adapter =  (ada = >adcontaindev)f(wque,t sge *sr *adap, dbndrop_tah()uccm_t4(ip->pardrams.chip)) {
		reg_radrain_dbnfifotidap->dbfifondrain_delay);i		notifyrtdma_uldtidap->CXGB4_CONTROLMDB_DROP);i		drain_dbnfifotidap->dbfifondrain_delay);i		reco
	s_all_of thsaidapn;i		drain_dbnfifotidap->dbfifondrain_delay);i		d(dev)_dbsaidapn;i		notifyrtdma_uldtidap->CXGB4_CONTROLMDB_EMPTYatic}{
		cap =  = bdropped_dbn=ek_seg(apteg adap->0x010acn;i		e16 qda	e-(dropped_dbn>> 15)>& 0x1ffff;x_te16 p0))_inc	;
dropped_dbn& 0x1fff;
  ed int cap;
	s_qpp;
  ed int cash0;
	udbnded ity;
  ed int calong qpshiftuc  p;
	pagRuct= = budb ((cm *d_warn(fn, ne.tru_	if, stru"Dropped_DB 0x%x qda	%=nbar2	%=ncoalesUcu%=np0))u%d\n"s = 	
dropped_db->qda- = 	
(dropped_dbn>> 14)>& 1- = 	
(dropped_dbn>> 13)>& 1- = 	
_	sk_inc  ((cm rain_dbnfifotidap->1);e>ars_qpp	;
QUEUESPERPAGEPF1 ofan, nefnuct= dbnded itye=b1n | QUEUESPERPAGEPF0_GET(k_seg(apteg adap-    pAT_MEGRESS_QUEUES_PER_PAGEMPF)AN_Ps_qpp);i	 qpshifte=bPAGEMSHIFT -eilog2( dbnded ity)uct= dbv_pqdan<< qpshiftuc   dbv&=bPAGEMGESKuc  pagRv_p dbv/bPAGEMSIZEuc   dbv+e-(ql)>-i(pagRv*	udbnded ity)) of128;e>arweg(al(PIDX(_	sk_inc , fan, nebar2	+p dbv	s-);e		 *  Re-d(dev) BAR2 WCif (cmk_susxqieg_field adap->0x10b0->1<<15->1<<15);et}(ick_susxqieg_field adap->A_AT_MDOORBELL_CONTROL, F_DROPPEDMDB  0);iDecet_sgt4_dbnfull( sge_etr *ap, voidapnt i;
t4(ip->pardrams.chip)) {
		reg_radis val_dbsaidapn;i		notifyrtdma_uldtidap->CXGB4_CONTROLMDB_F   /;!ick_susxqieg_field adap->AT_MINT_)
		re3  strruDBFIFO_HP_INTCH_DBFIFO_LP_INT  0);i	 of th_wqueer->fs wque , &fn, nedbnfull_tah()uct}(Decet_sgt4_dbndropped( sge_etr *ap, voidapnt i;
t4(ip->pardrams.chip)) {
		reg_radis val_dbsaidapn;i		notifyrtdma_uldtidap->CXGB4_CONTROLMDB_F   /;!i}c qf th_wqueer->fs wque , &fn, nedbndrop_tah()uctic void get_sguld_attacht adapter *ap, voidap->ed int cap;
	ula)ructet_sg*			dleuct adaptecxgb4_lla*pi = lli->aed int cash0;
	nst(clli.pap(>adfn, ne.tru;(clli.pf>adfn, nefnuctlli.l2t>adfn, nel2tuctlli.idatfw_&fn, neidas;(clli.p0;
s>adfn, ne.*/
ticlli.vrfw_&fn, nev}ec;
-lli.mtus>adfn, ne.chip))mtus;et_t4(ulde== CXGB4_ULD_RDMAreg_ralli.rxqpdatfw_sge.ethrx.tdma_rxq;_ralli.ciqpdatfw_sge.ethrx.tdma_ciq;_ralli.nrxqfw_sge.ethrx.tdmaqs;_ralli.nciqfw_sge.ethrx.tdmaciqs>rt}{
		ca_t4(ulde== CXGB4_ULD_ISCSIreg_ralli.rxqpdatfw_sge.ethrx.oflcmrxq;_ralli.nrxqfw_sge.ethrx.oflc ++i) ur}
-lli.ntxqfw_sge.ethrx.oflc ++i) urlli.nc			>adfn, ne.chip))np0;
s urlli.np0;
s>adfn, ne.chip))np0;
s urlli.wr_cegd>adfn, ne.chip))oflc _wr_cegd urlli.r *ap, _				e=brdrams.chip)) {
	 urlli.iscsi*polen	= MAXRXDATA_GET(k_seg(apteg adap->TP_PARA_REG2	);etlli.cclk_pss0x1000000000;/brdrams.chip))vpd.cclk;etlli. dbnded itye=b1n | QUEUESPERPAGEPF0_GET(
 p-k_seg(apteg adap- AT_MEGRESS_QUEUES_PER_PAGEMPF)AN_
 p-(fn, nefn of4	);etlli.ucqnded itye=b1n | QUEUESPERPAGEPF0_GET(
 p-k_seg(apteg adap- AT_MINGRESS_QUEUES_PER_PAGEMPF)AN_
 p-(fn, nefn of4	);etlli.fslt_mod)e=brdrams.chip))tp.vl		epri_metuct*  MODQ_REQMGEPoae
s>of ths 0-3tto_s			>0-3tf (c = 0;0 	if  ->nqNCHAN  -++)_ralli.tx_modq[i]-=vi->alli.gtsptege=brdramstegs +>MYPF_REG(AT_MPF_GTS);etlli.dbntege=brdramstegs +>MYPF_REG(AT_MPF_KDOORBELL);etlli.fw_vapsi=brdrams.chip))fw_vaps;etlli.dbfifonp;
__rresh	;
dbfifonp;
__rresh;etlli.gam(am *edbounaary w_sge.ethrx.fl_flint;etlli.gam(egr voiuspagR ARR	e-sge.ethrx. voislen;etlli.gam(pktshifte=bsge.ethrx.pktshift;etlli.d(dev)_fw_oflcmcon	>adfn, ne> FULL_IFW_OFLD_CONN;
-lli.max_or ird_qp>adfn, ne.chip))max_or ird_qp;
-lli.max_ird_r *ap, vadfn, ne.chip))max_ird_r *ap, ;etlli.ulptx_memweg(addsglvadfn, ne.chip))ulptx_memweg(addsglst(c			dlev_p lds[ ld].r d(&lli);et_t4(ISYERR(			dle	reg_rad*d_warn(fn, ne.tru_	if, stru"couldeallyattach po pe o%sv));
	s,
str= 0%ld\n"s = 	
uld_ ad[ ld], PTRYERR(			dle	r;i		re;
}
;et}(icr->fs uld_			dle[ ld]A=	h		dleucet_t4(!2adeven;_teg sdapedreg_rateg sdap_2adeven;_notifier(&cxgb4_2adeven;_nbn;i		nadeven;_teg sdaped w_lruuuc }
i;
pter->fs > FULL_IF   _ONE)
DONE)ethulds[ ld].gpnte*u			ge(			dle->CXGB4_STATE_UP)uctic void get_sgattach_uldsa adapter *ap, voidapnt i;ed int cap;
	0uc(! pin_ump( >sge._rcu_ump();etuit]_s =_tail_rcu >sge.etrcu_iod), &fn, _rcu_uit]ruct pin_unump( >sge._rcu_ump();e
	mutex_ump( >uld_mutex);etuit]_s =_tail >sge.etuit]_iod), &fn, dap_uit]ruct = 0;0 	if  ->nqCXGB4_ULD_MAX  -++)_ra_t4(ulds[i].r dsst  uld_attachtadap->i);
	mutex_unump( >uld_mutex);etic void get_sgdetach_uldsa adapter *ap, voidapnt i;ed int cap;
	0uc(!mutex_ump( >uld_mutex);etuit]_del >sge.etuit]_iod)ruct = 0;0 	if  ->nqCXGB4_ULD_MAX  -++)_ra_t4(r->fs uld_			dle[i]reg_raaulds[i].gpnte*u			ge(r->fs uld_			dle[i]d    p->>>>>CXGB4_STATE_DETACHnst 		r->fs uld_			dle[i] w_Ncmd->ar}i	s_t42adeven;_teg sdaped && lit]_empty(&fn, dap_uit]rs->ethedteg sdap_2adeven;_notifier(&cxgb4_2adeven;_nbn;i		nadeven;_teg sdaped w_facmd	if}
	mutex_unump( >uld_mutex);e(! pin_ump( >sge._rcu_ump();etuit]_del_rcu >sge.etrcu_iod)ruct pin_unump( >sge._rcu_ump();etic void get_sgnotifyruldsa adapter *ap, voidap->enumscxgb4_gpnte new_gpntent i;ed int cap;
	0uc(!mutex_ump( >uld_mutex);et = 0;0 	if  ->nqCXGB4_ULD_MAX  -++)_ra_t4(r->fs uld_			dle[i]r_raaulds[i].gpnte*u			ge(r->fs uld_			dle[i]d new_gpnten;
	mutex_unump( >uld_mutex);eticetetur	cxgb4_teg sdap_ulde- teg sdap	fn ed er-lay, v));
	stat	@				:ype oULD 				tat	@p:ype oULD methodstattat	 ag sdaps	fn ed er-lay, v));
	sfwith rtiso));
	sfap onotifiesype oULDtat	aboudeanyopt_aently avai
dev)s *dev, >twha ted */
 its 				 if a verstat	%-EBUSY>pf>aoULD  port. same 				 isoaleg(ay teg sdapedstaticosescxgb4_teg sdap_uld(enumscxgb4_ulde				,qstruct sge_etcxgb4_uld*pi = npnt i;
	co t4n_g0;ct sge_etr *adapter =  (adaptee			 >=qCXGB4_ULD_MAXturn FW_PORL;
	t4_resmutex_ump( >uld_mutex);et_t4(ulds[e			].r dseg_rate4n_g-EBUSY;etdgontioud;et}
	ulds[e			] p-netx_uit]_ = _eachm_pory(adap->>sge.dap_uit], uit]_iod)r
  uld_attachtadap->e			);eoud:	mutex_unump( >uld_mutex);em 0;
}

 0;uctiEXAP_ANSYMBOL(cxgb4_teg sdap_uld);eletetur	cxgb4_edteg sdap_ulde- edteg sdap	fn ed er-lay, v));
	stat	@				:ype oULD 				tattat	Udteg sdaps	fn ex sd8;
	ed er-lay, v));
	sstaticosescxgb4_edteg sdap_uld(enumscxgb4_ulde				)ruct sge_etr *adapter =  (adaptee			 >=qCXGB4_ULD_MAXturn FW_PORL;
	t4_resmutex_ump( >uld_mutex);etuit]_ = _eachm_pory(adap->>sge.dap_uit], uit]_iod)r
  r->fs uld_			dle[e			] p-Ncmd->aulds[e			].r d p-Ncmd->amutex_unump( >uld_mutex);em 0;
}

0uctiEXAP_ANSYMBOL(cxgb4_edteg sdap_uld);elet Checko_t4nedap(>on which even; isooccur|=nbelongsentius>or not.f a ver
ur _u  ",
teeruu)o_t4itnbelongseope rwise failereO(facmd stat Call	dfwith rcu_eg(apump( ) heldstatic#_t4IS_)
		reD(CONFIGNAPV6)c int idbool cxgb4_2ad *d
struct sge_etvice *dev, s2adap(truct adapter *adap = netuctp;
	0uc(!uit]_ = _eachm_pory_rcu adap->>sge._rcu_uit], rcu_iod)r
   = 0;0 	if  ->nqMAX_NAP_AS  -++)_raa_t4(r->fs  */
[i] wad2adap()_raat 0;
}

truuuc  0;
}

facmd	itic void gl_ficlip_r d( sge_etvice *dev, seven;_truct ethtooivic6_ifs =  *ifs  st _  ed int calong even;nt i;
	co t4n_gNOTIFY
DONE (t  cu_eg(apump( );et_t4(cxgb4_2ad *d
even;_tru)s->ethswi0, m(even;f->et=caseONETDEV_UP:M	=  t4n_gcxgb4_clip_tti even;_truc_raat(struct sge_etin6_s =  *)ifs->s = .h6_s = )uc 	t_t4(r_pn<>etap = 	t cu_eg(apunump( )uc 	tm 0;
}

 0;uc	  }urn	 t4n_gNOTIFY
OKuc  	break; =tcaseONETDEV_DOWN:M	= cxgb4_clip_ 0tiase(even;_truc_raat(struct sge_etin6_s =  *)ifs->s = .h6_s = )uc 	t t4n_gNOTIFY
OKuc  	break; =tPAfauli:
n		break; =t}ur}(c cu_eg(apunump( )uc  0;
}

 0;uctit void gl_ficxgb4_ivic6s = _			dlert net_devotifier_bump( srtis,
  ed int calong even;  et_sg*{
	if (st ethtooivic6_ifs =  *ifs ;
stat>rt sge_etvice *dev, seven;_tru;i;
	co t4n_gNOTIFY
DONE (t sge_etbo+ 8;
	*bo+ >ad2adap(epriv(ifs->iap(tedev) (t sge_etuit]_hg(aa*ip, ;et net_deglavhe*glavh;et net_deme(a
	s *qset];pap(>adNcmd->i;
t4(ifs->iap(tedev->ev);_> FULL_IIFF_802_1Q_VLANsap = even;_tru>advl		e *d_re(le *d
ifs->iap(tedev) (t  t4n_gclip_r d(even;_tructifs  even;n>rt}{
		ca_t4(ifs->iap(tedev->> FULL_IIFF_MASTER *
		 *  I; isod*csibleO wha twtidiffehen;>r *adapsfaeg bo+  to_f	on 
HY ofbo+ . Wef_pedentifip o_u hidiffehen;>r *adapsfap os =gclip
HY of_f	fllr port.m onlyroncest		if (cmbo+ _ = _eachmglavh(bo+ ,eglavhctidaptap = 	_t4(!qset];pap(tap = 	t t4n_gclip_r d(glavhtedevctifs  even;n>rt	tr/*0Ipoclip_r drisosu  ",
tp-cesonlyrpiitializ  strru* qset];pap(>since4itnmeans it	isoouro *dev,tstrru* (cmdd_t4(r_pn=_gNOTIFY
OK)_raat	qset];pap(>adto_me(a
	s(    p-thslavhtedevtedev..chen;n>rt	t}{
		ca_t4(qset];pap(>!=
  p->>>to_me(a
	s(slavhtedevtedev..chen;n)_raat	 t4n_gclip_r d(glavhtedevctifs  even;n>rt	}>a
		cmd->p t4n_gclip_r d(ifs->iap(tedevctifs  even;n>rc  0;
}

 0;uctit void g sge_etvotifier_bump( cxgb4_ivic6s = _votifierfw_{  .notifier_call>adcxgb4_ivic6s = _			dler
nt i*  Retrieves IPv6os = 0; esfw_li a roo_i *dev, (bo+ ,evl		)nassociat	dfwithtat a physic.l  *dev,stat Tt.  hysic.l  *dev,
 0fehenc	 iso_pededentid,ndype oactul CLIP comm		dstatic int del_fiupdadae *d_clip( sge_etvice *dev, sroo__truct ethtoovice *dev, int truct port_iivic6_
	s *iap(>adNcmd->t ethtooivic6_ifs =  *ifs;i;
	co t4n_g0;ci;
ap(>ad__in6_ *d_tti(roo__tru);et_t4(!iap()_ra FW_POR 0;uc
	eg(apump(_bh(&iap(teump();etuit]_ = _eachm_pory(ifs  &iap(tes = _uit], if_uit]reg_rate4n_gcxgb4_clip_tti truc_raat(struct sge_etin6_s =  *)ifs->s = .h6_s = )uc 	_t4(r_pn<>et
n		break; =}
	eg(apunump(_bh(&iap(teump();ec  0;
}

 0;uctit void gl_fiupdadaeroo__tru_clip( sge_etvice *dev, snt truct port_ivice *dev, sroo__tru>adNcmd->tp;
	0,o t4n_g0;ci;/*0First popul.
eane otealo_pro *dev,'s IPv6os = 0; esf* (c t4n_gupdadae *d_clip(tructtru);et_t4(r_pcetd FW_POR 0;uc
	/*0Parseaall>bo+ >ap ovl		s *dev, >lay, e o= entp  port.  hysic.l  *df* (c oo__tru>ad2adap(emasdap_ud er_ *d_tti_rcu tru);et_t4(roo__tru)eg_rate4n_gupdadae *d_clip(roo__tructdev) (t _t4(r_pcetdm 0;
}

 0;uc	}
et = 0;0 	if  ->nqVLAN_N_VID  -++)eg_ratoo__tru>ad__vl		efpid_tru_	iep_rcu tru,
htons(ETH_P_8021Q)->i);
	t_t4(!roo__tru)M	= cond8;uR;e>arte4n_gupdadae *d_clip(roo__tructdev) (t _t4(r_pcetdmbreak; =}cn 0;
}

 0;uctit void get_sgupdadaeclip(struct sge_etr *ap, voidapnt i;
;
	0uct port_ivice *dev, stru;i;
	co t4 (t  cu_eg(apump( );eet = 0;0 	if  ->nqMAX_NAP_AS  -++)eg_rad*d>adfn, ne.*/
[i] (arte4n_g0 ((cmd->atru)M	= te4n_gupdadaeroo__tru_clip(	stati(!	_t4(r_pn<>et
n		break; =}
	ecu_eg(apunump( )uc}
# + 8f /*0IS_)
		reD(CONFIGNAPV6)f* (letetur	cxgb_ud -eed val;te oa *ap, tat	@a *a:tr *ap, vbe8;
	ed valdturtat	Call	dfw-cesrk Rqirst port	isoed vald, rtisofunct  "r er =rms thetat	act  "s4ne ",
ary ntimak o		>a *ap, vopnsat  "al,o_u hias compled8;
tat	ek Rinitializat  "  poHW modulhs,bap oed va8;
	tscstrupt
stattat	Must b. call	dfwith rt ottnl ump( heldstatic void gl_ficxgb_uda adapter *ap, voidapnt i;
	co		err
			en_gstxuppgam(of thsaidapn;i	_t4(		ecetdgontioud;et		en_gstxupprssaidapn;i	_t4(		ecetdgontifreeq->i;
t4(r->fs > FULL_IUSING_MSIX)eg_raname_msix_vecsaidapn;i				en_gted_fc _irq(r->fs msix_pi =[0].vec,gt4_no+ tat_
	cr->_d
  p->>r->fs msix_pi =[0]. *sc->fn, ) (t _t4(		ecetddgontiirq_		err
				en_gted_fc _msix_qf th_irqs(fn, ) (t _t4(		ecap = 	free_irq(r->fs msix_pi =[0].vec,gfn, ) (t dgontiirq_		errt	}>a
		cmdap = e	en_gted_fc _irq(r->fs pap(teirq,gt4_
	cr_			dlertfn, )d
  p->>(r->fs > FULL_IUSING_MSI) ? 0 : IRQF_SHAREDd
  p->>r->fs .*/
[0]->nome->fn, ) (t _t4(		ecetddgontiirq_		err	}
etmutex_ump( >uld_mutex);etd(dev)_rx(fn, ) (tk_suam(p->ad(fn, ) (tk_s
	cr_d(dev)(fn, ) (tr->fs > FULL|=IF   _ONE)
DONE->amutex_unump( >uld_mutex);ei	notifyruldsaidap->CXGB4_STATE_UP)uc#_t4IS_)
		reD(CONFIGNAPV6)c	updadaeclip(fn, ) (# + 8f
 oud:cm 0;
}

		erriirq_		e:
hPAv*		e(fn, ne.tru_	if, "ted_fc _irq failed,
stru%d\n"s
str) (ifreeq:(tk_sfree_uam( 0;ourchsaidapn;i	gontioud;etit void get_sgcxgb_downa adapter *ap, voidaptertructk_s
	cr_dis val(idaptert; =canc	l_wque_ ync(&fn, dapneida_ 0tiase_tah()uc canc	l_wque_ ync(&fn, dapnedbnfull_tah()uctcanc	l_wque_ ync(&fn, dapnedbndrop_tah()uc	fn, dapneida_ 0tiase_tah(_busyfw_facmd	iffn, dapneida_ 0tiase_hg(aap-Ncmd->i;
t4(fn, dapne> FULL_IUSING_MSIX)eg_rafree_msix_qf th_irqs(fn, tert; =	free_irq(r->fdapnemsix_pi =[0].vec,gfn, tert; =
		cmd->pfree_irq(r->fdapnepap(teirq,gfn, tert; =quiesUc_rx(fn, tert; =k_suam(p-op(fn, tert; =k_sfree_uam( 0;ourchsaidaptert; =fn, dapne> FULL_= ~F   _ONE)
DONE->Deletturnvice *dev, opnsat  "static void gl_ficxgb_opnn( sge_etvice *dev, snt truct
	co		errt net_demfo *	i = npi>ad2adap(epriv(dev) (t sge_etr *ap, voidapter _pp	->s *ap, ;e
	nadif_carridev)ff(	stati(!_t4(!(fn, dapne> FULL_IF   _ONE)
DONE))ap = e	en_gcxgb_udafn, tert; =	_t4(		en<>et
n		 0;
}

		err	}
ete	en_ga8;k(p->ad(tru);et_t4(!		ecetdnadif_tx(p->ad_all_of thsadstatic 0;
}

		errtit void gl_ficxgb_t_tim( sge_etvice *dev, snt truct port_imfo *	i = npi>ad2adap(epriv(dev) (t sge_etr *ap, voidapter _pp	->s *ap, ;e
	nadif_tx(p-op_all_of thsadstaticnadif_carridev)ff(	statit 0;
}

tard(dev)_viafn, ter,gfn, ternefn,pp	->val)izfacmdizfacmd (!Dei*  Ret
}

a

str= 0nuo idiifork Rindicat	dffslt	sapsn't weg( val;..static void gl_fiweg( val_fslt	s( sge_etfslt	s__pory sfnt i;
t4(fteump(edturn FW_PORL;PERM;i;
t4(ftepe+ 8;
turn FW_PORL;BUSY;ecn 0;
}

static*  D0ti
eane ofslt	saha pe ospecifiedRindexe(if	valid).  Tt. shecks  = 0all
 ofpe ocomm "r rovalmsfwith do8;
	neisolikeane ofslt	sabe8;
	ump(ed, curhen;ly
 ofpe+ 8;
o_f	fnope r opnsat  "s
stcstatic void gl_fid0ti
e_fslt	s( sge_etr *ap, voidapter->ed int cap;
	f	sk	ruct sge_etfslt	s__pory sf;i;
	co t4 (t _t4(qssk-{
	fn, dapneidas.nfidas>+	fn, dapneidas.nsfidas)urn FW_PORL;
	t4_reet fw_&fn, dapneidas.fida_ede[f	sk] ( te4n_gweg( val_fslt	s(f);et_t4(r_pcetd FW_POR 0;uc;
t4(ftevalid)etd FW_PORdel_fslt	s_wrafn, ter,gf	sk	 Mc  0;
}

/* Delosescxgb4_ceg( |_aer
	s_fslt	s(struct adaptevice *dev,
			   ed int speed_tida-st _sbe= bsip, _sbe16fs */
, _sbe16fvl		    ed int cap;
	qf th, ed int cachar  */
, ed int cachar mask)t i;
	co t4;ct sge_etfslt	s__pory sf;i; adapter *adap = netuctp;
	0uc	u8 *valuc(!a = >ad2adap(dev);
	stati(!*  Adjust tidanto_sorrectnfslt	sapndexe* (ctidan-adfn, neidas.sfida_b_Fu;(ctidan+adfn, neidas.nfidasti(!*  Checkontimak otereO w ofslt	sated_fc |=nisoweg( val;..st	if (c fw_&fn, neidas.fida_ede[tida] ( te4n_gweg( val_fslt	s(f);et_t4(r_pcetd FW_POR 0;uc(!*  Ctiarnoudeanyooldet_aourchsabe8;
	us tobyane ofslt	sabeforht	iffwe>p->adqstrucdapt8;
	ne onewofslt	sst	if (capteftevalid)etdctiar_fslt	s(idap->f)uc(!*  Ctiarnoudefslt	saspecificat  "sif (c(buf, 0&ftefsizeof(*s));
 sge_etch_fslt	s_specificat  "	r;i	ftefs.val.l 0;
	}
cpups(sbe16(s */
r;i	ftefs.mask.l 0;
		}
~0;ctval	e-(u8 *)&tipticd->a(val[0] |	val[1] |	val[2] |	val[3])-!=vetap =  = 0;0 	if  ->nq4  -++)eg_ra	ftefs.val.lip[i] w_val[i] (ar	ftefs.mask.lip[i] w_~0;ct }urn_t4(r->fs  chip))tp.vl		epri_metL_IF_AP_A)eg_ra	ftefs.val.i 0;
	}
.*/
ticr	ftefs.mask.i 0;
	}
mask; =t}ur}(
n_t4(r->fs  chip))tp.vl		epri_metL_IF_AROTOCOLtap =  tefs.val. roton_gIPAROTO_TCP;
r	ftefs.mask. roton_g~0uc	}
et tefs.dirsteer _p1;i	ftefs.iqfw_qf th;ct*  Markofslt	sahs	ump(edif (c teump(ed _p1;i	ftefs.rpttid _p1;i( te4n_gusxqfslt	s_wrafn, ->sida);et_t4(r_pc->et=ctiar_fslt	s(idap->f)ucdm 0;
}

 0;uc	}
et 0;
}

0uctiEXAP_ANSYMBOL(cxgb4_ceg( |_aer
	s_fslt	s) (tosescxgb4_removh_aer
	s_fslt	s(struct adaptevice *dev,
			   ed int speed_tida-st ed int cap;
	qf th, bool ipv6)ruct
	co t4;ct sge_etfslt	s__pory sf;i; adapter *adap = netuc(!a = >ad2adap(dev);
	stati(!*  Adjust tidanto_sorrectnfslt	sapndexe* (ctidan-adfn, neidas.sfida_b_Fu;(ctidan+adfn, neidas.nfidasti(! fw_&fn, neidas.fida_ede[tida] ( *  Unump(ane ofslt	saf (c teump(ed _p0;i( te4n_gd0ti
e_fslt	s(fn, ->sida);et_t4(r_pc_ra FW_POR 0;uc
	eg;
}

0uctiEXAP_ANSYMBOL(cxgb4_removh_aer
	s_fslt	s);ic void g sge_etttnl_a8;k(p->ts64 scxgb_tti_gpnts( sge_etvice *dev,
			      p-t sge_etttnl_a8;k(p->ts64 snstruct port_imfo *gpnts gpntsrrt net_demfo *	i = np>ad2adap(epriv(dev) (t sge_etr *ap, voidapter _pp->s *ap, ;e
	*  Bump(aretriev8;
	 voidsidcs dur8;
	EEH
str= 
Y ofreco
	sy. Ope rwise,ane oteco
	sy might failt	iffip one oPCI  *dev,
willeb otemovhdr ermanen;ly
	if (c pin_ump( >sge.dapnegpnts_ump();et_t4(!2adif_ *dev,_pt_aent(tru)s->ethspin_unump( >sge.dapnegpnts_ump();ett -EBUSYv) ur}
-t4_gs(smfo *gpntsafn, ter,gporn, (str, &gpnts	 Mhspin_unump( >sge.dapnegpnts_ump();eicnsorn, by(as 		}
gpnts.tx_oct+i) urnsorn, pap(e
s>adgpnts.tx_frucn) urnsorr, by(as 		}
gpnts.rx_oct+i) urnsorr, pap(e
s>adgpnts.rx_frucn) urnsormulidcat] >adgpnts.rx_mcat]_frucn) u
	*  detailed rx_str= sif (cnsorr, length_str= siadgpnts.rx_jab idi+dgpnts.rx_too_long +
	st _   _ gpnts.rx_runt->cnsorr, o
	s_str= si n_g0;ctnsorr, crc_str= si n>adgpnts.rx_fcs_		err	nsorr, frucn_str= si adgpnts.rx_symbol_		err	nsorr, fifonstr= si n_ggpnts.rx_ovflow0i+dgpnts.rx_ovflow1 +
	st _   _ gpnts.rx_ovflow2i+dgpnts.rx_ovflow3 +
	st _   _ gpnts.rx_sgenc0i+dgpnts.rx_sgenc1 +
	st _   _ gpnts.rx_sgenc2i+dgpnts.rx_sgenc3rr	nsorr, miss t_str= siad0 u
	*  detailed tx_str= sif (cnsortx_abfo  t_str= si n_g0;ctnsorn, (arridevstr= si n_g0;ctnsorn, fifonstr= si ni n_g0;ctnsorn, hiartbg( _str= siad0 utnsorn, wpndow_str= si n>ad0;eicnsorn, str= siadgpnts.n, str= _frucn) urnsorr, str= siadgpnts.rx_symbol_		ei+dgpnts.rx_fcs_		e +
	snsorr, length_str= si+dgpnts.rx_len_		ei+dnsorr, fifonstr= stit 0;
}

v) utit void gl_ficxgb_ioctl( sge_etvice *dev,
			  t sge_etif 0qaang ->l_fi
	ift i;ed int cap;
	pbox;i;
	co t4 in_deprtadctdevadrrt net_demfo *	i = npi>ad2adap(epriv(dev) (t sge_etmii_ioctl_stata*{
	i p-( sge_etmii_ioctl_stata*)& 0q->if _stat>rst wi0, m(
	if->etcaseOSIOCGMIIPHY:
cmd->ap	ormdio_s =  <>et
n		 0;
}

-EOPNOTSUPP;
r	stats  hy_id _pp	ormdio_s = ;
r	break; =caseOSIOCGMIIREG: =caseOSIOCSMIIREG: =captemdio_ hy_id_is_c45(stats  hy_id	reg_raaprtad	}
mdio_ hy_id_prtad(stats  hy_id	ticr	devad	}
mdio_ hy_id_devad(stats  hy_id	ticr}{
		ca_t4(stats  hy_id | 32) g_raaprtad	}
stats  hy_idticr	devad	}
sta	 	stats ieg_nums&= 0x1fticr}{
		c
n		 0;
}

-E
	t4_reet	pbox _pp	->s *ap, nefnuct=_t4(cmde== SIOCGMIIREG)M	= te4n_gk_spdio_rd(p	->s *ap, ,	pboxdeprtadctdevadd    p->stats ieg_num, &stats val_oudn;i					c
n		 0;n_gk_spdio_wrap	->s *ap, ,	pboxdeprtadctdevadd    p->stats ieg_num, stats val_in);
r	break; =PAfauli:
n	 0;
}

-EOPNOTSUPP;
r}cn 0;
}

 0;uctit void get_sgcxgb_usxqixmod)( sge_etvice *dev, snt truct*  ui =rtun( |lyfwe>can't  0;
}

		e= sipo pe o vop( s (c sxqixmod)(		  t-1izfacmd (!Dei void gl_ficxgb_t			ge_mtu( adaptevice *dev,
			   p;
	new_mtutuuct
	co t4;ct sge_etmfo *	i = npi>ad2adap(epriv(dev) (i	s_t42aw_mtu | 81 ||	new_mtu >qMAX_MTU)         *  accomm dada SACKu* (cm FW_PORL;
	t4_res 0;n_gk_s sxqixmod)(p	->s *ap, ,	p	->s *ap, nefn,pp	->val)iznew_mtu t-1i
	st _  -1iz-1iz-1izeruu);et_t4(!r_pc_raap(temtuA=	new_mtu;c  0;
}

 0;uctit void gl_ficxgbs sxqmac_s = ( adaptevice *dev,
			   et_sg*ptuuct
	co t4;ct sge_etsmp(s =  *a =  =ip;ct sge_etmfo *	i = npi>ad2adap(epriv(dev) (i	s_t4!is_validm__r	s_a = (a = nega_stat))urn FW_PORL;ADDRNOTAVAILuc(! t4n_gk_st			ge_mac(p	->s *ap, ,	p	->s *ap, nefn,pp	->val)i
	st _  p	->xac]_s =s_fslt,ba = nega_statizeruuizeruu);et_t4(r_pn<>et
n	 FW_POR 0;uc
	memcpy(ap(tedev_a = ,fa = nega_statizap(tes = _uen);
rp	->xac]_s =s_fsltn_gtet;c  0;
}

/* Del#ifPAf CONFIGNNET_POLL_CONTROLLERt void get_sgcxgb_2adpoll( sge_etvice *dev,
			 truct port_imfo *	i = npi>ad2adap(epriv(dev) (t sge_etr *ap, voidap _pp	->s *ap, ;e
	
t4(r->fs > FULL_IUSING_MSIX)eg_rap;
	0uc	t sge_etsam(eth_rxqf*rxfw_&fn, nehrx.__rrxq[p	->qset]; ++i]reet	 = 0;0 	ip	->n ++i) 	0u i--, rx++)_raak_suam(
	cr_msix(_de&rxs ispqt; =
		cmd->pt4_
	cr_			dlertfn, )(0->fn, ) (}
# + 8fit void gstruct adaptevice *dev,_ops cxgb4_2ad *d_ops w_{  .ndo_opnn             =icxgb_opnn,  .ndo_p-op             =icxgb_t_tim,  .ndo_p->ad_xmit       =itardth_xmit,  .ndo_p0ticf_of th     =	cxgbs sticf_of th,  .ndo_tti_gpnts64      =icxgb_tti_gpnts,  .ndo_p0xqix_mod)e     =icxgb_ sxqixmod),  .ndo_p0xqmac_s = ",
t =icxgb_ sxqmac_s = ,  .ndo_p0xqfg( ures     =icxgb_ sxqfg( ures,  .ndo_valid( |_a =     =idth_valid( |_a = ,  .ndo_do_ioctl         =icxgb_ioctl,  .ndo_t			ge_mtu       =icxgb_t			ge_mtu,l#ifPAf CONFIGNNET_POLL_CONTROLLERt .ndo_poll_controll	sr =icxgb_2adpoll,
# + 8fi};ecet_sgt4_ftatl_		ea adapter *ap, voidapnt i;k_susxqieg_field adap->AT_MCONTROL, GLOBAL)
		re  0);i	k_s
	cr_dis val(idap);
hPAv*al	st(fn, ne.tru_	if, "encount, e oftatl
		e= ,gfn, ter p-opped\n"f;itii*  Ret
}

pe ospecifiedRPCI-E Configurat  " Space teg sdap	w_li ouroPhysic.l
 ofFunct  ".  Wefory qirst via a0Firmwaeg LDST Comm		d>since4we>p 0fehipo let
 ofpe ofirmwaeg owf	fllr port.se teg sdaps, butiiforkat fails4we>go  = 0it
 ofdirectlyrour stvesstatic void g = bk_seg(appcie_cfg4t adapter *ap, voidap->l_fiteg	ruct sge_etfw_ldt];cmdeldt];cmd;ct = bvaluc;
	co t4 (t *  Ctrucdaptbap oa,ndype oFirmwaeg LDST Comm		d>po retrieve pe 
Y ofspecifiedRPCI-E Configurat  " Space teg sdapst	if (c(buf, 0&ldt];cmdizeof(*s));
ldt];cmd	);etldt];cmd.op_to_s = space =
  htonl(FW_CMD_OP(FW_LDST_CMD(CHELS_   _ FW_CMD_REQUEST HELS_   _ FW_CMD_READ HELS_   _ FW_LDST_CMD_ADDRSPACE(FW_LDST_ADDRSPC_FUNC_PCIE	);etldt];cmd.cy