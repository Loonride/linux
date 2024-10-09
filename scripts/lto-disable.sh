scripts/config --disable LTO
scripts/config --disable LTO_CLANG
scripts/config --disable LTO_CLANG_FULL
ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- LLVM=1 BEANDIP=1 make olddefconfig

