#include <linux/vkey_map.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/hugetlb.h>
#include <linux/rmap.h>
#include <linux/vkeys.h>
#include <linux/cpumask.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>
#include <asm/vkeys.h>
#include <linux/sched.h>

#include "../mm/internal.h"

#ifdef CONFIG_HAS_VPK

static struct kmem_cache *vkey_vkm_cachep;
static struct kmem_cache *vkey_mvk_cachep;
static struct kmem_cache *vkey_vkm_arr_cachep;
static struct kmem_cache *vkey_mvk_arr_cachep;
static struct kmem_cache *vkey_vkrk_cachep;
static void mm_vkm_unmap(struct vkey_map_struct *vkm, struct mm_struct *mm);
void walk_vkey_thread(struct task_struct *tsk);

extern atomic64_t last_mm_ctx_id;

#define allocate_vkey_vkm()		(kmem_cache_alloc(vkey_vkm_cachep, GFP_KERNEL))
#define free_vkey_vkm(vkm)		(kmem_cache_free(vkey_vkm_cachep, (vkm)))
#define allocate_vkey_mvk()		(kmem_cache_alloc(vkey_mvk_cachep, GFP_KERNEL))
#define free_vkey_mvk(mvk)		(kmem_cache_free(vkey_mvk_cachep, (mvk)))
#define allocate_vkey_vkrk()	(kmem_cache_alloc(vkey_vkrk_cachep, GFP_KERNEL))
#define free_vkey_vkrk(vkrk)	(kmem_cache_free(vkey_vkrk_cachep, (vkrk)))
#define allocate_vkey_mvk_arr()		(kmem_cache_alloc(vkey_mvk_arr_cachep, GFP_KERNEL))
#define free_vkey_mvk_arr(mvk)		(kmem_cache_free(vkey_mvk_arr_cachep, (mvk)))
#define allocate_vkey_vkm_arr()		(kmem_cache_alloc(vkey_vkm_arr_cachep, GFP_KERNEL))
#define free_vkey_vkm_arr(vkm)		(kmem_cache_free(vkey_vkm_arr_cachep, (vkm)))

void vkey_print_error_message(unsigned long address, int vkey);

static inline void vkm_init_cpumask(struct vkey_map_struct *vkm)
{
	unsigned long cpu_bitmap = (unsigned long)vkm;

	cpu_bitmap += offsetof(struct vkey_map_struct, cpu_bitmap);
	cpumask_clear((struct cpumask *)cpu_bitmap);
}

static inline void init_rss_vec(int *rss)
{
	memset(rss, 0, sizeof(int) * NR_MM_COUNTERS);
}

static inline void add_mm_rss_vec(struct mm_struct *mm, int *rss)
{
	int i;

	if (current->mm == mm)
		sync_mm_rss(mm);
	for (i = 0; i < NR_MM_COUNTERS; i++)
		if (rss[i])
			add_mm_counter(mm, i, rss[i]);
}

static inline int mm_vkm_mod_pte_range(struct vm_area_struct *vma,
	       pmd_t *dst_pmd, pmd_t *src_pmd, unsigned long addr,
	       unsigned long end, bool one_pte, int pkey, bool cp)
{
	/* Copy the PTEs of the range from src_pmd to dst_pmd. */
	struct mm_struct *mm = vma->vm_mm;
	pte_t *src_pte, *dst_pte;
	struct page *page;
	int rss[NR_MM_COUNTERS];

	dst_pte = pte_alloc_map_ignore_vpk(mm, dst_pmd, addr);
	if (!dst_pte)
		return -ENOMEM;

	spin_lock(&mm->page_table_lock);
	if (cp) {
		src_pte = pte_offset_map(src_pmd, addr);
		if (pkey != -1)
			vkm_pmd_populate(mm, dst_pmd, pmd_pgtable(*dst_pmd), pkey);
		do {
			pte_t pte;
			if (pte_none(*src_pte) || !pte_present(*src_pte))
				continue;
			pte = *src_pte;
			if (pkey != -1)
				pte = mm_vkm_mkpte(pte, pkey);
			/* If concurrent fault, just skip the counter. */
			if (!pte_same(pte, *dst_pte)) {
				page = vm_normal_page(vma, addr, pte);
				/* Update data page statistics */
				if (page) {
					init_rss_vec(rss);
					get_page(page);
					page_dup_rmap(page, false);
					if (PageAnon(page)) {
						if (!pte_present(*dst_pte) || pte_none(*dst_pte)) {
							rss[MM_ANONPAGES]++;
						}
					} else if (PageSwapBacked(page))
						rss[MM_SHMEMPAGES]++;
					else
						rss[MM_FILEPAGES]++;
					add_mm_rss_vec(mm, rss);
				}
			}
			set_pte_at(mm, addr, dst_pte, pte);
		} while (dst_pte++, src_pte++, addr += PAGE_SIZE, addr != end && !one_pte);
	} else {
		bool skip_ptes = vkm_mod_pmd_fast(mm, dst_pmd, pmd_pgtable(*dst_pmd), pkey, addr, end);
		bool same_pkey = false;
		flush_tlb_batched_pending(mm);
		arch_enter_lazy_mmu_mode();
		/* Currently, no need to use mmu_notifier when only the vkey-pkey map changes. */
#ifdef ARCH_VPK_WRITE_PTE
		if (!skip_ptes) {
			do {
				if (pte_none(*dst_pte) || !pte_present(*dst_pte))
					continue;
				if (pkey == mm_vkm_pte_get_pkey(*dst_pte))
					same_pkey = true;
				break;
			} while (dst_pte++, addr += PAGE_SIZE, addr != end);
			if ((!same_pkey && addr != end) || one_pte)
				do {
					pte_t pte, oldpte;
					if (pte_none(*dst_pte) || !pte_present(*dst_pte))
						continue;
					oldpte = pte = ptep_modify_prot_start(vma, addr, dst_pte);
					pte = mm_vkm_mkpte(pte, pkey);
					ptep_modify_prot_commit(vma, addr, dst_pte, oldpte, pte);
				} while (dst_pte++, addr += PAGE_SIZE, addr != end && !one_pte);
		}
#endif
		arch_leave_lazy_mmu_mode();
	}
	spin_unlock(&mm->page_table_lock);
	return 0;
}

