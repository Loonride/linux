scripts/config --disable NET_SELFTESTS
scripts/config --disable EFI_TEST
scripts/config --disable CRYPTO_TEST
scripts/config --disable KGDB_TESTS
scripts/config --disable DEBUG_RODATA_TEST
scripts/config --disable LOCK_TORTURE_TEST
scripts/config --disable TORTURE_TEST
scripts/config --disable RCU_TORTURE_TEST
scripts/config --disable ATOMIC64_SELFTEST
scripts/config --disable ASYNC_RAID6_TEST
scripts/config --disable TEST_KSTRTOX
scripts/config --disable PM_TEST_SUSPEND
scripts/config --disable STMMAC_SELFTESTS 
scripts/config --disable DMATEST
scripts/config --disable STARFIVE_MBOX_TEST
ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- LLVM=1 BEANDIP=1 make olddefconfig

