// SPDX-License-Identifier: GPL-2.0
/*
 *  mm/mprotect.c
 *
 *  (C) Copyright 1994 Linus Torvalds
 *  (C) Copyright 2002 Christoph Hellwig
 *
 *  Address space accounting code	<alan@lxorguk.ukuu.org.uk>
 *  (C) Copyright 2002 Red Hat Inc, All Rights Reserved
 */

#include <linux/pagewalk.h>
#include <linux/hugetlb.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/security.h>
#include <linux/mempolicy.h>
#include <linux/personality.h>
#include <linux/syscalls.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/mmu_notifier.h>
#include <linux/migrate.h>
#include <linux/perf_event.h>
#include <linux/pkeys.h>
#include <linux/ksm.h>
#include <linux/uaccess.h>
#include <linux/mm_inline.h>
#include <linux/pgtable.h>
#include <linux/vkeys.h>
#include <linux/vkey_map.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/vkeys.h>

#include "internal.h"

extern bool try_to_free_pmd_page(pmd_t *pmd);

static unsigned long change_pte_range(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long addr, unsigned long end, pgprot_t newprot,
		unsigned long cp_flags, bool is_vkm)
{
	pte_t *pte, oldpte;
	spinlock_t *ptl;
	unsigned long pages = 0;
	int target_node = NUMA_NO_NODE;
	bool dirty_accountable = cp_flags & MM_CP_DIRTY_ACCT;
	bool prot_numa = cp_flags & MM_CP_PROT_NUMA;
	bool uffd_wp = cp_flags & MM_CP_UFFD_WP;
	bool uffd_wp_resolve = cp_flags & MM_CP_UFFD_WP_RESOLVE;

	/*
	 * Can be called with only the mmap_lock for reading by
	 * prot_numa so we must check the pmd isn't constantly
	 * changing from under us from pmd_none to pmd_trans_huge
	 * and/or the other way around.
	 */
	if (pmd_trans_unstable(pmd))
		return 0;

	/*
	 * The pmd points to a regular pte so the pmd can't change
	 * from under us even if the mmap_lock is only hold for
	 * reading.
	 */
	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);

	/* Get target node for single threaded private VMAs */
	if (prot_numa && !(vma->vm_flags & VM_SHARED) &&
	    atomic_read(&vma->vm_mm->mm_users) == 1)
		target_node = numa_node_id();

	flush_tlb_batched_pending(vma->vm_mm);
	arch_enter_lazy_mmu_mode();
	do {
		oldpte = *pte;
		if (pte_present(oldpte)) {
			pte_t ptent;
			bool preserve_write = prot_numa && pte_write(oldpte);

			/*
			 * Avoid trapping faults against the zero or KSM
			 * pages. See similar comment in change_huge_pmd.
			 */
			if (prot_numa) {
				struct page *page;

				/* Avoid TLB flush if possible */
				if (pte_protnone(oldpte))
					continue;

				page = vm_normal_page(vma, addr, oldpte);
				if (!page || PageKsm(page))
					continue;

				/* Also skip shared copy-on-write pages */
				if (is_cow_mapping(vma->vm_flags) &&
				    page_count(page) != 1)
					continue;

				/*
				 * While migration can move some dirty pages,
				 * it cannot move them all from MIGRATE_ASYNC
				 * context.
				 */
				if (page_is_file_lru(page) && PageDirty(page))
					continue;

				/*
				 * Don't mess with PTEs if page is already on the node
				 * a single-threaded process is running on.
				 */
				if (target_node == page_to_nid(page))
					continue;
			}

			oldpte = ptep_modify_prot_start(vma, addr, pte);
			ptent = pte_modify(oldpte, newprot);
			if (preserve_write)
				ptent = pte_mk_savedwrite(ptent);

			if (uffd_wp) {
				ptent = pte_wrprotect(ptent);
				ptent = pte_mkuffd_wp(ptent);
			} else if (uffd_wp_resolve) {
				/*
				 * Leave the write bit to be handled
				 * by PF interrupt handler, then
				 * things like COW could be properly
				 * handled.
				 */
				ptent = pte_clear_uffd_wp(ptent);
			}

			/* Avoid taking write faults for known dirty pages */
			if (dirty_accountable && pte_dirty(ptent) &&
					(pte_soft_dirty(ptent) ||
					 !(vma->vm_flags & VM_SOFTDIRTY))) {
				ptent = pte_mkwrite(ptent);
			}
			ptep_modify_prot_commit(vma, addr, pte, oldpte, ptent);
			pages++;
		} else if (is_swap_pte(oldpte)) {
			swp_entry_t entry = pte_to_swp_entry(oldpte);
			pte_t newpte;

			if (is_writable_migration_entry(entry)) {
				/*
				 * A protection check is difficult so
				 * just be safe and disable write
				 */
				entry = make_readable_migration_entry(
							swp_offset(entry));
				newpte = swp_entry_to_pte(entry);
				if (pte_swp_soft_dirty(oldpte))
					newpte = pte_swp_mksoft_dirty(newpte);
				if (pte_swp_uffd_wp(oldpte))
					newpte = pte_swp_mkuffd_wp(newpte);
			} else if (is_writable_device_private_entry(entry)) {
				/*
				 * We do not preserve soft-dirtiness. See
				 * copy_one_pte() for explanation.
				 */
				entry = make_readable_device_private_entry(
							swp_offset(entry));
				newpte = swp_entry_to_pte(entry);
				if (pte_swp_uffd_wp(oldpte))
					newpte = pte_swp_mkuffd_wp(newpte);
			} else if (is_writable_device_exclusive_entry(entry)) {
				entry = make_readable_device_exclusive_entry(
							swp_offset(entry));
				newpte = swp_entry_to_pte(entry);
				if (pte_swp_soft_dirty(oldpte))
					newpte = pte_swp_mksoft_dirty(newpte);
				if (pte_swp_uffd_wp(oldpte))
					newpte = pte_swp_mkuffd_wp(newpte);
			} else {
				newpte = oldpte;
			}

			if (uffd_wp)
				newpte = pte_swp_mkuffd_wp(newpte);
			else if (uffd_wp_resolve)
				newpte = pte_swp_clear_uffd_wp(newpte);

			if (!pte_same(oldpte, newpte)) {
				set_pte_at(vma->vm_mm, addr, pte, newpte);
				pages++;
			}
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);
	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(pte - 1, ptl);

	return pages;
}