static inline int mm_vkm_mod_pmd_range(struct vm_area_struct *vma,
	       pud_t *dst_pud, pud_t *src_pud, unsigned long addr,
	       unsigned long end, bool one_pte, int pkey, bool cp)
{
	struct mm_struct *mm = vma->vm_mm;
	pmd_t *src_pmd, *dst_pmd;
	unsigned long next;

	dst_pmd = pmd_alloc(mm, dst_pud, addr);
	if (!dst_pmd)
		return -ENOMEM;

	if (cp) {
		src_pmd = pmd_offset(src_pud, addr);
		do {
			next = pmd_addr_end(addr, end);
			if (is_swap_pmd(*src_pmd) || pmd_trans_huge(*src_pmd)
				|| pmd_devmap(*src_pmd)) {
				pmd_t pmd;
				VM_BUG_ON_VMA(next-addr != HPAGE_PMD_SIZE, vma);
				spin_lock(&mm->page_table_lock);
				pmd = *src_pmd;
				if (pkey != -1)
					pmd = mm_vkm_mkpmd(pmd, pkey);
				set_pmd_at(mm, addr, dst_pmd, pmd);
				spin_unlock(&mm->page_table_lock);
				continue;
			}
			if (pmd_none_or_clear_bad(src_pmd))
				continue;
			if (mm_vkm_mod_pte_range(vma, dst_pmd, src_pmd, addr, next, one_pte, pkey, cp))
				return -ENOMEM;
		} while (dst_pmd++, src_pmd++, addr = next, addr != end && !one_pte);
	} else {
		do {
			next = pmd_addr_end(addr, end);
			/* See mm/mprotect.c::change_pmd_range. */
			if (!is_swap_pmd(*dst_pmd) && !pmd_devmap(*dst_pmd) &&
				pmd_none_or_clear_bad(dst_pmd))
				continue;
			if (is_swap_pmd(*dst_pmd) || pmd_trans_huge(*dst_pmd)
				|| pmd_devmap(*dst_pmd)) {
				pmd_t pmd;
				VM_BUG_ON_VMA(next-addr != HPAGE_PMD_SIZE, vma);
				spin_lock(&mm->page_table_lock);
				pmd = pmdp_invalidate(vma, addr, dst_pmd);
				pmd = mm_vkm_mkpmd(pmd, pkey);
				set_pmd_at(mm, addr, dst_pmd, pmd);
				spin_unlock(&mm->page_table_lock);
				continue;
			}
			if (mm_vkm_mod_pte_range(vma, dst_pmd, src_pmd, addr, next, one_pte, pkey, cp))
				return -ENOMEM;
		} while (dst_pmd++, addr = next, addr != end && !one_pte);
	}
	return 0;
}

static inline int mm_vkm_mod_pud_range(struct vm_area_struct *vma,
	       p4d_t *dst_p4d, p4d_t *src_p4d, unsigned long addr,
	       unsigned long end, bool one_pte, int pkey, bool cp)
{
	struct mm_struct *mm = vma->vm_mm;
	pud_t *src_pud, *dst_pud;
	unsigned long next;

	dst_pud = pud_alloc(mm, dst_p4d, addr);
	if (!dst_pud)
		return -ENOMEM;

	if (cp) {
		src_pud = pud_offset(src_p4d, addr);
		do {
			next = pud_addr_end(addr, end);
			if (pud_trans_huge(*src_pud) || pud_devmap(*src_pud)) {
				pud_t pud;
				VM_BUG_ON_VMA(next-addr != HPAGE_PUD_SIZE, vma);
				spin_lock(&mm->page_table_lock);
				pud = *src_pud;
				if (pkey != -1)
					pud = mm_vkm_mkpud(pud, pkey);
				set_pud_at(mm, addr, dst_pud, pud);
				spin_unlock(&mm->page_table_lock);
				continue;
			}
			if (pud_none_or_clear_bad(src_pud))
				continue;
			if (mm_vkm_mod_pmd_range(vma, dst_pud, src_pud, addr, next, one_pte, pkey, cp))
				return -ENOMEM;
		} while (dst_pud++, src_pud++, addr = next, addr != end && !one_pte);
	} else {
		do {
			next = pud_addr_end(addr, end);
			if (pud_none_or_clear_bad(dst_pud))
				continue;
			mm_vkm_mod_pmd_range(vma, dst_pud, NULL, addr, next, one_pte, pkey, cp);
		} while (dst_pud++, addr = next, addr != end && !one_pte);
	}
	return 0;
}

