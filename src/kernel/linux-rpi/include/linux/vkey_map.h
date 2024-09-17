#ifndef _LINUX_VKEY_MAP_H
#define _LINUX_VKEY_MAP_H

#include <linux/vkey_types.h>
#include <linux/mm.h>

#ifdef CONFIG_HAS_VPK

/* These functions are not thread-free yet all mm-related, so need mmap-locks in the caller. */

/* Allocate a vkm and initialize it, returns NULL if pgd or vkm failed allocation. */
struct vkey_map_struct *mm_vkm_alloc_init(struct mm_struct *mm);

/* Judge first. */
bool mm_vkm_can_add(struct vkey_map_struct *vkm, vpmap_t *current_vkeys, int len);

/* Add the one vkey to the vkey mapping. */
vpmap_t mm_vkm_add_vkey(struct vkey_map_struct *vkm, int vkey, int evicted, int xok, int perm);

/* Add the current vkeys to the vkey mapping. */
void mm_vkm_add(struct vkey_map_struct *vkm, struct mapped_vkey_struct *mvk, int len, int xok, int *current_perm);

/* Delete the vkeys in the vkey mapping and free the whole structure if all vkeys are freed */
void mm_vkm_del(struct vkey_map_struct *vkm, struct mm_struct *mm, vpmap_t *current_vkeys, int len, int *current_perm, int evicted);

void mm_vkm_free_page_table(struct mmu_gather *tlb, struct vm_area_struct *vma,
				struct vkey_map_struct *vkm, unsigned long floor, unsigned long ceiling);

void walk_vkey_map(struct mm_struct *mm);

inline struct mapped_vkey_struct *tsk_mvk_alloc(void);
inline struct mapped_vkey_struct **tsk_mvk_arr_alloc(void);
inline struct vkey_map_struct **tsk_vkm_arr_alloc(void);
inline struct vkey_vkrk_struct *tsk_vkrk_alloc(void);
inline void tsk_mvk_free(struct mapped_vkey_struct *mvk);
inline void tsk_vkrk_free(struct vkey_vkrk_struct *vkrk);
inline void tsk_mvk_arr_free(struct mapped_vkey_struct **mvk);
inline void tsk_vkm_arr_free(struct vkey_map_struct **vkm);

int mm_vkm_mod_p4d_range(struct vm_area_struct *vma,
	    pgd_t *dst_pgd, pgd_t *src_pgd, unsigned long addr,
	    unsigned long end, int pkey, bool cp, bool one_pte);
#endif

extern void vkey_map_caches_init(void);
void destroy_vkey_map(struct mm_struct *mm);
void free_vkey_map(struct mm_struct *mm);

#endif
