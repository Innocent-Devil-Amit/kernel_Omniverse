/*
 *   Copyright (C) International Business Machines Corp., 2000-2005
 *   Portions Copyright (C) Christoph Hellwig, 2001-2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/*
 *	jfs_txnmgr.c: transaction manager
 *
 * notes:
 * transaction starts with txBegin() and ends with txCommit()
 * or txAbort().
 *
 * tlock is acquired at the time of update;
 * (obviate scan at commit time for xtree and dtree)
 * tlock and mp points to each other;
 * (no hashlist for mp -> tlock).
 *
 * special cases:
 * tlock on in-memory inode:
 * in-place tlock in the in-memory inode itself;
 * converted to page lock by iWrite() at commit time.
 *
 * tlock during write()/mmap() under anonymous transaction (tid = 0):
 * transferred (?) to transaction at commit time.
 *
 * use the page itself to update allocation maps
 * (obviate intermediate replication of allocation/deallocation data)
 * hold on to mp+lock thru update of maps
 */

#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/completion.h>
#include <linux/freezer.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kthread.h>
#include <linux/seq_file.h>
#include "jfs_incore.h"
#include "jfs_inode.h"
#include "jfs_filsys.h"
#include "jfs_metapage.h"
#include "jfs_dinode.h"
#include "jfs_imap.h"
#include "jfs_dmap.h"
#include "jfs_superblock.h"
#include "jfs_debug.h"

/*
 *	transaction management structures
 */
static struct {
	int freetid;		/* index of a free tid structure */
	int freelock;		/* index first free lock word */
	wait_queue_head_t freewait;	/* eventlist of free tblock */
	wait_queue_head_t freelockwait;	/* eventlist of free tlock */
	wait_queue_head_t lowlockwait;	/* eventlist of ample tlocks */
	int tlocksInUse;	/* Number of tlocks in use */
	spinlock_t LazyLock;	/* synchronize sync_queue & unlock_queue */
/*	struct tblock *sync_queue; * Transactions waiting for data sync */
	struct list_head unlock_queue;	/* Txns waiting to be released */
	struct list_head anon_list;	/* inodes having anonymous txns */
	struct list_head anon_list2;	/* inodes having anonymous txns
					   that couldn't be sync'ed */
} TxAnchor;

int jfs_tlocks_low;		/* Indicates low number of available tlocks */

#ifdef CONFIG_JFS_STATISTICS
static struct {
	uint txBegin;
	uint txBegin_barrier;
	uint txBegin_lockslow;
	uint txBegin_freetid;
	uint txBeginAnon;
	uint txBeginAnon_barrier;
	uint txBeginAnon_lockslow;
	uint txLockAlloc;
	uint txLockAlloc_freelock;
} TxStat;
#endif

static int nTxBlock = -1;	/* number of transaction blocks */
module_param(nTxBlock, int, 0);
MODULE_PARM_DESC(nTxBlock,
		 "Number of transaction blocks (max:65536)");

static int nTxLock = -1;	/* number of transaction locks */
module_param(nTxLock, int, 0);
MODULE_PARM_DESC(nTxLock,
		 "Number of transaction locks (max:65536)");

struct tblock *TxBlock;	/* transaction block table */
static int TxLockLWM;	/* Low water mark for number of txLocks used */
static int TxLockHWM;	/* High water mark for number of txLocks used */
static int TxLockVHWM;	/* Very High water mark */
struct tlock *TxLock;	/* transaction lock table */

/*
 *	transaction management lock
 */
static DEFINE_SPINLOCK(jfsTxnLock);

#define TXN_LOCK()		spin_lock(&jfsTxnLock)
#define TXN_UNLOCK()		spin_unlock(&jfsTxnLock)

#define LAZY_LOCK_INIT()	spin_lock_init(&TxAnchor.LazyLock);
#define LAZY_LOCK(flags)	spin_lock_irqsave(&TxAnchor.LazyLock, flags)
#define LAZY_UNLOCK(flags) spin_unlock_irqrestore(&TxAnchor.LazyLock, flags)

static DECLARE_WAIT_QUEUE_HEAD(jfs_commit_thread_wait);
static int jfs_commit_thread_waking;

/*
 * Retry logic exist outside these macros to protect from spurrious wakeups.
 */
static inline void TXN_SLEEP_DROP_LOCK(wait_queue_head_t * event)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(event, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);
	TXN_UNLOCK();
	io_schedule();
	remove_wait_queue(event, &wait);
}

#define TXN_SLEEP(event)\
{\
	TXN_SLEEP_DROP_LOCK(event);\
	TXN_LOCK();\
}

#define TXN_WAKEUP(event) wake_up_all(event)

/*
 *	statistics
 */
static struct {
	tid_t maxtid;		/* 4: biggest tid ever used */
	lid_t maxlid;		/* 4: biggest lid ever used */
	int ntid;		/* 4: # of transactions performed */
	int nlid;		/* 4: # of tlocks acquired */
	int waitlock;		/* 4: # of tlock wait */
} stattx;

/*
 * forward references
 */
static int diLog(struct jfs_log * log, struct tblock * tblk, struct lrd * lrd,
		struct tlock * tlck, struct commit * cd);
static int dataLog(struct jfs_log * log, struct tblock * tblk, struct lrd * lrd,
		struct tlock * tlck);
static void dtLog(struct jfs_log * log, struct tblock * tblk, struct lrd * lrd,
		struct tlock * tlck);
static void mapLog(struct jfs_log * log, struct tblock * tblk, struct lrd * lrd,
		struct tlock * tlck);
static void txAllocPMap(struct inode *ip, struct maplock * maplock,
		struct tblock * tblk);
static void txForce(struct tblock * tblk);
static int txLog(struct jfs_log * log, struct tblock * tblk,
		struct commit * cd);
static void txUpdateMap(struct tblock * tblk);
static void txRelease(struct tblock * tblk);
static void xtLog(struct jfs_log * log, struct tblock * tblk, struct lrd * lrd,
	   struct tlock * tlck);
static void LogSyncRelease(struct metapage * mp);

/*
 *		transaction block/lock management
 *		---------------------------------
 */

/*
 * Get a transaction lock from the free list.  If the number in use is
 * greater than the high water mark, wake up the sync daemon.  This should
 * free some anonymous transaction locks.  (TXN_LOCK must be held.)
 */
static lid_t txLockAlloc(void)
{
	lid_t lid;

	INCREMENT(TxStat.txLockAlloc);
	if (!TxAnchor.freelock) {
		INCREMENT(TxStat.txLockAlloc_freelock);
	}

	while (!(lid = TxAnchor.freelock))
		TXN_SLEEP(&TxAnchor.freelockwait);
	TxAnchor.freelock = TxLock[lid].next;
	HIGHWATERMARK(stattx.maxlid, lid);
	if ((++TxAnchor.tlocksInUse > TxLockHWM) && (jfs_tlocks_low == 0)) {
		jfs_info("txLockAlloc tlocks low");
		jfs_tlocks_low = 1;
		wake_up_process(jfsSyncThread);
	}

	return lid;
}

static void txLockFree(lid_t lid)
{
	TxLock[lid].tid = 0;
	TxLock[lid].next = TxAnchor.freelock;
	TxAnchor.freelock = lid;
	TxAnchor.tlocksInUse--;
	if (jfs_tlocks_low && (TxAnchor.tlocksInUse < TxLockLWM)) {
		jfs_info("txLockFree jfs_tlocks_low no more");
		jfs_tlocks_low = 0;
		TXN_WAKEUP(&TxAnchor.lowlockwait);
	}
	TXN_WAKEUP(&TxAnchor.freelockwait);
}

/*
 * NAME:	txInit()
 *
 * FUNCTION:	initialize transaction management structures
 *
 * RETURN:
 *
 * serialization: single thread at jfs_init()
 */
int txInit(void)
{
	int k, size;
	struct sysinfo si;

	/* Set defaults for nTxLock and nTxBlock if unset */

	if (nTxLock == -1) {
		if (nTxBlock == -1) {
			/* Base default on memory size */
			si_meminfo(&si);
			if (si.totalram > (256 * 1024)) /* 1 GB */
				nTxLock = 64 * 1024;
			else
				nTxLock = si.totalram >> 2;
		} else if (nTxBlock > (8 * 1024))
			nTxLock = 64 * 1024;
		else
			nTxLock = nTxBlock << 3;
	}
	if (nTxBlock == -1)
		nTxBlock = nTxLock >> 3;

	/* Verify tunable parameters */
	if (nTxBlock < 16)
		nTxBlock = 16;	/* No one should set it this low */
	if (nTxBlock > 65536)
		nTxBlock = 65536;
	if (nTxLock < 256)
		nTxLock = 256;	/* No one should set it this low */
	if (nTxLock > 65536)
		nTxLock = 65536;

	printk(KERN_INFO "JFS: nTxBlock = %d, nTxLock = %d\n",
	       nTxBlock, nTxLock);
	/*
	 * initialize transaction block (tblock) table
	 *
	 * transaction id (tid) = tblock index
	 * tid = 0 is reserved.
	 */
	TxLockLWM = (nTxLock * 4) / 10;
	TxLockHWM = (nTxLock * 7) / 10;
	TxLockVHWM = (nTxLock * 8) / 10;

	size = sizeof(struct tblock) * nTxBlock;
	TxBlock = vmalloc(size);
	if (TxBlock == NULL)
		return -ENOMEM;

	for (k = 1; k < nTxBlock - 1; k++) {
		TxBlock[k].next = k + 1;
		init_waitqueue_head(&TxBlock[k].gcwait);
		init_waitqueue_head(&TxBlock[k].waitor);
	}
	TxBlock[k].next = 0;
	init_waitqueue_head(&TxBlock[k].gcwait);
	init_waitqueue_head(&TxBlock[k].waitor);

	TxAnchor.freetid = 1;
	init_waitqueue_head(&TxAnchor.freewait);

	stattx.maxtid = 1;	/* statistics */

	/*
	 * initialize transaction lock (tlock) table
	 *
	 * transaction lock id = tlock index
	 * tlock id = 0 is reserved.
	 */
	size = sizeof(struct tlock) * nTxLock;
	TxLock = vmalloc(size);
	if (TxLock == NULL) {
		vfree(TxBlock);
		return -ENOMEM;
	}

	/* initialize tlock table */
	for (k = 1; k < nTxLock - 1; k++)
		TxLock[k].next = k + 1;
	TxLock[k].next = 0;
	init_waitqueue_head(&TxAnchor.freelockwait);
	init_waitqueue_head(&TxAnchor.lowlockwait);

	TxAnchor.freelock = 1;
	TxAnchor.tlocksInUse = 0;
	INIT_LIST_HEAD(&TxAnchor.anon_list);
	INIT_LIST_HEAD(&TxAnchor.anon_list2);

	LAZY_LOCK_INIT();
	INIT_LIST_HEAD(&TxAnchor.unlock_queue);

	stattx.maxlid = 1;	/* statistics */

	return 0;
}

/*
 * NAME:	txExit()
 *
 * FUNCTION:	clean up when module is unloaded
 */
void txExit(void)
{
	vfree(TxLock);
	TxLock = NULL;
	vfree(TxBlock);
	TxBlock = NULL;
}

/*
 * NAME:	txBegin()
 *
 * FUNCTION:	start a transaction.
 *
 * PARAMETER:	sb	- superblock
 *		flag	- force for nested tx;
 *
 * RETURN:	tid	- transaction id
 *
 * note: flag force allows to start tx for nested tx
 * to prevent deadlock on logsync barrier;
 */
