/*
 * NET		An implementation of the SOCKET network access protocol.
 *
 * Version:	@(#)socket.c	1.1.93	18/02/95
 *
 * Authors:	Orest Zborowski, <obz@Kodak.COM>
 *		Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 * Fixes:
 *		Anonymous	:	NOTSOCK/BADF cleanup. Error fix in
 *					shutdown()
 *		Alan Cox	:	verify_area() fixes
 *		Alan Cox	:	Removed DDI
 *		Jonathan Kamens	:	SOCK_DGRAM reconnect bug
 *		Alan Cox	:	Moved a load of checks to the very
 *					top level.
 *		Alan Cox	:	Move address structures to/from user
 *					mode above the protocol layers.
 *		Rob Janssen	:	Allow 0 length sends.
 *		Alan Cox	:	Asynchronous I/O support (cribbed from the
 *					tty drivers).
 *		Niibe Yutaka	:	Asynchronous I/O for writes (4.4BSD style)
 *		Jeff Uphoff	:	Made max number of sockets command-line
 *					configurable.
 *		Matti Aarnio	:	Made the number of sockets dynamic,
 *					to be allocated when needed, and mr.
 *					Uphoff's max is used as max to be
 *					allowed to allocate.
 *		Linus		:	Argh. removed all the socket allocation
 *					altogether: it's in the inode now.
 *		Alan Cox	:	Made sock_alloc()/sock_release() public
 *					for NetROM and future kernel nfsd type
 *					stuff.
 *		Alan Cox	:	sendmsg/recvmsg basics.
 *		Tom Dyas	:	Export net symbols.
 *		Marcin Dalecki	:	Fixed problems with CONFIG_NET="n".
 *		Alan Cox	:	Added thread locking to sys_* calls
 *					for sockets. May have errors at the
 *					moment.
 *		Kevin Buhr	:	Fixed the dumb errors in the above.
 *		Andi Kleen	:	Some small cleanups, optimizations,
 *					and fixed a copy_from_user() bug.
 *		Tigran Aivazian	:	sys_send(args) calls sys_sendto(args, NULL, 0)
 *		Tigran Aivazian	:	Made listen(2) backlog sanity checks
 *					protocol-independent
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *
 *	This module is effectively the top level interface to the BSD socket
 *	paradigm.
 *
 *	Based upon Swansea University Computer Society NET3.039
 */

#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/file.h>
#include <linux/net.h>
#include <linux/interrupt.h>
#include <linux/thread_info.h>
#include <linux/rcupdate.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/if_bridge.h>
#include <linux/if_frad.h>
#include <linux/if_vlan.h>
#include <linux/ptp_classify.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/cache.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/mount.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/compat.h>
#include <linux/kmod.h>
#include <linux/audit.h>
#include <linux/wireless.h>
#include <linux/nsproxy.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/xattr.h>
#include <linux/seemp_api.h>
#include <linux/seemp_instrumentation.h>

#include <asm/uaccess.h>
#include <asm/unistd.h>

#include <net/compat.h>
#include <net/wext.h>
#include <net/cls_cgroup.h>

#include <net/sock.h>
#include <linux/netfilter.h>

#include <linux/if_tun.h>
#include <linux/ipv6_route.h>
#include <linux/route.h>
#include <linux/sockios.h>
#include <linux/atalk.h>
#include <net/busy_poll.h>
#include <linux/errqueue.h>

#ifdef CONFIG_NET_RX_BUSY_POLL
unsigned int sysctl_net_busy_read __read_mostly;
unsigned int sysctl_net_busy_poll __read_mostly;
#endif

static BLOCKING_NOTIFIER_HEAD(sockev_notifier_list);

static int sock_no_open(struct inode *irrelevant, struct file *dontcare);
static ssize_t sock_aio_read(struct kiocb *iocb, const struct iovec *iov,
			 unsigned long nr_segs, loff_t pos);
static ssize_t sock_aio_write(struct kiocb *iocb, const struct iovec *iov,
			  unsigned long nr_segs, loff_t pos);
static int sock_mmap(struct file *file, struct vm_area_struct *vma);

static int sock_close(struct inode *inode, struct file *file);
static unsigned int sock_poll(struct file *file,
			      struct poll_table_struct *wait);
static long sock_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#ifdef CONFIG_COMPAT
static long compat_sock_ioctl(struct file *file,
			      unsigned int cmd, unsigned long arg);
#endif
static int sock_fasync(int fd, struct file *filp, int on);
static ssize_t sock_sendpage(struct file *file, struct page *page,
			     int offset, size_t size, loff_t *ppos, int more);
static ssize_t sock_splice_read(struct file *file, loff_t *ppos,
				struct pipe_inode_info *pipe, size_t len,
				unsigned int flags);

/*
 *	Socket files have a set of 'special' operations as well as the generic file ones. These don't appear
 *	in the operation structures but are done directly via the socketcall() multiplexor.
 */

static const struct file_operations socket_file_ops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.aio_read =	sock_aio_read,
	.aio_write =	sock_aio_write,
	.poll =		sock_poll,
	.unlocked_ioctl = sock_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_sock_ioctl,
#endif
	.mmap =		sock_mmap,
	.open =		sock_no_open,	/* special open code to disallow open via /proc */
	.release =	sock_close,
	.fasync =	sock_fasync,
	.sendpage =	sock_sendpage,
	.splice_write = generic_splice_sendpage,
	.splice_read =	sock_splice_read,
};

/*
 *	The protocol list. Each protocol is registered in here.
 */

static DEFINE_SPINLOCK(net_family_lock);
static const struct net_proto_family __rcu *net_families[NPROTO] __read_mostly;

/*
 *	Statistics counters of the socket lists
 */

static DEFINE_PER_CPU(int, sockets_in_use);

/*
 * Socket Event framework helpers
 */
static void sockev_notify(unsigned long event, struct socket *sk)
{
	blocking_notifier_call_chain(&sockev_notifier_list, event, sk);
}

/**
 * Support routines.
 * Move socket addresses back and forth across the kernel/user
 * divide and look after the messy bits.
 */

/**
 *	move_addr_to_kernel	-	copy a socket address into kernel space
 *	@uaddr: Address in user space
 *	@kaddr: Address in kernel space
 *	@ulen: Length in user space
 *
 *	The address is copied into kernel space. If the provided address is
 *	too long an error code of -EINVAL is returned. If the copy gives
 *	invalid addresses -EFAULT is returned. On a success 0 is returned.
 */

int move_addr_to_kernel(void __user *uaddr, int ulen, struct sockaddr_storage *kaddr)
{
	if (ulen < 0 || ulen > sizeof(struct sockaddr_storage))
		return -EINVAL;
	if (ulen == 0)
		return 0;
	if (copy_from_user(kaddr, uaddr, ulen))
		return -EFAULT;
	return audit_sockaddr(ulen, kaddr);
}

/**
 *	move_addr_to_user	-	copy an address to user space
 *	@kaddr: kernel space address
 *	@klen: length of address in kernel
 *	@uaddr: user space address
 *	@ulen: pointer to user length field
 *
 *	The value pointed to by ulen on entry is the buffer length available.
 *	This is overwritten with the buffer space used. -EINVAL is returned
 *	if an overlong buffer is specified or a negative buffer size. -EFAULT
 *	is returned if either the buffer or the length field are not
 *	accessible.
 *	After copying the data up to the limit the user specifies, the true
 *	length of the data is written over the length limit the user
 *	specified. Zero is returned for a success.
 */

static int move_addr_to_user(struct sockaddr_storage *kaddr, int klen,
			     void __user *uaddr, int __user *ulen)
{
	int err;
	int len;

	BUG_ON(klen > sizeof(struct sockaddr_storage));
	err = get_user(len, ulen);
	if (err)
		return err;
	if (len > klen)
		len = klen;
	if (len < 0)
		return -EINVAL;
	if (len) {
		if (audit_sockaddr(klen, kaddr))
			return -ENOMEM;
		if (copy_to_user(uaddr, kaddr, len))
			return -EFAULT;
	}
	/*
	 *      "fromlen shall refer to the value before truncation.."
	 *                      1003.1g
	 */
	return __put_user(klen, ulen);
}

static struct kmem_cache *sock_inode_cachep __read_mostly;

static struct inode *sock_alloc_inode(struct super_block *sb)
{
	struct socket_alloc *ei;
	struct socket_wq *wq;

