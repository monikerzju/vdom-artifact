## Microbenchmarks of VDom
In `switch.c`, both virtual domain eviction and pgd switch are tested. Using the 2 mentioned mechanisms, sequential access, random access, and must-switch-or-eviction access patterns are evaluated.

The shell script, `run.sh`, is used to automatically compile and run the microbenchmarks.

## Details of the program
The test program accesses a number of (`nrvdom`) domains according to different patterns. The average cycles consumed by domain switches is reported when the program terminates.

Below is an example of the output.

```
-----------------------------------------
[Only one address space]
vkey init with ret 0
sequential access with nas=1 size=4096 number=1 consumes 69 cycles per switch
rand access with nas=1 size=4096 number=1 consumes 73 cycles per switch
2 address spaces access with nas=1 size=4096 number=1 consumes 70 cycles per switch
-----------------------------------------
[Evict 2 address spaces]
vkey init with ret 0
sequential access with nas=1 size=4096 number=15 consumes 290 cycles per switch
rand access with nas=1 size=4096 number=15 consumes 217 cycles per switch
2 address spaces access with nas=1 size=4096 number=15 consumes 1390 cycles per switch
-----------------------------------------
[Switch 2 address spacee]
vkey init with ret 0
sequential access with nas=2 size=4096 number=28 consumes 124 cycles per switch
rand access with nas=2 size=4096 number=28 consumes 352 cycles per switch
2 address spaces access with nas=2 size=4096 number=28 consumes 538 cycles per switch
-----------------------------------------
```

## Note
On Raspberry Pi, to measure the accurate cycles consumed, we leverage the ARM Performance Monitor Unit, which is not exposed to user-space by default. Therefore, if the microbenchmarks crash due to the illegal instruction exception, it means that PMU is not available.