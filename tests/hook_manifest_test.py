#!/usr/bin/env python3
import hashlib,json,struct,sys
from pathlib import Path
p=Path(sys.argv[1]);doc=json.loads(p.read_text());hooks=doc['hooks']
assert len(hooks)==39, f"expected 39 hooks, found {len(hooks)}"
assert doc['counts']=={'hooks':39,'with_trampoline':29,'without_trampoline':10,'replacement_signatures_available':37,'replacement_signatures_missing':2},doc['counts']
assert doc['original_elf']['machine']=='AArch64' and doc['original_elf']['elf_type']=='ET_DYN'
seen=set();tramp_slots=set()
for h in hooks:
    off=int(h['target_offset'],16);expected=bytes.fromhex(h['target_expected_bytes']);assert off%4==0 and off not in seen;seen.add(off);assert len(expected)==16
    assert h['replacement']['symbol'] and int(h['replacement']['rva'],16)>0
    if h['replacement']['signature']: assert '(' in h['replacement']['signature']
    tr=h['trampoline']
    if tr:
        slot=int(tr['storage_rva'],16);assert slot%8==0 and slot not in tramp_slots;tramp_slots.add(slot)
        assert int(tr['expected_bytes_rva'],16)==int(h['target_expected_bytes_rva'],16)
if len(sys.argv)>2:
    binary=Path(sys.argv[2]).read_bytes();sha=hashlib.sha256(binary).hexdigest();assert sha==doc['original_elf']['sha256'],(sha,doc['original_elf']['sha256'])
    assert binary[:4]==b'\x7fELF' and binary[4]==2 and binary[5]==1
    machine=struct.unpack_from('<H',binary,18)[0];assert machine==183,machine
    phoff=struct.unpack_from('<Q',binary,32)[0];phentsize,phnum=struct.unpack_from('<HH',binary,54)
    loads=[]
    for i in range(phnum):
        off=phoff+i*phentsize;ptype,flags=struct.unpack_from('<II',binary,off)
        if ptype==1:
            vaddr=struct.unpack_from('<Q',binary,off+16)[0];filesz=struct.unpack_from('<Q',binary,off+32)[0];memsz=struct.unpack_from('<Q',binary,off+40)[0]
            loads.append((vaddr,vaddr+filesz,vaddr+memsz,flags))
    def in_segment(addr,size,flag,backed=True):
        end=addr+size
        return any(addr>=a and end<=(b if backed else c) and (f&flag)==flag for a,b,c,f in loads)
    for h in hooks:
        rva=int(h['target_expected_bytes_rva'],16);assert binary[rva:rva+16]==bytes.fromhex(h['target_expected_bytes']),h['id']
        assert in_segment(rva,16,4),f"expected bytes not in readable PT_LOAD: {h['id']}"
        rep=int(h['replacement']['rva'],16);assert in_segment(rep,4,1),f"replacement not in executable PT_LOAD: {h['id']}"
        call=int(h['hook_call_site_rva'],16);assert in_segment(call,4,1),f"hook callsite not in executable PT_LOAD: {h['id']}"
        tr=h['trampoline']
        if tr:
            slot=int(tr['storage_rva'],16);assert in_segment(slot,8,2,backed=False),f"trampoline slot not in writable PT_LOAD: {hex(slot)}"
    print(f"hook manifest matches original AArch64 ELF: {len(hooks)} targets, {len(tramp_slots)} trampoline slots; expected bytes, callbacks, callsites and writable slots mapped")
else: print(f"hook manifest structure valid: {len(hooks)} targets, {len(tramp_slots)} trampoline slots")
