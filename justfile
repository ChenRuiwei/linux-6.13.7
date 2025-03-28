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

make *TARGETS:
    make ARCH={{ARCH}} O={{O}} {{TARGETS}}

build-initramfs:
    cd {{O}}/initramfs/ && find . | cpio -ov --format=newc >../initramfs.cpio

build-kernel:
    just make -j$(nproc)

build: build-initramfs build-kernel

run *QEMU_EXTRA_ARGS:
    {{QEMU}} {{QEMU_ARGS}} {{QEMU_EXTRA_ARGS}}

clean:
    just make clean

debug:
    just run -s -S

gdb:
    gdb --ex "file {{O}}/vmlinux"

clang:
    ./scripts/clang-tools/gen_compile_commands.py -d {{O}}
