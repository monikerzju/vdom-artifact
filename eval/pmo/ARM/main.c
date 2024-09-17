#define _GNU_SOURCE

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <malloc.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/time.h>

#include "defines.h"
#include "libvkeys.h"

char **string_pointer_array;
char **pmo_pointer_array;
static pthread_mutex_t tlk;
static int tac = 0;
int PAGE_SIZE;
pthread_barrier_t barr;
long long start, end;

int tmp[THREAD_NUM + 1];

char search_string[SUB_STRING_LENGTH];
char copy_string[SUB_STRING_LENGTH];
__thread int switch_count;
int total_switch_count;

char alphanum[] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz";

static inline long long perf_counter(void) {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // calculate milliseconds
    return milliseconds;
}

void rand_str(char* str, int length) {
    for (int i = 0; i < length; i++)
        str[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
}

void create_random_strings(char** string_array, int size, int len) {
    int i;
    for(i = 0; i < size; i++) 
        rand_str(string_array[i], len);
}

void cleanup(void) {
    free(string_pointer_array);
    int i;
    for(i = 0; i < PMO_NUM; i++) {
        munmap(pmo_pointer_array[i], PMO_SIZE);
    }
    free(pmo_pointer_array);
    pthread_barrier_destroy(&barr);
}

int thd_s() {
  int ret_val;
  pthread_mutex_lock(&tlk);
  ret_val = tac;
  tac = (tac + 1) % THREAD_NUM;
  pthread_mutex_unlock(&tlk);
  return ret_val;
}

void s_func(int t, int k0, bool flag) {
#ifdef VKEYS
    int vkey;
    // mind that vkey value is 1 greater than PMO index
    vkey = k0 / (PMO_SIZE / STRING_LENGTH) + 1;
    wrvkru(vkey, VKEY_WD);
#endif
    char *string = string_pointer_array[k0];
	int i, j;
	for(i = 0; i <= STRING_LENGTH - SUB_STRING_LENGTH; i++) {
		bool in_flag = true;
		for(j = 0; j < SUB_STRING_LENGTH; j++) {
			if(*(string + i + j) > *(search_string + j)) {
				in_flag = false;
				break;
			}
		}
		if(in_flag) {
#ifdef VKEYS
			wrvkru(vkey, VKEY_ND);
#endif
			if(flag) {
                switch_count += 2;
                memcpy(string + i, copy_string, SUB_STRING_LENGTH);
            }
#ifdef VKEYS
			wrvkru(vkey, VKEY_WD);
#endif
		}
	}
#ifdef VKEYS
    wrvkru(vkey, VKEY_AD);
#endif
    if(flag) {
        switch_count += 2;
    }
}

void *func(void *p_args) {
    int i, core;
    int thread_ops = ((arg_t*)p_args)->thread_ops;
    int oper_count = 0;
    int s_k1 = 0, s_k2 = 0;
    unsigned char *buf; // store operations for this thread
    FILE *f;

    core = thd_s();

#ifdef VKEYS
    vkru_t *vkru;
    vkru = vkru_alloc(NAS);
    printf("VKRU Allocated %lx in core [%d]!\n", (unsigned long)vkru, core);
#endif
    char s[20] = "sps_inputX.txt\0";
    s[9] = core + '0';
    printf("%s\n", s);
    f = fopen(s, "rb");
    assert(f);

    buf = (unsigned char *)malloc(4 * thread_ops);
    size_t ret = fread(buf, 4 * thread_ops, 1, f);
    assert(ret == 1);

    /* ----------------- preheat begin --------------------- */
#ifdef VKEYS
    // for each operation in this thread
    for(i = 0; i < thread_ops; i++) {
        s_k1 = (buf[oper_count * 4] << 24 | buf[oper_count * 4 + 1] << 16 
                | buf[oper_count * 4 + 2] << 8 | buf[oper_count * 4 + 3]);
        s_func(core, s_k1, false);
        // printf("oper_count: %d\n", oper_count);
        oper_count++;
    }
    oper_count = 0;
#endif
    /* ----------------- preheat end --------------------- */

    oper_count = 0;
    pthread_barrier_wait(&barr);
    if(core == 0) {
        start = perf_counter();
        printf("start time: %lld in core 0!\n", start);
    }
    for(i = 0; i < thread_ops; i++) {
        s_k1 = (buf[oper_count * 4] << 24 | buf[oper_count * 4 + 1] << 16 
                | buf[oper_count * 4 + 2] << 8 | buf[oper_count * 4 + 3]);
        s_func(core, s_k1, true);
        // printf("oper_count: %d\n", oper_count);
        oper_count++;
        tmp[core]++;
        // if(oper_count % 50000 == 0) printf("core: %d, oper_count: %d\n", core, oper_count);
    }
    free(buf);
    fclose(f);
    pthread_mutex_lock(&tlk);
    total_switch_count += switch_count;
    pthread_mutex_unlock(&tlk);
    return NULL;
}

int main(int argc, char** argv) {
	int i;
    PAGE_SIZE = getpagesize();

    printf("TOTAL_OPS: %d\n", TOTAL_OPS);
    printf("THREAD_NUM: %d\n", THREAD_NUM);
    printf("STRING_LENGTH: %d\n", STRING_LENGTH);
    printf("ARRAY_SIZE: %d\n", ARRAY_SIZE);
    printf("PMO_SIZE: %d\n", PMO_SIZE);
    printf("PMO_NUM: %d\n", PMO_NUM);
    printf("String number for each PMO: %d\n", PMO_SIZE / STRING_LENGTH);
    
    string_pointer_array = (char **)malloc(ARRAY_SIZE * sizeof(char *));
    pmo_pointer_array = (char **)malloc(PMO_NUM * sizeof(char *));

    int size_2MB = 2097152;

    int j;
    for(i = 0; i < PMO_NUM; i++) {
        pmo_pointer_array[i] = (char *)memalign(size_2MB, PMO_SIZE);
        // printf("pmo %d, addr: %lx\n", i, (unsigned long int)pmo_pointer_array[i]);
        for(j = 0; j < PMO_SIZE / STRING_LENGTH; j++) {
            string_pointer_array[i * PMO_SIZE / STRING_LENGTH + j] = (char *)(pmo_pointer_array[i] + j * STRING_LENGTH);
        }
    }

	create_random_strings(string_pointer_array, ARRAY_SIZE, STRING_LENGTH);
    assert(TOTAL_OPS % THREAD_NUM == 0);

	for(i = 0; i < SUB_STRING_LENGTH; i++) {
		search_string[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
        copy_string[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
	}
    printf("search_string: ");
    for(i = 0; i < SUB_STRING_LENGTH; i++) {
        printf("%c", search_string[i]);
    }
	printf("\n");

    printf("copy_string: ");
    for(i = 0; i < SUB_STRING_LENGTH; i++) {
        printf("%c", copy_string[i]);
    }
	printf("\n");

#ifdef VKEYS
    vkru_t *vkru;
    int ret, vkey;
    ret = vkey_init(false);
    printf("VKeyS <false> Initialized %d!\n", ret);
    vkru = vkru_alloc(NAS);
    printf("VKRU Allocated %lx!\n", (unsigned long)vkru);
    
    // protect each PMO
    for(i = 0; i < PMO_NUM; i++) {
        vkey = vkey_alloc();
        vkey_mprotect(pmo_pointer_array[i], PMO_SIZE, vkey);
        // wrvkru(vkey, VKEY_AD);
        printf("vkey %d protect PMO %d, addr: %lx, size: %d\n", vkey, i, (unsigned long int)(pmo_pointer_array[i]), PMO_SIZE);
    }
    
    // extra operations for new optimization
    for(i = 0; i < PMO_NUM; i++) {
        vkey = i + 1;
        wrvkru(vkey, VKEY_ND);
    }
    for(i = 0; i < PMO_NUM; i++) {
        vkey = i + 1;
        wrvkru(vkey, VKEY_AD);
    }

#endif
    pthread_t threads[THREAD_NUM];
    arg_t args[THREAD_NUM];

    pthread_barrier_init(&barr, NULL, THREAD_NUM);
    pthread_mutex_init(&tlk, NULL);

    for(i = 0; i < THREAD_NUM; i++) {
        args[i].id = i;
        args[i].thread_ops = TOTAL_OPS / THREAD_NUM;
        int rc = pthread_create(&threads[i], NULL, func, &args[i]);
        if(rc) exit(-1);
    }
    for(i = 0; i < THREAD_NUM; i++) {
        pthread_join(threads[i], NULL);
    }

    // end
    end = perf_counter();
    printf("end time: %lld\n", end);

    printf("Done with time: %lld\n", end - start);
    cleanup();

    for(i = 0; i < THREAD_NUM; i++) {
        printf("Count for core %d: %d\n", i, tmp[i]);
    }

    printf("switch_count: %d\n", total_switch_count);

    return 0;
}
