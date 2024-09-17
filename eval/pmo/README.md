## Overview
`main.c` is the main program for the PMO application benchmark. We use a multi-PMO benchmark and protect each PMO with a vdom like prior study. The benchmark has 64 PMOs of 2MB size. Each PMO is filled with strings and each string is 512 bytes in length. Multiple threads are launched in the benchmark.  Each thread randomly picks a string and
performs a substring search and replacement operation on that string. VDom protect read / write of each PMO. 

## File tree structure
- `defines.h`: defines some core arguments.
- `main.c`: the main program of the PMO benchmark.
- `prepare.c`: prepares the input of the PMO benchmark.
- `*.py`: python scripts to process and plot performance data.
- `run.sh`: The testing process is automated by the shell script `run.sh` (`run-x86.sh` for X86, `run-arm.sh` for ARM).

## Run the test script
First, the script use `prepare.c` to prepare the input of the main program. Then, launch the main program of the original, VDom protected using VDS switch, VDom protected using domain eviction. Finally, use python script to process and plot the results. 

The `run.sh` scripts on X86 and ARM are almost the same, except that the number of launched threads and number of total operations is different. 