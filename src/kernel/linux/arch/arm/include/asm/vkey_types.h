#ifndef _ASM_ARM_VKEY_TYPES_H
#define _ASM_ARM_VKEY_TYPES_H

#ifdef CONFIG_HAS_VPK

/* 16 - DOM_KERN - DOM_IO, 1 is default, so 3 is xok */
#define arch_max_pkey()		(13)
typedef atomic64_t vkctx_t;

#endif  /* CONFIG_HAS_VPK */

#endif