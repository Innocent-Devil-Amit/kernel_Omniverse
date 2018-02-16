/*
 * Copyright (C) 2011, 2012 STRATO.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/blkdev.h>
#include <linux/ratelimit.h>
#include "ctree.h"
#include "volumes.h"
#include "disk-io.h"
#include "ordered-data.h"
#include "transaction.h"
#include "backref.h"
#include "extent_io.h"
#include "dev-replace.h"
#include "check-integrity.h"
#include "rcu-string.h"
#include "raid56.h"

/*
 * This is only the first step towards a full-features scrub. It reads all
 * extent and super block and verifies the checksums. In case a bad checksum
 * is found or the extent cannot be read, good data will be written back if
 * any can be found.
 *
 * Future enhancements:
 *  - In case an unrepairable extent is encountered, track which files are
 *    affected and report them
 *  - track and record media errors, throw out bad devices
 *  - add a mode to also read unallocated space
 */

struct scrub_block;
struct scrub_ctx;

/*
 * the following three values only influence the performance.
 * The last one configures the number of parallel and outstanding I/O
 * operations. The first two values configure an upper limit for the number
 * of (dynamically allocated) pages that are added to a bio.
 */
#define SCRUB_PAGES_PER_RD_BIO	32	/* 128k per bio */
#define SCRUB_PAGES_PER_WR_BIO	32	/* 128k per bio */
#define SCRUB_BIOS_PER_SCTX	64	/* 8MB per device in flight */

/*
 * the following value times PAGE_SIZE needs to be large enough to match the
 * largest node/leaf/sector size that shall be supported.
 * Values larger than BTRFS_STRIPE_LEN are not supported.
 */
#define SCRUB_MAX_PAGES_PER_BLOCK	16	/* 64k per node/leaf/sector */

struct scrub_page {
	struct scrub_block	*sblock;
	struct page		*page;
	struct btrfs_device	*dev;
	u64			flags;  /* extent flags */
	u64			generation;
	u64			logical;
	u64			physical;
	u64			physical_for_dev_replace;
	atomic_t		ref_count;
	struct {
		unsigned int	mirror_num:8;
		unsigned int	have_csum:1;
		unsigned int	io_error:1;
	};
	u8			csum[BTRFS_CSUM_SIZE];
};

struct scrub_bio {
	int			index;
	struct scrub_ctx	*sctx;
	struct btrfs_device	*dev;
	struct bio		*bio;
	int			err;
	u64			logical;
	u64			physical;
#if SCRUB_PAGES_PER_WR_BIO >= SCRUB_PAGES_PER_RD_BIO
	struct scrub_page	*pagev[SCRUB_PAGES_PER_WR_BIO];
#else
	struct scrub_page	*pagev[SCRUB_PAGES_PER_RD_BIO];
#endif
	int			page_count;
	int			next_free;
	struct btrfs_work	work;
};

struct scrub_block {
	struct scrub_page	*pagev[SCRUB_MAX_PAGES_PER_BLOCK];
	int			page_count;
	atomic_t		outstanding_pages;
	atomic_t		ref_count; /* free mem on transition to zero */
	struct scrub_ctx	*sctx;
	struct {
		unsigned int	header_error:1;
		unsigned int	checksum_error:1;
		unsigned int	no_io_error_seen:1;
		unsigned int	generation_error:1; /* also sets header_error */
	};
};

struct scrub_wr_ctx {
	struct scrub_bio *wr_curr_bio;
	struct btrfs_device *tgtdev;
	int pages_per_wr_bio; /* <= SCRUB_PAGES_PER_WR_BIO */
	atomic_t flush_all_writes;
	struct mutex wr_lock;
};

struct scrub_ctx {
	struct scrub_bio	*bios[SCRUB_BIOS_PER_SCTX];
	struct btrfs_root	*dev_root;
	int			first_free;
	int			curr;
	atomic_t		bios_in_flight;
	atomic_t		workers_pending;
	spinlock_t		list_lock;
	wait_queue_head_t	list_wait;
	u16			csum_size;
	struct list_head	csum_list;
	atomic_t		cancel_req;
	int			readonly;
	int			pages_per_rd_bio;
	u32			sectorsize;
	u32			nodesize;

	int			is_dev_replace;
	struct scrub_wr_ctx	wr_ctx;

	/*
	 * statistics
	 */
	struct btrfs_scrub_progress stat;
	spinlock_t		stat_lock;
};

struct scrub_fixup_nodatasum {
	struct scrub_ctx	*sctx;
	struct btrfs_device	*dev;
	u64			logical;
	struct btrfs_root	*root;
	struct btrfs_work	work;
	int			mirror_num;
};

struct scrub_nocow_inode {
	u64			inum;
	u64			offset;
	u64			root;
	struct list_head	list;
};

struct scrub_copy_nocow_ctx {
	struct scrub_ctx	*sctx;
	u64			logical;
	u64			len;
	int			mirror_num;
	u64			physical_for_dev_replace;
	struct list_head	inodes;
	struct btrfs_work	work;
};

struct scrub_warning {
	struct btrfs_path	*path;
	u64			extent_item_size;
	const char		*errstr;
	sector_t		sector;
	u64			logical;
	struct btrfs_device	*dev;
};

static void scrub_pending_bio_inc(struct scrub_ctx *sctx);
static void scrub_pending_bio_dec(struct scrub_ctx *sctx);
static void scrub_pending_trans_workers_inc(struct scrub_ctx *sctx);
static void scrub_pending_trans_workers_dec(struct scrub_ctx *sctx);
static int scrub_handle_errored_block(struct scrub_block *sblock_to_check);
static int scrub_setup_recheck_block(struct scrub_ctx *sctx,
				     struct btrfs_fs_info *fs_info,
				     struct scrub_block *original_sblock,
				     u64 length, u64 logical,
				     struct scrub_block *sblocks_for_recheck);
static void scrub_recheck_block(struct btrfs_fs_info *fs_info,
				struct scrub_block *sblock, int is_metadata,
				int have_csum, u8 *csum, u64 generation,
				u16 csum_size);
static void scrub_recheck_block_checksum(struct btrfs_fs_info *fs_info,
					 struct scrub_block *sblock,
					 int is_metadata, int have_csum,
					 const u8 *csum, u64 generation,
					 u16 csum_size);
static int scrub_repair_block_from_good_copy(struct scrub_block *sblock_bad,
					     struct scrub_block *sblock_good,
					     int force_write);
static int scrub_repair_page_from_good_copy(struct scrub_block *sblock_bad,
					    struct scrub_block *sblock_good,
					    int page_num, int force_write);
static void scrub_write_block_to_dev_replace(struct scrub_block *sblock);
static int scrub_write_page_to_dev_replace(struct scrub_block *sblock,
					   int page_num);
static int scrub_checksum_data(struct scrub_block *sblock);
static int scrub_checksum_tree_block(struct scrub_block *sblock);
static int scrub_checksum_super(struct scrub_block *sblock);
static void scrub_block_get(struct scrub_block *sblock);
static void scrub_block_put(struct scrub_block *sblock);
static void scrub_page_get(struct scrub_page *spage);
static void scrub_page_put(struct scrub_page *spage);
static int scrub_add_page_to_rd_bio(struct scrub_ctx *sctx,
				    struct scrub_page *spage);
static int scrub_pages(struct scrub_ctx *sctx, u64 logical, u64 len,
		       u64 physical, struct btrfs_device *dev, u64 flags,
		       u64 gen, int mirror_num, u8 *csum, int force,
		       u64 physical_for_dev_replace);
static void scrub_bio_end_io(struct bio *bio, int err);
static void scrub_bio_end_io_worker(struct btrfs_work *work);
static void scrub_block_complete(struct scrub_block *sblock);
static void scrub_remap_extent(struct btrfs_fs_info *fs_info,
			       u64 extent_logical, u64 extent_len,
			       u64 *extent_physical,
			       struct btrfs_device **extent_dev,
			       int *extent_mirror_num);
static int scrub_setup_wr_ctx(struct scrub_ctx *sctx,
			      struct scrub_wr_ctx *wr_ctx,
			      struct btrfs_fs_info *fs_info,
			      struct btrfs_device *dev,
			      int is_dev_replace);
static void scrub_free_wr_ctx(struct scrub_wr_ctx *wr_ctx);
static int scrub_add_page_to_wr_bio(struct scrub_ctx *sctx,
				    struct scrub_page *spage);
static void scrub_wr_submit(struct scrub_ctx *sctx);
static void scrub_wr_bio_end_io(struct bio *bio, int err);
static void scrub_wr_bio_end_io_worker(struct btrfs_work *work);
static int write_page_nocow(struct scrub_ctx *sctx,
			    u64 physical_for_dev_replace, struct page *page);
static int copy_nocow_pages_for_inode(u64 inum, u64 offset, u64 root,
				      struct scrub_copy_nocow_ctx *ctx);
static int copy_nocow_pages(struct scrub_ctx *sctx, u64 logical, u64 len,
			    int mirror_num, u64 physical_for_dev_replace);
static void copy_nocow_pages_worker(struct btrfs_work *work);
static void __scrub_blocked_if_needed(struct btrfs_fs_info *fs_info);
static void scrub_blocked_if_needed(struct btrfs_fs_info *fs_info);


static void scrub_pending_bio_inc(struct scrub_ctx *sctx)
{
	atomic_inc(&sctx->bios_in_flight);
}

static void scrub_pending_bio_dec(struct scrub_ctx *sctx)
{
	atomic_dec(&sctx->bios_in_flight);
	wake_up(&sctx->list_wait);
}

static void __scrub_blocked_if_needed(struct btrfs_fs_info *fs_info)
{
	while (atomic_read(&fs_info->scrub_pause_req)) {
		mutex_unlock(&fs_info->scrub_lock);
		wait_event(fs_info->scrub_pause_wait,
		   atomic_read(&fs_info->scrub_pause_req) == 0);
		mutex_lock(&fs_info->scrub_lock);
	}
}

static void scrub_blocked_if_needed(struct btrfs_fs_info *fs_info)
{
	atomic_inc(&fs_info->scrubs_paused);
	wake_up(&fs_info->scrub_pause_wait);

	mutex_lock(&fs_info->scrub_lock);
	__scrub_blocked_if_needed(fs_info);
	atomic_dec(&fs_info->scrubs_paused);
	mutex_unlock(&fs_info->scrub_lock);

	wake_up(&fs_info->scrub_pause_wait);
}

/*
 * used for workers that require transaction commits (i.e., for the
 * NOCOW case)
 */