	ei = kmem_cache_alloc(sock_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	wq = kmalloc(sizeof(*wq), GFP_KERNEL);
	if (!wq) {
		kmem_cache_free(sock_inode_cachep, ei);
		return NULL;
	}
	init_waitqueue_head(&wq->wait);
	wq->fasync_list = NULL;
	RCU_INIT_POINTER(ei->socket.wq, wq);

	ei->socket.state = SS_UNCONNECTED;
	ei->socket.flags = 0;
	ei->socket.ops = NULL;
	ei->socket.sk = NULL;
	ei->socket.file = NULL;

	return &ei->vfs_inode;
}

static void sock_destroy_inode(struct inode *inode)
{
	struct socket_alloc *ei;
	struct socket_wq *wq;

	ei = container_of(inode, struct socket_alloc, vfs_inode);
	wq = rcu_dereference_protected(ei->socket.wq, 1);
	kfree_rcu(wq, rcu);
	kmem_cache_free(sock_inode_cachep, ei);
}

static void init_once(void *foo)
{
	struct socket_alloc *ei = (struct socket_alloc *)foo;

	inode_init_once(&ei->vfs_inode);
}

static int init_inodecache(void)
{
	sock_inode_cachep = kmem_cache_create("sock_inode_cache",
					      sizeof(struct socket_alloc),
					      0,
					      (SLAB_HWCACHE_ALIGN |
					       SLAB_RECLAIM_ACCOUNT |
					       SLAB_MEM_SPREAD),
					      init_once);
	if (sock_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static const struct super_operations sockfs_ops = {
	.alloc_inode	= sock_alloc_inode,
	.destroy_inode	= sock_destroy_inode,
	.statfs		= simple_statfs,
};

/*
 * sockfs_dname() is called from d_path().
 */
static char *sockfs_dname(struct dentry *dentry, char *buffer, int buflen)
{
	return dynamic_dname(dentry, buffer, buflen, "socket:[%lu]",
				dentry->d_inode->i_ino);
}

static const struct dentry_operations sockfs_dentry_operations = {
	.d_dname  = sockfs_dname,
};

static struct dentry *sockfs_mount(struct file_system_type *fs_type,
			 int flags, const char *dev_name, void *data)
{
	return mount_pseudo(fs_type, "socket:", &sockfs_ops,
		&sockfs_dentry_operations, SOCKFS_MAGIC);
}

static struct vfsmount *sock_mnt __read_mostly;

static struct file_system_type sock_fs_type = {
	.name =		"sockfs",
	.mount =	sockfs_mount,
	.kill_sb =	kill_anon_super,
};

/*
 *	Obtains the first available file descriptor and sets it up for use.
 *
 *	These functions create file structures and maps them to fd space
 *	of the current process. On success it returns file descriptor
 *	and file struct implicitly stored in sock->file.
 *	Note that another thread may close file descriptor before we return
 *	from this function. We use the fact that now we do not refer
 *	to socket after mapping. If one day we will need it, this
 *	function will increment ref. count on file by 1.
 *
 *	In any case returned fd MAY BE not valid!
 *	This race condition is unavoidable
 *	with shared fd spaces, we cannot solve it inside kernel,
 *	but we take care of internal coherence yet.
 */

struct file *sock_alloc_file(struct socket *sock, int flags, const char *dname)
{
	struct qstr name = { .name = "" };
	struct path path;
	struct file *file;

	if (dname) {
		name.name = dname;
		name.len = strlen(name.name);
	} else if (sock->sk) {
		name.name = sock->sk->sk_prot_creator->name;
		name.len = strlen(name.name);
	}
	path.dentry = d_alloc_pseudo(sock_mnt->mnt_sb, &name);
	if (unlikely(!path.dentry))
		return ERR_PTR(-ENOMEM);
	path.mnt = mntget(sock_mnt);

	d_instantiate(path.dentry, SOCK_INODE(sock));
	SOCK_INODE(sock)->i_fop = &socket_file_ops;

	file = alloc_file(&path, FMODE_READ | FMODE_WRITE,
		  &socket_file_ops);
	if (unlikely(IS_ERR(file))) {
		/* drop dentry, keep inode */
		ihold(path.dentry->d_inode);
		path_put(&path);
		return file;
	}

	sock->file = file;
	file->f_flags = O_RDWR | (flags & O_NONBLOCK);
	file->private_data = sock;
	return file;
}
EXPORT_SYMBOL(sock_alloc_file);

static int sock_map_fd(struct socket *sock, int flags)
{
	struct file *newfile;
	int fd = get_unused_fd_flags(flags);
	if (unlikely(fd < 0))
		return fd;

	newfile = sock_alloc_file(sock, flags, NULL);
	if (likely(!IS_ERR(newfile))) {
		fd_install(fd, newfile);
		return fd;
	}

	put_unused_fd(fd);
	return PTR_ERR(newfile);
}

struct socket *sock_from_file(struct file *file, int *err)
{
	if (file->f_op == &socket_file_ops)
		return file->private_data;	/* set in sock_map_fd */

	*err = -ENOTSOCK;
	return NULL;
}
EXPORT_SYMBOL(sock_from_file);

/**
 *	sockfd_lookup - Go from a file number to its socket slot
 *	@fd: file handle
 *	@err: pointer to an error code return
 *
 *	The file handle passed in is locked and the socket it is bound
 *	too is returned. If an error occurs the err pointer is overwritten
 *	with a negative errno code and NULL is returned. The function checks
 *	for both invalid handles and passing a handle which is not a socket.
 *
 *	On a success the socket object pointer is returned.
 */

struct socket *sockfd_lookup(int fd, int *err)
{
	struct file *file;
	struct socket *sock;

	file = fget(fd);
	if (!file) {
		*err = -EBADF;
		return NULL;
	}

	sock = sock_from_file(file, err);
	if (!sock)
		fput(file);
	return sock;
}
EXPORT_SYMBOL(sockfd_lookup);

static struct socket *sockfd_lookup_light(int fd, int *err, int *fput_needed)
{
	struct fd f = fdget(fd);
	struct socket *sock;

	*err = -EBADF;
	if (f.file) {
		sock = sock_from_file(f.file, err);
		if (likely(sock)) {
			*fput_needed = f.flags;
			return sock;
		}
		fdput(f);
	}
	return NULL;
}

#define XATTR_SOCKPROTONAME_SUFFIX "sockprotoname"
#define XATTR_NAME_SOCKPROTONAME (XATTR_SYSTEM_PREFIX XATTR_SOCKPROTONAME_SUFFIX)
#define XATTR_NAME_SOCKPROTONAME_LEN (sizeof(XATTR_NAME_SOCKPROTONAME)-1)
static ssize_t sockfs_getxattr(struct dentry *dentry,
			       const char *name, void *value, size_t size)
{
	const char *proto_name;
	size_t proto_size;
	int error;

	error = -ENODATA;
	if (!strncmp(name, XATTR_NAME_SOCKPROTONAME, XATTR_NAME_SOCKPROTONAME_LEN)) {
		proto_name = dentry->d_name.name;
		proto_size = strlen(proto_name);

		if (value) {
			error = -ERANGE;
			if (proto_size + 1 > size)
				goto out;

			strncpy(value, proto_name, proto_size + 1);
		}
		error = proto_size + 1;
	}

out:
	return error;
}

static ssize_t sockfs_listxattr(struct dentry *dentry, char *buffer,
				size_t size)
{
	ssize_t len;
	ssize_t used = 0;

	len = security_inode_listsecurity(dentry->d_inode, buffer, size);
	if (len < 0)
		return len;
	used += len;
	if (buffer) {
		if (size < used)
			return -ERANGE;
		buffer += len;
	}

	len = (XATTR_NAME_SOCKPROTONAME_LEN + 1);
	used += len;
	if (buffer) {
		if (size < used)
			return -ERANGE;
		memcpy(buffer, XATTR_NAME_SOCKPROTONAME, len);
		buffer += len;
	}

	return used;
}

static int sockfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	int err = simple_setattr(dentry, iattr);

	if (!err && (iattr->ia_valid & ATTR_UID)) {
		struct socket *sock = SOCKET_I(dentry->d_inode);

		sock->sk->sk_uid = iattr->ia_uid;
	}

	return err;
}

static const struct inode_operations sockfs_inode_ops = {
	.getxattr = sockfs_getxattr,
	.listxattr = sockfs_listxattr,
	.setattr = sockfs_setattr,
};

/**
 *	sock_alloc	-	allocate a socket
 *
 *	Allocate a new inode and socket object. The two are bound together
 *	and initialised. The socket is then returned. If we are out of inodes
 *	NULL is returned.
 */

static struct socket *sock_alloc(void)
{
	struct inode *inode;
	struct socket *sock;

	inode = new_inode_pseudo(sock_mnt->mnt_sb);
	if (!inode)
		return NULL;

	sock = SOCKET_I(inode);

	kmemcheck_annotate_bitfield(sock, type);
	inode->i_ino = get_next_ino();
	inode->i_mode = S_IFSOCK | S_IRWXUGO;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_op = &sockfs_inode_ops;

	this_cpu_add(sockets_in_use, 1);
	return sock;
}

/*
 *	In theory you can't get an open on this inode, but /proc provides
 *	a back door. Remember to keep it shut otherwise you'll let the
 *	creepy crawlies in.
 */

static int sock_no_open(struct inode *irrelevant, struct file *dontcare)
{
	return -ENXIO;
}

const struct file_operations bad_sock_fops = {
	.owner = THIS_MODULE,
	.open = sock_no_open,
	.llseek = noop_llseek,
};

/**
 *	sock_release	-	close a socket
 *	@sock: socket to close
 *
 *	The socket is released from the protocol stack if it has a release
 *	callback, and the inode is then released if the socket is bound to
 *	an inode not a file.
 */

void sock_release(struct socket *sock)
{
	if (sock->ops) {
		struct module *owner = sock->ops->owner;

		sock->ops->release(sock);
		sock->ops = NULL;
		module_put(owner);
	}

	if (rcu_dereference_protected(sock->wq, 1)->fasync_list)
		pr_err("%s: fasync list not empty!\n", __func__);

	if (test_bit(SOCK_EXTERNALLY_ALLOCATED, &sock->flags))
		return;

	this_cpu_sub(sockets_in_use, 1);
	if (!sock->file) {
		iput(SOCK_INODE(sock));
		return;
	}
	sock->file = NULL;
}
EXPORT_SYMBOL(sock_release);

void __sock_tx_timestamp(const struct sock *sk, __u8 *tx_flags)
{
	u8 flags = *tx_flags;

	if (sk->sk_tsflags & SOF_TIMESTAMPING_TX_HARDWARE)
		flags |= SKBTX_HW_TSTAMP;

	if (sk->sk_tsflags & SOF_TIMESTAMPING_TX_SOFTWARE)
		flags |= SKBTX_SW_TSTAMP;

	if (sk->sk_tsflags & SOF_TIMESTAMPING_TX_SCHED)
		flags |= SKBTX_SCHED_TSTAMP;

	if (sk->sk_tsflags & SOF_TIMESTAMPING_TX_ACK)
		flags |= SKBTX_ACK_TSTAMP;

	*tx_flags = flags;
}
EXPORT_SYMBOL(__sock_tx_timestamp);

static inline int __sock_sendmsg_nosec(struct kiocb *iocb, struct socket *sock,
				       struct msghdr *msg, size_t size)
{
	struct sock_iocb *si = kiocb_to_siocb(iocb);

	si->sock = sock;
	si->scm = NULL;
	si->msg = msg;
	si->size = size;

	return sock->ops->sendmsg(iocb, sock, msg, size);
}

static inline int __sock_sendmsg(struct kiocb *iocb, struct socket *sock,
				 struct msghdr *msg, size_t size)
{
	int err = security_socket_sendmsg(sock, msg, size);

	return err ?: __sock_sendmsg_nosec(iocb, sock, msg, size);
}

int sock_sendmsg(struct socket *sock, struct msghdr *msg, size_t size)
{
	struct kiocb iocb;
	struct sock_iocb siocb;
	int ret;

	init_sync_kiocb(&iocb, NULL);
	iocb.private = &siocb;
	ret = __sock_sendmsg(&iocb, sock, msg, size);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(&iocb);
	return ret;
}
EXPORT_SYMBOL(sock_sendmsg);

static int sock_sendmsg_nosec(struct socket *sock, struct msghdr *msg, size_t size)
{
	struct kiocb iocb;
	struct sock_iocb siocb;
	int ret;

	init_sync_kiocb(&iocb, NULL);
	iocb.private = &siocb;
	ret = __sock_sendmsg_nosec(&iocb, sock, msg, size);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(&iocb);
	return ret;
}

int kernel_sendmsg(struct socket *sock, struct msghdr *msg,
		   struct kvec *vec, size_t num, size_t size)
{
	mm_segment_t oldfs = get_fs();
	int result;

	set_fs(KERNEL_DS);
	/*
	 * the following is safe, since for compiler definitions of kvec and
	 * iovec are identical, yielding the same in-core layout and alignment
	 */
	msg->msg_iov = (struct iovec *)vec;
	msg->msg_iovlen = num;
	result = sock_sendmsg(sock, msg, size);
	set_fs(oldfs);
	return result;
}
EXPORT_SYMBOL(kernel_sendmsg);

/*
 * called from sock_recv_timestamp() if sock_flag(sk, SOCK_RCVTSTAMP)
 */
void __sock_recv_timestamp(struct msghdr *msg, struct sock *sk,
	struct sk_buff *skb)
{
	int need_software_tstamp = sock_flag(sk, SOCK_RCVTSTAMP);
	struct scm_timestamping tss;
	int empty = 1;
	struct skb_shared_hwtstamps *shhwtstamps =
		skb_hwtstamps(skb);

	/* Race occurred between timestamp enabling and packet
	   receiving.  Fill in the current time for now. */
	if (need_software_tstamp && skb->tstamp.tv64 == 0)
		__net_timestamp(skb);

	if (need_software_tstamp) {
		if (!sock_flag(sk, SOCK_RCVTSTAMPNS)) {
			struct timeval tv;
			skb_get_timestamp(skb, &tv);
			put_cmsg(msg, SOL_SOCKET, SCM_TIMESTAMP,
				 sizeof(tv), &tv);
		} else {
			struct timespec ts;
			skb_get_timestampns(skb, &ts);
			put_cmsg(msg, SOL_SOCKET, SCM_TIMESTAMPNS,
				 sizeof(ts), &ts);
		}
	}

	memset(&tss, 0, sizeof(tss));
	if ((sk->sk_tsflags & SOF_TIMESTAMPING_SOFTWARE) &&
	    ktime_to_timespec_cond(skb->tstamp, tss.ts + 0))
		empty = 0;
	if (shhwtstamps &&
	    (sk->sk_tsflags & SOF_TIMESTAMPING_RAW_HARDWARE) &&
	    ktime_to_timespec_cond(shhwtstamps->hwtstamp, tss.ts + 2))
		empty = 0;
	if (!empty)
		put_cmsg(msg, SOL_SOCKET,
			 SCM_TIMESTAMPING, sizeof(tss), &tss);
}
EXPORT_SYMBOL_GPL(__sock_recv_timestamp);

void __sock_recv_wifi_status(struct msghdr *msg, struct sock *sk,
	struct sk_buff *skb)
{
	int ack;

	if (!sock_flag(sk, SOCK_WIFI_STATUS))
		return;
	if (!skb->wifi_acked_valid)
		return;

	ack = skb->wifi_acked;

	put_cmsg(msg, SOL_SOCKET, SCM_WIFI_STATUS, sizeof(ack), &ack);
}
EXPORT_SYMBOL_GPL(__sock_recv_wifi_status);

static inline void sock_recv_drops(struct msghdr *msg, struct sock *sk,
				   struct sk_buff *skb)
{
	if (sock_flag(sk, SOCK_RXQ_OVFL) && skb && skb->dropcount)
		put_cmsg(msg, SOL_SOCKET, SO_RXQ_OVFL,
			sizeof(__u32), &skb->dropcount);
}

void __sock_recv_ts_and_drops(struct msghdr *msg, struct sock *sk,
	struct sk_buff *skb)
{
	sock_recv_timestamp(msg, sk, skb);
	sock_recv_drops(msg, sk, skb);
}
EXPORT_SYMBOL_GPL(__sock_recv_ts_and_drops);

static inline int __sock_recvmsg_nosec(struct kiocb *iocb, struct socket *sock,
				       struct msghdr *msg, size_t size, int flags)
{
	struct sock_iocb *si = kiocb_to_siocb(iocb);

	si->sock = sock;
	si->scm = NULL;
	si->msg = msg;
	si->size = size;
	si->flags = flags;

	return sock->ops->recvmsg(iocb, sock, msg, size, flags);
}

static inline int __sock_recvmsg(struct kiocb *iocb, struct socket *sock,
				 struct msghdr *msg, size_t size, int flags)
{
	int err = security_socket_recvmsg(sock, msg, size, flags);

	return err ?: __sock_recvmsg_nosec(iocb, sock, msg, size, flags);
}

int sock_recvmsg(struct socket *sock, struct msghdr *msg,
		 size_t size, int flags)
{
	struct kiocb iocb;
	struct sock_iocb siocb;
	int ret;

	init_sync_kiocb(&iocb, NULL);
	iocb.private = &siocb;
	ret = __sock_recvmsg(&iocb, sock, msg, size, flags);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(&iocb);
	return ret;
}
EXPORT_SYMBOL(sock_recvmsg);

static int sock_recvmsg_nosec(struct socket *sock, struct msghdr *msg,
			      size_t size, int flags)
{
	struct kiocb iocb;
	struct sock_iocb siocb;
	int ret;

	init_sync_kiocb(&iocb, NULL);
	iocb.private = &siocb;
	ret = __sock_recvmsg_nosec(&iocb, sock, msg, size, flags);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(&iocb);
	return ret;
}

/**
 * kernel_recvmsg - Receive a message from a socket (kernel space)
 * @sock:       The socket to receive the message from
 * @msg:        Received message
 * @vec:        Input s/g array for message data
 * @num:        Size of input s/g array
 * @size:       Number of bytes to read
 * @flags:      Message flags (MSG_DONTWAIT, etc...)
 *
 * On return the msg structure contains the scatter/gather array passed in the
 * vec argument. The array is modified so that it consists of the unfilled
 * portion of the original array.
 *
 * The returned value is the total number of bytes received, or an error.
 */
int kernel_recvmsg(struct socket *sock, struct msghdr *msg,
		   struct kvec *vec, size_t num, size_t size, int flags)
{
	mm_segment_t oldfs = get_fs();
	int result;

	set_fs(KERNEL_DS);
	/*
	 * the following is safe, since for compiler definitions of kvec and
	 * iovec are identical, yielding the same in-core layout and alignment
	 */
	msg->msg_iov = (struct iovec *)vec, msg->msg_iovlen = num;
	result = sock_recvmsg(sock, msg, size, flags);
	set_fs(oldfs);
	return result;
}
EXPORT_SYMBOL(kernel_recvmsg);

static ssize_t sock_sendpage(struct file *file, struct page *page,
			     int offset, size_t size, loff_t *ppos, int more)
{
	struct socket *sock;
	int flags;

	sock = file->private_data;

	flags = (file->f_flags & O_NONBLOCK) ? MSG_DONTWAIT : 0;
	/* more is a combination of MSG_MORE and MSG_SENDPAGE_NOTLAST */
	flags |= more;

	return kernel_sendpage(sock, page, offset, size, flags);
}

static ssize_t sock_splice_read(struct file *file, loff_t *ppos,
				struct pipe_inode_info *pipe, size_t len,
				unsigned int flags)
{
	struct socket *sock = file->private_data;

	if (unlikely(!sock->ops->splice_read))
		return -EINVAL;

	return sock->ops->splice_read(sock, ppos, pipe, len, flags);
}

static struct sock_iocb *alloc_sock_iocb(struct kiocb *iocb,
					 struct sock_iocb *siocb)
{
	siocb->kiocb = iocb;
	iocb->private = siocb;
	return siocb;
}

static ssize_t do_sock_read(struct msghdr *msg, struct kiocb *iocb,
		struct file *file, const struct iovec *iov,
		unsigned long nr_segs)
{
	struct socket *sock = file->private_data;
	size_t size = 0;
	int i;

	for (i = 0; i < nr_segs; i++)
		size += iov[i].iov_len;

	msg->msg_name = NULL;
	msg->msg_namelen = 0;
	msg->msg_control = NULL;
	msg->msg_controllen = 0;
	msg->msg_iov = (struct iovec *)iov;
	msg->msg_iovlen = nr_segs;
	msg->msg_flags = (file->f_flags & O_NONBLOCK) ? MSG_DONTWAIT : 0;

	return __sock_recvmsg(iocb, sock, msg, size, msg->msg_flags);
}

static ssize_t sock_aio_read(struct kiocb *iocb, const struct iovec *iov,
				unsigned long nr_segs, loff_t pos)
{
	struct sock_iocb siocb, *x;

	if (pos != 0)
		return -ESPIPE;

	if (iocb->ki_nbytes == 0)	/* Match SYS5 behaviour */
		return 0;


	x = alloc_sock_iocb(iocb, &siocb);
	if (!x)
		return -ENOMEM;
	return do_sock_read(&x->async_msg, iocb, iocb->ki_filp, iov, nr_segs);
}

static ssize_t do_sock_write(struct msghdr *msg, struct kiocb *iocb,
			struct file *file, const struct iovec *iov,
			unsigned long nr_segs)
{
	struct socket *sock = file->private_data;
	size_t size = 0;
	int i;

	for (i = 0; i < nr_segs; i++)
		size += iov[i].iov_len;

	msg->msg_name = NULL;
	msg->msg_namelen = 0;
	msg->msg_control = NULL;
	msg->msg_controllen = 0;
	msg->msg_iov = (struct iovec *)iov;
	msg->msg_iovlen = nr_segs;
	msg->msg_flags = (file->f_flags & O_NONBLOCK) ? MSG_DONTWAIT : 0;
	if (sock->type == SOCK_SEQPACKET)
		msg->msg_flags |= MSG_EOR;

	return __sock_sendmsg(iocb, sock, msg, size);
}

static ssize_t sock_aio_write(struct kiocb *iocb, const struct iovec *iov,
			  unsigned long nr_segs, loff_t pos)
{
	struct sock_iocb siocb, *x;

	if (pos != 0)
		return -ESPIPE;

	x = alloc_sock_iocb(iocb, &siocb);
	if (!x)
		return -ENOMEM;

	return do_sock_write(&x->async_msg, iocb, iocb->ki_filp, iov, nr_segs);
}

/*
 * Atomic setting of ioctl hooks to avoid race
 * with module unload.
 */

static DEFINE_MUTEX(br_ioctl_mutex);
static int (*br_ioctl_hook) (struct net *, unsigned int cmd, void __user *arg);

void brioctl_set(int (*hook) (struct net *, unsigned int, void __user *))
{
	mutex_lock(&br_ioctl_mutex);
	br_ioctl_hook = hook;
	mutex_unlock(&br_ioctl_mutex);
}
EXPORT_SYMBOL(brioctl_set);

static DEFINE_MUTEX(vlan_ioctl_mutex);
static int (*vlan_ioctl_hook) (struct net *, void __user *arg);

void vlan_ioctl_set(int (*hook) (struct net *, void __user *))
{
	mutex_lock(&vlan_ioctl_mutex);
	vlan_ioctl_hook = hook;
	mutex_unlock(&vlan_ioctl_mutex);
}
EXPORT_SYMBOL(vlan_ioctl_set);

static DEFINE_MUTEX(dlci_ioctl_mutex);
static int (*dlci_ioctl_hook) (unsigned int, void __user *);

void dlci_ioctl_set(int (*hook) (unsigned int, void __user *))
{
	mutex_lock(&dlci_ioctl_mutex);
	dlci_ioctl_hook = hook;
	mutex_unlock(&dlci_ioctl_mutex);
}
EXPORT_SYMBOL(dlci_ioctl_set);

static long sock_do_ioctl(struct net *net, struct socket *sock,
				 unsigned int cmd, unsigned long arg)
{
	int err;
	void __user *argp = (void __user *)arg;

	err = sock->ops->ioctl(sock, cmd, arg);

	/*
	 * If this ioctl is unknown try to hand it down
	 * to the NIC driver.
	 */
	if (err == -ENOIOCTLCMD)
		err = dev_ioctl(net, cmd, argp);

	return err;
}

/*
 *	With an ioctl, arg may well be a user mode pointer, but we don't know
 *	what to do with it - that's up to the protocol still.
 */

static long sock_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	struct socket *sock;
	struct sock *sk;
	void __user *argp = (void __user *)arg;
	int pid, err;
	struct net *net;

	sock = file->private_data;
	sk = sock->sk;
	net = sock_net(sk);
	if (cmd >= SIOCDEVPRIVATE && cmd <= (SIOCDEVPRIVATE + 15)) {
		err = dev_ioctl(net, cmd, argp);
	} else
#ifdef CONFIG_WEXT_CORE
	if (cmd >= SIOCIWFIRST && cmd <= SIOCIWLAST) {
		err = dev_ioctl(net, cmd, argp);
	} else
#endif
		switch (cmd) {
		case FIOSETOWN:
		case SIOCSPGRP:
			err = -EFAULT;
			if (get_user(pid, (int __user *)argp))
				break;
			f_setown(sock->file, pid, 1);
			err = 0;
			break;
		case FIOGETOWN:
		case SIOCGPGRP:
			err = put_user(f_getown(sock->file),
				       (int __user *)argp);
			break;
		case SIOCGIFBR:
		case SIOCSIFBR:
		case SIOCBRADDBR:
		case SIOCBRDELBR:
			err = -ENOPKG;
			if (!br_ioctl_hook)
				request_module("bridge");

			mutex_lock(&br_ioctl_mutex);
			if (br_ioctl_hook)
				err = br_ioctl_hook(net, cmd, argp);
			mutex_unlock(&br_ioctl_mutex);
			break;
		case SIOCGIFVLAN:
		case SIOCSIFVLAN:
			err = -ENOPKG;
			if (!vlan_ioctl_hook)
				request_module("8021q");

			mutex_lock(&vlan_ioctl_mutex);
			if (vlan_ioctl_hook)
				err = vlan_ioctl_hook(net, argp);
			mutex_unlock(&vlan_ioctl_mutex);
			break;
		case SIOCADDDLCI:
		case SIOCDELDLCI:
			err = -ENOPKG;
			if (!dlci_ioctl_hook)
				request_module("dlci");

			mutex_lock(&dlci_ioctl_mutex);
			if (dlci_ioctl_hook)
				err = dlci_ioctl_hook(cmd, argp);
			mutex_unlock(&dlci_ioctl_mutex);
			break;
		default:
			err = sock_do_ioctl(net, sock, cmd, arg);
			break;
		}
	return err;
}

int sock_create_lite(int family, int type, int protocol, struct socket **res)
{
	int err;
	struct socket *sock = NULL;

	err = security_socket_create(family, type, protocol, 1);
	if (err)
		goto out;

	sock = sock_alloc();
	if (!sock) {
		err = -ENOMEM;
		goto out;
	}

	sock->type = type;
	err = security_socket_post_create(sock, family, type, protocol, 1);
	if (err)
		goto out_release;

out:
	*res = sock;
	return err;
out_release:
	sock_release(sock);
	sock = NULL;
	goto out;
}
EXPORT_SYMBOL(sock_create_lite);

/* No kernel lock held - perfect */
static unsigned int sock_poll(struct file *file, poll_table *wait)
{
	unsigned int busy_flag = 0;
	struct socket *sock;

	/*
	 *      We can't return errors to poll, so it's either yes or no.
	 */
	sock = file->private_data;

	if (sk_can_busy_loop(sock->sk)) {
		/* this socket can poll_ll so tell the system call */
		busy_flag = POLL_BUSY_LOOP;

		/* once, only if requested by syscall */
		if (wait && (wait->_key & POLL_BUSY_LOOP))
			sk_busy_loop(sock->sk, 1);
	}

	return busy_flag | sock->ops->poll(file, sock, wait);
}

static int sock_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct socket *sock = file->private_data;

