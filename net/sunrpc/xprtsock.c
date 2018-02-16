/*
 * linux/net/sunrpc/xprtsock.c
 *
 * Client-side transport implementation for sockets.
 *
 * TCP callback races fixes (C) 1998 Red Hat
 * TCP send fixes (C) 1998 Red Hat
 * TCP NFS related read + write fixes
 *  (C) 1999 Dave Airlie, University of Limerick, Ireland <airlied@linux.ie>
 *
 * Rewrite of larges part of the code in order to stabilize TCP stuff.
 * Fix behaviour when socket buffer is full.
 *  (C) 1999 Trond Myklebust <trond.myklebust@fys.uio.no>
 *
 * IP socket transport implementation, (C) 2005 Chuck Lever <cel@netapp.com>
 *
 * IPv6 support contributed by Gilles Quillard, Bull Open Source, 2005.
 *   <gilles.quillard@bull.net>
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/capability.h>
#include <linux/pagemap.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/un.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/addr.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/xprtsock.h>
#include <linux/file.h>
#ifdef CONFIG_SUNRPC_BACKCHANNEL
#include <linux/sunrpc/bc_xprt.h>
#endif

#include <net/sock.h>
#include <net/checksum.h>
#include <net/udp.h>
#include <net/tcp.h>

#include <trace/events/sunrpc.h>

#include "sunrpc.h"

static void xs_close(struct rpc_xprt *xprt);

/*
 * xprtsock tunables
 */
static unsigned int xprt_udp_slot_table_entries = RPC_DEF_SLOT_TABLE;
static unsigned int xprt_tcp_slot_table_entries = RPC_MIN_SLOT_TABLE;
static unsigned int xprt_max_tcp_slot_table_entries = RPC_MAX_SLOT_TABLE;

static unsigned int xprt_min_resvport = RPC_DEF_MIN_RESVPORT;
static unsigned int xprt_max_resvport = RPC_DEF_MAX_RESVPORT;

#define XS_TCP_LINGER_TO	(15U * HZ)
static unsigned int xs_tcp_fin_timeout __read_mostly = XS_TCP_LINGER_TO;

/*
 * We can register our own files under /proc/sys/sunrpc by
 * calling register_sysctl_table() again.  The files in that
 * directory become the union of all files registered there.
 *
 * We simply need to make sure that we don't collide with
 * someone else's file names!
 */

#ifdef RPC_DEBUG

static unsigned int min_slot_table_size = RPC_MIN_SLOT_TABLE;
static unsigned int max_slot_table_size = RPC_MAX_SLOT_TABLE;
static unsigned int max_tcp_slot_table_limit = RPC_MAX_SLOT_TABLE_LIMIT;
static unsigned int xprt_min_resvport_limit = RPC_MIN_RESVPORT;
static unsigned int xprt_max_resvport_limit = RPC_MAX_RESVPORT;

static struct ctl_table_header *sunrpc_table_header;

/*
 * FIXME: changing the UDP slot table size should also resize the UDP
 *        socket buffers for existing UDP transports
 */
static struct ctl_table xs_tunables_table[] = {
	{
		.procname	= "udp_slot_table_entries",
		.data		= &xprt_udp_slot_table_entries,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_slot_table_size,
		.extra2		= &max_slot_table_size
	},
	{
		.procname	= "tcp_slot_table_entries",
		.data		= &xprt_tcp_slot_table_entries,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_slot_table_size,
		.extra2		= &max_slot_table_size
	},
	{
		.procname	= "tcp_max_slot_table_entries",
		.data		= &xprt_max_tcp_slot_table_entries,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_slot_table_size,
		.extra2		= &max_tcp_slot_table_limit
	},
	{
		.procname	= "min_resvport",
		.data		= &xprt_min_resvport,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &xprt_min_resvport_limit,
		.extra2		= &xprt_max_resvport_limit
	},
	{
		.procname	= "max_resvport",
		.data		= &xprt_max_resvport,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &xprt_min_resvport_limit,
		.extra2		= &xprt_max_resvport_limit
	},
	{
		.procname	= "tcp_fin_timeout",
		.data		= &xs_tcp_fin_timeout,
		.maxlen		= sizeof(xs_tcp_fin_timeout),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{ },
};

static struct ctl_table sunrpc_table[] = {
	{
		.procname	= "sunrpc",
		.mode		= 0555,
		.child		= xs_tunables_table
	},
	{ },
};

#endif

/*
 * Wait duration for a reply from the RPC portmapper.
 */
#define XS_BIND_TO		(60U * HZ)

/*
 * Delay if a UDP socket connect error occurs.  This is most likely some
 * kind of resource problem on the local host.
 */
#define XS_UDP_REEST_TO		(2U * HZ)

/*
 * The reestablish timeout allows clients to delay for a bit before attempting
 * to reconnect to a server that just dropped our connection.
 *
 * We implement an exponential backoff when trying to reestablish a TCP
 * transport connection with the server.  Some servers like to drop a TCP
 * connection when they are overworked, so we start with a short timeout and
 * increase over time if the server is down or not responding.
 */
#define XS_TCP_INIT_REEST_TO	(3U * HZ)
#define XS_TCP_MAX_REEST_TO	(5U * 60 * HZ)

/*
 * TCP idle timeout; client drops the transport socket if it is idle
 * for this long.  Note that we also timeout UDP sockets to prevent
 * holding port numbers when there is no RPC traffic.
 */
#define XS_IDLE_DISC_TO		(5U * 60 * HZ)

#ifdef RPC_DEBUG
# undef  RPC_DEBUG_DATA
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

#ifdef RPC_DEBUG_DATA
static void xs_pktdump(char *msg, u32 *packet, unsigned int count)
{
	u8 *buf = (u8 *) packet;
	int j;

	dprintk("RPC:       %s\n", msg);
	for (j = 0; j < count && j < 128; j += 4) {
		if (!(j & 31)) {
			if (j)
				dprintk("\n");
			dprintk("0x%04x ", j);
		}
		dprintk("%02x%02x%02x%02x ",
			buf[j], buf[j+1], buf[j+2], buf[j+3]);
	}
	dprintk("\n");
}
#else
static inline void xs_pktdump(char *msg, u32 *packet, unsigned int count)
{
	/* NOP */
}
#endif

struct sock_xprt {
	struct rpc_xprt		xprt;

	/*
	 * Network layer
	 */
	struct socket *		sock;
	struct sock *		inet;

	/*
	 * State of TCP reply receive
	 */
	__be32			tcp_fraghdr,
				tcp_xid,
				tcp_calldir;

	u32			tcp_offset,
				tcp_reclen;

	unsigned long		tcp_copied,
				tcp_flags;

	/*
	 * Connection of transports
	 */
	struct delayed_work	connect_worker;
	struct sockaddr_storage	srcaddr;
	unsigned short		srcport;

	/*
	 * UDP socket buffer size parameters
	 */
	size_t			rcvsize,
				sndsize;

