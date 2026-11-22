#!/usr/bin/env python3
import subprocess,sys
if len(sys.argv)!=2: raise SystemExit("usage: abi_exports_test.py <shared-library>")
lib=sys.argv[1]
out=subprocess.check_output(["nm","-D","--defined-only",lib],text=True,stderr=subprocess.STDOUT)
names={line.split()[-1] for line in out.splitlines() if line.split()}
required={
 "AstraeOffline_Init","AstraeOffline_Shutdown","AstraeOffline_Handle","AstraeOffline_HandleEnvelope",
 "AstraeOffline_GetState","AstraeOffline_GetDiagnostics","AstraeOffline_GetProtocolCatalog",
 "AstraeOffline_DispatchManagedMessage","AstraeOffline_HandleManagedRequestForProtocol","AstraeOffline_RespondManaged",
 "AstraeOffline_Il2Cpp_Init","AstraeOffline_Il2Cpp_InitWait","AstraeOffline_Il2Cpp_Shutdown",
 "Java_com_nc_astraeoffline_OfflineBridge_getActions","Java_com_nc_astraeoffline_OfflineBridge_requestAction"
}
missing=sorted(required-names)
if missing:
 print("missing exports:",", ".join(missing));raise SystemExit(1)
print(f"ABI export smoke passed: {len(required)} required symbols present")