int mm_vkm_mod_p4d_range(struct vm_area_struct *vma,
	       pgd_t *dst_pgd, pgd_t *src_pgd, unsigned long addr,
	       unsigned long end, int pkey, bool cp, bool one_pte)
{
	struct mm_struct *mm = vma->vm_mm;
	p4d_t *src_p4d, *dst_p4d;
	unsigned long next;

	/* By default, p4d is shared by copied pgd range and not none. */
	dst_p4d = p4d_alloc(mm, dst_pgd, addr);
	if (!dst_p4d)
		return -ENOMEM;

	if (cp) {
		src_p4d = p4d_offset(src_pgd, addr);
		do {
			next = p4d_addr_end(addr, end);
			if (p4d_none_or_clear_bad(src_p4d))
				continue;
			if (mm_vkm_mod_pud_range(vma, dst_p4d, src_p4d,
					addr, next, one_pte, pkey, cp))
				return -ENOMEM;
		} while (dst_p4d++, src_p4d++, addr = next, addr != end && !one_pte);
	} else {
		do {
			next = p4d_addr_end(addr, end);
			if (p4d_none_or_clear_bad(dst_p4d))
				continue;
			mm_vkm_mod_pud_range(vma, dst_p4d, NULL,
					addr, next, one_pte, pkey, cp);
		} while (dst_p4d++, addr = next, addr != end && !one_pte);
	}
	return 0;
}

/* This function refers copy_page_range(), which calls the following chain. */
/* Copy the whole page table from the orig pgd, or change the mapped protection key when not -1. */
static int
mm_vkm_mod_page_range(struct vkey_map_struct *vkm, struct vm_area_struct *vma, int pkey, bool cp)
{
	pgd_t *src_pgd, *dst_pgd;
	struct mm_struct *src_mm = vma->vm_mm;
	unsigned long addr = vma->vm_start;
	unsigned long end = vma->vm_end;
	unsigned long next;

	/* Skip special mapping and to-be-filled by page fault vm areas. */
	if (!(vma->vm_flags & (VM_HUGETLB | VM_PFNMAP | VM_MIXEDMAP)) &&
	    !vma->anon_vma && cp)
		return 0;

	/* Special copy of huge TLB feature, currently not supported. */
	if (unlikely(is_vm_hugetlb_page(vma))) {
		printk(KERN_ERR "[%s] vkey currently does not support huge TLB page...\n", __func__);
		return -EINVAL;
	}

	/* Copy the PTEs one by one. */
	dst_pgd = vkm->pgd + pgd_index(addr);
	if (cp) {
		src_pgd = pgd_offset(src_mm, addr);
		do {
			next = pgd_addr_end(addr, end);
			if (pgd_none_or_clear_bad(src_pgd))
				continue;
			if (unlikely(mm_vkm_mod_p4d_range(vma, dst_pgd, src_pgd, addr, next, pkey, cp, false)))
				return -ENOMEM;
		} while (dst_pgd++, src_pgd++, addr = next, addr != end);
	} else {
		do {
			next = pgd_addr_end(addr, end);
			/* Leave it to page fault. */
			if (pgd_none_or_clear_bad(dst_pgd))
				continue;
			mm_vkm_mod_p4d_range(vma, dst_pgd, NULL, addr, next, pkey, cp, false);
		} while (dst_pgd++, addr = next, addr != end);
	}

	return 0;
}

static int mm_vkm_copy_page_table(struct vkey_map_struct *vkm, struct mm_struct *mm)
{
	struct vm_area_struct *src_vma;
	int err;

	/* No need to copy if the vkm is the first one. */
	if (mm->pgd == vkm->pgd)
		return 0;

	for (src_vma = mm->mmap; src_vma; src_vma = src_vma->vm_next) {
		if (mm_mprotect_vkey(src_vma, -1)) {	/* Initialize the vkey protected vmas with execute only pkey */
			if (unlikely(err = mm_vkm_mod_page_range(vkm, src_vma, execute_only_pkey(mm), true)))
				return err;
		} else if (unlikely(err = mm_vkm_mod_page_range(vkm, src_vma, -1, true)))
			return err;
	}

	return 0;
}

/* Change the pkey fields of present PTEs of vkeys. */
static int mm_vkm_mprotect_present_ptes(struct vkey_map_struct *vkm,
			struct mm_struct *mm, int vkey, int pkey)
{
	struct list_head *pos;
	struct vm_area_struct *vma;
	struct vkey_kgd_struct *kgd = mm->vkey.kgd;
	struct vkey_kte_struct *kte = NULL;

	if (!kgd)
		return 0;
	else
		kte = kgd->ktes[vkey_kgd_offset(vkey)];
	if (!kte)
		return 0;

