#!/usr/bin/env python3
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile

script, original, exports, readelf = map(str, sys.argv[1:5])
with tempfile.TemporaryDirectory(prefix="astrae-phase4-elf-") as td:
    report = pathlib.Path(td)/"elf.json"
    expected_sha = hashlib.sha256(pathlib.Path(original).read_bytes()).hexdigest()
    proc = subprocess.run([sys.executable, script, "--candidate", original, "--original", original, "--exports", exports, "--readelf", readelf, "--expected-original-sha256", expected_sha, "--report", str(report)], capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout
    result = json.loads(report.read_text(encoding="utf-8"))
    assert result["passed"]
    assert result["originalSha256"] == expected_sha
    assert result["candidateHeader"]["Machine"] == "AArch64"
    assert result["exportCount"] == result["allowListCount"] == 126
    assert result["candidateDynamic"]["soname"] == "libastraeoffline.so"
    assert set(result["candidateDynamic"]["needed"]) == {"libc.so", "libdl.so", "liblog.so"}
print("phase4 ELF validator self-comparison passed")
