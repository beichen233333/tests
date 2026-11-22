#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-host}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DASTRAE_BUILD_TESTS=ON
cmake --build "$BUILD_DIR" --parallel "$JOBS"
ctest --test-dir "$BUILD_DIR" --output-on-failure
OUT="$ROOT/artifacts/host-x86_64"
mkdir -p "$OUT"
cp "$BUILD_DIR/libastraeoffline.so" "$OUT/libastraeoffline.so"
ARCH="$(uname -m)"
FILE_INFO="$(file "$OUT/libastraeoffline.so")"
SONAME="$(readelf -d "$OUT/libastraeoffline.so" | sed -n 's/.*Library soname: \[\([^]]*\)\].*/\1/p' | head -1)"
SHA="$(sha256sum "$OUT/libastraeoffline.so" | awk '{print $1}')"
cat > "$OUT/HOST_BUILD.txt" <<TXT
AstraeOfflineBackend host artifact (development / regression only)
Platform: $(uname -s) $ARCH
File: libastraeoffline.so
SHA-256: $SHA
SONAME: $SONAME
ELF: $FILE_INFO
Build directory: $BUILD_DIR
Tests: CTest completed successfully (see build log at build time)
Important: this is a host-architecture ELF, not an Android arm64-v8a replacement.
TXT
readelf -h "$OUT/libastraeoffline.so" > "$OUT/ELF_REPORT.txt"
readelf -d "$OUT/libastraeoffline.so" >> "$OUT/ELF_REPORT.txt"
printf 'Built host-only artifact: %s\n' "$OUT/libastraeoffline.so"
