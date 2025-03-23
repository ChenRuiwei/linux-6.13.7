#!/bin/sh

qemu-system-x86_64 \
    -kernel ./arch/x86/boot/bzImage \
    -initrd ./initramfs.cpio \
    -nographic \
    -serial mon:stdio \
    -append "console=ttyS0"
