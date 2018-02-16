/*
 * Simple NUMA memory policy for the Linux kernel.
 *
 * Copyright 2003,2004 Andi Kleen, SuSE Labs.
 * (C) Copyright 2005 Christoph Lameter, Silicon Graphics, Inc.
 * Subject to the GNU Public License, version 2.
 *
 * NUMA policy allows the user to give hints in which node(s) memory should
 * be allocated.
 *
 * Support four policies per VMA and per process:
 *
 * The VMA policy has priority over the process policy for a page fault.
 *
 * interleave     Allocate memory interleaved over a set of nodes,
 *                with normal fallback if it fails.
 *                For VMA based allocations this interleaves based on the
 *                offset into the backing object or offset into the mapping
 *                for anonymous memory. For process policy an process counter
 *                is used.
 *
 * bind           Only allocate memory on a specific set of nodes,
 *                no fallback.
 *                FIXME: memory is allocated starting with the first node
 *                to the last. It would be better if bind would truly restrict
 *                the allocation to memory nodes instead
 *
 * preferred       Try a specific node first before normal fallback.
 *                As a special case NUMA_NO_NODE here means do the allocation
 *                on the local CPU. This is normally identical to default,
 *                but useful to set in a VMA when you have a non default
 *                process policy.
 *
 * default        Allocate on the local node first, or when on a VMA
 *                use the process policy. This is what Linux always did
 *		  in a NUMA aware kernel and still does by, ahem, default.
 *
 * The process policy is applied for most non interrupt memory allocations
 * in that process' context. Interrupts ignore the policies and always
 * try to allocate on the local CPU. The VMA policy is only applied for memory
 * allocations for a VMA in the VM.
 *
 * Currently there are a few corner cases in swapping where the policy
 * is not applied, but the majority should be handled. When process policy
 * is used it is not remembered over swap outs/swap ins.
 *
 * Only the highest zone in the zone hierarchy gets policied. Allocations
 * requesting a lower zone just use default policy. This implies that
 * on systems with highmem kernel lowmem allocation don't get policied.
 * Same with GFP_DMA allocations.
 *
 * For shmfs/tmpfs/hugetlbfs shared memory the policy is shared between
 * all users and remembered even when nobody has memory mapped.
 */

/* Notebook:
   fix mmap readahead to honour policy and enable policy for any page cache
   object
   statistics for bigpages
   global policy for page cache? currently it uses process policy. Requires
   first item above.
   handle mremap for shared memory (currently ignored for the policy)
   grows down?
   make bind policy root only? It can trigger oom much faster and the
   kernel is not always grateful with that.
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mempolicy.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/nodemask.h>
#include <linux/cpuset.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/export.h>
#include <linux/nsproxy.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/compat.h>
#include <linux/swap.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/migrate.h>
#include <linux/ksm.h>
#include <linux/rmap.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/ctype.h>
#include <linux/mm_inline.h>
#include <linux/mmu_notifier.h>
#include <linux/printk.h>

#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include <linux/random.h>

#include "internal.h"

/* Internal flags */
#define MPOL_MF_DISCONTIG_OK (MPOL_MF_INTERNAL << 0)	/* Skip checks for continuous vmas */
#define MPOL_MF_INVERT (MPOL_MF_INTERNAL << 1)		/* Invert check for nodemask */

static struct kmem_cache *policy_cache;
static struct kmem_cache *sn_cache;

/* Highest zone. An specific allocation for a zone below that is not
   policied. */
enum zone_type policy_zone = 0;

/*
 * run-time system-wide default policy => local allocation
 */
static struct mempolicy default_policy = {
	.refcnt = ATOMIC_INIT(1), /* never free it */
	.mode = MPOL_PREFERRED,
	.flags = MPOL_F_LOCAL,
};

static struct mempolicy preferred_node_policy[MAX_NUMNODES];

struct mempolicy *get_task_policy(struct task_struct *p)
{
	struct mempolicy *pol = p->mempolicy;
	int node;

	if (pol)
		return pol;

	node = numa_node_id();
	if (node != NUMA_NO_NODE) {
		pol = &preferred_node_policy[node];
		/* preferred_node_policy is not initialised early in boot */
		if (pol->mode)
			return pol;
	}

	return &default_policy;
}

static const struct mempolicy_operations {
	int (*create)(struct mempolicy *pol, const nodemask_t *nodes);
	/*
	 * If read-side task has no lock to protect task->mempolicy, write-side
	 * task will rebind the task->mempolicy by two step. The first step is
	 * setting all the newly nodes, and the second step is cleaning all the
	 * disallowed nodes. In this way, we can avoid finding no node to alloc
	 * page.
	 * If we have a lock to protect task->mempolicy in read-side, we do
	 * rebind directly.
	 *
	 * step:
	 * 	MPOL_REBIND_ONCE - do rebind work at once
	 * 	MPOL_REBIND_STEP1 - set all the newly nodes
	 * 	MPOL_REBIND_STEP2 - clean all the disallowed nodes
	 */
	void (*rebind)(struct mempolicy *pol, const nodemask_t *nodes,
			enum mpol_rebind_step step);
} mpol_ops[MPOL_MAX];

/* Check that the nodemask contains at least one populated zone */
static int is_valid_nodemask(const nodemask_t *nodemask)
{
	return nodes_intersects(*nodemask, node_states[N_MEMORY]);
}

static inline int mpol_store_user_nodemask(const struct mempolicy *pol)
{
	return pol->flags & MPOL_MODE_FLAGS;
}

static void mpol_relative_nodemask(nodemask_t *ret, const nodemask_t *orig,
				   const nodemask_t *rel)
{
	nodemask_t tmp;
	nodes_fold(tmp, *orig, nodes_weight(*rel));
	nodes_onto(*ret, tmp, *rel);
}

static int mpol_new_interleave(struct mempolicy *pol, const nodemask_t *nodes)
{
	if (nodes_empty(*nodes))
		return -EINVAL;
	pol->v.nodes = *nodes;
	return 0;
}

static int mpol_new_preferred(struct mempolicy *pol, const nodemask_t *nodes)
{
	if (!nodes)
		pol->flags |= MPOL_F_LOCAL;	/* local allocation */
	else if (nodes_empty(*nodes))
		return -EINVAL;			/*  no allowed nodes */
	else
		pol->v.preferred_node = first_node(*nodes);
	return 0;
}

static int mpol_new_bind(struct mempolicy *pol, const nodemask_t *nodes)
{
	if (!is_valid_nodemask(nodes))
		return -EINVAL;
	pol->v.nodes = *nodes;
	return 0;
}

/*
 * mpol_set_nodemask is called after mpol_new() to set up the nodemask, if
 * any, for the new policy.  mpol_new() has already validated the nodes
 * parameter with respect to the policy mode and flags.  But, we need to
 * handle an empty nodemask with MPOL_PREFERRED here.
 *
 * Must be called holding task's alloc_lock to protect task's mems_allowed
 * and mempolicy.  May also be called holding the mmap_semaphore for write.
 */
static int mpol_set_nodemask(struct mempolicy *pol,
		     const nodemask_t *nodes, struct nodemask_scratch *nsc)
{
	int ret;

	/* if mode is MPOL_DEFAULT, pol is NULL. This is right. */
	if (pol == NULL)
		return 0;
	/* Check N_MEMORY */
	nodes_and(nsc->mask1,
		  cpuset_current_mems_allowed, node_states[N_MEMORY]);

	VM_BUG_ON(!nodes);
	if (pol->mode == MPOL_PREFERRED && nodes_empty(*nodes))
		nodes = NULL;	/* explicit local allocation */
	else {
		if (pol->flags & MPOL_F_RELATIVE_NODES)
			mpol_relative_nodemask(&nsc->mask2, nodes,&nsc->mask1);
		else
			nodes_and(nsc->mask2, *nodes, nsc->mask1);

		if (mpol_store_user_nodemask(pol))
			pol->w.user_nodemask = *nodes;
		else
			pol->w.cpuset_mems_allowed =
						cpuset_current_mems_allowed;
	}

	if (nodes)
		ret = mpol_ops[pol->mode].create(pol, &nsc->mask2);
	else
		ret = mpol_ops[pol->mode].create(pol, NULL);
	return ret;
}

/*
 * This function just creates a new policy, does some check and simple
 * initialization. You must invoke mpol_set_nodemask() to set nodes.
 */
static struct mempolicy *mpol_new(unsigned short mode, unsigned short flags,
				  nodemask_t *nodes)
{
	struct mempolicy *policy;

	pr_debug("setting mode %d flags %d nodes[0] %lx\n",
		 mode, flags, nodes ? nodes_addr(*nodes)[0] : NUMA_NO_NODE);

	if (mode == MPOL_DEFAULT) {
		if (nodes && !nodes_empty(*nodes))
			return ERR_PTR(-EINVAL);
		return NULL;
	}
	VM_BUG_ON(!nodes);

	/*
	 * MPOL_PREFERRED cannot be used with MPOL_F_STATIC_NODES or
	 * MPOL_F_RELATIVE_NODES if the nodemask is empty (local allocation).
	 * All other modes require a valid pointer to a non-empty nodemask.
	 */
	if (mode == MPOL_PREFERRED) {
		if (nodes_empty(*nodes)) {
			if (((flags & MPOL_F_STATIC_NODES) ||
			     (flags & MPOL_F_RELATIVE_NODES)))
				return ERR_PTR(-EINVAL);
		}
	} else if (mode == MPOL_LOCAL) {
		if (!nodes_empty(*nodes))
			return ERR_PTR(-EINVAL);
		mode = MPOL_PREFERRED;
	} else if (nodes_empty(*nodes))
		return ERR_PTR(-EINVAL);
	policy = kmem_cache_alloc(policy_cache, GFP_KERNEL);
	if (!policy)
		return ERR_PTR(-ENOMEM);
	atomic_set(&policy->refcnt, 1);
	policy->mode = mode;
	policy->flags = flags;

	return policy;
}

/* Slow path of a mpol destructor. */
void __mpol_put(struct mempolicy *p)
{
	if (!atomic_dec_and_test(&p->refcnt))
		return;
	kmem_cache_free(policy_cache, p);
}

static void mpol_rebind_default(struct mempolicy *pol, const nodemask_t *nodes,
				enum mpol_rebind_step step)
{
}

/*
 * step:
 * 	MPOL_REBIND_ONCE  - do rebind work at once
 * 	MPOL_REBIND_STEP1 - set all the newly nodes
 * 	MPOL_REBIND_STEP2 - clean all the disallowed nodes
 */
