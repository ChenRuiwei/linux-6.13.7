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
        " -append 'console=ttyS0 nokaslr'"
    } else {
        " -M virt" + \
        " -kernel " + O + "/arch/" + ARCH + "/boot/Image" + \
        " -initrd " + O + "/initramfs.cpio" + \
        " -nographic" + \
        " -append 'nokaslr'"
    }
CROSS_COMPILE := if ARCH == "riscv" {
        "riscv64-linux-gnu-"
    } else {
        ""
    }

default:
    @just --list

make *TARGETS:
    make ARCH={{ARCH}} O={{O}} CROSS_COMPILE={{CROSS_COMPILE}} {{TARGETS}}

build-initramfs:
    cd {{O}}/initramfs/ && find . | cpio -o --format=newc >../initramfs.cpio

build-kernel:
    just ARCH={{ARCH}} make -j$(nproc)

build: build-initramfs build-kernel

run *QEMU_EXTRA_ARGS:
    {{QEMU}} {{QEMU_ARGS}} {{QEMU_EXTRA_ARGS}}

clean:
    just ARCH={{ARCH}} make clean

debug:
    just ARCH={{ARCH}} run -s -S

gdb:
    gdb --ex "file {{O}}/vmlinux" -ex "lx-symbols"

clang:
    ./scripts/clang-tools/gen_compile_commands.py -d {{O}}
