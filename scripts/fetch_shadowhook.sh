#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="v2.0.1"
DEST="${ASTRAE_SHADOWHOOK_SOURCE_DIR:-$ROOT/third_party/android-inline-hook}"
if [[ -f "$DEST/shadowhook/src/main/cpp/include/shadowhook.h" ]]; then
    FOUND_VERSION="$(grep -m1 '^#define SHADOWHOOK_VERSION ' "$DEST/shadowhook/src/main/cpp/include/shadowhook.h" | sed -E 's/.*"([^"]+)".*/\1/')"
    [[ "$FOUND_VERSION" == "${VERSION#v}" ]] || { echo "ERROR: ShadowHook version mismatch: found ${FOUND_VERSION:-unknown}, expected ${VERSION#v}" >&2; exit 2; }
    python3 "$ROOT/scripts/patch_shadowhook.py" "$DEST"
    echo "Using ShadowHook $FOUND_VERSION at $DEST (direct-address-only patch applied)"
    exit 0
fi
if [[ -e "$DEST" ]]; then
    echo "ERROR: $DEST exists but is not a valid ShadowHook checkout" >&2
    exit 2
fi
mkdir -p "$(dirname "$DEST")"
command -v git >/dev/null 2>&1 || { echo "ERROR: git is required to fetch ShadowHook $VERSION" >&2; exit 2; }
echo "Fetching ByteDance ShadowHook $VERSION from GitHub..."
git -c advice.detachedHead=false clone --depth 1 --branch "$VERSION" https://github.com/bytedance/android-inline-hook.git "$DEST"
FOUND_VERSION="$(grep -m1 '^#define SHADOWHOOK_VERSION ' "$DEST/shadowhook/src/main/cpp/include/shadowhook.h" | sed -E 's/.*"([^"]+)".*/\1/')"
[[ "$FOUND_VERSION" == "${VERSION#v}" ]] || { echo "ERROR: fetched ShadowHook version mismatch" >&2; exit 3; }
python3 "$ROOT/scripts/patch_shadowhook.py" "$DEST"
echo "ShadowHook $FOUND_VERSION ready: $DEST (direct-address-only patch applied)"
