#!/bin/sh

cd ./initramfs/ || exit
find . | cpio -ov --format=newc >../initramfs.cpio
cd .. || exit