	/*
	 * Saved socket callback addresses
	 */
	void			(*old_data_ready)(struct sock *);
	void			(*old_state_change)(struct sock *);
	void			(*old_write_space)(struct sock *);
	void			(*old_error_report)(struct sock *);
};

/*
 * TCP receive state flags
 */
#define TCP_RCV_LAST_FRAG	(1UL << 0)
#define TCP_RCV_COPY_FRAGHDR	(1UL << 1)
#define TCP_RCV_COPY_XID	(1UL << 2)
#define TCP_RCV_COPY_DATA	(1UL << 3)
#define TCP_RCV_READ_CALLDIR	(1UL << 4)
#define TCP_RCV_COPY_CALLDIR	(1UL << 5)

/*
 * TCP RPC flags
 */
#define TCP_RPC_REPLY		(1UL << 6)

static inline struct rpc_xprt *xprt_from_sock(struct sock *sk)
{
	return (struct rpc_xprt *) sk->sk_user_data;
}

static inline struct sockaddr *xs_addr(struct rpc_xprt *xprt)
{
	return (struct sockaddr *) &xprt->addr;
}

static inline struct sockaddr_un *xs_addr_un(struct rpc_xprt *xprt)
{
	return (struct sockaddr_un *) &xprt->addr;
}

static inline struct sockaddr_in *xs_addr_in(struct rpc_xprt *xprt)
{
	return (struct sockaddr_in *) &xprt->addr;
}

static inline struct sockaddr_in6 *xs_addr_in6(struct rpc_xprt *xprt)
{
	return (struct sockaddr_in6 *) &xprt->addr;
}

static void xs_format_common_peer_addresses(struct rpc_xprt *xprt)
{
	struct sockaddr *sap = xs_addr(xprt);
	struct sockaddr_in6 *sin6;
	struct sockaddr_in *sin;
	struct sockaddr_un *sun;
	char buf[128];

	switch (sap->sa_family) {
	case AF_LOCAL:
		sun = xs_addr_un(xprt);
		strlcpy(buf, sun->sun_path, sizeof(buf));
		xprt->address_strings[RPC_DISPLAY_ADDR] =
						kstrdup(buf, GFP_KERNEL);
		break;
	case AF_INET:
		(void)rpc_ntop(sap, buf, sizeof(buf));
		xprt->address_strings[RPC_DISPLAY_ADDR] =
						kstrdup(buf, GFP_KERNEL);
		sin = xs_addr_in(xprt);
		snprintf(buf, sizeof(buf), "%08x", ntohl(sin->sin_addr.s_addr));
		break;
	case AF_INET6:
		(void)rpc_ntop(sap, buf, sizeof(buf));
		xprt->address_strings[RPC_DISPLAY_ADDR] =
						kstrdup(buf, GFP_KERNEL);
		sin6 = xs_addr_in6(xprt);
		snprintf(buf, sizeof(buf), "%pi6", &sin6->sin6_addr);
		break;
	default:
		BUG();
	}

	xprt->address_strings[RPC_DISPLAY_HEX_ADDR] = kstrdup(buf, GFP_KERNEL);
}

static void xs_format_common_peer_ports(struct rpc_xprt *xprt)
{
	struct sockaddr *sap = xs_addr(xprt);
	char buf[128];

	snprintf(buf, sizeof(buf), "%u", rpc_get_port(sap));
	xprt->address_strings[RPC_DISPLAY_PORT] = kstrdup(buf, GFP_KERNEL);

	snprintf(buf, sizeof(buf), "%4hx", rpc_get_port(sap));
	xprt->address_strings[RPC_DISPLAY_HEX_PORT] = kstrdup(buf, GFP_KERNEL);
}

static void xs_format_peer_addresses(struct rpc_xprt *xprt,
				     const char *protocol,
				     const char *netid)
{
	xprt->address_strings[RPC_DISPLAY_PROTO] = protocol;
	xprt->address_strings[RPC_DISPLAY_NETID] = netid;
	xs_format_common_peer_addresses(xprt);
	xs_format_common_peer_ports(xprt);
}

static void xs_update_peer_port(struct rpc_xprt *xprt)
{
	kfree(xprt->address_strings[RPC_DISPLAY_HEX_PORT]);
	kfree(xprt->address_strings[RPC_DISPLAY_PORT]);

	xs_format_common_peer_ports(xprt);
}

static void xs_free_peer_addresses(struct rpc_xprt *xprt)
{
	unsigned int i;

	for (i = 0; i < RPC_DISPLAY_MAX; i++)
		switch (i) {
		case RPC_DISPLAY_PROTO:
		case RPC_DISPLAY_NETID:
			continue;
		default:
			kfree(xprt->address_strings[i]);
		}
}

#define XS_SENDMSG_FLAGS	(MSG_DONTWAIT | MSG_NOSIGNAL)

static int xs_send_kvec(struct socket *sock, struct sockaddr *addr, int addrlen, struct kvec *vec, unsigned int base, int more)
{
	struct msghdr msg = {
		.msg_name	= addr,
		.msg_namelen	= addrlen,
		.msg_flags	= XS_SENDMSG_FLAGS | (more ? MSG_MORE : 0),
	};
	struct kvec iov = {
		.iov_base	= vec->iov_base + base,
		.iov_len	= vec->iov_len - base,
	};

	if (iov.iov_len != 0)
		return kernel_sendmsg(sock, &msg, &iov, 1, iov.iov_len);
	return kernel_sendmsg(sock, &msg, NULL, 0, 0);
}

static int xs_send_pagedata(struct socket *sock, struct xdr_buf *xdr, unsigned int base, int more, bool zerocopy, int *sent_p)
{
	ssize_t (*do_sendpage)(struct socket *sock, struct page *page,
			int offset, size_t size, int flags);
	struct page **ppage;
	unsigned int remainder;
	int err;

	remainder = xdr->page_len - base;
	base += xdr->page_base;
	ppage = xdr->pages + (base >> PAGE_SHIFT);
	base &= ~PAGE_MASK;
	do_sendpage = sock->ops->sendpage;
	if (!zerocopy)
		do_sendpage = sock_no_sendpage;
	for(;;) {
		unsigned int len = min_t(unsigned int, PAGE_SIZE - base, remainder);
		int flags = XS_SENDMSG_FLAGS;

		remainder -= len;
		if (remainder != 0 || more)
			flags |= MSG_MORE;
		err = do_sendpage(sock, *ppage, base, len, flags);
		if (remainder == 0 || err != len)
			break;
		*sent_p += err;
		ppage++;
		base = 0;
	}
	if (err > 0) {
		*sent_p += err;
		err = 0;
	}
	return err;
}

/**
 * xs_sendpages - write pages directly to a socket
 * @sock: socket to send on
 * @addr: UDP only -- address of destination
 * @addrlen: UDP only -- length of destination address
 * @xdr: buffer containing this request
 * @base: starting position in the buffer
 * @zerocopy: true if it is safe to use sendpage()
 * @sent_p: return the total number of bytes successfully queued for sending
 *
 */
static int xs_sendpages(struct socket *sock, struct sockaddr *addr, int addrlen, struct xdr_buf *xdr, unsigned int base, bool zerocopy, int *sent_p)
{
	unsigned int remainder = xdr->len - base;
	int err = 0;
	int sent = 0;

	if (unlikely(!sock))
		return -ENOTSOCK;

	clear_bit(SOCK_ASYNC_NOSPACE, &sock->flags);
	if (base != 0) {
		addr = NULL;
		addrlen = 0;
	}

	if (base < xdr->head[0].iov_len || addr != NULL) {
		unsigned int len = xdr->head[0].iov_len - base;
		remainder -= len;
		err = xs_send_kvec(sock, addr, addrlen, &xdr->head[0], base, remainder != 0);
		if (remainder == 0 || err != len)
			goto out;
		*sent_p += err;
		base = 0;
	} else
		base -= xdr->head[0].iov_len;

	if (base < xdr->page_len) {
		unsigned int len = xdr->page_len - base;
		remainder -= len;
		err = xs_send_pagedata(sock, xdr, base, remainder != 0, zerocopy, &sent);
		*sent_p += sent;
		if (remainder == 0 || sent != len)
			goto out;
		base = 0;
	} else
		base -= xdr->page_len;

	if (base >= xdr->tail[0].iov_len)
		return 0;
	err = xs_send_kvec(sock, NULL, 0, &xdr->tail[0], base, 0);
out:
	if (err > 0) {
		*sent_p += err;
		err = 0;
	}
	return err;
}

static void xs_nospace_callback(struct rpc_task *task)
{
	struct sock_xprt *transport = container_of(task->tk_rqstp->rq_xprt, struct sock_xprt, xprt);

	transport->inet->sk_write_pending--;
	clear_bit(SOCK_ASYNC_NOSPACE, &transport->sock->flags);
}

/**
 * xs_nospace - place task on wait queue if transmit was incomplete
 * @task: task to put to sleep
 *
 */
static int xs_nospace(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct sock *sk = transport->inet;
	int ret = -EAGAIN;

	dprintk("RPC: %5u xmit incomplete (%u left of %u)\n",
			task->tk_pid, req->rq_slen - req->rq_bytes_sent,
			req->rq_slen);

	/* Protect against races with write_space */
	spin_lock_bh(&xprt->transport_lock);

	/* Don't race with disconnect */
	if (xprt_connected(xprt)) {
		if (test_bit(SOCK_ASYNC_NOSPACE, &transport->sock->flags)) {
			/*
			 * Notify TCP that we're limited by the application
			 * window size
			 */
			set_bit(SOCK_NOSPACE, &transport->sock->flags);
			sk->sk_write_pending++;
			/* ...and wait for more buffer space */
			xprt_wait_for_buffer_space(task, xs_nospace_callback);
		}
	} else {
		clear_bit(SOCK_ASYNC_NOSPACE, &transport->sock->flags);
		ret = -ENOTCONN;
	}

	spin_unlock_bh(&xprt->transport_lock);

	/* Race breaker in case memory is freed before above code is called */
	sk->sk_write_space(sk);
	return ret;
}

/*
 * Construct a stream transport record marker in @buf.
 */
static inline void xs_encode_stream_record_marker(struct xdr_buf *buf)
{
	u32 reclen = buf->len - sizeof(rpc_fraghdr);
	rpc_fraghdr *base = buf->head[0].iov_base;
	*base = cpu_to_be32(RPC_LAST_STREAM_FRAGMENT | reclen);
}

/**
 * xs_local_send_request - write an RPC request to an AF_LOCAL socket
 * @task: RPC task that manages the state of an RPC request
 *
 * Return values:
 *        0:	The request has been sent
 *   EAGAIN:	The socket was blocked, please call again later to
 *		complete the request
 * ENOTCONN:	Caller needs to invoke connect logic then call again
 *    other:	Some other error occured, the request was not sent
 */
static int xs_local_send_request(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;
	struct sock_xprt *transport =
				container_of(xprt, struct sock_xprt, xprt);
	struct xdr_buf *xdr = &req->rq_snd_buf;
	int status;
	int sent = 0;

	xs_encode_stream_record_marker(&req->rq_snd_buf);

	xs_pktdump("packet data:",
			req->rq_svec->iov_base, req->rq_svec->iov_len);

	status = xs_sendpages(transport->sock, NULL, 0, xdr, req->rq_bytes_sent,
			      true, &sent);
	dprintk("RPC:       %s(%u) = %d\n",
			__func__, xdr->len - req->rq_bytes_sent, status);
	if (likely(sent > 0) || status == 0) {
		req->rq_bytes_sent += sent;
		req->rq_xmit_bytes_sent += sent;
		if (likely(req->rq_bytes_sent >= req->rq_slen)) {
			req->rq_bytes_sent = 0;
			return 0;
		}
		status = -EAGAIN;
	}

	switch (status) {
	case -ENOBUFS:
	case -EAGAIN:
		status = xs_nospace(task);
		break;
	default:
		dprintk("RPC:       sendmsg returned unrecognized error %d\n",
			-status);
	case -EPIPE:
		xs_close(xprt);
		status = -ENOTCONN;
	}

	return status;
}

/**
 * xs_udp_send_request - write an RPC request to a UDP socket
 * @task: address of RPC task that manages the state of an RPC request
 *
 * Return values:
 *        0:	The request has been sent
 *   EAGAIN:	The socket was blocked, please call again later to
 *		complete the request
 * ENOTCONN:	Caller needs to invoke connect logic then call again
 *    other:	Some other error occurred, the request was not sent
 */
static int xs_udp_send_request(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct xdr_buf *xdr = &req->rq_snd_buf;
	int sent = 0;
	int status;

	xs_pktdump("packet data:",
				req->rq_svec->iov_base,
				req->rq_svec->iov_len);

	if (!xprt_bound(xprt))
		return -ENOTCONN;
	status = xs_sendpages(transport->sock, xs_addr(xprt), xprt->addrlen,
			      xdr, req->rq_bytes_sent, true, &sent);

	dprintk("RPC:       xs_udp_send_request(%u) = %d\n",
			xdr->len - req->rq_bytes_sent, status);

	/* firewall is blocking us, don't return -EAGAIN or we end up looping */
	if (status == -EPERM)
		goto process_status;

	if (sent > 0 || status == 0) {
		req->rq_xmit_bytes_sent += sent;
		if (sent >= req->rq_slen)
			return 0;
		/* Still some bytes left; set up for a retry later. */
		status = -EAGAIN;
	}

process_status:
	switch (status) {
	case -ENOTSOCK:
		status = -ENOTCONN;
		/* Should we call xs_close() here? */
		break;
	case -EAGAIN:
		status = xs_nospace(task);
		break;
	default:
		dprintk("RPC:       sendmsg returned unrecognized error %d\n",
			-status);
	case -ENETUNREACH:
	case -ENOBUFS:
	case -EPIPE:
	case -ECONNREFUSED:
	case -EPERM:
		/* When the server has died, an ICMP port unreachable message
		 * prompts ECONNREFUSED. */
		clear_bit(SOCK_ASYNC_NOSPACE, &transport->sock->flags);
	}

	return status;
}

/**
 * xs_tcp_shutdown - gracefully shut down a TCP socket
 * @xprt: transport
 *
 * Initiates a graceful shutdown of the TCP socket by calling the
 * equivalent of shutdown(SHUT_WR);
 */
static void xs_tcp_shutdown(struct rpc_xprt *xprt)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct socket *sock = transport->sock;

	if (sock != NULL) {
		kernel_sock_shutdown(sock, SHUT_WR);
		trace_rpc_socket_shutdown(xprt, sock);
	}
}

/**
 * xs_tcp_send_request - write an RPC request to a TCP socket
 * @task: address of RPC task that manages the state of an RPC request
 *
 * Return values:
 *        0:	The request has been sent
 *   EAGAIN:	The socket was blocked, please call again later to
 *		complete the request
 * ENOTCONN:	Caller needs to invoke connect logic then call again
 *    other:	Some other error occurred, the request was not sent
 *
 * XXX: In the case of soft timeouts, should we eventually give up
 *	if sendmsg is not able to make progress?
 */
