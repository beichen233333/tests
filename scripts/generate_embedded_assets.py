#!/usr/bin/env python3
"""Generate a read-only C++ bundle for the JSON runtime assets."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'src'/'embedded_assets.cpp'
def main() -> None:
    files=sorted((ROOT/'assets').rglob('*.json'))
    rows=[]
    out=['#include "embedded_assets.hpp"','#include <cstring>','#include <string>','namespace astrae::assets {','namespace {']
    for i,path in enumerate(files):
        rel=path.relative_to(ROOT).as_posix()
        data=path.read_text(encoding='utf-8')
        digest=hashlib.sha256(data.encode('utf-8')).hexdigest()[:10]
        delim='A'+digest
        while f'){delim}"' in data: delim='A'+hashlib.sha256((delim+data).encode('utf-8')).hexdigest()[:10]
        out.append(f'static constexpr char kAsset{i}[] = R"{delim}({data}){delim}";')
        rows.append((rel,f'kAsset{i}'))
    out.append('struct Entry { const char *path; const char *data; };')
    out.append('static const Entry kEntries[] = {')
    for rel,sym in rows: out.append(f'    {{{json.dumps(rel)}, {sym}}},')
    out.append('};')
    out.extend(['}','bool read_embedded_asset(const std::string &path,std::string &out){std::string normalized=path;while(normalized.rfind("./",0)==0)normalized.erase(0,2);for(const auto &entry:kEntries)if(normalized==entry.path){out.assign(entry.data,std::strlen(entry.data));return true;}return false;}','size_t embedded_asset_count(){return sizeof(kEntries)/sizeof(kEntries[0]);}','}'])
    OUT.write_text('\n'.join(out)+'\n',encoding='utf-8')
    print(f'generated {OUT.relative_to(ROOT)}: {len(files)} assets, {OUT.stat().st_size} bytes')
if __name__=='__main__': main()
