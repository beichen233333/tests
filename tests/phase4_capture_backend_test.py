#!/usr/bin/env python3
import json
import pathlib
import subprocess
import sys
import tempfile

script, library, assets = map(str, sys.argv[1:4])
with tempfile.TemporaryDirectory(prefix="astrae-phase4-capture-") as td:
    root = pathlib.Path(td)
    inputs = root/"inputs.json"
    output = root/"candidate.json"
    inputs.write_text(json.dumps({"schema": "astrae.replay.inputs.v1", "cases": [{"case_id": "log-001", "protocol": "LogTest", "request": {}, "save_before": {}}]}), encoding="utf-8")
    proc = subprocess.run([sys.executable, script, "--library", library, "--assets", assets, "--inputs", str(inputs), "--output", str(output)], capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout
    report = json.loads(output.read_text(encoding="utf-8"))
    assert report["schema"] == "astrae.replay.v1"
    assert len(report["cases"]) == 1
    case = report["cases"][0]
    assert case["case_id"] == "log-001" and case["protocol"] == "LogTest"
    assert isinstance(case["response"], dict) and isinstance(case["save_after"], dict)
print("phase4 candidate replay capture tests passed")