static void mpol_rebind_nodemask(struct mempolicy *pol, const nodemask_t *nodes,
				 enum mpol_rebind_step step)
{
	nodemask_t tmp;

	if (pol->flags & MPOL_F_STATIC_NODES)
		nodes_and(tmp, pol->w.user_nodemask, *nodes);
	else if (pol->flags & MPOL_F_RELATIVE_NODES)
		mpol_relative_nodemask(&tmp, &pol->w.user_nodemask, nodes);
	else {
		/*
		 * if step == 1, we use ->w.cpuset_mems_allowed to cache the
		 * result
		 */
		if (step == MPOL_REBIND_ONCE || step == MPOL_REBIND_STEP1) {
			nodes_remap(tmp, pol->v.nodes,
					pol->w.cpuset_mems_allowed, *nodes);
			pol->w.cpuset_mems_allowed = step ? tmp : *nodes;
		} else if (step == MPOL_REBIND_STEP2) {
			tmp = pol->w.cpuset_mems_allowed;
			pol->w.cpuset_mems_allowed = *nodes;
		} else
			BUG();
	}

	if (nodes_empty(tmp))
		tmp = *nodes;

	if (step == MPOL_REBIND_STEP1)
		nodes_or(pol->v.nodes, pol->v.nodes, tmp);
	else if (step == MPOL_REBIND_ONCE || step == MPOL_REBIND_STEP2)
		pol->v.nodes = tmp;
	else
		BUG();

	if (!node_isset(current->il_next, tmp)) {
		current->il_next = next_node(current->il_next, tmp);
		if (current->il_next >= MAX_NUMNODES)
			current->il_next = first_node(tmp);
		if (current->il_next >= MAX_NUMNODES)
			current->il_next = numa_node_id();
	}
}

static void mpol_rebind_preferred(struct mempolicy *pol,
				  const nodemask_t *nodes,
				  enum mpol_rebind_step step)
{
	nodemask_t tmp;

	if (pol->flags & MPOL_F_STATIC_NODES) {
		int node = first_node(pol->w.user_nodemask);

		if (node_isset(node, *nodes)) {
			pol->v.preferred_node = node;
			pol->flags &= ~MPOL_F_LOCAL;
		} else
			pol->flags |= MPOL_F_LOCAL;
	} else if (pol->flags & MPOL_F_RELATIVE_NODES) {
		mpol_relative_nodemask(&tmp, &pol->w.user_nodemask, nodes);
		pol->v.preferred_node = first_node(tmp);
	} else if (!(pol->flags & MPOL_F_LOCAL)) {
		pol->v.preferred_node = node_remap(pol->v.preferred_node,
						   pol->w.cpuset_mems_allowed,
						   *nodes);
		pol->w.cpuset_mems_allowed = *nodes;
	}
}

/*
 * mpol_rebind_policy - Migrate a policy to a different set of nodes
 *
 * If read-side task has no lock to protect task->mempolicy, write-side
 * task will rebind the task->mempolicy by two step. The first step is
 * setting all the newly nodes, and the second step is cleaning all the
 * disallowed nodes. In this way, we can avoid finding no node to alloc
 * page.
 * If we have a lock to protect task->mempolicy in read-side, we do
 * rebind directly.
 *
 * step:
 * 	MPOL_REBIND_ONCE  - do rebind work at once
 * 	MPOL_REBIND_STEP1 - set all the newly nodes
 * 	MPOL_REBIND_STEP2 - clean all the disallowed nodes
 */
static void mpol_rebind_policy(struct mempolicy *pol, const nodemask_t *newmask,
				enum mpol_rebind_step step)
{
	if (!pol)
		return;
	if (!mpol_store_user_nodemask(pol) && step == MPOL_REBIND_ONCE &&
	    nodes_equal(pol->w.cpuset_mems_allowed, *newmask))
		return;

	if (step == MPOL_REBIND_STEP1 && (pol->flags & MPOL_F_REBINDING))
		return;

	if (step == MPOL_REBIND_STEP2 && !(pol->flags & MPOL_F_REBINDING))
		BUG();

	if (step == MPOL_REBIND_STEP1)
		pol->flags |= MPOL_F_REBINDING;
	else if (step == MPOL_REBIND_STEP2)
		pol->flags &= ~MPOL_F_REBINDING;
	else if (step >= MPOL_REBIND_NSTEP)
		BUG();

	mpol_ops[pol->mode].rebind(pol, newmask, step);
}

/*
 * Wrapper for mpol_rebind_policy() that just requires task
 * pointer, and updates task mempolicy.
 *
 * Called with task's alloc_lock held.
 */

void mpol_rebind_task(struct task_struct *tsk, const nodemask_t *new,
			enum mpol_rebind_step step)
{
	mpol_rebind_policy(tsk->mempolicy, new, step);
}

/*
 * Rebind each vma in mm to new nodemask.
 *
 * Call holding a reference to mm.  Takes mm->mmap_sem during call.
 */

void mpol_rebind_mm(struct mm_struct *mm, nodemask_t *new)
{
	struct vm_area_struct *vma;

	down_write(&mm->mmap_sem);
	for (vma = mm->mmap; vma; vma = vma->vm_next)
		mpol_rebind_policy(vma->vm_policy, new, MPOL_REBIND_ONCE);
	up_write(&mm->mmap_sem);
}

static const struct mempolicy_operations mpol_ops[MPOL_MAX] = {
	[MPOL_DEFAULT] = {
		.rebind = mpol_rebind_default,
	},
	[MPOL_INTERLEAVE] = {
		.create = mpol_new_interleave,
		.rebind = mpol_rebind_nodemask,
	},
	[MPOL_PREFERRED] = {
		.create = mpol_new_preferred,
		.rebind = mpol_rebind_preferred,
	},
	[MPOL_BIND] = {
		.create = mpol_new_bind,
		.rebind = mpol_rebind_nodemask,
	},
};

static void migrate_page_add(struct page *page, struct list_head *pagelist,
				unsigned long flags);

/*
 * Scan through pages checking if pages follow certain conditions,
 * and move them to the pagelist if they do.
 */
static int queue_pages_pte_range(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long addr, unsigned long end,
		const nodemask_t *nodes, unsigned long flags,
		void *private)
{
	pte_t *orig_pte;
	pte_t *pte;
	spinlock_t *ptl;

	orig_pte = pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	do {
		struct page *page;
		int nid;

		if (!pte_present(*pte))
			continue;
		page = vm_normal_page(vma, addr, *pte);
		if (!page)
			continue;
		/*
		 * vm_normal_page() filters out zero pages, but there might
		 * still be PageReserved pages to skip, perhaps in a VDSO.
		 */
		if (PageReserved(page))
			continue;
		nid = page_to_nid(page);
		if (node_isset(nid, *nodes) == !!(flags & MPOL_MF_INVERT))
			continue;

		if (flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL))
			migrate_page_add(page, private, flags);
		else
			break;
	} while (pte++, addr += PAGE_SIZE, addr != end);
	pte_unmap_unlock(orig_pte, ptl);
	return addr != end;
}

static void queue_pages_hugetlb_pmd_range(struct vm_area_struct *vma,
		pmd_t *pmd, const nodemask_t *nodes, unsigned long flags,
				    void *private)
{
#ifdef CONFIG_HUGETLB_PAGE
	int nid;
	struct page *page;
	spinlock_t *ptl;
	pte_t entry;

	ptl = huge_pte_lock(hstate_vma(vma), vma->vm_mm, (pte_t *)pmd);
	entry = huge_ptep_get((pte_t *)pmd);
	if (!pte_present(entry))
		goto unlock;
	page = pte_page(entry);
	nid = page_to_nid(page);
	if (node_isset(nid, *nodes) == !!(flags & MPOL_MF_INVERT))
		goto unlock;
	/* With MPOL_MF_MOVE, we migrate only unshared hugepage. */
	if (flags & (MPOL_MF_MOVE_ALL) ||
	    (flags & MPOL_MF_MOVE && page_mapcount(page) == 1))
		isolate_huge_page(page, private);
unlock:
	spin_unlock(ptl);
#else
	BUG();
#endif
}

static inline int queue_pages_pmd_range(struct vm_area_struct *vma, pud_t *pud,
		unsigned long addr, unsigned long end,
		const nodemask_t *nodes, unsigned long flags,
		void *private)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (!pmd_present(*pmd))
			continue;
		if (pmd_huge(*pmd) && is_vm_hugetlb_page(vma)) {
			queue_pages_hugetlb_pmd_range(vma, pmd, nodes,
						flags, private);
			continue;
		}
		split_huge_page_pmd(vma, addr, pmd);
		if (pmd_none_or_trans_huge_or_clear_bad(pmd))
			continue;
		if (queue_pages_pte_range(vma, pmd, addr, next, nodes,
				    flags, private))
			return -EIO;
	} while (pmd++, addr = next, addr != end);
	return 0;
}

static inline int queue_pages_pud_range(struct vm_area_struct *vma, pgd_t *pgd,
		unsigned long addr, unsigned long end,
		const nodemask_t *nodes, unsigned long flags,
		void *private)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_huge(*pud) && is_vm_hugetlb_page(vma))
			continue;
		if (pud_none_or_clear_bad(pud))
			continue;
		if (queue_pages_pmd_range(vma, pud, addr, next, nodes,
				    flags, private))
			return -EIO;
	} while (pud++, addr = next, addr != end);
	return 0;
}

static inline int queue_pages_pgd_range(struct vm_area_struct *vma,
		unsigned long addr, unsigned long end,
		const nodemask_t *nodes, unsigned long flags,
		void *private)
{
	pgd_t *pgd;
	unsigned long next;

	pgd = pgd_offset(vma->vm_mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		if (queue_pages_pud_range(vma, pgd, addr, next, nodes,
				    flags, private))
			return -EIO;
	} while (pgd++, addr = next, addr != end);
	return 0;
}

#ifdef CONFIG_NUMA_BALANCING
/*
 * This is used to mark a range of virtual addresses to be inaccessible.
 * These are later cleared by a NUMA hinting fault. Depending on these
 * faults, pages may be migrated for better NUMA placement.
 *
 * This is assuming that NUMA faults are handled using PROT_NONE. If
 * an architecture makes a different choice, it will need further
 * changes to the core.
 */