/*
 * Used when setting automatic NUMA hinting protection where it is
 * critical that a numa hinting PMD is not confused with a bad PMD.
 */
static inline int pmd_none_or_clear_bad_unless_trans_huge(pmd_t *pmd)
{
	pmd_t pmdval = pmd_read_atomic(pmd);

	/* See pmd_none_or_trans_huge_or_clear_bad for info on barrier */
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	barrier();
#endif

	if (pmd_none(pmdval))
		return 1;
	if (pmd_trans_huge(pmdval))
		return 0;
	if (unlikely(pmd_bad(pmdval))) {
		pmd_clear_bad(pmd);
		return 1;
	}

	return 0;
}

static inline unsigned long change_pmd_range(struct vm_area_struct *vma,
		pud_t *pud, unsigned long addr, unsigned long end,
		pgprot_t newprot, unsigned long cp_flags, bool is_vkm)
{
	pmd_t *pmd;
	unsigned long next;
	unsigned long pages = 0;
	unsigned long nr_huge_updates = 0;
	struct mmu_notifier_range range;

	range.start = 0;

	pmd = pmd_offset(pud, addr);
	do {
		unsigned long this_pages;

		next = pmd_addr_end(addr, end);

		/*
		 * Automatic NUMA balancing walks the tables with mmap_lock
		 * held for read. It's possible a parallel update to occur
		 * between pmd_trans_huge() and a pmd_none_or_clear_bad()
		 * check leading to a false positive and clearing.
		 * Hence, it's necessary to atomically read the PMD value
		 * for all the checks.
		 */
		if (!is_swap_pmd(*pmd) && !pmd_devmap(*pmd) &&
		     pmd_none_or_clear_bad_unless_trans_huge(pmd))
			goto next;

		/* invoke the mmu notifier if the pmd is populated */
		if (!is_vkm) {	/* The subscription callback should do the operation to all vkms. */
			if (!range.start) {
				mmu_notifier_range_init(&range,
					MMU_NOTIFY_PROTECTION_VMA, 0,
					vma, vma->vm_mm, addr, end);
				mmu_notifier_invalidate_range_start(&range);
			}
		}

#ifdef CONFIG_HAS_VPK
		/* Here, we can use the value of main_vkm because the xo-vkey is always mapped to xo-pkey. */
		if (mm_mprotect_vkey(vma, -1))
			vkm_pmd_populate(vma->vm_mm, pmd, pmd_pgtable(*pmd), -1);
#endif

		if (is_swap_pmd(*pmd) || pmd_trans_huge(*pmd) || pmd_devmap(*pmd)) {
			if (next - addr != HPAGE_PMD_SIZE) {
				if (is_vkm)	{	/* Just invalidate the pmd for later page fault */
					spinlock_t *ptl;
					bool ret;
					ptl = pmd_lock(vma->vm_mm, pmd);
					ret = try_to_free_pmd_page(pmd);
					spin_unlock(ptl);
					if (!ret)
						printk(KERN_ERR "[%s] BUG: failed to free pmd...\n", __func__);
				} else
					__split_huge_pmd(vma, pmd, addr, false, NULL);
			} else {
				int nr_ptes = change_huge_pmd(vma, pmd, addr,
							      newprot, cp_flags);

				if (nr_ptes) {
					if (nr_ptes == HPAGE_PMD_NR) {
						pages += HPAGE_PMD_NR;
						nr_huge_updates++;
					}

					/* huge pmd was handled */
					goto next;
				}
			}
			/* fall through, the trans huge pmd just split */
		}
		this_pages = change_pte_range(vma, pmd, addr, next, newprot,
					      cp_flags, is_vkm);
		pages += this_pages;
next:
		if (!is_vkm)
			cond_resched();
	} while (pmd++, addr = next, addr != end);

	if (!is_vkm && range.start)
		mmu_notifier_invalidate_range_end(&range);

	if (nr_huge_updates)
		count_vm_numa_events(NUMA_HUGE_PTE_UPDATES, nr_huge_updates);
	return pages;
}

static inline unsigned long change_pud_range(struct vm_area_struct *vma,
		p4d_t *p4d, unsigned long addr, unsigned long end,
		pgprot_t newprot, unsigned long cp_flags, bool is_vkm)
{
	pud_t *pud;
	unsigned long next;
	unsigned long pages = 0;

	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		pages += change_pmd_range(vma, pud, addr, next, newprot,
					  cp_flags, is_vkm);
	} while (pud++, addr = next, addr != end);

	return pages;
}

static inline unsigned long change_p4d_range(struct vm_area_struct *vma,
		pgd_t *pgd, unsigned long addr, unsigned long end,
		pgprot_t newprot, unsigned long cp_flags, bool is_vkm)
{
	p4d_t *p4d;
	unsigned long next;
	unsigned long pages = 0;

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none_or_clear_bad(p4d))
			continue;
		pages += change_pud_range(vma, p4d, addr, next, newprot,
					  cp_flags, is_vkm);
	} while (p4d++, addr = next, addr != end);

	return pages;
}

