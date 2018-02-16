/*
*  Copyright (c) 2001 The Regents of the University of Michigan.
*  All rights reserved.
*
*  Kendrick Smith <kmsmith@umich.edu>
*  Andy Adamson <kandros@umich.edu>
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*  1. Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*  2. Redistributions in binary form must reproduce the above copyright
*     notice, this list of conditions and the following disclaimer in the
*     documentation and/or other materials provided with the distribution.
*  3. Neither the name of the University nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
*  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
*  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
*  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
*  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
*  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
*  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
*  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
*  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
*  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
*  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*/

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/ratelimit.h>
#include <linux/sunrpc/svcauth_gss.h>
#include <linux/sunrpc/addr.h>
#include <linux/hash.h>
#include "xdr4.h"
#include "xdr4cb.h"
#include "vfs.h"
#include "current_stateid.h"

#include "netns.h"

#define NFSDDBG_FACILITY                NFSDDBG_PROC

#define all_ones {{~0,~0},~0}
static const stateid_t one_stateid = {
	.si_generation = ~0,
	.si_opaque = all_ones,
};
static const stateid_t zero_stateid = {
	/* all fields zero */
};
static const stateid_t currentstateid = {
	.si_generation = 1,
};

static u64 current_sessionid = 1;

#define ZERO_STATEID(stateid) (!memcmp((stateid), &zero_stateid, sizeof(stateid_t)))
#define ONE_STATEID(stateid)  (!memcmp((stateid), &one_stateid, sizeof(stateid_t)))
#define CURRENT_STATEID(stateid) (!memcmp((stateid), &currentstateid, sizeof(stateid_t)))

/* forward declarations */
static bool check_for_locks(struct nfs4_file *fp, struct nfs4_lockowner *lowner);
static void nfs4_free_ol_stateid(struct nfs4_stid *stid);

/* Locking: */

/*
 * Currently used for the del_recall_lru and file hash table.  In an
 * effort to decrease the scope of the client_mutex, this spinlock may
 * eventually cover more:
 */
static DEFINE_SPINLOCK(state_lock);

/*
 * A waitqueue for all in-progress 4.0 CLOSE operations that are waiting for
 * the refcount on the open stateid to drop.
 */
static DECLARE_WAIT_QUEUE_HEAD(close_wq);

static struct kmem_cache *openowner_slab;
static struct kmem_cache *lockowner_slab;
static struct kmem_cache *file_slab;
static struct kmem_cache *stateid_slab;
static struct kmem_cache *deleg_slab;

static void free_session(struct nfsd4_session *);

static struct nfsd4_callback_ops nfsd4_cb_recall_ops;

static bool is_session_dead(struct nfsd4_session *ses)
{
	return ses->se_flags & NFS4_SESSION_DEAD;
}

static __be32 mark_session_dead_locked(struct nfsd4_session *ses, int ref_held_by_me)
{
	if (atomic_read(&ses->se_ref) > ref_held_by_me)
		return nfserr_jukebox;
	ses->se_flags |= NFS4_SESSION_DEAD;
	return nfs_ok;
}

static bool is_client_expired(struct nfs4_client *clp)
{
	return clp->cl_time == 0;
}

static __be32 get_client_locked(struct nfs4_client *clp)
{
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	lockdep_assert_held(&nn->client_lock);

	if (is_client_expired(clp))
		return nfserr_expired;
	atomic_inc(&clp->cl_refcount);
	return nfs_ok;
}

/* must be called under the client_lock */
static inline void
renew_client_locked(struct nfs4_client *clp)
{
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	if (is_client_expired(clp)) {
		WARN_ON(1);
		printk("%s: client (clientid %08x/%08x) already expired\n",
			__func__,
			clp->cl_clientid.cl_boot,
			clp->cl_clientid.cl_id);
		return;
	}

	dprintk("renewing client (clientid %08x/%08x)\n",
			clp->cl_clientid.cl_boot,
			clp->cl_clientid.cl_id);
	list_move_tail(&clp->cl_lru, &nn->client_lru);
	clp->cl_time = get_seconds();
}

static inline void
renew_client(struct nfs4_client *clp)
{
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	spin_lock(&nn->client_lock);
	renew_client_locked(clp);
	spin_unlock(&nn->client_lock);
}

static void put_client_renew_locked(struct nfs4_client *clp)
{
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	lockdep_assert_held(&nn->client_lock);

	if (!atomic_dec_and_test(&clp->cl_refcount))
		return;
	if (!is_client_expired(clp))
		renew_client_locked(clp);
}

static void put_client_renew(struct nfs4_client *clp)
{
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	if (!atomic_dec_and_lock(&clp->cl_refcount, &nn->client_lock))
		return;
	if (!is_client_expired(clp))
		renew_client_locked(clp);
	spin_unlock(&nn->client_lock);
}

static __be32 nfsd4_get_session_locked(struct nfsd4_session *ses)
{
	__be32 status;

	if (is_session_dead(ses))
		return nfserr_badsession;
	status = get_client_locked(ses->se_client);
	if (status)
		return status;
	atomic_inc(&ses->se_ref);
	return nfs_ok;
}

static void nfsd4_put_session_locked(struct nfsd4_session *ses)
{
	struct nfs4_client *clp = ses->se_client;
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	lockdep_assert_held(&nn->client_lock);

	if (atomic_dec_and_test(&ses->se_ref) && is_session_dead(ses))
		free_session(ses);
	put_client_renew_locked(clp);
}

static void nfsd4_put_session(struct nfsd4_session *ses)
{
	struct nfs4_client *clp = ses->se_client;
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	spin_lock(&nn->client_lock);
	nfsd4_put_session_locked(ses);
	spin_unlock(&nn->client_lock);
}

static inline struct nfs4_stateowner *
nfs4_get_stateowner(struct nfs4_stateowner *sop)
{
	atomic_inc(&sop->so_count);
	return sop;
}

static int
same_owner_str(struct nfs4_stateowner *sop, struct xdr_netobj *owner)
{
	return (sop->so_owner.len == owner->len) &&
		0 == memcmp(sop->so_owner.data, owner->data, owner->len);
}

static struct nfs4_openowner *
find_openstateowner_str_locked(unsigned int hashval, struct nfsd4_open *open,
			struct nfs4_client *clp)
{
	struct nfs4_stateowner *so;

	lockdep_assert_held(&clp->cl_lock);

	list_for_each_entry(so, &clp->cl_ownerstr_hashtbl[hashval],
			    so_strhash) {
		if (!so->so_is_open_owner)
			continue;
		if (same_owner_str(so, &open->op_owner))
			return openowner(nfs4_get_stateowner(so));
	}
	return NULL;
}

static struct nfs4_openowner *
find_openstateowner_str(unsigned int hashval, struct nfsd4_open *open,
			struct nfs4_client *clp)
{
	struct nfs4_openowner *oo;

	spin_lock(&clp->cl_lock);
	oo = find_openstateowner_str_locked(hashval, open, clp);
	spin_unlock(&clp->cl_lock);
	return oo;
}

static inline u32
opaque_hashval(const void *ptr, int nbytes)
{
	unsigned char *cptr = (unsigned char *) ptr;

	u32 x = 0;
	while (nbytes--) {
		x *= 37;
		x += *cptr++;
	}
	return x;
}

static void nfsd4_free_file(struct nfs4_file *f)
{
	kmem_cache_free(file_slab, f);
}

static inline void
put_nfs4_file(struct nfs4_file *fi)
{
	might_lock(&state_lock);

	if (atomic_dec_and_lock(&fi->fi_ref, &state_lock)) {
		hlist_del(&fi->fi_hash);
		spin_unlock(&state_lock);
		nfsd4_free_file(fi);
	}
}

static inline void
get_nfs4_file(struct nfs4_file *fi)
{
	atomic_inc(&fi->fi_ref);
}

static struct file *
__nfs4_get_fd(struct nfs4_file *f, int oflag)
{
	if (f->fi_fds[oflag])
		return get_file(f->fi_fds[oflag]);
	return NULL;
}

static struct file *
find_writeable_file_locked(struct nfs4_file *f)
{
	struct file *ret;

	lockdep_assert_held(&f->fi_lock);

	ret = __nfs4_get_fd(f, O_WRONLY);
	if (!ret)
		ret = __nfs4_get_fd(f, O_RDWR);
	return ret;
}

static struct file *
find_writeable_file(struct nfs4_file *f)
{
	struct file *ret;

	spin_lock(&f->fi_lock);
	ret = find_writeable_file_locked(f);
	spin_unlock(&f->fi_lock);

	return ret;
}

static struct file *find_readable_file_locked(struct nfs4_file *f)
{
	struct file *ret;

	lockdep_assert_held(&f->fi_lock);

	ret = __nfs4_get_fd(f, O_RDONLY);
	if (!ret)
		ret = __nfs4_get_fd(f, O_RDWR);
	return ret;
}

static struct file *
find_readable_file(struct nfs4_file *f)
{
	struct file *ret;

	spin_lock(&f->fi_lock);
	ret = find_readable_file_locked(f);
	spin_unlock(&f->fi_lock);

	return ret;
}

static struct file *
find_any_file(struct nfs4_file *f)
{
	struct file *ret;

	spin_lock(&f->fi_lock);
	ret = __nfs4_get_fd(f, O_RDWR);
	if (!ret) {
		ret = __nfs4_get_fd(f, O_WRONLY);
		if (!ret)
			ret = __nfs4_get_fd(f, O_RDONLY);
	}
	spin_unlock(&f->fi_lock);
	return ret;
}

static atomic_long_t num_delegations;
unsigned long max_delegations;

/*
 * Open owner state (share locks)
 */

/* hash tables for lock and open owners */
#define OWNER_HASH_BITS              8
#define OWNER_HASH_SIZE             (1 << OWNER_HASH_BITS)
#define OWNER_HASH_MASK             (OWNER_HASH_SIZE - 1)

static unsigned int ownerstr_hashval(struct xdr_netobj *ownername)
{
	unsigned int ret;

	ret = opaque_hashval(ownername->data, ownername->len);
	return ret & OWNER_HASH_MASK;
}

/* hash table for nfs4_file */
#define FILE_HASH_BITS                   8
#define FILE_HASH_SIZE                  (1 << FILE_HASH_BITS)

static unsigned int nfsd_fh_hashval(struct knfsd_fh *fh)
{
	return jhash2(fh->fh_base.fh_pad, XDR_QUADLEN(fh->fh_size), 0);
}

static unsigned int file_hashval(struct knfsd_fh *fh)
{
	return nfsd_fh_hashval(fh) & (FILE_HASH_SIZE - 1);
}

static bool nfsd_fh_match(struct knfsd_fh *fh1, struct knfsd_fh *fh2)
{
	return fh1->fh_size == fh2->fh_size &&
		!memcmp(fh1->fh_base.fh_pad,
				fh2->fh_base.fh_pad,
				fh1->fh_size);
}

static struct hlist_head file_hashtbl[FILE_HASH_SIZE];

static void
__nfs4_file_get_access(struct nfs4_file *fp, u32 access)
{
	lockdep_assert_held(&fp->fi_lock);

	if (access & NFS4_SHARE_ACCESS_WRITE)
		atomic_inc(&fp->fi_access[O_WRONLY]);
	if (access & NFS4_SHARE_ACCESS_READ)
		atomic_inc(&fp->fi_access[O_RDONLY]);
}

static __be32
nfs4_file_get_access(struct nfs4_file *fp, u32 access)
{
	lockdep_assert_held(&fp->fi_lock);

	/* Does this access mode make sense? */
	if (access & ~NFS4_SHARE_ACCESS_BOTH)
		return nfserr_inval;

	/* Does it conflict with a deny mode already set? */
	if ((access & fp->fi_share_deny) != 0)
		return nfserr_share_denied;

	__nfs4_file_get_access(fp, access);
	return nfs_ok;
}

static __be32 nfs4_file_check_deny(struct nfs4_file *fp, u32 deny)
{
	/* Common case is that there is no deny mode. */
	if (deny) {
		/* Does this deny mode make sense? */
		if (deny & ~NFS4_SHARE_DENY_BOTH)
			return nfserr_inval;

		if ((deny & NFS4_SHARE_DENY_READ) &&
		    atomic_read(&fp->fi_access[O_RDONLY]))
			return nfserr_share_denied;

		if ((deny & NFS4_SHARE_DENY_WRITE) &&
		    atomic_read(&fp->fi_access[O_WRONLY]))
			return nfserr_share_denied;
	}
	return nfs_ok;
}

static void __nfs4_file_put_access(struct nfs4_file *fp, int oflag)
{
	might_lock(&fp->fi_lock);

	if (atomic_dec_and_lock(&fp->fi_access[oflag], &fp->fi_lock)) {
		struct file *f1 = NULL;
		struct file *f2 = NULL;

		swap(f1, fp->fi_fds[oflag]);
		if (atomic_read(&fp->fi_access[1 - oflag]) == 0)
			swap(f2, fp->fi_fds[O_RDWR]);
		spin_unlock(&fp->fi_lock);
		if (f1)
			fput(f1);
		if (f2)
			fput(f2);
	}
}

static void nfs4_file_put_access(struct nfs4_file *fp, u32 access)
{
	WARN_ON_ONCE(access & ~NFS4_SHARE_ACCESS_BOTH);

	if (access & NFS4_SHARE_ACCESS_WRITE)
		__nfs4_file_put_access(fp, O_WRONLY);
	if (access & NFS4_SHARE_ACCESS_READ)
		__nfs4_file_put_access(fp, O_RDONLY);
}

static struct nfs4_stid *nfs4_alloc_stid(struct nfs4_client *cl,
					 struct kmem_cache *slab)
{
	struct nfs4_stid *stid;
	int new_id;

	stid = kmem_cache_zalloc(slab, GFP_KERNEL);
	if (!stid)
		return NULL;

	idr_preload(GFP_KERNEL);
	spin_lock(&cl->cl_lock);
	new_id = idr_alloc_cyclic(&cl->cl_stateids, stid, 0, 0, GFP_NOWAIT);
	spin_unlock(&cl->cl_lock);
	idr_preload_end();
	if (new_id < 0)
		goto out_free;
	stid->sc_client = cl;
	stid->sc_stateid.si_opaque.so_id = new_id;
	stid->sc_stateid.si_opaque.so_clid = cl->cl_clientid;
	/* Will be incremented before return to client: */
	atomic_set(&stid->sc_count, 1);

	/*
	 * It shouldn't be a problem to reuse an opaque stateid value.
	 * I don't think it is for 4.1.  But with 4.0 I worry that, for
	 * example, a stray write retransmission could be accepted by
	 * the server when it should have been rejected.  Therefore,
	 * adopt a trick from the sctp code to attempt to maximize the
	 * amount of time until an id is reused, by ensuring they always
	 * "increase" (mod INT_MAX):
	 */
	return stid;
out_free:
	kmem_cache_free(slab, stid);
	return NULL;
}

static struct nfs4_ol_stateid * nfs4_alloc_open_stateid(struct nfs4_client *clp)
{
	struct nfs4_stid *stid;
	struct nfs4_ol_stateid *stp;

	stid = nfs4_alloc_stid(clp, stateid_slab);
	if (!stid)
		return NULL;

	stp = openlockstateid(stid);
	stp->st_stid.sc_free = nfs4_free_ol_stateid;
	return stp;
}

static void nfs4_free_deleg(struct nfs4_stid *stid)
{
	kmem_cache_free(deleg_slab, stid);
	atomic_long_dec(&num_delegations);
}

/*
 * When we recall a delegation, we should be careful not to hand it
 * out again straight away.
 * To ensure this we keep a pair of bloom filters ('new' and 'old')
 * in which the filehandles of recalled delegations are "stored".
 * If a filehandle appear in either filter, a delegation is blocked.
 * When a delegation is recalled, the filehandle is stored in the "new"
 * filter.
 * Every 30 seconds we swap the filters and clear the "new" one,
 * unless both are empty of course.
 *
 * Each filter is 256 bits.  We hash the filehandle to 32bit and use the
 * low 3 bytes as hash-table indices.
 *
 * 'blocked_delegations_lock', which is always taken in block_delegations(),
 * is used to manage concurrent access.  Testing does not need the lock
 * except when swapping the two filters.
 */
static DEFINE_SPINLOCK(blocked_delegations_lock);
static struct bloom_pair {
	int	entries, old_entries;
	time_t	swap_time;
	int	new; /* index into 'set' */
	DECLARE_BITMAP(set[2], 256);
} blocked_delegations;

static int delegation_blocked(struct knfsd_fh *fh)
{
	u32 hash;
	struct bloom_pair *bd = &blocked_delegations;

	if (bd->entries == 0)
		return 0;
	if (seconds_since_boot() - bd->swap_time > 30) {
		spin_lock(&blocked_delegations_lock);
		if (seconds_since_boot() - bd->swap_time > 30) {
			bd->entries -= bd->old_entries;
			bd->old_entries = bd->entries;
			memset(bd->set[bd->new], 0,
			       sizeof(bd->set[0]));
			bd->new = 1-bd->new;
			bd->swap_time = seconds_since_boot();
		}
		spin_unlock(&blocked_delegations_lock);
	}
	hash = arch_fast_hash(&fh->fh_base, fh->fh_size, 0);
	if (test_bit(hash&255, bd->set[0]) &&
	    test_bit((hash>>8)&255, bd->set[0]) &&
	    test_bit((hash>>16)&255, bd->set[0]))
		return 1;

	if (test_bit(hash&255, bd->set[1]) &&
	    test_bit((hash>>8)&255, bd->set[1]) &&
	    test_bit((hash>>16)&255, bd->set[1]))
		return 1;

	return 0;
}

static void block_delegations(struct knfsd_fh *fh)
{
	u32 hash;
	struct bloom_pair *bd = &blocked_delegations;

	hash = arch_fast_hash(&fh->fh_base, fh->fh_size, 0);

	spin_lock(&blocked_delegations_lock);
	__set_bit(hash&255, bd->set[bd->new]);
	__set_bit((hash>>8)&255, bd->set[bd->new]);
	__set_bit((hash>>16)&255, bd->set[bd->new]);
	if (bd->entries == 0)
		bd->swap_time = seconds_since_boot();
	bd->entries += 1;
	spin_unlock(&blocked_delegations_lock);
}

static struct nfs4_delegation *
alloc_init_deleg(struct nfs4_client *clp, struct svc_fh *current_fh)
{
	struct nfs4_delegation *dp;
	long n;

	dprintk("NFSD alloc_init_deleg\n");
	n = atomic_long_inc_return(&num_delegations);
	if (n < 0 || n > max_delegations)
		goto out_dec;
	if (delegation_blocked(&current_fh->fh_handle))
		goto out_dec;
	dp = delegstateid(nfs4_alloc_stid(clp, deleg_slab));
	if (dp == NULL)
		goto out_dec;

	dp->dl_stid.sc_free = nfs4_free_deleg;
	/*
	 * delegation seqid's are never incremented.  The 4.1 special
	 * meaning of seqid 0 isn't meaningful, really, but let's avoid
	 * 0 anyway just for consistency and use 1:
	 */
	dp->dl_stid.sc_stateid.si_generation = 1;
	INIT_LIST_HEAD(&dp->dl_perfile);
	INIT_LIST_HEAD(&dp->dl_perclnt);
	INIT_LIST_HEAD(&dp->dl_recall_lru);
	dp->dl_type = NFS4_OPEN_DELEGATE_READ;
	dp->dl_retries = 1;
	nfsd4_init_cb(&dp->dl_recall, dp->dl_stid.sc_client,
		      &nfsd4_cb_recall_ops, NFSPROC4_CLNT_CB_RECALL);
	return dp;
out_dec:
	atomic_long_dec(&num_delegations);
	return NULL;
}

void
nfs4_put_stid(struct nfs4_stid *s)
{
	struct nfs4_file *fp = s->sc_file;
	struct nfs4_client *clp = s->sc_client;

	might_lock(&clp->cl_lock);

	if (!atomic_dec_and_lock(&s->sc_count, &clp->cl_lock)) {
		wake_up_all(&close_wq);
		return;
	}
	idr_remove(&clp->cl_stateids, s->sc_stateid.si_opaque.so_id);
	spin_unlock(&clp->cl_lock);
	s->sc_free(s);
	if (fp)
		put_nfs4_file(fp);
}

static void nfs4_put_deleg_lease(struct nfs4_file *fp)
{
	struct file *filp = NULL;

	spin_lock(&fp->fi_lock);
	if (fp->fi_deleg_file && atomic_dec_and_test(&fp->fi_delegees))
		swap(filp, fp->fi_deleg_file);
	spin_unlock(&fp->fi_lock);

	if (filp) {
		vfs_setlease(filp, F_UNLCK, NULL, NULL);
		fput(filp);
	}
}

static void unhash_stid(struct nfs4_stid *s)
{
	s->sc_type = 0;
}

static void
hash_delegation_locked(struct nfs4_delegation *dp, struct nfs4_file *fp)
{
	lockdep_assert_held(&state_lock);
	lockdep_assert_held(&fp->fi_lock);

	atomic_inc(&dp->dl_stid.sc_count);
	dp->dl_stid.sc_type = NFS4_DELEG_STID;
	list_add(&dp->dl_perfile, &fp->fi_delegations);
	list_add(&dp->dl_perclnt, &dp->dl_stid.sc_client->cl_delegations);
}

static void
unhash_delegation_locked(struct nfs4_delegation *dp)
{
	struct nfs4_file *fp = dp->dl_stid.sc_file;

	lockdep_assert_held(&state_lock);

	dp->dl_stid.sc_type = NFS4_CLOSED_DELEG_STID;
	/* Ensure that deleg break won't try to requeue it */
	++dp->dl_time;
	spin_lock(&fp->fi_lock);
	list_del_init(&dp->dl_perclnt);
	list_del_init(&dp->dl_recall_lru);
	list_del_init(&dp->dl_perfile);
	spin_unlock(&fp->fi_lock);
}

static void destroy_delegation(struct nfs4_delegation *dp)
{
	spin_lock(&state_lock);
	unhash_delegation_locked(dp);
	spin_unlock(&state_lock);
	nfs4_put_deleg_lease(dp->dl_stid.sc_file);
	nfs4_put_stid(&dp->dl_stid);
}

static void revoke_delegation(struct nfs4_delegation *dp)
{
	struct nfs4_client *clp = dp->dl_stid.sc_client;

	WARN_ON(!list_empty(&dp->dl_recall_lru));

	nfs4_put_deleg_lease(dp->dl_stid.sc_file);

	if (clp->cl_minorversion == 0)
		nfs4_put_stid(&dp->dl_stid);
	else {
		dp->dl_stid.sc_type = NFS4_REVOKED_DELEG_STID;
		spin_lock(&clp->cl_lock);
		list_add(&dp->dl_recall_lru, &clp->cl_revoked);
		spin_unlock(&clp->cl_lock);
	}
}

/* 
 * SETCLIENTID state 
 */