unsigned long change_prot_numa(struct vm_area_struct *vma,
			unsigned long addr, unsigned long end)
{
	int nr_updated;

	nr_updated = change_protection(vma, addr, end, vma->vm_page_prot, 0, 1);
	if (nr_updated)
		count_vm_numa_events(NUMA_PTE_UPDATES, nr_updated);

	return nr_updated;
}
#else
static unsigned long change_prot_numa(struct vm_area_struct *vma,
			unsigned long addr, unsigned long end)
{
	return 0;
}
#endif /* CONFIG_NUMA_BALANCING */

/*
 * Walk through page tables and collect pages to be migrated.
 *
 * If pages found in a given range are on a set of nodes (determined by
 * @nodes and @flags,) it's isolated and queued to the pagelist which is
 * passed via @private.)
 */
static int
queue_pages_range(struct mm_struct *mm, unsigned long start, unsigned long end,
		const nodemask_t *nodes, unsigned long flags, void *private)
{
	int err = 0;
	struct vm_area_struct *vma, *prev;

	vma = find_vma(mm, start);
	if (!vma)
		return -EFAULT;
	prev = NULL;
	for (; vma && vma->vm_start < end; vma = vma->vm_next) {
		unsigned long endvma = vma->vm_end;

		if (endvma > end)
			endvma = end;
		if (vma->vm_start > start)
			start = vma->vm_start;

		if (!(flags & MPOL_MF_DISCONTIG_OK)) {
			if (!vma->vm_next && vma->vm_end < end)
				return -EFAULT;
			if (prev && prev->vm_end < vma->vm_start)
				return -EFAULT;
		}

		if (flags & MPOL_MF_LAZY) {
			/* Similar to task_numa_work, skip inaccessible VMAs */
			if (vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE))
				change_prot_numa(vma, start, endvma);
			goto next;
		}

		if ((flags & MPOL_MF_STRICT) ||
		     ((flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL)) &&
		      vma_migratable(vma))) {

			err = queue_pages_pgd_range(vma, start, endvma, nodes,
						flags, private);
			if (err)
				break;
		}
next:
		prev = vma;
	}
	return err;
}

/*
 * Apply policy to a single VMA
 * This must be called with the mmap_sem held for writing.
 */
static int vma_replace_policy(struct vm_area_struct *vma,
						struct mempolicy *pol)
{
	int err;
	struct mempolicy *old;
	struct mempolicy *new;

	pr_debug("vma %lx-%lx/%lx vm_ops %p vm_file %p set_policy %p\n",
		 vma->vm_start, vma->vm_end, vma->vm_pgoff,
		 vma->vm_ops, vma->vm_file,
		 vma->vm_ops ? vma->vm_ops->set_policy : NULL);

	new = mpol_dup(pol);
	if (IS_ERR(new))
		return PTR_ERR(new);

	if (vma->vm_ops && vma->vm_ops->set_policy) {
		err = vma->vm_ops->set_policy(vma, new);
		if (err)
			goto err_out;
	}

	old = vma->vm_policy;
	vma->vm_policy = new; /* protected by mmap_sem */
	mpol_put(old);

	return 0;
 err_out:
	mpol_put(new);
	return err;
}

/* Step 2: apply policy to a range and do splits. */
static int mbind_range(struct mm_struct *mm, unsigned long start,
		       unsigned long end, struct mempolicy *new_pol)
{
	struct vm_area_struct *next;
	struct vm_area_struct *prev;
	struct vm_area_struct *vma;
	int err = 0;
	pgoff_t pgoff;
	unsigned long vmstart;
	unsigned long vmend;

	vma = find_vma(mm, start);
	if (!vma || vma->vm_start > start)
		return -EFAULT;

	prev = vma->vm_prev;
	if (start > vma->vm_start)
		prev = vma;

	for (; vma && vma->vm_start < end; prev = vma, vma = next) {
		next = vma->vm_next;
		vmstart = max(start, vma->vm_start);
		vmend   = min(end, vma->vm_end);

		if (mpol_equal(vma_policy(vma), new_pol))
			continue;

		pgoff = vma->vm_pgoff +
			((vmstart - vma->vm_start) >> PAGE_SHIFT);
		prev = vma_merge(mm, prev, vmstart, vmend, vma->vm_flags,
				  vma->anon_vma, vma->vm_file, pgoff,
				  new_pol, vma_get_anon_name(vma));
		if (prev) {
			vma = prev;
			next = vma->vm_next;
			if (mpol_equal(vma_policy(vma), new_pol))
				continue;
			/* vma_merge() joined vma && vma->next, case 8 */
			goto replace;
		}
		if (vma->vm_start != vmstart) {
			err = split_vma(vma->vm_mm, vma, vmstart, 1);
			if (err)
				goto out;
		}
		if (vma->vm_end != vmend) {
			err = split_vma(vma->vm_mm, vma, vmend, 0);
			if (err)
				goto out;
		}
 replace:
		err = vma_replace_policy(vma, new_pol);
		if (err)
			goto out;
	}

 out:
	return err;
}

/* Set the process memory policy */
static long do_set_mempolicy(unsigned short mode, unsigned short flags,
			     nodemask_t *nodes)
{
	struct mempolicy *new, *old;
	NODEMASK_SCRATCH(scratch);
	int ret;

	if (!scratch)
		return -ENOMEM;

	new = mpol_new(mode, flags, nodes);
	if (IS_ERR(new)) {
		ret = PTR_ERR(new);
		goto out;
	}

	task_lock(current);
	ret = mpol_set_nodemask(new, nodes, scratch);
	if (ret) {
		task_unlock(current);
		mpol_put(new);
		goto out;
	}
	old = current->mempolicy;
	current->mempolicy = new;
	if (new && new->mode == MPOL_INTERLEAVE &&
	    nodes_weight(new->v.nodes))
		current->il_next = first_node(new->v.nodes);
	task_unlock(current);
	mpol_put(old);
	ret = 0;
out:
	NODEMASK_SCRATCH_FREE(scratch);
	return ret;
}

/*
 * Return nodemask for policy for get_mempolicy() query
 *
 * Called with task's alloc_lock held
 */
static void get_policy_nodemask(struct mempolicy *p, nodemask_t *nodes)
{
	nodes_clear(*nodes);
	if (p == &default_policy)
		return;

	switch (p->mode) {
	case MPOL_BIND:
		/* Fall through */
	case MPOL_INTERLEAVE:
		*nodes = p->v.nodes;
		break;
	case MPOL_PREFERRED:
		if (!(p->flags & MPOL_F_LOCAL))
			node_set(p->v.preferred_node, *nodes);
		/* else return empty node mask for local allocation */
		break;
	default:
		BUG();
	}
}

static int lookup_node(struct mm_struct *mm, unsigned long addr)
{
	struct page *p;
	int err;

	err = get_user_pages(current, mm, addr & PAGE_MASK, 1, 0, 0, &p, NULL);
	if (err >= 0) {
		err = page_to_nid(p);
		put_page(p);
	}
	return err;
}

/* Retrieve NUMA policy */
static long do_get_mempolicy(int *policy, nodemask_t *nmask,
			     unsigned long addr, unsigned long flags)
{
	int err;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = NULL;
	struct mempolicy *pol = current->mempolicy;

	if (flags &
		~(unsigned long)(MPOL_F_NODE|MPOL_F_ADDR|MPOL_F_MEMS_ALLOWED))
		return -EINVAL;

	if (flags & MPOL_F_MEMS_ALLOWED) {
		if (flags & (MPOL_F_NODE|MPOL_F_ADDR))
			return -EINVAL;
		*policy = 0;	/* just so it's initialized */
		task_lock(current);
		*nmask  = cpuset_current_mems_allowed;
		task_unlock(current);
		return 0;
	}

	if (flags & MPOL_F_ADDR) {
		/*
		 * Do NOT fall back to task policy if the
		 * vma/shared policy at addr is NULL.  We
		 * want to return MPOL_DEFAULT in this case.
		 */
		down_read(&mm->mmap_sem);
		vma = find_vma_intersection(mm, addr, addr+1);
		if (!vma) {
			up_read(&mm->mmap_sem);
			return -EFAULT;
		}
		if (vma->vm_ops && vma->vm_ops->get_policy)
			pol = vma->vm_ops->get_policy(vma, addr);
		else
			pol = vma->vm_policy;
	} else if (addr)
		return -EINVAL;

	if (!pol)
		pol = &default_policy;	/* indicates default behavior */

	if (flags & MPOL_F_NODE) {
		if (flags & MPOL_F_ADDR) {
			err = lookup_node(mm, addr);
			if (err < 0)
				goto out;
			*policy = err;
		} else if (pol == current->mempolicy &&
				pol->mode == MPOL_INTERLEAVE) {
			*policy = current->il_next;
		} else {
			err = -EINVAL;
			goto out;
		}
	} else {
		*policy = pol == &default_policy ? MPOL_DEFAULT :
						pol->mode;
		/*
		 * Internal mempolicy flags must be masked off before exposing
		 * the policy to userspace.
		 */
		*policy |= (pol->flags & MPOL_MODE_FLAGS);
	}

	err = 0;
	if (nmask) {
		if (mpol_store_user_nodemask(pol)) {
			*nmask = pol->w.user_nodemask;
		} else {
			task_lock(current);
			get_policy_nodemask(pol, nmask);
			task_unlock(current);
		}
	}

 out:
	mpol_cond_put(pol);
	if (vma)
		up_read(&current->mm->mmap_sem);
	return err;
}

#ifdef CONFIG_MIGRATION
/*
 * page migration
 */
static void migrate_page_add(struct page *page, struct list_head *pagelist,
				unsigned long flags)
{
	/*
	 * Avoid migrating a page that is shared with others.
	 */
	if ((flags & MPOL_MF_MOVE_ALL) || page_mapcount(page) == 1) {
		if (!isolate_lru_page(page)) {
			list_add_tail(&page->lru, pagelist);
			inc_zone_page_state(page, NR_ISOLATED_ANON +
					    page_is_file_cache(page));
		}
	}
}

static struct page *new_node_page(struct page *page, unsigned long node, int **x)
{
	if (PageHuge(page))
		return alloc_huge_page_node(page_hstate(compound_head(page)),
					node);
	else
		return alloc_pages_exact_node(node, GFP_HIGHUSER_MOVABLE, 0);
}

/*
 * Migrate pages from one node to a target node.
 * Returns error or the number of pages not migrated.
 */
