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
if [[ ! -x "$NM" || ! -x "$READELF" ]]; then echo "ERROR: NDK LLVM tools missing under $TOOLBIN" >&2; exit 2; fi
python3 "$ROOT/scripts/generate_embedded_assets.py"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-android-arm64}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
cmake -S "$ROOT" -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-23 -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release -DASTRAE_BUILD_TESTS=OFF \
  -DASTRAE_ENABLE_RUNTIME_AUTOSTART="${ASTRAE_ENABLE_RUNTIME_AUTOSTART:-OFF}" \
  -DASTRAE_ORIGINAL_SO="${ASTRAE_ORIGINAL_SO:-}"
cmake --build "$BUILD_DIR" --parallel "$JOBS"
SO="$BUILD_DIR/libastraeoffline.so"
[[ -f "$SO" ]] || { echo "ERROR: expected output not found: $SO" >&2; exit 3; }
"$READELF" -h "$SO" | grep -q 'Machine:.*AArch64' || { echo "ERROR: output is not AArch64" >&2; exit 4; }
"$READELF" -d "$SO" | grep -q 'Library soname: \[libastraeoffline.so\]' || { echo "ERROR: incorrect SONAME" >&2; exit 5; }
python3 - "$NM" "$SO" "$ROOT/compat/original_abi/original_defined_exports.txt" <<'PY'
import subprocess,sys
nm,so,manifest=sys.argv[1:]
expected={x.strip() for x in open(manifest,encoding='utf-8') if x.strip()}
out=subprocess.check_output([nm,'-D','--defined-only','-P',so],text=True,errors='replace')
actual={line.split()[0].split('@')[0] for line in out.splitlines() if line.split()}
missing=sorted(expected-actual);extra=sorted(actual-expected)
if missing or extra:
 print('Export mismatch: missing=',missing,'extra=',extra,file=sys.stderr);sys.exit(1)
print(f'Export surface verified: {len(actual)} / {len(expected)} exact names')
PY
for lib in libc.so libdl.so liblog.so; do
  "$READELF" -d "$SO" | grep -Fq "Shared library: [$lib]" || { echo "ERROR: missing DT_NEEDED $lib" >&2; exit 6; }
done
if [[ -n "${ASTRAE_ORIGINAL_SO:-}" ]]; then
  [[ -f "$ASTRAE_ORIGINAL_SO" ]] || { echo "ERROR: ASTRAE_ORIGINAL_SO not found: $ASTRAE_ORIGINAL_SO" >&2; exit 7; }
  ORIGINAL_SHA256="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["original_elf"]["sha256"])' "$ROOT/assets/hooks/original_hook_manifest.json")"
  python3 "$ROOT/useless/tools/validate_android_artifact.py" \
    --candidate "$SO" --original "$ASTRAE_ORIGINAL_SO" \
    --exports "$ROOT/compat/original_abi/original_defined_exports.txt" \
    --readelf "$READELF" --expected-original-sha256 "$ORIGINAL_SHA256" \
    --report "$BUILD_DIR/PHASE4_VALIDATION.json"
else
  echo 'WARNING: set ASTRAE_ORIGINAL_SO to enable full original-vs-rebuild ELF validation.' >&2
fi
OUT="$ROOT/artifacts/android-arm64-v8a"
mkdir -p "$OUT"
cp "$SO" "$OUT/libastraeoffline.so"
"$READELF" -h "$OUT/libastraeoffline.so" > "$OUT/ELF_REPORT.txt"
"$READELF" -d "$OUT/libastraeoffline.so" >> "$OUT/ELF_REPORT.txt"
if [[ -f "$BUILD_DIR/PHASE4_VALIDATION.json" ]]; then cp "$BUILD_DIR/PHASE4_VALIDATION.json" "$OUT/PHASE4_VALIDATION.json"; fi
sha256sum "$OUT/libastraeoffline.so" > "$OUT/SHA256.txt"
printf 'Built Android ARM64 artifact: %s\n' "$OUT/libastraeoffline.so"
echo 'NOTE: this verifies ELF ABI/export metadata, not live Unity/IL2CPP behavior or full semantic equivalence.'
