#include "libvkeys.h"
#include <sys/syscall.h>
#include <sys/mman.h>
#include <stddef.h>

// Definition Derivatives
#define MAX_VKRU_PER_PROC       ((VKEY_META_DATA_SZ - 1) / (INTEL_L1_BYTES + 1))  /* 1 bit for allocation bm */
#define VKEY_LOCK_OFFSET        (INTEL_L1_BYTES / 2)        /* 8 bytes, unsigned long */
#define VKRU_OFFSET             ((MAX_VKRU_PER_PROC / 8 + INTEL_L1_BYTES - 1) / INTEL_L1_BYTES)
#define VKEY_TRAMP_ACT_SZ       (4096 * 1)
#define VKEY_XOK                (1)
#define arch_max_pkey()         (16)
#define arch_max_vkey()         (128)
#define GDT_ENTRY_PER_CPU       15	/* Abused to load per CPU data from limit */
#define __PER_CPU_SEG	        (GDT_ENTRY_PER_CPU * 8 + 3)
#if arch_max_vkey() > (1024 * 64)
typedef unsigned long vpmap_t;
#define VPMAP_LONGS		15
#elif arch_max_vkey() > (256)
typedef u16 vpmap_t;
#define VPMAP_LONGS		4
#else
typedef char vpmap_t;
#define VPMAP_LONGS		2
#endif



// Protected Data
__attribute__((aligned(4096))) static char red_zone[4096];
__attribute__((aligned(4096))) static char vkru_meta_and_data[VKEY_META_DATA_SZ];
__attribute__((aligned(4096))) static char vkru_ro_tramp[VKEY_TRAMP_ACT_SZ];




// Static Functions
static inline unsigned int __getcpu(void)
{
       unsigned int p;

       /*
        * Load per CPU data from GDT.  LSL is faster than RDTSCP and
        * works on all CPUs.  This is volatile so that it orders
        * correctly wrt barrier() and to keep gcc from cleverly
        * hoisting it out of the calling function.
        */
       asm volatile ("lsl %1,%0" : "=r" (p) : "r" (__PER_CPU_SEG));

       return (p & 0xfff);
}

static inline int vkey_reg_lib(void *laddr, void *taddr)
{
    return syscall(syscall_vkey_reg_lib, laddr, taddr);
}

static inline vkru_t **get_current_thread_vkru(void)
{
    return (vkru_t **)((unsigned long)vkru_ro_tramp + __getcpu() * INTEL_L1_BYTES);
}

static inline int get_bm_offset(char bm)
{       
    if (!(bm & 0x01))               
        return 0;                   
    if (!(bm & 0x02))               
        return 1;                   
    if (!(bm & 0x04))               
        return 2;                   
    if (!(bm & 0x08))               
        return 3;                   
    if (!(bm & 0x10))               
        return 4;                   
    if (!(bm & 0x20))               
        return 5;                   
    if (!(bm & 0x40))               
        return 6;                   
    return 7;                       
}

static inline void lock_bm()
{
    volatile unsigned long *lock = (unsigned long *)(vkru_meta_and_data + VKEY_LOCK_OFFSET);
    while (!__sync_bool_compare_and_swap(lock, 0, 1)) {
        while (*lock);
    }
}

static inline void unlock_bm()
{
    asm volatile ("":::"memory");
    *(unsigned long *)(vkru_meta_and_data + VKEY_LOCK_OFFSET) = 0;
}

static inline unsigned int rdpkru(void)
{
	unsigned int ecx = 0;
	unsigned int edx, pkru;

	/*
	 * "rdpkru" instruction.  Places PKRU contents in to EAX,
	 * clears EDX and requires that ecx=0.
	 */
	asm volatile(".byte 0x0f,0x01,0xee\n\t"
		     : "=a" (pkru), "=d" (edx)
		     : "c" (ecx));
	return pkru;
}

static inline void wrpkru(unsigned int pkru)
{
	unsigned int ecx = 0, edx = 0;

	/*
	 * "wrpkru" instruction.  Loads contents in EAX to PKRU,
	 * requires that ecx = edx = 0.
	 */
	asm volatile(".byte 0x0f,0x01,0xef\n\t"
		     : : "a" (pkru), "c"(ecx), "d"(edx));
}