static unsigned long change_protection_range(struct vm_area_struct *vma,
		unsigned long addr, unsigned long end, pgprot_t newprot,
		unsigned long cp_flags)
{
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	unsigned long next;
	unsigned long start = addr;
	unsigned long pages = 0;
#ifdef CONFIG_HAS_VPK
	struct list_head *pos;
	struct vkey_map_struct *vkm;
	unsigned long vkm_addr;
#endif

	BUG_ON(addr >= end);
	flush_cache_range(vma, addr, end);	/* x86 does nothing */
	inc_tlb_flush_pending(mm);
#ifdef CONFIG_HAS_VPK
	/* All PTEs must be present in the main vkm space, so no need to count pages. */
	/* if (!list_empty(&mm->vkm_chain))
		printk("[%s] begin changing PTEs of vkms [%lx, %lx)].\n", __func__, addr, end); */
	list_for_each(pos, &mm->vkm_chain) {
		vkm = list_entry(pos, struct vkey_map_struct, vkm_chain);
		if (vkm->pgd == mm->pgd)
			continue;
		vkm_addr = addr;
		pgd = vkm->pgd + pgd_index(vkm_addr);
		do {
			next = pgd_addr_end(vkm_addr, end);
			if (pgd_none_or_clear_bad(pgd))
				continue;
			change_p4d_range(vma, pgd, vkm_addr, next, newprot,
					  cp_flags, true);
		} while (pgd++, vkm_addr = next, vkm_addr != end);
		/* printk("[%s] a new vkm's PTEs are changed.\n", __func__); */
	}
	/* if (!list_empty(&mm->vkm_chain))
		printk("[%s] end changing PTEs of vkms.\n", __func__); */
#endif
	pgd = pgd_offset(mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		pages += change_p4d_range(vma, pgd, addr, next, newprot,
					  cp_flags, false);
	} while (pgd++, addr = next, addr != end);

	/* Only flush the TLB if we actually modified any entries: */
	if (pages)
		flush_tlb_range(vma, start, end);	/* flush tlb ranges of all vkms */
	dec_tlb_flush_pending(mm);

	return pages;
}

unsigned long change_protection(struct vm_area_struct *vma, unsigned long start,
		       unsigned long end, pgprot_t newprot,
		       unsigned long cp_flags)
{
	unsigned long pages;

	BUG_ON((cp_flags & MM_CP_UFFD_WP_ALL) == MM_CP_UFFD_WP_ALL);

	if (is_vm_hugetlb_page(vma))
		pages = hugetlb_change_protection(vma, start, end, newprot);
	else
		pages = change_protection_range(vma, start, end, newprot,
						cp_flags);

	return pages;
}

static int prot_none_pte_entry(pte_t *pte, unsigned long addr,
			       unsigned long next, struct mm_walk *walk)
{
	return pfn_modify_allowed(pte_pfn(*pte), *(pgprot_t *)(walk->private)) ?
		0 : -EACCES;
}

static int prot_none_hugetlb_entry(pte_t *pte, unsigned long hmask,
				   unsigned long addr, unsigned long next,
				   struct mm_walk *walk)
{
	return pfn_modify_allowed(pte_pfn(*pte), *(pgprot_t *)(walk->private)) ?
		0 : -EACCES;
}

static int prot_none_test(unsigned long addr, unsigned long next,
			  struct mm_walk *walk)
{
	return 0;
}

static const struct mm_walk_ops prot_none_walk_ops = {
	.pte_entry		= prot_none_pte_entry,
	.hugetlb_entry		= prot_none_hugetlb_entry,
	.test_walk		= prot_none_test,
};

int
mprotect_fixup(struct vm_area_struct *vma, struct vm_area_struct **pprev,
	unsigned long start, unsigned long end, unsigned long newflags, unsigned long newvkey)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long oldflags = vma->vm_flags;
	long nrpages = (end - start) >> PAGE_SHIFT;
	unsigned long charged = 0;
	pgoff_t pgoff;
	int error;
	int dirty_accountable = 0;
	int oldvkey = 0;
#ifdef CONFIG_HAS_VPK
	oldvkey = mm_mprotect_vkey(vma, -1);
#endif

	if (newflags == oldflags && newvkey == oldvkey) {
		*pprev = vma;
		return 0;
	}

	/*
	 * Do PROT_NONE PFN permission checks here when we can still
	 * bail out without undoing a lot of state. This is a rather
	 * uncommon case, so doesn't need to be very optimized.
	 */
	if (arch_has_pfn_modify_check() &&
	    (vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)) &&
	    (newflags & VM_ACCESS_FLAGS) == 0) {
		pgprot_t new_pgprot = vm_get_page_prot(newflags);

		/* This has a real impact on PTEs, so vkeys does not affect new_pgprot. */
		error = walk_page_range(current->mm, start, end,
				&prot_none_walk_ops, &new_pgprot);
		if (error)
			return error;
	}

	/*
	 * If we make a private mapping writable we increase our commit;
	 * but (without finer accounting) cannot reduce our commit if we
	 * make it unwritable again. hugetlb mapping were accounted for
	 * even if read-only so there is no need to account for them here
	 */
	if (newflags & VM_WRITE) {
		/* Check space limits when area turns into data. */
		if (!may_expand_vm(mm, newflags, nrpages) &&
				may_expand_vm(mm, oldflags, nrpages))
			return -ENOMEM;
		if (!(oldflags & (VM_ACCOUNT|VM_WRITE|VM_HUGETLB|
						VM_SHARED|VM_NORESERVE))) {
			charged = nrpages;
			if (security_vm_enough_memory_mm(mm, charged))
				return -ENOMEM;
			newflags |= VM_ACCOUNT;
		}
	}

	/*
	 * First try to merge with previous and/or next vma.
	 */
	pgoff = vma->vm_pgoff + ((start - vma->vm_start) >> PAGE_SHIFT);
	*pprev = vma_merge(mm, *pprev, start, end, newflags, newvkey,
			   vma->anon_vma, vma->vm_file, pgoff, vma_policy(vma),
			   vma->vm_userfaultfd_ctx, anon_vma_name(vma));
	if (*pprev) {
		vma = *pprev;
		VM_WARN_ON((vma->vm_flags ^ newflags) & ~VM_SOFTDIRTY);
		goto success;
	}

	*pprev = vma;

	if (start != vma->vm_start) {
		error = split_vma(mm, vma, start, 1);
		if (error)
			goto fail;
	}

	if (end != vma->vm_end) {
		error = split_vma(mm, vma, end, 0);
		if (error)
			goto fail;
	}