static int xs_tcp_send_request(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct xdr_buf *xdr = &req->rq_snd_buf;
	bool zerocopy = true;
	int status;
	int sent;

	xs_encode_stream_record_marker(&req->rq_snd_buf);

	xs_pktdump("packet data:",
				req->rq_svec->iov_base,
				req->rq_svec->iov_len);
	/* Don't use zero copy if this is a resend. If the RPC call
	 * completes while the socket holds a reference to the pages,
	 * then we may end up resending corrupted data.
	 */
	if (task->tk_flags & RPC_TASK_SENT)
		zerocopy = false;

	/* Continue transmitting the packet/record. We must be careful
	 * to cope with writespace callbacks arriving _after_ we have
	 * called sendmsg(). */
	while (1) {
		sent = 0;
		status = xs_sendpages(transport->sock, NULL, 0, xdr,
				      req->rq_bytes_sent, zerocopy, &sent);

		dprintk("RPC:       xs_tcp_send_request(%u) = %d\n",
				xdr->len - req->rq_bytes_sent, status);

		if (unlikely(sent == 0 && status < 0))
			break;

		/* If we've sent the entire packet, immediately
		 * reset the count of bytes sent. */
		req->rq_bytes_sent += sent;
		req->rq_xmit_bytes_sent += sent;
		if (likely(req->rq_bytes_sent >= req->rq_slen)) {
			req->rq_bytes_sent = 0;
			return 0;
		}

		if (sent != 0)
			continue;
		status = -EAGAIN;
		break;
	}

	switch (status) {
	case -ENOTSOCK:
		status = -ENOTCONN;
		/* Should we call xs_close() here? */
		break;
	case -ENOBUFS:
	case -EAGAIN:
		status = xs_nospace(task);
		break;
	default:
		dprintk("RPC:       sendmsg returned unrecognized error %d\n",
			-status);
	case -ECONNRESET:
		xs_tcp_shutdown(xprt);
	case -ECONNREFUSED:
	case -ENOTCONN:
	case -EPIPE:
		clear_bit(SOCK_ASYNC_NOSPACE, &transport->sock->flags);
	}

	return status;
}

/**
 * xs_tcp_release_xprt - clean up after a tcp transmission
 * @xprt: transport
 * @task: rpc task
 *
 * This cleans up if an error causes us to abort the transmission of a request.
 * In this case, the socket may need to be reset in order to avoid confusing
 * the server.
 */
static void xs_tcp_release_xprt(struct rpc_xprt *xprt, struct rpc_task *task)
{
	struct rpc_rqst *req;

	if (task != xprt->snd_task)
		return;
	if (task == NULL)
		goto out_release;
	req = task->tk_rqstp;
	if (req == NULL)
		goto out_release;
	if (req->rq_bytes_sent == 0)
		goto out_release;
	if (req->rq_bytes_sent == req->rq_snd_buf.len)
		goto out_release;
	set_bit(XPRT_CLOSE_WAIT, &xprt->state);
out_release:
	xprt_release_xprt(xprt, task);
}

static void xs_save_old_callbacks(struct sock_xprt *transport, struct sock *sk)
{
	transport->old_data_ready = sk->sk_data_ready;
	transport->old_state_change = sk->sk_state_change;
	transport->old_write_space = sk->sk_write_space;
	transport->old_error_report = sk->sk_error_report;
}

static void xs_restore_old_callbacks(struct sock_xprt *transport, struct sock *sk)
{
	sk->sk_data_ready = transport->old_data_ready;
	sk->sk_state_change = transport->old_state_change;
	sk->sk_write_space = transport->old_write_space;
	sk->sk_error_report = transport->old_error_report;
}

/**
 * xs_error_report - callback to handle TCP socket state errors
 * @sk: socket
 *
 * Note: we don't call sock_error() since there may be a rpc_task
 * using the socket, and so we don't want to clear sk->sk_err.
 */
static void xs_error_report(struct sock *sk)
{
	struct rpc_xprt *xprt;
	int err;

	read_lock_bh(&sk->sk_callback_lock);
	if (!(xprt = xprt_from_sock(sk)))
		goto out;

	err = -sk->sk_err;
	if (err == 0)
		goto out;
	dprintk("RPC:       xs_error_report client %p, error=%d...\n",
			xprt, -err);
	trace_rpc_socket_error(xprt, sk->sk_socket, err);
	if (test_bit(XPRT_CONNECTION_REUSE, &xprt->state))
		goto out;
	xprt_wake_pending_tasks(xprt, err);
 out:
	read_unlock_bh(&sk->sk_callback_lock);
}

static void xs_reset_transport(struct sock_xprt *transport)
{
	struct socket *sock = transport->sock;
	struct sock *sk = transport->inet;

	if (sk == NULL)
		return;

	transport->srcport = 0;

	write_lock_bh(&sk->sk_callback_lock);
	transport->inet = NULL;
	transport->sock = NULL;

	sk->sk_user_data = NULL;

	xs_restore_old_callbacks(transport, sk);
	write_unlock_bh(&sk->sk_callback_lock);

	trace_rpc_socket_close(&transport->xprt, sock);
	sock_release(sock);
}

/**
 * xs_close - close a socket
 * @xprt: transport
 *
 * This is used when all requests are complete; ie, no DRC state remains
 * on the server we want to save.
 *
 * The caller _must_ be holding XPRT_LOCKED in order to avoid issues with
 * xs_reset_transport() zeroing the socket from underneath a writer.
 */
static void xs_close(struct rpc_xprt *xprt)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);

	dprintk("RPC:       xs_close xprt %p\n", xprt);

	cancel_delayed_work_sync(&transport->connect_worker);

	xs_reset_transport(transport);
	xprt->reestablish_timeout = 0;

	smp_mb__before_atomic();
	clear_bit(XPRT_CONNECTION_ABORT, &xprt->state);
	clear_bit(XPRT_CLOSE_WAIT, &xprt->state);
	clear_bit(XPRT_CLOSING, &xprt->state);
	smp_mb__after_atomic();
	xprt_disconnect_done(xprt);
}

static void xs_tcp_close(struct rpc_xprt *xprt)
{
	if (test_and_clear_bit(XPRT_CONNECTION_CLOSE, &xprt->state))
		xs_close(xprt);
	else
		xs_tcp_shutdown(xprt);
}

static void xs_xprt_free(struct rpc_xprt *xprt)
{
	xs_free_peer_addresses(xprt);
	xprt_free(xprt);
}

/**
 * xs_destroy - prepare to shutdown a transport
 * @xprt: doomed transport
 *
 */
static void xs_destroy(struct rpc_xprt *xprt)
{
	dprintk("RPC:       xs_destroy xprt %p\n", xprt);

	xs_close(xprt);
	xs_xprt_free(xprt);
	module_put(THIS_MODULE);
}

static int xs_local_copy_to_xdr(struct xdr_buf *xdr, struct sk_buff *skb)
{
	struct xdr_skb_reader desc = {
		.skb		= skb,
		.offset		= sizeof(rpc_fraghdr),
		.count		= skb->len - sizeof(rpc_fraghdr),
	};

	if (xdr_partial_copy_from_skb(xdr, 0, &desc, xdr_skb_read_bits) < 0)
		return -1;
	if (desc.count)
		return -1;
	return 0;
}

/**
 * xs_local_data_ready - "data ready" callback for AF_LOCAL sockets
 * @sk: socket with data to read
 * @len: how much data to read
 *
 * Currently this assumes we can read the whole reply in a single gulp.
 */
static void xs_local_data_ready(struct sock *sk)
{
	struct rpc_task *task;
	struct rpc_xprt *xprt;
	struct rpc_rqst *rovr;
	struct sk_buff *skb;
	int err, repsize, copied;
	u32 _xid;
	__be32 *xp;

	read_lock_bh(&sk->sk_callback_lock);
	dprintk("RPC:       %s...\n", __func__);
	xprt = xprt_from_sock(sk);
	if (xprt == NULL)
		goto out;

	skb = skb_recv_datagram(sk, 0, 1, &err);
	if (skb == NULL)
		goto out;

	repsize = skb->len - sizeof(rpc_fraghdr);
	if (repsize < 4) {
		dprintk("RPC:       impossible RPC reply size %d\n", repsize);
		goto dropit;
	}

	/* Copy the XID from the skb... */
	xp = skb_header_pointer(skb, sizeof(rpc_fraghdr), sizeof(_xid), &_xid);
	if (xp == NULL)
		goto dropit;

	/* Look up and lock the request corresponding to the given XID */
	spin_lock(&xprt->transport_lock);
	rovr = xprt_lookup_rqst(xprt, *xp);
	if (!rovr)
		goto out_unlock;
	task = rovr->rq_task;

	copied = rovr->rq_private_buf.buflen;
	if (copied > repsize)
		copied = repsize;

	if (xs_local_copy_to_xdr(&rovr->rq_private_buf, skb)) {
		dprintk("RPC:       sk_buff copy failed\n");
		goto out_unlock;
	}

	xprt_complete_rqst(task, copied);

 out_unlock:
	spin_unlock(&xprt->transport_lock);
 dropit:
	skb_free_datagram(sk, skb);
 out:
	read_unlock_bh(&sk->sk_callback_lock);
}

/**
 * xs_udp_data_ready - "data ready" callback for UDP sockets
 * @sk: socket with data to read
 * @len: how much data to read
 *
 */
static void xs_udp_data_ready(struct sock *sk)
{
	struct rpc_task *task;
	struct rpc_xprt *xprt;
	struct rpc_rqst *rovr;
	struct sk_buff *skb;
	int err, repsize, copied;
	u32 _xid;
	__be32 *xp;

	read_lock_bh(&sk->sk_callback_lock);
	dprintk("RPC:       xs_udp_data_ready...\n");
	if (!(xprt = xprt_from_sock(sk)))
		goto out;

	if ((skb = skb_recv_datagram(sk, 0, 1, &err)) == NULL)
		goto out;

	repsize = skb->len - sizeof(struct udphdr);
	if (repsize < 4) {
		dprintk("RPC:       impossible RPC reply size %d!\n", repsize);
		goto dropit;
	}

	/* Copy the XID from the skb... */
	xp = skb_header_pointer(skb, sizeof(struct udphdr),
				sizeof(_xid), &_xid);
	if (xp == NULL)
		goto dropit;

	/* Look up and lock the request corresponding to the given XID */
	spin_lock(&xprt->transport_lock);
	rovr = xprt_lookup_rqst(xprt, *xp);
	if (!rovr)
		goto out_unlock;
	task = rovr->rq_task;

	if ((copied = rovr->rq_private_buf.buflen) > repsize)
		copied = repsize;

	/* Suck it into the iovec, verify checksum if not done by hw. */
	if (csum_partial_copy_to_xdr(&rovr->rq_private_buf, skb)) {
		UDPX_INC_STATS_BH(sk, UDP_MIB_INERRORS);
		goto out_unlock;
	}

	UDPX_INC_STATS_BH(sk, UDP_MIB_INDATAGRAMS);

	xprt_adjust_cwnd(xprt, task, copied);
	xprt_complete_rqst(task, copied);

 out_unlock:
	spin_unlock(&xprt->transport_lock);
 dropit:
	skb_free_datagram(sk, skb);
 out:
	read_unlock_bh(&sk->sk_callback_lock);
}

/*
 * Helper function to force a TCP close if the server is sending
 * junk and/or it has put us in CLOSE_WAIT
 */
static void xs_tcp_force_close(struct rpc_xprt *xprt)
{
	set_bit(XPRT_CONNECTION_CLOSE, &xprt->state);
	xprt_force_disconnect(xprt);
}

