/*
 * Copyright (C) 2008 Red Hat.  All rights reserved.
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

#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/math64.h>
#include <linux/ratelimit.h>
#include "ctree.h"
#include "free-space-cache.h"
#include "transaction.h"
#include "disk-io.h"
#include "extent_io.h"
#include "inode-map.h"

#define BITS_PER_BITMAP		(PAGE_CACHE_SIZE * 8)
#define MAX_CACHE_BYTES_PER_GIG	(32 * 1024)

static int link_free_space(struct btrfs_free_space_ctl *ctl,
			   struct btrfs_free_space *info);
static void unlink_free_space(struct btrfs_free_space_ctl *ctl,
			      struct btrfs_free_space *info);

static struct inode *__lookup_free_space_inode(struct btrfs_root *root,
					       struct btrfs_path *path,
					       u64 offset)
{
	struct btrfs_key key;
	struct btrfs_key location;
	struct btrfs_disk_key disk_key;
	struct btrfs_free_space_header *header;
	struct extent_buffer *leaf;
	struct inode *inode = NULL;
	int ret;

	key.objectid = BTRFS_FREE_SPACE_OBJECTID;
	key.offset = offset;
	key.type = 0;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		return ERR_PTR(ret);
	if (ret > 0) {
		btrfs_release_path(path);
		return ERR_PTR(-ENOENT);
	}

	leaf = path->nodes[0];
	header = btrfs_item_ptr(leaf, path->slots[0],
				struct btrfs_free_space_header);
	btrfs_free_space_key(leaf, header, &disk_key);
	btrfs_disk_key_to_cpu(&location, &disk_key);
	btrfs_release_path(path);

	inode = btrfs_iget(root->fs_info->sb, &location, root, NULL);
	if (!inode)
		return ERR_PTR(-ENOENT);
	if (IS_ERR(inode))
		return inode;
	if (is_bad_inode(inode)) {
		iput(inode);
		return ERR_PTR(-ENOENT);
	}

	mapping_set_gfp_mask(inode->i_mapping,
			mapping_gfp_mask(inode->i_mapping) & ~__GFP_FS);

	return inode;
}

struct inode *lookup_free_space_inode(struct btrfs_root *root,
				      struct btrfs_block_group_cache
				      *block_group, struct btrfs_path *path)
{
	struct inode *inode = NULL;
	u32 flags = BTRFS_INODE_NODATASUM | BTRFS_INODE_NODATACOW;

	spin_lock(&block_group->lock);
	if (block_group->inode)
		inode = igrab(block_group->inode);
	spin_unlock(&block_group->lock);
	if (inode)
		return inode;

	inode = __lookup_free_space_inode(root, path,
					  block_group->key.objectid);
	if (IS_ERR(inode))
		return inode;

	spin_lock(&block_group->lock);
	if (!((BTRFS_I(inode)->flags & flags) == flags)) {
		btrfs_info(root->fs_info,
			"Old style space inode found, converting.");
		BTRFS_I(inode)->flags |= BTRFS_INODE_NODATASUM |
			BTRFS_INODE_NODATACOW;
		block_group->disk_cache_state = BTRFS_DC_CLEAR;
	}

	if (!block_group->iref) {
		block_group->inode = igrab(inode);
		block_group->iref = 1;
	}
	spin_unlock(&block_group->lock);

	return inode;
}

static int __create_free_space_inode(struct btrfs_root *root,
				     struct btrfs_trans_handle *trans,
				     struct btrfs_path *path,
				     u64 ino, u64 offset)
{
	struct btrfs_key key;
	struct btrfs_disk_key disk_key;
	struct btrfs_free_space_header *header;
	struct btrfs_inode_item *inode_item;
	struct extent_buffer *leaf;
	u64 flags = BTRFS_INODE_NOCOMPRESS | BTRFS_INODE_PREALLOC;
	int ret;

	ret = btrfs_insert_empty_inode(trans, root, path, ino);
	if (ret)
		return ret;

	/* We inline crc's for the free disk space cache */
	if (ino != BTRFS_FREE_INO_OBJECTID)
		flags |= BTRFS_INODE_NODATASUM | BTRFS_INODE_NODATACOW;

	leaf = path->nodes[0];
	inode_item = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_inode_item);
	btrfs_item_key(leaf, &disk_key, path->slots[0]);
	memset_extent_buffer(leaf, 0, (unsigned long)inode_item,
			     sizeof(*inode_item));
	btrfs_set_inode_generation(leaf, inode_item, trans->transid);
	btrfs_set_inode_size(leaf, inode_item, 0);
	btrfs_set_inode_nbytes(leaf, inode_item, 0);
	btrfs_set_inode_uid(leaf, inode_item, 0);
	btrfs_set_inode_gid(leaf, inode_item, 0);
	btrfs_set_inode_mode(leaf, inode_item, S_IFREG | 0600);
	btrfs_set_inode_flags(leaf, inode_item, flags);
	btrfs_set_inode_nlink(leaf, inode_item, 1);
	btrfs_set_inode_transid(leaf, inode_item, trans->transid);
	btrfs_set_inode_block_group(leaf, inode_item, offset);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	key.objectid = BTRFS_FREE_SPACE_OBJECTID;
	key.offset = offset;
	key.type = 0;

	ret = btrfs_insert_empty_item(trans, root, path, &key,
				      sizeof(struct btrfs_free_space_header));
	if (ret < 0) {
		btrfs_release_path(path);
		return ret;
	}
	leaf = path->nodes[0];
	header = btrfs_item_ptr(leaf, path->slots[0],
				struct btrfs_free_space_header);
	memset_extent_buffer(leaf, 0, (unsigned long)header, sizeof(*header));
	btrfs_set_free_space_key(leaf, header, &disk_key);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	return 0;
}

int create_free_space_inode(struct btrfs_root *root,
			    struct btrfs_trans_handle *trans,
			    struct btrfs_block_group_cache *block_group,
			    struct btrfs_path *path)
{
	int ret;
	u64 ino;

	ret = btrfs_find_free_objectid(root, &ino);
	if (ret < 0)
		return ret;

	return __create_free_space_inode(root, trans, path, ino,
					 block_group->key.objectid);
}

int btrfs_check_trunc_cache_free_space(struct btrfs_root *root,
				       struct btrfs_block_rsv *rsv)
{
	u64 needed_bytes;
	int ret;

	/* 1 for slack space, 1 for updating the inode */
	needed_bytes = btrfs_calc_trunc_metadata_size(root, 1) +
		btrfs_calc_trans_metadata_size(root, 1);

	spin_lock(&rsv->lock);
	if (rsv->reserved < needed_bytes)
		ret = -ENOSPC;
	else
		ret = 0;
	spin_unlock(&rsv->lock);
	return ret;
}

int btrfs_truncate_free_space_cache(struct btrfs_root *root,
				    struct btrfs_trans_handle *trans,
				    struct inode *inode)
{
	int ret = 0;

	btrfs_i_size_write(inode, 0);
	truncate_pagecache(inode, 0);

	/*
	 * We don't need an orphan item because truncating the free space cache
	 * will never be split across transactions.
	 */
	ret = btrfs_truncate_inode_items(trans, root, inode,
					 0, BTRFS_EXTENT_DATA_KEY);
	if (ret) {
		btrfs_abort_transaction(trans, root, ret);
		return ret;
	}

	ret = btrfs_update_inode(trans, root, inode);
	if (ret)
		btrfs_abort_transaction(trans, root, ret);

	return ret;
}

static int readahead_cache(struct inode *inode)
{
	struct file_ra_state *ra;
	unsigned long last_index;

	ra = kzalloc(sizeof(*ra), GFP_NOFS);
	if (!ra)
		return -ENOMEM;

	file_ra_state_init(ra, inode->i_mapping);
	last_index = (i_size_read(inode) - 1) >> PAGE_CACHE_SHIFT;

	page_cache_sync_readahead(inode->i_mapping, ra, NULL, 0, last_index);

	kfree(ra);

	return 0;
}

struct io_ctl {
	void *cur, *orig;
	struct page *page;
	struct page **pages;
	struct btrfs_root *root;
	unsigned long size;
	int index;
	int num_pages;
	unsigned check_crcs:1;
};

static int io_ctl_init(struct io_ctl *io_ctl, struct inode *inode,
		       struct btrfs_root *root, int write)
{
	int num_pages;
	int check_crcs = 0;

	num_pages = DIV_ROUND_UP(i_size_read(inode), PAGE_CACHE_SIZE);

	if (btrfs_ino(inode) != BTRFS_FREE_INO_OBJECTID)
		check_crcs = 1;

	/* Make sure we can fit our crcs into the first page */
	if (write && check_crcs &&
	    (num_pages * sizeof(u32)) >= PAGE_CACHE_SIZE)
		return -ENOSPC;

	memset(io_ctl, 0, sizeof(struct io_ctl));

	io_ctl->pages = kzalloc(sizeof(struct page *) * num_pages, GFP_NOFS);
	if (!io_ctl->pages)
		return -ENOMEM;

	io_ctl->num_pages = num_pages;
	io_ctl->root = root;
	io_ctl->check_crcs = check_crcs;

	return 0;
}

static void io_ctl_free(struct io_ctl *io_ctl)
{
	kfree(io_ctl->pages);
}

static void io_ctl_unmap_page(struct io_ctl *io_ctl)
{
	if (io_ctl->cur) {
		kunmap(io_ctl->page);
		io_ctl->cur = NULL;
		io_ctl->orig = NULL;
	}
}

static void io_ctl_map_page(struct io_ctl *io_ctl, int clear)
{
	ASSERT(io_ctl->index < io_ctl->num_pages);
	io_ctl->page = io_ctl->pages[io_ctl->index++];
	io_ctl->cur = kmap(io_ctl->page);
	io_ctl->orig = io_ctl->cur;
	io_ctl->size = PAGE_CACHE_SIZE;
	if (clear)
		memset(io_ctl->cur, 0, PAGE_CACHE_SIZE);
}

static void io_ctl_drop_pages(struct io_ctl *io_ctl)
{
	int i;

	io_ctl_unmap_page(io_ctl);

	for (i = 0; i < io_ctl->num_pages; i++) {
		if (io_ctl->pages[i]) {
			ClearPageChecked(io_ctl->pages[i]);
			unlock_page(io_ctl->pages[i]);
			page_cache_release(io_ctl->pages[i]);
		}
	}
}

static int io_ctl_prepare_pages(struct io_ctl *io_ctl, struct inode *inode,
				int uptodate)
{
	struct page *page;
	gfp_t mask = btrfs_alloc_write_mask(inode->i_mapping);
	int i;

	for (i = 0; i < io_ctl->num_pages; i++) {
		page = find_or_create_page(inode->i_mapping, i, mask);
		if (!page) {
			io_ctl_drop_pages(io_ctl);
			return -ENOMEM;
		}
		io_ctl->pages[i] = page;
		if (uptodate && !PageUptodate(page)) {
			btrfs_readpage(NULL, page);
			lock_page(page);
			if (!PageUptodate(page)) {
				btrfs_err(BTRFS_I(inode)->root->fs_info,
					   "error reading free space cache");
				io_ctl_drop_pages(io_ctl);
				return -EIO;
			}
		}
	}

	for (i = 0; i < io_ctl->num_pages; i++) {
		clear_page_dirty_for_io(io_ctl->pages[i]);
		set_page_extent_mapped(io_ctl->pages[i]);
	}

	return 0;
}

