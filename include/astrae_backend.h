#pragma once
#include <stddef.h>
#include <stdint.h>
#if defined(__ANDROID__)
#include <jni.h>
#define ASTRAE_EXPORT __attribute__((visibility("default")))
#else
#define ASTRAE_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* Set save_directory to the app-private writable directory. table_directory may be NULL. */
ASTRAE_EXPORT int AstraeOffline_Init(const char *save_directory, const char *table_directory);
ASTRAE_EXPORT void AstraeOffline_Shutdown(void);
/* protocol may be either Name or Name.Request. Returned C string must be freed with AstraeOffline_Free. */
ASTRAE_EXPORT char *AstraeOffline_Handle(const char *protocol, const char *request_json);
/* Envelope form: {"protocol":"Name.Request","request":{...}}. */
ASTRAE_EXPORT char *AstraeOffline_HandleEnvelope(const char *envelope_json);
ASTRAE_EXPORT char *AstraeOffline_GetState(void);
ASTRAE_EXPORT int AstraeOffline_ReplaceState(const char *state_json);
/* Read-only framework introspection and non-blocking schema validation helpers. */
ASTRAE_EXPORT char *AstraeOffline_GetDiagnostics(void);
ASTRAE_EXPORT char *AstraeOffline_GetProtocolCatalog(void);
ASTRAE_EXPORT char *AstraeOffline_ValidateRequest(const char *protocol, const char *request_json);
ASTRAE_EXPORT void AstraeOffline_Free(void *p);
ASTRAE_EXPORT const char *AstraeOffline_Version(void);
/* Original library's two JNI entry points are intentionally preserved as ABI-compatible no-op functions. */
ASTRAE_EXPORT int Java_com_nc_astraeoffline_OfflineBridge_getActions(void *env, void *clazz);
ASTRAE_EXPORT void Java_com_nc_astraeoffline_OfflineBridge_requestAction(void *env, void *clazz, int action);
/* Stage 3 managed-message integration. Caller must execute these only inside the target game's IL2CPP process. */
ASTRAE_EXPORT int AstraeOffline_DispatchManagedMessage(void *network_object, const char *message_class_name, uint32_t argc, void **constructor_args);
/* Infers nested Request/Response protocol names using IL2CPP declaring-type metadata. */
ASTRAE_EXPORT int AstraeOffline_HandleManagedRequest(void *network_object, const void *request_object);
/* Preferred nested-message entry: protocol_name must explicitly identify e.g. AccountLogin.Request. */
ASTRAE_EXPORT int AstraeOffline_HandleManagedRequestForProtocol(void *network_object, const char *protocol_name, const void *request_object);
ASTRAE_EXPORT int AstraeOffline_RespondManaged(void *network_object, const char *protocol_name, const char *response_json);
/* Android-only capability bitmask; zero means libil2cpp is unavailable. This probe does not install hooks. */
ASTRAE_EXPORT uint64_t AstraeOffline_ProbeIl2Cpp(void);
/* Read-only validation of the 39 original IL2CPP Hook target fingerprints; never patches process memory. */
ASTRAE_EXPORT char *AstraeOffline_ValidateOriginalHooks(void);
/* Stage 3: explicit IL2CPP runtime/schema bridge. No hooks are installed by these APIs. */
ASTRAE_EXPORT int AstraeOffline_Il2Cpp_Init(const char *wire_shapes_json_path);
/* Optional bounded wait matching the original worker's libil2cpp load polling. timeout=0 uses 120s; poll=0 uses 100ms. */
ASTRAE_EXPORT int AstraeOffline_Il2Cpp_InitWait(const char *wire_shapes_json_path,uint32_t timeout_ms,uint32_t poll_interval_ms);
ASTRAE_EXPORT void AstraeOffline_Il2Cpp_Shutdown(void);
ASTRAE_EXPORT char *AstraeOffline_Il2Cpp_GetStatus(void);
ASTRAE_EXPORT char *AstraeOffline_Il2Cpp_GetLastError(void);
ASTRAE_EXPORT char *AstraeOffline_Il2Cpp_ReadObject(const char *wire_type, const void *managed_object);
/* Returns a strong pinned GCHandle during construction/use; not a raw object pointer. Release with FreeHandle. */
ASTRAE_EXPORT uint32_t AstraeOffline_Il2Cpp_BuildObject(const char *wire_type, const char *json);
ASTRAE_EXPORT void *AstraeOffline_Il2Cpp_GetHandleTarget(uint32_t gc_handle);
ASTRAE_EXPORT void AstraeOffline_Il2Cpp_FreeHandle(uint32_t gc_handle);
ASTRAE_EXPORT char *AstraeOffline_Il2Cpp_ResolveType(const char *managed_type_name);
#if defined(__ANDROID__)
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved);
JNIEXPORT jstring JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeDispatch(JNIEnv *env, jclass type, jstring protocol, jstring request_json);
JNIEXPORT jint JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeInit(JNIEnv *env, jclass type, jstring save_directory, jstring table_directory);
JNIEXPORT jint JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeIl2CppInit(JNIEnv *env, jclass type, jstring schema_path);
JNIEXPORT jint JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeIl2CppInitWait(JNIEnv *env, jclass type, jstring schema_path, jint timeout_ms, jint poll_interval_ms);
JNIEXPORT jstring JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeIl2CppStatus(JNIEnv *env, jclass type);
JNIEXPORT jstring JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeReadManaged(JNIEnv *env, jclass type, jstring wire_type, jlong object_ptr);
JNIEXPORT jlong JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeBuildManaged(JNIEnv *env, jclass type, jstring wire_type, jstring json);
JNIEXPORT jlong JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeGetManagedTarget(JNIEnv *env, jclass type, jint gc_handle);
JNIEXPORT void JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeFreeManagedHandle(JNIEnv *env, jclass type, jint gc_handle);
JNIEXPORT jint JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeHandleManagedRequest(JNIEnv *env, jclass type, jlong network_object, jstring protocol_name, jlong request_object);
JNIEXPORT jint JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeRespondManaged(JNIEnv *env, jclass type, jlong network_object, jstring protocol_name, jstring response_json);
#endif
#ifdef __cplusplus
}
#endif
