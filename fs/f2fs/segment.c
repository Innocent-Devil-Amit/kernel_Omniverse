/*
 * fs/f2fs/segment.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/prefetch.h>
#include <linux/kthread.h>
#include <linux/swap.h>
#include <linux/timer.h>
#include <linux/freezer.h>
#include <linux/sched.h>

#include "f2fs.h"
#include "segment.h"
#include "node.h"
#include "gc.h"
#include "trace.h"
#include <trace/events/f2fs.h>

#define __reverse_ffz(x) __reverse_ffs(~(x))

static struct kmem_cache *discard_entry_slab;
static struct kmem_cache *discard_cmd_slab;
static struct kmem_cache *sit_entry_set_slab;
static struct kmem_cache *inmem_entry_slab;

static unsigned long __reverse_ulong(unsigned char *str)
{
	unsigned long tmp = 0;
	int shift = 24, idx = 0;

#if BITS_PER_LONG == 64
	shift = 56;
#endif
	while (shift >= 0) {
		tmp |= (unsigned long)str[idx++] << shift;
		shift -= BITS_PER_BYTE;
	}
	return tmp;
}

/*
 * __reverse_ffs is copied from include/asm-generic/bitops/__ffs.h since
 * MSB and LSB are reversed in a byte by f2fs_set_bit.
 */
static inline unsigned long __reverse_ffs(unsigned long word)
{
	int num = 0;

#if BITS_PER_LONG == 64
	if ((word & 0xffffffff00000000UL) == 0)
		num += 32;
	else
		word >>= 32;
#endif
	if ((word & 0xffff0000) == 0)
		num += 16;
	else
		word >>= 16;

	if ((word & 0xff00) == 0)
		num += 8;
	else
		word >>= 8;

	if ((word & 0xf0) == 0)
		num += 4;
	else
		word >>= 4;

	if ((word & 0xc) == 0)
		num += 2;
	else
		word >>= 2;

	if ((word & 0x2) == 0)
		num += 1;
	return num;
}

/*
 * __find_rev_next(_zero)_bit is copied from lib/find_next_bit.c because
 * f2fs_set_bit makes MSB and LSB reversed in a byte.
 * @size must be integral times of unsigned long.
 * Example:
 *                             MSB <--> LSB
 *   f2fs_set_bit(0, bitmap) => 1000 0000
 *   f2fs_set_bit(7, bitmap) => 0000 0001
 */
static unsigned long __find_rev_next_bit(const unsigned long *addr,
			unsigned long size, unsigned long offset)
{
	const unsigned long *p = addr + BIT_WORD(offset);
	unsigned long result = size;
	unsigned long tmp;

	if (offset >= size)
		return size;

	size -= (offset & ~(BITS_PER_LONG - 1));
	offset %= BITS_PER_LONG;

	while (1) {
		if (*p == 0)
			goto pass;

		tmp = __reverse_ulong((unsigned char *)p);

		tmp &= ~0UL >> offset;
		if (size < BITS_PER_LONG)
			tmp &= (~0UL << (BITS_PER_LONG - size));
		if (tmp)
			goto found;
pass:
		if (size <= BITS_PER_LONG)
			break;
		size -= BITS_PER_LONG;
		offset = 0;
		p++;
	}
	return result;
found:
	return result - size + __reverse_ffs(tmp);
}

static unsigned long __find_rev_next_zero_bit(const unsigned long *addr,
			unsigned long size, unsigned long offset)
{
	const unsigned long *p = addr + BIT_WORD(offset);
	unsigned long result = size;
	unsigned long tmp;

	if (offset >= size)
		return size;

	size -= (offset & ~(BITS_PER_LONG - 1));
	offset %= BITS_PER_LONG;

	while (1) {
		if (*p == ~0UL)
			goto pass;

		tmp = __reverse_ulong((unsigned char *)p);

		if (offset)
			tmp |= ~0UL << (BITS_PER_LONG - offset);
		if (size < BITS_PER_LONG)
			tmp |= ~0UL >> size;
		if (tmp != ~0UL)
			goto found;
pass:
		if (size <= BITS_PER_LONG)
			break;
		size -= BITS_PER_LONG;
		offset = 0;
		p++;
	}
	return result;
found:
	return result - size + __reverse_ffz(tmp);
}

bool need_SSR(struct f2fs_sb_info *sbi)
{
	int node_secs = get_blocktype_secs(sbi, F2FS_DIRTY_NODES);
	int dent_secs = get_blocktype_secs(sbi, F2FS_DIRTY_DENTS);
	int imeta_secs = get_blocktype_secs(sbi, F2FS_DIRTY_IMETA);

	if (test_opt(sbi, LFS))
		return false;
	if (sbi->gc_thread && sbi->gc_thread->gc_urgent)
		return true;

	return free_sections(sbi) <= (node_secs + 2 * dent_secs + imeta_secs +
			SM_I(sbi)->min_ssr_sections + reserved_sections(sbi));
}

void register_inmem_page(struct inode *inode, struct page *page)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct inmem_pages *new;

	f2fs_trace_pid(page);

	set_page_private(page, (unsigned long)ATOMIC_WRITTEN_PAGE);
	SetPagePrivate(page);

	new = f2fs_kmem_cache_alloc(inmem_entry_slab, GFP_NOFS);

	/* add atomic page indices to the list */
	new->page = page;
	INIT_LIST_HEAD(&new->list);

	/* increase reference count with clean state */
	mutex_lock(&fi->inmem_lock);
	get_page(page);
	list_add_tail(&new->list, &fi->inmem_pages);
	spin_lock(&sbi->inode_lock[ATOMIC_FILE]);
	if (list_empty(&fi->inmem_ilist))
		list_add_tail(&fi->inmem_ilist, &sbi->inode_list[ATOMIC_FILE]);
	spin_unlock(&sbi->inode_lock[ATOMIC_FILE]);
	inc_page_count(F2FS_I_SB(inode), F2FS_INMEM_PAGES);
	mutex_unlock(&fi->inmem_lock);

	trace_f2fs_register_inmem_page(page, INMEM);
}

static int __revoke_inmem_pages(struct inode *inode,
				struct list_head *head, bool drop, bool recover)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct inmem_pages *cur, *tmp;
	int err = 0;

	list_for_each_entry_safe(cur, tmp, head, list) {
		struct page *page = cur->page;

		if (drop)
			trace_f2fs_commit_inmem_page(page, INMEM_DROP);

		lock_page(page);

		if (recover) {
			struct dnode_of_data dn;
			struct node_info ni;

			trace_f2fs_commit_inmem_page(page, INMEM_REVOKE);
retry:
			set_new_dnode(&dn, inode, NULL, NULL, 0);
			err = get_dnode_of_data(&dn, page->index, LOOKUP_NODE);
			if (err) {
				if (err == -ENOMEM) {
					congestion_wait(BLK_RW_ASYNC, HZ/50);
					cond_resched();
					goto retry;
				}
				err = -EAGAIN;
				goto next;
			}
			get_node_info(sbi, dn.nid, &ni);
			f2fs_replace_block(sbi, &dn, dn.data_blkaddr,
					cur->old_addr, ni.version, true, true);
			f2fs_put_dnode(&dn);
		}
next:
		/* we don't need to invalidate this in the sccessful status */
		if (drop || recover)
			ClearPageUptodate(page);
		set_page_private(page, 0);
		ClearPagePrivate(page);
		f2fs_put_page(page, 1);

		list_del(&cur->list);
		kmem_cache_free(inmem_entry_slab, cur);
		dec_page_count(F2FS_I_SB(inode), F2FS_INMEM_PAGES);
	}
	return err;
}

void drop_inmem_pages_all(struct f2fs_sb_info *sbi)
{
	struct list_head *head = &sbi->inode_list[ATOMIC_FILE];
	struct inode *inode;
	struct f2fs_inode_info *fi;
next:
	spin_lock(&sbi->inode_lock[ATOMIC_FILE]);
	if (list_empty(head)) {
		spin_unlock(&sbi->inode_lock[ATOMIC_FILE]);
		return;
	}
	fi = list_first_entry(head, struct f2fs_inode_info, inmem_ilist);
	inode = igrab(&fi->vfs_inode);
	spin_unlock(&sbi->inode_lock[ATOMIC_FILE]);

	if (inode) {
		drop_inmem_pages(inode);
		iput(inode);
	}
	congestion_wait(BLK_RW_ASYNC, HZ/50);
	cond_resched();
	goto next;
}

void drop_inmem_pages(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct f2fs_inode_info *fi = F2FS_I(inode);

	mutex_lock(&fi->inmem_lock);
	__revoke_inmem_pages(inode, &fi->inmem_pages, true, false);
	spin_lock(&sbi->inode_lock[ATOMIC_FILE]);
	if (!list_empty(&fi->inmem_ilist))
		list_del_init(&fi->inmem_ilist);
	spin_unlock(&sbi->inode_lock[ATOMIC_FILE]);
	mutex_unlock(&fi->inmem_lock);

	clear_inode_flag(inode, FI_ATOMIC_FILE);
	clear_inode_flag(inode, FI_HOT_DATA);
	stat_dec_atomic_write(inode);
}

void drop_inmem_page(struct inode *inode, struct page *page)
{
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct list_head *head = &fi->inmem_pages;
	struct inmem_pages *cur = NULL;

	f2fs_bug_on(sbi, !IS_ATOMIC_WRITTEN_PAGE(page));

	mutex_lock(&fi->inmem_lock);
	list_for_each_entry(cur, head, list) {
		if (cur->page == page)
			break;
	}

	f2fs_bug_on(sbi, !cur || cur->page != page);
	list_del(&cur->list);
	mutex_unlock(&fi->inmem_lock);

	dec_page_count(sbi, F2FS_INMEM_PAGES);
	kmem_cache_free(inmem_entry_slab, cur);

	ClearPageUptodate(page);
	set_page_private(page, 0);
	ClearPagePrivate(page);
	f2fs_put_page(page, 0);

	trace_f2fs_commit_inmem_page(page, INMEM_INVALIDATE);
}

static int __commit_inmem_pages(struct inode *inode,
					struct list_head *revoke_list)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct inmem_pages *cur, *tmp;
	struct f2fs_io_info fio = {
		.sbi = sbi,
		.ino = inode->i_ino,
		.type = DATA,
		.op = REQ_OP_WRITE,
		.op_flags = REQ_SYNC | REQ_PRIO,
		.io_type = FS_DATA_IO,
	};
	pgoff_t last_idx = ULONG_MAX;
	int err = 0;

	list_for_each_entry_safe(cur, tmp, &fi->inmem_pages, list) {
		struct page *page = cur->page;

		lock_page(page);
		if (page->mapping == inode->i_mapping) {
			trace_f2fs_commit_inmem_page(page, INMEM);

			set_page_dirty(page);
			f2fs_wait_on_page_writeback(page, DATA, true);
			if (clear_page_dirty_for_io(page)) {
				inode_dec_dirty_pages(inode);
				remove_dirty_inode(inode);
			}
retry:
			fio.page = page;
			fio.old_blkaddr = NULL_ADDR;
			fio.encrypted_page = NULL;
			fio.need_lock = LOCK_DONE;
			err = do_write_data_page(&fio);
			if (err) {
				if (err == -ENOMEM) {
					congestion_wait(BLK_RW_ASYNC, HZ/50);
					cond_resched();
					goto retry;
				}
				unlock_page(page);
				break;
			}
			/* record old blkaddr for revoking */
			cur->old_addr = fio.old_blkaddr;
			last_idx = page->index;
		}
		unlock_page(page);
		list_move_tail(&cur->list, revoke_list);
	}

	if (last_idx != ULONG_MAX)
		f2fs_submit_merged_write_cond(sbi, inode, 0, last_idx, DATA);

	if (!err)
		__revoke_inmem_pages(inode, revoke_list, false, false);

	return err;
}