static inline void xs_tcp_read_fraghdr(struct rpc_xprt *xprt, struct xdr_skb_reader *desc)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	size_t len, used;
	char *p;

	p = ((char *) &transport->tcp_fraghdr) + transport->tcp_offset;
	len = sizeof(transport->tcp_fraghdr) - transport->tcp_offset;
	used = xdr_skb_read_bits(desc, p, len);
	transport->tcp_offset += used;
	if (used != len)
		return;

	transport->tcp_reclen = ntohl(transport->tcp_fraghdr);
	if (transport->tcp_reclen & RPC_LAST_STREAM_FRAGMENT)
		transport->tcp_flags |= TCP_RCV_LAST_FRAG;
	else
		transport->tcp_flags &= ~TCP_RCV_LAST_FRAG;
	transport->tcp_reclen &= RPC_FRAGMENT_SIZE_MASK;

	transport->tcp_flags &= ~TCP_RCV_COPY_FRAGHDR;
	transport->tcp_offset = 0;

	/* Sanity check of the record length */
	if (unlikely(transport->tcp_reclen < 8)) {
		dprintk("RPC:       invalid TCP record fragment length\n");
		xs_tcp_force_close(xprt);
		return;
	}
	dprintk("RPC:       reading TCP record fragment of length %d\n",
			transport->tcp_reclen);
}

static void xs_tcp_check_fraghdr(struct sock_xprt *transport)
{
	if (transport->tcp_offset == transport->tcp_reclen) {
		transport->tcp_flags |= TCP_RCV_COPY_FRAGHDR;
		transport->tcp_offset = 0;
		if (transport->tcp_flags & TCP_RCV_LAST_FRAG) {
			transport->tcp_flags &= ~TCP_RCV_COPY_DATA;
			transport->tcp_flags |= TCP_RCV_COPY_XID;
			transport->tcp_copied = 0;
		}
	}
}

static inline void xs_tcp_read_xidxs_;
	}

	xprt_co0vlr2 nle_entriece_d_FRLxs_;
	}

	xprt_co0vlrn_unId;
	__be3>max,
	;
	}

	xprt>tcp_reclend __use(RCV_LAST_n6d;
	char *p;

	p = ((char *) &transport->tcp_fraghdr) + transport->tcp_offset;
	len = sizeof(transpotcp_reclen) {
		transport->tcp_flags |= = 0;
COPY_FRAGHDR;
		transport->tcp_oAG	(1UL << 00;
COPY_FRAGHDR;
	tcp_flags4	}

	xprt_co0vlr2 nle_entriece_%s_d_FReof( invalidrt)
{
	if (transport->tcp_offRCV_COPY) ? "e sunrpor"
", ntocallbac: "e er to
 am(nvalidrt->tcp_offset;
	usedt>tcprt: doforce_close(xprt);ck_xprt, xpr_LAST_FRAG) {
			transport->tcp_fl layer
 &= ~TCP_RCV_COPY_DATA;
			trace callbacansport->tcp_flags |= TCP_RCV_COPY_XID;
			transporty(stV_LAST_n6->tcp_copie
	strucW * xs_clcp_fraghdr) + transpoort
 *8 acp_send_dtranspet tt, 
		struc(4s_sent,INIT_Re t>t from4s_sent,INIT_Re  lay/e sunrport).strucWmsg rerick,ck:
	spport->sockINIT_Re firser nee->rq_svcp_fraghdr) + transpopor4 (ENOTCO Airce_al	spin_"data reat>tchis is aock_xprt vcp_fraghdr) + transpo-;
		}
	}
}

static inline vort = container_of(xprt, struct layer
voiddxs_;
	}

	xprt_co0vlr2 nle_entriece_UL </_COPYrportRLxs_;
	}

	xprt_co0vlrn_unId;
	__be3>max,
	;
	}

	xpr layer
vo+ V_LAST_n6d;
	char *p;

	p = ((char *) &transport->tcp_fraghdr) + transport->tcp_offset;
	len = sizeof(transpotcp_reclen) {
		transport->tcp_flaAG	(1UL << 00;

	strucW *dle TCy		reockelen -DRe applien tryinwRM)
sock;
_Re  layer
struc * trNOTCOying - req- appliczeof(stru'o read
 * @len:' */
	strq_bytesrt->tcp_offset;
	used layer
v>= req->rqfRCV_COPY:ansport->tcp_reclen);
}

static void xs_UL << 00;
sport->tcp_reclen);
}

static void xs_ TCP_RCVport->tcp_reclen);
}

static RCV_COPY;			continue;
		s RCVUL <:ansport->tcp_reclen);
}

static void xs_UL << 00;
sport->tcp_reclen);
}

static void xs_ TCP_RCVport->tcp_reclen);
}
ort->tcp_RCV_COPY;			continue:
		status = -ENOTCONN;
		/* ShAGHDR;
	e er to
;
	case el@n

	/* Sanity check of the r NULL;

	sk->sk_CONNRE: doforce_close(xprt);ck_xprt, xpr_LAST_FRAG) {
			transport->tcp_fl rt(st), "%4hx", rpc_get_port(sap));
	xnsport->tcp_flags |= TCP_RCt(sap));
	xnsport-e reset in orLOSE, &xprt->state);
	xprt_force_d
request(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tkrcv rpc_xY_XID;
			;ULL, 0, 0) usingcv rpk_rqstp;
	stlen) > repsder to av)
{
	if (transport->tcp_offsetd xs_UL << 0port->sportruct so_snd_buf)
		*se address of-DRe appli
		if (tememstrugcv rpstatic inline voidp_reclend __use(RC State of Tmax,
	;
	}

	xpr layer
 of Ttainer_of(xprt, struct layer
v)_RCVport->tcp_recletcp_fla+ontainer_of(xprt, struct layer
v_RCVport->tcp_reclen);
}
ort->tcp_void xs_UL << 00;
}pied = 0;P_RCtranu we'v sent = >nt of length %d\n",
		oid xs_tcp_read_xidxs_;
mittingsport->tcp_flags |= Tmyroy cntk("d = 0;t of length %d\n",
		oid xs_tcp_read_xidxs_;
;(tememstru&myroy c, oy c, tainer_myroy cv)_RCVmyroy c,
		.c err;
		ba socketb)
{
	struct xdr_skb_regcv rp,reclend __use(RC State of Tp));&myroy c, 	.skb		= skb,
		.o		baP_RCtranu wk, xr		baP_RCtrtransport-r		b_pagedata socketb)
{
	struct xdr_skb_regcv rp,reclend __use(RC State of Tp));oy c, 	.skb		= skb,
		.o		_from_sase -= xdrport->tcp_recletcp_fla+onrck_fraghdr(struct sock_xprrt-r		b__from_san = sizort->sp En starace_uct ader_pointeoid			(* applieortrucuslogic betus;
ryinw->io':	Some oth Thic > ortrucadd-- adlowsapplic be c. AM)
sady - do nowortrucpor sizeientatic void xs_ TCPen trquest
 *
 * onnecteM)
like td			(*anycadd-- adlowprt->aseortrucfromith t * .ortrucAnysock_reader&sk->zeof(stet to)) {
teM)ortruc
 *as parded.
		if (tecp_reclen) {
		transport->tcp_flags |= TCP_RCV= -ENOTCONN;
		/* Shd_FReof(->rqnc > 
	e er toxs_sendpart->tcp_offset;
	usedt>tcprt:*xprt;
	struct rpc_rqsnt errk);
R;
	tcp_flags%lu, "
", n"ct sock_xprt %u;
R;
	- transpo%uxs_sendpaguf, skclend __use(RC State of Tpraghdr(struct sock_xp skclend __use(RCgment lenength */
	if }

	xprt_co0vlr2 nle_ed_FReof(-"data%Zd;
	}

xs_sendprt->tcp_offset;
	usedt>tc, rrt *xprt;
	struct rpc_rqsnt errk);
R;
	tcp_flags%lu, ct sock_xprt %u;
