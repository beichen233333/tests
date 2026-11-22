#!/usr/bin/env python3
import json
import pathlib
import subprocess
import sys
import tempfile

script = pathlib.Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix="astrae-phase4-diff-") as td:
    root = pathlib.Path(td)
    ref = {"schema": "astrae.replay.v1", "cases": [{"case_id": "mail-001", "protocol": "MailDetail", "request": {"Uid": 9}, "save_before": {"Mail": []}, "response": {"Mail": {"Uid": 9}, "ServerTime": 100}, "save_after": {"Mail": []}}]}
    cand = json.loads(json.dumps(ref))
    cand["cases"][0]["response"]["ServerTime"] = 101
    p_ref, p_cand = root/"ref.json", root/"cand.json"
    p_ref.write_text(json.dumps(ref), encoding="utf-8")
    p_cand.write_text(json.dumps(cand), encoding="utf-8")
    ok = subprocess.run([sys.executable, str(script), "--reference", str(p_ref), "--candidate", str(p_cand), "--ignore", "$.response.ServerTime", "--report", str(root/"pass.json")], capture_output=True, text=True)
    assert ok.returncode == 0, ok.stderr + ok.stdout
    assert json.loads((root/"pass.json").read_text())["summary"]["passed"]
    cand["cases"][0]["response"]["Mail"]["Uid"] = 10
    cand["cases"][0]["save_after"] = {"Mail": [{"Uid": 9}]}
    p_cand.write_text(json.dumps(cand), encoding="utf-8")
    bad = subprocess.run([sys.executable, str(script), "--reference", str(p_ref), "--candidate", str(p_cand), "--report", str(root/"bad.json")], capture_output=True, text=True)
    assert bad.returncode == 1, bad.stderr + bad.stdout
    report = json.loads((root/"bad.json").read_text())
    assert report["summary"]["responseMismatches"] == 1
    assert report["summary"]["saveMismatches"] == 1
    cand["cases"][0]["request"]["Uid"] = 10
    p_cand.write_text(json.dumps(cand), encoding="utf-8")
    bad_input = subprocess.run([sys.executable, str(script), "--reference", str(p_ref), "--candidate", str(p_cand)], capture_output=True, text=True)
    assert bad_input.returncode == 1 and 'inputMismatches' in bad_input.stdout
    # JSONL import path, including a valid single record.
    line = ref["cases"][0]
    p_jsonl = root/"ref.jsonl"
    p_jsonl.write_text(json.dumps(line)+"\n", encoding="utf-8")
    p_cand.write_text(json.dumps(line)+"\n", encoding="utf-8")
    jsonl_ok = subprocess.run([sys.executable, str(script), "--reference", str(p_jsonl), "--candidate", str(p_cand)], capture_output=True, text=True)
    assert jsonl_ok.returncode == 0, jsonl_ok.stderr + jsonl_ok.stdout
print("phase4 replay diff tests passed")
