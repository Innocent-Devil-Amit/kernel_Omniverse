/*
 *  linux/fs/read_write.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/slab.h> 
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/aio.h>
#include <linux/fsnotify.h>
#include <linux/security.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>
#include <linux/splice.h>
#include <linux/compat.h>
#include "internal.h"

#include <asm/uaccess.h>
#include <asm/unistd.h>

typedef ssize_t (*io_fn_t)(struct file *, char __user *, size_t, loff_t *);
typedef ssize_t (*iov_fn_t)(struct kiocb *, const struct iovec *,
		unsigned long, loff_t);
typedef ssize_t (*iter_fn_t)(struct kiocb *, struct iov_iter *);

const struct file_operations generic_ro_fops = {
	.llseek		= generic_file_llseek,
	.read		= new_sync_read,
	.read_iter	= generic_file_read_iter,
	.mmap		= generic_file_readonly_mmap,
	.splice_read	= generic_file_splice_read,
};

EXPORT_SYMBOL(generic_ro_fops);

static inline int unsigned_offsets(struct file *file)
{
	return file->f_mode & FMODE_UNSIGNED_OFFSET;
}

/**
 * vfs_setpos - update the file offset for lseek
 * @file:	file structure in question
 * @offset:	file offset to seek to
 * @maxsize:	maximum file size
 *
 * This is a low-level filesystem helper for updating the file offset to
 * the value specified by @offset if the given offset is valid and it is
 * not equal to the current file offset.
 *
 * Return the specified offset on success and -EINVAL on invalid offset.
 */
loff_t vfs_setpos(struct file *file, loff_t offset, loff_t maxsize)
{
	if (offset < 0 && !unsigned_offsets(file))
		return -EINVAL;
	if (offset > maxsize)
		return -EINVAL;

	if (offset != file->f_pos) {
		file->f_pos = offset;
		file->f_version = 0;
	}
	return offset;
}
EXPORT_SYMBOL(vfs_setpos);

/**
 * generic_file_llseek_size - generic llseek implementation for regular files
 * @file:	file structure to seek on
 * @offset:	file offset to seek to
 * @whence:	type of seek
 * @size:	max size of this file in file system
 * @eof:	offset used for SEEK_END position
 *
 * This is a variant of generic_file_llseek that allows passing in a custom
 * maximum file size and a custom EOF position, for e.g. hashed directories
 *
 * Synchronization:
 * SEEK_SET and SEEK_END are unsynchronized (but atomic on 64bit platforms)
 * SEEK_CUR is synchronized against other SEEK_CURs, but not read/writes.
 * read/writes behave like SEEK_SET against seeks.
 */
loff_t
generic_file_llseek_size(struct file *file, loff_t offset, int whence,
		loff_t maxsize, loff_t eof)
{
	switch (whence) {
	case SEEK_END:
		offset += eof;
		break;
	case SEEK_CUR:
		/*
		 * Here we special-case the lseek(fd, 0, SEEK_CUR)
		 * position-querying operation.  Avoid rewriting the "same"
		 * f_pos value back to the file because a concurrent read(),
		 * write() or lseek() might have altered it
		 */
		if (offset == 0)
			return file->f_pos;
		/*
		 * f_lock protects against read/modify/write race with other
		 * SEEK_CURs. Note that parallel writes and reads behave
		 * like SEEK_SET.
		 */
		spin_lock(&file->f_lock);
		offset = vfs_setpos(file, file->f_pos + offset, maxsize);
		spin_unlock(&file->f_lock);
		return offset;
	case SEEK_DATA:
		/*
		 * In the generic case the entire file is data, so as long as
		 * offset isn't at the end of the file then the offset is data.
		 */
		if (offset >= eof)
			return -ENXIO;
		break;
	case SEEK_HOLE:
		/*
		 * There is a virtual hole at the end of the file, so as long as
		 * offset isn't i_size or larger, return i_size.
		 */
		if (offset >= eof)
			return -ENXIO;
		offset = eof;
		break;
	}

	return vfs_setpos(file, offset, maxsize);
}
EXPORT_SYMBOL(generic_file_llseek_size);

/**
 * generic_file_llseek - generic llseek implementation for regular files
 * @file:	file structure to seek on
 * @offset:	file offset to seek to
 * @whence:	type of seek
 *
 * This is a generic implemenation of ->llseek useable for all normal local
 * filesystems.  It just updates the file offset to the value specified by
 * @offset and @whence.
 */
loff_t generic_file_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file->f_mapping->host;

	return generic_file_llseek_size(file, offset, whence,
					inode->i_sb->s_maxbytes,
					i_size_read(inode));
}
EXPORT_SYMBOL(generic_file_llseek);

/**
 * fixed_size_llseek - llseek implementation for fixed-sized devices
 * @file:	file structure to seek on
 * @offset:	file offset to seek to
 * @whence:	type of seek
 * @size:	size of the file
 *
 */
