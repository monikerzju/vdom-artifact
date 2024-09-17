#! /bin/sh

# gcc switch.c -o switch.o -I. -lvkeys -L.
gcc switch.c -o switch.o -lvkeys
echo "For AE"
echo "-----------------------------------------"
echo "[Only one address space]"
./switch.o 4096 1 1
echo "-----------------------------------------"
echo "[Evict 2 address spaces]"
./switch.o 4096 15 1
echo "-----------------------------------------"
echo "[Switch 2 address spacee]"
./switch.o 4096 28 2
echo "-----------------------------------------"
# echo "-----------------------------------------"
# echo "For Microbenchmark"
# echo "-----------------------------------------"
# echo "[Switch 3 address spaces]"
# ./switch.o 4096 32 3
# echo "-----------------------------------------"
# echo "[Switch 5 address spaces]"
# ./switch.o 4096 64 5

rm *.o



