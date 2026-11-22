#!/usr/bin/env python3
import ctypes
import json
import os
import shutil
import sys

lib_path, asset_dir, save_dir = sys.argv[1:4]
shutil.rmtree(save_dir, ignore_errors=True)
lib = ctypes.CDLL(lib_path)
lib.AstraeOffline_Init.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
lib.AstraeOffline_Init.restype = ctypes.c_int
lib.AstraeOffline_Handle.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
lib.AstraeOffline_Handle.restype = ctypes.c_void_p
lib.AstraeOffline_GetProtocolCatalog.restype = ctypes.c_void_p
lib.AstraeOffline_Free.argtypes = [ctypes.c_void_p]
lib.AstraeOffline_Shutdown.argtypes = []
def take(ptr):
    assert ptr, "native API returned null"
    try:
        return ctypes.string_at(ptr).decode("utf-8")
    finally:
        lib.AstraeOffline_Free(ptr)
assert lib.AstraeOffline_Init(save_dir.encode(), asset_dir.encode()) == 1
protocols = json.load(open(os.path.join(asset_dir, "schema", "protocol_index.json"), encoding="utf-8"))["requests"]
catalog = json.loads(take(lib.AstraeOffline_GetProtocolCatalog()))
assert catalog["requestCount"] == len(protocols) == 93
assert len(catalog["requests"]) == len(protocols)
assert all(x["handlerGroup"] != "unmapped" for x in catalog["requests"])
assert all(x["schemaAvailable"] for x in catalog["requests"])
for protocol in protocols:
    result = take(lib.AstraeOffline_Handle((protocol + ".Request").encode(), b"{}"))
    decoded = json.loads(result)
    assert isinstance(decoded, dict), f"non-object response: {protocol}"
    assert decoded.get("Error") != "UnknownProtocol", f"unregistered protocol: {protocol}"
lib.AstraeOffline_Shutdown()
print(f"{len(protocols)} protocol smoke tests passed; all are explicitly routed")