loff_t fixed_size_llseek(struct file *file, loff_t offset, int whence, loff_t size)
{
	switch (whence) {
	case SEEK_SET: case SEEK_CUR: case SEEK_END:
		return generic_file_llseek_size(file, offset, whence,
						size, size);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL(fixed_size_llseek);

/**
 * noop_llseek - No Operation Performed llseek implementation
 * @file:	file structure to seek on
 * @offset:	file offset to seek to
 * @whence:	type of seek
 *
 * This is an implementation of ->llseek useable for the rare special case when
 * userspace expects the seek to succeed but the (device) file is actually not
 * able to perform the seek. In this case you use noop_llseek() instead of
 * falling back to the default implementation of ->llseek.
 */
loff_t noop_llseek(struct file *file, loff_t offset, int whence)
{
	return file->f_pos;
}
EXPORT_SYMBOL(noop_llseek);

loff_t no_llseek(struct file *file, loff_t offset, int whence)
{
	return -ESPIPE;
}
EXPORT_SYMBOL(no_llseek);

loff_t default_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file_inode(file);
	loff_t retval;

	mutex_lock(&inode->i_mutex);
	switch (whence) {
		case SEEK_END:
			offset += i_size_read(inode);
			break;
		case SEEK_CUR:
			if (offset == 0) {
				retval = file->f_pos;
				goto out;
			}
			offset += file->f_pos;
			break;
		case SEEK_DATA:
			/*
			 * In the generic case the entire file is data, so as
			 * long as offset isn't at the end of the file then the
			 * offset is data.
			 */
			if (offset >= inode->i_size) {
				retval = -ENXIO;
				goto out;
			}
			break;
		case SEEK_HOLE:
			/*
			 * There is a virtual hole at the end of the file, so
			 * as long as offset isn't i_size or larger, return
			 * i_size.
			 */
			if (offset >= inode->i_size) {
				retval = -ENXIO;
				goto out;
			}
			offset = inode->i_size;
			break;
	}
	retval = -EINVAL;
	if (offset >= 0 || unsigned_offsets(file)) {
		if (offset != file->f_pos) {
			file->f_pos = offset;
			file->f_version = 0;
		}
		retval = offset;
	}
out:
	mutex_unlock(&inode->i_mutex);
	return retval;
}
EXPORT_SYMBOL(default_llseek);

loff_t vfs_llseek(struct file *file, loff_t offset, int whence)
{
	loff_t (*fn)(struct file *, loff_t, int);

	fn = no_llseek;
	if (file->f_mode & FMODE_LSEEK) {
		if (file->f_op->llseek)
			fn = file->f_op->llseek;
	}
	return fn(file, offset, whence);
}
EXPORT_SYMBOL(vfs_llseek);

static inline struct fd fdget_pos(int fd)
{
	return __to_fd(__fdget_pos(fd));
}

static inline void fdput_pos(struct fd f)
{
	if (f.flags & FDPUT_POS_UNLOCK)
		mutex_unlock(&f.file->f_pos_lock);
	fdput(f);
}

SYSCALL_DEFINE3(lseek, unsigned int, fd, off_t, offset, unsigned int, whence)
{
	off_t retval;
	struct fd f = fdget_pos(fd);
	if (!f.file)
		return -EBADF;

	retval = -EINVAL;
	if (whence <= SEEK_MAX) {
		loff_t res = vfs_llseek(f.file, offset, whence);
		retval = res;
		if (res != (loff_t)retval)
			retval = -EOVERFLOW;	/* LFS: should only happen on 32 bit platforms */
	}
	fdput_pos(f);
	return retval;
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE3(lseek, unsigned int, fd, compat_off_t, offset, unsigned int, whence)
{
	return sys_lseek(fd, offset, whence);
}
#endif

#ifdef __ARCH_WANT_SYS_LLSEEK
SYSCALL_DEFINE5(llseek, unsigned int, fd, unsigned long, offset_high,
		unsigned long, offset_low, loff_t __user *, result,
		unsigned int, whence)
{
	int retval;
	struct fd f = fdget_pos(fd);
	loff_t offset;

	if (!f.file)
		return -EBADF;

	retval = -EINVAL;
	if (whence > SEEK_MAX)
		goto out_putf;

	offset = vfs_llseek(f.file, ((loff_t) offset_high << 32) | offset_low,
			whence);

	retval = (int)offset;
	if (offset >= 0) {
		retval = -EFAULT;
		if (!copy_to_user(result, &offset, sizeof(offset)))
			retval = 0;
	}
out_putf:
	fdput_pos(f);
	return retval;
}
#endif

/*
 * rw_verify_area doesn't like huge counts. We limit
 * them to something that fits in "int" so that others
 * won't have to do range checks all the time.
 */
int rw_verify_area(int read_write, struct file *file, const loff_t *ppos, size_t count)
{
	struct inode *inode;
	loff_t pos;
	int retval = -EINVAL;

	inode = file_inode(file);
	if (unlikely((ssize_t) count < 0))
		return retval;
	pos = *ppos;
	if (unlikely(pos < 0)) {
		if (!unsigned_offsets(file))
			return retval;
		if (count >= -pos) /* both values are in 0..LLONG_MAX */
			return -EOVERFLOW;
	} else if (unlikely((loff_t) (pos + count) < 0)) {
		if (!unsigned_offsets(file))
			return retval;
	}

	if (unlikely(inode->i_flock && mandatory_lock(inode))) {
		retval = locks_mandatory_area(
			read_write == READ ? FLOCK_VERIFY_READ : FLOCK_VERIFY_WRITE,
			inode, file, pos, count);
		if (retval < 0)
			return retval;
	}
	retval = security_file_permission(file,
				read_write == READ ? MAY_READ : MAY_WRITE);
	if (retval)
		return retval;
	return count > MAX_RW_COUNT ? MAX_RW_COUNT : count;
}

ssize_t do_sync_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
	struct iovec iov = { .iov_base = buf, .iov_len = len };
	struct kiocb kiocb;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	kiocb.ki_pos = *ppos;
	kiocb.ki_nbytes = len;

	ret = filp->f_op->aio_read(&kiocb, &iov, 1, kiocb.ki_pos);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(&kiocb);
	*ppos = kiocb.ki_pos;
	return ret;
}

EXPORT_SYMBOL(do_sync_read);

ssize_t new_sync_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
	struct iovec iov = { .iov_base = buf, .iov_len = len };
	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	kiocb.ki_pos = *ppos;
	kiocb.ki_nbytes = len;
	iov_iter_init(&iter, READ, &iov, 1, len);

	ret = filp->f_op->read_iter(&kiocb, &iter);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(&kiocb);
	*ppos = kiocb.ki_pos;
	return ret;
}

