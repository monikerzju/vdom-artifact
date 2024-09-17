#include <linux/slab.h>
#include <linux/vkeys.h>
#include <linux/log2.h>
#include <linux/vmalloc.h>
#include <linux/mmu_notifier.h>
#include <asm/tlb.h>
#include <asm/ptrace.h>

#ifdef CONFIG_HAS_VPK

static struct kmem_cache *vkey_kgd_cachep;
static struct kmem_cache *vkey_kte_cachep;
struct vkey_per_cpu_cl *vktramp;
spinlock_t vklock;

#define allocate_vkey_kgd()		(kmem_cache_alloc(vkey_kgd_cachep, GFP_KERNEL))
#define free_vkey_kgd(kgd)		(kmem_cache_free(vkey_kgd_cachep, (kgd)))
#define allocate_vkey_kte()		(kmem_cache_alloc(vkey_kte_cachep, GFP_KERNEL))
#define free_vkey_kte(kte)		(kmem_cache_free(vkey_kte_cachep, (kte)))
#define task_stack_page(task)	((void *)(task)->stack)

static inline void init_valid_kgd(struct vkey_kgd_struct *kgd)
{
	int i;
	for (i = 0; i < VKEY_KGD_ENTRIES; i++)
		kgd->ktes[i] = NULL;
}

static inline void init_valid_kte(struct vkey_kte_struct *kte)
{
	int i;
	for (i = 0; i < VKEY_KTE_ENTRIES; i++)
		INIT_LIST_HEAD(&kte->vkey_vma_heads[i]);
}

int mm_vkey_alloc(struct mm_struct *mm)
{
	int ret;
	int kgd_oft;

	ret = find_first_zero_bit(mm->vkey.vkey_alloc_bm, arch_max_vkey());
	if (ret == arch_max_vkey())
		return -1;

	if (unlikely(!mm->vkey.kgd)) {
		mm->vkey.kgd = allocate_vkey_kgd();
		if (!mm->vkey.kgd)
			return -ENOMEM;
		init_valid_kgd(mm->vkey.kgd);
	}

	kgd_oft = vkey_kgd_offset(ret);
	if (!(mm->vkey.kgd->ktes[kgd_oft])) {
		mm->vkey.kgd->ktes[kgd_oft] = allocate_vkey_kte();
		if (!(mm->vkey.kgd->ktes[kgd_oft]))
			return -ENOMEM;
		init_valid_kte(mm->vkey.kgd->ktes[kgd_oft]);
	}

	set_bit(ret, mm->vkey.vkey_alloc_bm);
	return ret;
}

int mm_vkey_free(struct mm_struct *mm, int vkey)
{
	/* vkey deafult should never be freed */
	if (!mm_vkey_is_allocated(mm, vkey) || vkey == ARCH_DEFAULT_VKEY)
		return -EINVAL;

	/* Here, should we go through every vm_area_struct(s) assigned the vkey
	 * to be freed in the current process (mm argument), and set their
	 * vkey to be -1?
	 */
	clear_bit(vkey, mm->vkey.vkey_alloc_bm);

	return 0;
}

void destroy_vkey(struct mm_struct *mm)
{
	struct vkey_kte_struct *kte;
	int i, j;

	/* free all kgd and kte structure, after the broken vma chain, hopefully no UAF */
	if (mm->vkey.kgd) {
		for (i = 0; i < VKEY_KGD_ENTRIES; i++) {
			kte = mm->vkey.kgd->ktes[i];
			if (kte) {
				for (j = 0; j < VKEY_KTE_ENTRIES; j++)
					list_del_init(&kte->vkey_vma_heads[j]);
				free_vkey_kte(kte);
			}
		}
		free_vkey_kgd(mm->vkey.kgd);
	}
}