static void io_ctl_set_generation(struct io_ctl *io_ctl, u64 generation)
{
	__le64 *val;

	io_ctl_map_page(io_ctl, 1);

	/*
	 * Skip the csum areas.  If we don't check crcs then we just have a
	 * 64bit chunk at the front of the first page.
	 */
	if (io_ctl->check_crcs) {
		io_ctl->cur += (sizeof(u32) * io_ctl->num_pages);
		io_ctl->size -= sizeof(u64) + (sizeof(u32) * io_ctl->num_pages);
	} else {
		io_ctl->cur += sizeof(u64);
		io_ctl->size -= sizeof(u64) * 2;
	}

	val = io_ctl->cur;
	*val = cpu_to_le64(generation);
	io_ctl->cur += sizeof(u64);
}

static int io_ctl_check_generation(struct io_ctl *io_ctl, u64 generation)
{
	__le64 *gen;

	/*
	 * Skip the crc area.  If we don't check crcs then we just have a 64bit
	 * chunk at the front of the first page.
	 */
	if (io_ctl->check_crcs) {
		io_ctl->cur += sizeof(u32) * io_ctl->num_pages;
		io_ctl->size -= sizeof(u64) +
			(sizeof(u32) * io_ctl->num_pages);
	} else {
		io_ctl->cur += sizeof(u64);
		io_ctl->size -= sizeof(u64) * 2;
	}

	gen = io_ctl->cur;
	if (le64_to_cpu(*gen) != generation) {
		printk_ratelimited(KERN_ERR "BTRFS: space cache generation "
				   "(%Lu) does not match inode (%Lu)\n", *gen,
				   generation);
		io_ctl_unmap_page(io_ctl);
		return -EIO;
	}
	io_ctl->cur += sizeof(u64);
	return 0;
}

static void io_ctl_set_crc(struct io_ctl *io_ctl, int index)
{
	u32 *tmp;
	u32 crc = ~(u32)0;
	unsigned offset = 0;

	if (!io_ctl->check_crcs) {
		io_ctl_unmap_page(io_ctl);
		return;
	}

	if (index == 0)
		offset = sizeof(u32) * io_ctl->num_pages;

	crc = btrfs_csum_data(io_ctl->orig + offset, crc,
			      PAGE_CACHE_SIZE - offset);
	btrfs_csum_final(crc, (char *)&crc);
	io_ctl_unmap_page(io_ctl);
	tmp = kmap(io_ctl->pages[0]);
	tmp += index;
	*tmp = crc;
	kunmap(io_ctl->pages[0]);
}

static int io_ctl_check_crc(struct io_ctl *io_ctl, int index)
{
	u32 *tmp, val;
	u32 crc = ~(u32)0;
	unsigned offset = 0;

	if (!io_ctl->check_crcs) {
		io_ctl_map_page(io_ctl, 0);
		return 0;
	}

	if (index == 0)
		offset = sizeof(u32) * io_ctl->num_pages;

	tmp = kmap(io_ctl->pages[0]);
	tmp += index;
	val = *tmp;
	kunmap(io_ctl->pages[0]);

	io_ctl_map_page(io_ctl, 0);
	crc = btrfs_csum_data(io_ctl->orig + offset, crc,
			      PAGE_CACHE_SIZE - offset);
	btrfs_csum_final(crc, (char *)&crc);
	if (val != crc) {
		printk_ratelimited(KERN_ERR "BTRFS: csum mismatch on free "
				   "space cache\n");
		io_ctl_unmap_page(io_ctl);
		return -EIO;
	}

	return 0;
}

static int io_ctl_add_entry(struct io_ctl *io_ctl, u64 offset, u64 bytes,
			    void *bitmap)
{
	struct btrfs_free_space_entry *entry;

	if (!io_ctl->cur)
		return -ENOSPC;

	entry = io_ctl->cur;
	entry->offset = cpu_to_le64(offset);
	entry->bytes = cpu_to_le64(bytes);
	entry->type = (bitmap) ? BTRFS_FREE_SPACE_BITMAP :
		BTRFS_FREE_SPACE_EXTENT;
	io_ctl->cur += sizeof(struct btrfs_free_space_entry);
	io_ctl->size -= sizeof(struct btrfs_free_space_entry);

	if (io_ctl->size >= sizeof(struct btrfs_free_space_entry))
		return 0;

	io_ctl_set_crc(io_ctl, io_ctl->index - 1);

	/* No more pages to map */
	if (io_ctl->index >= io_ctl->num_pages)
		return 0;

	/* map the next page */
	io_ctl_map_page(io_ctl, 1);
	return 0;
}

static int io_ctl_add_bitmap(struct io_ctl *io_ctl, void *bitmap)
{
	if (!io_ctl->cur)
		return -ENOSPC;

	/*
	 * If we aren't at the start of the current page, unmap this one and
	 * map the next one if there is any left.
	 */
	if (io_ctl->cur != io_ctl->orig) {
		io_ctl_set_crc(io_ctl, io_ctl->index - 1);
		if (io_ctl->index >= io_ctl->num_pages)
			return -ENOSPC;
		io_ctl_map_page(io_ctl, 0);
	}

	memcpy(io_ctl->cur, bitmap, PAGE_CACHE_SIZE);
	io_ctl_set_crc(io_ctl, io_ctl->index - 1);
	if (io_ctl->index < io_ctl->num_pages)
		io_ctl_map_page(io_ctl, 0);
	return 0;
}

static void io_ctl_zero_remaining_pages(struct io_ctl *io_ctl)
{
	/*
	 * If we're not on the boundary we know we've modified the page and we
	 * need to crc the page.
	 */
	if (io_ctl->cur != io_ctl->orig)
		io_ctl_set_crc(io_ctl, io_ctl->index - 1);
	else
		io_ctl_unmap_page(io_ctl);

	while (io_ctl->index < io_ctl->num_pages) {
		io_ctl_map_page(io_ctl, 1);
		io_ctl_set_crc(io_ctl, io_ctl->index - 1);
	}
}

static int io_ctl_read_entry(struct io_ctl *io_ctl,
			    struct btrfs_free_space *entry, u8 *type)
{
	struct btrfs_free_space_entry *e;
	int ret;

	if (!io_ctl->cur) {
		ret = io_ctl_check_crc(io_ctl, io_ctl->index);
		if (ret)
			return ret;
	}

	e = io_ctl->cur;
	entry->offset = le64_to_cpu(e->offset);
	entry->bytes = le64_to_cpu(e->bytes);
	*type = e->type;
	io_ctl->cur += sizeof(struct btrfs_free_space_entry);
	io_ctl->size -= sizeof(struct btrfs_free_space_entry);

	if (io_ctl->size >= sizeof(struct btrfs_free_space_entry))
		return 0;

	io_ctl_unmap_page(io_ctl);

	return 0;
}

static int io_ctl_read_bitmap(struct io_ctl *io_ctl,
			      struct btrfs_free_space *entry)
{
	int ret;

	ret = io_ctl_check_crc(io_ctl, io_ctl->index);
	if (ret)
		return ret;

	memcpy(entry->bitmap, io_ctl->cur, PAGE_CACHE_SIZE);
	io_ctl_unmap_page(io_ctl);

	return 0;
}

/*
 * Since we attach pinned extents after the fact we can have contiguous sections
 * of free space that are split up in entries.  This poses a problem with the
 * tree logging stuff since it could have allocated across what appears to be 2
 * entries since we would have merged the entries when adding the pinned extents
 * back to the free space cache.  So run through the space cache that we just
 * loaded and merge contiguous entries.  This will make the log replay stuff not
 * blow up and it will make for nicer allocator behavior.
 */
static void merge_space_tree(struct btrfs_free_space_ctl *ctl)
{
	struct btrfs_free_space *e, *prev = NULL;
	struct rb_node *n;

again:
	spin_lock(&ctl->tree_lock);
	for (n = rb_first(&ctl->free_space_offset); n; n = rb_next(n)) {
		e = rb_entry(n, struct btrfs_free_space, offset_index);
		if (!prev)
			goto next;
		if (e->bitmap || prev->bitmap)
			goto next;
		if (prev->offset + prev->bytes == e->offset) {
			unlink_free_space(ctl, prev);
			unlink_free_space(ctl, e);
			prev->bytes += e->bytes;
			kmem_cache_free(btrfs_free_space_cachep, e);
			link_free_space(ctl, prev);
			prev = NULL;
			spin_unlock(&ctl->tree_lock);
			goto again;
		}
next:
		prev = e;
	}
	spin_unlock(&ctl->tree_lock);
}

static int __load_free_space_cache(struct btrfs_root *root, struct inode *inode,
				   struct btrfs_free_space_ctl *ctl,
				   struct btrfs_path *path, u64 offset)
{
	struct btrfs_free_space_header *header;
	struct extent_buffer *leaf;
	struct io_ctl io_ctl;
	struct btrfs_key key;
	struct btrfs_free_space *e, *n;
	struct list_head bitmaps;
	u64 num_entries;
	u64 num_bitmaps;
	u64 generation;
	u8 type;
	int ret = 0;

	INIT_LIST_HEAD(&bitmaps);

	/* Nothing in the space cache, goodbye */
	if (!i_size_read(inode))
		return 0;

	key.objectid = BTRFS_FREE_SPACE_OBJECTID;
	key.offset = offset;
	key.type = 0;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		return 0;
	else if (ret > 0) {
		btrfs_release_path(path);
		return 0;
	}

	ret = -1;

	leaf = path->nodes[0];
	header = btrfs_item_ptr(leaf, path->slots[0],
				struct btrfs_free_space_header);
	num_entries = btrfs_free_space_entries(leaf, header);
	num_bitmaps = btrfs_free_space_bitmaps(leaf, header);
	generation = btrfs_free_space_generation(leaf, header);
	btrfs_release_path(path);

	if (!BTRFS_I(inode)->generation) {
		btrfs_info(root->fs_info,
			   "The free space cache file (%llu) is invalid. skip it\n",
			   offset);
		return 0;
	}

	if (BTRFS_I(inode)->generation != generation) {
		btrfs_err(root->fs_info,
			"free space inode generation (%llu) "
			"did not match free space cache generation (%llu)",
			BTRFS_I(inode)->generation, generation);
		return 0;
	}

	if (!num_entries)
		return 0;

	ret = io_ctl_init(&io_ctl, inode, root, 0);
	if (ret)
		return ret;

	ret = readahead_cache(inode);
	if (ret)
		goto out;

	ret = io_ctl_prepare_pages(&io_ctl, inode, 1);
	if (ret)
		goto out;

	ret = io_ctl_check_crc(&io_ctl, 0);
	if (ret)
		goto free_cache;

	ret = io_ctl_check_generation(&io_ctl, generation);
	if (ret)
		goto free_cache;