static unsigned int clientid_hashval(u32 id)
{
	return id & CLIENT_HASH_MASK;
}

static unsigned int clientstr_hashval(const char *name)
{
	return opaque_hashval(name, 8) & CLIENT_HASH_MASK;
}

/*
 * We store the NONE, READ, WRITE, and BOTH bits separately in the
 * st_{access,deny}_bmap field of the stateid, in order to track not
 * only what share bits are currently in force, but also what
 * combinations of share bits previous opens have used.  This allows us
 * to enforce the recommendation of rfc 3530 14.2.19 that the server
 * return an error if the client attempt to downgrade to a combination
 * of share bits not explicable by closing some of its previous opens.
 *
 * XXX: This enforcement is actually incomplete, since we don't keep
 * track of access/deny bit combinations; so, e.g., we allow:
 *
 *	OPEN allow read, deny write
 *	OPEN allow both, deny none
 *	DOWNGRADE allow read, deny none
 *
 * which we should reject.
 */
static unsigned int
bmap_to_share_mode(unsigned long bmap) {
	int i;
	unsigned int access = 0;

	for (i = 1; i < 4; i++) {
		if (test_bit(i, &bmap))
			access |= i;
	}
	return access;
}

/* set share access for a given stateid */
static inline void
set_access(u32 access, struct nfs4_ol_stateid *stp)
{
	unsigned char mask = 1 << access;

	WARN_ON_ONCE(access > NFS4_SHARE_ACCESS_BOTH);
	stp->st_access_bmap |= mask;
}

/* clear share access for a given stateid */
static inline void
clear_access(u32 access, struct nfs4_ol_stateid *stp)
{
	unsigned char mask = 1 << access;

	WARN_ON_ONCE(access > NFS4_SHARE_ACCESS_BOTH);
	stp->st_access_bmap &= ~mask;
}

/* test whether a given stateid has access */
static inline bool
test_access(u32 access, struct nfs4_ol_stateid *stp)
{
	unsigned char mask = 1 << access;

	return (bool)(stp->st_access_bmap & mask);
}

/* set share deny for a given stateid */
static inline void
set_deny(u32 deny, struct nfs4_ol_stateid *stp)
{
	unsigned char mask = 1 << deny;

	WARN_ON_ONCE(deny > NFS4_SHARE_DENY_BOTH);
	stp->st_deny_bmap |= mask;
}

/* clear share deny for a given stateid */
static inline void
clear_deny(u32 deny, struct nfs4_ol_stateid *stp)
{
	unsigned char mask = 1 << deny;

	WARN_ON_ONCE(deny > NFS4_SHARE_DENY_BOTH);
	stp->st_deny_bmap &= ~mask;
}

/* test whether a given stateid is denying specific access */
static inline bool
test_deny(u32 deny, struct nfs4_ol_stateid *stp)
{
	unsigned char mask = 1 << deny;

	return (bool)(stp->st_deny_bmap & mask);
}

static int nfs4_access_to_omode(u32 access)
{
	switch (access & NFS4_SHARE_ACCESS_BOTH) {
	case NFS4_SHARE_ACCESS_READ:
		return O_RDONLY;
	case NFS4_SHARE_ACCESS_WRITE:
		return O_WRONLY;
	case NFS4_SHARE_ACCESS_BOTH:
		return O_RDWR;
	}
	WARN_ON_ONCE(1);
	return O_RDONLY;
}

/*
 * A stateid that had a deny mode associated with it is being released
 * or downgraded. Recalculate the deny mode on the file.
 */
static void
recalculate_deny_mode(struct nfs4_file *fp)
{
	struct nfs4_ol_stateid *stp;

	spin_lock(&fp->fi_lock);
	fp->fi_share_deny = 0;
	list_for_each_entry(stp, &fp->fi_stateids, st_perfile)
		fp->fi_share_deny |= bmap_to_share_mode(stp->st_deny_bmap);
	spin_unlock(&fp->fi_lock);
}

static void
reset_union_bmap_deny(u32 deny, struct nfs4_ol_stateid *stp)
{
	int i;
	bool change = false;

	for (i = 1; i < 4; i++) {
		if ((i & deny) != i) {
			change = true;
			clear_deny(i, stp);
		}
	}

	/* Recalculate per-file deny mode if there was a change */
	if (change)
		recalculate_deny_mode(stp->st_stid.sc_file);
}

/* release all access and file references for a given stateid */
static void
release_all_access(struct nfs4_ol_stateid *stp)
{
	int i;
	struct nfs4_file *fp = stp->st_stid.sc_file;

	if (fp && stp->st_deny_bmap != 0)
		recalculate_deny_mode(fp);

	for (i = 1; i < 4; i++) {
		if (test_access(i, stp))
			nfs4_file_put_access(stp->st_stid.sc_file, i);
		clear_access(i, stp);
	}
}

static void nfs4_put_stateowner(struct nfs4_stateowner *sop)
{
	struct nfs4_client *clp = sop->so_client;

	might_lock(&clp->cl_lock);

	if (!atomic_dec_and_lock(&sop->so_count, &clp->cl_lock))
		return;
	sop->so_ops->so_unhash(sop);
	spin_unlock(&clp->cl_lock);
	kfree(sop->so_owner.data);
	sop->so_ops->so_free(sop);
}

static void unhash_ol_stateid(struct nfs4_ol_stateid *stp)
{
	struct nfs4_file *fp = stp->st_stid.sc_file;

	lockdep_assert_held(&stp->st_stateowner->so_client->cl_lock);

	spin_lock(&fp->fi_lock);
	list_del(&stp->st_perfile);
	spin_unlock(&fp->fi_lock);
	list_del(&stp->st_perstateowner);
}

static void nfs4_free_ol_stateid(struct nfs4_stid *stid)
{
	struct nfs4_ol_stateid *stp = openlockstateid(stid);

	release_all_access(stp);
	if (stp->st_stateowner)
		nfs4_put_stateowner(stp->st_stateowner);
	kmem_cache_free(stateid_slab, stid);
}

static void nfs4_free_lock_stateid(struct nfs4_stid *stid)
{
	struct nfs4_ol_stateid *stp = openlockstateid(stid);
	struct nfs4_lockowner *lo = lockowner(stp->st_stateowner);
	struct file *file;

	file = find_any_file(stp->st_stid.sc_file);
	if (file)
		filp_close(file, (fl_owner_t)lo);
	nfs4_free_ol_stateid(stid);
}

/*
 * Put the persistent reference to an already unhashed generic stateid, while
 * holding the cl_lock. If it's the last reference, then put it onto the
 * reaplist for later destruction.
 */
static void put_ol_stateid_locked(struct nfs4_ol_stateid *stp,
				       struct list_head *reaplist)
{
	struct nfs4_stid *s = &stp->st_stid;
	struct nfs4_client *clp = s->sc_client;

	lockdep_assert_held(&clp->cl_lock);

	WARN_ON_ONCE(!list_empty(&stp->st_locks));

	if (!atomic_dec_and_test(&s->sc_count)) {
		wake_up_all(&close_wq);
		return;
	}

	idr_remove(&clp->cl_stateids, s->sc_stateid.si_opaque.so_id);
	list_add(&stp->st_locks, reaplist);
}

static void unhash_lock_stateid(struct nfs4_ol_stateid *stp)
{
	struct nfs4_openowner *oo = openowner(stp->st_openstp->st_stateowner);

	lockdep_assert_held(&oo->oo_owner.so_client->cl_lock);

	list_del_init(&stp->st_locks);
	unhash_ol_stateid(stp);
	unhash_stid(&stp->st_stid);
}

static void release_lock_stateid(struct nfs4_ol_stateid *stp)
{
	struct nfs4_openowner *oo = openowner(stp->st_openstp->st_stateowner);

	spin_lock(&oo->oo_owner.so_client->cl_lock);
	unhash_lock_stateid(stp);
	spin_unlock(&oo->oo_owner.so_client->cl_lock);
	nfs4_put_stid(&stp->st_stid);
}

static void unhash_lockowner_locked(struct nfs4_lockowner *lo)
{
	struct nfs4_client *clp = lo->lo_owner.so_client;

	lockdep_assert_held(&clp->cl_lock);

	list_del_init(&lo->lo_owner.so_strhash);
}

/*
 * Free a list of generic stateids that were collected earlier after being
 * fully unhashed.
 */
static void
free_ol_stateid_reaplist(struct list_head *reaplist)
{
	struct nfs4_ol_stateid *stp;
	struct nfs4_file *fp;

	might_sleep();

	while (!list_empty(reaplist)) {
		stp = list_first_entry(reaplist, struct nfs4_ol_stateid,
				       st_locks);
		list_del(&stp->st_locks);
		fp = stp->st_stid.sc_file;
		stp->st_stid.sc_free(&stp->st_stid);
		if (fp)
			put_nfs4_file(fp);
	}
}

static void release_lockowner(struct nfs4_lockowner *lo)
{
	struct nfs4_client *clp = lo->lo_owner.so_client;
	struct nfs4_ol_stateid *stp;
	struct list_head reaplist;

	INIT_LIST_HEAD(&reaplist);

	spin_lock(&clp->cl_lock);
	unhash_lockowner_locked(lo);
	while (!list_empty(&lo->lo_owner.so_stateids)) {
		stp = list_first_entry(&lo->lo_owner.so_stateids,
				struct nfs4_ol_stateid, st_perstateowner);
		unhash_lock_stateid(stp);
		put_ol_stateid_locked(stp, &reaplist);
	}
	spin_unlock(&clp->cl_lock);
	free_ol_stateid_reaplist(&reaplist);
	nfs4_put_stateowner(&lo->lo_owner);
}

static void release_open_stateid_locks(struct nfs4_ol_stateid *open_stp,
				       struct list_head *reaplist)
{
	struct nfs4_ol_stateid *stp;

	while (!list_empty(&open_stp->st_locks)) {
		stp = list_entry(open_stp->st_locks.next,
				struct nfs4_ol_stateid, st_locks);
		unhash_lock_stateid(stp);
		put_ol_stateid_locked(stp, reaplist);
	}
}

static void unhash_open_stateid(struct nfs4_ol_stateid *stp,
				struct list_head *reaplist)
{
	lockdep_assert_held(&stp->st_stid.sc_client->cl_lock);

	unhash_ol_stateid(stp);
	release_open_stateid_locks(stp, reaplist);
}

static void release_open_stateid(struct nfs4_ol_stateid *stp)
{
	LIST_HEAD(reaplist);

	spin_lock(&stp->st_stid.sc_client->cl_lock);
	unhash_open_stateid(stp, &reaplist);
	put_ol_stateid_locked(stp, &reaplist);
	spin_unlock(&stp->st_stid.sc_client->cl_lock);
	free_ol_stateid_reaplist(&reaplist);
}

static void unhash_openowner_locked(struct nfs4_openowner *oo)
{
	struct nfs4_client *clp = oo->oo_owner.so_client;

	lockdep_assert_held(&clp->cl_lock);

	list_del_init(&oo->oo_owner.so_strhash);
	list_del_init(&oo->oo_perclient);
}

static void release_last_closed_stateid(struct nfs4_openowner *oo)
{
	struct nfsd_net *nn = net_generic(oo->oo_owner.so_client->net,
					  nfsd_net_id);
	struct nfs4_ol_stateid *s;

	spin_lock(&nn->client_lock);
	s = oo->oo_last_closed_stid;
	if (s) {
		list_del_init(&oo->oo_close_lru);
		oo->oo_last_closed_stid = NULL;
	}
	spin_unlock(&nn->client_lock);
	if (s)
		nfs4_put_stid(&s->st_stid);
}

static void release_openowner(struct nfs4_openowner *oo)
{
	struct nfs4_ol_stateid *stp;
	struct nfs4_client *clp = oo->oo_owner.so_client;
	struct list_head reaplist;

	INIT_LIST_HEAD(&reaplist);

	spin_lock(&clp->cl_lock);
	unhash_openowner_locked(oo);
	while (!list_empty(&oo->oo_owner.so_stateids)) {
		stp = list_first_entry(&oo->oo_owner.so_stateids,
				struct nfs4_ol_stateid, st_perstateowner);
		unhash_open_stateid(stp, &reaplist);
		put_ol_stateid_locked(stp, &reaplist);
	}
	spin_unlock(&clp->cl_lock);
	free_ol_stateid_reaplist(&reaplist);
	release_last_closed_stateid(oo);
	nfs4_put_stateowner(&oo->oo_owner);
}

static inline int
hash_sessionid(struct nfs4_sessionid *sessionid)
{
	struct nfsd4_sessionid *sid = (struct nfsd4_sessionid *)sessionid;

	return sid->sequence % SESSION_HASH_SIZE;
}

#ifdef NFSD_DEBUG
static inline void
dump_sessionid(const char *fn, struct nfs4_sessionid *sessionid)
{
	u32 *ptr = (u32 *)(&sessionid->data[0]);
	dprintk("%s: %u:%u:%u:%u\n", fn, ptr[0], ptr[1], ptr[2], ptr[3]);
}
#else
static inline void
dump_sessionid(const char *fn, struct nfs4_sessionid *sessionid)
{
}
#endif

/*
 * Bump the seqid on cstate->replay_owner, and clear replay_owner if it
 * won't be used for replay.
 */
void nfsd4_bump_seqid(struct nfsd4_compound_state *cstate, __be32 nfserr)
{
	struct nfs4_stateowner *so = cstate->replay_owner;

	if (nfserr == nfserr_replay_me)
		return;

	if (!seqid_mutating_err(ntohl(nfserr))) {
		nfsd4_cstate_clear_replay(cstate);
		return;
	}
	if (!so)
		return;
	if (so->so_is_open_owner)
		release_last_closed_stateid(openowner(so));
	so->so_seqid++;
	return;
}

static void
gen_sessionid(struct nfsd4_session *ses)
{
	struct nfs4_client *clp = ses->se_client;
	struct nfsd4_sessionid *sid;

	sid = (struct nfsd4_sessionid *)ses->se_sessionid.data;
	sid->clientid = clp->cl_clientid;
	sid->sequence = current_sessionid++;
	sid->reserved = 0;
}

/*
 * The protocol defines ca_maxresponssize_cached to include the size of
 * the rpc header, but all we need to cache is the data starting after
 * the end of the initial SEQUENCE operation--the rest we regenerate
 * each time.  Therefore we can advertise a ca_maxresponssize_cached
 * value that is the number of bytes in our cache plus a few additional
 * bytes.  In order to stay on the safe side, and not promise more than
 * we can cache, those additional bytes must be the minimum possible: 24
 * bytes of rpc header (xid through accept state, with AUTH_NULL
 * verifier), 12 for the compound header (with zero-length tag), and 44
 * for the SEQUENCE op response:
 */
#define NFSD_MIN_HDR_SEQ_SZ  (24 + 12 + 44)

static void
free_session_slots(struct nfsd4_session *ses)
{
	int i;

	for (i = 0; i < ses->se_fchannel.maxreqs; i++)
		kfree(ses->se_slots[i]);
}

/*
 * We don't actually need to cache the rpc and session headers, so we
 * can allocate a little less for each slot:
 */
static inline u32 slot_bytes(struct nfsd4_channel_attrs *ca)
{
	u32 size;

	if (ca->maxresp_cached < NFSD_MIN_HDR_SEQ_SZ)
		size = 0;
	else
		size = ca->maxresp_cached - NFSD_MIN_HDR_SEQ_SZ;
	return size + sizeof(struct nfsd4_slot);
}

/*
 * XXX: If we run out of reserved DRC memory we could (up to a point)
 * re-negotiate active sessions and reduce their slot usage to make
 * room for new connections. For now we just fail the create session.
 */
static u32 nfsd4_get_drc_mem(struct nfsd4_channel_attrs *ca)
{
	u32 slotsize = slot_bytes(ca);
	u32 num = ca->maxreqs;
	int avail;

	spin_lock(&nfsd_drc_lock);
	avail = min((unsigned long)NFSD_MAX_MEM_PER_SESSION,
		    nfsd_drc_max_mem - nfsd_drc_mem_used);
	num = min_t(int, num, avail / slotsize);
	nfsd_drc_mem_used += num * slotsize;
	spin_unlock(&nfsd_drc_lock);

	return num;
}

static void nfsd4_put_drc_mem(struct nfsd4_channel_attrs *ca)
{
	int slotsize = slot_bytes(ca);

	spin_lock(&nfsd_drc_lock);
	nfsd_drc_mem_used -= slotsize * ca->maxreqs;
	spin_unlock(&nfsd_drc_lock);
}

static struct nfsd4_session *alloc_session(struct nfsd4_channel_attrs *fattrs,
					   struct nfsd4_channel_attrs *battrs)
{
	int numslots = fattrs->maxreqs;
	int slotsize = slot_bytes(fattrs);
	struct nfsd4_session *new;
	int mem, i;

	BUILD_BUG_ON(NFSD_MAX_SLOTS_PER_SESSION * sizeof(struct nfsd4_slot *)
			+ sizeof(struct nfsd4_session) > PAGE_SIZE);
	mem = numslots * sizeof(struct nfsd4_slot *);

	new = kzalloc(sizeof(*new) + mem, GFP_KERNEL);
	if (!new)
		return NULL;
	/* allocate each struct nfsd4_slot and data cache in one piece */
	for (i = 0; i < numslots; i++) {
		new->se_slots[i] = kzalloc(slotsize, GFP_KERNEL);
		if (!new->se_slots[i])
			goto out_free;
	}

	memcpy(&new->se_fchannel, fattrs, sizeof(struct nfsd4_channel_attrs));
	memcpy(&new->se_bchannel, battrs, sizeof(struct nfsd4_channel_attrs));

	return new;
out_free:
	while (i--)
		kfree(new->se_slots[i]);
	kfree(new);
	return NULL;
}

static void free_conn(struct nfsd4_conn *c)
{
	svc_xprt_put(c->cn_xprt);
	kfree(c);
}

static void nfsd4_conn_lost(struct svc_xpt_user *u)
{
	struct nfsd4_conn *c = container_of(u, struct nfsd4_conn, cn_xpt_user);
	struct nfs4_client *clp = c->cn_session->se_client;

	spin_lock(&clp->cl_lock);
	if (!list_empty(&c->cn_persession)) {
		list_del(&c->cn_persession);
		free_conn(c);
	}
	nfsd4_probe_callback(clp);
	spin_unlock(&clp->cl_lock);
}

static struct nfsd4_conn *alloc_conn(struct svc_rqst *rqstp, u32 flags)
{
	struct nfsd4_conn *conn;

	conn = kmalloc(sizeof(struct nfsd4_conn), GFP_KERNEL);
	if (!conn)
		return NULL;
	svc_xprt_get(rqstp->rq_xprt);
	conn->cn_xprt = rqstp->rq_xprt;
	conn->cn_flags = flags;
	INIT_LIST_HEAD(&conn->cn_xpt_user.list);
	return conn;
}

static void __nfsd4_hash_conn(struct nfsd4_conn *conn, struct nfsd4_session *ses)
{
	conn->cn_session = ses;
	list_add(&conn->cn_persession, &ses->se_conns);
}

static void nfsd4_hash_conn(struct nfsd4_conn *conn, struct nfsd4_session *ses)
{
	struct nfs4_client *clp = ses->se_client;

	spin_lock(&clp->cl_lock);
	__nfsd4_hash_conn(conn, ses);
	spin_unlock(&clp->cl_lock);
}

static int nfsd4_register_conn(struct nfsd4_conn *conn)
{
	conn->cn_xpt_user.callback = nfsd4_conn_lost;
	return register_xpt_user(conn->cn_xprt, &conn->cn_xpt_user);
}

static void nfsd4_init_conn(struct svc_rqst *rqstp, struct nfsd4_conn *conn, struct nfsd4_session *ses)
{
	int ret;

	nfsd4_hash_conn(conn, ses);
	ret = nfsd4_register_conn(conn);
	if (ret)
		/* oops; xprt is already down: */
		nfsd4_conn_lost(&conn->cn_xpt_user);
	/* We may have gained or lost a callback channel: */
	nfsd4_probe_callback_sync(ses->se_client);
}

static struct nfsd4_conn *alloc_conn_from_crses(struct svc_rqst *rqstp, struct nfsd4_create_session *cses)
{
	u32 dir = NFS4_CDFC4_FORE;

	if (cses->flags & SESSION4_BACK_CHAN)
		dir |= NFS4_CDFC4_BACK;
	return alloc_conn(rqstp, dir);
}

/* must be called under client_lock */
static void nfsd4_del_conns(struct nfsd4_session *s)
{
	struct nfs4_client *clp = s->se_client;
	struct nfsd4_conn *c;

	spin_lock(&clp->cl_lock);
	while (!list_empty(&s->se_conns)) {
		c = list_first_entry(&s->se_conns, struct nfsd4_conn, cn_persession);
		list_del_init(&c->cn_persession);
		spin_unlock(&clp->cl_lock);

		unregister_xpt_user(c->cn_xprt, &c->cn_xpt_user);
		free_conn(c);

		spin_lock(&clp->cl_lock);
	}
	spin_unlock(&clp->cl_lock);
}

static void __free_session(struct nfsd4_session *ses)
{
	free_session_slots(ses);
	kfree(ses);
}

static void free_session(struct nfsd4_session *ses)
{
	nfsd4_del_conns(ses);
	nfsd4_put_drc_mem(&ses->se_fchannel);
	__free_session(ses);
}

static void init_session(struct svc_rqst *rqstp, struct nfsd4_session *new, struct nfs4_client *clp, struct nfsd4_create_session *cses)
{
	int idx;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	new->se_client = clp;
	gen_sessionid(new);

	INIT_LIST_HEAD(&new->se_conns);

	new->se_cb_seq_nr = 1;
	new->se_flags = cses->flags;
	new->se_cb_prog = cses->callback_prog;
	new->se_cb_sec = cses->cb_sec;
	atomic_set(&new->se_ref, 0);
	idx = hash_sessionid(&new->se_sessionid);
	list_add(&new->se_hash, &nn->sessionid_hashtbl[idx]);
	spin_lock(&clp->cl_lock);
	list_add(&new->se_perclnt, &clp->cl_sessions);
	spin_unlock(&clp->cl_lock);

	if (cses->flags & SESSION4_BACK_CHAN) {
		struct sockaddr *sa = svc_addr(rqstp);
		/*
		 * This is a little silly; with sessions there's no real
		 * use for the callback address.  Use the peer address
		 * as a reasonable default for now, but consider fixing
		 * the rpc client not to require an address in the
		 * future:
		 */
		rpc_copy_addr((struct sockaddr *)&clp->cl_cb_conn.cb_addr, sa);
		clp->cl_cb_conn.cb_addrlen = svc_addr_len(sa);
	}
}