success:
	/*
	 * vm_flags and vm_page_prot are protected by the mmap_lock
	 * held in write mode.
	 */
#ifdef CONFIG_HAS_VPK
	vma->vm_vkey = newvkey;
#endif
	vma->vm_flags = newflags;
	dirty_accountable = vma_wants_writenotify(vma, vma->vm_page_prot);
	vma_set_page_prot(vma);

	/* Change protection bits and flush related TLB. */
	change_protection(vma, start, end, vma->vm_page_prot,
			  dirty_accountable ? MM_CP_DIRTY_ACCT : 0);

	/* link the vma with the table if vkey is not -1 or 0 */
#ifdef CONFIG_HAS_VPK
	list_del_init(&vma->vkey_chain);
	if (newvkey) {
		struct vkey_kgd_struct *kgd = current->mm->vkey.kgd;
		int kgd_oft = vkey_kgd_offset(newvkey);
		int kte_oft = vkey_kte_offset(newvkey);
		if (kgd && kgd->ktes[kgd_oft])
			list_add(&vma->vkey_chain, &(kgd->ktes[kgd_oft]->vkey_vma_heads[kte_oft]));
		else
			printk(KERN_ERR "[%s] vkey table is not initialized before vkey_mprotect...\n", __func__);
	}
#endif

	/*
	 * Private VM_LOCKED VMA becoming writable: trigger COW to avoid major
	 * fault on access.
	 */
	if ((oldflags & (VM_WRITE | VM_SHARED | VM_LOCKED)) == VM_LOCKED &&
			(newflags & VM_WRITE)) {
		populate_vma_page_range(vma, start, end, NULL);
	}

	vm_stat_account(mm, oldflags, -nrpages);
	vm_stat_account(mm, newflags, nrpages);
	perf_event_mmap(vma);
	return 0;

fail:
	vm_unacct_memory(charged);
	return error;
}

/*
 * pkey==-1 when doing a legacy mprotect()
 */
static int do_mprotect_pkey(unsigned long start, size_t len,
		unsigned long prot, int pkey, int vkey, bool has_locked)
{
	unsigned long nstart, end, tmp, reqprot;
	struct vm_area_struct *vma, *prev;
	int error = -EINVAL;
	const int grows = prot & (PROT_GROWSDOWN|PROT_GROWSUP);
	const bool rier = (current->personality & READ_IMPLIES_EXEC) &&
				(prot & PROT_READ);

	start = untagged_addr(start);

	prot &= ~(PROT_GROWSDOWN|PROT_GROWSUP);
	if (grows == (PROT_GROWSDOWN|PROT_GROWSUP)) /* can't be both */
		return -EINVAL;

	if (start & ~PAGE_MASK)
		return -EINVAL;
	if (!len)
		return 0;
	len = PAGE_ALIGN(len);
	end = start + len;
	if (end <= start)
		return -ENOMEM;
	if (!arch_validate_prot(prot, start))
		return -EINVAL;

	reqprot = prot;

	if (!has_locked && mmap_write_lock_killable(current->mm))
		return -EINTR;

	/*
	 * If userspace did not allocate the pkey (or vkey), do not let
	 * them use it here.
	 * When the config does not support, this fuunction ends here.
	 */
	error = -EINVAL;
	if ((vkey != -1) && !mm_vkey_is_allocated(current->mm, vkey))
		goto out;
	if ((pkey != -1) && !mm_pkey_is_allocated(current->mm, pkey)) {
		if (vkey == -1 && pkey != execute_only_pkey(current->mm))
			goto out;
	}

	vma = find_vma(current->mm, start);
	error = -ENOMEM;
	if (!vma)
		goto out;

	if (unlikely(grows & PROT_GROWSDOWN)) {
		if (vma->vm_start >= end)
			goto out;
		start = vma->vm_start;
		error = -EINVAL;
		if (!(vma->vm_flags & VM_GROWSDOWN))
			goto out;
	} else {
		if (vma->vm_start > start)
			goto out;
		if (unlikely(grows & PROT_GROWSUP)) {
			end = vma->vm_end;
			error = -EINVAL;
			if (!(vma->vm_flags & VM_GROWSUP))
				goto out;
		}
	}

	if (start > vma->vm_start)
		prev = vma;
	else
		prev = vma->vm_prev;

	for (nstart = start ; ; ) {
		unsigned long mask_off_old_flags;
		unsigned long newflags;
		int new_vma_pkey;
		int new_vma_vkey;

		/* Here we know that vma->vm_start <= nstart < vma->vm_end. */

		/* Does the application expect PROT_READ to imply PROT_EXEC */
		if (rier && (vma->vm_flags & VM_MAYEXEC))
			prot |= PROT_EXEC;

		/*
		 * Each mprotect() call explicitly passes r/w/x permissions.
		 * If a permission is not passed to mprotect(), it must be
		 * cleared from the VMA.
		 */
		mask_off_old_flags = VM_READ | VM_WRITE | VM_EXEC |
					VM_FLAGS_CLEAR;

		new_vma_vkey = mm_mprotect_vkey(vma, vkey);
		new_vma_pkey = arch_override_mprotect_pkey(vma, prot, pkey);
		newflags = calc_vm_prot_bits(prot, new_vma_pkey);
		newflags |= (vma->vm_flags & ~mask_off_old_flags);
		/* Here, we do not merge calc_vm_vkey_bits with calc_vm_prot_bits
		 * for 2 reasons.
		 * First, calc_vm_prot_bits(prot, pkey) is invoked, with
		 * pkey a non-zero value only in mprotect.c and mmap.c. This means other
		 * call sites want the pkey to be zero, which is default. Also, the default
		 * vkey is also zero no matter calc_vm_vkey_bits(vkey) is invoked or not.
		 * Second, mmap.c cannot really deal with user-assigned pkeys, only zero
		 * or the XO-pkey might be the value.
		 * Hence, calc_vm_vkey_bits(vkey) has to be called only in this function.
		 */

		/* newflags >> 4 shift VM_MAY% in place of VM_% */
		if ((newflags & ~(newflags >> 4)) & VM_ACCESS_FLAGS) {
			error = -EACCES;
			goto out;
		}

		/* Allow architectures to sanity-check the new flags */
		/* For X86, this always returns true, other archs are not affected by the vkey */
		if (!arch_validate_flags(newflags)) {
			error = -EINVAL;
			goto out;
		}

		error = security_file_mprotect(vma, reqprot, prot);
		if (error)
			goto out;

		tmp = vma->vm_end;
		if (tmp > end)
			tmp = end;

		if (vma->vm_ops && vma->vm_ops->mprotect) {
			error = vma->vm_ops->mprotect(vma, nstart, tmp, newflags);	/* This is always NULL in this version of kernel. */
			if (error)
				goto out;
		}

		error = mprotect_fixup(vma, &prev, nstart, tmp, newflags, calc_vm_vkey_bits(new_vma_vkey));
		if (error)
			goto out;

		nstart = tmp;

		if (nstart < prev->vm_end)
			nstart = prev->vm_end;
		if (nstart >= end) {
			goto out;
		}

		vma = prev->vm_next;
		if (!vma || vma->vm_start != nstart) {
			error = -ENOMEM;
			goto out;
		}
		prot = reqprot;
	}
out:
	if (!has_locked)
		mmap_write_unlock(current->mm);
	return error;
}