EXPORT_SYMBOL(new_sync_read);

ssize_t vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	ssize_t ret;

	if (!(file->f_mode & FMODE_READ))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_READ))
		return -EINVAL;
	if (unlikely(!access_ok(VERIFY_WRITE, buf, count)))
		return -EFAULT;

	ret = rw_verify_area(READ, file, pos, count);
	if (ret >= 0) {
		count = ret;
		if (file->f_op->read)
			ret = file->f_op->read(file, buf, count, pos);
		else if (file->f_op->aio_read)
			ret = do_sync_read(file, buf, count, pos);
		else
			ret = new_sync_read(file, buf, count, pos);
		if (ret > 0) {
			fsnotify_access(file);
			add_rchar(current, ret);
		}
		inc_syscr(current);
	}

	return ret;
}

EXPORT_SYMBOL(vfs_read);

ssize_t do_sync_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{
	struct iovec iov = { .iov_base = (void __user *)buf, .iov_len = len };
	struct kiocb kiocb;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	kiocb.ki_pos = *ppos;
	kiocb.ki_nbytes = len;

	ret = filp->f_op->aio_write(&kiocb, &iov, 1, kiocb.ki_pos);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(&kiocb);
	*ppos = kiocb.ki_pos;
	return ret;
}

EXPORT_SYMBOL(do_sync_write);

ssize_t new_sync_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{
	struct iovec iov = { .iov_base = (void __user *)buf, .iov_len = len };
	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	kiocb.ki_pos = *ppos;
	kiocb.ki_nbytes = len;
	iov_iter_init(&iter, WRITE, &iov, 1, len);

	ret = filp->f_op->write_iter(&kiocb, &iter);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(&kiocb);
	*ppos = kiocb.ki_pos;
	return ret;
}

EXPORT_SYMBOL(new_sync_write);

ssize_t __kernel_write(struct file *file, const char *buf, size_t count, loff_t *pos)
{
	mm_segment_t old_fs;
	const char __user *p;
	ssize_t ret;

	if (!(file->f_mode & FMODE_CAN_WRITE))
		return -EINVAL;

	old_fs = get_fs();
	set_fs(get_ds());
	p = (__force const char __user *)buf;
	if (count > MAX_RW_COUNT)
		count =  MAX_RW_COUNT;
	if (file->f_op->write)
		ret = file->f_op->write(file, p, count, pos);
	else if (file->f_op->aio_write)
		ret = do_sync_write(file, p, count, pos);
	else
		ret = new_sync_write(file, p, count, pos);
	set_fs(old_fs);
	if (ret > 0) {
		fsnotify_modify(file);
		add_wchar(current, ret);
	}
	inc_syscw(current);
	return ret;
}

EXPORT_SYMBOL(__kernel_write);

ssize_t vfs_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	ssize_t ret;

	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_WRITE))
		return -EINVAL;
	if (unlikely(!access_ok(VERIFY_READ, buf, count)))
		return -EFAULT;

	ret = rw_verify_area(WRITE, file, pos, count);
	if (ret >= 0) {
		count = ret;
		file_start_write(file);
		if (file->f_op->write)
			ret = file->f_op->write(file, buf, count, pos);
		else if (file->f_op->aio_write)
			ret = do_sync_write(file, buf, count, pos);
		else
			ret = new_sync_write(file, buf, count, pos);
		if (ret > 0) {
			fsnotify_modify(file);
			add_wchar(current, ret);
		}
		inc_syscw(current);
		file_end_write(file);
	}

	return ret;
}