static inline unsigned int pkru_set_bits_dis(int pkey, int perm) 
{
	unsigned int pkru;
	pkru = rdpkru();
	if (pkey != arch_max_pkey()) {
		pkru &= (~(0x3 << (pkey << 1)));
		pkru |= ((perm & 0x3) << (pkey << 1));
#ifndef VKEY_SAFE
        pkru |= 0x4;
        wrpkru(pkru);
#endif
	}
#ifdef VKEY_SAFE
	pkru |= 0x4;
	wrpkru(pkru);
#endif
}

static inline int vkey_init_c(bool sprot)
{
    // push ebp, mov ebp esp... prologue
    // Initialize VKeyS in kernel.
    //      1. Get the address in the process.
    //      2. Syscall to register the library and lock the corresponding VMA.
    //      3. Allocate the HW_CACHE_ALIGN (to avoid coherence protocol) VKRU bm.
    //      4. VKRU activates self protection.
    //      5. Get the fixed kernel mappings of vkey-pkey mapping of different VKSes.
    int ret;

    // Step 3
    for (register int i = 0; i < MAX_VKRU_PER_PROC / 8; i++)
        vkru_meta_and_data[i] = 0;
    *(unsigned long *)(vkru_meta_and_data + VKEY_LOCK_OFFSET) = 0;

    // Step 1, 2 and 4
    if (pkey_mprotect(vkru_ro_tramp, VKEY_TRAMP_ACT_SZ, PROT_READ, 0) ||
        pkey_mprotect(red_zone, 4096, PROT_READ, 0))
        return -1;
    if (vkey_reg_lib(vkru_meta_and_data, vkru_ro_tramp))
        return -2;
#ifdef VKEY_SAFE
    if (pkey_mprotect(vkru_meta_and_data, VKEY_META_DATA_SZ, PROT_READ | PROT_WRITE, VKEY_SPROT))
#else
    if (pkey_mprotect(vkru_meta_and_data, VKEY_META_DATA_SZ, PROT_READ | PROT_WRITE, 0))
#endif
        return -3;

    return 0;
}

static inline vkru_t *vkru_alloc_c(int nas)
{
    vkru_t *ret = NULL;
    int i;

    lock_bm();   /* spinlock */
    for (i = 0; i < MAX_VKRU_PER_PROC / 8; i++)
        if (vkru_meta_and_data[i] != -1) {
            if (syscall(syscall_vkey_reg_vkru, (unsigned long)
                (ret = (vkru_t *)(vkru_meta_and_data + INTEL_L1_BYTES * VKRU_OFFSET) +
                 8 * i + get_bm_offset(vkru_meta_and_data[i])), nas) == 0)
                vkru_meta_and_data[i] |= (1 << get_bm_offset(vkru_meta_and_data[i]));
            break;
        }
    unlock_bm();
    return ret;
}

static inline void vkru_free_c(void)
{
    lock_bm();
    // Free the metadata in userspace, then check if the thread kills its own vkru
    vkru_t *vkru = *get_current_thread_vkru();
    if (syscall(syscall_vkey_reg_vkru, NULL))
        vkru_meta_and_data[((unsigned long)vkru - (unsigned long)vkru_meta_and_data - INTEL_L1_BYTES * VKRU_OFFSET) / sizeof(vkru_t) / 8] &= 
            (~(0x1 << ((((unsigned long)vkru - (unsigned long)vkru_meta_and_data - INTEL_L1_BYTES * VKRU_OFFSET) / sizeof(vkru_t)) & 0x7)));
    unlock_bm();
}

static inline long wrvkru_c(int vkey, int perm, vkru_t **vkrupp)
{
    // Write perm to the VKRU of the thread, if the VKS activates the vkey, wrpkru too.
    //      1. Go through the call gate (1st wrpkru) to get permission to write (any) VKRU.
    //      2. Get which VKS and which VKRU to write through kernel-mapped vDSO for security issues.
    //      3. Change the permission of VKRU and maybe 2nd wrpkru.
    //      4. Use the 3rd wrpkru and switch back to the untrusted world.

    vkru_t *vkru;
    vpmap_t *vkm;
    register long i;

    // TODO: Step 1 and 4 for security

    // Step 2 and 3
    vkru = *vkrupp;
    vkm = (vpmap_t *)((void *)vkrupp + 8);
    for (i = VKEY_XOK; i < arch_max_pkey() - 1; i++)
        if (vkm[i] == vkey)
            break;
    if (vkru) {
        vkru->perm[vkey / 4] &= (~(0x3 << (2 * (vkey & 0x3))));
        vkru->perm[vkey / 4] |= ((perm & 0x3) << (2 * (vkey & 0x3)));
    }
    return i;
}