	list_for_each(pos, &(kte->vkey_vma_heads[vkey_kte_offset(vkey)])) {

		/* Finally, we get the vmas indexed by the vkey from our vkey table. */
		vma = list_entry(pos, struct vm_area_struct, vkey_chain);

		/*
		 * 1. No change the vma's vm_flags.pkey field. Just leave this to modified page fault handler.
		 * 2. No change to the vma's vm_page_prot. Again, handler has to look the map to handle page fault.
		 * 3. Change the pkey bits in the vkm-local and present PTEs.
		 * 4. Flush local TLB entries of a range.
		 */
		flush_cache_range(vma, vma->vm_start, vma->vm_end);
		inc_tlb_flush_pending(mm);
		if (unlikely(mm_vkm_mod_page_range(vkm, vma, pkey, false))) {
			printk(KERN_ERR "[%s] fatal error detected for vkm, please kill the process\n", __func__);
			flush_tlb_mm(vma->vm_mm);
			dec_tlb_flush_pending(mm);
			return -EINVAL;
		}
		flush_tlb_vkm_range(vma, vkm);
		dec_tlb_flush_pending(mm);
	}
	return 0;
}

struct vkey_map_struct *mm_vkm_alloc_init(struct mm_struct *mm)
{
	struct vkey_map_struct *vkm;
	int i;
	int is_free = atomic_cmpxchg(&mm->is_main_vkm_free, 1, 0);

	vkm = allocate_vkey_vkm();
	if (vkm) {
		vkm->nr_thread = 0;
		for (i = 0; i < arch_max_pkey() - 1; i++) {
			vkm->pkey_nr_thread[i] = 0;
			vkm->pkey_vkey[i] = ARCH_DEFAULT_VKEY;
		}
		arch_vkm_init(mm, vkm, is_free);
		vkm_init_cpumask(vkm);	/* The main vkm's cpu_bitmap will never be in use. */
		/* This caller is called by vkey_activate when mmap is locked. */
		/* So we can use the vma area to duplicate all present PTEs */
		if (vkm->pgd && !mm_vkm_copy_page_table(vkm, mm)) {
			/* This is used when the vkm is not the same with main address space. */
			/* The main space uses page_table_lock in mm_struct. */
			spin_lock_init(&vkm->slock);
		} else
			free_vkey_vkm(vkm);
	}

	return vkm;
}

bool mm_vkm_can_add(struct vkey_map_struct *vkm, vpmap_t *current_vkeys, int len)
{
	int nr_vkm_vkey;
	int nr_extra_vkey;
	int i, j;

	nr_extra_vkey = nr_vkm_vkey = 0;

	for (i = 0; i < arch_max_pkey() - 1; i++)
		if (vkm->pkey_vkey[i])
			nr_vkm_vkey++;

	for (i = 0; i < MAX_ACTIVE_VKEYS; i++) {
		if (!current_vkeys[i])
			continue;
		for (j = 0; j < arch_max_pkey() - 1; j++)
			if (current_vkeys[i] == vkm->pkey_vkey[j])
				break;
		if (j == arch_max_pkey() - 1)
			nr_extra_vkey++;
	}

	return (nr_vkm_vkey + nr_extra_vkey <= MAX_ACTIVE_VKEYS);
}

bool mm_vkm_is_in(struct vkey_map_struct *vkm, vpmap_t *current_vkeys, int len)
{
	int i, j;
	for (i = 0; i < len; i++) {
		if (!current_vkeys[i])
			continue;
		for (j = 0; j < arch_max_pkey() - 1; j++)
			if (current_vkeys[i] == vkm->pkey_vkey[j])
				break;
		if (j == arch_max_pkey() - 1)
			return false;
	}
	return true;
}

vpmap_t mm_vkm_add_vkey(struct vkey_map_struct *vkm, int vkey, int evicted, int xok, int perm)
{
	int i;
	int first_evicted;
	int orig_vkey;
	vpmap_t ret;

	first_evicted = 0;
	for (i = (arch_max_pkey() - 1) - 1; i >= 0; i--) {
		if (mm_vkm_idx_to_pkey(i) == xok)
			continue;
		if (vkm->pkey_vkey[i] == vkey) {
			vkm->pkey_nr_thread[i]++;
			mm_vkm_mprotect_present_ptes(vkm, current->mm, vkey, mm_vkm_idx_to_pkey(i));
			mm_vkm_pkru_set_bits(mm_vkm_idx_to_pkey(i), (u32)perm);
			ret = mm_vkm_idx_to_pkey(i);
			goto out;
		}
		if (vkm->pkey_vkey[i] == evicted)
			first_evicted = i;
	}
	orig_vkey = vkm->pkey_vkey[first_evicted];
	vkm->pkey_vkey[first_evicted] = vkey;

	/* Update the PTEs of the vkm. */
	/* If the evicted vkey is originally 0, just update the present PTE, non-present are handled by later PFs. */
	/* If the evicted vkey is not zero, set the live PTEs to xok, and update present PTEs. */
	if (orig_vkey)
		mm_vkm_mprotect_present_ptes(vkm, current->mm, orig_vkey, xok);
	else
		vkm->pkey_nr_thread[first_evicted] = 1;
	mm_vkm_mprotect_present_ptes(vkm, current->mm, vkey, mm_vkm_idx_to_pkey(first_evicted));
	mm_vkm_pkru_set_bits(mm_vkm_idx_to_pkey(first_evicted), (u32)perm);
	ret = mm_vkm_idx_to_pkey(first_evicted);

out:
	return ret;
}