#ifdef CONFIG_HAS_VPK
extern void switch_mm_fast(struct mm_struct *mm, struct task_struct *tsk);
extern void vkey_print_error_message(unsigned long address);

static inline vm_fault_t activate_vkey(int vkey)
{
	struct vkey_map_struct *orig_vkm, *new_vkm, *real_orig_vkm;
	bool stay_still, alloc_new, vkm_single, already_mapped;
	int evicted_vkey;
	int xok;
	int vkru_perm;
	int i, j, idx;
	int conflict_vkey, conflict_j;
	int perm[MAX_ACTIVE_VKEYS];

	i = j = 0;
	xok = get_execute_only_pkey(current->mm);
	idx = ((vkey - 1) / MAX_ACTIVE_VKEYS) % current->vkm_nas;
	already_mapped = false;	/* might be triggered by cross pgd page faults */
	if (!vkey)
		return 0;

	/* alloc metadata according to index */
	if (!current->mapped_vkeys) {
		current->vkm_arr = tsk_vkm_arr_alloc();
		current->mvk_arr = tsk_mvk_arr_alloc();
		if (!current->vkm_arr || !current->mvk_arr) {
			printk(KERN_ERR "[%s] no memory for per thread mapped metadata\n", __func__);
			return VM_FAULT_SIGSEGV;
		}
	}
	if (!current->mvk_arr[idx]) {
		current->mvk_arr[idx] = tsk_mvk_alloc();
		if (!current->mvk_arr[idx]) {
			printk(KERN_ERR "[%s] no memory for per thread mapped vkeys\n", __func__);
			return VM_FAULT_SIGSEGV;
		}
	}
	real_orig_vkm = current->vkm;
	if (current->vkm != current->vkm_arr[idx])
		current->mapped_vkeys->pkru = mm_vkm_pkru_get();
	current->vkm = current->vkm_arr[idx];	/* might be NULL ptr */
	current->mapped_vkeys = current->mvk_arr[idx];

	conflict_vkey = current->vkm ? current->vkm->pkey_vkey[(vkey - 1) % MAX_ACTIVE_VKEYS + 1] : 0;
	for (i = 0; i < MAX_ACTIVE_VKEYS; i++) {
		if (!current->mapped_vkeys->ts[i]) {
			current->mapped_vkeys->ts[i] = MAX_ACTIVE_VKEYS - 1;
			j = i;
		} else
			current->mapped_vkeys->ts[i]--;
		if (current->mapped_vkeys->map[i] == conflict_vkey)
			conflict_j = i;
		if (current->mapped_vkeys->map[i] == vkey && vkey) {
			j = conflict_j = i;
			already_mapped = true;
			break;
		}
	}

	if (current->mapped_vkeys->map[j]) {	/* try the same pkey */
#ifdef CONFIG_HAS_VPK_USER_VKRU
		if (vkey_get_vkru_permission(current, conflict_vkey) == VKEY_AD)	/* !(accessible or pinned) */
#else
		if (vkey_get_vkrk_permission(current->vkrk, conflict_vkey) == VKEY_AD)
#endif
			j = conflict_j;
	}	/* lru evict */
	evicted_vkey = current->mapped_vkeys->map[j];
	current->mapped_vkeys->map[j] = vkey;

	/* Try stage */
	new_vkm = orig_vkm = current->vkm;
#ifdef CONFIG_HAS_VPK_USER_VKRU
	if (current->vkru)
		vkru_perm = vkey_get_vkru_permission(current, vkey);
	else
		vkru_perm = VKEY_AD;
#else
	if (current->vkrk)
		vkru_perm = vkey_get_vkrk_permission(current->vkrk, vkey);
	else
		vkru_perm = VKEY_AD;
#endif
	/*
	 * If the thread overflows, then the corresponding vkm has only 1 thread and cannot
	 * be the migration dest of any other thread. So, no lock is needed if the system
	 * works fine.
	 * Otherwise, more than one threads may modify the vkm. To avoid TOCTOU inconsistency
	 * caused by race condition, we use mmap lock for slower but safer implementation.
	 */
	alloc_new = false;
	if (!already_mapped) {
		if (orig_vkm)
			spin_lock(&orig_vkm->slock);
		vkm_single = orig_vkm ? (orig_vkm->nr_thread == 1) : false;
		if (!vkm_single) {
			stay_still = false;
			if (orig_vkm)	/* try self first, stay_still when succeed */
				stay_still = mm_vkm_can_add(orig_vkm, current->mapped_vkeys->map, MAX_ACTIVE_VKEYS);
			if (!stay_still) {	/* try existing vkms, !alloc_new when succeed */
				for (i = 0; i < MAX_ACTIVE_VKEYS; i++)
					perm[i] = VKEY_AD;
				if (orig_vkm) {
					mm_vkm_del(orig_vkm, current->mm, current->mapped_vkeys->map, MAX_ACTIVE_VKEYS, perm, evicted_vkey);
					preempt_disable_notrace();
					arch_cpumask_clear_vkm(smp_processor_id(), vkm_cpumask(orig_vkm), orig_vkm);
					preempt_enable_no_resched_notrace();
				}
				perm[j] = vkru_perm;
				alloc_new = true;
				read_lock(&current->mm->vkey_lock);
				if (!evicted_vkey) {
					struct list_head *pos;
					list_for_each(pos, &current->mm->vkm_chain) {
						struct vkey_map_struct *entry_vkm;
						bool try_entry = true;
						entry_vkm = list_entry(pos, struct vkey_map_struct, vkm_chain);
						for (i = 0; i < current->vkm_nas; i++) /* no need to lock */
							if (entry_vkm == current->vkm_arr[i])
								try_entry = false;
						if (try_entry) {
							if (!spin_trylock(&entry_vkm->slock))
								continue;
							if (mm_vkm_can_add(entry_vkm, current->mapped_vkeys->map, MAX_ACTIVE_VKEYS)) {
								new_vkm = entry_vkm;
								alloc_new = false;
								break;
							} else
								spin_unlock(&entry_vkm->slock);
						}
					}
				}
				if (alloc_new) {
					read_unlock(&current->mm->vkey_lock);
					/* may cause extra vkm alloc but no harm done */
					if (orig_vkm)
						spin_unlock(&orig_vkm->slock);
					new_vkm = mm_vkm_alloc_init(current->mm);
					if (!new_vkm) {
						current->mapped_vkeys->map[j] = evicted_vkey;
						current->mapped_vkeys->ts[j] = -1;
						for (j = 0; j < MAX_ACTIVE_VKEYS; j++)
							current->mapped_vkeys->ts[j]++;
						if (orig_vkm)
							spin_unlock(&orig_vkm->slock);
						printk(KERN_ERR "[%s] no memory for vkey space\n", __func__);
						return VM_FAULT_SIGSEGV;
					}
				} else
					read_unlock(&current->mm->vkey_lock);
			}
		}

		/* Decide stage */
		if (new_vkm == orig_vkm)
			current->mapped_vkeys->pmap[j] = mm_vkm_add_vkey(new_vkm, vkey, evicted_vkey, xok, vkru_perm);	/* the mapped pkey */
		else {
			preempt_disable_notrace();
			cpumask_set_cpu(smp_processor_id(), vkm_cpumask(new_vkm));
			preempt_enable_no_resched_notrace();
			mm_vkm_add(new_vkm, current->mapped_vkeys, MAX_ACTIVE_VKEYS, xok, perm);
		}

		current->vkm = current->vkm_arr[idx] = new_vkm;
#ifdef CONFIG_HAS_VPK_USER_VKRU
		preempt_disable_notrace();
		copy_vktramp_map(smp_processor_id(), new_vkm, current->mapped_vkeys->map, MAX_ACTIVE_VKEYS);
		preempt_enable_no_resched_notrace();
#endif

		if (orig_vkm && !alloc_new)
			spin_unlock(&orig_vkm->slock);

		if (new_vkm != orig_vkm) {
			if (alloc_new) {
				write_lock(&current->mm->vkey_lock);
				if (current->mm->pgd == new_vkm->pgd)
					current->mm->main_vkm = new_vkm;
				list_add(&new_vkm->vkm_chain, &current->mm->vkm_chain);
				write_unlock(&current->mm->vkey_lock);
			} else if (new_vkm)
				spin_unlock(&new_vkm->slock);
		}
	} else	/* besides save, restore is also needed */
#ifdef CONFIG_HAS_VPK_USER_VKRU
	{
		u32 target_pkru = current->mapped_vkeys->pkru;
		int perm_xok = mm_vkm_pkru_get() & 0x0000000c;
		target_pkru &= 0xfffffff3;
		target_pkru |= perm_xok;
		wrpkru((target_pkru & (~(0x3 <<
			(current->mapped_vkeys->pmap[j] << 1)))) |
			(vkru_perm << (current->mapped_vkeys->pmap[j] << 1)));
	}
#else
		set_domain((current->mapped_vkeys->pkru &
			(~domain_mask(current->mapped_vkeys->pmap[j]))) |
			domain_val(current->mapped_vkeys->pmap[j], vkru_perm));
#endif

	if (new_vkm != real_orig_vkm) {
		/* eager context switch as exec */
		mb();	/* local mfence instruction */
		local_irq_disable();
		if (!IS_ENABLED(CONFIG_ARCH_WANT_IRQS_OFF_ACTIVATE_MM))
			local_irq_enable();
#if defined(CONFIG_X86) || defined(CONFIG_X86_64)
		paravirt_activate_mm(current->active_mm, current->active_mm);
#endif
		switch_mm(current->active_mm, current->active_mm, current);
		if (IS_ENABLED(CONFIG_ARCH_WANT_IRQS_OFF_ACTIVATE_MM))
			local_irq_enable();
	}

	// vkey_print_error_message(0);

	return 0;
}