tid_t txBegin(struct super_block *sb, int flag)
{
	tid_t t;
	struct tblock *tblk;
	struct jfs_log *log;

	jfs_info("txBegin: flag = 0x%x", flag);
	log = JFS_SBI(sb)->log;

	TXN_LOCK();

	INCREMENT(TxStat.txBegin);

      retry:
	if (!(flag & COMMIT_FORCE)) {
		/*
		 * synchronize with logsync barrier
		 */
		if (test_bit(log_SYNCBARRIER, &log->flag) ||
		    test_bit(log_QUIESCE, &log->flag)) {
			INCREMENT(TxStat.txBegin_barrier);
			TXN_SLEEP(&log->syncwait);
			goto retry;
		}
	}
	if (flag == 0) {
		/*
		 * Don't begin transaction if we're getting starved for tlocks
		 * unless COMMIT_FORCE or COMMIT_INODE (which may ultimately
		 * free tlocks)
		 */
		if (TxAnchor.tlocksInUse > TxLockVHWM) {
			INCREMENT(TxStat.txBegin_lockslow);
			TXN_SLEEP(&TxAnchor.lowlockwait);
			goto retry;
		}
	}

	/*
	 * allocate transaction id/block
	 */
	if ((t = TxAnchor.freetid) == 0) {
		jfs_info("txBegin: waiting for free tid");
		INCREMENT(TxStat.txBegin_freetid);
		TXN_SLEEP(&TxAnchor.freewait);
		goto retry;
	}

	tblk = tid_to_tblock(t);

	if ((tblk->next == 0) && !(flag & COMMIT_FORCE)) {
		/* Don't let a non-forced transaction take the last tblk */
		jfs_info("txBegin: waiting for free tid");
		INCREMENT(TxStat.txBegin_freetid);
		TXN_SLEEP(&TxAnchor.freewait);
		goto retry;
	}

	TxAnchor.freetid = tblk->next;

	/*
	 * initialize transaction
	 */

	/*
	 * We can't zero the whole thing or we screw up another thread being
	 * awakened after sleeping on tblk->waitor
	 *
	 * memset(tblk, 0, sizeof(struct tblock));
	 */
	tblk->next = tblk->last = tblk->xflag = tblk->flag = tblk->lsn = 0;

	tblk->sb = sb;
	++log->logtid;
	tblk->logtid = log->logtid;

	++log->active;

	HIGHWATERMARK(stattx.maxtid, t);	/* statistics */
	INCREMENT(stattx.ntid);	/* statistics */

	TXN_UNLOCK();

	jfs_info("txBegin: returning tid = %d", t);

	return t;
}

/*
 * NAME:	txBeginAnon()
 *
 * FUNCTION:	start an anonymous transaction.
 *		Blocks if logsync or available tlocks are low to prevent
 *		anonymous tlocks from depleting supply.
 *
 * PARAMETER:	sb	- superblock
 *
 * RETURN:	none
 */
void txBeginAnon(struct super_block *sb)
{
	struct jfs_log *log;

	log = JFS_SBI(sb)->log;

	TXN_LOCK();
	INCREMENT(TxStat.txBeginAnon);

      retry:
	/*
	 * synchronize with logsync barrier
	 */
	if (test_bit(log_SYNCBARRIER, &log->flag) ||
	    test_bit(log_QUIESCE, &log->flag)) {
		INCREMENT(TxStat.txBeginAnon_barrier);
		TXN_SLEEP(&log->syncwait);
		goto retry;
	}

	/*
	 * Don't begin transaction if we're getting starved for tlocks
	 */
	if (TxAnchor.tlocksInUse > TxLockVHWM) {
		INCREMENT(TxStat.txBeginAnon_lockslow);
		TXN_SLEEP(&TxAnchor.lowlockwait);
		goto retry;
	}
	TXN_UNLOCK();
}

/*
 *	txEnd()
 *
 * function: free specified transaction block.
 *
 *	logsync barrier processing:
 *
 * serialization:
 */
void txEnd(tid_t tid)
{
	struct tblock *tblk = tid_to_tblock(tid);
	struct jfs_log *log;

	jfs_info("txEnd: tid = %d", tid);
	TXN_LOCK();

	/*
	 * wakeup transactions waiting on the page locked
	 * by the current transaction
	 */
	TXN_WAKEUP(&tblk->waitor);

	log = JFS_SBI(tblk->sb)->log;

	/*
	 * Lazy commit thread can't free this guy until we mark it UNLOCKED,
	 * otherwise, we would be left with a transaction that may have been
	 * reused.
	 *
	 * Lazy commit thread will turn off tblkGC_LAZY before calling this
	 * routine.
	 */
	if (tblk->flag & tblkGC_LAZY) {
		jfs_info("txEnd called w/lazy tid: %d, tblk = 0x%p", tid, tblk);
		TXN_UNLOCK();

		spin_lock_irq(&log->gclock);	// LOGGC_LOCK
		tblk->flag |= tblkGC_UNLOCKED;
		spin_unlock_irq(&log->gclock);	// LOGGC_UNLOCK
		return;
	}

	jfs_info("txEnd: tid: %d, tblk = 0x%p", tid, tblk);

	assert(tblk->next == 0);

	/*
	 * insert tblock back on freelist
	 */
	tblk->next = TxAnchor.freetid;
	TxAnchor.freetid = tid;

	/*
	 * mark the tblock not active
	 */
	if (--log->active == 0) {
		clear_bit(log_FLUSH, &log->flag);

		/*
		 * synchronize with logsync barrier
		 */
		if (test_bit(log_SYNCBARRIER, &log->flag)) {
			TXN_UNLOCK();

			/* write dirty metadata & forward log syncpt */
			jfs_syncpt(log, 1);

			jfs_info("log barrier off: 0x%x", log->lsn);

			/* enable new transactions start */
			clear_bit(log_SYNCBARRIER, &log->flag);

			/* wakeup all waitors for logsync barrier */
			TXN_WAKEUP(&log->syncwait);

			goto wakeup;
		}
	}

	TXN_UNLOCK();
wakeup:
	/*
	 * wakeup all waitors for a free tblock
	 */
	TXN_WAKEUP(&TxAnchor.freewait);
}

/*
 *	txLock()
 *
 * function: acquire a transaction lock on the specified <mp>
 *
 * parameter:
 *
 * return:	transaction lock id
 *
 * serialization:
 */
struct tlock *txLock(tid_t tid, struct inode *ip, struct metapage * mp,
		     int type)
{
	struct jfs_inode_info *jfs_ip = JFS_IP(ip);
	int dir_xtree = 0;
	lid_t lid;
	tid_t xtid;
	struct tlock *tlck;
	struct xtlock *xtlck;
	struct linelock *linelock;
	xtpage_t *p;
	struct tblock *tblk;

	TXN_LOCK();

	if (S_ISDIR(ip->i_mode) && (type & tlckXTREE) &&
	    !(mp->xflag & COMMIT_PAGE)) {
		/*
		 * Directory inode is special.  It can have both an xtree tlock
		 * and a dtree tlock associated with it.
		 */
		dir_xtree = 1;
		lid = jfs_ip->xtlid;
	} else
		lid = mp->lid;

	/* is page not locked by a transaction ? */
	if (lid == 0)
		goto allocateLock;

	jfs_info("txLock: tid:%d ip:0x%p mp:0x%p lid:%d", tid, ip, mp, lid);

	/* is page locked by the requester transaction ? */
	tlck = lid_to_tlock(lid);
	if ((xtid = tlck->tid) == tid) {
		TXN_UNLOCK();
		goto grantLock;
	}

	/*
	 * is page locked by anonymous transaction/lock ?
	 *
	 * (page update without transaction (i.e., file write) is
	 * locked under anonymous transaction tid = 0:
	 * anonymous tlocks maintained on anonymous tlock list of
	 * the inode of the page and available to all anonymous
	 * transactions until txCommit() time at which point
	 * they are transferred to the transaction tlock list of
	 * the committing transaction of the inode)
	 */
	if (xtid == 0) {
		tlck->tid = tid;
		TXN_UNLOCK();
		tblk = tid_to_tblock(tid);
		/*
		 * The order of the tlocks in the transaction is important
		 * (during truncate, child xtree pages must be freed before
		 * parent's tlocks change the working map).
		 * Take tlock off anonymous list and add to tail of
		 * transaction list
		 *
		 * Note:  We really need to get rid of the tid & lid and
		 * use list_head's.  This code is getting UGLY!
		 */
		if (jfs_ip->atlhead == lid) {
			if (jfs_ip->atltail == lid) {
				/* only anonymous txn.
				 * Remove from anon_list
				 */
				TXN_LOCK();
				list_del_init(&jfs_ip->anon_inode_list);
				TXN_UNLOCK();
			}
			jfs_ip->atlhead = tlck->next;
		} else {
			lid_t last;
			for (last = jfs_ip->atlhead;
			     lid_to_tlock(last)->next != lid;
			     last = lid_to_tlock(last)->next) {
				assert(last);
			}
			lid_to_tlock(last)->next = tlck->next;
			if (jfs_ip->atltail == lid)
				jfs_ip->atltail = last;
		}

		/* insert the tlock at tail of transaction tlock list */

		if (tblk->next)
			lid_to_tlock(tblk->last)->next = lid;
		else
			tblk->next = lid;
		tlck->next = 0;
		tblk->last = lid;

		goto grantLock;
	}

	goto waitLock;

	/*
	 * allocate a tlock
	 */
      allocateLock:
	lid = txLockAlloc();
	tlck = lid_to_tlock(lid);

	/*
	 * initialize tlock
	 */
	tlck->tid = tid;

	TXN_UNLOCK();

	/* mark tlock for meta-data page */
	if (mp->xflag & COMMIT_PAGE) {

		tlck->flag = tlckPAGELOCK;

		/* mark the page dirty and nohomeok */
		metapage_nohomeok(mp);

		jfs_info("locking mp = 0x%p, nohomeok = %d tid = %d tlck = 0x%p",
			 mp, mp->nohomeok, tid, tlck);

		/* if anonymous transaction, and buffer is on the group
		 * commit synclist, mark inode to show this.  This will
		 * prevent the buffer from being marked nohomeok for too
		 * long a time.
		 */
		if ((tid == 0) && mp->lsn)
			set_cflag(COMMIT_Synclist, ip);
	}
	/* mark tlock for in-memory inode */
	else
		tlck->flag = tlckINODELOCK;

	if (S_ISDIR(ip->i_mode))
		tlck->flag |= tlckDIRECTORY;

	tlck->type = 0;

	/* bind the tlock and the page */
	tlck->ip = ip;
	tlck->mp = mp;
	if (dir_xtree)
		jfs_ip->xtlid = lid;
	else
		mp->lid = lid;

	/*
	 * enqueue transaction lock to transaction/inode
	 */
	/* insert the tlock at tail of transaction tlock list */
	if (tid) {
		tblk = tid_to_tblock(tid);
		if (tblk->next)
			lid_to_tlock(tblk->last)->next = lid;
		else
			tblk->next = lid;
		tlck->next = 0;
		tblk->last = lid;
	}
	/* anonymous transaction:
	 * insert the tlock at head of inode anonymous tlock list
	 */
	else {
		tlck->next = jfs_ip->atlhead;
		jfs_ip->atlhead = lid;
		if (tlck->next == 0) {
			/* This inode's first anonymous transaction */
			jfs_ip->atltail = lid;
			TXN_LOCK();
			list_add_tail(&jfs_ip->anon_inode_list,
				      &TxAnchor.anon_list);
			TXN_UNLOCK();
		}
	}

	/* initialize type dependent area for linelock */
	linelock = (struct linelock *) & tlck->lock;
	linelock->next = 0;
	linelock->flag = tlckLINELOCK;
	linelock->maxcnt = TLOCKSHORT;
	linelock->index = 0;

	switch (type & tlckTYPE) {
	case tlckDTREE:
		linelock->l2linesize = L2DTSLOTSIZE;
		break;

	case tlckXTREE:
		linelock->l2linesize = L2XTSLOTSIZE;

		xtlck = (struct xtlock *) linelock;
		xtlck->header.offset = 0;
		xtlck->header.length = 2;

		if (type & tlckNEW) {
			xtlck->lwm.offset = XTENTRYSTART;
		} else {
			if (mp->xflag & COMMIT_PAGE)
				p = (xtpage_t *) mp->data;
			else
				p = &jfs_ip->i_xtroot;
			xtlck->lwm.offset =
			    le16_to_cpu(p->header.nextindex);
		}
		xtlck->lwm.length = 0;	/* ! */
		xtlck->twm.offset = 0;
		xtlck->hwm.offset = 0;

		xtlck->index = 2;
		break;

	case tlckINODE:
		linelock->l2linesize = L2INODESLOTSIZE;
		break;

	case tlckDATA:
		linelock->l2linesize = L2DATASLOTSIZE;
		break;

	default:
		jfs_err("UFO tlock:0x%p", tlck);
	}

	/*
	 * update tlock vector
	 */
      grantLock:
	tlck->type |= type;

	return tlck;

	/*
	 * page is being locked by another transaction:
	 */
      waitLock:
	/* Only locks on ipimap or ipaimap should reach here */
	/* assert(jfs_ip->fileset == AGGREGATE_I); */
	if (jfs_ip->fileset != AGGREGATE_I) {
		printk(KERN_ERR "txLock: trying to lock locked page!");
		print_hex_dump(KERN_ERR, "ip: ", DUMP_PREFIX_ADDRESS, 16, 4,
			       ip, sizeof(*ip), 0);
		print_hex_dump(KERN_ERR, "mp: ", DUMP_PREFIX_ADDRESS, 16, 4,
			       mp, sizeof(*mp), 0);
		print_hex_dump(KERN_ERR, "Locker's tblock: ",
			       DUMP_PREFIX_ADDRESS, 16, 4, tid_to_tblock(tid),
			       sizeof(struct tblock), 0);
		print_hex_dump(KERN_ERR, "Tlock: ", DUMP_PREFIX_ADDRESS, 16, 4,
			       tlck, sizeof(*tlck), 0);
		BUG();
	}
	INCREMENT(stattx.waitlock);	/* statistics */
	TXN_UNLOCK();
	release_metapage(mp);
	TXN_LOCK();
	xtid = tlck->tid;	/* reacquire after dropping TXN_LOCK */

	jfs_info("txLock: in waitLock, tid = %d, xtid = %d, lid = %d",
		 tid, xtid, lid);

	/* Recheck everything since dropping TXN_LOCK */
echeck eve("txLock: in waitLock,tid;
		TXNmp->xjfs_ip->xxtli!(mp->transactit;
		}wait);
	TxAnc\
}

