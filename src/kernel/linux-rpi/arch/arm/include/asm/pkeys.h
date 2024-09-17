#ifndef _ASM_ARM_PKEYS_H
#define _ASM_ARM_PKEYS_H

#include <asm/domain.h>
#define ARCH_DEFAULT_PKEY	DOMAIN_USER

#ifdef CONFIG_HAS_VPK
#define arch_max_pkey() (13)
#define execute_only_pkey(mm) (4)   /* 0 kernel, 1 user, 2 io, 3 vector, 4 for xok, 4 - 15 others */
#define get_execute_only_pkey(mm) (4)
#define arch_override_mprotect_pkey(vma, prot, pkey) (((pkey) == -1) ? vma_pkey(vma) : (pkey))
#define ARCH_VM_PKEY_FLAGS 0    /* never touch 32 bits vm_flags */

static inline int vma_pkey(struct vm_area_struct *vma)
{
	unsigned long arm_domain_mask = 0xffff0000;
    unsigned long arm_domain_shift = 16;
    return (vma->vm_vkey & arm_domain_mask) >> arm_domain_shift;
}

/* This is never called from pure pkey mprotect, just for VPK */
static inline bool mm_pkey_is_allocated(struct mm_struct *mm, int pkey)
{
	if (pkey <= execute_only_pkey(mm))
		return false;
	if (pkey >= arch_max_pkey())
		return false;
    return true;
}

static inline int mm_pkey_alloc(struct mm_struct *mm)
{
	return execute_only_pkey(mm);
}

static inline int mm_pkey_free(struct mm_struct *mm, int pkey)
{
	return 0;
}

static inline int arch_set_user_pkey_access(struct task_struct *tsk, int pkey,
			unsigned long init_val)
{
	return 0;
}

static inline bool arch_pkeys_enabled(void)
{
	return true;
}

#endif  /* CONFIG_HAS_VPK */
#endif
