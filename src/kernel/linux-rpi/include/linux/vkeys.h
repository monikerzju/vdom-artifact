#ifndef _LINUX_VKEYS_H
#define _LINUX_VKEYS_H

#include <linux/mm.h>
#include <linux/vkey_types.h>
#include <linux/types.h>
#include <asm/vkeys.h>
#include <asm/uaccess.h>

#define vkey_kgd_offset(vkey)		((vkey >> const_ilog2(VKEY_KTE_ENTRIES)) & ((1U << const_ilog2(VKEY_KGD_ENTRIES)) - 1U))
#define vkey_kte_offset(vkey)		(vkey & ((1U << const_ilog2(VKEY_KTE_ENTRIES)) - 1U))
#define vkru_byte_offset(vkey)		(vkey >> 2U)

#ifdef CONFIG_HAS_VPK

#define calc_vm_vkey_bits_unmasked(key) (		\
		((key) & 0x1   ? VM_VKEY_BIT0 : 0) |      \
		((key) & 0x2   ? VM_VKEY_BIT1 : 0) |      \
		((key) & 0x4   ? VM_VKEY_BIT2 : 0) |      \
		((key) & 0x8   ? VM_VKEY_BIT3 : 0) |      \
        ((key) & 0x10  ? VM_VKEY_BIT4 : 0) |      \
		((key) & 0x20  ? VM_VKEY_BIT5 : 0) |      \
		((key) & 0x40  ? VM_VKEY_BIT6 : 0) |      \
		((key) & 0x80  ? VM_VKEY_BIT7 : 0) |      \
        ((key) & 0x100 ? VM_VKEY_BIT8 : 0) |      \
		((key) & 0x200 ? VM_VKEY_BIT9 : 0))

static inline bool mm_vkey_is_allocated(struct mm_struct *mm, int vkey)
{
	if (vkey < 0 || vkey >= arch_max_vkey())
		return false;

	return test_bit(vkey, mm->vkey.vkey_alloc_bm);
}

/* from a vkey to the vm_flag */
static inline u64 calc_vm_vkey_bits(int vkey)
{
	u64 wide_vkey_vm_flag = calc_vm_vkey_bits_unmasked(vkey);
    return wide_vkey_vm_flag & VMA_VKEY_MASK;
}

int mm_vkey_alloc(struct mm_struct *mm);
int mm_vkey_free(struct mm_struct *mm, int vkey);
int vktramp_mmap_lock(struct vm_area_struct *vma);
int lvkru_mmap_lock(struct vm_area_struct *vma, unsigned long laddr_base);

void walk_vkey_chain(struct mm_struct *mm, int vkey);

static inline int mm_mprotect_vkey(struct vm_area_struct *vma, int vkey)
{
	if (vkey != -1)
		return vkey;
	return (vma->vm_vkey & VMA_VKEY_MASK);
}

#ifndef CONFIG_HAS_VPK_USER_VKRU
static inline int vkey_get_vkrk_permission(struct vkey_vkrk_struct *vkrk, int vkey)
{
	int idx = 2 * vkey / sizeof(unsigned long);
	int oft = vkey % sizeof(unsigned long);	/* Guaranteed to be not overflowed */
	if (vkrk)
		return ((vkrk->bm[idx]) >> (oft << 1U)) & 0x3;
	else
		return VKEY_AD;
}

static inline void vkey_set_vkrk_permission(int vkey, int perm)
{
	int idx = 2 * vkey / sizeof(unsigned long);
	int oft = vkey % sizeof(unsigned long);	/* Guaranteed to be not overflowed */
	struct vkey_vkrk_struct *vkrk = current->vkrk;
	vkrk->bm[idx] &= (~(3UL << (oft << 1U)));
	vkrk->bm[idx] |= ((unsigned long)perm << (oft << 1U));
}
#else
static inline int vkey_get_vkru_permission(struct task_struct *tsk, int vkey)
{
	u8 vkey_oft = vkey & 0x3;
	unsigned long vkru_oft = tsk->vkru - tsk->mm->lvkru_uaddr;
	u8 *vkru_kaddr = (u8 *)(tsk->mm->lvkru_kaddr + vkru_oft + vkru_byte_offset(vkey));
	return tsk->mm->lvkru_kaddr ? (((*vkru_kaddr) >> (vkey_oft << 1U)) & 0x3) : VKEY_AD;
}
#endif

static inline void copy_vktramp_map_fast(int cpu, struct vkey_map_struct *vkm)
{
	extern struct vkey_per_cpu_cl *vktramp;
	unsigned long *vktramp_map_base = (unsigned long *)&vktramp[cpu].map;
	unsigned long *vkm_map_base = (unsigned long *)vkm->pkey_vkey;
	*(vktramp_map_base) = *(vkm_map_base);
	*(vktramp_map_base + 1) = *(vkm_map_base + 1);
}

static inline void copy_vktramp_map(int cpu, struct vkey_map_struct *vkm, vpmap_t *mvk_map, int len)
{
	extern struct vkey_per_cpu_cl *vktramp;
	int i, j;
	vpmap_t *vktramp_map_base = (vpmap_t *)&vktramp[cpu].map;
	if (vkm && mvk_map) {
		vpmap_t *vkm_map_base = vkm->pkey_vkey;
		/* vkm lock should be outside this function */
		/* create the mask for security, not readily synced map may cause */
		/* A -> pkru ND pkey; B -> delete a pkey; C -> another vkey to that pkey; A -> can access C's vkey */
		for (i = 0; i < arch_max_pkey() - 1; i++) {
			*(vktramp_map_base + i) = 0;
			if (*(vkm_map_base + i))
				for (j = 0; j < len; j++)
					if (mvk_map[j] == *(vkm_map_base + i)) {
						*(vktramp_map_base + i) = *(vkm_map_base + i);
						break;
					}
		}
	} else {
		for (i = 0; i < arch_max_pkey() - 1; i++)
			*(vktramp_map_base + i) = 0;
	}
}

#else

static inline bool mm_vkey_is_allocated(struct mm_struct *mm, int vkey)
{
	return false;
}

static inline unsigned long calc_vm_vkey_bits(int vkey)
{
	return 0;
}

static inline int mm_vkey_alloc(struct mm_struct *mm)
{
	return -1;
}

static inline int mm_vkey_free(struct mm_struct *mm, int vkey)
{
	return -1;
}

static inline int mm_mprotect_vkey(struct vm_area_struct *vma, int vkey)
{
	return 0;
}

#endif

void destroy_vkey(struct mm_struct *mm);
extern void vkey_caches_init(void);

#endif