#defin&zeof(struct tb		TX)>sb)->log;

			xtlcmp);
	TXN_LOCK(id = %d, xtid = %d>waitor
	UG();, lid);

	/* Rechec ng since droping loc*
 * FUNCTION:	start a /*
 *		tmous transaction.
/*
 *		ok for s_xtree = 1;
		lidst be held.)
 */
,out edgettting /
	el
		 */
yetffer tx
 * to itLock:
	/* Only o pref the GNUFO  for s,out ewn take thtLom go prediskpoint
	how thirecordting sctu& lidgetxCoritteock
 *		flag	- forcgements t	-eginAnon(structErrWAKEUc inlubtblkGC_EEP_DROP_LOCK(blk);
static void xtLog(struct jfs_lp = JFS_IP({
	struct lid =tlock *tlck;ck *xtlck;
	struct li_mode) && (type     llize transaction *tlc
	/* Reode's firsOCK();
		t = tlck->tid) == tid) {et_cflp->xtxjfs_ip-)_ERR*
 *GE)) 
		BU(k;

	/*
	 *t = XTBTROOTaiting for fock(last (xtpage_t *) mp->data;
le16_transactio0pendent area  on the page locked
	 * by the curreatransaction
	 */
	TXN_WAKEUP(&tblk->waitor);

	log = JFS_SBI(tblk->sb)->log;

	*
 * function: free sp	start a U_LOCK_mous transaction.
Ir links *ranse) i*/
parent' thei? */
	tieful,jo lop", tgemenobjk->s
	tlcs chsXN_Wiractioait;EEP_DROP_LOCK(blk);
sU_LOCK_id xtLog(struct jfs_lp = JFS_IP(ck;
	struct linelock ;
	struct tblock *tbltlock *tl, firssin*tl, t linelock {
	struct lid ="txEnd: tid = %d", ti	tid_t ffssit ffid =unsigor
	 0) &E_HEA0x%p mp:0x%p lidU_LOCK:ert(tblk->next =ck_irq(/*
	 * Lazy commit thread can't free th/
	el(i.e.,l Publtruct
		 */
(its, 1);hasommitCoritteo)locks oe     llize transaction *tlc
	/* RefirsOCK();
		t = tlck->tid) == tid) {e->atltail == lid)
	nohomeok = %dGC_UNLn:
	 , lid);

ok, tid, tlck *tl, ction, and buuntlck-(i.e.er in lock *) &et_cflp->xtxjfs_ip-)_ERR*
 *GE)) 
		BU(k;

	/*
	 *t = XTBTROOTaiting for fock(last (xtpage_t *) mp->data;
leous traf map  for 
del_init(&f mad = tlck->tid;	r fock(last (xte.
		 */
>tx.wait	d = tlck-_ mp = 0x%p, nohea forLoci.
 *
nger/larger c->loinit(&LOG wake) && er ofE_HEAD
				jfs_ (xtc->ld_to_tloer t ff(t ffssitransac->lransaon_inoder t ff(t ff/* if ac->lransaon_inod lid;
ffs
>tt ff/
			el	if ac->lze transac->lxt = t 			xtlck-if ac->lze transac->lxt = LOG wake functier ofE_HEAD
	r fock(last!(lck->type =t = XTFXTSde is 
	r foputd = tlck->tid;	/l of transaction l%d",
	tlctblock *(s)n is importanuffer y,tid == list
	 */
chor.freetl_init(mode) && (type	l , lid(;
	linelock->next = 0;
	linelock)= lid)
			))
		TXl.
				 * R tlck->lock;
	linelock->next = tlck->tid) ==  tid) {e	t = tlg = tlckLINE) {e	lid].tid = 0 tid) {e	l , lidk* ! */
	lid].tid = 0;
(type	mp);
	TXN_LOCK(}->xflag = tblk->flag = tblk-0n't free th
}

#dog(strucer in(&log-> jfs_ip- (rmediate repli
parentorLoci.r
	 s= 0) {t(tbcode p- hasommitCsactioseful,(&log->snsactitk);
static vo)>tid = tid;
	++log->l			 * LOG wake) && er ofE_HEAD
			
		}