static void scrub_pending_trans_workers_inc(struct scrub_ctx *sctx)
{
	struct btrfs_fs_info *fs_info = sctx->dev_root->fs_info;

	/*
	 * increment scrubs_running to prevent cancel requests from
	 * completing as long as a worker is running. we must also
	 * increment scrubs_paused to prevent deadlocking on pause
	 * requests used for transactions commits (as the worker uses a
	 * transaction context). it is safe to regard the worker
	 * as paused for all matters practical. effectively, we only
	 * avoid cancellation requests from completing.
	 */
	mutex_lock(&fs_info->scrub_lock);
	atomic_inc(&fs_info->scrubs_running);
	atomic_inc(&fs_info->scrubs_paused);
	mutex_unlock(&fs_info->scrub_lock);

	/*
	 * check if @scrubs_running=@scrubs_paused condition
	 * inside wait_event() is not an atomic operation.
	 * which means we may inc/dec @scrub_running/paused
	 * at any time. Let's wake up @scrub_pause_wait as
	 * much as we can to let commit transaction blocked less.
	 */
	wake_up(&fs_info->scrub_pause_wait);

	atomic_inc(&sctx->workers_pending);
}

/* used for workers that require transaction commits */
static void scrub_pending_trans_workers_dec(struct scrub_ctx *sctx)
{
	struct btrfs_fs_info *fs_info = sctx->dev_root->fs_info;

	/*
	 * see scrub_pending_trans_workers_inc() why we're pretending
	 * to be paused in the scrub counters
	 */
	mutex_lock(&fs_info->scrub_lock);
	atomic_dec(&fs_info->scrubs_running);
	atomic_dec(&fs_info->scrubs_paused);
	mutex_unlock(&fs_info->scrub_lock);
	atomic_dec(&sctx->workers_pending);
	wake_up(&fs_info->scrub_pause_wait);
	wake_up(&sctx->list_wait);
}

static void scrub_free_csums(struct scrub_ctx *sctx)
{
	while (!list_empty(&sctx->csum_list)) {
		struct btrfs_ordered_sum *sum;
		sum = list_first_entry(&sctx->csum_list,
				       struct btrfs_ordered_sum, list);
		list_del(&sum->list);
		kfree(sum);
	}
}

static noinline_for_stack void scrub_free_ctx(struct scrub_ctx *sctx)
{
	int i;

	if (!sctx)
		return;

	scrub_free_wr_ctx(&sctx->wr_ctx);

	/* this can happen when scrub is cancelled */
	if (sctx->curr != -1) {
		struct scrub_bio *sbio = sctx->bios[sctx->curr];

		for (i = 0; i < sbio->page_count; i++) {
			WARN_ON(!sbio->pagev[i]->page);
			scrub_block_put(sbio->pagev[i]->sblock);
		}
		bio_put(sbio->bio);
	}

	for (i = 0; i < SCRUB_BIOS_PER_SCTX; ++i) {
		struct scrub_bio *sbio = sctx->bios[i];

		if (!sbio)
			break;
		kfree(sbio);
	}

	scrub_free_csums(sctx);
	kfree(sctx);
}

static noinline_for_stack
struct scrub_ctx *scrub_setup_ctx(struct btrfs_device *dev, int is_dev_replace)
{
	struct scrub_ctx *sctx;
	int		i;
	struct btrfs_fs_info *fs_info = dev->dev_root->fs_info;
	int pages_per_rd_bio;
	int ret;

	/*
	 * the setting of pages_per_rd_bio is correct for scrub but might
	 * be wrong for the dev_replace code where we might read from
	 * different devices in the initial huge bios. However, that
	 * code is able to correctly handle the case when adding a page
	 * to a bio fails.
	 */
	if (dev->bdev)
		pages_per_rd_bio = min_t(int, SCRUB_PAGES_PER_RD_BIO,
					 bio_get_nr_vecs(dev->bdev));
	else
		pages_per_rd_bio = SCRUB_PAGES_PER_RD_BIO;
	sctx = kzalloc(sizeof(*sctx), GFP_NOFS);
	if (!sctx)
		goto nomem;
	sctx->is_dev_replace = is_dev_replace;
	sctx->pages_per_rd_bio = pages_per_rd_bio;
	sctx->curr = -1;
	sctx->dev_root = dev->dev_root;
	for (i = 0; i < SCRUB_BIOS_PER_SCTX; ++i) {
		struct scrub_bio *sbio;

		sbio = kzalloc(sizeof(*sbio), GFP_NOFS);
		if (!sbio)
			goto nomem;
		sctx->bios[i] = sbio;

		sbio->index = i;
		sbio->sctx = sctx;
		sbio->page_count = 0;
		btrfs_init_work(&sbio->work, btrfs_scrub_helper,
				scrub_bio_end_io_worker, NULL, NULL);

		if (i != SCRUB_BIOS_PER_SCTX - 1)
			sctx->bios[i]->next_free = i + 1;
		else
			sctx->bios[i]->next_free = -1;
	}
	sctx->first_free = 0;
	sctx->nodesize = dev->dev_root->nodesize;
	sctx->sectorsize = dev->dev_root->sectorsize;
	atomic_set(&sctx->bios_in_flight, 0);
	atomic_set(&sctx->workers_pending, 0);
	atomic_set(&sctx->cancel_req, 0);
	sctx->csum_size = btrfs_super_csum_size(fs_info->super_copy);
	INIT_LIST_HEAD(&sctx->csum_list);

	spin_lock_init(&sctx->list_lock);
	spin_lock_init(&sctx->stat_lock);
	init_waitqueue_head(&sctx->list_wait);

	ret = scrub_setup_wr_ctx(sctx, &sctx->wr_ctx, fs_info,
				 fs_info->dev_replace.tgtdev, is_dev_replace);
	if (ret) {
		scrub_free_ctx(sctx);
		return ERR_PTR(ret);
	}
	return sctx;

nomem:
	scrub_free_ctx(sctx);
	return ERR_PTR(-ENOMEM);
}

static int scrub_print_warning_inode(u64 inum, u64 offset, u64 root,
				     void *warn_ctx)
{
	u64 isize;
	u32 nlink;
	int ret;
	int i;
	struct extent_buffer *eb;
	struct btrfs_inode_item *inode_item;
	struct scrub_warning *swarn = warn_ctx;
	struct btrfs_fs_info *fs_info = swarn->dev->dev_root->fs_info;
	struct inode_fs_paths *ipath = NULL;
	struct btrfs_root *local_root;
	struct btrfs_key root_key;

	root_key.objectid = root;
	root_key.type = BTRFS_ROOT_ITEM_KEY;
	root_key.offset = (u64)-1;
	local_root = btrfs_read_fs_root_no_name(fs_info, &root_key);
	if (IS_ERR(local_root)) {
		ret = PTR_ERR(local_root);
		goto err;
	}

	ret = inode_item_info(inum, 0, local_root, swarn->path);
	if (ret) {
		btrfs_release_path(swarn->path);
		goto err;
	}

	eb = swarn->path->nodes[0];
	inode_item = btrfs_item_ptr(eb, swarn->path->slots[0],
					struct btrfs_inode_item);
	isize = btrfs_inode_size(eb, inode_item);
	nlink = btrfs_inode_nlink(eb, inode_item);
	btrfs_release_path(swarn->path);

	ipath = init_ipath(4096, local_root, swarn->path);
	if (IS_ERR(ipath)) {
		ret = PTR_ERR(ipath);
		ipath = NULL;
		goto err;
	}
	ret = paths_from_inode(inum, ipath);

	if (ret < 0)
		goto err;

	/*
	 * we deliberately ignore the bit ipath might have been too small to
	 * hold all of the paths here
	 */
	for (i = 0; i < ipath->fspath->elem_cnt; ++i)
		printk_in_rcu(KERN_WARNING "BTRFS: %s at logical %llu on dev "
			"%s, sector %llu, root %llu, inode %llu, offset %llu, "
			"length %llu, links %u (path: %s)\n", swarn->errstr,
			swarn->logical, rcu_str_deref(swarn->dev->name),
			(unsigned long long)swarn->sector, root, inum, offset,
			min(isize - offset, (u64)PAGE_SIZE), nlink,
			(char *)(unsigned long)ipath->fspath->val[i]);

	free_ipath(ipath);
	return 0;

err:
	printk_in_rcu(KERN_WARNING "BTRFS: %s at logical %llu on dev "
		"%s, sector %llu, root %llu, inode %llu, offset %llu: path "
		"resolving failed with ret=%d\n", swarn->errstr,
		swarn->logical, rcu_str_deref(swarn->dev->name),
		(unsigned long long)swarn->sector, root, inum, offset, ret);

	free_ipath(ipath);
	return 0;
}

static void scrub_print_warning(const char *errstr, struct scrub_block *sblock)
{
	struct btrfs_device *dev;
	struct btrfs_fs_info *fs_info;
	struct btrfs_path *path;
	struct btrfs_key found_key;
	struct extent_buffer *eb;
	struct btrfs_extent_item *ei;
	struct scrub_warning swarn;
	unsigned long ptr = 0;
	u64 extent_item_pos;
	u64 flags = 0;
	u64 ref_root;
	u32 item_size;
	u8 ref_level;
	int ret;

	WARN_ON(sblock->page_count < 1);
	dev = sblock->pagev[0]->dev;
	fs_info = sblock->sctx->dev_root->fs_info;

	path = btrfs_alloc_path();
	if (!path)
		return;

	swarn.sector = (sblock->pagev[0]->physical) >> 9;
	swarn.logical = sblock->pagev[0]->logical;
	swarn.errstr = errstr;
	swarn.dev = NULL;

	ret = extent_from_logical(fs_info, swarn.logical, path, &found_key,
				  &flags);
	if (ret < 0)
		goto out;

	extent_item_pos = swarn.logical - found_key.objectid;
	swarn.extent_item_size = found_key.offset;

	eb = path->nodes[0];
	ei = btrfs_item_ptr(eb, path->slots[0], struct btrfs_extent_item);
	item_size = btrfs_item_size_nr(eb, path->slots[0]);

	if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
		do {
			ret = tree_backref_for_extent(&ptr, eb, &found_key, ei,
						      item_size, &ref_root,
						      &ref_level);
			printk_in_rcu(KERN_WARNING
				"BTRFS: %s at logical %llu on dev %s, "
				"sector %llu: metadata %s (level %d) in tree "
				"%llu\n", errstr, swarn.logical,
				rcu_str_deref(dev->name),
				(unsigned long long)swarn.sector,
				ref_level ? "node" : "leaf",
				ret < 0 ? -1 : ref_level,
				ret < 0 ? -1 : ref_root);
		} while (ret != 1);
		btrfs_release_path(path);
	} else {
		btrfs_release_path(path);
		swarn.path = path;
		swarn.dev = dev;
		iterate_extent_inodes(fs_info, found_key.objectid,
					extent_item_pos, 1,
					scrub_print_warning_inode, &swarn);
	}

