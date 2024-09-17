#! /bin/sh

gcc violation.c -o violation.o -DTRY_ILLEGAL_ACCESS -DDOMSZ=4096 -lpthread -lvkeys
./violation.o

rm *.o