	return sock->ops->mmap(file, sock, vma);
}

static int sock_close(struct inode *inode, struct file *filp)
{
	sock_release(SOCKET_I(inode));
	return 0;
}

/*
 *	Update the socket async list
 *
 *	Fasync_list locking strategy.
 *
 *	1. fasync_list is modified only under process context socket lock
 *	   i.e. under semaphore.
 *	2. fasync_list is used under read_lock(&sk->sk_callback_lock)
 *	   or under socket lock
 */

static int sock_fasync(int fd, struct file *filp, int on)
{
	struct socket *sock = filp->private_data;
	struct sock *sk = sock->sk;
	struct socket_wq *wq;

	if (sk == NULL)
		return -EINVAL;

	lock_sock(sk);
	wq = rcu_dereference_protected(sock->wq, sock_owned_by_user(sk));
	fasync_helper(fd, filp, on, &wq->fasync_list);

	if (!wq->fasync_list)
		sock_reset_flag(sk, SOCK_FASYNC);
	else
		sock_set_flag(sk, SOCK_FASYNC);

	release_sock(sk);
	return 0;
}

/* This function may be called only under socket lock or callback_lock or rcu_lock */

int sock_wake_async(struct socket *sock, int how, int band)
{
	struct socket_wq *wq;

	if (!sock)
		return -1;
	rcu_read_lock();
	wq = rcu_dereference(sock->wq);
	if (!wq || !wq->fasync_list) {
		rcu_read_unlock();
		return -1;
	}
	switch (how) {
	case SOCK_WAKE_WAITD:
		if (test_bit(SOCK_ASYNC_WAITDATA, &sock->flags))
			break;
		goto call_kill;
	case SOCK_WAKE_SPACE:
		if (!test_and_clear_bit(SOCK_ASYNC_NOSPACE, &sock->flags))
			break;
		/* fall through */
	case SOCK_WAKE_IO:
call_kill:
		kill_fasync(&wq->fasync_list, SIGIO, band);
		break;
	case SOCK_WAKE_URG:
		kill_fasync(&wq->fasync_list, SIGURG, band);
	}
	rcu_read_unlock();
	return 0;
}
EXPORT_SYMBOL(sock_wake_async);