c*
ntchor.de_list);(tblk->sog-> jfsD
			LOG wake functier ofE_HEAD
	(}-: acquire aM
statinsaction lock on theateLock:
	lst.  If the numbet.txBegk-(i.e/(&txEndireet.txBegk-(i.e,ock * tbli	/* Verasoder.of/dnTxLocke is id, struct inode *ipM
statinuct metapage * mp,
		     intfo *jfs_ip = JFS_IP(ip);
	int dir_xtree = 0;
	lid_t lid;
	tlock *tlck;ck *xtlcg;

	jfs_info("txBegick;
	struct linelock ck * tblkck * tb li_mode) && (type   allocateLock:
	lid = txLocd_to_tlock(lid);

	/*
	 * initialize tlock
	 */
	tlck->tid = tid;

	TXN_UNLOCK();

	/* mark tlock fo/
	tlck->ip = ip;
	tlck->mobjk->CK();

	/* DIR(ip->i_mode))
		tlc->flag |= tlckDIRECTORY;

	tlck->type = 0;

	/* bind the 	if (dir_xtree)
		jfs_ip->xt*
 * Ftlock and the pge is beto transaction/inode
	 */
	/* insert the tlock at tail of transaction tlock list */
	if (tid) {
		tblk = tid_to_tblock(tid);
		if (tblk->next)
			lid_to_tlock(tblk->last)->next = lid;
		else
			tblk->next = lid;
		tlck->next = 0;
		tblk->last = lid;
	}
	/* anonymous transaction:
	 * insert the tlock at head of inode anonymous tlock list
	 */
	else {
		tlck->next = jfs_ip->atlhead;
		jfs_ip->atlhead = lid;
		if (tlck->next == 0) {
			/* This inode's first anonymous transaction */
			jfs_ip->atltail = lid;
			TXN_LOCK();
			list_add_tail(&jfe_list,
				      &TxAnchor.anon_list);
			TXN_UNLOCK();
		}
	}

	/* initi
	 * wakeup all waitorea for linelock */
	linelock = (struct ck * tblkXN_ck * tblck;
	linelck * tblk= 0;
	linelock->fck * tb	}
	/* anonymck * tb	}index = 0onymck * tb	}TYPE) {
	caseing locked by: acquire a block *(saction lock on theateLock:
	lst.  If the numbet.t 1);ck->ty= jfs_id, struct ock->next  a block *(struct ock->next lloc(sit.txLockAlloc); JFS_IP(ck;
	struct linelock ;
	struct tblock *tbi_mode) && (type  heateLock:
		TxBlocindex first fr_to_tlock(lid);

	/*
	 * initialize tlock
	 */
	tlkeup all waitorea for linelocklinelock *) & tlck->lock;
	linelock->next = ruct lilag = tlckLINELOCK;
	linelock->maxcnt = TLOCKSHORT;
	linelock->index = 0;

	sLONG (type & tlckTYPE) {
	cais inode's ype =t = XT* bind th)SLOTSIZE;
		bype = 0;

	/* bind the tl  hepnelolock->next_info(tlock *) & tlck->lip->atltail tlckLINE) {tk->lip->atltan lock kFree(lidlock *tb}management
 *		------how thi----------------
 */

/*
 * Get a transactiock from the start a tlock is acle is unloaded
C_LAZY beake tlosf
	 * thobjk->s
 *
 * retuin-----c* greatFty=jo lop", t seg----sve fro* t-----ce tlosf is imp%p", rion tode)
	 eckede*/
	tieable tlock on-jo lop", t seg----sv impMMIT_on tfluoundatoble tdiskp	tlck->nY beake tlof
	 * thdiskp	else {
tuindan haGNUFO d reacode)
	 eck;
ont nTxLonewfromateLockof
	 * tGNUFOseg----T ANY WARmase {ing tf is impseg----Tatomi%p",y on in-mfocllf is impseg----s
 *
 * retuin c* grt's tlockin-----f (nanonysystem.;
		TXN_up the6pseg----s
on t and tgement * sndltx
 *uunix svcson in-mfoi) {
		t_ait_mbeiemap undehdiskp	else it_mbc*
nt)n-mfoisw up ,
	tlck->m */
	*/
	else e;
 h
}gularnanonyer ve>xtli have aterysymbold_t l_mb,able to allist be freetgement * up a.offse. * (dure freuncate,ode)
	 eckut e* tGNUFOVMtruco lces
on tunaforctid = nt
	AZYte,oloseck;
etGNUFOiput {
tuiolose)ck
 *		flag	- forcgem single thread at jfs_init()
 */-----f  (&txEable to alllock f  (ilesseg----Tis_xtrumetgement *kAlloc(vead at i/o errWA:size;
	strutlock iuct metapa	*
 *	transactioniock * ret);

	de_info *n in	*/
module_para		   tht *teMap(st

	de_ine * mp,
		      iist);*/
mcksInUse	 * preveteMap(st

	de_inlock *tblk;
	lockrc {
	cai txUpdateMap(scc); JFS_IP( tid = %d", ti	ck *xtlcg;

	jfs_info("txBegick);
ck)fo("txBegi
		     i;= JFS_IP(ip);
	int dir_xtree = ti	tid_tranti	tio metoi;= JFS_IP(og *log;

	log 0x%p mp:0x%p lidtlock ,
		 tid, xtmaxcnt chec ng si
	TXN_LOer traefor-e froanonysystemcateLock;

isRforshou( iist)[0]s specirc {
-EROFSniti a tlTheEndr
	 */
logticd.logti iist)[0]IRECd = lcd.mark tlock fok(tid);s_info("tmark tlnt flag)bttx.wai(tblk->next)
			lid_to_tloctlck->tid = tid;

	Thow thisndex fir txLocd_K();
	INCREMENT(TxStat.lcd._K();
 can't frfor linelocklK()record	liscriptck->feteMap(st

	ck);= &cd._k)fo(_k)>active;

	cput)
	le32
			tblktive;)fo(_k)>at = ke ilogtid;
	tblk->xype = 0;ype lag & COMype =t (ich may ultim|*) mp->d wak))s_info("tmblk->xype = 0;) mp->d w/l;tlck->ti	prd.h>ek on-jo lop"* thbjk->s
lockteMap(ansactionfluou MMIT_PAGEsf is on-jo lop"* tanonof theoarked nohomeoanony->atlhea on-or linelocdhdiskpt nTxL->tid =ode:
f iscrash._ip- (ar_bt nTxLo- >tid = tcd. iist)gti iist)t.lcd.nr_xtrn= titlck->ti	*/

	jfsd <mp>
 *
 * paramet(on-disk)a		   tansactionnux/fs.hn-diski
		   er in>flag = tlckINO alloc/

	j*/
	if (xtid ==  nTxLolockAFfor)recordL->tidt, markhn-diski
		   */
cnonyebjk->ansactionsoonymous		   thor Fy * tl		    the high lisc	/*  beindRRIER,barrier;
 */
tid_t tx txExc/

	j*/
	if (xtid ==  nTx->tidtfkhn-diski
		  ach hm*/
	/* Nhn-diski
		   PAGEsfbyee th/*/
	/* NconKEUP(&tblk->waitor) > TxLoc)
		TxLoc0[k].necd.nr_ 1;
		init_toiock;cd. iist)[k])IREC
		niti)
		Tlogtt_wait n.necd.nr_ 1n
		init_	r_xtrcd. iist)[n]
				jfs_kDIREC
		 >etoid_to_tlotoiockkDIREC
		n_inodcd. iist)[n]xtrcd. iist)[k]n_inodcd. iist)[k]xtree)
	! */
	 of tr_xtrcd. iist)[k]n_inree = 0;
	lid_t lid;

s in the tBUGBUGo- 
		if (jfshasotemporarifrommitC
}

#ddffer t the tcati--Tis_or mnsurut WITHprogcnonyMMIT_ixCoritteonge the workimous
			jfs_ste,ode)
	 eck
	 * thjo lop". the hoper;
 *MMIT_INODor linelocdhdfs_sOCK();pneaj*/
	ireatcnony_info(t t the tjo lop" hasommitCcatiayddffe(r txODor linelocdhdfs_
 to shohat maysmnsiSH, &dfs_s
}

#dd    waitLockujfs.se > T
the transproblemcnowTis_oWITHwe
on tf matblk->e IWRITEEnd: titidt, mark
		  ,
	tlcif (tblkcnonmap_fdfs_og synhe inMIT_INODm;pnek-(i.e. ANY caettia/
tid_t txul,jee ->a_cessing > T
the trans 0) &tionnsolueuncate,it tiave ow, marku:
f i
the tIWRITEEnd:.he tion toEUP(&tfrof matblkAZY oos 0) list an we ohat als *kAlsmg t riobe) irred tMMIT_PAGEsf and workim *kAloritteonge the * (during truncate,ode)
	 eckcode is g txExweedgettd and
		 wor tlobe) iictitkf (ng > T
the t& COM!g |= tlckDIRECTORY;

	t *_UNLO&jfs_info("txEnd) mp->dDEL fo)s_info("tireenonmap_og sy_codkwaitckDIRECTaLock:initi_tlock(in the tMll
		 * praso
	if_nohock
		  ANY stANY WARt, mark_noho the tcalse itn-meut ewe'NY kn 0;
	treveteMap(sictig iloODE (w the tctdgetxCg a tim_nohomeg iletl_init(>flag)/* mark tlockDnohomemory 
hea forLoci.
{
		tlck->next (s)n isckINODELOC		if (jfs_ip->atltai) jfs_ip->ate tlock
CK();
			list_a)g = tblk->flag LINE) {e	l0;
		tblk->ltlock(last)->next !=/*
					tblk->neo_tlots transactioCK();
			list_a;N_LOCK();
			li0) {
		CK();
			list_add_onymofs_ip->anon_inode_list);
				TXN_UNLOCK();
			}
			jfs_ip->alize type dependenck(in the t*/

	jfsd <mp>
 *
 * paramethn-diski
		   PAGE the t(beco and	jfs_ to tail(--log->k'->next = jfsse > TxLockVHW((rc {
dlock dulocked ))e
			n a tle) r
	 */
      	og synlK()recordKEUc inif (xtid ==  nTxLlock index;
static vo)tructtxCXAD_NEWxul,XADLAZY) {
		jf(rc {
ct tbler of
	 */
&cd)e
			 a tlTheEndr
tlck->tidEnsurut WITHto allisettdad wilnge the w == tblkGC_LAZY beforef			shes:
 */
void tid = tid;
	++logx("txEnd) mp->dDEL fo)sjfs_if ma
	++logu.lid;
	(in the tAblk);_s
iave tid_t tg > T
the tIfable to allistction
,ise, wrommbt nTxetuin-the tjRetry logiincludater so,xweedgettdwanohome-the t tbl_GC_LAZY beforedotblk->e nsactiputo)tt, mark
		  -the tk eve( *
	 * Lab paramet->e nnTxetuincludatenstead,
 to sho_LAZY bea) and ends wi		 */
	ck-lomesok->e nsactipute is g ANY WARdf (n	TXN_WAKf (tblk->efore(ty= on. se > TxLocin the tI WAlievCKED,
	 (jfs_ipnos 0) ert and tic LiisatlheaI_End: titidcatok-w *kits,aI_NEWx	tlcId wak(jfs_ip-rked nohomiw the t
tid_t txasowel". tBt ek eve(Iedgettdd a da
	lig)) {vfre workim *v(nTxBlomiw,ve froma) iv inos/I_End:/Id wak/dwasRdf (list anJoerne > TxLockVHW	++logu.liIRECd_UNLEndId wak) {e	l0;
		x("txEn= ~) mp->d w/l;tl */
ASSERT(t!(l++logx("txEnd) mp->dDEL fo)&log->flag	BU(W	++logu.liIRECait_mbOMMIT_Sy {e!flag)/* mark tlockNoit_msitransau.lid)loctlck->ti	og synk tlocnlK()record txLocd_k)>ad the pcput)
	le16(LOG_k tloc)fo(_k)>a.offset = 0o(_m tbler of
	 */
tatiurn -E;

	/mGinodtlock ier of
	 *loctlck->ti	-during truncate,n 0;ode)
	 eck- whole thing orted txparentorAKfrNTY;nnux/fs_ip- (sert(addr
void cindex firsnux/fs>tid = tid;
't let a non-forced it_tatic in
	 *loctlck->ti	te intermediate repli.ansactionnux/fs.	 * pramediate repli
{
tuin-memog ort	 * parer* parametag = tlobjk->C*/
	else efer y.ctionnux/fs.b pararmediate repli.ansactionx;
static vo)tructtxCXAD_NEWxul,XADLAZY) {
		jfl++logx("txEnd) mp->dorced it_ta
static vo
	 *loctlck->ti	Anchorf (xtid ==  nTxLoack-(i.ee) /t	 * pare > TxLoc
static vo
	 *loctl& COMMIT_FO("txEnd called w/laz_info("tmsU_LOCK_
	 *locttlck->ti	ructtn>flag = tlobjk->Cd_UNL> TxLoc)
		TxLoc0[k].necd.nr_ 1;
		init_r_xtrcd. iist)[k]n_inree = 0;
	lid_t lid;

s in the tructtn>flag = tl	else d_UNL> 	 for free tiIRb 0;

	tb0(tlck->next bsactio0pen}ze with e) 
		/*
	rc !info("tms at thlocke1nize with TheEnd:%p mp:0x%p lidtlock :
		 tid, xtn t;
}

/*chec ng sircOCK();ree(lrcn: free sp	start a  tbls acle is unloadedock dskAFfor)lK()recordKEUP(&TNY ak;

t' thei? GNUFO 
	tiefUP(&seg----s
 *
 * retu* tl		  s->feteMMMITable tC* prasrumesve froWRITEEnd:Sion trecordseful,(&ioait;EEP_D*		flag	- forScgem single th  tid, stlog, struct tblock * tblk,
		struct commit * cd);
static v dataLog(struct jfs_k;
	lockrc {
	cai txUpda
		     i;= LockAlloc); JFS_IP(ck;
	struct linelock ;k);
ck);= &cd>a.rdr
tlck->tidog synlK()record(s)nUP(&(iles to tail(- group
		 *ocks oe     llize transaction *tlc
	/* Reode's firsOCK();
		t = tlck->tid) == tid) 
	tlck->type = 0;

	/LOGy 
hea for linelocklk);(str			TXN_Li->xtxjfs_iee)
	!_k)>aaggreg/fs.
	cput)
	le32
	INCREMEliIRECdT(Txaggreg/fs))
	!_k)>alK(.redo#incl(jfs_ip->	cput)
	le32
	INC_t lidintk(KERN))
	!_k)>alK(.redo#incl
		   >	cput)
	le32
kDIREC
		ry 
hea fog synlK()recordi*/
pareber in use lock *) &e tlckDTRE;

	/*
	 *t = XTock->l2liinesize = L2XTSLOTS	ct tbler of
	 */
tatiuvector
	FO tlock:0xinesize = L2DTSLOTS	dt tbler of
	 */
tatiuvector
	FO tlock:0xinesize = L2INODESL	ct tbler of
	 */
tatiuvect,ruct t	FO tlock:0xinesize = MAPDESL	uct tbler of
	 */
tatiuvector
	FO tlock:0xinesize = LDATASLO	ruct tbler of
	 */
tatiuvector
	FO tlock:0xi%p", tlck);;
	}

	/*
	 * update tlock vector
	i
	 * wa);ree(lrcn: free sp	ct tblsaction lock on td_K()	 * pre ip;
	tlc tloaelck * tblviate intebck ;tid, stlog, struct tblock * tblk, struct lrd * lrd,
		struct tlock * tlck, struct commid LogSyncRelease(stv dataLog(struct jfs_k;
	lockrc {
	cai txUpda{
	struct lid =pxE(waipxEcai txUpdapxE(cReleapxEck *tbi_p->xtxjfs_ip-n't frfor linelockasRREDOde i)recordi tloaelLocd_k)>alK(.redo#incld the pcput)
	le16(LOG_L2INO)fo(_k)>a.K(.redo#inclreak;

	defaucput)
	le16(LtlckDATA:
		liry 
hpxE;= &_k)>a.K(.redo#inclpxEcatlck->ti		 * prainfo(serg{
		clear_biE;

	/*
	 *t = XT->xfltransacti strainfo-serg{bet.t 1)redo(): *) &e_k)>ad the pcput)
	le16(LOG_REDOde ior
	iPXDaddr
vo(pxE* if a= 0;	/* ! PXD.offse(pxE*
		TXNtransto pal_
	def>> mmit thr th_cessi
	de);

s))
	!_k)>at = ke ilogtcput)
	le32
_m tbler of
	 */
tatiuvectory 
hea f/
	el(i.e.asR mp (log,bountlock tlck->type = 0;

	/WRITEde i;
))
			nTxLocE;

	/*
	 *t = XTFT_PAG{
s in the 	Ancho	 * prirsend to get rid(PAGEsf is use is
do	 * prirsendazy commito	 vnelda eckcode is galck * tblat.txBegi is useirsendazysommitC tloae eckcte is g a trans mp->initi_t workimousltruct
ping xExc/

	j* the ble to allrmediate repli
pareet rid(i, &l *
	  *
 * ress use is
doirsend,t, cu ite)ghimous
ade to stransact
	tr

sele lowgn
,iarrier;
 */ranse) i*/
mous
ade to stransge the * (dStat.li_tlock(ini strLOG_NOREDOlckEXTf is use is
do	 * prirsendafor-the t 1)redo() prevent dNoRedoPrg{beiltr s,o	tlckonnux/fs_iitidcpli
{
tubck lat.txBegi is useirsendt.li_tlo&e_k)>ad the pcput)
	le16(LOG_NOREDOlckEXTd;
	(in the tFty= useLOG_NOREDOlckEXTfrecord,xwee and workim *pasrk->e IAG  the hi{
tuin-merirsend to gved.
	 (		lii
	 *
	 IAG)ber inrred tome-the t useirsendaion:
	);
	xtidffer tseazy commit tto strssnd
		 ut
		 * (d iist)[1]i{
tuiiist)[2]..li_tlo&e_k)>a.K(.noredoin-irs.i,  thnextiith cput)
	le32
(u32) 	vfre_sOCcd>a iist)[1]))
	!_k)>alK(.noredoin-irs.in-irs_idxnextiith cput)
	le32
(u32) 	vfre_sOCcd>a iist)[2]ry 
hepxEck *ock;
	linelpxE(cRelea= 0;
	linelock->f	*pxE;= pxEck *->pxEcai!_k)>at = ke ilogtcput)
	le32
_m tbler of
	 */
tatiurn -En, and buue intebck lock tlck->type = 0;

	/UPLDAEMAPy 
hea f/
	el(i.e.asR mp (log,bountlock tlck->type = 0;

	/WRITEde i;
))
			n
;;
	}

	/*
ct tb: 	 * u the

	/e tlock vector
#if%p"  _	INCWIPtlck->ti	*medi/t	 * irselop" EArirsend tsactionalck * tblat.tx;
static vo)tviate intebPWMAPEUP(&TNYdi/t	 *->tidtfk useirsendazysommitC tloae eckctg a trans mp->;_ip->atlhead;
		* insert;

	/*
	 *t = XT-An, and bu strLOG_UPLDAEMAPbet.t 1)redo()lviate intebck afor-the tTNYdif is ew (	tlcs chdtfkhld) irselop" EArirsendt.li_tlo&e_k)>ad the pcput)
	le16(LOG_UPLDAEMAPlock: xEck *ock;
	linelpxE(cRelea= 0;
	linelock->f	nck *ockpxEck *->ed.
	niti)
		TiLoc0[ki.nexlock- i++,kpxEck *
		init_	rLocpxEck *->("txEndm	liALEndPXDeo_tlo_k)>alK(.te intpli.d the 
			TXN_Ucput)
	le16(LOG_ALEndPXDer
	FO			xtlck-_k)>alK(.te intpli.d the 
			TXN_Ucput)
	le16(LOG_FXTSdXDer
	FO_k)>alK(.te intpli.nxde pcput)
	le16(1er
	FO_k)>alK(.te intpli.pxE;= pxEck *->pxEcai!!_k)>at = ke ilog
		TXN_Ucput)
	le32
_m tbler of
	 */
tatiurn -En, /l of tranue intebck lock tlck->type = 0;

	/UPLDAEMAPy l o#	/* f	us tra_	INCWIPxExit()
 *
 *rcn: free sp	cuct tblsaction lock on td_K()MMIT_XN_UNLid, stlog, strucuct tblock * tblk, struct lrd * lrd,
		struct tlock * tlck);
static voe_ine * mp,ncRelease(strp = JFS_IP({
	struct lid =pxE(waipxEcai_p->xtxjfs_ip-n't frfor linelockasRREDOde i)recordi tloaelLocd_k)>alK(.redo#incld the pcput)
	le16(LOG_LDAT)fo(_k)>a.K(.redo#inclreak;

	defaucput)
	le16(Lt:
		jfs_err(ry 
hpxE;= &_k)>a.K(.redo#inclpxEcatlcki strainfo-serg{bet.t 1)redo(): *) &_k)>ad the pcput)
	le16(LOG_REDOde ior

		if (jfs_nohctio_iait_e(xjfs_iees special.  It ransk - 1;zysommitC be freet,xwe' co's tlzy cod;
	tnd workim>e nsact(&txEmesokdgettdbitLocklowgtblk->flagi_tlo&etransactio0pendgrabd = tlck->tid;	/l = tlck-_ mp = 0x%p,  tdisclogd = tlck->tid;	/l	jfs_ip->xt*
 * F	()
 *
 * FUl of PXDaddr
vo(pxE* if a= 0;	/* !PXD.offse(pxE*Ntransto pal_
	def>> mmit thr th_cessi
	de);

s))
o(_k)>at = ke ilogtcput)
	le32
_m tbler of
	 */
tatiuvectory 
ha f/
	el(i.e.asR mp (log,bountlock lck->type = 0;

	/WRITEde i;
t()
 *
 * FUNCTION:		dt tblsaction lock on td_K()M	 */
		dir_xtlc tloaelck * tblviate intebck ;tid, stlog, blk);dt tblock * tblk, struct lrd * lrd,
	   struct tlock * tlck);
static void LogSyncRelease(strp = JFS_IP({
	struct lid = txUpdapxE(cReleapxEck *tb=pxE(waipxEcai_p->xtxjfs_ip-n't frfor linelockasRREDOde i/NOREDOde i)recordi tloaelLocd_k)>alK(.redo#incld the pcput)
	le16(LOG_LIT_PAfo(_k)>a.K(.redo#inclreak;

	defaucput)
	le16(Lt:Tjfs_err(ry 
hpxE;= &_k)>a.K(.redo#inclpxEcatlxLocE;

	/*
	 *t = XTBTROOTa
	!_k)>alK(.redo#incl*
	 * pacput)
	le16(LOG_BTROOTacatlck->ti	tructirsense revi_s
}ediate r: (&txEae anone r;->ti	tructirsense rein-place: (&txEae anone r;->ti	 ew right
pareber inparebsiisaxtn or linelocdhin-ak;
->ti	rootber inrootbparebsiisa: (&txEae anone r;->tilear_biE;

	/*
	 *t (= XTENT | = XT-mp->D)transacti strainfo-serg{btfk use ew prg{bet.t 1)redo(): worki/
	el str(LOG_NNTRYet.t 1)redo()lviaor lineloc
Anchor.tlnsaction te intebck afortTNYdif is use ew prg{t.li_tlo&e_k)>ad the pcput)
	le16(LOG_REDOde ior
	ir_biE;

	/*
	 *t = XT-mp->D)
	FO_k)>alK(.redo#incl*
	 * pacput)
	le16(LOG_-mp->D)r
	i			xtlck_k)>alK(.redo#incl*
	 * pacput)
	le16(LOG_NNTRr
	iPXDaddr
vo(pxE* if a= 0;	/* ! PXD.offse(pxE*
		TXNtransto pal_
	def>> mmit thr th_cessi
	de);

s))
	!_k)>at = ke ilogtcput)
	le32
_m tbler of
	 */
tatiuvectory 
hea f tloaelalck * tblat.tx;
static vo)tviate intebPMAPbet.-the tTNYdif is use ew prg{t.li_tlo&exLocE;

	/*
	 *t = XTBTROOTa
	!id: %d, tbtlck->type = 0;

	/UPLDAEMAPy l: xEck *ock;
	linelpxE(cRelea= 0;
	linelock->f	pxEck *->("txE=dm	liALEndPXD->f	pxEck *->pxE;= ipxEcai_	pxEck *->ed.
	 lse
	
hea f/
	el(i.e.asR mp (log,bountlock tlck->type = 0;

	/WRITEde i;
)id: %d, tblk =ck->ti	(&txEae anone r/d;
	t		 *ocks	sib(tblkransact_mbte inte( mapright
parebge the siisa);->tilear_biE;

	/*
	 *t (= XT->xfl | = XTRELINK)transacti strainfo-serg{bet.t 1)redo(): *) &e_k)>ad the pcput)
	le16(LOG_REDOde ior
	iPXDaddr
vo(pxE* if a= 0;	/* ! PXD.offse(pxE*
		TXNtransto pal_
	def>> mmit thr th_cessi
	de);

s))
	!_k)>at = ke ilogtcput)
	le32
_m tbler of
	 */
tatiuvectory 
hea f/
	el(i.e.asR mp (log,bountlock tlck->type = 0;

	/WRITEde i;
)id: %d, tblk =ck->ti	apage_;
	t		 :l(i.e.hasommitCsavnelda ec->ti	apage
}ediate r: co lcerirsend tsactio	alck * tblat.txBegi is use(i.e.hasommitC tloae ecctio	atg a trans mp->inittilear_biE;

	/*
	 *t (= XTFXTS | = XTRELOCATE)transacti strLOG_NOREDOde i) is used;
	tnd prg{bet.t 1)redo() workim *vent dNoRedoPrg{beiltr o	tlckonnux/fsubck lat.txBeg worki is used;
	td
pareet rilo&e_k)>ad the pcput)
	le16(LOG_NOREDOde ior
	ipxEck *ock;
	linelpxE(cRelea= 0;
	linelock->f	*pxE;= pxEck *->pxEcai!_k)>at = ke ilogtcput)
	le32
_m tbler of
	 */
tatiurn -En, and bualck * tblat.tx;
static vo)tat.txBegi is use(i.e workizysommitC tloae eckctg a trans mp->;_i	lock tlck->type = 0;

	/UPLDAEMAPy l oid: %d, tNCTION:		xt tblsaction lock on td_K()x	 */
		dir_xtlc tloaelck * tblviate intebck ;tid, stlog, blk);xt tblock * tblk, struct lrd * lrd,
	   struct tlock * tlck);
static void LogSyncRelease(strp = JFS_IP(
		     i;= JFS_IP({
	struct lid =k;

	TXN_LOCK();

	if*linelock;
	xtpage_t *pck * tblkck * tb l();

	if*dnsacinelockad* tb l();

	ifpxE(cReleapxEck *tb=pxE(waip
	TXpxEcaifo *nirssinwm, hwmcatlx->xtxjfs_iee)
	p->xtxjfs_ip-n't frfor linelockasRREDOde i/NOREDOde i)recordi tloaelLocd_k)>alK(.redo#incld the pcput)
	le16(LOG_XIT_PAfo(_k)>a.K(.redo#inclreak;

	defaucput)
	le16(LtXTjfs_err(ry 
hp
	TXpxE;= &_k)>a.K(.redo#inclpxEcatlxLocE;

	/*
	 *t = XTBTROOTaransa_k)>alK(.redo#incl*
	 * pacput)
	le16(LOG_BTROOTacak->lwm.	INC_t lidin
			    le16>flag |= tlckDIRECTORY;

	ta_k)>alK(.redo#incl*
	 * p
		TXN_Ucput)
	le16(LOG_LIR_XIT_PAfo()
			n
;;se
				p = &jfs_ip->i_xtroot->atltan
		}
		xtlck->lwm.length = 0;	/* 
ock;
		xtlck->header.offset0;
	linelock->N_ck * tblck;
	linelck * tblk= 0;
	linelock->fkad* tb	xtlck->headdnsacineloc) mk * tb li_ck->ti	(&txEae anone r/irsense r;ocks	sib(tblkransact_mbte inte( mapright
parebge the siisa);->tilear_biE;

	/*
	 *t (= XTENT | = XTGROW | = XTRELINK)transacti strainfo-serg{bet.t 1)redo():-the t 1)redo()  ANY te intebck afortTNYdif is ew/irsendnd workiirsends (XAD_NEW|XAD_-mp->D)f isXAD[nwm:firsOCer i-the tTinfo-serg{btfkXADist)t.lhe t 1)redo() ructtxC(XAD_NEW|XAD_-mp->D)fype = txE-the tTrblotblk->e Tinfo-serg{b
	 * th & COMMIT_PAGE.et rilo&e_k)>ad the pcput)
	le16(LOG_REDOde ior
	iPXDaddr
vo(p
	TXpxE* if a= 0;	/* ! PXD.offse(p
	TXpxE*
		TXNtransto pal_
	def>> mmit thr th_cessi
	de);

s))
	!_k)>at = ke ilogtcput)
	le32
_m tbler of
	 */
tatiuvectory 
hea f tloaelalck * tblat.tx;
static vo)tviate intebPMAP
AnchorortTNYdif is ew/irsendndiirsends  isXAD[nwm:firsO
Anchor. in usetransa
selet.lhe tx;
static vo)tructtxC(XAD_NEW|XAD_-mp->D)fype .et rilo&e_wmogtto_cpu(p->header.le16>fla_wmoginfo("te_wmogtXTde iMAXjfs_lck->lwm._wmogin)->next = a tle) r
	>lwm._wmo>efirsOCK();;
	}

	/*
xt tb: _wmo>efirs\nip: ",= a tle) r
	>}tbtlck->type = 0;

	/UPLDAEMAPy l:kad* tb->("txE=dm	liALEndXADLISTy l:kad* tb->c*
nt in)->n - _wmr
	>lwm.(kad* tb->c*
nt <= 4)LO&jfs_infox("txEnd) mp->d w/la	init_	r--Ti: ",=pxE(waipxEcaicial.  he t tblkGC_LAZY* La
 * t)x	 */
	 *kAl' thei? */e the wohe tx;
static v runuffeCopy kaddcatokock->nextto wohe tpructrvwe ori ha MMITab	ti_t wo	 or we scbeiSynwicckasR* LapxE'srasodadt
		 * (d_t tg > rilo&e:kad* tb->("txE=dm	liALEndPXDLISTy l:hpxE;= kad* tb->ddnsaclwm.to_cpu(pxEck *tb=ti)
		TiLoc0[ki.nekad* tb->c*
nt- i++d_to_tloPXDaddr
vo(pxE* addr
voXAD(&(xtpad[nwm + i])on_inodPXD.offse(pxE*N.offseXAD(&(xtpad[nwm + i])on_inod(xtpad[nwm + i].("txEn=
			TXN_U~(XAD_NEW | XAD_-mp->DEDon_inod(xd++n_ino*/
	 T_PAGE)
			al.  he tddnsacl ANY o thelviaorviaorn */
	x	 */, mnsuru wohe tx*
	 uring truncate,n t;ode)
	 ecklazilock > rilo&e:kad* tb->("txE=dm	liALEndXADLISTy l:	kad* tb->ddnsaclwm.(xtpad[nwm]n_inol0;
		x("txEn= ~) mp->d w/l;tlo*/
	 mp:0x%p lxt tb: TNYdif, mp, lid);

	/*

	/e tlo _wm, ip"k > r"c*
nt page ljfs_ieeby thee(stv nwm, kad* tb->c*
ntry 
heck * tb	}TYPE) {
e
	
h with e) 
		ea f/
	el(i.e.asR mp (log,bountlock tlck->type = 0;

	/WRITEde i;

)id: %d, tblk =ck->ti	apage_;
	t		 :lcnonyM;
	t		 /ure freunca(ref.	x	Tbe freeo)>tid ction (i.e. ANY WAR	 vnelda eckcinfo(_K()	xCoritteon{
tubck  transacte intdor. in usetran);->tilear_biE;

	/*
	 *t = XTFT_PAG{
s inrLOG_NOREDOde i)_K())
		NoRedoPrg{beiltr :-the ti/
parebereeber incnonyM;
	t/, NoRedoFnonyeiltr oer i-the t	else eerg{btfk up a.t_mbc*
nt  ANY subrume	NoRedoPrg{
Anchoriltr snUP(&(ilesprg{t.li_tti/
parebereeber incnonyure fre
		 * og synNoRedoPrg{
Anchoriltr niti_t workiupadt{btfkb pararmediate replilat.txusetransa
sele:-the ti/
parebereeber inM;
	t		 o	tlckre freunc,rLOG_UPLDAEMAP.lhe t 1)lat.txusetransa
seles_ip->nerintdor. in
 */
void tihe titsing mapetranskadd(&txies;_i	lock t/tti/
parebereeber incnonyure freunc,r strLOG_NOREDOde i worki is used;
	tnd prg{bet.t 1)redo()im *vent dNoRedoPrg{
Anchoriltr lat.txusetrant.li_tlo&exLocE_infox("txEnd) mp->dTRUNCATE)ymous traog synNOREDOde i)at.txusetransilo&e:_k)>ad the pcput)
	le16(LOG_NOREDOde ior
	iiPXDaddr
vo(p
	TXpxE* if a= 0;	/* !  PXD.offse(p
	TXpxE*
		TTXNtransto pal_
	def>> mmit thr t
		TTXNh_cessi
	de);

s))
	!!_k)>at = ke ilog
		TXN_Ucput)
	le32
_m tbler of
	 */
tatiurn -En, 
	&exLocE;

	/*
	 *t = XTBTROOTa		 * RemovEmpty)x	 */
's tlocklowgn
_init(&j_k)>ad the pcput)
	le16(LOG_REDOde ior
	i!!_k)>at = ke ilog
		TTXN_Ucput)
	le32
_m tbler of
	 */
tatiuvectory 	! */
	 of tfrfor lrLOG_UPLDAEMAPb is use is
doirsends
AnchoXAD[mp->xflag & :hwm)or. in used;
	tnd prg{ba
sele
Anchorort 1)redo()lviate intebck t.li_tlo&e_k)>ad the pcput)
	le16(LOG_UPLDAEMAPlock:_k)>alK(.te intpli.d the Ucput)
	le16(LOG_FXTSXADLISTlock:ck;
		xtlck->header.offset0;
	linelock->		hwmogtto_cpu(h->header.le16_k)>alK(.te intpli.nxde 
	TXN_Ucput)
	le16(hwmo-(mp->xflag & _waid;
	(intru tloaelock->nextrort m tbl)tlck->hwm.offer.length = 2;

mp->xflag & COMM(type & tlckNEW) {
			xhwmo-(mp->xflag & _waiCOMM(type &TYPE) {
e
		!_k)>at = ke ilogtcput)
	le32
_m tbler of
	 */
tatiuvectory 
hea f tloaelalck * tblat.tx;
static vo)tviate inteb
ade to sviat	 * irsends  isXAD[mp->xflag & :hwm)or. in us
the t
t
	tnd prg{ba
sele;_i	lock tlck->type = 0;

	/UPLDAEMAPy l:kad* tb->c*
nt inhwmo-(mp->xflag & _waiCOMMlwm.(kad* tb->c*
nt <= 4)LO&jfs_infox("txEnd) mp->d w/la	init_	r--Ti: ",=pxE(waipxEcaicial.  he t tblkGC_LAZY* La
 * t)x	 */
	 *kAl' thei? */e the wohe tx;
static v runuffeCopy kaddcatokock->nextto wohe tpructrvwe ori ha MMITab	ti_t wo	 or we scbeiSynwicckasR* LapxE'srasodadt
		 * (d_t tg > rilo&e:kad* tb->("txE=dm	liFXTSdXDLISTy l:hpxE;= kad* tb->ddnsaclwm.to_cpu(pxEck *tb=ti)
		TiLoc0[ki.nekad* tb->c*
nt- i++d_to_tloPXDaddr
vo(pxE*o_tlo	addr
voXAD(&(xtpad[mp->xflag & _wai])on_inodPXD.offse(pxE*o_tlo	.offseXAD(&(xtpad[mp->xflag & _wai])on_inod(xd++n_ino*/
	 T_PAGE)
			al.  he tddnsacl ANY o thelviaorviaorn */
	x	 */, mnsuru wohe tx*
	 uring truncate,n t;ode)
	 ecklazilock > rilo&e:kad* tb->("txE=dm	liFXTSXADLISTy l:	kad* tb->ddnsaclwm.(xtpad[mp->xflag & ]n_inol0;
		x("txEn= ~) mp->d w/l;tlo*/
	 mp:0x%p lxt tb: Ancho	 mp, lid);

	/*c*
nt pa _wm,2, 16, 4ljfs_ieeby thekad* tb->c*
ntry 
heck * tb	}TYPE) {
e
	
hea f/
	el(i.e.asR	 vneld TxLockVHW((s_infox("txEnd) mp->dPWMAP&log g |= tlckDIRECTORY;

	t UNLO&j!cE;

	/*
	 *t = XTBTROOTa) {e	lck->type = 0;

	/FXTSde i;
	(in
	t UN_PAGE(s_infox("txEnd) mp->dPMAP&
	t UN?	);
	xtitxusetrant.li_tlo&ed: %d, tblk =ck->ti	apag/(&txEabre freunc:ncnonyure freunca(ref.	x	Tbe freeo)>tid ctio	|transactio+sactio+sactio+sactioooooooooo|ctio	t UN| with | with |ctio	t UN| with | withhwmo-(hwmoge the *re freuncctio	t UN| with)->n - ure freuncao the tra	t Ulwmo-(lwmoge the *re freuncctio  tlckNtransalear_biE;

	/*
	 *t = XTTRUNCATE)ymoustransactodd	liclarreuncasupprucs tho,boguip-cc (lo}

/*tlo&epxE(wapxE;= pxE;*
 *	te freeteirsenda iskaddTxLockhelvwmcatl(in the tFty= re freunca useint	jfsock->next wrommb wilmesoki ewnuld workiWARdifficultim *vethe kaddnsacli(lidlock *ba
selelist andet = the,ewe'NY js tlted txid) {
		tblk  *kAlode)
	 ecist ani		 */
	ck-lomesok->ate
		 * parentwn takbeake tloange the workix;
static v runuf_i	lock tl0;
		x("txEn= ~) mp->d w/l;tlo_wmogtto_cpu(p->header.le16>fla_wmoginfo("te_wmogtXTde iMAXjfs_lc		hwmogtto_cpu(h->header.le16twmogtto_cpu(t->header.letl(in the 	og synlK()recordK_i	lock t/tt strainfo-serg{bet.t 1)redo():-the .lhe t 1)redo()  ANY te intebck afortTNYdif is ew/irsendnd workiirsends (XAD_NEW|XAD_-mp->D)f isXAD[nwm:firsOCer i-the tTinfo-serg{btfkXADist)t.lhe t 1)redo() ructtxC(XAD_NEW|XAD_-mp->D)fype = txE-the tTrblotblk->e Tinfo-serg{b
	 * th & COMMIT_PAGE.et rilo&e_k)>ad the pcput)
	le16(LOG_REDOde ior
	iPXDaddr
vo(p
	TXpxE* if a= 0;	/* ! PXD.offse(p
	TXpxE*
		TXNtransto pal_
	def>> mmit thr th_cessi
	de);

s))
	!_k)>at = ke ilogtcput)
	le32
_m tbler of
	 */
tatiuvectory 
hea  workixte free (&txEaXAD[twmogin)->no-(1]:.li_tlo&exLocEwmogin)->no-(1)ymous traor lrLOG_UPLDAEMAPbet.t 1)redo()lviate intebck afor-thnchor.tlail(- e freete
t
IT_irsenda is* (dure frend woorkii&txEaXAD[)->no-(1]:.lition to_cpu(pxEck *>xtx e freete
t
IT_irsendon_inorilo&e:pxEck *ock;
	linelpxE(cRelea= 0;to_cpu(pxEck *tb=tiREGATE_I);pxEck *->*
	 *t = XTTRUNCATE);silo&e:_k)>ad the pcput)
	le16(LOG_UPLDAEMAPlock::_k)>alK(.te intpli.d the Ucput)
	le16(LOG_FXTSdXDer
	FO_k)>alK(.te intpli.nxde pcput)
	le16(1er
	FO_k)>alK(.te intpli.pxE;= pxEck *->pxEcai!!pxE;= pxEck *->pxEc
	rely coviattloaelck * tblilo&e:_k)>at = ke ilog
		TXN_Ucput)
	le32
_m tbler of
	 */
tatiurn -En, /l of tra
hnchor.tla(&txiesaXAD[)->n:hwm]:.li_tlo&exLochwmo>RefirsOCK();tfrfor lrLOG_UPLDAEMAPb is use is
doirsends
AnnchoXAD[)->n:hwm]or. in used;
	tnd prg{ba
sele
Annchorort 1)redo()lviate intebck t.li	silo&e:_k)>ad the pcput)
	le16(LOG_UPLDAEMAPlock::_k)>alK(.te intpli.d the 
		TXN_Ucput)
	le16(LOG_FXTSXADLISTlock::ck;
		xtlck->header.offset0;
	linelock->			hwmogtto_cpu(h->header.le166_k)>alK(.te intpli.nxde 
	TTXN_Ucput)
	le16(hwmo-()->nowaid;
	((intru tloaelock->nextrort m tbl)tlck->>hwm.offer.length = 2;

LINE) {e	(type & tlckNEW) {
			xhwmo-()->nowai) {e	(type &TYPE) {
e
		!:_k)>at = ke ilog
		TXN_Ucput)
	le32
_m tbler of
	 */
tatiuvectory 	! of tra
hnch	ttloaelck * tb(s)nUP(&x;
static vo)tviate inteb
ade to /
heck * tb	}TYPE) {
0y 
hea  workieateLock:(&txiesaXAD[nwm:firsO:.li_tlo&exLoc_wmo<efirsOCK();tfrf tloaelalck * tblat.tx;
static vo)tviate intebPMAP
AnnchorortTNYdif is ew/irsendndiirsends  isXAD[nwm:firsO
Annchor. in usetransa
selet.lhhe tx;
static vo)tructtxC(XAD_NEW|XAD_-mp->D)fype .et 	silo&e:lck->type = 0;

	/UPLDAEMAPy l::kad* tb->("txE=dm	liALEndXADLISTy l:	kad* tb->c*
nt in)->n - _wmr
	>	kad* tb->ddnsaclwm.(xtpad[nwm]n_
	>	 mp:0x%p lxt tb: TNYdif, mp, lid);

	/*c*
nt pa "k > 	 "_wm, ip)->n:check e, 4ljfs_ieeby thekad* tb->c*
ntv nwm, firsOr
	>	ck * tb	}TYPE)++n_inokad* tb++n_in} 
hea  workixte free (&txEaXAD[twmogin)->no-(1]:.li_tlo&exLocEwmogin)->no-(1)ymous tra tloaelalck * tblat.tx;
static vo)tviate inteb
ade tto sviat	 * - e freete
t
IT_irsenda is* (dure frend woorkii&txEaXAD[)->no-(1]t.lhhe t to_cpu(pxEck *>xtx e freete
t
IT_irsendon_inorilo&e:lck->type = 0;

	/UPLDAEMAPy l::pxEck *ock;
	linelpxE(cRelea= kad* tb l(f	pxEck *->("txE=dm	liFXTSdXD l(f	pxEck *->c*
nt in1 l(f	pxEck *->pxE;= pxE;_
	>	 mp:0x%p lxt tb: xte free , mp, lid);

	/*c*
nt pa "k > 	 "hwm, i", eeby thepxEck *->c*
nt, hwmOr
	>	ck * tb	}TYPE)++n_inokad* tb++n_in} 
hea  workir.tla(&txiesaXAD[)->n:hwm]:.li_tlo&exLochwmo>RefirsOCK();tfrf tloaelalck * tblat.tx;
static vo)tviate inteb
ade tto sviat	 * irsends  isXAD[)->n:hwm]or. in usd;
	tnd wo	o stransa
selet.lhhe lo&e:lck->type = 0;

	/UPLDAEMAPy l::kad* tb->("txE=dm	liFXTSXADLISTy l:	kad* tb->c*
nt inhwmo-()->nowai) {e	(ad* tb->ddnsaclwm.(xtpad[)->n]n_
	>	 mp:0x%p lxt tb: Ancho	 mp, lid);

	/*c*
nt pa "k > 	 ")->n:ch hwm, i",k e, 4ljfs_ieeby thekad* tb->c*
ntv nirssihwmOr
	>	ck * tb	}TYPE)++n_in}	
hea f/
	el(i.e.asR mp (log,bountlock tlck->type = 0;

	/WRITEde i;
))oid: %d, tNCTION:		uct tblsaction lock on td_K()r. inck * tbl*/
chordhdfs_sirsends;tid, stlog, blk);uct tblock * tblk, struct lrd * lrd,
		struct tlock * tlck, struct commioid LogSyncRelease(strp = JFS_IP(pxE(cReleapxEck *tb=r--Ti,exlock-b=pxE(waipxEcai_ck->ti	apage
}ediate r: t	 * -impso lcertructirsend tsactionalck * tblat.tx;
static vo)tat.txBegi is use(i.e wrkizysommitC tloae eckctg a trans mp->ely tblk->e srcee th
}leLockof(i.e.addr
vo;->tilear_biE;

	/*
	 *t = XTRELOCATE)ransacti strLOG_NOREDOde i) is use mapr}leLockof(i.e
nnchorort 1)redo()lviavent dNoRedoPrg{beiltr t.li_tlo&e_k)>ad the pcput)
	le16(LOG_NOREDOde ior
	ipxEck *ock;
	linelpxE(cRelea= 0;
	linelock->f	pxE;= &_k)>a.K(.redo#inclpxEcaf	*pxE;= pxEck *->pxEcai!_k)>at = ke ilogtcput)
	le32
_m tbler of
	 */
tatiurn -En, and bu(N.B.toEUP(&tfr,t 1)redo()ldoesaNOTate inteb
ade to sat.txBegi is use(i.e.a
seles)
		TLOG_XIT_P|LOG_NOREDOde ior
	ie ti/
parebereeber in
}ediate r,rLOG_UPLDAEMAP(_K()	x
	ie t *
 * rc& lidgenerintdon 0;et.t 1)redo() workim *nux/fsubck lat.txBegi issrcpr}leLockof(i.er
	ie t(ar_bype =LOG_RELOCATEt wrommbr--roduc1;
	red t ANY
	ie tin tlot 1)redo()lviavent dNORedoPrg{beiltr o	tlcals 
workiupx/fs.b pararmediate replikctg >e sa->emp->,ablux
	ie tablk)tblkantirsra(_K()og sy)t.li_tlo&e_k)>ad the pcput)
	le16(LOG_UPLDAEMAPlock:_k)>alK(.te intpli.d the Ucput)
	le16(LOG_FXTSdXDer
	F_k)>alK(.te intpli.nxde pcput)
	le16(1er
	F_k)>alK(.te intpli.pxE;= pxEck *->pxEcai!_k)>at = ke ilogtcput)
	le32
_m tbler of
	 */
tatiurn -En, and bualck * tblat.tx;
static vo)tat.txBegi is use(i.e workizysommitC tloae eckctg a trans mp->;_i	lock tlck->type = 0;

	/UPLDAEMAPy led: %d, tblk_ck-ee thOtLocwi	nTxt'e,n t;_s
}ediatge
}quesd tsactio>atlhead;
		 bu strLOG_UPLDAEMAPbet.t 1)redo()lviate intebck afor-the tr.tlail(- e freet/r}leLockof
t
IT_irsenda is* (d_xtrootorkii.g.: irselop" EArirsend,pr}leLocko/	te freeteirsend-the tr. inxtTailgreeo)t.li_tlo&e_k)>ad the pcput)
	le16(LOG_UPLDAEMAPlock: xEck *ock;
	linelpxE(cRelea= 0;
	linelock->f	nck *ockpxEck *->ed.
	niti)
		TiLoc0[ki.nexlock- i++,kpxEck *
		init_	rLocpxEck *->("txEndm	liALEndPXDeo_tlo_k)>alK(.te intpli.d the 
			TXN_Ucput)
	le16(LOG_ALEndPXDer
	FO			xtlck-_k)>alK(.te intpli.d the 
			TXN_Ucput)
	le16(LOG_FXTSdXDer
	FO_k)>alK(.te intpli.nxde pcput)
	le16(1er
	FO_k)>alK(.te intpli.pxE;= pxEck *->pxEcai!!_k)>at = ke ilog
		TXN_Ucput)
	le32
_m tbler of
	 */
tatiurn -En, /l	 mp:0x%p luct tb:ekaddr;

	lx xlen;

	x",k e, 4(u 0) ).addr
vodXD(&pxEck *->pxE),k e, 4W) {
	dXD(&pxEck *->pxE)n, /l of tranue intebck lock tlck->type = 0;

	/UPLDAEMAPy l o: acquire aEAlsaction lock on td*/

	jfsck * tblat.tEA/ACL irsends  rGNUFOsetd) mp->dINOCKS;ype lalockblk); aEAluct metapage * mp,
		     intdxE(waie maeantdxE(waiear_earp = JFS_IP(ck;
	struct>xt*
 * F	
	linelpxE(cReleack * tblck*
 *,eapxEck * xt*
 * Ftlhing ortedoaelck * tblrortTNYdif is ew EArirsend tsalear_biar_eard;
		 buS eve( *eear_eashohat mayashomp
	tnlid up edii&txEawee andtto wo orchetblat.tx (duwofE_HEA
	red tindaiatgeweejfs_ip-actu& li wo orcC_LAZY ew EArdfs_
 to lo&exLocar_ea->("txEndDXD_-mp->Ta		 * Rruct>xtipM
statinuct, eebyructMAPlock::ck * tblck;
	linelpxE(cRelea= 0;
	linelock->f	: xEck *ock;
	linelpxE(cRelea= ck * tb l(f	pxEck *->("txE=dm	liALEndPXD->f	iPXDaddr
vo(&pxEck *->pxE* addr
voDXDiar_ear/* !  PXD.offse(&pxEck *->pxE* .offseDXDiar_ear/* !  pxEck *
	r
	>	ck * tb	}TYPE) in1 l(f)
			nTxLocar_ea->("txEndDXD_INOCKSa		 * Rruct>xt*
 * FtlFOset)/* mark tlockIait_eeantiid;	/l oblk =ck->tirtedoaelck * tblrorts chdtfkhld EArirsend tsalear_bi!flag)/* mark tlockNoit_msiiidLO&j maea->("txEndDXD_-mp->Ta		 * r_biE;

oginrn -E		 * Rruct>xtipM
statinuct, eebyructMAPlock::ck * tblck;
	linelpxE(cRelea= 0;
	linelock->f	: xEck *ock;
	linelpxE(cRelea= ck * tb l(f	ck * tb	}TYPE) {
0y lo*/
	pxEck *->("txE=dm	liFXTSdXD l(fPXDaddr
vo(&pxEck *->pxE* addr
voDXDi maea)/* ! PXD.offse(&pxEck *->pxE* .offseDXDi maea)/* ! ck * tb	}TYPE)++n_i o: acquire atic insaction lock on thi		 */
	ck-loaog synparent* tbetu* tid) {
		tbluire with_info(t  tblseut ege the *;
static vo);tid, stlog, blk); atic in * lrd,
		struct tlorp = JFS_IP(ck;
	struct;= LockAlloc,
LINE) {JFS_IP({
	struct lid  =ck->tirer;
rtitxuseindRRf (tid) {
		tblk = tisuin-t orcfrNTY;nnux/fseindRRf (taddr
voved.
	 pare > Tx (right
tokoef-meuott inup>tid = truct>xt lid;
		else
			tblfirsOr
		/* Reode's firs;
lid;
	}
	/* anonym	rele  lliOCK();
		t = tlck->tid) == tid) 	t->atltaode's firs;
llid;
	}
	/* an>flag LINE) {e 0;
		tblk->last = l	/* ReLINE) {lk =ck->tiri		 */
	ck-loaog syn use(i.e,kcode rkizhld  use(i.e.at.tx;
static vo);->tilea     llize transaction *tlc
	/* RefirsOCK();
		t = tlck->tid) == tid) 	t->atltaode's firs;
 * r_bi(p->xtxjfs_ip-) !inrn -_Sy {eg	BU(E;

	/*
	 *t = XTBTROOTa	 anonymous ATE_I);tranx("txEnd) mp->dPe ior

	&exLocE;

	/ype =t = XTWRITEde id_to_tlot;

	/ype =t= ~

	/WRITEde i;

)i		 budo,n t;);
	xtitprg{b
	 r.tlnsactinit(&jted td = tlck->tid;	#xLo0
)i		 bk e, 4t rans"right"k->fblk-okdg et =ate,itk e, 4t i		 */
	ck-loaog syn use
			jfs_.k e, 4t WithimousKEUP(&tbimp
	----reunca u	x
	i	ie tisizyrdek eve(og sy_{
	struct
}qu	jfx
	i	ie tue,it kODm;p=t rtpli  use(i.edater we
	i	ie tzy co = tisuo the*/
	ir
	 * th & Cdfs_
 to	o strans,xweedgettdwanohookdg  u	xdatek->fbk
 to	o swwe scbgetd* t		li i		 */
	ck-loaog sid tihohe tx** parentw->nY beyion tre
	xtidftihohe nit(&jATE_I);tranno mp = on_inodset)bit(METAs_nohy, &tran
	TXN_LOnodset)bit(METAsi		 , &tran
	TXN_L#	/* fLOno}	/l oblk: acquire a
static vo)action lock on tdnux/fsep
rtsac---Tamediate replik(	tlcwork*/
	
ade-mfoi) appropri/fs))
ction para & er:tid, stlog, blk); a
static vo * lrd,
		struct tlorp = JFS_IP(
		     i;= JFS_IP(
		     iick t.lLockAlloc); JFS_IP(ck;
	struct linelock ck * tblkck * tb l();

	ifpxE(cRelepxEck *tb=r--Tck *
	 tb=r--Tk,exlock-b=JFS_IP({
	struct li>xt*
 * Ftl iick  ;
	INCREMEmmit thrdin
iick t.N_ck d the p(s_infox("txEnd) mp->dPMAP& ?d) mp->dPMAP :d) mp->dPWMAPocttlck->ti	upx/fs.b pararmediate repliansactionnux/fs.rmediate red_UNLEilopplik(	tlcwpli)kcode rkinux/fs.lsni is use(pli
pare;->tileack->tiri scb(iles to t/prg{btfk d) {
		tblk    b pararmediate r/t	 *:ansactionfP(&(iles to t/prg{btfk d) {
		tbl,inux/fs.mli.ansaUN?	ahe * (r/
		dir_fP(&ppli
{
tupwplikctg >e sa->emp->transalea     llize transaction *tlc
	/* Reode's firsOCK();
		t = tlck->tid) == tid) 
	t& COMM;

	/ype =t = XTUPLDAEMAPloginfo("tecohe*/ue) 
	t& COM;

	/ype =t = XTFXTSde i)E)
			al.  he tAaitLock->efore* La
ttempohookad wi
chordhspacu wohe timmedi/fslomesokwwewanohookgetdridi*/
mous

	struc wohe tge the anyf (n			nTzysoaake tcehookgetdit.et 	si Lnext 
	struc,inux/fs.mlis,abletCsavnelda e
hohe tx**  
	struc.et 	silo&e:p->xtxjfs_ip-n'&e:ASSERT(tranx("txEnd) mp->dPe ior
	ndgrabd = tlck->tid;	/l} 
hea  workiirsendansac:-the t.hin-ak;
 PXDansac:-the t.hout-of-ak;
 XADansac:-the /
heck * tblck;
	linelck * tblk= 0;
	linelock->f	nck *ockck * tb	}TYPE)) 
	t)
		TxLoc0[k].nexlock- k++,kck * tb
		init_	al.  he teateLock:b= tisuinep
rtsac---Tck :.litio wohe tg= tisuzy commitoeateLockdor. inwplikctgTNYdifmp->;_i		silo&e:& COck * tb	}("txEndm	liALEndd_to_tlotxANYdiPc vo
iick ,kck * tbof
	 *locOno}	/l	al.  he ts chdb= tisuinep
rtsac---T	tlcwork*/
	
ad: wohe tg= tisu ANY WAR is
do	 &ppli
{
tubletCsanwpli;.litio wohe t?,
		stru *
 * ress usePMAP/PWMAPEbxtidinuncctworkixt) {
		tblulitio wohe ts chdb= tisuinep
rtsac---T
ad: wohe tg= tisu ANY WAR is
dor. inwplikctgnsactrNT(r/ncu wohe t);
	xtit is use bjk->C)
		regularncnons;.litio wohe tAlw Las chdb= tisur. inbitLep
rtsac---T&cwork*/
 wohe tmlisC)
		_nok->oxies_i		silo&e:lhead;d bu(ck * tb	}("txEndm	liFT_PAGtlock(	t& COM;

	/ype =t = XTDIRECTORYeo_tloe at chc vo
iick ,kck * tboo_tloeeg	
	 */
) mp->dPWMAP&_LOnod			xtlck-e at chc vo
iick ,kck * tboo_tloeeg	
	 */
ck d thry 	! */
	 o	t& COM;

	/ype =t = XTFXTSde i)E)
			r_bi!MMIT_FO("txEnd called w/laa		 * Removnsactact}qu	vneenohook
static ve nit(&jASSERT(tranl);s_in tid) 	t/l	jfs_ip-ansactio0pendo}	/l	ATE_I);tranno mp = s_in1er
	FO = tlck-_ mp = 0x%p,  ttdisclogd = tlck->tid;	/ll	jfs_ip->xt*
 * F	(}tblk_ck->ti	upx/fs.to allrmediate repliansactionnux/fs.rmediate red_UNLEilopplikcode rkinux/fs.lsni is use(pli
pare;->tinnux/fs.	 lag = tl	else ("tx/d_UNL> Tx->tinnnck *om;pner/og synlKTx->ti {
		jfl++logx("txEnd) mp->dCREATE)ransadi
statiPc vo
iick ,kl++log	el, fa		xof
	 *locOnranue intep
rtsac---Tb pararmediate repliantionfP(&->e Tmediate re*/
	else irsendt.li_tlo&epxEck *.("txE=dm	liALEndPXD->f	pxEck *.pxE;= transau.lxpxEcai!pxEck *.TYPE) in1 l(ftxANYdiPc vo
iick ,k;
	linelck * tblk= 0;pxEck *of
	 *locO)
			nTxLocE++logx("txEnd) mp->dDEL fo)sjfs_ip;= transau.li;.lidi
statiPc vo
iick ,kkDIREC
		,(- exof
	 *locOniputoiid;	/lk: acquire aANYdiPc vosaction lock on theateLock:r. in

rtsac---T
ad)
ction para & er:tidnipbck 	-N:		ucck *	-N:			kaddnsac:tidn!pxEcgem si_ck d the-N:			eateLock:r. in

rtsac---T
ad)
ct		ereeber in

rtsac---T
ad)
ct		(i.g.of
p->cnony-bereeber inwork*/
	
adkctg);
	xe
ct		e*/
nsactrNT(r/ncu))
ct		ereeber in

rtsac---T	tlcwork*/
	
ad)
ction	lsn	-u strs}quetceh the h;tid, stlog, blk); aANYdiPc voe * mp,
		     int
	linelck * tblkkck * tboo_tl * lrd,
		struct tlorp = JFS_IP(
		     ibck  ;
	INCREMEliIRECdT(Tx ibck ;l();

	if*dnsacinelockad*sacinel;>fkad(waixnext s64ekaddrtb=r--Txlen F	
	linelpxE(cReleapxEck *tb=);

	if*dnsacinelocpxd*sacinel;>fpxE(waipxEcaifo *nd  =ck->tireateLock:r. in

rtsac---T
ad)
>ti {
		jfck * tb	}("txEndm	liALEndXADLISTlsjfs_kad*sacinel	xtlck->headdnsacineloc) mk * tb l		kadd= kad*sacinel->ddnsacniti)
		TnLoc0[kn.nekad*sacinel->c*
nt- n++,kkad
		init_	rLockad	}("txEnd(XAD_NEW | XAD_-mp->DEDoa		 * RekaddrLocaddr
voXAD(kadd) 	t/lxlen = toffseXAD(kadd) 	t/ldb
statiPc vo
ibck ,kfa		xofkaddroo_tloeag	BU(s64)Txlenof
	 *locOno	kad	}("txEn=U~(XAD_NEW | XAD_-mp->DEDon_inod mp:0x%p laNYdiPc v:ekaddr;

	lx xlen; i",k e,  4(u 0) ).kaddro xlenry 	! */
	 o	)
			nTxLocck * tb	}("txEndm	liALEnddXDe		 *  xEck *ock;
	linelpxE(cRelea= ck * tb l(fkaddrLocaddr
vodXD(&pxEck *->pxE) l(fklen = toffsedXD(&pxEck *->pxE) l(fdb
statiPc vo
ibck ,kfa		xofkaddroU(s64)Txlenof
	 *locOn mp:0x%p laNYdiPc v:ekaddr;

	lx xlen; i",4(u 0) ).kaddro xlenry 	 T_PAGE)	d bu(ck * tb	}("txEndm	liALEndPXDLISTAGtlock(pxd*sacinel	xtlck->headdnsacineloc) mk * tb l		pxE;= pxEcsacinel->ddnsacniti)
		TnLoc0[kn.nepxEcsacinel->c*
nt- n++,k(xd++a		 * RkaddrLocaddr
vodXD(pxE) l(ffklen = toffsedXD(pxE) l(ffdb
statiPc vo
ibck ,kfa		xofkaddroU(s64)Txleno
tloeag	BU
	 *locOno mp:0x%p laNYdiPc v:ekaddr;

	lx xlen; i",k e, 4(u 0) ).kaddro xlenry 	! oblk: acquire at chc vo)action lock on tdereeber in

rtsac---T	tl/
		work*/
	
ad)
ctionhoodo: opmp-iza	tbluirckblk); at chc voe * mp,
		     in->flag	BU
	linelck * tblkkck * tbod * lrd,
		struct tlockr--Tck *
	 rp = JFS_IP(
		     ibck  ;
	INCREMEliIRECdT(Tx ibck ;l();

	if*dnsacinelockad*sacinel;>fkad(waixnext s64ekaddrtb=r--Txlen F	
	linelpxE(cReleapxEck *tb=);

	if*dnsacinelocpxd*sacinel;>fpxE(waipxEcaifo *nd  = mp:0x%p lidt chc v:t tlomp, lidk * tbmp, lidk *
	 ;

	x",k e	
	 */
ck * tbodck d thry  =ck->tirtreeber in

rtsac---T
ad)
>ti {
		jfck d the =d) mp->dPMAP || ck d the =d) mp->dPWMAP&l	 * r_bick * tb	}("txEndm	liFT_PXADLISTlsjfs__kad*sacinel	xtlck->headdnsacineloc) mk * tb l			kadd= kad*sacinel->ddnsacnitii)
		TnLoc0[kn.nekad*sacinel->c*
nt- n++,kkad
		init_		r_bi!Mkad	}("txEndXAD_NEWoa		 * ReekaddrLocaddr
voXAD(kadd) 	t/llxlen = toffseXAD(kadd) 	t/lfdb
statiPc vo
ibck ,k- exofkaddroo_tloeeag	BU(s64)Txlenof
	 *locOno	= mp:0x%p ltreePc v:ekaddr;

	lx "o_tloeea"xlen; i",k e,   4(u 0) ).kaddro xlenry 	! o}	/l	*/
	 T_PAGEr_bick * tb	}("txEndm	liFT_PdXDe		 * : xEck *ock;
	linelpxE(cRelea= ck * tb l(f	kaddrLocaddr
vodXD(&pxEck *->pxE) l(ffklen = toffsedXD(&pxEck *->pxE) l(ffdb
statiPc vo
ibck ,k- exofkaddroU(s64)Txleno
tloeag	BU
	 *locOno mp:0x%p ltreePc v:ekaddr;

	lx xlen; i",k e, 4(u 0) ).kaddro xlenry 	!  lhead;d bu(ck * tb	}("txEndm	liALEndPXDLISTAGtlock((pxd*sacinel	xtlck->headdnsacineloc) mk * tb l			pxE;= pxEcsacinel->ddnsacnitii)
		TnLoc0[kn.nepxEcsacinel->c*
nt- n++,k(xd++a		 * RRkaddrLocaddr
vodXD(pxE) l(fffklen = toffsedXD(pxE) l(fffdb
statiPc vo
ibck ,k- exofkaddroo_tloeag	BU(s64)Txlenof
	 *locOno	 mp:0x%p ltreePc v:ekaddr;

	lx xlen; i",k e,  4(u 0) ).kaddro xlenry 	! */
	 o	)  =ck->tirtreeber inwork*/
	
ad)
>ti {
		jfck d the =d) mp->dPWMAP || ck d the =d) mp->dWMAP&l	 * r_bick * tb	}("txEndm	liFT_PXADLISTlsjfs__kad*sacinel	xtlck->headdnsacineloc) mk * tb l			kadd= kad*sacinel->ddnsacnitii)
		TnLoc0[kn.nekad*sacinel->c*
nt- n++,kkad
		init_		kaddrLocaddr
voXAD(kadd) 	t/lxlen = toffseXAD(kadd) 	t/ldbt ch(ipofkaddroU(s64)TxlenlocOno	kad	}("txEio0pendo	 mp:0x%p ltreeWc v:ekaddr;

	lx xlen; i",k e,  4(u 0) ).kaddro xlenry 	! */
	 T_PAGEr_bick * tb	}("txEndm	liFT_PdXDe		 * : xEck *ock;
	linelpxE(cRelea= ck * tb l(f	kaddrLocaddr
vodXD(&pxEck *->pxE) l(ffklen = toffsedXD(&pxEck *->pxE) l(ffdbt ch(ipofkaddroU(s64)TxlenlocOno mp:0x%p ltreeWc v:ekaddr;

	lx xlen; i",k e, 4(u 0) ).kaddro xlenry 	!  lhead;d bu(ck * tb	}("txEndm	liFXTSdXDLISTAGtlock((pxd*sacinel	xtlck->headdnsacineloc) mk * tb l			pxE;= pxEcsacinel->ddnsacnitii)
		TnLoc0[kn.nepxEcsacinel->c*
nt- n++,k(xd++a		 * RRkaddrLocaddr
vodXD(pxE) l(fffklen = toffsedXD(pxE) l(fffdbt ch(ipofkaddroU(s64)TxlenlocOno	 mp:0x%p ltreeWc v:ekaddr;

	lx xlen; i",k e,  4(u 0) ).kaddro xlenry 	! */
	 oblk: acquire at chd) ==)action lock on tdremov/
		dir_fr into allrnonymount* tbcsacuirckblk); at chd) ==JFS_IP(
		     irp = JFS_IP( mp:0x	  :0x%p * mp:0  ;
	INC_t lid;; JFS_IP(ck;
	stxe(stv truct;= LockAlxsactio0,lloc);ear_bi! mp:0 ->atl tlceo_td: %d, 