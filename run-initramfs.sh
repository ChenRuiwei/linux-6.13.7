#!/bin/sh

qemu-system-arm -M virt -m 1G -cpu cortex-a15 -nographic -kernel arch/arm/boot/zImage -initrd initramfs.cpio
