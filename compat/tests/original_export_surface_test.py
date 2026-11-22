#!/usr/bin/env python3
import ctypes
import pathlib
import subprocess
import sys

lib_path = pathlib.Path(sys.argv[1]).resolve()
expected_path = pathlib.Path(sys.argv[2]).resolve()
expected = {x.strip() for x in expected_path.read_text().splitlines() if x.strip()}
raw = subprocess.check_output(["nm", "-D", "--defined-only", str(lib_path)], text=True)
actual = {line.split()[-1] for line in raw.splitlines() if line.split()}
missing = expected - actual
extra = actual - expected
# __errno_location in the original is a 4-byte tail-branch to the platform libc errno provider.
# The rebuilt DSO imports/resolves the provider from libc rather than shadowing libc's TLS errno.
allowed_missing = {"__errno_location"}
if missing - allowed_missing or extra:
    print(f"missing={sorted(missing)}")
    print(f"extra={sorted(extra)}")
    raise SystemExit(1)
lib = ctypes.CDLL(str(lib_path))
for name in expected - allowed_missing:
    try:
        getattr(lib, name)
    except AttributeError:
        print(f"dlsym failed: {name}")
        raise SystemExit(1)
try:
    getattr(lib, "__errno_location")
except AttributeError:
    print("dlsym failed: __errno_location (expected through libc dependency)")
    raise SystemExit(1)
print(f"original export surface verified: {len(expected)-1} defined custom exports; __errno_location delegated to libc")