void __init vkey_caches_init(void)
{
	vkey_kgd_cachep = kmem_cache_create("vkey_kgd_cache",
			sizeof(struct vkey_kgd_struct), 0,
			SLAB_HWCACHE_ALIGN|SLAB_NOLEAKTRACE,
			NULL);
	if (!vkey_kgd_cachep)
		printk(KERN_ERR "[%s] vkey kgd slab initialization failed...\n", __func__);
	else
		printk(KERN_INFO "[%s] vkey kgd slab initialized\n", __func__);

	vkey_kte_cachep = kmem_cache_create("vkey_kte_cache",
			sizeof(struct vkey_kte_struct), 0,
			SLAB_HWCACHE_ALIGN|SLAB_NOLEAKTRACE,
			NULL);
	if (!vkey_kte_cachep)
		printk(KERN_ERR "[%s] vkey kte slab initialization failed...\n", __func__);
	else
		printk(KERN_INFO "[%s] vkey kte slab initialized\n", __func__);

	vktramp = kzalloc((cpumask_size() * sizeof(struct vkey_per_cpu_cl) +
						PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE, GFP_KERNEL);
	if (!vktramp)
		printk(KERN_ERR "[%s] vkey trampoline initialization failed...\n", __func__);
	else
		printk(KERN_INFO "[%s] vkey trampoline initialized\n", __func__);
}

void walk_vkey_chain(struct mm_struct *mm, int vkey)
{
	struct list_head *pos;
	struct vm_area_struct *vma;
	list_for_each(pos, &(mm->vkey.kgd->ktes[vkey_kgd_offset(vkey)]->vkey_vma_heads[vkey_kte_offset(vkey)])) {
		vma = list_entry(pos, struct vm_area_struct, vkey_chain);
		printk(KERN_INFO "[%s] [start, end) = [%lx, %lx)\n", __func__, vma->vm_start, vma->vm_end);
	}
}

#ifdef CONFIG_HAS_VPK_USER_VKRU
int lvkru_mmap_lock(struct vm_area_struct *vma, unsigned long laddr_base)
{
	void *lvkru_kbase;
	struct page *pages[VKEY_META_DATA_SZ / PAGE_SIZE];

	if (vma->vm_end - vma->vm_start != VKEY_META_DATA_SZ) {
		printk(KERN_ERR "[%s] the allocated anonymous memory is not proper...\n", __func__);
		return -EINVAL;
	}

	if (get_user_pages(laddr_base, VKEY_META_DATA_SZ / PAGE_SIZE, 0,
			pages, NULL) != VKEY_META_DATA_SZ / PAGE_SIZE) {
		printk(KERN_ERR "[%s] failed to get user pages...\n", __func__);
		return -EINVAL;
	}

	lvkru_kbase = vmap(pages, VKEY_META_DATA_SZ / PAGE_SIZE, VM_MAP, PAGE_KERNEL);
	vma->vm_mm->lvkru_kaddr = lvkru_kbase;

	if (lvkru_kbase)
		return 0;
	printk(KERN_ERR "[%s] failed to vmap the user pages...\n", __func__);
	return -EINVAL;
}

/* Map this to user as read only, then lock against mprotect, mremap, etc. */
int vktramp_mmap_lock(struct vm_area_struct *vma)
{
	struct page *page;
	struct mmu_notifier_range range;
	struct mmu_gather tlb;

	if (vma->vm_end - vma->vm_start != (cpumask_size() * sizeof(struct vkey_per_cpu_cl) +
						PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE) {
		printk(KERN_ERR "[%s] the allocated anonymous memory is not proper...\n", __func__);
		return -EINVAL;
	}

	page = virt_to_page((unsigned long)vktramp);
	if (page) {
		int ret;
		lru_add_drain();
		mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma, vma->vm_mm,
				vma->vm_start, vma->vm_end);
		tlb_gather_mmu(&tlb, vma->vm_mm);
		update_hiwater_rss(vma->vm_mm);
		mmu_notifier_invalidate_range_start(&range);
		ret = remap_pfn_range(vma, vma->vm_start, page_to_pfn(page), vma->vm_end - vma->vm_start, vma->vm_page_prot);
		mmu_notifier_invalidate_range_end(&range);
		tlb_finish_mmu(&tlb);
		return ret;
	}
	return -EINVAL;
}

inline void vkey_thread_check_migrate(struct task_struct *tsk)
{
	// FIXME: [VDom] assign in_user_critical_section!!! And this func should be called as early as possible!!!
	// may be as soon as "->__state, ", what about migrate_disable(); and migrate_enable();?
	const unsigned long ulib_start = 0x0, ulib_end = 0x1000;
	if (tsk->vkru) {
		struct pt_regs *regs = task_pt_regs(tsk);
		struct vm_area_struct *vma = tsk->mm ? tsk->mm->lvkey_code_vma : NULL;
		if (vma) {
			bool in_user_critical_section = (regs->ip >= vma->vm_start + ulib_start && regs->ip < vma->vm_start + ulib_end);
			if (unlikely(in_user_critical_section)) {
				if (tsk->vkey_can_load_balance)
					tsk->vkey_can_load_balance = false;
			} else if (unlikely(!tsk->vkey_can_load_balance))
				tsk->vkey_can_load_balance = true;
		}
	}
}
#endif

#else

void destroy_vkey(struct mm_struct *mm)
{

}

void __init vkey_caches_init(void)
{

}

#endif
