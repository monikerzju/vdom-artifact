We included a file, x86_linux.diff, showing the differences between the original Linux kernel (v5.17-rc7, commit dda64ead7e) and our customized version.

### As our original implementation does not represent the best practices for modifying Linux, if we want to make VDom truly useful, we recommend:
1. **Using a secondary MMU for multiple page tables**: Although we did not use a secondary MMU for creating and synchronizing multiple page tables for a process, we strongly recommend doing so. This approach is similar to how KVM builds page tables for VMs based on the page tables of host QEMU processes. For an example implementation, you can refer to LightZone (https://github.com/hsyhhh/lightzone), particularly the few lines of code in lzsrc/lzko/arch/arm64/pt.c in the recommended repository.
2. **Optimizing virtual-to-physical key mappings**: We implemented an automatic method to map virtual memory protection keys (vkeys) to physical protection keys (pkeys), with automatic selection between unmapping/remapping or switching page tables (updating CR3) during switching to an vkey unmapped in the current page table. However, this approach ignores semantic knowledge of applications, as it may create too many page tables (for CR3 switches) or cause excessive unmapping/remapping. Therefore, we recommend letting the application decide how to switch domains.

### Tips:
1. **Ignoring ARM-specific differences in the diff file**: The linux/arch/arm directory contains modifications for 32-bit ARM. If you are focusing on x86, you can safely ignore the differences in this directory.
2. **Kernel configuration for VDom**: We provide an x86 kernel configuration that enables VDom (and disables memory compaction) for compilation.
