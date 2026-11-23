# Android ARM64 ELF architecture validation

The 2026-11-23 CI run checked out source revision `18b6fc7ba5e48193568d08e91c2c9b236167ce10`, configured the NDK 27.2.12479018 toolchain, resolved `libz.so` from `sysroot/usr/lib/aarch64-linux-android/23`, and linked `libastraeoffline.so` successfully. The job then exited with code 4 at the old text check:

```bash
llvm-readelf -h libastraeoffline.so | grep -q 'Machine:.*AArch64'
```

That check relies on the exact spelling emitted by the NDK LLVM `readelf` build. LLVM ELF machine names may be rendered as `EM_AARCH64`, which does not match the case-sensitive `AArch64` expression. It is not a reliable way to determine the ELF machine type.

The build now validates the ELF header bytes directly:

- `EI_CLASS == ELFCLASS64 (2)`
- `EI_DATA == ELFDATA2LSB (1)`
- `e_type == ET_DYN (3)`
- `e_machine == EM_AARCH64 (183)`
- CMake cache records `ANDROID_ABI=arm64-v8a` or `CMAKE_ANDROID_ARCH_ABI=arm64-v8a`

The check runs on both the unstripped link output and final stripped deployment library. If it fails, the script prints the complete `llvm-readelf -h` output and relevant CMake ABI/compiler cache entries, so a genuine wrong-target build can be distinguished from a formatting false negative.

The validator self-test is part of `scripts/build_host.sh` and covers valid AArch64 shared libraries, x86-64 rejection, ELF32 rejection, non-shared ELF rejection, and CMake ABI match/mismatch.

This source-side fix has passed host build and tests. The Android GitHub Actions workflow must be rerun to verify the actual NDK-produced library; no Android artifact is claimed from this host environment.
