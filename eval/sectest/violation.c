#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <stdbool.h>
#include <libvkeys.h>

#define NTHREADS    15
#define ITER        50

char *mem[NTHREADS];
int finished[NTHREADS];
int allocated[NTHREADS];
int imid, itid;

void *rand_gen_func(void *arg)
{
    int tid = *((int *)arg);
    int i, j;
    char file_buff[128];
    vkru_t *vkru = vkru_alloc(1);
    int vkey = vkey_alloc();
    int count;
    const int iter = ITER;

    mem[tid] = (char *)memalign(DOMSZ, DOMSZ);

    if (!vkru)
        goto out;
    if (vkey > 0 && mem[tid])
        printf("[%d] vkru %lx, mem %lx, vkey %d\n", tid, (unsigned long)vkru, (unsigned long)mem[tid], vkey);
    else
        goto out;
    vkey_mprotect((void *)mem[tid], DOMSZ, vkey);
    printf("mem[%d] is protected by %d with tid %d\n", tid, vkey, tid);

    allocated[tid] = 1;
    for (i = 0; i < NTHREADS; i++)
        if (allocated[i] == 0)
            i -= 1;

    sleep(tid);
    for (i = 1; i <= NTHREADS; i++)
        wrvkru(i, VKEY_ND);
    printf("Thread %d enables all access\n", tid);
    for (i = 0; i < NTHREADS; i++) {
        mem[i][0] = 0xc0;
        mem[i][1] = 0x01;
        mem[i][2] = 0xbe;
        mem[i][3] = 0xef;
        mem[i][4] = 0xde;
        mem[i][5] = 0xad;
        mem[i][6] = 0xb0;
        mem[i][7] = 0xd1 + (char)tid;
    }
    for (i = 1; i <= NTHREADS; i++)
        wrvkru(i, VKEY_AD);
    printf("Thread %d disables all access\n", tid);
    // Each thread is in its unique address space

    // Open a file, use the private key to do XOR, then write to another file
    for (j = 0; j < iter; j++) {
        wrvkru(vkey, VKEY_WD);
        for (i = 0; i < 128; i++)
            file_buff[i] = file_buff[i] ^ mem[tid][i % 8];
        wrvkru(vkey, VKEY_AD);
    }

    // Illegal access
#ifdef TRY_ILLEGAL_ACCESS
    if (tid == itid) {
        printf("Busy waiting in core %d\n", tid);
        for (i = 0; i < NTHREADS; i++)
            if (i != tid && finished[i] == 0)
                i -= 1;
        printf("Finished waiting in core %d, try illegal access (read)...\n", tid);
        sleep(1);
        for (i = 0; i < 8; i++)
            printf("%02x", mem[imid][i]);
        printf("\n");
    }
#endif

    printf("Core %d finished\n", tid);
    finished[tid] = 1;
    for (i = 0; i < NTHREADS; i++)
        if (finished[i] == 0)
            i -= 1;
    printf("Core %d unmapped\n", tid);

out:
    munmap(mem[tid], DOMSZ);
    vkey_free(vkey);
    vkru_free();
}

int main(void)
{
    int i;
    pthread_t threads[NTHREADS];
    int args[NTHREADS];
    int vkey;

    vkey_init(false);
    printf("Domain size is %d\n", DOMSZ);
    printf("Please enter the attacker thread id and vkey (can be the same) to test intra-and-inter-thread attack:\n");
    scanf("%d%d", &itid, &imid);
    itid %= NTHREADS;
    imid %= NTHREADS;

    for (i = 0; i < NTHREADS; i++) {
        allocated[i] = 0;
        finished[i] = 0;
    }

    for (i = 0; i < NTHREADS; i++) {
        args[i] = i;
        pthread_create(&threads[i], NULL, rand_gen_func, (void*)(&(args[i])));
    }

    for (i = 0; i < NTHREADS; i++)
        pthread_join(threads[i], NULL);

    printf("All threads joined\n");

    return 0;
}