int commit_inmem_pages(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct list_head revoke_list;
	int err;

	INIT_LIST_HEAD(&revoke_list);
	f2fs_balance_fs(sbi, true);
	f2fs_lock_op(sbi);

	set_inode_flag(inode, FI_ATOMIC_COMMIT);

	mutex_lock(&fi->inmem_lock);
	err = __commit_inmem_pages(inode, &revoke_list);
	if (err) {
		int ret;
		/*
		 * try to revoke all committed pages, but still we could fail
		 * due to no memory or other reason, if that happened, EAGAIN
		 * will be returned, which means in such case, transaction is
		 * already not integrity, caller should use journal to do the
		 * recovery or rewrite & commit last transaction. For other
		 * error number, revoking was done by filesystem itself.
		 */
		ret = __revoke_inmem_pages(inode, &revoke_list, false, true);
		if (ret)
			err = ret;

		/* drop all uncommitted pages */
		__revoke_inmem_pages(inode, &fi->inmem_pages, true, false);
	}
	spin_lock(&sbi->inode_lock[ATOMIC_FILE]);
	if (!list_empty(&fi->inmem_ilist))
		list_del_init(&fi->inmem_ilist);
	spin_unlock(&sbi->inode_lock[ATOMIC_FILE]);
	mutex_unlock(&fi->inmem_lock);

	clear_inode_flag(inode, FI_ATOMIC_COMMIT);

	f2fs_unlock_op(sbi);
	return err;
}

/*
 * This function balances dirty node and dentry pages.
 * In addition, it controls garbage collection.
 */
void f2fs_balance_fs(struct f2fs_sb_info *sbi, bool need)
{
#ifdef CONFIG_F2FS_FAULT_INJECTION
	if (time_to_inject(sbi, FAULT_CHECKPOINT)) {
		f2fs_show_injection_info(FAULT_CHECKPOINT);
		f2fs_stop_checkpoint(sbi, false);
	}
#endif

	/* balance_fs_bg is able to be pending */
	if (need && excess_cached_nats(sbi))
		f2fs_balance_fs_bg(sbi);

	/*
	 * We should do GC or end up with checkpoint, if there are so many dirty
	 * dir/node pages without enough free segments.
	 */
	if (has_not_enough_free_secs(sbi, 0, 0)) {
		mutex_lock(&sbi->gc_mutex);
		f2fs_gc(sbi, false, false, NULL_SEGNO);
	}
}

void f2fs_balance_fs_bg(struct f2fs_sb_info *sbi)
{
	/* try to shrink extent cache when there is no enough memory */
	if (!available_free_memory(sbi, EXTENT_CACHE))
		f2fs_shrink_extent_tree(sbi, EXTENT_CACHE_SHRINK_NUMBER);

	/* check the # of cached NAT entries */
	if (!available_free_memory(sbi, NAT_ENTRIES))
		try_to_free_nats(sbi, NAT_ENTRY_PER_BLOCK);

	if (!available_free_memory(sbi, FREE_NIDS))
		try_to_free_nids(sbi, MAX_FREE_NIDS);
	else
		build_free_nids(sbi, false, false);

	if (!is_idle(sbi) && !excess_dirty_nats(sbi))
		return;

	/* checkpoint is the only way to shrink partial cached entries */
	if (!available_free_memory(sbi, NAT_ENTRIES) ||
			!available_free_memory(sbi, INO_ENTRIES) ||
			excess_prefree_segs(sbi) ||
			excess_dirty_nats(sbi) ||
			f2fs_time_over(sbi, CP_TIME)) {
		if (test_opt(sbi, DATA_FLUSH)) {
			struct blk_plug plug;

			blk_start_plug(&plug);
			sync_dirty_inodes(sbi, FILE_INODE);
			blk_finish_plug(&plug);
		}
		f2fs_sync_fs(sbi->sb, true);
		stat_inc_bg_cp_count(sbi->stat_info);
	}
}

static int __submit_flush_wait(struct f2fs_sb_info *sbi,
				struct block_device *bdev)
{
	struct bio *bio = f2fs_bio_alloc(sbi, 0, true);
	int ret;

	bio->bi_rw = REQ_OP_WRITE;
	bio->bi_bdev = bdev;
	ret = submit_bio_wait(WRITE_FLUSH, bio);
	bio_put(bio);

	trace_f2fs_issue_flush(bdev, test_opt(sbi, NOBARRIER),
				test_opt(sbi, FLUSH_MERGE), ret);
	return ret;
}

static int submit_flush_wait(struct f2fs_sb_info *sbi, nid_t ino)
{
	int ret = 0;
	int i;

	if (!sbi->s_ndevs)
		return __submit_flush_wait(sbi, sbi->sb->s_bdev);

	for (i = 0; i < sbi->s_ndevs; i++) {
		if (!is_dirty_device(sbi, ino, i, FLUSH_INO))
			continue;
		ret = __submit_flush_wait(sbi, FDEV(i).bdev);
		if (ret)
			break;
	}
	return ret;
}

static int issue_flush_thread(void *data)
{
	struct f2fs_sb_info *sbi = data;
	struct flush_cmd_control *fcc = SM_I(sbi)->fcc_info;
	wait_queue_head_t *q = &fcc->flush_wait_queue;
repeat:
	if (kthread_should_stop())
		return 0;

	sb_start_intwrite(sbi->sb);

	if (!llist_empty(&fcc->issue_list)) {
		struct flush_cmd *cmd, *next;
		int ret;

		fcc->dispatch_list = llist_del_all(&fcc->issue_list);
		fcc->dispatch_list = llist_reverse_order(fcc->dispatch_list);

		cmd = llist_entry(fcc->dispatch_list, struct flush_cmd, llnode);

		ret = submit_flush_wait(sbi, cmd->ino);
		atomic_inc(&fcc->issued_flush);

		llist_for_each_entry_safe(cmd, next,
					  fcc->dispatch_list, llnode) {
			cmd->ret = ret;
			complete(&cmd->wait);
		}
		fcc->dispatch_list = NULL;
	}

	sb_end_intwrite(sbi->sb);

	wait_event_interruptible(*q,
		kthread_should_stop() || !llist_empty(&fcc->issue_list));
	goto repeat;
}

int f2fs_issue_flush(struct f2fs_sb_info *sbi, nid_t ino)
{
	struct flush_cmd_control *fcc = SM_I(sbi)->fcc_info;
	struct flush_cmd cmd;
	int ret;

	if (test_opt(sbi, NOBARRIER))
		return 0;

	if (!test_opt(sbi, FLUSH_MERGE)) {
		ret = submit_flush_wait(sbi, ino);
		atomic_inc(&fcc->issued_flush);
		return ret;
	}

	if (atomic_inc_return(&fcc->issing_flush) == 1 || sbi->s_ndevs > 1) {
		ret = submit_flush_wait(sbi, ino);
		atomic_dec(&fcc->issing_flush);

		atomic_inc(&fcc->issued_flush);
		return ret;
	}

	cmd.ino = ino;
	init_completion(&cmd.wait);

	llist_add(&cmd.llnode, &fcc->issue_list);

	/* update issue_list before we wake up issue_flush thread */
	smp_mb();

	if (waitqueue_active(&fcc->flush_wait_queue))
		wake_up(&fcc->flush_wait_queue);

	if (fcc->f2fs_issue_flush) {
		wait_for_completion(&cmd.wait);
		atomic_dec(&fcc->issing_flush);
	} else {
		struct llist_node *list;

		list = llist_del_all(&fcc->issue_list);
		if (!list) {
			wait_for_completion(&cmd.wait);
			atomic_dec(&fcc->issing_flush);
		} else {
			struct flush_cmd *tmp, *next;

			ret = submit_flush_wait(sbi, ino);

			llist_for_each_entry_safe(tmp, next, list, llnode) {
				if (tmp == &cmd) {
					cmd.ret = ret;
					atomic_dec(&fcc->issing_flush);
					continue;
				}
				tmp->ret = ret;
				complete(&tmp->wait);
			}
		}
	}

	return cmd.ret;
}

int create_flush_cmd_control(struct f2fs_sb_info *sbi)
{
	dev_t dev = sbi->sb->s_bdev->bd_dev;
	struct flush_cmd_control *fcc;
	int err = 0;

	if (SM_I(sbi)->fcc_info) {
		fcc = SM_I(sbi)->fcc_info;
		if (fcc->f2fs_issue_flush)
			return err;
		goto init_thread;
	}

	fcc = kzalloc(sizeof(struct flush_cmd_control), GFP_KERNEL);
	if (!fcc)
		return -ENOMEM;
	atomic_set(&fcc->issued_flush, 0);
	atomic_set(&fcc->issing_flush, 0);
	init_waitqueue_head(&fcc->flush_wait_queue);
	init_llist_head(&fcc->issue_list);
	SM_I(sbi)->fcc_info = fcc;
	if (!test_opt(sbi, FLUSH_MERGE))
		return err;

init_thread:
	fcc->f2fs_issue_flush = kthread_run(issue_flush_thread, sbi,
				"f2fs_flush-%u:%u", MAJOR(dev), MINOR(dev));
	if (IS_ERR(fcc->f2fs_issue_flush)) {
		err = PTR_ERR(fcc->f2fs_issue_flush);
		kfree(fcc);
		SM_I(sbi)->fcc_info = NULL;
		return err;
	}

	return err;
}

void destroy_flush_cmd_control(struct f2fs_sb_info *sbi, bool free)
{
	struct flush_cmd_control *fcc = SM_I(sbi)->fcc_info;

	if (fcc && fcc->f2fs_issue_flush) {
		struct task_struct *flush_thread = fcc->f2fs_issue_flush;

		fcc->f2fs_issue_flush = NULL;
		kthread_stop(flush_thread);
	}
	if (free) {
		kfree(fcc);
		SM_I(sbi)->fcc_info = NULL;
	}
}

int f2fs_flush_device_cache(struct f2fs_sb_info *sbi)
{
	int ret = 0, i;

	if (!sbi->s_ndevs)
		return 0;

	for (i = 1; i < sbi->s_ndevs; i++) {
		if (!f2fs_test_bit(i, (char *)&sbi->dirty_device))
			continue;
		ret = __submit_flush_wait(sbi, FDEV(i).bdev);
		if (ret)
			break;

		spin_lock(&sbi->dev_lock);
		f2fs_clear_bit(i, (char *)&sbi->dirty_device);
		spin_unlock(&sbi->dev_lock);
	}

	return ret;
}

static void __locate_dirty_segment(struct f2fs_sb_info *sbi, unsigned int segno,
		enum dirty_type dirty_type)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);

	/* need not be added */
	if (IS_CURSEG(sbi, segno))
		return;

	if (!test_and_set_bit(segno, dirty_i->dirty_segmap[dirty_type]))
		dirty_i->nr_dirty[dirty_type]++;

	if (dirty_type == DIRTY) {
		struct seg_entry *sentry = get_seg_entry(sbi, segno);
		enum dirty_type t = sentry->type;

		if (unlikely(t >= DIRTY)) {
			f2fs_bug_on(sbi, 1);
			return;
		}
		if (!test_and_set_bit(segno, dirty_i->dirty_segmap[t]))
			dirty_i->nr_dirty[t]++;
	}
}

