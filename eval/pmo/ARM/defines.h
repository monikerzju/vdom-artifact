#ifndef _DEFINES_H_
#define _DEFINES_H_

#define STRING_LENGTH 512
#define SUB_STRING_LENGTH 8
#define PMO_SIZE (2097152) // 2 ^ 21 <-> 2MB, size of each PMO
#define PMO_NUM 64
#define ARRAY_SIZE ((PMO_SIZE / STRING_LENGTH) * PMO_NUM) // number of strings

typedef struct {
  int id; // custom thread id (from 0 to THREAD_NUM - 1)
  int thread_ops; // number of operations in this thread
} arg_t;

#endif // _DEFINE_H_