out:
	btrfs_free_path(path);
}

static int scrub_fixup_readpage(u64 inum, u64 offset, u64 root, void *fixup_ctx)
{
	struct page *page = NULL;
	unsigned long index;
	struct scrub_fixup_nodatasum *fixup = fixup_ctx;
	int ret;
	int corrected = 0;
	struct btrfs_key key;
	struct inode *inode = NULL;
	struct btrfs_fs_info *fs_info;
	u64 end = offset + PAGE_SIZE - 1;
	struct btrfs_root *local_root;
	int srcu_index;

	key.objectid = root;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = (u64)-1;

	fs_info = fixup->root->fs_info;
	srcu_index = srcu_read_lock(&fs_info->subvol_srcu);

	local_root = btrfs_read_fs_root_no_name(fs_info, &key);
	if (IS_ERR(local_root)) {
		srcu_read_unlock(&fs_info->subvol_srcu, srcu_index);
		return PTR_ERR(local_root);
	}

	key.type = BTRFS_INODE_ITEM_KEY;
	key.objectid = inum;
	key.offset = 0;
	inode = btrfs_iget(fs_info->sb, &key, local_root, NULL);
	srcu_read_unlock(&fs_info->subvol_srcu, srcu_index);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	index = offset >> PAGE_CACHE_SHIFT;

	page = find_or_create_page(inode->i_mapping, index, GFP_NOFS);
	if (!page) {
		ret = -ENOMEM;
		goto out;
	}

	if (PageUptodate(page)) {
		if (PageDirty(page)) {
			/*
			 * we need to write the data to the defect sector. the
			 * data that was in that sector is not in memory,
			 * because the page was modified. we must not write the
			 * modified page to that sector.
			 *
			 * TODO: what could be done here: wait for the delalloc
			 *       runner to write out that page (might involve
			 *       COW) and see whether the sector is still
			 *       referenced afterwards.
			 *
			 * For the meantime, we'll treat this error
			 * incorrectable, although there is a chance that a
			 * later scrub will find the bad sector again and that
			 * there's no dirty page in memory, then.
			 */
			ret = -EIO;
			goto out;
		}
		ret = repair_io_failure(inode, offset, PAGE_SIZE,
					fixup->logical, page,
					offset - page_offset(page),
					fixup->mirror_num);
		unlock_page(page);
		corrected = !ret;
	} else {
		/*
		 * we need to get good data first. the general readpage path
		 * will call repair_io_failure for us, we just have to make
		 * sure we read the bad mirror.
		 */
		ret = set_extent_bits(&BTRFS_I(inode)->io_tree, offset, end,
					EXTENT_DAMAGED, GFP_NOFS);
		if (ret) {
			/* set_extent_bits should give proper error */
			WARN_ON(ret > 0);
			if (ret > 0)
				ret = -EFAULT;
			goto out;
		}

		ret = extent_read_full_page(&BTRFS_I(inode)->io_tree, page,
						btrfs_get_extent,
						fixup->mirror_num);
		wait_on_page_locked(page);

		corrected = !test_range_bit(&BTRFS_I(inode)->io_tree, offset,
						end, EXTENT_DAMAGED, 0, NULL);
		if (!corrected)
			clear_extent_bits(&BTRFS_I(inode)->io_tree, offset, end,
						EXTENT_DAMAGED, GFP_NOFS);
	}

out:
	if (page)
		put_page(page);

	iput(inode);

	if (ret < 0)
		return ret;

	if (ret == 0 && corrected) {
		/*
		 * we only need to call readpage for one of the inodes belonging
		 * to this extent. so make iterate_extent_inodes stop
		 */
		return 1;
	}

	return -EIO;
}

static void scrub_fixup_nodatasum(struct btrfs_work *work)
{
	int ret;
	struct scrub_fixup_nodatasum *fixup;
	struct scrub_ctx *sctx;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_path *path;
	int uncorrectable = 0;

	fixup = container_of(work, struct scrub_fixup_nodatasum, work);
	sctx = fixup->sctx;

	path = btrfs_alloc_path();
	if (!path) {
		spin_lock(&sctx->stat_lock);
		++sctx->stat.malloc_errors;
		spin_unlock(&sctx->stat_lock);
		uncorrectable = 1;
		goto out;
	}

	trans = btrfs_join_transaction(fixup->root);
	if (IS_ERR(trans)) {
		uncorrectable = 1;
		goto out;
	}

	/*
	 * the idea is to trigger a regular read through the standard path. we
	 * read a page from the (failed) logical address by specifying the
	 * corresponding copynum of the failed sector. thus, that readpage is
	 * expected to fail.
	 * that is the point where on-the-fly error correction will kick in
	 * (once it's finished) and rewrite the failed sector if a good copy
	 * can be found.
	 */
	ret = iterate_inodes_from_logical(fixup->logical, fixup->root->fs_info,
						path, scrub_fixup_readpage,
						fixup);
	if (ret < 0) {
		uncorrectable = 1;
		goto out;
	}
	WARN_ON(ret != 1);

	spin_lock(&sctx->stat_lock);
	++sctx->stat.corrected_errors;
	spin_unlock(&sctx->stat_lock);

out:
	if (trans && !IS_ERR(trans))
		btrfs_end_transaction(trans, fixup->root);
	if (uncorrectable) {
		spin_lock(&sctx->stat_lock);
		++sctx->stat.uncorrectable_errors;
		spin_unlock(&sctx->stat_lock);
		btrfs_dev_replace_stats_inc(
			&sctx->dev_root->fs_info->dev_replace.
			num_uncorrectable_read_errors);
		printk_ratelimited_in_rcu(KERN_ERR "BTRFS: "
		    "unable to fixup (nodatasum) error at logical %llu on dev %s\n",
			fixup->logical, rcu_str_deref(fixup->dev->name));
	}

	btrfs_free_path(path);
	kfree(fixup);

	scrub_pending_trans_workers_dec(sctx);
}

/*
 * scrub_handle_errored_block gets called when either verification of the
 * pages failed or the bio failed to read, e.g. with EIO. In the latter
 * case, this function handles all pages in the bio, even though only one
 * may be bad.
 * The goal of this function is to repair the errored block by using the
 * contents of one of the mirrors.
 */