/* caller must hold client_lock */
static struct nfsd4_session *
__find_in_sessionid_hashtbl(struct nfs4_sessionid *sessionid, struct net *net)
{
	struct nfsd4_session *elem;
	int idx;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	lockdep_assert_held(&nn->client_lock);

	dump_sessionid(__func__, sessionid);
	idx = hash_sessionid(sessionid);
	/* Search in the appropriate list */
	list_for_each_entry(elem, &nn->sessionid_hashtbl[idx], se_hash) {
		if (!memcmp(elem->se_sessionid.data, sessionid->data,
			    NFS4_MAX_SESSIONID_LEN)) {
			return elem;
		}
	}

	dprintk("%s: session not found\n", __func__);
	return NULL;
}

static struct nfsd4_session *
find_in_sessionid_hashtbl(struct nfs4_sessionid *sessionid, struct net *net,
		__be32 *ret)
{
	struct nfsd4_session *session;
	__be32 status = nfserr_badsession;

	session = __find_in_sessionid_hashtbl(sessionid, net);
	if (!session)
		goto out;
	status = nfsd4_get_session_locked(session);
	if (status)
		session = NULL;
out:
	*ret = status;
	return session;
}

/* caller must hold client_lock */
static void
unhash_session(struct nfsd4_session *ses)
{
	struct nfs4_client *clp = ses->se_client;
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	lockdep_assert_held(&nn->client_lock);

	list_del(&ses->se_hash);
	spin_lock(&ses->se_client->cl_lock);
	list_del(&ses->se_perclnt);
	spin_unlock(&ses->se_client->cl_lock);
}

/* SETCLIENTID and SETCLIENTID_CONFIRM Helper functions */
static int
STALE_CLIENTID(clientid_t *clid, struct nfsd_net *nn)
{
	if (clid->cl_boot == nn->boot_time)
		return 0;
	dprintk("NFSD stale clientid (%08x/%08x) boot_time %08lx\n",
		clid->cl_boot, clid->cl_id, nn->boot_time);
	return 1;
}

/* 
 * XXX Should we use a slab cache ?
 * This type of memory management is somewhat inefficient, but we use it
 * anyway since SETCLIENTID is not a common operation.
 */
static struct nfs4_client *alloc_client(struct xdr_netobj name)
{
	struct nfs4_client *clp;
	int i;

	clp = kzalloc(sizeof(struct nfs4_client), GFP_KERNEL);
	if (clp == NULL)
		return NULL;
	clp->cl_name.data = kmemdup(name.data, name.len, GFP_KERNEL);
	if (clp->cl_name.data == NULL)
		goto err_no_name;
	clp->cl_ownerstr_hashtbl = kmalloc(sizeof(struct list_head) *
			OWNER_HASH_SIZE, GFP_KERNEL);
	if (!clp->cl_ownerstr_hashtbl)
		goto err_no_hashtbl;
	for (i = 0; i < OWNER_HASH_SIZE; i++)
		INIT_LIST_HEAD(&clp->cl_ownerstr_hashtbl[i]);
	clp->cl_name.len = name.len;
	INIT_LIST_HEAD(&clp->cl_sessions);
	idr_init(&clp->cl_stateids);
	atomic_set(&clp->cl_refcount, 0);
	clp->cl_cb_state = NFSD4_CB_UNKNOWN;
	INIT_LIST_HEAD(&clp->cl_idhash);
	INIT_LIST_HEAD(&clp->cl_openowners);
	INIT_LIST_HEAD(&clp->cl_delegations);
	INIT_LIST_HEAD(&clp->cl_lru);
	INIT_LIST_HEAD(&clp->cl_callbacks);
	INIT_LIST_HEAD(&clp->cl_revoked);
	spin_lock_init(&clp->cl_lock);
	rpc_init_wait_queue(&clp->cl_cb_waitq, "Backchannel slot table");
	return clp;
err_no_hashtbl:
	kfree(clp->cl_name.data);
err_no_name:
	kfree(clp);
	return NULL;
}

static void
free_client(struct nfs4_client *clp)
{
	while (!list_empty(&clp->cl_sessions)) {
		struct nfsd4_session *ses;
		ses = list_entry(clp->cl_sessions.next, struct nfsd4_session,
				se_perclnt);
		list_del(&ses->se_perclnt);
		WARN_ON_ONCE(atomic_read(&ses->se_ref));
		free_session(ses);
	}
	rpc_destroy_wait_queue(&clp->cl_cb_waitq);
	free_svc_cred(&clp->cl_cred);
	kfree(clp->cl_ownerstr_hashtbl);
	kfree(clp->cl_name.data);
	idr_destroy(&clp->cl_stateids);
	kfree(clp);
}

/* must be called under the client_lock */
static void
unhash_client_locked(struct nfs4_client *clp)
{
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);
	struct nfsd4_session *ses;

	lockdep_assert_held(&nn->client_lock);

	/* Mark the client as expired! */
	clp->cl_time = 0;
	/* Make it invisible */
	if (!list_empty(&clp->cl_idhash)) {
		list_del_init(&clp->cl_idhash);
		if (test_bit(NFSD4_CLIENT_CONFIRMED, &clp->cl_flags))
			rb_erase(&clp->cl_namenode, &nn->conf_name_tree);
		else
			rb_erase(&clp->cl_namenode, &nn->unconf_name_tree);
	}
	list_del_init(&clp->cl_lru);
	spin_lock(&clp->cl_lock);
	list_for_each_entry(ses, &clp->cl_sessions, se_perclnt)
		list_del_init(&ses->se_hash);
	spin_unlock(&clp->cl_lock);
}

static void
unhash_client(struct nfs4_client *clp)
{
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	spin_lock(&nn->client_lock);
	unhash_client_locked(clp);
	spin_unlock(&nn->client_lock);
}

static __be32 mark_client_expired_locked(struct nfs4_client *clp)
{
	if (atomic_read(&clp->cl_refcount))
		return nfserr_jukebox;
	unhash_client_locked(clp);
	return nfs_ok;
}

static void
__destroy_client(struct nfs4_client *clp)
{
	struct nfs4_openowner *oo;
	struct nfs4_delegation *dp;
	struct list_head reaplist;

	INIT_LIST_HEAD(&reaplist);
	spin_lock(&state_lock);
	while (!list_empty(&clp->cl_delegations)) {
		dp = list_entry(clp->cl_delegations.next, struct nfs4_delegation, dl_perclnt);
		unhash_delegation_locked(dp);
		list_add(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&state_lock);
	while (!list_empty(&reaplist)) {
		dp = list_entry(reaplist.next, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		nfs4_put_deleg_lease(dp->dl_stid.sc_file);
		nfs4_put_stid(&dp->dl_stid);
	}
	while (!list_empty(&clp->cl_revoked)) {
		dp = list_entry(clp->cl_revoked.next, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		nfs4_put_stid(&dp->dl_stid);
	}
	while (!list_empty(&clp->cl_openowners)) {
		oo = list_entry(clp->cl_openowners.next, struct nfs4_openowner, oo_perclient);
		nfs4_get_stateowner(&oo->oo_owner);
		release_openowner(oo);
	}
	nfsd4_shutdown_callback(clp);
	if (clp->cl_cb_conn.cb_xprt)
		svc_xprt_put(clp->cl_cb_conn.cb_xprt);
	free_client(clp);
}

static void
destroy_client(struct nfs4_client *clp)
{
	unhash_client(clp);
	__destroy_client(clp);
}

static void expire_client(struct nfs4_client *clp)
{
	unhash_client(clp);
	nfsd4_client_record_remove(clp);
	__destroy_client(clp);
}

static void copy_verf(struct nfs4_client *target, nfs4_verifier *source)
{
	memcpy(target->cl_verifier.data, source->data,
			sizeof(target->cl_verifier.data));
}

static void copy_clid(struct nfs4_client *target, struct nfs4_client *source)
{
	target->cl_clientid.cl_boot = source->cl_clientid.cl_boot; 
	target->cl_clientid.cl_id = source->cl_clientid.cl_id; 
}

static int copy_cred(struct svc_cred *target, struct svc_cred *source)
{
	if (source->cr_principal) {
		target->cr_principal =
				kstrdup(source->cr_principal, GFP_KERNEL);
		if (target->cr_principal == NULL)
			return -ENOMEM;
	} else
		target->cr_principal = NULL;
	target->cr_flavor = source->cr_flavor;
	target->cr_uid = source->cr_uid;
	target->cr_gid = source->cr_gid;
	target->cr_group_info = source->cr_group_info;
	get_group_info(target->cr_group_info);
	target->cr_gss_mech = source->cr_gss_mech;
	if (source->cr_gss_mech)
		gss_mech_get(source->cr_gss_mech);
	return 0;
}

static int
compare_blob(const struct xdr_netobj *o1, const struct xdr_netobj *o2)
{
	if (o1->len < o2->len)
		return -1;
	if (o1->len > o2->len)
		return 1;
	return memcmp(o1->data, o2->data, o1->len);
}

static int same_name(const char *n1, const char *n2)
{
	return 0 == memcmp(n1, n2, HEXDIR_LEN);
}

static int
same_verf(nfs4_verifier *v1, nfs4_verifier *v2)
{
	return 0 == memcmp(v1->data, v2->data, sizeof(v1->data));
}

static int
same_clid(clientid_t *cl1, clientid_t *cl2)
{
	return (cl1->cl_boot == cl2->cl_boot) && (cl1->cl_id == cl2->cl_id);
}

static bool groups_equal(struct group_info *g1, struct group_info *g2)
{
	int i;

	if (g1->ngroups != g2->ngroups)
		return false;
	for (i=0; i<g1->ngroups; i++)
		if (!gid_eq(GROUP_AT(g1, i), GROUP_AT(g2, i)))
			return false;
	return true;
}

/*
 * RFC 3530 language requires clid_inuse be returned when the
 * "principal" associated with a requests differs from that previously
 * used.  We use uid, gid's, and gss principal string as our best
 * approximation.  We also don't want to allow non-gss use of a client
 * established using gss: in theory cr_principal should catch that
 * change, but in practice cr_principal can be null even in the gss case
 * since gssd doesn't always pass down a principal string.
 */
static bool is_gss_cred(struct svc_cred *cr)
{
	/* Is cr_flavor one of the gss "pseudoflavors"?: */
	return (cr->cr_flavor > RPC_AUTH_MAXFLAVOR);
}


static bool
same_creds(struct svc_cred *cr1, struct svc_cred *cr2)
{
	if ((is_gss_cred(cr1) != is_gss_cred(cr2))
		|| (!uid_eq(cr1->cr_uid, cr2->cr_uid))
		|| (!gid_eq(cr1->cr_gid, cr2->cr_gid))
		|| !groups_equal(cr1->cr_group_info, cr2->cr_group_info))
		return false;
	if (cr1->cr_principal == cr2->cr_principal)
		return true;
	if (!cr1->cr_principal || !cr2->cr_principal)
		return false;
	return 0 == strcmp(cr1->cr_principal, cr2->cr_principal);
}

static bool svc_rqst_integrity_protected(struct svc_rqst *rqstp)
{
	struct svc_cred *cr = &rqstp->rq_cred;
	u32 service;

	if (!cr->cr_gss_mech)
		return false;
	service = gss_pseudoflavor_to_service(cr->cr_gss_mech, cr->cr_flavor);
	return service == RPC_GSS_SVC_INTEGRITY ||
	       service == RPC_GSS_SVC_PRIVACY;
}

static bool mach_creds_match(struct nfs4_client *cl, struct svc_rqst *rqstp)
{
	struct svc_cred *cr = &rqstp->rq_cred;

	if (!cl->cl_mach_cred)
		return true;
	if (cl->cl_cred.cr_gss_mech != cr->cr_gss_mech)
		return false;
	if (!svc_rqst_integrity_protected(rqstp))
		return false;
	if (!cr->cr_principal)
		return false;
	return 0 == strcmp(cl->cl_cred.cr_principal, cr->cr_principal);
}

static void gen_confirm(struct nfs4_client *clp, struct nfsd_net *nn)
{
	__be32 verf[2];

	/*
	 * This is opaque to client, so no need to byte-swap. Use
	 * __force to keep sparse happy
	 */
	verf[0] = (__force __be32)get_seconds();
	verf[1] = (__force __be32)nn->clientid_counter;
	memcpy(clp->cl_confirm.data, verf, sizeof(clp->cl_confirm.data));
}

static void gen_clid(struct nfs4_client *clp, struct nfsd_net *nn)
{
	clp->cl_clientid.cl_boot = nn->boot_time;
	clp->cl_clientid.cl_id = nn->clientid_counter++;
	gen_confirm(clp, nn);
}

static struct nfs4_stid *
find_stateid_locked(struct nfs4_client *cl, stateid_t *t)
{
	struct nfs4_stid *ret;

	ret = idr_find(&cl->cl_stateids, t->si_opaque.so_id);
	if (!ret || !ret->sc_type)
		return NULL;
	return ret;
}

static struct nfs4_stid *
find_stateid_by_type(struct nfs4_client *cl, stateid_t *t, char typemask)
{
	struct nfs4_stid *s;

	spin_lock(&cl->cl_lock);
	s = find_stateid_locked(cl, t);
	if (s != NULL) {
		if (typemask & s->sc_type)
			atomic_inc(&s->sc_count);
		else
			s = NULL;
	}
	spin_unlock(&cl->cl_lock);
	return s;
}

static struct nfs4_client *create_client(struct xdr_netobj name,
		struct svc_rqst *rqstp, nfs4_verifier *verf)
{
	struct nfs4_client *clp;
	struct sockaddr *sa = svc_addr(rqstp);
	int ret;
	struct net *net = SVC_NET(rqstp);

	clp = alloc_client(name);
	if (clp == NULL)
		return NULL;

	ret = copy_cred(&clp->cl_cred, &rqstp->rq_cred);
	if (ret) {
		free_client(clp);
		return NULL;
	}
	nfsd4_init_cb(&clp->cl_cb_null, clp, NULL, NFSPROC4_CLNT_CB_NULL);
	clp->cl_time = get_seconds();
	clear_bit(0, &clp->cl_cb_slot_busy);
	copy_verf(clp, verf);
	rpc_copy_addr((struct sockaddr *) &clp->cl_addr, sa);
	clp->cl_cb_session = NULL;
	clp->net = net;
	return clp;
}

static void
add_clp_to_name_tree(struct nfs4_client *new_clp, struct rb_root *root)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	struct nfs4_client *clp;

	while (*new) {
		clp = rb_entry(*new, struct nfs4_client, cl_namenode);
		parent = *new;

		if (compare_blob(&clp->cl_name, &new_clp->cl_name) > 0)
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	rb_link_node(&new_clp->cl_namenode, parent, new);
	rb_insert_color(&new_clp->cl_namenode, root);
}

static struct nfs4_client *
find_clp_in_name_tree(struct xdr_netobj *name, struct rb_root *root)
{
	int cmp;
	struct rb_node *node = root->rb_node;
	struct nfs4_client *clp;

	while (node) {
		clp = rb_entry(node, struct nfs4_client, cl_namenode);
		cmp = compare_blob(&clp->cl_name, name);
		if (cmp > 0)
			node = node->rb_left;
		else if (cmp < 0)
			node = node->rb_right;
		else
			return clp;
	}
	return NULL;
}

static void
add_to_unconfirmed(struct nfs4_client *clp)
{
	unsigned int idhashval;
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	lockdep_assert_held(&nn->client_lock);

	clear_bit(NFSD4_CLIENT_CONFIRMED, &clp->cl_flags);
	add_clp_to_name_tree(clp, &nn->unconf_name_tree);
	idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	list_add(&clp->cl_idhash, &nn->unconf_id_hashtbl[idhashval]);
	renew_client_locked(clp);
}

static void
move_to_confirmed(struct nfs4_client *clp)
{
	unsigned int idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	lockdep_assert_held(&nn->client_lock);

	dprintk("NFSD: move_to_confirm nfs4_client %p\n", clp);
	list_move(&clp->cl_idhash, &nn->conf_id_hashtbl[idhashval]);
	rb_erase(&clp->cl_namenode, &nn->unconf_name_tree);
	add_clp_to_name_tree(clp, &nn->conf_name_tree);
	set_bit(NFSD4_CLIENT_CONFIRMED, &clp->cl_flags);
	renew_client_locked(clp);
}

static struct nfs4_client *
find_client_in_id_table(struct list_head *tbl, clientid_t *clid, bool sessions)
{
	struct nfs4_client *clp;
	unsigned int idhashval = clientid_hashval(clid->cl_id);

	list_for_each_entry(clp, &tbl[idhashval], cl_idhash) {
		if (same_clid(&clp->cl_clientid, clid)) {
			if ((bool)clp->cl_minorversion != sessions)
				return NULL;
			renew_client_locked(clp);
			return clp;
		}
	}
	return NULL;
}

static struct nfs4_client *
find_confirmed_client(clientid_t *clid, bool sessions, struct nfsd_net *nn)
{
	struct list_head *tbl = nn->conf_id_hashtbl;

	lockdep_assert_held(&nn->client_lock);
	return find_client_in_id_table(tbl, clid, sessions);
}

static struct nfs4_client *
find_unconfirmed_client(clientid_t *clid, bool sessions, struct nfsd_net *nn)
{
	struct list_head *tbl = nn->unconf_id_hashtbl;

	lockdep_assert_held(&nn->client_lock);
	return find_client_in_id_table(tbl, clid, sessions);
}

static bool clp_used_exchangeid(struct nfs4_client *clp)
{
	return clp->cl_exchange_flags != 0;
} 

static struct nfs4_client *
find_confirmed_client_by_name(struct xdr_netobj *name, struct nfsd_net *nn)
{
	lockdep_assert_held(&nn->client_lock);
	return find_clp_in_name_tree(name, &nn->conf_name_tree);
}

static struct nfs4_client *
find_unconfirmed_client_by_name(struct xdr_netobj *name, struct nfsd_net *nn)
{
	lockdep_assert_held(&nn->client_lock);
	return find_clp_in_name_tree(name, &nn->unconf_name_tree);
}

static void
gen_callback(struct nfs4_client *clp, struct nfsd4_setclientid *se, struct svc_rqst *rqstp)
{
	struct nfs4_cb_conn *conn = &clp->cl_cb_conn;
	struct sockaddr	*sa = svc_addr(rqstp);
	u32 scopeid = rpc_get_scope_id(sa);
	unsigned short expected_family;

	/* Currently, we only support tcp and tcp6 for the callback channel */
	if (se->se_callback_netid_len == 3 &&
	    !memcmp(se->se_callback_netid_val, "tcp", 3))
		expected_family = AF_INET;
	else if (se->se_callback_netid_len == 4 &&
		 !memcmp(se->se_callback_netid_val, "tcp6", 4))
		expected_family = AF_INET6;
	else
		goto out_err;

	conn->cb_addrlen = rpc_uaddr2sockaddr(clp->net, se->se_callback_addr_val,
					    se->se_callback_addr_len,
					    (struct sockaddr *)&conn->cb_addr,
					    sizeof(conn->cb_addr));

	if (!conn->cb_addrlen || conn->cb_addr.ss_family != expected_family)
		goto out_err;

	if (conn->cb_addr.ss_family == AF_INET6)
		((struct sockaddr_in6 *)&conn->cb_addr)->sin6_scope_id = scopeid;

	conn->cb_prog = se->se_callback_prog;
	conn->cb_ident = se->se_callback_ident;
	memcpy(&conn->cb_saddr, &rqstp->rq_daddr, rqstp->rq_daddrlen);
	return;
out_err:
	conn->cb_addr.ss_family = AF_UNSPEC;
	conn->cb_addrlen = 0;
	dprintk(KERN_INFO "NFSD: this client (clientid %08x/%08x) "
		"will not receive delegations\n",
		clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);

	return;
}

/*
 * Cache a reply. nfsd4_check_resp_size() has bounded the cache size.
 */
static void
nfsd4_store_cache_entry(struct nfsd4_compoundres *resp)
{
	struct xdr_buf *buf = resp->xdr.buf;
	struct nfsd4_slot *slot = resp->cstate.slot;
	unsigned int base;

	dprintk("--> %s slot %p\n", __func__, slot);

	slot->sl_opcnt = resp->opcnt;
	slot->sl_status = resp->cstate.status;

	slot->sl_flags |= NFSD4_SLOT_INITIALIZED;
	if (nfsd4_not_cached(resp)) {
		slot->sl_datalen = 0;
		return;
	}
	base = resp->cstate.data_offset;
	slot->sl_datalen = buf->len - base;
	if (read_bytes_from_xdr_buf(buf, base, slot->sl_data, slot->sl_datalen))
		WARN("%s: sessions DRC could not cache compound\n", __func__);
	return;
}

/*
 * Encode the replay sequence operation from the slot values.
 * If cachethis is FALSE encode the uncached rep error on the next
 * operation which sets resp->p and increments resp->opcnt for
 * nfs4svc_encode_compoundres.
 *
 */
static __be32
nfsd4_enc_sequence_replay(struct nfsd4_compoundargs *args,
			  struct nfsd4_compoundres *resp)
{
	struct nfsd4_op *op;
	struct nfsd4_slot *slot = resp->cstate.slot;

	/* Encode the replayed sequence operation */
	op = &args->ops[resp->opcnt - 1];
	nfsd4_encode_operation(resp, op);

	/* Return nfserr_retry_uncached_rep in next operation. */
	if (args->opcnt > 1 && !(slot->sl_flags & NFSD4_SLOT_CACHETHIS)) {
		op = &args->ops[resp->opcnt++];
		op->status = nfserr_retry_uncached_rep;
		nfsd4_encode_operation(resp, op);
	}
	return op->status;
}

/*
 * The sequence operation is not cached because we can use the slot and
 * session values.
 */
static __be32
nfsd4_replay_cache_entry(struct nfsd4_compoundres *resp,
			 struct nfsd4_sequence *seq)
{
	struct nfsd4_slot *slot = resp->cstate.slot;
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;
	__be32 status;

	dprintk("--> %s slot %p\n", __func__, slot);

	status = nfsd4_enc_sequence_replay(resp->rqstp->rq_argp, resp);
	if (status)
		return status;

	p = xdr_reserve_space(xdr, slot->sl_datalen);
	if (!p) {
		WARN_ON_ONCE(1);
		return nfserr_serverfault;
	}
	xdr_encode_opaque_fixed(p, slot->sl_data, slot->sl_datalen);
	xdr_commit_encode(xdr);

	resp->opcnt = slot->sl_opcnt;
	return slot->sl_status;
}

/*
 * Set the exchange_id flags returned by the server.
 */
static void
nfsd4_set_ex_flags(struct nfs4_client *new, struct nfsd4_exchange_id *clid)
{
	/* pNFS is not supported */
	new->cl_exchange_flags |= EXCHGID4_FLAG_USE_NON_PNFS;

	/* Referrals are supported, Migration is not. */
	new->cl_exchange_flags |= EXCHGID4_FLAG_SUPP_MOVED_REFER;

	/* set the wire flags to return to client. */
	clid->flags = new->cl_exchange_flags;
}

static bool client_has_state(struct nfs4_client *clp)
{
	/*
	 * Note clp->cl_openowners check isn't quite right: there's no
	 * need to count owners without stateid's.
	 *
	 * Also note we should probably be using this in 4.0 case too.
	 */
	return !list_empty(&clp->cl_openowners)
		|| !list_empty(&clp->cl_delegations)
		|| !list_empty(&clp->cl_sessions);
}

__be32
nfsd4_exchange_id(struct svc_rqst *rqstp,
		  struct nfsd4_compound_state *cstate,
		  struct nfsd4_exchange_id *exid)
{
	struct nfs4_client *conf, *new;
	struct nfs4_client *unconf = NULL;
	__be32 status;
	char			addr_str[INET6_ADDRSTRLEN];
	nfs4_verifier		verf = exid->verifier;
	struct sockaddr		*sa = svc_addr(rqstp);
	bool	update = exid->flags & EXCHGID4_FLAG_UPD_CONFIRMED_REC_A;
	struct nfsd_net		*nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	rpc_ntop(sa, addr_str, sizeof(addr_str));
	dprintk("%s rqstp=%p exid=%p clname.len=%u clname.data=%p "
		"ip_addr=%s flags %x, spa_how %d\n",
		__func__, rqstp, exid, exid->clname.len, exid->clname.data,
		addr_str, exid->flags, exid->spa_how);

	if (exid->flags & ~EXCHGID4_FLAG_MASK_A)
		return nfserr_inval;

	switch (exid->spa_how) {
	case SP4_MACH_CRED:
		if (!svc_rqst_integrity_protected(rqstp))
			return nfserr_inval;
	case SP4_NONE:
		break;
	default:				/* checked by xdr code */
		WARN_ON_ONCE(1);
	case SP4_SSV:
		return nfserr_encr_alg_unsupp;
	}

	new = create_client(exid->clname, rqstp, &verf);
	if (new == NULL)
		return nfserr_jukebox;

	/* Cases below refer to rfc 5661 section 18.35.4: */
	spin_lock(&nn->client_lock);
	conf = find_confirmed_client_by_name(&exid->clname, nn);
	if (conf) {
		bool creds_match = same_creds(&conf->cl_cred, &rqstp->rq_cred);
		bool verfs_match = same_verf(&verf, &conf->cl_verifier);

		if (update) {
			if (!clp_used_exchangeid(conf)) { /* buggy client */
				status = nfserr_inval;
				goto out;
			}
			if (!mach_creds_match(conf, rqstp)) {
				status = nfserr_wrong_cred;
				goto out;
			}
			if (!creds_match) { /* case 9 */
				status = nfserr_perm;
				goto out;
			}
			if (!verfs_match) { /* case 8 */
				status = nfserr_not_same;
				goto out;
			}
			/* case 6 */
			exid->flags |= EXCHGID4_FLAG_CONFIRMED_R;
			goto out_copy;
		}
		if (!creds_match) { /* case 3 */
			if (client_has_state(conf)) {
				status = nfserr_clid_inuse;
				goto out;
			}
			goto out_new;
		}
		if (verfs_match) { /* case 2 */
			conf->cl_exchange_flags |= EXCHGID4_FLAG_CONFIRMED_R;
			goto out_copy;
		}
		/* case 5, client reboot */
		conf = NULL;
		goto out_new;
	}

	if (update) { /* case 7 */
		status = nfserr_noent;
		goto out;
	}

	unconf  = find_unconfirmed_client_by_name(&exid->clname, nn);
	if (unconf) /* case 4, possible retry or client restart */
		unhash_client_locked(unconf);

	/* case 1 (normal case) */
out_new:
	if (conf) {
		status = mark_client_expired_locked(conf);
		if (status)
			goto out;
	}
	new->cl_minorversion = cstate->minorversion;
	new->cl_mach_cred = (exid->spa_how == SP4_MACH_CRED);

	gen_clid(new, nn);
	add_to_unconfirmed(new);
	swap(new, conf);
out_copy:
	exid->clientid.cl_boot = conf->cl_clientid.cl_boot;
	exid->clientid.cl_id = conf->cl_clientid.cl_id;

	exid->seqid = conf->cl_cs_slot.sl_seqid + 1;
	nfsd4_set_ex_flags(conf, exid);

	dprintk("nfsd4_exchange_id seqid %d flags %x\n",
		conf->cl_cs_slot.sl_seqid, conf->cl_exchange_flags);
	status = nfs_ok;

out:
	spin_unlock(&nn->client_lock);
	if (new)
		expire_client(new);
	if (unconf)
		expire_client(unconf);
	return status;
}

static __be32
check_slot_seqid(u32 seqid, u32 slot_seqid, int slot_inuse)
{
	dprintk("%s enter. seqid %d slot_seqid %d\n", __func__, seqid,
		slot_seqid);

	/* The slot is in use, and no response has been sent. */
	if (slot_inuse) {
		if (seqid == slot_seqid)
			return nfserr_jukebox;
		else
			return nfserr_seq_misordered;
	}
	/* Note unsigned 32-bit arithmetic handles wraparound: */
	if (likely(seqid == slot_seqid + 1))
		return nfs_ok;
	if (seqid == slot_seqid)
		return nfserr_replay_cache;
	return nfserr_seq_misordered;
}

/*
 * Cache the create session result into the create session single DRC
 * slot cache by saving the xdr structure. sl_seqid has been set.
 * Do this for solo or embedded create session operations.
 */
static void
nfsd4_cache_create_session(struct nfsd4_create_session *cr_ses,
			   struct nfsd4_clid_slot *slot, __be32 nfserr)
{
	slot->sl_status = nfserr;
	memcpy(&slot->sl_cr_ses, cr_ses, sizeof(*cr_ses));
}

static __be32
nfsd4_replay_create_session(struct nfsd4_create_session *cr_ses,
			    struct nfsd4_clid_slot *slot)
{
	memcpy(cr_ses, &slot->sl_cr_ses, sizeof(*cr_ses));
	return slot->sl_status;
}

#define NFSD_MIN_REQ_HDR_SEQ_SZ	((\
			2 * 2 + /* credential,verifier: AUTH_NULL, length 0 */ \
			1 +	/* MIN tag is length with zero, only length */ \
			3 +	/* version, opcount, opcode */ \
			XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + \
				/* seqid, slotID, slotID, cache */ \
			4 ) * sizeof(__be32))

#define NFSD_MIN_RESP_HDR_SEQ_SZ ((\
			2 +	/* verifier: AUTH_NULL, length 0 */\
			1 +	/* status */ \
			1 +	/* MIN tag is length with zero, only length */ \
			3 +	/* opcount, opcode, opstatus*/ \
			XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + \
				/* seqid, slotID, slotID, slotID, status */ \
			5 ) * sizeof(__be32))