static void __remove_dirty_segment(struct f2fs_sb_info *sbi, unsigned int segno,
		enum dirty_type dirty_type)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);

	if (test_and_clear_bit(segno, dirty_i->dirty_segmap[dirty_type]))
		dirty_i->nr_dirty[dirty_type]--;

	if (dirty_type == DIRTY) {
		struct seg_entry *sentry = get_seg_entry(sbi, segno);
		enum dirty_type t = sentry->type;

		if (test_and_clear_bit(segno, dirty_i->dirty_segmap[t]))
			dirty_i->nr_dirty[t]--;

		if (get_valid_blocks(sbi, segno, true) == 0)
			clear_bit(GET_SEC_FROM_SEG(sbi, segno),
						dirty_i->victim_secmap);
	}
}

/*
 * Should not occur error such as -ENOMEM.
 * Adding dirty entry into seglist is not critical operation.
 * If a given segment is one of current working segments, it won't be added.
 */
static void locate_dirty_segment(struct f2fs_sb_info *sbi, unsigned int segno)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	unsigned short valid_blocks;

	if (segno == NULL_SEGNO || IS_CURSEG(sbi, segno))
		return;

	mutex_lock(&dirty_i->seglist_lock);

	valid_blocks = get_valid_blocks(sbi, segno, false);

	if (valid_blocks == 0) {
		__locate_dirty_segment(sbi, segno, PRE);
		__remove_dirty_segment(sbi, segno, DIRTY);
	} else if (valid_blocks < sbi->blocks_per_seg) {
		__locate_dirty_segment(sbi, segno, DIRTY);
	} else {
		/* Recovery routine with SSR needs this */
		__remove_dirty_segment(sbi, segno, DIRTY);
	}

	mutex_unlock(&dirty_i->seglist_lock);
}

static struct discard_cmd *__create_discard_cmd(struct f2fs_sb_info *sbi,
		struct block_device *bdev, block_t lstart,
		block_t start, block_t len)
{
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;
	struct list_head *pend_list;
	struct discard_cmd *dc;

	f2fs_bug_on(sbi, !len);

	pend_list = &dcc->pend_list[plist_idx(len)];

	dc = f2fs_kmem_cache_alloc(discard_cmd_slab, GFP_NOFS);
	INIT_LIST_HEAD(&dc->list);
	dc->bdev = bdev;
	dc->lstart = lstart;
	dc->start = start;
	dc->len = len;
	dc->ref = 0;
	dc->state = D_PREP;
	dc->error = 0;
	init_completion(&dc->wait);
	list_add_tail(&dc->list, pend_list);
	atomic_inc(&dcc->discard_cmd_cnt);
	dcc->undiscard_blks += len;

	return dc;
}

static struct discard_cmd *__attach_discard_cmd(struct f2fs_sb_info *sbi,
				struct block_device *bdev, block_t lstart,
				block_t start, block_t len,
				struct rb_node *parent, struct rb_node **p)
{
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;
	struct discard_cmd *dc;

	dc = __create_discard_cmd(sbi, bdev, lstart, start, len);

	rb_link_node(&dc->rb_node, parent, p);
	rb_insert_color(&dc->rb_node, &dcc->root);

	return dc;
}

static void __detach_discard_cmd(struct discard_cmd_control *dcc,
							struct discard_cmd *dc)
{
	if (dc->state == D_DONE)
		atomic_dec(&dcc->issing_discard);

	list_del(&dc->list);
	rb_erase(&dc->rb_node, &dcc->root);
	dcc->undiscard_blks -= dc->len;

	kmem_cache_free(discard_cmd_slab, dc);

	atomic_dec(&dcc->discard_cmd_cnt);
}

static void __remove_discard_cmd(struct f2fs_sb_info *sbi,
							struct discard_cmd *dc)
{
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;

	trace_f2fs_remove_discard(dc->bdev, dc->start, dc->len);

	f2fs_bug_on(sbi, dc->ref);

	if (dc->error == -EOPNOTSUPP)
		dc->error = 0;

	if (dc->error)
		f2fs_msg(sbi->sb, KERN_INFO,
			"Issue discard(%u, %u, %u) failed, ret: %d",
			dc->lstart, dc->start, dc->len, dc->error);
	__detach_discard_cmd(dcc, dc);
}

static void f2fs_submit_discard_endio(struct bio *bio, int err)
{
	struct discard_cmd *dc = (struct discard_cmd *)bio->bi_private;

	dc->error = err;
	dc->state = D_DONE;
	complete_all(&dc->wait);
	bio_put(bio);
}

/* copied from block/blk-lib.c in 4.10-rc1 */
static int __blkdev_issue_discard(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, int flags,
		struct bio **biop)
{
	struct request_queue *q = bdev_get_queue(bdev);
	struct bio *bio = *biop;
	unsigned int granularity;
	int op = REQ_WRITE | REQ_DISCARD;
	int alignment;
	sector_t bs_mask;

	if (!q)
		return -ENXIO;

	if (!blk_queue_discard(q))
		return -EOPNOTSUPP;

	if (flags & BLKDEV_DISCARD_SECURE) {
		if (!blk_queue_secdiscard(q))
			return -EOPNOTSUPP;
		op |= REQ_SECURE;
	}

	bs_mask = (bdev_logical_block_size(bdev) >> 9) - 1;
	if ((sector | nr_sects) & bs_mask)
		return -EINVAL;

	/* Zero-sector (unknown) and one-sector granularities are the same.  */
	granularity = max(q->limits.discard_granularity >> 9, 1U);
	alignment = (bdev_discard_alignment(bdev) >> 9) % granularity;

	while (nr_sects) {
		unsigned int req_sects;
		sector_t end_sect, tmp;

		/* Make sure bi_size doesn't overflow */
		req_sects = min_t(sector_t, nr_sects, UINT_MAX >> 9);

		/**
		 * If splitting a request, and the next starting sector would be
		 * misaligned, stop the discard at the previous aligned sector.
		 */
		end_sect = sector + req_sects;
		tmp = end_sect;
		if (req_sects < nr_sects &&
		    sector_div(tmp, granularity) != alignment) {
			end_sect = end_sect - alignment;
			sector_div(end_sect, granularity);
			end_sect = end_sect * granularity + alignment;
			req_sects = end_sect - sector;
		}

		if (bio) {
			int ret = submit_bio_wait(op, bio);
			bio_put(bio);
			if (ret)
				return ret;
		}

		bio = bio_alloc(GFP_NOIO | __GFP_NOFAIL, 1);
		bio->bi_iter.bi_sector = sector;
		bio->bi_bdev = bdev;
		bio_set_op_attrs(bio, op, 0);

		bio->bi_iter.bi_size = req_sects << 9;
		nr_sects -= req_sects;
		sector = end_sect;

		/*
		 * We can loop for a long time in here, if someone does
		 * full device discards (like mkfs). Be nice and allow
		 * us to schedule out to avoid softlocking if preempt
		 * is disabled.
		 */
		cond_resched();
	}

	*biop = bio;
	return 0;
}

void __check_sit_bitmap(struct f2fs_sb_info *sbi,
				block_t start, block_t end)
{
#ifdef CONFIG_F2FS_CHECK_FS
	struct seg_entry *sentry;
	unsigned int segno;
	block_t blk = start;
	unsigned long offset, size, max_blocks = sbi->blocks_per_seg;
	unsigned long *map;

	while (blk < end) {
		segno = GET_SEGNO(sbi, blk);
		sentry = get_seg_entry(sbi, segno);
		offset = GET_BLKOFF_FROM_SEG0(sbi, blk);

		if (end < START_BLOCK(sbi, segno + 1))
			size = GET_BLKOFF_FROM_SEG0(sbi, end);
		else
			size = max_blocks;
		map = (unsigned long *)(sentry->cur_valid_map);
		offset = __find_rev_next_bit(map, size, offset);
		f2fs_bug_on(sbi, offset != size);
		blk = START_BLOCK(sbi, segno + 1);
	}
#endif
}

/* this function is copied from blkdev_issue_discard from block/blk-lib.c */
static void __submit_discard_cmd(struct f2fs_sb_info *sbi,
						struct discard_policy *dpolicy,
						struct discard_cmd *dc)
{
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;
	struct list_head *wait_list = (dpolicy->type == DPOLICY_FSTRIM) ?
					&(dcc->fstrim_list) : &(dcc->wait_list);
	struct bio *bio = NULL;
	int flag = dpolicy->sync ? REQ_SYNC : 0;

	if (dc->state != D_PREP)
		return;

	trace_f2fs_issue_discard(dc->bdev, dc->start, dc->len);

	dc->error = __blkdev_issue_discard(dc->bdev,
				SECTOR_FROM_BLOCK(dc->start),
				SECTOR_FROM_BLOCK(dc->len),
				GFP_NOFS, 0, &bio);
	if (!dc->error) {
		/* should keep before submission to avoid D_DONE right away */
		dc->state = D_SUBMIT;
		atomic_inc(&dcc->issued_discard);
		atomic_inc(&dcc->issing_discard);
		if (bio) {
			bio->bi_private = dc;
			bio->bi_end_io = f2fs_submit_discard_endio;
			submit_bio(flag, bio);
			list_move_tail(&dc->list, wait_list);
			__check_sit_bitmap(sbi, dc->start, dc->start + dc->len);

			f2fs_update_iostat(sbi, FS_DISCARD, 1);
		}
	} else {
		__remove_discard_cmd(sbi, dc);
	}
}

static struct discard_cmd *__insert_discard_tree(struct f2fs_sb_info *sbi,
				struct block_device *bdev, block_t lstart,
				block_t start, block_t len,
				struct rb_node **insert_p,
				struct rb_node *insert_parent)
{
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct discard_cmd *dc = NULL;

	if (insert_p && insert_parent) {
		parent = insert_parent;
		p = insert_p;
		goto do_insert;
	}

	p = __lookup_rb_tree_for_insert(sbi, &dcc->root, &parent, lstart);
do_insert:
	dc = __attach_discard_cmd(sbi, bdev, lstart, start, len, parent, p);
	if (!dc)
		return NULL;

	return dc;
}

static void __relocate_discard_cmd(struct discard_cmd_control *dcc,
						struct discard_cmd *dc)
{
	list_move_tail(&dc->list, &dcc->pend_list[plist_idx(dc->len)]);
}

static void __punch_discard_cmd(struct f2fs_sb_info *sbi,
				struct discard_cmd *dc, block_t blkaddr)
{
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;
	struct discard_info di = dc->di;
	bool modified = false;

	if (dc->state == D_DONE || dc->len == 1) {
		__remove_discard_cmd(sbi, dc);
		return;
	}

	dcc->undiscard_blks -= di.len;

	if (blkaddr > di.lstart) {
		dc->len = blkaddr - dc->lstart;
		dcc->undiscard_blks += dc->len;
		__relocate_discard_cmd(dcc, dc);
		modified = true;
	}

	if (blkaddr < di.lstart + di.len - 1) {
		if (modified) {
			__insert_discard_tree(sbi, dc->bdev, blkaddr + 1,
					di.start + blkaddr + 1 - di.lstart,
					di.lstart + di.len - 1 - blkaddr,
					NULL, NULL);
		} else {
			dc->lstart++;
			dc->len--;
			dc->start++;
			dcc->undiscard_blks += dc->len;
			__relocate_discard_cmd(dcc, dc);
		}
	}
}

