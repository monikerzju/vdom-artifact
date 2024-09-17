#ifndef _ASM_X86_VKEYS_H
#define _ASM_X86_VKEYS_H

#define VMA_VKEY_MASK	0xffffffff
#define ARCH_VPK_WRITE_PTE

#define VKEY_AD 0x1		/* access  disable */
#define VKEY_WD 0x2		/* write   disable */
#define VKEY_ND 0x0		/* nothing disable */
#define VKEY_PINNED 0x3
#define VKEY_MASK 0x5

#ifdef CONFIG_HAS_VPK
#include <linux/cpumask.h>
#include <asm/pgalloc.h>
#include <asm/paravirt.h>
#include <asm/mmu_context.h>
#include <asm/trap_pf.h>

extern atomic64_t last_mm_ctx_id;

#define get_execute_only_pkey(mm) (1)
#define flush_tlb_vkm_range(vma, vkm)				\
	flush_tlb_mm_range((vma)->vm_mm, (vma)->vm_start, (vma)->vm_end,	\
				((vma)->vm_flags & VM_HUGETLB) ? 	\
				huge_page_shift(hstate_vma(vma))	\
				: PAGE_SHIFT, false, vkm)
#define flush_tlb_vkm_page(addr, vkm)				\
	flush_tlb_mm_range((vma)->vm_mm, (addr) & PAGE_MASK, ((addr) & PAGE_MASK) + PAGE_SIZE,	\
				((vma)->vm_flags & VM_HUGETLB) ? 	\
				huge_page_shift(hstate_vma(vma))	\
				: PAGE_SHIFT, false, vkm)

static inline unsigned long mm_vkm_pmd_pkey(pmd_t pmd, int pkey)
{
	pmdval_t val = pmd_val(pmd);
	val &= (pmdval_t)(~_PAGE_PKEY_MASK);
	val |= (pmdval_t)(
		(pkey & 0x1 ? _PAGE_PKEY_BIT0 : 0) |	\
		(pkey & 0x2 ? _PAGE_PKEY_BIT1 : 0) |	\
		(pkey & 0x4 ? _PAGE_PKEY_BIT2 : 0) |	\
		(pkey & 0x8 ? _PAGE_PKEY_BIT3 : 0)
	);
	return val;
}

static inline int mm_vkm_pte_get_pkey(pte_t pte)
{
	pteval_t val = pte_val(pte);
	return (val & _PAGE_PKEY_MASK) >> _PAGE_BIT_PKEY_BIT0;
}

static inline bool vkm_pmd_populate(struct mm_struct *mm,
					pmd_t *pmd, pgtable_t pte, int tag)
{
	return false;
}

static inline bool get_vkm_pmd_taint(pmd_t *pmd)
{
	return (pmd_val(*pmd) & _PAGE_RESERVED_MASK);
}

static inline void vkm_pmd_taint(struct mm_struct *mm,
			unsigned long addr, pmd_t *pmd, bool taint)
{
	pmd_t reserved_pmd = *pmd;
	pmdval_t val = pmd_val(reserved_pmd);
	if (taint)
		val |= (pmdval_t)(_PAGE_RESERVED_MASK);
	else
		val &= (pmdval_t)(~_PAGE_RESERVED_MASK);
	reserved_pmd = __pmd(val);
	set_pmd_at(mm, addr, pmd, reserved_pmd);
}

static inline bool vkm_mod_pmd_fast(struct mm_struct *mm,
			pmd_t *pmd, pgtable_t pte, int tag,
			unsigned long start, unsigned long end)
{
	bool skip_ptes = false;

	if ((start & PMD_MASK) == start && start + PMD_SIZE <= end) {	/* pmd aligned start and end */
		if (tag == get_execute_only_pkey(mm)) {
			vkm_pmd_taint(mm, start, pmd, true);
			skip_ptes = true;
		} else if (get_vkm_pmd_taint(pmd))
			vkm_pmd_taint(mm, start, pmd, false);
	}

	return skip_ptes;
}

static inline void mm_vkm_pkru_set_bits(int pkey, u32 perm)
{
	u32 pkru = rdpkru();
	pkru &= (~(0x3 << (pkey << 1)));
	pkru |= (perm << (pkey << 1));
	wrpkru(pkru);
	return;
}

static inline u32 mm_vkm_pkru_get_bits(int pkey)
{
	return ((rdpkru() >> (pkey << 1)) & 0x3);
}

static inline u32 mm_vkm_pkru_get(void)
{
	return rdpkru();
}

static inline void mm_vkm_pkru_reset(bool access)
{
	u32 pkru = rdpkru();
	pkru &= 0x0000000c;
	if (!access)
		pkru |= 0x55555550;
	wrpkru(pkru);
}

static inline int mm_vkm_idx_to_pkey(int i)
{
	return i + 1;
}

static inline pte_t mm_vkm_mkpte(pte_t org_pte, int pkey)
{
	return __pte(mm_vkm_pmd_pkey(__pmd(pte_val(org_pte)), pkey));
}

static inline pmd_t mm_vkm_mkpmd(pmd_t org_pmd, int pkey)
{
	return __pmd(mm_vkm_pmd_pkey(org_pmd, pkey));
}

static inline pud_t mm_vkm_mkpud(pud_t org_pud, int pkey)
{
	return __pud(mm_vkm_pmd_pkey(__pmd(pud_val(org_pud)), pkey));
}

static __always_inline void
arch_cpumask_clear_vkm(unsigned int cpu, struct cpumask *dstp, struct vkey_map_struct *vkm) {}

static inline void arch_vkm_init(struct mm_struct *mm,
				struct vkey_map_struct *vkm, bool main)
{
	if (main) {
		vkm->pgd = mm->pgd;
		vkm->ctx_id = mm->context.ctx_id;
	} else {
		vkm->pgd = pgd_alloc(mm);
		vkm->ctx_id = atomic64_inc_return(&last_mm_ctx_id);
	}
	atomic64_set(&vkm->tlb_gen, 0);
}

static inline bool mm_vkm_is_reserved_pk_fault(unsigned int flags)
{
	return flags & X86_PF_RSVD;
}

#endif

#endif