static __be32 check_forechannel_attrs(struct nfsd4_channel_attrs *ca, struct nfsd_net *nn)
{
	u32 maxrpc = nn->nfsd_serv->sv_max_mesg;

	if (ca->maxreq_sz < NFSD_MIN_REQ_HDR_SEQ_SZ)
		return nfserr_toosmall;
	if (ca->maxresp_sz < NFSD_MIN_RESP_HDR_SEQ_SZ)
		return nfserr_toosmall;
	ca->headerpadsz = 0;
	ca->maxreq_sz = min_t(u32, ca->maxreq_sz, maxrpc);
	ca->maxresp_sz = min_t(u32, ca->maxresp_sz, maxrpc);
	ca->maxops = min_t(u32, ca->maxops, NFSD_MAX_OPS_PER_COMPOUND);
	ca->maxresp_cached = min_t(u32, ca->maxresp_cached,
			NFSD_SLOT_CACHE_SIZE + NFSD_MIN_HDR_SEQ_SZ);
	ca->maxreqs = min_t(u32, ca->maxreqs, NFSD_MAX_SLOTS_PER_SESSION);
	/*
	 * Note decreasing slot size below client's request may make it
	 * difficult for client to function correctly, whereas
	 * decreasing the number of slots will (just?) affect
	 * performance.  When short on memory we therefore prefer to
	 * decrease number of slots instead of their size.  Clients that
	 * request larger slots than they need will get poor results:
	 */
	ca->maxreqs = nfsd4_get_drc_mem(ca);
	if (!ca->maxreqs)
		return nfserr_jukebox;

	return nfs_ok;
}

#define NFSD_CB_MAX_REQ_SZ	((NFS4_enc_cb_recall_sz + \
				 RPC_MAX_HEADER_WITH_AUTH) * sizeof(__be32))
#define NFSD_CB_MAX_RESP_SZ	((NFS4_dec_cb_recall_sz + \
				 RPC_MAX_REPHEADER_WITH_AUTH) * sizeof(__be32))

static __be32 check_backchannel_attrs(struct nfsd4_channel_attrs *ca)
{
	ca->headerpadsz = 0;

	/*
	 * These RPC_MAX_HEADER macros are overkill, especially since we
	 * don't even do gss on the backchannel yet.  But this is still
	 * less than 1k.  Tighten up this estimate in the unlikely event
	 * it turns out to be a problem for some client:
	 */
	if (ca->maxreq_sz < NFSD_CB_MAX_REQ_SZ)
		return nfserr_toosmall;
	if (ca->maxresp_sz < NFSD_CB_MAX_RESP_SZ)
		return nfserr_toosmall;
	ca->maxresp_cached = 0;
	if (ca->maxops < 2)
		return nfserr_toosmall;

	return nfs_ok;
}

static __be32 nfsd4_check_cb_sec(struct nfsd4_cb_sec *cbs)
{
	switch (cbs->flavor) {
	case RPC_AUTH_NULL:
	case RPC_AUTH_UNIX:
		return nfs_ok;
	default:
		/*
		 * GSS case: the spec doesn't allow us to return this
		 * error.  But it also doesn't allow us not to support
		 * GSS.
		 * I'd rather this fail hard than return some error the
		 * client might think it can already handle:
		 */
		return nfserr_encr_alg_unsupp;
	}
}

__be32
nfsd4_create_session(struct svc_rqst *rqstp,
		     struct nfsd4_compound_state *cstate,
		     struct nfsd4_create_session *cr_ses)
{
	struct sockaddr *sa = svc_addr(rqstp);
	struct nfs4_client *conf, *unconf;
	struct nfs4_client *old = NULL;
	struct nfsd4_session *new;
	struct nfsd4_conn *conn;
	struct nfsd4_clid_slot *cs_slot = NULL;
	__be32 status = 0;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	if (cr_ses->flags & ~SESSION4_FLAG_MASK_A)
		return nfserr_inval;
	status = nfsd4_check_cb_sec(&cr_ses->cb_sec);
	if (status)
		return status;
	status = check_forechannel_attrs(&cr_ses->fore_channel, nn);
	if (status)
		return status;
	status = check_backchannel_attrs(&cr_ses->back_channel);
	if (status)
		goto out_release_drc_mem;
	status = nfserr_jukebox;
	new = alloc_session(&cr_ses->fore_channel, &cr_ses->back_channel);
	if (!new)
		goto out_release_drc_mem;
	conn = alloc_conn_from_crses(rqstp, cr_ses);
	if (!conn)
		goto out_free_session;

	spin_lock(&nn->client_lock);
	unconf = find_unconfirmed_client(&cr_ses->clientid, true, nn);
	conf = find_confirmed_client(&cr_ses->clientid, true, nn);
	WARN_ON_ONCE(conf && unconf);

	if (conf) {
		status = nfserr_wrong_cred;
		if (!mach_creds_match(conf, rqstp))
			goto out_free_conn;
		cs_slot = &conf->cl_cs_slot;
		status = check_slot_seqid(cr_ses->seqid, cs_slot->sl_seqid, 0);
		if (status == nfserr_replay_cache) {
			status = nfsd4_replay_create_session(cr_ses, cs_slot);
			goto out_free_conn;
		} else if (cr_ses->seqid != cs_slot->sl_seqid + 1) {
			status = nfserr_seq_misordered;
			goto out_free_conn;
		}
	} else if (unconf) {
		if (!same_creds(&unconf->cl_cred, &rqstp->rq_cred) ||
		    !rpc_cmp_addr(sa, (struct sockaddr *) &unconf->cl_addr)) {
			status = nfserr_clid_inuse;
			goto out_free_conn;
		}
		status = nfserr_wrong_cred;
		if (!mach_creds_match(unconf, rqstp))
			goto out_free_conn;
		cs_slot = &unconf->cl_cs_slot;
		status = check_slot_seqid(cr_ses->seqid, cs_slot->sl_seqid, 0);
		if (status) {
			/* an unconfirmed replay returns misordered */
			status = nfserr_seq_misordered;
			goto out_free_conn;
		}
		old = find_confirmed_client_by_name(&unconf->cl_name, nn);
		if (old) {
			status = mark_client_expired_locked(old);
			if (status) {
				old = NULL;
				goto out_free_conn;
			}
		}
		move_to_confirmed(unconf);
		conf = unconf;
	} else {
		status = nfserr_stale_clientid;
		goto out_free_conn;
	}
	status = nfs_ok;
	/*
	 * We do not support RDMA or persistent sessions
	 */
	cr_ses->flags &= ~SESSION4_PERSIST;
	cr_ses->flags &= ~SESSION4_RDMA;

	init_session(rqstp, new, conf, cr_ses);
	nfsd4_get_session_locked(new);

	memcpy(cr_ses->sessionid.data, new->se_sessionid.data,
	       NFS4_MAX_SESSIONID_LEN);
	cs_slot->sl_seqid++;
	cr_ses->seqid = cs_slot->sl_seqid;

	/* cache solo and embedded create sessions under the client_lock */
	nfsd4_cache_create_session(cr_ses, cs_slot, status);
	spin_unlock(&nn->client_lock);
	/* init connection and backchannel */
	nfsd4_init_conn(rqstp, conn, new);
	nfsd4_put_session(new);
	if (old)
		expire_client(old);
	return status;
out_free_conn:
	spin_unlock(&nn->client_lock);
	free_conn(conn);
	if (old)
		expire_client(old);
out_free_session:
	__free_session(new);
out_release_drc_mem:
	nfsd4_put_drc_mem(&cr_ses->fore_channel);
	return status;
}

static __be32 nfsd4_map_bcts_dir= &coi);
	cs_sdnt
sameoe == RPC_GSS_SVC_INTEGRI	gen_clid(nt)d)
		expire_on and backchanno
{
	ca->headerpadsz = S_ses->seqi_OR_BOTH)d)
		expire_on and bac_OR_BOTH)d)	*S4_CDFC4_FORE;

	BOTH;channo
{
	ca->heade}n nfserr_seq_misostatus 2