	while (num_entries) {
		e = kmem_cache_zalloc(btrfs_free_space_cachep,
				      GFP_NOFS);
		if (!e)
			goto free_cache;

		ret = io_ctl_read_entry(&io_ctl, e, &type);
		if (ret) {
			kmem_cache_free(btrfs_free_space_cachep, e);
			goto free_cache;
		}

		if (!e->bytes) {
			kmem_cache_free(btrfs_free_space_cachep, e);
			goto free_cache;
		}

		if (type == BTRFS_FREE_SPACE_EXTENT) {
			spin_lock(&ctl->tree_lock);
			ret = link_free_space(ctl, e);
			spin_unlock(&ctl->tree_lock);
			if (ret) {
				btrfs_err(root->fs_info,
					"Duplicate entries in free space cache, dumping");
				kmem_cache_free(btrfs_free_space_cachep, e);
				goto free_cache;
			}
		} else {
			ASSERT(num_bitmaps);
			num_bitmaps--;
			e->bitmap = kzalloc(PAGE_CACHE_SIZE, GFP_NOFS);
			if (!e->bitmap) {
				kmem_cache_free(
					btrfs_free_space_cachep, e);
				goto free_cache;
			}
			spin_lock(&ctl->tree_lock);
			ret = link_free_space(ctl, e);
			ctl->total_bitmaps++;
			ctl->op->recalc_thresholds(ctl);
			spin_unlock(&ctl->tree_lock);
			if (ret) {
				btrfs_err(root->fs_info,
					"Duplicate entries in free space cache, dumping");
				kmem_cache_free(btrfs_free_space_cachep, e);
				goto free_cache;
			}
			list_add_tail(&e->list, &bitmaps);
		}

		num_entries--;
	}

	io_ctl_unmap_page(&io_ctl);

	/*
	 * We add the bitmaps at the end of the entries in order that
	 * the bitmap entries are added to the cache.
	 */
	list_for_each_entry_safe(e, n, &bitmaps, list) {
		list_del_init(&e->list);
		ret = io_ctl_read_bitmap(&io_ctl, e);
		if (ret)
			goto free_cache;
	}

	io_ctl_drop_pages(&io_ctl);
	merge_space_tree(ctl);
	ret = 1;
out:
	io_ctl_free(&io_ctl);
	return ret;
free_cache:
	io_ctl_drop_pages(&io_ctl);
	__btrfs_remove_free_space_cache(ctl);
	goto out;
}

int load_free_space_cache(struct btrfs_fs_info *fs_info,
			  struct btrfs_block_group_cache *block_group)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_root *root = fs_info->tree_root;
	struct inode *inode;
	struct btrfs_path *path;
	int ret = 0;
	bool matched;
	u64 used = btrfs_block_group_used(&block_group->item);

	/*
	 * If this block group has been marked to be cleared for one reason or
	 * another then we can't trust the on disk cache, so just return.
	 */
	spin_lock(&block_group->lock);
	if (block_group->disk_cache_state != BTRFS_DC_WRITTEN) {
		spin_unlock(&block_group->lock);
		return 0;
	}
	spin_unlock(&block_group->lock);

	path = btrfs_alloc_path();
	if (!path)
		return 0;
	path->search_commit_root = 1;
	path->skip_locking = 1;

	inode = lookup_free_space_inode(root, block_group, path);
	if (IS_ERR(inode)) {
		btrfs_free_path(path);
		return 0;
	}

	/* We may have converted the inode and made the cache invalid. */
	spin_lock(&block_group->lock);
	if (block_group->disk_cache_state != BTRFS_DC_WRITTEN) {
		spin_unlock(&block_group->lock);
		btrfs_free_path(path);
		goto out;
	}
	spin_unlock(&block_group->lock);

	ret = __load_free_space_cache(fs_info->tree_root, inode, ctl,
				      path, block_group->key.objectid);
	btrfs_free_path(path);
	if (ret <= 0)
		goto out;

	spin_lock(&ctl->tree_lock);
	matched = (ctl->free_space == (block_group->key.offset - used -
				       block_group->bytes_super));
	spin_unlock(&ctl->tree_lock);

	if (!matched) {
		__btrfs_remove_free_space_cache(ctl);
		btrfs_warn(fs_info, "block group %llu has wrong amount of free space",
			block_group->key.objectid);
		ret = -1;
	}
out:
	if (ret < 0) {
		/* This cache is bogus, make sure it gets cleared */
		spin_lock(&block_group->lock);
		block_group->disk_cache_state = BTRFS_DC_CLEAR;
		spin_unlock(&block_group->lock);
		ret = 0;

		btrfs_warn(fs_info, "failed to load free space cache for block group %llu, rebuild it now",
			block_group->key.objectid);
	}

	iput(inode);
	return ret;
}

static noinline_for_stack
int write_cache_extent_entries(struct io_ctl *io_ctl,
			      struct btrfs_free_space_ctl *ctl,
			      struct btrfs_block_group_cache *block_group,
			      int *entries, int *bitmaps,
			      struct list_head *bitmap_list)
{
	int ret;
	struct btrfs_free_cluster *cluster = NULL;
	struct rb_node *node = rb_first(&ctl->free_space_offset);

	/* Get the cluster for this block_group if it exists */
	if (block_group && !list_empty(&block_group->cluster_list)) {
		cluster = list_entry(block_group->cluster_list.next,
				     struct btrfs_free_cluster,
				     block_group_list);
	}

	if (!node && cluster) {
		node = rb_first(&cluster->root);
		cluster = NULL;
	}

	/* Write out the extent entries */
	while (node) {
		struct btrfs_free_space *e;

		e = rb_entry(node, struct btrfs_free_space, offset_index);
		*entries += 1;

		ret = io_ctl_add_entry(io_ctl, e->offset, e->bytes,
				       e->bitmap);
		if (ret)
			goto fail;

		if (e->bitmap) {
			list_add_tail(&e->list, bitmap_list);
			*bitmaps += 1;
		}
		node = rb_next(node);
		if (!node && cluster) {
			node = rb_first(&cluster->root);
			cluster = NULL;
		}
	}
	return 0;
fail:
	return -ENOSPC;
}

static noinline_for_stack int
update_cache_item(struct btrfs_trans_handle *trans,
		  struct btrfs_root *root,
		  struct inode *inode,
		  struct btrfs_path *path, u64 offset,
		  int entries, int bitmaps)
{
	struct btrfs_key key;
	struct btrfs_free_space_header *header;
	struct extent_buffer *leaf;
	int ret;

	key.objectid = BTRFS_FREE_SPACE_OBJECTID;
	key.offset = offset;
	key.type = 0;

	ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
	if (ret < 0) {
		clear_extent_bit(&BTRFS_I(inode)->io_tree, 0, inode->i_size - 1,
				 EXTENT_DIRTY | EXTENT_DELALLOC, 0, 0, NULL,
				 GFP_NOFS);
		goto fail;
	}
	leaf = path->nodes[0];
	if (ret > 0) {
		struct btrfs_key found_key;
		ASSERT(path->slots[0]);
		path->slots[0]--;
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid != BTRFS_FREE_SPACE_OBJECTID ||
		    found_key.offset != offset) {
			clear_extent_bit(&BTRFS_I(inode)->io_tree, 0,
					 inode->i_size - 1,
					 EXTENT_DIRTY | EXTENT_DELALLOC, 0, 0,
					 NULL, GFP_NOFS);
			btrfs_release_path(path);
			goto fail;
		}
	}

	BTRFS_I(inode)->generation = trans->transid;
	header = btrfs_item_ptr(leaf, path->slots[0],
				struct btrfs_free_space_header);
	btrfs_set_free_space_entries(leaf, header, entries);
	btrfs_set_free_space_bitmaps(leaf, header, bitmaps);
	btrfs_set_free_space_generation(leaf, header, trans->transid);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	return 0;

fail:
	return -1;
}

static noinline_for_stack int
write_pinned_extent_entries(struct btrfs_root *root,
			    struct btrfs_block_group_cache *block_group,
			    struct io_ctl *io_ctl,
			    int *entries)
{
	u64 start, extent_start, extent_end, len;
	struct extent_io_tree *unpin = NULL;
	int ret;

	if (!block_group)
		return 0;

	/*
	 * We want to add any pinned extents to our free space cache
	 * so we don't leak the space
	 *
	 * We shouldn't have switched the pinned extents yet so this is the
	 * right one
	 */
	unpin = root->fs_info->pinned_extents;

	start = block_group->key.objectid;

	while (start < block_group->key.objectid + block_group->key.offset) {
		ret = find_first_extent_bit(unpin, start,
					    &extent_start, &extent_end,
					    EXTENT_DIRTY, NULL);
		if (ret)
			return 0;

		/* This pinned extent is out of our range */
		if (extent_start >= block_group->key.objectid +
		    block_group->key.offset)
			return 0;

		extent_start = max(extent_start, start);
		extent_end = min(block_group->key.objectid +
				 block_group->key.offset, extent_end + 1);
		len = extent_end - extent_start;

		*entries += 1;
		ret = io_ctl_add_entry(io_ctl, extent_start, len, NULL);
		if (ret)
			return -ENOSPC;

		start = extent_end;
	}

	return 0;
}

static noinline_for_stack int
write_bitmap_entries(struct io_ctl *io_ctl, struct list_head *bitmap_list)
{
	struct list_head *pos, *n;
	int ret;

	/* Write out the bitmaps */
	list_for_each_safe(pos, n, bitmap_list) {
		struct btrfs_free_space *entry =
			list_entry(pos, struct btrfs_free_space, list);

		ret = io_ctl_add_bitmap(io_ctl, entry->bitmap);
		if (ret)
			return -ENOSPC;
		list_del_init(&entry->list);
	}

	return 0;
}

static int flush_dirty_cache(struct inode *inode)
{
	int ret;

	ret = btrfs_wait_ordered_range(inode, 0, (u64)-1);
	if (ret)
		clear_extent_bit(&BTRFS_I(inode)->io_tree, 0, inode->i_size - 1,
				 EXTENT_DIRTY | EXTENT_DELALLOC, 0, 0, NULL,
				 GFP_NOFS);

	return ret;
}

static void noinline_for_stack
cleanup_write_cache_enospc(struct inode *inode,
			   struct io_ctl *io_ctl,
			   struct extent_state **cached_state,
			   struct list_head *bitmap_list)
{
	struct list_head *pos, *n;

	list_for_each_safe(pos, n, bitmap_list) {
		struct btrfs_free_space *entry =
			list_entry(pos, struct btrfs_free_space, list);
		list_del_init(&entry->list);
	}
	io_ctl_drop_pages(io_ctl);
	unlock_extent_cached(&BTRFS_I(inode)->io_tree, 0,
			     i_size_read(inode) - 1, cached_state,
			     GFP_NOFS);
}

/**
 * __btrfs_write_out_cache - write out cached info to an inode
 * @root - the root the inode belongs to
 * @ctl - the free space cache we are going to write out
 * @block_group - the block_group for this cache if it belongs to a block_group
 * @trans - the trans handle
 * @path - the path to use
 * @offset - the offset for the key we'll insert
 *
 * This function writes out a free space cache struct to disk for quick recovery
 * on mount.  This will return 0 if it was successfull in writing the cache out,
 * and -1 if it was not.
 */