void mm_vkm_add(struct vkey_map_struct *vkm, struct mapped_vkey_struct *mvk, int len, int xok, int *current_perm)
{
	int i, j;
	int new_vk;
	vpmap_t *current_vkeys = mvk->map;
	vpmap_t *current_pkeys = mvk->pmap;

	vkm->nr_thread++;

	mm_vkm_pkru_reset(false);
	for (i = 0; i < len; i++) {
		new_vk = current_vkeys[i];
		if (!new_vk)
			continue;

		/* The first pass tries if the new vkey is in the original mapping. */
		for (j = 0; j < arch_max_pkey() - 1; j++)
			if (vkm->pkey_vkey[j] == new_vk) {
				vkm->pkey_nr_thread[j]++;
				mm_vkm_mprotect_present_ptes(vkm, current->mm, new_vk, mm_vkm_idx_to_pkey(j));
				break;
			}

		/* The new vkey has not been mapped. */
		if (j == arch_max_pkey() - 1)
			for (j = 0; j < arch_max_pkey() - 1; j++) {
				if (xok == mm_vkm_idx_to_pkey(j))   /* eXecute-Only pkey should never be mapped */
					continue;
				if (!vkm->pkey_vkey[j]) {
					vkm->pkey_vkey[j] = new_vk;
					vkm->pkey_nr_thread[j] = 1;
					mm_vkm_mprotect_present_ptes(vkm, current->mm, new_vk, mm_vkm_idx_to_pkey(j));
					break;
				}
			}

		/* Set the PKRU register. */
		mm_vkm_pkru_set_bits(mm_vkm_idx_to_pkey(j), (u32)current_perm[i]);
		current_pkeys[i] = mm_vkm_idx_to_pkey(j);

		if (j == arch_max_pkey() - 1)
			break;
	}
}

void mm_vkm_del(struct vkey_map_struct *vkm, struct mm_struct *mm, vpmap_t *current_vkeys, int len, int *current_perm, int evicted)
{
	int new_vk;
	int i, j;
	vpmap_t *orig_map;
	int *orig_nr_thread;
	int original_perm;

	orig_map = vkm->pkey_vkey;
	orig_nr_thread = vkm->pkey_nr_thread;
	vkm->nr_thread--;

	if (!current_perm)
		mm_vkm_pkru_reset(true);

	if (!vkm->nr_thread) {
		for (j = 0; j < arch_max_pkey() - 1; j++) {
			orig_nr_thread[j] = 0;
			orig_map[j] = ARCH_DEFAULT_VKEY;
		}
	} else for (i = 0; i <= len; i++) {
		if (i == len)
			new_vk = evicted;
		else
			new_vk = current_vkeys[i];
		if (!new_vk)
			continue;

		/* Find all pkeys which maps to new_vk, del the pkey_nr_thread, if 0 -> map 0. */
		for (j = 0; j < arch_max_pkey() - 1; j++)
			if (orig_map[j] == new_vk) {
				/* Here, memorize the perm in current_perm, and then disable */
				/* No need to override */
				original_perm = mm_vkm_pkru_get_bits(mm_vkm_idx_to_pkey(j));
				if (current_perm && i < len) {
					current_perm[i] = original_perm;
				}
				orig_nr_thread[j]--;
				if (!orig_nr_thread[j]) {
					orig_map[j] = ARCH_DEFAULT_VKEY;
					/* No need to lock up if our pkru value is always right */
					/*mm_vkm_mprotect_present_ptes(vkm, mm, new_vk, get_execute_only_pkey(mm));*/
				}
				break;
			}
	}
}

/*
 * Note: this doesn't free the actual pages themselves. That
 * has been handled earlier when unmapping all the memory regions.
 */
static void mm_vkm_free_pte_range(struct mmu_gather *tlb, pmd_t *pmd,
			   unsigned long addr)
{
	pgtable_t token = pmd_pgtable(*pmd);
	pmd_clear(pmd);
	pte_free_tlb(tlb, token, addr);
	mm_dec_nr_ptes(tlb->mm);
}

static inline void mm_vkm_free_pmd_range(struct mmu_gather *tlb, pud_t *pud,
				unsigned long addr, unsigned long end,
				unsigned long floor, unsigned long ceiling)
{
	pmd_t *pmd;
	unsigned long next;
	unsigned long start;

	start = addr;
	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd))
			continue;
		mm_vkm_free_pte_range(tlb, pmd, addr);
	} while (pmd++, addr = next, addr != end);

	start &= PUD_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= PUD_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	pmd = pmd_offset(pud, start);
	pud_clear(pud);
	pmd_free_tlb(tlb, pmd, start);
	mm_dec_nr_pmds(tlb->mm);
}

static inline void mm_vkm_free_pud_range(struct mmu_gather *tlb, p4d_t *p4d,
				unsigned long addr, unsigned long end,
				unsigned long floor, unsigned long ceiling)
{
	pud_t *pud;
	unsigned long next;
	unsigned long start;

	start = addr;
	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		mm_vkm_free_pmd_range(tlb, pud, addr, next, floor, ceiling);
	} while (pud++, addr = next, addr != end);

	start &= P4D_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= P4D_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	pud = pud_offset(p4d, start);
	p4d_clear(p4d);
	pud_free_tlb(tlb, pud, start);
	mm_dec_nr_puds(tlb->mm);
}