nfsd4_cap_bctsnel_attrs(&cctlsvc_rqst *rqstp,
		     nfsd4_compound_state *cstate,
		     nfsd4_compoundnel_attrs(&cctl *bcuct nfsd4_session *session;
	__be3e->minorver
	__be32 sfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	if (crstatus;

	dprintk( nfsd4_check_cb_sec(&cr_ses->bchanc_
	if (status)
		return status;
	status = (&nn->client_lock);
	unconf =
	__be3prog = cses->cabchanc_
	ises-ramf =
	__be3prog = ces->cbbchanc_
	if (unlock(&nn->client_lock);
	/* initprobe_callback_sync(se(
	__be3prog =		nfs4_g nfs_ok;
}

static _fsd4_cap_bctsnirmed_cnice(crruct svc_rqst *rqstp,
		     struct nfsd4_compound_state *cstate,
		     struct nfsd4_create_snirmed_cnice(crruct  *bctse32 verf[2];	status = cd4_conn *conn;
	struct nfsd4_clid_slo*session;
	__be32 set *net = SVC_NET(rqstp);

	clp = nfsd_net *nn = net_generic(net, nfsd_net_id);

	lockdep_)
		god_slolastd_state *cop
		return false;
	ot_same;
		gth _uct nf(&nn->client_lock);
	unconf =
	__be3confirmeonid_hashtbl(sessioni&bctsnid.data, n	if (>sl_	spin_unlock(&nn->client_lock);
	/* init sion)
		goto out;
	statu:
	k
	__be32 setfserr_wrong_cred;
		if (!mah_creds_match(unconf, 
	__be3prog =		nfs)
			goto ou;
	status = nfsd4_get_sess_dir= &coi);
&bctsni_GSStatus)
		goto out_release= alloc_conn_from_crconn, nebctsni_GSStat nfserr_jukebox;
	new = aln)
		goto out_free_se;init_conn(rqstp, conn, new);
	n;
	if (stat nfs_ok;
	/*
	 * in_unlt_session(new);
	i;
	if (staatu:
	k
	__be3: status;
}

static __be32 nfs_mampound_state *conid_hashtnfsd4_session *ses)
{
	strcount,fs4_stid *s;
 *sessionidtruct nsion)
		goto out;
	dprintk(list_empe->se_ca n	i&
	__be3prog d, struct ner_ses))dtrube32
nfsd4_exchangeclient(ccrruct svc_rqst *rqstp,
		 struct nnfsd4_compound_state *cstate,
		     struct nnfsd4_compoundclient(ccrruct nid, structuct nfsd4_session *session;
	_crstatus;

	dprint;
	strufn->cl(&un	/* Make et *net = SVC_NET(rqstp);

 = nfsd_net *nn = net_generic(net, nfsd_net_id);

	lockdep_ nfserr_not_same;
		gth _uct n4_not_cach_state *conid_hashtnminorver
	__be3	i&
	__be3d = co structu (!same_creod_slolastd_state *cop
	to out_free_co;channfn->cl(&un	/ses->}essionid(__func__, sessioni&
	__be3d = co structut nf(&nn->client_lock);
	unconf =
	_confirmeonid_hashtbl(sessioni&
	__be3d = co struct	if (>sl_	spin_unlsion)
		 out;
	statu:ock);
	unco2 setfserr_wrong_cred;
		if (!mah_creds_match(unconf, 
	_prog =		nfs)
	to ou;
	statusion(new);
	2 setfserr_went_elocked(nds_fression);
	, 1 +trufn->cl(&un	/status)
		goto out_release_ion(new);
	2 session(struct nfen_unlock(&nn->client_lock);
	/* initprobe_callback_sync(se_sync 
	_prog =		nfs)_lock(&nn->client_lock);
	unconf = nfs_ok;
	/*
	 * in__ion(new);
	unlt_session(new);
	ression);
	opy:
	exck);
	uncounlock(&nn->client_lock);
	free_coin_unltatus;
}

static __be32 n cd4_conn *conn;
	s__nn *cofirmed_c svc_rqst *rqfree *xpt nfsd4_session,
				sen;
uct nfsd4_session nn;
	st_for_each_entry(clp, &tbl>sl_prog =n;
ot, n			g
		goto (!same_crcha n	free == xpteturn elem;
		c else if NULL;
}

static struct nfsd4_cap_bcts_replay(r_sec(&c_c svc_rqstssion nn;
	suct nfsd4_exchange*session;
	_uct nfs4_client *clp = ses->se_client;
	struct nfsd_net *nnn nn;
	st_fstatus = nfserr_bads (seqid	struct ock(&clp->cl_lock);
	list_for->cb__nn *cofirmed_c smach_cn	free	n;
	conf) {
	 out_free_session2 setfserr_wrong_crd_cni;
		the cice(crruct onf) {
	inorvers)
		return t_free_session2 s__nn *coent_lo_c smac	n;
	confock(&clp->cl_lock);
}

statiopy_creplay_cregessirlo_c smacconf) {
	frese 5, oo
		ifree is handle:
rinc (like	 *nnn nn;
}

stp->clh_cn	frtchanurn service 	/*
	 * in__sionunlock(&nn->clielock);
}

station(conn);
	b_insertatus;
}

static __be32 nfs_mampoundnew);
	rtoors)n _ucssvc_rqst *rqstp,
		     nfsd4_compoundcrruct nid, struuct nfsd4_session nngs *args,
			  s>cst_argp, resp);_g nfs_ok;
cnt > 1 && !(s
	__be3prog fattrs(&. 2)
		tic __be32 nfs_mampoundlarger rtoorbigsvc_rqst *rqstp,
		     strct nfsd4_exchangecrruct nid, struuct nfsd4_se*buf = rexb ->rq_cred;

	cnt_g nfs_ok;
xbo2->len)
	__be3prog fattrs(&. 2) NFSD_e32
nfsd4_exchange_replay(svc_rqst *rqstp,
		     st uct nnfsd4_compound_state *cstate,
		     st uct nnfsd4_compound *seq)
{
	struct nfsd4_slot *slres *resp,
			 st>cst_argp, re	 st; nfsd4_se*bufxdr = &resp->xdr;
	__be32 *fsd4_clid_slo*session;
	__be32 set *net nt *clp;
	struct sockaddrnt *slot = resp-s = cd4_conn *conn;
	struct ntatus;

	dprint;
	st = IT_LISet *net = SVC_NET(rqstp);

	clp = nfsd_net *nn = net_generic(net, nfsd_net_id);

	lockdep_)
		cnt = slot->!= 1rn false;
	ot_same_replay(rpos * These RPWpoorbe eis faihangisten(co codep_bcts_replay(r_sec(&c_c se RPient'return !lloc_conn_from_crconn, nez = S_ses->seqi!conn)
		goto outfserr_jukebox;

	return nf(&nn->client_lock);
	unconf =
	__be3confirmeonid_hashtbl(sessioni&_re= co struct	if (>sl_	spin_unlsion)
		goto out;
	statu:
	k
	__be32 ss->se_cli_be3prog =		nfsdep_ nfserr_not_sametoors)n _ucst n4_not_cachnew);
	rtoors)n _ucss	     nf		goto  out_release_ion(new);
	2 p_ nfserr_not_same NFStoorbigt n4_not_cachlarger rtoorbigs	     nf		goto  out_release_ion(new);
	2 p_ nfserr_not_samebadesp-s =d == slid;
o/%08>=)
	__be3prog fattrs(&. 2) NFo out_release_ion(new);
	2 sl_stase_cli_be3prog an th[ slid;
o/%0_ver"%s enter. : ;
o/%08_func__, seqid,
		slid;
o/%0The slott support ne_reth aber of slots will (juye need wire flse RP 2)an theyover.
 *__be3c= nfsd4_ts resisihangiyovdr);

se RPsr_higher r;
o/%08 for r.
 metr_gsslot = igiyov 2)an the_lockslid 2)an the=)
	__be3prog fattrs(&. 2) NFo2 p_ nfserr_not_seqid(cr_ses->_re= cotID, slotid, 0);
		istrctl_flags |= NFSD4&LOT_INITIALIZEUSEstatus)
		gotorr_replay_cache) {
			status  nfserr_seq_misordered;
			goto ousion)l_flags & NFSD4_SLOT_CACHETHIZED;
	if (nto out_free_conion(new);
	2 s	minorver
stase_ctatus =minorver
	__be3e=)
	__be3us =minorvers->se_
	}
	ren nfserr_re size.
  retury= nfserr forwireminorver
nfserient m cliobe_callbclres *res forc	__bngatus = nfserr_noeny_cache_entry(struct n);
	}
struus =minorver
nfserr_not_same Nhe;
	return nt;
	}
	new->cl_us)
		goto out_release_ion(new);
	2  = nfsd4_get_sess_replay(r_sec(&c_c sw);
	n;
	if (statlloc_co		goto us)
		goto out_release_ion(new);
	2  = = IT_->sp_re= s is FALS) ?us = 	__be3prog fattrs(&. 2) Nd = 0;
	if:us = 	__be3prog fattrs(&. 2) Nd =D_e3= nfsd4_gep_re= s is FALS) ?not_same Nhrtoorbigirmedo and:
sizeof(cot_same Nhrtoorbigto us)
rve_spat nf
	co IT_ot->sl = IT_--st_argp, resutn(sl(seto ou;
	statusion(new);
	2 se*rqspace(xconn, neb= IT_kdep_ nfserr_not_
	 * We  Succ	__!eb=mpze belos_slo_lockseqid + 1) {
	= _re= cotID;sl_flags |= NFSD4_SLOT_INITIALIZEUSEs =d == slids is FALS)
tl_flags |= NFSD4_SLOT_INITIALIS)) {
		o;	goto out_flags & NFSD4_S= ~OT_INITIALIS)) {
		o;	
	minorver
stase_ctatus minorver
	__be3e=)
	__be3us minorvers->se_
	}
	pin_unlobs->flavb_session =t		if (!c
		expirT_COB_DOWN:
si_re= cnfser_new->cl_SEQNITTATUSCOB_PATH_DOWNn ntdefault:
		expirT_COB_FAULT:
si_re= cnfser_new->cl_SEQNITTATUSC bacCHANNEL_FAULTn ntdefault:
		/*
		 * _re= cnfser_new->cl_0w->cl_us)
pty(&clp->cl_revoked)) {
		dp  * _re= cnfser_new->c|l_SEQNITTATUSCRECALLABLEITTATECREVOK(nfsatu:
	k
	__be3: s->cb_add  * n(conn);
	if (old)ock(&nn->client_lock);
	/* init tatus;
out_free_conion(new);
	unlt_session(new);
	ression);
	if (stat;
	statu:
	k
	__be32 2
nd4_set_ex_flaeplay(rdonesvc_rqstssion nns *resp)
{
	struct nfsd4_op *op;
_state *cstate,
		->xdr;
	__minorvdep_)
		nn *coentid_hashtnmiu (!same_crcser
nfserr!_replay_cache) {
			status =nre_cache_entry(struct nif (stat =mier
stags & NFSD4_S= ~OT_INITIALIZEUSEs =case 5, Drop result intf	go)
{
viouswaheyake gss change_replay(s)(like	 *nnn ion(new);
	icsnid.data,eturn (cr_ses->stid, p  * p
	exck);
	ient_>stid, p e32
nfsd4_exchangeclient(c;
		gotosvc_rqst *rqstp,
		     nfsd4_compound_state *cstate,
		     nfsd4_compoundclient(c;
		goto *dcuct nfsd4_sessi *conf, *unconf;
	struct nfs4_client *old = NUs->se__be32 status = 0;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	if (cr_sk(&nn->client_lock);
	unconf = find_unconfirmed_client(&cr_ses->dcid, true, nn);
	conf = find_confirmed_client(&cr_ses->dcid, true, nn);
	conf = fONCE(conf && unconf);

	if (conf) {
		status =nt_has_state(conf)) {
				status  nfserr_clid_inuse;*clid,copy; out_free_co;chaus = nfserr_went_expired_locked(conf);
		if (status)
			goto out;
	}
	new->ss->se_ else {
		statnf)
		expire_cs->se_	struct n	status = nfserr_stale_clientid;
		goto out_free_cow->cl_us)
pds_match(conf, rqsct n{
				statuss->se__be32 s= nfserr_wrong_cred;
		if (!mac_free_cow->cl_lient_locked(unconf);, p e3in_unlock(&nn->client_lock);
	if (new)
		e, p  * lient(old);
ou	returntatus;
}

static _fsd4_replay_creclaimd_stalet(svc_rqst *rqstp,
		     nfsd4_compound_state *cstate,
		     nfsd4_compoundreclaimd_stalet(
		ce32 verf[2];	statu/*
	 * T)
		ccidrca_one_fs (!same_creminorvers, we o_fh.fh_druct urn nfserr_jukebox;nofile		 */

	ren ient mt supw usyake advantiresss "pserca_one_fshe sp'd rathTiou's OK, iu's op bac6", se the saft
	 igner tit'd ratike	 nno
{
	ca->heade} p_ nfserr_not_same_stalet(_handle:ew)
		eter ra *csFSD4_CLIENT_CONFIRMERECLAIMD);
	LETE struct n &minorver
	__be3prog =		nfsgs);
	renew out_free_se;ip_ nfserr_not_sameientid;
		goto ou
		eisexpired_locked(nminorver
	__be3prog =		nfs))	ren ient m is fonot bngae
		 *ite rindllh */ gal'd rathlso support  res pretnf)nt_lock */ ffecd_falicitlyd rathclient(ache siz/
	clid Surt
	 itonsel;
	er thp)
{wrequer.  But i itog->p nnel ackchon from the allbackds_f
ent might t'd ratike	_free_se;ip_ nfserr_not_>headend_slot *);
	iec
		sessionnminorver
	__be3prog =		nfs)coin_unltatus;
}

static _fsd4_exchange_rt;
		gotosvc_rqst *rqstp,
		     nfsd4_compound_state *cstate,
		     
ct nfsd4_exchangecrd *se, structt	/* pNFS dr_netobj *name, s	nn);
	e=)
	tags = sonst c;erifier		verf = ecl *verf)
{=)
	tags = sontch)t nfs4_client *old = 	new;
	struct nfs4_client *unconf	= NULL;
	__be32 status;
us  nfsert nfsd_net *nn = nnet_generic(SVC_NET(rqstp), nfsd_net_id);

	rpc_ntopeate_client(exid->clrqstp, &verf);
	cl *verf)
new == NULL)
		return nfserr_jukebox;

	/* Cass below refer to rfc 5661 secguage18.35.4: 4.2.33in_lock(&nn->client_lock);
	conf = find_confirmed_client_by_name(&exid->clnn);
	if (conf) {
		bool cre1 (norma0:atus = nfserr_noent;
	se;
			goto ou
		e, pchangeid(conf)) { /* bu out;
	}
	new->se_creds(&unconf->_cred, &rqstp->rq_cred) ||
		  status emask[INET6_ADDRSTRLEN];
	nfs4_ver	p(sa, addrsockaddr *) &clp->cl_aaddr)) {
			tr, sizeof(
sizeoddr_str));
	dprintk(	("NFSD: move_to_crd *se, st:as our band nocodeock */ "
size"ous%clp->r, sizeofto out_free_co else if N find_unconfirmed_client(&cr_ses(&exid->clnn);
	if (conf) {
		expire_client_locked(unconf);

	/* casf) {
		bonf);f(&verf, &c_verifier);

		if;
	cl *verf)
n)cre1 (norma1:y be usick channel  exid->f = NULpyw, nn);
	ad	/* casf	stat4, possib NULL)=		nfs)nt reow re2, 3_has_sta*/
		co):atus =(new, nn);
	add_to_uminorversion = cstate->ct nback(struct nmac	n;
tags  n{
				;_unconfirmed(new);
	swap(new,	tags = son.cl_boot = conf->clminorver.cl_boot;
	exid->cl,	tags = son.cl_boot = c>clieninorver.cl_boot;
	eto our_ses->,	tags = son.ata, verf, sininorver.ata, verf, siddr_str,	tags = son.ata, verf, )to_umin
	struct nfsfs_ok;
	/*
	 * in_unlock(&nn->client_lock);
	if (new)
		expire_cent(clp);
		f (unconf)
		expire_client(unconf);
	return status;
}

static __fsd4_exchange_rt;
		goto(struct nfs4_cli *rqstp,
		     strcnfsd4_compound_state *cstate,
		     
ct nfsd4_sequence *t;
		goto(struct ructt	/*	goto(struct uct nfsd4_sessi *conf, *unconf;
	struct nfs4_client *old = NULL;
				goto ifier		verf = struct r= ctt	/*	goto(struct nt);
		ruct ; flagseclid, boessio->xdctt	/*	goto(struct nt);
	
		goto ouerf[2];	status = cd4_conn * = nnt_generic(SVC_NET(rqstp), nfsd_net_id);

	if (cr_ses->STALECONFIRMID(ags  nnnurn false;
	ot_sameientid;
		goto oock(&nn->client_lock);
	conf = find_confirmed_client_by_name(ags  nreturadd_to_u find_unconfirmed_client(&cr_ses-ags  nreturadd_to_ue do not slienn retuo ggatie a unilien;
		goto'sneed es-sup resa se RPattlp->t owneruct r r.
 amen;
		gotoro, onalt foenew;
qstp-se RPno
	 * nalienror t	 * did Leu's emasitusingassume iu's ourse RPiugreturn ! nfserr_seq_misordt;
	}
	xdr_enf)
		expiot->sds(&unconf->cl_cred, &rqstp->rq_cred) ||
		   out_free_se;in) {
		bonf);eds(&unconf->_cred, &rqstp->rq_cred) ||
		  sout_free_se;in4, possrefer to rfc 5661 secguage18.35.4: 4.2.34 (likely(s!		expio_empf(&verf, &c_verct ,->cl_cs_slot;
truct uatus =nt_ha	bonf);e/* case 4, possi2:y be usick cliansmittatus = nfserr_seq_>headef	stat4, possib:eock */ ente rinoticget e*/
		cogetyet?tatus = nfserr_seq_misorentid;
		goto out_free_cow->cl_ nfserr_bads (seqid {
		bool  1 (norma1:y channel  exid->f = NLL;
			struct nclient_locked(unconf);	if (stannnel_attrgck_sync(se(nconf;>cl_cs_slot;
	struceturn (cr_se 3 */
			i: ase) */
out;inin ts:
			cogetold = NU = NLL;
		firmed_client_by_name(&unconf->cl_name, nn);
		if (old) {
			status = mark_client_expired_locked(old);
			if (status) {
				old = NULL;
				goto out_free_coto oumove_to_confirmed(unconf);
		conf = unconf;
	} else {
 nbatlocked(unconf);	conf = ock(&nn->client_lock);
	if (new)obe_callback_sync(se;	conf = ock(&->client_lock);
	if (new)p
	exck);
	ient_unconf);	conf =in_unlock(&nn->client_lock);
	if (new)
		expire_client(old);
	return status;
out_freec struct nfs4_client *filestrbe_cann_frofile(d4_surn clp->cl_kr_sntry(stnn_fr(file(sl(b, GFP_O "NEL) * Cach OPEN Shortedtate,hel frn correctsic void
nfsd4_c it_conn(rqsfile(fs4_client *filestf  nfsd4_cok_id);fhstfhigned int idhashva= clientidfile(clid->clfhidep_assert_held(&nn->cliedtate	/* initpratomin(&ct(&fed)fi	ienf;1f = ock(&->clnn(rq(&fed)fi	if (new)ZED;_LISTmacro(&fed)fi	dtateidsnew)ZED;_LISTmacro(&fed)fi	ons)
		|| !lew)fhdr((stsh not (&fed)fi	f		 */
 nrhlew)fed)fi	ons)
*files				goto fed)fi	hamed_cllic->clreturto fed)fi	short_druye->ct nr_s&ct(fed)fi	fdsne0siddr_strfed)fi	fds)to_ur_s&ct(fed)fi	acc	__ne0siddr_strfed)fi	acc	__)to_uh(&clp->cbl = (&fed)fi	n->conffile(clidshva);
	rb_eras2
nd4_set_ex_fsion(nl(bs(d4_surn ckr_sntry(stclient((rs)
		|| (nl(bto_ukr_sntry(stclient((if (		|| (nl(bto_ukr_sntry(stclient((file(sl(bto_ukr_sntry(stclient((dtateid(sl(bto_ukr_sntry(stclient((ons)
*sl(bto_2
nshv
it_conn(rqsnl(bs(d4_surn crs)
		|| (nl(be->kr_sntry(stessionnxchangers)
		|| !f->cl	ddr_str,s4_client *rs)
		|| )ne0si0sireturew)
		exs)
		|| (nl(be-		return n_free_coto if (		|| (nl(be->kr_sntry(stessionnxchangeif (		|| !f->cl	ddr_str,s4_client *if (		|| )ne0si0sireturew)
		eif (		|| (nl(be-		return n_free_cofsion(xs)
		|| (nl(bto file(sl(be->kr_sntry(stessionnxchangefile!f->cl	ddr_str,s4_client *file)ne0si0sireturew)
		efile(sl(be-		return n_free_cofsion(if (		|| (nl(bt nfsfseid(sl(be->kr_sntry(stessionnxchangedtateidsf->cl	ddr_str,s4_client *rledtateid)ne0si0sireturew)
		efsfseid(sl(be-		return n_free_cofsion(file(sl(b;t:
	s)
*sl(be->kr_sntry(stessionnxchangeons)
		|| !f->cl	ddr_str,s4_client *ons)
		|| )ne0si0sireturew)
		e
	s)
*sl(be-		return n_free_cofsion(fsfseid(sl(bn status;
0
	pin_fsion(fsfseid(sl(b:_ukr_sntry(stclient((dtateid(sl(bto__cofsion(file(sl(b:_ukr_sntry(stclient((file(sl(bto__cofsion(if (		|| (nl(b:_ukr_sntry(stclient((if (		|| (nl(bto__cofsion(xs)
		|| (nl(b:_ukr_sntry(stclient((rs)
		|| (nl(bto_in_unl"nfsd4_exchang:ie a ofwe therefhilesn(rqializur bchav4\n"rn status;
-ENOMEMtic void
gen_calsn(rq_ent *truct nfsd4_compo *truct 
		turn cled) p= nfserr;
	memcpordt;
	}
	xdr_eled) p= = IT_->s0r_eled) p= = >csted) p=iruct nmutexnn(rq(&ted) p=mutex)tic void
gen_calsnnnel_adtate	eldt istruct nfsd4_compoundargs *arcstate,
		     
ctfs4_stid *s;
    		||  *souct nsion)nn *coentid_hashtnmit		ifold = mutexn->cliesont)ostruct . p=mutex)ti =minorverache) {		||  =id *s;gsslo    		|| (sou;_be32
n_calsnnnel_adtate	clearstruct nfsd4_compoundargs *arcstate,
		    uct nfsd4_sessi *
    		||  *soe->minorverache) {		|| cr_ses->soe!		returtatussinorverache) {		||  =i		goto omutexnnn->cliesont)ostruct . p=mutex)ti =ssi *ion(n    		|| (sou;_be32
noid
geninl_CB__cals*ssion(&    		|| (ssd4_cokr_sntry(s res(b, dr_netobj *name, s*		|| t,fs4_stid *s;old = NUs->uct nfsd4_sessi *
    		||  *so;_g nsgs->okr_sntry(stnn_fr(sl(b, GFP_O "NEL) *se_credoprn false;
			goto nsgsnt)os		|| erf, ->okr_sdup(		|| ->rf, si		|| ->d->clGFP_O "NEL) *se_credopnt)os		|| erf, rtatuskr_sntry(stsion(sl(b, s	returfalse;
			goto>cl_ opnt)os		|| eIT_->s		|| ->d->to nZED;_LISTmacro(& opnt)osdtateidsnew) opnt)osold = Ne_
	}
	rn(rq_ent *truct n& opnt)ostruct 	;_untomin(&ct(& opnt)osocode, 1rn status;
oo;_gc void
gen_calsent_lrs)
		|| r,s4_client *rs)
		|| s*	ot,fs4_stid *s;old = NUs->,  int idhashvafs4);
	rb_kdep_assert_held(&nn->clielock);
}

statior_each->c(&oontoos		|| e)osdt4);
	 
ct _openowner	|| !tr(clidshvafs4);
	rb_erasr_each->c(&oontoos fr=		nfs)
_openowners)
		|| !ltic void
gen_calsnnn4_lient_lrs)
		|| r,s4_client *
    		||  *souct nlient_lrs)
		|| old);
			s)
		|| r,o)ltic void
gen_calsnnn4_sion(xs)
		|| r,s4_client *
    		||  *souct n,s4_client *rs)
		|| s*	o->s	s)
		|| r,o)tiorkr_sntry(stsion(rs)
		|| (nl(bsi	oltic void
gend_cst ,s4_client *
    		|| on(resp, oss	s)
		|| _n_t(u3t ne)oslient_ =	nnn4_lient_lrs)
		|| , ne)ossion =	nnn4_sion(xs)
		|| ,
}; struct nfs4_client *rs)
		|| s*
ssion(n(rq_rs)
(&    		|| ( int idhashvafs4);
	rb_ nfsd4_compoundrs)
truc	    (s nnfsd4_compound_state *cstate,
		    uct nfs4_client *clp = ses->se_minorvers->; n,s4_client *rs)
		|| s*	o,{
	se;ip_	o->sssion(&    		|| (rs)
		|| (nl(bsi&rs)
= sl_		|| t,	returne_cre	olurfalse;
			goto>oontoos		|| e)osn_t(u3&	s)
		|| _n_tto>oontoos		|| e)osis_rs)
(		||  =i1to>oontoos		|| e)os1) {
	= rs)
= sl_cotID;sloontoosnew->cl_0w->)
		nn *coentid_hashtnmit		ifo= NLontoosnew->c|DFC4_FOOOED_R;
			g;sloontoosti	/* Make oontooslastd_loangesti;
				goto ZED;_LISTmacro(&oontoos_loan_lruf = ock(&->clielock);
}

statiopy_crefirmers)

    		|| o!tr(ession);s4);
	rb_ nuc	  ,	returne_crpy_cr		returtatusent_lrs)
		|| r	o,{s->, fs4);
	rb_kturfals	= roturn (cr_i =ssi *sion(xs)
		|| r&oontoos		|| confock(&clp->cl_lock);
}

statiopy_e;
		se;ic void
gen_calsn(rq_rs)
(&    tosvc_rqstent *rledtateid *s   nfsd4_compo *filestf  nfsd4_compoundrs)
truc	 rtatu,s4_client *rs)
		|| s*	o->s	s)
= sl_	s)
		|| itpratomin(inciedts;
}
esti;.);
		ufs)co	dts;
}
esti;.);
types			4_FOOPEN_STIDto ZED;_LISTmacro(&dts;
}
e

sts)co	dts;
}
est   		||  =id *s;gsslo    		|| (&oontoos		|| confgsslmpo *file(fp)co	dts;
}
esti;.);
files		fpco	dts;
}
eacc	___bmaptruct nfss;
}
edruy_bmaptruct nfss;
}
ers)

 >se__be32 sn(rq_rwses->fss;
}
erwsesf = ock(&->clieoontoos		|| e)os=		nfsgs);


statio_each->c(&fss;
}
e		g
    		|| si&rontoos		|| e)osdtateidsnew) ck(&->cliefed)fi	if (new)_each->c(&fss;
}
e		gfileonffed)fi	dtateidsnew)ock(&clp->cl_fed)fi	if (new)ock(&clp->cl_oontoos		|| e)os=		nfsgs);


stati Cache thI_re si too.
	 *we count owkeepre siithout */
	if a littlesfhiles ow		 */
e theLOSEreturns.ot s* lessuppoount owdrc_mem anydfile acc	__
viousis,hel cod
 RPno
m befer tpy_e;
ur bhowe
static void
nfsd4_seconfirmedloan_lrusvc_rqstent *rledtateid *s nfsd4_com= SVC_Nuct n,s4_client *rledtateid *last; n,s4_client *rs)
		|| s*	o->s	s)
		|| r,;
}
est   		||  = nfsd_net *nn = net_generic(net, nfss;
}
esti;.);
			nfsgsf (>o outtannne;

	if (cr_s"NFSD: move_to_confirmedloan_lruient *rs)
		|| s_func__oo) * These RPWe k
		
viouswebhol coneintf	go)
{
viaompound_loan respoanono
	se RP"nt session"intf	go)
{
 allbackz/
	clidIs "pserefners wis,higherse RPnoane2, tt onno
	 ported less_syn, andses-r	__
viousorteis in 4.0 se RPstateid.ot scae ripure fla);
filesntf	go)
{
rs iled wilortefirished.se RPWait
 allbackrefners w owdrop  ow2. S	 * ditn set.
 * lient_tp-se RPno
	 probablybeonseda
	er os "pserefners wgo in nnel  e agaandequest l harpo itreturn !wae(xdvses-agoan_wq retomin(r = (&s;
}
esti;.);
		ufs)cr		2)_g nfsc_mem: \
	acc	__(n_unlsions;
}
esti;.);
filertatusion(mpo *file(s;
}
esti;.);
filerturfs;
}
esti;.);
filese__be32 s}oock(&nn->client_lock);
	conf = flast	= rontooslastd_loangesti;ke oontooslastd_loangesti;
		sew)_eachconfirail(&oontoos_loan_lruonfnt_locoan_lruf = oontoosti	/* Mion_lod_cds(f = ock(&nn->client_lock);
	if (new)
		eiast)i =ssi *ion(n tos&iast;
}
esti;) * Cach search file(clidshva]
 allfilest/struct nfs4_client *filest
firmefile(ession);s44_cok_id);fhstfhigned int idhashva= clientidfile(clid->clfhide	fsd4_compo *filestf dep_assert_held(&nn->cliedtate	/* initprh_each_entry(clp, &tbf  nffile(clidshva);
	rb_e, fi	n->catus =nt_h_id);fhonf, rq&fed)fi	f		 */
 nrhlold = Ngsslmpo *file(fp)co	rfalse;
	fpco	se if NULL;
}

static struct nfs4_client *filest
firmefile);s44_cok_id);fhstfhignedfsd4_compo *filestf dep_k(&nn->cliedtate	/* init	fpcrefirmefile(ession)fhide	fck(&nn->cliedtate	/* init	alse;
	fpcoc struct nfs4_client *filest
firmeent->cbfile(fs4_client *filestuct nfsd4_exk_id);fhstfhignedfsd4_compo *filestf dep_k(&nn->cliedtate	/* init	fpcrefirmefile(ession)fhide	
		efpcr		returtatusit_conn(rqsfile(uct nfhide		fpcre
	if (up	fck(&nn->cliedtate	/* initt	alse;
	fpcoc sche the llunt own't qudruyewt onRcroro, onaessly lPstateid ore thWRITEro, onaessly lPallaessoneistateidtic void
nfsfsd4_excha4	short_d_cllic-nfs4_cli *rqfhsts, we o_fh,  int idhashvadruy_typeignedfsd4_compo *filestf deuerf[2];py_creplat:
	sp	fpcrefirmefilel_l, we o_fhd)fh_		 */
turne_crefprn false;
		se;is bel't qunt to_cllic- sizehortespace(arectsic v) ck(&->cliefed)fi	if (new)
		efpd)fi	short_druye&adruy_typeigrfals	= 	memcpoessionew)ock(&clp->cl_fed)fi	if (new)ion(mpo *file(feturntatus;
	se;ic void
gen_calsmpound__sz + \
	petuortnfsd4_compoundachannel *cbignedfsd4_compo *ons)
		||  *d>se_mbirmeons)
		|| (cb = nfsd_net *nn = net_generic(net, nfsded)dlesti;.);
			nfsgsf (>o outt t_id);

	if (cr_sb->clnons)
		|| !(&ded)dlesti;.);
filed)fi	f		 */
) * These RPWe cae ridor soloss chan_defau	ons)
*cbybeca noci wisse RPaandle:
	ol ur banoded)i	if (returse RPIs "psedleti	/*!=i0sitt onwe k
		
viousitn setaandle:
.
 *se RPqueudhant ta l_mem defau. Dpw usqueud doesgaanreturn ! (&nn->cliedtate	/* init	
		e
ed)dleti	/* =i0rtatus
ed)dleti	/* Mion_lod_cds(f = )_each->cirail(&
ed)dlez + \
	lruonfnt_lonsez + \
	lru)f (up	fck(&nn->cliedtate	/* init2
noid
genintsmpound__sz + \
	donesvc_rqstssion nchannel *cb 
ctfs4_sti(sa,tasl *taslignedfsd4_compo *ons)
		||  *d>se_mbirmeons)
		|| (cb = nlobs->flatasl->tk_{
				old =norma0:n false;
	1to>norma-EBADHANDLt)d)
		ex-	4_FERR_BADITTATEID	 * GSS caseRace:eock */  be usiywgot __sz + \
 befer trs)
teturyS casegran- sizons)
		|| 'd ratike	
		e
ed)dle cliies--eturn elsa,onsayatasl,	con HZ)co	rfalse;
	0co	se i GSFALLTHRUtike
		/*
		 * tatus;
-1;_be32
noid
gen_calsmpound__sz + \
	fsc_memnfsd4_compoundachannel *cbignedfsd4_compo *ons)
		||  *d>se_mbirmeons)
		|| (cb = 
=ssi *ion(n tos&ded)dlesti;)tic __be32 n cd4_conn *consync(se_n_t(mpound__sz + \
	n_t(u3t nepetuort	=smpound__sz + \
	petuort, nedone		=smpound__sz + \
	done, nefsc_mem	=smpound__sz + \
	fsc_mem,
}; struct n_calsmpou_defau	one_ons)
r,s4_client *ons)
		||  *d>ignedhese RPWe'	 possumnumber ostate,\
			ne
stwdroplostssntf	go)
{se RPo, oe a firsa*/
connumber oc_memid S	 * don'	 pi	 * eroc_memse RPachannel ( forwi)
{
vi oc_mem,\
			
	 *, nalizo codevi okerness than* innwe k
		
vi.
 *t;
	 ente ri/
conache sic_mem,ye newe k
		turns o's safteyoveake asntf	go)
{return !atomin(incieded)dlesti;.);
		ufs)co	mpoundrund__(&
ed)dlez + \
) * Cach e lluntes(r defau_c_memn)ro, oni	if (,hel .ic void
nfsfs_m
chan_defau	ons)
*cbr,s4_clifile(essistf_kdep_fs_mapy_crefeturto fsd4_compo *filestf _gep_sd4_compo *filest)fl&= ~{		|| crdfsd4_compo *ons)
		||  *d>dep_)
		gfetturn ONCE(1, "(%p)&= ~{		|| 	retuunc__f_kturfalse;
		se;is}w)
		efpd)fi	hamed_cllic-tturn ONCE(1, "dualicate,defau ack_func__fpkturfalse;
		se;is}w)e do not supe riwanre fla

sts,\
			yovei	/oure flac_mem,nt ttus = RPoe'lli/
cona doeourself es-azons)
		|| *ite rindse;
edturns nvei	/if (ca->fl&= ~{defau	ti	/* Makev) ck(&->cliefed)fi	if (new)fed)fi	hamed_cllic->cl);
	;w)e do noIs "ps	 portensedns)
		|| !backchan_eacsitt onhis
		 *;
	se RPso
viousvi oc_mem,\
			 poor o al = respodns)t tit'd (likely(seq(&clp->cl_fed)fi	ons)
		|| !ligrfals	= );
	;w)(cr_i =_each_entry(clp, &tbdponffed)fi	ons)
		|| !,edle		gfileu outmpou_defau	one_ons)
rdpnew)ock(&clp->cl_fed)fi	if (new)tatus;
	se;ic void
genshv
it_c_attrgckons)
*cbr,s4_clifile(essist*on_eacsiintsarg nfsd4_ex_eachl = r*dispoprintk(ly(sarge&aF_UNLCKrn false;
	c_mem:modify(on_eacsiarg ndispopri;w)(cr_i =tatus;
-EAGAINtic void
gend_cst ,s4_cli->clnmanag| on(resp, ossit_c_c_mem:mng	n_t(u3t nelm_defau_get_se_defau	ons)
*cb, nelm_attrgc_get_se_attrgckons)
*cb,
}; struct nfsd4_check_cb_sec(s_ses->_sd4_compound_state *cstate,
		     nfsd4_compo *
    		||  *soot_seqireturn{->)
		nn *coentid_hashtnmit		ifo= Nfs_ok;
	if (seqid == slot_seqiont)os slot_- 1rn false;
	ot_sameache) { c;erd == slot_seqiont)os sloto= Nfs_ok;
	if (seqialse;
	ot_samebad_cotID;sc struct nfsd4_calookupc;
		gotosagseclid, boags  
ctfs4_stid *undargs *arcstate,
		     
ctfs4_stid *n)
{
	u32 maxrfs4_client *clp = sef *arconf) {
	inorvers->tturn f *arse_minorvers->; nse_creds(&un nn)&f *arorver.cl_boot,essio)urn nfserr_jukebox;rentid;
		goto outnno
{
	ca->heade} p_es->STALECONFIRMID(ags  nnnurn false;
	ot_sameientid;
		goto ooce do noFt tv4.1+-sup resnt_lock */ nlikelySEQUENCEtrsidIs w supe rihaatienmse RPac;
	ifaandle:
tt onwe k
		
viill
	 f	 *it f	 *v tooespo" under t"se RPo,oorbe fetur'd (likeONCE(conf && uinorver
	__be3f = ock(&->client_lock);
	if (new)f *arse_firmed_client_by_name(ags  nreturadd_to_u)
		gf *artatus  ck(&nn->client_lock);
	if (new)nfserr_encr_alg_ocked(;is}w)atomin(incief *arorverrefners f = ock(&nn->client_lock);
	if (newss beloy(s er ofnt *clp = snliuinorv!urn !linorvers->se_f *arconfs_ok;
}

static _fsd4_c
it_conforc	__*rs)
1nfsd4_compoundargs *arcstate,
		     
ctt nnfsd4_compoundrs)
truc	  nfsd_net *nn)
{
	u32 maxragseclid, boags	gotor=i&rs)
= sl_;
		goto oufs4_client *old = NUs->se__be32 s int idhashvafs4);
	rb_; n,s4_client *rs)
		|| s*	o->s_be32 status = 0;
	scr_ses->STALECONFIRMID(&rs)
= sl_;
		goto nnnurn false;
	ot_sameientid;
		goto o)e do noIno.
	 *we counti slot| siaft| Poe'atiaandle:
essionde flse RPfile aspodpe riwanre o risl a furs faid thur/if (ca->rs)
= sl_filese_nbe_cann_frofile(rew)
		exs)
= sl_filese		return nfserr_jukebox;

	/* Casl_ nfserr_blookupc;
		gotosagseclid, 		     n (status)
		return status;
	status =s->se_minorvers->;  n,s4= clientidr	|| !tr(clid->cl&rs)
= sl_		|| f = oocrefirmers)

    		|| o!tr);s4);
	rb_ nuc	  ,	return	s)
= sl_	s)
		|| 	= roture_cre	olatus _freent_u		|| crdcl_us)
p(Lontoosnew->c&FC4_FOOOED_R;
			g)ol cre1 (Rche)
{
rsd_client_iithout o, oe a _sec(numbfts:
	urns.ourn nfsc_mem:rs)
		|| r	onew)n	s)
= sl_	s)
		|| 	= 		goto o_freent_u		|| crdcl_ nfserr;
	me_cb_sec(s_ses->		     n_oontoos		|| , rs)
= sl_cotIDstatus)
		return status;
	status =_freession(&    to ont_u		|| :p_	o->sssion(n(rq_rs)
(&    		|| (;s4);
	rb_ nuc	  ,	it		ifew)
		exo)
		return nfserr_jukebox;

	/* Cass	s)
= sl_	s)
		|| 	= rotussion(&    to:ss	s)
= sl_
 >se_ent *ssion(rs)
(&    tos	returne_cre	s)
= sl_
 >rn nfserr_jukebox;

	/* Cassfs_ok;
}

static __be32 ninl_CB_fsd4_excha4	_sec(sons)
moder,s4_client *ons)
		||  *d>siints	renewn{->)
		(new->c&FWRITTATE)nf);e
ed)dletypes				4_FOOPEN_DELEGATECREADurn false;
	ot_same	s)
mode;w)(cr_i =tatus;
}

static __be32 nintzehorteacc	___tosnew->
	cs_ehorteacc	__urn clp->cl_ehorteacc	__s				4_FOSHARE_ACCESSCREAD ? RDITTATE :FWRITTATEcoc struct nfs4_client *ons)
		||  *firme
	s)
*s    tosvc_rqstent *old = NUs-,istateid, bo_uct nfs4_client *sti;

	se;ip_py_crefirmestateid,by_type(s-,isnez = SDELEG_STIDturne_cre	frese alse;
			goto>alse;
	
	s)
s    tos	fretic __be32 nfs_mampoundiskons)
*cur(fsd4_compoundrs)
truc	 urn clp->cl_rs)
= sl_;
aimdtypes				4_FOOPEN_CLAIMDDELEGATECCUR  !rp uct nnrs)
= sl_;
aimdtypes				4_FOOPEN_CLAIMDDELEGCCUR_FH;sc struct nfsd4_cxcha4	_sec(sons)
svc_rqstent *old = NUs-,istd4_compoundrs)
truc	    (fs4_client *ons)
		||  **d>ignedints	rene2 status = 0;
	s	= 	memcpobad_c    to odfsd4_compo *ons)
		||  *dns)
cr_s"ns)
crefirme
	s)
*s    toss-,i&rs)
= sl_ons)
		n(fsfseidrew)
		e
	s)
e-		return n_free_coto new->cl_ehorteacc	___tosnew->
	s)
= sl_
horteacc	__u;l_ nfserr;
	me4	_sec(sons)
moderons)
,s	renewtatus)
		returtatusit_ *ion(n tos&dns)
d)dlesti;)tiut_free_cow->cl_*d>se_dns)
crin_unlsion)nn *coiskons)
*cur(uc	 uo= Nfs_ok;
	if (seqid == 	return status;
	status =	s)
= sl_	s)
		|| ntoosnew->c|DFC4_FOOOED_R;
			g;sltatus;
}

static __be32 n,s4_client *rledtateid *et_ex_fsirmeexis- si_	s)
(fs4_client *filestf  nfsd4_compoundrs)
truc	 urn c,s4_client *rledtateid *locb_ n*py_cretruct nfsd4_sessi *rs)
		|| s*	o->s	s)
= sl_	s)
		|| itpr ck(&->cliefed)fi	if (new)_each_entry(clp, &tblocb_ nffed)fi	dtateids nfse		gfileul cre1 (igner tessisithout tike	
		elocb_;
}
est   		|| nt)osis_rs)
(		||  ==i0rrn nd_c- suto ou
		elocb_;
}
est   		||  ==i&oontoos		|| cturn elemr_blocb_; n	)atomin(incielem;
}
esti;.);
		ufs)co	ntdefault:se if Nock(&clp->cl_fed)fi	if (new)tatus;
	se;ic void
genshl_CB_intsmpo4eacc	___tosacc	__(u_check4eacc	__urn cints	rene/*
	 * T)
		eck4eacc	__c&FC4_FOSHARE_ACCESSCREADre_ceNFSD4_SLOT_I_MAYCREAD; T)
		eck4eacc	__c&FC4_FOSHARE_ACCESSCWRITEre_ceNFSD4_SLOT_I_MAYCWRITEit	alse;
	frene2 c __be32 ninl_CB_fsd4_exchaundsd4ncatesvc_rqst *rqstp,
		     nfsd4_co *rqfhstfh 
ctfs4_stid *undrs)
truc	 urn c,s4_cliir_seiir_seiu3t n	.ia_rb_tor=iATTRITIZE str.ia_ddr_/*
	,de}n ne_cre	s)
= sl_sd4ncate out;
	dprintk(us)
p(Ls)
= sl_
horteacc	__c&FC4_FOSHARE_ACCESSCWRITErrn false;
	ot_samestatus false;
	ot_*csFSr_seconn, nefh, &ir_sesi0si(ti	/_t)0);sc struct nfsd4_cad *s;gsslv

sfile(fs4_cli *rqstp,
		     nfsd4_compo *filestf  
ctfs4_sti *rqfhsts, _fh, vc_rqstent *rledtateid *s   
ctfs4_stid *undrs)
truc	 urn c,s4_clifilestfi->se__be32 status = 0;
	s; cintsofrense_ent *scc	___tosomoder	s)
= sl_
horteacc	__u;l_intsacc	__s	smpo4eacc	___tosacc	__(	s)
= sl_
horteacc	__u;l_ int idhaemaskoldeacc	___bmap,koldedruy_bmapitpr ck(&->cliefed)fi	if (newo)e do noAr *we  &tnumbed wireaudruyem
			yiouswbablyd_cllic->o, ose RPa, we osacc	__?eturn ! nfserr_seq_ *file	_sec(sonntbf  n	s)
= sl_
horteonntwtatus)
		retur!_replstatatus  ck(&nn->cliefed)fi	if (new)t_free_cow->clss bewireacc	__
vover.
filest/s! nfserr_seq_ *file	gsslacc	__(f  n	s)
= sl_
horteacc	__u;l_is)
		retur!_replstatatus  ck(&nn->cliefed)fi	if (new)t_free_cow->clss beSireacc	__
bstssinPstateid ca->rldeacc	___bmapcl_ets;
}
eacc	___bmap;cl,	tsacc	__(	s)
= sl_
horteacc	__, vcpnewss beSirenin druyemasl *a->rldedruy_bmaptrufss;
}
edruy_bmap;cl,	tsonntb	s)
= sl_
horteonnt, vcpnew fed)fi	short_druye_SLb	s)
= sl_
horteonntc&FC4_FOSHARE_DENY	BOTH)dep_)
		gfed)fi	fds[ofren]tatus  ck(&nn->cliefed)fi	if (new)t nfserr;
	me__	s)
(onn, new, _fh, S_IFREG, acc	__, ffilpkturfus)
			goto out;
	}
	ne*ion(acc	__;us  ck(&->cliefed)fi	if (new)t)
		gfed)fi	fds[ofren]tatus 	fed)fi	fds[ofren]tidfil>; ns	fi->se__be32 sse if Nock(&clp->cl_fed)fi	if (new)
		efil>rn nfion(filpktul_ nfserr;
	me_cbsd4ncatesonn, new, _fh, uc	 ueqid == 	return s;
	}
	ne*ion(acc	__;uin_unltatus;
}

stati	ne*ion(acc	__:o	dts;
}
eacc	___bmaptrurldeacc	___bmap;
=ssi *file	ion(acc	__(f  n	s)
= sl_
horteacc	__u;l_spact&cl;
	rbmapsonntbbmapsce(chortemoder	ldedruy_bmap), vcpnew _free_cow-c struct nfsd4_cxcha4	upgrade_	s)
(fs4_cli *rqstp,
		     nfsd4_compo *filestf  nfsd4_co *rqfhsts, _fh, vc_rqstent *rledtateid *s   nfsd4_compoundrs)
truc	 urn ctatus = 0;
	s; c int idhaemaskoldedruy_bmaptrufss;
}
edruy_bmap;cp_)
		gter racc	__(	s)
= sl_
horteacc	__, vcpnrn false;
	ot_s;gsslv

sfile(onn, nef new, _fh, n, neuc	 ueqss beter r forwiredruyem
			c v) ck(&->cliefed)fi	if (new) nfserr_seq_ *file	_sec(sonntbf  n	s)
= sl_
horteonntwtatus)
		retur=_replstatatus  	tsonntb	s)
= sl_
horteonnt, vcpnew  fed)fi	short_druye_So outb	s)
= sl_
horteonntc&FC4_FOSHARE_DENY	BOTH)deif Nock(&clp->cl_fed)fi	if (newl_is)
		retur!_replstatn status;
	status l_ nfserr;
	me_cbsd4ncatesonn, new, _fh, uc	 ueqid == 	retur!_replstatn staact&cl;
	rbmapsonntboldedruy_bmap, vcpnew tatus;
out_freec struct nd4_set_ece *t_;
aimdprev(fsd4_compoundrs)
truc	 ,nfs_maentid_hashturn crs)
= sl_	s)
		|| ntoosnew->c|DFC4_FOOOED_R;
			g;s Cach Sobablysup gatie a z + \
usickdtate?:ic void
nfsfs_msmpound__sattrs(&cgooosvc_rqstent *old = NUs-pwn{->)
		vb_session =t		is				4_T_COB_UPtn status;
);
	;w)e do noInver.
 *__be3she sp,rwi)
{
w supe rihaatiyovdstusiish ase RPstuorate,\
rs(cm the allachannelsnewe assume iu's OKse RPrs ilewebheaskos fawis/if (ca->lem;
		cinorversion = cstate&& vb_session =t		is				4_T_COB_UNKNOWNn c __be32 n,s4_clifile(essistent *ssion(n(rq_c_memnfsd4_compo *filestf  nints	renurn c,s4_clifile(essistf_	sp	flr_blocks*ssion(p->clto_u)
		gflese alse;
			goto>fl&= ~{lmn_t(u3&it_c_c_mem:mng	n_tto>fl&= ~{	rene/*
FLDDELEGto>fl&= ~{types		frense			4_FOOPEN_DELEGATECREAD? F_RDLCK: F_WRLCKto>fl&= ~{earse_OFFSET_MAXto>fl&= ~{		|| 	= ( ~{		|| _t)f deufl&= ~{ptor=ia, we o->tgirconfs_ok;
f_	s2
noid
genintsmpoce *tc_memnfsd4_compo *ons)
		||  *d>ignedfsd4_compo *filestf _geded)dlesti;.);
file; c,s4_clifile(essistf_,{
	se;ic,s4_clifilestfi->;l_ints nfserr;
0	sp	flr_bent *ssion(n(rq_c_memnf nez = SOPEN_DELEGATECREADuo_u)
		gflese alse;
	-ENOMEMti	fi->se_sirmendleusic*file(feturn)
		gfi->tturn lott srobablyalwaysihaatiasntleusicifilesps	 ptike	ONCE(conf && 1new)nfserr_e-EBADFdeif Nfl&= ~{	iles		fi->;l_py_creflew) nfserr_sv

s *tc_memnfi->,s	r&= ~{type, fflsireturew)
		eflese locks*sion(if ((f_kturd == 	return s;
	}
	ne*fion;
! (&nn->cliedtate	/* init	 ck(&->cliefed)fi	if (new)5, Diche sic_mem, resbroke gbefer twe  ooke fla

st?st/s! nfserr_s-EAGAINti)
		efpd)fi	hamed_cllic-tn s;
	}
	ne*clp->cew)5, Race,defau| s*/i)
		efpd)fi	ons)
*filetatus  nfserr_s0co	satomin(inciefpd)fi	ons)
e
	confsent_lons)
		|| (ession)dp__fpkturf;
	}
	ne*clp->cew)}w)fed)fi	ons)
*files		fi->;l_atomin(&ct(&fed)fi	ons)
e
	, 1rn sent_lons)
		|| (ession)dp__fpkturock(&clp->cl_fed)fi	if (new)ock(&clp->cl_dtate	/* init	alse;
	0ti	ne*clp->cunlock(&nn->cliefed)fi	if (new)ock(&clp->cl_dtate	/* init	ne*fion:
nfion(filpktustatus;
out_freec struct nfs4_client *ons)
		||  *et_ece *t_ons)
		|| (fs4_stid *s;old = NUs->, fsd4_co *rqfhstfh 
ctt nnfsd4_compo *filestf urn cints 0;
	s; cfsd4_compo *ons)
		||  *d>dep_)
		fpd)fi	hamed_cllic-tn status;
ERR_PTR(-EAGAIN(cr_s"N->sssion(n(rq_ons)
ss->, fhide	
		e!dptn status;
ERR_PTR(-ENOMEM(cr_sgsslmpo *file(fp)co	d(&nn->cliedtate	/* init	 ck(&->cliefed)fi	if (new)ded)dlesti;.);
files		fpco	)
		gfed)fi	ons)
*filetatus  ck(&nn->cliefed)fi	if (new)t ck(&clp->cl_dtate	/* init	) nfserr_seq_ * *tc_memndpkturf;
	}
	ne;is}w)
		efpd)fi	hamed_cllic-tturn  nfserr_s-EAGAINti)f;
	}
	ne*clp->cew)}w)atomin(inciefpd)fi	ons)
e
	confent_lons)
		|| (ession)dp__fpkturonfserr_s0co	ne*clp->cunlock(&nn->cliefed)fi	if (new)ock(&clp->cl_dtate	/* init	ne:atus)
		returtatusit_ *ion(n tos&dpd)dlesti;)tiuttatus;
ERR_PTR(		retur; if NULL;
}
d;_gc void
gen_calsmpoundrs)
	ons)
*none_ext(fsd4_compoundrs)
truc	 ,nints 0;
	surn crs)
= sl_ons)
		n(types			4_FOOPEN_DELEGATECNONE_EXTtatus)
		retur=_r-EAGAIN(w)n	s)
= sl_why:
	k"ns)
creWNT_COONTIRMIONti)	status =	s)
= sl_why:
	k"ns)
creWNT_CRESOURCEs =cobs->flars)
= sl_ons)
_wanrrtatuss		expirFOSHARE_WARMEREAD_DELEG:tuss		expirFOSHARE_WARMEWRITE_DELEG:tuss		expirFOSHARE_WARMEANY	DELEG:tustdefault:ss		expirFOSHARE_WARMECA &&L:tust	s)
= sl_why:
	k"ns)
creWNT_COA &&LL	g;slstdefault:ss		expirFOSHARE_WARMENO	DELEG:tustONCE(conf && 1new)n}_be32
nche thAttlp->t ow		 *ie a azons)
		|| 'd he thNot{
w supe risupport writeedns)
		|| ! respowpe rirs iled wsv

aente thprn(reisupport  allbacmatic void
nfsd4_sessi *rs)
_ons)
		|| (fs4_sti *rqfhstfh nfsd4_compoundrs)
truc	    (svc_rqstent *rledtateid *s  ignedfsd4_compo *ons)
		||  *d>; cfsd4_compo *rs)
		|| s*	o->s	s)
		|| r,ts;
}
est   		|| ide	fsd4_compo *old = NUs->se_dts;
}
esti;.);
			nfsdecintson u>;l_ints nfserr;
0	sp	on u> =smpound__sattrs(&cgooosoontoos		|| e)os=		nfsturn	s)
= sl_z + \
 ruct nfbs->flars)
= sl_;
aimdtypertatuss		expirFOOPEN_CLAIMDPREVIOUS:tuste_cremn u>o outn	s)
= sl_z + \
 ru1;tuste_crrs)
= sl_ons)
		n(types!			4_FOOPEN_DELEGATECREADu
 out_free_co:
	k"ns)
;slstdefault:ss		expirFOOPEN_CLAIMDretu:t:ss		expirFOOPEN_CLAIMDFH:tustGSS ce RPLeu's ort  gatie a anyddns)
		|| !b lesse
styone'sS ce RPhamsnt_lottrcet owdr;
aimsnt_irs....S ce R/tuste_crlocks*k(&grace	vb_seC_Nuu
 out_free_co:
	k"ns)
;slste_cremn u>o_emp(Lontoosnew->c&FC4_FOOOED_R;
			g)o
 out_free_co:
	k"ns)
;slstGSS ce RPAlsootnf)nt_lfileswahers)
dhant twriteeorS ce RPession,Pno
	 * nalgooolottrcet t_lock */'sS ce RPaboure otwritee otit,espaultur banda sece RPimmedth abz + \
 (wi)
{
w supe risupportsece RPwriteedns)
		|| !):S ce R/tuste_crLs)
= sl_
horteacc	__c&FC4_FOSHARE_ACCESSCWRITEr
 out_free_co:
	k"ns)
;slste_crrs)
= sl_;ssions				4_FOOPEN_CREATEr
 out_free_co:
	k"ns)
;slstdefault:s
		/*
		 * t_free_co:
	k"ns)
;sl}_s"N->st_ece *t_ons)
		|| (s->, fh,_dts;
}
esti;.);
filerture_crIS_ERRndpk out;
	statu:
	kdns)
cr_sr_ses->&rs)
= sl_ons)
		n(fsfseid, fded)dlesti;.);
fsfseid, ddr_strded)dlesti;.);
fsfseid))cr_s"NFSD: move_to_ons)
		||  fsfseid=" TTATEID_FMT "unc_outTTATEID_VALieded)dlesti;.);
fsfseid))crcrs)
= sl_ons)
		n(types			4_FOOPEN_DELEGATECREAD; Tit_ *ion(n tos&dpd)dlesti;)tiuULL;
}fsatu:
	kons)
: crs)
= sl_ons)
		n(types			4_FOOPEN_DELEGATECNONE;w)
		exs)
= sl_;
aimdtypes				4_FOOPEN_CLAIMDPREVIOUS &&rp uctrs)
= sl_ons)
		n(types!			4_FOOPEN_DELEGATECNONErtatus
eFSD: move_to_ONCEING:sntfis in ons)
		||  dr;
aim\n"rn sn	s)
= sl_z + \
 ru1;tuclss be4.1lock */ as(numbfts:azons)
		|| ?s*/i)
		ers)
= sl_ons)
_wanrr
usit_conrs)
	ons)
*none_ext(uc	  nfsretur; iULL;
}fsc void
gen_calsmpoundons)
*xgrade_none_ext(fsd4_compoundrs)
truc	 ,
trctl_sd4_compo *ons)
		||  *d>igned
		ers)
= sl_ons)
_wanrs				4_FOSHARE_WARMEREAD_DELEG &&rp uct
ed)dletypes				4_FOOPEN_DELEGATECWRITErtus =	s)
= sl_ons)
		n(types			4_FOOPEN_DELEGATECNONE_EXTtatt	s)
= sl_why:
	k"ns)
creWNT_CNOT_SUPP_DOWNGRADE;w)
		statnf)
rs)
= sl_ons)
_wanrs				4_FOSHARE_WARMEWRITE_DELEG &&rp	uct
ed)dletypes				4_FOOPEN_DELEGATECWRITErtus =	s)
= sl_ons)
		n(types			4_FOOPEN_DELEGATECNONE_EXTtatt	s)
= sl_why:
	k"ns)
creWNT_CNOT_SUPP_UPGRADE;w)
ss beOs fawis/snt_lock */ mfecdbe,\
rfhangiwanrnumbazons)
		|| turns oPaandle:
	as,Pno
	 fer twe upe riULL;
}turns	4_FOOPEN_DELEGATECNONE_EXTrespondlsonreturn c _fsd4_c
it_conforc	__*rs)
2svc_rqst *rqstp,
		     nfsd4_co *rqfhsts, we o_fh, fsd4_compoundrs)
truc	 urn c,s4_clienton nns *resp)
{
	strcreq_cred) ||	strde	fsd4_compo *old = NUs-->s	s)
= sl_	s)
		|| ntoos		|| e)os=		nfs;edfsd4_compo *filestf _getruct nfsd4_sessi *rledtateid *s  _getruct nfsd4_sessi *ons)
		||  *d>se__be32 status = 0;
	scr_se do noLookuplfile;tnf)f *ar,blookupPstateid espon't qurs)
