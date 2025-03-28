ARCH := "x86_64"
O := "./build/" + ARCH
QEMU := if ARCH == "riscv" {
        "qemu-system-riscv64"
    } else {
        "qemu-system-" + ARCH
    }
QEMU_ARGS := if ARCH == "x86_64" {
        " -kernel " + O + "/arch/" + ARCH + "/boot/bzImage" + \
        " -initrd " + O + "/initramfs.cpio" + \
        " -nographic" + \
        " -serial mon:stdio" + \
        " -append \"console=ttyS0 nokaslr\""
    } else {
        ""
    }

default:
    @just --list

build-initramfs:
    cd {{O}}/initramfs/ && find . | cpio -ov --format=newc >../initramfs.cpio

build-kernel:
    make ARCH={{ARCH}} O={{O}} -j$(nproc)

make TARGET="":
    make ARCH={{ARCH}} O={{O}} {{TARGET}}

run:
    {{QEMU}} {{QEMU_ARGS}}

dbg:
    {{QEMU}} {{QEMU_ARGS}} -s -S
