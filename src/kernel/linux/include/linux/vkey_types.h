#ifndef _LINUX_VKEY_TYPES_H
#define _LINUX_VKEY_TYPES_H

#include <linux/bitmap.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <asm/mmu.h>
#ifdef CONFIG_HAS_VPK
#include <asm/vkey_types.h>
#else
typedef int vkctx_t;
#define arch_max_pkey()		(1)
#endif

/* 128 virt pkey per proc */
#define arch_max_vkey()		(128)
#define VKEY_KGD_ENTRIES	(8)
#define VKEY_KTE_ENTRIES	(16)
#define ARCH_DEFAULT_VKEY	(0)
#define MAX_ACTIVE_VKEYS	(arch_max_pkey() - 2 < 0 ? 0 : arch_max_pkey() - 2)
#define MAX_ADDR_SPACE_PER_THREAD	(6)
#define VKEY_META_DATA_SZ	(8192)

#if arch_max_vkey() > (1024 * 64)
typedef unsigned long vpmap_t;
#define VPMAP_LONGS		(15)
#elif arch_max_vkey() > (256)
typedef u16 vpmap_t;
#define VPMAP_LONGS		(4)
#else
typedef u8 vpmap_t;
#define VPMAP_LONGS		(2)
#endif

struct vkey_vkrk_struct {
	unsigned long bm[arch_max_vkey() * 2 / sizeof(unsigned long)];
};

/*
 * This is the per-domain vkey mapping.
 * When crossing domain, both pgd and pkru register should be switched
 * vkey_dmap_struct bookkeeps:
 * - lru_array:			all pkeys and their vkeys and number of active thread
 * - nr_thread:			number of threads in this mapping
 * - pgd:				the root of the shared address space with different keys
 * Specifically, the vkm_pgd should be dropped when there are no threads in this domain.
 * However, if the vkm_pgd equals its mm->pgd, it shall not be dropped.
 * When vkm is created, if the mm->vkm_chain is empty, the vkm_pgd is copied rather than allocated.
 */
struct vkey_map_struct {
	spinlock_t slock;
	pgd_t *pgd;
	vkctx_t ctx_id;
	atomic64_t tlb_gen;
	int nr_thread;
	struct {
		int pkey_nr_thread[arch_max_pkey() - 1];
		vpmap_t pkey_vkey[arch_max_pkey() - 1];	/* For stronger sec, this should be in another page */
	};
	struct list_head vkm_chain;	/* protected by mm->vkey_lock in PF, and mmap write semophore otherwise */
	/* This varies with the number of CPUs. */
	unsigned long cpu_bitmap[];
};

struct mapped_vkey_struct {
	vpmap_t map[MAX_ACTIVE_VKEYS];
	vpmap_t pmap[MAX_ACTIVE_VKEYS];
	u32 pkru;
	int ts[MAX_ACTIVE_VKEYS];
};

struct vkey_kgd_struct {
	struct vkey_kte_struct *ktes[VKEY_KGD_ENTRIES];
};

struct vkey_kte_struct {
	struct list_head vkey_vma_heads[VKEY_KTE_ENTRIES];
};

struct vkey_per_cpu_cl {
	unsigned long vkru;
	unsigned long map;		/* Note that this is insecure and let the kernel attacker know the pgd easily */
	char fillings[L1_CACHE_BYTES - 2 * sizeof(unsigned long)];
};

/*
 * This is a per-proc structure and a field in mm_struct (as a ptr).
 * vkey_struct bookkeeps:
 * - vkey allocation bitmap (mutex)
 * - 
 */
struct vkey_struct {
	DECLARE_BITMAP(vkey_alloc_bm, arch_max_vkey());
	struct vkey_kgd_struct *kgd;
};

static inline cpumask_t *vkm_cpumask(struct vkey_map_struct *vkm)
{
	return (struct cpumask *)&vkm->cpu_bitmap;
}

#endif