// Common API
// Here, we must guarantee that even with ROP
int vkey_init(register bool sprot)
{
    register long ret;

    ret = vkey_init_c(sprot);

    return ret;
}

// No need to protect
int vkey_alloc(void)
{
    // Simple wrapper for syscall.
    return syscall(syscall_vkey_alloc);
}

// No need to protect
int vkey_free(int vkey)
{
    // Simple wrapper for syscall.
    return syscall(syscall_vkey_free, vkey);
}

// No need to protect
int vkey_mprotect(void *addr, size_t len, int vkey)
{
    // Simple wrapper for syscall.
    return syscall(syscall_vkey_mprotect, addr, len, PROT_READ | PROT_WRITE, vkey);
}

vkru_t *vkru_alloc(int nas)
{
    register vkru_t *ret;

#ifdef VKEY_SAFE
    asm volatile (
        "xorl %%ecx, %%ecx\n\t"
        "rdpkru\n\t"
        "andl $0xfffffff3, %%eax\n\t"
        "wrpkru"
        ::: "memory"
    );
#endif

    ret = vkru_alloc_c(nas);    /* nas is not security critical, no stack switch */

#ifdef VKEY_SAFE
    asm volatile (
        "movq %0, %%r12\n\t"
        "xorl %%ecx, %%ecx\n\t"
        "rdpkru\n\t"
        "orl $0x55555554, %%eax\n\t"
        "wrpkru\n\t"
        "andl $0xc,  %%eax\n\t"
        "cmpl $0x4,  %%eax\n\t"
        "jne 0x0\n\t"
        "movq %%r12, %%rax"
        :: "r"(ret) : "memory"
    );
#else
    wrpkru(0x55555554);
#endif

    return ret;
}

void vkru_free(void)
{
#ifdef VKEY_SAFE
    asm volatile (
        "xorl %%ecx, %%ecx\n\t"
        "rdpkru\n\t"
        "andl $0xfffffff3, %%eax\n\t"
        "wrpkru\n\t"
        ::: "memory"
    );
#endif

    vkru_free_c();

#ifdef VKEY_SAFE
    asm volatile (
        "xorl %%ecx, %%ecx\n\t"
        "rdpkru\n\t"
        "orl $0x4,  %%eax\n\t"
        "wrpkru\n\t"
        "andl $0xc,  %%eax\n\t"
        "cmpl $0x4,  %%eax\n\t"
        "jne 0x0\n\t"
        ::: "memory"
    );
#endif
}

void wrvkru(int vkey, int perm)
{
    register long rsp;
    register vkru_t **vkrupp;

#ifdef VKEY_SAFE
    asm volatile (
        "xorl %%ecx, %%ecx\n\t"
        "rdpkru\n\t"
        "andl $0xfffffff3, %%eax\n\t"
        "wrpkru\n\t"
        "movq %%rsp, %0"
        : "=r"(rsp) :: "memory"
    );
#endif

    vkrupp = get_current_thread_vkru();

#ifdef VKEY_SAFE
    asm volatile (
        "add $64, %1\n\t"
        "movq %1, %%rsp\n\t"
        "push %0"
        :: "r"(rsp), "r"(*vkrupp) : "memory"
    );
#endif

    rsp = wrvkru_c(vkey, perm, vkrupp) + 1;

    // syscall of vkey activate
    if (rsp == arch_max_pkey() && !(perm & VKEY_MASK))
        asm volatile (
            "movq $340L, %%rax\n\t"
            "movq %0, %%rdi\n\t"
            "syscall\n\t"
            :: "r"((long)vkey) : "memory"
        );

#ifdef VKEY_SAFE
    asm volatile (
        "pop %%rsp\n\t"
        :::"memory"
    );
#endif

    pkru_set_bits_dis(rsp, perm);

#ifdef VKEY_SAFE
    asm volatile (
        "andl $0xc,  %%eax\n\t"
        "cmpl $0x4,  %%eax\n\t"
        "jne 0x0\n\t"
        ::: "memory"
    );
#endif
}

// Read only
int rdvkru(int vkey)
{
#ifndef VKEY_SAFE
    // A dummy interface for now.
    vkru_t *vkru = *get_current_thread_vkru();
    if (vkru)
        return (vkru->perm[vkey / 4] >> (2 * (vkey & 0x3))) & 0x3;
    return VKEY_AD | VKEY_WD;
#else
    return -1;
#endif
}
