package com.nc.astraeoffline;
public final class OfflineBridge {
    static { System.loadLibrary("astraeoffline"); }
    private OfflineBridge() {}
    public static native int nativeInit(String saveDirectory, String tableDirectory);
    public static native String nativeDispatch(String protocol, String requestJson);
    public static native int nativeIl2CppInit(String wireShapesJsonPath);
    /** Bounded wait for libil2cpp.so; timeout=0 defaults to 120s and poll=0 defaults to 100ms. */
    public static native int nativeIl2CppInitWait(String wireShapesJsonPath, int timeoutMs, int pollIntervalMs);
    public static native String nativeIl2CppStatus();
    /** objectPtr must be a live object pointer from this game process; native code attaches the calling thread for scoped access. */
    public static native String nativeReadManaged(String wireType, long objectPtr);
    /** Returns a strong pinned GCHandle token; release it with nativeFreeManagedHandle. */
    public static native long nativeBuildManaged(String wireType, String json);
    public static native long nativeGetManagedTarget(int gcHandle);
    public static native void nativeFreeManagedHandle(int gcHandle);
    /** Native integration only: networkObject/requestObject are live IL2CPP pointers from this process. */
    public static native int nativeHandleManagedRequest(long networkObject, String protocolName, long requestObject);
    /** Native integration only: dispatches a JSON response through the game's managed callback. */
    public static native int nativeRespondManaged(long networkObject, String protocolName, String responseJson);
}