tetquest-se RPespon't qufts:dns)
		|| !bnlikelyforc	__ os benumbz + \
ed.se RPIf ort f *ar,b;ssionser ofnt *filesfsd4_sf (ca->f>se_sirmeent->cbfile(xs)
= sl_file)
_o, we o_fhd)fh_		 */
turne_crf>s!>s	s)
= sl_filetatus  nfserr_scha4	_sec(sons)
sc_ nuc	  ,&dpkturfus)
			goto out;
	}
	neit	) n> =smpoundsirmeexis- si_	s)
(f neuc	 ueqrn (cr_seatt	s)
= sl_filese__be32 s= nfserr_seq_misobad_c    to od>)
		nn *coiskons)
*cur(uc	 uo= Nt;
	}
	neit	) nfserr_seq_miso

	/* Cass}r_se do noOPEN nt_lfile,Pallupgradekchoexis- sioOPEN.se RPIf sd4ncateid ths,Pno
oOPEN d ths'd (likely(ss  iturn lotStateid wahef *ar,bviill
	 choOPEN upgradekR/tusd		|(r = (&sss;
}
erwsesf =   nfserr_scha4	upgrade_	s)
(onn, nef new, we o_fh, fs neuc	 ueqrtus) {
				old = Nup(r = (&sss;
}
erwsesf =  t_free_co else if 	status = np->s	s)
= sl_ nptatt	s)
= sl_s  _getruct n	n(rq_rs)
(&    tosvc nef neuc	 ueqrtd		|(r = (&sss;
}
erwsesf =   nfserr_scha4	gsslv

sfile(onn, nef new, we o_fh, fs neuc	 ueqrtus) {
				old = Nup(r = (&sss;
}
erwsesf =  tfsc_mem:rs)
(&    tosvc f =  t_free_co else if
	 exid-(&    tosedts;
}
esti;.);
fsfseidrew)r_ses->&rs)
= sl_fsfseid, fdts;
}
esti;.);
fsfseidsiddr_str,tateid, ))crcup(r = (&sss;
}
erwsesf =->)
		nn *coentid_hashtn&	str->mit		ifold = nf)
rs)
= sl_ons)
_wanrs&xpirFOSHARE_WARMENO	DELEGold = N	s)
= sl_ons)
		n(types			4_FOOPEN_DELEGATECNONE_EXTtattt	s)
= sl_why:
	k"ns)
creWNT_CNOT_WARM	g;slst_freeno"ns)
;sls}ss}r_se dothAttlp->t ow		 *ie a azons)
		|| ' No But i ULL;
},ybeca noc flsenoOPEN succ	edsse
sn es-supd th'd R/tussi *rs)
_ons)
		|| (w, we o_fh, uc	  nfs f =no"ns)
:l_ nfserr_bads (seq_s"NFSD: mo%s: fsfseid=" TTATEID_FMT "unc_ __ cor___outTTATEID_VALiedts;
}
esti;.);
fsfseidrnit	ne:at be4.1lock */  &tnumbed upgrade/d		|gradekons)
		|| ?s*/i)
		ers)
