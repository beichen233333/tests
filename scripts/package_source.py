#!/usr/bin/env python3
"""Create a source-only archive of the maintainable AstraeOffline project."""
from __future__ import annotations
import argparse
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED_DIRS = {
    ".git", ".idea", ".vscode", "build", "build-host", "build-android",
    "build-android-arm64", "artifacts", "dist", "__pycache__", ".pytest_cache",
    ".mypy_cache", ".cache", "tests", "useless", "third_party",
}
EXCLUDED_SUFFIXES = {".pyc", ".pyo", ".o", ".a", ".so", ".dylib", ".dll", ".class"}
EXCLUDED_NAMES = {"Thumbs.db", ".DS_Store"}

def should_skip(path: Path) -> bool:
    relative = path.relative_to(ROOT)
    if set(relative.parts) & EXCLUDED_DIRS:
        return True
    if path.name in EXCLUDED_NAMES or path.suffix.lower() in EXCLUDED_SUFFIXES:
        return True
    if path.name.endswith("~") or path.name.startswith(".#"):
        return True
    return False

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, help="destination ZIP path")
    args = parser.parse_args()
    output = Path(args.output).expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    files = sorted((path for path in ROOT.rglob("*") if path.is_file() and not should_skip(path) and path.resolve() != output), key=lambda path: path.relative_to(ROOT).as_posix())
    if not files:
        parser.error("no project files found")
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in files:
            archive.write(path, f"AstraeOfflineBackend/{path.relative_to(ROOT).as_posix()}")
    print(f"Source archive: {output}")
    print(f"Files: {len(files)}")
    print(f"Size: {output.stat().st_size} bytes")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
