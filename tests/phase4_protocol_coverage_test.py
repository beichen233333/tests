#!/usr/bin/env python3
import csv
import json
import pathlib
import subprocess
import sys
import tempfile

script, library, assets = map(str, sys.argv[1:4])
with tempfile.TemporaryDirectory(prefix="astrae-phase4-coverage-") as td:
    output = pathlib.Path(td)/"coverage.csv"
    proc = subprocess.run([sys.executable, script, "--library", library, "--assets", assets, "--output", str(output)], capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout
    summary = json.loads(proc.stdout)
    with output.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    assert summary["requestCount"] == len(rows) == 93
    assert summary["schemaAvailableCount"] == 93
    assert all(row["route_status"] == "mapped" for row in rows)
    assert all(row["semantic_parity"] == "not_verified" for row in rows)
print("phase4 protocol coverage export tests passed")