static void __update_discard_tree_range(struct f2fs_sb_info *sbi,
				struct block_device *bdev, block_t lstart,
				block_t start, block_t len)
{
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;
	struct discard_cmd *prev_dc = NULL, *next_dc = NULL;
	struct discard_cmd *dc;
	struct discard_info di = {0};
	struct rb_node **insert_p = NULL, *insert_parent = NULL;
	block_t end = lstart + len;

	mutex_lock(&dcc->cmd_lock);

	dc = (struct discard_cmd *)__lookup_rb_tree_ret(&dcc->root,
					NULL, lstart,
					(struct rb_entry **)&prev_dc,
					(struct rb_entry **)&next_dc,
					&insert_p, &insert_parent, true);
	if (dc)
		prev_dc = dc;

	if (!prev_dc) {
		di.lstart = lstart;
		di.len = next_dc ? next_dc->lstart - lstart : len;
		di.len = min(di.len, len);
		di.start = start;
	}

	while (1) {
		struct rb_node *node;
		bool merged = false;
		struct discard_cmd *tdc = NULL;

		if (prev_dc) {
			di.lstart = prev_dc->lstart + prev_dc->len;
			if (di.lstart < lstart)
				di.lstart = lstart;
			if (di.lstart >= end)
				break;

			if (!next_dc || next_dc->lstart > end)
				di.len = end - di.lstart;
			else
				di.len = next_dc->lstart - di.lstart;
			di.start = start + di.lstart - lstart;
		}

		if (!di.len)
			goto next;

		if (prev_dc && prev_dc->state == D_PREP &&
			prev_dc->bdev == bdev &&
			__is_discard_back_mergeable(&di, &prev_dc->di)) {
			prev_dc->di.len += di.len;
			dcc->undiscard_blks += di.len;
			__relocate_discard_cmd(dcc, prev_dc);
			di = prev_dc->di;
			tdc = prev_dc;
			merged = true;
		}

		if (next_dc && next_dc->state == D_PREP &&
			next_dc->bdev == bdev &&
			__is_discard_front_mergeable(&di, &next_dc->di)) {
			next_dc->di.lstart = di.lstart;
			next_dc->di.len += di.len;
			next_dc->di.start = di.start;
			dcc->undiscard_blks += di.len;
			__relocate_discard_cmd(dcc, next_dc);
			if (tdc)
				__remove_discard_cmd(sbi, tdc);
			merged = true;
		}

		if (!merged) {
			__insert_discard_tree(sbi, bdev, di.lstart, di.start,
							di.len, NULL, NULL);
		}
 next:
		prev_dc = next_dc;
		if (!prev_dc)
			break;

		node = rb_next(&prev_dc->rb_node);
		next_dc = rb_entry_safe(node, struct discard_cmd, rb_node);
	}

	mutex_unlock(&dcc->cmd_lock);
}

static int __queue_discard_cmd(struct f2fs_sb_info *sbi,
		struct block_device *bdev, block_t blkstart, block_t blklen)
{
	block_t lblkstart = blkstart;

	trace_f2fs_queue_discard(bdev, blkstart, blklen);

	if (sbi->s_ndevs) {
		int devi = f2fs_target_device_index(sbi, blkstart);

		blkstart -= FDEV(devi).start_blk;
	}
	__update_discard_tree_range(sbi, bdev, lblkstart, blkstart, blklen);
	return 0;
}

static void __issue_discard_cmd_range(struct f2fs_sb_info *sbi,
					struct discard_policy *dpolicy,
					unsigned int start, unsigned int end)
{
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;
	struct discard_cmd *prev_dc = NULL, *next_dc = NULL;
	struct rb_node **insert_p = NULL, *insert_parent = NULL;
	struct discard_cmd *dc;
	struct blk_plug plug;
	int issued;

next:
	issued = 0;

	mutex_lock(&dcc->cmd_lock);
	f2fs_bug_on(sbi, !__check_rb_tree_consistence(sbi, &dcc->root));

	dc = (struct discard_cmd *)__lookup_rb_tree_ret(&dcc->root,
					NULL, start,
					(struct rb_entry **)&prev_dc,
					(struct rb_entry **)&next_dc,
					&insert_p, &insert_parent, true);
	if (!dc)
		dc = next_dc;

	blk_start_plug(&plug);

	while (dc && dc->lstart <= end) {
		struct rb_node *node;

		if (dc->len < dpolicy->granularity)
			goto skip;

		if (dc->state != D_PREP) {
			list_move_tail(&dc->list, &dcc->fstrim_list);
			goto skip;
		}

		__submit_discard_cmd(sbi, dpolicy, dc);

		if (++issued >= dpolicy->max_requests) {
			start = dc->lstart + dc->len;

			blk_finish_plug(&plug);
			mutex_unlock(&dcc->cmd_lock);

			schedule();

			goto next;
		}
skip:
		node = rb_next(&dc->rb_node);
		dc = rb_entry_safe(node, struct discard_cmd, rb_node);

		if (fatal_signal_pending(current))
			break;
	}

	blk_finish_plug(&plug);
	mutex_unlock(&dcc->cmd_lock);
}

static int __issue_discard_cmd(struct f2fs_sb_info *sbi,
					struct discard_policy *dpolicy)
{
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;
	struct list_head *pend_list;
	struct discard_cmd *dc, *tmp;
	struct blk_plug plug;
	int i, iter = 0, issued = 0;
	bool io_interrupted = false;

	for (i = MAX_PLIST_NUM - 1; i >= 0; i--) {
		if (i + 1 < dpolicy->granularity)
			break;
		pend_list = &dcc->pend_list[i];

		mutex_lock(&dcc->cmd_lock);
		f2fs_bug_on(sbi, !__check_rb_tree_consistence(sbi, &dcc->root));
		blk_start_plug(&plug);
		list_for_each_entry_safe(dc, tmp, pend_list, list) {
			f2fs_bug_on(sbi, dc->state != D_PREP);

			if (dpolicy->io_aware && i < dpolicy->io_aware_gran &&
								!is_idle(sbi)) {
				io_interrupted = true;
				goto skip;
			}

			__submit_discard_cmd(sbi, dpolicy, dc);
			issued++;
skip:
			if (++iter >= dpolicy->max_requests)
				break;
		}
		blk_finish_plug(&plug);
		mutex_unlock(&dcc->cmd_lock);

		if (iter >= dpolicy->max_requests)
			break;
	}

	if (!issued && io_interrupted)
		issued = -1;

	return issued;
}

static bool __drop_discard_cmd(struct f2fs_sb_info *sbi)
{
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;
	struct list_head *pend_list;
	struct discard_cmd *dc, *tmp;
	int i;
	bool dropped = false;

	mutex_lock(&dcc->cmd_lock);
	for (i = MAX_PLIST_NUM - 1; i >= 0; i--) {
		pend_list = &dcc->pend_list[i];
		list_for_each_entry_safe(dc, tmp, pend_list, list) {
			f2fs_bug_on(sbi, dc->state != D_PREP);
			__remove_discard_cmd(sbi, dc);
			dropped = true;
		}
	}
	mutex_unlock(&dcc->cmd_lock);

	return dropped;
}

static unsigned int __wait_one_discard_bio(struct f2fs_sb_info *sbi,
							struct discard_cmd *dc)
{
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;
	unsigned int len = 0;

	wait_for_completion_io(&dc->wait);
	mutex_lock(&dcc->cmd_lock);
	f2fs_bug_on(sbi, dc->state != D_DONE);
	dc->ref--;
	if (!dc->ref) {
		if (!dc->error)
			len = dc->len;
		__remove_discard_cmd(sbi, dc);
	}
	mutex_unlock(&dcc->cmd_lock);

	return len;
}

static unsigned int __wait_discard_cmd_range(struct f2fs_sb_info *sbi,
						struct discard_policy *dpolicy,
						block_t start, block_t end)
{
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;
	struct list_head *wait_list = (dpolicy->type == DPOLICY_FSTRIM) ?
					&(dcc->fstrim_list) : &(dcc->wait_list);
	struct discard_cmd *dc, *tmp;
	bool need_wait;
	unsigned int trimmed = 0;

next:
	need_wait = false;

	mutex_lock(&dcc->cmd_lock);
	list_for_each_entry_safe(dc, tmp, wait_list, list) {
		if (dc->lstart + dc->len <= start || end <= dc->lstart)
			continue;
		if (dc->len < dpolicy->granularity)
			continue;
		if (dc->state == D_DONE && !dc->ref) {
			wait_for_completion_io(&dc->wait);
			if (!dc->error)
				trimmed += dc->len;
			__remove_discard_cmd(sbi, dc);
		} else {
			dc->ref++;
			need_wait = true;
			break;
		}
	}
	mutex_unlock(&dcc->cmd_lock);

	if (need_wait) {
		trimmed += __wait_one_discard_bio(sbi, dc);
		goto next;
	}

	return trimmed;
}

static void __wait_all_discard_cmd(struct f2fs_sb_info *sbi,
						struct discard_policy *dpolicy)
{
	__wait_discard_cmd_range(sbi, dpolicy, 0, UINT_MAX);
}

/* This should be covered by global mutex, &sit_i->sentry_lock */
void f2fs_wait_discard_bio(struct f2fs_sb_info *sbi, block_t blkaddr)
{
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;
	struct discard_cmd *dc;
	bool need_wait = false;

	mutex_lock(&dcc->cmd_lock);
	dc = (struct discard_cmd *)__lookup_rb_tree(&dcc->root, NULL, blkaddr);
	if (dc) {
		if (dc->state == D_PREP) {
			__punch_discard_cmd(sbi, dc, blkaddr);
		} else {
			dc->ref++;
			need_wait = true;
		}
	}
	mutex_unlock(&dcc->cmd_lock);

	if (need_wait)
		__wait_one_discard_bio(sbi, dc);
}

void stop_discard_thread(struct f2fs_sb_info *sbi)
{
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;

	if (dcc && dcc->f2fs_issue_discard) {
		struct task_struct *discard_thread = dcc->f2fs_issue_discard;

		dcc->f2fs_issue_discard = NULL;
		kthread_stop(discard_thread);
	}
}

/* This comes from f2fs_put_super */
bool f2fs_wait_discard_bios(struct f2fs_sb_info *sbi)
{
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;
	struct discard_policy dpolicy;
	bool dropped;

	init_discard_policy(&dpolicy, DPOLICY_UMOUNT, dcc->discard_granularity);
	__issue_discard_cmd(sbi, &dpolicy);
	dropped = __drop_discard_cmd(sbi);
	__wait_all_discard_cmd(sbi, &dpolicy);

	return dropped;
}

static int issue_discard_thread(void *data)
{
	struct f2fs_sb_info *sbi = data;
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;
	wait_queue_head_t *q = &dcc->discard_wait_queue;
	struct discard_policy dpolicy;
	unsigned int wait_ms = DEF_MIN_DISCARD_ISSUE_TIME;
	int issued;

	set_freezable();

	do {
		init_discard_policy(&dpolicy, DPOLICY_BG,
					dcc->discard_granularity);

		wait_event_interruptible_timeout(*q,
				kthread_should_stop() || freezing(current) ||
				dcc->discard_wake,
				msecs_to_jiffies(wait_ms));
		if (try_to_freeze())
			continue;
		if (kthread_should_stop())
			return 0;

		if (dcc->discard_wake) {
			dcc->discard_wake = 0;
			if (sbi->gc_thread && sbi->gc_thread->gc_urgent)
				init_discard_policy(&dpolicy,
							DPOLICY_FORCE, 1);
		}

		sb_start_intwrite(sbi->sb);

		issued = __issue_discard_cmd(sbi, &dpolicy);
		if (issued) {
			__wait_all_discard_cmd(sbi, &dpolicy);
			wait_ms = dpolicy.min_interval;
		} else {
			wait_ms = dpolicy.max_interval;
		}

		sb_end_intwrite(sbi->sb);

	} while (!kthread_should_stop());
	return 0;
}

