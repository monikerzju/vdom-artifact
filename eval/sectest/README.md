## Security Test of VDom
Both in-thread attacks and cross-thread attacks on random vdoms are tested in `violation.c` program.

The shell script, `run.sh`, is used to automatically compile and run the security test.

## Details of the program
The test program creates `NTHREADS` threads. Each thread has its private memory region, protected by a corresponding vdom (vkey). User is asked to input the attacker thread id `itid` and a vkey `imid`, which means that thread `itid` is an attacker and it wants to access the memory region indexed `imid` protected by vkey `imid`. If `itid` and `imid` are the same, the attack is intra-thread attack; otherwise, the attack is inter-thread attack.

The attacker thread will try the illegal access, and the program will terminate immediately once the attacker thread attempt to access memory protected by vkey `imid`.
