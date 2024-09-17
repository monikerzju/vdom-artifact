#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>

#include "defines.h"

struct tmp {
	char c0, c1, c2, c3;
};

int main() {
	srand(time(0));
	// RAND_MAX: 7fffffff

	char s[20] = "sps_inputX.txt\0";
	struct tmp tmp;

	int i, j;
	for(i = 0; i < THREAD_NUM; i++) {
		s[9] = i + '0';
		printf("Generating file: %s\n", s);

		FILE *f;
		f = fopen(s, "wb");
		assert(f);

		// randomly access a string
		for(j = 0; j < TOTAL_OPS / THREAD_NUM; j++) {
			// randomly access a string
			int random_num = rand() % ARRAY_SIZE;
			// printf("%d: \trandom_num for public PMO: %d, PMO index: %d\n", j, random_num, random_num / (PMO_SIZE / STRING_LENGTH));
			tmp.c0 = (char)(random_num >> 24);
			tmp.c1 = (char)((random_num << 8) >> 24);
			tmp.c2 = (char)((random_num << 16) >> 24); 
			tmp.c3 = (char)((random_num << 24) >> 24); 
			fwrite(&tmp, sizeof(struct tmp), 1, f);
		}
	}
	return 0;
}