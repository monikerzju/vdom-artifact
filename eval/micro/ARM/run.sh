#! /bin/sh

sudo sysctl vm.swappiness=0
gcc switch.c -o switch.o -lvkeys 
echo "For AE"
echo "-----------------------------------------"
echo "[Only one address space]"
./switch.o 2097152 1 1
echo "-----------------------------------------"
echo "[Evict 2 address spaces]"
./switch.o 2097152 15 1
echo "-----------------------------------------"
echo "[Switch 2 address spacee]"
./switch.o 2097152 15 2
echo "-----------------------------------------"
# echo "-----------------------------------------"
# echo "For Microbenchmark"
# echo "-----------------------------------------"
# echo "[Switch 3 address spaces]"
# ./switch.o 2097152 32 3
# echo "-----------------------------------------"
# echo "[Switch 5 address spaces]"
# ./switch.o 2097152 64 5

rm *.o
