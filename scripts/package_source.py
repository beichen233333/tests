#!/usr/bin/env python3
"""Create a clean source archive without build products or prebuilt shared libraries."""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED_DIRS = {".git", "build", "build-host", "build-android", "build-android-arm64", "artifacts", "dist", "__pycache__", ".pytest_cache", ".mypy_cache", ".cache", ".idea", ".vscode"}
EXCLUDED_SUFFIXES = {".pyc", ".pyo", ".o", ".a", ".so", ".dylib", ".dll", ".class"}
EXCLUDED_NAMES = {"Thumbs.db", ".DS_Store"}

def should_skip(path: Path, include_useless: bool) -> bool:
    rel = path.relative_to(ROOT)
    parts = set(rel.parts)
    if parts & EXCLUDED_DIRS: return True
    if rel.parts and rel.parts[0].startswith("build"): return True
    if not include_useless and rel.parts and rel.parts[0] == "useless": return True
    if path.name in EXCLUDED_NAMES or path.suffix.lower() in EXCLUDED_SUFFIXES: return True
    if path.name.endswith("~") or path.name.startswith(".#"): return True
    return False

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, help="output ZIP path")
    parser.add_argument("--exclude-useless", action="store_true", help="omit archived reverse-engineering material")
    args = parser.parse_args()
    output = Path(args.output).expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    prefix = "AstraeOfflineBackend"
    files = [p for p in ROOT.rglob("*") if p.is_file() and not should_skip(p, not args.exclude_useless) and p.resolve() != output]
    files.sort(key=lambda p: p.relative_to(ROOT).as_posix())
    if not files:
        parser.error("no source files found")
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in files:
            rel = path.relative_to(ROOT).as_posix()
            archive.write(path, f"{prefix}/{rel}")
    print(f"Source archive: {output}")
    print(f"Files: {len(files)}")
    print(f"Includes useless/: {not args.exclude_useless}")
    print(f"Size: {output.stat().st_size} bytes")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
