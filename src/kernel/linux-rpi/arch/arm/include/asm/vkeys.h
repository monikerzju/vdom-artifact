#ifndef _ASM_ARM_VKEYS_H
#define _ASM_ARM_VKEYS_H

#define VMA_VKEY_MASK	0x0000ffff	/* In 32 bit arch, the upper 16 bits are for pkeys */

#define VKEY_AD 0x0		/* access  disable */
#define VKEY_WD 0x1		/* write   disable */
#define VKEY_PINNED 0x2
#define VKEY_ND 0x3		/* nothing disable */
#define VKEY_MASK 0x4

#ifdef CONFIG_HAS_VPK
#include <linux/mm_types.h>
#include <linux/vkey_types.h>
#include <linux/pkeys.h>
#include <linux/cpumask.h>
#include <asm/domain.h>
#include <asm/pgalloc.h>

/* arm32 no transparent huge page */
#define set_pmd_at(mm, addr, dst_pmd, pmd)	do { } while(0);
#define set_pud_at(mm, addr, dst_pud, pud)	do { } while(0);

extern void flush_tlb_vkm_range(struct vm_area_struct *vma, struct vkey_map_struct *vkm);
extern void flush_tlb_vkm_page(unsigned long addr, struct vkey_map_struct *vkm);

static inline void mm_vkm_pkru_set_bits(int pkey, u32 perm)
{
	unsigned int domain = get_domain();
	domain &= ~domain_mask(pkey);
	domain = domain | domain_val(pkey, perm);
	set_domain(domain);
    return;
}

static inline u32 mm_vkm_pkru_get_bits(int pkey)
{
	return ((get_domain() >> (pkey << 1)) & 0x3);
}

static inline u32 mm_vkm_pkru_get(void)
{
	return get_domain();
}

static inline void mm_vkm_pkru_reset(bool access)
{
	if (access)
		set_domain(0x55555500 | DACR_INIT);
	else
		set_domain(DACR_INIT);
}

static inline int mm_vkm_idx_to_pkey(int i)
{
	return i + 4;   /* 1 for user default, just like 0, 4 is xok */
}

static inline pte_t mm_vkm_mkpte(pte_t org_pte, int pkey)
{
    return org_pte;
}

static inline pmd_t mm_vkm_mkpmd(pmd_t org_pmd, int pkey)
{
	return __pmd((pmd_val(org_pmd) & (~PMD_DOMAIN(15))) | PMD_DOMAIN(pkey));
}

static inline pud_t mm_vkm_mkpud(pud_t org_pud, int pkey)
{
	return org_pud;
}

static inline void arch_vkm_init(struct mm_struct *mm,
				struct vkey_map_struct *vkm, bool main)
{
	if (main) {
		vkm->pgd = mm->pgd;
		atomic64_set(&vkm->ctx_id, atomic64_read(&mm->context.id));
	} else {
		vkm->pgd = pgd_alloc(mm);
		atomic64_set(&vkm->ctx_id, 0);
	}
}

static __always_inline void
arch_cpumask_clear_vkm(unsigned int cpu, struct cpumask *dstp, struct vkey_map_struct *vkm)
{
	/* First, make sure to clear all related ASID TLB entries, then clear the bit */
	/* needs __flush_tlb_mm(vkm's fake mm) */
	cpumask_clear_cpu(cpu, dstp);
}

static inline bool vkm_pmd_populate(struct mm_struct *mm,
					pmd_t *pmd, pgtable_t pte, int tag)
{
	struct vm_area_struct *vma;
	struct vkey_map_struct *main_vkm;
	int vkey;
	unsigned long aligned_addr;
	unsigned long offset = 20UL;

	if (tag == -1) {
		/* Use the pmd offset to find vma, this is because pmd has 2 * u32 per entry */
		aligned_addr = (((unsigned long)pmd - (unsigned long)(mm->pgd)) / sizeof(u32)) << offset;

		vma = find_vma(mm, aligned_addr);
		main_vkm = mm->main_vkm;
		if (vma) {
			vkey = vma->vm_vkey & VMA_VKEY_MASK;
			if (vkey) {
				if (main_vkm) {
					int i;
					/* Find the mapping in the main vkm */
					tag = execute_only_pkey(mm);
					spin_lock(&main_vkm->slock);
					for (i = 0; i < arch_max_pkey() - 1; i++) {
						if (main_vkm->pkey_vkey[i] == vkey) {
							tag = mm_vkm_idx_to_pkey(i);
							break;
						}
					}
					spin_unlock(&main_vkm->slock);
				} else
					tag = execute_only_pkey(mm);
			} else
				tag = DOMAIN_USER;
		} else
			tag = DOMAIN_USER;
	}

	pmd_populate_tag(mm, pmd, pte, tag);
	return true;
}

static inline int mm_vkm_pmd_get_pkey(pmd_t pmd)
{
	pmdval_t val = pmd_val(pmd);
	return (val & PMD_DOMAIN_MASK) >> 5;
}

static inline bool vkm_mod_pmd_fast(struct mm_struct *mm,
			pmd_t *pmd, pgtable_t pte, int tag,
			unsigned long start, unsigned long end)
{
	return vkm_pmd_populate(mm, pmd, pte, tag);
}

static inline bool mm_vkm_is_reserved_pk_fault(unsigned int flags)
{
	return false;
}

#endif

#endif
