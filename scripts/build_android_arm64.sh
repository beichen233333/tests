#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-${ANDROID_NDK:-}}}"
if [[ -z "$NDK" || ! -f "$NDK/build/cmake/android.toolchain.cmake" ]]; then
    echo "ERROR: set ANDROID_NDK_HOME to a valid Android NDK directory." >&2
    exit 2
fi
case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) HOST_TAG=linux-x86_64 ;;
    Darwin-x86_64) HOST_TAG=darwin-x86_64 ;;
    Darwin-arm64) HOST_TAG=darwin-x86_64 ;;
    *) echo "ERROR: unsupported NDK host: $(uname -s)-$(uname -m)" >&2; exit 2 ;;
esac
TOOLBIN="$NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin"
NM="$TOOLBIN/llvm-nm"
READELF="$TOOLBIN/llvm-readelf"
STRIP="$TOOLBIN/llvm-strip"
OBJCOPY="$TOOLBIN/llvm-objcopy"
if [[ ! -x "$NM" || ! -x "$READELF" || ! -x "$STRIP" || ! -x "$OBJCOPY" ]]; then
    echo "ERROR: NDK LLVM tools missing under $TOOLBIN" >&2
    exit 2
fi

RUNTIME_AUTOSTART="${ASTRAE_ENABLE_RUNTIME_AUTOSTART:-ON}"
TOUCH_HOOK="${ASTRAE_ENABLE_TOUCH_DIRECT_HOOK:-ON}"
REQUEST_HOOK="${ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK:-ON}"
echo "AstraeOffline Android build profile: runtime=$RUNTIME_AUTOSTART touch_hook=$TOUCH_HOOK request_hook=$REQUEST_HOOK"
if [[ "$TOUCH_HOOK" == "ON" && "$REQUEST_HOOK" != "ON" ]]; then
    echo "ERROR: refusing to build a touch replacement without Hook #5; it would consume the login tap while the custom prelude is incomplete." >&2
    echo "Either enable ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK=ON or disable ASTRAE_ENABLE_TOUCH_DIRECT_HOOK=OFF." >&2
    exit 2
fi
NEED_HOOK_ENGINE=OFF
if [[ "$TOUCH_HOOK" == "ON" || "$REQUEST_HOOK" == "ON" ]]; then NEED_HOOK_ENGINE=ON; fi
if [[ "$NEED_HOOK_ENGINE" == "ON" ]]; then
    [[ "$RUNTIME_AUTOSTART" == "ON" ]] || { echo "ERROR: direct hooks require ASTRAE_ENABLE_RUNTIME_AUTOSTART=ON" >&2; exit 2; }
    [[ "${ASTRAE_ENABLE_SHADOWHOOK:-ON}" == "ON" ]] || { echo "ERROR: direct hooks require ASTRAE_ENABLE_SHADOWHOOK=ON" >&2; exit 2; }
    ASTRAE_SHADOWHOOK_SOURCE_DIR="${ASTRAE_SHADOWHOOK_SOURCE_DIR:-$ROOT/third_party/android-inline-hook}"
    FETCH_HOOK_SCRIPT="$ROOT/scripts/fetch_shadowhook.sh"
    if [[ ! -f "$FETCH_HOOK_SCRIPT" ]]; then
        echo "ERROR: ShadowHook fetch script not found: $FETCH_HOOK_SCRIPT" >&2
        exit 2
    fi
    # Do not execute this helper directly: ZIP downloads and some Git checkouts
    # may drop the executable bit. Calling it through bash is portable.
    ASTRAE_SHADOWHOOK_SOURCE_DIR="$ASTRAE_SHADOWHOOK_SOURCE_DIR" bash "$FETCH_HOOK_SCRIPT"
else
    echo "Hook engine fetch skipped: both direct hooks are disabled (safe-load/runtime-only profile)."
fi
BUILD_DIR="${BUILD_DIR:-$ROOT/build-android-arm64}"
python3 "$ROOT/scripts/generate_embedded_assets.py" --stats "$BUILD_DIR/generated/embedded_assets_size.txt"
OUT="$ROOT/artifacts/android-arm64-v8a"
mkdir -p "$OUT"
# Clear stale outputs before building so a failed build cannot leave an older
# two-file package looking like the result of this direct-address-only build.
rm -f "$OUT/libastraeoffline.so" "$OUT/libastraeoffline.so.debug" "$OUT/libshadowhook_nothing.so" "$OUT/INSTALL_HOOK_DEPENDENCIES.txt" "$OUT/ELF_REPORT.txt" "$OUT/SIZE_REPORT.txt" "$OUT/SHA256.txt"
rm -f "$BUILD_DIR/libshadowhook_nothing.so" "$BUILD_DIR/generated/shadowhook_nothing_blob.cpp"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-23 \
    -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release \
    -DASTRAE_ENABLE_RUNTIME_AUTOSTART="$RUNTIME_AUTOSTART" \
    -DASTRAE_ENABLE_SHADOWHOOK="${ASTRAE_ENABLE_SHADOWHOOK:-ON}" \
    -DASTRAE_ENABLE_TOUCH_DIRECT_HOOK="$TOUCH_HOOK" \
    -DASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK="$REQUEST_HOOK" \
    -DASTRAE_SHADOWHOOK_SOURCE_DIR="${ASTRAE_SHADOWHOOK_SOURCE_DIR:-$ROOT/third_party/android-inline-hook}"