= sl_ons)
		n(types				4_FOOPEN_DELEGATECNONEe&& d>s&&rp uctrs)
= sl_ons)
_wanrr
usit_conons)
*xgrade_none_ext(uc	  ndp)dep_)
		fpr
usion(mpo *file(feturnus)
		retur=_r0e&& xs)
= sl_;
aimdtypes				4_FOOPEN_CLAIMDPREVIOUS)i =ssi * *t_;
aimdprev(uc	  nnn *coentid_hashtn&	str->mit		ifo;_se dothToefirishre siis)
tetsp| !enewe ffecdoount owsresnt_lrnew->'d R/tu	s)
= sl_z	rene/*
	4_FOOPEN_RESULT_LOCKTYPE_POSIXco	)
		g(	s)
= sl_	s)
		|| ntoosnew->c&FC4_FOOOED_R;
			g)s&&rp uct)nn *coentid_hashtn&	str->mit		ifo sn	s)
= sl_znew->c|DFC4_FOOPEN_RESULT_D_R;
		;t	
		e
e)i =ssi *ion(n tos&dpd)dlesti;)tiuly(ss  ii =ssi *ion(n tos&dts;
}
esti;)itt	alse;
	out_freec s_calsmpound_c_mnup:rs)
(&    nfsd4_compoundargs *arcstate,
		     
ctp uct nfsd4_compoundrs)
truc	 ,ntatus = 0;
	signed
		ers)
= sl_	s)
		|| tatus  nd4_sessi *
    		||  *soe->&	s)
= sl_	s)
		|| ntoos		|| itprannnel_adtate	eldt istruct n		     nfonew)nssi *ion(n a  		|| (sou;_be3)
		exs)
= sl_filer
usit_consion(fileexs)
= sl_filer;w)
		exs)
= sl_s  ii =ssi *ion(n tos&xs)
= sl_s  ;
}
esti;) * Cafsd4_c
it_conient_svc_rqst *rqstp,
		     nfsd4_compound_state *cstate,
		     
cct nagseclid, boags uct nfs4_client *clp = ses->2 status = 0;
	scrnfsd_net *nn = net_generic(net, nfsrqstp), nfsd_net_id);

	if (cr_s"NFSD: moforc	__*ient_s%08x/%08x): fsfr- siunc_ 
ctpags = 
	exid-,essio= 
	ei;)tiu nfserr_blookupc;
		gotosagsd, 		     n (status)
		return s_free_co els->se_minorvers->; n,sfserr_noent;
	sb_path_d		|co	)
		geq(&clp->cl_vb_sessidns)
		|| !)
ctp&& vb_session =t		is!			4_T_COB_UPtn s_free_co el nfserr_bads (seqin_unltatus;
}

stati2
nd4_set_ex_fe *cgrace	fsd_net *nn)
{
	u32 maxr beuppooviir baf grace p, noifaandle:
e *eds*/i)
		ent_lgrace_e *edtn status;cr_s"NFSD: move_to_earsof grace p, noi\n"rn snt_lgrace_e *ed>cl);
	;w)e do noIs "ps
 *t;
	 go)
{d		| agaandrighet ow res		4_v4se RPalp = so,oord lessbesssiowunt owdr;
aimsaft| Pit _stsrefnel  e-se RPe
sn es-i/ ente riyetPhamsalottrcet owdr;
aims=t		isviillti	/returse R/o	mpoundrecor*cgrace	donesd_to_ue do noAt l harpo it,		4_v4Palp = she nrd lessdr;
aim.  B a nf)nt_se RPstt;
	 crnt_t! resy	yioushaatiort yetPdr;
aimngiwlessbesin_se RPof luckbackchannextsfs_treturse RP(	4_v4.1+-alp = shorted_csiderunt owhaatidr;
aimngiorcet t_yse RPacha RECLAIMDCOMPLETE.  	4_v4.0-alp = shorted_csiderunt o
e RPhaatidr;
aimngiaft| Pnt_ir firsa*OPEN.)se R/o	locks*e *cgrace	ent_lmpoundmanag| to_ue do noAt l harpo it,		 *iercetasser		 */ts:asy	os faed_ctaanersse RPexitPnt_ir grace p, noi, furs faidr;
aimsiwlessd th		 *se RPregulartasseir be nrspaumereturn c _oid
genti	/_tessi *larespomat	fsd_net *nn)
{
	u32 maxrfs4_client *clp = ses->2 sfsd4_compo *rs)
		|| s*	o; cfsd4_compo *ons)
		||  *d>denfsd4_sessi *rledtateid *s  denfsd4_se_eachl = r*po! r*next,espap_eacdenti	/_tew,toff* Mion_lod_cds(f - nt_lmpoundc_memdenti	/_tetsinin	ti	/oe->nt_lmpoundc_memde_s"NFSD: move_to_larespomatPstt;ice - fsfr- siunc);o	mpounde *cgrace	d_to_uZED;_LISTmacro(&spap_eacf = ock(&->client_lock);
	if (new)_each_entry(clsaft(po! rnext,eent_lock);
	irurtatuss->se_eq(&cl, &tbpo! rfs4_client *clp = ,ess	lru)f ()
		eti	/_aft| (( int idhalong)vb_sessiti	/, ( int idhalong)v,tofflold = N Ne_
	}sessiti	/ - v,toff;slstnin	ti	/oe->min(nin	ti	/o, s)co	ntdefault:se i)
		eent_expired_locked(old);
		s-pwold = N"NFSD: move_to_clp = snli nocsagseclid %08x)unc_outuss->orver.cl_boot.
	ei;)tiu nd_c- suto oue i)_each->c(&lock);
}
ruonfspap_eacf = f Nock(&clp->cl_nt_lock);
	if (new)_each_entry(clsaft(po! rnext,eespap_eacftatuss->se_eq(&cl, &tbpo! rfs4_client *clp = ,ess	lru)f ()"NFSD: move_to_purgir bunhangiclp = ssagseclid %08x)unc_outus->orver.cl_boot.
	ei;)tiu eq(&consen(rq(&lock);
}
ru)tiu lient(old);
	r	return}o	d(&nn->cliedtate	/* init	_each_entry(clsaft(po! rnext,eent_lonsez + \
	lru)tatus
ese_eq(&cl, &t bpo! rfs4_client *ons)
		|| ,edlez + \
	lru)f ()
		enic(net, nfsded)dlesti;.);
			nfsgsf (>t_id);

	if (r!_renrrn nd_c- suto ou
		eti	/_aft| (( int idhalong)
ed)dleti	/, ( int idhalong)v,tofflold = N Ne_
ed)dleti	/*- v,toff;slstnin	ti	/oe->min(nin	ti	/o, s)co	ntdefault:se i)lient_lons)
		|| (ession)dp)tiu eq(&c->c(&
ed)dlez + \
	lruonfspap_eacf = f Nock(&clp->cl_dtate	/* init	fhiles	geq(&clp->cl_spap_eacf)tatus
ese_eq(&cfirsacl, &tb_spap_eac rfs4_client *ons)
		|| ,
trctldlez + \
	lru)f ()eq(&consen(rq(&
ed)dlez + \
	lru)tiuttavoke_ons)
		|| (deturn}o= ock(&->client_lock);
	if (new)fhiles	geq(&clp->cl_nt_locoan_lrufrtus =	ose_eq(&cfirsacl, &tb_nt_locoan_lru, vc_rqstent *rp)
		|| , noutn	os_loan_lruf = u
		eti	/_aft| (( int idhalong)oontoosti	/ 
ctp uct n ( int idhalong)v,tofflold = N Ne_oontoosti	/*- v,toff;slstnin	ti	/oe->min(nin	ti	/o, s)co	ntdefault:se i)eq(&consen(rq(&oontoos_loan_lruf = = np->s	ontooslastd_loangesti;ke  oontooslastd_loangesti;
				goto   ck(&nn->client_lock);
	if (new)nssi *ion(n tos&dts;
}
esti;)it	 ock(&->client_lock);
	if (new)f Nock(&clp->cl_nt_lock);
	if (new
tnin	ti	/oe->max_t(ti	/_tsinin	ti	/o,LOT_I_LAUNDROMAT_MINTIMEOUTktustatus;
nin	ti	/oeec struct nfs4_cliworkqueud_fs4_cli*larespy_wq;void
gen_calslarespomatdmai (fs4_stiwork_fs4_cli*); struct n_cal
larespomatdmai (fs4_stiwork_fs4_cli*larespy maxrti	/_tetdenfsd4_seonsayngework *dwork =ed_ctaaner_of(larespy, vc_rqstonsayngework>o outta iworkide	fsd4_compon = net_gened_ctaaner_of(dwork nfsd_net *nn)
{
>o outt tslarespomatdworkide
	tr_scha4	larespomat	d_to_u"NFSD: move_to_larespomatdmai  - fleepnumbfts:%lorwid_cdsunc_ s)co	queud_onsayngework(larespy_wq,eent_llarespomatdwork_ s*HZ)coc __be32 ninl_CB_fsd4_escha4	_sec(sfh(fs4_sti *rqfhstfh  nfsd4_compo *sti;

s  ignedsion)nn *;fhonf, rq&fhpd)fh_		 */
, fdts;
};
filed)fi	f		 */
)rn false;
	ot_samebad_c    to odfs_ok;
}

static __be32 ninl_CB_shv
acc	___p, mrq_r = (vc_rqstent *rledtateid *s  ignedtatus;
)er racc	__(C4_FOSHARE_ACCESSCREAD nfs f  !rp	)er racc	__(C4_FOSHARE_ACCESSCBOTH nfs f  !rp	)er racc	__(C4_FOSHARE_ACCESSCWRITE nfs f =c __be32 ninl_CB_shv
acc	___p, mrq_write(vc_rqstent *rledtateid *s  ignedtatus;
)er racc	__(C4_FOSHARE_ACCESSCWRITE nfs f  !rp	)er racc	__(C4_FOSHARE_ACCESSCBOTH nfs f =c __be32 
fsd4_escha4	_sec(srp)
moder,s4_client *rledtateid *s   nints	renewn{- uct n  tatus = 0;
	s	= 	memcporp)
modeeqss beFt tessisdtateid'snewe ter rkelyfawe osuc	  nnore fla

st:(likely(ss  ;
}
ers)

 >)= = np->ss  ;
}
ers)

 >;->)
		(new->c&FWRITTATE)nf);!acc	___p, mrq_write(vcpk o                _free_co el)
		(new->c&FRDITTATE)nf);!acc	___p, mrq_r = (vcpk o                _free_co el nfserr_bads (seqin_unltatus;
}

stati2
n_be32 ninl_CB_fsd4_e
_sec(s_pecialedtateidsr,s4_clie= SVC_N,o *rqfhsts, we o_fh, fsateid, bo_sfseidsiints	renewn{->)
		ONE_TTATEIDr,tateid)nf);enew->c&FRDITTATE)o= Nfs_ok;
	if (seqi	statnf)
locks*k(&grace	C_Nuuturn lotAnsw| Pinrspmai ir be s)
{des)
d!backexis-o)
{
of
ce RPe_cllic- sizetate;Pso
we mfecdwae( oure flagrace p, noi.ourn nfsse;
	ot_samegrace;w)
		statnf)
new->c&FWRITTATE)
 false;
	ot_s;short_d_cllic-ns, we o_fh,o outC4_FOSHARE_DENY	WRITEreqi	statlot(new->c&FRDITTATE)nf);ZERO_TTATEIDr,tateid)nurn nfsse;
	ot_s;short_d_cllic-ns, we o_fh,o outC4_FOSHARE_DENY	READuo_2
nche thAsiownRcro/WRITErdur sizgrace p, noif|  dr;overunt=t		isonly
 allfiles
 RPnoousortenoreusicireeprovide m	 *athereasseir atic void
nfsinl_CB_shv
grace	disssiows*kor,s4_clie= SVC_N,o s4_cliin
			cin
		ignedtatus;
locks*k(&grace	C_Nunf);m	 *ather&->cliin
		i;s Cach Ratus;sl);
	tnfs-azerocat| Pntan b:ic void
