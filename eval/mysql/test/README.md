## Before running the test script
The testing process is automated by the shell script `run.sh` (`run-x86.sh` for X86, `run-arm.sh` for ARM). However, before we run the script, we need to prepare python environment and modify some entries in it, in order to specify the paths of application binaries.

### Prepare Python environment
Our plot script uses Python matplotlib and numpy package. 

### Replace entries in `run.sh`
There are 3 entries needed to be replaced:
- `PREFIX-MYSQL-ORIGIN`: the path where the original MySQL installed.
- `PREFIX-MYSQL-VDOM`: the path where the VDom protected MySQL installed.
- `YOUR_PASSWORD`: the password of the MySQL user `sbtest_user`

## How the test script works
First, the script launches the original / VDom protected MySQL. Then, use sysbench to test the throughput of MySQL and output results to directory `results`. You can check results following the script output. 

## Run the script
The `run.sh` scripts on X86 and ARM are almost the same, except that the number of sysbench threads configuration is different. 
```
# X86
./run-x86.sh
# ARM
./run-arm.sh
```