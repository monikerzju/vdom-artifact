#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <libvkeys.h>
#include <stdint.h>

#define MAXVDOM 64

int szvdom = 4096;
int nrvdom = 1;
int nas = 1;

static inline unsigned long tsc(void)
{
    unsigned long t;
    __asm volatile ("rdtsc" : "=A"(t));
    return t;
}

int main(int argc, char **argv)
{
    vkru_t *vkru;
    int vkey;
    int perm;
    int *mem[MAXVDOM];
    int i;
    int k, iter = 3000;
    register unsigned long end;
    int ret;
    unsigned long sumt;
    int randd;

    if (argc != 4)
        return -1;
    if (strcmp(argv[1], "4096") != 0)
        szvdom = 4096;
    nrvdom = (int)strtol(argv[2], NULL, 10);
    if (nrvdom < 0 || nrvdom > MAXVDOM)
        return -2;
    nas = (int)strtol(argv[3], NULL, 10);
    if (nas < 1)
        nas = 1;
    else if (nas > 6)
        nas = 6;

    for (i = 0; i < nrvdom; i++) {
        mem[i] = (int *)memalign(szvdom, szvdom);
        for (int t = 0; t < szvdom / sizeof(int); t++)
	    mem[i][t] = t;    // write all the pages
    }
    ret = vkey_init(false);
    printf("vkey init with ret %d\n", ret);
    vkru_alloc(nas);

    for (i = 0; i < nrvdom; i++) {
        vkey = vkey_alloc();
        wrvkru(vkey, VKEY_AD);
        vkey_mprotect(mem[i], szvdom, vkey);
    }

    // preheat
    for (k = 0; k < iter; k++) {
        for (i = 0; i < nrvdom; i++) {
            wrvkru(i + 1, VKEY_ND);
            for (int t = 0; t < szvdom / sizeof(int); t++)
	            mem[i][t] = t;    // write all the pages
	    wrvkru(i + 1, VKEY_AD);
        }
    }

    // sequential access
    sumt = 0;
    for (k = 0; k < iter; k++) {
        for (i = 0; i < nrvdom; i++) {
            randd = i;
            asm volatile("": : :"memory");
            end = tsc();
            asm volatile("": : :"memory");
            wrvkru(randd + 1, VKEY_ND);
            asm volatile("": : :"memory");
            end = tsc() - end;
            asm volatile("": : :"memory");
            sumt += end;
            for (int t = 0; t < szvdom / sizeof(int); t++)
	            mem[randd][t] = t;    // write all the pages
	    wrvkru(randd + 1, VKEY_AD);
        }
    }
    printf("sequential access with nas=%d size=%d number=%d consumes %ld cycles per switch\n",
             nas, szvdom, nrvdom, sumt / iter / nrvdom);

    // preheat
    for (k = 0; k < iter; k++) {
        for (i = 0; i < nrvdom; i++) {
            randd = rand() % nrvdom;
            wrvkru(randd + 1, VKEY_ND);
            for (int t = 0; t < szvdom / sizeof(int); t++)
	            mem[randd][t] = t;    // write all the pages
	    wrvkru(randd + 1, VKEY_AD);
        }
    }

    // random access
    sumt = 0;
    for (k = 0; k < iter; k++) {
        for (i = 0; i < nrvdom; i++) {
            randd = rand() % nrvdom;
            asm volatile("": : :"memory");
            end = tsc();
            asm volatile("": : :"memory");
            wrvkru(randd + 1, VKEY_ND);
            asm volatile("": : :"memory");
            end = tsc() - end;
            asm volatile("": : :"memory");
            sumt += end;
            for (int t = 0; t < szvdom / sizeof(int); t++)
	            mem[randd][t] = t;    // write all the pages
	    wrvkru(randd + 1, VKEY_AD);
        }
    }
    printf("rand access with nas=%d size=%d number=%d consumes %ld cycles per switch\n",
             nas, szvdom, nrvdom, sumt / iter / nrvdom);

    // preheat
    for (k = 0; k < iter; k++) {
        for (i = 0; i < nrvdom; i++) {
            randd = i % 2 ? 0 : (nrvdom - 1);
            wrvkru(randd + 1, VKEY_ND);
            for (int t = 0; t < szvdom / sizeof(int); t++)
	            mem[randd][t] = t;    // write all the pages
	    wrvkru(randd + 1, VKEY_AD);
        }
    }

    // 2 spaces access
    sumt = 0;
    for (k = 0; k < iter; k++) {
        for (i = 0; i < nrvdom; i++) {
            randd = i % 2 ? 0 : (nrvdom - 1);
            asm volatile("": : :"memory");
            end = tsc();
            asm volatile("": : :"memory");
            wrvkru(randd + 1, VKEY_ND);
            asm volatile("": : :"memory");
            end = tsc() - end;
            asm volatile("": : :"memory");
            sumt += end;
            for (int t = 0; t < szvdom / sizeof(int); t++)
	            mem[randd][t] = t;    // write all the pages
	    wrvkru(randd + 1, VKEY_AD);
        }
    }
    printf("2 address spaces access with nas=%d size=%d number=%d consumes %ld cycles per switch\n",
             nas, szvdom, nrvdom, sumt / iter / nrvdom);

    return 0;
}