#ifdef CONFIG_BLK_DEV_ZONED
static int __f2fs_issue_discard_zone(struct f2fs_sb_info *sbi,
		struct block_device *bdev, block_t blkstart, block_t blklen)
{
	sector_t sector, nr_sects;
	block_t lblkstart = blkstart;
	int devi = 0;

	if (sbi->s_ndevs) {
		devi = f2fs_target_device_index(sbi, blkstart);
		blkstart -= FDEV(devi).start_blk;
	}

	/*
	 * We need to know the type of the zone: for conventional zones,
	 * use regular discard if the drive supports it. For sequential
	 * zones, reset the zone write pointer.
	 */
	switch (get_blkz_type(sbi, bdev, blkstart)) {

	case BLK_ZONE_TYPE_CONVENTIONAL:
		if (!blk_queue_discard(bdev_get_queue(bdev)))
			return 0;
		return __queue_discard_cmd(sbi, bdev, lblkstart, blklen);
	case BLK_ZONE_TYPE_SEQWRITE_REQ:
	case BLK_ZONE_TYPE_SEQWRITE_PREF:
		sector = SECTOR_FROM_BLOCK(blkstart);
		nr_sects = SECTOR_FROM_BLOCK(blklen);

		if (sector & (bdev_zone_sectors(bdev) - 1) ||
				nr_sects != bdev_zone_sectors(bdev)) {
			f2fs_msg(sbi->sb, KERN_INFO,
				"(%d) %s: Unaligned discard attempted (block %x + %x)",
				devi, sbi->s_ndevs ? FDEV(devi).path: "",
				blkstart, blklen);
			return -EIO;
		}
		trace_f2fs_issue_reset_zone(bdev, blkstart);
		return blkdev_reset_zones(bdev, sector,
					  nr_sects, GFP_NOFS);
	default:
		/* Unknown zone type: broken device ? */
		return -EIO;
	}
}
#endif

static int __issue_discard_async(struct f2fs_sb_info *sbi,
		struct block_device *bdev, block_t blkstart, block_t blklen)
{
#ifdef CONFIG_BLK_DEV_ZONED
	if (f2fs_sb_mounted_blkzoned(sbi->sb) &&
				bdev_zoned_model(bdev) != BLK_ZONED_NONE)
		return __f2fs_issue_discard_zone(sbi, bdev, blkstart, blklen);
#endif
	return __queue_discard_cmd(sbi, bdev, blkstart, blklen);
}

static int f2fs_issue_discard(struct f2fs_sb_info *sbi,
				block_t blkstart, block_t blklen)
{
	sector_t start = blkstart, len = 0;
	struct block_device *bdev;
	struct seg_entry *se;
	unsigned int offset;
	block_t i;
	int err = 0;

	bdev = f2fs_target_device(sbi, blkstart, NULL);

	for (i = blkstart; i < blkstart + blklen; i++, len++) {
		if (i != start) {
			struct block_device *bdev2 =
				f2fs_target_device(sbi, i, NULL);

			if (bdev2 != bdev) {
				err = __issue_discard_async(sbi, bdev,
						start, len);
				if (err)
					return err;
				bdev = bdev2;
				start = i;
				len = 0;
			}
		}

		se = get_seg_entry(sbi, GET_SEGNO(sbi, i));
		offset = GET_BLKOFF_FROM_SEG0(sbi, i);

		if (!f2fs_test_and_set_bit(offset, se->discard_map))
			sbi->discard_blks--;
	}

	if (len)
		err = __issue_discard_async(sbi, bdev, start, len);
	return err;
}

static bool add_discard_addrs(struct f2fs_sb_info *sbi, struct cp_control *cpc,
							bool check_only)
{
	int entries = SIT_VBLOCK_MAP_SIZE / sizeof(unsigned long);
	int max_blocks = sbi->blocks_per_seg;
	struct seg_entry *se = get_seg_entry(sbi, cpc->trim_start);
	unsigned long *cur_map = (unsigned long *)se->cur_valid_mapnsigned long *cur_mget_seg_entry(soted long *)se->cur_valid_mapnssoteed long *cur_mget_seg_entry			sbi->disong *)se->cur_valid_mapn			sbi->discur_mget_seg_entry	disong_SIZfo;
	wait(waiant granularity;
		len = 00,+ len;
ssuely)
{
artctry((t);
	t happsecCP_TIME;
	card_cmd *dc, *tmp;t_seg_edio.need_locad *wait_list = (dmem_page_info;
	wait_queue_->void f2i
	INIT_LIi== NULL_SE->= 0) {
		__locatbi->blocks_y(&fsbi, c, *tmp;t_o_intefs_issue_dc->state == D!artct>error)
			DATA_FLUSH)) {
IME;
	c_y(&fSE->= 0) {
		__los != b_info;
	wait_queue_->

		i *tmps >get_de_info;
	wait_queue_->bi->	i *tmps
		return __t discarneed tP_SIZE / sizeof(unsigred by glomownipl for d long);
	int max_bloEIO;
< sbi->s_ndevs; VBLOCK_(!f2fs_tarrty_i]_devrtctr? ~(soted l_i]_& ~			sbi->dis_i]_r >= 	(igned l_i]_^ (soted l_i]	ret(soted l_i]&& dc->lstavrtctry(&_info;
	wait_queue_->

		i *tmps <get_de_info;
	wait_queue_->bi->	i *tmps
sk_struen = 0t_bit(map, size, offdset);bi->blocks,+ lenf
}

/*NULL_S			breakbi->blocks	node = rb_next len;
t_bit(map, size,zero, offdset);bi->blocks,+; i++, l}

/*NULL_vrtctread- i++,ed_nlen!akbi->blocksstrim_l&BLOCK(-truct bl<rt);
	unsigminext;

		 (kthread_/*NULL_	int entries	return __mutex_ror)
			liscard_wae che_alloc(discard_cmd_slab, GFP_ tmp, womicheck_onl&bio)	struZERO= true;edcc->unL_ADDR;
			i, segno + 1);
	}t);
	unsigned long 	(&dc->list, pend_efstrim_lh This c_seg_e< sbi->s_start + blkOCK(!f2fs_taard_ se->d_lebi->d	struc)d))
			sbi->discx_ror_info;
	wait_queue_->

		i *tmps +art;
			ile (1) {
eturn __t discaiscard_trel hae(struct f2fs_sb_info *sbi, struct cp_cofs_sb_info *t_list = (dmem_page(_info;
	wait_queue_->void f2i
	card_cmd *dc, *tmp;t_seg_et_segneedhisctor (umittescarde_dirtentry_safe(dc, tmp, wait_t_segnedhis_lh Th (dc->lstartlist);
	rbid_map) skip;
		e(discard_cmd_slab, dc); tmp, womicglist is comes fr occur errocpagesy_for_i) ||
			et be  af->mathe only way tstemoid locate_dirty_s_ se_i) ||
	as_ ||
			et be b_info *sbi)
{
	struct discard_cmd_contfo *dirty_i = DIRTY_I(sbi);
	unsigned short valid_block_t blk =dirty_i->seglist_lock);

	valid_blocks	_safe(dc, dirty_i->dirty_segmap[t]))
			dirty_PRE]MINOIN

		So_intefs_rd_ seit(offsetf di.lstaret = GET_dirty_i->seglist_lock);
}

static struct rty_ssy_for_i) ||
			et be b_info *sbi, struct cp_control *cpc,
							bool scard_cmd_control *dcc = SM_I(sbi)->dcc_info;
	struct list_head *wait_list = (dmem_pageake =void f2i
	INI_cmd *dc, *tmp;t_seg_et_segneedhisctd_cmd_contfo *dirty_i = DIRTY_I(sbi);
	unsigned short valid_bl_entry_i) ||
	disong_segmap[t]))
			dirty_PRE]t granularity;
		len = 00,+ len;
ssuel valid_block_t cirtyc->unLt blk = s)
{
artctry((t);
	t happsecCP_TIME;
	cardirty_i->seglist_lock);

	valid_blocks =struct rb_noderopped =truen = 0bit(msize, off_i) ||
	disMINOIN

		So_int,+ lenf
}

/*NULL_S			breakNOIN

		So_intefs_
	mutex_un len;
bit(msize,zero, off_i) ||
	disMINOIN

		So_int,heck_onl&; i++, l}

/g_e< sbi->s_start + blkOCK(!f2fs_taachar *)&sbi->_i) ||
	dis

/g_erty[t]--;

		if (gPRE].stat;
			ile (1)ror)
			DATA_FLUSH)) {
IME;
	c;

		 (kthread_/*NULL_vrtctread- i++,eakt);
	unsigned ls_idle(sbLOCK(-trb_<akt);
	unsign = end - (kthread_/*NULL_	DATA_FLUSH)) {LFs_pre? FDEV(egruct segcemove_disca	card(struct f2fs_sb_)) {i, segno + 1);
	}
TOR_FROM_BLLOCK(-truct bl<per_seglog_) {
		__locate_;		if (kthread_sh}ait = fa	t cir i, blk);, segno),
						ded long 	c->unLt blk i, blk);G segno),C.lstaretc= GET_BULL_	 segno))C.lstaretc= Gs_idle(!ks(sbi, segno, false);
eturn c)
		)sca	card(struct f2fs_sb_)) {i, segno + 1);
	}
TOR__dirty_i->vic FDEV(egruct segce<per_seglog_) {
		__locate_;	 =truen = 0c->unLt blk +? FDEV(egruct segc
/*NULL_S			br<en = end 
skip:
		noden = next len;
;
		}

	1;ock(&dcc->cmd_lock);t_lock);

	valid_blocks = bef lensmpage	i *tmps dirtentry_safe(dc, tmp, wait_t_segnedhis_lh Th (dc->lstart valid_block_ignepos= 00,+size,pos p);
	itoding		}

		se =upted ssbi, s}
	mar *)&s_leb0cglist )
			sbi->discx_rbit(msizeueue_dis ssbi, s->di.lstart pos= 0bit(msize,zero, of_leblist )
			sbi->disdiscard__seg;
	struct seg_,_igneposong 	(&lstart - dpos=-_ignepos2 != bdev)d_blkzoned(sbi->sb) &&
				bdev_zos != b(tmp_vrtctread>granut);
	unsigminext; end -if (dc->state 	card(struct f2fs_sb_)) {list )
c->unL_ADDR;
	+_ignepos,heck_onl&	err)
				toding		}
rn dc;
}wait_ms = dpoltart pos= 0bit(msize, of_leblist )
			sbi->disdiscard__seg;
	struct seg_,_igneposong 	 = rb_nextignepos= 0tart pos
/*NUssbi, s}
	!Ussbi, sd_/*NULL_	gnepos=per_seg) {
		__locate_end 
skipbit(msized_/*Nlist);
	rbid_map) skip;
		ake =

		i *tmps statoding		};
		e(discard_cmd_slab, dc); tmp, womicglist is com
>flush_wd(struct f2fs_sb_se);

	iftruct rty_solicy(&dpolicy,
				_policy *dpolicy,
						block_t star
	if struct fype end)
{
	struct		continue;
	{t is tommppsy,
					irt== DPOLICY_FSTRf struct fype;rt== DPOLICC : 0
	mutex_ularity)
			continue;ry(swhile (nr_sect>= endtruct fypeTRIM) ?
				BG2fs_tarquests)
		} else {
	CARD_ISSUE_TIME;
	int issued;

		equests)
			belse {
	CARD_ISSAX_TIME;
	int issued;

		equests)
			break;
	}CARD_ISSAX_TIME;
	iREQUEST

		equests)
&&
								!isM - 1; i >= 0; 

		equests)
&&
					0
	mutex_uid_blocks <ndtruct fypeTRIM) ?
				

		s2fs_tarquests)
		} else {
	CARD_ISSUE_TIME;
	int issued;

		equests)
			belse {
	CARD_ISSAX_TIME;
	int issued;

		equests)
			break;
	}CARD_ISSAX_TIME;
	iREQUEST

		equests)
&&
								!isM - 1; i >= 0; 

		equests)
&&
					0
	mutex_uid_blocks <ndtruct fypeTRIM) ?
					&(dcc->{
		equests)
			break;
	}CARD_ISSAX_TIME;
	iREQUEST

		equests)
&&
								!isM - 1; i >= 0; 

		equests)
&&
					0
	t discarnd_blocks <ndtruct fypeTRIM) ?
				iscard->{
		equests)
			break;
	}CARD_ISSAX_TIME;
	iREQUEST

		equests)
&&
								!isM - 1; i >= 0; 

		equests)
&&
					0
	t discarn f2fs_issue_dis_cmd(sbi, bdev, lst f2fs_sb_info *sbi)
{
	dev_t dev = sbi->sb->s_bdev->bd_dev;
	struct flush_cmd_contntrol *dcc = SM_I(sbi)->;

	bdev = f2fi->s_ndevs)_info;
	struct list= blkadd>dcc_info;
	struct list_heead;
	}

	fcc = kzalloc(dizeof(struct flush_cmd_contntrol *dcc = SM_I(s;
	if (!fcc)
		return dENOMEM;
	atomic_set(&f
granularity);

		wait_eveCARD_IAULT_TIME;
	iGRANULARITY;AD(&dc->list);
	dc->c->void f2i
	card< sbi->s_ndevs; - 1; i >= 0; 
!f2fs_ta(&dc->list);
	dc->c->		list_for_e);AD(&dc->list);
	dc->c->truct discard(&dc->list);
	dc->c->			goto skip;
	dcc->c}

	k);
	dc = (struct dc->issing_flscard);
		atomic_inset(&fcc->issing_flscard);
		if (bio) set(&fcc->issing_flscardntrol *dcc =  (!d0ct dise =

		i *tmps e = D_PRc->bi->	i *tmps akNOIN

		So_inte<per_seglog_) {
		__locate;rd_blks -= di.len;

	ie = D_PRc->kaddie RB_ROOTrd_policy(&fcc->flush_waiait_queue;
	struct discct d_info;
	struct listssue_d;	fcc->f2fs_issue_discard = NULL;
		kthreasue_flush_thread, (struct f2fs_sfs_flush-%u:%u", (struct(dev), MINOR(dev));
	if (IS_ERR(fcc->f2fs_issue_discard) {
		struct taR_ERR(fcc->f2fs_issue_discard) {
		struct ta;
		emd_slai)->fcc_info = NULd;
		return err;
	}

	return err;
}

void destroy_fcate_dirty_scmd_conti, bdev, lst f2fs_sb_info *sbi)
{
	dev_t dev = sb_cmd_control *dcc = SM_I(sbi)->dcc_info;

	if (dcc && dcc->f2 dENOMEM;
	atoble()read(struct f2fs_sb_t_and_cemd_slai)->fc_info = NULd;
		return err;
 __drop_discard_cmarbi, dcvoid f;t_lob_info *sbi, unsigned int segno)
{
	struct dirty_seglist_lock gned iock ong_SIZfo;
	w dcc->f2 _bit(offset, se->di->dirtylock */]))
			dBLOCK_c->staraR_ERR(lock */]))
			dBLOCK_it = tturn __t discarneedturn __mutex_ __wait_all_disca, se, dcvoid f, blksinfo *sbi, unsigned int s0;