vm_fault_t do_vkey_activate(int vkey)
{
	return activate_vkey(vkey);
}
#endif

SYSCALL_DEFINE3(mprotect, unsigned long, start, size_t, len,
		unsigned long, prot)
{
	return do_mprotect_pkey(start, len, prot, -1, -1, false);
}

#ifdef CONFIG_ARCH_HAS_PKEYS

SYSCALL_DEFINE4(pkey_mprotect, unsigned long, start, size_t, len,
		unsigned long, prot, int, pkey)
{
	return do_mprotect_pkey(start, len, prot, pkey, -1, false);
}

SYSCALL_DEFINE2(pkey_alloc, unsigned long, flags, unsigned long, init_val)
{
	int pkey;
	int ret;

	/* No flags supported yet. */
	if (flags)
		return -EINVAL;
	/* check for unsupported init values */
	if (init_val & ~PKEY_ACCESS_MASK)
		return -EINVAL;

	mmap_write_lock(current->mm);
	pkey = mm_pkey_alloc(current->mm);

	ret = -ENOSPC;
	if (pkey == -1)
		goto out;

	ret = arch_set_user_pkey_access(current, pkey, init_val);
	if (ret) {
		mm_pkey_free(current->mm, pkey);
		goto out;
	}
	ret = pkey;
out:
	mmap_write_unlock(current->mm);
	return ret;
}

