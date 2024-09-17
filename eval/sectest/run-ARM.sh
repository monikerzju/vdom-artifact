#! /bin/sh

gcc violation.c -o violation.o -DTRY_ILLEGAL_ACCESS -DDOMSZ=2097152 -lpthread -lvkeys
./violation.o

rm *.o