static int __btrfs_write_out_cache(struct btrfs_root *root, struct inode *inode,
				   struct btrfs_free_space_ctl *ctl,
				   struct btrfs_block_group_cache *block_group,
				   struct btrfs_trans_handle *trans,
				   struct btrfs_path *path, u64 offset)
{
	struct extent_state *cached_state = NULL;
	struct io_ctl io_ctl;
	LIST_HEAD(bitmap_list);
	int entries = 0;
	int bitmaps = 0;
	int ret;

	if (!i_size_read(inode))
		return -1;

	ret = io_ctl_init(&io_ctl, inode, root, 1);
	if (ret)
		return -1;

	if (block_group && (block_group->flags & BTRFS_BLOCK_GROUP_DATA)) {
		down_write(&block_group->data_rwsem);
		spin_lock(&block_group->lock);
		if (block_group->delalloc_bytes) {
			block_group->disk_cache_state = BTRFS_DC_WRITTEN;
			spin_unlock(&block_group->lock);
			up_write(&block_group->data_rwsem);
			BTRFS_I(inode)->generation = 0;
			ret = 0;
			goto out;
		}
		spin_unlock(&block_group->lock);
	}

	/* Lock all pages first so we can lock the extent safely. */
	io_ctl_prepare_pages(&io_ctl, inode, 0);

	lock_extent_bits(&BTRFS_I(inode)->io_tree, 0, i_size_read(inode) - 1,
			 0, &cached_state);

	io_ctl_set_generation(&io_ctl, trans->transid);

	/* Write out the extent entries in the free space cache */
	ret = write_cache_extent_entries(&io_ctl, ctl,
					 block_group, &entries, &bitmaps,
					 &bitmap_list);
	if (ret)
		goto out_nospc;

	/*
	 * Some spaces that are freed in the current transaction are pinned,
	 * they will be added into free space cache after the transaction is
	 * committed, we shouldn't lose them.
	 */
	ret = write_pinned_extent_entries(root, block_group, &io_ctl, &entries);
	if (ret)
		goto out_nospc;

	/* At last, we write out all the bitmaps. */
	ret = write_bitmap_entries(&io_ctl, &bitmap_list);
	if (ret)
		goto out_nospc;