nype iscardegno,
		enum dirty_enum 	__insertty_seglist_l get_seg_entry(sbi, cpc->trim_staret = GET_SE->Y_FSTRffype;rt{
			__insertfs_rdmarbi, dcvoid f;t_lob_staret = GET __wait_all_dis_tree_r, dcvoid b_info *sbi, block_t blkaddr)
{
	struct di_enum ;
	tty_seglist_l get_seg_entort valid_block_t blk,k_t i;
	in_entrock_new_v= (unsignscardexi
	INF2FS_CHECK_FS
	struct seg_entfalse;iafexi
	INFint __iEGNO(sbi, blk);
		sentry = e {
			
entry(sbi, GET_SEGNO(sbi,et = GET_new_v= (unsbdevE->= 0) {
		__lo+ ;
	ET_LKOFF_FROM_SEG0(sbi, blk);

		if (end e {
			
ei, dc->state != D(new_v= (unsb>> (d long);
	int maocks;te<pe3cts != bd(new_v= (unsb>er_seg) {
		__locate_)			
ent->= 0) {
		__loc_new_v= (unsignnt->mif soy(sbi,mif sed short_SIZfo;
	waibi->mif soy(nt->mif sks = beUtree_

	if dr)
{
 ->starRSEG(sbi,;
	b>ee_dirtyexi
	_device(st(offset, se->discard_map))
;
		offset = __fF2FS_CHECK_FS
	struct seg_ent	;iafexi
	_device(st(offset, se->discard_mheck_onp))
;
		offset = _;iaGET_BULL_DIRTY)) {exi
	_!akbiafexi
	sg(sbi->sb, KERN_INFO,
				"(%diss, "Ini, &dcc->t -ENOME"sh-%u:whone ot, and->star(end :, reoby git:tart, dc	ruct di_eexi
	s;bi->sb, K;
			return;
		}
	ic int ___BULL_DIRTY)) {exi
	sg(sbi->sb, KERN_INFO,
				"(%diss,sh-%u:B>starRwas wrently rd_mand :, "} else {
			dcbi, 1);
			return;
		}
		nt->= 0) {
		__lrt++;
		el

		se =ed) {
			sbi, c, *tmp;t_o_ints_idle(!set_bit(offset, se->discard_map))
			sbi->discard_blks--;
	}

	if (len)

one tdo/
		req_
	 */
bys */
pe omiss(&dc-chainEIO;
	ULL_SE->fypeTRIMgno))
_WARM_NODE = submiand_set_bit(offset, se->discard_map))
ssoteed long *; end -p))
ssoteed lon
		__lit = t__remove_discaexi
	_device(st(offsetchar *)&sbscard_map))
;
		offset = __fF2FS_CHECK_FS
	struct seg_ent	;iafexi
	_device(st(offsetchar *)&sbscard_mheck_onp))
;
		offset = _;iaGET_BULL_DIRTY)) {exi
	_!akbiafexi
	sg(sbi->sb, KERN_INFO,
				"(%diss, "Ini, &dcc->t -ENOME"sh-%u:whonechar  and->star(end :, reoby git:tart, dc	ruct di_eexi
	s;bi->sb, K;
			return;
		}
	ic int ___BULL_DIRTY)) {!exi
	sg(sbi->sb, KERN_INFO,
				"(%diss,sh-%u:B>starRwas wrently char iscand :, "} else {
			dcbi, 1);
			return;
		}
		nt->= 0) {
		__ldiscard_el

		se =ed) {
			sbi, c, *tmp;t_o_ints_idle(vice(st(offsetchar *)&sbscard_map))
			sbi->discard_blks--;
	}

	if (lic voidt_bit(i, (char *)&sbscard_map))
ssoteed long *; endp))
ssoteed lon
		__l		__r
	ET
_rdmarbi, dcvoid f;t_lob_staret = GET = be_tree_atodin numberfor 
	if dr)
{
out tbr.
	 *c->omeossot  */aEIO;
_SIZfo;
	wai
	 *c->eed lon
		__l		__r
	ET
_vs) {
		deegruct segce>;
	heeabi, GcT_SEGNO(sbi,et = G->= 0) {
		__lo+__r
	ETct rty_sol= 0) ee_rgno, falinfo *sbi, block_t blkaddr)
{
	stt discard valid_block_t blkbi, blk);
		sentrye {
			deglist_lock gned iock ong_SIZfo;
	w dcci, 1);
			return;DR;
		| IS_CUADDR(fcc->f2DR;
		| IEWUADDR(MEM;
	atoble( beDR;ddeds not i sbimeobuffs_wait	down_;

	} _lock */
void f2fs_w dcc_tree_r, dcvoid b_urn;DR;
, -1)ble( beDR;ddeds notto seg criticadirtegment(sbi, segno, DIRTY);
	} ew dcc_t_;

	} _lock */