EXPORT_SYMBOL(vfs_write);

static inline loff_t file_pos_read(struct file *file)
{
	return file->f_pos;
}

static inline void file_pos_write(struct file *file, loff_t pos)
{
	file->f_pos = pos;
}

SYSCALL_DEFINE3(read, unsigned int, fd, char __user *, buf, size_t, count)
{
	struct fd f = fdget_pos(fd);
	ssize_t ret = -EBADF;

	if (f.file) {
		loff_t pos = file_pos_read(f.file);
		ret = vfs_read(f.file, buf, count, &pos);
		if (ret >= 0)
			file_pos_write(f.file, pos);
		fdput_pos(f);
	}
	return ret;
}

SYSCALL_DEFINE3(write, unsigned int, fd, const char __user *, buf,
		size_t, count)
{
	struct fd f = fdget_pos(fd);
	ssize_t ret = -EBADF;

	if (f.file) {
		loff_t pos = file_pos_read(f.file);
		ret = vfs_write(f.file, buf, count, &pos);
		if (ret >= 0)
			file_pos_write(f.file, pos);
		fdput_pos(f);
	}

	return ret;
}

SYSCALL_DEFINE4(pread64, unsigned int, fd, char __user *, buf,
			size_t, count, loff_t, pos)
{
	struct fd f;
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	f = fdget(fd);
	if (f.file) {
		ret = -ESPIPE;
		if (f.file->f_mode & FMODE_PREAD)
			ret = vfs_read(f.file, buf, count, &pos);
		fdput(f);
	}

	return ret;
}

SYSCALL_DEFINE4(pwrite64, unsigned int, fd, const char __user *, buf,
			 size_t, count, loff_t, pos)
{
	struct fd f;
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	f = fdget(fd);
	if (f.file) {
		ret = -ESPIPE;
		if (f.file->f_mode & FMODE_PWRITE)  
			ret = vfs_write(f.file, buf, count, &pos);
		fdput(f);
	}

	return ret;
}

/*
 * Reduce an iovec's length in-place.  Return the resulting number of segments
 */
unsigned long iov_shorten(struct iovec *iov, unsigned long nr_segs, size_t to)
{
	unsigned long seg = 0;
	size_t len = 0;

	while (seg < nr_segs) {
		seg++;
		if (len + iov->iov_len >= to) {
			iov->iov_len = to - len;
			break;
		}
		len += iov->iov_len;
		iov++;
	}
	return seg;
}
EXPORT_SYMBOL(iov_shorten);

static ssize_t do_iter_readv_writev(struct file *filp, int rw, const struct iovec *iov,
		unsigned long nr_segs, size_t len, loff_t *ppos, iter_fn_t fn)
{
	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	kiocb.ki_pos = *ppos;
	kiocb.ki_nbytes = len;

	iov_iter_init(&iter, rw, iov, nr_segs, len);
	ret = fn(&kiocb, &iter);
	if (ret == -EIOCBQUEUED)
		ret = wait_on_sync_kiocb(&kiocb);
	*ppos = kiocb.ki_pos;
	return ret;
}

static ssize_t do_sync_readv_writev(struct file *filp, const struct iovec *iov,
		unsigned long nr_segs, size_t len, loff_t *ppos, iov_fn_t fn)
{
	struct kiocb kiocb;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	kiocb.ki_pos = *ppos;
	kiocb.ki_nbytes = len;

	ret = fn(&kiocb, iov, nr_segs, kiocb.ki_pos);
	if (ret == -EIOCBQUEUED)
		ret = wait_on_sync_kiocb(&kiocb);
	*ppos = kiocb.ki_pos;
	return ret;
}

/* Do it by hand, with file-ops */
static ssize_t do_loop_readv_writev(struct file *filp, struct iovec *iov,
		unsigned long nr_segs, loff_t *ppos, io_fn_t fn)
{
	struct iovec *vector = iov;
	ssize_t ret = 0;

	while (nr_segs > 0) {
		void __user *base;
		size_t len;
		ssize_t nr;

		base = vector->iov_base;
		len = vector->iov_len;
		vector++;
		nr_segs--;

		nr = fn(filp, base, len, ppos);

		if (nr < 0) {
			if (!ret)
				ret = nr;
			break;
		}
		ret += nr;
		if (nr != len)
			break;
	}

	return ret;
}

/* A write operation does a read from user space and vice versa */
#define vrfy_dir(type) ((type) == READ ? VERIFY_WRITE : VERIFY_READ)