	/* Zero out the rest of the pages just to mabtrfs_rodn=ts(&io_)
	 &io_ctl, &entries);
	if (ret)
		goto ou * onin the s_bitmntr * an 've mo cacheages just t free spile.et = btrfs_truncacache(;
	if inode, ctl,
	ret)
	. GFP_NOret)
	.ges, GFP_N &bitize_read(inode) - 1,
hed_state);

	io_cet)
		goto out_nospc;

	/* Zerock_group && (block_group->flags & BTRFS_BLOCK_GROUP_DATA)) {
		d
rite(&block_group->data_rwsem);
			BTRF Some sR_path(ages just t if (&blocxtent entririte o adddirtyhey willmhe rel
	ir	if (io_drop_pages(&io_ctl);
	__btrck_extent_cached(&BTRFS_I(inode)->io_tree, 0,
			     i_size_read(inode) - 1, cachedd_state);

	i,OFS);

	return ouFirtyxtentcacheajust t free sfile (%llu.et = btrfs_dirty_cache(struct;
	if (ret)
		goto out;

	ret =  ouUcacheache invalidcaus caceadde spyasonache if it %llu)he  */
	spin_lbtrfs__cache_item(struct root, inode,
					 0, 1);,
		  int 		s, &bitmas);
	btrfsio_ctl_free(&io_ctl);
	returnt) {
				btrfd. */
	ode_items(just 2->i_mapping);
	int i;_I(inode)->generation = 0;
			ret }s_releas_inode(trans, root, inode);
	if (re ret;

	ret =spc;

	/*ctlp_write_cache_enospc(struct
					 l, &bitmap_state);

	i,Op_list);
	if (rerock_group && (block_group->flags & BTRFS_BLOCK_GROUP_DATA)) {
		d
rite(&block_group->data_rwsem);
			BTR out;
}

int load_fwrite_out_cache(struct btrfs_root *root, struc
truct btrfs_block_handle *trans,
			    stru btrfs_block_group_cache *block_group,
			    stru btrfs_path *path)
{
	struct inode free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_*inode;
	structt = 0;

	INIT_L root;
	io_nfo->pinnedoot;
	struc_lock(&block_group->lock);
	if (block_group->disk_cache_state != BTR<_DC_WRITTESETUPspin_unlock(&block_group->lock);
		return 0;
	}
	spin_block_group->disk_cac_bytes) {
			blockgroup->disk_cache_state = BTRFS_DC_WRITTEN;
			spin_nlock(&block_group->lock);
		return 0;
	}
	spin_unlock(&block_group->lock);

	path == lookup_free_space_inode(root, block_group, path);
	if (IS_ERR(inode)) {
		b
rn 0;

	ret = io_ctls_write_out_cache(structinode, ctl,
				 _group, path);
				     struct block_group->key.objectid);
	btrfs_t) {
				btrfock(&block_group->lock);
		block_group->disk_cache_state = BTRFS_DC_CLEAR;odeOpin_unlock(&block_group->lock);
		ret = 0;

		btr#ifdef DEBUG
s_err(root->fs_info,
			"free spto load fout alpace cache for block group %llu, rebublock_group->key.objectid);
	}

	i#endifiput(inode);
	return ret;
}

static noinli crc's ed long size;
_index)tonpin, es,
list); len, Nc = uo_c	    EXTEfset)
{
	struct(path-> for thck_glist); len,turn - the ok_glist); len,urn ret;
}ned long)header(div_uset);
	en, uo_c))tatic noinli crc's ed long size;
super)tonpins(tes,
			  Nc = uo_ctruct ret;
}ned long)header(div_uset
			  Nco_c))tatic noinli crc's eset)
{
	s)tonpinruct io_ctlfree_space_ctl *ctl,
				   structfset)
{
	struct es,
list); len,;ct es,
uper)pernpinrucrfs_iuper)pernpinrucFS_DR_BITMAP		(PAGmittee_sco_cfs_rlist); len,set;
	keycacize >=len,;ctrlist); len,setdiv64_uset
list); len, Niuper)pernpinruc);ctrlist); len,s*=,
uper)pernpinrucrftrlist); len,s+=cize >=len,;cct ret;
}
list); len,;ctic int flush_oot;
_empty_;
	keyt io_ctlrb*root, structfset,
		  int  struct list_he *node = rb_bitmaps)
{
	truct inode e *node =*cFS_&fs_infe *nodeuct rb_node *node =agesntL;
	struct io_ctlfree_space *entry 			"ile (start *ppage = fesntL;
*p (blo an ntry(node,  fesntct btrfs_free_space, offset_index);
		*ent(block_;
	keyc< pinned) {
			clear_cFS_& *ppnfe *	 */		spi f (ret >  for thc pinned) {
			clear_cFS_& *ppnfe *one
			spi f (relear_ Som	ll nee have allocatp entries aryt if ant entries inyom	ll nshnnedace
	amet for twe dons the
	 * r foserite oantom	ll ntent entries inyblocklway mar_key.o so we ion't ch 64b	ll nrc'sar _slot(gh the space 0,
			we would h add anllocom	ll ntentrecov thetor beh0;
	tim			 if aor beh0e spaom	 iom	ll n is out offase tranantaor beh0e spaom	 p entrirun tom	ll nie not on_empty0e stp entrie
	 * nfirst ant  aryt tom	ll nte of ;
	en, ld h add ango one
	et_r the tranises inyom	ll nlogicaorywe don't a on_empty0e stnt entrie
	 * n'ocom	ll nkey.o ap, PAGE_Cld h add ango 	 */et_r beockeom	ll nlogicaorywom	ll  (exlock_g {
				kmem_cdex ==nnedg {
				kmem_c	WARN_ON_ONCE(1	goto fn -ENOSPC;EXISTgoto fist_a_cFS_& *ppnfe *one
			sppi f (relear_inode)
	nnedg {
				kmem_c	WARN_ON_ONCE(1	goto fn -ENOSPC;EXISTgoto fist_a_cFS_& *ppnfe *	 */		sp
	}

	for (ie *	ee_soot, 					 0,esntctc);cte *_empty_color(root, 1);
eturn 0;

fail: * Since _slot(espace 0,
	he key wegivtr *for tw This ffuzzycacdons the
	 	en, e can't nnedary write s_rodantaor beh0nera
	 * nf* loadedh addaons
 * oare frong a the the+= e-> 1,
	
	 *comeg a t_r the tranwegivtr free or tw Thic int __ io_ctlfree_space *entry 
oot;
_slot(t;
	keyt io_ctlfree_space_ctl *ctl,
				   suctfset)
{
	sbitmaps)
{
	_onlybitmapfuzzytruct inode e *node =
			ree_space_offset);

	/.e *nodeuct rb_nodfree_space *entry, u8 *ty= NULL;
	struc=  ouirst s inyble frhe ihem.mabtrfanwe');

	/'hile (node) 1if (io_ctl!	btrfs_ = io_ctl			spin_ubreak		spin
_ = io_ctlry(n, struct btrfs_free_space, offset_index);
		if (!p e;
	}
	
	if (!ilock_;
	keyc< >offset = le6eturn
			nnfe *	 */		spf (ret >  for thc >offset = le6eturn
			nnfe *one
			sp	io_ctlubreak		sn_block_g)
{
	_onlyif (io_ctl!
{
	int fn -ENOSP		io_ctl-ent_bitmap);
		ift fn -ENOSP	
	if (!il Som	w upentries aryt if  entries inybve cshnned	amet for t,om	w uioare frfoseripentries arytcomeg ahe tr entries iny.
	ll  (ex_next(n)) {
		;(io_ctl!	bt fn -ENOSP		io_ctl= io_ctlry(n, struct btrfs_free_space, offset_index);
		if (!p-ent_bitmap != offset) {
			t fn -ENOSP		io_c
c	WARN_ONl!
{
	iap);
		if (re -ENOSP	
	if (pi f (ret > 
{
	inf (io_ctl
{
	iap);
		ifelear_ Som	ll n_ct e;
itriesentries inyb
 * osffset for t,om	ll nee n't lon 0 if iis_*ise ahe entriepentries aryom	ll  (exl_next(n e;
y->list);_index);
		if (!po_ctl	btrfs_ p e;
	}
ry(n, struct btrfs_free_space, offseoto fn	_index);
		if (!poprev)
			gap = kzal  (nt  struoffset + prev->bytes == e->>t) {
			t fntl= io_ctlbyte		sp
	}

	foe -ENOSP	
	if (pi(!i_size			goto  -ENOSP		io_c
c ouirst we wes inybbeockefanwe');

	/'hile = io_ctlbyte		s-ent_bitmap != off>t) {
			clear_next(n e;
y->list);_index);
		if (!p_ctl	btrfs_ = io_ctlry(n, struct btrfs_free_space, offse
o fn	_index);
		if (!po(path->_bitmap != off<et) {
					spi f (relear_und_kuzzytr	 fn -ENOSP	
	if (	sp	io_ctlun -ENOSP		io_ctl}(pi(!i_siz
{
	iap);
		ifelear_next(n e;
y->list);_index);
		if (!p_ctl	btrfs_  e;
	}
ry(n, struct btrfs_free_space, offseoto fn_index);
		if (!po_ctl
			gap = kzal  (nt struoffset + prev->bytes == e->>t) {
			t fnt -ENOSPbyte		sp}(!p-ent_bitmap != off+_DR_BITMAP		(PAGmittee_sco_c>>t) {
			t fn -ENOSP	
	if (pi f (ret > 
{
	iap != off+_
{
	iap)= e->>t) {
			t f -ENOSP	
	if (!i_ctl
kuzzytr	  -ENOSP		io_c
c(node) 1if (io_ctl
{
	iap);
		ifelear_-ent_bitmap != off+_DR_BITMAP		(PAGmi(nt strutee_sco_c>>t) {
			t fnubreak		spi f (relear_und_
{
	iap != off+_
{
	iap)= e->>t) {
			t f_ubreak		spin
_ _next(n)) {
->list);_index);
		if (!p_ctl!	bt fn -ENOSP		io_ctl= io_ctlry(n, struct btrfs_free_space, offset_index);
		if (!urn 0;
fai	
	if (tic noinli crc's oinl
___free_space(ctl, e io_ctlfree_space_ctl *ctl,
				   suct_ io_ctlfree_space *entry 
	nntruct y(nr_ctl&pinned) {
		);
		imap_ee_space_offset);

	/* G	_ee_space_s;

	stbtrftic void noinli_free_space(ctl, e io_ctlfree_space_ctl *ctl,
				   s struct btrfs_free_space *entry)
	nntruct___free_space(ctl, enode, 0fo* G	_ee_space_*entry-= pinned
			kmetic int flush_ree_space(ctl, e io_ctlfree_space_ctl *ctl,
				   s str btrfs_free_space *entry)
	nntructt = 0;

	INIT_L(io_ctl-inned
			kev->
	nnedg {
			;= io_ctloot;
_empty_;
	keytp_ee_space_offset);

	/, pinned) {
		   stru&pinned) {
		);
		ima ==nnedg {
		fset
		if (ret)
		return -1;

		if (!i_ee_space_*entry+= pinned
			kme	_ee_space_s;

	st	ctl- ret;
}

static void noinli_thresul
	iholds(ctl);
 btrfs_free_space_ctl *ctl)
{
	struct btrfs_free_sgroup_cache *block_group,
			 			ree_spriv
	i;ct es,max;
	int re es,
list);
	int re es,_bits(&B	int re es, PAGE_Cgroup->key.offset, exte;ct es,
uper)pernpgFS_DR_BITMAP		(PAGmittee_sco_cfs_t = max;
 = 0;
	indiv64_uset PAGE+,
uper)pernpgFached
uper)pernpgeturnmax;
 = 0;
	intentmax;
 = 0;

	/*
	 *(io_ctlotal_bitmaps++;
		f<etmax;
 = 0;
*
	 * If thiee sgoale
	 *o keenext obitmat of free smemoo_c
				per 1gbee s	 *
	 * W a t_r s tow 32k, can loo crc thadave a ow muchsmemoo_cwcated w clear * W 
				byesentrieba				pace that arra= 1;
	if (io_ctl PAGE< 1024 W 1024 W 1024turnmax;
	int	inMAX_SIZE, BYTEBITMAPGIG;
		io_ctlmax;
	int	inMAX_SIZE, BYTEBITMAPGIGmi(nt div64_uset PAG, 1024 W 1024 W 1024t
	 * If thild h add anaccf free ke1pages g {
		fanantw just
 witchean lock ts_roo we d can fileak tgo  * oange  * oe bigoalee sMAX_SIZE, BYTEBITMAPGIGmasf thild y piages g {
		/
	ret = 
list);
	intl->free_sbitmaps++;
		f		lethiACHE_SIZE;
	if (cblock_g)
{
	_)= e->>=,max;
	intclear_eee_ss;

	stholds(c			goto r
	}

	if (in If thild h add ent entries inybllds(ctl)blocklway mar_jusmoe a1/2d entmaxwf thi)= e-> have contiet_r w jue spl
	 lessfanantre f
	ret = _bits(&B	int =,max;
	intFac
list);
	int re_bits(&B	int =,min_n, es,,_bits(&B	int,ndiv64_usetmax;
	int, 2io_ctleee_ss;

	stholds(c		
t div64_uset_bits(&B	int,n((struct btrfs_free_space_entry)))tatic noinli crc's oinli_p_entrieextentpins( io_ctlfree_space_ctl *ctl,
				   structruct btrfs_free_space *entry)
	nn   structructfset, u64 bytes,
			 truct d long size;
	len, Ncf frart = block__index)tonpin,pinned) {
		 ttee_sco_c,t) {
					scf fre=
super)tonpins(B	int,ntee_sco_c			s(io_ctl len,s+Ncf frf<etDR_BITMAP		(PAGo_ctl_entrieexten ==nnedg {
		,
	len, Ncf frath == nned
			ke-=
supertatic void noinli_entrieextentpins( io_ctlfree_space_ctl *ctl,
				   stctruct btrfs_free_space *entry)
	nn tfset,
		  int  structtes,
			 truct_p_entrieextentpins(node, 0fo);,
		  i ;
	*type _ee_space_*entry-= supertatic void noinli_entriedex)pins( io_ctlfree_space_ctl *ctl,
				   stctru btrfs_free_space *entry)
	nn tfset,
		  int  strutes,
			 truct d long size;
	len, Ncf frart = block__index)tonpin,pinned) {
		 ttee_sco_c,t) {
					scf fre=
super)tonpins(B	int,ntee_sco_c			s(io_ctl len,s+Ncf frf<etDR_BITMAP		(PAGo_ctl_entrie_ctl-=nnedg {
		,
	len, Ncf frath == nned
			ke+= supertai_ee_space_*entry+= supertaticSince don't ve ctch irst suitablnt entririte o add
		i)= e->locry
 rd
l ntent PAGE entriemaxt entristatic int __btrf_slot(tpinruct io_ctlfree_space_ctl *ctl,
				   stu btrfs_free_space *entry)_entrie
	nn tfset*,
		  int  sfset*
			 truct d long size;
key.ofpins			goto d long size;
max;
 =s			goto d long size;

 =s,for o d long size;
)) {o_ctlr o d long size;
_bits(&BTRFth ==ck__index)tonpin,_entrie
	nned) {
		 ttee_sco_c,nt  stmax;n, es,,*,
		  i ;entrie
	nned) {
		));ctrlise=
super)tonpins(*B	int,ntee_sco_c			(n = safe(poex)pin_paom(ii ;entrie
	nnedg {
		,
DR_BITMAP		(PAGoelear_) {o_ctld_first__) {o_ctlnpin,_entrie
	nnedg {
		,
	 structructDR_BITMAP		(PAG,foxtent_end = rlise=
_) {o_ctld-for oxtent_start rliseck_gli		block_key.ofpins			_bits(&BTRFthfnubreak		spi f (retent_start rlisec
max;
 =s	block_max;
 =s			_bits(&BTRFthfn}(!p-e=
_) {o_ctl (pi(!i_sizkey.ofpins	block* = offset1);
	(imittee_sco_cizeo;entrie
	nned) {
		thfn*
	intl->f);
	(key.ofpins	bittee_sco_cfs_n 0;
	}
	spin_bl*
	intl->f);
	(max;
 =s	bittee_sco_cfs_ -1;
}

static/* Chat we nt PAGE entriemaxt entriuioa
	intlhic int __ io_ctlfree_space *entry 
irst_eace(ctl, e io_ctlfree_space_ctl *ctl,
				 tfset*,
		  isfset*
			 ,
	  d long size;
allonisfset*max;_state *PAGtruct btrfs_free_space *e, *pre
	if (p list_he *node = rb_;ct es,u32 crces,allont);
;ret;

	if (!i_sizeree_space_offset);

	/.e *nodeto out;

	ret = = io_ctloot;
_slot(t;
	keyt			 t)
{
	s)tonpinruct			 t*,
		  ));
	if (ret < !
{
	int fut;

	ret =  = rb_lookup->list);_index);
		i;  rb_;  rb_next(node);
		ife = kme io_ctlry(n, strutruct btrfs_free_space, offset_index);
		*entri_ctl
{
	iap)	intl<t*
			 telear_und_
{
	iap)= e->>t*max;_state *PAGtr str*max;_state *PAG	}
	
	ifs;
			kmem_cuous nu

		if (ty the rodn=ts(ace cache, -1;
}o fr mai;
_nhe som	w ute s_ freege *equesross llonmentom	if (extent*)= e->>=, llontelear_kmap(i
{
	iap != offacize >=len,zeo llonFachmem_cdo_div(al;
	 llont;ear_kmap(ikmap*o llonF+cize >=len,;ct		allont);
p(ikmap-i
{
	iap != of		spi f (relear_allont);
p(i0;ear_kmap(i
{
	iap != of
		if (ty_ctl
{
	iap)	intl<t*
			 zeo llont);
telear_und_
{
	iap)= e->>t*max;_state *PAGtr str*max;_state *PAG	}
	
	ifs;
			kmem_cuous nu

		if (ty_ctl
{
	iap);
		ifelear_ es, PAGE_C*
			kmet fn -EE_C_slot(tpinructntry->bitme);
l;
	&*PAGt (!po_ctl

				btrfs_* = offsetu32 cr	fn*
	intl->*PAG;r	 fn -ENOSP	
	if (	spi f (retent= sizet*max;_state *PAGt	btrfs_*max;_state *PAG	}
*PAG;r	 f}em_cuous nu

		if (ty* = offsetu32 cr	*
	intl->
{
	iap)= e->-,allont);
;ree -ENOSP	
	if (pi(io_ctl -ENOSP		io_ctic void noinlitmapnewtpinruct io_ctlfree_space_ctl *ctl,
				   sturu btrfs_free_space *entry)
	nn tfset,
		  tructt nned) {
		ck__index)tonpinructntry-) {
					s= nned
			ke(i0;eaIST_HEAD(&bitmap= nned
	if (reree_space(ctl, enode, 0fo* G	_ee_sbitmaps++;
			ctl
op->recalc_thresholds(ctl);
			spitic void noinlipace(pinruct io_ctlfree_space_ctl *ctl,
				   st btrfs_free_space *entry)_entrie
	nntruct dree_space(ctl, enode,_entrie
	nnt G	ktrfs_fentrie
	nnedg {
		t G	kche_free(btrfs_free_space_cachep, e);
		_entrie
	nnt G	_ee_sbitmaps++;
		btrfsp->recalc_thresholds(ctl);
			spitic void nne_for_s t;

	iree_spaomtpinruct io_ctlfree_space_ctl *ctl,
				   sturuuru btrfs_free_space *entry)_entrie
	nn nt  structtes,*,
		  isfset*
			 truct es,}

	re es, slot(trstart);slot(tp			kmemt;

	if (!
	spin_lmin(bl;entrie
	nned) {
		zeof);
	(DR_BITMAP		(PAGmittee_sco_c)Fachme * We want to crc th_slot(gock g =s	ioarer maintrirunWe have aonlyb
 * o somoo we  entriesentriuioarer maintrifanank->lochve mo y pi offsetcan loo cr
	w ute _slot(gock as muchsas itsas 't ve c
	 *cxtenare freof frra
	 *e ca
	w uge _slot(0e sp key wene ifain
	spin_loslot(trstarE_C* != of
		;slot(tp			k			ree_sco_cfs_;slot(tp			k			ock_;slot(tp			k, extentoslot(trstarE+return 0;E_C_slot(tpinructntry-_entrie
	nn t& slot(trstart)&;slot(tp			k (ret < 0) {
		ev->oslot(trstarE!_C* != ofturn -ENOSPC;INVAo_c
c ou have converkey.o ages g {sfanantw just
 o crcin_loslot(tp			k			ock_;slot(tp			k, *p			k (r
c ouCantch cxtenapasend of the entrieaintrifin_loslot(tp			k			ock_;slot(tp			k, extentoslot(trstarE+returtl_entrieextentpins(node,_entrie
	nn t slot(trstart);slot(tp			k (re* = offs+=);slot(tp			kmem*
			ke-=
;slot(tp			kme
xtent*)= e-struct btrfs_e *node = e ifext(n)) {
-;entrie
	nned) {
		);
		if (!p_ctl!fentrie
	nnedg			 trck_kace(pinructnode,_entrie
	nnt G
il Som	w unoes aryt he traniseg {
		,
buust
 st addonver)= e->loom	w u	iree_etcansomoin theong gasonamoun.
	ll  (ex_ctl!	) {eturn -ENOSPC;INVAo_c
c	_entrie
	nnctlry(n, stru			 t btrfs_free_space, offseoto fuctruct_index);
		*ent(bl Som	w uifey wene ifs arytis the eaintrif loo crc th -ENOSPd fr clust
	ll n is outnot
 *do  {sfwork.
	ll  (ex_ctl!fentrie
	nnedg {
		tturn -ENOSPC;AGAINnt(bl Som	w uOkey wene ifdcaus lefeg {
		,
buusitave ctch actuaory ctl)
	ll ntent
	nnrmeh0;
	p key wef the page offace that anot
 etca
	ll nlooquick irra
	 *ion't ch theirst irn.
	 */hean lock taryom	l n * onin the * o 
	spi.
	ll  (exoslot(trstarE_C* != of
			;slot(tp			k			ree_sco_cfs_n 0;E_C_slot(tpinructntry-_entrie
	nn t& slot(trstartoto fuctr&;slot(tp			k (reet < 0) {
		ev->oslot(trstarE!_C* != ofturnn -ENOSPC;AGAINnt(blgain;
		}
nexi f (retent!fentrie
	nnedg			 trckkace(pinructnode,_entrie
	nnt G
i 0;
}

static int flces,amap(uper)tonpinruct io_ctlfree_space_ctl *ctl,
				   sturuurut btrfs_free_space *entry)
	nn tfset,
		  int  structutes,
			 truct es,
uper)tons;
			goto es,}

	r_lmin(bl
	nned) {
		zeof);
	(DR_BITMAP		(PAGmittee_sco_c)rfs_iuper)tons;
			ock_extent,
		  i ;
	*typetl_entrie_cttpins(node, 0fo);,
		  i ;
	*t)tons;
t G
i 0;
}

;
	*t)tons;
 G
tic int flatcheuse(pinruct io_ctlfree_space_ctl *ctl,
				   sstruct btrfs_free_space *entry)
	nntruct btrfs_free_sgroup_cache *block_group,
			 			ree_spriv
	i;c * If we aren't at s tow triesentrisbllds(ctl)bl can't truhe bitm lefs	 ioml n is oura
	 *ch theonverk foealthe
 * tr,_entri	if (io_ctl_ee_space_s;

	st{
	eee_ss;

	stholds(c/* This om	l ns block group has been msaces me bis;

	st{'t ch theh add aom	l n
		i it llr range face t],
	t free sfile (he
 * trmrite oantom	w ute ds(erv.
	 */Pd frarg tr entrik, hvee spl
fst
 witchplentom	ifr rafile (	 */bl cange cachedantde bitmamn 'v>osn(retng the p
	ll ntent * oh ahe en eaintrifion't ch theonverk .
	ll  (ex_ctl= nned
			ke<_Cgroup->key.ofns
 or*PAG	* 4telear_und__ee_space_s;

	st{* 2e<_Ceee_ss;

	stholds(c/r	 fn -ENOSPfa (r		spi f (relear_ -ENOSPfa (r		spiif (in If thiee s		ioinaltgroup has bsspaom	mkfs truhbon orory  me b);
	ke 8f thimegap			k, eanch thebthen whe
 * eaintrifp key em.
e.  This wHvee spo we dometgroup has bsstruhbon me b tranantppearseaintrif ave a
 * o buu * W ar
 st addrarg 
_nhe sare frill bhave ao* oftow trie32ksmemoo_clim_c,ntwe donted w c em.
group has bsste _t added ied wcrc thllocatp entrioml n  iny.
	f (io_ctl((DR_BITMAP		(PAGmittee_sco_c)F>>	let>_group->key.offset)
			retur -ENOSPfa (r		
i 0;
}

btretatic int fl io_ctlfree_space_ctl *cop pace_ctl *cop =elea._thresholds(ctl);	=i_thresul
	iholds(ctl);,nt.use(pinruc		=iuse(pinruc,
};ic int io_ctl_empty_ree tpinruct io_ctlfree_space_ctl *ctl,
				   sturuuru btrfs_free_space *entry)
	nntruct btrfs_free_space *entry)_entrie
	nn;ct btrfs_free_sgroup_cache *block_group,
			 				int ret;

into f		goto es,p			k, ,
		  i ;
	*t)into ;ret;

	if (!ip			k			pinned
			kme	) {
		ck_
	nned) {
		th!i_sizeree_scalcuse(pinructnode, 0fo*turn 0;

	io_ctl_nd__ee_sop == &pace_ctl *copturngroup,
			 			ree_spriv
	i;c
	spin_l Some spe would ree_ps = 0;
	one
	 ree suster for thw to crc th_sefion'tome sllocatpr for then era
	 *iondontst irnen mnge aintrif loo crc thinthe bitmappace that aroare fraintrir
	f (io_ctlgroup && !list_empty(&block_group->cluster_list)) {
		cluste btrfs_free_cluster *cluster = NUL;uct btrfs_e *node = rb_;ctt btrfs_free_space *e, *pre
	if (
ter = list_entry(block_group->cluster_list.next,
				     struct btrfs_free_cluster,
				     block_group_list);
	}

	iflock(&ctl->tr->root);
		ret =  rb_first(&cluster->root);
			clustode && cl	spin_lock(&(&ctl->tr->root);
		ret = next;
	oer,
				npinrucrftpin
_ = io_ctlry(n, strutruct btrfs_free_space, offset_index);
		*entri_ctl!
{
	iap);
		ifelear_ock(&(&ctl->tr->root);
		ret = next;
	oer,
				npinrucrftpin
_ und_
{
	iap != off=k__index)tonpinructntry-) {
				block_g
	*t)into ctlamap(uper)tonpinructntry->bitme
	 str blo,
		  i ;
	*type k_g
	*ty-= super)into ;refn_indexy+= super)into ;refin_unlock(&block_r->root);
		ret = tent!f			 telear_r;
out:
		 out;
		}
		spin_in
	oer,
				npinruc:
	_entrie
	nnctloot;
_slot(t;
	keyt			 t)
{
	s)tonpinruct			 t,
		  ))
	 strchedf (ret)
	!fentrie
	nntelear(io_ctlinto ct=df (renext;
		wnpinrucrft}fs_iuper)into ctlamap(uper)tonpinructntry-_entrie
	nn t,
		  i ;
	*type g
	*ty-= super)into ;re_indexy+= super)into ;reinto f		got
 tent!f			 telearr;
out:
		 ut;
	}
	spin 	io_ctlgain;
		}
ne
		wnpinruc(ret < 
	nncst_
	nnedg {
				kmemtmapnewtpinructnode, 0fo);,
		   (reninto f		:
		 
	nnctl		io_ctlgain;
		}
nexi f (re{n_unlock(&block_ree_lock);

	if (!mc ou'v>pre-aor beho to an,taor behcatpneweason  (ex_ctl!
	nntelear 
	nnctlache_zalloc(btrfs_free_space_cachep,
				      _NOFS);
		goto fx_ctl!
	nntelear lock(&ctl->tree_lock);
			ret =  -1;
	}
ENOMEMet =  ut;
		}
		sp
	}

	f!mc ouaor behcatrieaintrifin_l	
	nnedg {
		 loc(PAGE_CACHE_SIZE, GFP_NOFS);
			if (!ock(&ctl->tree_lock);
			ret =node)
	nnedg {
				kmem_-1;
	}
ENOMEMet = ut;
		}
		spin_ugain;
		}
nexi
	if (ret < 
	nnteleardex ==nnedg {
			t = ktrfs_
	nnedg {
		t G		kche_free(btrfs_free_space_cachep, e);
		
	nnt G	turn 0;
}



static void natchefe(espace_pace(ctl, e io_ctlfree_space_ctl *ctl,
				   s st btrfs_free_space *entry)
	nn tatcheuinode( voitruct btrfs_free_space *entry)	 */e
	nn;ct btrfs_free_space *entry)one
	e
	nn;ctatchedpace f		fa (r		sfset,
		  ck_
	nned) {
		tho es,p			k			pinned
			kme_l Some sso we ld h add an_sefionhen u)he pace that aadjat nabtrfanwe*/
		i'tome sded to e p,fionhen u)he 	iree_are fr btrfs_ if a pinpneweason aommittev tranwe nairwe*/
			ret = wne
	e
	nnctloot;
_slot(t;
	keyt			 t)
{
	sE+,
uper, NULL (ret < 0ne
	e
	nncst_t(n e;
y-0ne
	e
	nned) {
		);
		if	t =	 */e
	nnctlry(n, strt(n e;
y-0ne
	e
	nned) {
		);
		if     struct btrfs_free_cluste offset_index);
		*entr	io_ctl	 */e
	nnctloot;
_slot(t;
	keyt			 t)
{
	sEached0
	lock_et < 0ne
	e
	nncst_!0ne
	e
	nnedg {
				kmemt < uinode( voitrar_ dree_space(ctl, enode,0ne
	e
	nnxtent_io_ctlu___free_space(ctl, enode,0ne
	e
	nnxtent= nned
			ke+= 0ne
	e
	nnedg			kmem_kche_free(btrfs_free_space_cachep, e);
		0ne
	e
	nnxtentdpace f		btretapi(!i_siz	 */e
	nncst_em */e
	nnap = kzal  (nructm */e
	nnap)
{
	sE+,m */e
	nnap 			k		et) {
			cleart < uinode( voitrar_ dree_space(ctl, enode,m */e
	nnxtent_io_ctlu___free_space(ctl, enode,m */e
	nnxtentt nned) {
		ck_m */e
	nnap)
{
	stent= nned
			ke+= m */e
	nnap 			kmem_kche_free(btrfs_free_space_cachep, e);
		m */e
	nnxtentdpace f		btretapi(!i 0;
}

dpace tatic void natchese alspaomtpinruc)tonend( io_ctlfree_space_ctl *ctl,
				   structru btrfs_free_space *entry)
	nn   structruatcheuinode( voitruct btrfs_free_space *entry)pinrucrft d long size;
or o d long size;
jrfsponwe  es,}

(bl
	nned) {
		zeopinned
			kme	_onwe  es,pinruc)) {
		ck__index)tonpinructntry-}

)tho es,p			kpetl_entrictloot;
_slot(t;
	keyt			 tpinruc)) {
		,chedf (ret)
	!fentrietur -ENOSPfa (r		
i=ck__index)tonpin,_entried) {
		 ttee_sco_c,t}

)thojd_first__) {o_ctlnpin,_entriedg {
		,
DR_BITMAP		(PAG,foxtent)
	j		etietur -ENOSPfa (r		ip			k				j	-tiemittee_sco_cfs_t nned
			ke+= superta
rt < uinode( voitrar_entrieextentpins(node,_entri,t}

i ;
	*type _io_ctl_p_entrieextentpins(node,_entri,t}

i ;
	*typeret)
	!fentriedg			 trckkace(pinructnode,_entri)		
i 0;
}

btretatic int flatchese alspaomtpinruc)tonpaonyt io_ctlfree_space_ctl *ctl,
				   structruct btrfs_free_space *entry)
	nn   structructatcheuinode( voitruct btrfs_free_space *entry)pinrucrft es,pinruc)) {
		rft d long size;
or o d long size;
jrfs d long size;
 e;
_jtho es,p			kpetl_entri)) {
		ck__index)tonpinructntry-
	nned) {
		);
c ouIe not on pintatundatme)inybllet e;
itrielogicaomaintriru (io_ctlgentri)) {
		ckbl
	nned) {
		teleardex ==nned) {
		ckbl0turnn -ENOSPfa (r		sp_entri)) {
		ck__index)tonpinructntry-
	nned) {
		Eacht G	turn_entrictloot;
_slot(t;
	keyt			 tpinruc)) {
		,chedf (ret)
	!fentrietur -ENOSPfa (r		
i=ck__index)tonpin,_entried) {
		 ttee_sco_c,t
	nned) {
		teachmemjf		goto e;
_jl->f)d long)header
out: = safe(pextentpin_paom(j tpinrucedg {
		,
DR_BITMAP		(PAGoeleart)
	j	>tieturubreak		sp e;
_jl->j;o_ctl_)
	 e;
_jl-etietur -ENOSPfa (r		tl_)
	 e;
_jl-etf)d long)header
otrar_			k				if		lethitee_sco_cfs__io_ctl_			k				if-
 e;
_jethitee_sco_cfsctt nned) {
		c-= superta_t nned
			ke+= superta
rt < uinode( voitrar_entrieextentpins(node,_entri,t
	nned) {
		 t;
	*type _io_ctl_p_entrieextentpins(node,_entri,t
	nned) {
		 t;
	*typeret)
	!fentriedg			 trckkace(pinructnode,_entri)		
i 0;
}

btretaticSince Wet e;eaf;klway mtouaor behcapaom	 entries in th,ebthefp ker,
				oss nd
l nnon-r,
				oss or beh0;
	*equesrs. So w canatt&blo write a pinpnewe entri
l n  inye)inyblan_sefionhen u'saadjat nabpace that ai
}
list)es in th,e
	 *io
l nten u)he, migrehcatr fr hat apaom	tmaps */
	litrfanwe entristat L	ke lock ld gengs tlistchanat ae s	eh0sfy writhat aaor beh0;
	*equesrstat beca
		in't tt&bloblan_eh0sfy
	 */Pba				 pints wrl sfile (  inye)
	 *ne spomount.2t_r ages s in thp-i
vtr ifey wes in thprepds(eaddaouous gutriepace that 
w u	ig0;
	(e.g. 1t entries inyb		lipentries arytrstar wriwen u) ent entries iny
l n  ds)static int __oinlise alspaomtpinruc( io_ctlfree_space_ctl *ctl,
				   stctruct btrfs_free_space *entry)
	nn   stctructatcheuinode( voitruct Some sOnlybworkwhe
 *disuounecho ts in th,e
s 't ve cchangu) enirt,
		  int -1 if mve abnt entries in thr
	f (io(io_ctl)
	nnedg {
			;io(io_ctlRB_EMPTY_NODEl&pinned) {
		);
		i))_ctl_nd__ee_sbitmaps++;
		f
		structatcheseole	}

	retatcheseole	paonyf		fa (r		ctt bole	}

E_C_e alspaomtpinruc)tonend(node, 0fo);uinode( voitet =node_ee_sbitmaps++;
		f
		sear_oeole	paonyf		se alspaomtpinruc)tonpaonytnode, 0fo)
	 str b	;uinode( voitett =node bole	}

Ev->oeole	paonysear_fe(espace_pace(ctl, enode, 0fo);uinode( voitet }t load_fs_write_tmappace(ctl, e io_ctlfree_space_ctl *ctl,
				   s strfset, u64 bytes,
			 truct io_ctlfree_space *entry 			"iltt = 0;

	INIT_L
	nnctlache_zalloc(btrfs_free_space_cachep,
				 OFS);
			if (_ctl!
	nnt
nn -ENOSPC;NOMEMetctt nned) {
		ck__indexta_t nned
			ke= superta_RB_CLEAR_NODEl&pinned) {
		);
		i)uc_lock(&block_ree_lock);

	if (!m_ctlfe(espace_pace(ctl, enode, 0fo);btre)nt fut;

ree_
	 * If thiee can .
 */t entriedirechlyitrfanwe	 */b_r one
	 * block newoml n is oubl can't k've mo'ng to write onverk faor behcatpnewe is ourasaommitbeockef't ch tr fr sefion'too crc thagesns the
nlock_gentri	if (ioio_ctl_empty_ree tpinructnode, 0fo* G	t < 0) {
		structut;
	}
	spin 	io_ t) {
				btrf0;
			goto ut;
	}
	spin
ree_:ct Some sOnlybse albpace that apaom	 djat nabs++;
		fie not ond can ft onnot
	w uge write a piy wenewppace that aroa */
	ie;

 =st)es in thFac
eca
		he bitmjustave amee cuunecessarybworkwtmjustave abon o spho .iee caockeom W a t&bloblan_e albthat apaom	s++;
		fie not onto e p ant entries iny
	spin_loe alspaomtpinruc(node, 0fo);btre)t = io_ctlree_space(ctl, enode, 0fo* G	t)
		returnkche_free(btrfs_free_space_cachep, e);
		
	nnt Gif (renlock(&block_ree_lock);

	if (!mt) {
				btrfprreek(KERN_CRIT "DC_CL:cuuablntte a pipace that a:%d\n", 0;
tet =(io_ctlroffsetC;EXISTt G	turn 0;
}



staticad_fwrite_	iree_space(ctl, e io_ctlfree_sgroup_cache *block_group,
			    structfset, u64 bytes,
			 truct io_ctlfree_space *entrctl = block_group->free_space_ctl;
	struct btrfs_free_space *entry 			"iltt = 0;
;ctatcher;
_slot(f		fa (r		ctock(&block_ree_lock);

	if (!
	spin_l0;
			gototent!f			 tto ut;
	}
	;

	iIT_L
	nnctloot;
_slot(t;
	keyt			 t)
{
	s, NULL (ret < !
	nntelears om	l noo		fdid theirst ant entrietmjuss_ fr pinned*entryld h ade)
	ll ntou	iree_etlooquick  eaintrifiise ah
	ll  (ex_	nnctloot;
_slot(t;
	keyt			 t)
{
	s)tonpinruct			 t,
		  ))
	 strcchedf (rex_ctl!
	nntelear  Som	ll naren'tkey.o apptar aomainr range face that ai
}64b	ll naintrifbuubl canct lose tirst tent hen wptarns themayom	ll ned it eoblnm, eanWARN abouusitwom	ll  (exlWARN_ONlr;
_slot(et = next;
}
	;

	iIT

	for (ie;
_slot(f		fa (r		=node)
	nnedg {
				kmem_free_space(ctl, enode, 0fo* G	lock_;
	keyckbl
	nned) {
		teleart es,uospace			ock_
uper, 
	nnedg			 tmet fnt nned
			ke-= uospace;t fnt nned_indexy+= uospace;t fntctl= nned
			ktelear lio_ctlree_space(ctl, enode, 0fo* G	exlWARN_ONlr;
tet =pi f (relear_ikche_free(btrfs_free_space_cachep, e);
		
	nnt G	

	f!mc	_indexy+= uospace;t fn
			ke-= uospace;t fngain;
		}
nexpi f (relear_fset,ld_}

(bl
	nned
			 zeo
	nned) {
		th!i	_t nned
			ke= ) {
		Eac
	nnap)
{
	stentlio_ctlree_space(ctl, enode, 0fo* G	exWARN_ONlr;
tet =pt)
		returno ut;
	}
	;

	iIT_Lmc ouNot
_nhe sa
			keioarer me inybloc_eh0sfy
usl  (exlock_,ld_}

(<t)
{
	sE+,
upertelear l
			ke-= ,ld_}

(- )
{
	stentl	) {
		ck_,ld_}

et =  ut;
	
		}
nexppi f (ret >  ld_}

(b=t)
{
	sE+,
upertelear l ouaornch on  (ex next;
}
	;

	iIT

f}em_cnlock(&block_ree_lock);

	if (!mc btrfs_truncatmappace(ctl, egroup, path);)
{
	sE+,
uper,
 str blo ,ld_}

(- ()
{
	sE+,
upert* G	exWARN_ONlr;
tet =put;
		}
		spin_in
 btrfs_	iree_spaomtpinructnode, 0fo);&)
{
	s, &p			k (ret < 0) {==PC;AGAIN		btrf0;
_slot(f		btretapugain;
		}
nexi
}
	;

	i(renlock(&block_ree_lock);

	if (io_ctl -ENOSP

staticoinli_runcacumpspace(ctl, e io_ctlfree_sgroup_cache *block_group,
			    structes,
			 truct io_ctlfree_space *entrctl = block_group->free_space_ctl;
	struct btrfs_free_space *entry 			"ilt btrfs_e *node = iltt = cf fre=
0t =  = rb_first(&clusteree_space_offset);

	/*  n  nnext(node);
)		btrfd.nnctlry(n, struct btrfs_free_space, offset_index);
		*entri_ctlt nned
			ke>= supercst_egroup->free_srnt
nnscf fr	ctl-_err(rocrin,_roup->free_sp,
			"free ruc"e inyb)
{
	sErebu t;
	*tErebu t;intrif%s"free ruc
	nned) {
		 tt nned
			k   sstructx ==nnedg {
				? "yes" : "no"f (!urnerr(ro==nn,_roup->free_sp,
			"f "group has been mr,
				?:f%s"frestructxmpty(&block_group->cluster_list)) {
			? "no" : "yes"ype grr(ro==nn,_roup->free_sp,
			"f  sstr"%d_group ae space that aa t_r sigg tranant
			keis"fNcf frathticoinli_runcaio_cspace *entrctl e io_ctlfree_sgroup_cache *block_group,
			 truct io_ctlfree_space *entrctl = block_group->free_space_ctl;
	strucctock(&bloc&io_ctlree_lock);
			ret tee_sco_cck_group->free_sns
 or*PAGet tee_srstarE_Cgroup->key.objectid);
	}
et tee_spriv
	iE_Cgroup->key.;fsp->reca = &pace_ctl *cop
	 * If thild onlybh add anlloce32kse sram	per group has be = rkeen1;
	if arra= ae space that ra
	 *ion't passa1/2d* bloae ld h add antwe dstarEconvpty0e sin thse * o 
 * @ie;

 =st)s	spin_leee_ss;

	stholds(c				(1024 W 32) / 2) /
 str(struct btrfs_free_space_entry)taticSince ick  egivtr r,
				  p the biof  {sfs;

	st{ba= aree sustepacence _hat a*blocwe dons .
group has b passe *ches thes_ fres .
group has bnce pe wtcrc thbysuster for t, domeh onf (rera=the cu
	 *pacet tennce r = listalnodeywe doare frfoseri nf* lon.
	 */hhe
 ched hange p annin thtatic int __btr
s_write_.
	 */er,
				nuospace(ctl, e  stctruc io_ctlfree_sgroup_cache *block_group,
			    structt btrfs_free_cluster,
				ter = NULtruct io_ctlfree_space *entrctl = block_group->free_space_ctl;
	struct btrfs_free_space *entry e
	if (p list_he *node = rb_;c
lock(&ctl->tr->root);
		ret =node_->root);group && !li!k_group->freent fut;

	ret = _->root);group && !litl		io_ct_->root);w;
	owtrstarE_Cgotoel_init(&entry-_->root);group && !l;
	if (rer rb_first(&cluster->root);
			clus(node) & cl	spin_atchepinrucrf
_ = io_ctlry(n, strutruct btrfs_free_space, offset_index);
		*entri rb_next(node);->list);_index);
		if (!p y(nr_ctl&>list);_index);
		imap_->root);
			clustRB_CLEAR_NODEl&>list);_index);
		if (rar_entri				
{
	iap);
		ifset
		if(rex_ctl!g {
				kmem_fe(espace_pace(ctl, enode,  inye)fa (rtet =poe alspaomtpinruc(node,  inye)fa (rtet =}em_oot;
_empty_;
	keytp_ee_space_offset);

	/,  struct>list);_index,p->list);_index);
		ie,_entri)		=}em_->root);
						RB_ROOT;
Gif (renlock(&block_r->root);
		ret =path *p
	;group && !l,_roup->freeturn 0;
}

static int floinli_p_rite_	iree_space(ctl, _free(brouped(
 str(btrfs_free_space_ctl *ctl)
{
	struct btrfs_free_space *entry 			"ilt btrfs_e *node = rb_;c
l(node) ( rb_next(nwe wteree_space_offset);

	/*)fset
		if	btrfd.nnctlry(n, strutruct btrfs_free_space, offset_index);
		*entri_ctl!
	nnedg {
				kmem__free_space(ctl, enode, 0fo* G	likche_free(btrfs_free_space_cachep, e);
		
	nnt G	
i f (relear_kace(pinructnode,
	nnt G	
itri_ctlo cr_	isBTRFS)felear_ock(&(&ctl->tree_lock);
			ret = conr_	isBTRFS);ear_ock(&ctl->tree_lock);
			ret =}t }t looinli_p_rite_	iree_space(ctl, _free(
 btrfs_free_space_ctl *ctl)
{
	struct ck(&ctl->tree_lock);
			ret _p_rite_	iree_space(ctl, _free(brouped(			spi_ock(&(&ctl->tree_lock);
			retticoinli_runca	iree_space(ctl, _free(
 btrfs_free_sgroup_cache *block_group,
			 truct io_ctlfree_space *entrctl = block_group->free_space_ctl;
	struce btrfs_free_cluster *cluster = NUL;uc btrfs_el_inh ahe*h ah		ctock(&block_ree_lock);

	if (l(node) (h ahek_group->free_sr_list.next,
				)fserestructx_group->cluster_list)) {
			learr = list_entry(block_h ahct btrfs_free_space,r,
				     block_group_list);
	}

	i
exWARN_ONl_->root);group && !li!k_group->freen;ctl_p_rite_.
	 */er,
				nuospace(ctl, egroup, path);r = NULt;tri_ctlo cr_	isBTRFS)felear_ock(&(&ctl->tree_lock);
			ret = conr_	isBTRFS);ear_ock(&ctl->tree_lock);
			ret =}t }t _p_rite_	iree_space(ctl, _free(brouped(			spi_ock(&(&ctl->tree_lock);
			retttictes,
ree_sprst_ctl, _ = sbtrfs_ io_ctlfree_sgroup_cache *block_group,
			    structtctfset, u64 bytes,
			 bytes,&bloc *PAG   structtctfset*max;_state *PAGtruct btrfs_free_space *e, *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *entry e
	ifitl		io_ct es,
uper)_slot(f		
			 zeo&bloc *PAG_ct es,0;
			gotoces,allontgri			gotoces,allontgri_lene=
0t = ock(&ctl->tree_lock);
			ret e
	ifitlirst_eace(ctl, enode,&)
{
	s, &p			k)_slot(     bgroup->free_spulltrsripe_len, max;_state *PAGt		=node)
{
	int fut;

	ret = r		ck__indexta_tctl
{
	iap);
		ifelear_entrieextentpins(node,  inye),
		  i ;
	*type k_ctl!
{
	iap)			 trck_kace(pinructnode,
{
	in;exi f (re{n_u_free_space(ctl, enode,
{
	in;ex	allontgri_lene=
) {
		Eac
{
	iap != of
		iallontgri			
{
	iap != of
	
_ = io_ed) {
		ck__indexE+,
uper;
exWARN_ONl
{
	iap)	intl<t
			 zeo llontgri_len)
	
_ = io_edg
	*ty-= superzeo llontgri_lenpe k_ctl!
{
	iap)			 trck_kche_free(btrfs_free_space_cachep, e);
		
{
	in;ex	_io_ctluree_space(ctl, enode,
{
	in;ex}Gif (renlock(&block_ree_lock);

	if (!mt) { llontgri_len)ctl_p_rite_tmappace(ctl, enode, llontgri,o llontgri_len)
	l -ENOSP

staticSince givtr a r,
				  p the biof  {sfs;

	st{ba= aree sustepace that 
w u*blocwe dona
group has b is passe ,age offun
 * oao addonlybpacence tpr for thre fraeeadelitrfanwepasse *group has bw This fOhen wiseriit' bigeddaoe;eafe wouonns .
group has b pe wtcrc thbysustnce r = listand 	iree_areter for thpaom	iistaticad_fwrite_	i	 */er,
				nuospace(ctl, e  stctrucuc io_ctlfree_sgroup_cache *block_group,
			    structttt btrfs_free_cluster,
				ter = NULtruct io_ctlfree_space *entrctl = bloiltt = 0;
;c
c ouirlus,igeddaosafe pe wtcritrfanwegroup has b in_lock(&ctl->tr->root);
		ret =nodeegroup->free	blockgroup->disk			r->root);group && !l(rex_ctl!group->free	block_ock(&(&ctl->tr->root);
		ret = n 0;
	}
	spi=}t } f (ret > _->root);group && !li!k_group->freenelears  domeh onf (reong alnodey*pacet itnch therech trnirtworkw  (exock(&(&ctl->tr->root);
		ret =  0;
	}
	spin_uatomic);
ck_group->clusterf frathxock(&(&ctl->tr->root);
		ret _leeeck_group->free_space_ctl;
	strucct ou'vwn.
	 */hanyesentrisbllter for thha		 piitnin_lock(&ctl->tree_lock);
			ret io_ctls_write_.
	 */er,
				nuospace(ctl, egroup, path);r = NULt;trnlock(&block_ree_lock);

	if (!m ouirsrory agesnege *efet = 
ath *p
	;group && !l,_roup->freeturn 0;
}



static void ntes,
ree_sbtrfsspaomtpinruc( io_ctlfree_sgroup_cache *block_group,
			    stturu btrfs_free_space r,
				ter = NUL   stturu btrfs_free_space *entry e
	if   structfset
			 bytes,min_rstartoto fuctfset*max;_state *PAGtruct btrfs_free_space *e, *ctl = block_group->free_space_ctl;
	structt = err;re es, slot(trstar			r->root);w;
	owtrstar;re es, slot(t
			ke= superta_ es,0;
			got_loslot(trstarE_Cmin_rstar;_loslot(tp			k			superta
rerrE_C_slot(tpinructntry->bitme); slot(trstart)&;slot(tp			k (ret < erroeleart)
	oslot(tp			k	>t*max;_state *PAGtr st*max;_state *PAG	}
*slot(tp			kmemn 0;
	}
	spin_bl 0;E_C_slot(trstar;_l_p_entrieextentpins(node,>bitme)r		 t;
	*typere -ENOSP

staticSince givtr a r,
				  tnyblocklr behca';
	*t'hpaom	ii,n.
	 */s 0
w uifeitnct lose tirst annin th suitablydrarg et_r telogicaomdisk__index
w uifey  thsework			 uttatictes,
ree_sbtrfsspaomtr,
				_ io_ctlfree_sgroup_cache *block_group,
			    structt btrfs_free_space r,
				ter = NUL tfset
			 b  structttes,min_rstarttfset*max;_state *PAGtruct btrfs_free_space *e, *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *entry e
	ifitl		io_ct list_he *node = rb_;ct es,0;
			got_lock(&ctl->tr->root);
		ret =nodep			k	>tr->root);max;*PAGtr sut;

	ret = node_->root);group && !li!k_group->freent fut;

	ret =  rb_first(&cluster->root);
			clusode && cl	
 out;

	ret = = io_ctlry(n, strutruct btrfs_free_space, offset_index);
		*entr(node) 1if (io_ctl
{
	iap)	intl<t
			 z&&_
{
	iap)= e->>t*max;_state *PAGtr st*max;_state *PAG	}
	
	ifs;
			kme(io_ctl
{
	iap)	intl<t
			 z||  sstrul!
{
	iap);
		iz&&_
{
	iap_indexE<,min_rstar)felear_ rb_next(node);->list);_index);
		if (!psode && cl	
 orubreak		sp = io_ctlry(n, strutruct btrfs_free_space, offse
 str t_index);
		*entricuous nu

		if (ty_ctl
{
	iap);
		ifelear_btrfs_truncattrfsspaomtpinruc(group,
			    sttstructttr = NUL t>bitme)
uper,
 str blo tttr = NUL);w;
	owtrstar,
 str blo tttmax;_state *PAGt		=	et < 0) {==P0telear l rb_next(node);->list);_index);
		if (!pssode && cl	
 oruubr