static int scrub_handle_errored_block(struct scrub_block *sblock_to_check)
{
	struct scrub_ctx *sctx = sblock_to_check->sctx;
	struct btrfs_device *dev;
	struct btrfs_fs_info *fs_info;
	u64 length;
	u64 logical;
	u64 generation;
	unsigned int failed_mirror_index;
	unsigned int is_metadata;
	unsigned int have_csum;
	u8 *csum;
	struct scrub_block *sblocks_for_recheck; /* holds one for each mirror */
	struct scrub_block *sblock_bad;
	int ret;
	int mirror_index;
	int page_num;
	int success;
	static DEFINE_RATELIMIT_STATE(_rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);

	BUG_ON(sblock_to_check->page_count < 1);
	fs_info = sctx->dev_root->fs_info;
	if (sblock_to_check->pagev[0]->flags & BTRFS_EXTENT_FLAG_SUPER) {
		/*
		 * if we find an error in a super block, we just report it.
		 * They will get written with the next transaction commit
		 * anyway
		 */
		spin_lock(&sctx->stat_lock);
		++sctx->stat.super_errors;
		spin_unlock(&sctx->stat_lock);
		return 0;
	}
	length = sblock_to_check->page_count * PAGE_SIZE;
	logical = sblock_to_check->pagev[0]->logical;
	generation = sblock_to_check->pagev[0]->generation;
	BUG_ON(sblock_to_check->pagev[0]->mirror_num < 1);
	failed_mirror_index = sblock_to_check->pagev[0]->mirror_num - 1;
	is_metadata = !(sblock_to_check->pagev[0]->flags &
			BTRFS_EXTENT_FLAG_DATA);
	have_csum = sblock_to_check->pagev[0]->have_csum;
	csum = sblock_to_check->pagev[0]->csum;
	dev = sblock_to_check->pagev[0]->dev;

	if (sctx->is_dev_replace && !is_metadata && !have_csum) {
		sblocks_for_recheck = NULL;
		goto nodatasum_case;
	}

	/*
	 * read all mirrors one after the other. This includes to
	 * re-read the extent or metadata block that failed (that was
	 * the cause that this fixup code is called) another time,
	 * page by page this time in order to know which pages
	 * caused I/O errors and which ones are good (for all mirrors).
	 * It is the goal to handle the situation when more than one
	 * mirror contains I/O errors, but the errors do not
	 * overlap, i.e. the data can be repaired by selecting the
	 * pages from those mirrors without I/O error on the
	 * particular pages. One example (with blocks >= 2 * PAGE_SIZE)
	 * would be that mirror #1 has an I/O error on the first page,
	 * the second page is good, and mirror #2 has an I/O error on
	 * the second page, but the first page is good.
	 * Then the first page of the first mirror can be repaired by
	 * taking the first page of the second mirror, and the
	 * second page of the second mirror can be repaired by
	 * copying the contents of the 2nd page of the 1st mirror.
	 * One more note: if the pages of one mirror contain I/O
	 * errors, the checksum cannot be verified. In order to get
	 * the best data for repairing, the first attempt is to find
	 * a mirror without I/O errors and with a validated checksum.
	 * Only if this is not possible, the pages are picked from
	 * mirrors with I/O errors without considering the checksum.
	 * If the latter is the case, at the end, the checksum of the
	 * repaired area is verified in order to correctly maintain
	 * the statistics.
	 */

	sblocks_for_recheck = kzalloc(BTRFS_MAX_MIRRORS *
				     sizeof(*sblocks_for_recheck),
				     GFP_NOFS);
	if (!sblocks_for_recheck) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.malloc_errors++;
		sctx->stat.read_errors++;
		sctx->stat.uncorrectable_errors++;
		spin_unlock(&sctx->stat_lock);
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_READ_ERRS);
		goto out;
	}

	/* setup the context, map the logical blocks and alloc the pages */
	ret = scrub_setup_recheck_block(sctx, fs_info, sblock_to_check, length,
					logical, sblocks_for_recheck);
	if (ret) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.read_errors++;
		sctx->stat.uncorrectable_errors++;
		spin_unlock(&sctx->stat_lock);
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_READ_ERRS);
		goto out;
	}
	BUG_ON(failed_mirror_index >= BTRFS_MAX_MIRRORS);
	sblock_bad = sblocks_for_recheck + failed_mirror_index;

	/* build and submit the bios for the failed mirror, check checksums */
	scrub_recheck_block(fs_info, sblock_bad, is_metadata, have_csum,
			    csum, generation, sctx->csum_size);

	if (!sblock_bad->header_error && !sblock_bad->checksum_error &&
	    sblock_bad->no_io_error_seen) {
		/*
		 * the error disappeared after reading page by page, or
		 * the area was part of a huge bio and other parts of the
		 * bio caused I/O errors, or the block layer merged several
		 * read requests into one and the error is caused by a
		 * different bio (usually one of the two latter cases is
		 * the cause)
		 */
		spin_lock(&sctx->stat_lock);
		sctx->stat.unverified_errors++;
		spin_unlock(&sctx->stat_lock);

		if (sctx->is_dev_replace)
			scrub_write_block_to_dev_replace(sblock_bad);
		goto out;
	}

	if (!sblock_bad->no_io_error_seen) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.read_errors++;
		spin_unlock(&sctx->stat_lock);
		if (__ratelimit(&_rs))
			scrub_print_warning("i/o error", sblock_to_check);
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_READ_ERRS);
	} else if (sblock_bad->checksum_error) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.csum_errors++;
		spin_unlock(&sctx->stat_lock);
		if (__ratelimit(&_rs))
			scrub_print_warning("checksum error", sblock_to_check);
		btrfs_dev_stat_inc_and_print(dev,
					     BTRFS_DEV_STAT_CORRUPTION_ERRS);
	} else if (sblock_bad->header_error) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.verify_errors++;
		spin_unlock(&sctx->stat_lock);
		if (__ratelimit(&_rs))
			scrub_print_warning("checksum/header error",
					    sblock_to_check);
		if (sblock_bad->generation_error)
			btrfs_dev_stat_inc_and_print(dev,
				BTRFS_DEV_STAT_GENERATION_ERRS);
		else
			btrfs_dev_stat_inc_and_print(dev,
				BTRFS_DEV_STAT_CORRUPTION_ERRS);
	}

	if (sctx->readonly) {
		ASSERT(!sctx->is_dev_replace);
		goto out;
	}

	if (!is_metadata && !have_csum) {
		struct scrub_fixup_nodatasum *fixup_nodatasum;

nodatasum_case:
		WARN_ON(sctx->is_dev_replace);

		/*
		 * !is_metadata and !have_csum, this means that the data
		 * might not be COW'ed, that it might be modified
		 * concurrently. The general strategy to work on the
		 * commit root does not help in the case when COW is not
		 * used.
		 */
		fixup_nodatasum = kzalloc(sizeof(*fixup_nodatasum), GFP_NOFS);
		if (!fixup_nodatasum)
			goto did_not_correct_error;
		fixup_nodatasum->sctx = sctx;
		fixup_nodatasum->dev = dev;
		fixup_nodatasum->logical = logical;
		fixup_nodatasum->root = fs_info->extent_root;
		fixup_nodatasum->mirror_num = failed_mirror_index + 1;
		scrub_pending_trans_workers_inc(sctx);
		btrfs_init_work(&fixup_nodatasum->work, btrfs_scrub_helper,
				scrub_fixup_nodatasum, NULL, NULL);
		btrfs_queue_work(fs_info->scrub_workers,
				 &fixup_nodatasum->work);
		goto out;
	}

	/*
	 * now build and submit the bios for the other mirrors, check
	 * checksums.
	 * First try to pick the mirror which is completely without I/O
	 * errors and also does not have a checksum error.
	 * If one is found, and if a checksum is present, the full block
	 * that is known to contain an error is rewritten. Afterwards
	 * the block is known to be corrected.
	 * If a mirror is found which is completely correct, and no
	 * checksum is present, only those pages are rewritten that had
	 * an I/O error in the block to be repaired, since it cannot be
	 * determined, which copy of the other pages is better (and it
	 * could happen otherwise that a correct page would be
	 * overwritten by a bad one).
	 */
	for (mirror_index = 0;
	     mirror_index < BTRFS_MAX_MIRRORS &&
	     sblocks_for_recheck[mirror_index].page_count > 0;
	     mirror_index++) {
		struct scrub_block *sblock_other;

		if (mirror_index == failed_mirror_index)
			continue;
		sblock_other = sblocks_for_recheck + mirror_index;

		/* build and submit the bios, check checksums */
		scrub_recheck_block(fs_info, sblock_other, is_metadata,
				    have_csum, csum, generation,
				    sctx->csum_size);

		if (!sblock_other->header_error &&
		    !sblock_other->checksum_error &&
		    sblock_other->no_io_error_seen) {
			if (sctx->is_dev_replace) {
				scrub_write_block_to_dev_replace(sblock_other);
			} else {
				int force_write = is_metadata || have_csum;

				ret = scrub_repair_block_from_good_copy(
						sblock_bad, sblock_other,
						force_write);
			}
			if (0 == ret)
				goto corrected_error;
		}
	}

	/*
	 * for dev_replace, pick good pages and write to the target device.
	 */
	if (sctx->is_dev_replace) {
		success = 1;
		for (page_num = 0; page_num < sblock_bad->page_count;
		     page_num++) {
			int sub_success;

			sub_success = 0;
			for (mirror_index = 0;
			     mirror_index < BTRFS_MAX_MIRRORS &&
			     sblocks_for_recheck[mirror_index].page_count > 0;
			     mirror_index++) {
				struct scrub_block *sblock_other =
					sblocks_for_recheck + mirror_index;
				struct scrub_page *page_other =
					sblock_other->pagev[page_num];

				if (!page_other->io_error) {
					ret = scrub_write_page_to_dev_replace(
							sblock_other, page_num);
					if (ret == 0) {
						/* succeeded for this page */
						sub_success = 1;
						break;
					} else {
						btrfs_dev_replace_stats_inc(
							&sctx->dev_root->
							fs_info->dev_replace.
							num_write_errors);
					}
				}
			}

			if (!sub_success) {
				/*
				 * did not find a mirror to fetch the page
				 * from. scrub_write_page_to_dev_replace()
				 * handles this case (page->io_error), by
				 * filling the block with zeros before
				 * submitting the write request
				 */
				success = 0;
				ret = scrub_write_page_to_dev_replace(
						sblock_bad, page_num);
				if (ret)
					btrfs_dev_replace_stats_inc(
						&sctx->dev_root->fs_info->
						dev_replace.num_write_errors);
			}
		}

		goto out;
	}

	/*
	 * for regular scrub, repair those pages that are errored.
	 * In case of I/O errors in the area that is supposed to be
	 * repaired, continue by picking good copies of those pages.
	 * Select the good pages from mirrors to rewrite bad pages from
	 * the area to fix. Afterwards verify the checksum of the block
	 * that is supposed to be repaired. This verification step is
	 * only done for the purpose of statistic counting and for the
	 * final scrub report, whether errors remain.
	 * A perfect algorithm could make use of the checksum and try
	 * all possible combinations of pages from the different mirrors
	 * until the checksum verification succeeds. For example, when
	 * the 2nd page of mirror #1 faces I/O errors, and the 2nd page
	 * of mirror #2 is readable but the final checksum test fails,
	 * then the 2nd page of mirror #3 could be tried, whether now
	 * the final checksum succeedes. But this would be a rare
	 * exception and is therefore not implemented. At least it is
	 * avoided that the good copy is overwritten.
	 * A more useful improvement would be to pick the sectors
	 * without I/O error based on sector sizes (512 bytes on legacy
	 * disks) instead of on PAGE_SIZE. Then maybe 512 byte of one
	 * mirror could be repaired by taking 512 byte of a different
	 * mirror, even if other 512 byte sectors in the same PAGE_SIZE
	 * area are unreadable.
	 */

	/* can only fix I/O errors from here on */
	if (sblock_bad->no_io_error_seen)
		goto did_not_correct_error;

	success = 1;
	for (page_num = 0; page_num < sblock_bad->page_count; page_num++) {
		struct scrub_page *page_bad = sblock_bad->pagev[page_num];

		if (!page_bad->io_error)
			continue;

		for (mirror_index = 0;
		     mirror_index < BTRFS_MAX_MIRRORS &&
		     sblocks_for_recheck[mirror_index].page_count > 0;
		     mirror_index++) {
			struct scrub_block *sblock_other = sblocks_for_recheck +
							   mirror_index;
			struct scrub_page *page_other = sblock_other->pagev[
							page_num];

			if (!page_other->io_error) {
				ret = scrub_repair_page_from_good_copy(
					sblock_bad, sblock_other, page_num, 0);
				if (0 == ret) {
					page_bad->io_error = 0;
					break; /* succeeded for this page */
				}
			}
		}

		if (page_bad->io_error) {
			/* did not find a mirror to copy the page from */
			success = 0;
		}
	}

	if (success) {
		if (is_metadata || have_csum) {
			/*
			 * need to verify the checksum now that all
			 * sectors on disk are repaired (the write
			 * request for data to be repaired is on its way).
			 * Just be lazy and use scrub_recheck_block()
			 * which re-reads the data before the checksum
			 * is verified, but most likely the data comes out
			 * of the page cache.
			 */
			scrub_recheck_block(fs_info, sblock_bad,
					    is_metadata, have_csum, csum,
					    generation, sctx->csum_size);
			if (!sblock_bad->header_error &&
			    !sblock_bad->checksum_error &&
			    sblock_bad->no_io_error_seen)
				goto corrected_error;
			else
				goto did_not_correct_error;
		} else {
corrected_error:
			spin_lock(&sctx->stat_lock);
			sctx->stat.corrected_errors++;
			spin_unlock(&sctx->stat_lock);
			printk_ratelimited_in_rcu(KERN_ERR
				"BTRFS: fixed up error at logical %llu on dev %s\n",
				logical, rcu_str_deref(dev->name));
		}
	} else {
did_not_correct_error:
		spin_lock(&sctx->stat_lock);
		sctx->stat.uncorrectable_errors++;
		spin_unlock(&sctx->stat_lock);
		printk_ratelimited_in_rcu(KERN_ERR
			"BTRFS: unable to fixup (regular) error at logical %llu on dev %s\n",
			logical, rcu_str_deref(dev->name));
	}

