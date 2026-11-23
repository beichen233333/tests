#!/usr/bin/env python3
"""Validate the ELF header of the Android ARM64 library without parsing readelf prose."""
from __future__ import annotations
import argparse
from pathlib import Path
import struct
import sys

ELF_MAGIC = b"\x7fELF"
ELFCLASS64 = 2
ELFDATA2LSB = 1
ET_DYN = 3
EM_AARCH64 = 183


def parse_elf_header(path: Path) -> dict[str, int | str]:
    data = path.read_bytes()[:64]
    if len(data) < 64:
        raise ValueError(f"truncated ELF header: {len(data)} bytes (need 64)")
    if data[:4] != ELF_MAGIC:
        raise ValueError("missing ELF magic")
    elf_class, data_encoding = data[4], data[5]
    if data_encoding not in (1, 2):
        raise ValueError(f"unsupported ELF data encoding: {data_encoding}")
    endian = "<" if data_encoding == 1 else ">"
    e_type, e_machine = struct.unpack_from(endian + "HH", data, 16)
    return {
        "elf_class": elf_class,
        "data_encoding": data_encoding,
        "type": e_type,
        "machine": e_machine,
        "endianness": "little" if data_encoding == 1 else "big",
    }


def read_cmake_cache(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    if not path.is_file():
        return result
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line or line.startswith("//") or line.startswith("#") or "=" not in line or ":" not in line.split("=", 1)[0]:
            continue
        left, value = line.split("=", 1)
        key = left.split(":", 1)[0]
        result[key] = value
    return result


def validate(path: Path, expected_abi: str, cache_path: Path | None = None) -> dict[str, int | str]:
    header = parse_elf_header(path)
    if header["elf_class"] != ELFCLASS64:
        raise ValueError(f"expected ELFCLASS64=2, got {header['elf_class']}")
    if header["data_encoding"] != ELFDATA2LSB:
        raise ValueError(f"expected little-endian ELF=1, got {header['data_encoding']}")
    if header["type"] != ET_DYN:
        raise ValueError(f"expected ET_DYN shared library=3, got {header['type']}")
    if header["machine"] != EM_AARCH64:
        raise ValueError(f"expected EM_AARCH64=183, got e_machine={header['machine']}")
    if cache_path is not None:
        cache = read_cmake_cache(cache_path)
        abi_values = [cache[k] for k in ("ANDROID_ABI", "CMAKE_ANDROID_ARCH_ABI") if k in cache]
        if not abi_values:
            raise ValueError("CMakeCache.txt does not record ANDROID_ABI or CMAKE_ANDROID_ARCH_ABI")
        if any(value != expected_abi for value in abi_values):
            raise ValueError(f"CMake ABI mismatch: expected {expected_abi}, found {abi_values}")
        header["cmake_abi"] = ",".join(abi_values)
        header["cmake_processor"] = cache.get("CMAKE_SYSTEM_PROCESSOR", "(unset)")
        header["c_compiler_target"] = cache.get("CMAKE_C_COMPILER_TARGET", "(default from toolchain)")
        header["cxx_compiler_target"] = cache.get("CMAKE_CXX_COMPILER_TARGET", "(default from toolchain)")
    return header


def _self_test() -> None:
    import tempfile
    def fixture(machine: int, elf_class: int = 2, endian: int = 1, e_type: int = 3) -> bytes:
        data = bytearray(64)
        data[:4] = ELF_MAGIC
        data[4] = elf_class
        data[5] = endian
        struct.pack_into("<HH", data, 16, e_type, machine)
        return bytes(data)
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / "test.so"
        p.write_bytes(fixture(EM_AARCH64))
        assert validate(p, "arm64-v8a")["machine"] == EM_AARCH64
        cache = Path(tmp) / "CMakeCache.txt"
        cache.write_text("ANDROID_ABI:UNINITIALIZED=arm64-v8a\nCMAKE_ANDROID_ARCH_ABI:STRING=arm64-v8a\nCMAKE_SYSTEM_PROCESSOR:STRING=aarch64\n", encoding="utf-8")
        assert validate(p, "arm64-v8a", cache)["cmake_abi"] == "arm64-v8a,arm64-v8a"
        cache.write_text("ANDROID_ABI:UNINITIALIZED=x86_64\n", encoding="utf-8")
        try:
            validate(p, "arm64-v8a", cache)
        except ValueError as exc:
            assert "CMake ABI mismatch" in str(exc)
        else:
            raise AssertionError("x86_64 CMake ABI must be rejected")
        p.write_bytes(fixture(62))
        try:
            validate(p, "arm64-v8a")
        except ValueError as exc:
            assert "e_machine=62" in str(exc)
        else:
            raise AssertionError("x86-64 ELF must be rejected")
        p.write_bytes(fixture(EM_AARCH64, elf_class=1))
        try:
            validate(p, "arm64-v8a")
        except ValueError as exc:
            assert "ELFCLASS64" in str(exc)
        else:
            raise AssertionError("ELF32 must be rejected")
        p.write_bytes(fixture(EM_AARCH64, e_type=1))
        try:
            validate(p, "arm64-v8a")
        except ValueError as exc:
            assert "ET_DYN" in str(exc)
        else:
            raise AssertionError("ET_REL must be rejected")
    print("ELF architecture validator self-test: AArch64 acceptance, x86 rejection, ELF32 rejection, non-DYN rejection, CMake ABI acceptance and mismatch rejection passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", nargs="?", type=Path)
    parser.add_argument("--expected-abi", default="arm64-v8a")
    parser.add_argument("--cmake-cache", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        _self_test()
        return 0
    if args.elf is None:
        parser.error("ELF path is required unless --self-test is used")
    try:
        header = validate(args.elf, args.expected_abi, args.cmake_cache)
    except (OSError, ValueError) as exc:
        print(f"ELF architecture verification failed: {exc}", file=sys.stderr)
        raise SystemExit(4)
    print(
        "ELF architecture verified: "
        f"ELFCLASS64 little-endian ET_DYN EM_AARCH64({header['machine']}); "
        f"ABI={header.get('cmake_abi', args.expected_abi)}; "
        f"processor={header.get('cmake_processor', '(not checked)')}; "
        f"C target={header.get('c_compiler_target', '(not checked)')}; "
        f"CXX target={header.get('cxx_compiler_target', '(not checked)')}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