static int migrate_to_node(struct mm_struct *mm, int source, int dest,
			   int flags)
{
	nodemask_t nmask;
	LIST_HEAD(pagelist);
	int err = 0;

	nodes_clear(nmask);
	node_set(source, nmask);

	/*
	 * This does not "check" the range but isolates all pages that
	 * need migration.  Between passing in the full user address
	 * space range and MPOL_MF_DISCONTIG_OK, this call can not fail.
	 */
	VM_BUG_ON(!(flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL)));
	queue_pages_range(mm, mm->mmap->vm_start, mm->task_size, &nmask,
			flags | MPOL_MF_DISCONTIG_OK, &pagelist);

	if (!list_empty(&pagelist)) {
		err = migrate_pages(&pagelist, new_node_page, NULL, dest,
					MIGRATE_SYNC, MR_SYSCALL);
		if (err)
			putback_movable_pages(&pagelist);
	}

	return err;
}

/*
 * Move pages between the two nodesets so as to preserve the physical
 * layout as much as possible.
 *
 * Returns the number of page that could not be moved.
 */
int do_migrate_pages(struct mm_struct *mm, const nodemask_t *from,
		     const nodemask_t *to, int flags)
{
	int busy = 0;
	int err;
	nodemask_t tmp;

	err = migrate_prep();
	if (err)
		return err;

	down_read(&mm->mmap_sem);

	err = migrate_vmas(mm, from, to, flags);
	if (err)
		goto out;

	/*
	 * Find a 'source' bit set in 'tmp' whose corresponding 'dest'
	 * bit in 'to' is not also set in 'tmp'.  Clear the found 'source'
	 * bit in 'tmp', and return that <source, dest> pair for migration.
	 * The pair of nodemasks 'to' and 'from' define the map.
	 *
	 * If no pair of bits is found that way, fallback to picking some
	 * pair of 'source' and 'dest' bits that are not the same.  If the
	 * 'source' and 'dest' bits are the same, this represents a node
	 * that will be migrating to itself, so no pages need move.
	 *
	 * If no bits are left in 'tmp', or if all remaining bits left
	 * in 'tmp' correspond to the same bit in 'to', return false
	 * (nothing left to migrate).
	 *
	 * This lets us pick a pair of nodes to migrate between, such that
	 * if possible the dest node is not already occupied by some other
	 * source node, minimizing the risk of overloading the memory on a
	 * node that would happen if we migrated incoming memory to a node
	 * before migrating outgoing memory source that same node.
	 *
	 * A single scan of tmp is sufficient.  As we go, we remember the
	 * most recent <s, d> pair that moved (s != d).  If we find a pair
	 * that not only moved, but what's better, moved to an empty slot
	 * (d is not set in tmp), then we break out then, with that pair.
	 * Otherwise when we finish scanning from_tmp, we at least have the
	 * most recent <s, d> pair that moved.  If we get all the way through
	 * the scan of tmp without finding any node that moved, much less
	 * moved to an empty node, then there is nothing left worth migrating.
	 */

	tmp = *from;
	while (!nodes_empty(tmp)) {
		int s,d;
		int source = NUMA_NO_NODE;
		int dest = 0;

		for_each_node_mask(s, tmp) {

			/*
			 * do_migrate_pages() tries to maintain the relative
			 * node relationship of the pages established between
			 * threads and memory areas.
                         *
			 * However if the number of source nodes is not equal to
			 * the number of destination nodes we can not preserve
			 * this node relative relationship.  In that case, skip
			 * copying memory from a node that is in the destination
			 * mask.
			 *
			 * Example: [2,3,4] -> [3,4,5] moves everything.
			 *          [0-7] - > [3,4,5] moves only 0,1,2,6,7.
			 */

			if ((nodes_weight(*from) != nodes_weight(*to)) &&
						(node_isset(s, *to)))
				continue;

			d = node_remap(s, *from, *to);
			if (s == d)
				continue;

			source = s;	/* Node moved. Memorize */
			dest = d;

			/* dest not in remaining from nodes? */
			if (!node_isset(dest, tmp))
				break;
		}
		if (source == NUMA_NO_NODE)
			break;

		node_clear(source, tmp);
		err = migrate_to_node(mm, source, dest, flags);
		if (err > 0)
			busy += err;
		if (err < 0)
			break;
	}
out:
	up_read(&mm->mmap_sem);
	if (err < 0)
		return err;
	return busy;

}

/*
 * Allocate a new page for page migration based on vma policy.
 * Start by assuming the page is mapped by the same vma as contains @start.
 * Search forward from there, if not.  N.B., this assumes that the
 * list of pages handed to migrate_pages()--which is how we get here--
 * is in virtual address order.
 */
static struct page *new_page(struct page *page, unsigned long start, int **x)
{
	struct vm_area_struct *vma;
	unsigned long uninitialized_var(address);

	vma = find_vma(current->mm, start);
	while (vma) {
		address = page_address_in_vma(page, vma);
		if (address != -EFAULT)
			break;
		vma = vma->vm_next;
	}

	if (PageHuge(page)) {
		BUG_ON(!vma);
		return alloc_huge_page_noerr(vma, address, 1);
	}
	/*
	 * if !vma, alloc_page_vma() will use task or system default policy
	 */
	return alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, address);
}
#else

static void migrate_page_add(struct page *page, struct list_head *pagelist,
				unsigned long flags)
{
}

int do_migrate_pages(struct mm_struct *mm, const nodemask_t *from,
		     const nodemask_t *to, int flags)
{
	return -ENOSYS;
}

static struct page *new_page(struct page *page, unsigned long start, int **x)
{
	return NULL;
}
#endif

static long do_mbind(unsigned long start, unsigned long len,
		     unsigned short mode, unsigned short mode_flags,
		     nodemask_t *nmask, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct mempolicy *new;
	unsigned long end;
	int err;
	LIST_HEAD(pagelist);

	if (flags & ~(unsigned long)MPOL_MF_VALID)
		return -EINVAL;
	if ((flags & MPOL_MF_MOVE_ALL) && !capable(CAP_SYS_NICE))
		return -EPERM;

	if (start & ~PAGE_MASK)
		return -EINVAL;

	if (mode == MPOL_DEFAULT)
		flags &= ~MPOL_MF_STRICT;

	len = (len + PAGE_SIZE - 1) & PAGE_MASK;
	end = start + len;

	if (end < start)
		return -EINVAL;
	if (end == start)
		return 0;

	new = mpol_new(mode, mode_flags, nmask);
	if (IS_ERR(new))
		return PTR_ERR(new);

	if (flags & MPOL_MF_LAZY)
		new->flags |= MPOL_F_MOF;

	/*
	 * If we are using the default policy then operation
	 * on discontinuous address spaces is okay after all
	 */
	if (!new)
		flags |= MPOL_MF_DISCONTIG_OK;

	pr_debug("mbind %lx-%lx mode:%d flags:%d nodes:%lx\n",
		 start, start + len, mode, mode_flags,
		 nmask ? nodes_addr(*nmask)[0] : NUMA_NO_NODE);

	if (flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL)) {

		err = migrate_prep();
		if (err)
			goto mpol_out;
	}
	{
		NODEMASK_SCRATCH(scratch);
		if (scratch) {
			down_write(&mm->mmap_sem);
			task_lock(current);
			err = mpol_set_nodemask(new, nmask, scratch);
			task_unlock(current);
			if (err)
				up_write(&mm->mmap_sem);
		} else
			err = -ENOMEM;
		NODEMASK_SCRATCH_FREE(scratch);
	}
	if (err)
		goto mpol_out;

	err = queue_pages_range(mm, start, end, nmask,
			  flags | MPOL_MF_INVERT, &pagelist);
	if (!err)
		err = mbind_range(mm, start, end, new);

	if (!err) {
		int nr_failed = 0;

		if (!list_empty(&pagelist)) {
			WARN_ON_ONCE(flags & MPOL_MF_LAZY);
			nr_failed = migrate_pages(&pagelist, new_page, NULL,
				start, MIGRATE_SYNC, MR_MEMPOLICY_MBIND);
			if (nr_failed)
				putback_movable_pages(&pagelist);
		}

		if (nr_failed && (flags & MPOL_MF_STRICT))
			err = -EIO;
	} else
		putback_movable_pages(&pagelist);

	up_write(&mm->mmap_sem);
 mpol_out:
	mpol_put(new);
	return err;
}

/*
 * User space interface with variable sized bitmaps for nodelists.
 */

/* Copy a node mask from user space. */
static int get_nodes(nodemask_t *nodes, const unsigned long __user *nmask,
		     unsigned long maxnode)
{
	unsigned long k;
	unsigned long nlongs;
	unsigned long endmask;

	--maxnode;
	nodes_clear(*nodes);
	if (maxnode == 0 || !nmask)
		return 0;
	if (maxnode > PAGE_SIZE*BITS_PER_BYTE)
		return -EINVAL;

	nlongs = BITS_TO_LONGS(maxnode);
	if ((maxnode % BITS_PER_LONG) == 0)
		endmask = ~0UL;
	else
		endmask = (1UL << (maxnode % BITS_PER_LONG)) - 1;

	/* When the user specified more nodes than supported just check
	   if the non supported part is all zero. */
	if (nlongs > BITS_TO_LONGS(MAX_NUMNODES)) {
		if (nlongs > PAGE_SIZE/sizeof(long))
			return -EINVAL;
		for (k = BITS_TO_LONGS(MAX_NUMNODES); k < nlongs; k++) {
			unsigned long t;
			if (get_user(t, nmask + k))
				return -EFAULT;
			if (k == nlongs - 1) {
				if (t & endmask)
					return -EINVAL;
			} else if (t)
				return -EINVAL;
		}
		nlongs = BITS_TO_LONGS(MAX_NUMNODES);
		endmask = ~0UL;
	}

	if (copy_from_user(nodes_addr(*nodes), nmask, nlongs*sizeof(unsigned long)))
		return -EFAULT;
	nodes_addr(*nodes)[nlongs-1] &= endmask;
	return 0;
}

/* Copy a kernel node mask to user space */
static int copy_nodes_to_user(unsigned long __user *mask, unsigned long maxnode,
			      nodemask_t *nodes)
{
	unsigned long copy = ALIGN(maxnode-1, 64) / 8;
	const int nbytes = BITS_TO_LONGS(MAX_NUMNODES) * sizeof(long);

	if (copy > nbytes) {
		if (copy > PAGE_SIZE)
			return -EINVAL;
		if (clear_user((char __user *)mask + nbytes, copy - nbytes))
			return -EFAULT;
		copy = nbytes;
	}
	return copy_to_user(mask, nodes_addr(*nodes), copy) ? -EFAULT : 0;
}

