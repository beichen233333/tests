#!/usr/bin/env python3
import ctypes
import json
import os
import shutil
import sys
import tempfile

lib_path, source_assets, save_dir = sys.argv[1:4]
assets_dir = save_dir + "-assets"
shutil.rmtree(save_dir, ignore_errors=True)
shutil.rmtree(assets_dir, ignore_errors=True)
shutil.copytree(source_assets, assets_dir)
# Deliberately make the reward overflow after a successful spend. The request
# must roll back the spend and leave inventory unchanged.
rules = {
    "shopItems": {
        "91001": {
            "costs": [{"Type": 1, "TableId": 1001, "Count": 10}],
            "rewards": [{"Type": 1, "TableId": 2001, "Count": 2147483648}],
        },
        "91002": {
            "costs": [{"Type": 1, "TableId": 1001, "Count": 5}],
            "rewards": [{"Type": 1, "TableId": 2001, "Count": 7}],
        },
    }
}
with open(os.path.join(assets_dir, "rules.json"), "w", encoding="utf-8") as f:
    json.dump(rules, f)
lib = ctypes.CDLL(lib_path)
lib.AstraeOffline_Init.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
lib.AstraeOffline_Init.restype = ctypes.c_int
lib.AstraeOffline_Handle.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
lib.AstraeOffline_Handle.restype = ctypes.c_void_p
lib.AstraeOffline_GetState.restype = ctypes.c_void_p
lib.AstraeOffline_Free.argtypes = [ctypes.c_void_p]
lib.AstraeOffline_Shutdown.argtypes = []
def take(ptr):
    if not ptr:
        raise AssertionError("native API returned null")
    try:
        return ctypes.string_at(ptr).decode("utf-8")
    finally:
        lib.AstraeOffline_Free(ptr)
def call(protocol, request):
    return take(lib.AstraeOffline_Handle(protocol.encode(), json.dumps(request).encode()))
assert lib.AstraeOffline_Init(save_dir.encode(), assets_dir.encode()) == 1
call("DevCheat.Request", {"Rewards": [{"Type": 1, "TableId": 1001, "Count": 15}]})
before = json.loads(take(lib.AstraeOffline_GetState()))
assert before["inventory"][0]["Count"] == 15
call("ShopBuyItem.Request", {"ShopItemTableId": 91001, "Count": 1})
after_failed = json.loads(take(lib.AstraeOffline_GetState()))
assert after_failed["inventory"][0]["Count"] == 15, "failed reward grant must roll back prior cost spend"
call("ShopBuyItem.Request", {"ShopItemTableId": 91002, "Count": 1})
after_success = json.loads(take(lib.AstraeOffline_GetState()))
values = {(x["Type"], x["TableId"]): x["Count"] for x in after_success["inventory"]}
assert values[(1, 1001)] == 10
assert values[(1, 2001)] == 7
lib.AstraeOffline_Shutdown()
print("transaction rollback and commit tests passed")