"
", "R;
	- transpo%uxs_s guf, skclend __use(RC State of Traghdr(struct sock_xp skclend __use(RCgment lenr to av)
{
	if (transtcp_flaglease;
	reck(&xprt->transport(tecp_reclen) {
		transport->tcp_flags |= TCP_RCflags   reading TCP record fragment of length %d\n",
			transsport)
{
	if (transport->tcp_offset == transpt->tcp_reclen) {
		transport->tcp_flags |= TCP_RC}copied);

Finds skb... */
	xp = skb_header_pointebuf)t>t fromhe reqs req->rt(sts up cp-"datatranssk_call req-if thiory is freed beforuest was no skb,e sun), "%4hx", rpc_get_port(sap));
	
	set_bit(XPRT_CONNECTION_CLOSE, &xprt->state);
	xprt_force_d
request(struct rpc_task *task)
{
	struct rpc_rqst *req =e reset in order 
	xprt_co0vlr2 nle_entriam(sk, d_FReof( invort->tcp_offset;
	usedt>tcprt_RCV_Find from the skb... */
	xp = skb_header_poinet t>t eof(struct udphdr),
				sizeof(_xid), &_c_xpr	if (xp == NULL)
		goto
}

static inline vort;

	/*orLflags &= ~TCP_RCV_COPY_FRd_FReof(-"d */
	xlikefreq-!xs_sendpart->tcp_offset;
	usedt>tcprt:*ck;
	}

	UDPX_INC_STATS_BH(sk, UDP_Mnt		= skb->len}
E: doforccp_fl rt(st)		gotooy c, *orLdr_buf *xav)
{
	if (transport->tcp_offsetd xs_ TCP;
	xpr_STATS_BH(sk, UDP_ase;
	reMIB_INv)
{
	if (transtcp_flint sek;
	}

	UDPX_INC_STATS_BH(sk, UDP_Mn - sizeof(rpc#ift
 * hod(CONFIG_SUN_RCVBACKCHAN(bufied);

Ob(strsretue reset i 60 iousk,  Thic > t fromhe reqs req->rt(sts up cp-"datatranssk_call req-if th et connsstacportructddress ofete_rqsts uprt);
ter a frace wi	= prossk_ob(str skb.. reset iyinge_c/
stss ofetheaderranspo We implement afrom		= skb->hiory is freed tsport->tcp_fl layd_kvec(sock, NULpc_get_port(sap));
	x
	
	set_bit(XPRT_CONNECTION_CLOSE, &xprt->state);
	xprt_force_d
request(struct rpc_task *task)
{
	struct rpc_rqst *req =e reset in order py the XID from the skb... */
	xp = skb_header_pointer(skb, sizeof(struct udphdr),
				sizeof(_xid), &_c_xpr	if (xp == Nbc = 0;
		s		goto
}

static inline vort;

	ask *task)
{ittingk;
	}

	UDPX_INC_STATS_BH(sk, UDP_Mnt&= ~TCPdup(_WARNING "Cansport_len	 in_timion flown;
	if (coand/or it has put us in CLOSEnt		= skb->len}
E:
	xprt_co0vlr2 nle_entriaransport_ d_FReof( invort->tcatus < 0)>tcprt: doforccp_fl rt(st)		gotooy c, *orLdr_buf *xav)
{
	if (transport->tcp_offsetd xs_ TCP;
	xpr_STATS_BH(sk,bc = 0;
		s= 0INv)
{
	if (transtcp_flintngk;
	}

	UDPX_INC_STATS_BH(sk, UDP_MMn - sizeof(rpcis freed beforuest_ doforccp_fllen != 0)
		 NULpc_get_port(sap)ngsport->tcp_flags |= TION_CLOSE, &xprt->state);
	xprt_force_d
request(struct rpc_task *task)
{
	struct rpc_rqsstruct rpct)
{
	if (transport->tcp_offRCV_COPY) ? Sanity ch skb,e sun)		gotooy c) : Sanity ch skb, layd_kve		gotooy c)(j)
				dprintk("\n");
	uest_ doforccp_fllen != 0)
		 NULpc_get_port(sap)ngsport->tcp_flags |= TION_CLOSE,ruct rpnity ch skb,e sun)		gotooy c);uf[j+1],   = sONFIG_SUN_RCVBACKCHAN(buzeofied);

Rtria&sk->ientor not respon

/*
 * y - 
 *eicked,n(xprt_UL <intkats upfRCV_COPY

/Ry somges,
bytes_ader_pohout_unlock:
	ssor causes us to abort the t_fllen != 0)
		 NULpc_get_port(sap)	x
	
	set_bit(XPRT_CONNECTION_CLOSE, &xprt->state);
	xprt_force_d
request(struct rpc_task *task)
{
	struct rpc_rqs_buf *_ doforccp_fllen !		gotooy c)  rpc_xpr doforce_close(xprt);ck_xprt, xpr	flags);
	sportrucTr not responk, UD_peerus s skb... */
	x
	sk-p a TrtrucTr re'ops transportsporm(skngthrt->ator noansport-.
		if (tecp_reclen) {
		transport->tcp_flags |= TCP_RCCV_LAST_FRAG) {
			transport->tcp_flas pard &= ~TCP_RCV_COPY_DATA;
			transport->tcp_flags |= TCP_RCV_COPY_XID;
			opied = 0;t of length %d\n",
		oid xs_tcp_read_xidxs_;
;(t sent = >nP_RCtranu w)k("d = 0;P_RCtranu we'vP_RCtranu wk, xr;
		bP_RCtrtransport-r;
		braghdr(struct sock_xprrt-r;
		bP	xprt_co0vlr2 nle_eas parded xs_;
	}

xprt_co0vlrn doforce_close(xprt);ck_xprt, xpr_LAST_FRAG) tsport->tr;
	strcv(cp_fla_RCriptor_ in droy c,  data to read
 *
 *tes successfulldr_buf *xdr, unport({ * @sk: socket with dat_loodroy c->arg.*/
#dess?
 */
static int xs_tcp_send_request(struct rpc_task *task)
{
	struct rpc_rqst *req =rt);
	xs_xprt_free(xprt);
	odule_put(THIS_MO=ldr_buf _local_copt-r;
uct sk_bndmsg(). */
	while (1) {
		r;
	strcvdr: UDn;
	if (cdoort->sp Rtria for new
		dprintkaker in f netes_aryif (te = san connecrentrgic exphas ngtg - TS_BH(skic empty
		dprints 0)
			to av)
{
	if (transport->tcp_offsetd xs_tcp_cheport->txs_tcp_force_close(s{
	str&oy c);u>rq_xmit_bytes_}t->sp Rtria fo_Re t>t  f netes_aryif (teto av)
{
	if (transport->tcp_offsetd xs_d_Fport->txs_tcp_forceags ATA;
			tra&oy c);u>rq_xmit_bytes_}t->sp Rtria fo_Re  lay/e sunrportif (teto av)
{
	if (transport->tcp_offsetAG	(1UL << 0port->txs_tcp_force layer
 ATA;
			tra&oy c);u>rq_xmit_bytes_}t->sp Rtria fo_Re .. */
	x&sk->f (teto av)
{
	if (transport->tcp_offsetd xs_ TCP;ort->txs_tcp_forcelen !		goto&oy c);u>rq_xmit_bytes_}t->sp Skipmion *anycv)
i
}

/_sent,t acopieentris>f (teport->tcp_flas pard ATA;
			tra&oy c);u>}cket daghdr),
		.cot *xprt;
	struct rpc_rqs) {
		r;
	strcvdrovr
	if (cruct rp
		oidhdr),
		.chutdown(xprt);
	casock:
	spin_unlock(&xprt->transport_lockk_write_sp:
	skb_free_datagram(sk, skb);
 out:
	r_sent_unlock_bh(&sk->sk_callback_lock);
}

/**
 * x	casock:
	spin call sock_error() since there may be a rpc_tcp_fla_RCriptor_ iodroy c;
 rpc_xpadsk_bndmsg(). */
	while (1) {
		r;
	struct sk_buff ing the socket, and so we don't want to clear sk->sk_err.
 */
static void xs_error_rep>sp Anys&sk->mgs);
T)
		t f->tcrt->_xmon s(xprten t>rq_svec handle TCransportdy somges,nex Race t us is is a res_INC_Sprintk("RPC:       
	xpr_ST	dprintk("RPC:       xs_close/ucW *s;
rodroy cear_bassansport->k->sar_s) {
		r;
	strcvdis aodroy c.arg.*/
#sk_err. (cdoort->odroy c.
		.c er65536SEnt		alagst->tcp_fltatic vra& droy c, s) {
		r;
	strcv);u>}cket dag		ala>e = 0;
	} ek, copied);
	xprt_complete_rqst(task, copied);

Dpointes_tcp_shutdown
}

er/
}

er2x
	sk-p a_lockdtrgp a_e complebrokest tk);
s
		traror_rep>tran * on the s for     lymplefashE:
		cwn a TCP socket
 * @xpe_c/
st_
}

er:       != 0)
		 NULpc_get_port(sas successloder_      
	SE, &xprt->state);
	xprt_force, *xp);
	rr.
t(XPRT_CLe a implemenng>rq_snd_buf;
	in;rce a TCP close if the sersport->connect_worker);xs_tcp_send_request(struct rpc_task *task)
{
	struct rpc_rt);
 *xprt)
{
	stC:  io
{
	strt);
n",
			-statusansport = cont,
				re_      
ength\n");
		xs_tcp_force_xprt 
}

er:       != 0)
		 NULpc_get_por
	SE, &xprt->state);
	xprt_force, *xxs_tcp_send_request(struct rpc_task *task)
{
	struct rpco clearo out;
	dprintk("RPC:    sport->connect_worke ||
)	x
	!c_xprt *xprt)
{
	stsock_xprt *transport = contad_buf;
	in;rcel_delayed_work_sync(&transport->connect_worker);r_STATl_delimplemenng>rq_sn>rq_bytes_sent == req NULL;e a implemenonsport-!= 0)
		 NULpc_get_por
	SE, xprt %p\n", xprt);

	cancel_delayed_work_sync(&transport->connect_worker);

	xs_reset_tranif the server is sending
 * junk 

	xs_reset_transport(transport);
	xprt->reestablish_timeout = 0;

	smp_mb__before_atomic();
	clear_bit(XPRq_bytes_sent == req NULakerf the dt)
{
	if (test_and_clear_bit(Xq NULL;e a implemenonsport-!uct rpc_/ucMkerck);

	/* Rasep>trat from.\n"XID fll 
			xprskb))shdr);
	ONNECTION_ABORT, &xprt->statect socket *sock
static void_state_change;
	sk->sk_write_space = tic voi:
	skb_free_datagrhtrance = t, skic voilback_lock);
}

/**
 * x	cask
static voi call sock_error() since there may be a rpc_ing the socket, and so we don't want to clear sk->sk_err.
 */
static void xs_error_rep>ndmsg(). */
	while (1) {
		s
static voidack_lock) *rovr;
uct rpc_
	copied = rovr->rq_pe = t%x impl %dkdtrd %dkzapped %dkst sock_xpr    invalidport;
}

sta;
uct eq->rq_slen);

	validpsockportc vracogniDG	(	validpsockportc vracogniZAPPE(	validport;
}
ock_xpr) = NULL;
	transport->k
static voi t rpc_terr;
	if (erore_aq_bytes_ort;
}

sta>= req->rqp_ofESTABLISHlt:
	(struct udphdr),
				sizeof(_xid), &buf *xdr = t(XPRT_CLe a implemelen);

	/* Pross?
 */
static int xs_tcp_send_request(struct rpc(sap)ngsport-ask)
{
	struct rpco ->sp Rt_xpren < 8)) {
info
			 * raghdr(struct sock_xprt *trant->tcp_fraghdr) - transpo*trant->tcp_fraghdr) tcp_flags & TCecp_reclen) {
		transpd
requp_offsetd xs_tcp_che |rt->tcp_offset = 0;
		dr),
	ION_ABORcooki	flag;
		dr),..\n",
			xprt, -err);
	tr
		brea)tes_}t->gk;
	}

	UDPX_INC_STATS_BH(sk, UDP_Mntcontinue;
		st->tFINt(tra1    sensockeck_lociOSPACE,t f-sock->flags);
	}nding_ta/
		dr),
	ION_ABORcooki	flagxpr_ST	dprintk("RPC:       xs_clo	ULL)
		goto out_re 0;

	smp_mb__before_, xprt %p\n", xprt);

	canc;

	xs_reset_tranif theED

	smp_mb__before_,

	xs_reset_transport(transport);
	xprt->re_atomic();
	clear_bit(XPRT	t
 * @xpe_c/
st_
}

er:       !{
	struity che);
	      
enntcontinue;
		st->tnsport(tra    sensockock);
 dOSPACE,t f-sock->flags);
	}nding_ta/
		dr),
	ION_ABORcooki	flagxp

	xs_reset_tranif theED

	smp_mb__before_,nity check of the record le;
		st->tnspo 0;:;
	sportrucIport_lock);
 p>trat ->flareq->rplemenon,her ersutdow	tr onnecte _chanientNC_NOSPace t us 
	sk		if (te res_INC_Sprintk("RPC:        <f (!t->tINIT_REEST_TO);
		dr),
	printk("RPC:       xs_ (!t->tINIT_REEST_TOenntcontinue;
		st->t == tA {
			rL)
		goto out_re 0;

	smp_mb__before_,t
 * @xpe_c/
st_
}

er:       !{
	struity che);
	      
ennt xprt %p\n", xprt);

	canc;

	xs_reset_tranif theED

	smp_mb__before_,atomic();
	clear_bit(XPRT	continue;
		st->tnspor: Sanity che_xprt 
}

er:       !n CLOSEnt req NULakerf the dtn CLOSEn}pc_socket_error(xprt, sk->sk_socket, err);
	if (test_bit(XPRT_CON	ret = -ENOTCall sock_error() since thending_tasks(;since there may be a rpc_intruct xdr_buf (port->iterr;
	if (eroad_buf;
	in;rcel_delayedCK_ASYNC_NOSPAC);
	case -ECONintruct xdr_buf (sk->sk_err.
 */
static voidserver.
 */
static(XPRT_CLOSING, &xp int *sent_p)
{
	unsigned int rem  rpc_xpr(transportdr),..ret = -ENOTprt->statect socket *rovprt *transpo_state_chanhe req	trace_eturn stapplicationsend_requessssssssssssssssssssssbce ta_ravailn_ti
	skb_free_datagrhtrance = t, skic voilback_l C->sockrace_y the_sob);
 application
i_ravailn_tikINIT_Rdrop_datater aW notyxlikerro.\n"Xoursave.
 s untilareqy y - er er"succifiy -t"
NETUNR occu,lockedwi;
ryi'M)
sas
	socsourcqs rerashEprs)
{
	struld we
necte to sbunyte:	Camrpc_socket_cor causes us to abort*rovprt *transpTCall sock_error() sig the socket, and so we don't want to      eof(net/)) e/ck_e.c:q NULdef_ %5u xmit incomp(struct _ %5u n_tic voiEnt re	ret = -ENOTCONN;t_unlock;
	}

	xprt_complete_rqst(task, copied);

 outy chprt *transpo_state_chanhe req	trace_eturn stapplicationsend_requessssssssssssssssssssssbce ta_ravailn_ti
	skb_free_datagrhtrance = t, skic voilback_l C->sockrace_y the_sob);
 application
i_ravailn_tikINIT_Rdrop_datater aW notyxlikerro.\n"Xoursave.
 s untilareqy y - er er"succifiy -t"
NETUNR occu,lockedwi;
ryi'M)
sas
	socsourcqs rerashEprs)
{
	struld we
necte to sbunyte:	Camrpc_socket_cor causes us to aborty chprt *transpTCall sock_error() sig the socket, and so we don't want to      eof(net/)) e/cntain.c:qkcontaine %5u xmit incomp(strukcontaineis_ %5u n_tic voiEnt re	ret = -ENOTCONN;t_unlock;
	}

	xprt_complete_rqst(task, copik);
}

/**
 * xs_udpoLe a 	 */
			iz holding XPRT_LOCKED in order to avoid issues with
 * xs_reset_transport() zeroing the socket from undead_unlock_bh(&sk->sk_callback_lock);
}