SYSCALL_DEFINE6(mbind, unsigned long, start, unsigned long, len,
		unsigned long, mode, const unsigned long __user *, nmask,
		unsigned long, maxnode, unsigned, flags)
{
	nodemask_t nodes;
	int err;
	unsigned short mode_flags;

	mode_flags = mode & MPOL_MODE_FLAGS;
	mode &= ~MPOL_MODE_FLAGS;
	if (mode >= MPOL_MAX)
		return -EINVAL;
	if ((mode_flags & MPOL_F_STATIC_NODES) &&
	    (mode_flags & MPOL_F_RELATIVE_NODES))
		return -EINVAL;
	err = get_nodes(&nodes, nmask, maxnode);
	if (err)
		return err;
	return do_mbind(start, len, mode, mode_flags, &nodes, flags);
}

/* Set the process memory policy */
SYSCALL_DEFINE3(set_mempolicy, int, mode, const unsigned long __user *, nmask,
		unsigned long, maxnode)
{
	int err;
	nodemask_t nodes;
	unsigned short flags;

	flags = mode & MPOL_MODE_FLAGS;
	mode &= ~MPOL_MODE_FLAGS;
	if ((unsigned int)mode >= MPOL_MAX)
		return -EINVAL;
	if ((flags & MPOL_F_STATIC_NODES) && (flags & MPOL_F_RELATIVE_NODES))
		return -EINVAL;
	err = get_nodes(&nodes, nmask, maxnode);
	if (err)
		return err;
	return do_set_mempolicy(mode, flags, &nodes);
}

SYSCALL_DEFINE4(migrate_pages, pid_t, pid, unsigned long, maxnode,
		const unsigned long __user *, old_nodes,
		const unsigned long __user *, new_nodes)
{
	const struct cred *cred = current_cred(), *tcred;
	struct mm_struct *mm = NULL;
	struct task_struct *task;
	nodemask_t task_nodes;
	int err;
	nodemask_t *old;
	nodemask_t *new;
	NODEMASK_SCRATCH(scratch);

	if (!scratch)
		return -ENOMEM;

	old = &scratch->mask1;
	new = &scratch->mask2;

	err = get_nodes(old, old_nodes, maxnode);
	if (err)
		goto out;

	err = get_nodes(new, new_nodes, maxnode);
	if (err)
		goto out;

	/* Find the mm_struct */
	rcu_read_lock();
	task = pid ? find_task_by_vpid(pid) : current;
	if (!task) {
		rcu_read_unlock();
		err = -ESRCH;
		goto out;
	}
	get_task_struct(task);

	err = -EINVAL;

	/*
	 * Check if this process has the right to modify the specified
	 * process. The right exists if the process has administrative
	 * capabilities, superuser privileges or the same
	 * userid as the target process.
	 */
	tcred = __task_cred(task);
	if (!uid_eq(cred->euid, tcred->suid) && !uid_eq(cred->euid, tcred->uid) &&
	    !uid_eq(cred->uid,  tcred->suid) && !uid_eq(cred->uid,  tcred->uid) &&
	    !capable(CAP_SYS_NICE)) {
		rcu_read_unlock();
		err = -EPERM;
		goto out_put;
	}
	rcu_read_unlock();

	task_nodes = cpuset_mems_allowed(task);
	/* Is the user allowed to access the target nodes? */
	if (!nodes_subset(*new, task_nodes) && !capable(CAP_SYS_NICE)) {
		err = -EPERM;
		goto out_put;
	}

	if (!nodes_subset(*new, node_states[N_MEMORY])) {
		err = -EINVAL;
		goto out_put;
	}

	err = security_task_movememory(task);
	if (err)
		goto out_put;

	mm = get_task_mm(task);
	put_task_struct(task);

	if (!mm) {
		err = -EINVAL;
		goto out;
	}

	err = do_migrate_pages(mm, old, new,
		capable(CAP_SYS_NICE) ? MPOL_MF_MOVE_ALL : MPOL_MF_MOVE);

	mmput(mm);
out:
	NODEMASK_SCRATCH_FREE(scratch);

	return err;

out_put:
	put_task_struct(task);
	goto out;

}


/* Retrieve NUMA policy */
SYSCALL_DEFINE5(get_mempolicy, int __user *, policy,
		unsigned long __user *, nmask, unsigned long, maxnode,
		unsigned long, addr, unsigned long, flags)
{
	int err;
	int uninitialized_var(pval);
	nodemask_t nodes;

	if (nmask != NULL && maxnode < MAX_NUMNODES)
		return -EINVAL;

	err = do_get_mempolicy(&pval, &nodes, addr, flags);

	if (err)
		return err;

	if (policy && put_user(pval, policy))
		return -EFAULT;

	if (nmask)
		err = copy_nodes_to_user(nmask, maxnode, &nodes);

	return err;
}

#ifdef CONFIG_COMPAT

COMPAT_SYSCALL_DEFINE5(get_mempolicy, int __user *, policy,
		       compat_ulong_t __user *, nmask,
		       compat_ulong_t, maxnode,
		       compat_ulong_t, addr, compat_ulong_t, flags)
{
	long err;
	unsigned long __user *nm = NULL;
	unsigned long nr_bits, alloc_size;
	DECLARE_BITMAP(bm, MAX_NUMNODES);

	nr_bits = min_t(unsigned long, maxnode-1, MAX_NUMNODES);
	alloc_size = ALIGN(nr_bits, BITS_PER_LONG) / 8;

	if (nmask)
		nm = compat_alloc_user_space(alloc_size);

	err = sys_get_mempolicy(policy, nm, nr_bits+1, addr, flags);

	if (!err && nmask) {
		unsigned long copy_size;
		copy_size = min_t(unsigned long, sizeof(bm), alloc_size);
		err = copy_from_user(bm, nm, copy_size);
		/* ensure entire bitmap is zeroed */
		err |= clear_user(nmask, ALIGN(maxnode-1, 8) / 8);
		err |= compat_put_bitmap(nmask, bm, nr_bits);
	}

	return err;
}

COMPAT_SYSCALL_DEFINE3(set_mempolicy, int, mode, compat_ulong_t __user *, nmask,
		       compat_ulong_t, maxnode)
{
	unsigned long __user *nm = NULL;
	unsigned long nr_bits, alloc_size;
	DECLARE_BITMAP(bm, MAX_NUMNODES);

	nr_bits = min_t(unsigned long, maxnode-1, MAX_NUMNODES);
	alloc_size = ALIGN(nr_bits, BITS_PER_LONG) / 8;

	if (nmask) {
		if (compat_get_bitmap(bm, nmask, nr_bits))
			return -EFAULT;
		nm = compat_alloc_user_space(alloc_size);
		if (copy_to_user(nm, bm, alloc_size))
			return -EFAULT;
	}

	return sys_set_mempolicy(mode, nm, nr_bits+1);
}

COMPAT_SYSCALL_DEFINE6(mbind, compat_ulong_t, start, compat_ulong_t, len,
		       compat_ulong_t, mode, compat_ulong_t __user *, nmask,
		       compat_ulong_t, maxnode, compat_ulong_t, flags)
{
	unsigned long __user *nm = NULL;
	unsigned long nr_bits, alloc_size;
	nodemask_t bm;

	nr_bits = min_t(unsigned long, maxnode-1, MAX_NUMNODES);
	alloc_size = ALIGN(nr_bits, BITS_PER_LONG) / 8;

	if (nmask) {
		if (compat_get_bitmap(nodes_addr(bm), nmask, nr_bits))
			return -EFAULT;
		nm = compat_alloc_user_space(alloc_size);
		if (copy_to_user(nm, nodes_addr(bm), alloc_size))
			return -EFAULT;
	}

	return sys_mbind(start, len, mode, nm, nr_bits+1, flags);
}

#endif

struct mempolicy *__get_vma_policy(struct vm_area_struct *vma,
						unsigned long addr)
{
	struct mempolicy *pol = NULL;

	if (vma) {
		if (vma->vm_ops && vma->vm_ops->get_policy) {
			pol = vma->vm_ops->get_policy(vma, addr);
		} else if (vma->vm_policy) {
			pol = vma->vm_policy;

			/*
			 * shmem_alloc_page() passes MPOL_F_SHARED policy with
			 * a pseudo vma whose vma->vm_ops=NULL. Take a reference
			 * count on these policies which will be dropped by
			 * mpol_cond_put() later
			 */
			if (mpol_needs_cond_ref(pol))
				mpol_get(pol);
		}
	}

	return pol;
}

/*
 * get_vma_policy(@vma, @addr)
 * @vma: virtual memory area whose policy is sought
 * @addr: address in @vma for shared policy lookup
 *
 * Returns effective policy for a VMA at specified address.
 * Falls back to current->mempolicy or system default policy, as necessary.
 * Shared policies [those marked as MPOL_F_SHARED] require an extra reference
 * count--added by the get_policy() vm_op, as appropriate--to protect against
 * freeing by another task.  It is the caller's responsibility to free the
 * extra reference for shared policies.
 */
static struct mempolicy *get_vma_policy(struct vm_area_struct *vma,
						unsigned long addr)
{
	struct mempolicy *pol = __get_vma_policy(vma, addr);

	if (!pol)
		pol = get_task_policy(current);

	return pol;
}

bool vma_policy_mof(struct vm_area_struct *vma)
{
	struct mempolicy *pol;

	if (vma->vm_ops && vma->vm_ops->get_policy) {
		bool ret = false;

		pol = vma->vm_ops->get_policy(vma, vma->vm_start);
		if (pol && (pol->flags & MPOL_F_MOF))
			ret = true;
		mpol_cond_put(pol);

		return ret;
	}

	pol = vma->vm_policy;
	if (!pol)
		pol = get_task_policy(current);

	return pol->flags & MPOL_F_MOF;
}

static int apply_policy_zone(struct mempolicy *policy, enum zone_type zone)
{
	enum zone_type dynamic_policy_zone = policy_zone;

	BUG_ON(dynamic_policy_zone == ZONE_MOVABLE);

	/*
	 * if policy->v.nodes has movable memory only,
	 * we apply policy when gfp_zone(gfp) = ZONE_MOVABLE only.
	 *
	 * policy->v.nodes is intersect with node_states[N_MEMORY].
	 * so if the following test faile, it implies
	 * policy->v.nodes has movable memory only.
	 */
	if (!nodes_intersects(policy->v.nodes, node_states[N_HIGH_MEMORY]))
		dynamic_policy_zone = ZONE_MOVABLE;

	return zone >= dynamic_policy_zone;
}

