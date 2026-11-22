import ctypes
import json
import sys
from pathlib import Path
lib_path, schema_path = sys.argv[1], sys.argv[2]
lib = ctypes.CDLL(str(Path(lib_path).resolve()))
lib.AstraeOffline_Il2Cpp_Init.argtypes = [ctypes.c_char_p]
lib.AstraeOffline_Il2Cpp_Init.restype = ctypes.c_int
lib.AstraeOffline_Il2Cpp_InitWait.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32]
lib.AstraeOffline_Il2Cpp_InitWait.restype = ctypes.c_int
lib.AstraeOffline_Il2Cpp_GetStatus.restype = ctypes.c_void_p
lib.AstraeOffline_Il2Cpp_GetLastError.restype = ctypes.c_void_p
lib.AstraeOffline_Il2Cpp_ResolveType.argtypes = [ctypes.c_char_p]
lib.AstraeOffline_Il2Cpp_ReadObject.argtypes = [ctypes.c_char_p, ctypes.c_void_p]
lib.AstraeOffline_Il2Cpp_ReadObject.restype = ctypes.c_void_p
lib.AstraeOffline_Il2Cpp_BuildObject.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
lib.AstraeOffline_Il2Cpp_BuildObject.restype = ctypes.c_uint32
lib.AstraeOffline_Version.restype = ctypes.c_char_p
lib.AstraeOffline_Il2Cpp_ResolveType.restype = ctypes.c_void_p
lib.AstraeOffline_Free.argtypes = [ctypes.c_void_p]
lib.AstraeOffline_Free.restype = None

def take(ptr):
    if not ptr:
        raise RuntimeError("native API returned a null string")
    try:
        return ctypes.string_at(ptr).decode("utf-8")
    finally:
        lib.AstraeOffline_Free(ptr)

# Host builds intentionally cannot attach to a live Unity IL2CPP runtime.
assert lib.AstraeOffline_Il2Cpp_Init(schema_path.encode()) == 0
assert lib.AstraeOffline_Il2Cpp_InitWait(schema_path.encode(), 1, 1) == 0
status = json.loads(take(lib.AstraeOffline_Il2Cpp_GetStatus()))
assert status["initialized"] is False
assert status["coreNetworkClassReady"] is False
assert status["attached"] is False
assert status["canReadManagedObjects"] is False
assert status["canBuildManagedObjects"] is False
assert status["canEnumerateCollections"] is False
assert status["canResolveGenericArguments"] is False
assert status["wireSchema"]["typeCount"] == 359
assert "Android game process" in status["lastError"]
assert lib.AstraeOffline_Version().decode().endswith("1.0.0-stage4")
assert lib.AstraeOffline_Il2Cpp_BuildObject(b"AccountLogin.Response", b"{}") == 0
read_error = json.loads(take(lib.AstraeOffline_Il2Cpp_ReadObject(b"AccountLogin.Response", ctypes.c_void_p(1))))
assert read_error["error"] == "ManagedWireReadFailed"
resolved = json.loads(take(lib.AstraeOffline_Il2Cpp_ResolveType(b"System.String")))
assert resolved["resolved"] is False
last_error = take(lib.AstraeOffline_Il2Cpp_GetLastError())
assert last_error
lib.AstraeOffline_Il2Cpp_Shutdown()
print("managed runtime C ABI smoke OK: schema loaded, host runtime correctly unavailable")
