# VDom: Fast and Unlimited Virtual Domains on Multiple Architectures

This archive contains the source code of VDom modified Linux kernel, user-space libraries, microbenchmarks, application benchmarks, and scripts necessary to reproduce the papaer’s evaluation results.

## File tree structure
- `eval`: consists of files to conduct the following 5 experiments
	- `sectest`: security test, corresponding to "7.2 Security Evaluation: Penetration Tests" section in our paper.
	- `micro`: microbenchmark, corresponding to "7.4 Microbenchmarks".
	- `httpd`: an application benchmark, corresponding to "7.5 Application Benchmarks: OpenSSL: isolate many in-library secrets".
	- `mysql`: an application benchmark, corresponding to "7.5 Application Benchmarks: MySQL: separate many threads".
	- `pmo`: an application benchmark, corresponding to "7.5 Application Benchmarks: String Replace: protect many PMOs".
- `src`
	- `kernel`: the source code of VDom modified kernel.
	- `libvkeys`: the source code of VDom user-space library.

## Install VDom Linux kernel and user-space library
The VDom kernel is applicable on both Intel X86 and ARM architectures. The configurations to boot Dell T440 and Raspberry Pi 3B are in the corresponding source code directories. To install VDom Linux kernel:
```
# under src/kernel/linux for generic platforms (X86)
cp config_dellx86 .config
make menuconfig
make -j
sudo make modules_install install
```
```
# under src/kernel/linux-rpi for raspberry pi (ARM)
cp rpi3b_config .config
make menuconfig
make -j zImage modules dtbs
sudo make modules_install install
sudo cp /boot/initrd.img-5.17.8-v7+ /boot/firmware/initrd.img
sudo cp arch/arm/boot/zImage /boot/firmware/vmlinuz
sudo cp arch/arm/boot/dts/*.dtb /boot/firmware/
sudo cp arch/arm/boot/dts/overlays/*.dtbo /boot/firmware/overlays/
```
You may also need to choose the corresponding grub option when reboot the machine. 

The user-space libraries are different on the two architectures. For example, on Intel X86 architecture, to install VDom user-space library:
```
# under src/libvkeys/src/x86 or src/libvkeys/src/arm
make
```

## Security test
This test program tests in-thread attacks and cross-thread attacks on random vdoms with multiple VDSes.

## Microbenchmarks
The microbenchmarks measure the average costs of pgd switch, domain eviction, and `wrvdr` with a domain hit. The build and test process is detailed is the corresponding directories for ARM and X86.


## Application benchmarks
There are 3 application benchmarks, including httpd, MySQL, and a custom PMO benchmark. The build and test process is detailed for each application benchmark under their directories. 