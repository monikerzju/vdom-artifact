#ifndef _VKEYS_H
#define _VKEYS_H

#include <unistd.h>
#include <stdbool.h>

#define syscall_vkey_alloc      452     /* arch independent */
#define syscall_vkey_free       453     /* arch independent */
#define syscall_vkey_mprotect   454     /* arch independent */
#define syscall_vkey_reg_vkru   455
#define syscall_vkey_wrvkrk     456     /* vkey, perm */

#define VKEY_AD     0x0		/* access  disable */
#define VKEY_WD     0x1		/* not implemented in arm, give the client */
#define VKEY_ND     0x3		/* nothing disable, and no need to check the page table */
#define VKEY_ID     0x4     /* illegal disable will cause rdvkrk */

#define VKEY_AD_INIT    0x4 /* access   disable when init */
#define VKEY_WD_INIT    0x5 /* write    disable when init */
#define VKEY_ND_INIT    0x7 /* nothing  disable when init */
#define VKEY_MASK       0x4

#define MAX_VKEY_PER_PROC   128

typedef char vkru_t;

int vkey_init(bool sprot);  /* self prot */
int vkey_alloc(void);       /* syscall */
int vkey_free(int vkey);    /* syscall */
int vkey_mprotect(void *addr, size_t len, int vkey);    /* syscall */
vkru_t *vkru_alloc(int nas);   /* self prot */
void vkru_free(void);       /* self prot */
void wrvkru(int vkey, int perm);    /* self prot */
int rdvkru(int vkey);       /* self prot */

#endif
