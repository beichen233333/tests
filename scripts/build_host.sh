#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-host}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target astraeoffline permanent_raid_tables_test artifact_capacity_tables_test character99_asset_test character99_object_graph_test managed_class_describe_test touch_fallback_policy_test legacy_save_migration_test character99_invoke_marshalling_test backend_legacy_save_integration_test --parallel "$JOBS"
ctest --test-dir "$BUILD_DIR" --output-on-failure
printf 'Host shared library built: %s/libastraeoffline.so\n' "$BUILD_DIR"
printf 'Note: host architecture output is for development only, not an Android replacement.\n'