/*
 * Return a nodemask representing a mempolicy for filtering nodes for
 * page allocation
 */
static nodemask_t *policy_nodemask(gfp_t gfp, struct mempolicy *policy)
{
	/* Lower zones don't get a nodemask applied for MPOL_BIND */
	if (unlikely(policy->mode == MPOL_BIND) &&
			apply_policy_zone(policy, gfp_zone(gfp)) &&
			cpuset_nodemask_valid_mems_allowed(&policy->v.nodes))
		return &policy->v.nodes;

	return NULL;
}

/* Return a zonelist indicated by gfp for node representing a mempolicy */
static struct zonelist *policy_zonelist(gfp_t gfp, struct mempolicy *policy,
	int nd)
{
	switch (policy->mode) {
	case MPOL_PREFERRED:
		if (!(policy->flags & MPOL_F_LOCAL))
			nd = policy->v.preferred_node;
		break;
	case MPOL_BIND:
		/*
		 * Normally, MPOL_BIND allocations are node-local within the
		 * allowed nodemask.  However, if __GFP_THISNODE is set and the
		 * current node isn't part of the mask, we use the zonelist for
		 * the first node in the mask instead.
		 */
		if (unlikely(gfp & __GFP_THISNODE) &&
				unlikely(!node_isset(nd, policy->v.nodes)))
			nd = first_node(policy->v.nodes);
		break;
	default:
		BUG();
	}
	return node_zonelist(nd, gfp);
}

/* Do dynamic interleaving for a process */
static unsigned interleave_nodes(struct mempolicy *policy)
{
	unsigned nid, next;
	struct task_struct *me = current;

	nid = me->il_next;
	next = next_node(nid, policy->v.nodes);
	if (next >= MAX_NUMNODES)
		next = first_node(policy->v.nodes);
	if (next < MAX_NUMNODES)
		me->il_next = next;
	return nid;
}

/*
 * Depending on the memory policy provide a node from which to allocate the
 * next slab entry.
 */
unsigned int mempolicy_slab_node(void)
{
	struct mempolicy *policy;
	int node = numa_mem_id();

	if (in_interrupt())
		return node;

	policy = current->mempolicy;
	if (!policy || policy->flags & MPOL_F_LOCAL)
		return node;

	switch (policy->mode) {
	case MPOL_PREFERRED:
		/*
		 * handled MPOL_F_LOCAL above
		 */
		return policy->v.preferred_node;

	case MPOL_INTERLEAVE:
		return interleave_nodes(policy);

	case MPOL_BIND: {
		/*
		 * Follow bind policy behavior and start allocation at the
		 * first node.
		 */
		struct zonelist *zonelist;
		struct zone *zone;
		enum zone_type highest_zoneidx = gfp_zone(GFP_KERNEL);
		zonelist = &NODE_DATA(node)->node_zonelists[0];
		(void)first_zones_zonelist(zonelist, highest_zoneidx,
							&policy->v.nodes,
							&zone);
		return zone ? zone->node : node;
	}

	default:
		BUG();
	}
}

/* Do static interleaving for a VMA with known offset. */
static unsigned offset_il_node(struct mempolicy *pol,
		struct vm_area_struct *vma, unsigned long off)
{
	unsigned nnodes = nodes_weight(pol->v.nodes);
	unsigned target;
	int c;
	int nid = NUMA_NO_NODE;

	if (!nnodes)
		return numa_node_id();
	target = (unsigned int)off % nnodes;
	c = 0;
	do {
		nid = next_node(nid, pol->v.nodes);
		c++;
	} while (c <= target);
	return nid;
}

/* Determine a node number for interleave */
static inline unsigned interleave_nid(struct mempolicy *pol,
		 struct vm_area_struct *vma, unsigned long addr, int shift)
{
	if (vma) {
		unsigned long off;

		/*
		 * for small pages, there is no difference between
		 * shift and PAGE_SHIFT, so the bit-shift is safe.
		 * for huge pages, since vm_pgoff is in units of small
		 * pages, we need to shift off the always 0 bits to get
		 * a useful offset.
		 */
		BUG_ON(shift < PAGE_SHIFT);
		off = vma->vm_pgoff >> (shift - PAGE_SHIFT);
		off += (addr - vma->vm_start) >> shift;
		return offset_il_node(pol, vma, off);
	} else
		return interleave_nodes(pol);
}

/*
 * Return the bit number of a random bit set in the nodemask.
 * (returns NUMA_NO_NODE if nodemask .
	*/
sma: vis->getko pr.nodes,
							&
kfset. b]ogs)
{
	inw,om bid = NUMA_NO_NODE;
wes = nodes_weight. b]ogcy;
	ifnew)
m bid t_bitm_ordpy_tposer(map->r_bits,
	= ge pr.noin_i()ff w-1, MAX_NUMNODES);
	returnitrr;
}

#ifdef CONFIG_HUGETFS}

/*
 c_hugs_zonelisy(@vma, @adma,= gf, flags@m(!pol)
 * @vma: virtual memory area whose policy is sought
 * @addr: address in @vma for shared policy lookft anr interleavd poliht
 *= gf, fla:ma fo] regheares_zoht
 *m(!p:vd r intht to mempolicd r intha fo] ifferenc* couedto mempoliby
 * @nor(ma:vd r intht tf nodemasd r intha fo, MPOL_BINf nodemaup
 *
 * Returnn a zonelissuiratabng for r huge part allocationfind a r int *
 d to the(struct mempolicg fol_coicatituaun] iay after allocati. *
 * Io ths effective policy i'L_BIo', retusnd a r int d to tht mempoli'sby
 * @nor(macy for filterine the zonelile.
 *
 M must b* protected bu_read_mems_allow_begin(e.)
 */
struct zonelistc_hugs_zonelis	 struct vm_area_struct *vma, unsigned long adds,
		t(gfp_t gf, flagsd(struct mempolicy*m *pol,
	;
	nodemask_t*
	nodemaid)
{
	struct zonelist pol;
*m *pol = get_vma_policy(vma, addr)	*
	nodemaol = NUL{
	/s assu !r MPOL_BIND **/
	if (unlikel(*m *p)ol->mode == MPOL_INTERLEAVma) {
zl	d = nods_zonelisd interleave_ni*m *poE, vma, adol,
	;c_huge_pag> shi(e_hstait_vma(ves),_t gf, flaff);
	} ela) {
zl	d  *policy_zonelist(gf, flags*m *poEn numa_node_idrt);
		if(*m *p)ol->mode == MPOL_BINl,
	*
	nodemaol &(*m *p)ol->v.nodes;
	}
	returzpol;
}

/*
 uninet_nodemasof_o mempoliby
 *
 * Io th* currenh task't mempolicy i"
	defau" [= NU]o', retur'= fal' *
 d tt indicaem default poli.  * Otherwi, * extct * the policf nodemaup
cy fo'w bi'cy o'd interlea've policynd to th taumrrent noe mask for
 uninitialito th taumrrent noe mack tooaintain thA singla node for
 'v.preferr'cy o'e-loc've polic, and retur'= tr' d tt indicaerepresence
 r of n-m default mempolile.
 *
 Wees don'banotheA wit] ifferenc* couading the me polic[		mpol_g/put]

	 * cae use th* currenh task .
xammainino it'knowe me policnfind h task

	 t mempolicy is onloweve		chanded by thh task itsele.
 *
   N.B.i It is the caller's responsibility to freand retuowed nodemas.)
 *		boouninet_nodemasof_o mempoli(					&
kfset. b]id)
{
	struct mempolicy->mempolicy;
	int nDE;

	if er(maol &= current->mempolit())
		retur= false;
		task_lock(current);->mempolicy = current->mempolicy;
	switch->mempolicy->mode) {
	case MPOL_PREFERRED:
		if->mempolicy->flags & MPOL_F_LOCAL)

		nid = numa_node_id();
		else
	
	nid = mempolicy->v.preferred_node;
uninet_nodemasof_l_nod nmask,ides);
		break {
	case MPOL_BIND:
		/* Fall through */
	case MPOL_INTERLEAVE:
	demaol  = mempolicy->v.nodes;
		break {
	default:
		BUG();
	}		task_unlock(current)	}
	retur= true;
}
#endif

/*
 t mempolict_nodemass_intersecby
 *
 * Iotask't mempolicy i"
	defau" [= NU]o', retur'= tr' d tt indicae
	defau

	 * poli.  * Otherwi, t cheer for intersecties betweedemaskFind thempoliby
  @nor(macy fo'w bi'cy o'd interlea've poli.  Fy o'perreferr'cy o'e-loc'

	 * poli,he alway
	retur= trs, sinci Is mao allocat		elw therony, fallbant.
 *
 * make		task_loctmasks to p_eve * freeinr o bitt mempolile.
 *		boot mempolict_nodemass_intersec(
	struct task_structtmask,
	,
		const nodemask_t. b]id)
{
	struct mempolicy->mempolicy;		bool ret = true;

	if (nmask)
		return ret;		task_loctmast);->mempolicy tmant->mempolicy;
	if ->mempolit
)
		goto out;

	switch->mempolicy->mode) {
	case MPOL_PREFERRED:
		/*
		 e MPOL_PREFERRge and MPOL_F_LOCALheronpply.preferrof nodes he
		 * allocat, from,heyIs ma, fallback tanothef nodey wheoomfe.
		 Thus,no it'f possibly fotmack tt hav* allocaand memor, fr

			 * noess i* mask.
on */
		break;
	case MPOL_BIND:
	case MPOL_INTERLEAVE:
		res = nodes_intersec(= mempolicy->v.nod,_t. b]is);
		break;
	default:
		BUG();
	;
out:		task_unloctmast);
	return ret;
}

 * Allocate e pageurn interleared polias.
 Oween wit* cae us bit_neeck td do ecitualc* couadi.cy */