int __sock_create(struct net *net, int family, int type, int protocol,
			 struct socket **res, int kern)
{
	int err;
	struct socket *sock;
	const struct net_proto_family *pf;

	/*
	 *      Check protocol is in range
	 */
	if (family < 0 || family >= NPROTO)
		return -EAFNOSUPPORT;
	if (type < 0 || type >= SOCK_MAX)
		return -EINVAL;

	/* Compatibility.

	   This uglymoron is moved from INET layer to here to avoid
	   deadlock in module load.
	 */
	if (family == PF_INET && type == SOCK_PACKET) {
		static int warned;
		if (!warned) {
			warned = 1;
			pr_info("%s uses obsolete (PF_INET,SOCK_PACKET)\n",
				current->comm);
		}
		family = PF_PACKET;
	}

	err = security_socket_create(family, type, protocol, kern);
	if (err)
		return err;

	/*
	 *	Allocate the socket and allow the family to set things up. if
	 *	the protocol is 0, the family is instructed to select an appropriate
	 *	default.
	 */
	sock = sock_alloc();
	if (!sock) {
		net_warn_ratelimited("socket: no more sockets\n");
		return -ENFILE;	/* Not exactly a match, but its the
				   closest posix thing */
	}

	sock->type = type;

#ifdef CONFIG_MODULES
	/* Attempt to load a protocol module if the find failed.
	 *
	 * 12/09/1996 Marcin: But! this makes REALLY only sense, if the user
	 * requested real, full-featured networking support upon configuration.
	 * Otherwise module support will break!
	 */
	if (rcu_access_pointer(net_families[family]) == NULL)
		request_module("net-pf-%d", family);
#endif

	rcu_read_lock();
	pf = rcu_dereference(net_families[family]);
	err = -EAFNOSUPPORT;
	if (!pf)
		goto out_release;

	/*
	 * We will call the ->create function, that possibly is in a loadable
	 * module, so we have to bump that loadable module refcnt first.
	 */
	if (!try_module_get(pf->owner))
		goto out_release;

	/* Now protected by module ref count */
	rcu_read_unlock();

	err = pf->create(net, sock, protocol, kern);
	if (err < 0)
		goto out_module_put;

	/*
	 * Now to bump the refcnt of the [loadable] module that owns this
	 * socket at sock_release time we decrement its refcnt.
	 */
	if (!try_module_get(sock->ops->owner))
		goto out_module_busy;

	/*
	 * Now that we're done with the ->create function, the [loadable]
	 * module can have its refcnt decremented
	 */
	module_put(pf->owner);
	err = security_socket_post_create(sock, family, type, protocol, kern);
	if (err)
		goto out_sock_release;
	*res = sock;

	return 0;

out_module_busy:
	err = -EAFNOSUPPORT;
out_module_put:
	sock->ops = NULL;
	module_put(pf->owner);
out_sock_release:
	sock_release(sock);
	return err;

out_release:
	rcu_read_unlock();
	goto out_sock_release;
}
EXPORT_SYMBOL(__sock_create);

int sock_create(int family, int type, int protocol, struct socket **res)
{
	return __sock_create(current->nsproxy->net_ns, family, type, protocol, res, 0);
}
EXPORT_SYMBOL(sock_create);

int sock_create_kern(int family, int type, int protocol, struct socket **res)
{
	return __sock_create(&init_net, family, type, protocol, res, 1);
}
EXPORT_SYMBOL(sock_create_kern);

SYSCALL_DEFINE3(socket, int, family, int, type, int, protocol)
{
	int retval;
	struct socket *sock;
	int flags;

	/* Check the SOCK_* constants for consistency.  */
	BUILD_BUG_ON(SOCK_CLOEXEC != O_CLOEXEC);
	BUILD_BUG_ON((SOCK_MAX | SOCK_TYPE_MASK) != SOCK_TYPE_MASK);
	BUILD_BUG_ON(SOCK_CLOEXEC & SOCK_TYPE_MASK);
	BUILD_BUG_ON(SOCK_NONBLOCK & SOCK_TYPE_MASK);

	flags = type & ~SOCK_TYPE_MASK;
	if (flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK))
		return -EINVAL;
	type &= SOCK_TYPE_MASK;

	if (SOCK_NONBLOCK != O_NONBLOCK && (flags & SOCK_NONBLOCK))
		flags = (flags & ~SOCK_NONBLOCK) | O_NONBLOCK;

	retval = sock_create(family, type, protocol, &sock);
	if (retval < 0)
		goto out;

	if (retval == 0)
		sockev_notify(SOCKEV_SOCKET, sock);

	retval = sock_map_fd(sock, flags & (O_CLOEXEC | O_NONBLOCK));
	if (retval < 0)
		goto out_release;

