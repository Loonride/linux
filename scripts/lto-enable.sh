scripts/config --enable LTO
scripts/config --enable LTO_CLANG
scripts/config --enable LTO_CLANG_FULL
ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- LLVM=1 BEANDIP=1 make olddefconfig