ssize_t rw_copy_check_uvector(int type, const struct iovec __user * uvector,
			      unsigned long nr_segs, unsigned long fast_segs,
			      struct iovec *fast_pointer,
			      struct iovec **ret_pointer)
{
	unsigned long seg;
	ssize_t ret;
	struct iovec *iov = fast_pointer;

	/*
	 * SuS says "The readv() function *may* fail if the iovcnt argument
	 * was less than or equal to 0, or greater than {IOV_MAX}.  Linux has
	 * traditionally returned zero for zero segments, so...
	 */
	if (nr_segs == 0) {
		ret = 0;
		goto out;
	}

	/*
	 * First get the "struct iovec" from user memory and
	 * verify all the pointers
	 */
	if (nr_segs > UIO_MAXIOV) {
		ret = -EINVAL;
		goto out;
	}
	if (nr_segs > fast_segs) {
		iov = kmalloc(nr_segs*sizeof(struct iovec), GFP_KERNEL);
		if (iov == NULL) {
			ret = -ENOMEM;
			goto out;
		}
	}
	if (copy_from_user(iov, uvector, nr_segs*sizeof(*uvector))) {
		ret = -EFAULT;
		goto out;
	}

	/*
	 * According to the Single Unix Specification we should return EINVAL
	 * if an element length is < 0 when cast to ssize_t or if the
	 * total length would overflow the ssize_t return value of the
	 * system call.
	 *
	 * Linux caps all read/write calls to MAX_RW_COUNT, and avoids the
	 * overflow case.
	 */
	ret = 0;
	for (seg = 0; seg < nr_segs; seg++) {
		void __user *buf = iov[seg].iov_base;
		ssize_t len = (ssize_t)iov[seg].iov_len;

		/* see if we we're about to use an invalid len or if
		 * it's about to overflow ssize_t */
		if (len < 0) {
			ret = -EINVAL;
			goto out;
		}
		if (type >= 0
		    && unlikely(!access_ok(vrfy_dir(type), buf, len))) {
			ret = -EFAULT;
			goto out;
		}
		if (len > MAX_RW_COUNT - ret) {
			len = MAX_RW_COUNT - ret;
			iov[seg].iov_len = len;
		}
		ret += len;
	}
out:
	*ret_pointer = iov;
	return ret;
}

static ssize_t do_readv_writev(int type, struct file *file,
			       const struct iovec __user * uvector,
			       unsigned long nr_segs, loff_t *pos)
{
	size_t tot_len;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov = iovstack;
	ssize_t ret;
	io_fn_t fn;
	iov_fn_t fnv;
	iter_fn_t iter_fn;

	ret = rw_copy_check_uvector(type, uvector, nr_segs,
				    ARRAY_SIZE(iovstack), iovstack, &iov);
	if (ret <= 0)
		goto out;

	tot_len = ret;
	ret = rw_verify_area(type, file, pos, tot_len);
	if (ret < 0)
		goto out;

	fnv = NULL;
	if (type == READ) {
		fn = file->f_op->read;
		fnv = file->f_op->aio_read;
		iter_fn = file->f_op->read_iter;
	} else {
		fn = (io_fn_t)file->f_op->write;
		fnv = file->f_op->aio_write;
		iter_fn = file->f_op->write_iter;
		file_start_write(file);
	}

	if (iter_fn)
		ret = do_iter_readv_writev(file, type, iov, nr_segs, tot_len,
						pos, iter_fn);
	else if (fnv)
		ret = do_sync_readv_writev(file, iov, nr_segs, tot_len,
						pos, fnv);
	else
		ret = do_loop_readv_writev(file, iov, nr_segs, pos, fn);

	if (type != READ)
		file_end_write(file);

out:
	if (iov != iovstack)
		kfree(iov);
	if ((ret + (type == READ)) > 0) {
		if (type == READ)
			fsnotify_access(file);
		else
			fsnotify_modify(file);
	}
	return ret;
}