out:
	if (sblocks_for_recheck) {
		for (mirror_index = 0; mirror_index < BTRFS_MAX_MIRRORS;
		     mirror_index++) {
			struct scrub_block *sblock = sblocks_for_recheck +
						     mirror_index;
			int page_index;

			for (page_index = 0; page_index < sblock->page_count;
			     page_index++) {
				sblock->pagev[page_index]->sblock = NULL;
				scrub_page_put(sblock->pagev[page_index]);
			}
		}
		kfree(sblocks_for_recheck);
	}

	return 0;
}

static int scrub_setup_recheck_block(struct scrub_ctx *sctx,
				     struct btrfs_fs_info *fs_info,
				     struct scrub_block *original_sblock,
				     u64 length, u64 logical,
				     struct scrub_block *sblocks_for_recheck)
{
	int page_index;
	int mirror_index;
	int ret;

	/*
	 * note: the two members ref_count and outstanding_pages
	 * are not used (and not set) in the blocks that are used for
	 * the recheck procedure
	 */

	page_index = 0;
	while (length > 0) {
		u64 sublen = min_t(u64, length, PAGE_SIZE);
		u64 mapped_length = sublen;
		struct btrfs_bio *bbio = NULL;

		/*
		 * with a length of PAGE_SIZE, each returned stripe
		 * represents one mirror
		 */
		ret = btrfs_map_block(fs_info, REQ_GET_READ_MIRRORS, logical,
				      &mapped_length, &bbio, 0);
		if (ret || !bbio || mapped_length < sublen) {
			kfree(bbio);
			return -EIO;
		}

		BUG_ON(page_index >= SCRUB_PAGES_PER_RD_BIO);
		for (mirror_index = 0; mirror_index < (int)bbio->num_stripes;
		     mirror_index++) {
			struct scrub_block *sblock;
			struct scrub_page *page;

			if (mirror_index >= BTRFS_MAX_MIRRORS)
				continue;

			sblock = sblocks_for_recheck + mirror_index;
			sblock->sctx = sctx;
			page = kzalloc(sizeof(*page), GFP_NOFS);
			if (!page) {
leave_nomem:
				spin_lock(&sctx->stat_lock);
				sctx->stat.malloc_errors++;
				spin_unlock(&sctx->stat_lock);
				kfree(bbio);
				return -ENOMEM;
			}
			scrub_page_get(page);
			sblock->pagev[page_index] = page;
			page->logical = logical;
			page->physical = bbio->stripes[mirror_index].physical;
			BUG_ON(page_index >= original_sblock->page_count);
			page->physical_for_dev_replace =
				original_sblock->pagev[page_index]->
				physical_for_dev_replace;
			/* for missing devices, dev->bdev is NULL */
			page->dev = bbio->stripes[mirror_index].dev;
			page->mirror_num = mirror_index + 1;
			sblock->page_count++;
			page->page = alloc_page(GFP_NOFS);
			if (!page->page)
				goto leave_nomem;
		}
		kfree(bbio);
		length -= sublen;
		logical += sublen;
		page_index++;
	}

	return 0;
}

/*
 * this function will check the on disk data for checksum errors, header
 * errors and read I/O errors. If any I/O errors happen, the exact pages
 * which are errored are marked as being bad. The goal is to enable scrub
 * to take those pages that are not errored from all the mirrors so that
 * the pages that are errored in the just handled mirror can be repaired.
 */
static void scrub_recheck_block(struct btrfs_fs_info *fs_info,
				struct scrub_block *sblock, int is_metadata,
				int have_csum, u8 *csum, u64 generation,
				u16 csum_size)
{
	int page_num;

	sblock->no_io_error_seen = 1;
	sblock->header_error = 0;
	sblock->checksum_error = 0;

	for (page_num = 0; page_num < sblock->page_count; page_num++) {
		struct bio *bio;
		struct scrub_page *page = sblock->pagev[page_num];

		if (page->dev->bdev == NULL) {
			page->io_error = 1;
			sblock->no_io_error_seen = 0;
			continue;
		}

		WARN_ON(!page->page);
		bio = btrfs_io_bio_alloc(GFP_NOFS, 1);
		if (!bio) {
			page->io_error = 1;
			sblock->no_io_error_seen = 0;
			continue;
		}
		bio->bi_bdev = page->dev->bdev;
		bio->bi_iter.bi_sector = page->physical >> 9;

		bio_add_page(bio, page->page, PAGE_SIZE, 0);
		if (btrfsic_submit_bio_wait(READ, bio))
			sblock->no_io_error_seen = 0;

		bio_put(bio);
	}

	if (sblock->no_io_error_seen)
		scrub_recheck_block_checksum(fs_info, sblock, is_metadata,
					     have_csum, csum, generation,
					     csum_size);

	return;
}

static inline int scrub_check_fsid(u8 fsid[],
				   struct scrub_page *spage)
{
	struct btrfs_fs_devices *fs_devices = spage->dev->fs_devices;
	int ret;

	ret = memcmp(fsid, fs_devices->fsid, BTRFS_UUID_SIZE);
	return !ret;
}