static struct pagen alloc_pagn interleast(gfp_t gfpc unsigned ordsk,
	,

	unsigned nid)
{
	struct zonelist pol{
	struct page  paut;
zl	d = nods_zonelise(nid, gfp)	t pagl = n alloc_pag(t gfpd ordsrzpgcy;
	ift pagy &&ahugs_zont(page) =s_zoneligs_zon&zl->gs_zo.prsts[t())
		inc_zone_page_state(page, NUML_INTERLEA_HITst);
	retur  paut;
}


 *
 	n alloc_pagea(v	- * Allocate e pagg for a Vnt.
 *
 	*= g:
		 *    %e(GFGHUS	    sfter allocati. *
 *    %e(GFP_KERN  a kerneD allocatio, *
 *    %e(GFN_HIMEMt, hi= m/ sfter allocatio, *
 *    %e(GFFS *    t allocatiosh could noc fall bacynd ta m_filr syst. *
 *    %e(GFATOMIC es don'sleepnt.
 *
	@d ord:O ordrt of the(Ger allocati. *
 	* @vma P r int d ta VM fo= NUL, if n availasible.
	* @addrV virtuaA addressoff the allocati. M must bi	unduse tha Vnt.
 *
 	* Thifunsectie* allocaste e pagg from t a kernee pagp	boonfindpimplie.
	ave NUMA polics aociocaand with tha VM foe th* current process.
	* Whea VMs is no= NULe callgs mush
	ol
	down_reng on the mmap_sesoff ts.
	e mm_strucsoff tha VMs to p_eve itgg froutgoinaalw. Sh coulb we udde for
	n aND allocatione for pas * that will be mappeynd  *
 	m user spaceR(returns NULy wheo no pawe cabav* allocaant.
 *
	Sh coulb we called with the ap_sesoff tdo vmh
	oes.
 */
struct page
n alloc_pagea(vst(gfp_t gfp
	ind ordsry(struct vm_area_struct *vma,
, unsigned long addr, inaxnode)
{
	struct mempolicy *pol{
	struct page  paut	/
unsigned in= cpuset_memcookiaut;
	rry_= cpus:

		pol = get_vma_policy(vma, addr)	= cpuset_memcookiaol u_read_mems_allow_begin(e;**/
	if (unlikelp	pol->mode == MPOL_INTERLEAVma) {
		unsignet nDE;
	
	nid d interleave_ni *poE, vma, ado - PAGE_SHI +nd ord)ue;
		mpol_cond_put(pol)		t pagl n alloc_pagn interleast(gfpd ordsr,ides);

	if (unlikel!t pagy &u_read_mems_allow_
	rry(= cpuset_memcookiaes)))
			goto rry_= cpusl);

		retur  paut	})	t pagl = n alloc_pagcy_nodemask(g,ed ordsk,
	, *     *policy_zonelist(gid, pes, no)sk,
	, *     *policy_nodemask(g,et(post);-	mpol_cond_put(pol)	
	if (unlikel!t pagy &u_read_mems_allow_
	rry(= cpuset_memcookiaes)))
		goto rry_= cpusl);
	retur  paut;
}


 *
 	n alloc_page* curren- * Allocatc_pagnt.
 *
	@= g:
				%e(GFGHUS	   sfter allocati, *
 *    	%e(GFP_KERN a kerneD allocati, *
 *    	%e(GFN_HIMEMt, hi= meD allocati, *
 *    	%e(GFFS *   s don'c fall bacynd ta m_filr syst. *
 *    	%e(GFATOMIC s don'sleepnt.
	@d ord: P wrdrt ofwoer of allocatiosalitintc_pagn 0rt ishA singlc_pant.
 *
	* Allocate e pagg from t a kernee pagp	boLL.  wheo set  *
	n_interrutooai nexnfindpimyoe th* current proceve NUMA polint.
	R(returns NULy wheo no pawe cabav* allocaant.
 *
	D don'c fal= cpuseupdgrate_task memoge_stat)pc h les*
	1)no it'kack to kel= cpusep_se(e caWAITsp', as*
	2)of allocaving fo* currenh tas(o set interru)es.
 */
struct pagen alloc_page* currest(gfp_t gfpc unsigned orddr)
{
	struct mempolicy *pol = &default_policl{
	struct page  paut	/
unsigned in= cpuset_memcookiaut;
		if ((in_interrupts) &&ly(gfp & __GFP_THISNODol)
		pol = get_task_policy(current);
	rry_= cpus:

= cpuset_memcookiaol u_read_mems_allow_begin(e;**/
	/*
	 Not] ifferenc* couadint_nelied fo= current->mempoli a
	 *  or system defau_empoli a
	/;

	if (pol->mode == MPOL_INTERLEAV)		t pagl n alloc_pagn interleast(gfpd ordsrn interleave_nodes(pool)	} else
	 pagl = n alloc_pagcy_nodemask(g,ed ordsk,
	, *policy_zonelist(gid, pes, numa_node_idrsk,
	, *policy_nodemask(g,et(post))	
	if (unlikel!t pagy &u_read_mems_allow_
	rry(= cpuset_memcookiaes)))
		goto rry_= cpusl));
	retur  paut;
EXPORPAT_MBOL(n alloc_page* currest))d int_vmdupma_policy(struct vm_area_structsrcsry(struct vm_area_structdstdr)
{
	struct mempolicy *pol -	mpodup(t_vma_policyrcost))	
	if (IS_ERp(pol))
		return PTR_ERt(pol)	dstma->vm_poli	d  *p;k;
	return 0;
}

 *
 * I-	mpodup() seas = current= cpusol == cpusebreei_
	b foude, theiu

	 
	bindsng the me polic bit* copyinby'c fapying	mpo
	bindet_policy

	 d with the_mems_allownd retuoweby'c cpuset_mems_allowe d).* Th

	 keepitt mempolicie= cpusoe relativy afte bit* cpuso] movd).Sence
 furnothea kern/* cpus.c updgraty_nodemas)nt.
 *
 = curresk't mempolics mabe 
	bindnded by thanother ta(y thh tasn that	chanh

	 * cpussk't mdes)sos, we nedon'do 
	bindft wkng fo* currenh tats.
 */

/Sollon witr of t mempolicduplndicae
 */
struct mempolicy *-	mpodup(/
struct mempolicy
	odr)
{
	struct mempolicy
	new ka_mee_cac n alles(poliee_cacde, GFP_KERNEL);

	if (!new)

	returR_E_n P(n -ENOMe;**/
	nh task't mempolicy i* protected bn allounlo
	/;

	if
	oldy = current->mempolima) {
		task_lock(current);
			new t *old;			task_unlock(current);
	} else
			new t *old;

	if= current cpuseisebreei_
	b fou(Vma) {
t nodemask_t mdes = cpuset_mems_allowek(current);

	if (wcy->flags & MPOL_REL_BIING)))
	g	mpo
	bindet_polic(*new&t_mely, MPOREL_BI_STEP2d();
		else
	g	mpo
	bindet_polic(*new&t_mely, MPOREL_BI_N_ONG();
	}atonamiisse& (wcy] ic int1et);
	return*new}*/

/Sollon witr of t mempolic= comristio
 *		boo *-	mpot equ(/
struct mempolicyagsd(struct mempolicybft)
{
	if!a 0 ||b())
		retur= false
		if ol->mod!= bcy->mod))
		retur= false
		if ol->flag!= bcy, flags)
		retur= false
		if (mpol_store_user_nodemasat())
		if (!nodet equ(aol->w.user_nodema, bcy->w.user_nodemaze))
			retur= false;

	switchacy->mode) {
	case MPOL_BIND:
		/* Fall through */
	case MPOL_INTERLEAVE:
		retur! (!nodet equ(aol->v.nod,_bol->v.nodes);
	case MPOL_PREFERRED:

	returncy->v.preferred_noldy bcy->v.preferred_node;
	default:
		BUG();
		retur= false
}0;
}

 *
 * Shared memorl backing_stoMA policn suppole.
 *
 * remembeempoliciep_evLy wheo bodyes has Shared memore mapp. *
 These policieLherkepset iRed-Bllback freapyarkeg from t it node*
 Theory ai* protected by the ->unlo
spi_unlo,om whicsh coulb wheldup
cy fog ano acceries ty thh frts.
 */

/y lookf* firselemrrenr interseclong sta- (en
 *

/* callgh
	oshe ->unlo
y */
static strucskup_no.
 skuy look(/
strucs Sharvm_poli	*spge, unsigned long start
	unsigned long edr)
{
	strucrbup_no.
len e ->root.rbup_noDE;
w whilenma) {
c strucskup_no.
tmp rbub ent(n,ic strucskup_no,nt n 0;

		ifd star>d  ->g edre
	
	d =->rb_e rig();
		el 
	if (endd  ->= start)
	
	d =->rb_g le();
		else
			break;
	
		if (gs)
		retur= NULL;		for;;ma) {
c strucskup_no.
wnm = NULL;{
	strucrbup_no.
o p_mp rbuo p_enmt);

	if!o p_LT)
			break;
wmp rbub ent(o p_,ic strucskup_no,nt n 0;

	ifw->g endd = start)
			break;

	d o p_es;
	}
	returrbub ent(n,ic strucskup_no,nt n 0}*/

/In.ustte a ner shared policynd to thneliln
 *

/* callgh
	oshe ->unlo
y */
static voiskuin.ust(/
strucs Sharvm_poli	*spgec strucskup_no.
(!new)
{
	strucrbup_no.

tmp &e ->root.rbup_noDE{
	strucrbup_no.
oaurrenm = NULL;
	strucskup_no.
(dDE;
w while*pma) {
oaurrenm *pak;

dmp rbub ent(oaurre,ic strucskup_no,nt n 0;

	if (wcyd star<nt ->= start)
	pol &(*p)->rb_g le();
		el 
	if (wcyg en>nt ->g edre
	pol &(*p)->rb_e rig();
		elre
			BUG();
	}
b_gpya_l_nod& (wcyt(nd,aurre,igfp)	
b_in.ustmcolord& (wcyt(nd&e ->rootfp)	t	pr_debugin.ustclond %lx-%: %dlx\n  (wcyd stan  (wcy, enk.
o (wcym_poli	?o (wcym_poliol->mod:E, 0);
}

/* Finr shared policyndnterseclonneid
 */
struct mempolicy
 (mpol Sharvm_poliuy look(/
strucs Sharvm_poli	*spge, unsigned lonneidr)
{
	struct mempolicy *pol = NULL;
	strucskup_no.
slen;

	if!e ->root.rbup_nogs)
		retur= NULL;spi_sk_loc&e ->unlofp)	slen e uy look(/gfp
dxfp
dxts+1)
		ifdnma) {
		mpol_gesncym_poliol)		t*pol sncym_poli();
	}spi_sk_unloc&e ->unlofp)	
	return pol;
}/
static voisku* fr(
	strucskup_no.
(dr)
{
	mpol_putcym_poliol)	ka_mee_cac * fr(
nee_cacden)ut;
}


 *
 
	mpomispllbgne- t cheey wnothe* current pagt node  k_valtintcmpoliby
 *
 @t pa:of pageolb we cheedl)
 * @vma:mry area therr page mappht
 * @addrn virtual addresa therr page mappht
ht
 L lookf* current policf nop
dcy fo, vm (addnfin"= comrageo"rr pask

	 f nop
dle.
 *
 * Retur:
			-1	-eo semispllbgn,he page itain the righf no
			f no	-eo nop
dca therg the pagsh coulb e.
 *
 Pmpolicd Determocatio"minams"a, alloc_page_vmade*
 C calleg frodefault wita ther, wh knff tdo vmnfindefauilong adocess.
 vis->
	mpomispllbgndd(struct page *page, struct vm_area_struct *vma, unsigned long adde)
{
	struct mempolicy *pol{
	struct zone *zone;d in=etu	nid c_page_to_ni (pag;L;
	unsigned lonm_pgone;d inf th= cmp raw_smp_t proceorde_id();d inf thu	nid = cte_to_nodf th= cd();d in *pu	nid -1();d in		res -1ne;

	BUG_ON(!vma)

		pol = get_vma_policy(vma, addr)			if (!(pol->flags & MPOL_F_MOF))
		goto out;

	switch (pol->mode) {
	case MPOL_INTERLEAVE:

	BUG_O (add>f = vma->vet n 0;

	BUG_O (add<a, vma->vm_start))		tg		off = vma->vm_pgo;)		tg		off += (addr - vma->vm_start) >- PAGE_SHIl)		t*pu	nid n offset_il_node(pol, vmatg		oes);
		break {
	case MPOL_PREFERRED:
		if!(pol->flags & MPOL_F_LOCAL)

t*pu	nid = numa_node_id();
		else
	t*pu	nid p pol->v.preferred_node;
		breakk;
	case MPOL_BIND:
		/*
		 s_allsow biating tmfauipngla nosfe.
		 e us* current pag		iintcmpolint noe mase.
		 		el selecbit_m_ast * allowed noer, ig afe.
		 I If n* allowed nod,_e us* curren[!mispllbgn]ad.
		 */
		if(!node_isse=etu	nid, pol->v.node)))
			goto out;
		(void)first_zones_zonelisl,
	;
	nods_zonelise numa_node_idde, GFP_HIGHUS)ds,
		t(gfp_zone(GFP_HIGHUS)ds,
		&, pol->v.nodnd&	&zone);
t*pu	nid ? zone->noes;
		break {
	default:
		BUG();
	*/
	nM migratg the pagtoorwasin the noea whosCPU this iffereinino 
	/;

	if (pol->flags & MPOL_F_RONma) {
o*pu	nid f thu	n 0;

		if sh cou_= numa_migratvememor* curre,he pa, =etu	nidf th= cd)))
			goto out;}d;

	if= cu	ni!=n *pu	n)E:
		res  *pu	nm);
out:		mpol_cond_put(pol);

	return ret;
}/
static voiskudelete(/
strucs Sharvm_poli	*spgec strucskup_no.
(de)
{t	pr_debugdeletclond %lls:%lx\n =->d stan  ->g edp)	
b_er	cad& cyt(nd&e ->rootfp)	sku* fr(n)ut;
}/
static voiskua_nodenit(
	strucskup_no.
(odage, unsigned long star
					unsigned lon, end
	struct mempolicy *pgs)
{
	nocyd stard = sta;
{
	nocy
	end g end;
	nocym_poli	d  *p;k;
}

static strucskup_no.
sp n alle, unsigned long start
	unsigned long eLL,
				struct mempolicy *pgs)
{
	strucskup_no.
(;
{
	struct mempolicy
	np*pol;
new ka_mee_cac n alle
nee_cacdee(GFP_KERNEL);		if (gs)
		retur= NULL1;
	n *pol -	mpodup(t(pol)	
	if (IS_ERR(np(pola) {
ka_mee_cac * fr(
nee_cacden)ut)
		retur= NULL;}1;
	n *pew->flags |= MPOL_F_SHARp)	skua_nodenit(nmm, start, end, nt(pol);

	returnLL;
}

/* pllbg vma poli d_rance. */
static inl Sharvm_poliur pllbg(/
strucs Sharvm_poli	*spge, unsigned long stark,
	, 		unsigned lon, end
	strucskup_no.
(!new)
{
	strucskup_no.
(;
{
	strucskup_no.
(_		new = NULL;
	struct mempolicy-pmpol_new = NULL;d in		res 0t);
	g sta:L;spi_sk_loc&e ->unlofp)	len e uy look(/gfp, start, esk);
	/. TakcLherofm, ose policietain thA samd_rance. *
w whilenrr &&cyd star<n, esa) {
c strucrbup_no.
l_next rbup_ned& cyt(n 0;

	if cyd star>d = stary) {
	
	if cyg endd g edre
		skudelete(/pden)ut)

		else
		ncyd stard g end;;
	} ela) {
;
	/Olred policspanainina wngla_ned_rance. *

	
	if cyg en>n, esa) {
)
		if (_(!new)
)
			got, allon*neww)
)
y-pmpol_new *ncym_poli();&
		tonamiisse&-pmpol_ncy] ic int1et);
		skua_nodenit(n_(*new, end,cy, en = mpol_net);
		ncy
	end = sta;
{
		skuin.ust(/pdenol_net);
		nol_new = NULL;e
	g	mpol_new = NULL;e
			break;

		} else
		ncy
	end = sta;
{
		}
		if!p_nert)
			break;

	d rbub ent(n_ne,ic strucskup_no,nt n 0;});
	if (new)
skuin.ust(/pden_net);spi_sk_unloc&e ->unlofp)	
	res 0t);efeol_out:		if (mpol_new)

	mpol_pu= mpol_net);
	if ol_new)
ka_mee_cac * fr(
nee_cacden_, new);

	return ret
, allon*n:);spi_sk_unloc&e ->unlofp)	
	res = -ENOMEM;nol_new ka_mee_cac n alle
nee_cacdee(GFP_KERNEL);		if (ol_new)
		gotefeol_o;
	g	mpol_new ka_mee_cac n alles(poliee_cacde, GFP_KERNEL);
	if -(mpol_new)
		gotefeol_o;
			goto = sta;
;
}


 *
 
	mpol Sharvm_poliuenitdr uninitialitr shared policr for  no
		 @sp:vd r intht tr  notr shared poliht
 *m(!p:vd
	struct mempolict tr = sllby
 *
 * = sllof n-s NUL*m(!pgeurn  no'str shared policrb-h frts.
 Onab ent,se th* currenh tass hast] ifferencourn a n-s NUL*m(!pde*
 Ththim must brelea uddo an itde*
 Ththithie calleaint gen  no()ie casmnfinwawe cae us, GFP_KERNss.
 vc void	mpol Sharvm_poliuenit(/
strucs Sharvm_poli	*spgec struct mempolicy-pmpgs)
{
	inn ret
	e ->rootew RB_ROOT;;
		/*	*/
ck fre== m default mempolie. *
spi_sk_louenit(&e ->unlofp)t:		if (mpsa) {
c struct vm_area_strucp, vLL;{
	struct mempolicy
	nLL;{
	NODEMASK_SCRATCH(scratch);


	if (!scratch)
			gott_pum *pol{
		/ooai neutialito thtmpfhas u in *d int mempolie. *


	new = mpol_new (pol->mon = mpew->flaew&t mpew->w.user_nodemaz 0;

	if (IS_ERR(new))
			got* fr_(!scrat; /	 f  k_valtf nodemask intersectieD **/
		task_lock(current);

	res d	mpol_set_nodemask(new&t mpew->w.user_nodemask, scratch);
		task_unlock(current);

	if
	rch)
			gott_pun*neww)


/*_arcatc pseu-o vmn thatoaintased jus* the polic. *

t_messe&p, vma0ng, sizeoc struct vm_area_strudrt);
p, v.->vet es TEMASKIZEL{
	/empolic= vntere entirm_fil. *

t	mpol_ses Sharvm_poli(/pde&p, vma, new /	 addhis ieD **t_pun*n:w)

	mpol_pu, new{
;
	/e dr uninitiis ieD ** fr_(!scrat:L;{
	NODEMASK_SCRATCH_FREE(scratch)t_pum *p:w)

	mpol_pu= mp);;
	/e dr oufor conanins ietiosb d	mpl. *
}0;
}is->
	mpol_ses Sharvm_poli(/
strucs Sharvm_poli	*infork,
	, struct vm_area_struct *vma
	struct mempolicy
pmpgs)
{
	ing err;
	strucskup_no.
(!nnm = NULL;
	unsigned lonszff = voc_pag((!vma)

		pr_debugl_ses Sharvm_polind %nszf%lu %d %d %:%lx\n",
	= vma->vm_pgool,
		zma,	mpl?a,	mpol->mod:E-1nk.
o 	mpl?a,	mpol->flag:E-1nk.
o 	mpl?a,onodes_addn, pol->v.nodek)[0] : NUMA_NO_NODE);

	ifn(mpsa) {
(!nnm sp n alle= vma->vm_pgoo = vma->vm_pgof+		zma,	mpmt);

	if!(!new)
)		return -ENOMEM;});
	err = Sharvm_poliur pllbg(infor = vma->vm_pgoo = vma->vm_pgo+	zma,_net);
	if(!err &&(new)
sku* fr(n(new);
	return err;
}

 F freanl backind polics_stoMourn  no delete..
 vc void	mpo* fr_( Sharvm_poli(/
strucs Sharvm_poli	*pew)
{
	strucskup_no.
(;
{
	strucrbup_no.
l_ner);

	if (->root.rbup_nogs)
		retuLL;spi_sk_loc& ->unlofp)	l_next rbud)firc& ->rootfp)	w whilen_nera) {
(	d rbub ent(n_ne,ic strucskup_no,nt n 0;	l_next rbup_ned& cyt(n 0;
skudelete(pden)ut)
	}spi_sk_unloc& ->unlofp);
}

#ifdef CONFI: NUMBALANCING*/
static in_uenitdata = nubalareini_ vntrinoDE;/
static voi_uenitwe che_= nubalareini_enapable(void)
{		boo= nubalareini_m default = false;

	if (ISNOVABD(f CONFI: NUMBALANCINGLL_DEFAUIS