ssk_callbackrcv;
	roitting (sk == NUtask}

st int cp_BUdesc,K;ting (sk =gcv rpk_rsk_callbackrcv;
	r

 omp_mbmax = 0s

 2;	b__from_sk_callbacksnd;
	roitting (sk == NUtask}

st int SNDBUdesc,K;ting (sk =snd rpk_rsk_callbacksnd;
	r

 omp_mbmax = 0s

 2;	blags);
		ret = -ENOTCONN;
}tatect socket *rove a 	 */
			iz o-;
e_xprtdafrom		d			(*th disd xs_xprt_fgerann sek_callba
	skb_nd;
	r:_socket_falsiz ooain
 *e applienin/_sent
	skbrcv;
	r:_socket_falsiz ooaioid			(* applienin/_sent
	s
	skSehending_tprtdafrom		d			(* applicaiz oth disor causes us to abort*rove a 	 */
			iz holding XPRT_LOCKED in *xdr, un_nd;
	r *xdr, unrcv;
	roorder to avoid issues with
 * xs_reset_transport() zeroing the socket from undersk_callbacksnd;
	r
 = req-strund;
	ro(tecp_reclen) snd;
	r
 =snd;
	r
+ 1024		braghdr(strurcv;
	r
 = req-strrcv;
	roo	braghdr(strurcv;
	r
 =rcv;
	r
+ 1024		bit(Xs_udpoLe a 	 */
			iz hprt->statect socket *rov    ro_state
	trace_rsent a to te_      se calt,t a			-stek_callba
	skbSPACE,WR);
		tra_   de_so
	s
	skArq_prareq->rpget_nt ated(xprENOTCONNent a to te_      s, ske call aor causes us to abort*rov    rn of a request.
 * In this case, the socket may nedr),.>rq_private_buf, skb)) {-ETIMEDOUT, copik);
}

s successcopieeortg a  a d/
sllba(to aurn re successcopieer void xdr),.max = sv * xs-xdr),.m);
= sv * x; re successcopieer vlags(e successcopie)TUNa d/
su32() %er voi (cruct rpr vla+xdr),.m);
= sv * x; atect socket e a  * xs-xlen - req- * xsnumb in ca_Re ..motend_d4) {
down(socd xs_xprt_fgerann sek_callba
	skbllba: new
 * xsnumb iback_lock);
}