void f2fs_w dct upted ssthe only w	atoatab_info *sbi, block_t blkaddr)
{
	struct discard_cmd_colock gned iock ong_SIZfo;
	w dt valid_block_t blk,k_t i;
	ineglist_l get_seg_entortupted sstp (dc->state == D_ADDR;
		| IEWUADDRpre?_ADDR;
		| IS_CUADDR(
return __mutex_rodown_fs_sb_lock */
void f2fs_w dccGNO(sbi, blk);
		sentry = e {
			entry(sbi, GET_SEGNO(sbi,et = GET_LKOFF_FROM_SEG0(sbi, blk);

		if (end e {
			
e
			sbi, har *)&sbscard_map))
ssoteed long *; end sstp (dmutex_roc->rs_sb_lock */
void f2fs_w dcc
}

statistp;mes fr occTs copied fromred by gloresid_blunds_w poi;
	 GETdcc->segmkid locate_dirty_s_>listsumcvoid b_info *sbi, block_t blkadd0;

nype iscard_info *sbi, bummarg_enumscard_cmd_co;
	 GETck_t b;
	 GE IMgno))
_I	mergedypeGET_	strucDR;
			;
	 GE*/
umc * We DR;
	+		;
	 GE*/size, lkscaoccflush_cmd_contsbi, bummargp;
	demcpy(DR;
, bum,cflush_cmd_contsbi, bummargp);mes fr occCalculee_atng sumberfor g segmenbummarg_pagut_s * m samngid loock_npaguty_safbummarg_flushb_info *sbi, block_t blkaddr)
{
art_raes = SIT_ed lon
umccd(sb pletion( issu
umcin_pagulkstart; i < gno))
_HOT_DATAdevs;< gno))
_COLD_DATAdev!= start) {
{
		dssot->tructf, bl_i]_d=s */ard_bed lon
umccd(sb +per_seg;
	struct seg_ent	_ms = dpolULL_vrt_raes	d_bed lon
umccd(sb +pele16			ccpu(iscard	strucKPTo;
	wai;
		oata, lkscar_e);ADlen = next_ed lon
umccd(sb +pe;
	 GET lkscaf (!f2fs_tunlock(rd_umcin_pagury((PAGE(unsig- 2occuUM_JOURNAL(unsig-nextuUM_FOOTER(unsi) /cuUMMARY(unsifcc->f2ed lon
umccd(sb d <=umcin_pagu(
return __1;oc_blocks <2ed lon
umccd(sb -<=umcin_pagu( <get_(PAGE(unsig- uUM_FOOTER(unsi) /cuUMMARY(unsi(
return __ i;
turn __3;mes fr occCallerfred by pftlocoverummarg_paguid locanfo *pagur*sbi, umcpagub_info *sbi, unsigned int segno)
{
	struct dirty_sturn __sbi,meta,pagub_sbi, i));UMgno + 1);
	}
#end)truct rty_s_tree_rmeta,pagub_info *sbi, unsigned int s	strucsraddr)
{
	struc_t discard_cmd_copagur*pagury(grab,meta,pagub_sbi,ruc_t disardiremcpy(pagu_t diess(pagu)	}
raddPAGE(unsi			entt,paguf;t_lobpagu);cci, 1)ool pagubpagun;
		} __wait_all_dis;

	}, umcpagub_info *sbi, unsigned int 
ard_info *sbi, bummargrgno, _enumT lkddr)
{
	struc_t discard_tree_rmeta,pagub_bi->d	struc)numT lkddr)c_t disar __wait_all_dis;

	},g segme, umcpagub_info *sbi, unsigned int 
ardtar
	iffype er)
{
	struc_t discard_cmd_co;
	 GETck_t b;
	 GE IMgno))
_I	mergedypeGET__cmd_copagur*pagury(grab,meta,pagub_sbi,ruc_t disard_info *sbi, bummargrgno, _enrc			;
	 GE*/
umc * We _info *sbi, bummargrgno, _edsed_/*d>type _info *sbi, bummargrgno, _e)pagu_t diess(pagu)ardirty_i->segli;
	 GE*/;
	 GETdcc->)x_rodown_fs_sb_;
	 GE*/jon _al_rwsemp;
	demcpy(&d>t*/jon _al, ;
	 GE*/jon _al,cuUM_JOURNAL(unsiw dt p_fs_sb_;
	 GE*/jon _al_rwsemp;

	demcpy(d>t*/dBLOCK_	}
ra*/dBLOCK_	}uUM_ENTRY(unsi(;
	demcpy(&d>t*/footerry_lra*/footerryuUM_FOOTER(unsi)ardirty_i-md_lock);
	 GE*/;
	 GETdcc->)x_rontt,paguf;t_lobpagu);cci, 1)ool pagubpagun;
		} __wait_aly way msize,egno, Dtf di.linfo *sbi, block_t blkadd0;

nypescard_cmd_co;
	 GETck_t b;
	 GE IMgno))
_I	mergedypeGET_ valid_block_t blkbi,;
	 GE*/
#endif
}We _info *s||
			etapTck_t bs||
	i < FREEZfo;
	w dcc->f2
#endi<kNOIN

		So_inteead-#endi%? FDEV(egruct segc(
return __!har *)&sbt blk,ks||
	i*/f||
			etap;
}

static0;mes fr occFius t_new egno, D_supertng f||
 egno, Ds ->starRnot		dc->ords_ occTs copied fromred by gloretati_blwitheruccess,k_tngrwiPE_SUGid locate_dirty_ssbi,new_egno, DIRinfo *sbi, unsigned int 
ard valid_block_*neweg_,_ = falsw_egc_enum ;iiscard_cmd_cos||
			etapTck_t bs||
	i < FREEZfo;
	w dt valid_block_t blk,kt cirty &&
nnt len = 0;

	waitodingctor, akNOIN

	CSo_inte/? FDEV(ecruct sctort len = 0;

	waihint i, blk);, segno),
						*neweg_w dt valid_block_olev, bl(sbi, blkRITE_segno),
						*neweg_w dt valid_block_left_ruen = 0hintortupted n
	}
	mutex_uruct	o_left pletion( isx_ronpin__lock)s||
	i*/		etapTeed_wait)
		_!lsw_egceead((*neweg_, l}
i%? FDEV(egruct segc(GET_SEGNO(sbi,bit(msize,zero, offs||
	i*/f||
			etap 
ard blk);G segno),C.lstarhint  l}
		*neweg_nf
}

/*NULL_S#endi<k blk);G segno),C.lstarhint  l}
_end 
skip
sk_i(1) {
bit(m_tngrv, bl:
	t cir i,bit(msize,zero, offs||
	i*/f||
			cdisMINOIN

	CSo_int,+hint(fcc->f2t cir >akNOIN

	CSo_intstate == D_i
		| ALo +_RIGHTblock_de cir i,bit(msize,zero, offs||
	i*/f||
			cdisMheck_onlNOIN

	CSo_int,+0			dcbi, 1);
			return;t cir >akNOIN

	CSo_ints;}wait_ms = dpol	o_left pl1;g 	(&lft_ruen = 0hint

	1;ocnlock(&== D	o_left pple)
 -if (dc->s_leftks =struct har *)&sb&lft_ruen ,ks||
	i*/f||
			ctaraR_ERR( = __ift_ruen =>ee_dirty(&lft_ruen rt++;
	 (kthread_sh}a	(&lft_ruen = 0bit(msize,zero, offs||
	i*/f||
			cdisMheck_onlNOIN

	CSo_int,+0			dci, 1);
			return;_ift_ruen =>akNOIN

	CSo_ints;}wa (!issued de cir i,_ift_ruen (++ite_left:ccGNO(sbi, blk);
 segno),C.lstaretc= GET_, bl(sbi, blkRITE_segno),C.lstaretc= GETe( begt. Fup rombit( and n_tngroken dSEG(sbi,! n
	)
 -if (d
sk_i(1) vs) {
		deecruct sctoremove_
 -if (d
sk_i(1) vs) , bl(sbi=_olev, bl(s_
 -if (d
sk_i(1) vs) _i
		| ALo +_LEFT>error)
				o_left ead, bl(sb>gra>=itodingctor,_end 
skip
sk_i(1) &== D	o_left ead, bl(sbpple)
 - 
skip
sk_i(1) {
d< sbi->s_ndevs; NR_gno))
_PREF
!f2fs_ta== Dgno))
_I	merge	waictoremov, bl(s_
 -e = rb_nex) {
		; NR_gno))
_PREFd keep bectore coiniscarn c)yd n_tngroIO;
	ULL_	o_left_
 -ehint i,, bl(sb*? FDEV(ecruct sctor

	1;ocn_blocks <, bl(sb>gra>=itodingctor,_end hint i,0;ocn_bloend hint i,<, bl(sb>gr)b*? FDEV(ecruct sctor1) &=nmutex_lock(&d 
skipbit(m_tngrv, bl1) {

sk_i(: = bef tddedastto seg cro, D_inif||
 egnoarRSEG(i, 1);
			return;har *)&sbt blk,ks||
	i*/f||
			etap;GET_ca, sehresi.lstaret = GET_*neweg_n=_t blk = npin_md_lock)s||
	i*/		etapTeed_wai __wait_all_disev, se;
	 GEb_info *sbi, block_t blkadd0;

nype enum 	__insertty_seglist_;
	 GETck_t b;
	 GE IMgno))
_I	mergedypeGET__cmd_cobummarg_footer_enumTfooter_nex;
	 GE*/
#endi		;
	 GE*/size,t blk = ;
	 GE*/ctorem, blkRITE_segno),
						;
	 GE*/
#end) = ;
	 GE*/size, lkscaoi,0;oc;
	 GE*/size,t blkurn errk);
		;(rd_umcfooter_age(;
	 GE*/
umc * */footer(;
	demng_f_umcfooter;
}

flush_cmd_contbummarg_footerR(fcc->f2fs_DATA),
	dypeG_endSi));UMgPREFf_umcfooter;
;UMgPREF_DATA(fcc->f2fs_NODE),
	dypeG_endSi));UMgPREFf_umcfooter;
;UMgPREF_NODE ET_ca, se, dcvoid f, blksergedype		;
	 GE*/
#end, 	__insertsigned int __wait_discard_csbi,neze,t blk.linfo *sbi, block_t blkadd0;

nypescard be->feegruct segce colbi, rtnan 1, w the type omisso		di useerval;
dSEG(sbi,{
		deegruct segce!ove_
 -
staticgno))
_I	mergedypeG*/
#end_nex) {
fypeTRIMgno))
_HOT_DATApre?fs_NODE),
	dypeG_endf (dcc->discevs)_SIZfo;
	wailar *vid fm[ALo +_NEXT]_endf (dcc-_SIZfo;
	wailar *vid fm[ALo +_NEXT]
}

staticgno))
_I	mergedypeG*/
#end_nes fr occAlegment t_g segmenwork and cro, D. occTs copied fromalways alegments a f||
 egno, D_iniLFS mannch (d locate_dirty_slsw_;
	 GEb_info *sbi, block_t blkadd0;

nype e = falsw_egcscard_cmd_co;
	 GETck_t b;
	 GE IMgno))
_I	mergedypeGET_ valid_block_t blkbi,;
	 GE*/
#endtion( i_i
		 ALo +_LEFTks =s

	}, umcpagub_				;
	 GE*/
umT lkdheck_ i));UMgno + 1);
	}
#end)trux) {
fypeTRIMgno))
_WARM_DATApre?fypeTRIMgno))
_COLD_DATAs_tari
		 ALo +_RIGHT_nex) {
fATA_FLUSH)) {NOHEAP)s_tari
		 ALo +_RIGHT_next blkbi,_csbi,neze,t blk.lergedypeGET_sbi,new_egno, DIR
			w
#end, lsw_egc_ediaGET_;
	 GE*/size,t blkurn
#endtioev, se;
	 GEb_ergedype		1GET_;
	 GE*/tructf, blurnLFSx_ __wait_all_discasize,s||
	 lkscaf info *sbi, unsigned int 
ard_info *;
	 GETck_t beg_,_ block_t end)tty_seglist_l get_seg_entry(sbi, cpc->trim_staret */
#end) = SIT_VBLOCK_MAP_SIZE / sizeof(unsigned long);
	int max_blocks valid_bl_entrysbi, i,disong_SIZfo;
	wait(waiant granularit_entry(soted long *)se->cur_valid_mapnssoteed long *cur_mget_seg_entryigned long *)se->cur_valid_mapnsigned long *curn( issupos2 !=< sbi->s_ndevs; VBLOCK_(!f2fs_tasbi, i,dis_i]_de(soted l_i]_| igned l_i]2 !=pos= 0t_bit(map, size,zero, offsbi, i,dis,er_seg;
	struct seg_		ded longext b*/size, lkscaoi,pos2 es fr occIf a egno, D_is.
	 *c->obyiLFS mannch,+sizedr)
{
 LKOFF_Fis.just obtai>cu occbyiin_cmd
		iw poi;
	egmenr)
{
 LKOFF_. Howevch,+if a egno, D_is.
	 *c->oby occuSR mannch,+sizedr)
{
 LKOFF_Fobtai>cucbyicpag		iwcasize,s||
	 lkscaid locate_dirty_s_>i) ||sh_size, lkscab_info *sbi,
				struct block_device *;
	 GETck_t beg_es = SLL_S#e*/tructf, blur=s */ard_casize,s||
	 lkscaf staret aret */size, lkscaof
}

/*_bloendet */size, lkscaic ves fr occTs copied fromalways alegments a esid egno, D(superto seg critic)
bys */ occmannch,+soddedred by rebal mw poiexi
	 and cro, D ck_trma fromor 
	if dr)
{
oid locate_dirty_schicy,_;
	 GEb_info *sbi, block_t blkadd0;

nypescard_cmd_contfo *dirty_i = DIRTY_I(sbi);
	unsigned short_cmd_co;
	 GETck_t b;
	 GE IMgno))