nfsfs_msfsateid,net, 		|| (aft| (fsateid, boa, fsateid, bobignedtatus;
(s32)(a;
}i,net, 		||  - b;
}i,net, 		|| ) >uct c struct nfsd4_ca_sec(s_sateid,net, 		|| (fsateid, boin, fsateid, boref,nfs_maentid_hashturn ce do nott on *__be3shorteisache sidtateid net, 		||  numbe *it igner *se RPwt oni wissly l'd (likely(sentid_hashtnf);in;
}i,net, 		||  ==i0rrn fs_ok;
	if (seqkely(sin;
}i,net, 		||  ==iref;
}i,net, 		|| )rn fs_ok;
	if (seqke/noIs "ps
clp = ss)
d!bu naldtateid es(r nt_lfu_ok
, iu's buggy:(likely(ss ateid,net, 		|| (aft| (in, ref)rn false;
	ot_samebad_c    to ode do noHowe
stnewe cbablyseenaldtateid es(r nt_lpaac re
sn es(r ase RPnon-buggykz/
	clideFt texamp/
, is "ps
clp = ss)
d!ba
lockse RPwtilesfo	/*IOwissourdtan ur ,e fla

st;m	y bump }i,net, 		|| se RPwtiles flaIOwissd lessi;
f_igheideTps
clp = scbablya_calsnoouse RPsitu		||  bydwae(numbfts:
	sp| !e!backcha  flaIOwetquests-se RPbucdbett| P		gformtrcetm	y spaultPinrsp &tnumbIOwnoouse RPdr;eive	 chooldedtateid But i is etquestshorterortlyse RPdrorderunti;
f_igheif (ca->lem;
			memcporld_c    to oc struct nfsd4_cad *d4	_sec(srp)
		|| _d_client_r,s4_client *rledtateid *olsigned
		erl,;
}
est   		|| nt)osis_rs)
(		||  &&rp uct)		s)
		|| rrl,;
}
est   		|| )ntoosnew->c&FC4_FOOOED_R;
			g)o
 oalse;
	ot_samebad_c    to odfs_ok;
}

static __be32 nfsd4_cad *d4	rb_toid-(&    tosvc_rqstent *old = NUs-,istateid, bo_tateid)ct nfs4_client *sti;

e2 status = 0;
	s	= 	memcpobad_c    to oed
		eZERO_TTATEIDr,tateid)n|| ONE_TTATEIDr,tateid)rn status;
	status = belld = Ndebuggnumbaii.ourn e_creds(&un nn)&,tateid;
}i,opaquee)os=		d, fclorver.cl_bootuuturn emaskadd o!tr[INET6_ADDRSTRLEN]tiuttpc_ntop((fs4_sti 
stadd i*)fclorveradd ,kadd o!tr,o outiddr_stradd o!trfo;_s	pr_warn_ratelimrqt_rove_to_clp = s%s ter  sizetate ID "o outt"o, oninco westiclp = sIDunc_ add o!trf;n status;
	status =}o	d(&nn->clieclorverif (new)ocrefirmestateid,ld);
		s-,istateidide	
		e!urn s;
	}
	ne*clp->cew) 0;
	s	= _sec(s_sateid,net, 		|| (fsateid, fd;
};
fsateid, 1kturd == 	return s;
	}
	ne*clp->cew) bs->flad;
};
typertatus		expirFODELEG_STID:=   nfserr_scha (seqitdefault:s		expirFOREVOKEDODELEG_STID:=   nfserr_schamcpoons)
*tavokedeqitdefault:s		expirFOOPEN_STID:t:s		expirFOLOCK_STID:=   nfserr_schad4	_sec(srp)
		|| _d_client_rrp)
locks    tosvfo;_s	default:
		/*
		 * NFSD: mounk
		nPstateid types%xunc_ d;
};
typer;rn lotFchathroughurn !l		expirFOeLOSED_STID:t:s		expirFOeLOSED_DELEG_STID:=   nfserr_schamcpobad_c    to od}o	ne*clp->cunlock(&nn->clieclorverif (new)tatus;
}

stati2
n_be32 nfsd4_c
it_conlookupc&    tosvc_rqstentundargs *arcstate,
		     
ctt nn fsateid, bo_sfseidsi int idhaemasktypemasl 
ctt nn fs4_client *sti;

*s nfsd_net *nn)
{
	u32 maxrtatus = 0;
	scr_ses->ZERO_TTATEIDr,tateid)n|| ONE_TTATEIDr,tateid)rn status;
chamcpobad_c    to od nfserr_blookupc;
		gotos&,tateid;
}i,opaquee)os=		d, 		     n (status)
		retur=_replsameientid;
		gotoold = nf)
uinorver
	__be3f
n nfserr_jukebox;bad_c    to od>alse;
	ot_sameientidc    to od}oid == 	return status;
	status =*ocrefirmestateid,by_type(sinorvers->,istateid,ktypemaslide	
		e!*so
 oalse;
	ot_samebad_c    to odfs_ok;
}

static __be32 n,s4_clifilest
mpo *fincbfile(fs4_client *sti;

esiints	renewn{-> bs->flad;
};
typertatus		expirFODELEG_STID:=  
		eONCE(conf && !s;
};
filed)fi	ons)
*filetf
n nfserr_j		goto  fserr_jion_file(s;
};
filed)fi	ons)
*filetlt:s		expirFOOPEN_STID:t:s		expirFOLOCK_STID:=  nf)
new->c&FRDITTATE)
n nfserr_jsirmendleusic*file(s;
};
file)tiu lcr_i =nfserr_jsirmewriteusic*file(s;
};
file)tiu default:}tt	alse;
	
static struct nfsd4_cxcha4	_sec(srl,    tosvc_rqst *rqfhstfh  nfsd4_compo *rledtateid *olssiints	renewn{->tatus = 0;
	scr_s nfserr_schad4	_sec(srp)
		|| _d_client_rrlewtatus)
		return status;
	status =fsse;
	ot_s;_sec(srp)
moderolssi	renewtac sche thesec(t f	 *dtateid o(resp, ostic vfsd4_cxcha4	petuorc	__*stateid,op(,s4_clie= SVC_N,o s4_climpoundargs *arcstate,
		     
ctp ucfsateid, bo_sfseidsiints	rene,n,s4_clifilesttfi-> ignedfsd4_co *rqfhstfh e->&sinorvers, we o_fhde	fsd4_coin
			cin
crefhpd)fh_dl, &t->d_in
		;  n,s4_net *nn = net_generic(net, nfsf (>t_id);

	if (; nfs4_client *sti;

e2 status = 0;
	sdep_)
		fi-> ig		tfi-> 
				gotop_)
		grace	disssiows*korf (>tin
)rn status;
chamcpograce;w_ses->ZERO_TTATEIDr,tateid)n|| ONE_TTATEIDr,tateid)rn status;
_sec(s_pecialedtateidsrf (>tfh  nfsfseidsi	renewta_s nfserr_schad4	lookupc&    tos		     nfsfseidso outC4_FODELEG_STID|pirFOOPEN_STID|pirFOLOCK_STIDso out&s n (status)
		return status;
	status = 0;
	s	= _sec(s_sateid,net, 		|| (fsateid, fd;
};
fsateid,
	usit_conentid_hashtnmit		ifoturd == 	return s;
	}
	ne= nlobs->flad;
};
typertatus		expirFODELEG_STID:=   nfserr_scha4	_sec(sons)
moderons)
s    tosvf,s	renewtattdefault:s		expirFOOPEN_STID:t:s		expirFOLOCK_STID:=   nfserr_scha4	_sec(srl,    tosfh  nrp)
locks    tosvf,s	renewtattdefault:
		/*
		 *  nfserr_schamcpobad_c    to od default:}ttus)
		return s_free_co el nfserr_scha4	_sec(sfhsfh  ns)dep_)
		! nfserr&& fi-> ild = tfi-> 
		mpo *fincbfile(f,s	renewtatt
		e!*fi-> ig		  nfserr_schamcpostt;
		/*
	 od}o	ne: Tit_ *ion(n tossnew)tatus;
}

stati2
nche thTer ris "ps
dtateid is rb_totic vfsd4_cxcha_cbse}
est   tosvc_rqst *rqstp,
		     nfsd4_compound_state *cstate,
		     
c	t nfsd4_compoundse}
est   to *se}
est   tourn c,s4_clienton se}
est   to_id *s    to od nd4_compo *old = NUs-->suinorver
	__be3er
	s=		nfs;ew)_each_entry(clp, &tbfsateid, fse}
est   to->t_*stateid,_eacsits*kd,_eac) *  nfs to->t_*i*cstaterr_
	usit_conrb_toid-(&    toss-,i& nfs to->t_*i*cstatei;)itt	alse;
	n

static _fsd4_c
it_consion(st   tosvc_rqst *rqstp,
		     nfsd4_compound_state *cstate,
		     
c	t nfsd4_compoundsion(st   tostfion(st   tourn c,sateid, bo_sfseide->&fion(st   tod)fr_s    to od nd4_compo *sti;

e2 sfsd4_compo *ons)
		||  *d>denfsd4_sessi *rledtateid *s  denfsd4_sempo *old = NUs-->suinorver
	__be3er
	s=		nfs;estatus =als	= 	memcpobad_c    to oedd(&nn->clieclorverif (new)ocrefirmestateid,ld);
		s-,istateidide	
		e!urn s;
	}
	ne*clp->cew) bs->flad;
};
typertatus		expirFODELEG_STID:=  als	= 	memcpolocks*hel tattdefault:s		expirFOOPEN_STID:t: als	= _sec(s_sateid,net, 		|| (fsateid, fd;
};
fsateid, 1ktur	
		e	frese tdefault:sals	= 	memcpolocks*hel tattdefault:s		expirFOLOCK_STID:=  als	= _sec(s_sateid,net, 		|| (fsateid, fd;
};
fsateid, 1ktur	
		e	frese tdefault:s np->s	s)
locks    tosvflt:sals	= 	memcpolocks*hel tattnf)
usec(sfopolocks(dts;
}
esti;.);
file,
outt tsa

st		|| r,ts;
}
est   		|| i)ese tdefault:slient_l

st(&    tosvc f =  ock(&nn->clieclorverif (new)Tit_ *ion(n tossnew)sals	= 	me (seqit_free_co els		expirFOREVOKEDODELEG_STID:=  d>se_dns)
s    tosvflt:seq(&consen(rq(&
ed)dlez + \
	lru)tiutock(&nn->clieclorverif (new)Tit_ *ion(n tossnew)sals	= 	me (seqit_free_co el5, D		/*
	nretls throughuespondtus;sl	memcpobad_c    tourn !}o	ne*clp->cunlock(&nn->clieclorverif (newin_unltatus;
	se;ic void
genshl_CB_int
 *tckflg(sintktypeignedtatus;
(types				4_FORcroW_LTn|| types				4_FORcro_LT) ?qitRDITTATE :FWRITTATEcoc struct nfsd4_escha4	cotID_sl_;sec(t>_sd4_compound_state *cstate,
		     nfsateid, bo_sfseidsi seqiretu, vc_rqstent *rledtateid *s  ignedfsd4_co *rqfhsts, we o_fhe->&sinorvers, we o_fhde	fsd4_cossi *
    		||  *so>se_dts;
}
est   		|| 2 status = 0;
	sdep_ nfserr_schad4	_sec(s_ses->		     nso>, cotIDstatus)
		return status;
	status =ly(ss  ;
}
esti;.);
types				4_FOeLOSED_STIDn s|| s  ;
}
esti;.);
types				4_FOREVOKEDODELEG_STID)
stGSS c th"Cloang"sdtateid'skexis- *on_y*t owdrL;
}tue RPnt_sameache) { c es(r nt_lprevioussd ep,		 *see RPdrvoked:dns)
		|| !bortekeptsonly
 allfion(st   to.
ce R/tusalse;
	ot_samebad_c    to odd		|(write(&sss;
}
erwsesf =  0;
	s	= _sec(s_sateid,net, 		|| (fsateid, fdts;
}
esti;.);
fsfseidsiit_conentid_hashtnmit		ifoturd == 	retur=_replstat=   nfserr_scha4	_sec(sfhss, we o_fh, &dts;
}
esti;)it	d == 	retur!_replstatn supcwrite(&sss;
}
erwsesf = tatus;
}

stati2
nch e thesec(t f	 *dtquercetid muructng o(resp, os. tic void
nfsfsd4_cxcha4	petuorc	__*sotID_sl>_sd4_compound_state *cstate,
		     n seqiretu,
ctp fsateid, bo_sfseidsiemasktypemasl 
ct	 vc_rqstent *rledtateid **s  p 
ct	 vc_rqstentn)
{
	u32 maxrtatus = 0;
	scrd nd4_compo *sti;

e2 sfsd4_compo *rledtateid *s  _getruct _u"NFSD: move_to_%s: fretu=%d
dtateid = " TTATEID_FMT "unc_ __ cor___outiretu, TTATEID_VALi,tateid)rt _u*s  p_getruct nfsfserr_schad4	lookupc&    tos		     nfsfseidsktypemasl  &s n (status)
		return status;
	status = 0p->s	s)
locks    tosvflt:nnnel_adtate	eldt istruct n		     nfts;
}
est   		|| ideel nfserr_scha4	cotID_sl_;sec(t>		     nfsfseidskiretu, vceturn)
		g		return s*s  p_ges  denlcr_i =ssi *ion(n tos&dts;
}
esti;)it	tatus;
}

stati2
n_be32 nfsd4_c cha4	petuorc	__*d_client_bsotID_sl>_sd4_compound_state *cstate,
		     n seqiretu,
ctpctp fsateid, bo_sfseidsivc_rqstent *rledtateid **s  p  vc_rqstentn)
{
	u32 maxrtatus = 0;
	scrd nd4_compo *rs)
		|| s*	o; cfsd4_compo *rledtateid *s  deel nfserr_scha4	petuorc	__*sotID_sl>		     nfretu, vcfseidso oututC4_FOOPEN_STID, &dts n (status)
		return status;
	status =	o->s	s)
		|| r,ts;
}
est   		|| ide	is)
p(Lontoosnew->c&FC4_FOOOED_R;
			g)ol creupcwrite(&sss;
}
erwsesf = nssi *ion(n tos&dts;
}
esti;)it	 alse;
	ot_samebad_c    to od}
s*s  p_ges  denalse;
	n

static _fsd4_c
it_conrs)
(d_cliensvc_rqst *rqstp,
		     nfsd4_compound_state *cstate,
		     
c	t nfsd4_compoundrs)
(d_cliens*	c maxrtatus = 0;
	scrd nd4_compo *rs)
		|| s*	o; cfsd4_compo *rledtateid *s  denfsd_net *nn = net_generic(net, nfsrqstp), nfsd_net_id);

	if (cr_s"NFSD: move_to_mpoundrs)
(d_cliensthe iles%pdunc_outusinorvers, we o_fh.fh_dl, &tideel nfserr_sfh_v, nfy(onn, ne&sinorvers, we o_fh, S_IFREG, 0status)
		return status;
	status el nfserr_scha4	petuorc	__*sotID_sl>		     o outuocntoc_fretu, &ocntoc_req_vcfseidso outuC4_FOOPEN_STID, &dts n (status)
		return s_free_co el	o->s	s)
		|| r,ts;
}
est   		|| ide	fsfserr_schamcpobad_c    to od
		erontoosnew->c&FC4_FOOOED_R;
			g)l creupcwrite(&sss;
}
erwsesf = n_freeion(n a  to od}
srontoosnew->c|DFC4_FOOOED_R;
			g;sl exid-(&    tosedts;
}
esti;.);
fsfseidrew)r_ses->&rcntoc_resl_fsfseid, fdts;
}
esti;.);
fsfseidsiddr_str,tateid, ))crcup(write(&sss;
}
erwsesf = "NFSD: move_to_%s: fucc	__, vretu=%d
dtateid=" TTATEID_FMT "unc_out__ cor___ ocntoc_fretu, TTATEID_VALiedts;
}
esti;.);
fsfseidrnitt:nnnel_ack);
	recor*c;ssionsoontoos		|| e)os=		nfsturnfsfserr_schastatiion(n a  to: Tit_ *ion(n tos&dts;
}
esti;)it	ne: Tit_el_bumps_ses->		     nstretur; iULL;
}
}

stati2
n_be32 ninl_CB__calsmpo4s_sateid,d		|grade_bit	fsd_net *n *rledtateid *s   n seqacc	__urn ci
		gter racc	__(acc	__, vcpnrn false;
;
=ssi *file	ion(acc	__(dts;
}
esti;.);
file, acc	__u;l__c_mrracc	__(acc	__, vcpnti2
n_be32 ninl_CB__calsmpo4s_sateid,d		|grade	fsd_net *n *rledtateid *s   n seqtosacc	__wn{-> bs->flatosacc	__wtatus		expirFOSHARE_ACCESSCREAD:= nssi *_sateid,d		|grade_bit	fs nez = SSHARE_ACCESSCWRITEr;= nssi *_sateid,d		|grade_bit	fs nez = SSHARE_ACCESSCBOTH)deitdefault:s		expirFOSHARE_ACCESSCWRITE:= nssi *_sateid,d		|grade_bit	fs nez = SSHARE_ACCESSCREADuo_unssi *_sateid,d		|grade_bit	fs nez = SSHARE_ACCESSCBOTH)deitdefault:s		expirFOSHARE_ACCESSCBOTH:attdefault:
		/*
		 * ONCE(conf && 1new)}ic _fsd4_c
it_conrs)
(d		|grade	fsd_net *rqstp,
		     
ctt nn fs4_clientundargs *arcstate,
		     
ctt nn fsd4_compoundrs)
(d		|gradek*od maxrtatus = 0;
	scrd nd4_compo *rledtateid *s  denfsd_net *nn = net_generic(net, nfsrqstp), nfsd_net_id);

	if (cr_s"NFSD: move_to_mpoundrs)
(d		|gradekthe iles%pdunc_ outusinorvers, we o_fh.fh_dl, &tideellott supe riyetPsupport WARM
bsts:(likely(sodntod_ons)
_wanrr
us"NFSD: move_to_%s: od_ons)
_wanr=0x%x igner *unc_ __ cor___out	odntod_ons)
_wanrrs el nfserr_scha4	petuorc	__*d_client_bsotID_sl>		     nodntod_iretu,
ctpct&odntod_isfseid, fdts n (status)
		return s_free_co  e	fsfserr_schamcpostatus f)
		gter racc	__(	dntod_ihorteacc	__, vcpnrtatus
eFSD: move_to_acc	__cnoreuPsubsresofPa, we osbstmap: 0x%hhx>tinpureacc	__=%08xunc_outudts;
}
eacc	___bmap nodntod_ihorteacc	__u;l_n_freeion(n a  to od}
s)
		gter ronntbodntod_ihorteonnt, vcpnrtatus
eFSD: move_to_onntcnoreuPsubsresofPa, we osbstmap: 0x%hhx>tinpureonnt=%08xunc_outudts;
}
edruy_bmap, odntod_ihorteonntu;l_n_freeion(n a  to od}
smpo4s_sateid,d		|grade	fsp nodntod_ihorteacc	__u;ll_spact&cl;
	rbmapsonntbodntod_ihorteonnt, vcpn;ll_ exid-(&    tosedts;
}
esti;.);
fsfseidrew)r_ses->&rdntod_isfseid, fdts;
}
esti;.);
fsfseidsiddr_str,tateid, ))crcfsfserr_schastatiion(n a  to: Tup(write(&sss;
}
erwsesf = it_ *ion(n tos&dts;
}
esti;)it	ne: Tit_el_bumps_ses->		     nstretur; iULL;
}
}

stati2
n_be32 n_calsmpound_coem:rs)
(&    tosvcd4_compo *rledtateid *s maxrfs4_client *clp = ses->_ges;
}
esti;.);
			nfsdecLISTmacro(spap_eacf =xrf;
}
esti;.);
types			4_FOeLOSED_STID;edd(&nn->clieclporverif (new)lient_lrs)
(&    tosvonfspap_eacf =
>)
		vb_sessision = cstatrtatusion(rledtateid,ld);
		vonfspap_eacf =utock(&nn->clieclporverif (new)	fion(rledtateid,spap_eac(&spap_eacf = f 	status = ck(&nn->clieclporverif (new)	fion(rledtateid,spap_eac(&spap_eacf = 	movesce(_loan_lru	vonvb_seC_Nu;_be32
nche thent *nn->cledtate() + \
ediaft| Pencodetic vfsd4_cxcha_cb_loansvc_rqst *rqstp,
		     nfsd4_compound_state *cstate,
		     
cct nfsd4_compound_loanses-oan maxrtatus = 0;
	scrd nd4_compo *rledtateid *s  denfsd_net = SVC_Ns		rqstp), nfsd_ndenfsd_net *nn = net_generic(net, nfsf (>t_id);

	if (; _s"NFSD: move_to_mpound_loansthe iles%pdunc_ outusinorvers, we o_fh.fh_dl, &tideel nfserr_scha4	petuorc	__*sotID_sl>		     n_loanorveriretu,
ctpct&_loanorvericfseidso outuC4_FOOPEN_STID|	4_FOeLOSED_STID,
ctpct&dts n (statit_el_bumps_ses->		     nstretur; ius)
		return s_free_co  e	 exid-(&    tosedts;
}
esti;.);
fsfseidrew)r_ses->&_loanorvericfseids fdts;
}
esti;.);
fsfseidsiddr_str,tateid, ))crcup(write(&sss;
}
erwsesf =t:nnnel_acoem:rs)
(&    tosvcpnewss bep a z ferercetes(r cha4	petuorc	__*sotID_sl R/tussi *ion(n tos&dts;
}
esti;)it	ne: TULL;
}
}

stati2
nfsd4_cxcha_cbons)
ULL;
}svc_rqst *rqstp,
		     nfsd4_compound_state *cstate,
		     
c	t fsd4_compoundons)
ULL;
} *drignedfsd4_compo *ons)
		||  *d>; cfsateid, bo_sfseide->&dr->dr_s    to od nd4_compo *sti;

e2 status = 0;
	scrnfsd_net *nn = net_generic(net, nfsrqstp), nfsd_net_id);

	if (cr_s)
		( nfserr_sfh_v, nfy(onn, ne&sinorvers, we o_fh, S_IFREG, 0s)rn status;
	status  nfsfserr_schad4	lookupc&    tos		     nfsfseidskpirFODELEG_STID  &s n (status)
		return s_free_co eld>se_dns)
s    tosvflt:fsfserr_s_sec(s_sateid,net, 		|| (fsateid, fded)dlesti;.);
fsfseid, it_conentid_hashtnmit		ifoturd == 	return s;
	}
ion(n a  to ot:
	fsdoy_ons)
		|| (deturion(n a  to: Tit_ *ion(n tos&dpd)dlesti;)ti	ne: TULL;
}
}

stati2
n
#
		_CB_LOFF_OVERFLOW= 	rr(>tletrtct n ((u64)(letrt> ~(u64)( 	rr()rnn_be32 ninl_CB_u64
e *coff&ct(u64  	rr(>tu64 l	 urn cu64 e * ot:earse_ 	rr( + l	 ; iULL;
}
ears>e_ 	rr( ?
ear:kpirFOMAX_UINT64ti2
nch last octetPinra rangeic void
nfsinl_CB_u64
lastdbyd-(off&ct(u64  	rr(>tu64 l	 urn cu64 e * ot:ONCE(conf && !l	 ueqrearse_ 	rr( + l	 ; iULL;
}
ears>_ 	rr( ?
ear - 1:kpirFOMAX_UINT64ti2
nche thTODO: Linuxe ilesoff&ct!borte_nt idh_ 64-bst quanrntit! rwhich mea;sl)oous RPwebe n riprn(rely 		 */
a

st;etquestshnoousgo beyonche si(2^63 - 1)-ths RPbyd-,ybeca nocofPnt i extenashtnprnblems.  Si)
{
	4_v4Paetls f	 *64-bsts RPasseir ,bviillprev = sherres(r benumb_staletely prntocol-_stalianeideTps
 RPdral solu	||  	}
viillprnblem is  ows	rr( is in  int idha ilesoff&ct!bin
 RPnoe VFS,Pbucdviill
	 c 
sty_dneplottrge!tic void
nfsinl_CB_d4_sessi *transforml

st(off&ct(,s4_clifile(essistif (nrn ci
		if (&= ~{s	rr( <i0rrn if (&= ~{s	rr( e_OFFSET_MAXto>i
		if (&= ~{ear <i0rrn if (&= ~{earse_OFFSET_MAXto2
n_be32 n_calsmpound ~{ion_		|| r,t4_clifile(essistdac rfs4_clifile(essistsrcignedfsd4_compo *

st		|| stifse_(fsd4_compo *

st		|| st)src&= ~{		||  eldst&= ~{		|| 	= ( ~{		|| _t)

st		|| rnha4	gsslst   		|| (&lo->los		|| ))to2
n_be32 n_calsmpound ~{ion(r	|| r,t4_clifile(essistflesnedfsd4_compo *

st		|| stifse_(fsd4_compo *

st		|| st)fl&= ~{		|| cr_s)
		lortatusit_ *ion(n    		|| (&lo->los		|| )ew)	fl&= ~{		|| 	= truct n}o2
n_be32 nd_cstrfs4_cli

st(manag| _o(resp, ost_id);posix:mng	n_t 	= atu.lm{ion_		||  =smpounds~{ion_		|| ,tu.lm{ion(r	||  =smpounds~{ion(r	|| ,
};
n_be32 ninl_C