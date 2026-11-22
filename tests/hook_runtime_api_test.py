#!/usr/bin/env python3
import ctypes,json,sys
lib=ctypes.CDLL(sys.argv[1]);lib.AstraeOffline_ValidateOriginalHooks.restype=ctypes.c_void_p;lib.AstraeOffline_Free.argtypes=[ctypes.c_void_p]
p=lib.AstraeOffline_ValidateOriginalHooks();assert p
try:
    doc=json.loads(ctypes.string_at(p).decode())
finally: lib.AstraeOffline_Free(p)
assert doc['installAttempted'] is False
assert doc['totalHooks']==39 and doc['expectedTrampolines']==29 and doc['expectedDirectHooks']==10
assert doc['platformSupported'] is False and doc['fingerprintCompatible'] is False
assert doc['error']=='Android process inspection is unavailable on this host platform'
print('host Hook validation API is fail-closed and read-only')
