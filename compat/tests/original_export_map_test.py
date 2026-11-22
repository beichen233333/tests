#!/usr/bin/env python3
"""Check the linker version script is an exact allow-list of original exports."""
import pathlib, re, sys
script=pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
expected={x.strip() for x in pathlib.Path(sys.argv[2]).read_text(encoding="utf-8").splitlines() if x.strip()}
match=re.search(r"global\s*:\s*(.*?)\blocal\s*:\s*\*\s*;",script,re.S)
if not match:
    raise SystemExit("version script must contain explicit global list followed by local: *;")
actual=set(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)\s*;",match.group(1)))
if actual!=expected:
    print("missing from map:",sorted(expected-actual))
    print("extra in map:",sorted(actual-expected))
    raise SystemExit(1)
print(f"exact linker export allow-list verified: {len(actual)} symbols")