cmake --build "$BUILD_DIR" --target astraeoffline --parallel "$JOBS"

SO="$BUILD_DIR/libastraeoffline.so"
[[ -f "$SO" ]] || { echo "ERROR: expected output not found: $SO" >&2; exit 3; }
if [[ "$NEED_HOOK_ENGINE" == "ON" ]]; then
    # This build is direct-address-only: it deliberately does not build or
    # package ShadowHook's libshadowhook_nothing.so linker-probe ELF.
    if "$READELF" -d "$SO" | grep -Fq 'Shared library: [libshadowhook_nothing.so]'; then
        echo "ERROR: final SO unexpectedly depends on a ShadowHook companion library" >&2
        exit 6
    fi
    echo "Verified: ShadowHook direct-address-only profile; no companion library or linker probe is required."
fi
python3 "$ROOT/scripts/verify_elf_arch.py" "$SO" --expected-abi arm64-v8a --cmake-cache "$BUILD_DIR/CMakeCache.txt" || {
    echo "ERROR: Android output failed binary ELF architecture validation; readelf header follows:" >&2
    "$READELF" -h "$SO" >&2 || true
    echo "CMake ABI/compiler cache follows:" >&2
    grep -E '^(ANDROID_ABI|CMAKE_ANDROID_ARCH_ABI|CMAKE_SYSTEM_PROCESSOR|CMAKE_C_COMPILER_TARGET|CMAKE_CXX_COMPILER_TARGET):' "$BUILD_DIR/CMakeCache.txt" >&2 || true
    exit 4
}
"$READELF" -d "$SO" | grep -q 'Library soname: \[libastraeoffline.so\]' || { echo "ERROR: incorrect SONAME" >&2; exit 5; }
python3 - "$NM" "$SO" "$ROOT/compat/original_abi/original_defined_exports.txt" <<'PY'
import subprocess
import sys
nm, so, manifest = sys.argv[1:]
expected = {line.strip() for line in open(manifest, encoding="utf-8") if line.strip()}
output = subprocess.check_output([nm, "-D", "--defined-only", "-P", so], text=True, errors="replace")
actual = {line.split()[0].split("@")[0] for line in output.splitlines() if line.split()}
missing = sorted(expected - actual)
extra = sorted(actual - expected)
if missing or extra:
    print("Export mismatch:", file=sys.stderr)
    if missing: print("  missing:", *missing, sep="\n    ", file=sys.stderr)
    if extra: print("  extra:", *extra, sep="\n    ", file=sys.stderr)
    raise SystemExit(1)
print(f"Export surface matches the compatibility manifest: {len(actual)} symbols")
PY
for lib in libc.so libdl.so liblog.so libz.so; do
    "$READELF" -d "$SO" | grep -Fq "Shared library: [$lib]" || { echo "ERROR: missing DT_NEEDED $lib" >&2; exit 6; }
done

# Keep a separate debug-symbol companion for tombstone symbolication, while the
# only runtime library copied to the game is the stripped, self-built SO.
DEBUG_SO="$OUT/libastraeoffline.so.debug"
cp "$SO" "$OUT/libastraeoffline.so"
"$OBJCOPY" --only-keep-debug "$OUT/libastraeoffline.so" "$DEBUG_SO"
RAW_SIZE=$(python3 -c 'import os,sys;print(os.path.getsize(sys.argv[1]))' "$OUT/libastraeoffline.so")
# All project APIs are dynamic exports validated below; local ELF symbols are
# kept in the separate .debug file and are not needed by the deployed library.
"$STRIP" --strip-all "$OUT/libastraeoffline.so"
"$OBJCOPY" --add-gnu-debuglink="$DEBUG_SO" "$OUT/libastraeoffline.so"
STRIPPED_SIZE=$(python3 -c 'import os,sys;print(os.path.getsize(sys.argv[1]))' "$OUT/libastraeoffline.so")
python3 "$ROOT/scripts/verify_elf_arch.py" "$OUT/libastraeoffline.so" --expected-abi arm64-v8a --cmake-cache "$BUILD_DIR/CMakeCache.txt"
python3 - "$OUT/SIZE_REPORT.txt" "$RAW_SIZE" "$STRIPPED_SIZE" "$DEBUG_SO" "$BUILD_DIR/generated/embedded_assets_size.txt" <<'PY_SIZE'
import os,sys
report,raw,deployed,debug,asset_stats=sys.argv[1:]
raw=int(raw);deployed=int(deployed)
stats={}
with open(asset_stats,encoding="utf-8") as src:
    for line in src:
        key,_,value=line.strip().partition("=")
        if key: stats[key]=value
