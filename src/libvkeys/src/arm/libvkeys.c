#include "libvkeys.h"
#include <sys/syscall.h>
#include <sys/mman.h>
#include <stddef.h>

// Common API
// Here, we must guarantee that even with ROP
int vkey_init(register bool sprot)
{
    return 0;
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
    syscall(syscall_vkey_reg_vkru, NULL, nas);
    return NULL;
}

void vkru_free(void)
{
    return;
}

void wrvkru(int vkey, int perm)
{
    syscall(syscall_vkey_wrvkrk, vkey, perm);
}

// Read only
int rdvkru(int vkey)
{
    // A dummy interface for now.
    return syscall(syscall_vkey_wrvkrk, vkey, VKEY_ID);
}