out:
	/* It may be already another descriptor 8) Not kernel problem. */
	return retval;

out_release:
	sock_release(sock);
	return retval;
}

/*
 *	Create a pair of connected sockets.
 */

SYSCALL_DEFINE4(socketpair, int, family, int, type, int, protocol,
		int __user *, usockvec)
{
	struct socket *sock1, *sock2;
	int fd1, fd2, err;
	struct file *newfile1, *newfile2;
	int flags;

	flags = type & ~SOCK_TYPE_MASK;
	if (flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK))
		return -EINVAL;
	type &= SOCK_TYPE_MASK;

	if (SOCK_NONBLOCK != O_NONBLOCK && (flags & SOCK_NONBLOCK))
		flags = (flags & ~SOCK_NONBLOCK) | O_NONBLOCK;

	/*
	 * Obtain the first socket and check if the underlying protocol
	 * supports the socketpair call.
	 */

	err = sock_create(family, type, protocol, &sock1);
	if (err < 0)
		goto out;

	err = sock_create(family, type, protocol, &sock2);
	if (err < 0)
		goto out_release_1;

	err = sock1->ops->socketpair(sock1, sock2);
	if (err < 0)
		goto out_release_both;

	fd1 = get_unused_fd_flags(flags);
	if (unlikely(fd1 < 0)) {
		err = fd1;
		goto out_release_both;
	}

	fd2 = get_unused_fd_flags(flags);
	if (unlikely(fd2 < 0)) {
		err = fd2;
		goto out_put_unused_1;
	}

	newfile1 = sock_alloc_file(sock1, flags, NULL);
	if (unlikely(IS_ERR(newfile1))) {
		err = PTR_ERR(newfile1);
		goto out_put_unused_both;
	}

	newfile2 = sock_alloc_file(sock2, flags, NULL);
	if (IS_ERR(newfile2)) {
		err = PTR_ERR(newfile2);
		goto out_fput_1;
	}

	err = put_user(fd1, &usockvec[0]);
	if (err)
		goto out_fput_both;

	err = put_user(fd2, &usockvec[1]);
	if (err)
		goto out_fput_both;

	audit_fd_pair(fd1, fd2);

	fd_install(fd1, newfile1);
	fd_install(fd2, newfile2);
	/* fd1 and fd2 may be already another descriptors.
	 * Not kernel problem.
	 */

	return 0;

out_fput_both:
	fput(newfile2);
	fput(newfile1);
	put_unused_fd(fd2);
	put_unused_fd(fd1);
	goto out;

out_fput_1:
	fput(newfile1);
	put_unused_fd(fd2);
	put_unused_fd(fd1);
	sock_release(sock2);
	goto out;

out_put_unused_both:
	put_unused_fd(fd2);
out_put_unused_1:
	put_unused_fd(fd1);
out_release_both:
	sock_release(sock2);
out_release_1:
	sock_release(sock1);
out:
	return err;
}

/*
 *	Bind a name to a socket. Nothing much to do here since it's
 *	the protocol's responsibility to handle the local address.
 *
 *	We move the socket address to kernel space before we call
 *	the protocol layer (having also checked the address is ok).
 */

SYSCALL_DEFINE3(bind, int, fd, struct sockaddr __user *, umyaddr, int, addrlen)
{
	struct socket *sock;
	struct sockaddr_storage address;
	int err, fput_needed;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (sock) {
		err = move_addr_to_kernel(umyaddr, addrlen, &address);
		if (err >= 0) {
			err = security_socket_bind(sock,
						   (struct sockaddr *)&address,
						   addrlen);
			if (!err)
				err = sock->ops->bind(sock,
						      (struct sockaddr *)
						      &address, addrlen);
		}
		if (!err) {
			if (sock->sk)
				sock_hold(sock->sk);
			sockev_notify(SOCKEV_BIND, sock);
			if (sock->sk)
				sock_put(sock->sk);
		}
		fput_light(sock->file, fput_needed);
	}
	return err;
}

/*
 *	Perform a listen. Basically, we allow the protocol to do anything
 *	necessary for a listen, and if that works, we mark the socket as
 *	ready for listening.
 */

SYSCALL_DEFINE2(listen, int, fd, int, backlog)
{
	struct socket *sock;
	int err, fput_needed;
	int somaxconn;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (sock) {
		somaxconn = sock_net(sock->sk)->core.sysctl_somaxconn;
		if ((unsigned int)backlog > somaxconn)
			backlog = somaxconn;

		err = security_socket_listen(sock, backlog);
		if (!err)
			err = sock->ops->listen(sock, backlog);

		if (!err) {
			if (sock->sk)
				sock_hold(sock->sk);
			sockev_notify(SOCKEV_LISTEN, sock);
			if (sock->sk)
				sock_put(sock->sk);
		}
		fput_light(sock->file, fput_needed);
	}
	return err;
}

/*
 *	For accept, we attempt to create a new socket, set up the linbLk*	with a e curlie, bake_ap the lirlie, baen returnedhe liw s*	calnected sofdWe usclen cthe address is the curnected acikernel_s*	socce bed mapo it in crer *,athe adve foen The iss unknear_nefocau
 *	ca aten = e socket asen returnedh error.
 */
*	1. 3.1g
	ddrethe soality to hacvmsg(io)o haque fornectedn mapdif

 *	neatus);o hacvmsg(iWe used it a sddhat wopport wi a low beat w *	reear_newn re retustture cocept, lso c */

SYSCALL_DEFINE4(socept, 4int, fd, struct sockaddr __user *, umypeer_ckaddr _		int __user *, usopeer_drlen, &at, fd,gs)
{
	struct socket *sock =*newfick;
	struct sole *newfile;
	int fdr, &fn, &awfildfput_needed;
	inruct sockaddr_storage address;
	iif (flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK))
		return -EINVAL;
	tif (SOCK_NONBLOCK != O_NONBLOCK && (flags & SOCK_NONBLOCK))
		flags = (flags & ~SOCK_NONBLOCK) | O_NONBLOCK;

	/*ck = sockfd_lookup_light(fd, &err, &fput_needed);
	if (soock)
		reto out;

	err = soNFILE;	/newfick = sock_alloc();
	if (!sowfick =		goto out_fpt;

	/*wfick =type = tyck =type =;/*wfick =tys = NUck =tys =
	/*
	 * We win't kned it _module_get(sere sandshe linbening.
ocket (keck =		g Wehdshe liotocol module ifock->ops->owner))
eld -	 */
	if_odule_get(sowfick =tys =owner);
ounewfil= get_unused_fd_flags(flags);
	if (unlikely(fdwfil= 0)) {
		err = fdwfil=;	sock_resease(sowfick =			goto out_put_u	}
	rewfile = sock_alloc_file(sowfick =flags, NUck->sk);sk_prot_creator->name;

	if (unlikely(IS_ERR(newfile1)) {
		err = PTR_ERR(newfile1)			got_unused_fd(fdwfil=);	sock_resease(sowfick =			goto out_put_u	}
	rerr = security_socket_pocept, ock, bawfick =			g (err)
		goto out_fpud
	err = sock->ops->iocept, ock, bawfick =NUck->skle->f_flags &
	if (err < 0)
		goto out_reud
	er (unlpeer_ckaddr _{
		if (!swfick =tys =owt_ume(dewfick =NUtruct sockaddr *)&address,
						 &adn, &a2) 0)
	
			err = se-ECONNABORTED			ifto out_reud
	}
		error move_addr_to_keer(fdddress,
						 n, &aypeer_ckaddr _	sopeer_drlen, 
		if (err >=0)
		gofto out_reud
	}}	/* ChFe = ags & e idt eminre ed(" viaocept, o)olikelynother deOSes./

	re_install(fdwfildfpwfile1)			gr = fdwfil=;	s (!err)
			eckev_notify(SOCKEV_LIACCEP sock);

	t_put_u	fput(night(sock->file, fput_needed);
	}t:
	return err;
}
t_reud	fput(newfile1)
	put_unused_fd(fdwfil=);	sto out_put_u	}}SYSCALL_DEFINE3(bicept, wet, fd, struct sockaddr __user *, umypeer_ckaddr _		int __user *, usopeer_drlen, {
	return __sctocept, 4d, &eypeer_ckaddr _	sopeer_drlen, 0);
}
EX*
 *	Fotempt to lornectedo a socket. ith the ->r *v *,aress.
 The soaress.
*	Fo in a er *,cce be we hav *y(St is boOKed mapo it in crrnel space b */
*	1.r ac 3.1g
	dwused it a sddhear_nepport wir a lind(st a AF_UNSP | 
 *	aneak!
ind(sgs u*/
*	1.NOTE:c 3.1g
	ddraft 6.3s bourok re h thrpec_cin crAX.25/NetROMnd
	 	of er deQPACKET)
iotocol mshat woke came we lornectedo)oast downest k*	Fo neaudthe soINVAPROGRESS atus);or a sh tockets.
 */

SYSCALL_DEFINE4(3(rnectedwet, fd, struct sockaddr __user *, umyr *vdr _		int _addrlen)
{
	struct socket *sock;
	struct sockaddr_storage address;
	int err, fput_needed;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (soock)
		reto out;

	rr = move_addr_to_kernel(umyr *vdr _	ddrlen, &address);
		i (err < 0)
		goto out_rep;

	err = s    ktcurity_socket_crenectedock =NUtruct sockaddr *)&address,
	ddrlen);
		} (err)
		goto out_fpp;

	err = sock_ctys =owenectedock =NUtruct sockaddr *)&address,
	ddrlen);				 strk->skle->f_flags &
	if (errr)
			eckev_notify(SOCKEV_LICONNEC sock);

	t_put_u	fput(night(sock->file, fput_needed);
	}t:
	return err;
}
EX*
 *	FoG asen ocal address.
 ('me(d')s thsocket. iject. ThMe the sooain t
 * p	me to a er *,cce b */

SYSCALL_DEFINE4(3(t_ucketme, prt, fd, struct sockaddr __user *, umyrkaddr _		int __user *, usockaddr_stn)
{
	struct socket *sock;
	struct sockaddr_storage address;
	int ern, &ar, fput_needed;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (soock)
		reto out;

	err = security_socket_pot_ucketme, ock);
	re (err)
		goto out_fpp;

	err = sock_ctys =owt_ume(deck =NUtruct sockaddr *)&address,
	d&n, 0);
}
e (err)
		goto out_fpp;

	rror move_addr_to_keer(fdddress,
	rn, &ayrkaddr _	sockaddr_stn)
{
out_put_u	fput(night(sock->file, fput_needed);
	}t:
	return err;
}
EX*
 *	FoG asen oremoteddress.
 ('me(d')s thsocket. iject. ThMe the sooain t
 * p	me to a er *,cce b */