static void scrub_recheck_block_checksum(struct btrfs_fs_info *fs_info,
					 struct scrub_block *sblock,
					 int is_metadata, int have_csum,
					 const u8 *csum, u64 generation,
					 u16 csum_size)
{
	int page_num;
	u8 calculated_csum[BTRFS_CSUM_SIZE];
	u32 crc = ~(u32)0;
	void *mapped_buffer;

	WARN_ON(!sblock->pagev[0]->page);
	if (is_metadata) {
		struct btrfs_header *h;

		mapped_buffer = kmap_atomic(sblock->pagev[0]->page);
		h = (struct btrfs_header *)mapped_buffer;

		if (sblock->pagev[0]->logical != btrfs_stack_header_bytenr(h) ||
		    !scrub_check_fsid(h->fsid, sblock->pagev[0]) ||
		    memcmp(h->chunk_tree_uuid, fs_info->chunk_tree_uuid,
			   BTRFS_UUID_SIZE)) {
			sblock->header_error = 1;
		} else if (generation != btrfs_stack_header_generation(h)) {
			sblock->header_error = 1;
			sblock->generation_error = 1;
		}
		csum = h->csum;
	} else {
		if (!have_csum)
			returnF:eendped_! a mirror urnF:eendpput_pag(u32)0;
	voi;
		isum_s_UUID_tadat
	voif (!p(,
		ffer = kmap_ato)v = generation,
				 intisu,node *inode = generation,
					btrfs_dev_isum_s_UUID_tadat
	voier = kmap_ato, isu,node *inod_metadkun = h->csum;er = kmap_ato)leave_nomum_er;mirror urnF:eend>=error = 0;
	sblock-PER_SCTX; ++i)ted_csum[BTRFS_CSUM_SIZ_num++) {(!have_csrror = 1;
		}
		csum = h->csum;
	} else {
		i_num++) {(!have_csllu on dev tadat	 * t(isu,nst u8 *csum, u6 crc = ~tenr(h)st u8 *csum, u6echeck_btadata, ino_io_page_num;

	sblock->no_i1agev[page_index]);
			lse {
				int force_write = uct scrub_block *sblocks_for_rsum(fs_info,uct scrub_block *sblocks_fot mirum(fs_info, {
				scrub_wrnt have_csum,
					 age)
{
o_io_error_seen = 1;
	sblock->header_error =not_correct_error;

	success = 1;
age)
{
cal csrro
{
cal ther->pagev[
							page_num];

			iocks_for_rsum(fs_um_sizcks_fot mirum(fs_um_siret = scrum(fs_um_sirub_repair_block REQ_GEcal >generatirty 
cal cs	kfree(bbio)ret;

	ret = medex]);
			lse {
					page_num];

			ioct scrub_block *sblocks_for_rsum(fs_infouct scrub_block *sblocks_fot mirum(fs_infove_csum,
			,, {
				scrub_wrnt ha	for (page_num = 0; page_num < sblock_bad->page_count; pagea	for (page_num = 0; page and =izcks_fot mint; page_num++) {
		s logical;
		_bad->pagruct scru;	s logical;
		t mint; paruct scru;	ss_ext		scrub_wri||< sblock_bad- else if (genhead(!sblock_bad->hem;

	sblock->no||<r = 0;
					break; /* su0;

	for (page_num age)
{
	strum++) {
		struct_per_rd_biN_ON(!psctx->stat.uncorr->fspath->val[i]);

	f		printk;
			lse {
					page_num];

			i		struct scrub		printtheuncorrespo!\n"bbio, 0);
		if (ret || !bbock->no_io_error_seen = 0;
			continue;
		}

		WARio, 0);
		if (ret |age->io_error = 1;
struct_per_rd_bno_io_error_seen = 0;
			continue	_bad->	}
		bio->bi_bdev of PAG= page->dev->bdev;
			t mint; pai_iter.bi_sector = pageode *inode!rom_good_coptrfsic_submit_b, 0);
		if (ret || !bbpage->physical >> 9;

		bio_WRITEpage(biod_copttat_inc_and_print(dev,
				B 1;
struct_peS_DEV_STAT_GENERATIONWRITEror)
			btite_page_to_dev_replace(
						sbl&lock_bad->he_bad, page_num);
				if (ret)
			btrfs_dev_replace_stats_inc(
		trfsic_submit_b, 0);
		if (ret || (btrfsic_submit_bio_wng long)swarn->sector, root, inck_other->no_io_error_seen) h);
	return 0;
}

static void ta,
				int haveor_seen = 1;
	sblock->header_error = 0;
	sblock->checksum_error = age)
{
	struefore
				 * submitting the write requ	if (sblhecksum_ts(&BTRFS_I(i	btite_page_to_dev_replace(
						sbl&lock_bhe_bad, page_num);
				if (	btrfs_dev_replac_replace_stats_inc(
}

	ret = medex]);
			 submitting the write requ	oid scrub_recheck_block_checksum(fove_csum,
			nt ha	for (page_num = 0;sock->page_count; page_num++) {
		sgev[0]->v;
			page->ct scru;	ss_exspage->page);
	ror = t page_num;
	u8 calcsum = h->csum;
ror = 1;
			srroreme, oer = kmap_ato, 0,ndex);
	if (I
					btflush_d		 * ->dev-
ror = 1;
			sadkun = h->csum;er = kmap_ato)lea_replace);
	e_nuage->devg thwr9;

(lock_bhe_bad,		   s);

	ret = medex]);
			age->devg thwr9;

(l_for_recheck);
	}

	return 0;
}ze);

	return;
}

static inline int );
			 sk);
	} sk);
	=init_waitqueuegea	for (page_nu>dev_root;
 age)
{
	strmutexkzalloctqueueaitqutat_locgh thgical %!tqueueaitqut th_	WARN_ON(tqueueaitqut th_	WAk = sblocks_for_rectqueueaitqut th_	WARrum(fs_info,
		 * used.
		 */
	tqueueaitqut th_	WARN_ON(rmutexk	spin_lotqueueaitqutat_loc.malloc_errors++;
			}ON(tqueueaitqut th_	WA			continue;

			tqueueaitqut th_	WA		;
	sblock-S, 1);
>bio	WAk =tqueueaitqut th_	WA;	ss_exs	WA		;
	sblock-S,ite_page_0;

	for (page_nu
omem;
		sblock->pag
ror = 1ysical_for_dev_replace =
		em;
		k);
		retur
			}
			scru=
		em;
		rror =tqueueaib_setu	sblock->nso_erroroe;
		}

		WARN_ON(!ock->no_io_error_seen = 0;
			contitqueueaie tho_mmitwr9;

index + 1;
	WARN_ON(!rmutexk	spin_lotqueueaitqutat_loc.maalloc_errors++;
				spin_uo_erroro PAG= et || !bbockrror_
		vate->nso_eno_io_error_ = 0;
re
				 * scount = 0;
et |age->io_error =em;
		rror_rd_bno_io_error_seen = 0;
			contem;
		sblock->p);
	if _uo_erre);S, 1);
>v_stat_inc_a;
		sblock->p+ s	WA		;
	sblock-St I/O errore!rader_b
ror = 1ysical_for_dev_replace _header_bem;
		k);
		re+ s	WA		;
	sblock-St I/O errore!rader_b
ror = k);
		rx, fs_info,
 scal >> mirror_numcountgh tht_bio_wng  PAG= page->dev-uo_erroro, 
ror = 1;
	i_iter.bi_sector = REQ_GET!roode *inod_a mirror s	WA		;
	sblock-Sk->pod_coptrfsic_suo_errorolock);
	_erroro PAcount;
		mutexk	spin_lotqueueaitqutat_loc.malloc_erro(ret || (binfo,
 scal >> mirror_numcountgh tht_bio_ws	WA		;
	sv[s	WA		;
	sblock-]etur
			;
_unlock(&sctx->s	   s);
	s	WA		;
	sblock-er;mis_exs	WA		;
	sblock-S,ittqueueaie tho_mmitwr9;

i (binfo,
 scal >> mirror_numutexk	spin_lotqueueaitqutat_loc_wng long)swarn->sector, root, inckcal >> mi_for_recheck);
	}

	r inline int );
			 sk);
	} sk);
	=init_waitqueuegea	for (page_nu>dev_root;
ical %!tqueueaitqut th_	WAR
ock->sctx->de	WAk =tqueueaitqut th_	WA;	stqueueaitqut th_	WAk =count;
ted_csum[BT_erroro->io_erro);r_deref(fixup->dtrfsm->mirror_nu/* not usled to submsev[0]->p->le 	scrub th wil.as an I/O check_ge bi, whenase,sages
	 * bio cst be lsixup->agesmpage)) {
		vethe othectorsogotoderror), by
ntinormime, l impinuct es
	 * are goeaath
doverwritt Linux 3.5	phys->physical >> 9;

_WRITEpauo_errorolocarn->sector, root, inckcount = 0;
(0;

	for (page_,, {
	e); inline int );
			>dev_root PAG= rror_
		vate;uct scrub_ctx *sctx = sblock_tor =em;
		rror_age_count < 1);
	dev uo_erre);S, ;
	if uo_erroro PAG= et
m = failed_mirror_uo_errb_pending_trans_wwrcorkers_inc(soot, inckcount = 0;
		scrubk(&fixup_nodatasm->work, btrfs_scrub_helper,
				qutthe blhead	scrub_f _uo_errb_pelocarn->sector, root, inckcount = 0;
d	scrub	 */
		return 1;
	}

	return ne int );
			>dev_root PAns = NULL;
	struct btrfs_path *pge_,,ctable = nt scrub_handle_errored_blWA			con;
 age)i
	u64 flags = WA		;
	sblock-S>< sublen) {
			kfWRe(bbio);s_exs	WA		e); d *mapped_buffer;
ev_replace _*G_ON(page_index >&blWA			con, page_num);
				if (	btrfs_dev_ = sblock_might have = WA		;
	sblock- har_index = 0; mirror_inm = 0;sock->pageWA		;
	sv[i			struspage->page);
	neration te_page_to_dev_replace(
				&e_to_dev_re (ret)
		fo,
_replace_stats_inc(
	}sum)
			retmight have = WA		;
	sblock- har_i
		     page_index++WA		;
	sv[i	loc_wtrfsic_suo_errorolocklock = Nrolockderef(fixup->dtrfs

	btrfs_free_et = medex]);
			id, BTRFS_UUID_Seturn 0;
}

static void ct scrub_;
 age)
{
	str4 flags = 0;
	u64 ref_root;
	u32 icrub_war
	} else {
		if (!crub_;
 
{
o_io_efs_extent_item);
	item_size = bs_mettruefore
				 *m;

	sblo
	voitatic v_ef_stat_inctent_item);
	item_size = btrfs_item_struefore
				 *m;

	sbloots[0]he jusatic v_ef_stat_inctent_item);
	item_size = bot->fstru(r, r)				 *m;

	sblocommiusatic v_ef_sta+i)ted_csum1tx, fs_info,
		     pk by using the
 * contbtat_loc_wng longret;

	ret = medex]);
			m;

	sblo
	voith);
	return 0;
}

static void scrub_pub_handle_errored_block(			con;
 cons u64 generation,
					 u8 *eads
	 		BTRFS_rub_fixup_readpag;int pageap_atomi u16 csum_size)
{
	ins_info * arning swato leaage)i *sblockgev[0]->logicu64 ref_root;
	u32 is_metadata,ev[0]->flags &
			BTRFS);
	}

ouo_erreads
	 		BTRwar
	} else {
		if (!	BTRFS_ock->page_count; pagef (!havetasm8 calcsum = h->csum;1;
			srrecheckck(&sctve_csur = 0; in the bloc		ret_! a mir swateck procedure
	 *,node *inod_metadisum_s_UUID_tadat
	voiap_ato, isu,nl		sadkun = h->csum;ap_ato)leaveche-= le;
		}

echec=tent_biCTX; ++i)em;
		}
		kgev[0]-	page->phbbio->stripes[mirror_ingev[0]-tadata,ev[0]->f);
			pa1;
			sadock->page_count; page);
			pa1;
	no_io8 calcsum = h->csum;1;
			sllu on dev tadat	 * t(isu,ns u6 crc = ~tenr(h)seck_beads
	 		BTR sblock_bad,
					nt_bfo * ar1oc_wng longfo *;

	ret = medex]);
			m;

	sbloots[0]he jush);
	return 0;
}

static void scrub_pub_handle_errored_block(			con;
 pped_buffer;

	WARN_ON(!LL;
	struct btrfs_fsm->dev 	con, page_num;uct scrub_ctx *sctx = sblock_tor =URST);

	BUG_ON(const u8 *csum, u64 generation,
					 u8beads
	 		BTR4 generation,
					 rub_fixup_readpag;int pageer = kmap_atomi;
	while (leur = 0;t pagepmi u16 csum_size)
{
	ins_info * arningng inrcofo * arning swato leaage)i *sblockgev[0]->logicu64 ref_root;
	u32 iock->page_count; pagef (!havetas_num;
	u8 calcsum = h->csum;1;
			sls_header *h;

		mapped_buffer = kmap_atomioremcpy(eads
	 		BTR sblock->ck(fs_info, sblock_badsblocksw the
'/*
	 ption , sinclen;
		lsGE_SI, any*
	 * ta)the
'/*he mirn read alu8 calcarder tob)checksum
	e mel wilyum = p
dover/o_wait(READ, bi[0]->page);
		h = (struct btrfs_header *)mapped_bnt_b++fo *;
_wait(READ, bi[0]->page)d,
			   BTRFS_UUID_SIZE)) {
			sblock->header_t_b++fo *;
_wait(if (sblock->pagev[0]->logical != btrfs_stack_t_b++fo *;
_wait(tenr(h) ||
		    !scrub_check_fsid(h->fsid, sblock->pagv[0]) ||
		    memcmt_b++fo *;
_wecheckck(&sche faloce = generation,
		tas_num;
	aloce=node *inode = generation,
		2 io_hea,
		ffer = kmap_ato)v = generation,
		 0; in the bloc		ret_! a mir swateck procedure
	 *,n_num;
	aloc_metadisum_s_UUID_tadat
	voip, isu,nl		sadkun = h->csum;er = kmap_ato)leaveche-= le;
		}

echec=tent_biCTX; ++i)em;
		}
		kgev[0]-	page->phbbio->stripes[mirror_ingev[0]-tadata,ev[0]->f);
			pa1;
			sadock->page_count; page);
			pa1;
	no_i_num;
	u8 calcsum = h->csum;1;
			sls_num;
	aloce=node *inod	sadoeck r = kmap_atomiolu on dev tadat	 * t(isu,nst u8 *csum, u6 crc = ~tenr(h)st u8 *csum, u6eceads
	 		BTR sblock_bad,
					nt_b++nrcofo *oc_wng longfo *o||<nrcofo *oc
	ret = medex]);
			m;

	sblocommiush);
	return 0;
}

static void scrub_p_UUID_Sommit0;
}