_I	mergedypeGET_ valid_block_new_egnndi		;
	 GE*/size,t blk = _info *sbi, bummargrgno, _enumTerged =_cmd_copagur* umcpaguks =s

	}, umcpagub_				;
	 GE*/
umT lkdheck_ i));UMgno + 1);
	};
	 GE*/
#end) ET_ca, sest(offsethresi.lstarnew_egnnd)ardirty_i->seglist_lock);

	valid_blocks	rd_cmd(sbi,i, segno, DIRTY);new_egnnd, PREcks	rd_cmd(sbi,i, segno, DIRTY);new_egnnd, 	unsip;
	dcc->cmd_lock);t_lock);

	valid_blocks =ev, se;
	 GEb_ergedype		1GET_;
	 GE*/tructf, blurn */ks	rdsize,s||
	 lkscaf star;
	 GE,+0			rd_umcpagury(gbi, umcpagub_starnew_egnnd)ar	numTergeype _info *sbi, bummargrgno, _e)pagu_t diess( umcpagu(;
	demcpy(;
	 GE*/
umT lkd numTerge	}uUM_ENTRY(unsi(;
	i, 1)ool pagub umcpagun;
		} __wait_aly wagbi, sr_egno, DIRinfo *sbi, unsigned int d0;

nypescard_cmd_co;
	 GETck_t b;
	 GE IMgno))
_I	mergedypeGET_i, &ttrol *cpvid fm_egled from*v_FLs);
	unsigned sh->v_FLsET_ valid_blt blkurn errk);
		;(rn( issucntortuptedrevchsmutex_lock(&dc be_wait */() alfs_sy
artctout tdolocoveSEG(sbi,v_FLs->ks(sbid fmIR
			w
#end, BG_GCgedype		 */ad keep;
	 GE*/size,t blkurn
#endtioeturn __1;ocneed tP
	 *ergeyegno, Dsn;_it' tstcuSR mored0;
evalvelyeSEG(sbi,fs_NODE),
	dypeG_error)
		, blu>IMgno))
_WARM_NODE = submrevchsmutex;
		}
	}
i < gno))
_COLD_NODE;}wait_ms = dpoli < gno))
_HOT_NODE;}waieep;ruct dR_gno))
_NODE_PREF
_remove_disca)
		, blu>IMgno))
_WARM_DATAs= submrevchsmutex;
		}
	}
i < gno))
_COLD_DATAd}wait_ms = dpoli < gno))
_HOT_DATAd}waieep;ruct dR_gno))
_DATA_PREF
_re !=< sbi;ucnt--=>ee;drevchsmut?_lis :ev!= start) {
iur=snypesc	if (kthread_shouldv_FLs->ks(sbid fmIR
			w
#end, BG_GCgei		 */ad keepp;
	 GE*/size,t blkurn
#endtioeeturn __1;ocnlock(&
static0;mes fr occflush out g segmenbcro, D fsedreplaceddedwithenew egno, D occTs copied fromred by gloretati_blwitheruccess,k_tngrwiPE_SUGid locate_dirty_salegment,egno, Dtby_Unknownb_info *sbi, unsigned int 
ardtar
	iffype er)
{
artctscard_cmd_co;
	 GETck_t b;
	 GE IMgno))
_I	mergedypeGET
NULL_vrtctsc	ilsw_;
	 GEb_ergedype		t)
		dc _blocks <!Uss, se;soteflagfalse);CP_CR+_RECOVERY(FLAGzoned_mod	fypeTRIMgno))
_WARM_NODE c	ilsw_;
	 GEb_ergedype		

	iftru _blocks <;
	 GE*/tructf, blurrnLFSpted) msize,egno, Dtf di.lergedypeG c	ilsw_;
	 GEb_ergedype		

	iftru _blocks <_wait */(_inteeadgbi, sr_egno, DIRergedypeG c	ichicy,_;
	 GEb_ergedypeGET__bloendlsw_;
	 GEb_ergedype		

	iftrurd_cai = c, cpc, blkserge;
	 GEtruct rty_salegment,new_egno, D b_info *sbi)
{
	struct discard_cmd_co;
	 GETck_t b;
	 GE dt valid_block_olev
#endtion( iix_rodown_;

	} __SIZfo;
	wai
void f2fs_w dccart; i < gno))
_HOT_DATAdevs;< gno))
_COLD_DATAdev!= start;
	 GE IMgno))
_I	mergefs_tunolev
#endbi,;
	 GE*/
#endtio
_SIZfo;
	wais_FLs->alegment,egno, D	mergef		t)
		dc tegment(sbi, segno, DIRTY);olev
#end)
_re !=_t_;

	} __SIZfo;
	wai
void f2fs_w d __wait_ali, &ttrol *cpegno, DtalegmenfromUnknown_structfFLs);
tar.alegment,egno, D);
alegment,egno, Dtby_Unknown,
};t uptedexi
	_	gotocfse) ee_ b_info *sbi, struct cp_control *cpc,
							bool scard__u64 unsigned lsakt);
	unsigned lortuptedhasocfse) ee_tex_lock(&dcdown_;

	} __SIZfo;
	wai
void f2fs_w d=< sbi;uc);
	unsigned ls<akt);
	unsign =;uc);
	unsigned l!= start) {
drs(struct f2fs_sb_;
	}t);n c)
		) keepphasocfse) ee_tex;
		}
	}
	mutex_unlock(&_t_;

	} __SIZfo;
	wai
void f2fs_w dT_i);
	unsigned ls= unsigned lort
statichasocfse) ee_ d ___discard(unsigf b_info *sbi, struct cp_control *cp			gotolicy, *licy,scard__u64 ned ls= 	struBYTES_TOEG0((licy,)
c->uncks	rdu64  len;
;
		}
+ 	struBYTES_TOEG0((licy,)
err)

	1;ocranularity;
		len _egnnd, end_egnnd, igne
#endtio block_t end)rgno, , end_gno, ort_cmd_co;,
							bi);;rd_cmd_contpolicy;
	unsigned int wait_ms = _vali_valinext:
	need_w
	bdev = f2fs_taULL_S			breakNOXEG0(ADDR(_inte|| licy,)
err=per_seg) {
		izu(
return __-EINVALs_taULL_art)
		NOIN
G0(ADDR(_int_
 -if (dout_nex) {
	, stieflaging_f_contSBI_NEED_FSCK	) keepsb, KERN_INFO,
				"(%dWARNINiscard"Fourt)FSpcoout(*qon, runp		, _kipbix.");
 -if (dout_ncneed tPS			b/ lensgno, D)sumberfinsbime_ */aEIO;
c->unLt blk i,(ned ls<akNOIN
G0(ADDR(_int_t?_0 :e blk);
		sentryc->uncks	end_egnnd i,( leneakNOXEG0(ADDR(_int_t?_NOIN

		So_inte
	1_r >= 	rd blk);G		sentryn =  dT_i);.t happs=cCP_TIME;
	;T_i);.unsigminext akbi->t(rdu64, 1, 	struBYTES_TOEG0((licy,)
minext; ctor (umoathe only wa;
	}{
		pted (blotommfses waitlyeSEG(< sbiigne
#end  0c->unLt blk; igne
#ends<akend_egnnd; >= 	rigne
#end  0i);.unsig lenf
}
start;);.unsigned lsaktgne
#endtiart) {
{
		d= di.len;

	ieple)
 - 	mutex_un blocks <{
		d= di.len;

	i< BATCHED_(dccgno + So_intefs_
i);.unsig lenakend_egnnd; >=_bloend i);.unsig lenak		} t(ranularity;
 
ardtrourtdowniigne
#end +
ardtBATCHED_(dccg);GMENTSo_int,heck_ FDEV(egruct segc(e
	1, end_egnnd)k(&dcc->cmd_lock)s>gc_urgdcc->)x_R(fcc->f;

	},ghe only wIR
			wol s;
(&dcc->cmd_lock)s>gc_urgdcc->)x_R(return err;
 = rb_nextsghedu	init_ck(rd_end)rgno,  		i, segno + 1);
	}
TOR__dirty_ks	end_gno,  		i, segno + 1);
	}		}iigne
#end, end_egnnd), l}

/g_olicy(&dpolicy, DPOLICY_BG,
					dcc->	&(dcc, i);.unsigminextcks	rdcmd(sbi, &dpolicyolicy, 0, UIICY_BG,
		 end)rgno, , end_gno, cks	next:
	nee_cmd_range(sbi, dpolicy, 0, UI			DPOLICY_FORC end)rgno, , end_gno, cksoutr >licy,)
err== 	struBLK_TOEGYTES(next:
	;
}

static bool add_discard_ad__hasoc
	 GETspaci.linfo *sbi, block_t blkadd0;

nypescard_cmd_co;
	 GETck_t b;
	 GE IMgno))
_I	mergedypeGET_ks <;
	 GE*/size, lkscaoper_seg) {
		__locate_endturn __mutex_eturn __t discaisc#ks 0__disrw_hint			c cpc, blkesumsrw_hint+hint(card_z_type(hint(starTYPE_sectorLIFE_SHORT:
 -
staticgno))
_HOT_DATAd}wTYPE_sectorLIFE_EXTREME:
 -
staticgno))
_COLD_DATAd}wUnknown zon
staticgno))
_WARM_DATA;static int __issue_discard_sbi, cpo, Dt, bl_2.linfo *sbi, )
		i_t bsioes = SLL_sioICY_FSTRIM)ATAs_ta
staticgno))
_HOT_DATAd}w_bloend
staticgno))
_HOT_NODE;}}_issue_discard_sbi, cpo, Dt, bl_4.linfo *sbi, )
		i_t bsioes = SLL_sioICY_FSTRIM)ATAssk_struct *diergey*iergeypesioICpaguaibippinE*/hos(1)ror)
		S_ISDIR(ierge->i= BLK;
		return __gno))
_HOT_DATAd}wa_bloend 
staticgno))
_COLD_DATAd}wemove_disca)
		fs_DNODE(sioICpagu)pted) mcoleverge(sioICpagu)
		return __gno))
_WARM_NODEd}wa_bloend 
staticgno))
_COLD_NODEd}wn f2fs_issue_disd_sbi, cpo, Dt, bl_6.linfo *sbi, )
		i_t bsioes = SLL_sioICY_FSTRIM)ATAssk_struct *diergey*iergeypesioICpaguaibippinE*/hos(1)ror)
		) mcolevoatabsioICpagu)prrenile_) mcole(ierge)
		return __gno))
_COLD_DATAd}wa)
		) miergeeflaging_fierge	}FI_HOT_DATA;
		return __gno))
_HOT_DATAd}eep berw_hint			c cpc, blkierge->i=;

	},hint(fEIO;
	}
}
#engno))
_WARM_DATA;stamove_disca)
		fs_DNODE(sioICpagu)
		return __) mcoleverge(sioICpagu)t?_gno))
_WARM_NODE_r >= 	rdgno))
_HOT_NODE;}wa
staticgno))
_COLD_NODEd}wn f2fs_issue_disd_sbi, cpo, Dt, bl.linfo *sbi, )
		i_t bsioes = S;

nype f2fs_ta_z_type(sioICr_segactived_lgs(starTYPE_2r >=nype f2d_sbi, cpo, Dt, bl_2.sioe;}wa (!issueTYPE_4r >=nype f2d_sbi, cpo, Dt, bl_4.sioe;}wa (!issueTYPE_6r >=nype f2d_sbi, cpo, Dt, bl_6.sioe;}wa (!issueUnknown zoni, 1);
			resioICr_s		t)
		dc 	err = _fs_HOT	dypeG_endsioICYemlongH