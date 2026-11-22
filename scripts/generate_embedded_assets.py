#!/usr/bin/env python3
"""Generate a zlib-compressed read-only C++ bundle for JSON runtime assets."""
from __future__ import annotations
import argparse
import hashlib
import json
import zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'src'/'embedded_assets.cpp'

def byte_rows(data: bytes, width: int = 16) -> list[str]:
    return ["    "+", ".join(f"0x{value:02x}" for value in data[i:i+width])+"," for i in range(0,len(data),width)]

def main() -> None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stats',type=Path,default=None,help='write machine-readable raw/compressed asset sizes')
    args=parser.parse_args()
    files=sorted((ROOT/'assets').rglob('*.json'))
    rows=[]
    out=['#include "embedded_assets.hpp"','#include <cstring>','#include <limits>','#include <string>','#include <zlib.h>','namespace astrae::assets {','namespace {']
    total_raw=0
    total_compressed=0
    for i,path in enumerate(files):
        rel=path.relative_to(ROOT).as_posix()
        raw=path.read_bytes()
        raw.decode('utf-8')
        packed=zlib.compress(raw,9)
        if len(packed)>=len(raw):
            packed=raw
            compression=0
        else:
            compression=1
        digest=hashlib.sha256(raw).hexdigest()[:12]
        out.append(f'static constexpr unsigned char kAsset{i}[] = {{')
        out.extend(byte_rows(packed))
        out.append('};')
        rows.append((rel,f'kAsset{i}',len(raw),len(packed),compression))
        total_raw+=len(raw)
        total_compressed+=len(packed)
    out.append('struct Entry { const char *path; const unsigned char *data; size_t packed_size; size_t raw_size; bool compressed; };')
    out.append('static const Entry kEntries[] = {')
    for rel,sym,raw_size,packed_size,compressed in rows:
        out.append(f'    {{{json.dumps(rel)}, {sym}, {packed_size}u, {raw_size}u, {"true" if compressed else "false"}}},')
    out.append('};')
    out.extend(['}','bool read_embedded_asset(const std::string &path,std::string &out){','    std::string normalized=path;while(normalized.rfind("./",0)==0)normalized.erase(0,2);','    for(const auto &entry:kEntries){if(normalized!=entry.path)continue;','        if(entry.raw_size>static_cast<size_t>(std::numeric_limits<uLongf>::max())||entry.packed_size>static_cast<size_t>(std::numeric_limits<uLong>::max()))return false;','        out.resize(entry.raw_size);if(!entry.compressed){if(entry.packed_size!=entry.raw_size)return false;std::memcpy(out.data(),entry.data,entry.raw_size);return true;}','        uLongf actual=static_cast<uLongf>(entry.raw_size);const int rc=uncompress(reinterpret_cast<Bytef*>(out.data()),&actual,reinterpret_cast<const Bytef*>(entry.data),static_cast<uLong>(entry.packed_size));','        if(rc!=Z_OK||actual!=entry.raw_size){out.clear();return false;}return true;','    }','    return false;','}','size_t embedded_asset_count(){return sizeof(kEntries)/sizeof(kEntries[0]);}','}'])
    OUT.write_text('\n'.join(out)+'\n',encoding='utf-8')
    ratio=(100.0*total_compressed/total_raw) if total_raw else 0.0
    if args.stats:
        args.stats.parent.mkdir(parents=True,exist_ok=True)
        args.stats.write_text(f'asset_count={len(files)}\nasset_raw_bytes={total_raw}\nasset_packed_bytes={total_compressed}\nasset_packed_percent={ratio:.2f}\nasset_savings_percent={100.0-ratio:.2f}\n',encoding='utf-8')
    print(f'generated {OUT.relative_to(ROOT)}: {len(files)} assets, raw={total_raw} bytes, packed={total_compressed} bytes ({ratio:.1f}%), C++ source={OUT.stat().st_size} bytes')
if __name__=='__main__': main()