ste = nt scrub_handle_errored_block(			con;
 const u8 *csum, u64 generation,
					 u8beads
	 		BTR4 generation,
					 rub_fixup_readpag;int pageer = kmap_atomi;
	while (leur = 0;t pagepmi u16 csum_size)
{
	ins_info *sblo arningng ifo *sce)
{
	int swato leaage)i *sblockgev[0]->logicu64 ref_root;
	u32 iock->page_count; pagef (!havetas_num;
	u8 calcsum = h->csum;1;
			sls_header *h;

		maSommit0;
}

sfer = kmap_atomioremcpy(eads
	 		BTR sslock->ck(fs_info, sblock_badait(READ, bi[0]->page);
		h = (struct btommit0mappedscmt_b++fo *sce);
_wait(READ, bi[0]->page)d,
			   BTRFS_UUID_Sommitblock->headscmt_b++fo *sgen;
_wait(if (sblock->pagev[s]->logical != btrfs_stack_t_b++fo *sce);
_wecheck generot->f_INFO*inode = generation,
		2 i_num;
	aloce=node *inode = generation,
		2 io_hea,
		ffer = kmap_ato)v = generation,
		 0; in the bloc		ret_! a mir swateck procedure
	 *,n_num;
	aloc_metadisum_s_UUID_tadat
	voip, isu,nl		sadkun = h->csum;er = kmap_ato)leaveche-= le;
		}

echec=tent_biCTX; ++i)em;
		}
		kgev[0]-	page->phbbio->stripes[mirror_ingev[0]-tadata,ev[0]->f);
			pa1;
			sadock->page_count; page);
			pa1;
	no_i_num;
	u8 calcsum = h->csum;1;
			sls_num;
	aloce=node *inod	sadoeck r = kmap_atomiolu on dev tadat	 * t(isu,nst u8 *csum, u6 crc = ~tenr(h)st u8 *csum, u6eceads
	 		BTR sblock_bad,
					nt_b++fo *sce);
_wait(fo *sce)
+nfo *sblos_info;
	if (sblock_to_check->pagev[0]->flags & BTRFS_EXTENT_FLAG_SUPER) {
		/*
		 * if we find an error in a super block, we just report it.
		 * They will get written with the next transaction commit
		 * anyway
		 */
		spin_lock(&sctx->stat_ait(fo *sce)r error",
					    sblock_to_checkREAD, bi[0]->page)TRFS_DEV_STAT_GENERATION_ERRS);
		else
			btrfs_dev_stat_inc_and_print(dev,
				BREAD, bi[0]->page)TRFS_DEV_STAT_GENERATIONeration_error)
			b}c_wng longfo *sce)
+nfo *sbloocarn->sector, root, incks_fotetush);
	return 0;
}

static void ->csum
				&READ, biage_indexlocarn->sector, root, incks_foic_suh);
	return 0;
}

static void tit(->csum
det(dev,, an	&READ, biage_indexlror = age)i = sblock_might have = or = 0;
	sblock->car_i
			     page_index++) {
				sbloipage_ilock = NULL;nc(
}

	ret = mer, root, in(&sctx->s	e);

	return;
}

static inli->csum
				&Rpage->age_indexlocarn->sector, root, inage_index+e);

	return;
}

static inlitit(->csum
det(dev,, an	&Rpage->age_indexl_a mirror sror = 1;
		
			_ %s\n",
ev-
ror = 1;
			sadkock = 1;
			slluarn->sector, root, inal >> mi_for_recheck);
	}

	r inline int );
			>dev_root;
ical %block_bu);S,= -1R
ock->sctx->de	WAk =block_	WAs[block_bu);			 rlock_bu);S, -1ockderef(fixup->dtrfsm->mirror_n is_metad_erroro->io_erro)or && !sblock_be page
	shdisksck theksum on dGE_SIZE, each re) is causewrocksurewre
	 * determrite);
-(page_inopck->heasfirst of on_sblock->page are gnoo enable es tavo *aI/O e)
		insblock_be page
	rewshdisksel wilyufo *os to ndexER) {
		/e page
	rs from allthe end, t()
		_very_ slowl repai* Theypsctx->stat.uncorr->fspath->val
			i]);

	fot, inal >> m	WAk		struct scrubtheuncorrespo!\n"bbio,ount = 

(lo_erroro, ro(r		sller_error =->physical >> 9;

_add_pauo_errorolock}

	ret = medex]);
			age->devg thrd9;

(l_for_recheck);
	}

	return 0;
}ze);

	return;
}

static inline int );
			0;
}

static etur
			}
int)bbio-	for (page_nu>dev_root;
 age)
{
	stgh thgicsblocksgrab0]-ockshor (p theainow th
		 l
			e datavo *aI/O/*
	 * ks thatblock_bu);S,= -1R BTRFS_DEV_STAT_CORRUPlisev->name));
		}
	bu);S, 
		}
	nnot  %s\ntat_ait(
		}
	bu);S!= -1R BTRF	
		}
	nnot  %s\nk =block_	WAs[block_bu);	-> a s %s\ntat_	block_	WAs[block_bu);	-> a s %s\nS, -1ock_	block_	WAs[block_bu);	->;
	sblock-S, 1);
yway
		 */
		spin_loclisev->name));/* succeededay
		 */
		spin_loclisev->name));	eain_mirrt(in_locliseveain, 
		}
	nnot  %s\nS!= -1R;(
	}sum)de	WAk =block_	WAs[block_bu);			 s_exs	WA		;
	sblock-S,ite_page_0;

	for (page_nu
omem;
		sblock->pag
ror = 1ysical_;
		em;
		k);
		retur
			}
			scru=
		em;
		rror =	   structbio,oun->nso_erroroe;
		}

		WARN_ON(!ock->no_io_error_seen = 0;
			contiseueaie tho_mmitrd9;

index + 1;
	WAR
.maalloc_errors++;
				uo_erroro PAG= et || !bbockrror_
		vate->nso_eno_io_error_ = 0;
re
				 *ount = 0;
et |age->io_error =em;
		rror_rd_bno_io_error_seen = 0;
			contem;
		sblock->p);
	if _uo_erre);S, 1);
>v_stat_inc_a;
		sblock->p+ s	WA		;
	sblock-St I/O errore!rader_b
ror = 1ysical__header_bem;
		k);
		re+ s	WA		;
	sblock-St I/O errore!rader_b
ror = k);
		r_header_bem;
		rror! =	   structx, fs_info,
al >> mirror_numcountgh tht_bio_ws	WA		;
	sv[s	WA		;
	sblock-]etur
			;
_ng  PAG= page->dev-uo_erroro, 
ror = 1;
	i_iter.bi_sector = REQ_GET!roode *inod_a mirror s	WA		;
	sblock-Sk->pod_coptrfsic_suo_errorolock);
	_erroro PAcount;
		lloc_erro(ret || (binfo,
al >> mirror_numcountgh tht_bio_wst, incks_fotetusNULL;nce_nu* that is sup
}

age	}
	}
ock_oth
	 * ->csum
				&READ, bit mirror_index;
	);
	s	WA		;
	sblock-er;mis_exs	WA		;
	sblock-S,itseueaie tho_mmitrd9;

i (binfo,
al >> mirror_n(sblock->pagev[page_index]);
			e tho(l_for_recheck);
	}

	renfo,
				    s_info *>pagv[0s_fs_inf1ysical_, pped_buffer;
ev_ie _*G_O s_inftent_>pagv[0s_fs_infg *,n
				     sn_info,
				str {
				sc>pagv[0s_fs_inf1ysical_for_dev_replace  inline int );
			0;
}

static leaage)i *sblocktatic etu sblocks_for_recsNULL;n,loc(BTRFS_MAX_MIRRORS *
of(*sblocks_for_recheck),
				     GFP_NOFS);
	if (!sblocks_for_rechy
		 */
		spin_lock(&sctx->stat_lock);
rors++;
		fs_dev_* thagewithide-= sublen;
		l, plusu* that i	strup
}

age	}
	}er to goth
 *csrZE
	 * a->csum
e, o&READ, biage_indextinue;
ORS)
				continue;

		_csum, u8 *csum, u64 generatio
			retmO;
		}

		eche>ht ha
	     sblocks_for_rechec;
}

static;mir swateck procedure
	 *,node *inod_metadsock->pa sblocks_for_recsatic t
		 * used.
		 */
	satic >sctx = sctx;
			pa			else
				goto did_not_correct_error;
		}(!page) {
leave_nome:
				spin_lock(&sctx->stat_lock);ot, incks_foic_subtat_lock);lloc_errors++;
			}ON(gev[0]-	page->ph subleock n) {
			kfitem_s;
	_unlock(&sctx->s	   s);
		ge_count; page);
			etur
			;
_	r
			}
int)bb>page_cou;
_	r
			}
_not_correct_r
			}
crub_warcrub_;
 _r
			}
d,
			   BT= g	u64 suror = k);
		r__page_get(pag
ror = 1ysical__= 1ysical_;
		eror = 1ysical_			BUG_ON(page_indf1ysical_for_dev_replace ;
		eror = ev is NULL */
			pag					 	 */
m */
			suceror =  &
			BTRneration remcpy(eror = , u6echeck_bblock_bad,
					    /* succeededayor =  &
			BTRnerr) {
			->stripes[mirror_index].d>v;
			page->mirror_num = mirror_index */
	satic= 1;
		
			count++;
			page->paeche-= le;
	->page)
			le;
	sblock->p+		le;
	sblock->			BUG_ON(page_in+		le;
}str4 flags = 0;
	u64 ref_root;,ite_;
			retmO;
		}

					     mirror_index;
			inha
	     sblocks_for_rechec;
}

static>page_count; page);
			;
= age)
{
	struefore
				 *age->devg thrd9;

(lbad,		   s);
&BTRFS_I(i BTRF	
	, incks_foic_subtat_lock);lloc_er
{
	s{
			/* did n			sci (binfo,
al >> mirror_n(sev_ls wo* thas\ns, eiocks_E_SIZEgev[0oth
tthe blheahat ils wo;
}

s/
	
	, incks_foic_subtat_lockng long)swarn->sector, root, inount = 0;
(0;

	for (page_,, {
	e); inline int );
			>dev_root PAG= rror_
		vate;uct scrub_ctx *sctx = sblock_tor =em;
		rror_age_count < 1);
	dev uo_erre);S, ;
	if uo_erroro PAG= et
m = faik, btrfs_scrub_helper,
				scrub_f _uo_errb_pelocarn->sector, root, inount = 0;
d	scrub	 */
		return 1;
	}

	return ne int );
			>dev_root PAns = NULL;
	struct btrfs_path *pge_,,ctable = nt scrub_handle_errored_blWA			con;
 age)i
	u6gev[0]->lWA		;
	sblock-S>< sublen) {
			kfree(bbio);s_exs	WA		e); d *malock_might have = WA		;
	sblock- har_index = 0; mirror_inm = 0;sock->pageWA		;
	sv[i			struspage->page);
	neration r
			}
int)bb_bio_alloc(GFP_NOFS, 1);
	
						foif (i the blog 512 ;
			0;
}

seemscorrect, mirn.
	 thoi the blodotherwise might have = WA		;
	sblock- har_index  0; mirror_inm = 0;sock->pageWA		;
	sv[i			index = 0; mirror_index < BTRFS_
			}