SYSCALL_DEFINE1(pkey_free, int, pkey)
{
	int ret;

	mmap_write_lock(current->mm);
	ret = mm_pkey_free(current->mm, pkey);
	mmap_write_unlock(current->mm);

	/*
	 * We could provide warnings or errors if any VMA still
	 * has the pkey set here.
	 */
	return ret;
}

#endif /* CONFIG_ARCH_HAS_PKEYS */


#ifdef CONFIG_HAS_VPK

#ifdef CONFIG_HAS_VPK_USER_VKRU
extern struct vkey_per_cpu_cl *vktramp;
SYSCALL_DEFINE2(vkey_reg_lib, unsigned long, laddr, unsigned long, taddr)
{
	int ret;
	struct vm_area_struct *vma;

	mmap_write_lock(current->mm);
	ret = -EINVAL;

	if (current->mm->lvkru_uaddr || current->mm->vktramp_uaddr) {
		printk(KERN_ERR "The trusted memory in userspace has been set and locked...\n");
		/* This means an attacker, should we terminates gracefully? */
		goto fail;
	}

	vma = find_vma(current->mm, laddr);
	if (!vma) {
		printk(KERN_ERR "The trusted libvkeys is not found...\n");
		goto fail;
	}
	if (lvkru_mmap_lock(vma, laddr & PAGE_MASK)) {
		printk(KERN_ERR "The libvkeys vkru failed map...\n");
		goto fail;
	}
	current->mm->lvkru_uaddr = laddr;

	vma = find_vma(current->mm, taddr);
	if (!vma) {
		printk(KERN_ERR "The trusted trampoline is not found...\n");
		goto fail;
	}
	// FIXME: [VDom] check vm_flags
	if (vktramp_mmap_lock(vma)) {
		printk(KERN_ERR "The trusted trampoline failed map...\n");
		goto fail;
	}
	current->mm->vktramp_uaddr = taddr;

	for (vma = current->mm->mmap; vma; vma = vma->vm_next)
		if ((vma->vm_flags & PROT_EXEC) &&
				vma->vm_file && vma->vm_file->f_path.dentry &&
				(strcmp(vma->vm_file->f_path.dentry->d_iname, "libvkeys.so") == 0 ||
				strcmp(vma->vm_file->f_path.dentry->d_iname, "libvkeyss.so") == 0))
			break;
	if (!vma) {
		printk(KERN_ERR "The trusted libvkeys code section is not found...\n");
		goto fail;
	}
	current->mm->lvkey_code_vma = vma;

	ret = 0;
	goto out;
fail:
	current->mm->lvkru_kaddr = 0;
	current->mm->lvkru_uaddr = 0;
	current->mm->vktramp_uaddr = 0;
	current->mm->lvkey_code_vma = NULL;
out:
	mmap_write_unlock(current->mm);
	return ret;
}

SYSCALL_DEFINE2(vkey_reg_vkru, unsigned long, addr, unsigned int, nas)
{
	unsigned long *vktramp_map_base;
	int i;

	if (nas > 0 && nas <= MAX_ADDR_SPACE_PER_THREAD && current->vkm_nas <= nas)
		current->vkm_nas = nas;
	else
		return -EINVAL;

	if (current->vkru && addr != current->vkru) {
		if (addr != 0)
			printk(KERN_ERR "The vkru of the thread set and locked...\n");
		/* If the addr is poisoned, it cannot be realloced */
		current->vkru = 0;
		preempt_disable_notrace();
		vktramp[smp_processor_id()].vkru = 0;
		preempt_enable_no_resched_notrace();
		return -EINVAL;
	}
	current->vkru = addr;
	preempt_disable_notrace();
	vktramp[smp_processor_id()].vkru = addr;
	vktramp_map_base = &vktramp[smp_processor_id()].map;
	/* vkm lock should be outside this function */
	for (i = 0; i < VPMAP_LONGS; i++)
		*(vktramp_map_base + i) = 0;
	preempt_enable_no_resched_notrace();
	return 0;
}

SYSCALL_DEFINE2(vkey_wrvkrk, int, vkey, int, perm)
{
	return -EINVAL;
}

SYSCALL_DEFINE1(vkey_activate, int, vkey)
{
	int ret, i;
	struct mapped_vkey_struct *current_mvk, *target_mvk;
	int idx = ((vkey - 1) / MAX_ACTIVE_VKEYS) % current->vkm_nas;
	int pkey_oft, perm, pkru;

	current_mvk = current->mapped_vkeys;
	if (current_mvk) {
		target_mvk = current->mvk_arr[idx];
		if (target_mvk && current_mvk != target_mvk) {
			for (i = 0; i < MAX_ACTIVE_VKEYS; i++)
				if (target_mvk->map[i] == vkey)
					break;
			if (i != MAX_ACTIVE_VKEYS) {
				current->vkm = current->vkm_arr[idx];
				current->mapped_vkeys = target_mvk;
				current_mvk->pkru = mm_vkm_pkru_get();
				perm = vkey_get_vkru_permission(current, vkey);
				pkey_oft = target_mvk->pmap[i] << 1;
				pkru = target_mvk->pkru & 0xfffffff3;
				pkru &= (~(0x3 << pkey_oft));
				pkru |= (perm << pkey_oft);
				switch_mm_fast(current->mm, current);
				wrpkru(pkru);
				return 0;
			}
		}
	}
	mmap_read_lock(current->mm);
	ret = do_vkey_activate(vkey);
	mmap_read_unlock(current->mm);
	return ret;
}