ssize_t vfs_readv(struct file *file, const struct iovec __user *vec,
		  unsigned long vlen, loff_t *pos)
{
	if (!(file->f_mode & FMODE_READ))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_READ))
		return -EINVAL;

	return do_readv_writev(FY_READ, bufFMODE_CAN_READ))Asvec io_dify(file|;
		if (len + iov->iov_len >= to) {
			ioturn do_readv_writ= len;ed loO_len *pos)
{
	if (!(file->f_mode & FMODE_READuct kiocb kiQUEUED == the end ofuct file *de)DEAD)
		file_end_write(file:f&c io_dify(file|;
		if (len + iov->iov_len >= to) {
			#v->i (nr_segs > UIO_MAXIOV) {
		ret = -EINVAL;
		goto out;
	}
	if (nr_segs > fast_segs) {
		iov = kmalloc(nr_segs*sizeof(struct iovec), GFP_KERNEL);
		if (iov == NULL) {
			ret = -ENOMEM;
			goto out;
		}
	}
	if (copy_from_user(iov, uvector, nr_segs*sizeof(*uvecRRAY_SIZE(iovstack), iovstack, &iov);
	i if tovstack, &iovecRRAlong nr_segs, size_t to)
turn ret;
}

SYSCa+ iov->iov_len >= to) /r_fn = file->fwCq)Asvt >= ov_len >= to) .hturn file->f_pos;
}
EXPORT_SYMBOL(noop_llseekepet = filp->)lly rhilo(y(file);
	}
	r res;fy(file);
	}
	rze_L) {*vector HALF_e to BITS (BITS_PER_e to / 2)
}
EXPORT(rea(int rfset, whHALF_e to BITS), whHALF_e to BITS),|rze_	if (nr_segs > fast5			fil{
		iov = kmalloc(nr_segs*sizeof(struct iovec), GFP_KERNEL);
		if (iov == NUL
		iov = kmalloc(
{
	l
		iov = kmalloc(
{
	h) {
				retv(file, bu)lly rhilo(
{
	hc(
{
	l);signed int, fd, char __user *, buf,
			size_t, count, loff_t, pos)
{
	struct fd f;
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	f = fdget pos = file_pos_read(f.&iov);
	i if tovstack, &iovecRe & FMODE_PREADto) {
			#v->i (nr_segs > UIO_MAXIOV) {
		ret = -EINVAL;
		goto out;
	}
	if (nr_segs > fast5_pos_wr{
		iov = kmalloc(nr_segs*sizeof(struct iovec), GFP_KERNEL);
		if (iov == NUL
		iov = kmalloc(
{
	l
		iov = kmalloc(
{
	h) {
				retv(file, bu)lly rhilo(
{
	hc(
{
	l);signed int, fd, char __user *, buf,
			size_t, count, loff_t, pos)
{
	struct fd f;
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	f = fdget(fd);
f (f.file) {
		ret &iov);
	i if tovstack, &iovecRe & FMODE_PREADto) {
			#v->i (nr_Asvt >= ov_len >= to) .hturn file->f_pos;
}
EXPORT_SYMBOL(L_DEFINE3(lseek, unsi see if we we'ree)
{
	o about to use an invalid len or if
		 * it's about to overflow ssizee)
{
	oruct iovec), G < 0) {
			ret = -EINVAL;
			goto out;
		}
		if (type >= 0e)
{
	os
		    && unlikely(!access_ok(vrfy_dir(type), buf, len))) {
			ret = -EFAULT;
			goto out;
		}
		if (len > MAX_RW_COUNT - ret) {
			len = MAX_RW_COUNT - re)
{
	oet;
			iov[seg].iov_len = len;
		}
		ret += len;
	ret = -Etype), buf,ov;
	return ret;
}

static ssize_t do_readv_writev(int type, struct file *file,
			       const struct iovec __user * uvector,
			       unsigned long nr_segs, loff_t *pos)
{
	size_t tot_len;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov = iovstack;
	ssize_t ret;
	io_fn_t fn;
	iov_fn_t fnv;
	iter_fn_t iter_fn;

	ret = rw_copy_check_uvector(type, uvector, nr_segs,
				    ARRAY_SIZE(iovstack), iovstack, &iov);
	if (ret <= 0)
		goto out;

	tot_len = ret;
	ret = rw_verify_area(type, file, pos, tot_len);
	if (ret < 0)
		goto out;

	fnv = NULL;
	if (type == READ) {
		fn = file->f_op->read;
		fnv = file->f_op->aio_read;
		iter_fn = file->f_op->read_iter;
	} else {
		fn = (io_fn_t)file->f_op->write;
		fnv = file->f_op->aio_write;
		iter_fn = file->f_op->write_iter;
		file_start_write(file);
	}

	if (iter_fn)
		ret = do_iter_readv_writev(file, type, iov, nr_segs, tee if w we'ree)
{
	o			pos, iter_fn);
	else 			ret verflow ssizee)
{
	oruct iovec), G_KERNELodify(file);
	}
	return ret;
}

ssize_t char __user *, buf,
			size_ fnv);
	else
		ret = do_loop_rea		       unsld overflow the iov, nr_segs, pos, fn);

	if (type != RE		       unsld ovee)
{
	o about to use anv != iovstack)
		kfree(iov);
->write;
		{
			#v->i (nr_segs > UIO_MAXIOV) {
		ret = -EINVAL;
		goto out;
	}
	if (gned int, fd, off_t, of	fil{
	e)
{
	ou	}
	_);
		f
		verflow ssizee)
{
	oruct iovec), G,_KERNELe)
{
	ou	}
	_);
 NULL) {
			ret = -ENOMEM;
			goto out;
		}
	}
	if;EFAULT;
		if (
#ifdef CONFIG_COMPAT
COMPAT_SYSl;
}
#ent whence)
{
;OUNT - re)
{
	oe(f.&iov);
	i if tovstack, &iovecRAlong nr_segs, nt whence)
{
le, buf,igned int, whence)
{
	int egs, tee if 	}
	r__e)
{
	osys_		fil{64(y(file);
	}
	r		f
		Lodiverflow ssizee)
{
	oruct iovec), G_KERNELotev(file, iov, nr_segs, tot_le64, unsigned int, fd, char __user			size_t, count, loff_t, pos)
{
	sruct fd f;
	ssize_t rf CONFIG_COMPAT
COMPAT_SYSl

	if (pos < 0)
	return -EINVAL;

	f = fdget pos = fNT - re)
{
	oe(f.&iov);
	i if tovstack, &iovece & FMODE_P
EXPORT_SYMBOL(L_DEFINif (whence <gned int, et posV64(gned int, fd, off_t, 
			filvite(f.file, palloc(nr_
		verflow ssizee)
{
	oruct iovec), G,_KERNEL);
		if (iov == NUL
	E4(pread64, unsirunlock(&e)
{
	osys_		fil{64(nr_sDF;
	if (!(file->f_
	structgned int, fd, off_t, 5			fil{
	e)
{
	ou	}
	_);
		f
		verflow ssizee)
{
	oruct iovec), G,_KERNELe)
{
	ou	}
	_);
 NULe(f32c(
{
	lal)
f32c(
{
	fset) {
				retv(file,rea(int r
{
	fset, whence);
{
	lalile->f_mode(&e)
{
	osys_		fil{64(nr_sDF;
	if (!(file->f_ tee if w we'ree)
{
	oype == READ)
			fsnotify_			ret =verflow ssizee)
{
	oruct iovec), G_KERNELodiffy(file);
	}
	return ret;
}

ssize_t char __user *, buf,
			size_ fnv);
	else
		ret = do loff_t *p		       unsld overflow the iov, nr_segs, pos, fn);

	if (t loff_t *p		       unsld ovee)
{
	o about to use an		return -EBADF;
	if (!(file->->write;
		{
			#v->i (nr_Asvt >= ov_len >= to) .hturn file->f_pos;
}
EXPORT_SYMBOL(gned int, fd, off_t, ofos_wr{
	e)
{
	ou	}
	_);
		f
		verflow ssizee)
{
	oruct iovec), G, _KERNELe)
{
	ou	}
	_);
 NULL) {
			ret = -ENOMEM;
			goto out;
		}
	}
	if;EFAULT;
		if (
#ifdef CONFIG_COMPAT
COMPAT_SYSl;
}
#ent whence)
{
;OUNT - re)
{
	o	ret &iov);
	i if tovstack, &iovecRAlong nr_segs, nt whence)
{
le, buf,igned int, whence)
{
	int egs, tee if 	}
	r__e)
{
	osys_		ret &64(y(file);
	}
	r		f
		Lodiiverflow ssizee)
{
	oruct iovec), G_KERNELotevv(file, iov, nr_segs, tot_le64, unsigned int, fd, char __user			size_t, count, loff_t, pos)
{
	sruct fd f;
	ssize_t rf CONFIG_COMPAT
COMPAT_SYSl

	if (pos < 0)
	return -EINVAL;

	f = fdget(fd);
f (NT - re)
{
	o	ret &iov);
	i if tovstack, &iovece & FMODE_P
EXPORT_SYMBOL(L_DEFINif (whence <gned int, et(fd);V64(gned int, fd, off_t, 
			ret &64e(f.file, palloc(nr_
		verflow ssizee)
{
	oruct iovec), G,_KERNEL);
		if (iov == NUL
	E4(pread64, unsirunlock(&e)
{
	osys_		ret &64(nr_sDF;
	if (!(file->f_
	structgned int, fd, off_t, 5		os_wr{
	e)
{
	ou	}
	_);
		f
		verflow ssizee)
{
	oruct iovec), G,_KERNELe)
{
	ou	}
	_);
 NULe(f32c(
{
	lal)
f32c(
{
	fset) {
				retv(file,rea(int r
{
	fset, whence);
{
	lalile->f_mode(&e)
{
	osys_		ret &64(nr_sDF;
	if (!(file->f_
	structconst struct iovec 	st);
	n invult,nr_s invin,nr_sfn)
{
	struct_moditevvp->write)
		ret = filmax unsigned int, i(!(	 * ov>= 0) {
		retval_os < 0)*ult,al = -EFAULT;
		if (!turn retd int,d, char __userount) out:
l operation Gto on& Frn -EBA)
{
	**ret_nce >stati ok.	if (copy_f_user(rec __usernct fd f;
in,nrsize_t rfinCONFIG_CO the
	 * oviov, ninCONFI
	else
		ret = do_loop_rea		   fplt,al;opy_f_user(reos < 0)
	ret!	retu;
		f(file,inCONFI
	elnt,d, ze_t ret;
	 len;

	iov_itviov, ninCONFI
	else
		ret = doP_loop_reaa		   fplt,al;opf (count >= UED == ret)
		ret = winCONFIck, &i ret);
	}
	inc_sysfile))
			r		   fplt,al;op;
	return rback toration Gto 	 *& Frn -EBA)
{
	**ret_nce >stati ok.	if (copy_f_user(rec __use cast fd f;
ult,nrsize_t rfultCONFIG_CO the
fplt,al;opiov, nultCONFI
	else
		ret = do loff_t *p		   fplt,	 * ovrL_DEFINE3(lseek, unl_os < sizeof(*offsetinCONFIGuse ca_os < sizeof(*offsetultCONFIGuse ca_ len;
ultCONFI
	el
{
;OUNT nt >= UED == ret)
		r		returultCONFInsigca_ le ret);
	}
	inc_sysfile))
			r		   fplt,	 * ov;
	return rback toiov, max u	 0 &&= min(nl_os < o seek to
 * @whenc  ca_os < o seek to
 * @whenread_iterrw_verify_are+{
		if (!max  unsigned int, fd the time.
size_t, cr_smax u	 r		   fplt,	 * ovv;
	retur0 &&-		if (!k), fser *, L_D * sration We n_lls_putebiov_whe @whewif (n en, sizeINVAor				.;
		tion m(n  do_ovecdir(ts EAGAIN_segs) {
	case S	 *& FraAccoas;	/*ile-
{
se Saphar " frome SErgu, sy buggy_lenit);
	loff_D:
		r/*ileEAGAIN_ole->non-bfile	 * 		fsndescriptor,
			      uinCONFI
	elllseek(sO_NONBfile, lofser SPLICE_F_NONBfile;_
	struc				    ARRAY_SIZEultCONFIGusened int, vec har __generi(inCONFIck, &i rultCONFInsigca_ le ret);
;
	l);si = (io_fn_t)filultCONFIGus
	inc_sysfileiter;
		f(nr_segs > UIO_MAXIOV)(lof;>i (nr_Asvt >= ov_len >= (lof;>i ;
	}

	if (iter_inCONFIGuseter_readv_writev(ultCONFIGuse	ultCONFI
	el
{
n;
ult_iov_itviov,= len 	 r= kiocb.iov_itv = do_itinCONFI
	elnt,cb.iov_itk), iret = -EINVAL;
		got.hturn file->f_pos;
}ize_t, crsmax u	 ned int, fd the time.
fplt,	 *nsigned (ults;
fplt,alnsigned (i>rea	 *nsi
EXPORT_SY

SYSCALt, fd, off_t, 
	 	st);
	,pos);
ult,nr_s in,vin,nr_s		retval = -EOVEgned long, ole->f_pos = poAULT;
		if (!urn retvad, char __user			size_et_low,;
		file_rw_verify;
		eturnet_VEgned l)ize_t count)
, and avoidpgoto out;f (ret < 0)
		st);
	nult,nr_s i,nr_s& le ret);
;
 < nNON_LFSovecRRAlorw_verify_u		eturn &i runed l)ize_t count)
, and avoid
EXPORT_SYMB struct iove0)
		st);
	nult,nr_s i,nr_slong ret);
;
0)YSCALt, fd, off_t, 
	 	st);
	64e(os);
ult,nr_s in,vin,nr_s			retval = -EOVEgned long, ole->f_pos = poAULT;
		if (!char __user			size_et_low,;
		file_rw_verifytionally return& &i runed long, offsa(int r)ize_t count)
, and avoid
EX < 0)
		st);
	nult,nr_s i,nr_s& le ret);
;
0ovecRRAlorw_verify_u		eturn &i runed l)ize_t count)
, and avoid
EXPORT_SYMB struct iove0)
		st);
	nult,nr_s i,nr_slong ret);
;
0)YSCALL_DEFINE3(lseek, unsigned int, fd, off_t, 
	 	st);
	,pos);
ult,nr_s in,vin,nr_NELe)
{
	o		retval = -EOVEgned lone)
{
	os, ole->f_pos = poAULT;
		if (!urn retvad, char __user			size_et_low,;
		file_rw_verify;
		eturnet_VEgned l)ize_t count)
, and avoidpgoto out;f (ret < 0)
		st);
	nult,nr_s i,nr_s& le ret);
;
 < nNON_LFSovecRRAlorw_verify_u		eturn &i runed l)ize_t count)
, and avoid
EXPORT_SYMB struct iove0)
		st);
	nult,nr_s i,nr_slong ret);
;
0)YSCALgned int, fd, off_t, 
	 	st);
	64e(os);
ult,nr_s in,vin,nr_NELe)
{
	o			retval = -EOVEgned lone)
{
	os, ole->f_pos = poAULT;
		if (!char __user			size_et_low,;
		file_rw_verifytionally return& &i runed long, offsa(int r)ize_t count)
, and avoid
EX < 0)
		st);
	nult,nr_s i,nr_s& le ret);
;
0ovecRRAlorw_verify_u		eturn &i runed l)ize_t count)
, and avoid
EXPORT_SYMB struct iove0)
		st);
	nult,nr_s i,nr_slong ret);
;
0)YSCA
	struc