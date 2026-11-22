#include "astrae_backend.h"
#include "backend.hpp"
#include "json.hpp"
#include "il2cpp_runtime.hpp"
#include "managed_wire.hpp"
#include "hook_runtime.hpp"
#include <mutex>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <string>
#include <ctime>
#include <chrono>
#include <limits>
#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

static char *copy_out(const std::string &s){char *p=static_cast<char *>(std::malloc(s.size()+1));if(!p)return nullptr;std::memcpy(p,s.data(),s.size());p[s.size()]='\0';return p;}
static std::string safe(const char *s){return s?std::string(s):std::string();}
static std::mutex g_il2cpp_error_mutex;
static std::string g_il2cpp_last_error;
static void set_il2cpp_error(const std::string &s){std::lock_guard<std::mutex> lock(g_il2cpp_error_mutex);g_il2cpp_last_error=s;}
static std::string get_il2cpp_error(){std::lock_guard<std::mutex> lock(g_il2cpp_error_mutex);return g_il2cpp_last_error;}
static void *find_nested_declaring_type(const astrae::il2cpp::Api &api,astrae::il2cpp::Runtime &runtime,void *nested){
    if(!nested)return nullptr;if(api.class_get_declaring_type){void *owner=api.class_get_declaring_type(nested);if(owner)return owner;}
    if(!api.domain_get_assemblies||!api.assembly_get_image||!api.image_get_class_count||!api.image_get_class||!api.class_get_nested_types)return nullptr;
    size_t assembly_count=0;void **assemblies=api.domain_get_assemblies(runtime.domain(),&assembly_count);if(!assemblies)return nullptr;
    for(size_t i=0;i<assembly_count;++i){const void *image=api.assembly_get_image(assemblies[i]);if(!image)continue;size_t class_count=api.image_get_class_count(image);if(class_count>500000)class_count=500000;for(size_t j=0;j<class_count;++j){void *outer=api.image_get_class(image,j);if(!outer)continue;void *iter=nullptr,*child=nullptr;while((child=api.class_get_nested_types(outer,&iter)))if(child==nested)return outer;}}
    return nullptr;
}
extern "C" {
int Java_com_nc_astraeoffline_OfflineBridge_getActions(void *,void *){return 0;}
void Java_com_nc_astraeoffline_OfflineBridge_requestAction(void *,void *,int){}
int AstraeOffline_Init(const char *save_directory,const char *table_directory){try{return astrae::Backend::instance().init(safe(save_directory),safe(table_directory));}catch(...){return 0;}}
void AstraeOffline_Shutdown(void){astrae::Backend::instance().shutdown();}
char *AstraeOffline_Handle(const char *protocol,const char *request_json){if(!protocol)return copy_out("{}");try{return copy_out(astrae::Backend::instance().handle(protocol,request_json?request_json:"{}"));}catch(...){return copy_out("{\"Error\":\"InternalError\"}");}}
char *AstraeOffline_HandleEnvelope(const char *envelope_json){try{auto v=astrae::json::parse(safe(envelope_json));if(!v.is_object())return copy_out("{\"Error\":\"InvalidEnvelope\"}");std::string protocol=v["protocol"].as_string(v["Protocol"].as_string());if(protocol.empty())return copy_out("{\"Error\":\"MissingProtocol\"}");auto req=v.contains("request")?v["request"]:v["Request"];if(!req.is_object())return copy_out("{\"Error\":\"RequestMustBeObject\"}");return copy_out(astrae::Backend::instance().handle(protocol,astrae::json::dump(req)));}catch(...){return copy_out("{\"Error\":\"InvalidEnvelope\"}");}}
char *AstraeOffline_GetState(void){try{return copy_out(astrae::Backend::instance().state_json());}catch(...){return copy_out("{}");}}
int AstraeOffline_ReplaceState(const char *state_json){try{return state_json?astrae::Backend::instance().replace_state(state_json):0;}catch(...){return 0;}}
char *AstraeOffline_GetDiagnostics(void){try{return copy_out(astrae::Backend::instance().diagnostics_json());}catch(...){return copy_out("{\"error\":\"InternalError\"}");}}
char *AstraeOffline_GetProtocolCatalog(void){try{return copy_out(astrae::Backend::instance().protocol_catalog_json());}catch(...){return copy_out("{}");}}
char *AstraeOffline_ValidateRequest(const char *protocol,const char *request_json){try{return copy_out(astrae::Backend::instance().validate_request_json(safe(protocol),safe(request_json)));}catch(...){return copy_out("{\"valid\":false,\"error\":\"InternalError\"}");}}
void AstraeOffline_Free(void *p){std::free(p);}
const char *AstraeOffline_Version(void){return "AstraeOfflineBackend-Reconstruction/1.0.0-stage5";}
int AstraeOffline_DispatchManagedMessage(void *network_object,const char *message_class_name,uint32_t argc,void **constructor_args){
    using astrae::il2cpp::Runtime;
    try{
        if(!network_object||!message_class_name||!*message_class_name||argc>32||(argc&&!constructor_args)){set_il2cpp_error("invalid managed message dispatch arguments");return 0;}
        Runtime &rt=Runtime::instance();Runtime::ThreadScope scope(rt);if(!scope.ok()){set_il2cpp_error(scope.error());return 0;}
        std::string class_name=message_class_name; if(class_name.find('.')==std::string::npos)class_name="_AT.Messages."+class_name;
        std::string error;void *message_class=rt.find_class(class_name,&error);if(!message_class){set_il2cpp_error(error);return 0;}
        const auto &api=rt.api();if(!api.object_new){set_il2cpp_error("il2cpp_object_new is unavailable");return 0;}
        void *message=api.object_new(message_class);if(!message){set_il2cpp_error("il2cpp_object_new failed for managed message");return 0;}
        uint32_t root=rt.retain(message,true);if(!root){set_il2cpp_error("unable to pin managed message during Dispatch");return 0;}
        void *ctor=rt.find_method(message_class,".ctor",static_cast<int>(argc));
        if(!ctor){rt.release(root);set_il2cpp_error("managed message constructor was not found with the supplied arity");return 0;}
        if(!rt.invoke(ctor,message,constructor_args,nullptr,&error)){rt.release(root);set_il2cpp_error("managed message constructor failed: "+error);return 0;}
        void *network_class=rt.find_class("_AT.Network.NetworkHandler",&error);if(!network_class){rt.release(root);set_il2cpp_error(error);return 0;}
        void *dispatch=rt.find_method(network_class,"Dispatch",1);if(!dispatch){rt.release(root);set_il2cpp_error("_AT.Network.NetworkHandler.Dispatch(1) was not found");return 0;}
        void *args[]={message};bool ok=rt.invoke(dispatch,network_object,args,nullptr,&error);rt.release(root);if(!ok){set_il2cpp_error("NetworkHandler.Dispatch failed: "+error);return 0;}set_il2cpp_error("");return 1;
    }catch(const std::exception &e){set_il2cpp_error(e.what());return 0;}catch(...){set_il2cpp_error("unknown managed message dispatch failure");return 0;}
}
int AstraeOffline_RespondManaged(void *network_object,const char *protocol_name,const char *response_json){
    using astrae::il2cpp::Runtime;
    try{
        if(!network_object||!protocol_name||!*protocol_name||!response_json){set_il2cpp_error("invalid managed response arguments");return 0;}
        Runtime &rt=Runtime::instance();Runtime::ThreadScope scope(rt);if(!scope.ok()){set_il2cpp_error(scope.error());return 0;}
        std::string base=protocol_name;const std::string request_suffix=".Request",response_suffix=".Response";if(base.size()>=request_suffix.size()&&base.compare(base.size()-request_suffix.size(),request_suffix.size(),request_suffix)==0)base.resize(base.size()-request_suffix.size());else if(base.size()>=response_suffix.size()&&base.compare(base.size()-response_suffix.size(),response_suffix.size(),response_suffix)==0)base.resize(base.size()-response_suffix.size());
        const bool error_packet=base=="ErrorPacket";const std::string response_type=error_packet?"ErrorPacket":base+".Response";const std::string callback_name=error_packet?"OnResponseError":"OnResponse"+base;
        astrae::json::Value body=astrae::json::parse(response_json);if(!body.is_object()){set_il2cpp_error("managed response JSON must be an object");return 0;}
        const uint64_t unix_seconds=static_cast<uint64_t>(std::time(nullptr));body["ServerTime"]=unix_seconds*UINT64_C(10000000)+UINT64_C(621355968000000000);
        std::string error;uint32_t token=astrae::managed::ManagedWire::instance().build_object_handle(response_type,astrae::json::dump(body),&error);if(!token){set_il2cpp_error("cannot construct managed response "+response_type+": "+error);return 0;}
        void *response=rt.public_handle_target(token);if(!response){rt.release_public_handle(token);set_il2cpp_error("managed response GCHandle target is unavailable");return 0;}
        void *network_class=rt.find_class("_AT.Network.NetworkHandler",&error);if(!network_class){rt.release_public_handle(token);set_il2cpp_error(error);return 0;}
        void *callback=rt.find_method(network_class,callback_name,1);if(!callback){rt.release_public_handle(token);set_il2cpp_error("NetworkHandler callback was not found: "+callback_name);return 0;}
        void *args[]={response};bool ok=rt.invoke(callback,network_object,args,nullptr,&error);bool released=rt.release_public_handle(token);if(!released){set_il2cpp_error("response callback completed but managed response GCHandle release failed");return 0;}if(!ok){set_il2cpp_error("managed response callback failed: "+error);return 0;}set_il2cpp_error("");return 1;
    }catch(const std::exception &e){set_il2cpp_error(e.what());return 0;}catch(...){set_il2cpp_error("unknown managed response failure");return 0;}
}
int AstraeOffline_HandleManagedRequestForProtocol(void *network_object,const char *protocol_name,const void *request_object){
    using astrae::il2cpp::Runtime;
    try{
        if(!network_object||!request_object||!protocol_name||!*protocol_name){set_il2cpp_error("managed request requires network, protocol name and request object");return 0;}
        std::string protocol=protocol_name;const std::string request_suffix=".Request",response_suffix=".Response";
        if(protocol.size()>=response_suffix.size()&&protocol.compare(protocol.size()-response_suffix.size(),response_suffix.size(),response_suffix)==0){protocol.resize(protocol.size()-response_suffix.size());protocol+=request_suffix;}
        else if(protocol.size()<request_suffix.size()||protocol.compare(protocol.size()-request_suffix.size(),request_suffix.size(),request_suffix)!=0)protocol+=request_suffix;
        const std::string base=protocol.substr(0,protocol.size()-request_suffix.size());
        Runtime &rt=Runtime::instance();Runtime::ThreadScope scope(rt);if(!scope.ok()){set_il2cpp_error(scope.error());return 0;}
        std::string error;std::string request=astrae::managed::ManagedWire::instance().read_object_json(protocol,request_object,&error);if(!error.empty()){set_il2cpp_error("cannot read managed request "+protocol+": "+error);return 0;}
        std::string response=astrae::Backend::instance().handle(protocol,request);if(response.empty()){set_il2cpp_error("backend returned an empty response for "+protocol);return 0;}
        if(!AstraeOffline_RespondManaged(network_object,base.c_str(),response.c_str()))return 0;set_il2cpp_error("");return 1;
    }catch(const std::exception &e){set_il2cpp_error(e.what());return 0;}catch(...){set_il2cpp_error("unknown managed request handling failure");return 0;}
}
int AstraeOffline_HandleManagedRequest(void *network_object,const void *request_object){
    using astrae::il2cpp::Runtime;
    try{
        if(!network_object||!request_object){set_il2cpp_error("managed request network/object is null");return 0;}
        Runtime &rt=Runtime::instance();Runtime::ThreadScope scope(rt);if(!scope.ok()){set_il2cpp_error(scope.error());return 0;}
        const auto &api=rt.api();if(!api.object_get_class||!api.class_get_name){set_il2cpp_error("IL2CPP object class metadata APIs are unavailable");return 0;}
        void *klass=api.object_get_class(const_cast<void*>(request_object));if(!klass){set_il2cpp_error("request object has no IL2CPP class");return 0;}
        const char *raw_name=api.class_get_name(klass);if(!raw_name||!*raw_name){set_il2cpp_error("request class name is unavailable");return 0;}
        std::string name=raw_name;const std::string request_suffix=".Request",response_suffix=".Response";
        if(name=="Request"||name=="Response"){
            void *outer=find_nested_declaring_type(api,rt,klass);const char *outer_name=outer&&api.class_get_name?api.class_get_name(outer):nullptr;
            if(!outer_name||!*outer_name){set_il2cpp_error("nested request protocol could not be inferred: il2cpp_class_get_declaring_type and nested-type enumeration returned no owner");return 0;}
            name=std::string(outer_name)+request_suffix;
        }
        if(name.size()>=response_suffix.size()&&name.compare(name.size()-response_suffix.size(),response_suffix.size(),response_suffix)==0)name.resize(name.size()-response_suffix.size());
        if(name.size()<request_suffix.size()||name.compare(name.size()-request_suffix.size(),request_suffix.size(),request_suffix)!=0)name+=request_suffix;
        return AstraeOffline_HandleManagedRequestForProtocol(network_object,name.c_str(),request_object);
    }catch(const std::exception &e){set_il2cpp_error(e.what());return 0;}catch(...){set_il2cpp_error("unknown managed request handling failure");return 0;}
}
int AstraeOffline_Il2Cpp_Init(const char *wire_shapes_json_path){try{std::string error;std::string path=safe(wire_shapes_json_path);if(path.empty())path="assets/schema/wire_shapes.json";if(!astrae::managed::ManagedWire::instance().load_schema(path,&error)){set_il2cpp_error(error);return 0;}if(!astrae::il2cpp::Runtime::instance().initialize(&error)){set_il2cpp_error(error);return 0;}set_il2cpp_error("");return 1;}catch(const std::exception &e){set_il2cpp_error(e.what());return 0;}catch(...){set_il2cpp_error("unknown IL2CPP initialization failure");return 0;}}
int AstraeOffline_Il2Cpp_InitWait(const char *wire_shapes_json_path,uint32_t timeout_ms,uint32_t poll_interval_ms){try{std::string error;std::string path=safe(wire_shapes_json_path);if(path.empty())path="assets/schema/wire_shapes.json";if(!astrae::managed::ManagedWire::instance().load_schema(path,&error)){set_il2cpp_error(error);return 0;}if(!astrae::il2cpp::Runtime::instance().initialize_wait(timeout_ms,poll_interval_ms,&error)){set_il2cpp_error(error);return 0;}set_il2cpp_error("");return 1;}catch(const std::exception &e){set_il2cpp_error(e.what());return 0;}catch(...){set_il2cpp_error("unknown IL2CPP initialization wait failure");return 0;}}
void AstraeOffline_Il2Cpp_Shutdown(void){astrae::il2cpp::Runtime::instance().shutdown();set_il2cpp_error("");}
char *AstraeOffline_Il2Cpp_GetStatus(void){try{auto s=astrae::il2cpp::Runtime::instance().status();astrae::json::Value v=astrae::json::Value::object();v["loaded"]=s.loaded;v["initialized"]=s.initialized;v["coreNetworkClassReady"]=s.core_network_class_ready;v["attached"]=s.attached;v["canReadManagedObjects"]=s.can_read;v["canBuildManagedObjects"]=s.can_build;v["canResolveGenericArguments"]=s.can_generic_reflection;v["canEnumerateCollections"]=s.can_enumerate;v["library"]=s.library_path;v["assemblyCount"]=static_cast<uint64_t>(s.assembly_count);v["resolvedSymbolCount"]=static_cast<uint64_t>(s.resolved_symbols);v["missingRequiredSymbolCount"]=static_cast<uint64_t>(s.missing_required);v["runtimeError"]=s.error;v["lastError"]=get_il2cpp_error();v["wireSchema"]=astrae::json::parse(astrae::managed::ManagedWire::instance().schema_report());return copy_out(astrae::json::dump(v,true));}catch(...){return copy_out("{\"error\":\"InternalError\"}");}}
char *AstraeOffline_Il2Cpp_GetLastError(void){return copy_out(get_il2cpp_error());}
char *AstraeOffline_Il2Cpp_ReadObject(const char *wire_type,const void *managed_object){try{std::string error;std::string result=astrae::managed::ManagedWire::instance().read_object_json(safe(wire_type),managed_object,&error);if(!error.empty()){set_il2cpp_error(error);return copy_out("{\"error\":\"ManagedWireReadFailed\"}");}set_il2cpp_error("");return copy_out(result);}catch(const std::exception &e){set_il2cpp_error(e.what());return copy_out("{\"error\":\"ManagedWireReadException\"}");}catch(...){set_il2cpp_error("unknown managed wire read exception");return copy_out("{\"error\":\"ManagedWireReadException\"}");}}
uint32_t AstraeOffline_Il2Cpp_BuildObject(const char *wire_type,const char *json){try{std::string error;uint32_t handle=astrae::managed::ManagedWire::instance().build_object_handle(safe(wire_type),safe(json),&error);set_il2cpp_error(error);return handle;}catch(const std::exception &e){set_il2cpp_error(e.what());return 0;}catch(...){set_il2cpp_error("unknown managed wire build exception");return 0;}}
void *AstraeOffline_Il2Cpp_GetHandleTarget(uint32_t gc_handle){try{void *target=astrae::il2cpp::Runtime::instance().public_handle_target(gc_handle);if(!target)set_il2cpp_error("invalid, stale, or unavailable public GCHandle token");else set_il2cpp_error("");return target;}catch(...){set_il2cpp_error("failed to obtain public GCHandle target");return nullptr;}}
void AstraeOffline_Il2Cpp_FreeHandle(uint32_t gc_handle){try{if(!astrae::il2cpp::Runtime::instance().release_public_handle(gc_handle))set_il2cpp_error("invalid, stale, or already released public GCHandle token");else set_il2cpp_error("");}catch(...){set_il2cpp_error("failed to release public GCHandle token");}}
char *AstraeOffline_Il2Cpp_ResolveType(const char *managed_type_name){try{std::string error;astrae::il2cpp::Runtime::ThreadScope scope(astrae::il2cpp::Runtime::instance());if(!scope.ok())throw std::runtime_error(scope.error());void *klass=astrae::il2cpp::Runtime::instance().find_class(safe(managed_type_name),&error);if(!klass)throw std::runtime_error(error);const auto &a=astrae::il2cpp::Runtime::instance().api();astrae::json::Value v=astrae::json::Value::object();const char *name=a.class_get_name?a.class_get_name(klass):nullptr,*ns=a.class_get_namespace?a.class_get_namespace(klass):nullptr;v["name"]=name?name:"";v["namespace"]=ns?ns:"";v["fullName"]=(ns&&*ns)?std::string(ns)+"."+(name?name:""):(name?name:"");v["resolved"]=true;set_il2cpp_error("");return copy_out(astrae::json::dump(v,false));}catch(const std::exception &e){set_il2cpp_error(e.what());return copy_out("{\"resolved\":false}");}}
char *AstraeOffline_ValidateOriginalHooks(void){try{return copy_out(astrae::hooks::validate_original_hooks_json());}catch(...){return copy_out("{\"error\":\"HookValidationFailed\",\"installAttempted\":false}");}}
uint64_t AstraeOffline_ProbeIl2Cpp(void){
#if defined(__ANDROID__)
    void *h=dlopen("libil2cpp.so",RTLD_NOW|RTLD_NOLOAD);if(!h)return 0;const char *names[]={"il2cpp_domain_get","il2cpp_thread_attach","il2cpp_domain_get_assemblies","il2cpp_assembly_get_image","il2cpp_class_from_name","il2cpp_class_get_method_from_name","il2cpp_runtime_invoke","il2cpp_object_new","il2cpp_string_new","il2cpp_runtime_class_init","il2cpp_class_get_fields","il2cpp_field_get_value","il2cpp_field_set_value"};uint64_t mask=0;for(size_t i=0;i<sizeof(names)/sizeof(names[0]);++i)if(dlsym(h,names[i]))mask|=uint64_t(1)<<i;dlclose(h);return mask;
#else
    return 0;
#endif
}
}
#if defined(__ANDROID__)
#include <jni.h>
static std::string jstring_utf8(JNIEnv *env,jstring value){if(!value)return {};const char *p=env->GetStringUTFChars(value,nullptr);if(!p)return {};std::string s(p);env->ReleaseStringUTFChars(value,p);return s;}
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *,void *){return JNI_VERSION_1_6;}
JNIEXPORT jint JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeInit(JNIEnv *env,jclass,jstring save_directory,jstring table_directory){std::string s=jstring_utf8(env,save_directory),t=jstring_utf8(env,table_directory);return AstraeOffline_Init(s.c_str(),t.empty()?nullptr:t.c_str());}
JNIEXPORT jstring JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeDispatch(JNIEnv *env,jclass,jstring protocol,jstring request_json){std::string p=jstring_utf8(env,protocol),r=jstring_utf8(env,request_json);char *out=AstraeOffline_Handle(p.c_str(),r.c_str());jstring result=env->NewStringUTF(out?out:"{}");AstraeOffline_Free(out);return result;}
JNIEXPORT jint JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeIl2CppInit(JNIEnv *env,jclass,jstring schema_path){std::string p=jstring_utf8(env,schema_path);return AstraeOffline_Il2Cpp_Init(p.empty()?nullptr:p.c_str());}
JNIEXPORT jint JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeIl2CppInitWait(JNIEnv *env,jclass,jstring schema_path,jint timeout_ms,jint poll_interval_ms){std::string p=jstring_utf8(env,schema_path);return AstraeOffline_Il2Cpp_InitWait(p.empty()?nullptr:p.c_str(),timeout_ms<0?0:static_cast<uint32_t>(timeout_ms),poll_interval_ms<0?0:static_cast<uint32_t>(poll_interval_ms));}
JNIEXPORT jstring JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeIl2CppStatus(JNIEnv *env,jclass){char *out=AstraeOffline_Il2Cpp_GetStatus();jstring result=env->NewStringUTF(out?out:"{}");AstraeOffline_Free(out);return result;}
JNIEXPORT jstring JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeReadManaged(JNIEnv *env,jclass,jstring wire_type,jlong object_ptr){std::string t=jstring_utf8(env,wire_type);char *out=AstraeOffline_Il2Cpp_ReadObject(t.c_str(),reinterpret_cast<const void*>(static_cast<uintptr_t>(object_ptr)));jstring result=env->NewStringUTF(out?out:"{}");AstraeOffline_Free(out);return result;}
JNIEXPORT jlong JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeBuildManaged(JNIEnv *env,jclass,jstring wire_type,jstring json){std::string t=jstring_utf8(env,wire_type),j=jstring_utf8(env,json);return static_cast<jlong>(AstraeOffline_Il2Cpp_BuildObject(t.c_str(),j.c_str()));}
JNIEXPORT jlong JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeGetManagedTarget(JNIEnv *,jclass,jint gc_handle){return reinterpret_cast<jlong>(AstraeOffline_Il2Cpp_GetHandleTarget(static_cast<uint32_t>(gc_handle)));}
JNIEXPORT void JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeFreeManagedHandle(JNIEnv *,jclass,jint gc_handle){AstraeOffline_Il2Cpp_FreeHandle(static_cast<uint32_t>(gc_handle));}
JNIEXPORT jint JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeHandleManagedRequest(JNIEnv *env,jclass,jlong network_object,jstring protocol_name,jlong request_object){std::string p=jstring_utf8(env,protocol_name);return AstraeOffline_HandleManagedRequestForProtocol(reinterpret_cast<void*>(static_cast<uintptr_t>(network_object)),p.c_str(),reinterpret_cast<const void*>(static_cast<uintptr_t>(request_object)));}
JNIEXPORT jint JNICALL Java_com_nc_astraeoffline_OfflineBridge_nativeRespondManaged(JNIEnv *env,jclass,jlong network_object,jstring protocol_name,jstring response_json){std::string p=jstring_utf8(env,protocol_name),j=jstring_utf8(env,response_json);return AstraeOffline_RespondManaged(reinterpret_cast<void*>(static_cast<uintptr_t>(network_object)),p.c_str(),j.c_str());}
#endif
