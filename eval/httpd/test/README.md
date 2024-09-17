## Before running the test script
The testing process is automated by the shell script `run.sh` (`run-x86.sh` for X86, `run-arm.sh` for ARM). However, before we run the script, we need to prepare python environment, do some configuration on httpd and modify some entries in the script, in order to specify the paths of application binaries.

### Prepare Python environment
Our plot script uses Python matplotlib and numpy package. 

### Config httpd
You can find the following lines in `PREFIX-HTTPD/httpd/conf/httpd.conf`:
```
<IfModule dir_module>
    DirectoryIndex index.html
</IfModule>
```

`index.html` is by default placed at `PREFIX-HTTPD/httpd/htdocs`. To use the test script we provide, you should change `index.html` to `test` as:
```
<IfModule dir_module>
    DirectoryIndex test
</IfModule>
```

### Replace entries in `run.sh`
There are 3 entries needed to be replaced:
- `PREFIX-HTTPD`: the path where httpd installed.
- `PREFIX-OPENSSL-ORIGIN`: the path where the original openssl installed.
- `PREFIX-OPENSSL-VDOM`: the path where the VDom protected openssl installed.


## How the test script works
First, the script generates https document of designated size. In our paper, we use 1KB or 128KB. User needs to enter the file size he/she wants to test. 

Next, the script launches httpd using the original OpenSSL, and starts testing using ab. Multiple ab instances will be launched to avoid server throughput saturation. Then, kill the original httpd process and start http with the VDom protected OpenSSL, and test. 

Finally, a python script is launched to plot the result graph. You can check results following the script output. 


## Run the script
The `run.sh` scripts on X86 and ARM are almost the same, except that the number of ab threads is different. 
```
# X86
./run-x86.sh
# ARM
./run-arm.sh
```