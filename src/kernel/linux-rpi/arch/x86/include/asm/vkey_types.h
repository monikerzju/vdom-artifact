#ifndef _ASM_X86_VKEY_TYPES_H
#define _ASM_X86_VKEY_TYPES_H

#ifdef CONFIG_HAS_VPK
#define arch_max_pkey()		(16)
typedef u64 vkctx_t;
#endif

#endif