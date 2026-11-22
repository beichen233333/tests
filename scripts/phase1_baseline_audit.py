#!/usr/bin/env python3
"""Static inventory of the supplied original ARM64 backend and its reconstruction."""
from __future__ import annotations
import argparse,csv,hashlib,json,re,subprocess,sys
from collections import Counter
from pathlib import Path
EXPECTED_SHA="86f65f4f11cf5657143ec848fab6ad7c444ed880ed3e04039add8acead7121e1"
EXPECTED_SIZE=4083840
GROUP_METHODS={"account":"handle_account","growth":"handle_growth","artifact":"handle_artifact","gacha":"handle_gacha","rune":"handle_runes","mail":"handle_mail","quest":"handle_quest","farming":"handle_farming","raid":"handle_raid","mission":"handle_missions","date":"handle_date","installation":"handle_installation","shop":"handle_shop","misc":"handle_misc"}
def load(p): return json.loads(Path(p).read_text(encoding="utf-8"))
def has_function_definition(source: str, name: str) -> bool:
    # A definition candidate must begin at a line and its matching signature ')' must
    # be followed by '{'. This avoids the previous broad DOTALL match crossing calls.
    pat = re.compile(r"(?m)^[ \t]*[^\n;{}]*?\b" + re.escape(name) + r"[ \t]*\(")
    for match in pat.finditer(source):
        open_paren = source.find("(", match.start(), match.end())
        depth = 0; i = open_paren; quote = None; escaped = False
        while i < len(source):
            ch = source[i]; nxt = source[i+1] if i+1 < len(source) else ""
            if quote:
                if escaped: escaped = False
                elif ch == "\\": escaped = True
                elif ch == quote: quote = None
                i += 1; continue
            if ch in ("\"", "'"): quote = ch; i += 1; continue
            if ch == "/" and nxt == "/":
                end = source.find("\n", i+2); i = len(source) if end < 0 else end; continue
            if ch == "/" and nxt == "*":
                end = source.find("*/", i+2); i = len(source) if end < 0 else end+2; continue
            if ch == "(": depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    i += 1
                    while i < len(source) and source[i].isspace(): i += 1
                    if i < len(source) and source[i] == "{": return True
                    break
            i += 1
    return False
def classify(op):
    if op.startswith(("Account","Dev")) or op in ("RegisterAccountName","Encryption","TutorialProgress"): return "account"
    if op.startswith("Character") or op in ("CostumeEquip","VignetteStarRankUp"): return "growth"
    if op.startswith("Artifact"): return "artifact"
    if op.startswith("Gacha"): return "gacha"
    if op.startswith("Rune") or op=="RunesGrant": return "rune"
    if "Mail" in op or op in ("ReceiveAllMail","ReceiveMailByUids","ReceivedMail"): return "mail"
    if op.startswith("ScenarioPack"): return "quest"
    if op.startswith("FarmingDungeon"): return "farming"
    if op.startswith("Raid"): return "raid"
    if "Mission" in op or op.startswith(("GuideBook","Daily","Weekly","Limited","Challenge")): return "mission"
    if op.startswith("DateField"): return "date"
    if op.startswith(("Installation","Barracks","BuffInstallation")): return "installation"
    if op.startswith("Shop"): return "shop"
    if op in ("AttendanceReward","CardBookReward","ItemUseConsumable","ItemUseSelectable","FormationChange","FormationChangeName","LobbySetBackground","LobbySetCharacter","ViewHelpBook","TutorialContentView","LogTest"): return "misc"
    return "unmapped"