SYSCALL_DEFINE4(3(t_upeerme, prt, fd, struct sockaddr __user *, umyrkaddr _		int __user *, usockaddr_stn)
{
	struct socket *sock;
	struct sockaddr_storage address;
	int ern, &ar, fput_needed;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (sock = !NULL)
	
		err = PTcurity_socket_pot_upeerme, ock);
		so (err)
	
			erut_light(sock->file, fput_needed);
	}
return err;

		}}	/*rr = s     ktck_ctys =owt_ume(deck =NUtruct sockaddr *)&address,
	d&n, 0
		      &a);
			e (!err)
			err = sove_addr_to_keer(fdddress,
	rn, &ayrkaddr _						  ockaddr_stn)
{
ofput_light(sock->file, fput_needed);
	}
	return err;
}

/*
 *	FoSe a nata;
gramo a sogiv rearess.
 T move the sodress is n crrnel s*	socce bed macck the SOer *,cce beta;
 e ias releable mofore weinvok

 *	nee liotocol m */

SYSCALL_DEFINE4(6(ndmstoprt, fd, stid __user *)abuffersize_t l,en,
				signed int, voags, NUcuct sockaddr __user *, umdr _		int _addrletn)
{
	struct socket *sock;
	struct sockaddr_storage address;
	int err;
	struct fighdr *mg;
	si-uct iovec *iv;
	mst flat_needed;

	socept ookgsendmstod, &effersin, flags);umdr _	 dr_stn)
{
ou	 (len < > INTAX)
		ren = nrINTAX)
	if (unlikely(IS!cess_pook(VERIFY_READ&effersin, )
		return -EINULT;
			ck = sockfd_lookup_light(fd, &err, &fput_needed);
	if (soock)
		reto out;

	erv;
ov_lebe ti=effer;erv;
ov_len = nrn;
	}
g(iWg_name = NULL;
	msg->.g_iov = (s&v;
	msg->.g_iovlen = nr1	msg->.g_iontrol = NULL;
	msg->.g_controllen = 0;
	msg->.g_namelen = 0;
	ms (sodr _{
		ifr = move_addr_to_kernel(umdr _	 dr_stn)
&address);
		if (err >=0)
		gofto out_rep;

	r
g(iWg_name = NUtruct sockaddr *)&address,

	r
g(iWg_name =n = 0;dr_stn)
	}
	re (sock->skle->f_flags & O_NONBLOCK) ?
flags |= SKG_DONTWAIT :	msg->.g_naags = flags;

	rr = sock_crndmsg(sock, ms&g, ion)
{
out_put_u	fput(night(sock->file, fput_needed);
	}t:
	return err;
}
EX*
 *	FoSe a nata;
gramown
	 socket. N*/

SYSCALL_DEFINE4(soce, int, fd, stid __user *)abuffersize_t l,en,
				signed int, voags, {
	return __sctondmstod, &effersin, flags);umLL;
0);
}
EX*
 *	Foceive a mefre = om the prcket *sd maopdn mly, acvmordhe address is the c*	soceerly T mov *y(Ste adffer +& e idite(le mod if theded);ove the s*	soceerlyddress isom thrnel sp a er *,cce b */

SYSCALL_DEFINE4(6(cvmsom tint, fd, stid __user *)abuuffesize_t l,eze, m			signed int, voags, NUcuct sockaddr __user *, umdr _		int __user *, umdr _tn)
{
	struct socket *sock;
	struct sovec *iv;
	msruct fighdr *mg;
	si-uct iockaddr_storage address;
	int err, fpr, 2	mst flat_needed;

	soi(size < > INTAX)
		reze = 0;INTAX)
	if (unlikely(IS!cess_pook(VERIFY_WRITEbuuffesize_t)
		return -EINULT;
			ck = sockfd_lookup_light(fd, &err, &fput_needed);
	if (soock)
		reto out;

	erg->.g_iontrol = NULL;
	msg->.g_controllen = 0;
	msg->.g_navlen = nr1	msg->.g_iov = (s&v;
	msv;
ov_len = nrze;
	siv;
ov_lebe ti=euffe;/* ChSe itso = cyclesod ifn't kncopyhe sodress is fdt emeded);o
	msg->Wg_name = NUdr __?Utruct sockaddr *)&address,
 :ULL;
	ms/We wiassumallowhrnel spce *iow
 the soce of inckaddr_storage ad
	msg->Wg_name =n = 0;
	ms (sock->skle->f_flags & O_NONBLOCK) ?
flags |= SKG_DONTWAIT :	msr = sock_crcvmsg(sock, ms&g, size, flags);

	re (err >= 0) & (fdr *)!NULL)
	
		err =2 move_addr_to_keer(fdddress,
						  g->Wg_name =n =umdr _	 dr_stn)
{
oif (err >2=0)
		gofr = sor, 2	ms
	fd2t(night(sock->file, fput_needed);
	}t:
	return err;
}
EX*
 *	Foceive a meta;
gramoom a socket (N*/

SYSCALL_DEFINE4(socvmsint, fd, stid __user *)abuuffesize_t l,eze, m			signed int, voags, {
	return __sctocvmsom td, &eyffesize_tflags);umLL;
0)LL);
	iEX*
 *	FoSethsocket. ijpdn m. Bocau
 e don't know
 he soopdn min, gthse have t*	neeoassedhe SOer *,de poiare =t *,r a e liotocol m;o hast wit:
N*/

SYSCALL_DEFINE4(5oceucketo, wet, fd, stt, fdlevelstt, fdo, me, p			cha__user *, umo, valstt, fdo, n)
{
	stt err, fput_needed;
	inruct socket *sock;

	/* (ero, n)
=0)
		goturn -EINVAL;
	tifck = sockfd_lookup_light(fd, &err, &fput_needed);
	if (sock = !NULL)
	
		err = PTcurity_socket_poceucketo, ock, mslevelsto, me, 
		so (err)
		gofto out_rep;

		so (erlevel= SOCKSOCKET, 		gofr = s
	    ktck_coceucketo, ock, mslevelsto, me, umo, vals					 &a do, n)
{		sose
		sofr = s
	    ktck_cops->senducketo, ock, mslevelsto, me, umo, vals					 	 do, n)
{		t_put_u	fpput_light(sock->file, fput_needed);
	}
	return err;
}

/*
 *	FoGethsocket. ijpdn m. Bocau
 e don't know
 he soopdn min, gthse have t*	neeoassedhaOer *,de poiare =t *,r a e liotocol m;o hast wit:
N*/

SYSCALL_DEFINE4(5ogeucketo, wet, fd, stt, fdlevelstt, fdo, me, p			cha__user *, umo, valstt, _user *, umo, n)
{
	stt err, fput_needed;
	inruct socket *sock;

	/*ck = sockfd_lookup_light(fd, &err, &fput_needed);
	if (sock = !NULL)
	
		err = PTcurity_socket_pot_ucketo, ock, mslevelsto, me, 
		so (err)
		gofto out_rep;

		so (erlevel= SOCKSOCKET, 		gofr = s
	    ktck_cot_ucketo, ock, mslevelsto, me, umo, vals					 &a do, n)
{		sose
		sofr = s
	    ktck_cops->set_ucketo, ock, mslevelsto, me, umo, vals					 	 do, n)
{		t_put_u	fpput_light(sock->file, fput_needed);
	}
	return err;
}

/*
 *	FoShutwn
	 socket. N*/

SYSCALL_DEFINE4(2(shutwn
	wet, fd, stt, fdw) {
	stt err, fput_needed;
	inruct socket *sock;

	/*ck = sockfd_lookup_light(fd, &err, &fput_needed);
	if (sock = !NULL)
	
		erckev_notify(SOCKEV_LISHUTDOW sock);
			ir = PTcurity_socket_pochutwn
	ock, msw) {			e (!err)
			err = sock_cops->senhutwn
	ock, msw) {			eut_light(sock->file, fput_needed);
	}
	return err;
}

/*
  Aountplof inlperful macro;or a gting ofe address is the cu32/64 bik*	F fldinse hich e ide same inpe = nt fa/nsigned i)  of ur platrm a
 */

S#finite COMPATAXSGsg, skmeer o)	(SG_DOCG_DOCOMPAT O_ags, {_?U&g, ##_cpatib->meer o :U&g->msgeer o)S#finite COMPATANAMELENsg, )	COMPATAXSGsg, skm_name =n =)S#finite COMPATAFLAGSsg, )	COMPATAXSGsg, skm_naags, {

ruct soed_fddress is{si-uct iockaddr_storage adme =;unsigned int bume =tn)
	}}
static int socopy_ghdr *_om tser(sk)uct msghdr *mskg,
			  struct msghdr *muser *, ug, )
	if (socopy_om tser(skkg,
	 ug, size, ofk)uct msghdr *)
		return -EINULT;
		if (sokg->msg_name = NNULL)
		rekg->msg_name =n = 0;
	mif (sokg->msg_name =n)
=0)
		goturn -EINVAL;
	tif (sokg->msg_name =n)
=>ize, ofk)uct msckaddr_storage a)		rekg->msg_name =n = 0;ze, ofk)uct msckaddr_storage a);return 0;
}