int)bbio
&BTRFS->csum
det(dev,, an	&READ, bit mirror_index;
	))TRF	
	, incks_fo the blosubtat_lock)
	, incks_foic_subtat_lock}c_wtrfsic_suo_errorolock
	_erroro PAcount;
S_DEV_STAT_CORRUPlisev->name))
	_err a s %s\nS, 
		}
	nnot  %s\ntat
		}
	nnot  %s\nk =b	_errS_MAX_MIay
		 */
		spin_loclisev->name)ical %block_place, pick goe).
	 */
->csum
 wil(nit_waitqueue.flush_rn.place_sl_a mirmutexkzallocit_waitqueue.tqutat_loc.minfo,
 scal >> mirror_nummutexk	spin_loit_waitqueue.tqutat_loc.io_wst, infixup->dtrfs

	btrfs_free_et = mer, root, incks_fo the blosuh);
	return 0;
}

static void tit(!int)bb_bio_alloc(GFP_NOFx, fs_info,
k by using the
 * contbtat_loc /* succeede;
	if (sblohas;
	}

	return 0,r), by
vianum_writmeck bism	insblock_notpick goeage
,mined, whic), by
E_SIZink_notpick gosblockage
epai* Theyait(if (sblock->TRFS_RS *
of&&_block(			con>stat.unverified_errors++;
		spin_unlock(&sctx->stat_lock);
lock}

	ret = medex]);
			to_c		BTR(l_for_recheck);
	}

	renfo,
				    s_info *>pag	 */o,
				svoid scrub_p_UUID_nase,;
	aut;
BTRnercount;
uthigrfs_long)i *sblo
uthigrfs_long)_PAGEve_csue)icks that!lisevemptyloit_waibad,
lise)od pagesRnerlisevnnot  entryloit_waibad,
liseor
		 */
		r scrub_p_UUID_nase,;
	aut,rlise);
&BTRFSL, NU0mappeS><k);
		rxck);lloc_er0;
&BTRFSL, NU0mappeS+ up_nodche>hk);
		rxck);CTX; ++
xt transaction tadat
iscagese;
	-isevdelloip_nodise);
&Bkock = m_ts(&BBTRnercount;
}d tit(!iTRFS);
	}

ouo_errmO;
		}
(ze)
{(->page)
- L, NU0mappe)od/kck(&sctve_csur = 0;_PAGEve_csuk =bp_nodche/kck(&sctve_csur = 0;remcpy(heck_bb
		ifr_re+)i *sb_bblock_bad,
					    = ~(  mirror_PAGEve_csuk-->pod_co-isevdelloip_nodise);
&Bkock = m_ts(&_replace);1
		lengrechec read alfinas				}
lbe
	 up				infkBhat i	struoth
	 *et = medex]);
			read a(l_for_recheck);
	}

	renfo,
				    s_info *>pag	_inf1ysical_, pped_buffer;
ev_ie _*G_O s_inftent_>pag	_infg *,n
				     sn_infoinf1ysical_for_dev_replace  inliage)
{
	s cons u64 generation,
					 u32s ref_cr = 0efs_extent_item);
	item_size = bs_metror =-ref_cr =eckck(&sctve_csur = 0;blocks_for_recheck),
				     GFP_NOFS);
	i
	vo	read atrans_wbeddex].d>_NOFS);
	i
	vo	e to rans_wbedn+		l	u64 sur				spin_lock(&sctx->stat_lock>v_stat_inctent_item);
	item_size = btrfs_item_sror =-ref_cr =eckck(&sche faloc 0;blocks_for_recheck),
				     GFP_NOFS);
	i, sblread atrans_wbeddex].d>_NOFS);
	iots[0] to rans_wbedn+		l	u64 sur				spin_lock(&sctx->stat_lock>v_stator =-ref_cr =eckck(&sctve_csur = 0;bted_csum1tx, }
ocks that ar a mir swateck procedure
	 *,n-ref_cr =);
= age) &
			BTRnerr) 
&BTRFStent_item);
	item_size = bs_metror =u/* nushns u6s				soth
	 * 		 &
			BTRner);
			to_c		BTR(l
	ren				    sl,ns u6 crc&BTRFS &
			BTRne=tent_bit transaction io_	BTRFS_lock_other->checksum_errorf&&_!e from */
			sucuefore

			_nocow	e tho(l
	ren				    sl,
et)
		fo,
);
		for ( scrum(fs_uo,
);
1ysical_for_dev_replace  FS_lo	countbeho_c	);
			e thoFS_lodex]->sbefore
				 *e tho(l
	ren				    sl,f1ysical_, G_O stent_>fg *,r
		 */	     sn_inf &
			BTRn?ns u6 :(&fixup0,r
		 */1ysical_for_dev_replace  FSbeho_c	);
			e tho:
&BTRFS_I(ick);lloc_er
{
	s{
eche-= le;
	->page)
			le;
	sblock->p+		le;
	sblock->			BUG_ON(page_in+		le;
}see(bbio);
		length -Giirroa/1ysical_
ageress,or #3 cical t u8 *cs
se' * er->page)
offset.t_inr #3 e me
	 	spy_bio *bsurewcicale(bbio* to tak	 * whefte_indebio *b'sr->page)
offset.* t
 = NUbbio);t_inuld baybeindebio *b, 1goeans
	 	spy_bio *b* the pages tage)get_raid56_->pag_offset(_inf1ysical_, age) scrum(fs	r scrub_pE, elookup	eer nfoinf*offset inliage)i;
 age)j
{
	int swabio *b_nomi;
	whls w_offset;
 age)bio *b_truct scrub_bo
	strls w_offset	}
(1ysical_
-pE, ce;
			/* +) {age->logi) *pagv[0s_fnrt
	voGES_PER_;er  FS_*offset	}
ls w_offset;
 wise might have nrt
	voGES_PER_;er  Fhar_index *offset	}
ls w_offsete+)iof o, ce;
			/_len;
_w	bio *b_no	}
*offset;
 	d(&siv(bio *b_no, o, ce;
			/_len);
 	d(&siv(bio *b_no, nrt
	voGES_PER_;er  )index)
	W;
	}oor #1 f	pagebo
	   BTonnr #3 ES_PER-seteength >dev d(&siv(bio *b_no, o, ce_PAGES_PER_);
 	512 b u8 *cs
rors aES_PERnr #3 einderefabmseength >de+		i;_w	bio *b_mO;
		}
 >de% o, ce_PAGES_PER_;
&BTRFSLio *b_mO;
		}or_PAxck);lloc_er0;
&BTRFSLio *b_mO;
		<r_PAxck);j	}
		kf *offset	}
ls w_offsete+)jof o, ce;
			/_len;
eplace);1
		lepages tnohave_c			BUSIZE)ndex]);
			}
			/(l_for_recheck);
	}

	return s	r scrub_pE, elookup	eer nturn s	r scrub_pffer;
ev_ie _*echeckTRFS_DEV_m(fove_cn_infoinfbge
,m_info *fs_i_DEV_m(fove_ctat.unverified_eid scrub_p_UUID_patheadptN(!LL;
	struct btsctx = sblock_tor =econ, page_num);
				if(!LL;
	struct btrfs_fsm->dev rub_helperead ae_num;uct scrub_ctx *rfs_fsbad,
m->dev rub_helpebad,
m->d;uct scrub_ctx *read aeseem *read a;uct scrub_lk_plug plug;d ct scrub_;
 age)
{
	s age)bl>d;ucct snES_PER_;
&t scrubread aeu8 calc*l;uct scrub_ctx *key key;ucct s1ysical_;
	fo,
				   ;
	fo,
				 t = ;ucct s1ysical_t = ;ucct sd,
			   B	s age)
			pag					 t scrubr, ev_ns =rolfsm, ev1		 t scrubr, ev_ns =rolfsm, ev2;uct scrub_ctx *key keyUSIZrd;uct scrub_ctx *key keyU = ;ucct sincr	 * av= o, ce;
			/_len;
ect soffset;
 ct sread ae				   ;
	fo,
read ae1ysical_;
	fo,
read ae	en;
escrub_pffer;
ev_ie _*read aeorrect {
	eead ae
			pag					 age)bio eloopnerr) 
&nES_PER_ =fo *fs_		 1ysical__= E, ce;
			/* +) {age->logi		 offset	}
0;
&d(&siv(nES_PER_, o, ce;
			/_len);
  = ~t, cetyERntem);
	iitem__GROUP_RAIDe_page_offset	}
o, ce;
			/_lencks_				 	 ncr	 * av= o, ce;
			/_lenof o, ce_PAGES_PER_;
&Bev is NULL */1ock>v_stat_inct, cetyERntem);
	iitem__GROUP_RAID10ror = age)fa			conto, ce_PAGES_PER_e/ko, ce;			}
			/_;
&Boffset	}
o, ce;
			/_lencks(ULL /ko, ce;			}
			/_)		 	 ncr	 * av= o, ce;
			/_lenof fa			c;
&Bev is NULL */ULL %ko, ce;			}
			/_v = bbi>v_stat_inct, cetyERntem);
	iitem__GROUP_RAID1ror = agcr	 * av= o, ce;
			/_len;
eBev is NULL */ULL %ko, ce_PAGES_PER_e = bbi>v_stat_inct, cetyERntem);
	iitem__GROUP_DUPror = agcr	 * av= o, ce;
			/_len;
eBev is NULL */ULL %ko, ce_PAGES_PER_e = bbi>v_stat_inct, cetyERnte(m);
	iitem__GROUP_RAID5 |
DEV_STAT_Gitem__GROUP_RAID6)od pagget_raid56_->pag_offset(1ysical_, n_infer nf&offset 		 	 ncr	 * av= o, ce;
			/_lenof nrt
	voGES_PER_;er  FS_Bev is NULL */1ock>v_statr = agcr	 * av= o, ce;
			/_len;
eBev is NULL */1x, }
ocpathe>no_io_eirror_nuth(_MAX_MIRRnuthFS);
	}

ourors++;
	adsblocksw;
	}o we just=URSTrs hapre *csuf	page-ref_c es tpages ta	int rlong)as COWd bayppliards verioeans,nuld bas, miing good cng_pagesmiing g_writ	page for chritten.
isk r goeaondi>heas/*
	 * nuthsctvarch_e just
m->dev 1x, nuthsctkipstat_blocratio
	sblocks
		ggerages
	  ev {
	hat i	ead alfieens u6 fieentioneainow tlockstthe blhea. Durbloc	  ev {
	,g 512 ;
		s on fficiirry
	 utstlocks
osck thol I/Ofuper block, we justs/*
	 * k)