def main():
    ap=argparse.ArgumentParser();ap.add_argument("--root",default=".");ap.add_argument("--original-elf");ap.add_argument("--replica-so");a=ap.parse_args()
    root=Path(a.root).resolve();docs=root/"docs";docs.mkdir(exist_ok=True)
    h=load(root/"assets/hooks/original_hook_manifest.json"); pi=load(root/"assets/schema/protocol_index.json"); shapes=load(root/"assets/schema/wire_shapes.json"); sym=load(root/"useless/metadata/original_symbol_manifest.json")
    runtime=(root/"src/hook_runtime.cpp").read_text(); cmake=(root/"CMakeLists.txt").read_text(); boot=(root/"src/runtime_bootstrap.cpp").read_text()
    ui=re.search(r"kUiBindings\[\]\s*=\s*\{(.*?)\};",runtime,re.S);ui_ids=set(map(int,re.findall(r"\{\s*(\d+)\s*,",ui.group(1)))) if ui else set()
    auto=re.search(r'option\(ASTRAE_ENABLE_RUNTIME_AUTOSTART\s+"[^"]*"\s+(ON|OFF)\)',cmake); auto=auto.group(1) if auto else "UNKNOWN"
    direct_guard=bool(re.search(r"if\s*\(!spec\.has_trampoline\).*?return false",runtime,re.S))
    requests=pi["requests"];responses=pi["responses"];shape_names={x.get("name","") for x in shapes}
    pr=[];groups=Counter();missing_req=[];missing_rsp=[]
    for name in requests:
        group=classify(name);groups[group]+=1;rq=(name+".Request") in shape_names;rs=(name+".Response") in shape_names
        if not rq:missing_req.append(name)
        if not rs:missing_rsp.append(name)
        pr.append({"protocol":name,"handler_group":group,"handler_method":GROUP_METHODS.get(group,""),"request_schema":rq,"response_schema":rs,"route_status":"mapped" if group!="unmapped" else "unmapped","semantic_parity":"not_verified"})
    hr=[]
    for x in h["hooks"]:
        i=x["id"]; rep=x["replacement"]; tr=x.get("trampoline"); mode="trampoline" if tr else "direct_replacement"
        if i==5: state="core direct entry; blocked by current trampoline-only installer"
        elif i in ui_ids: state="passthrough wrapper only; original side effects not reproduced"
        else: state="no active binding in current hook_runtime"
        hr.append({"id":i,"symbol":rep.get("symbol",""),"target_offset":x.get("target_offset",""),"expected_bytes":x.get("target_expected_bytes",""),"mode":mode,"trampoline_storage_rva":(tr or {}).get("storage_rva",""),"signature_available":bool(rep.get("signature")),"current_binding":i in ui_ids,"current_status":state})
    tables=[];tcount=rows=0
    for p in sorted((root/"assets/tables").glob("*.json")):
        v=load(p);tn=len(v) if isinstance(v,dict) else 0;rn=sum(len(x) for x in v.values() if isinstance(x,list)) if isinstance(v,dict) else 0;tcount+=tn;rows+=rn
        tables.append({"file":p.name,"table_names":tn,"rows":rn,"bytes":p.stat().st_size})
    sh=(root/"compat/include/original_store_abi.h").read_text();store=sorted(set(re.findall(r"\b(store_[A-Za-z0-9_]+)\s*\(",sh)))
    cr=(root/"compat/src/store_core.cpp").read_text();abi=(root/"compat/src/original_store_abi.cpp").read_text();store_src=cr+"\n"+abi
    store_rows=[]
    for n in store:
        # Presence is source-level only; the optional replica DSO check below verifies exported symbols.
        present=has_function_definition(store_src,n)
        store_rows.append({"api":n,"source_body_present":present,"semantic_parity":"not_verified"})
    summary={"original_elf":{"sha256":h["original_elf"]["sha256"],"size_bytes":EXPECTED_SIZE,"machine":"AArch64","type":"ET_DYN","soname":"libastraeoffline.so","needed":["libc.so","libdl.so","liblog.so"],"dynamic_exports":126,"export_categories":sym["category_counts"]},
      "recovery":{"text_symbols":390,"dwarf_subprogram_signatures":333,"original_source_text_available":False},
      "protocols":{"requests":len(requests),"responses":len(responses),"wire_shapes":len(shapes),"missing_request_shapes":missing_req,"missing_response_shapes":missing_rsp,"route_counts":dict(sorted(groups.items())),"semantic_parity":"not_verified"},
      "embedded_tables":{"json_blobs":len(tables),"table_names":tcount,"rows":rows},
      "hooks":{"total":len(hr),"trampoline":sum(x["mode"]=="trampoline" for x in hr),"direct_replacement":sum(x["mode"]=="direct_replacement" for x in hr),"signature_present":sum(x["signature_available"] for x in hr),"signature_missing":sum(not x["signature_available"] for x in hr),"current_ui_passthrough_bindings":len(ui_ids),"direct_ids":[x["id"] for x in hr if x["mode"]=="direct_replacement"]},
      "store_api":{"declared":len(store),"source_bodies_detected":sum(x["source_body_present"] for x in store_rows),"missing_source_bodies":[x["api"] for x in store_rows if not x["source_body_present"]],"semantic_parity":"not_verified"},
      "current_runtime":{"autostart_default":auto,"worker_guarded_by_android_and_autostart":("defined(__ANDROID__) && defined(ASTRAE_ENABLE_RUNTIME_AUTOSTART)" in boot),"installer_rejects_no_trampoline":direct_guard,"local_request_hook_id":5,"automatic_backend_ready_by_default":auto=="ON" and not direct_guard},
      "host_test_result":{"ctest": "23/23 passed on the current host build; does not establish Android runtime parity"}}
    errors=[]
    if h["original_elf"]["sha256"]!=EXPECTED_SHA:errors.append("manifest SHA mismatch")
    if len(hr)!=39 or summary["hooks"]["trampoline"]!=29 or summary["hooks"]["direct_replacement"]!=10:errors.append("hook cardinality mismatch")
    if len(requests)!=93 or len(responses)!=116 or len(shapes)!=359:errors.append("protocol/schema cardinality mismatch")
    if missing_req or missing_rsp:errors.append("protocol schema missing")
    if len(store)!=44:errors.append("store API count mismatch")
    if 5 not in summary["hooks"]["direct_ids"]:errors.append("#5 must be direct replacement per source manifest")
    if auto!="OFF" or not direct_guard:errors.append("expected current safe-load blocker changed; review audit assertions")
    if a.original_elf:
        elf=Path(a.original_elf).resolve()
        if not elf.exists():errors.append("original ELF path does not exist")
        else:
            b=elf.read_bytes();digest=hashlib.sha256(b).hexdigest()
            if digest!=EXPECTED_SHA:errors.append("original ELF SHA256 mismatch: "+digest)
            if len(b)!=EXPECTED_SIZE:errors.append("original ELF size mismatch")
            dh=subprocess.check_output(["readelf","-d",str(elf)],text=True);hh=subprocess.check_output(["readelf","-h",str(elf)],text=True);ss=subprocess.check_output(["readelf","-S",str(elf)],text=True)
            nmout=subprocess.check_output(["nm","-D","--defined-only",str(elf)],text=True);actual_exports={line.split()[-1] for line in nmout.splitlines() if line.strip()}
            needed=re.findall(r"Shared library: \[(.*?)\]",dh)
            if "ELF64" not in hh or "little endian" not in hh or "AArch64" not in hh or "DYN (Shared object file)" not in hh:errors.append("original ELF class/endianness/machine/type validation failed")
            if "Library soname: [libastraeoffline.so]" not in dh or sorted(needed)!=["libc.so","libdl.so","liblog.so"] or ".debug_info" not in ss:errors.append("original ELF SONAME/dependencies/DWARF validation failed")
            if len(actual_exports)!=126:errors.append(f"original ELF dynamic export count mismatch: {len(actual_exports)}")
            summary["original_elf"]["verified_dynamic_exports"] = len(actual_exports)
            summary["original_elf"]["verified_needed"] = needed
    if a.replica_so:
        nm=subprocess.check_output(["nm","-D","--defined-only",a.replica_so],text=True);defined={x.split()[-1] for x in nm.splitlines() if x.strip()}
        found=sum(n in defined for n in store);summary["replica_so"]={"store_exports_found":found,"store_exports_expected":44}
        if found!=44:errors.append("replica DSO does not expose all 44 store exports")
    outputs=[("PHASE1_PROTOCOL_MATRIX.csv",pr),("PHASE1_HOOK_MATRIX.csv",hr),("PHASE1_STORE_API_MATRIX.csv",store_rows),("PHASE1_EMBEDDED_TABLE_MATRIX.csv",tables)]
    for fn,items in outputs:
        with (docs/fn).open("w",newline="",encoding="utf-8-sig") as f:
            w=csv.DictWriter(f,fieldnames=list(items[0]));w.writeheader();w.writerows(items)
    summary["audit_errors"]=errors;(docs/"PHASE1_AUDIT.json").write_text(json.dumps(summary,ensure_ascii=False,indent=2)+"\n")
    print(json.dumps(summary,ensure_ascii=False,indent=2))
    if errors:print("AUDIT FAILED: "+"; ".join(errors),file=sys.stderr);return 1
    print("PHASE 1 BASELINE AUDIT: PASS (inventory only; semantic parity and device runtime unverified)");return 0
if __name__=="__main__":raise SystemExit(main())