/*atic int so___sctondmsg(struct socket *sock, struct msghdr *muser *, g,
			  truct msghdr *msg,
_sctunsigned int cmags);u		  truct msed_fddress is*ed_fddress i{
	struct socpatib_ghdr *muser *, g,
_cpatib s    kt(ruct socpatib_ghdr *muser *, )g;
	si-uct iockaddr_storage address;
	inruct sovec *iv;
atick[UIOASYNTIOV],iov,
 =iv;
atick;unsigned incha__ctl[ze, ofk)uct mscghdr *) 2))0]    kt__attribute__ ((ignme(soceof(__u3rnel_sene_t l)
	
	/* fd20s safe of inppv6_pktfo *pi/unsigned incha__*l_soffe =il_s	int err, fpl_son =umtal nen;

	msr = se-EULT;
			 (soG_DOCG_DOCOMPAT O_ags, {		ir = PTg_crenatib_ghdr *(g,
_sctung,
_cpatib			gre
		sor = PTcopy_ghdr *_om tser(skg,
_sctung,

}
e (err)
		goturn err;

	/* (erg,
_sctmsg_iovlen = > UIOASYNTIOV{
		err = -ENOG_DSIZE			e (!eg,
_sctmsg_iovlen = > UIOAMAXIOV{	gofto out_r;	err = -ENOMEM;
		gov,
 =ikmloc();g,
_sctmsg_iovlen = *;ze, ofk)uct msvec *)			      siGFP_RNEL_D{			e (!ervec{	gofto out_r;	e}	/* Che issll caso chve the sodress ista;
  n crrnel s,cce be
	if (!tG_DOCG_DOCOMPAT O_ags, {_		err = -Ev *y(Srenatib_vec *kg,
_sctunv, nrddress,
	rVERIFY_READ
	}
	 re
		sor = PTv *y(Srvec *kg,
_sctunv, nrddress,
	rVERIFY_READ
	}
 (err < 0)
		goto out_reureev;
	mstal nen;
 sor, 
	err = soNFIOBUFS
	/* (erg,
_sctmsg_iontrollen = > INTAX)
		reto out_reureev;
	msl_son = msg;
_sctmsg_iontrollen =	}
 (ertG_DOCG_DOCOMPAT O_ags, {_ cmd_son ={_		err = -     ktcghdr *_om tser(srenatib__kernelkg,
_sctunck->fi, SOl, a					 	 d size_tofkl, )
		so (err)
		gofto out_reureev;
	ms	l_soffe =ig;
_sctmsg_iontrolle	ms	l_son = msg;
_sctmsg_iontrollen =	}
	 re
	  (soc_son ={_		er (soc_son ==>ize, ofkl, )

			erl_soffe =ick_cokmloc();ck->fi, SOl, on =umGFP_RNEL_D{			er (soc_soffe =NULL)
		reofto out_reureev;
	ms	}	err = -ENOULT;
			i
	 *g WeCe iful! Bore weis
	,sg;
_sctmsg_iontrolleontains thaOer *,inter(n. *g WeAfr(nwardtunvwill brea mernel pronter(n.he ushe curniler d-sedbenid *g Wecck tg ofll tsown
	 oneis
	. *g W		if (wacopy_om tser(skc_soffe0
		     oid __user *)__re  be
)g;
_sctmsg_iontrolle0
		     c_son ={		gofto out_reureel_s	in	g;
_sctmsg_iontrolleo=il_s_ffe;/*}
	g;
_sctmsg_ioags = flags;

	re (sock->skle->f_flags & O_NONBLOCK) ?
flg;
_sctmsg_ioags =  SKG_DONTWAIT :	ms
	 * If this iocs ndmsgg(io)od macrent->escrtation ofdress is okme ina	 * sopreour s senuccded);odress,
	romias
 tg ofLSM'secreisn.
	 * Oted_fddress iname;
on == in aitiignz it a UINTAX)
  that ite first s * Otscrtation ofdress isnev *,tch, es	 */
	if (!ted_fddress is cmg;
_sctmsg_iome = &&    kted_fddress iname;
on ====mg;
_sctmsg_iome =n ==&&    kt!geecmp(&ed_fddress iname;
,mg;
_sctmsg_iome =		   stted_fddress iname;
on = {
		err = PTck_crndmsg(sosec(stro, msg, _sctuntal nen;

		goto out_fpureel_s	in}	rr = sock_crndmsg(sock, msg, _sctuntal nen;

		g
	 * If this iocs ndmsgg(io)od masdif

 e lorrent->escrtation ofdress iswa	 * sonuccdssfulres,meer o it	 */
	if (!ted_fddress is cmr >= 0) {
			eed_fddress iname;
on ===mg;
_sctmsg_iome =n =			e (!eg,
_sctmsg_iome, 
	gofgeecpy(&ed_fddress iname;
,mg;
_sctmsg_iome =		       &a)ed_fddress iname;
on = ;	e}	/t_fpureel_s:
r (soc_soffe !=il_s		sock_rekuree_s;ck->fi, SOl, offesic_son ={;
t_reureev;
:
r (sov,
 !=iv;
atick		rekureeov,

out:
	return err;
}

/*
 *	BinSD ndmsg(s ter(nfe
 * w/

ng ar__sctondmsg(stt fd, struct fighdr *muser *, g,
	nsigned inags)
{
	int erut_needed);,rr;
	struct fighdr *mg;
_sct	inruct socket *sock;

	/*ck = sockfd_lookup_light(fd, &err, &fput_needed);
	if (soock)
		reto out;

	err = se___sctondmsg(strk, msg, ms&g, _sctunags, NULL);
	ifd2t(night(sock->file, fput_needed);
	}t:
	return err;
}
EX*SCALL_DEFINE3(socdmsg(sprt, fd, struct soghdr *muser *, msg, mssigned int, voags, {
	re (flags & ~(G_DOCG_DOCOMPAT		return -EINVAL;
	tyturn __socctondmsg(st, stg, msags);
}

st
 *	BiLinux ndmsgg(i ter(nfe
 * w/

t __socctondmsgg(stt fd, struct figghdr *muser *, gg, mssigned int,  vn,
				  nsigned int cmags);{
	int erut_needed);,rr;
,eta;
gramt	inruct socket *sock;

		ruct figghdr *muser *, t->ry;struct socpatib_gghdr *muser *, cpatib_t->ry;struct soghdr *mg;
_sct	inruct soed_fddress ised_fddress i
	re (soen = > UIOAMAXIOV{	goen = nrUIOAMAXIOV
	reta;
gramt 0;
	mifck = sockfd_lookup_light(fd, &err, &fput_needed);
	if (soock)
		return err;

	/*ed_fddress i.me;
on ===mUINTAX)
	ift->ry==mgg;
	sicpatib_t->ry NUtruct socpatib_gghdr *muser *, )gg;
	sie = 0;
		
	whe = (ta;
gramt < vn,
{_		er (soG_DOCG_DOCOMPAT O_ags, {_		errr = se___sctondmsg(strk, ms(ruct soghdr *muser *, )cpatib_t->rys					 &a ds&g, _sctunags, NU&ed_fddress i{			er (sor >=0)
		gofreak;
			f_r = se__t_user(fdr, &fpcpatib_t->rymsg_ion);
			if++cpatib_t->ry;st
	 re
	 		errr = se___sctondmsg(strk, m					 &a ds(ruct soghdr *muser *, )t->rys					 &a ds&g, _sctunags, NU&ed_fddress i{			er (sor >=0)
		gofreak;
			f_r = set_user(fdr, &fpt->rymsg_ion);
			if++t->ry;st
			so (err)
		gofeak;
			f++ta;
gramt	in
	fd2t(night(sock->file, fput_needed);
	}ms/We wily seturnedh error.
s fdt eta;
gramt we sile mo bum->r  */
	rc (erta;
gramt  0)
		return -Eta;
gramt	ireturn err;
}
EX*SCALL_DEFINE3(soce, mg(sprt, fd, struct sogghdr *muser *, msgg,
			 signed int, voin, &ayigned int, voags, {
	re (flags & ~(G_DOCG_DOCOMPAT		return -EINVAL;
	tyturn __socctondmsgg(st, stgg(sprvn, flags);
}

static stt so___sctocvmsg(socuct socket *sock, struct msghdr *muser *, g,
			  truct msghdr *msg,
_sctunsigned int cmags);unt bumec(s{
	struct socpatib_ghdr *muser *, g,
_cpatib s    kt(ruct socpatib_ghdr *muser *, )g;
	si-uct iovec *iv;
atick[UIOASYNTIOV]	si-uct iovec *iov,
 =iv;
atick;unsigned inng arcghd_ptr	int err, fptal nen;
, n;

	ms/Wernel prde podress is*/si-uct iockaddr_storage addres
	ms/Weer *,de podress isonter(nis*/si-uct iockaddr_smuser *, udres
	nt __user *, udr_stn)
	}		 (soG_DOCG_DOCOMPAT O_ags, {		ir = PTg_crenatib_ghdr *(g,
_sctung,
_cpatib			gre
		sor = PTcopy_ghdr *_om tser(skg,
_sctung,

}
e (err)
		goturn err;

	/* (erg,
_sctmsg_iovlen = > UIOASYNTIOV{
		err = -ENOG_DSIZE			e (!eg,
_sctmsg_iovlen = > UIOAMAXIOV{	gofto out_r;	err = -ENOMEM;
		gov,
 =ikmloc();g,
_sctmsg_iovlen = *;ze, ofk)uct msvec *)			      siGFP_RNEL_D{			e (!ervec{	gofto out_r;	e}	/* ChSe ite SOer *-de podress is(v *y(Srvec *ill calhge
	he
			 Wernel prdhdr *m a er te SOrnel prdress iscce b) */
	ifudr_s NUt__re  beid __user *)arg;
_sctmsg_iome =;ifudr_son ===mCOMPATANAMELENsg, )			 (soG_DOCG_DOCOMPAT O_ags, {		ir = PTv *y(Srenatib_vec *kg,
_sctunv, nrddres	rVERIFY_WRITE			gre
		sor = PTv *y(Srvec *kg,
_sctunv, nrddres	rVERIFY_WRITE			g (err < 0)
		goto out_reureev;
	mstal nen;
 sor, 
	ercghd_ptr NUtsigned inng a)g;
_sctmsg_iontrolle;
	g;
_sctmsg_ioags = flags;
 (O_G_DOCG_DOCEXEC ||G_DOCG_DOCOMPAT		}ms/We wiassumallowhrnel spce *iow
 the soce of inckaddr_storage ad
	msg->_sctmsg_iome =n ==0;
	mif (sock->skle->f_flags & O_NONBLOCK) ?
flags |= SKG_DONTWAIT :	msr = so(mec(s ?ock_crcvmsg(sosec(s :ock_crcvmsg(s)ock, msg, _sctu					 	   tal nen;
, ags &
	if (err < 0)
		goto out_reureev;
	msn;
 sor, 
	er (!tedr *)!NULL)
	
		err = move_addr_to_keer(fdddresu					 g->_sctmsg_iome =n =unsdresu					 udr_son =
		if (err >=0)
		gofto out_reureev;
	ms}
_r = se__t_user(fd(g;
_sctmsg_ioags = & ~G_DOCG_DOCOMPAT				   COMPATAFLAGSsg, )			g (err)
		goto out_fpureev;
	ms (soG_DOCG_DOCOMPAT O_ags, {		ir = PT__t_user(fd(signed inng a)g;
_sctmsg_iontrolle -rcghd_ptr0
		   &g,
_cpatibmsg_iontrollen =			gre
		sor = PT__t_user(fd(signed inng a)g;
_sctmsg_iontrolle -rcghd_ptr0
		   &g,
msg_iontrollen =			g (err)
		goto out_fpureev;
	msr = PTn)
	}	t_reureev;
:
r (sov,
 !=iv;
atick		rekureeov,

out:
	return err;
}

/*
 *	BinSD cvmsg(s ter(nfe
 * w/

ng ar__sctocvmsg(sot fd, struct fighdr *muser *, g,
	nsigned inags)
{
	int erut_needed);,rr;
	struct fighdr *mg;
_sct	inruct socket *sock;

	/*ck = sockfd_lookup_light(fd, &err, &fput_needed);
	if (soock)
		reto out;

	err = se___sctocvmsg(sock, msg, ms&g, _sctunags, NU0
	ifd2t(night(sock->file, fput_needed);
	}t:
	return err;
}
EX*SCALL_DEFINE3(socvmsg(sprt, fd, struct soghdr *muser *, msg, m		 signed int, voags, {
	re (flags & ~(G_DOCG_DOCOMPAT		return -EINVAL;
	tyturn __socctocvmsg(so, stg, msags);
}

st
 *	B   siLinux cvmsgg(i ter(nfe
 * w/

t __socctocvmsgg(itt fd, struct figghdr *muser *, gg, mssigned int,  vn,
				  nsigned int cmags);struct fime wec_c *me wt:
{
	int erut_needed);,rr;
,eta;
gramt	inruct socket *sock;

		ruct figghdr *muser *, t->ry;struct socpatib_gghdr *muser *, cpatib_t->ry;struct soghdr *mg;
_sct	inruct some wec_c dms_me w
	er (!tme wt:
=&&    ktll_lllect aet_flme wt:
(&dms_me w, me wt:
->tvet_c0
		      me wt:
->tvenc(s{		goturn -EINVAL;
	tifta;
gramt 0;
	mifck = sockfd_lookup_light(fd, &err, &fput_needed);
	if (soock)
		return err;

	/*r = sock_crror.
ock->sk);
		} (err)
	
			eta;
gramt 0;r;

		}to out_put_u	}
	rerr->ry==mgg;
	sicpatib_t->ry NUtruct socpatib_gghdr *muser *, )gg;
	s
	whe = (ta;
gramt < vn,
{_		er
	 *g WeNosed it a sskfLSMor a me weisathe first sota;
gram. *g W		if (waG_DOCG_DOCOMPAT O_ags, {_		errr = se___sctocvmsg(sock, ms(ruct soghdr *muser *, )cpatib_t->rys					 &a ds&g, _sctunags,  & ~G_DOIT :FORONEs					 &a dsta;
gramt{			er (sor >=0)
		gofreak;
			f_r = se__t_user(fdr, &fpcpatib_t->rymsg_ion);
			if++cpatib_t->ry;st
	 re
	 		errr = se___sctocvmsg(sock, m					 &a ds(ruct soghdr *muser *, )t->rys					 &a ds&g, _sctunags,  & ~G_DOIT :FORONEs					 &a dsta;
gramt{			er (sor >=0)
		gofreak;
			f_r = set_user(fdr, &fpt->rymsg_ion);
			if++t->ry;st
			so (err)
		gofeak;
			f++ta;
gramt	i	er
	 G_DOIT :FORONE rn es oneG_DONTWAIT : afr(n e wice t *so		if (waags & ~(G_DOIT :FORONE		gofags |= SKG_DONTWAIT :	m	if (wame wt:
{ 		errkme wot_u_tsame wt:
{			if*me wt:
 seme wec_c_sub(dms_me w, *me wt:
{			if (wame wt:
->tvet_c 0)
	
			er	me wt:
->tvet_c = me wt:
->tvenc(s 0;
			brreak;
			f_			so* Chee wt:
,eturnedhls to sathen = ta;
gramt o		iff (wame wt:
->tvenc(s 00) & (fme wt:
->tvet_c ==)
		gofreak;
			f}i	er
	 Ouof thbd ifna;
,eturnedhrht(f aw beo		if (wag, _sct.g_naags = ~(G_DOOOB		gofeak;
			}	re (err >===)
		goto out_rep;

		s (erta;
gramt =0) {
			eta;
gramt 0;r;

		}to out_put_u	}
	rer
	 * We wiy beturnedhls tot->rieto sathquested re(vn,
{_ifhe
			 Weck = is nonebck ind iftre siaret knengh */ta;
gramt..	 */
	if (!tr >=!-EAFNGAIN{_		er
	 *g We..	r rcf requmsg(s turnedsh error.
safr(n we *g Wereive adtso = ta;
gramt,ewn  we cacvmordhe a *g Weror.
s hacvrnedhodhe liw xtall th.
s fde a *g Weapp ssksh bo its  us

 et_ucketo, oSORR(nOR). *g W		ifck->sk);sk_prr = -ENr;

		}ut_put_u	fput(night(sock->file, fput_needed);
	}
eturn -Eta;
gramt	iEX*SCALL_DEFINE3(5(cvmsgg(iprt, fd, struct sogghdr *muser *, msgg,
			 signed int, voin, &ayigned int, voags, 			 ruct some wec_c user *, msme wt:
{
	int erta;
gramt	inruct some wec_c me wt:
_sct	ire (flags & ~(G_DOCG_DOCOMPAT		return -EINVAL;
	tif (soome wt:
{
	yturn __socctocvmsgg(st, stgg(sprvn, flags);NULL);
	ifd (wacopy_om tser(sk&me wt:
_sctmsme wt:
size, ofkme wt:
_sct)
		return -EINULT;
		ifta;
gramt 0;socctocvmsgg(st, stgg(sprvn, flags);NU&me wt:
_sct)
		s (erta;
gramt >) & (    ktcopy__keer(fdme wt:
si&me wt:
_sctmsze, ofkme wt:
_sct)
		reta;
gramt 0;INULT;
		ifturn -Eta;
gramt	iEX*fdef CO__ARCHOITNSYMBSOCKET, LL_D*
  Argunt itst isze, ;or a sctonket *ll */
		#finite AL(x) ((x) *;ze, ofksigned inng a))tatic stnst stsigned incha__nargs[21] 0;	inAL(0), AL(3), AL(3), AL(3), AL(2), AL(3),inAL(3), AL(3), AL(4), AL(4), AL(4), AL(6),inAL(6), AL(2), AL(5), AL(5), AL(3), AL(3),inAL(4), AL(5), AL(4)}}
st#derlf AL/*
 *	FoStem call */vted a
 *
 *	WeArgunt itcck tg ofear_n undp.hSe id 20%n a ze, .*	B  is function mawnest ksed it a s asen ornel lock hefocau
 *	c t is bos asbyhe solled e
 */

SYSCALL_DEFINE4(2(nket *ll *prt, fdll *prsigned inng aruser *, umdr, {
	resigned inng ara[AUDITSC_ARGS];unsigned inng ara0umd1	int err, ;unsigned int bun)
	}		 (soll */< 1| tyll */> MBSOCENDMG_D		return -EINVAL;
	tifn;
 sonargs[ll *]		} (ern ==>ize, ofka{		goturn -EINVAL;
	tif/* copy_om tser(s shouldum->SMP safe./
	if (!tcopy_om tser(skaumdr, sin, )
	return -EINULT;
		ifr = -Edit_fdnket *ll *(nargs[ll *] /;ze, ofksigned inng a)umd
}
e (err)
		goturn err;

	/*a0 -Ed[0]		}a1 -Ed[1]	mifctch (holl *{
	case SOCBSOCKET, :	sor = PTsctonket *(a0umd1umd[2;
	ifreak;
	case SOCBSOND, :	sor = PTsctond(soa0umtruct sockaddr *)user *, )d1umd[2;
	ifreak;
	case SOCBSOCONNEC :	sor = PTsctoenectedoa0umtruct sockaddr *)user *, )d1umd[2;
	ifreak;
	case SOCBSOSTEN, :	sor = PTsctosten(soa0umd1
	ifreak;
	case SOCBSOACCEP :	sor = PTsctocept, 4da0umtruct sockaddr *)user *, )d1u
		    (t __user *, )d[2;0);
}
ereak;
	case SOCBSOGETCKETNAME:	sor = P
    ktcctot_ucketme, oa0umtruct sockaddr *)user *, )d1u
		      (t __user *, )d[2;
}
ereak;
	case SOCBSOGETPEERNAME:	sor = P
    ktcctot_upeerme, oa0umtruct sockaddr *)user *, )d1u
		      (t __user *, )d[2;
}
ereak;
	case SOCBSOCKET, PAIR:	sor = PTsctonket *ir(fda0umd1umd[2;, (t __user *, )d[3;
}
ereak;
	case SOCBSOCE, :	sor = PTsctondmsoa0umtid __user *)ard1umd[2;, d[3;
}
ereak;
	case SOCBSOCE, TO:	sor = PTsctondmstooa0umtid __user *)ard1umd[2;, d[3;u
		   truct sockaddr *)user *, )d[4;, d[5;
}
ereak;
	case SOCBSORECV:	sor = PTsctocvmsoa0umtid __user *)ard1umd[2;, d[3;
}
ereak;
	case SOCBSORECVFROM:	sor = PTsctocvmsom tda0umtid __user *)ard1umd[2;, d[3;u
		   (struct sockaddr *)user *, )d[4;,
		   (stt __user *, )d[5;
}
ereak;
	case SOCBSOSHUTDOW :	sor = PTsctonhutwn
	oa0umd1
	ifreak;
	case SOCBSOSETCKETOP :	sor = PTsctonducketo, oa0umd1umd[2;, (cha__user *, )d[3;umd[4;
}
ereak;
	case SOCBSOGETCKETOP :	sor = P
    ktcctot_ucketo, oa0umd1umd[2;, (cha__user *, )d[3;u
		   (stt __user *, )d[4;
}
ereak;
	case SOCBSOCENDMSG:	sor = PTsctondmsg(sta0umtruct soghdr *muser *, )d1umd[2;
	ifreak;
	case SOCBSOCENDMG_D:	sor = PTsctondmsgg(sta0umtruct sogghdr *muser *, )d1umd[2;, d[3;
}
ereak;
	case SOCBSORECVG_D:	sor = PTsctocvmsg(soa0umtruct soghdr *muser *, )d1umd[2;
	ifreak;
	case SOCBSORECVGG_D:	sor = PTsctocvmsgg(sta0umtruct sogghdr *muser *, )d1umd[2;, d[3;u
		   (struct some wec_c user *, )d[4;
}
ereak;
	case SOCBSOACCEP 4:	sor = PTsctocept, 4da0umtruct sockaddr *)user *, )d1u
		    (t __user *, )d[2;0)d[3;
}
ereak;
	cafault.
:	sor = PTINVAL;
	tyfeak;
			}	eturn err;
}
EX*ndif

	so* Ch__ARCHOITNSYMBSOCKET, LL_D/

SY/* *	Weck_crcvgten(r - sddhsocket. iotocol mondle tr*	We@ops:escriptorn maofrotocol
	 
 *	Weis function ma fulled by moaiotocol mondle trhat worts fo
 *	anadverte mos redress isoily, tyd ifve its inbLk int bohe s*	socket. iter(nfe
 .he sovalusoopsfilily iscorsponsid;o hae s*	socket. istem call */otocol molily i */

St sock_crcvgten(r(nst struct sot_nsotoco_lily is*ops{
	stt err, 
	/* (ero,sfilily is>= APROTO{_		erpr_iptt("otocol mo%ds>= APROTO(%dn",
	oopsfilily i, APROTO{;	return -EINIOBUFS
	

	socpinock();&t_familieyock()
}
e (eru_dereference(n_otected bet_families[faopsfilily i;u
		   (ssssck()dep_is_ld -;&t_familieyock()
))	sor = PTINEXTEN		gre
	_		eru_accsgnedointer(net_families[faopsfilily i;uoops
			ir = P