#else	/* Kernel VKRK */

SYSCALL_DEFINE2(vkey_reg_lib, unsigned long, laddr, unsigned long, taddr)
{
	return -EINVAL;
}

SYSCALL_DEFINE2(vkey_reg_vkru, unsigned long, addr, unsigned int, nas)
{
	if (unlikely(!current->vkrk)) {
		current->vkrk = tsk_vkrk_alloc();
		if (!current->vkrk)
			return -EINVAL;
	}
	if (nas > 0 && nas <= MAX_ADDR_SPACE_PER_THREAD && current->vkm_nas <= nas) {
		current->vkm_nas = nas;
		return 0;
	}
	return -EINVAL;
}

SYSCALL_DEFINE2(vkey_wrvkrk, int, vkey, int, perm)
{
	if (unlikely(!current->vkrk)) {
		printk(KERN_ERR "[%s] Please allocate with VKRK before assigning", __func__);
		return -EINVAL;
	}

	/* First, find mvk, then vkm mapping. */
	if (vkey != ARCH_DEFAULT_VKEY) {
		int i;
		int idx = ((vkey - 1) / MAX_ACTIVE_VKEYS) % current->vkm_nas;
		vkey_set_vkrk_permission(vkey, perm & 0x3);
		if (current->mapped_vkeys && current->mvk_arr[idx]) {
			vpmap_t *target_maps = current->mvk_arr[idx]->map;
			vpmap_t *target_pmaps = current->mvk_arr[idx]->pmap;
			struct vkey_map_struct *target_vkm = current->vkm_arr[idx];
			for (i = 0; i < MAX_ACTIVE_VKEYS; i++)	/* Mapped vkeys, no data race */
				if (target_maps[i] == vkey)
					break;
			if (i != MAX_ACTIVE_VKEYS) {
				/* Vkm is never NULL, and no need to lock because other threads in vkm
				* can never write the vkey pkey map used by current thread */
				if (target_vkm != current->vkm) {
					if (!(perm & VKEY_MASK)) {
						current->vkm = target_vkm;
						current->mapped_vkeys->pkru = mm_vkm_pkru_get();
						current->mapped_vkeys = current->mvk_arr[idx];
						switch_mm_fast(current->mm, current);
						set_domain((current->mapped_vkeys->pkru &
							(~domain_mask(target_pmaps[i]))) |
							domain_val(target_pmaps[i], perm & 0x3));
					}
				} else
					mm_vkm_pkru_set_bits(target_pmaps[i], perm & 0x3);
				return 0;
			}
		}

		if (!(perm & VKEY_MASK)) {
			int ret;
			mmap_read_lock(current->mm);
			ret = do_vkey_activate(vkey);
			mmap_read_unlock(current->mm);
			return ret;
		}
	}

	return -EINVAL;
}

SYSCALL_DEFINE1(vkey_activate, int, vkey)
{
	return -EINVAL;
}

#endif /* HAS_VPK_USER_VKRU */

SYSCALL_DEFINE0(vkey_alloc)
{
	int vkey;
	int ret;

	mmap_write_lock(current->mm);
	vkey = mm_vkey_alloc(current->mm);

	ret = -ENOSPC;
	if (vkey == -1)
		goto out;

	ret = vkey;
	if (ret == 1)
		do_vkey_activate(ret);
out:
	mmap_write_unlock(current->mm);
	return ret;
}

SYSCALL_DEFINE1(vkey_free, int, vkey)
{
	int ret;

	/* Pkey use after free should be avoided by the user */
	mmap_write_lock(current->mm);
	ret = mm_vkey_free(current->mm, vkey);
	mmap_write_unlock(current->mm);

	return ret;
}

SYSCALL_DEFINE4(vkey_mprotect, unsigned long, start, size_t, len,
		unsigned long, prot, int, vkey)
{
	/* Map the area with vkey, disable both read and write (with the only execute-only pkey)
	 * Let the following page fault to trigger the vkey->pkey mapping
	 */
	int ret;
	int pkey;
	mmap_write_lock(current->mm);
#ifdef CONFIG_HAS_VPK_USER_VKRU
	if (unlikely(!current->mm->lvkru_uaddr)) {
		printk(KERN_ERR "Cannot find the trusted libvkeys in userspace...\n");
		mmap_write_unlock(current->mm);
		return -EINVAL;
	}
#endif
	if (unlikely(execute_only_pkey(current->mm) != get_execute_only_pkey(current->mm))) {
		printk(KERN_ERR "Cannot find the xok %d...\n", execute_only_pkey(current->mm));
		mmap_write_unlock(current->mm);
		return -EINVAL;
	}

	/* If the vkey is 0 (default), just set the pkey to 0 as well. */
	pkey = execute_only_pkey(current->mm);
	if (current->vkm) {
		int i, j;
		for (i = arch_max_pkey() - 2; i >= 0; i--)
			if (current->vkm->pkey_vkey[i] == vkey) {
				struct mapped_vkey_struct* mvk = current->mapped_vkeys;
				if (mvk) {
					for (j = 0; j < MAX_ACTIVE_VKEYS; j++)
						if (vkey == mvk->map[j]) {
							pkey = mm_vkm_idx_to_pkey(i);
							break;
						}
				}
				break;
			}
	}
	if (unlikely(!vkey))
		pkey = 0;

	/* Just shift left the vkey to use the high bits of the vm_flags and,
	 * make the real pkey to the execute-only one
	 * to trigger later pkey-caused page fault.
	 * If the XO-pkey is 3, then is the final vkey sent to do_mprotect_pkey().
	 * [ vkey, 0011 ]
	 */
	ret = do_mprotect_pkey(start, len, prot, pkey, vkey, true);
	mmap_write_unlock(current->mm);
	return ret;
}

#endif /* CONFIG_HAS_VPK */