saving=(100.0*(raw-deployed)/raw) if raw else 0.0
with open(report,"w",encoding="utf-8") as f:
    f.write(f"unstripped_elf_bytes={raw}\n")
    f.write(f"deployed_stripped_elf_bytes={deployed}\n")
    f.write(f"external_debug_symbols_bytes={os.path.getsize(debug)}\n")
    f.write(f"strip_size_reduction_percent={saving:.2f}\n")
    for key in ("asset_count","asset_raw_bytes","asset_packed_bytes","asset_packed_percent","asset_savings_percent"):
        if key in stats: f.write(f"{key}={stats[key]}\n")
    f.write("deployment_files=1\n")
    f.write("runtime_file=libastraeoffline.so\n")
    f.write("debug_file_is_not_a_runtime_dependency=true\n")
print(f"ELF strip: {raw} -> {deployed} bytes ({saving:.2f}% reduction); debug symbols kept separately")
if stats: print(f"Embedded JSON: {stats.get('asset_raw_bytes')} -> {stats.get('asset_packed_bytes')} bytes")
PY_SIZE

# Validate exports against the actual deployable, stripped ELF.
python3 - "$NM" "$OUT/libastraeoffline.so" "$ROOT/compat/original_abi/original_defined_exports.txt" <<'PY2'
import subprocess
import sys
nm, so, manifest = sys.argv[1:]
expected = {line.strip() for line in open(manifest, encoding="utf-8") if line.strip()}
output = subprocess.check_output([nm, "-D", "--defined-only", "-P", so], text=True, errors="replace")
actual = {line.split()[0].split("@")[0] for line in output.splitlines() if line.split()}
missing = sorted(expected - actual)
extra = sorted(actual - expected)
if missing or extra:
    print("Stripped export mismatch:", file=sys.stderr)
    if missing: print("  missing:", *missing, sep="\n    ", file=sys.stderr)
    if extra: print("  extra:", *extra, sep="\n    ", file=sys.stderr)
    raise SystemExit(1)
print(f"Stripped export surface matches the compatibility manifest: {len(actual)} symbols")
PY2
for lib in libc.so libdl.so liblog.so libz.so; do
    "$READELF" -d "$OUT/libastraeoffline.so" | grep -Fq "Shared library: [$lib]" || { echo "ERROR: stripped SO is missing DT_NEEDED $lib" >&2; exit 6; }
done
"$READELF" -h "$OUT/libastraeoffline.so" > "$OUT/ELF_REPORT.txt"
"$READELF" -d "$OUT/libastraeoffline.so" >> "$OUT/ELF_REPORT.txt"
"$READELF" -S "$OUT/libastraeoffline.so" >> "$OUT/ELF_REPORT.txt"
printf 'raw_size_bytes=%s\nstripped_size_bytes=%s\n' "$RAW_SIZE" "$STRIPPED_SIZE" >> "$OUT/ELF_REPORT.txt"
cat "$OUT/SIZE_REPORT.txt" >> "$OUT/ELF_REPORT.txt"
sha256sum "$OUT/libastraeoffline.so" "$DEBUG_SO" > "$OUT/SHA256.txt"
printf 'Built Android ARM64 shared library: %s\n' "$OUT/libastraeoffline.so"
printf 'Raw ELF: %s bytes; deployable stripped ELF: %s bytes; separate debug symbols: %s\n' "$RAW_SIZE" "$STRIPPED_SIZE" "$DEBUG_SO"
printf 'Deploy exactly one runtime file into the game: lib/arm64-v8a/libastraeoffline.so\n'
printf 'libz.so is an Android system library dependency; no second application SO is bundled.\n'
printf 'ShadowHook runs in direct-address-only mode; its private Linker monitor and companion probe ELF are disabled.\n'
printf 'This build checks the ELF architecture, SONAME, dependencies, and exported symbols.\n'
printf 'Live IL2CPP behavior and lobby entry still require validation in the target game.\n'