/**
 * xe a  * xn of a request.
 * In the successcopieert, xe(xprt);
}

/**
 * xs_dee ahe pa * xsINITst.
 %perro%uxs_s guf, srt, xprcord me a  * xn,
				req->rq_srt, xpr	ort*rt->aCONNEC * xn,q_sn>rq_bytes_ses successcopieeortg a t sock_;
		return;
	}
	dprintk("RPC:    e successcopieert, k_rsk_callbacks sock_k);
}

srt, k_t, zeroNULL;

	sk->sk_.= sv * xoo	brt, k_rortg a  a d/
sllba(f (cruct rp * x; ateytes_ses successcopieeortnex  t sock_;
		return;
	}
	dprintk("RPCthe successcopieert, xe(xprom_sk_callbacks sock_x		req->rsk_callbacks sock_x = req-str!NULL;

	sk->sk_.= sv * xoo	bq->rq_bytes}

srt, k<=xdr),.m);
= sv * xtatu * xt>xdr),.max = sv * xoo	bq->rq_bdr),.max = sv * x (cruct rp-- * x; atST_FRAG) tsporbed(;
	transport->old_write_space = sk->sk_wrg_tasks(oorder to avoid 			r_portase my			r;
static voin   xx = reqe successcopieert, k_rortg a t sock_;ck_xprt, xpr	e successcopieelas
opie
	struc fraclose(b))p a_lockanycephe  ralert, k(i.e. rt, k_t, zer>rq_svcp_fraghdr>sk_.= sv * x  rpc_t, true,bed(.  L - req- (!ro>rq_s * x selement ahappenskb_lici* xswmsg retur the s k);

	struc(lockexa_BH( acpest has     chis i
	lback_loensutds
		trasady - ca refereno intk("RPCst->xs_pktdplemenonsnnecttrace_rpc_ (!rocephe  ralert, close(al	spinxs_pka )
{
N:	Calm>sk_wrtdplemenon

/*
 * er eops tdifc->iov_xs_pklock);
 dropit:,* a
dols tdoens' s, rf(strmhis i
	lba frace wib))p a_lockanyclen rvessrt, k(i.e. rt, k_t, zer>rq_svcp_fraghdr>sk_.= sv * x  rp1)rortg a t sock_ * xve
teM)or* xsnsutdow	tr rt, kothern-ode_sfrom.inwRM)
bind fsCransed.
	ncomp(str * x  rpc_o	bq->rq_byte
ememstru&my			rn",
			-status)rc			rn"vcp_fraghdr>sk_.ov_basef (cdoort->rd me a  * xn(r to avoid 			r *)&my			rn"rt, xpr	rt(stru)
{
	stbed(;
ocope(r to avoid 			r *)&my			rn
f Tpraghdr(stru>sk_.ov_basef (c{
	struct rpc_ort->tcp_reclen) s sock_x = * x (cT	continue_}t->las
x = * x (cTrt, k_rortnex  t sock_;rite_space rt, xpr	r(str * x >elas
);
		n   xflagx}cket dagruct rp-EADDRINUSEzeron   xx		r2CONintrucmy			r.ss_fami xs rpAFo thused;xprt;
	struct rpc_rqst %pI4:%u:qst (%d)ovr;
	struct n
f Tp&n(r to avoid 			r_ ca*)&my			r)) s);
			rn
f Tpspace ruct? "uflen;"c: "ok"r == 0)
	agedataxprt;
	struct rpc_rqst %pI6:%u:qst (%d)ovr;
	struct n
f Tp&n(r to avoid 			r_ c6a*)&my			r)) s);6
			rn
f Tpspace ruct? "uflen;"c: "ok"r == 0)
	q->rq_b*sk)
opied);

W *dle TCsupock_ *utobind t a&desc, xdr_skb_reads
 * @sk: socket with drd bed(;
	trans, the socket may nercu_g the soc(XPRT_CONNe a 	req->rcu_devec->iov_send. If teck_lo->cl_);

	/;nercu_g the}

	UDP, copik);
}

/**
 * xwith de a  * xn of a request.
 * In the successcopieert, xe(xrpc#ifdef sONFIG_DEBUGesc,K_ALsc,ik);
}

 of a r
	}

class_key * xkey[2];ik);
}

 of a r
	}

class_key * xs
	}

key[2];iAST_FRAG) {
			transpor- trassify	if (eru(= sk->sk_wrg_tasks(oorder to avoid h(&sk->gned i to thport-
	}

dOSP
class_T_CLnamec vra"s
	}
-&desc, x-rucnvali&* xs
	}

key[1]ra"st-
	}
-&desc, x-rucnv &* xkey[1]xpr_LAST_FRAG) {
			transpor- trassify	if (er4(= sk->sk_wrg_tasks(oorder to avoid h(&sk->gned i to thport-
	}

dOSP
class_T_CLnamec vra"s
	}
-&de thu-rucnvali&* xs
	}

key[0]ra"st-
	}
-&de thu-rucnv &* xkey[0]xpr_LAST_FRAG) {
			transpor- trassify	if (er6(= sk->sk_wrg_tasks(oorder to avoid h(&sk->gned i to thport-
	}

dOSP
class_T_CLnamec vra"s
	}
-&de thu6-rucnvali&* xs
	}

key[1]ra"st-
	}
-&de thu6-rucnv &* xkey[1]xpr_LAST_FRAG) {
			transpor- trassify	if (er(tatifami xe = sk->sk_wrg_tasks(oordeWARN_ON_ONCEruct _owces_by== NU(gned i t) to cleauct _owces_by== NU(gned i t) xpr(transportaq_bytesfami x>= req->rq&desc, x: Sanit- trassify	if (eru(= UDP_Mntcontinue;
		s&de thu: Sanit- trassify	if (er4(= UDP_Mntcontinue;
		s&de thu6: Sanit- trassify	if (er6(= UDP_Mntcontinue}j)
				dprintk("\n");
	transpor- trassify	if (eru(= sk->sk_wrg_tasks(oord_LAST_FRAG) {
			transpor- trassify	if (er4(= sk->sk_wrg_tasks(oord_LAST_FRAG) {
			transpor- trassify	if (er6(= sk->sk_wrg_tasks(oord_LAST_FRAG) {
			transpor- trassify	if (er(tatifami xe = sk->sk_wrg_tasks(oordf[j+1], pik);
}

/**
 * xdummyde aup	if (er(= sk->s
	stru sk->s*
	stoord_LAST_FRAG= sk->sk_wrg_taanspontt = okvec(sock, NULpc_get_port(sa
	transport->old_write_space tatifami xe tatiel@ne tatipeerocol() since thending_tasks(;siask
 * usint(stru_eq NULpontt s_INC_S_CONNnuf *fami xe el@ne peerocolnsigned,p1)to cleat(st<pc_ort->xprt;
	struct rpc_rqy -_repontt  %dkwith
 * xsnding_t(%d)t = xprt_	peerocolnsock(sk))s_error_rep>}
anit- trassify	if (er(fami xe =ant to  t(struporbed(;rite_space =ont to cleack(sittingoNULL;

	xs_restore)s_error_rep>}
SET:
		xs_ks(;s_socket_
		xsERR_PTR(ck(sk)y(struct rpc_xprt *xprtfdOSshlimplemenng>c(sock, NULpc_get_port(sap));
	x
= sk->sk_wrg_tasks(oorder to avoid issues with
 * xs_reset_transport() zeroing the socket 
									uct rpco clearok_callback_locmittingsport-oid h(&sk->gned i to thransport)
{
	struct socket *sock = tranEnt req->rq_snd_buf.len)
rite_space =tranEntf (sk == NULL)
		rea rpc_	e;
	transport->old_eret with data to rea;	blags);
		ret = -ENO_eret *rovprt *transp;	blags);
	ady = transportxsdy;
	sk->sk_stablags);
	 Thic >nt a= GFP_ATOMICanEnt _STATl_delimpleme dtn CLOSE
->sp Rt_xprto new
nding_ta/
		cp_reclen) socsk->gned;
		cp_reclen) 
	struc to thranspork;
	}

	xprt_complete_rqst(task, cpsize = Tell retur the sprt)en alr: UD implemenngudphdr);
	p_mb__be.ION_ABORcountflagx
	p_mb__be.ION_ABORr: UD = jifcies)
	q->rq_b)
{
	stput us igned,p,
				req->rq_s
	p_mbov_base,e = 0rpc_fraghdr),
	};

e aup	if (ero_stontt  &desc, xdr_skb_,pest has  _sf_ (!roce_d4) {
d xs_xprt_fbuf)with
 * xsket  t us i	skbSith
 * xree_datagwith
 * xsket  t us i	skbpontt = okv:nlock:
	spin_tontt  k);
	wriags);
	}p = sctiel@niory is freed tspor
	};

e aup	if (er&xprt->state))
		goto out;
	xprt_wake_peocket with dat_lo NULL;

	sk->sk_;since thending_tasks(;siask
req->rq_bytIOo thel_delayed_work_sync(&transport->connect_worker);req->rq_b_eq NULpontt s_INC_S_CONNnuf *&desc, x 
					 int Scp_ofr descgned,p1)to cleareq->rq<pc_ort->xprt;
	struct rpc_rqy -_repontt  &desc, xd"
", "Rith
 * xsnding_t(%d)t = x e -ENOBUFS:s_error_rep>}
anit- trassify	if (eru(= UDP_Mxprt);
}

/**
 * xs_de= cont implemenngTst.
 %pevia &desc, xdrro%
xs_sendpcket from mbov_bess_ncenngs[fRCVDISPLAY_ADDR]int se

	switch ( *xprtfdOSshlimplemenng>t() zero transpor;
	transport->sut us in CLzero t,  -ENOBUFS:aq_bytes_-ENOBU= req->rq0tus = -ENOTCONN;
		/* Shst.
 %peimpleme ddrro%
xs_sendppcket from mbov_bess_ncenngs[fRCVDISPLAY_ADDR]int	T_CONNe a impleme dtn CLOSE		dprintk(BUFS:Mntcontinue;
		sntk(ENTtus = -ENOTCONN;
		/* Shst.
 %pree_datag%stdoesxlikeexitoxs_sendpacket from mbov_bess_ncenngs[fRCVDISPLAY_ADDR]int	Tcontinue;
		snt_synREFUSlt:
	(= -ENOTCONN;
		/* Shst.
 %preimplement aref;
	lelock%
xs_sendppcket from mbov_bess_ncenngs[fRCVDISPLAY_ADDR]int	Tcontinue:
		status &= ~TCPdup(_ERR "%s: un
	sk->d
	if (t(%d) implemenngTrro%
xs_sendpp	struct n e -ENOBsendppcketmbov_bess_ncenngs[fRCVDISPLAY_ADDR]int	rpc_socke _STATl_delimplemenng>rq_sn>r	dr),..\n",
			xprt, -err);
	tr -ENOBUFS:T:
		xs_-ENOB copik);
}

/**
 * xwith dput us igof a request.
 * In this case, the socket may ner to avoid issues with
 * xs_reset_transport() zeroing the socket from underpc_xp
opies   rfRCVIS *sentsend.)port->sportrucW * xs_clh  &desc, xdest has  _sb	socsolvtddress oortrucfilesystem name -ENO_gs);
	}
bytes_ er ins
 * o, tortrucete_

/*
u_readyst has uct hronousk,.
		ifortrucIpo
 * xs_closeupock_ *uct hronous &desc, xdete_seortrucyi'M)
ransportfiguthe_sounlocar_bassaa name -ENO_toortrucest has.
		if (te, thexiP_MIB_INntk(T_synOSEnt		= skep>}
aretitch ( *xprte aup	if (er&ck_xprt, xpr	;

	astzero!fRCVIS SOFT_synsend.)pEntmsleep_ {
		ruptv_da(1500 = 0rpc#ifdef sONFIG_SUN_RCVSWAPck);
}

/**
 * xe a mem Thicholding XPRT_LOCKED in order to avoid issues with
 * xs_reset_transport() zeroing the socket 
			uct rpco cleaonnect_wappeook uk =s a mem Thichok_callback_locm; atect socket ewappeoo_sTagT_Rdrok);

	/* Rasebeins
;
	lelockewap.te_unlock_bh(&sk->skr_poiae
nec@e= pro: e= pro/disn_ti
	siory d tsporewappeoigof a request.
 * In thask
 = pro order to avoid issues with
 * xs_reset_transport() zeroing the socket 
			uct rpciask
 * xs_closecleac= pro ort->onnect_wappeoflagxpr xe a mem Thichn CLOSEn} flags   ronnect_wappeooort->onnect_wappeo--tablagATl_delmem Thichok_callback_locm; >}
SET:
		xs*sk)
opEXPort_SYMBOL_GPL(porewappeom; 				dprintk("/**
 * xe a mem Thicholding XPRT_LOCKED in ordf[j+1], pik);
}

/**
 * x*rovfdOSshlimplemenng>c(sock, NULpc_get_port
= sk->sk_wrg_tasks(oorder to avoid issues with
 * xs_reset_transport() zeroing the socket ruct rpco clearok_callback_locmittingsport-oid h(&sk->gned i to thransport)
{
	struct socket *sock = tranEnt req->rq_snd_buf.len)
rite_space =tranEntf (sk == NULL)
		rea rpc_	e;
	transport->old_eret *rovr;
	struct;	blags);
		ret = -ENO_eret *rovprt *transp;	blags);
	 Thic >nt a= GFP_ATOMICanEnt _STAe a impleme dtn CLOSE
->sp Rt_xprto new
nding_ta/
		cp_reclen) socsk->gned;
		cp_reclen) 
	struc to thrr xe a mem Thichn CLOSEthranspork;
	}

	xprt_complete_rqst(task, cpsiit(Xs_udpoLe a 	 */
			iz hprt->stateses us to abort*rove aup	if (er(= sk->s
	stru sk->s*
	stoord, &xprt->state);
	xprt_force_d
reeset_transpor
	stzeroing the socket ransport = cont.
	sto;since there may be a rp_lo NULL;

	sk->sk_;since thending_tasks(k_rsk_callbacksks(;siask
req->rq_bytIOo thsp S: UD byclen ahe paanycexitoe pace = tdr);
of(xprt, struct sock_xprt, xprtsks(k_ranspontt = okvebuf, skite_space
->txs_			req->rqcksa_fami xe cogniDy_to, IPPROTO_UDPxpr	;

	IS ERR(= UDPtk("RPC:       xrt);
}

/**
 * xs_de= cont implemenngTst.
 %pevia %stC: "
", n"st (orce_%
	xprt_cket 
			uct mbov_bess_ncenngs[fRCVDISPLAY_PROTO] 
			uct mbov_bess_ncenngs[fRCVDISPLAY_ADDR] 
			uct mbov_bess_ncenngs[fRCVDISPLAY_Port]int s* x*rovfdOSshlimplemenng>t() zero transpor;
	transport->sut us in CLzero t, 0er);req->rq_b0;c_socke _STATl_delimplemenng>rq_sn>r	dr),..\n",
			xprt, -err);
	tr -ENOBUFSopied);

W *ransportplen rve req- * xsnumb in trquest
sunrcacuesonort_lock);
 pats upfind turrcacuedtagram(skia_reace_ying - arreq->sk_caimplemenngueads
 * @sk: socket abora implemenon;
		return;
	}
	dprintk("RPC:     pc_xpssta;der to avoid 			raany   xrt);
}

/**
 * xs_deECTION_ABOnngTst.
 %pesk_cas;
r"RPCxprt_ck_xprt, xprie
	strucDCTION_ABOtor not respon_eturn stytdoe paadyst has opeo >nt 
	necte to&deUNSPEC

/*
 * shoulom		= skbimmedACE,lt sk
	ncompmemprt(&anyr destainer_any) to any.sa_fami x =o&deUNSPECFS:T:sstac=b)
{
	stput us isk_callbacksks(, &anyr tainer_any), 0er);por;
	transport->L;e a implemenonr NULL;

	sk->sk_ of Traghdr(strusks(, T:ssta to clearT:ssta Ent req NULL;e a implemenonsport-! NULL;

	sk->sk_CONNrt);
}

/**
 * xs_de&deUNSPECdyst has 		= skbtrans
	if (skbssta togth\n");
		xs_tcp_forccas;
 implemenon;
		return;
	}
	dprintk("RPC:    s successfullce = t->sk_callback_locrt;
}

stapco cleace = t-=st->tnsporzeroNULL;

	sk-gned i e = t-=sSSeUNnif theEDzort->sp handle TCransportaboraareq->rplemenonnsport_loport-ortruchase TCeq-ergovr-f-sock->fl
		if (te ressk_callback_locrt;
}
ock->fla rpc_o	bt		= skep>axprt;
	struct rpc_rqst:st->tnspordafrom;
}
ock->fla_xprto    invalidp	struct n sk_callback_locrt;
}
ock->fl, cpsiie giv1 << 

sta>=& (t->FfESTABLISHlt|t->Ffsen_S= uszort->sp handle TCransportaboraareq->rplemenonnsport_loport-ortruchase TCeq-ergovr-f-sock->fl
		if (te ressk_callback_locrt;
}
ock->fla rpc_o	bt		= skep>axprt;
	struct rpc_rqst:sESTABLISHlt/sen_S= u "
", n";
}
ock->fla_xprto    invalidp	struct n sk_callback_locrt;
}
ock->fl, cpsiiet abora implemenon;ck_xprt, xpr_LAST_FRAG) tsport->tfdOSshlimplemenng>c(sock, NULpc_get_port
= sk->sk_wrg_tasks(oorder to avoid issues with
 * xs_reset_transport() zeroing the socket ruct rpcerpc_xp
q_bytk(T_synpco clearok_callback_locmittingsport-oid h(&sk->gned i to 	 s successfullkeepik->s=from mb_      mb_o
dOSPval / HZo 	 s successfullkeepcnts=from mb_      mb_o
ent ia_r+ 1o 	 s successfullopt_t a= 1SE
->sp k_wrKeepal		(*optnonsnf (te)
{
	strutgnedoptigned,pSOL_cognET,pSO_KEEPALIVEvalidp;
	__be3&opt_t r tainer_opt_t cprt:*)
{
	strutgnedoptigned,pSOL_k_w,st->tKEEPIDLEvalidp;
	__be3&keepik->r tainer_keepik->cprt:*)
{
	strutgnedoptigned,pSOL_k_w,st->tKEEPINTVLvalidp;
	__be3&keepik->r tainer_keepik->cprt:*)
{
	strutgnedoptigned,pSOL_k_w,st->tKEEPCNTvalidp;
	__be3&keepcntr tainer_keepcnt))o thransport)
{
	struct socket *sock = tranEnt req->rq_snd_buf.len)
rite_space =tranEntf (sk == NULL)
		rea rpc_	e;
	transport->old_eret {
		r;
	structc_	e;
	trans
static voideret {
		s
static voi;	blags);
		ret = -ENO_eret y chprt *transp;	blags);
	ady = transportxsdy;
	sk->sk_stablags);
	 Thic >nt a= GFP_ATOMICanEntsp ;
	wriagptnonsnf (teg (sk == NUtask}

st int BINDPort_sc,K;ting NULL;e a portc vracogniLINGERprt:*{
		sic vo->
}

er2xs_clo	U{
		sic vo->nonag->s
staticNAGLE_OFFanEnt _STATl_delimpleme dtn CLOSE
->sp Rt_xprto new
nding_ta/
		cp_reclen) socsk->gned;
		cp_reclen) 
	struc to thranspork;
	}

	xprt_complete_rqst(task, cpsizeuf *xdr = 	req->);

	/k("RPC:       xr xe a mem Thichn CLOSEth = Tell retur the sprt)en alr: UD implemenngudphdr);
	p_mb__be.ION_ABORcountflagx
	p_mb__be.ION_ABORr: UD = jifcies)
	q->c=b)
{
	stput us igned,p,
				req->rq_s
	p_mbov_base,eO_NONBsc,KUFS:aq_bytesrocmittiq->rq0tus;
		sntINPROGRESS    sensen_S= u!if (te res_INC_Sprintk("RPC:        <f (!t->tINIT_REEST_TO);
		dr),
	printk("RPC:       xs_ (!t->tINIT_REEST_TOenn}s_socket_
		xsxp
opatect socket *sock aup	if (ero_stontt  ask_write_spafromest has  _sf_..motend_d4) {
d xs_xprt_fbuf)with
 * xsket  t us i	skbSith
 * xree_datagwith
 * xsket  t us i	skbpontt = okv:nlock:
	spin_tontt  k);
	wriags);
	}p = sctiel@niorer a e req	ttytae= coprt);
skb))ltater ock);
}

/**
 * x	cask aup	if (er(= sk->s
	stru sk->s*
	stoord, &xprt->state);
	xprt_force_d
reeset_transpor
	stzeroing the socket ransport = cont.
	sto;since thending_tasks(k_rsk_callbacksks(;since there may be a rp_lo NULL;

	sk->sk_;siask
req->rq_bytIOo thuf *xsks(oittinel_delayed_work_sync(&transport->connect_worker);tsks(k_ranspontt = okvebuf, skite_space
->ttxs_			req->rqcksa_fami xe cogniScp_ofr IPPROTO_k_wxpr	r(strIS ERR(= UDPt* Pross?q->rq_bPTR ERR(= UDPpr	rs_error_rep>CP_RC flags);
	 {
dobora T_CLexiPo throbora T_CLexiPk_rs(XPRT_CLOSING, &xp_work_sync(&transport-
->ttconnect_worker);tsen" the " retur the ,tplen rvins
 * o (!rocorce_f (teg a TCP close if the serREU is sending
 * junk 	cp_forccas;
 implemenon;ck_xprt, xprtnel_delayed_work_sync(&tranREU is sending
 * junkr	r(strobora T_CLexiP)r	rs_error_r_eag_tr;en}
E:
	xprt_co0vlr2 nle_e= cont implemenngTst.
 %pevia %stC: "
", n"st (orce_%
	xprt_cket 
			uct mbov_bess_ncenngs[fRCVDISPLAY_PROTO] 
			uct mbov_bess_ncenngs[fRCVDISPLAY_ADDR] 
			uct mbov_bess_ncenngs[fRCVDISPLAY_Port]int se

	switch (t->tfdOSshlimplemenng>t() zero transpor;
	transport->sut us in CLzero t,  -ENOBUFS:xprt;
	struct rpc_rqspdyst has u

	swi%deimpleme dd%dksks(kpe = t%dxs_sendpcket fe -ENOBs
uct eq->rq_slen);

	validpsoccomprt;
}

sta>FS:aq_bytes_-ENOBU= re:
		status &= ~TCP"%s: yst has 		= sk ddun
	sk->d
	if (t%dxs_sendp	struct n  -ENOBUFS:;
		sntADDRk(TAVAIL    senWce wiprobk("ydresTIMEt(tra. Gg_tridtranexitoe pac the ,
rtrucfroment y
		if (tenity check of the record leTcontinue;
		s0tus;
		sntINPROGRESS   ;
		sntALAG	(Y:ans _STATl_delimplemenng>rq_sn>r	uf;
	in;rce
		sntINVAL    senHappenBs
lockin -Encne ts);
	}= NU specifi,t f-
}
k
rtruc (!rocIPv6down(soccte t * tr scope-id.
		if (t;
		snt_synREFUSlt:
	;
		snt_synREShu: S;
		sntkETUnREACH:E		dprintk(BUFS:Mntsenent ycte toexitoe pac the ,rENOTCONNdy somf (te_error_rep>}
r_r_eag_tr: se

	switc
		brea;c_socke _STATl_delimplemenng>rq_sn>r	dr),..\n",
			xprt, -err);
	tr -ENOBUFSopied socket yst has - yst has k);
	wria _sf_..motend_d4) {
d xs_xprt_f4) {
)en alot respon_ece thure
	skbSPACE,own(soccranbuf)wR);
		tramanbe ckpe = tranyst has 		 *
 * orer ak_w:cIport_l..motend_d dropped req->rplemenon,hdy somcaimplemenngueader a);
 dropit->rplemeclose(uct hronous,* a
dw *s;
rae= coprt);
sanywaymplengtguat rtedow	tr necttunck(&ileg ddu NU 
bytes_e* y - _xprID fmple;
	wriag a		ck(&ileg ddsponueader aIpoaa);
 dropit->rpleme ufles,*req-iy sombe Airockr rei 60 intser aent ycfloodt (	__d mounts)ter ock);
}

/**
 * xput us igof a request.
 * In this case, the socket may ner to avoid issues with
 * xs_reset_transport() zeroing the socket from undxprom_sk_callbacksks(k!task)
zero!fRCVIS SOFT_synsend.)port->xprt;
	struct rpc_rqet yst has *xprt)
Tst.
 %pelock%lu "
", n";aimpd
xs_sendppcket from mbprintk("RPC:       x/ HZn>r	urt);
 *xprt)
{
	stC:  io
{
	strt);
n
sap));
,
			-statusansport = cont,
				 Shst.
_Sprintk("RPC:       
nt	T_CON_Sprintk("RPC:        <<= 1o 	  res_INC_Sprintk("RPC:        <f (!t->tINIT_REEST_TO);
		dr),
	printk("RPC:       xs_ (!t->tINIT_REEST_TOennt res_INC_Sprintk("RPC:        >_ (!t->tMAX_REEST_TO);
		dr),
	printk("RPC:       xs_ (!t->tMAX_REEST_TO;_RC flags);
	xprt;
	struct rpc_rqet yst has pe_c/
st
Tst.
 %povr;
uct rpc_urt);
 *xprt)
{
	stC:  io
{
	strt);
n
sap));
,
			-statusansport = cont, 0er);}0rpc_fraghdr),
	};

prt;
}

stsoidhisp som&desc, xdr_skb_-specifckpe =cd xs_xprt_fequest.
 roing teset_tre pace =itoec:
	skb_eq:e_sob);
fileback_lock);
}

/**
 * x
	};

prt;
}

sts>c(sock, NULpc_get_port
= sk->skeq_filetasorLOSE,loderik->:    xs_closecleauct eq->rq_slen);

	);
	ik->:    xs_(lode)(jifciess-xdr),->las
== Nd) / HZo rce q
prt;
f(s 0IN"\txprt_\t (!roc%lu %lu %lu %ldc%lu %lu %lu "
", "%llu %llu %lu %llu %lluxs_sendpcketmb__be.bed(Rcountsendpcketmb__be.ION_ABORcountsendpcketmb__be.ION_ABOR    sendpik->:    sendpcketmb__be.prtdssendpcketmb__be.trcvssendpcketmb__be.brceagsssendpcketmb__be.trq_usendpcketmb__be.bklog_usendpcketmb__be.max len	ssendpcketmb__be.s			xprtusendpcketmb__be.
			xprtu>statect socket *rovprt;
}

stsoidhisp som);
 dropit-specifckpe =cd xs_xprt_fequest.
 roing teset_tre pace =itoec:
	skb_eq:e_sob);
fileback_lock);
}

/**
 * x*rovprt;
}

sts>c(sock, NULpc_get_port
= sk->skeq_filetasorLOSE,r to avoid issues with
 * xs_reset_transport() zeroing the socket from undxpe q
prt;
f(s 0IN"\txprt_\t*ro %uc%lu %lu %lu %lu %llu %llu "
", "%lu %llu %lluxs_sendpcp_reclen) s sock_sendpcketmb__be.bed(Rcountsendpcketmb__be.prtdssendpcketmb__be.trcvssendpcketmb__be.brceagsssendpcketmb__be.trq_usendpcketmb__be.bklog_usendpcketmb__be.max len	ssendpcketmb__be.s			xprtusendpcketmb__be.
			xprtu>statect socket y chprt;
}

stsoidhisp somk_write_sp-specifckpe =cd xs_xprt_fequest.
 roing teset_tre pace =itoec:
	skb_eq:e_sob);
fileback_lock);
}

/**
 * xy chprt;
}

sts>c(sock, NULpc_get_port
= sk->skeq_filetasorLOSE,r to avoid issues with
 * xs_reset_transport() zeroing the socket from und,loderik->:    xs_closecleauct eq->rq_slen);

	);
	ik->:    xs_(lode)(jifciess-xdr),->las
== Nd) / HZo rce q
prt;
f(s 0IN"\txprt_\t cp-%uc%lu %lu %lu %ldc%lu %lu %lu "
", "%llu %llu %lu %llu %lluxs_sendpcp_reclen) s sock_sendpcketmb__be.bed(Rcountsendpcketmb__be.ION_ABORcountsendpcketmb__be.ION_ABOR    sendpik->:    sendpcketmb__be.prtdssendpcketmb__be.trcvssendpcketmb__be.brceagsssendpcketmb__be.trq_usendpcketmb__be.bklog_usendpcketmb__be.max len	ssendpcketmb__be.s			xprtusendpcketmb__be.
			xprtu>statect
	skAThic > o sbunyte:	Cpbe cklocka perabyte applicINIT_R o, tbt