static inline void mm_vkm_free_p4d_range(struct mmu_gather *tlb, pgd_t *pgd,
				unsigned long addr, unsigned long end,
				unsigned long floor, unsigned long ceiling)
{
	p4d_t *p4d;
	unsigned long next;
	unsigned long start;

	start = addr;
	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none_or_clear_bad(p4d))
			continue;
		mm_vkm_free_pud_range(tlb, p4d, addr, next, floor, ceiling);
	} while (p4d++, addr = next, addr != end);

	start &= PGDIR_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= PGDIR_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	p4d = p4d_offset(pgd, start);
	pgd_clear(pgd);
	p4d_free_tlb(tlb, p4d, start);
}

static void mm_vkm_free_pgd_range(struct mmu_gather *tlb, struct vkey_map_struct *vkm,
			unsigned long addr, unsigned long end, unsigned long floor, unsigned long ceiling)
{
	pgd_t *pgd;
	unsigned long next;

	addr &= PMD_MASK;
	if (addr < floor) {
		addr += PMD_SIZE;
		if (!addr)
			return;
	}
	if (ceiling) {
		ceiling &= PMD_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		end -= PMD_SIZE;
	if (addr > end - 1)
		return;

	tlb_change_page_size(tlb, PAGE_SIZE);
	pgd = vkm->pgd + pgd_index(addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		mm_vkm_free_p4d_range(tlb, pgd, addr, next, floor, ceiling);
	} while (pgd++, addr = next, addr != end);
}

void mm_vkm_free_page_table(struct mmu_gather *tlb, struct vm_area_struct *vma,
				struct vkey_map_struct *vkm, unsigned long floor, unsigned long ceiling)
{
	while (vma) {
		struct vm_area_struct *next = vma->vm_next;
		unsigned long addr = vma->vm_start;
		if (is_vm_hugetlb_page(vma))
			printk(KERN_ERR "[%s] vkey currently does not support huge TLB page...\n", __func__);
		else
			mm_vkm_free_pgd_range(tlb, vkm, addr, vma->vm_end, floor, next ? next->vm_start : ceiling);
		vma = next;
    }
}

static void mm_vkm_unmap(struct vkey_map_struct *vkm, struct mm_struct *mm)
{
	struct mmu_gather tlb;
	struct vm_area_struct *vma = mm->mmap;

	/* If the address space is the same, then leave the delete of pgtable to exit_mmap(). */
	if (vkm->pgd != mm->pgd) {
		/* Free pgd and all related puds, pmds and ptes. */
		tlb_gather_mmu(&tlb, mm);
		tlb.vkm = vkm;
		unmap_vmas(&tlb, vma, 0, -1);
		mm_vkm_free_page_table(&tlb, vma, vkm, FIRST_USER_ADDRESS, USER_PGTABLES_CEILING);
		tlb_finish_mmu(&tlb);
		pgd_free(mm, vkm->pgd);
	}
}

void __init vkey_map_caches_init(void)
{
	vkey_vkm_cachep = kmem_cache_create("vkey_vkm_cache",
			sizeof(struct vkey_map_struct) + cpumask_size(), 0,
			SLAB_HWCACHE_ALIGN|SLAB_NOLEAKTRACE,
			NULL);
	if (!vkey_vkm_cachep)
		printk(KERN_ERR "[%s] vkey vkm slab initialization failed...\n", __func__);
	else
		printk(KERN_INFO "[%s] vkey vkm slab initialized\n", __func__);

	vkey_mvk_cachep = kmem_cache_create("vkey_mvk_cache",
			sizeof(struct mapped_vkey_struct), 0,
			SLAB_HWCACHE_ALIGN|SLAB_NOLEAKTRACE,
			NULL);
	if (!vkey_mvk_cachep)
		printk(KERN_ERR "[%s] vkey mapped vk slab initialization failed...\n", __func__);
	else
		printk(KERN_INFO "[%s] vkey mapped vk slab initialized\n", __func__);

	vkey_vkm_arr_cachep = kmem_cache_create("vkey_vkm_arr_cache",
			sizeof(struct vkey_map_struct*) * MAX_ADDR_SPACE_PER_THREAD, 0,
			SLAB_HWCACHE_ALIGN|SLAB_NOLEAKTRACE,
			NULL);
	if (!vkey_vkm_arr_cachep)
		printk(KERN_ERR "[%s] vkey vkm arr slab initialization failed...\n", __func__);
	else
		printk(KERN_INFO "[%s] vkey vkm arr slab initialized\n", __func__);

	vkey_mvk_arr_cachep = kmem_cache_create("vkey_mvk_arr_cache",
			sizeof(struct mapped_vkey_struct*) * MAX_ADDR_SPACE_PER_THREAD, 0,
			SLAB_HWCACHE_ALIGN|SLAB_NOLEAKTRACE,
			NULL);
	if (!vkey_mvk_arr_cachep)
		printk(KERN_ERR "[%s] vkey mvk arr slab initialization failed...\n", __func__);
	else
		printk(KERN_INFO "[%s] vkey mvk arr slab initialized\n", __func__);

#ifndef CONFIG_HAS_VPK_USER_VKRU
	vkey_vkrk_cachep = kmem_cache_create("vkey_vkrk_cache",
			sizeof(struct vkey_vkrk_struct), 0,
			SLAB_HWCACHE_ALIGN|SLAB_NOLEAKTRACE,
			NULL);
	if (!vkey_vkrk_cachep)
		printk(KERN_ERR "[%s] vkey vkrk slab initialization failed...\n", __func__);
	else
		printk(KERN_INFO "[%s] vkey vkrk slab initialized\n", __func__);
#endif
}

void destroy_vkey_map(struct mm_struct *mm)
{
	struct list_head *pos;
	struct list_head *n;
	struct vkey_map_struct *vkm;

	list_for_each_safe(pos, n, &mm->vkm_chain) {
		vkm = list_entry(pos, struct vkey_map_struct, vkm_chain);
		mm_vkm_unmap(vkm, mm);
	}
}

void free_vkey_map(struct mm_struct *mm)
{
	struct list_head *pos;
	struct list_head *n;
	struct vkey_map_struct *vkm;
	bool pr = !list_empty(&mm->vkm_chain);

	if (pr)
		printk(KERN_INFO "Start free vkms from tsk %lx\n", (unsigned long)current);

	list_for_each_safe(pos, n, &mm->vkm_chain) {
		vkm = list_entry(pos, struct vkey_map_struct, vkm_chain);
		list_del(&vkm->vkm_chain);
		free_vkey_vkm(vkm);
	}

	if (pr)
		printk(KERN_INFO "End free vkms from %lx\n", (unsigned long)current);
}

void walk_vkey_map(struct mm_struct *mm)
{
	struct list_head *pos;
	struct vkey_map_struct *vkm;
	int i;

	list_for_each(pos, &mm->vkm_chain) {
		vkm = list_entry(pos, struct vkey_map_struct, vkm_chain);
		printk(KERN_INFO "[%s] vkey vkm %lx pgd %lx dumped with %d threads: ", __func__, (unsigned long)vkm, (unsigned long)vkm->pgd, vkm->nr_thread);
		for (i = 0; i < arch_max_pkey() - 1; i++)
			printk("v(%d)->p(%d)->t(%d)  ", vkm->pkey_vkey[i], mm_vkm_idx_to_pkey(i), vkm->pkey_nr_thread[i]);
		printk("\n");
	}
}

void walk_vkey_thread(struct task_struct *tsk)
{
#ifdef CONFIG_HAS_VPK_USER_VKRU
	printk(KERN_INFO "[%s] thread %lx(%d) in is vkm %lx, vkru %lx:\n", __func__, (unsigned long)tsk, tsk->pid, (unsigned long)(tsk->vkm), (unsigned long)(tsk->vkru));
	if (tsk->mapped_vkeys && tsk->vkru)
	printk("v(%d)[%d] "
	       "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d]",
			tsk->mapped_vkeys->map[0],  vkey_get_vkru_permission(tsk, tsk->mapped_vkeys->map[0]),
			tsk->mapped_vkeys->map[1],  vkey_get_vkru_permission(tsk, tsk->mapped_vkeys->map[1]),
			tsk->mapped_vkeys->map[2],  vkey_get_vkru_permission(tsk, tsk->mapped_vkeys->map[2]),
			tsk->mapped_vkeys->map[3],  vkey_get_vkru_permission(tsk, tsk->mapped_vkeys->map[3]),
			tsk->mapped_vkeys->map[4],  vkey_get_vkru_permission(tsk, tsk->mapped_vkeys->map[4]),
			tsk->mapped_vkeys->map[5],  vkey_get_vkru_permission(tsk, tsk->mapped_vkeys->map[5]),
			tsk->mapped_vkeys->map[6],  vkey_get_vkru_permission(tsk, tsk->mapped_vkeys->map[6]),
			tsk->mapped_vkeys->map[7],  vkey_get_vkru_permission(tsk, tsk->mapped_vkeys->map[7]),
			tsk->mapped_vkeys->map[8],  vkey_get_vkru_permission(tsk, tsk->mapped_vkeys->map[8]),
			tsk->mapped_vkeys->map[9],  vkey_get_vkru_permission(tsk, tsk->mapped_vkeys->map[9]),
			tsk->mapped_vkeys->map[10], vkey_get_vkru_permission(tsk, tsk->mapped_vkeys->map[10]),
			tsk->mapped_vkeys->map[11], vkey_get_vkru_permission(tsk, tsk->mapped_vkeys->map[11]),
			tsk->mapped_vkeys->map[12], vkey_get_vkru_permission(tsk, tsk->mapped_vkeys->map[12]),
			tsk->mapped_vkeys->map[13], vkey_get_vkru_permission(tsk, tsk->mapped_vkeys->map[13]));
	printk("pkru register %x\n", rdpkru());
#else
	printk(KERN_INFO "[%s] thread %lx(%d) in is vkm %lx, vkrk %lx:\n", __func__, (unsigned long)tsk, tsk->pid, (unsigned long)(tsk->vkm), (unsigned long)(tsk->vkrk));
	if (tsk->mapped_vkeys && tsk->vkrk)
	printk("v(%d)[%d] "
	       "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d] "
		   "v(%d)[%d]",
			tsk->mapped_vkeys->map[0],  vkey_get_vkrk_permission(tsk->vkrk, tsk->mapped_vkeys->map[0]),
			tsk->mapped_vkeys->map[1],  vkey_get_vkrk_permission(tsk->vkrk, tsk->mapped_vkeys->map[1]),
			tsk->mapped_vkeys->map[2],  vkey_get_vkrk_permission(tsk->vkrk, tsk->mapped_vkeys->map[2]),
			tsk->mapped_vkeys->map[3],  vkey_get_vkrk_permission(tsk->vkrk, tsk->mapped_vkeys->map[3]),
			tsk->mapped_vkeys->map[4],  vkey_get_vkrk_permission(tsk->vkrk, tsk->mapped_vkeys->map[4]),
			tsk->mapped_vkeys->map[5],  vkey_get_vkrk_permission(tsk->vkrk, tsk->mapped_vkeys->map[5]),
			tsk->mapped_vkeys->map[6],  vkey_get_vkrk_permission(tsk->vkrk, tsk->mapped_vkeys->map[6]),
			tsk->mapped_vkeys->map[7],  vkey_get_vkrk_permission(tsk->vkrk, tsk->mapped_vkeys->map[7]),
			tsk->mapped_vkeys->map[8],  vkey_get_vkrk_permission(tsk->vkrk, tsk->mapped_vkeys->map[8]),
			tsk->mapped_vkeys->map[9],  vkey_get_vkrk_permission(tsk->vkrk, tsk->mapped_vkeys->map[9]),
			tsk->mapped_vkeys->map[10], vkey_get_vkrk_permission(tsk->vkrk, tsk->mapped_vkeys->map[10]));
	printk("dacr register %x\n", get_domain());
#endif
}

void vkey_print_error_message(unsigned long address, int vkey)
{
	walk_vkey_thread(current);

	/* print the vkey map */
	walk_vkey_map(current->mm);

#ifdef CONFIG_64BIT
	if (address)
		printk("[%s] The fault PTE of vkey %d of %lx is %lx in the vkm\n", __func__, vkey, address, pte_val(*pte_offset_map(pmd_offset(pud_offset(
				p4d_offset((current->vkm ? (current->vkm->pgd + pgd_index(address)) : pgd_offset_pgd(current->mm->pgd, (address))), address), address), address), address)));
#else
	if (address)
		printk("[%s] The fault PTE of vkey %d of %lx is %x in the vkm\n", __func__, vkey, address, pte_val(*pte_offset_map(pmd_offset(pud_offset(
				p4d_offset((current->vkm ? (current->vkm->pgd + pgd_index(address)) : pgd_offset_pgd(current->mm->pgd, (address))), address), address), address), address)));
#endif
}

inline struct mapped_vkey_struct *tsk_mvk_alloc(void)
{
	struct mapped_vkey_struct *mvk;
	int i;
	mvk = allocate_vkey_mvk();
	if (mvk) {
		for (i = 0; i < MAX_ACTIVE_VKEYS; i++) {
			mvk->map[i] = ARCH_DEFAULT_VKEY;
			mvk->pmap[i] = 0;
			mvk->ts[i] = i;
		}
	}
	return mvk;
}

inline struct vkey_vkrk_struct *tsk_vkrk_alloc(void)
{
#ifdef CONFIG_HAS_VPK_USER_VKRU
	return NULL;
#else
	int i;
	struct vkey_vkrk_struct *vkrk = allocate_vkey_vkrk();
	if (!vkrk)
		return NULL;
	for (i = 0; i < arch_max_vkey(); i++) {
		int vkey = i + 1;
		int idx = 2 * vkey / sizeof(unsigned long);
		int oft = vkey % sizeof(unsigned long);
		vkrk->bm[idx] &= (~(3UL << (oft << 1U)));
		vkrk->bm[idx] |= ((unsigned long)VKEY_ND << (oft << 1U));
	}
	return vkrk;
#endif
}

inline void tsk_mvk_free(struct mapped_vkey_struct *mvk)
{
	free_vkey_mvk(mvk);
}

inline void tsk_vkrk_free(struct vkey_vkrk_struct *vkrk)
{
	free_vkey_vkrk(vkrk);
}

inline struct mapped_vkey_struct **tsk_mvk_arr_alloc(void)
{
	struct mapped_vkey_struct **arr = allocate_vkey_mvk_arr();
	int i;
	if (arr)
		for (i = 0; i < MAX_ADDR_SPACE_PER_THREAD; i++)
			arr[i] = NULL;
	return arr;
}

inline struct vkey_map_struct **tsk_vkm_arr_alloc(void)
{
	struct vkey_map_struct **arr = allocate_vkey_vkm_arr();
	int i;
	if (arr)
		for (i = 0; i < MAX_ADDR_SPACE_PER_THREAD; i++)
			arr[i] = NULL;
	return arr;
}

inline void tsk_mvk_arr_free(struct mapped_vkey_struct **mvk)
{
	free_vkey_mvk_arr(mvk);
}

inline void tsk_vkm_arr_free(struct vkey_map_struct **vkm)
{
	free_vkey_vkm_arr(vkm);
}

#else

void vkey_map_caches_init(void)
{

}

void destroy_vkey_map(struct mm_struct *mm)
{

}

void free_vkey_map(struct mm_struct *mm)
{

}

#endif
