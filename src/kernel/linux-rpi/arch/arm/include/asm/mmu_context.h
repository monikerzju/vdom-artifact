/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/mmu_context.h
 *
 *  Copyright (C) 1996 Russell King.
 *
 *  Changelog:
 *   27-06-1996	RMK	Created
 */
#ifndef __ASM_ARM_MMU_CONTEXT_H
#define __ASM_ARM_MMU_CONTEXT_H

#include <linux/compiler.h>
#include <linux/sched.h>
#include <linux/mm_types.h>
#include <linux/preempt.h>

#include <asm/cacheflush.h>
#include <asm/cachetype.h>
#include <asm/proc-fns.h>
#include <asm/smp_plat.h>
#include <asm-generic/mm_hooks.h>

#ifdef CONFIG_HAS_VPK
DECLARE_PER_CPU(struct vkey_map_struct *, loaded_vkm);
#ifdef CONFIG_UACCESS_WITH_MEMCPY
#error "CONFIG_HAS_VPK is not compatible with CONFIG_UACCESS_WITH_MEMCPY"
#endif
#endif

void __check_vmalloc_seq(struct mm_struct *mm);

#ifdef CONFIG_CPU_HAS_ASID

void check_and_switch_context(struct mm_struct *mm, struct task_struct *tsk);
#ifdef CONFIG_HAS_VPK
void check_and_switch_context_fast(struct mm_struct *mm, struct task_struct *tsk);
#endif

#define init_new_context init_new_context
static inline int
init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	atomic64_set(&mm->context.id, 0);
	return 0;
}

#ifdef CONFIG_ARM_ERRATA_798181
void a15_erratum_get_cpumask(int this_cpu, struct mm_struct *mm,
			     cpumask_t *mask);
#else  /* !CONFIG_ARM_ERRATA_798181 */
static inline void a15_erratum_get_cpumask(int this_cpu, struct mm_struct *mm,
					   cpumask_t *mask)
{
}
#endif /* CONFIG_ARM_ERRATA_798181 */

#else	/* !CONFIG_CPU_HAS_ASID */

#ifdef CONFIG_MMU

static inline void check_and_switch_context(struct mm_struct *mm,
					    struct task_struct *tsk)
{
	if (unlikely(mm->context.vmalloc_seq != init_mm.context.vmalloc_seq))
		__check_vmalloc_seq(mm);

	if (irqs_disabled())
		/*
		 * cpu_switch_mm() needs to flush the VIVT caches. To avoid
		 * high interrupt latencies, defer the call and continue
		 * running with the old mm. Since we only support UP systems
		 * on non-ASID CPUs, the old mm will remain valid until the
		 * finish_arch_post_lock_switch() call.
		 */
		mm->context.switch_pending = 1;
	else {
#ifdef CONFIG_HAS_VPK
		if (tsk && tsk->vkm)
			cpu_switch_vkm(tsk->vkm);
		else
#endif
		cpu_switch_mm(mm->pgd, mm);
	}
}

#ifndef MODULE
#define finish_arch_post_lock_switch \
	finish_arch_post_lock_switch
static inline void finish_arch_post_lock_switch(void)
{
	struct mm_struct *mm = current->mm;

	if (mm && mm->context.switch_pending) {
		/*
		 * Preemption must be disabled during cpu_switch_mm() as we
		 * have some stateful cache flush implementations. Check
		 * switch_pending again in case we were preempted and the
		 * switch to this mm was already done.
		 */
		preempt_disable();
		if (mm->context.switch_pending) {
			mm->context.switch_pending = 0;
#ifdef CONFIG_HAS_VPK
		if (current->vkm && current->vkm->pgd != vkm->pgd)
			cpu_switch_vkm(current->vkm);
		else
#endif
			cpu_switch_mm(mm->pgd, mm);
		}
		preempt_enable_no_resched();
	}
}
#endif /* !MODULE */

#endif	/* CONFIG_MMU */

#endif	/* CONFIG_CPU_HAS_ASID */

#define activate_mm(prev,next)		switch_mm(prev, next, NULL)

#ifdef CONFIG_HAS_VPK
static inline void
switch_mm_fast(struct mm_struct *mm, struct task_struct *tsk)
{
#ifdef CONFIG_MMU
	unsigned int cpu = smp_processor_id();
	struct vkey_map_struct *new_vkm;

	if (cache_ops_need_broadcast() &&
	    !cpumask_empty(mm_cpumask(mm)) &&
	    !cpumask_test_cpu(cpu, mm_cpumask(mm)))
		__flush_icache_all();

	new_vkm = tsk->vkm;
	cpumask_set_cpu(cpu, vkm_cpumask(new_vkm));

	check_and_switch_context_fast(mm, tsk);
#endif
}
#endif

/*
 * This is the actual mm switch as far as the scheduler
 * is concerned.  No registers are touched.  We avoid
 * calling the CPU specific function when the mm hasn't
 * actually changed.
 */
static inline void
switch_mm(struct mm_struct *prev, struct mm_struct *next,
	  struct task_struct *tsk)
{
#ifdef CONFIG_MMU
	unsigned int cpu = smp_processor_id();
#ifdef CONFIG_HAS_VPK
	struct vkey_map_struct *new_vkm;
	struct vkey_map_struct *prev_vkm;
	bool is_vkm_new;
#endif

	/*
	 * __sync_icache_dcache doesn't broadcast the I-cache invalidation,
	 * so check for possible thread migration and invalidate the I-cache
	 * if we're new to this CPU.
	 */
	if (cache_ops_need_broadcast() &&
	    !cpumask_empty(mm_cpumask(next)) &&
	    !cpumask_test_cpu(cpu, mm_cpumask(next)))
		__flush_icache_all();

#ifdef CONFIG_HAS_VPK
	prev_vkm = per_cpu(loaded_vkm, cpu);
	if (tsk)
		new_vkm = tsk->vkm;
	else
		new_vkm = NULL;
	if (new_vkm)
		is_vkm_new = !cpumask_test_and_set_cpu(cpu, vkm_cpumask(new_vkm));
	else
		is_vkm_new = false;
	if (!cpumask_test_and_set_cpu(cpu, mm_cpumask(next)) ||	/* set the mm bitmap anyway */
			is_vkm_new || prev != next || prev_vkm != new_vkm) {
		check_and_switch_context(next, tsk);
		if (cache_is_vivt()) {
			if (prev->main_vkm) {	/* only clear the right vkm */
				if (likely(prev_vkm))
					cpumask_clear_cpu(cpu, vkm_cpumask(prev_vkm));
				else
					cpumask_clear_cpu(cpu, vkm_cpumask(prev->main_vkm));
			} else
				cpumask_clear_cpu(cpu, mm_cpumask(prev));
		}
	}
#else	/* !CONFIG_HAS_VPK */
	if (!cpumask_test_and_set_cpu(cpu, mm_cpumask(next)) || prev != next) {
		check_and_switch_context(next, tsk);
		if (cache_is_vivt())
			cpumask_clear_cpu(cpu, mm_cpumask(prev));
	}
#endif	/* CONFIG_HAS_VPK */
#endif
}

#include <asm-generic/mmu_context.h>

#endif
